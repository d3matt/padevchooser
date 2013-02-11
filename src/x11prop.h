#ifndef foox11prophfoo
#define foox11prophfoo

/***
  This file is part of pulseaudio.

  pulseaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  pulseaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with pulseaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <sys/types.h>

#include <X11/Xlib.h>

void x11_set_prop(Display *d, const char *name, const char *data);
void x11_del_prop(Display *d, const char *name);
char* x11_get_prop(Display *d, const char *name, char *p, size_t l);

#endif
