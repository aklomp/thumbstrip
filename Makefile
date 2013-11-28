CFLAGS += -std=c99 -Wall -Wextra -pedantic

.PHONY: clean

thumbstrip: thumbstrip.c
	$(CC) $(CFLAGS) -o $@ $^ `pkg-config --cflags --libs MagickWand`

clean:
	rm -f thumbstrip
