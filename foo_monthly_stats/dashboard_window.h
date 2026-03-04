#pragma once
#include "stdafx.h"
#include "resource.h"
#include "db_manager.h"

namespace fms
{
    // Static resize parameters for dashboard dialog
    static const CDialogResizeHelper::Param dashboardResizeParams[] = {
        // Upper row: Mode, Prev, Period label, Next, Delete, Reset buttons
        {IDC_BTN_MODE_TOGGLE, 0, 0, 0, 0}, // fixed left-top
        {IDC_BTN_PREV, 0, 0, 0, 0},        // fixed left-top
        {IDC_STATIC, 0, 0, 0, 0},          // fixed width and position (do not expand)
        {IDC_BTN_NEXT, 0, 0, 0, 0},        // fixed left-top
        {IDC_BTN_DELETE, 1, 0, 1, 0},      // anchored to right, follows right edge
        {IDC_BTN_RESET, 1, 0, 1, 0},       // anchored to right, follows right edge
        // Main list view: expands in all directions
        {IDC_LIST_TRACKS, 0, 0, 1, 1}, // expands in all directions
        // Bottom row: All buttons and status fixed (no horizontal expansion)
        // Only anchored to bottom (snapBottom=1), positions and widths are fixed
        {IDC_BTN_EXPORT, 0, 1, 0, 1},        // fixed left, anchored to bottom
        {IDC_BTN_EXPORT_FORMAT, 0, 1, 0, 1}, // fixed position, anchored to bottom
        {IDC_BTN_PREFERENCES, 0, 1, 0, 1},   // fixed position, anchored to bottom
        {IDC_STATIC_STATUS, 0, 1, 0, 1},     // fixed position, anchored to bottom
    };

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
            YEAR,
            DAY
        };

        // Constructor: Initialize m_resizer with resize parameters
        DashboardWindow() : m_resizer(dashboardResizeParams, CRect(300, 150, 0, 0)) {}

        static void Open(); // Creates or activates the singleton window
        static void Close();

        BEGIN_MSG_MAP_EX(DashboardWindow)
        CHAIN_MSG_MAP_MEMBER(m_resizer)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_TIMER(OnTimer)
        COMMAND_HANDLER_EX(IDC_BTN_MODE_TOGGLE, BN_CLICKED, OnModeToggle)
        COMMAND_HANDLER_EX(IDC_BTN_PREV, BN_CLICKED, OnPrev)
        COMMAND_HANDLER_EX(IDC_BTN_NEXT, BN_CLICKED, OnNext)
        COMMAND_HANDLER_EX(IDC_BTN_DELETE, BN_CLICKED, OnDelete)
        COMMAND_HANDLER_EX(IDC_BTN_RESET, BN_CLICKED, OnReset)
        COMMAND_HANDLER_EX(IDC_BTN_EXPORT_FORMAT, BN_CLICKED, OnToggleExportFormat)
        COMMAND_HANDLER_EX(IDC_BTN_EXPORT, BN_CLICKED, OnExport)
        COMMAND_HANDLER_EX(IDC_BTN_PREFERENCES, BN_CLICKED, OnPreferences)
        NOTIFY_HANDLER_EX(IDC_LIST_TRACKS, LVN_COLUMNCLICK, OnColumnClick)
        END_MSG_MAP()

    private:
        BOOL OnInitDialog(CWindow, LPARAM);
        void OnClose();
        void OnDestroy();
        void OnTimer(UINT_PTR nIDEvent);
        void OnModeToggle(UINT, int, CWindow);
        void OnPrev(UINT, int, CWindow);
        void OnNext(UINT, int, CWindow);
        void OnDelete(UINT, int, CWindow);
        void OnReset(UINT, int, CWindow);
        void OnToggleExportFormat(UINT, int, CWindow);
        void OnExport(UINT, int, CWindow);
        void OnPreferences(UINT, int, CWindow);
        LRESULT OnColumnClick(LPNMHDR);

        void SetupListColumns();
        void Populate();
        void UpdatePeriodLabel();
        void SetStatus(const char *msg);
        void UpdateExportFormatButton();

        ViewMode m_viewMode = MONTH;
        std::string m_period; // "YYYY-MM", "YYYY", or "YYYY-MM-DD"
        std::vector<MonthlyEntry> m_entries;
        int m_sortCol = 4; // default: sort by plays
        bool m_sortAsc = false;
        bool m_exportFormatIsSmartphone = false; // Toggle between Desktop and Smartphone HTML export

        // Dialog resize helper for auto-layout management
        CDialogResizeHelper m_resizer;

        // Playback callback for auto-refresh
        class PlaybackCallbackImpl;
        std::unique_ptr<PlaybackCallbackImpl> m_playback_callback;

        static DashboardWindow *s_instance;
    };

} // namespace fms
