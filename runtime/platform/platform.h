// Operating-system services of the port behind one interface (jrt::platform). Everything that
// differs between Linux and Windows lives in platform/linux.{h,cpp} or platform/windows.{h,cpp};
// CMake compiles the one of the target system. Code outside platform/ includes only this header.
#pragma once

#if defined(_WIN32)
#include "platform/windows.h"
#else
#include "platform/linux.h"
#endif
