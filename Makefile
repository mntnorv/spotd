#
# The MIT License (MIT)
# 
# Copyright (c) 2015 Mantas Norvaiša
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# This file is part of spotd.
#

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
export CC ?= clang

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
