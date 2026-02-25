#pragma once
#include "stdafx.h"
#include "resource.h"
#include "db_manager.h"

namespace fms
{

    // dashboard_window.h
    // Modeless dashboard window showing per-month play statistics.

    class DashboardWindow
        : public CDialogImpl<DashboardWindow>
    {
    public:
        enum
        {
            IDD = IDD_DASHBOARD
        };

        enum ViewMode
        {
            MONTH,
            YEAR
        };

        static void Open(); // Creates or activates the singleton window
        static void Close();

        BEGIN_MSG_MAP_EX(DashboardWindow)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MSG_WM_TIMER(OnTimer)
        COMMAND_HANDLER_EX(IDC_BTN_MODE_TOGGLE, BN_CLICKED, OnModeToggle)
        COMMAND_HANDLER_EX(IDC_BTN_PREV, BN_CLICKED, OnPrev)
        COMMAND_HANDLER_EX(IDC_BTN_NEXT, BN_CLICKED, OnNext)
        COMMAND_HANDLER_EX(IDC_BTN_DELETE, BN_CLICKED, OnDelete)
        COMMAND_HANDLER_EX(IDC_BTN_RESET, BN_CLICKED, OnReset)
        COMMAND_HANDLER_EX(IDC_BTN_EXPORT, BN_CLICKED, OnExport)
        COMMAND_HANDLER_EX(IDC_BTN_PREFERENCES, BN_CLICKED, OnPreferences)
        NOTIFY_HANDLER_EX(IDC_LIST_TRACKS, LVN_COLUMNCLICK, OnColumnClick)
        END_MSG_MAP()

    private:
        BOOL OnInitDialog(CWindow, LPARAM);
        void OnClose();
        void OnDestroy();
        void OnSize(UINT, CSize);
        void OnTimer(UINT_PTR nIDEvent);
        void OnModeToggle(UINT, int, CWindow);
        void OnPrev(UINT, int, CWindow);
        void OnNext(UINT, int, CWindow);
        void OnDelete(UINT, int, CWindow);
        void OnReset(UINT, int, CWindow);
        void OnExport(UINT, int, CWindow);
        void OnPreferences(UINT, int, CWindow);
        LRESULT OnColumnClick(LPNMHDR);

        void SetupListColumns();
        void Populate();
        void UpdatePeriodLabel();
        void SetStatus(const char *msg);

        ViewMode m_viewMode = MONTH;
        std::string m_period; // "YYYY-MM" or "YYYY"
        std::vector<MonthlyEntry> m_entries;
        int m_sortCol = 4; // default: sort by plays
        bool m_sortAsc = false;

        // Playback callback for auto-refresh
        class PlaybackCallbackImpl;
        std::unique_ptr<PlaybackCallbackImpl> m_playback_callback;

        static DashboardWindow *s_instance;
    };

} // namespace fms
