// test_db_manager.cpp – Unit tests for DbManager (uses in-memory SQLite)
// These tests are standalone and do NOT depend on the foobar2000 SDK.

#include "../catch2/catch_amalgamated.hpp"

// ---- Minimal stubs to isolate DbManager from foobar2000 SDK ----

// We need to pull in sqlite3 directly. Provide a minimal build environment.
#include "../../third_party/sqlite/sqlite3.h"

#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// ---- Standalone re-implementation of only the DB logic (no SDK) ----
// Copy the essential logic from db_manager.cpp for isolated testing.

struct TrackInfoTest
{
    std::string track_crc;
    std::string title;
    std::string artist;
    std::string album;
    int64_t played_at;
};

struct MonthlyEntryTest
{
    std::string ym;
    std::string track_crc;
    std::string title;
    std::string artist;
    std::string album;
    int64_t playcount;
    int64_t prev_playcount;
};

class TestDb
{
public:
    sqlite3 *db = nullptr;

    bool open(const char *path = ":memory:")
    {
        return sqlite3_open(path, &db) == SQLITE_OK && ensureSchema();
    }
    void close()
    {
        if (db)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }
    ~TestDb() { close(); }

    bool ensureSchema()
    {
        const char *ddl =
            "CREATE TABLE IF NOT EXISTS play_log ("
            "  id INTEGER PRIMARY KEY, track_crc TEXT, title TEXT,"
            "  artist TEXT, album TEXT, played_at INTEGER);"
            "CREATE INDEX IF NOT EXISTS ix_pa ON play_log(played_at);"
            "CREATE TABLE IF NOT EXISTS monthly_count ("
            "  ym TEXT, track_crc TEXT, title TEXT, artist TEXT, album TEXT,"
            "  playcount INTEGER DEFAULT 0, PRIMARY KEY(ym,track_crc));";
        return sqlite3_exec(db, ddl, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    void insertPlay(const TrackInfoTest &ti)
    {
        time_t t = static_cast<time_t>(ti.played_at / 1000);
        struct tm lm;
#ifdef _WIN32
        localtime_s(&lm, &t);
#else
        localtime_r(&t, &lm);
#endif
        char ym[8];
        strftime(ym, sizeof(ym), "%Y-%m", &lm);

        // play_log
        sqlite3_stmt *s1 = nullptr;
        sqlite3_prepare_v2(db,
                           "INSERT INTO play_log(track_crc,title,artist,album,played_at) VALUES(?,?,?,?,?)",
                           -1, &s1, nullptr);
        sqlite3_bind_text(s1, 1, ti.track_crc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s1, 2, ti.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s1, 3, ti.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s1, 4, ti.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s1, 5, ti.played_at);
        sqlite3_step(s1);
        sqlite3_finalize(s1);

        // monthly_count upsert
        sqlite3_stmt *s2 = nullptr;
        sqlite3_prepare_v2(db,
                           "INSERT INTO monthly_count(ym,track_crc,title,artist,album,playcount) VALUES(?,?,?,?,?,1)"
                           " ON CONFLICT(ym,track_crc) DO UPDATE SET playcount=playcount+1,"
                           "  title=excluded.title,artist=excluded.artist,album=excluded.album",
                           -1, &s2, nullptr);
        sqlite3_bind_text(s2, 1, ym, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s2, 2, ti.track_crc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s2, 3, ti.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s2, 4, ti.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s2, 5, ti.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s2);
        sqlite3_finalize(s2);
    }

    std::vector<MonthlyEntryTest> queryMonth(const std::string &ym)
    {
        std::vector<MonthlyEntryTest> res;
        sqlite3_stmt *s = nullptr;
        sqlite3_prepare_v2(db,
                           "SELECT c.ym,c.track_crc,c.title,c.artist,c.album,c.playcount,0"
                           " FROM monthly_count c WHERE c.ym=? ORDER BY c.playcount DESC",
                           -1, &s, nullptr);
        sqlite3_bind_text(s, 1, ym.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            MonthlyEntryTest e;
            e.ym = (const char *)sqlite3_column_text(s, 0);
            e.track_crc = (const char *)sqlite3_column_text(s, 1);
            e.title = (const char *)sqlite3_column_text(s, 2);
            e.artist = (const char *)sqlite3_column_text(s, 3);
            e.album = (const char *)sqlite3_column_text(s, 4);
            e.playcount = sqlite3_column_int64(s, 5);
            res.push_back(e);
        }
        sqlite3_finalize(s);
        return res;
    }

    int64_t rowCount(const char *table)
    {
        sqlite3_stmt *s = nullptr;
        std::string sql = "SELECT COUNT(*) FROM ";
        sql += table;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr);
        sqlite3_step(s);
        int64_t n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
        return n;
    }
};

// Helper: ms timestamp for "2025-07-01 12:00:00 UTC"
static int64_t ts(int year, int mon, int day)
{
    struct tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
#ifdef _WIN32
    time_t ut = _mkgmtime(&t);
#else
    time_t ut = timegm(&t);
#endif
    return static_cast<int64_t>(ut) * 1000;
}

// =====================================================================
// Tests
// =====================================================================

TEST_CASE("Database schema creation", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    REQUIRE(db.rowCount("play_log") == 0);
    REQUIRE(db.rowCount("monthly_count") == 0);
}

TEST_CASE("Single play inserted into play_log", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    TrackInfoTest ti{"abc123", "Track A", "Artist X", "Album 1", ts(2025, 7, 15)};
    db.insertPlay(ti);
    REQUIRE(db.rowCount("play_log") == 1);
}

TEST_CASE("Play count increments for same track same month", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    TrackInfoTest ti{"abc123", "Track A", "Artist X", "Album 1", ts(2025, 7, 15)};
    db.insertPlay(ti);
    db.insertPlay(ti);
    db.insertPlay(ti);
    REQUIRE(db.rowCount("play_log") == 3);
    auto rows = db.queryMonth("2025-07");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].playcount == 3);
}

TEST_CASE("Multiple tracks – sorted by playcount desc", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    TrackInfoTest a{"aaa", "Song A", "Artist", "Album", ts(2025, 7, 1)};
    TrackInfoTest b{"bbb", "Song B", "Artist", "Album", ts(2025, 7, 1)};
    db.insertPlay(a);
    db.insertPlay(b);
    db.insertPlay(b);
    auto rows = db.queryMonth("2025-07");
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].track_crc == "bbb"); // 2 plays
    REQUIRE(rows[1].track_crc == "aaa"); // 1 play
}

TEST_CASE("Plays in different months are separated", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    TrackInfoTest ti{"ccc", "Track C", "Artist", "Album", ts(2025, 7, 1)};
    TrackInfoTest ti2{"ccc", "Track C", "Artist", "Album", ts(2025, 8, 1)};
    db.insertPlay(ti);
    db.insertPlay(ti2);
    auto jul = db.queryMonth("2025-07");
    auto aug = db.queryMonth("2025-08");
    REQUIRE(jul.size() == 1);
    REQUIRE(aug.size() == 1);
    REQUIRE(jul[0].playcount == 1);
    REQUIRE(aug[0].playcount == 1);
}

TEST_CASE("Empty month returns empty vector", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    auto rows = db.queryMonth("2000-01");
    REQUIRE(rows.empty());
}

TEST_CASE("Metadata (title/artist/album) stored correctly", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    TrackInfoTest ti{"ddd", "My Title", "My Artist", "My Album", ts(2025, 7, 10)};
    db.insertPlay(ti);
    auto rows = db.queryMonth("2025-07");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].title == "My Title");
    REQUIRE(rows[0].artist == "My Artist");
    REQUIRE(rows[0].album == "My Album");
}
