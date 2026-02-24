#pragma once
#include "stdafx.h"

namespace fms
{

    // play_recorder.h
    // Uses playback_statistics_collector to record track plays.
    // foobar2000 automatically calls on_item_played() when:
    //   - 60 seconds have been played, OR
    //   - Track ends and at least 1/3 was played
    //
    // Also uses play_callback_static to track actual playback time.

    // Time tracker for getting actual played time
    class PlaybackTimeTracker : public play_callback_static
    {
    public:
        unsigned get_flags() override
        {
            return flag_on_playback_new_track | flag_on_playback_time | flag_on_playback_stop;
        }

        void on_playback_new_track(metadb_handle_ptr track) override;
        void on_playback_time(double p_time) override;
        void on_playback_stop(play_control::t_stop_reason reason) override;

        void on_playback_starting(play_control::t_track_command, bool) override {}
        void on_playback_seek(double) override {}
        void on_playback_pause(bool) override {}
        void on_playback_edited(metadb_handle_ptr) override {}
        void on_playback_dynamic_info(const file_info &) override {}
        void on_playback_dynamic_info_track(const file_info &) override {}
        void on_volume_change(float) override {}

        // Get actual played time for a specific track
        static double get_played_time(metadb_handle_ptr track);

    private:
        static metadb_handle_ptr s_current_track;
        static double s_last_playback_time;
    };

    class PlaybackStatsCollector : public playback_statistics_collector
    {
    public:
        void on_item_played(metadb_handle_ptr p_item) override;
    };

} // namespace fms
