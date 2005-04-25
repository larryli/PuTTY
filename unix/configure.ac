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
    CFLAGS="$CFLAGS -Wall -Werror"
fi

AC_CHECK_HEADERS([sys/sockio.h],,,[ ])

AM_PATH_GTK([1.2.0], [all_targets="all-cli all-gtk"], [all_targets="all-cli"])
AC_SUBST([all_targets])

AC_SEARCH_LIBS([socket], [xnet])

AC_CHECK_FUNCS([getaddrinfo ptsname pututline setresuid strsignal])

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
#ifndef HAVE_PUTUTLINE
# define OMIT_UTMP
#endif
#ifndef HAVE_PTSNAME
# define BSD_PTYS
#endif
])
