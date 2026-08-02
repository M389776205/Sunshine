/**
 * @file src/virtual_display.cpp
 * @brief Session-scoped virtual-display lifecycle management.
 */

// standard includes
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

// local includes
#include "config.h"
#include "display_device.h"
#include "logging.h"
#include "rtsp.h"
#include "virtual_display.h"

#ifdef _WIN32
  #include <parsec-vdd/core/parsec-vdd.h>
#endif

using namespace std::chrono_literals;

namespace virtual_display {
  namespace {
    using backend_e = config::video_t::virtual_display_t::backend_e;
    using mode_e = config::video_t::virtual_display_t::mode_e;

    [[nodiscard]] const config::video_t::virtual_display_t::profile_t &selected_profile(const config::video_t &video_config) {
      static const config::video_t::virtual_display_t::profile_t fallback {"Follow client", 0, 0, 0};
      const auto &settings {video_config.virtual_display};
      if (settings.profiles.empty()) {
        return fallback;
      }

      const auto index {std::clamp(settings.default_profile, 0, static_cast<int>(settings.profiles.size() - 1))};
      return settings.profiles[static_cast<std::size_t>(index)];
    }

#ifdef _WIN32
    [[nodiscard]] std::int64_t steady_milliseconds() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
      ).count();
    }

    [[nodiscard]] bool is_valid_handle(const HANDLE handle) {
      return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] std::optional<display_device::EnumeratedDevice> find_new_device(
      const display_device::EnumeratedDeviceList &devices,
      const std::unordered_set<std::string> &existing_ids
    ) {
      std::optional<display_device::EnumeratedDevice> first_new_device;
      for (const auto &device : devices) {
        if (existing_ids.contains(device.m_device_id)) {
          continue;
        }
        if (!first_new_device) {
          first_new_device = device;
        }

        auto identity {device.m_device_id + " " + device.m_display_name + " " + device.m_friendly_name};
        std::ranges::transform(identity, identity.begin(), [](const unsigned char value) {
          return static_cast<char>(std::tolower(value));
        });
        if (identity.contains("parsec") || identity.contains("psccdd")) {
          return device;
        }
      }
      return first_new_device;
    }

    class manager_t {
    public:
      ~manager_t() {
        release();
      }

      [[nodiscard]] bool prepare(config::video_t &video_config, std::string &device_id) {
        std::jthread completed_worker;
        {
          std::unique_lock lock {mutex_};
          if (active_) {
            release_at_ms_.store(0, std::memory_order_release);
            video_config.output_name = device_id_;
            video_config.dd.config_revert_on_disconnect = true;
            device_id = device_id_;
            return true;
          }

          if (keepalive_worker_.joinable()) {
            completed_worker = std::move(keepalive_worker_);
            lock.unlock();
            completed_worker.join();
            lock.lock();
          }
        }

        const auto status {parsec_vdd::QueryDeviceStatus(&parsec_vdd::VDD_CLASS_GUID, parsec_vdd::VDD_HARDWARE_ID)};
        if (status != parsec_vdd::DEVICE_OK) {
          BOOST_LOG(error) << "Parsec Virtual Display Adapter is unavailable (status "sv << static_cast<int>(status) << ")."sv;
          return false;
        }

        const auto before_devices {display_device::enumerate_devices()};
        std::unordered_set<std::string> before_ids;
        for (const auto &device : before_devices) {
          before_ids.insert(device.m_device_id);
        }

        const auto handle {parsec_vdd::OpenDeviceHandle(&parsec_vdd::VDD_ADAPTER_GUID)};
        if (!is_valid_handle(handle)) {
          BOOST_LOG(error) << "Failed to open the Parsec Virtual Display Adapter."sv;
          return false;
        }

        const auto display_index {parsec_vdd::VddAddDisplay(handle)};
        if (display_index < 0 || display_index >= parsec_vdd::VDD_MAX_DISPLAYS) {
          BOOST_LOG(error) << "Parsec VDD rejected the virtual display request (index "sv << display_index << ")."sv;
          parsec_vdd::CloseDeviceHandle(handle);
          return false;
        }

        const auto deadline {std::chrono::steady_clock::now() + video_config.virtual_display.startup_timeout};
        std::optional<display_device::EnumeratedDevice> new_device;
        while (std::chrono::steady_clock::now() < deadline) {
          parsec_vdd::VddUpdate(handle);
          new_device = find_new_device(display_device::enumerate_devices(), before_ids);
          if (new_device) {
            break;
          }
          std::this_thread::sleep_for(100ms);
        }

        if (!new_device) {
          BOOST_LOG(error) << "Windows did not enumerate the new Parsec virtual display before the startup timeout."sv;
          parsec_vdd::VddRemoveDisplay(handle, display_index);
          parsec_vdd::CloseDeviceHandle(handle);
          return false;
        }

        {
          std::lock_guard lock {mutex_};
          original_output_name_ = video_config.output_name;
          original_revert_on_disconnect_ = video_config.dd.config_revert_on_disconnect;
          device_id_ = new_device->m_device_id;
          active_ = true;
          release_at_ms_.store(0, std::memory_order_release);
          video_config.output_name = device_id_;
          video_config.dd.config_revert_on_disconnect = true;
          device_id = device_id_;

          keepalive_worker_ = std::jthread([this, handle, display_index](std::stop_token stop_token) {
            keepalive_loop(stop_token, handle, display_index);
          });
        }

        BOOST_LOG(info) << "Virtual display ready: "sv << new_device->m_friendly_name
                        << " ("sv << new_device->m_device_id << ", Parsec index "sv << display_index << ")."sv;
        return true;
      }

      void schedule_release(const std::chrono::milliseconds delay) {
        std::lock_guard lock {mutex_};
        if (!active_) {
          return;
        }
        release_at_ms_.store(steady_milliseconds() + std::max(delay, 0ms).count(), std::memory_order_release);
      }

      void release() {
        std::jthread worker;
        {
          std::lock_guard lock {mutex_};
          if (!keepalive_worker_.joinable()) {
            return;
          }
          release_at_ms_.store(steady_milliseconds(), std::memory_order_release);
          worker = std::move(keepalive_worker_);
        }
        worker.join();
      }

    private:
      void keepalive_loop(const std::stop_token stop_token, const HANDLE handle, const int display_index) {
        while (!stop_token.stop_requested()) {
          parsec_vdd::VddUpdate(handle);

          const auto release_at {release_at_ms_.load(std::memory_order_acquire)};
          if (release_at > 0 && steady_milliseconds() >= release_at) {
            std::lock_guard lock {mutex_};
            const auto confirmed_release_at {release_at_ms_.load(std::memory_order_acquire)};
            if (confirmed_release_at > 0 && steady_milliseconds() >= confirmed_release_at) {
              active_ = false;
              break;
            }
          }
          std::this_thread::sleep_for(80ms);
        }

        parsec_vdd::VddRemoveDisplay(handle, display_index);
        parsec_vdd::CloseDeviceHandle(handle);

        std::lock_guard lock {mutex_};
        config::video.output_name = original_output_name_;
        config::video.dd.config_revert_on_disconnect = original_revert_on_disconnect_;
        device_id_.clear();
        release_at_ms_.store(0, std::memory_order_release);
        active_ = false;
        BOOST_LOG(info) << "Virtual display released and Sunshine capture settings restored."sv;
      }

      std::mutex mutex_;
      std::jthread keepalive_worker_;
      std::atomic<std::int64_t> release_at_ms_ {0};
      std::string device_id_;
      std::string original_output_name_;
      bool original_revert_on_disconnect_ {false};
      bool active_ {false};
    };

    [[nodiscard]] manager_t &manager() {
      static manager_t instance;
      return instance;
    }
#endif
  }  // namespace

  config::video_t build_display_configuration(
    const config::video_t &video_config,
    const rtsp_stream::launch_session_t &session,
    const std::string &device_id
  ) {
    auto effective_config {video_config};
    const auto &profile {selected_profile(video_config)};
    const auto width {profile.width > 0 ? profile.width : session.width};
    const auto height {profile.height > 0 ? profile.height : session.height};
    const auto refresh_rate {profile.refresh_rate > 0 ? profile.refresh_rate : session.fps};

    effective_config.output_name = device_id;
    effective_config.dd.resolution_option = config::video_t::dd_t::resolution_option_e::manual;
    effective_config.dd.manual_resolution = std::format("{}x{}", width, height);
    effective_config.dd.refresh_rate_option = config::video_t::dd_t::refresh_rate_option_e::manual;
    effective_config.dd.manual_refresh_rate = std::to_string(refresh_rate);
    effective_config.dd.config_revert_on_disconnect = true;

    switch (video_config.virtual_display.mode) {
      case mode_e::extend:
        effective_config.dd.configuration_option = config::video_t::dd_t::config_option_e::ensure_active;
        break;
      case mode_e::extend_primary:
        effective_config.dd.configuration_option = config::video_t::dd_t::config_option_e::ensure_primary;
        break;
      case mode_e::virtual_only:
        effective_config.dd.configuration_option = config::video_t::dd_t::config_option_e::ensure_only_display;
        break;
    }
    return effective_config;
  }

  bool prepare(
    config::video_t &video_config,
    const rtsp_stream::launch_session_t &session,
    config::video_t &effective_config
  ) {
    if (!video_config.virtual_display.enabled) {
      effective_config = video_config;
      return true;
    }

#ifdef _WIN32
    if (video_config.virtual_display.backend != backend_e::parsec_vdd) {
      BOOST_LOG(error) << "The configured virtual display backend is not supported by this build."sv;
      return false;
    }

    std::string device_id;
    if (!manager().prepare(video_config, device_id)) {
      return false;
    }
    effective_config = build_display_configuration(video_config, session, device_id);
    return true;
#else
    BOOST_LOG(error) << "Virtual display integration is currently available only on Windows."sv;
    return false;
#endif
  }

  void schedule_release(const std::chrono::milliseconds delay) {
#ifdef _WIN32
    manager().schedule_release(delay);
#else
    (void) delay;
#endif
  }

  void release() {
#ifdef _WIN32
    manager().release();
#endif
  }
}  // namespace virtual_display
