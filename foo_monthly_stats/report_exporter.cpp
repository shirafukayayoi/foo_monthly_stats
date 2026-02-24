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

            for (const auto &e : entries)
            {
                metadb_handle_ptr h;

                // If path is available, use it directly
                if (!e.path.empty())
                {
                    try
                    {
                        h = metadb::get()->handle_create(e.path.c_str(), 0);
                    }
                    catch (...)
                    {
                        console::printf("[fms] Failed to create handle from path: %s", e.path.c_str());
                    }
                }

                // If no path or handle creation failed, search library by metadata
                if (!h.is_valid())
                {
                    console::printf("[fms] Searching library for: %s - %s - %s",
                                    e.title.c_str(), e.artist.c_str(), e.album.c_str());

                    try
                    {
                        metadb_handle_list allItems;
                        lib->get_all_items(allItems);

                        // Search for matching track
                        for (t_size i = 0; i < allItems.get_count(); ++i)
                        {
                            metadb_handle_ptr item = allItems[i];
                            file_info_impl fi;
                            if (item->get_info(fi))
                            {
                                const char *title = fi.meta_get("TITLE", 0);
                                const char *artist = fi.meta_get("ARTIST", 0);
                                const char *album = fi.meta_get("ALBUM", 0);

                                if (title && artist && album &&
                                    e.title == title &&
                                    e.artist == artist &&
                                    e.album == album)
                                {
                                    h = item;
                                    console::printf("[fms] Found in library: %s", item->get_path());
                                    break;
                                }
                            }
                        }

                        if (!h.is_valid())
                        {
                            console::printf("[fms] Track not found in library");
                            continue;
                        }
                    }
                    catch (...)
                    {
                        console::printf("[fms] Exception searching library");
                        continue;
                    }
                }

                // Now we have a valid handle, try to get album art
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
                        }
                        else
                        {
                            console::printf("[fms] No album art data for: %s", e.title.c_str());
                        }
                    }
                    else
                    {
                        console::printf("[fms] Failed to open album art instance for: %s", e.title.c_str());
                    }
                }
                catch (const std::exception &ex)
                {
                    console::printf("[fms] Exception getting art for %s: %s", e.title.c_str(), ex.what());
                }
                catch (...)
                {
                    console::printf("[fms] Unknown exception getting art for: %s", e.title.c_str());
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

    // ---------------------------------------------------------------------------
    // HTML generation using pugixml
    // ---------------------------------------------------------------------------
    std::string ReportExporter::exportHtml(
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
            meta.append_attribute("content") = "width=device-width, initial-scale=1";
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
    min-height: 100vh;
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
    max-width: 100%;
    box-shadow: 0 4px 12px rgba(0,0,0,0.1);
}
.artist-ranking h2 {
    font-size: 1.5rem;
    font-weight: 600;
    color: white;
    margin-bottom: 1.5rem;
    text-align: center;
}
.artist-list {
    display: flex;
    gap: 2rem;
    overflow-x: auto;
    padding: 0.5rem;
    scrollbar-width: thin;
    scrollbar-color: rgba(255,255,255,0.3) transparent;
}
.artist-list::-webkit-scrollbar {
    height: 8px;
}
.artist-list::-webkit-scrollbar-track {
    background: rgba(255,255,255,0.1);
    border-radius: 4px;
}
.artist-list::-webkit-scrollbar-thumb {
    background: rgba(255,255,255,0.3);
    border-radius: 4px;
}
.artist-item {
    display: flex;
    flex-direction: column;
    align-items: center;
    min-width: 140px;
    text-align: center;
}
.artist-avatar {
    width: 140px;
    height: 140px;
    border-radius: 50%;
    object-fit: cover;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    margin-bottom: 0.75rem;
    border: 3px solid rgba(255,255,255,0.5);
    transition: transform 0.3s ease;
}
.artist-avatar:hover {
    transform: scale(1.05);
}
.artist-name {
    font-weight: 600;
    color: white;
    font-size: 0.9rem;
    margin-bottom: 0.25rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 140px;
}
.artist-plays {
    font-weight: 500;
    color: rgba(255,255,255,0.8);
    font-size: 0.85rem;
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
@media (max-width: 768px) {
    body { padding: 1.5rem 1rem; }
    h1 { font-size: 2rem; margin-bottom: 1.5rem; }
    .grid { grid-template-columns: 1fr; }
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

        // Total playback time
        {
            double totalSeconds = 0.0;
            for (const auto &e : entries)
            {
                totalSeconds += e.length_seconds * e.playcount;
            }

            int hours = static_cast<int>(totalSeconds / 3600);
            int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);

            auto stats = container.append_child("div");
            stats.append_attribute("style") = "text-align: center; margin: 1.5rem 0; font-size: 1.1rem; color: rgba(255,255,255,0.9);";

            std::string timeText = "Total Listening Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m";
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

                auto artistList = rankingDiv.append_child("div");
                artistList.append_attribute("class") = "artist-list";

                int count = 0;
                for (const auto &[artist, info] : artistVec)
                {
                    if (++count > 10)
                        break;

                    auto item = artistList.append_child("div");
                    item.append_attribute("class") = "artist-item";

                    // Album art (circular avatar)
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
                        // Placeholder for missing art
                        auto placeholder = item.append_child("div");
                        placeholder.append_attribute("class") = "artist-avatar";
                        placeholder.append_attribute("style") = "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);";
                    }

                    auto nameDiv = item.append_child("div");
                    nameDiv.append_attribute("class") = "artist-name";
                    nameDiv.text().set(artist.c_str());

                    auto playsDiv = item.append_child("div");
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
    std::string ReportExporter::exportPng(
        const std::string &chromePath,
        const std::wstring &htmlPath,
        const std::wstring &pngPath)
    {
        if (chromePath.empty())
            return "chrome-headless.exe path is not configured in Preferences.";

        // A4 landscape 300dpi = 3508×2480 px
        // chrome-headless --headless --disable-gpu --screenshot=<out> --window-size=3508,2480 file:///<in>
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
        std::wstring wCmd = L"\"" + wChrome + L"\""
                                              L" --headless --disable-gpu"
                                              L" --screenshot=\"" +
                            pngPath + L"\""
                                      L" --window-size=3508,2480"
                                      L" \"" +
                            fileUrl + L"\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, &wCmd[0], nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            DWORD err = GetLastError();
            return "CreateProcess failed (error " + std::to_string(err) + ")";
        }

        WaitForSingleObject(pi.hProcess, 60000); // wait up to 60 s
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0)
            return "chrome-headless exited with code " + std::to_string(exitCode);
        return "";
    }

} // namespace fms
