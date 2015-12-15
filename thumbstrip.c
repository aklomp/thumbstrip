/* thumbstrip: create a thumbnail image from multiple input images
 * Copyright (C) 2013  Alfred Klomp
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE		// Unlock basename() in string.h
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wand/MagickWand.h>

#define JPEG_QUALITY		70
#define DEFAULT_HEIGHT		28
#define DEFAULT_WIDTH		732
#define DEFAULT_SPACE		4
#define DEFAULT_OUTFILE		"pnm:-"		// MagickWand code for "PNM to stdout"

#define STR_EXPAND(x)		#x
#define NUM_TO_STR(x)		STR_EXPAND(x)

struct size {
	size_t wd;
	size_t ht;
};

struct pos {
	size_t x;
	size_t y;
};

struct img {
	char *filename;
	struct img *next;
	MagickWand *mw;
	struct size orig;
	struct size thumb;
	struct pos offset;
};

int nrows = 1;
struct img *imgs = NULL;
struct img *last = NULL;

static void
print_usage (void)
{
	fputs(
	"Creates a strip of thumbnail images, and an optional mapfile.\n"
	"  -o <outfile>   image file to create, default '" DEFAULT_OUTFILE "'\n"
	"  -m <mapfile>   map file to create, optional\n"
	"  -h <height>    height of a row in pixels, default " NUM_TO_STR(DEFAULT_HEIGHT) "\n"
	"  -s <space>     space between thumbnails in pixels, default " NUM_TO_STR(DEFAULT_SPACE) "\n"
	"  -w <width>     width of a row in pixels, default " NUM_TO_STR(DEFAULT_WIDTH) "\n"
	"  -v             be verbose\n"
	"  -?             print this help screen\n"
	, stderr);
}

static void
magick_error (MagickWand *mw)
{
	ExceptionType severity;

	char *desc = MagickGetException(mw, &severity);
	fprintf(stderr, "%s %s %lu %s\n", GetMagickModule(), desc);
	MagickRelinquishMemory(desc);
}

static bool
img_add (char *filename, size_t thumb_ht, bool verbose)
{
	struct img *i;

	// Allocate memory for image object:
	if ((i = malloc(sizeof(*i))) == NULL) {
		perror("malloc");
		return false;
	}

	// Duplicate filename:
	if ((i->filename = strdup(filename)) == NULL) {
		perror("strdup");
		free(i);
		return false;
	}

	if (verbose)
		fprintf(stderr, "Reading %s\n", i->filename);

	i->mw = NewMagickWand();
	if (MagickReadImage(i->mw, i->filename) == MagickFalse) {
		goto err;
	}
	i->orig.wd = MagickGetImageWidth(i->mw);
	i->orig.ht = MagickGetImageHeight(i->mw);
	i->next = NULL;

	// Scale down the image:
	i->thumb.ht = thumb_ht;
	i->thumb.wd = (i->orig.wd * i->thumb.ht) / i->orig.ht;
	if (verbose) {
		fprintf(stderr, "Resizing %s from %zdx%zd to %zdx%zd\n"
			, i->filename
			, i->orig.wd
			, i->orig.ht
			, i->thumb.wd
			, i->thumb.ht
		);
	}
	if (MagickResizeImage(i->mw, i->thumb.wd, i->thumb.ht, SincFilter, 1.0) == MagickFalse) {
		goto err;
	}
	if (MagickUnsharpMaskImage(i->mw, 1.0, 0.5, 1.0, 1.0) == MagickFalse) {
		goto err;
	}
	if (imgs == NULL) {
		imgs = last = i;
	}
	else {
		last = last->next = i;
	}
	return true;

err:	magick_error(i->mw);
	DestroyMagickWand(i->mw);
	free(i->filename);
	free(i);
	return false;
}

static void
imgs_destroy (void)
{
	struct img *i = imgs;
	struct img *j;

	while (i) {
		j = i->next;
		if (i->mw != NULL) {
			DestroyMagickWand(i->mw);
		}
		free(i->filename);
		free(i);
		i = j;
	}
}

static bool
mosaic_layout (const struct size *canvas, size_t thumb_ht, size_t thumb_space)
{
	size_t col = 0;
	size_t row = 0;

	for (struct img *i = imgs; i; i = i->next) {
		while (col + i->thumb.wd > canvas->wd) {
			if (col == 0) {
				fprintf(stderr, "Image too large: %s\n", i->filename);
				return false;
			}
			nrows++;
			col = 0;
			row += thumb_ht + thumb_space;
		}
		i->offset.x = col;
		i->offset.y = row;
		col += i->thumb.wd + thumb_space;
	}
	return true;
}

static bool
mosaic_render (char *filename, const struct size *canvas)
{
	int ret = true;
	MagickWand *mw = NewMagickWand();
	PixelWand *pw = NewPixelWand();

	PixelSetColor(pw, "white");
	if (MagickNewImage(mw, canvas->wd, canvas->ht, pw) == MagickFalse) {
		magick_error(mw);
		ret = false;
		goto exit;
	}
	for (struct img *i = imgs; i; i = i->next) {
		if (MagickCompositeImage(mw, i->mw, OverCompositeOp, i->offset.x, i->offset.y) == MagickFalse) {
			magick_error(mw);
			ret = false;
			goto exit;
		}
	}
	if (MagickSetImageCompressionQuality(mw, JPEG_QUALITY) == MagickFalse
	 || MagickWriteImage(mw, filename) == MagickFalse) {
		magick_error(mw);
		ret = false;
		goto exit;
	}

exit:	DestroyMagickWand(mw);
	DestroyPixelWand(pw);
	return ret;
}

static bool
mosaic_mapfile (char *filename)
{
	FILE *f;

	if (filename == NULL) {
		return true;
	}
	if ((f = fopen(filename, "w")) == NULL) {
		perror("fopen");
		return false;
	}
	for (struct img *i = imgs; i; i = i->next) {
		fprintf(f, "%s\t%zd\t%zd\t%zd\t%zd\n"
			, basename(i->filename)
			, i->offset.x
			, i->offset.y
			, i->offset.x + i->thumb.wd
			, i->offset.y + i->thumb.ht
		);
	}
	fclose(f);
	return true;
}

int
main (int argc, char *argv[])
{
	int opt;
	int ret = 0;
	bool verbose = false;
	char *mapfile = NULL;
	char *outfile = DEFAULT_OUTFILE;
	size_t thumb_space = DEFAULT_SPACE;
	size_t thumb_height = DEFAULT_HEIGHT;
	struct size canvas = { .wd = DEFAULT_WIDTH };

	while ((opt = getopt(argc, argv, "h:m:s:o:w:v?")) != -1) {
		switch (opt) {
			case 'h':
				thumb_height = atoi(optarg);
				break;

			case 's':
				thumb_space = atoi(optarg);
				break;

			case 'o':
				outfile = optarg;
				break;

			case 'w':
				canvas.wd = atoi(optarg);
				break;

			case 'm':
				mapfile = optarg;
				break;

			case 'v':
				verbose = true;
				break;

			case '?':
				print_usage();
				return 0;
		}
	}
	MagickWandGenesis();

	// Open all images, scale them, add to linked list:
	for (; optind < argc; optind++) {
		if (!img_add(argv[optind], thumb_height, verbose)) {
			ret = 1;
			goto exit;
		}
	}
	if (imgs == NULL) {
		fprintf(stderr, "No input images given\n\n");
		print_usage();
		goto exit;
	}
	// Layout images into rows:
	if (!mosaic_layout(&canvas, thumb_height, thumb_space)) {
		ret = 1;
		goto exit;
	}
	canvas.ht = nrows * thumb_height + (nrows - 1) * thumb_space;

	// Render output image:
	if (!mosaic_render(outfile, &canvas)) {
		ret = 1;
		goto exit;
	}

	// Make mapfile if desired:
	if (!mosaic_mapfile(mapfile)) {
		ret = 1;
		goto exit;
	}

exit:	imgs_destroy();
	MagickWandTerminus();
	return ret;
}
