`thumbstrip` is a Linux commandline program that creates thumbnail strips from
a set of input images. A thumbnail strip is a single image containing small
thumbnails of all the input images in order, each thumbnail having the same
height. Optionally `thumbstrip` writes a mapfile describing the output.
`thumbstrip` is useful for generating thumbnail images for online photo
galleries.

Say you have four large images with different heights and widths:

    +------+   +------------------+   +--+   +--+
    |      |   |                  |   |  |   +--+
    |      |   |                  |   |  |
    |      |   +------------------+   |  |
    +------+                          |  |
                                      +--+

If you feed these to `thumbstrip`, it will output a thumbnail image that looks
like this:

    +--+  +----------+  ++  +-----+
    |  |  |          |  ||  |     |
    +--+  +----------+  ++  +-----+

The thumbnails that make up the image all have the same height in pixels
(configurable), and are placed left to right with a configurable margin. The
line wraps when the maximum line length is reached. The thumbnails retain the
original aspect ratio of the input images, so the width of the thumbnails can
vary.

Optionally, `thumbstrip` creates a mapfile with the names of the images and
their coordinates in the thumbnail image. Other scripts can use the data in the
mapfile to generate clickmaps and such.

`thumbstrip` uses the ImageMagick library to do the heavy lifting on the image
processing. `thumbstrip` is only concerned with properly resizing and
positioning the thumbnail images.

## Options and usage

Run `thumbstrip` without arguments to get a usage summary:

    -o <outfile>   image file to create, default 'pnm:-'
    -m <mapfile>   map file to create, optional
    -h <height>    height of a row in pixels, default 28
    -s <space>     space between thumbnails in pixels, default 4
    -w <width>     width of a row in pixels, default 732
    -v             be verbose
    -?             print this help screen

The default values for height and width are a bit peculiar: they were chosen to
fit the layout of the author's photo site. You can set your own defaults
directly at the top of the code and recompile. The help message is
self-updating.

The default output is a portable bitmap (PBM) image to standard output. You can
create a PNG or a JPEG from it as follows:

    ./thumbstrip a.jpg b.jpg c.jpg | pnmtopng > strip.png
    ./thumbstrip a.jpg b.jpg c.jpg | cjpeg > strip.jpg

You could also specify `strip.png` or `strip.jpg` directly as the output file
with the `-o` option, and ImageMagick will do the right thing.

# The mapfile

The mapfile looks something like this:

    p1030763.jpg	0	0	41	28
    p1030764.jpg	45	0	86	28
    p1030765.jpg	90	0	131	28
    p1030766.jpg	135	0	176	28

The columns are tab-separated. The first column is the filename, the second is
the x offset, the third is the y offset, the fourth is the x offset plus the
width of the thumbnail, and the fifth is the y offset plus the height of the
thumbnail. These coordinates happened to be convenient for further processing.
It's trivial to modify the code to output different values, such as the width
and height of the thumbnail, or the original dimensions of the image.

## Compiling and installing

This software depends on libMagickWand, which is the ImageMagick support
library. If you have ImageMagick installed, you probably have the library too.
Download the tarball from the `releases` page, then run the following:

    tar xvzf thumbnail-0.1.tar.gz    # untar the package
    cd thumbnail-0.1
    make

This should compile and link the code against ImageMagick, and create a
standalone binary in the current directory called `thumbstrip`. You can move or
install this binary wherever you want.

## License

Thumbstrip is licensed under the GPL version 3.
