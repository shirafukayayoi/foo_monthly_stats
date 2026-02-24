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

        const char *path = track->get_path();
        uint64_t crcVal = crc64(path, strlen(path));
        char crcHex[17];
        snprintf(crcHex, sizeof(crcHex), "%016llx", (unsigned long long)crcVal);

        // Read metadata — get_info() is internally thread-safe; no explicit lock needed
        file_info_impl fi;
        if (!track->get_info(fi))
            return;

        TrackInfo ti;
        ti.track_crc = crcHex;
        ti.path = path;
        ti.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
        ti.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
        ti.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
        ti.length_seconds = track->get_length();

        // UNIX epoch milliseconds (UTC)
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        // FILETIME is 100-ns ticks since 1601-01-01; convert to ms since 1970-01-01
        ti.played_at = static_cast<int64_t>((ul.QuadPart / 10000ULL) - 11644473600000ULL);

        DbManager::get().postPlay(ti);
    }

    // Register static factory
    static play_callback_static_factory_t<PlayRecorder> g_playRecorder;

} // namespace fms
