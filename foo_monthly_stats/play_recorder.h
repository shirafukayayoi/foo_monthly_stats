#pragma once
#include "stdafx.h"

namespace fms
{

    // play_recorder.h
    // Hooks play_callback_static to record every new track play.
    // Tracks actual playback time (stops when user skips/pauses).

    // Forward declaration – implementation in play_recorder.cpp
    class PlayRecorder : public play_callback_static
    {
    public:
        PlayRecorder() : m_last_playback_time(0.0) {}

        unsigned get_flags() override
        {
            return flag_on_playback_new_track | flag_on_playback_time | flag_on_playback_stop;
        }
        void on_playback_starting(play_control::t_track_command, bool) override {}
        void on_playback_new_track(metadb_handle_ptr track) override;
        void on_playback_stop(play_control::t_stop_reason reason) override;
        void on_playback_seek(double) override {}
        void on_playback_pause(bool) override {}
        void on_playback_edited(metadb_handle_ptr) override {}
        void on_playback_dynamic_info(const file_info &) override {}
        void on_playback_dynamic_info_track(const file_info &) override {}
        void on_playback_time(double p_time) override;
        void on_volume_change(float) override {}

    private:
        metadb_handle_ptr m_current_track; // Currently playing track
        double m_last_playback_time;       // Last reported playback position (seconds)
        std::string m_current_track_crc;   // CRC of current track (for verification)
    };

} // namespace fms
