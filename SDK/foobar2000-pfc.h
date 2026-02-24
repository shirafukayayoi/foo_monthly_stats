#pragma once

// Path adjusted: SDK is at fb2k/SDK/ (not foobar2000/SDK/), so pfc is one level up
#include "../pfc/pfc.h"

// These were in a global namespace before and are commonly referenced as such.
using pfc::bit_array;
using pfc::bit_array_bittable;
using pfc::bit_array_false;
using pfc::bit_array_one;
using pfc::bit_array_range;
using pfc::bit_array_true;
using pfc::bit_array_val;
using pfc::bit_array_var;
#ifdef _WIN32
using pfc::LastErrorRevertScope;
#endif
