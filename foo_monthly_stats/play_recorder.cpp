#include "stdafx.h"
#include "play_recorder.h"
#include "db_manager.h"
#include "preferences.h"

namespace fms
{

    // CRC64 – must match db_manager.cpp's implementation
    static uint64_t crc64(const char *data, size_t len)
    {
        static const uint64_t POLY = 0xad93d23594c935a9ULL;
        uint64_t crc = 0;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= static_cast<uint8_t>(data[i]);
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ POLY;
                else
                    crc >>= 1;
            }
        }
        return crc;
    }

    // PlaybackTimeTracker implementation
    metadb_handle_ptr PlaybackTimeTracker::s_current_track;
    double PlaybackTimeTracker::s_last_playback_time = 0.0;

    void PlaybackTimeTracker::on_playback_new_track(metadb_handle_ptr track)
    {
        s_current_track = track;
        s_last_playback_time = 0.0;
    }

    void PlaybackTimeTracker::on_playback_time(double p_time)
    {
        s_last_playback_time = p_time;
    }

    void PlaybackTimeTracker::on_playback_stop(play_control::t_stop_reason reason)
    {
        if (!s_current_track.is_valid())
            return;

        // Record played time if >= 1 second
        if (s_last_playback_time >= 1.0)
        {
            // Get track info and record
            file_info_impl fi;
            if (!s_current_track->get_info(fi))
                return;

            const char *path = s_current_track->get_path();
            uint64_t crcVal = crc64(path, strlen(path));
            char crcHex[17];
            snprintf(crcHex, sizeof(crcHex), "%016llx", (unsigned long long)crcVal);

            TrackInfo ti;
            ti.track_crc = crcHex;
            ti.path = path;
            ti.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
            ti.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
            ti.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
            ti.length_seconds = s_last_playback_time;

            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER ul;
            ul.LowPart = ft.dwLowDateTime;
            ul.HighPart = ft.dwHighDateTime;
            ti.played_at = static_cast<int64_t>((ul.QuadPart / 10000ULL) - 11644473600000ULL);

            console::formatter() << "[foo_monthly_stats] on_playback_stop: recording "
                                 << ti.title.c_str() << " (" << s_last_playback_time << "s)";

            DbManager::get().postPlay(ti);
        }

        // Clear state
        s_current_track.release();
        s_last_playback_time = 0.0;
    }

    double PlaybackTimeTracker::get_played_time(metadb_handle_ptr track)
    {
        if (s_current_track.is_valid() && s_current_track == track)
            return s_last_playback_time;
        return 0.0;
    }

    void PlaybackStatsCollector::on_item_played(metadb_handle_ptr p_item)
    {
        if (!p_item.is_valid())
        {
            console::formatter() << "[foo_monthly_stats] on_item_played: invalid track";
            return;
        }

        // Read metadata
        file_info_impl fi;
        if (!p_item->get_info(fi))
        {
            console::formatter() << "[foo_monthly_stats] on_item_played: failed to get file info";
            return;
        }

        // Calculate CRC for track path
        const char *path = p_item->get_path();
        uint64_t crcVal = crc64(path, strlen(path));
        char crcHex[17];
        snprintf(crcHex, sizeof(crcHex), "%016llx", (unsigned long long)crcVal);

        // Build TrackInfo
        TrackInfo ti;
        ti.track_crc = crcHex;
        ti.path = path;
        ti.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
        ti.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
        ti.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";

        // Only record play count (not duration) in on_item_played
        // Actual playback time will be recorded by on_playback_stop
        ti.length_seconds = 0;

        // UNIX epoch milliseconds (UTC)
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        ti.played_at = static_cast<int64_t>((ul.QuadPart / 10000ULL) - 11644473600000ULL);

        console::formatter() << "[foo_monthly_stats] on_item_played: " << ti.title.c_str()
                             << " by " << ti.artist.c_str()
                             << " (play count only, duration tracked by on_playback_stop)";

        DbManager::get().postPlay(ti);
    }

    // Register static factories
    static playback_statistics_collector_factory_t<PlaybackStatsCollector> g_playback_stats_collector;
    static play_callback_static_factory_t<PlaybackTimeTracker> g_time_tracker;

} // namespace fms
