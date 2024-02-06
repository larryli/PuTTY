/*
 * Miscellaneous stuff to include in all .rc files.
 */

#ifndef PUTTY_RCSTUFF_H
#define PUTTY_RCSTUFF_H

#ifdef HAVE_CMAKE_H
#include "cmake.h"
#endif

#if HAVE_WINRESRC_H
#include <winresrc.h>
#elif HAVE_WINRES_H
#include <winres.h>
#endif

/* Some systems don't define this, so I do it myself if necessary */
#ifndef TCS_MULTILINE
#define TCS_MULTILINE 0x0200
#endif

/* Likewise */
#ifndef RT_MANIFEST
#define RT_MANIFEST 24
#endif

/* LCC is the offender here. */
#ifndef VS_FF_DEBUG
#define VS_FF_DEBUG        1
#endif
#ifndef VS_FF_PRERELEASE
#define VS_FF_PRERELEASE   2
#endif
#ifndef VS_FF_PRIVATEBUILD
#define VS_FF_PRIVATEBUILD 8
#endif
#ifndef VOS__WINDOWS32
#define VOS__WINDOWS32     4
#endif
#ifndef VFT_APP
#define VFT_APP            1
#endif

#endif /* PUTTY_RCSTUFF_H */
