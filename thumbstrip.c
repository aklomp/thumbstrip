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

struct state {
	struct img *imgs;
	struct img *last;
	struct {
		char *map;
		char *out;
	} file;
	bool verbose;
	bool usage;
	struct {
		size_t space;
		size_t ht;
	} thumb;
	struct size canvas;
	size_t nrows;
};

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
img_add (char *filename, struct state *state)
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

	if (state->verbose)
		fprintf(stderr, "Reading %s\n", i->filename);

	i->mw = NewMagickWand();
	if (MagickReadImage(i->mw, i->filename) == MagickFalse)
		goto err;

	i->orig.wd = MagickGetImageWidth(i->mw);
	i->orig.ht = MagickGetImageHeight(i->mw);
	i->next = NULL;

	// Scale down the image:
	i->thumb.ht = state->thumb.ht;
	i->thumb.wd = (i->orig.wd * i->thumb.ht) / i->orig.ht;
	if (state->verbose) {
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
	if (state->imgs == NULL) {
		state->imgs = state->last = i;
	}
	else {
		state->last = state->last->next = i;
	}
	return true;

err:	magick_error(i->mw);
	DestroyMagickWand(i->mw);
	free(i->filename);
	free(i);
	return false;
}

static void
imgs_destroy (struct state *state)
{
	struct img *i = state->imgs;
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
mosaic_layout (struct state *state)
{
	size_t col = 0;
	size_t row = 0;

	for (struct img *i = state->imgs; i; i = i->next) {
		while (col + i->thumb.wd > state->canvas.wd) {
			if (col == 0) {
				fprintf(stderr, "Image too large: %s\n", i->filename);
				return false;
			}
			state->nrows++;
			col = 0;
			row += state->thumb.ht + state->thumb.space;
		}
		i->offset.x = col;
		i->offset.y = row;
		col += i->thumb.wd + state->thumb.space;
	}
	return true;
}

static bool
mosaic_render (const struct state *state)
{
	int ret = true;
	MagickWand *mw = NewMagickWand();
	PixelWand  *pw = NewPixelWand();

	// Create blank canvas image:
	PixelSetColor(pw, "white");
	if (MagickNewImage(mw, state->canvas.wd, state->canvas.ht, pw) == MagickFalse) {
		magick_error(mw);
		ret = false;
		goto exit;
	}

	// Composite thumbnail images onto canvas:
	for (const struct img *i = state->imgs; i; i = i->next) {
		if (MagickCompositeImage(mw, i->mw, OverCompositeOp, i->offset.x, i->offset.y) == MagickFalse) {
			magick_error(mw);
			ret = false;
			goto exit;
		}
	}

	// Write output image:
	if (MagickSetImageCompressionQuality(mw, JPEG_QUALITY) == MagickFalse
	 || MagickWriteImage(mw, state->file.out) == MagickFalse) {
		magick_error(mw);
		ret = false;
		goto exit;
	}

	// Cleanup:
exit:	DestroyMagickWand(mw);
	DestroyPixelWand(pw);

	return ret;
}

static bool
mosaic_mapfile (const struct state *state)
{
	FILE *f;

	if (!state->file.map)
		return true;

	if ((f = fopen(state->file.map, "w")) == NULL) {
		perror("fopen");
		return false;
	}
	for (const struct img *i = state->imgs; i; i = i->next) {
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

static void
parse_options (int argc, char *argv[], struct state *state)
{
	int opt;

	// Set defaults:
	state->nrows       = 1;
	state->file.out    = DEFAULT_OUTFILE;
	state->thumb.space = DEFAULT_SPACE;
	state->thumb.ht    = DEFAULT_HEIGHT;
	state->canvas.wd   = DEFAULT_WIDTH;

	// Override from options:
	while ((opt = getopt(argc, argv, "h:m:s:o:w:v?")) != -1) {
		switch (opt) {
		case 'h':
			state->thumb.ht = atoi(optarg);
			break;

		case 's':
			state->thumb.space = atoi(optarg);
			break;

		case 'o':
			state->file.out = optarg;
			break;

		case 'w':
			state->canvas.wd = atoi(optarg);
			break;

		case 'm':
			state->file.map = optarg;
			break;

		case 'v':
			state->verbose = true;
			break;

		case '?':
			state->usage = true;
			break;
		}
	}
}

int
main (int argc, char *argv[])
{
	int ret = 0;
	struct state state = { 0 };

	// Parse commandline options:
	parse_options(argc, argv, &state);

	// Quick exit if usage text requested:
	if (state.usage) {
		print_usage();
		return ret;
	}

	// Initialize MagicWand:
	MagickWandGenesis();

	// Open all images, scale them, add to linked list:
	for (; optind < argc; optind++) {
		if (!img_add(argv[optind], &state)) {
			ret = 1;
			goto exit;
		}
	}

	// Check that we have images:
	if (!state.imgs) {
		fprintf(stderr, "No input images given\n\n");
		print_usage();
		goto exit;
	}

	// Layout images into rows:
	if (!mosaic_layout(&state)) {
		ret = 1;
		goto exit;
	}

	// Calculate height of canvas:
	state.canvas.ht  = state.nrows * state.thumb.ht;
	state.canvas.ht += (state.nrows - 1) * state.thumb.space;

	// Render output image:
	if (!mosaic_render(&state)) {
		ret = 1;
		goto exit;
	}

	// Make mapfile if desired:
	if (!mosaic_mapfile(&state)) {
		ret = 1;
		goto exit;
	}

	// Cleanup:
exit:	imgs_destroy(&state);
	MagickWandTerminus();

	return ret;
}
