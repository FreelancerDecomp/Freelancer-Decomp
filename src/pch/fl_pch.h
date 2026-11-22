// Precompiled header for the largest projects (Common, freelancer, Content), forced in front of
// every source file by Directory.Build.targets. Build-time only: it holds just Windows SDK and
// standard library headers that most of those files include already (dacom.h, at the top of nearly
// every one, includes <windows.h>). Never put project headers here.
// A header here can still change code in a file that did not include it: <cmath>, for one, adds the
// float overloads of sqrt and abs. The Win32 Release code of all three modules was compared function
// by function with a build without this header and is the same (only the placement of functions
// and data differs), so any change to this list needs the same comparison.
// A file that has to set up macros before <windows.h> (WIN32_LEAN_AND_MEAN, NOMINMAX) opts out in
// Directory.Build.targets.
#pragma once

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <list>
#include <map>
#include <string>
#include <vector>
