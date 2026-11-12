PREFIX = /usr/local
CFLAGS ?= -O2
CFLAGS += -std=c99 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE $(shell pkg-config --cflags x11)
X11LIBS = $(shell pkg-config --libs x11)
BIN = distro-clipserve distro-lang sb-sys

all: $(BIN)

distro-clipserve: clipserve.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ clipserve.c $(X11LIBS)

distro-lang: lang.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ lang.c $(X11LIBS)

sb-sys: sbsys.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ sbsys.c

install: all
	install -Dm755 -t $(DESTDIR)$(PREFIX)/bin $(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean
