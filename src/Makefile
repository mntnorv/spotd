# Compiler flags
INCLUDES = -I/usr/include/alsa
DEFINES = -DVERSION=\"$(VERSION)\"

CFLAGS = -g -Wall -Werror $(INCLUDES) $(DEFINES)
LDFLAGS = $(LIBS)

# Filenames
SOURCES = main.c audio.c alsa-audio.c appkey.c
OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o "$(LOCAL_BIN_DIR)/$@"

.c.o:
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f $(OBJECTS)

.PHONY: clean
