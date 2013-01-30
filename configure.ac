# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# This file is part of padevchooser.
#
# padevchooser is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# padevchooser is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with padevchooser; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.

AC_PREREQ([2.69])
AC_INIT([padevchooser],[0.9.4],[mzcnqripubbfre (at) 0pointer (dot) de],,[http://0pointer.de/lennart/projects/padevchooser/])
AC_CONFIG_SRCDIR([src/padevchooser.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign 1.9 -Wall])

if type -p stow > /dev/null && test -d /usr/local/stow ; then
   AC_MSG_NOTICE([*** Found /usr/local/stow: default install prefix set to /usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION} ***])
   ac_default_prefix="/usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION}"
fi

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LN_S
AC_HEADER_STDC
AM_PROG_CC_C_O

PKG_CHECK_MODULES(GUILIBS, [ gtk+-2.0 >= 2.10 libnotify libglade-2.0 gconf-2.0 libgnomeui-2.0 gnome-desktop-2.0 x11 ])

if test -d ../pulseaudio ; then
   PULSE_CFLAGS='-I$(top_srcdir)/../pulseaudio/src'
   PULSE_LIBS='-L$(top_srcdir)/../pulseaudio/src/.libs -lpulse -lpulse-mainloop-glib'
   echo "*** Found pulseaudio in ../pulseaudio, using that version ***"
   AC_SUBST(PULSE_LIBS)
   AC_SUBST(PULSE_CFLAGS)
else
   PKG_CHECK_MODULES(PULSE, [ libpulse >= 0.9.2 libpulse-mainloop-glib >= 0.9.2 ])
fi

PKG_CHECK_MODULES(AVAHI, [avahi-client])

# If using GCC specifiy some additional parameters
if test "x$GCC" = "xyes" ; then
   AM_CFLAGS="-pipe -Wall -W -Wno-unused-parameter"
   AM_CXXFLAGS="$CXXFLAGS -pipe -Wall -W"
fi

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CXXFLAGS)

# LYNX documentation generation
AC_ARG_ENABLE(lynx,
        AS_HELP_STRING(--disable-lynx,Turn off lynx usage for documentation generation),
[case "${enableval}" in
  yes) lynx=yes ;;
  no)  lynx=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-lynx) ;;
esac],[lynx=yes])

if test x$lynx = xyes ; then
   AC_CHECK_PROG(have_lynx, lynx, yes, no)

   if test x$have_lynx = xno ; then
     AC_MSG_ERROR([*** Sorry, you have to install lynx or use --disable-lynx ***])
   fi
fi

AM_CONDITIONAL([USE_LYNX], [test "x$lynx" = xyes])

AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile doc/README.html])
AC_OUTPUT
