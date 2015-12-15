CFLAGS  += -std=c99 -Wall -Wextra -pedantic
CFLAGS  += $(shell pkg-config --cflags MagickWand)
LDFLAGS += $(shell pkg-config --libs   MagickWand)

.PHONY: analyze clean

BIN = thumbstrip

$(BIN): thumbstrip.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

analyze: clean
	scan-build --status-bugs make

clean:
	rm -f $(BIN)
