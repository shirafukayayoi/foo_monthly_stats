#pragma once
#include "stdafx.h"

namespace fms
{

    // Settings exposed via cfg_var (persistent across foobar2000 sessions).
    // Global accessors declared here, defined in preferences.cpp.

    extern cfg_var_modern::cfg_string g_cfg_db_path;
    extern cfg_var_modern::cfg_int g_cfg_art_size; // 32 / 64 / 128
    extern cfg_var_modern::cfg_bool g_cfg_auto_report;
    extern cfg_var_modern::cfg_string g_cfg_chrome_path;

    // Returns the effective DB path (default = profile dir / foo_monthly_stats.db)
    std::string effectiveDbPath();

} // namespace fms
