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

        // Bootstrap 5 CDN
        {
            auto link = head.append_child("link");
            link.append_attribute("rel") = "stylesheet";
            link.append_attribute("href") =
                "https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css";
        }

        // <body>
        auto body = html.append_child("body");
        auto container = body.append_child("div");
        container.append_attribute("class") = "container py-4";

        // Title
        {
            auto h1 = container.append_child("h1");
            h1.append_attribute("class") = "mb-4";
            h1.text().set(("Monthly Stats – " + ym).c_str());
        }

        // Table
        auto table = container.append_child("table");
        table.append_attribute("class") = "table table-striped table-hover align-middle";

        // thead
        auto thead = table.append_child("thead");
        auto hrow = thead.append_child("tr");
        const char *colNames[] = {"Art", "#", "Title", "Artist", "Album", "Plays", "\xce\x94"};
        for (const char *col : colNames)
        {
            auto th = hrow.append_child("th");
            th.text().set(col);
        }

        // tbody
        auto tbody = table.append_child("tbody");
        int rank = 1;
        for (const auto &e : entries)
        {
            auto tr = tbody.append_child("tr");

            // Album art cell
            auto artTd = tr.append_child("td");
            auto it = artMap.find(e.track_crc);
            if (it != artMap.end())
            {
                auto img = artTd.append_child("img");
                img.append_attribute("src") = it->second.c_str();
                img.append_attribute("style") = "width:60px;height:60px;object-fit:cover;border-radius:4px;";
                img.append_attribute("loading") = "lazy";
            }

            tr.append_child("td").text().set(std::to_string(rank++).c_str());
            tr.append_child("td").text().set(e.title.c_str());
            tr.append_child("td").text().set(e.artist.c_str());
            tr.append_child("td").text().set(e.album.c_str());
            tr.append_child("td").text().set(std::to_string(e.playcount).c_str());

            int64_t delta = e.playcount - e.prev_playcount;
            std::string deltaStr = (delta >= 0 ? "+" : "") + std::to_string(delta);
            auto td = tr.append_child("td");
            td.text().set(deltaStr.c_str());
            td.append_attribute("class") = (delta >= 0) ? "text-success" : "text-danger";
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
