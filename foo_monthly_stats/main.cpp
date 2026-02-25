#include "stdafx.h"

// Component declaration
DECLARE_COMPONENT_VERSION(
    "Monthly Stats",
    "1.3.0",
    "foo_monthly_stats\n"
    "Visualizes monthly play counts for foobar2000 1.6+\n"
    "License: BSD-2-Clause\n"
    "https://github.com/your-repo/foo_monthly_stats");

VALIDATE_COMPONENT_FILENAME("foo_monthly_stats.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
