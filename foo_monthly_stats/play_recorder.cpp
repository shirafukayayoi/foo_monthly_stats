#include "stdafx.h"
#include "play_recorder.h"
#include "db_manager.h"
#include "preferences.h"

namespace fms
{

    // CRC64 – must match db_manager.cpp's implementation; share via header later
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

    void PlayRecorder::on_playback_new_track(metadb_handle_ptr track)
    {
        if (!track.is_valid())
            return;

        // Store current track info
        m_current_track = track;
        m_last_playback_time = 0.0;

        const char *path = track->get_path();
        uint64_t crcVal = crc64(path, strlen(path));
        char crcHex[17];
        snprintf(crcHex, sizeof(crcHex), "%016llx", (unsigned long long)crcVal);
        m_current_track_crc = crcHex;
    }

    void PlayRecorder::on_playback_time(double p_time)
    {
        // Update last playback position every second
        m_last_playback_time = p_time;
    }

    void PlayRecorder::on_playback_stop(play_control::t_stop_reason reason)
    {
        if (!m_current_track.is_valid())
            return;

        // Only record if we actually played something (at least 1 second)
        if (m_last_playback_time < 1.0)
            return;

        // Read metadata
        file_info_impl fi;
        if (!m_current_track->get_info(fi))
            return;

        TrackInfo ti;
        ti.track_crc = m_current_track_crc;
        ti.path = m_current_track->get_path();
        ti.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
        ti.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
        ti.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
        ti.length_seconds = m_last_playback_time; // Actual played time

        // UNIX epoch milliseconds (UTC)
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        // FILETIME is 100-ns ticks since 1601-01-01; convert to ms since 1970-01-01
        ti.played_at = static_cast<int64_t>((ul.QuadPart / 10000ULL) - 11644473600000ULL);

        DbManager::get().postPlay(ti);

        // Clear current track
        m_current_track.release();
        m_last_playback_time = 0.0;
        m_current_track_crc.clear();
    }

    // Register static factory
    static play_callback_static_factory_t<PlayRecorder> g_playRecorder;

} // namespace fms
