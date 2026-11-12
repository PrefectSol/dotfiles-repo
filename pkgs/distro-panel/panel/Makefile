PREFIX = /usr/local
PKGS = x11 xft xinerama xtst fontconfig
CFLAGS ?= -O2
CFLAGS += -std=c99 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

distro-panel: panel.c config.h colors.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ panel.c $(LDLIBS)

install: distro-panel
	install -Dm755 distro-panel $(DESTDIR)$(PREFIX)/bin/distro-panel

clean:
	rm -f distro-panel

.PHONY: install clean
