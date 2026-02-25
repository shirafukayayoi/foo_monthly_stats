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
        SetupListColumns();
        UpdatePeriodLabel();
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
        s_instance = nullptr;
    }

    void DashboardWindow::OnTimer(UINT_PTR nIDEvent)
    {
        if (nIDEvent == 1)
        {
            KillTimer(1);
            Populate(); // Refresh after DB write completes
        }
    }

    void DashboardWindow::OnSize(UINT, CSize sz)
    {
        HWND hList = GetDlgItem(IDC_LIST_TRACKS);
        if (!hList)
            return;
        // Resize list to fill most of the dialog
        RECT rcDlg;
        GetClientRect(&rcDlg);
        int btnBottom = 25;
        int statusH = 18;
        ::SetWindowPos(hList, nullptr,
                       7, 25,
                       rcDlg.right - 14,
                       rcDlg.bottom - 25 - statusH - 22,
                       SWP_NOZORDER);
        // Move bottom controls
        HWND hExport = GetDlgItem(IDC_BTN_EXPORT);
        HWND hPrefs = GetDlgItem(IDC_BTN_PREFERENCES);
        HWND hStatus = GetDlgItem(IDC_STATIC_STATUS);
        int y = rcDlg.bottom - 20;
        if (hExport)
            ::SetWindowPos(hExport, nullptr, 7, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        if (hPrefs)
            ::SetWindowPos(hPrefs, nullptr, 75, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        if (hStatus)
            ::SetWindowPos(hStatus, nullptr, 145, y + 2, rcDlg.right - 150, 14, SWP_NOZORDER);
    }

    void DashboardWindow::OnModeToggle(UINT, int, CWindow)
    {
        if (m_viewMode == MONTH)
        {
            // Switch to Year mode
            m_viewMode = YEAR;
            m_period = m_period.substr(0, 4); // "2026-02" -> "2026"
        }
        else
        {
            // Switch to Month mode
            m_viewMode = MONTH;
            m_period = m_period + "-" + DbManager::currentYM().substr(5, 2); // "2026" -> "2026-02"
        }
        UpdatePeriodLabel();
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
                DbManager::get().deleteEntry(entry.ym, entry.track_crc);
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

    void DashboardWindow::OnExport(UINT, int, CWindow)
    {
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

        // Append default filename
        std::string filenameSuffix = m_viewMode == MONTH ? m_period : ("year_" + m_period);
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

        // Generate HTML
        std::string periodLabel = m_viewMode == MONTH ? ("Monthly Stats – " + m_period) : ("Yearly Stats – " + m_period);
        std::string err = ReportExporter::exportHtml(periodLabel, m_entries, htmlPath, artMap);
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

        std::string exportMsg = "Export succeeded. (" + std::to_string(m_entries.size()) +
                                " tracks, " + std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(seconds) + "s)";
        SetStatus(exportMsg.c_str());
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
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        struct Col
        {
            const wchar_t *name;
            int width;
        };
        const Col cols[] = {
            {L"#", 40},
            {L"Title – Artist", 260},
            {L"Artist", 120},
            {L"Album", 120},
            {L"Plays", 60},
            {L"先月比", 60},
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
        m_entries = m_viewMode == MONTH
                        ? DbManager::get().queryMonth(m_period)
                        : DbManager::get().queryYear(m_period);
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
