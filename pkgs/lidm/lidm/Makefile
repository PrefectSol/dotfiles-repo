VERSION := 2.0.2
.DEFAULT_GOAL := lidm

CDIR = src
IDIR = include
ODIR = dist
PREFIX = /usr/local

INFO_GIT_REV ?= v$(VERSION)
INFO_BUILD_TS ?= $(shell date +%s)

CFLAGS ?= -O2 -Wall -Wextra -fdata-sections -ffunction-sections
CPPFLAGS ?=
_DFLAGS = \
	-DLIDM_VERSION=\"$(VERSION)\" \
	-DLIDM_GIT_REV=\"$(INFO_GIT_REV)\" \
	-DLIDM_BUILD_TS=$(INFO_BUILD_TS)
ALLFLAGS = $(_DFLAGS) $(CFLAGS) $(CPPFLAGS) -I$(IDIR)
LDFLAGS ?= -Wl,--gc-sections
LIBS = -lpam

DEPS = $(wildcard $(IDIR)/*.h $(IDIR)/**/*.h)
_SOURCES = $(wildcard $(CDIR)/*.c) $(wildcard $(CDIR)/**/*.c)
OBJ = $(patsubst $(CDIR)/%.c,$(ODIR)/%.o,$(_SOURCES))

$(ODIR)/%.o: $(CDIR)/%.c $(DEPS)
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(ALLFLAGS)

lidm: $(OBJ)
	$(CC) -o $@ $^ $(ALLFLAGS) $(LIBS) $(LDFLAGS)

clean:
	rm -rf $(ODIR) lidm

install: lidm
	install -Dm755 lidm $(DESTDIR)$(PREFIX)/bin/lidm
	install -Dm644 lidm.ini $(DESTDIR)/etc/lidm.ini
	install -Dm644 lidm.service $(DESTDIR)$(PREFIX)/lib/systemd/system/lidm.service
	install -Dm644 assets/man/lidm.1 $(DESTDIR)$(PREFIX)/share/man/man1/lidm.1
	install -Dm644 assets/man/lidm-config.5 $(DESTDIR)$(PREFIX)/share/man/man5/lidm-config.5

.PHONY: clean install
