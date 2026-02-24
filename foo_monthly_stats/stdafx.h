#pragma once

#ifdef __cplusplus
// ATL/WTL helpers must be included BEFORE foobar2000 SDK headers
#include <helpers/foobar2000+atl.h>
// foobar2000 SDK (includes menu.h, play_callback.h, album_art.h, preferences_page.h, etc.)
#include <SDK/foobar2000.h>
// Additional ATL/WTL helpers
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
// UI library
#include <libPPUI/CListControl.h>
#include <libPPUI/CListControlSimple.h>
#include <libPPUI/CListControlComplete.h>

// Windows
#include <shellapi.h>
#include <shlobj.h>

// STL
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <memory>
#include <cassert>

// SQLite (amalgamation – included in third_party/sqlite)
#include <sqlite3.h>

// pugixml
#include <pugixml.hpp>
#endif
