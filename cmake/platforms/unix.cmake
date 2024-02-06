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

check_c_source_compiles("
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
    setpgrp();
}" HAVE_NULLARY_SETPGRP)
check_c_source_compiles("
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
    setpgrp(0, 0);
}" HAVE_BINARY_SETPGRP)

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

if(GTK_FOUND)
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
else()
  # If we didn't even have GTK, behave as if X11 is not available.
  # (There's nothing useful we could do with it even if there was.)
  set(NOT_X_WINDOWS ON)
endif()

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
  set(KRB5_CFLAGS)
  set(KRB5_LDFLAGS)

  # First try using pkg-config
  find_package(PkgConfig)
  pkg_check_modules(KRB5 krb5-gssapi)

  # Failing that, try the dedicated krb5-config
  if(NOT KRB5_FOUND)
    find_program(KRB5_CONFIG krb5-config)
    if(KRB5_CONFIG)
      execute_process(COMMAND ${KRB5_CONFIG} --cflags gssapi
        OUTPUT_VARIABLE krb5_config_cflags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE krb5_config_cflags_result)
      execute_process(COMMAND ${KRB5_CONFIG} --libs gssapi
        OUTPUT_VARIABLE krb5_config_libs
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE krb5_config_libs_result)

      if(krb5_config_cflags_result EQUAL 0 AND krb5_config_libs_result EQUAL 0)
        set(KRB5_INCLUDE_DIRS)
        set(KRB5_LIBRARY_DIRS)
        set(KRB5_LIBRARIES)

        # We can safely put krb5-config's cflags directly into cmake's
        # cflags, without bothering to extract the include directories.
        set(KRB5_CFLAGS ${krb5_config_cflags})

        # But krb5-config --libs isn't so simple. It will actually
        # deliver a mix of libraries and other linker options. We have
        # to separate them for cmake purposes, because if we pass the
        # whole lot to add_link_options then they'll appear too early
        # in the command line (so that by the time our own code refers
        # to GSSAPI functions it'll be too late to search these
        # libraries for them), and if we pass the whole lot to
        # link_libraries then it'll get confused about options that
        # aren't libraries.
        separate_arguments(krb5_config_libs NATIVE_COMMAND
          ${krb5_config_libs})
        foreach(opt ${krb5_config_libs})
          string(REGEX MATCH "^-l" ok ${opt})
          if(ok)
            list(APPEND KRB5_LIBRARIES ${opt})
            continue()
          endif()
          string(REGEX MATCH "^-L" ok ${opt})
          if(ok)
            string(REGEX REPLACE "^-L" "" optval ${opt})
            list(APPEND KRB5_LIBRARY_DIRS ${optval})
            continue()
          endif()
          list(APPEND KRB5_LDFLAGS ${opt})
        endforeach()

        message(STATUS "Found Kerberos via krb5-config")
        set(KRB5_FOUND YES)
      endif()
    endif()
  endif()

  if(KRB5_FOUND)
    include_directories(${KRB5_INCLUDE_DIRS})
    link_directories(${KRB5_LIBRARY_DIRS})
    link_libraries(${KRB5_LIBRARIES})
    add_compile_options(${KRB5_CFLAGS})
    add_link_options(${KRB5_LDFLAGS})
    set(STATIC_GSSAPI ON)
  else()
    message(WARNING
      "Could not find krb5 via pkg-config or krb5-config -- \
cannot provide static GSSAPI support")
    set(NO_GSSAPI ON)
  endif()
endif()

if(PUTTY_GSSAPI STREQUAL OFF)
  set(NO_GSSAPI ON)
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
