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

# Compiler flags
INCLUDES = -I/usr/include/alsa
DEFINES = -DVERSION=\"$(VERSION)\"

CFLAGS = -g -Wall -Werror $(INCLUDES) $(DEFINES)
LDFLAGS = $(LIBS)

# Filenames
SOURCES = main.c alsa-audio.c appkey.c audio.c server.c types.c util.c
OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o "$(LOCAL_BIN_DIR)/$@"

.c.o:
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f $(OBJECTS)

.PHONY: clean
