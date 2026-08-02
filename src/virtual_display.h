/**
 * @file src/virtual_display.h
 * @brief Session-scoped virtual-display lifecycle management.
 */
#pragma once

// standard includes
#include <chrono>
#include <string>

namespace config {
  struct video_t;
}

namespace rtsp_stream {
  struct launch_session_t;
}

namespace virtual_display {
  /**
   * @brief Build the effective Sunshine display configuration for a virtual-display session.
   *
   * @param video_config Saved host video configuration.
   * @param session Client launch parameters.
   * @param device_id Device identifier returned by libdisplaydevice.
   * @return A copy of the video configuration targeting the requested virtual display and profile.
   */
  [[nodiscard]] config::video_t build_display_configuration(
    const config::video_t &video_config,
    const rtsp_stream::launch_session_t &session,
    const std::string &device_id
  );

  /**
   * @brief Create and verify a virtual display before display topology and encoder setup.
   *
   * When virtual-display integration is disabled, this simply copies the existing video
   * configuration to @p effective_config.
   *
   * @param video_config Mutable global video configuration used by video capture.
   * @param session Client launch parameters.
   * @param effective_config Receives the configuration that should be passed to display_device.
   * @return True when streaming may continue, or false when an enabled provider failed.
   */
  [[nodiscard]] bool prepare(
    config::video_t &video_config,
    const rtsp_stream::launch_session_t &session,
    config::video_t &effective_config
  );

  /**
   * @brief Remove the active virtual display after Sunshine has had time to restore topology.
   *
   * A later call to prepare() cancels a pending removal so reconnects can reuse the display.
   *
   * @param delay Delay before removing the provider display.
   */
  void schedule_release(std::chrono::milliseconds delay);

  /**
   * @brief Remove the active virtual display immediately and restore capture settings.
   */
  void release();
}  // namespace virtual_display
