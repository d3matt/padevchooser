CFLAGS=-Wextra -Wall -g -O0 -pipe `pkg-config --cflags gtk+-2.0 polyplib polyplib-browse polyplib-glib-mainloop libnotify libglade-2.0 gconf-2.0` -I. -DEGG_COMPILATION
LIBS=`pkg-config --libs gtk+-2.0 polyplib polyplib-browse polyplib-glib-mainloop libnotify libglade-2.0 gconf-2.0`

padevchooser: eggtrayicon.o padevchooser.o x11prop.o
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f *.o padevchooser

.PHONY: clean
