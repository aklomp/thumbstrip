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

struct img {
	char *filename;
	size_t orig_wd;
	size_t orig_ht;
	size_t thumb_ht;
	size_t thumb_wd;
	size_t offs_x;
	size_t offs_y;
	struct img *next;
	MagickWand *mw;
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
	char *desc;
	ExceptionType severity;

	desc = MagickGetException(mw, &severity);
	fprintf(stderr, "%s %s %lu %s\n", GetMagickModule(), desc);
	MagickRelinquishMemory(desc);
}

static bool
img_add (char *filename, size_t thumb_ht, int verbose)
{
	struct img *i;

	if ((i = malloc(sizeof(*i))) == NULL) {
		perror("malloc");
		return false;
	}
	if ((i->filename = malloc(strlen(filename) + 1)) == NULL) {
		perror("malloc");
		free(i);
		return false;
	}
	strcpy(i->filename, filename);
	if (verbose) fprintf(stderr, "reading %s\n", i->filename);
	i->mw = NewMagickWand();
	if (MagickReadImage(i->mw, i->filename) == MagickFalse) {
		goto err;
	}
	i->orig_wd = MagickGetImageWidth(i->mw);
	i->orig_ht = MagickGetImageHeight(i->mw);
	i->next = NULL;

	// Scale down the image:
	i->thumb_ht = thumb_ht;
	i->thumb_wd = (i->orig_wd * i->thumb_ht) / i->orig_ht;
	if (verbose) {
		fprintf(stderr, "Resizing %s from %zdx%zd to %zdx%zd\n"
			, i->filename
			, i->orig_wd
			, i->orig_ht
			, i->thumb_wd
			, i->thumb_ht
		);
	}
	if (MagickResizeImage(i->mw, i->thumb_wd, i->thumb_ht, SincFilter, 1.0) == MagickFalse) {
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
mosaic_layout (size_t canvas_wd, size_t thumb_ht, size_t thumb_space)
{
	size_t col = 0;
	size_t row = 0;

	for (struct img *i = imgs; i; i = i->next) {
		while (col + i->thumb_wd > canvas_wd) {
			if (col == 0) {
				fprintf(stderr, "Image too large: %s\n", i->filename);
				return false;
			}
			nrows++;
			col = 0;
			row += thumb_ht + thumb_space;
		}
		i->offs_x = col;
		i->offs_y = row;
		col += i->thumb_wd + thumb_space;
	}
	return true;
}

static bool
mosaic_render (char *filename, size_t canvas_width, size_t canvas_height)
{
	int ret = true;
	MagickWand *mw = NewMagickWand();
	PixelWand *pw = NewPixelWand();

	PixelSetColor(pw, "white");
	if (MagickNewImage(mw, canvas_width, canvas_height, pw) == MagickFalse) {
		magick_error(mw);
		ret = false;
		goto exit;
	}
	for (struct img *i = imgs; i; i = i->next) {
		if (MagickCompositeImage(mw, i->mw, OverCompositeOp, i->offs_x, i->offs_y) == MagickFalse) {
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
	struct img *i;

	if (filename == NULL) {
		return true;
	}
	if ((f = fopen(filename, "w")) == NULL) {
		perror("fopen()");
		return false;
	}
	for (i = imgs; i; i = i->next) {
		fprintf(f, "%s\t%zd\t%zd\t%zd\t%zd\n"
			, basename(i->filename)
			, i->offs_x
			, i->offs_y
			, i->offs_x + i->thumb_wd
			, i->offs_y + i->thumb_ht
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
	int verbose = 0;
	char *mapfile = NULL;
	char *outfile = DEFAULT_OUTFILE;
	size_t thumb_space = DEFAULT_SPACE;
	size_t thumb_height = DEFAULT_HEIGHT;
	size_t canvas_width = DEFAULT_WIDTH;
	size_t canvas_height;

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
				canvas_width = atoi(optarg);
				break;

			case 'm':
				mapfile = optarg;
				break;

			case 'v':
				verbose = 1;
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
	if (!mosaic_layout(canvas_width, thumb_height, thumb_space)) {
		ret = 1;
		goto exit;
	}
	canvas_height = nrows * thumb_height + (nrows - 1) * thumb_space;

	// Render output image:
	if (!mosaic_render(outfile, canvas_width, canvas_height)) {
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
