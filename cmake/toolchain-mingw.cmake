# Simple toolchain file for cross-building Windows PuTTY on Linux
# using MinGW (tested on Ubuntu).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER  x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR          x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB      x86_64-w64-mingw32-ranlib)
