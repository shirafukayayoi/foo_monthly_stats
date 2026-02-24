#include "stdafx.h"
#include "db_manager.h"

namespace fms
{

    // -----------------------------------------------------------------------
    // CRC64 – Jones polynomial (lightweight, no extra dependency)
    // -----------------------------------------------------------------------
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

    static std::string pathToCrc(const char *path)
    {
        uint64_t v = crc64(path, strlen(path));
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
        return buf;
    }

    // -----------------------------------------------------------------------
    // DbManager
    // -----------------------------------------------------------------------

    static DbManager g_instance;

    DbManager &DbManager::get() { return g_instance; }

    DbManager::DbManager() = default;

    DbManager::~DbManager() { close(); }

    bool DbManager::open(const char *dbPath)
    {
        if (m_opened)
            return true;

        int rc = sqlite3_open(dbPath, &m_db);
        if (rc != SQLITE_OK)
        {
            FB2K_console_formatter() << "foo_monthly_stats: sqlite3_open failed: " << sqlite3_errmsg(m_db);
            sqlite3_close(m_db);
            m_db = nullptr;
            return false;
        }

        // Performance settings
        sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

        ensureSchema();

        m_running = true;
        m_thread = std::thread(&DbManager::workerThread, this);
        m_opened = true;
        return true;
    }

    void DbManager::close()
    {
        if (!m_opened)
            return;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_running = false;
            m_cv.notify_all();
        }
        if (m_thread.joinable())
            m_thread.join();
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        m_opened = false;
    }

    void DbManager::postPlay(const TrackInfo &info)
    {
        if (!m_opened)
            return;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_queue.push(info);
        }
        m_cv.notify_one();
    }

    void DbManager::workerThread()
    {
        while (true)
        {
            TrackInfo item;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this]
                          { return !m_queue.empty() || !m_running; });
                if (!m_running && m_queue.empty())
                    break;
                if (m_queue.empty())
                    continue;
                item = m_queue.front();
                m_queue.pop();
            }
            insertPlay(item);
        }
    }

    void DbManager::ensureSchema()
    {
        const char *ddl =
            "CREATE TABLE IF NOT EXISTS play_log ("
            "  id        INTEGER PRIMARY KEY,"
            "  track_crc TEXT NOT NULL,"
            "  path      TEXT NOT NULL DEFAULT '',"
            "  title     TEXT,"
            "  artist    TEXT,"
            "  album     TEXT,"
            "  played_at INTEGER NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS ix_played_at ON play_log(played_at);"
            "CREATE TABLE IF NOT EXISTS monthly_count ("
            "  ym        TEXT NOT NULL,"
            "  track_crc TEXT NOT NULL,"
            "  path      TEXT NOT NULL DEFAULT '',"
            "  title     TEXT,"
            "  artist    TEXT,"
            "  album     TEXT,"
            "  playcount INTEGER NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (ym, track_crc)"
            ");";
        char *errmsg = nullptr;
        sqlite3_exec(m_db, ddl, nullptr, nullptr, &errmsg);
        if (errmsg)
        {
            FB2K_console_formatter() << "foo_monthly_stats: schema error: " << errmsg;
            sqlite3_free(errmsg);
        }
        // Migrate existing DBs: add path column if it doesn't exist yet (error is ignored if already present)
        sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
    }

    void DbManager::insertPlay(const TrackInfo &info)
    {
        // 1. Insert into play_log
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql =
                "INSERT INTO play_log(track_crc,path,title,artist,album,played_at) VALUES(?,?,?,?,?,?)";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, info.path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, info.album.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 6, info.played_at);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // 2. Determine YYYY-MM from played_at (local time)
        time_t t = static_cast<time_t>(info.played_at / 1000);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char ym[8];
        strftime(ym, sizeof(ym), "%Y-%m", &local_tm);

        // 3. Upsert into monthly_count
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql =
                "INSERT INTO monthly_count(ym,track_crc,path,title,artist,album,playcount) VALUES(?,?,?,?,?,?,1)"
                " ON CONFLICT(ym,track_crc) DO UPDATE SET playcount=playcount+1,"
                "  path=excluded.path, title=excluded.title, artist=excluded.artist, album=excluded.album";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, ym, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, info.artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, info.album.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    std::vector<MonthlyEntry> DbManager::queryMonth(const std::string &ym)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        // Current month data
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "SELECT c.ym, c.track_crc, c.path, c.title, c.artist, c.album, c.playcount,"
            "       COALESCE(p.playcount, 0) AS prev_pc"
            " FROM monthly_count c"
            " LEFT JOIN monthly_count p"
            "   ON p.track_crc = c.track_crc AND p.ym = ?"
            " WHERE c.ym = ?"
            " ORDER BY c.playcount DESC";

        // Compute previous month YM (for delta)
        int year = std::stoi(ym.substr(0, 4));
        int month = std::stoi(ym.substr(5, 2));
        month--;
        if (month == 0)
        {
            month = 12;
            year--;
        }
        char prevYm[8];
        snprintf(prevYm, sizeof(prevYm), "%04d-%02d", year, month);

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, prevYm, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, ym.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry e;
                e.ym = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                e.track_crc = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                e.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                e.title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                e.artist = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                e.album = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
                e.playcount = sqlite3_column_int64(stmt, 6);
                e.prev_playcount = sqlite3_column_int64(stmt, 7);
                result.push_back(std::move(e));
            }
            sqlite3_finalize(stmt);
        }
        return result;
    }

    std::string DbManager::currentYM()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[8];
        strftime(buf, sizeof(buf), "%Y-%m", &local_tm);
        return buf;
    }

} // namespace fms
