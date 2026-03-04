#include "stdafx.h"
#include "report_exporter.h"
#include "preferences.h"

namespace fms
{

    // ---------------------------------------------------------------------------
    // Base64 encoder (for embedding album art as data URIs)
    // ---------------------------------------------------------------------------
    static std::string base64_encode(const void *raw, size_t len)
    {
        static const char kB64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const auto *d = static_cast<const uint8_t *>(raw);
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len;)
        {
            uint32_t v = 0;
            int n = 0;
            while (i < len && n < 3)
            {
                v = (v << 8) | d[i++];
                ++n;
            }
            v <<= (3 - n) * 8;
            out += kB64[(v >> 18) & 63];
            out += kB64[(v >> 12) & 63];
            out += (n >= 2 ? kB64[(v >> 6) & 63] : '=');
            out += (n >= 3 ? kB64[(v) & 63] : '=');
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    // Helper: trim whitespace from C-string
    // ---------------------------------------------------------------------------
    static std::string trimString(const char *str)
    {
        if (!str)
            return "";
        std::string s(str);
        // Trim leading whitespace
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        // Trim trailing whitespace
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // ---------------------------------------------------------------------------
    // Helper: case-insensitive string comparison
    // ---------------------------------------------------------------------------
    static bool caseInsensitiveMatch(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (tolower(static_cast<unsigned char>(a[i])) !=
                tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Helper: check if file path exists using filesystem
    // ---------------------------------------------------------------------------
    static bool filePathExists(const char *path)
    {
        try
        {
            abort_callback_dummy abort;
            auto fs = filesystem::get(path);
            if (fs.is_valid())
            {
                return fs->file_exists(path, abort);
            }
        }
        catch (...)
        {
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // Album art collection (must be called on main thread)
    // ---------------------------------------------------------------------------
    std::map<std::string, std::string> ReportExporter::collectArt(
        const std::vector<MonthlyEntry> &entries)
    {
        std::map<std::string, std::string> artMap;
        try
        {
            auto aam = album_art_manager_v2::get();
            auto lib = library_manager::get();
            abort_callback_dummy abort;

            // Pre-fetch all library items once for efficiency
            metadb_handle_list allItems;
            try
            {
                lib->get_all_items(allItems);
                console::printf("[fms] Album art: loaded %u items from library", (unsigned)allItems.get_count());
            }
            catch (const std::exception &ex)
            {
                console::printf("[fms] Failed to load library items: %s", ex.what());
            }

            for (const auto &e : entries)
            {
                metadb_handle_ptr h;

                // Step 1: Try to create handle from stored path, but verify file exists
                if (!e.path.empty())
                {
                    // Check if path looks like a file:// URL and validate existence
                    bool pathIsValid = false;
                    try
                    {
                        // Verify the file actually exists before using this path
                        if (filePathExists(e.path.c_str()))
                        {
                            h = metadb::get()->handle_create(e.path.c_str(), 0);
                            console::printf("[fms] Using path handle for: %s", e.path.c_str());
                            pathIsValid = true;
                        }
                        else
                        {
                            console::printf("[fms] Path file does not exist: %s", e.path.c_str());
                        }
                    }
                    catch (const std::exception &ex)
                    {
                        console::printf("[fms] Path validation failed: %s (path: %s)", ex.what(), e.path.c_str());
                    }
                    catch (...)
                    {
                        console::printf("[fms] Path validation failed (unknown exception): %s", e.path.c_str());
                    }

                    if (!pathIsValid)
                    {
                        h = nullptr;
                    }
                }

                // Step 2: If path failed or file doesn't exist, search library by metadata
                if (!h.is_valid())
                {
                    console::printf("[fms] Searching library for: '%s' - '%s' - '%s'",
                                    e.title.c_str(), e.artist.c_str(), e.album.c_str());

                    std::string searchTitle = trimString(e.title.c_str());
                    std::string searchArtist = trimString(e.artist.c_str());
                    std::string searchAlbum = trimString(e.album.c_str());

                    // First pass: exact match with trimmed strings
                    for (t_size i = 0; i < allItems.get_count(); ++i)
                    {
                        metadb_handle_ptr item = allItems[i];
                        file_info_impl fi;
                        if (!item->get_info(fi))
                            continue;

                        std::string itemTitle = trimString(fi.meta_get("TITLE", 0));
                        std::string itemArtist = trimString(fi.meta_get("ARTIST", 0));
                        std::string itemAlbum = trimString(fi.meta_get("ALBUM", 0));

                        if (itemTitle == searchTitle &&
                            itemArtist == searchArtist &&
                            itemAlbum == searchAlbum)
                        {
                            h = item;
                            console::printf("[fms] Found exact match in library: %s", item->get_path());
                            break;
                        }
                    }

                    // Second pass: case-insensitive match if exact match failed
                    if (!h.is_valid())
                    {
                        for (t_size i = 0; i < allItems.get_count(); ++i)
                        {
                            metadb_handle_ptr item = allItems[i];
                            file_info_impl fi;
                            if (!item->get_info(fi))
                                continue;

                            std::string itemTitle = trimString(fi.meta_get("TITLE", 0));
                            std::string itemArtist = trimString(fi.meta_get("ARTIST", 0));
                            std::string itemAlbum = trimString(fi.meta_get("ALBUM", 0));

                            if (caseInsensitiveMatch(itemTitle, searchTitle) &&
                                caseInsensitiveMatch(itemArtist, searchArtist) &&
                                caseInsensitiveMatch(itemAlbum, searchAlbum))
                            {
                                h = item;
                                console::printf("[fms] Found case-insensitive match in library: %s", item->get_path());
                                break;
                            }
                        }
                    }

                    if (!h.is_valid())
                    {
                        console::printf("[fms] Track not found in library after metadata search");
                        continue;
                    }
                }

                // Step 3: Now we have a valid handle, try to get album art
                bool artObtained = false;
                std::string failureReason;

                try
                {
                    metadb_handle_list handles;
                    handles.add_item(h);
                    pfc::list_single_ref_t<GUID> types(album_art_ids::cover_front);
                    auto inst = aam->open(handles, types, abort);

                    if (inst.is_valid())
                    {
                        album_art_data_ptr data = inst->query(album_art_ids::cover_front, abort);
                        if (data.is_valid() && data->get_size() > 0)
                        {
                            // Successfully got album art
                            std::string dataUri = "data:image/jpeg;base64," +
                                                  base64_encode(data->get_ptr(), data->get_size());
                            artMap[e.track_crc] = dataUri;
                            console::printf("[fms] Got album art for: %s (%u bytes)", e.title.c_str(), (unsigned)data->get_size());
                            artObtained = true;
                        }
                        else
                        {
                            failureReason = "No album art data";
                            console::printf("[fms] No album art data for: %s", e.title.c_str());
                        }
                    }
                    else
                    {
                        failureReason = "Failed to open album art instance";
                        console::printf("[fms] Failed to open album art instance for: %s", e.title.c_str());
                    }
                }
                catch (const std::exception &ex)
                {
                    failureReason = ex.what();
                    console::printf("[fms] Exception getting art for %s: %s", e.title.c_str(), ex.what());
                }
                catch (...)
                {
                    failureReason = "Unknown exception";
                    console::printf("[fms] Unknown exception getting art for: %s", e.title.c_str());
                }

                // Step 4: If art not obtained, try fallback search for alternative versions of the same track
                if (!artObtained && !e.title.empty() && !e.artist.empty())
                {
                    console::printf("[fms] Attempting fallback search for alternative versions: '%s' by '%s'",
                                    e.title.c_str(), e.artist.c_str());

                    std::string searchTitle = trimString(e.title.c_str());
                    std::string searchArtist = trimString(e.artist.c_str());

                    // Search for alternative tracks with same title and artist (possibly different album)
                    for (t_size i = 0; i < allItems.get_count() && !artObtained; ++i)
                    {
                        metadb_handle_ptr altItem = allItems[i];
                        file_info_impl fi;
                        if (!altItem->get_info(fi))
                            continue;

                        std::string itemTitle = trimString(fi.meta_get("TITLE", 0));
                        std::string itemArtist = trimString(fi.meta_get("ARTIST", 0));

                        // Match on title and artist only, ignore album
                        if (itemTitle == searchTitle && itemArtist == searchArtist)
                        {
                            try
                            {
                                metadb_handle_list handles;
                                handles.add_item(altItem);
                                pfc::list_single_ref_t<GUID> types(album_art_ids::cover_front);
                                auto inst = aam->open(handles, types, abort);

                                if (inst.is_valid())
                                {
                                    album_art_data_ptr data = inst->query(album_art_ids::cover_front, abort);
                                    if (data.is_valid() && data->get_size() > 0)
                                    {
                                        // Got album art from alternative track
                                        std::string dataUri = "data:image/jpeg;base64," +
                                                              base64_encode(data->get_ptr(), data->get_size());
                                        artMap[e.track_crc] = dataUri;
                                        console::printf("[fms] Got album art from fallback (alt album): %s (%u bytes)",
                                                        e.title.c_str(), (unsigned)data->get_size());
                                        artObtained = true;
                                    }
                                }
                            }
                            catch (...)
                            {
                                // Ignore errors and continue searching
                            }
                        }
                    }

                    if (!artObtained)
                    {
                        console::printf("[fms] Fallback search failed to find album art for: %s", e.title.c_str());
                    }
                }
            }

            console::printf("[fms] Album art collection complete. Found %u of %u covers.", (unsigned)artMap.size(), (unsigned)entries.size());
        }
        catch (const std::exception &ex)
        {
            console::printf("[fms] Exception in collectArt: %s", ex.what());
        }
        catch (...)
        {
            console::print("[fms] Unknown exception in collectArt");
        }
        return artMap;
    }

    // Smartphone HTML generation (1080x1980px fixed canvas)
    // Must be called before the main exportHtml function
    namespace
    {
        std::string GenerateSmartphoneHtml(
            const std::string &periodLabel,
            const std::vector<MonthlyEntry> &entries,
            const std::wstring &htmlPath,
            const std::map<std::string, std::string> &artMap)
        {
            pugi::xml_document doc;

            // DOCTYPE
            doc.append_child(pugi::node_doctype).set_value("html");

            auto html = doc.append_child("html");
            html.append_attribute("lang") = "ja";

            // <head>
            auto head = html.append_child("head");
            head.append_child("meta").append_attribute("charset") = "UTF-8";
            {
                auto meta = head.append_child("meta");
                meta.append_attribute("name") = "viewport";
                meta.append_attribute("content") = "width=1080, initial-scale=1, maximum-scale=1, user-scalable=no";
            }
            head.append_child("title").text().set(periodLabel.c_str());

            // Google Fonts (Inter)
            {
                auto link = head.append_child("link");
                link.append_attribute("rel") = "preconnect";
                link.append_attribute("href") = "https://fonts.googleapis.com";
            }
            {
                auto link = head.append_child("link");
                link.append_attribute("rel") = "preconnect";
                link.append_attribute("href") = "https://fonts.gstatic.com";
                link.append_attribute("crossorigin") = "";
            }
            {
                auto link = head.append_child("link");
                link.append_attribute("rel") = "stylesheet";
                link.append_attribute("href") =
                    "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
            }

            // Embedded CSS for smartphone
            auto style = head.append_child("style");
            style.text().set(R"CSS(
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body {
    width: 1080px;
    height: auto;
    min-height: 1980px;
}
body {
    font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    padding: 40px 20px;
    color: #1a1a1a;
    overflow-y: auto;
    overflow-x: hidden;
}
.container {
    width: 100%;
    max-width: 1040px;
    margin: 0 auto;
}
h1 {
    font-size: 28px;
    font-weight: 700;
    color: white;
    margin-bottom: 30px;
    text-align: center;
    text-shadow: 0 2px 10px rgba(0,0,0,0.2);
    letter-spacing: 0.5px;
}
/* 統計情報セクション - Instagram用に大きく表示 */
.stats-container {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin-bottom: 30px;
    width: 100%;
}
.stat-card {
    background: rgba(255,255,255,0.18);
    backdrop-filter: blur(15px);
    border-radius: 16px;
    padding: 24px;
    text-align: center;
    box-shadow: 0 8px 20px rgba(0,0,0,0.15);
    border: 1px solid rgba(255,255,255,0.25);
    transition: transform 0.2s;
}
.stat-card:hover {
    transform: translateY(-4px);
}
.stat-label {
    font-size: 12px;
    color: rgba(255,255,255,0.85);
    margin-bottom: 12px;
    font-weight: 500;
    letter-spacing: 0.5px;
    text-transform: uppercase;
}
.stat-value {
    font-size: 36px;
    font-weight: 700;
    color: white;
    line-height: 1.2;
}
.stat-subtext {
    font-size: 13px;
    color: rgba(255,255,255,0.75);
    margin-top: 8px;
}
/* トップアーティスト - 大きく強調 */
.top-artist-section {
    background: rgba(255,255,255,0.12);
    backdrop-filter: blur(15px);
    border-radius: 16px;
    padding: 30px;
    margin: 30px 0;
    text-align: center;
    box-shadow: 0 8px 20px rgba(0,0,0,0.15);
    border: 2px solid rgba(255,255,255,0.3);
}
.section-label {
    font-size: 14px;
    color: rgba(255,255,255,0.8);
    margin-bottom: 10px;
    text-transform: uppercase;
    letter-spacing: 1px;
}
.top-artist-name {
    font-size: 32px;
    font-weight: 700;
    color: white;
    margin-bottom: 8px;
    word-break: break-word;
}
.top-artist-info {
    font-size: 18px;
    color: rgba(255,255,255,0.85);
    margin-bottom: 20px;
}
.top-artist-art {
    width: 200px;
    height: 200px;
    border-radius: 12px;
    object-fit: cover;
    margin: 0 auto;
    box-shadow: 0 12px 30px rgba(0,0,0,0.25);
    border: 3px solid rgba(255,255,255,0.4);
}
.artist-ranking {
    background: rgba(255,255,255,0.12);
    backdrop-filter: blur(15px);
    border-radius: 16px;
    padding: 24px;
    margin: 30px 0;
    box-shadow: 0 8px 20px rgba(0,0,0,0.15);
    border: 1px solid rgba(255,255,255,0.2);
    width: 100%;
}
.artist-ranking h2 {
    font-size: 18px;
    font-weight: 600;
    color: white;
    margin-bottom: 20px;
    text-align: center;
    text-transform: uppercase;
    letter-spacing: 1px;
}
.artist-list {
    display: flex;
    flex-direction: column;
    gap: 14px;
    width: 100%;
}
.artist-item {
    display: flex;
    align-items: center;
    gap: 14px;
    background: rgba(255,255,255,0.08);
    padding: 14px;
    border-radius: 12px;
    min-width: 0;
    overflow: hidden;
    width: 100%;
    box-sizing: border-box;
    border: 1px solid rgba(255,255,255,0.15);
    transition: background 0.2s;
}
.artist-item:hover {
    background: rgba(255,255,255,0.12);
}
.artist-avatar {
    width: 70px;
    height: 70px;
    border-radius: 8px;
    object-fit: cover;
    flex-shrink: 0;
    border: 2px solid rgba(255,255,255,0.5);
    box-shadow: 0 4px 12px rgba(0,0,0,0.2);
}
.artist-info {
    flex: 1;
    min-width: 0;
    max-width: 100%;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    justify-content: center;
}
.artist-rank {
    font-weight: 700;
    color: rgba(255,255,255,0.9);
    font-size: 13px;
    margin-bottom: 2px;
}
.artist-name {
    font-weight: 700;
    color: white;
    font-size: 14px;
    margin-bottom: 2px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    width: 100%;
}
.artist-plays {
    font-weight: 500;
    color: rgba(255,255,255,0.8);
    font-size: 12px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    width: 100%;
}
.tracks-section {
    margin: 30px 0;
    width: 100%;
}
.tracks-section h2 {
    font-size: 18px;
    font-weight: 600;
    color: white;
    margin-bottom: 20px;
    text-align: center;
    text-transform: uppercase;
    letter-spacing: 1px;
}
.track-item {
    display: flex;
    gap: 14px;
    margin-bottom: 14px;
    background: white;
    border-radius: 12px;
    overflow: hidden;
    box-shadow: 0 4px 12px rgba(0,0,0,0.2);
    width: 100%;
    box-sizing: border-box;
    transition: transform 0.2s;
}
.track-item:hover {
    transform: translateY(-2px);
}
.track-art {
    width: 90px;
    height: 90px;
    flex-shrink: 0;
    background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
    object-fit: cover;
}
.track-info {
    flex: 1;
    min-width: 0;
    padding: 14px;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
}
.track-title {
    font-size: 15px;
    font-weight: 700;
    color: #1a1a1a;
    margin-bottom: 4px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    line-height: 1.2;
}
.track-artist {
    font-size: 12px;
    color: #666;
    margin-bottom: 2px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.track-album {
    font-size: 11px;
    color: #999;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    margin-bottom: 4px;
}
.track-rank {
    display: inline-block;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    padding: 4px 8px;
    border-radius: 6px;
    font-size: 12px;
    font-weight: 700;
    min-width: 28px;
}
.track-stats {
    font-size: 14px;
    font-weight: 600;
    color: #667eea;
}
.footer {
    text-align: center;
    margin-top: 30px;
    padding-top: 20px;
    border-top: 1px solid rgba(255,255,255,0.2);
    color: rgba(255,255,255,0.7);
    font-size: 12px;
}
)CSS");

            // <body>
            auto body = html.append_child("body");
            auto container = body.append_child("div");
            container.append_attribute("class") = "container";

            // Title
            {
                auto h1 = container.append_child("h1");
                h1.text().set(periodLabel.c_str());
            }

            // Calculate statistics
            int64_t totalPlaycount = 0;
            double totalSeconds = 0.0;
            std::string topArtist;
            int64_t topArtistPlays = 0;
            std::string topArtistCrc;

            // Aggregate stats
            std::map<std::string, int64_t> artistPlays;
            for (const auto &e : entries)
            {
                totalPlaycount += e.playcount;
                totalSeconds += e.total_time_seconds;
                artistPlays[e.artist] += e.playcount;
            }

            // Find top artist
            for (const auto &[artist, plays] : artistPlays)
            {
                if (plays > topArtistPlays)
                {
                    topArtistPlays = plays;
                    topArtist = artist;
                }
            }

            // Find album art for top artist
            if (!topArtist.empty())
            {
                for (const auto &e : entries)
                {
                    if (e.artist == topArtist)
                    {
                        topArtistCrc = e.track_crc;
                        break;
                    }
                }
            }

            // Statistics cards
            {
                auto statsContainer = container.append_child("div");
                statsContainer.append_attribute("class") = "stats-container";

                // Total plays card
                {
                    auto card = statsContainer.append_child("div");
                    card.append_attribute("class") = "stat-card";

                    auto label = card.append_child("div");
                    label.append_attribute("class") = "stat-label";
                    label.text().set("Total Plays");

                    auto value = card.append_child("div");
                    value.append_attribute("class") = "stat-value";
                    value.text().set(std::to_string(totalPlaycount).c_str());
                }

                // Total listening time card
                {
                    auto card = statsContainer.append_child("div");
                    card.append_attribute("class") = "stat-card";

                    auto label = card.append_child("div");
                    label.append_attribute("class") = "stat-label";
                    label.text().set("Total Time");

                    int hours = static_cast<int>(totalSeconds / 3600);
                    int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);

                    auto value = card.append_child("div");
                    value.append_attribute("class") = "stat-value";
                    std::string timeText = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
                    value.text().set(timeText.c_str());

                    auto subtext = card.append_child("div");
                    subtext.append_attribute("class") = "stat-subtext";
                    std::string subtextStr = std::to_string(static_cast<long long>(totalSeconds)) + " seconds";
                    subtext.text().set(subtextStr.c_str());
                }
            }

            // Top Artist Section (highlighted)
            if (!topArtist.empty())
            {
                auto topArtistDiv = container.append_child("div");
                topArtistDiv.append_attribute("class") = "top-artist-section";

                auto sectionLabel = topArtistDiv.append_child("div");
                sectionLabel.append_attribute("class") = "section-label";
                sectionLabel.text().set("Most Played Artist");

                auto artistName = topArtistDiv.append_child("div");
                artistName.append_attribute("class") = "top-artist-name";
                artistName.text().set(topArtist.c_str());

                auto artistInfo = topArtistDiv.append_child("div");
                artistInfo.append_attribute("class") = "top-artist-info";
                std::string infoText = std::to_string(topArtistPlays) + " plays";
                artistInfo.text().set(infoText.c_str());

                // Album art
                auto it = artMap.find(topArtistCrc);
                if (it != artMap.end())
                {
                    auto img = topArtistDiv.append_child("img");
                    img.append_attribute("class") = "top-artist-art";
                    img.append_attribute("src") = it->second.c_str();
                    img.append_attribute("alt") = topArtist.c_str();
                }
                else
                {
                    auto placeholder = topArtistDiv.append_child("div");
                    placeholder.append_attribute("class") = "top-artist-art";
                    placeholder.append_attribute("style") = "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);";
                }
            }

            // Artist Ranking (Top 5)
            {
                // Aggregate plays by artist
                struct ArtistInfo
                {
                    int64_t totalPlays = 0;
                    std::string topTrackCrc;
                    int64_t topTrackPlays = 0;
                };
                std::map<std::string, ArtistInfo> artistMap;

                for (const auto &e : entries)
                {
                    auto &info = artistMap[e.artist];
                    info.totalPlays += e.playcount;

                    if (e.playcount > info.topTrackPlays)
                    {
                        info.topTrackPlays = e.playcount;
                        info.topTrackCrc = e.track_crc;
                    }
                }

                // Sort by total playcount
                std::vector<std::pair<std::string, ArtistInfo>> artistVec;
                for (const auto &[artist, info] : artistMap)
                {
                    artistVec.push_back({artist, info});
                }
                std::sort(artistVec.begin(), artistVec.end(),
                          [](const auto &a, const auto &b)
                          { return a.second.totalPlays > b.second.totalPlays; });

                // Display top 5 artists
                if (!artistVec.empty())
                {
                    auto rankingDiv = container.append_child("div");
                    rankingDiv.append_attribute("class") = "artist-ranking";

                    auto h2 = rankingDiv.append_child("h2");
                    h2.text().set("Top 5 Artists");

                    auto artistList = rankingDiv.append_child("div");
                    artistList.append_attribute("class") = "artist-list";

                    int count = 0;
                    for (const auto &[artist, info] : artistVec)
                    {
                        if (++count > 5)
                            break;

                        auto item = artistList.append_child("div");
                        item.append_attribute("class") = "artist-item";

                        // Rank badge
                        auto rankBadge = item.append_child("div");
                        rankBadge.append_attribute("class") = "artist-rank";
                        rankBadge.text().set(("#" + std::to_string(count)).c_str());

                        // Album art
                        auto it = artMap.find(info.topTrackCrc);
                        if (it != artMap.end())
                        {
                            auto img = item.append_child("img");
                            img.append_attribute("class") = "artist-avatar";
                            img.append_attribute("src") = it->second.c_str();
                            img.append_attribute("alt") = artist.c_str();
                            img.append_attribute("loading") = "lazy";
                        }
                        else
                        {
                            auto placeholder = item.append_child("div");
                            placeholder.append_attribute("class") = "artist-avatar";
                            placeholder.append_attribute("style") = "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);";
                        }

                        auto info_div = item.append_child("div");
                        info_div.append_attribute("class") = "artist-info";

                        auto nameDiv = info_div.append_child("div");
                        nameDiv.append_attribute("class") = "artist-name";
                        nameDiv.text().set(artist.c_str());

                        auto playsDiv = info_div.append_child("div");
                        playsDiv.append_attribute("class") = "artist-plays";
                        std::string playsText = std::to_string(info.totalPlays) + " plays";
                        playsDiv.text().set(playsText.c_str());
                    }
                }
            }

            // Top Tracks (limit to top 10)
            {
                std::vector<MonthlyEntry> sortedEntries = entries;
                std::sort(sortedEntries.begin(), sortedEntries.end(),
                          [](const auto &a, const auto &b)
                          { return a.playcount > b.playcount; });

                auto tracksDiv = container.append_child("div");
                tracksDiv.append_attribute("class") = "tracks-section";

                auto h2 = tracksDiv.append_child("h2");
                h2.text().set("Top Tracks");

                int rank = 1;
                for (const auto &e : sortedEntries)
                {
                    if (rank > 10)
                        break;

                    auto item = tracksDiv.append_child("div");
                    item.append_attribute("class") = "track-item";

                    // Rank badge
                    auto rankBadge = item.append_child("div");
                    rankBadge.append_attribute("class") = "track-rank";
                    rankBadge.text().set(("#" + std::to_string(rank)).c_str());

                    // Album art
                    auto it = artMap.find(e.track_crc);
                    if (it != artMap.end())
                    {
                        auto img = item.append_child("img");
                        img.append_attribute("class") = "track-art";
                        img.append_attribute("src") = it->second.c_str();
                        img.append_attribute("alt") = e.album.c_str();
                        img.append_attribute("loading") = "lazy";
                    }
                    else
                    {
                        auto placeholder = item.append_child("div");
                        placeholder.append_attribute("class") = "track-art";
                        placeholder.append_attribute("style") = "background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);";
                    }

                    // Track info
                    auto info = item.append_child("div");
                    info.append_attribute("class") = "track-info";

                    auto title = info.append_child("div");
                    title.append_attribute("class") = "track-title";
                    title.text().set(e.title.c_str());

                    auto artist = info.append_child("div");
                    artist.append_attribute("class") = "track-artist";
                    artist.text().set(e.artist.c_str());

                    auto album = info.append_child("div");
                    album.append_attribute("class") = "track-album";
                    album.text().set(e.album.c_str());

                    auto stats = info.append_child("div");
                    stats.append_attribute("class") = "track-stats";
                    std::string playsText = std::to_string(e.playcount) + " plays";
                    stats.text().set(playsText.c_str());

                    rank++;
                }
            }

            // Footer
            {
                auto footer = container.append_child("div");
                footer.append_attribute("class") = "footer";
                footer.text().set("Generated by foo_monthly_stats");
            }

            // Save file
            bool ok = doc.save_file(htmlPath.c_str(), "  ", pugi::format_default | pugi::format_write_bom, pugi::encoding_utf8);
            if (!ok)
            {
                std::string errPath = pfc::stringcvt::string_utf8_from_wide(htmlPath.c_str());
                return "Failed to write smartphone HTML file: " + errPath;
            }
            return "";
        }
    } // anonymous namespace

    // ---------------------------------------------------------------------------
    // HTML generation using pugixml (Desktop version)
    // ---------------------------------------------------------------------------
    std::string ReportExporter::exportHtml(
        const std::string &periodLabel,
        const std::vector<MonthlyEntry> &entries,
        const std::wstring &htmlPath,
        const std::map<std::string, std::string> &artMap,
        bool isSmartphone)
    {
        // For smartphone format, use a separate implementation
        if (isSmartphone)
        {
            return GenerateSmartphoneHtml(periodLabel, entries, htmlPath, artMap);
        }

        pugi::xml_document doc;

        // DOCTYPE
        doc.append_child(pugi::node_doctype).set_value("html");

        auto html = doc.append_child("html");
        html.append_attribute("lang") = "ja";

        // <head>
        auto head = html.append_child("head");
        head.append_child("meta").append_attribute("charset") = "UTF-8";
        {
            auto meta = head.append_child("meta");
            meta.append_attribute("name") = "viewport";
            meta.append_attribute("content") = "width=1280, initial-scale=1";
        }
        head.append_child("title").text().set(periodLabel.c_str());

        // Google Fonts (Inter)
        {
            auto link = head.append_child("link");
            link.append_attribute("rel") = "preconnect";
            link.append_attribute("href") = "https://fonts.googleapis.com";
        }
        {
            auto link = head.append_child("link");
            link.append_attribute("rel") = "preconnect";
            link.append_attribute("href") = "https://fonts.gstatic.com";
            link.append_attribute("crossorigin") = "";
        }
        {
            auto link = head.append_child("link");
            link.append_attribute("rel") = "stylesheet";
            link.append_attribute("href") =
                "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
        }

        // Embedded CSS
        auto style = head.append_child("style");
        style.text().set(R"CSS(
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    padding: 2rem 1.5rem;
    color: #1a1a1a;
}
.container {
    max-width: 1800px;
    margin: 0 auto;
}
h1 {
    font-size: 2.5rem;
    font-weight: 700;
    color: white;
    margin-bottom: 2rem;
    text-align: center;
    text-shadow: 0 2px 10px rgba(0,0,0,0.2);
}
.artist-ranking {
    background: rgba(255,255,255,0.15);
    backdrop-filter: blur(10px);
    border-radius: 16px;
    padding: 1.5rem;
    margin: 2rem auto;
    width: 100%;
    box-shadow: 0 4px 12px rgba(0,0,0,0.1);
    overflow-x: auto;
}
.artist-ranking h2 {
    font-size: 1.5rem;
    font-weight: 600;
    color: white;
    margin-bottom: 1.5rem;
    text-align: center;
}
.artist-table {
    border-collapse: separate;
    border-spacing: 1.5rem 0;
    white-space: nowrap;
}
.artist-cell {
    vertical-align: top;
    text-align: center;
    width: 140px;
    padding-bottom: 0.5rem;
}
.artist-avatar {
    display: block;
    width: 140px;
    height: 140px;
    margin: 0 auto 0.75rem;
    border-radius: 50%;
    object-fit: cover;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    border: 3px solid rgba(255,255,255,0.5);
}
.artist-name {
    display: block;
    font-weight: 600;
    color: white;
    font-size: 0.9rem;
    margin-bottom: 0.25rem;
    text-align: center;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    width: 140px;
}
.artist-plays {
    display: block;
    font-weight: 500;
    color: rgba(255,255,255,0.8);
    font-size: 0.85rem;
    text-align: center;
}
.grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    gap: 1.25rem;
}
.card {
    background: white;
    border-radius: 12px;
    overflow: hidden;
    box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    transition: all 0.3s ease;
    position: relative;
}
.card:hover {
    transform: translateY(-8px);
    box-shadow: 0 12px 24px rgba(0,0,0,0.25);
}
.art-container {
    position: relative;
    width: 100%;
    padding-top: 100%;
    background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
    overflow: hidden;
}
.art-img {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    object-fit: cover;
    transition: transform 0.3s ease;
}
.card:hover .art-img {
    transform: scale(1.05);
}
.rank-badge {
    position: absolute;
    top: 10px;
    left: 10px;
    width: 32px;
    height: 32px;
    background: rgba(0,0,0,0.75);
    color: white;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-weight: 700;
    font-size: 0.8125rem;
    backdrop-filter: blur(8px);
    z-index: 1;
}
.info {
    padding: 1rem;
}
.title {
    font-size: 1rem;
    font-weight: 600;
    color: #1a1a1a;
    margin-bottom: 0.3rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.artist {
    font-size: 0.875rem;
    color: #666;
    margin-bottom: 0.2rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.album {
    font-size: 0.8125rem;
    color: #999;
    margin-bottom: 0.875rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.stats {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding-top: 0.625rem;
    border-top: 1px solid #eee;
}
.plays {
    font-size: 0.875rem;
    font-weight: 600;
    color: #667eea;
}
.delta {
    font-size: 0.8125rem;
    font-weight: 600;
    padding: 0.2rem 0.5rem;
    border-radius: 10px;
}
.delta.positive {
    background: #d4edda;
    color: #155724;
}
.delta.negative {
    background: #f8d7da;
    color: #721c24;
}
)CSS");

        // JavaScript to detect page height for full-page screenshot
        auto script = head.append_child("script");
        script.text().set(R"JS(
window.addEventListener('load', function() {
    // Wait for all images to load
    var images = document.getElementsByTagName('img');
    var loaded = 0;
    var total = images.length;
    
    function checkComplete() {
        if (loaded >= total) {
            // Get actual content height using container
            var container = document.querySelector('.container');
            var body = document.body;
            var bodyStyle = window.getComputedStyle(body);
            var paddingTop = parseFloat(bodyStyle.paddingTop);
            var paddingBottom = parseFloat(bodyStyle.paddingBottom);
            
            // Calculate total height: container height + body padding
            var containerHeight = container ? container.offsetHeight : body.scrollHeight;
            var height = Math.ceil(containerHeight + paddingTop + paddingBottom);
            
            document.title = 'HEIGHT:' + height;
        }
    }
    
    if (total === 0) {
        checkComplete();
    } else {
        for (var i = 0; i < total; i++) {
            if (images[i].complete) {
                loaded++;
            } else {
                images[i].addEventListener('load', function() {
                    loaded++;
                    checkComplete();
                });
                images[i].addEventListener('error', function() {
                    loaded++;
                    checkComplete();
                });
            }
        }
        checkComplete();
    }
});
)JS");

        // <body>
        auto body = html.append_child("body");
        auto container = body.append_child("div");
        container.append_attribute("class") = "container";

        // Title
        {
            auto h1 = container.append_child("h1");
            h1.text().set(periodLabel.c_str());
        }

        // Total playback time
        {
            double totalSeconds = 0.0;
            for (const auto &e : entries)
            {
                totalSeconds += e.total_time_seconds;
            }

            int hours = static_cast<int>(totalSeconds / 3600);
            int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);
            int seconds = static_cast<int>(totalSeconds - hours * 3600 - minutes * 60);

            auto stats = container.append_child("div");
            stats.append_attribute("style") = "text-align: center; margin: 1.5rem 0; font-size: 1.1rem; color: rgba(255,255,255,0.9);";

            std::string timeText = "Total Listening Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
            stats.text().set(timeText.c_str());
        }

        // Artist Ranking (Top 10)
        {
            // Aggregate plays by artist and track their most played song
            struct ArtistInfo
            {
                int64_t totalPlays = 0;
                std::string topTrackCrc;
                int64_t topTrackPlays = 0;
            };
            std::map<std::string, ArtistInfo> artistMap;

            for (const auto &e : entries)
            {
                auto &info = artistMap[e.artist];
                info.totalPlays += e.playcount;

                // Track the most played song for this artist
                if (e.playcount > info.topTrackPlays)
                {
                    info.topTrackPlays = e.playcount;
                    info.topTrackCrc = e.track_crc;
                }
            }

            // Sort by total playcount descending
            std::vector<std::pair<std::string, ArtistInfo>> artistVec;
            for (const auto &[artist, info] : artistMap)
            {
                artistVec.push_back({artist, info});
            }
            std::sort(artistVec.begin(), artistVec.end(),
                      [](const auto &a, const auto &b)
                      { return a.second.totalPlays > b.second.totalPlays; });

            // Display top 10 artists
            if (!artistVec.empty())
            {
                auto rankingDiv = container.append_child("div");
                rankingDiv.append_attribute("class") = "artist-ranking";

                auto h2 = rankingDiv.append_child("h2");
                h2.text().set("Top Artists");

                // Use table for reliable single-row horizontal layout
                auto table = rankingDiv.append_child("table");
                table.append_attribute("class") = "artist-table";
                auto tbody = table.append_child("tbody");
                auto tr = tbody.append_child("tr");

                int count = 0;
                for (const auto &[artist, info] : artistVec)
                {
                    if (++count > 10)
                        break;

                    auto td = tr.append_child("td");
                    td.append_attribute("class") = "artist-cell";

                    // Album art (circular avatar)
                    auto it = artMap.find(info.topTrackCrc);
                    if (it != artMap.end())
                    {
                        auto img = td.append_child("img");
                        img.append_attribute("class") = "artist-avatar";
                        img.append_attribute("src") = it->second.c_str();
                        img.append_attribute("alt") = artist.c_str();
                        img.append_attribute("loading") = "lazy";
                    }
                    else
                    {
                        auto placeholder = td.append_child("div");
                        placeholder.append_attribute("class") = "artist-avatar";
                        placeholder.append_attribute("style") = "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);";
                    }

                    auto nameDiv = td.append_child("div");
                    nameDiv.append_attribute("class") = "artist-name";
                    nameDiv.text().set(artist.c_str());

                    auto playsDiv = td.append_child("div");
                    playsDiv.append_attribute("class") = "artist-plays";
                    std::string playsText = std::to_string(info.totalPlays) + " plays";
                    playsDiv.text().set(playsText.c_str());
                }
            }
        }

        // Grid
        auto grid = container.append_child("div");
        grid.append_attribute("class") = "grid";

        int rank = 1;
        for (const auto &e : entries)
        {
            // Card
            auto card = grid.append_child("div");
            card.append_attribute("class") = "card";

            // Art container
            auto artContainer = card.append_child("div");
            artContainer.append_attribute("class") = "art-container";

            // Album art image
            auto it = artMap.find(e.track_crc);
            if (it != artMap.end())
            {
                auto img = artContainer.append_child("img");
                img.append_attribute("class") = "art-img";
                img.append_attribute("src") = it->second.c_str();
                img.append_attribute("alt") = e.album.c_str();
                img.append_attribute("loading") = "lazy";
            }

            // Rank badge
            auto badge = artContainer.append_child("div");
            badge.append_attribute("class") = "rank-badge";
            badge.text().set(std::to_string(rank++).c_str());

            // Info section
            auto info = card.append_child("div");
            info.append_attribute("class") = "info";

            // Title
            auto title = info.append_child("div");
            title.append_attribute("class") = "title";
            title.text().set(e.title.c_str());

            // Artist
            auto artist = info.append_child("div");
            artist.append_attribute("class") = "artist";
            artist.text().set(e.artist.c_str());

            // Album
            auto album = info.append_child("div");
            album.append_attribute("class") = "album";
            album.text().set(e.album.c_str());

            // Stats section
            auto stats = info.append_child("div");
            stats.append_attribute("class") = "stats";

            // Plays
            auto plays = stats.append_child("div");
            plays.append_attribute("class") = "plays";
            std::string playsText = "\xe2\x96\xb6 " + std::to_string(e.playcount) + " plays";
            plays.text().set(playsText.c_str());

            // Delta
            int64_t delta = e.playcount - e.prev_playcount;
            std::string deltaStr = (delta >= 0 ? "+" : "") + std::to_string(delta);
            auto deltaDiv = stats.append_child("div");
            std::string deltaClass = delta >= 0 ? "delta positive" : "delta negative";
            deltaDiv.append_attribute("class") = deltaClass.c_str();
            deltaDiv.text().set(deltaStr.c_str());
        }

        // Save via pugixml's save_file (use wide string to avoid encoding issues)
        bool ok = doc.save_file(htmlPath.c_str(), "  ", pugi::format_default | pugi::format_write_bom, pugi::encoding_utf8);
        if (!ok)
        {
            std::string errPath = pfc::stringcvt::string_utf8_from_wide(htmlPath.c_str());
            return "Failed to write HTML file: " + errPath;
        }
        return "";
    }

    // ---------------------------------------------------------------------------
    // PNG via chrome-headless
    // ---------------------------------------------------------------------------
    int ReportExporter::getPageHeight(
        const std::string &chromePath,
        const std::wstring &htmlPath)
    {
        if (chromePath.empty())
            return 10000; // Default fallback height

        std::wstring fileUrl = L"file:///";
        {
            std::wstring fwd = htmlPath;
            for (auto &c : fwd)
                if (c == L'\\')
                    c = L'/';
            fileUrl += fwd;
        }

        std::wstring wChrome = pfc::stringcvt::string_wide_from_utf8(chromePath.c_str());

        // Use --dump-dom to get HTML with updated title containing height
        std::wstring tempOut = htmlPath + L".dump";
        std::wstring wCmd = L"\"" + wChrome + L"\""
                                              L" --headless --disable-gpu"
                                              L" --dump-dom"
                                              L" --virtual-time-budget=10000"
                                              L" --run-all-compositor-stages-before-draw"
                                              L" \"" +
                            fileUrl + L"\" > \"" + tempOut + L"\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        // Use cmd.exe to handle output redirection
        std::wstring cmdLine = L"cmd.exe /C \"" + wCmd + L"\"";
        if (!CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            return 10000; // Fallback on error
        }

        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Read dumped HTML and extract height from title
        int height = 10000; // Default
        HANDLE hFile = CreateFileW(tempOut.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD fileSize = GetFileSize(hFile, nullptr);
            if (fileSize > 0 && fileSize < 50 * 1024 * 1024) // Max 50MB
            {
                std::vector<char> buffer(fileSize + 1);
                DWORD bytesRead = 0;
                if (ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr))
                {
                    buffer[bytesRead] = '\0';
                    std::string html(buffer.data());

                    // Extract height from <title>HEIGHT:12345</title>
                    size_t pos = html.find("<title>HEIGHT:");
                    if (pos != std::string::npos)
                    {
                        pos += 14; // Length of "<title>HEIGHT:"
                        size_t endPos = html.find("</title>", pos);
                        if (endPos != std::string::npos)
                        {
                            std::string heightStr = html.substr(pos, endPos - pos);
                            try
                            {
                                int parsedHeight = std::stoi(heightStr);
                                if (parsedHeight > 0 && parsedHeight < 50000)
                                {
                                    height = parsedHeight + 50; // Add small padding
                                }
                            }
                            catch (...)
                            {
                                // Keep default
                            }
                        }
                    }
                }
            }
            CloseHandle(hFile);
            DeleteFileW(tempOut.c_str()); // Clean up temp file
        }

        return height;
    }

    std::string ReportExporter::exportPng(
        const std::string &chromePath,
        const std::wstring &htmlPath,
        const std::wstring &pngPath)
    {
        if (chromePath.empty())
            return "chrome-headless.exe path is not configured in Preferences.";

        // Get actual page height dynamically
        int pageHeight = getPageHeight(chromePath, htmlPath);

        // 3508px width (A4 landscape @ 300dpi), dynamic height for full page capture
        std::wstring fileUrl = L"file:///";
        {
            // Convert backslashes
            std::wstring fwd = htmlPath;
            for (auto &c : fwd)
                if (c == L'\\')
                    c = L'/';
            fileUrl += fwd;
        }

        std::wstring wChrome = pfc::stringcvt::string_wide_from_utf8(chromePath.c_str());
        std::wstring windowSize = L"--window-size=3508," + std::to_wstring(pageHeight);
        std::wstring wCmd = L"\"" + wChrome + L"\""
                                              L" --headless --disable-gpu"
                                              L" --run-all-compositor-stages-before-draw"
                                              L" --virtual-time-budget=10000"
                                              L" --screenshot=\"" +
                            pngPath + L"\""
                                      L" " +
                            windowSize +
                            L" \"" + fileUrl + L"\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, &wCmd[0], nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            DWORD err = GetLastError();
            return "CreateProcess failed (error " + std::to_string(err) + ")";
        }

        WaitForSingleObject(pi.hProcess, 120000); // wait up to 120 s for large pages
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0)
            return "chrome-headless exited with code " + std::to_string(exitCode);
        return "";
    }

} // namespace fms
