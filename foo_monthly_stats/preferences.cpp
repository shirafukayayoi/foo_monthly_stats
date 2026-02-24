#include "stdafx.h"
#include "preferences.h"
#include "db_manager.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// GUIDs – generated uniquely for foo_monthly_stats
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_db_path = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x05}};
static constexpr GUID guid_cfg_art_size = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x06}};
static constexpr GUID guid_cfg_auto_report = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x07}};
static constexpr GUID guid_cfg_chrome_path = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x08}};
static constexpr GUID guid_preferences_page = {0xf1a2b3c4, 0xd5e6, 0x4789, {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x00, 0x02}};

namespace fms
{

    // ---------------------------------------------------------------------------
    // Persisted settings
    // ---------------------------------------------------------------------------
    cfg_var_modern::cfg_string g_cfg_db_path(guid_cfg_db_path, "");
    cfg_var_modern::cfg_int g_cfg_art_size(guid_cfg_art_size, 64);
    cfg_var_modern::cfg_bool g_cfg_auto_report(guid_cfg_auto_report, false);
    cfg_var_modern::cfg_string g_cfg_chrome_path(guid_cfg_chrome_path, "");

    std::string effectiveDbPath()
    {
        pfc::string8 v = g_cfg_db_path.get();
        if (v.is_empty())
        {
            pfc::string8 profile;
            filesystem::g_get_native_path(core_api::get_profile_path(), profile);
            return std::string(profile.c_str()) + "\\foo_monthly_stats.db";
        }
        return v.c_str();
    }

    // ---------------------------------------------------------------------------
    // initquit – lifecycle management
    // ---------------------------------------------------------------------------
    class FmsInitQuit : public initquit
    {
    public:
        void on_init() override
        {
            auto path = effectiveDbPath();
            if (!DbManager::get().open(path.c_str()))
            {
                FB2K_console_formatter() << "foo_monthly_stats: Failed to open DB at " << path.c_str();
            }
        }
        void on_quit() override
        {
            DbManager::get().close();
        }
    };
    static initquit_factory_t<FmsInitQuit> g_initQuit;

    // ---------------------------------------------------------------------------
    // Preferences dialog
    // ---------------------------------------------------------------------------

    class CPreferencesDlg
        : public CDialogImpl<CPreferencesDlg>,
          public preferences_page_instance
    {
    public:
        enum
        {
            IDD = IDD_PREFERENCES
        };

        CPreferencesDlg(preferences_page_callback::ptr cb) : m_callback(cb) {}

        // preferences_page_instance
        t_uint32 get_state() override
        {
            t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
            if (HasChanged())
                state |= preferences_state::changed;
            return state;
        }
        fb2k::hwnd_t get_wnd() override { return m_hWnd; }

        void apply() override
        {
            // DB path
            CString dbPath;
            GetDlgItemText(IDC_EDIT_DB_PATH, dbPath);
            g_cfg_db_path = pfc::stringcvt::string_utf8_from_os(dbPath);

            // Art size
            int idx = SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_GETCURSEL, 0, 0);
            const int sizes[] = {32, 64, 128};
            g_cfg_art_size = (idx >= 0 && idx < 3) ? sizes[idx] : 64;

            // Auto report
            g_cfg_auto_report = (IsDlgButtonChecked(IDC_CHECK_AUTO_REPORT) == BST_CHECKED);

            // Chrome path
            CString chromePath;
            GetDlgItemText(IDC_EDIT_CHROME_PATH, chromePath);
            g_cfg_chrome_path = pfc::stringcvt::string_utf8_from_os(chromePath);

            OnChanged();
        }

        void reset() override
        {
            SetDlgItemText(IDC_EDIT_DB_PATH, L"");
            int idx = 1; // 64px default
            SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_SETCURSEL, idx, 0);
            CheckDlgButton(IDC_CHECK_AUTO_REPORT, BST_UNCHECKED);
            SetDlgItemText(IDC_EDIT_CHROME_PATH, L"");
            OnChanged();
        }

        BEGIN_MSG_MAP_EX(CPreferencesDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_EDIT_DB_PATH, EN_CHANGE, OnChange)
        COMMAND_HANDLER_EX(IDC_EDIT_CHROME_PATH, EN_CHANGE, OnChange)
        COMMAND_HANDLER_EX(IDC_COMBO_ART_SIZE, CBN_SELCHANGE, OnChange)
        COMMAND_HANDLER_EX(IDC_CHECK_AUTO_REPORT, BN_CLICKED, OnChange)
        COMMAND_HANDLER_EX(IDC_BTN_BROWSE_DB, BN_CLICKED, OnBrowseDb)
        COMMAND_HANDLER_EX(IDC_BTN_BROWSE_CHROME, BN_CLICKED, OnBrowseChrome)
        END_MSG_MAP()

    private:
        BOOL OnInitDialog(CWindow, LPARAM)
        {
            m_dark.AddDialogWithControls(*this);

            // DB path
            pfc::string8 dbPath = g_cfg_db_path.get();
            SetDlgItemText(IDC_EDIT_DB_PATH, pfc::stringcvt::string_os_from_utf8(dbPath));

            // Art size combo
            SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_ADDSTRING, 0, (LPARAM)L"32 px");
            SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_ADDSTRING, 0, (LPARAM)L"64 px");
            SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_ADDSTRING, 0, (LPARAM)L"128 px");
            int64_t artSize = g_cfg_art_size.get();
            int sel = (artSize == 32) ? 0 : (artSize == 128) ? 2
                                                             : 1;
            SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_SETCURSEL, sel, 0);

            // Auto report
            CheckDlgButton(IDC_CHECK_AUTO_REPORT, g_cfg_auto_report.get() ? BST_CHECKED : BST_UNCHECKED);

            // Chrome path
            pfc::string8 chromePath = g_cfg_chrome_path.get();
            SetDlgItemText(IDC_EDIT_CHROME_PATH, pfc::stringcvt::string_os_from_utf8(chromePath));

            return FALSE;
        }

        void OnChange(UINT, int, CWindow) { OnChanged(); }

        void OnBrowseDb(UINT, int, CWindow)
        {
            OPENFILENAME ofn{};
            wchar_t buf[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hWnd;
            ofn.lpstrFilter = L"SQLite DB\0*.db\0All Files\0*.*\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
            if (GetSaveFileName(&ofn))
                SetDlgItemText(IDC_EDIT_DB_PATH, buf);
            OnChanged();
        }

        void OnBrowseChrome(UINT, int, CWindow)
        {
            OPENFILENAME ofn{};
            wchar_t buf[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hWnd;
            ofn.lpstrFilter = L"Executable\0*.exe\0All Files\0*.*\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileName(&ofn))
                SetDlgItemText(IDC_EDIT_CHROME_PATH, buf);
            OnChanged();
        }

        bool HasChanged()
        {
            CString dlgDbPath, dlgChrome;
            GetDlgItemText(IDC_EDIT_DB_PATH, dlgDbPath);
            GetDlgItemText(IDC_EDIT_CHROME_PATH, dlgChrome);
            int idx = (int)SendDlgItemMessage(IDC_COMBO_ART_SIZE, CB_GETCURSEL, 0, 0);
            const int sizes[] = {32, 64, 128};
            int dlgSize = (idx >= 0 && idx < 3) ? sizes[idx] : 64;
            bool dlgAuto = (IsDlgButtonChecked(IDC_CHECK_AUTO_REPORT) == BST_CHECKED);

            pfc::string8 curDb = g_cfg_db_path.get();
            pfc::string8 curChrome = g_cfg_chrome_path.get();

            if (CString(pfc::stringcvt::string_os_from_utf8(curDb)) != dlgDbPath)
                return true;
            if (dlgSize != (int)g_cfg_art_size.get())
                return true;
            if (dlgAuto != g_cfg_auto_report.get())
                return true;
            if (CString(pfc::stringcvt::string_os_from_utf8(curChrome)) != dlgChrome)
                return true;
            return false;
        }

        void OnChanged() { m_callback->on_state_changed(); }

        preferences_page_callback::ptr m_callback;
        fb2k::CDarkModeHooks m_dark;
    };

    class FmsPreferencesPage : public preferences_page_impl<CPreferencesDlg>
    {
    public:
        const char *get_name() override { return "Monthly Stats"; }
        GUID get_guid() override { return guid_preferences_page; }
        GUID get_parent_guid() override { return preferences_page::guid_tools; }
    };

    static preferences_page_factory_t<FmsPreferencesPage> g_prefsFactory;

} // namespace fms
