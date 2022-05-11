CFLAGS += -std=c99 -Wall -Wextra -pedantic
CFLAGS += $(shell pkg-config --cflags MagickWand)
LIBS   += $(shell pkg-config --libs   MagickWand)

.PHONY: analyze clean

BIN = thumbstrip

$(BIN): thumbstrip.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

analyze: clean
	scan-build --status-bugs $(MAKE)

clean:
	$(RM) $(BIN)
