# Toolchain file for cross-building a Winelib version of Windows PuTTY
# on Linux, using winegcc (tested on Ubuntu).

# Winelib is weird because it's basically compiling ordinary Linux
# objects and executables, but we want to pretend to be Windows for
# purposes of (a) having resource files, and (b) selecting the Windows
# platform subdirectory.
#
# So, do we tag this as a weird kind of Windows build, or a weird kind
# of Linux build? Either way we have to do _something_ out of the
# ordinary.
#
# After some experimentation, it seems to make more sense to treat
# Winelib builds as basically Linux, and set a flag WINELIB that
# PuTTY's main build scripts will detect and handle specially.
# Specifically, that flag will cause cmake/setup.cmake to select the
# Windows platform (overriding the usual check of CMAKE_SYSTEM_NAME),
# and also trigger a call to enable_language(RC), which for some kind
# of cmake re-entrancy reason we can't do in this toolchain file
# itself.
set(CMAKE_SYSTEM_NAME Linux)
set(WINELIB ON)

# We need a wrapper script around winegcc proper, because cmake's link
# command lines will refer to system libraries as "-lkernel32.lib"
# rather than the required "-lkernel32". The winegcc script alongside
# this toolchain file bodges that command-line translation.
set(CMAKE_C_COMPILER ${CMAKE_SOURCE_DIR}/cmake/winegcc)

set(CMAKE_RC_COMPILER wrc)
set(CMAKE_RC_OUTPUT_EXTENSION .res.o)
set(CMAKE_RC_COMPILE_OBJECT
    "<CMAKE_RC_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")
