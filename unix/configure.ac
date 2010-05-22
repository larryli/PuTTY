# To compile this into a configure script, you need:
# * Autoconf 2.50 or newer
# * Gtk (for $prefix/share/aclocal/gtk.m4)
# * Automake (for aclocal)
# If you've got them, running "autoreconf" should work.

AC_INIT
AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADERS([uxconfig.h:uxconfig.in])

AC_PROG_INSTALL
AC_PROG_CC
if test "X$GCC" = Xyes; then
    PUTTYCFLAGS="-Wall -Werror"
else
    PUTTYCFLAGS=""
fi
AC_SUBST(PUTTYCFLAGS)

AC_ARG_WITH([gssapi],
  [AS_HELP_STRING([--without-gssapi],
                  [disable GSSAPI support])],
  [],
  [with_gssapi=yes])

WITH_GSSAPI=
AS_IF([test "x$with_gssapi" != xno],
  [AC_DEFINE([WITH_GSSAPI], [1], [Define if building with GSSAPI support.])])

AC_CHECK_HEADERS([utmpx.h sys/select.h],,,[
#include <sys/types.h>
#include <utmp.h>])

# Look for both GTK 1 and GTK 2.
AM_PATH_GTK([1.2.0], [gtk=1], [gtk=none])
AM_PATH_GTK_2_0([2.0.0], [gtk=2], [])
if test "$gtk" = "none"; then
  all_targets="all-cli"
else
  all_targets="all-cli all-gtk"
fi
if test "$gtk" = "2"; then
  ac_save_CFLAGS="$CFLAGS"
  ac_save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $GTK_CFLAGS"
  LIBS="$GTK_LIBS $LIBS"
  AC_CHECK_FUNCS([pango_font_family_is_monospace pango_font_map_list_families])
  CFLAGS="$ac_save_CFLAGS"
  LIBS="$ac_save_LIBS"
fi
AC_SUBST([all_targets])

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

AC_CHECK_LIB(X11, XOpenDisplay)

AC_CHECK_FUNCS([getaddrinfo ptsname setresuid strsignal updwtmpx])

AC_OUTPUT

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
