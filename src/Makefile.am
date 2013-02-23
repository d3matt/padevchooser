# This file is part of padevchooser.
#
# padevchooser is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# padevchooser is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with padevchooser; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA.

desktopdir = $(datadir)/applications

bin_PROGRAMS=padevchooser

dist_pkgdata_DATA=padevchooser.glade
dist_desktop_DATA=padevchooser.desktop

padevchooser_SOURCES=padevchooser.c x11prop.c x11prop.h browser.h browser.c stubs.c pulsecore/avahi-wrap.c

AM_CPPFLAGS+=-DGLADE_FILE=\"$(pkgdatadir)/padevchooser.glade\" 
AM_CPPFLAGS+=-DDESKTOP_FILE=\"$(desktopdir)/padevchooser.desktop\" 
AM_CPPFLAGS+=-DDESKTOP_DIR=\"$(desktopdir)\"
