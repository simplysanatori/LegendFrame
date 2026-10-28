#pragma once
// Force-included (-include) by mathtest.sh, like the mingw build's
// seh_compat.h: the whole standard library first, so its own __try macro is
// defined before windows.h (host stand-in) replaces it.
#include <bits/stdc++.h>
#include "windows.h"
