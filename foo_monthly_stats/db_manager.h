#pragma once
#include "stdafx.h"

namespace fms
{

    // -----------------------------------------------------------------------
    // TrackInfo – data recorded for each play event
    // -----------------------------------------------------------------------
    struct TrackInfo
    {
        std::string track_crc; // CRC64 of track path (hex string)
        std::string path;      // original file path (for album art lookup)
        std::string title;
        std::string artist;
        std::string album;
        double length_seconds; // track length in seconds
        int64_t played_at;     // UNIX epoch milliseconds
    };

    // -----------------------------------------------------------------------
    // MonthlyEntry – one row from monthly_count JOIN data
    // -----------------------------------------------------------------------
    struct MonthlyEntry
    {
        std::string ym; // "2025-07"
        std::string track_crc;
        std::string path; // original file path (for album art lookup)
        std::string title;
        std::string artist;
        std::string album;
        double length_seconds; // track length in seconds
        int64_t playcount;
        int64_t prev_playcount; // same month previous year (for delta)
    };

    // -----------------------------------------------------------------------
    // DbManager – thread-safe SQLite wrapper
    // All mutating operations are posted to a single worker thread.
    // -----------------------------------------------------------------------
    class DbManager
    {
    public:
        DbManager();
        ~DbManager();

        // Open (or create) the database at the given UTF-8 path.
        // Must be called once before any other method.
        bool open(const char *dbPath);

        // Close and join the worker thread.
        void close();

        // Post a play event (non-blocking, returns immediately)
        void postPlay(const TrackInfo &info);

        // Query monthly data synchronously (called on main thread for UI)
        std::vector<MonthlyEntry> queryMonth(const std::string &ym);

        // Query yearly data synchronously (aggregates all months in a year)
        std::vector<MonthlyEntry> queryYear(const std::string &year);

        // Compute current "YYYY-MM" string
        static std::string currentYM();

        // Compute current "YYYY" string
        static std::string currentYear();

        // Singleton access
        static DbManager &get();

    private:
        void workerThread();
        void ensureSchema();
        void insertPlay(const TrackInfo &info);

        sqlite3 *m_db{nullptr};
        std::thread m_thread;
        std::queue<TrackInfo> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_running{false};
        bool m_opened{false};
    };

} // namespace fms
