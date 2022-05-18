set(PUTTY_GSSAPI DYNAMIC
  CACHE STRING "Build PuTTY with dynamically or statically linked \
Kerberos / GSSAPI support, if possible")
set_property(CACHE PUTTY_GSSAPI
  PROPERTY STRINGS DYNAMIC STATIC OFF)

include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(GNUInstallDirs)

set(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
  -D_DEFAULT_SOURCE -D_GNU_SOURCE)

check_include_file(sys/auxv.h HAVE_SYS_AUXV_H)
check_include_file(asm/hwcap.h HAVE_ASM_HWCAP_H)
check_include_file(sys/sysctl.h HAVE_SYS_SYSCTL_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(glob.h HAVE_GLOB_H)
check_include_file(utmp.h HAVE_UTMP_H)
check_include_file(utmpx.h HAVE_UTMPX_H)

check_symbol_exists(futimes "sys/time.h" HAVE_FUTIMES)
check_symbol_exists(getaddrinfo "sys/types.h;sys/socket.h;netdb.h"
  HAVE_GETADDRINFO)
check_symbol_exists(posix_openpt "stdlib.h;fcntl.h" HAVE_POSIX_OPENPT)
check_symbol_exists(ptsname "stdlib.h" HAVE_PTSNAME)
check_symbol_exists(setresuid "unistd.h" HAVE_SETRESUID)
check_symbol_exists(setresgid "unistd.h" HAVE_SETRESGID)
check_symbol_exists(strsignal "string.h" HAVE_STRSIGNAL)
check_symbol_exists(updwtmpx "utmpx.h" HAVE_UPDWTMPX)
check_symbol_exists(fstatat "sys/types.h;sys/stat.h;unistd.h" HAVE_FSTATAT)
check_symbol_exists(dirfd "sys/types.h;dirent.h" HAVE_DIRFD)
check_symbol_exists(setpwent "sys/types.h;pwd.h" HAVE_SETPWENT)
check_symbol_exists(endpwent "sys/types.h;pwd.h" HAVE_ENDPWENT)
check_symbol_exists(getauxval "sys/auxv.h" HAVE_GETAUXVAL)
check_symbol_exists(elf_aux_info "sys/auxv.h" HAVE_ELF_AUX_INFO)
check_symbol_exists(sysctlbyname "sys/types.h;sys/sysctl.h" HAVE_SYSCTLBYNAME)
check_symbol_exists(CLOCK_MONOTONIC "time.h" HAVE_CLOCK_MONOTONIC)
check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)

check_c_source_compiles("
#define _GNU_SOURCE
#include <features.h>
#include <sys/socket.h>
int main(int argc, char **argv) {
    struct ucred cr;
    socklen_t crlen = sizeof(cr);
    return getsockopt(0, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) +
           cr.pid + cr.uid + cr.gid;
}" HAVE_SO_PEERCRED)

if(HAVE_GETADDRINFO AND PUTTY_IPV6)
  set(NO_IPV6 OFF)
else()
  set(NO_IPV6 ON)
endif()

if(HAVE_UTMPX_H)
  set(OMIT_UTMP OFF)
else()
  set(OMIT_UTMP ON)
endif()

include(cmake/gtk.cmake)

# See if we have X11 available. This requires libX11 itself, and also
# the GDK integration to X11.
find_package(X11)

function(check_x11)
  list(APPEND CMAKE_REQUIRED_INCLUDES ${GTK_INCLUDE_DIRS})
  check_include_file(gdk/gdkx.h HAVE_GDK_GDKX_H)

  if(X11_FOUND AND HAVE_GDK_GDKX_H)
    set(NOT_X_WINDOWS OFF PARENT_SCOPE)
  else()
    set(NOT_X_WINDOWS ON PARENT_SCOPE)
  endif()
endfunction()
check_x11()

include_directories(${CMAKE_SOURCE_DIR}/charset ${GTK_INCLUDE_DIRS} ${X11_INCLUDE_DIR})
link_directories(${GTK_LIBRARY_DIRS})

function(add_optional_system_lib library testfn)
  check_library_exists(${library} ${testfn} "" HAVE_LIB${library})
  if (HAVE_LIB${library})
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES};-l${library})
    link_libraries(-l${library})
  endif()
endfunction()

add_optional_system_lib(m pow)
add_optional_system_lib(rt clock_gettime)
add_optional_system_lib(xnet socket)

set(extra_dirs charset)

if(PUTTY_GSSAPI STREQUAL DYNAMIC)
  add_optional_system_lib(dl dlopen)
  if(HAVE_NO_LIBdl)
    message(WARNING
      "Could not find libdl -- cannot provide dynamic GSSAPI support")
    set(NO_GSSAPI ON)
  endif()
endif()

if(PUTTY_GSSAPI STREQUAL STATIC)
  find_package(PkgConfig)
  pkg_check_modules(KRB5 krb5-gssapi)
  if(KRB5_FOUND)
    include_directories(${KRB5_INCLUDE_DIRS})
    link_directories(${KRB5_LIBRARY_DIRS})
    link_libraries(${KRB5_LIBRARIES})
    set(STATIC_GSSAPI ON)
  else()
    message(WARNING
      "Could not find krb5 via pkg-config -- \
cannot provide static GSSAPI support")
    set(NO_GSSAPI ON)
  endif()
endif()

if(STRICT AND (CMAKE_C_COMPILER_ID MATCHES "GNU" OR
               CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -Wpointer-arith -Wvla")
endif()

function(installed_program target)
  if(CMAKE_VERSION VERSION_LESS 3.14)
    # CMake 3.13 and earlier required an explicit install destination.
    install(TARGETS ${target} RUNTIME DESTINATION bin)
  else()
    # 3.14 and above selects a sensible default, which we should avoid
    # overriding here so that end users can override it using
    # CMAKE_INSTALL_BINDIR.
    install(TARGETS ${target})
  endif()

  if(HAVE_MANPAGE_${target}_1)
    install(FILES ${CMAKE_BINARY_DIR}/doc/${target}.1
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
  else()
    message(WARNING "Could not build man page ${target}.1")
  endif()
endfunction()
