#pragma once
#include "stdafx.h"

namespace fms
{

    // play_recorder.h
    // Uses playback_statistics_collector to record track plays.
    // foobar2000 automatically calls on_item_played() when:
    //   - 60 seconds have been played, OR
    //   - Track ends and at least 1/3 was played

    class PlaybackStatsCollector : public playback_statistics_collector
    {
    public:
        void on_item_played(metadb_handle_ptr p_item) override;
    };

} // namespace fms
