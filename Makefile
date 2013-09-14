CFLAGS += -std=c99 -Wall -Wextra -pedantic

thumbstrip: thumbstrip.c
	$(CC) $(CFLAGS) -o $@ $^ `pkg-config --cflags --libs MagickWand`
