/**
 * @file tests/unit/test_virtual_display.cpp
 * @brief Tests for virtual-display profile and topology policy mapping.
 */

#include <gtest/gtest.h>

#include "src/config.h"
#include "src/rtsp.h"
#include "src/virtual_display.h"

namespace {
  using config_option_e = config::video_t::dd_t::config_option_e;
  using mode_e = config::video_t::virtual_display_t::mode_e;

  TEST(VirtualDisplayConfigTest, FollowClientProfileUsesLaunchDimensions) {
    config::video_t video_config {};
    video_config.virtual_display.profiles = {{"Follow client", 0, 0, 0}};
    video_config.virtual_display.default_profile = 0;
    video_config.virtual_display.mode = mode_e::virtual_only;

    rtsp_stream::launch_session_t session {};
    session.width = 2560;
    session.height = 1600;
    session.fps = 120;

    const auto result {virtual_display::build_display_configuration(video_config, session, "virtual-device")};
    EXPECT_EQ(result.output_name, "virtual-device");
    EXPECT_EQ(result.dd.manual_resolution, "2560x1600");
    EXPECT_EQ(result.dd.manual_refresh_rate, "120");
    EXPECT_EQ(result.dd.configuration_option, config_option_e::ensure_only_display);
    EXPECT_TRUE(result.dd.config_revert_on_disconnect);
  }

  TEST(VirtualDisplayConfigTest, SavedProfileOverridesClientDimensions) {
    config::video_t video_config {};
    video_config.virtual_display.profiles = {
      {"1080p", 1920, 1080, 60},
      {"2K high refresh", 2560, 1440, 165},
    };
    video_config.virtual_display.default_profile = 1;
    video_config.virtual_display.mode = mode_e::extend_primary;

    const auto result {virtual_display::build_display_configuration(video_config, {}, "virtual-device")};
    EXPECT_EQ(result.dd.manual_resolution, "2560x1440");
    EXPECT_EQ(result.dd.manual_refresh_rate, "165");
    EXPECT_EQ(result.dd.configuration_option, config_option_e::ensure_primary);
  }

  TEST(VirtualDisplayConfigTest, OutOfRangeProfileIndexUsesLastProfile) {
    config::video_t video_config {};
    video_config.virtual_display.profiles = {
      {"First", 1920, 1080, 60},
      {"Last", 3840, 2160, 120},
    };
    video_config.virtual_display.default_profile = 63;
    video_config.virtual_display.mode = mode_e::extend;

    const auto result {virtual_display::build_display_configuration(video_config, {}, "virtual-device")};
    EXPECT_EQ(result.dd.manual_resolution, "3840x2160");
    EXPECT_EQ(result.dd.manual_refresh_rate, "120");
    EXPECT_EQ(result.dd.configuration_option, config_option_e::ensure_active);
  }
}  // namespace
