# To compile this into a configure script, you need:
# * Autoconf 2.59c or newer
# * Gtk (for $prefix/share/aclocal/gtk.m4)
# * Automake (for aclocal)
# If you've got them, running "autoreconf" should work.

# Version number is substituted by Buildscr for releases, snapshots
# and custom builds out of svn; X.XX shows up in ad-hoc developer
# builds, which shouldn't matter
AC_INIT(putty, X.XX)
AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADERS([uxconfig.h:uxconfig.in])
AM_INIT_AUTOMAKE([-Wall foreign])

AC_PROG_INSTALL
AC_PROG_RANLIB
ifdef([AM_PROG_AR],[AM_PROG_AR])
AM_PROG_CC_C_O

AC_PROG_CC_C99

# Mild abuse of the '--enable' option format to allow manual
# specification of setuid or setgid setup in pterm.
setidtype=none
AC_ARG_ENABLE([setuid],
            [AS_HELP_STRING([--enable-setuid=USER],
                            [make pterm setuid to a given user])],
            [case "$enableval" in
               no) setidtype=none;;
               *) setidtype=setuid; setidval="$enableval";;
             esac])
AC_ARG_ENABLE([setgid],
            [AS_HELP_STRING([--enable-setgid=GROUP],
                            [make pterm setgid to a given group])],
            [case "$enableval" in
               no) setidtype=none;;
               *) setidtype=setgid; setidval="$enableval";;
             esac])
AM_CONDITIONAL(HAVE_SETID_CMD, [test "$setidtype" != "none"])
AS_IF([test "x$setidtype" = "xsetuid"],
      [SETID_CMD="chown $setidval"; SETID_MODE="4755"])
AS_IF([test "x$setidtype" = "xsetgid"],
      [SETID_CMD="chgrp $setidval"; SETID_MODE="2755"])
AC_SUBST(SETID_CMD)
AC_SUBST(SETID_MODE)

AC_ARG_ENABLE([git-commit],
  [AS_HELP_STRING([--disable-git-commit],
                  [disable embedding current git HEAD in binaries])],
  [],
  [if test -d "$srcdir/.git"; then
    enable_git_commit=yes; else enable_git_commit=no; fi])

if test "x$enable_git_commit" = "xyes" -a ! -d "$srcdir/.git"; then
  AC_ERROR([Cannot --enable-git-commit when source tree is not a git checkout])
fi
AM_CONDITIONAL(AUTO_GIT_COMMIT, [test "x$enable_git_commit" = "xyes"])

AC_ARG_WITH([gssapi],
  [AS_HELP_STRING([--without-gssapi],
                  [disable GSSAPI support])],
  [],
  [with_gssapi=yes])

AC_ARG_WITH([quartz],
  [AS_HELP_STRING([--with-quartz],
                  [build for the MacOS Quartz GTK back end])],
  [AC_DEFINE([OSX_GTK], [1], [Define if building with GTK for MacOS.])
   with_quartz=yes],
  [with_quartz=no])

AM_CONDITIONAL([HAVE_QUARTZ],[test "x$with_quartz" = "xyes"])

WITH_GSSAPI=
AS_IF([test "x$with_gssapi" != xno],
  [AC_DEFINE([WITH_GSSAPI], [1], [Define if building with GSSAPI support.])])

AC_ARG_WITH([gtk],
  [AS_HELP_STRING([--with-gtk=VER],
                  [specify GTK version to use (`1', `2' or `3')])
AS_HELP_STRING([--without-gtk],
                  [do not use GTK (build command-line tools only)])],
  [gtk_version_desired="$withval"],
  [gtk_version_desired="any"])

case "$gtk_version_desired" in
  1 | 2 | 3 | any | no) ;;
  yes) gtk_version_desired="any" ;;
  *) AC_ERROR([Invalid GTK version specified])
esac

AC_CHECK_HEADERS([utmpx.h],,,[
#include <sys/types.h>
#include <utmp.h>])

# Look for GTK 3, GTK 2 and GTK 1, in descending order of preference.
# If we can't find any, have the makefile only build the CLI programs.

gtk=none

case "$gtk_version_desired:$gtk" in
  3:none | any:none)
    ifdef([AM_PATH_GTK_3_0],[AM_PATH_GTK_3_0([3.0.0], [gtk=3], [])],
    [AC_WARNING([generating configure script without GTK 3 autodetection])])
    ;;
esac

case "$gtk_version_desired:$gtk" in
  2:none | any:none)
    ifdef([AM_PATH_GTK_2_0],[AM_PATH_GTK_2_0([2.0.0], [gtk=2], [])],
    [AC_WARNING([generating configure script without GTK 2 autodetection])])
    ;;
esac

case "$gtk_version_desired:$gtk" in
  1:none | any:none)
    ifdef([AM_PATH_GTK],[AM_PATH_GTK([1.2.0], [gtk=1], [])],[
    # manual check for gtk1
    AC_PATH_PROG(GTK1_CONFIG, gtk-config, absent)
    if test "$GTK1_CONFIG" != "absent"; then
      GTK_CFLAGS="`"$GTK1_CONFIG" --cflags`"
      GTK_LIBS=`"$GTK1_CONFIG" --libs`
      AC_SUBST(GTK_CFLAGS)
      AC_SUBST(GTK_LIBS)
      gtk=1
    fi
    ])
    ;;
esac

case "$gtk" in
  1)
    # Add some manual #defines to make the GTK 1 headers work when
    # compiling in C99 mode. Left to themselves, they'll expect the
    # old-style pre-C99 GNU semantics of 'inline' and 'extern inline',
    # with the effect that they'll end up defining out-of-line
    # versions of the inlined functions in more than one translation
    # unit and cause a link failure. Override them to 'static inline',
    # which is safe.
    GTK_CFLAGS="$GTK_CFLAGS -DG_INLINE_FUNC='static inline' -DG_CAN_INLINE=1"
esac

AM_CONDITIONAL(HAVE_GTK, [test "$gtk" != "none"])

if test "$gtk" = "2" -o "$gtk" = "3"; then
  ac_save_CFLAGS="$CFLAGS"
  ac_save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $GTK_CFLAGS"
  LIBS="$GTK_LIBS $LIBS"
  AC_CHECK_FUNCS([pango_font_family_is_monospace pango_font_map_list_families])
  CFLAGS="$ac_save_CFLAGS"
  LIBS="$ac_save_LIBS"
fi

AC_SEARCH_LIBS([socket], [xnet])

AS_IF([test "x$with_gssapi" != xno],
  [AC_SEARCH_LIBS(
    [dlopen],[dl],
    [],
    [AC_DEFINE([NO_LIBDL], [1], [Define if we could not find libdl.])
     AC_CHECK_HEADERS([gssapi/gssapi.h])
     AC_SEARCH_LIBS(
       [gss_init_sec_context],[gssapi gssapi_krb5 gss],
       [],
       [AC_DEFINE([NO_GSSAPI_LIB], [1], [Define if we could not find a gssapi library])])])])

AC_CHECK_LIB(X11, XOpenDisplay,
             [GTK_LIBS="-lX11 $GTK_LIBS"
              AC_DEFINE([HAVE_LIBX11],[],[Define if libX11.a is available])])

AC_CHECK_FUNCS([getaddrinfo posix_openpt ptsname setresuid strsignal updwtmpx fstatat dirfd futimes setpwent endpwent getauxval elf_aux_info sysctlbyname])
AC_CHECK_DECLS([CLOCK_MONOTONIC], [], [], [[#include <time.h>]])
AC_CHECK_HEADERS([sys/auxv.h asm/hwcap.h sys/sysctl.h sys/types.h glob.h])
AC_SEARCH_LIBS([clock_gettime], [rt], [AC_DEFINE([HAVE_CLOCK_GETTIME],[],[Define if clock_gettime() is available])])

AC_CACHE_CHECK([for SO_PEERCRED and dependencies], [x_cv_linux_so_peercred], [
    AC_COMPILE_IFELSE([
        AC_LANG_PROGRAM([[
            #define _GNU_SOURCE
            #include <features.h>
            #include <sys/socket.h>
          ]],[[
            struct ucred cr;
            socklen_t crlen = sizeof(cr);
            return getsockopt(0, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) +
            cr.pid + cr.uid + cr.gid;
          ]]
        )],
        AS_VAR_SET(x_cv_linux_so_peercred, yes),
        AS_VAR_SET(x_cv_linux_so_peercred, no)
    )
])
AS_IF([test AS_VAR_GET(x_cv_linux_so_peercred) = yes],
    [AC_DEFINE([HAVE_SO_PEERCRED], [1],
     [Define if SO_PEERCRED works in the Linux fashion.])]
)

if test "x$GCC" = "xyes"; then
  :
  AC_SUBST(WARNINGOPTS, ['-Wall -Wpointer-arith -Wvla'])
else
  :
  AC_SUBST(WARNINGOPTS, [])
fi

AC_SEARCH_LIBS([pow], [m])

AC_OUTPUT

if test "$gtk_version_desired" = "no"; then cat <<EOF

'configure' was instructed not to build using GTK. Therefore, PuTTY
itself and the other GUI utilities will not be built by the generated
Makefile: only the command-line tools such as puttygen, plink and
psftp will be built.

EOF
elif test "$gtk" = "none"; then cat <<EOF

'configure' was unable to find any version of the GTK libraries on
your system. Therefore, PuTTY itself and the other GUI utilities will
not be built by the generated Makefile: only the command-line tools
such as puttygen, plink and psftp will be built.

EOF
fi

AH_BOTTOM([
/* Convert autoconf definitions to ones that PuTTY wants. */

#ifndef HAVE_GETADDRINFO
# define NO_IPV6
#endif
#ifndef HAVE_SETRESUID
# define HAVE_NO_SETRESUID
#endif
#ifndef HAVE_STRSIGNAL
# define HAVE_NO_STRSIGNAL
#endif
#if !defined(HAVE_UTMPX_H) || !defined(HAVE_UPDWTMPX)
# define OMIT_UTMP
#endif
#ifndef HAVE_PTSNAME
# define BSD_PTYS
#endif
#ifndef HAVE_SYS_SELECT_H
# define HAVE_NO_SYS_SELECT_H
#endif
#ifndef HAVE_PANGO_FONT_FAMILY_IS_MONOSPACE
# define PANGO_PRE_1POINT4
#endif
#ifndef HAVE_PANGO_FONT_MAP_LIST_FAMILIES
# define PANGO_PRE_1POINT6
#endif
#if !defined(WITH_GSSAPI)
# define NO_GSSAPI
#endif
#if !defined(NO_GSSAPI) && defined(NO_LIBDL)
# if !defined(HAVE_GSSAPI_GSSAPI_H) || defined(NO_GSSAPI_LIB)
#  define NO_GSSAPI
# endif
#endif
])
