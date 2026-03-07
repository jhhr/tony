/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

/*
    mingw_byte_fix.h — force-include header for MinGW + C++17 builds
    -----------------------------------------------------------------
    Problem
    ~~~~~~~
    rpcndr.h (pulled in by <windows.h> unless WIN32_LEAN_AND_MEAN is
    set) contains the unconditional declaration:

        typedef unsigned char byte;

    C++17's <cstddef> introduces std::byte as an enum class.  When
    both end up in scope in the same translation unit, GCC 14 raises a
    hard error:

        error: reference to 'byte' is ambiguous
          candidates are: 'enum class std::byte'
                          'typedef unsigned char byte'

    This fires on virtually every TU in this project because:
      • svcore/system/System.h includes <windows.h> (which drags in
        rpcndr.h via winscard.h), AND
      • Qt6 headers and/or the C++ standard library bring in <cstddef>.

    Fix
    ~~~
    This header is force-included before every TU (via -include on
    the compiler command line — see meson.build).

    We include <windows.h> here with WIN32_LEAN_AND_MEAN pre-defined.
    That causes windows.h to set its own include guard (__WINDOWS_H__)
    while skipping the RPC/COM sub-headers (including rpcndr.h).

    When any later code does #include <windows.h> the include guard
    fires and the header is a no-op — rpcndr.h is therefore never
    processed and the conflicting 'byte' typedef never appears.

    None of the code in this project uses RPC/DCOM types directly, so
    omitting those sub-headers has no practical effect.

    After including windows.h we include <cstddef> so that std::byte
    is already fully defined before any other header has a chance to
    race with it.

    This header is a no-op on non-MinGW / non-C++17 builds.
*/

#ifndef MINGW_BYTE_FIX_H
#define MINGW_BYTE_FIX_H

#if defined(__GNUC__) && defined(_WIN32) && defined(__cplusplus) && __cplusplus >= 201703L

/* Only act if windows.h hasn't been included yet — if it has, we
   are too late to block rpcndr.h, but hopefully the conflict isn't
   present in that translation unit. */
#ifndef _WINDOWS_

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#    define MINGW_BYTE_FIX_DEFINED_LEAN_AND_MEAN
#  endif

#  include <windows.h>   /* sets _WINDOWS_; skips rpcndr.h */

#  ifdef MINGW_BYTE_FIX_DEFINED_LEAN_AND_MEAN
#    undef WIN32_LEAN_AND_MEAN
#    undef MINGW_BYTE_FIX_DEFINED_LEAN_AND_MEAN
#  endif

/* Restore NT types (NTSTATUS etc.) that WIN32_LEAN_AND_MEAN excluded.
   These are used directly by svcore/data/fileio/test/UnsupportedFormat.cpp
   and indirectly by other Windows API consumers in the tree.
   <ntstatus.h> must be included before <winternl.h>; both are safe to
   include after a lean <windows.h> because they do not re-include
   rpcndr.h. */
#  ifndef NTSTATUS
#    include <ntstatus.h>
#  endif
#  include <winternl.h>

#endif /* !_WINDOWS_ */

/* Ensure std::byte is now defined cleanly. */
#include <cstddef>

#endif /* MinGW C++17 */

#endif /* MINGW_BYTE_FIX_H */