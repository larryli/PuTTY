#!/usr/bin/perl

# Take a collection of input image files and convert them into a
# multi-resolution Windows .ICO icon file.
#
# The input images can be treated as having four different colour
# depths:
#
#  - 24-bit true colour
#  - 8-bit with custom palette
#  - 4-bit using the Windows 16-colour palette (see comment below
#    for details)
#  - 1-bit using black and white only.
#
# The images can be supplied in any input format acceptable to
# ImageMagick, but their actual colour usage must already be
# appropriate for the specified mode; this script will not do any
# substantive conversion. So if an image intended to be used in 4-
# or 1-bit mode contains any colour not in the appropriate fixed
# palette, that's a fatal error; if an image to be used in 8-bit
# mode contains more than 256 distinct colours, that's also a fatal
# error.
#
# Command-line syntax is:
#
#   icon.pl -depth imagefile [imagefile...] [-depth imagefile [imagefile...]]
#
# where `-depth' is one of `-24', `-8', `-4' or `-1', and tells the
# script how to treat all the image files given after that option
# until the next depth option. For example, you might execute
#
#   icon.pl -24 48x48x24.png 32x32x24.png -8 32x32x8.png -1 monochrome.png
#
# to build an icon file containing two differently sized 24-bit
# images, one 8-bit image and one black and white image.
#
# Windows .ICO files support a 1-bit alpha channel on all these
# image types. That is, any pixel can be either opaque or fully
# transparent, but not partially transparent. The alpha channel is
# separate from the main image data, meaning that `transparent' is
# not required to take up a palette entry. (So an 8-bit image can
# have 256 distinct _opaque_ colours, plus transparent pixels as
# well.) If the input images have alpha channels, they will be used
# to determine which pixels of the icon are transparent, by simple
# quantisation half way up (e.g. in a PNG image with an 8-bit alpha
# channel, alpha values of 00-7F will be mapped to transparent
# pixels, and 80-FF will become opaque).

# The Windows 16-colour palette consists of:
#  - the eight corners of the colour cube (000000, 0000FF, 00FF00,
#    00FFFF, FF0000, FF00FF, FFFF00, FFFFFF)
#  - dim versions of the seven non-black corners, at 128/255 of the
#    brightness (000080, 008000, 008080, 800000, 800080, 808000,
#    808080)
#  - light grey at 192/255 of full brightness (C0C0C0).
%win16pal = (
    "\x00\x00\x00\x00" => 0,
    "\x00\x00\x80\x00" => 1,
    "\x00\x80\x00\x00" => 2,
    "\x00\x80\x80\x00" => 3,
    "\x80\x00\x00\x00" => 4,
    "\x80\x00\x80\x00" => 5,
    "\x80\x80\x00\x00" => 6,
    "\xC0\xC0\xC0\x00" => 7,
    "\x80\x80\x80\x00" => 8,
    "\x00\x00\xFF\x00" => 9,
    "\x00\xFF\x00\x00" => 10,
    "\x00\xFF\xFF\x00" => 11,
    "\xFF\x00\x00\x00" => 12,
    "\xFF\x00\xFF\x00" => 13,
    "\xFF\xFF\x00\x00" => 14,
    "\xFF\xFF\xFF\x00" => 15,
);
@win16pal = sort { $win16pal{$a} <=> $win16pal{$b} } keys %win16pal;

# The black and white palette consists of black (000000) and white
# (FFFFFF), obviously.
%win2pal = (
    "\x00\x00\x00\x00" => 0,
    "\xFF\xFF\xFF\x00" => 1,
);
@win2pal = sort { $win16pal{$a} <=> $win2pal{$b} } keys %win2pal;

@hdr = ();
@dat = ();

$depth = undef;
foreach $_ (@ARGV) {
    if (/^-(24|8|4|1)$/) {
        $depth = $1;
    } elsif (defined $depth) {
        &readicon($_, $depth);
    } else {
        $usage = 1;
    }
}
if ($usage || length @hdr == 0) {
    print "usage: icon.pl ( -24 | -8 | -4 | -1 ) image [image...]\n";
    print "             [ ( -24 | -8 | -4 | -1 ) image [image...] ...]\n";
    exit 0;
}

# Now write out the output icon file.
print pack "vvv", 0, 1, scalar @hdr; # file-level header
$filepos = 6 + 16 * scalar @hdr;
for ($i = 0; $i < scalar @hdr; $i++) {
    print $hdr[$i];
    print pack "V", $filepos;
    $filepos += length($dat[$i]);
}
for ($i = 0; $i < scalar @hdr; $i++) {
    print $dat[$i];
}

sub readicon {
    my $filename = shift @_;
    my $depth = shift @_;
    my $pix;
    my $i;
    my %pal;

    # Determine the icon's width and height.
    my $w = `identify -format %w $filename`;
    my $h = `identify -format %h $filename`;

    # Read the file in as RGBA data. We flip vertically at this
    # point, to avoid having to do it ourselves (.BMP and hence
    # .ICO are bottom-up).
    my $data = [];
    open IDATA, "convert -flip -depth 8 $filename rgba:- |";
    push @$data, $rgb while (read IDATA,$rgb,4,0) == 4;
    close IDATA;
    # Check we have the right amount of data.
    $xl = $w * $h;
    $al = scalar @$data;
    die "wrong amount of image data ($al, expected $xl) from $filename\n"
      unless $al == $xl;

    # Build the alpha channel now, so we can exclude transparent
    # pixels from the palette analysis. We replace transparent
    # pixels with undef in the data array.
    #
    # We quantise the alpha channel half way up, so that alpha of
    # 0x80 or more is taken to be fully opaque and 0x7F or less is
    # fully transparent. Nasty, but the best we can do without
    # dithering (and don't even suggest we do that!).
    my $x;
    my $y;
    my $alpha = "";

    for ($y = 0; $y < $h; $y++) {
        my $currbyte = 0, $currbits = 0;
        for ($x = 0; $x < (($w+31)|31)-31; $x++) {
            $pix = ($x < $w ? $data->[$y*$w+$x] : "\x00\x00\x00\xFF");
            my @rgba = unpack "CCCC", $pix;
            $currbyte <<= 1;
            $currbits++;
            if ($rgba[3] < 0x80) {
                if ($x < $w) {
                    $data->[$y*$w+$x] = undef;
                }
                $currbyte |= 1; # MS has the alpha channel inverted :-)
            } else {
                # Might as well flip RGBA into BGR0 while we're here.
                if ($x < $w) {
                    $data->[$y*$w+$x] = pack "CCCC",
                      $rgba[2], $rgba[1], $rgba[0], 0;
                }
            }
            if ($currbits >= 8) {
                $alpha .= pack "C", $currbyte;
                $currbits -= 8;
            }
        }
    }

    # For an 8-bit image, check we have at most 256 distinct
    # colours, and build the palette.
    %pal = ();
    if ($depth == 8) {
        my $palindex = 0;
        foreach $pix (@$data) {
            next unless defined $pix;
            $pal{$pix} = $palindex++ unless defined $pal{$pix};
        }
        die "too many colours in 8-bit image $filename\n" unless $palindex <= 256;
    } elsif ($depth == 4) {
        %pal = %win16pal;
    } elsif ($depth == 1) {
        %pal = %win2pal;
    }

    my $raster = "";
    if ($depth < 24) {
        # For a non-24-bit image, flatten the image into one palette
        # index per pixel.
        $pad = 32 / $depth; # number of pixels to pad scanline to 4-byte align
        $pmask = $pad-1;
        for ($y = 0; $y < $h; $y++) {
            my $currbyte = 0, $currbits = 0;
            for ($x = 0; $x < (($w+$pmask)|$pmask)-$pmask; $x++) {
                $currbyte <<= $depth;
                $currbits += $depth;
                if ($x < $w && defined ($pix = $data->[$y*$w+$x])) {
                    if (!defined $pal{$pix}) {
                        $pixhex = sprintf "%02x%02x%02x", unpack "CCC", $pix;
                        die "illegal colour value $pixhex at pixel ($x,$y) in $filename\n";
                    }
                    $currbyte |= $pal{$pix};
                }
                if ($currbits >= 8) {
                    $raster .= pack "C", $currbyte;
                    $currbits -= 8;
                }
            }
        }
    } else {
        # For a 24-bit image, reverse the order of the R,G,B values
        # and stick a padding zero on the end.
        #
        # (In this loop we don't need to bother padding the
        # scanline out to a multiple of four bytes, because every
        # pixel takes four whole bytes anyway.)
        for ($i = 0; $i < scalar @$data; $i++) {
            if (defined $data->[$i]) {
                $raster .= $data->[$i];
            } else {
                $raster .= "\x00\x00\x00\x00";
            }
        }
        $depth = 32; # and adjust this
    }

    # Prepare the icon data. First the header...
    my $data = pack "VVVvvVVVVVV",
      40, # size of bitmap info header
      $w, # icon width
      $h*2, # icon height (x2 to indicate the subsequent alpha channel)
      1, # 1 plane (common to all MS image formats)
      $depth, # bits per pixel
      0, # no compression
      length $raster, # image size
      0, 0, 0, 0; # resolution, colours used, colours important (ignored)
    # ... then the palette ...
    if ($depth <= 8) {
        my $ncols = (1 << $depth);
        my $palette = "\x00\x00\x00\x00" x $ncols;
        foreach $i (keys %pal) {
            substr($palette, $pal{$i}*4, 4) = $i;
        }
        $data .= $palette;
    }
    # ... the raster data we already had ready ...
    $data .= $raster;
    # ... and the alpha channel we already had as well.
    $data .= $alpha;

    # Prepare the header which will represent this image in the
    # icon file.
    my $header = pack "CCCCvvV",
      $w, $h, # width and height (this time the real height)
      1 << $depth, # number of colours, if less than 256
      0, # reserved
      1, # planes
      $depth, # bits per pixel
      length $data; # size of real icon data

    push @hdr, $header;
    push @dat, $data;
}
