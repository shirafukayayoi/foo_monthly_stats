#pragma once
#include "stdafx.h"
#include "db_manager.h"

namespace fms
{

    // report_exporter.h
    // Generates an HTML report (with Bootstrap 5) from monthly data,
    // and optionally a PNG screenshot using chrome-headless.exe.

    class ReportExporter
    {
    public:
        // Collect album art for each entry via foobar2000's album_art_manager_v2.
        // Returns a map of track_crc -> "data:image/jpeg;base64,..." string.
        // Must be called on the main (UI) thread.
        static std::map<std::string, std::string> collectArt(
            const std::vector<MonthlyEntry> &entries);

        // Export HTML to the given path and optionally create PNG via Chrome headless.
        // artMap: optional map of track_crc -> base64 JPEG data URI for album art thumbnails.
        // Returns an empty string on success, or an error message on failure.
        static std::string exportHtml(
            const std::string &periodLabel,
            const std::vector<MonthlyEntry> &entries,
            const std::wstring &htmlPath,
            const std::map<std::string, std::string> &artMap = {});

        // Launch chrome-headless.exe to convert htmlPath → pngPath.
        // chromePath: full path to chrome-headless.exe (may be empty = return error string)
        static std::string exportPng(
            const std::string &chromePath,
            const std::wstring &htmlPath,
            const std::wstring &pngPath);
    };

} // namespace fms
