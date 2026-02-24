#pragma once
#include "stdafx.h"

namespace fms
{

    // play_recorder.h
    // Hooks play_callback_static to record every new track play.

    // Forward declaration – implementation in play_recorder.cpp
    class PlayRecorder : public play_callback_static
    {
    public:
        unsigned get_flags() override
        {
            return flag_on_playback_new_track;
        }
        void on_playback_starting(play_control::t_track_command, bool) override {}
        void on_playback_new_track(metadb_handle_ptr track) override;
        void on_playback_stop(play_control::t_stop_reason) override {}
        void on_playback_seek(double) override {}
        void on_playback_pause(bool) override {}
        void on_playback_edited(metadb_handle_ptr) override {}
        void on_playback_dynamic_info(const file_info &) override {}
        void on_playback_dynamic_info_track(const file_info &) override {}
        void on_playback_time(double) override {}
        void on_volume_change(float) override {}
    };

} // namespace fms
