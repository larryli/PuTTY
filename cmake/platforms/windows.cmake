set(PUTTY_MINEFIELD OFF
  CACHE BOOL "Build PuTTY with its built-in memory debugger 'Minefield'")
set(PUTTY_GSSAPI ON
  CACHE BOOL "Build PuTTY with GSSAPI support")
set(PUTTY_LINK_MAPS OFF
  CACHE BOOL "Attempt to generate link maps")
set(PUTTY_EMBEDDED_CHM_FILE ""
  CACHE FILEPATH "Path to a .chm help file to embed in the binaries")

function(define_negation newvar oldvar)
  if(${oldvar})
    set(${newvar} OFF PARENT_SCOPE)
  else()
    set(${newvar} ON PARENT_SCOPE)
  endif()
endfunction()

include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CheckCSourceCompiles)

# Still needed for AArch32 Windows builds
set(CMAKE_REQUIRED_DEFINITIONS -D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE)

check_include_files("windows.h;winresrc.h" HAVE_WINRESRC_H)
if(NOT HAVE_WINRESRC_H)
  # A couple of fallback names for the header file you can include in
  # .rc files. We conditionalise even these checks, to save effort at
  # cmake time.
  check_include_files("windows.h;winres.h" HAVE_WINRES_H)
  if(NOT HAVE_WINRES_H)
    check_include_files("windows.h;win.h" HAVE_WIN_H)
  endif()
endif()
check_include_files("stdint.h" HAVE_STDINT_H)
define_negation(HAVE_NO_STDINT_H HAVE_STDINT_H)

check_include_files("windows.h;multimon.h" HAVE_MULTIMON_H)
define_negation(NO_MULTIMON HAVE_MULTIMON_H)

check_include_files("windows.h;htmlhelp.h" HAVE_HTMLHELP_H)
define_negation(NO_HTMLHELP HAVE_HTMLHELP_H)

check_symbol_exists(strtoumax "inttypes.h" HAVE_STRTOUMAX)
check_symbol_exists(AddDllDirectory "windows.h" HAVE_ADDDLLDIRECTORY)
check_symbol_exists(SetDefaultDllDirectories "windows.h"
  HAVE_SETDEFAULTDLLDIRECTORIES)
check_symbol_exists(GetNamedPipeClientProcessId "windows.h"
  HAVE_GETNAMEDPIPECLIENTPROCESSID)
check_symbol_exists(CreatePseudoConsole "windows.h" HAVE_CONPTY)

check_c_source_compiles("
#include <windows.h>
GCP_RESULTSW gcpw;
int main(void) { return 0; }
" HAVE_GCP_RESULTSW)

set(NO_SECURITY ${PUTTY_NO_SECURITY})

add_compile_definitions(
  _WINDOWS
  _CRT_SECURE_NO_WARNINGS
  _WINSOCK_DEPRECATED_NO_WARNINGS
  _ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE)

if(PUTTY_MINEFIELD)
  add_compile_definitions(MINEFIELD)
endif()
if(NOT PUTTY_GSSAPI)
  add_compile_definitions(NO_GSSAPI)
endif()
if(PUTTY_EMBEDDED_CHM_FILE)
  add_compile_definitions("EMBEDDED_CHM_FILE=\"${PUTTY_EMBEDDED_CHM_FILE}\"")
endif()

if(WINELIB)
  enable_language(RC)
  set(LFLAG_MANIFEST_NO "")
elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR
       CMAKE_C_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /nologo /C1252")
  set(LFLAG_MANIFEST_NO "/manifest:no")
else()
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -c1252")
  set(LFLAG_MANIFEST_NO "")
endif()

if(STRICT AND (CMAKE_C_COMPILER_ID MATCHES "GNU" OR
               CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wpointer-arith -Wvla")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  # Turn off some warnings that I've just found too noisy.
  #
  #  - 4244, 4267: "possible loss of data" when narrowing an integer
  #    type (separate warning numbers for initialisers and
  #    assignments). Every time I spot-check instances of this, they
  #    turn out to be sensible (e.g. something was already checked, or
  #    was assigned from a previous variable that must have been in
  #    range). I don't think putting a warning-suppression idiom at
  #    every one of these sites would improve code legibility.
  #
  #  - 4018: "signed/unsigned mismatch" in integer comparison. Again,
  #    comes up a lot, and generally my spot checks make it look as if
  #    it's OK.
  #
  #  - 4146: applying unary '-' to an unsigned type. We do that all
  #    the time in deliberate bit-twiddling code like mpint.c or
  #    crypto implementations.
  #
  #  - 4293: warning about undefined behaviour if a shift count is too
  #    big. We often do this inside a ?: clause which doesn't evaluate
  #    the overlong shift unless the shift count _isn't_ too big. When
  #    the shift count is constant, MSVC spots the potential problem
  #    in one branch of the ?:, but doesn't also spot that that branch
  #    isn't ever taken, so it complains about a thing that's already
  #    guarded.
  #
  #  - 4090: different 'const' qualifiers. It's a shame to suppress
  #    this one, because const mismatches really are a thing I'd
  #    normally like to be warned about. But MSVC (as of 2017 at
  #    least) seems to have a bug in which assigning a 'void *' into a
  #    'const char **' thinks there's a const-qualifier mismatch.
  #    There isn't! Both are pointers to modifiable objects. The fact
  #    that in one case, the modifiable object is a pointer to
  #    something _else_ const should make no difference.

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
/wd4244 /wd4267 /wd4018 /wd4146 /wd4293 /wd4090")
endif()

if(CMAKE_C_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
  set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} /dynamicbase /nxcompat")
endif()

set(platform_libraries
  advapi32.lib comdlg32.lib gdi32.lib imm32.lib
  ole32.lib shell32.lib user32.lib ws2_32.lib kernel32.lib)

# Generate link maps
if(PUTTY_LINK_MAPS)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND
      "x${CMAKE_C_COMPILER_FRONTEND_VARIANT}" STREQUAL "xMSVC")
    set(CMAKE_C_LINK_EXECUTABLE
      "${CMAKE_C_LINK_EXECUTABLE} /lldmap:<TARGET>.map")
  elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_C_LINK_EXECUTABLE
      "${CMAKE_C_LINK_EXECUTABLE} /map:<TARGET>.map")
  else()
    message(WARNING
      "Don't know how to generate link maps on this toolchain")
  endif()
endif()

# Write out a file in the cmake output directory listing the
# executables that are 'official' enough to want to code-sign and
# ship.
file(WRITE ${CMAKE_BINARY_DIR}/shipped.txt "")
function(installed_program target)
  file(APPEND ${CMAKE_BINARY_DIR}/shipped.txt
    "${target}${CMAKE_EXECUTABLE_SUFFIX}\n")
endfunction()
