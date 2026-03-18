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

    void DbManager::refreshPeriod(const std::string &period, bool isYear)
    {
        if (!m_db)
            return;

        // Determine mode from period string length: 4=Year, 7=Month, 10=Day
        bool isDay = (period.size() == 10);

        // 1. Delete existing monthly_count entries for this period
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql = isYear
                                  ? "DELETE FROM monthly_count WHERE SUBSTR(ymd, 1, 4) = ?"
                              : isDay
                                  ? "DELETE FROM monthly_count WHERE ymd = ?"
                                  : "DELETE FROM monthly_count WHERE SUBSTR(ymd, 1, 7) = ?";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                // For year/month: period already has the right format for SUBSTR comparison
                sqlite3_bind_text(stmt, 1, period.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // 2. Recalculate from play_log
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql =
                "INSERT INTO monthly_count(ymd, track_crc, path, title, artist, album, length_seconds, playcount, total_time_seconds)"
                " SELECT strftime('%Y-%m-%d', datetime(played_at/1000, 'unixepoch', 'localtime')) AS ymd,"
                "        track_crc, path, title, artist, album,"
                "        MAX(length_seconds) AS length_seconds,"
                "        COUNT(*) AS playcount,"
                "        SUM(length_seconds) AS total_time_seconds"
                " FROM play_log"
                " WHERE strftime('%Y-%m-%d', datetime(played_at/1000, 'unixepoch', 'localtime'))"
                "       LIKE ? ESCAPE '\\'"
                " GROUP BY ymd, track_crc, path, title, artist, album";

            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                // Year: "2026-%", Month: "2026-03-%", Day: "2026-03-04" (exact)
                std::string pattern = isYear ? (period + "-%") : isDay ? period
                                                                       : (period + "-%");
                sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    void DbManager::deleteEntry(const std::string &ymd, const std::string &track_crc)
    {
        if (!m_db)
            return;

        sqlite3_stmt *stmt = nullptr;
        const char *sql = "DELETE FROM monthly_count WHERE ymd = ? AND track_crc = ?";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, ymd.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, track_crc.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
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
        char *errmsg = nullptr;

        // Always ensure play_log exists
        sqlite3_exec(m_db,
                     "CREATE TABLE IF NOT EXISTS play_log ("
                     "  id        INTEGER PRIMARY KEY,"
                     "  track_crc TEXT NOT NULL,"
                     "  path      TEXT NOT NULL DEFAULT '',"
                     "  title     TEXT,"
                     "  artist    TEXT,"
                     "  album     TEXT,"
                     "  length_seconds REAL NOT NULL DEFAULT 0,"
                     "  played_at INTEGER NOT NULL"
                     ");"
                     "CREATE INDEX IF NOT EXISTS ix_played_at ON play_log(played_at);",
                     nullptr, nullptr, &errmsg);
        if (errmsg)
        {
            FB2K_console_formatter() << "foo_monthly_stats: play_log schema error: " << errmsg;
            sqlite3_free(errmsg);
        }

        // Add missing columns to play_log (ignore errors if already exists)
        sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN length_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

        // -----------------------------------------------------------------------
        // Detect old schema: monthly_count with 'ym' column (pre-1.3.4)
        // -----------------------------------------------------------------------
        bool hasYmColumn = false;
        bool hasYmdColumn = false;
        {
            sqlite3_stmt *info = nullptr;
            if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(monthly_count)", -1, &info, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step(info) == SQLITE_ROW)
                {
                    const char *col = reinterpret_cast<const char *>(sqlite3_column_text(info, 1));
                    if (!col)
                        continue;
                    if (strcmp(col, "ym") == 0)
                        hasYmColumn = true;
                    if (strcmp(col, "ymd") == 0)
                        hasYmdColumn = true;
                }
                sqlite3_finalize(info);
            }
        }

        if (hasYmColumn && !hasYmdColumn)
        {
            // ---------------------------------------------------------------
            // Migrate from old schema (ym TEXT PRIMARY KEY) to new (ymd TEXT)
            // Strategy: create new table, copy with ym||'-01', swap
            // ---------------------------------------------------------------
            FB2K_console_formatter() << "foo_monthly_stats: migrating monthly_count schema (ym -> ymd) ...";
            sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

            // Create new table with ymd column
            sqlite3_exec(m_db,
                         "CREATE TABLE monthly_count_new ("
                         "  ymd       TEXT NOT NULL,"
                         "  track_crc TEXT NOT NULL,"
                         "  path      TEXT NOT NULL DEFAULT '',"
                         "  title     TEXT,"
                         "  artist    TEXT,"
                         "  album     TEXT,"
                         "  length_seconds REAL NOT NULL DEFAULT 0,"
                         "  playcount INTEGER NOT NULL DEFAULT 0,"
                         "  total_time_seconds REAL NOT NULL DEFAULT 0,"
                         "  PRIMARY KEY (ymd, track_crc)"
                         ");",
                         nullptr, nullptr, &errmsg);
            if (errmsg)
            {
                FB2K_console_formatter() << "foo_monthly_stats: create new table error: " << errmsg;
                sqlite3_free(errmsg);
                sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            }
            else
            {
                // Copy data: ym (YYYY-MM) -> ymd (YYYY-MM-01)
                sqlite3_exec(m_db,
                             "INSERT OR IGNORE INTO monthly_count_new "
                             " SELECT ym || '-01', track_crc,"
                             "        COALESCE(path,''), title, artist, album,"
                             "        COALESCE(length_seconds,0), playcount,"
                             "        COALESCE(total_time_seconds,0)"
                             " FROM monthly_count;",
                             nullptr, nullptr, &errmsg);
                if (errmsg)
                {
                    FB2K_console_formatter() << "foo_monthly_stats: copy data error: " << errmsg;
                    sqlite3_free(errmsg);
                }

                sqlite3_exec(m_db, "DROP TABLE monthly_count;", nullptr, nullptr, nullptr);
                sqlite3_exec(m_db, "ALTER TABLE monthly_count_new RENAME TO monthly_count;", nullptr, nullptr, nullptr);
                sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
                FB2K_console_formatter() << "foo_monthly_stats: schema migration complete";
            }
        }
        else if (!hasYmColumn && !hasYmdColumn)
        {
            // Fresh install: create monthly_count with ymd column
            sqlite3_exec(m_db,
                         "CREATE TABLE IF NOT EXISTS monthly_count ("
                         "  ymd       TEXT NOT NULL,"
                         "  track_crc TEXT NOT NULL,"
                         "  path      TEXT NOT NULL DEFAULT '',"
                         "  title     TEXT,"
                         "  artist    TEXT,"
                         "  album     TEXT,"
                         "  length_seconds REAL NOT NULL DEFAULT 0,"
                         "  playcount INTEGER NOT NULL DEFAULT 0,"
                         "  total_time_seconds REAL NOT NULL DEFAULT 0,"
                         "  PRIMARY KEY (ymd, track_crc)"
                         ");",
                         nullptr, nullptr, &errmsg);
            if (errmsg)
            {
                FB2K_console_formatter() << "foo_monthly_stats: monthly_count schema error: " << errmsg;
                sqlite3_free(errmsg);
            }
        }
        // else: hasYmdColumn == true → already on new schema, nothing to do

        // Add missing columns to monthly_count (ignore errors if already exists)
        sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN length_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN total_time_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

        // Create temporary table for deduplication (will be cleared after use)
        sqlite3_exec(m_db,
                     "CREATE TABLE IF NOT EXISTS monthly_count_temp ("
                     "  ymd       TEXT NOT NULL,"
                     "  track_crc TEXT NOT NULL,"
                     "  path      TEXT NOT NULL DEFAULT '',"
                     "  title     TEXT,"
                     "  artist    TEXT,"
                     "  album     TEXT,"
                     "  length_seconds REAL NOT NULL DEFAULT 0,"
                     "  playcount INTEGER NOT NULL DEFAULT 0,"
                     "  total_time_seconds REAL NOT NULL DEFAULT 0,"
                     "  PRIMARY KEY (ymd, track_crc)"
                     ");",
                     nullptr, nullptr, nullptr);

        // Remove duplicates on initialization (merge same titles by metadata)
        removeDuplicates();
    }

    void DbManager::insertPlay(const TrackInfo &info)
    {
        // 1. Insert into play_log
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql =
                "INSERT INTO play_log(track_crc,path,title,artist,album,length_seconds,played_at) VALUES(?,?,?,?,?,?,?)";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, info.path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, info.album.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 6, info.length_seconds);
                sqlite3_bind_int64(stmt, 7, info.played_at);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // 2. Determine YYYY-MM-DD from played_at (local time)
        time_t t = static_cast<time_t>(info.played_at / 1000);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char ymd[11];
        strftime(ymd, sizeof(ymd), "%Y-%m-%d", &local_tm);

        // 3. Upsert into monthly_count
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql =
                "INSERT INTO monthly_count(ymd,track_crc,path,title,artist,album,length_seconds,playcount,total_time_seconds) VALUES(?,?,?,?,?,?,?,?,?)"
                " ON CONFLICT(ymd,track_crc) DO UPDATE SET playcount=playcount+excluded.playcount,"
                "  total_time_seconds=total_time_seconds+excluded.total_time_seconds,"
                "  path=excluded.path, title=excluded.title, artist=excluded.artist, album=excluded.album, length_seconds=excluded.length_seconds";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, ymd, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, info.path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, info.title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, info.artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, info.album.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 7, info.length_seconds);
                // playcount: 1 when on_item_played fires (length=0), 0 when on_playback_stop fires
                sqlite3_bind_int(stmt, 8, (info.length_seconds == 0.0) ? 1 : 0);
                // total_time_seconds: actual played seconds (0 on item_played, real value on stop)
                sqlite3_bind_double(stmt, 9, info.length_seconds);
                int rc = sqlite3_step(stmt);
                if (rc != SQLITE_DONE && rc != SQLITE_ROW)
                {
                    FB2K_console_formatter() << "foo_monthly_stats: monthly_count insert error: "
                                             << sqlite3_errmsg(m_db) << " (ymd=" << ymd << ")";
                }
                sqlite3_finalize(stmt);
            }
            else
            {
                FB2K_console_formatter() << "foo_monthly_stats: monthly_count prepare error: " << sqlite3_errmsg(m_db);
            }
        }
    }

    void DbManager::removeDuplicates()
    {
        if (!m_db)
            return;

        // Detect and consolidate duplicate entries with same title/artist/album but different track_crc
        // This handles the case where files were moved/renamed and acquired different CRCs

        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        // Step 1: Create a temporary table with consolidated data
        sqlite3_exec(m_db, "DELETE FROM monthly_count_temp;", nullptr, nullptr, nullptr);

        // For each group of (title, artist, album), use the first encountered track_crc as canonical
        // and sum up all playcounts and times
        const char *consolidateSql =
            "INSERT INTO monthly_count_temp "
            "SELECT ymd, "
            "       MIN(track_crc) AS track_crc,"
            "       MAX(path) AS path,"
            "       title, artist, album,"
            "       MAX(length_seconds) AS length_seconds,"
            "       SUM(playcount) AS playcount,"
            "       SUM(total_time_seconds) AS total_time_seconds"
            " FROM monthly_count"
            " WHERE title != '' AND artist != '' AND album != ''"
            " GROUP BY ymd, title, artist, album"
            " HAVING COUNT(DISTINCT track_crc) > 1";

        if (sqlite3_exec(m_db, consolidateSql, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            FB2K_console_formatter() << "foo_monthly_stats: consolidate duplicates error: " << sqlite3_errmsg(m_db);
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return;
        }

        // Check how many duplicates we found
        int dupCount = 0;
        {
            sqlite3_stmt *stmt = nullptr;
            const char *sql = "SELECT COUNT(*) FROM monthly_count_temp;";
            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
            {
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    dupCount = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }

        if (dupCount > 0)
        {
            // Step 2: Remove old entries for duplicated groups
            // Find which (title, artist, album) groups had duplicates
            sqlite3_exec(m_db,
                         "DELETE FROM monthly_count "
                         " WHERE (title, artist, album) IN "
                         "   (SELECT DISTINCT title, artist, album FROM monthly_count_temp);",
                         nullptr, nullptr, nullptr);

            // Step 3: Insert consolidated entries
            sqlite3_exec(m_db,
                         "INSERT INTO monthly_count "
                         " SELECT ymd, track_crc, path, title, artist, album, "
                         "        length_seconds, playcount, total_time_seconds"
                         " FROM monthly_count_temp;",
                         nullptr, nullptr, nullptr);

            // Step 4: Clear temp table
            sqlite3_exec(m_db, "DELETE FROM monthly_count_temp;", nullptr, nullptr, nullptr);

            FB2K_console_formatter() << "foo_monthly_stats: consolidated " << dupCount << " duplicate track entries";
        }

        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    std::vector<MonthlyEntry> DbManager::queryMonth(const std::string &ym)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        // Aggregate daily rows into monthly totals
        // Use correlated subquery for prev_playcount to avoid JOIN cross-multiplication
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "SELECT c.track_crc, c.path, c.title, c.artist, c.album,"
            "       MAX(c.length_seconds) AS length_seconds,"
            "       SUM(c.playcount) AS playcount,"
            "       SUM(c.total_time_seconds) AS total_time_seconds,"
            "       COALESCE((SELECT SUM(p.playcount) FROM monthly_count p"
            "                 WHERE p.track_crc = c.track_crc AND SUBSTR(p.ymd,1,7) = ?), 0) AS prev_pc"
            " FROM monthly_count c"
            " WHERE SUBSTR(c.ymd, 1, 7) = ?"
            " GROUP BY c.track_crc, c.path, c.title, c.artist, c.album"
            " HAVING SUM(c.playcount) > 0"
            " ORDER BY playcount DESC";

        // Compute previous month for delta comparison
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
                e.ymd = ym + "-01"; // representative date for monthly aggregate
                e.track_crc = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                e.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                e.title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                e.artist = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                e.album = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                e.length_seconds = sqlite3_column_double(stmt, 5);
                e.playcount = sqlite3_column_int64(stmt, 6);
                e.total_time_seconds = sqlite3_column_double(stmt, 7);
                e.prev_playcount = sqlite3_column_int64(stmt, 8);
                result.push_back(std::move(e));
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: queryMonth prepare error: " << sqlite3_errmsg(m_db);
        }
        return result;
    }

    std::vector<MonthlyEntry> DbManager::queryDay(const std::string &ymd)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        // Single day data
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "SELECT c.ymd, c.track_crc, c.path, c.title, c.artist, c.album, c.length_seconds, c.playcount, c.total_time_seconds,"
            "       COALESCE(p.playcount, 0) AS prev_pc"
            " FROM monthly_count c"
            " LEFT JOIN monthly_count p"
            "   ON p.track_crc = c.track_crc AND p.ymd = ?"
            " WHERE c.ymd = ? AND c.playcount > 0"
            " ORDER BY c.playcount DESC";

        // Compute previous day for delta
        int year = std::stoi(ymd.substr(0, 4));
        int month = std::stoi(ymd.substr(5, 2));
        int day = std::stoi(ymd.substr(8, 2));
        day--;
        if (day == 0)
        {
            month--;
            if (month == 0)
            {
                month = 12;
                year--;
            }
            // Quick approximation: use last day of previous month
            static const int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
            day = daysInMonth[month] + (month == 2 ? leap : 0);
        }
        char prevYmd[11];
        snprintf(prevYmd, sizeof(prevYmd), "%04d-%02d-%02d", year, month, day);

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, prevYmd, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, ymd.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry e;
                e.ymd = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                e.track_crc = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                e.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                e.title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                e.artist = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                e.album = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
                e.length_seconds = sqlite3_column_double(stmt, 6);
                e.playcount = sqlite3_column_int64(stmt, 7);
                e.total_time_seconds = sqlite3_column_double(stmt, 8);
                e.prev_playcount = sqlite3_column_int64(stmt, 9);
                result.push_back(std::move(e));
            }
            sqlite3_finalize(stmt);
        }
        return result;
    }

    std::vector<MonthlyEntry> DbManager::queryYear(const std::string &year)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        sqlite3_stmt *stmt = nullptr;
        // Use subquery for prev_playcount to avoid double-counting from cross-JOIN
        const char *sql =
            "SELECT c.track_crc, c.path, c.title, c.artist, c.album,"
            "       MAX(c.length_seconds) AS length_seconds,"
            "       SUM(c.playcount) AS total_plays,"
            "       SUM(c.total_time_seconds) AS total_time,"
            "       COALESCE((SELECT SUM(p.playcount) FROM monthly_count p"
            "                 WHERE p.track_crc = c.track_crc AND SUBSTR(p.ymd,1,4) = ?), 0) AS prev_total"
            " FROM monthly_count c"
            " WHERE SUBSTR(c.ymd, 1, 4) = ?"
            " GROUP BY c.track_crc, c.path, c.title, c.artist, c.album"
            " HAVING SUM(c.playcount) > 0"
            " ORDER BY total_plays DESC";

        std::string prevYear = std::to_string(std::stoi(year) - 1);

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, prevYear.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, year.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry e;
                e.ymd = year + "-01-01"; // Representative date for yearly aggregate
                e.track_crc = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                e.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                e.title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                e.artist = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                e.album = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                e.length_seconds = sqlite3_column_double(stmt, 5);
                e.playcount = sqlite3_column_int64(stmt, 6);
                e.total_time_seconds = sqlite3_column_double(stmt, 7);
                e.prev_playcount = sqlite3_column_int64(stmt, 8);
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

    std::string DbManager::currentYMD()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &local_tm);
        return buf;
    }

    std::string DbManager::currentYear()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[5];
        strftime(buf, sizeof(buf), "%Y", &local_tm);
        return buf;
    }

} // namespace fms
