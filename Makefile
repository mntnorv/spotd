# App info
export APPNAME = spotd
export VERSION = 0.0.1
export DATE = 2015-04-04

# Filenames
export EXECUTABLE = spotd
export MANPAGE = spotd.1

# Libs
export LIBS = -lspotify -lpthread -lasound

# Compiler
export CC ?= gcc

# Directories
export LOCAL_BIN_DIR = $(CURDIR)/bin

PREFIX ?= /usr
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

# Dist files
DIST_PATTERNS = *.[ch] *.sh Makefile
DIST_DIRS = src
DIST_FILES = Makefile LICENSE README.md spotd.1 $(foreach dir, $(DIST_DIRS), $(foreach pattern, $(DIST_PATTERNS), $(wildcard $(dir)/$(pattern))))
DIST_PATH = dist
TARNAME = $(APPNAME)-$(VERSION)
TARFILE = $(DIST_PATH)/$(TARNAME).tar.gz

all:
	mkdir -p "$(LOCAL_BIN_DIR)"
	@$(MAKE) -C src

install:
	install -Dm755 "$(LOCAL_BIN_DIR)/$(EXECUTABLE)" "$(DESTDIR)$(BINDIR)/$(EXECUTABLE)"
	install -Dm644 "$(MANPAGE)" "$(DESTDIR)$(MANDIR)/$(MANPAGE)"
	@sed -i -e 's/@VERSION@/'$(VERSION)'/' "$(DESTDIR)$(MANDIR)/$(MANPAGE)"
	@sed -i -e 's/@DATE@/'$(DATE)'/' "$(DESTDIR)$(MANDIR)/$(MANPAGE)"
	gzip -9 -f "$(DESTDIR)$(MANDIR)/$(MANPAGE)"

dist: clean $(TARFILE)

test:
	@$(MAKE) -C tests test

clean:
	rm -rf "$(LOCAL_BIN_DIR)" "$(DIST_PATH)"
	@$(MAKE) -C src clean

$(TARFILE): $(DIST_FILES)
	mkdir -p $(DIST_PATH)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	rsync -R $(DIST_FILES) $(TARNAME)
	tar -czf $(TARFILE) $(TARNAME)
	rm -rf $(TARNAME)

.PHONY: install clean test dist
