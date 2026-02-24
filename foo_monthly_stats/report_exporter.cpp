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
            abort_callback_dummy abort;
            for (const auto &e : entries)
            {
                if (e.path.empty())
                    continue;
                try
                {
                    metadb_handle_ptr h = metadb::get()->handle_create(e.path.c_str(), 0);
                    metadb_handle_list handles;
                    handles.add_item(h);
                    pfc::list_single_ref_t<GUID> types(album_art_ids::cover_front);
                    auto inst = aam->open(handles, types, abort);
                    album_art_data_ptr data = inst->query(album_art_ids::cover_front, abort);
                    if (data.is_valid() && data->get_size() > 0)
                    {
                        artMap[e.track_crc] = "data:image/jpeg;base64," +
                                              base64_encode(data->get_ptr(), data->get_size());
                    }
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
        return artMap;
    }

    // ---------------------------------------------------------------------------
    // HTML generation using pugixml
    // ---------------------------------------------------------------------------
    std::string ReportExporter::exportHtml(
        const std::string &ym,
        const std::vector<MonthlyEntry> &entries,
        const std::string &htmlPath,
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
        head.append_child("title").text().set(("Monthly Stats – " + ym).c_str());

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
    padding: 3rem 2rem;
    color: #1a1a1a;
}
.container {
    max-width: 1400px;
    margin: 0 auto;
}
h1 {
    font-size: 3rem;
    font-weight: 700;
    color: white;
    margin-bottom: 2.5rem;
    text-align: center;
    text-shadow: 0 2px 10px rgba(0,0,0,0.2);
}
.grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
    gap: 1.5rem;
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
    top: 12px;
    left: 12px;
    width: 36px;
    height: 36px;
    background: rgba(0,0,0,0.75);
    color: white;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-weight: 700;
    font-size: 0.875rem;
    backdrop-filter: blur(8px);
    z-index: 1;
}
.info {
    padding: 1.25rem;
}
.title {
    font-size: 1.125rem;
    font-weight: 600;
    color: #1a1a1a;
    margin-bottom: 0.375rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.artist {
    font-size: 0.9375rem;
    color: #666;
    margin-bottom: 0.25rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.album {
    font-size: 0.875rem;
    color: #999;
    margin-bottom: 1rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.stats {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding-top: 0.75rem;
    border-top: 1px solid #eee;
}
.plays {
    font-size: 0.9375rem;
    font-weight: 600;
    color: #667eea;
}
.delta {
    font-size: 0.875rem;
    font-weight: 600;
    padding: 0.25rem 0.625rem;
    border-radius: 12px;
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
            h1.text().set(("Monthly Stats – " + ym).c_str());
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

        // Save via pugixml's save_file
        bool ok = doc.save_file(htmlPath.c_str(), "  ", pugi::format_default | pugi::format_write_bom, pugi::encoding_utf8);
        if (!ok)
            return "Failed to write HTML file: " + htmlPath;
        return "";
    }

    // ---------------------------------------------------------------------------
    // PNG via chrome-headless
    // ---------------------------------------------------------------------------
    std::string ReportExporter::exportPng(
        const std::string &chromePath,
        const std::string &htmlPath,
        const std::string &pngPath)
    {
        if (chromePath.empty())
            return "chrome-headless.exe path is not configured in Preferences.";

        // A4 landscape 300dpi = 3508×2480 px
        // chrome-headless --headless --disable-gpu --screenshot=<out> --window-size=3508,2480 file:///<in>
        pfc::string8 fileUrl = "file:///";
        {
            // Convert backslashes
            std::string fwd = htmlPath;
            for (auto &c : fwd)
                if (c == '\\')
                    c = '/';
            fileUrl += fwd.c_str();
        }

        std::string cmdLine = "\"" + chromePath + "\""
                                                  " --headless --disable-gpu"
                                                  " --screenshot=\"" +
                              pngPath + "\""
                                        " --window-size=3508,2480"
                                        " \"" +
                              std::string(fileUrl.c_str()) + "\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::wstring wCmd = pfc::stringcvt::string_wide_from_utf8(cmdLine.c_str());
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
