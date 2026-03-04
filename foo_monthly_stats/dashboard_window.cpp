#include "stdafx.h"
#include "dashboard_window.h"
#include "report_exporter.h"
#include "preferences.h"
#include "resource.h"
#include "i18n.h"

namespace fms
{
    // ---------------------------------------------------------------------------
    // Playback callback for auto-refresh
    // ---------------------------------------------------------------------------
    class DashboardWindow::PlaybackCallbackImpl : public play_callback_impl_base
    {
    public:
        explicit PlaybackCallbackImpl(DashboardWindow *owner)
            : play_callback_impl_base(flag_on_playback_stop), m_owner(owner)
        {
        }

        void on_playback_stop(play_control::t_stop_reason reason) override
        {
            // Auto-refresh dashboard when playback stops
            // Use a timer to delay the update, allowing async DB write to complete
            if (m_owner && m_owner->IsWindow())
            {
                m_owner->SetTimer(1, 200); // 200ms delay
            }
        }

    private:
        DashboardWindow *m_owner;
    };

    DashboardWindow *DashboardWindow::s_instance = nullptr;

    // ---------------------------------------------------------------------------
    // Open / Close
    // ---------------------------------------------------------------------------
    void DashboardWindow::Open()
    {
        if (s_instance && s_instance->IsWindow())
        {
            ::SetForegroundWindow(s_instance->m_hWnd);
            return;
        }
        delete s_instance;
        s_instance = new DashboardWindow();
        s_instance->Create(core_api::get_main_window());
        s_instance->ShowWindow(SW_SHOW);
    }

    void DashboardWindow::Close()
    {
        if (s_instance && s_instance->IsWindow())
            s_instance->DestroyWindow();
    }

    // ---------------------------------------------------------------------------
    // Window messages
    // ---------------------------------------------------------------------------
    BOOL DashboardWindow::OnInitDialog(CWindow, LPARAM)
    {
        m_viewMode = MONTH;
        m_period = DbManager::currentYM();
        SetDlgItemTextA(m_hWnd, IDC_BTN_MODE_TOGGLE, "Month");
        SetupListColumns();
        UpdatePeriodLabel();
        UpdateExportFormatButton();
        Populate();

        // Register playback callback for auto-refresh
        m_playback_callback = std::make_unique<PlaybackCallbackImpl>(this);

        return TRUE;
    }

    void DashboardWindow::OnClose()
    {
        // Modeless dialog: X button sends WM_CLOSE → destroy the window properly
        DestroyWindow();
    }

    void DashboardWindow::OnDestroy()
    {
        KillTimer(1); // Stop any pending timer
        KillTimer(2); // Stop export status restoration timer
        KillTimer(3); // Stop export format toggle status restoration timer
        s_instance = nullptr;
    }

    void DashboardWindow::OnTimer(UINT_PTR nIDEvent)
    {
        if (nIDEvent == 1)
        {
            KillTimer(1);
            Populate(); // Refresh after DB write completes
        }
        else if (nIDEvent == 2)
        {
            KillTimer(2);
            Populate(); // Restore normal status display (tracks count and listening time)
        }
        else if (nIDEvent == 3)
        {
            KillTimer(3);
            Populate(); // Restore normal status display after export format toggle
        }
    }

    void DashboardWindow::OnModeToggle(UINT, int, CWindow)
    {
        if (m_viewMode == MONTH)
        {
            // Switch to Year mode
            m_viewMode = YEAR;
            m_period = m_period.substr(0, 4); // "2026-02" -> "2026"
        }
        else if (m_viewMode == YEAR)
        {
            // Switch to Day mode
            m_viewMode = DAY;
            m_period = DbManager::currentYMD(); // Get today's date
        }
        else // DAY
        {
            // Switch to Month mode
            m_viewMode = MONTH;
            m_period = m_period.substr(0, 7); // "2026-03-04" -> "2026-03"
        }
        // Update button text based on current mode
        const char *btnText;
        if (m_viewMode == MONTH)
            btnText = "Month";
        else if (m_viewMode == YEAR)
            btnText = "Year";
        else
            btnText = "Day";
        SetDlgItemTextA(m_hWnd, IDC_BTN_MODE_TOGGLE, btnText);

        UpdatePeriodLabel();
        SetupListColumns(); // Update column header (先月比 ↔ 昨日比 ↔ 前年比)
        Populate();
    }

    void DashboardWindow::OnPrev(UINT, int, CWindow)
    {
        if (m_viewMode == MONTH)
        {
            int year = std::stoi(m_period.substr(0, 4));
            int month = std::stoi(m_period.substr(5, 2));
            if (--month == 0)
            {
                month = 12;
                --year;
            }
            char buf[8];
            snprintf(buf, sizeof(buf), "%04d-%02d", year, month);
            m_period = buf;
        }
        else if (m_viewMode == DAY)
        {
            int year = std::stoi(m_period.substr(0, 4));
            int month = std::stoi(m_period.substr(5, 2));
            int day = std::stoi(m_period.substr(8, 2));
            day--;
            if (day == 0)
            {
                month--;
                if (month == 0)
                {
                    month = 12;
                    year--;
                }
                // Last day of previous month
                static const int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
                day = daysInMonth[month] + (month == 2 ? leap : 0);
            }
            char buf[11];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
            m_period = buf;
        }
        else // YEAR
        {
            int year = std::stoi(m_period);
            --year;
            m_period = std::to_string(year);
        }
        UpdatePeriodLabel();
        Populate();
    }

    void DashboardWindow::OnNext(UINT, int, CWindow)
    {
        if (m_viewMode == MONTH)
        {
            int year = std::stoi(m_period.substr(0, 4));
            int month = std::stoi(m_period.substr(5, 2));
            if (++month == 13)
            {
                month = 1;
                ++year;
            }
            char buf[8];
            snprintf(buf, sizeof(buf), "%04d-%02d", year, month);
            m_period = buf;
        }
        else if (m_viewMode == DAY)
        {
            int year = std::stoi(m_period.substr(0, 4));
            int month = std::stoi(m_period.substr(5, 2));
            int day = std::stoi(m_period.substr(8, 2));
            day++;
            // Simple month lengths (leap year not perfectly handled for edge cases)
            static const int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
            int maxDay = daysInMonth[month] + (month == 2 ? leap : 0);
            if (day > maxDay)
            {
                day = 1;
                month++;
                if (month > 12)
                {
                    month = 1;
                    year++;
                }
            }
            char buf[11];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
            m_period = buf;
        }
        else // YEAR
        {
            int year = std::stoi(m_period);
            ++year;
            m_period = std::to_string(year);
        }
        UpdatePeriodLabel();
        Populate();
    }

    void DashboardWindow::OnDelete(UINT, int, CWindow)
    {
        HWND hList = GetDlgItem(IDC_LIST_TRACKS);
        if (!hList)
            return;

        // Collect all selected items
        std::vector<int> selectedIndices;
        int idx = -1;
        while ((idx = ListView_GetNextItem(hList, idx, LVNI_SELECTED)) != -1)
        {
            selectedIndices.push_back(idx);
        }

        if (selectedIndices.empty())
            return;

        // Delete from database (in reverse order to maintain indices)
        for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it)
        {
            int index = *it;
            if (index >= 0 && index < static_cast<int>(m_entries.size()))
            {
                const auto &entry = m_entries[index];
                DbManager::get().deleteEntry(entry.ymd, entry.track_crc);
            }
        }

        // Refresh the display
        Populate();
    }

    void DashboardWindow::OnReset(UINT, int, CWindow)
    {
        // Recalculate this period from play_log
        DbManager::get().refreshPeriod(m_period, m_viewMode == YEAR);
        Populate();
    }

    void DashboardWindow::UpdateExportFormatButton()
    {
        HWND hBtn = GetDlgItem(IDC_BTN_EXPORT_FORMAT);
        if (hBtn)
        {
            const char *btnText = m_exportFormatIsSmartphone ? "Export: Smartphone (1080x1980)" : "Export: Desktop";
            SetDlgItemTextA(m_hWnd, IDC_BTN_EXPORT_FORMAT, btnText);
        }
    }

    void DashboardWindow::OnToggleExportFormat(UINT, int, CWindow)
    {
        // Toggle between Desktop and Smartphone format
        m_exportFormatIsSmartphone = !m_exportFormatIsSmartphone;
        UpdateExportFormatButton();

        // Update status message
        const char *statusMsg = m_exportFormatIsSmartphone
                                    ? "Smartphone (1080x1980px, Top 5/10)"
                                    : "Desktop (full)";
        SetStatus(statusMsg);

        // Schedule restoration of normal status display after 3 seconds
        SetTimer(3, 3000);
    }

    void DashboardWindow::OnExport(UINT, int, CWindow)
    {
        // Use the pre-selected format (m_exportFormatIsSmartphone)

        // Ask user for save location - use Downloads folder as default
        wchar_t htmlBuf[MAX_PATH] = {};

        // Get Downloads folder path using SHGetKnownFolderPath
        PWSTR pDownloadPath = nullptr;
        std::wstring initialPath;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &pDownloadPath)))
        {
            initialPath = pDownloadPath;
            CoTaskMemFree(pDownloadPath);
            initialPath += L"\\";
        }

        // Append default filename (with _smartphone suffix if applicable)
        std::string filenameSuffix = m_viewMode == MONTH ? m_period : ("year_" + m_period);
        if (m_exportFormatIsSmartphone)
            filenameSuffix += "_smartphone";
        std::wstring defaultName = pfc::stringcvt::string_wide_from_utf8(
            ("report_" + filenameSuffix + ".html").c_str());
        initialPath += defaultName;
        wcsncpy_s(htmlBuf, MAX_PATH, initialPath.c_str(), _TRUNCATE);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hWnd;
        ofn.lpstrFilter = L"HTML File\0*.html\0";
        ofn.lpstrFile = htmlBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"html";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn))
            return;

        std::wstring htmlPath = htmlBuf;

        // Collect album art for each entry (runs on main thread, SDK access allowed)
        std::map<std::string, std::string> artMap = ReportExporter::collectArt(m_entries);

        // Generate HTML (Desktop or Smartphone format)
        std::string periodLabel = m_viewMode == MONTH ? ("Monthly Stats – " + m_period) : ("Yearly Stats – " + m_period);
        std::string err = ReportExporter::exportHtml(periodLabel, m_entries, htmlPath, artMap, m_exportFormatIsSmartphone);
        if (!err.empty())
        {
            SetStatus(err.c_str());
            return;
        }

        // PNG generation disabled (HTML only)
        // pfc::string8 chromePath = g_cfg_chrome_path.get();
        // if (!chromePath.is_empty())
        // {
        //     std::wstring pngPath = htmlPath.substr(0, htmlPath.rfind(L'.')) + L".png";
        //     err = ReportExporter::exportPng(chromePath.c_str(), htmlPath, pngPath);
        //     if (!err.empty())
        //     {
        //         SetStatus(err.c_str());
        //         return;
        //     }
        // }

        // Calculate total time for export confirmation message
        double totalSeconds = 0.0;
        for (const auto &e : m_entries)
        {
            totalSeconds += e.total_time_seconds;
        }
        int hours = static_cast<int>(totalSeconds / 3600);
        int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);
        int seconds = static_cast<int>(totalSeconds - hours * 3600 - minutes * 60);

        std::string formatStr = m_exportFormatIsSmartphone ? " (Smartphone format)" : " (Desktop format)";
        std::string exportMsg = "Export succeeded." + formatStr + " (" + std::to_string(m_entries.size()) +
                                " tracks, " + std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(seconds) + "s)";
        SetStatus(exportMsg.c_str());

        // Schedule restoration of normal status display after 3 seconds
        SetTimer(2, 3000);
    }
    void DashboardWindow::OnPreferences(UINT, int, CWindow)
    {
        // Open preferences page via foobar2000 API
        static const GUID guid_prefs = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x02}};
        static_api_ptr_t<ui_control>()->show_preferences(guid_prefs);
    }

    LRESULT DashboardWindow::OnColumnClick(LPNMHDR pnmh)
    {
        auto *pnmlv = reinterpret_cast<NMLISTVIEW *>(pnmh);
        if (pnmlv->iSubItem == m_sortCol)
            m_sortAsc = !m_sortAsc;
        else
        {
            m_sortCol = pnmlv->iSubItem;
            m_sortAsc = false;
        }

        auto &e = m_entries;
        std::stable_sort(e.begin(), e.end(), [&](const MonthlyEntry &a, const MonthlyEntry &b)
                         {
        bool lt = false;
        switch (m_sortCol) {
            case 0: lt = false; break;                    // rank – keep order
            case 1: lt = a.title  < b.title;  break;
            case 2: lt = a.artist < b.artist; break;
            case 3: lt = a.album  < b.album;  break;
            case 4: lt = a.playcount < b.playcount; break;
            case 5: lt = (a.playcount - a.prev_playcount) < (b.playcount - b.prev_playcount); break;
            default: break;
        }
        return m_sortAsc ? lt : !lt; });

        HWND hList = GetDlgItem(IDC_LIST_TRACKS);
        ListView_DeleteAllItems(hList);
        int i = 0;
        for (auto &entry : m_entries)
        {
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i++;
            std::wstring rank = std::to_wstring(i);
            lvi.pszText = &rank[0];
            ListView_InsertItem(hList, &lvi);

            auto setCol = [&](int col, const std::string &txt)
            {
                std::wstring w = pfc::stringcvt::string_wide_from_utf8(txt.c_str());
                ListView_SetItemText(hList, lvi.iItem, col, &w[0]);
            };
            std::string title_artist = entry.title + " – " + entry.artist;
            setCol(1, title_artist);
            setCol(2, entry.artist);
            setCol(3, entry.album);
            setCol(4, std::to_string(entry.playcount));
            int64_t delta = entry.playcount - entry.prev_playcount;
            setCol(5, (delta >= 0 ? "+" : "") + std::to_string(delta));
        }
        return 0;
    }

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------
    void DashboardWindow::SetupListColumns()
    {
        HWND hList = GetDlgItem(IDC_LIST_TRACKS);

        // Clear existing columns
        while (ListView_DeleteColumn(hList, 0))
        {
        }

        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        struct Col
        {
            const wchar_t *name;
            int width;
        };

        // Adjust delta column label based on view mode
        const wchar_t *deltaLabel;
        if (m_viewMode == MONTH)
            deltaLabel = L"先月比"; // Month-over-Month
        else if (m_viewMode == DAY)
            deltaLabel = L"昨日比"; // Day-over-Day
        else                        // YEAR
            deltaLabel = L"前年比"; // Year-over-Year

        const Col cols[] = {
            {L"#", 40},
            {L"Title – Artist", 260},
            {L"Artist", 120},
            {L"Album", 120},
            {L"Plays", 60},
            {deltaLabel, 60},
        };
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        int idx = 0;
        for (auto &c : cols)
        {
            lvc.pszText = const_cast<wchar_t *>(c.name);
            lvc.cx = c.width;
            ListView_InsertColumn(hList, idx++, &lvc);
        }
    }

    void DashboardWindow::Populate()
    {
        if (m_viewMode == MONTH)
            m_entries = DbManager::get().queryMonth(m_period);
        else if (m_viewMode == DAY)
            m_entries = DbManager::get().queryDay(m_period);
        else // YEAR
            m_entries = DbManager::get().queryYear(m_period);

        // Sort by playcount desc initially
        std::sort(m_entries.begin(), m_entries.end(), [](const MonthlyEntry &a, const MonthlyEntry &b)
                  { return a.playcount > b.playcount; });

        HWND hList = GetDlgItem(IDC_LIST_TRACKS);
        ListView_DeleteAllItems(hList);

        int i = 0;
        for (auto &e : m_entries)
        {
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            std::wstring rank = std::to_wstring(i + 1);
            lvi.pszText = &rank[0];
            ListView_InsertItem(hList, &lvi);

            auto setCol = [&](int col, const std::string &txt)
            {
                std::wstring w = pfc::stringcvt::string_wide_from_utf8(txt.c_str());
                ListView_SetItemText(hList, lvi.iItem, col, &w[0]);
            };
            std::string title_artist = e.title + " – " + e.artist;
            setCol(1, title_artist);
            setCol(2, e.artist);
            setCol(3, e.album);
            setCol(4, std::to_string(e.playcount));
            int64_t delta = e.playcount - e.prev_playcount;
            setCol(5, (delta >= 0 ? "+" : "") + std::to_string(delta));
            ++i;
        }

        // Calculate and display total listening time
        double totalSeconds = 0.0;
        for (const auto &e : m_entries)
        {
            totalSeconds += e.total_time_seconds;
        }
        int hours = static_cast<int>(totalSeconds / 3600);
        int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);
        int seconds = static_cast<int>(totalSeconds - hours * 3600 - minutes * 60);

        std::string statusText = "Tracks: " + std::to_string(m_entries.size()) +
                                 " | Total Listening Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
        SetStatus(statusText.c_str());
    }

    void DashboardWindow::UpdatePeriodLabel()
    {
        SetDlgItemTextA(m_hWnd, IDC_STATIC, m_period.c_str());
    }

    void DashboardWindow::SetStatus(const char *msg)
    {
        SetDlgItemTextA(m_hWnd, IDC_STATIC_STATUS, msg);
    }

    // ---------------------------------------------------------------------------
    // Main menu command "View > Monthly Stats..."
    // ---------------------------------------------------------------------------
    static const GUID guid_mainmenu_group = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x03}};
    static const GUID guid_cmd_open_stats = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x04}};

    class FmsMainMenuCmd : public mainmenu_commands
    {
    public:
        t_uint32 get_command_count() override { return 1; }
        GUID get_command(t_uint32) override { return guid_cmd_open_stats; }
        void get_name(t_uint32, pfc::string_base &out) override { out = "Monthly Stats..."; }
        bool get_description(t_uint32, pfc::string_base &out) override
        {
            out = "Open the Monthly Stats dashboard window.";
            return true;
        }
        bool get_display(t_uint32 idx, pfc::string_base &out, t_uint32 &flags) override
        {
            get_name(idx, out);
            flags = 0;
            return true;
        }
        GUID get_parent() override { return mainmenu_groups::view; }
        void execute(t_uint32, mainmenu_commands::ctx_t) override
        {
            DashboardWindow::Open();
        }
    };

    static mainmenu_commands_factory_t<FmsMainMenuCmd> g_mainMenuCmd;

} // namespace fms
