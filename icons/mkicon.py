#!/usr/bin/env python3

from __future__ import division

import sys
import decimal
import math

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

# Python code which draws the PuTTY icon components at a range of
# sizes.

# TODO
# ----
#
#  - use of alpha blending
#     + try for variable-transparency borders
#
#  - can we integrate the Mac icons into all this? Do we want to?

# Python 3 prefers round-to-even.  Emulate Python 2's behaviour instead.
def round(number):
    return float(
        decimal.Decimal(number).to_integral(rounding=decimal.ROUND_HALF_UP))

def pixel(x, y, colour, canvas):
    canvas[(int(x),int(y))] = colour

def overlay(src, x, y, dst):
    x = int(x)
    y = int(y)
    for (sx, sy), colour in src.items():
        dst[sx+x, sy+y] = blend(colour, dst.get((sx+x, sy+y), cT))

def finalise(canvas):
    for k in canvas.keys():
        canvas[k] = finalisepix(canvas[k])

def bbox(canvas):
    minx, miny, maxx, maxy = None, None, None, None
    for (x, y) in canvas.keys():
        if minx == None:
            minx, miny, maxx, maxy = x, y, x+1, y+1
        else:
            minx = min(minx, x)
            miny = min(miny, y)
            maxx = max(maxx, x+1)
            maxy = max(maxy, y+1)
    return (minx, miny, maxx, maxy)

def topy(canvas):
    miny = {}
    for (x, y) in canvas.keys():
        miny[x] = min(miny.get(x, y), y)
    return miny

def render(canvas, minx, miny, maxx, maxy):
    w = maxx - minx
    h = maxy - miny
    ret = []
    for y in range(h):
        ret.append([outpix(cT)] * w)
    for (x, y), colour in canvas.items():
        if x >= minx and x < maxx and y >= miny and y < maxy:
            ret[y-miny][x-minx] = outpix(colour)
    return ret

# Code to actually draw pieces of icon. These don't generally worry
# about positioning within a canvas; they just draw at a standard
# location, return some useful coordinates, and leave composition
# to other pieces of code.

sqrthash = {}
def memoisedsqrt(x):
    if x not in sqrthash:
        sqrthash[x] = math.sqrt(x)
    return sqrthash[x]

BR, TR, BL, TL = list(range(4)) # enumeration of quadrants for border()

def border(canvas, thickness, squarecorners, out={}):
    # I haven't yet worked out exactly how to do borders in a
    # properly alpha-blended fashion.
    #
    # When you have two shades of dark available (half-dark H and
    # full-dark F), the right sequence of circular border sections
    # around a pixel x starts off with these two layouts:
    #
    #   H    F
    #  HxH  FxF
    #   H    F
    #
    # Where it goes after that I'm not entirely sure, but I'm
    # absolutely sure those are the right places to start. However,
    # every automated algorithm I've tried has always started off
    # with the two layouts
    #
    #   H   HHH
    #  HxH  HxH
    #   H   HHH
    #
    # which looks much worse. This is true whether you do
    # pixel-centre sampling (define an inner circle and an outer
    # circle with radii differing by 1, set any pixel whose centre
    # is inside the inner circle to F, any pixel whose centre is
    # outside the outer one to nothing, interpolate between the two
    # and round sensibly), _or_ whether you plot a notional circle
    # of a given radius and measure the actual _proportion_ of each
    # pixel square taken up by it.
    #
    # It's not clear what I should be doing to prevent this. One
    # option is to attempt error-diffusion: Ian Jackson proved on
    # paper that if you round each pixel's ideal value to the
    # nearest of the available output values, then measure the
    # error at each pixel, propagate that error outwards into the
    # original values of the surrounding pixels, and re-round
    # everything, you do get the correct second stage. However, I
    # haven't tried it at a proper range of radii.
    #
    # Another option is that the automated mechanisms described
    # above would be entirely adequate if it weren't for the fact
    # that the human visual centres are adapted to detect
    # horizontal and vertical lines in particular, so the only
    # place you have to behave a bit differently is at the ends of
    # the top and bottom row of pixels in the circle, and the top
    # and bottom of the extreme columns.
    #
    # For the moment, what I have below is a very simple mechanism
    # which always uses only one alpha level for any given border
    # thickness, and which seems to work well enough for Windows
    # 16-colour icons. Everything else will have to wait.

    thickness = memoisedsqrt(thickness)

    if thickness < 0.9:
        darkness = 0.5
    else:
        darkness = 1
    if thickness < 1: thickness = 1
    thickness = round(thickness - 0.5) + 0.3

    out["borderthickness"] = thickness

    dmax = int(round(thickness))
    if dmax < thickness: dmax = dmax + 1

    cquadrant = [[0] * (dmax+1) for x in range(dmax+1)]
    squadrant = [[0] * (dmax+1) for x in range(dmax+1)]

    for x in range(dmax+1):
        for y in range(dmax+1):
            if max(x, y) < thickness:
                squadrant[x][y] = darkness
            if memoisedsqrt(x*x+y*y) < thickness:
                cquadrant[x][y] = darkness

    bvalues = {}
    for (x, y), colour in canvas.items():
        for dx in range(-dmax, dmax+1):
            for dy in range(-dmax, dmax+1):
                quadrant = 2 * (dx < 0) + (dy < 0)
                if (x, y, quadrant) in squarecorners:
                    bval = squadrant[abs(dx)][abs(dy)]
                else:
                    bval = cquadrant[abs(dx)][abs(dy)]
                if bvalues.get((x+dx,y+dy),0) < bval:
                    bvalues[(x+dx,y+dy)] = bval

    for (x, y), value in bvalues.items():
        if (x,y) not in canvas:
            canvas[(x,y)] = dark(value)

def sysbox(size, out={}):
    canvas = {}

    # The system box of the computer.

    height = int(round(3.6*size))
    width = int(round(16.51*size))
    depth = int(round(2*size))
    highlight = int(round(1*size))
    bothighlight = int(round(1*size))

    out["sysboxheight"] = height

    floppystart = int(round(19*size)) # measured in half-pixels
    floppyend = int(round(29*size)) # measured in half-pixels
    floppybottom = height - bothighlight
    floppyrheight = 0.7 * size
    floppyheight = int(round(floppyrheight))
    if floppyheight < 1:
        floppyheight = 1
    floppytop = floppybottom - floppyheight

    # The front panel is rectangular.
    for x in range(width):
        for y in range(height):
            grey = 3
            if x < highlight or y < highlight:
                grey = grey + 1
            if x >= width-highlight or y >= height-bothighlight:
                grey = grey - 1
            if y < highlight and x >= width-highlight:
                v = (highlight-1-y) - (x-(width-highlight))
                if v < 0:
                    grey = grey - 1
                elif v > 0:
                    grey = grey + 1
            if y >= floppytop and y < floppybottom and \
            2*x+2 > floppystart and 2*x < floppyend:
                if 2*x >= floppystart and 2*x+2 <= floppyend and \
                floppyrheight >= 0.7:
                    grey = 0
                else:
                    grey = 2
            pixel(x, y, greypix(grey/4.0), canvas)

    # The side panel is a parallelogram.
    for x in range(depth):
        for y in range(height):
            pixel(x+width, y-(x+1), greypix(0.5), canvas)

    # The top panel is another parallelogram.
    for x in range(width-1):
        for y in range(depth):
            grey = 3
            if x >= width-1 - highlight:
                grey = grey + 1
            pixel(x+(y+1), -(y+1), greypix(grey/4.0), canvas)

    # And draw a border.
    border(canvas, size, [], out)

    return canvas

def monitor(size):
    canvas = {}

    # The computer's monitor.

    height = int(round(9.55*size))
    width = int(round(11.49*size))
    surround = int(round(1*size))
    botsurround = int(round(2*size))
    sheight = height - surround - botsurround
    swidth = width - 2*surround
    depth = int(round(2*size))
    highlight = int(round(math.sqrt(size)))
    shadow = int(round(0.55*size))

    # The front panel is rectangular.
    for x in range(width):
        for y in range(height):
            if x >= surround and y >= surround and \
            x < surround+swidth and y < surround+sheight:
                # Screen.
                sx = (float(x-surround) - swidth//3) / swidth
                sy = (float(y-surround) - sheight//3) / sheight
                shighlight = 1.0 - (sx*sx+sy*sy)*0.27
                pix = bluepix(shighlight)
                if x < surround+shadow or y < surround+shadow:
                    pix = blend(cD, pix) # sharp-edged shadow on top and left
            else:
                # Complicated double bevel on the screen surround.

                # First, the outer bevel. We compute the distance
                # from this pixel to each edge of the front
                # rectangle.
                list = [
                (x, +1),
                (y, +1),
                (width-1-x, -1),
                (height-1-y, -1)
                ]
                # Now sort the list to find the distance to the
                # _nearest_ edge, or the two joint nearest.
                list.sort()
                # If there's one nearest edge, that determines our
                # bevel colour. If there are two joint nearest, our
                # bevel colour is their shared one if they agree,
                # and neutral otherwise.
                outerbevel = 0
                if list[0][0] < list[1][0] or list[0][1] == list[1][1]:
                    if list[0][0] < highlight:
                        outerbevel = list[0][1]

                # Now, the inner bevel. We compute the distance
                # from this pixel to each edge of the screen
                # itself.
                list = [
                (surround-1-x, -1),
                (surround-1-y, -1),
                (x-(surround+swidth), +1),
                (y-(surround+sheight), +1)
                ]
                # Now we sort to find the _maximum_ distance, which
                # conveniently ignores any less than zero.
                list.sort()
                # And now the strategy is pretty much the same as
                # above, only we're working from the opposite end
                # of the list.
                innerbevel = 0
                if list[-1][0] > list[-2][0] or list[-1][1] == list[-2][1]:
                    if list[-1][0] >= 0 and list[-1][0] < highlight:
                        innerbevel = list[-1][1]

                # Now we know the adjustment we want to make to the
                # pixel's overall grey shade due to the outer
                # bevel, and due to the inner one. We break a tie
                # in favour of a light outer bevel, but otherwise
                # add.
                grey = 3
                if outerbevel > 0 or outerbevel == innerbevel:
                    innerbevel = 0
                grey = grey + outerbevel + innerbevel

                pix = greypix(grey / 4.0)

            pixel(x, y, pix, canvas)

    # The side panel is a parallelogram.
    for x in range(depth):
        for y in range(height):
            pixel(x+width, y-x, greypix(0.5), canvas)

    # The top panel is another parallelogram.
    for x in range(width):
        for y in range(depth-1):
            pixel(x+(y+1), -(y+1), greypix(0.75), canvas)

    # And draw a border.
    border(canvas, size, [(0,int(height-1),BL)])

    return canvas

def computer(size):
    # Monitor plus sysbox.
    out = {}
    m = monitor(size)
    s = sysbox(size, out)
    x = int(round((2+size/(size+1))*size))
    y = int(out["sysboxheight"] + out["borderthickness"])
    mb = bbox(m)
    sb = bbox(s)
    xoff = sb[0] - mb[0] + x
    yoff = sb[3] - mb[3] - y
    overlay(m, xoff, yoff, s)
    return s

def lightning(size):
    canvas = {}

    # The lightning bolt motif.

    # We always want this to be an even number of pixels in height,
    # and an odd number in width.
    width = round(7*size) * 2 - 1
    height = round(8*size) * 2

    # The outer edge of each side of the bolt goes to this point.
    outery = round(8.4*size)
    outerx = round(11*size)

    # And the inner edge goes to this point.
    innery = height - 1 - outery
    innerx = round(7*size)

    for y in range(int(height)):
        list = []
        if y <= outery:
            list.append(width-1-int(outerx * float(y) / outery + 0.3))
        if y <= innery:
            list.append(width-1-int(innerx * float(y) / innery + 0.3))
        y0 = height-1-y
        if y0 <= outery:
            list.append(int(outerx * float(y0) / outery + 0.3))
        if y0 <= innery:
            list.append(int(innerx * float(y0) / innery + 0.3))
        list.sort()
        for x in range(int(list[0]), int(list[-1]+1)):
            pixel(x, y, cY, canvas)

    # And draw a border.
    border(canvas, size, [(int(width-1),0,TR), (0,int(height-1),BL)])

    return canvas

def document(size):
    canvas = {}

    # The document used in the PSCP/PSFTP icon.

    width = round(13*size)
    height = round(16*size)

    lineht = round(1*size)
    if lineht < 1: lineht = 1
    linespc = round(0.7*size)
    if linespc < 1: linespc = 1
    nlines = int((height-linespc)/(lineht+linespc))
    height = nlines*(lineht+linespc)+linespc # round this so it fits better

    # Start by drawing a big white rectangle.
    for y in range(int(height)):
        for x in range(int(width)):
            pixel(x, y, cW, canvas)

    # Now draw lines of text.
    for line in range(nlines):
        # Decide where this line of text begins.
        if line == 0:
            start = round(4*size)
        elif line < 5*nlines//7:
            start = round((line - (nlines//7)) * size)
        else:
            start = round(1*size)
        if start < round(1*size):
            start = round(1*size)
        # Decide where it ends.
        endpoints = [10, 8, 11, 6, 5, 7, 5]
        ey = line * 6.0 / (nlines-1)
        eyf = math.floor(ey)
        eyc = math.ceil(ey)
        exf = endpoints[int(eyf)]
        exc = endpoints[int(eyc)]
        if eyf == eyc:
            end = exf
        else:
            end = exf * (eyc-ey) + exc * (ey-eyf)
        end = round(end * size)

        liney = height - (lineht+linespc) * (line+1)
        for x in range(int(start), int(end)):
            for y in range(int(lineht)):
                pixel(x, y+liney, cK, canvas)

    # And draw a border.
    border(canvas, size, \
    [(0,0,TL),(int(width-1),0,TR),(0,int(height-1),BL), \
    (int(width-1),int(height-1),BR)])

    return canvas

def hat(size):
    canvas = {}

    # The secret-agent hat in the Pageant icon.

    topa = [6]*9+[5,3,1,0,0,1,2,2,1,1,1,9,9,10,10,11,11,12,12]
    topa = [round(x*size) for x in topa]
    botl = round(topa[0]+2.4*math.sqrt(size))
    botr = round(topa[-1]+2.4*math.sqrt(size))
    width = round(len(topa)*size)

    # Line equations for the top and bottom of the hat brim, in the
    # form y=mx+c. c, of course, needs scaling by size, but m is
    # independent of size.
    brimm = 1.0 / 3.75
    brimtopc = round(4*size/3)
    brimbotc = round(10*size/3)

    for x in range(int(width)):
        xs = float(x) * (len(topa)-1) / (width-1)
        xf = math.floor(xs)
        xc = math.ceil(xs)
        topf = topa[int(xf)]
        topc = topa[int(xc)]
        if xf == xc:
            top = topf
        else:
            top = topf * (xc-xs) + topc * (xs-xf)
        top = math.floor(top)
        bot = round(botl + (botr-botl) * x/(width-1))

        for y in range(int(top), int(bot)):
            pixel(x, y, cK, canvas)

    # Now draw the brim.
    for x in range(int(width)):
        brimtop = brimtopc + brimm * x
        brimbot = brimbotc + brimm * x
        for y in range(int(math.floor(brimtop)), int(math.ceil(brimbot))):
            tophere = max(min(brimtop - y, 1), 0)
            bothere = max(min(brimbot - y, 1), 0)
            grey = bothere - tophere
            # Only draw brim pixels over pixels which are (a) part
            # of the main hat, and (b) not right on its edge.
            if (x,y) in canvas and \
            (x,y-1) in canvas and \
            (x,y+1) in canvas and \
            (x-1,y) in canvas and \
            (x+1,y) in canvas:
                pixel(x, y, greypix(grey), canvas)

    return canvas

def key(size):
    canvas = {}

    # The key in the PuTTYgen icon.

    keyheadw = round(9.5*size)
    keyheadh = round(12*size)
    keyholed = round(4*size)
    keyholeoff = round(2*size)
    # Ensure keyheadh and keyshafth have the same parity.
    keyshafth = round((2*size - (int(keyheadh)&1)) / 2) * 2 + (int(keyheadh)&1)
    keyshaftw = round(18.5*size)
    keyhead = [round(x*size) for x in [12,11,8,10,9,8,11,12]]

    squarepix = []

    # Ellipse for the key head, minus an off-centre circular hole.
    for y in range(int(keyheadh)):
        dy = (y-(keyheadh-1)/2.0) / (keyheadh/2.0)
        dyh = (y-(keyheadh-1)/2.0) / (keyholed/2.0)
        for x in range(int(keyheadw)):
            dx = (x-(keyheadw-1)/2.0) / (keyheadw/2.0)
            dxh = (x-(keyheadw-1)/2.0-keyholeoff) / (keyholed/2.0)
            if dy*dy+dx*dx <= 1 and dyh*dyh+dxh*dxh > 1:
                pixel(x + keyshaftw, y, cy, canvas)

    # Rectangle for the key shaft, extended at the bottom for the
    # key head detail.
    for x in range(int(keyshaftw)):
        top = round((keyheadh - keyshafth) / 2)
        bot = round((keyheadh + keyshafth) / 2)
        xs = float(x) * (len(keyhead)-1) / round((len(keyhead)-1)*size)
        xf = math.floor(xs)
        xc = math.ceil(xs)
        in_head = 0
        if xc < len(keyhead):
            in_head = 1
            yf = keyhead[int(xf)]
            yc = keyhead[int(xc)]
            if xf == xc:
                bot = yf
            else:
                bot = yf * (xc-xs) + yc * (xs-xf)
        for y in range(int(top),int(bot)):
            pixel(x, y, cy, canvas)
            if in_head:
                last = (x, y)
        if x == 0:
            squarepix.append((x, int(top), TL))
        if x == 0:
            squarepix.append(last + (BL,))
        if last != None and not in_head:
            squarepix.append(last + (BR,))
            last = None

    # And draw a border.
    border(canvas, size, squarepix)

    return canvas

def linedist(x1,y1, x2,y2, x,y):
    # Compute the distance from the point x,y to the line segment
    # joining x1,y1 to x2,y2. Returns the distance vector, measured
    # with x,y at the origin.

    vectors = []

    # Special case: if x1,y1 and x2,y2 are the same point, we
    # don't attempt to extrapolate it into a line at all.
    if x1 != x2 or y1 != y2:
        # First, find the nearest point to x,y on the infinite
        # projection of the line segment. So we construct a vector
        # n perpendicular to that segment...
        nx = y2-y1
        ny = x1-x2
        # ... compute the dot product of (x1,y1)-(x,y) with that
        # vector...
        nd = (x1-x)*nx + (y1-y)*ny
        # ... multiply by the vector we first thought of...
        ndx = nd * nx
        ndy = nd * ny
        # ... and divide twice by the length of n.
        ndx = ndx / (nx*nx+ny*ny)
        ndy = ndy / (nx*nx+ny*ny)
        # That gives us a displacement vector from x,y to the
        # nearest point. See if it's within the range of the line
        # segment.
        cx = x + ndx
        cy = y + ndy
        if cx >= min(x1,x2) and cx <= max(x1,x2) and \
        cy >= min(y1,y2) and cy <= max(y1,y2):
            vectors.append((ndx,ndy))

    # Now we have up to three candidate result vectors: (ndx,ndy)
    # as computed just above, and the two vectors to the ends of
    # the line segment, (x1-x,y1-y) and (x2-x,y2-y). Pick the
    # shortest.
    vectors = vectors + [(x1-x,y1-y), (x2-x,y2-y)]
    bestlen, best = None, None
    for v in vectors:
        vlen = v[0]*v[0]+v[1]*v[1]
        if bestlen == None or bestlen > vlen:
            bestlen = vlen
            best = v
    return best

def spanner(size):
    canvas = {}

    # The spanner in the config box icon.

    headcentre = 0.5 + round(4*size)
    headradius = headcentre + 0.1
    headhighlight = round(1.5*size)
    holecentre = 0.5 + round(3*size)
    holeradius = round(2*size)
    holehighlight = round(1.5*size)
    shaftend = 0.5 + round(25*size)
    shaftwidth = round(2*size)
    shafthighlight = round(1.5*size)
    cmax = shaftend + shaftwidth

    # Define three line segments, such that the shortest distance
    # vectors from any point to each of these segments determines
    # everything we need to know about where it is on the spanner
    # shape.
    segments = [
    ((0,0), (holecentre, holecentre)),
    ((headcentre, headcentre), (headcentre, headcentre)),
    ((headcentre+headradius/math.sqrt(2), headcentre+headradius/math.sqrt(2)),
    (cmax, cmax))
    ]

    for y in range(int(cmax)):
        for x in range(int(cmax)):
            vectors = [linedist(a,b,c,d,x,y) for ((a,b),(c,d)) in segments]
            dists = [memoisedsqrt(vx*vx+vy*vy) for (vx,vy) in vectors]

            # If the distance to the hole line is less than
            # holeradius, we're not part of the spanner.
            if dists[0] < holeradius:
                continue
            # If the distance to the head `line' is less than
            # headradius, we are part of the spanner; likewise if
            # the distance to the shaft line is less than
            # shaftwidth _and_ the resulting shaft point isn't
            # beyond the shaft end.
            if dists[1] > headradius and \
            (dists[2] > shaftwidth or x+vectors[2][0] >= shaftend):
                continue

            # We're part of the spanner. Now compute the highlight
            # on this pixel. We do this by computing a `slope
            # vector', which points from this pixel in the
            # direction of its nearest edge. We store an array of
            # slope vectors, in polar coordinates.
            angles = [math.atan2(vy,vx) for (vx,vy) in vectors]
            slopes = []
            if dists[0] < holeradius + holehighlight:
                slopes.append(((dists[0]-holeradius)/holehighlight,angles[0]))
            if dists[1]/headradius < dists[2]/shaftwidth:
                if dists[1] > headradius - headhighlight and dists[1] < headradius:
                    slopes.append(((headradius-dists[1])/headhighlight,math.pi+angles[1]))
            else:
                if dists[2] > shaftwidth - shafthighlight and dists[2] < shaftwidth:
                    slopes.append(((shaftwidth-dists[2])/shafthighlight,math.pi+angles[2]))
            # Now we find the smallest distance in that array, if
            # any, and that gives us a notional position on a
            # sphere which we can use to compute the final
            # highlight level.
            bestdist = None
            bestangle = 0
            for dist, angle in slopes:
                if bestdist == None or bestdist > dist:
                    bestdist = dist
                    bestangle = angle
            if bestdist == None:
                bestdist = 1.0
            sx = (1.0-bestdist) * math.cos(bestangle)
            sy = (1.0-bestdist) * math.sin(bestangle)
            sz = math.sqrt(1.0 - sx*sx - sy*sy)
            shade = sx-sy+sz / math.sqrt(3) # can range from -1 to +1
            shade = 1.0 - (1-shade)/3

            pixel(x, y, yellowpix(shade), canvas)

    # And draw a border.
    border(canvas, size, [])

    return canvas

def box(size, back):
    canvas = {}

    # The back side of the cardboard box in the installer icon.

    boxwidth = round(15 * size)
    boxheight = round(12 * size)
    boxdepth = round(4 * size)
    boxfrontflapheight = round(5 * size)
    boxrightflapheight = round(3 * size)

    # Three shades of basically acceptable brown, all achieved by
    # halftoning between two of the Windows-16 colours. I'm quite
    # pleased that was feasible at all!
    dark = halftone(cr, cK)
    med = halftone(cr, cy)
    light = halftone(cr, cY)
    # We define our halftoning parity in such a way that the black
    # pixels along the RHS of the visible part of the box back
    # match up with the one-pixel black outline around the
    # right-hand side of the box. In other words, we want the pixel
    # at (-1, boxwidth-1) to be black, and hence the one at (0,
    # boxwidth) too.
    parityadjust = int(boxwidth) % 2

    # The entire back of the box.
    if back:
        for x in range(int(boxwidth + boxdepth)):
            ytop = max(-x-1, -boxdepth-1)
            ybot = min(boxheight, boxheight+boxwidth-1-x)
            for y in range(int(ytop), int(ybot)):
                pixel(x, y, dark[(x+y+parityadjust) % 2], canvas)

    # Even when drawing the back of the box, we still draw the
    # whole shape, because that means we get the right overall size
    # (the flaps make the box front larger than the box back) and
    # it'll all be overwritten anyway.

    # The front face of the box.
    for x in range(int(boxwidth)):
        for y in range(int(boxheight)):
            pixel(x, y, med[(x+y+parityadjust) % 2], canvas)
    # The right face of the box.
    for x in range(int(boxwidth), int(boxwidth+boxdepth)):
        ybot = boxheight + boxwidth-x
        ytop = ybot - boxheight
        for y in range(int(ytop), int(ybot)):
            pixel(x, y, dark[(x+y+parityadjust) % 2], canvas)
    # The front flap of the box.
    for y in range(int(boxfrontflapheight)):
        xadj = int(round(-0.5*y))
        for x in range(int(xadj), int(xadj+boxwidth)):
            pixel(x, y, light[(x+y+parityadjust) % 2], canvas)
    # The right flap of the box.
    for x in range(int(boxwidth), int(boxwidth + boxdepth + boxrightflapheight + 1)):
        ytop = max(boxwidth - 1 - x, x - boxwidth - 2*boxdepth - 1)
        ybot = min(x - boxwidth - 1, boxwidth + 2*boxrightflapheight - 1 - x)
        for y in range(int(ytop), int(ybot+1)):
            pixel(x, y, med[(x+y+parityadjust) % 2], canvas)

    # And draw a border.
    border(canvas, size, [(0, int(boxheight)-1, BL)])

    return canvas

def boxback(size):
    return box(size, 1)
def boxfront(size):
    return box(size, 0)

# Functions to draw entire icons by composing the above components.

def xybolt(c1, c2, size, boltoffx=0, boltoffy=0, aux={}):
    # Two unspecified objects and a lightning bolt.

    canvas = {}
    w = h = round(32 * size)

    bolt = lightning(size)

    # Position c2 against the top right of the icon.
    bb = bbox(c2)
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    overlay(c2, w-bb[2], 0-bb[1], canvas)
    aux["c2pos"] = (w-bb[2], 0-bb[1])
    # Position c1 against the bottom left of the icon.
    bb = bbox(c1)
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    overlay(c1, 0-bb[0], h-bb[3], canvas)
    aux["c1pos"] = (0-bb[0], h-bb[3])
    # Place the lightning bolt artistically off-centre. (The
    # rationale for this positioning is that it's centred on the
    # midpoint between the centres of the two monitors in the PuTTY
    # icon proper, but it's not really feasible to _base_ the
    # calculation here on that.)
    bb = bbox(bolt)
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    overlay(bolt, (w-bb[0]-bb[2])/2 + round(boltoffx*size), \
    (h-bb[1]-bb[3])/2 + round((boltoffy-2)*size), canvas)

    return canvas

def putty_icon(size):
    return xybolt(computer(size), computer(size), size)

def puttycfg_icon(size):
    w = h = round(32 * size)
    s = spanner(size)
    canvas = putty_icon(size)
    # Centre the spanner.
    bb = bbox(s)
    overlay(s, (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2, canvas)
    return canvas

def puttygen_icon(size):
    return xybolt(computer(size), key(size), size, boltoffx=2)

def pscp_icon(size):
    return xybolt(document(size), computer(size), size)

def puttyins_icon(size):
    aret = {}
    # The box back goes behind the lightning bolt.
    canvas = xybolt(boxback(size), computer(size), size, boltoffx=-2, boltoffy=+1, aux=aret)
    # But the box front goes over the top, so that the lightning
    # bolt appears to come _out_ of the box. Here it's useful to
    # know the exact coordinates where xybolt placed the box back,
    # so we can overlay the box front exactly on top of it.
    c1x, c1y = aret["c1pos"]
    overlay(boxfront(size), c1x, c1y, canvas)
    return canvas

def pterm_icon(size):
    # Just a really big computer.

    canvas = {}
    w = h = round(32 * size)

    c = computer(size * 1.4)

    # Centre c in the return canvas.
    bb = bbox(c)
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    overlay(c, (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2, canvas)

    return canvas

def ptermcfg_icon(size):
    w = h = round(32 * size)
    s = spanner(size)
    canvas = pterm_icon(size)
    # Centre the spanner.
    bb = bbox(s)
    overlay(s, (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2, canvas)
    return canvas

def pageant_icon(size):
    # A biggish computer, in a hat.

    canvas = {}
    w = h = round(32 * size)

    c = computer(size * 1.2)
    ht = hat(size)

    cbb = bbox(c)
    hbb = bbox(ht)

    # Determine the relative y-coordinates of the computer and hat.
    # We just centre the one on the other.
    xrel = (cbb[0]+cbb[2]-hbb[0]-hbb[2])//2

    # Determine the relative y-coordinates of the computer and hat.
    # We do this by sitting the hat as low down on the computer as
    # possible without any computer showing over the top. To do
    # this we first have to find the minimum x coordinate at each
    # y-coordinate of both components.
    cty = topy(c)
    hty = topy(ht)
    yrelmin = None
    for cx in cty.keys():
        hx = cx - xrel
        assert hx in hty
        yrel = cty[cx] - hty[hx]
        if yrelmin == None:
            yrelmin = yrel
        else:
            yrelmin = min(yrelmin, yrel)

    # Overlay the hat on the computer.
    overlay(ht, xrel, yrelmin, c)

    # And centre the result in the main icon canvas.
    bb = bbox(c)
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    overlay(c, (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2, canvas)

    return canvas

# Test and output functions.

import os
import sys

def testrun(func, fname):
    canvases = []
    for size in [0.5, 0.6, 1.0, 1.2, 1.5, 4.0]:
        canvases.append(func(size))
    wid = 0
    ht = 0
    for canvas in canvases:
        minx, miny, maxx, maxy = bbox(canvas)
        wid = max(wid, maxx-minx+4)
        ht = ht + maxy-miny+4
    block = []
    for canvas in canvases:
        minx, miny, maxx, maxy = bbox(canvas)
        block.extend(render(canvas, minx-2, miny-2, minx-2+wid, maxy+2))
    with open(fname, "wb") as f:
        f.write((("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 3\nMAXVAL 255\n" +
                  "TUPLTYPE RGB\nENDHDR\n") % (wid, ht)).encode('ASCII'))
        assert len(block) == ht
        for line in block:
            assert len(line) == wid
            for r, g, b, a in line:
                # Composite on to orange.
                r = int(round((r * a + 255 * (255-a)) / 255.0))
                g = int(round((g * a + 128 * (255-a)) / 255.0))
                b = int(round((b * a +   0 * (255-a)) / 255.0))
                f.write(bytes(bytearray([r, g, b])))

def drawicon(func, width, fname, orangebackground = 0):
    canvas = func(width / 32.0)
    finalise(canvas)
    minx, miny, maxx, maxy = bbox(canvas)
    assert minx >= 0 and miny >= 0 and maxx <= width and maxy <= width

    block = render(canvas, 0, 0, width, width)
    with open(fname, "wb") as f:
        f.write((("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n" +
                  "TUPLTYPE RGB_ALPHA\nENDHDR\n") %
                 (width, width)).encode('ASCII'))
        assert len(block) == width
        for line in block:
            assert len(line) == width
            for r, g, b, a in line:
                if orangebackground:
                    # Composite on to orange.
                    r = int(round((r * a + 255 * (255-a)) / 255.0))
                    g = int(round((g * a + 128 * (255-a)) / 255.0))
                    b = int(round((b * a +   0 * (255-a)) / 255.0))
                    a = 255
                f.write(bytes(bytearray([r, g, b, a])))

args = sys.argv[1:]

orangebackground = test = 0
colours = 1 # 0=mono, 1=16col, 2=truecol
doingargs = 1

realargs = []
for arg in args:
    if doingargs and arg[0] == "-":
        if arg == "-t":
            test = 1
        elif arg == "-it":
            orangebackground = 1
        elif arg == "-2":
            colours = 0
        elif arg == "-T":
            colours = 2
        elif arg == "--":
            doingargs = 0
        else:
            sys.stderr.write("unrecognised option '%s'\n" % arg)
            sys.exit(1)
    else:
        realargs.append(arg)

if colours == 0:
    # Monochrome.
    cK=cr=cg=cb=cm=cc=cP=cw=cR=cG=cB=cM=cC=cD = 0
    cY=cy=cW = 1
    cT = -1
    def greypix(value):
        return [cK,cW][int(round(value))]
    def yellowpix(value):
        return [cK,cW][int(round(value))]
    def bluepix(value):
        return cK
    def dark(value):
        return [cT,cK][int(round(value))]
    def blend(col1, col2):
        if col1 == cT:
            return col2
        else:
            return col1
    pixvals = [
    (0x00, 0x00, 0x00, 0xFF), # cK
    (0xFF, 0xFF, 0xFF, 0xFF), # cW
    (0x00, 0x00, 0x00, 0x00), # cT
    ]
    def outpix(colour):
        return pixvals[colour]
    def finalisepix(colour):
        return colour
    def halftone(col1, col2):
        return (col1, col2)
elif colours == 1:
    # Windows 16-colour palette.
    cK,cr,cg,cy,cb,cm,cc,cP,cw,cR,cG,cY,cB,cM,cC,cW = list(range(16))
    cT = -1
    cD = -2 # special translucent half-darkening value used internally
    def greypix(value):
        return [cK,cw,cw,cP,cW][int(round(4*value))]
    def yellowpix(value):
        return [cK,cy,cY][int(round(2*value))]
    def bluepix(value):
        return [cK,cb,cB][int(round(2*value))]
    def dark(value):
        return [cT,cD,cK][int(round(2*value))]
    def blend(col1, col2):
        if col1 == cT:
            return col2
        elif col1 == cD:
            return [cK,cK,cK,cK,cK,cK,cK,cw,cK,cr,cg,cy,cb,cm,cc,cw,cD,cD][col2]
        else:
            return col1
    pixvals = [
    (0x00, 0x00, 0x00, 0xFF), # cK
    (0x80, 0x00, 0x00, 0xFF), # cr
    (0x00, 0x80, 0x00, 0xFF), # cg
    (0x80, 0x80, 0x00, 0xFF), # cy
    (0x00, 0x00, 0x80, 0xFF), # cb
    (0x80, 0x00, 0x80, 0xFF), # cm
    (0x00, 0x80, 0x80, 0xFF), # cc
    (0xC0, 0xC0, 0xC0, 0xFF), # cP
    (0x80, 0x80, 0x80, 0xFF), # cw
    (0xFF, 0x00, 0x00, 0xFF), # cR
    (0x00, 0xFF, 0x00, 0xFF), # cG
    (0xFF, 0xFF, 0x00, 0xFF), # cY
    (0x00, 0x00, 0xFF, 0xFF), # cB
    (0xFF, 0x00, 0xFF, 0xFF), # cM
    (0x00, 0xFF, 0xFF, 0xFF), # cC
    (0xFF, 0xFF, 0xFF, 0xFF), # cW
    (0x00, 0x00, 0x00, 0x80), # cD
    (0x00, 0x00, 0x00, 0x00), # cT
    ]
    def outpix(colour):
        return pixvals[colour]
    def finalisepix(colour):
        # cD is used internally, but can't be output. Convert to cK.
        if colour == cD:
            return cK
        return colour
    def halftone(col1, col2):
        return (col1, col2)
else:
    # True colour.
    cK = (0x00, 0x00, 0x00, 0xFF)
    cr = (0x80, 0x00, 0x00, 0xFF)
    cg = (0x00, 0x80, 0x00, 0xFF)
    cy = (0x80, 0x80, 0x00, 0xFF)
    cb = (0x00, 0x00, 0x80, 0xFF)
    cm = (0x80, 0x00, 0x80, 0xFF)
    cc = (0x00, 0x80, 0x80, 0xFF)
    cP = (0xC0, 0xC0, 0xC0, 0xFF)
    cw = (0x80, 0x80, 0x80, 0xFF)
    cR = (0xFF, 0x00, 0x00, 0xFF)
    cG = (0x00, 0xFF, 0x00, 0xFF)
    cY = (0xFF, 0xFF, 0x00, 0xFF)
    cB = (0x00, 0x00, 0xFF, 0xFF)
    cM = (0xFF, 0x00, 0xFF, 0xFF)
    cC = (0x00, 0xFF, 0xFF, 0xFF)
    cW = (0xFF, 0xFF, 0xFF, 0xFF)
    cD = (0x00, 0x00, 0x00, 0x80)
    cT = (0x00, 0x00, 0x00, 0x00)
    def greypix(value):
        value = max(min(value, 1), 0)
        return (int(round(0xFF*value)),) * 3 + (0xFF,)
    def yellowpix(value):
        value = max(min(value, 1), 0)
        return (int(round(0xFF*value)),) * 2 + (0, 0xFF)
    def bluepix(value):
        value = max(min(value, 1), 0)
        return (0, 0, int(round(0xFF*value)), 0xFF)
    def dark(value):
        value = max(min(value, 1), 0)
        return (0, 0, 0, int(round(0xFF*value)))
    def blend(col1, col2):
        r1,g1,b1,a1 = col1
        r2,g2,b2,a2 = col2
        r = int(round((r1*a1 + r2*(0xFF-a1)) / 255.0))
        g = int(round((g1*a1 + g2*(0xFF-a1)) / 255.0))
        b = int(round((b1*a1 + b2*(0xFF-a1)) / 255.0))
        a = int(round((255*a1 + a2*(0xFF-a1)) / 255.0))
        return r, g, b, a
    def outpix(colour):
        return colour
    if colours == 2:
        # True colour with no alpha blending: we still have to
        # finalise half-dark pixels to black.
        def finalisepix(colour):
            if colour[3] > 0:
                return colour[:3] + (0xFF,)
            return colour
    else:
        def finalisepix(colour):
            return colour
    def halftone(col1, col2):
        r1,g1,b1,a1 = col1
        r2,g2,b2,a2 = col2
        colret = (int(r1+r2)//2, int(g1+g2)//2, int(b1+b2)//2, int(a1+a2)//2)
        return (colret, colret)

if test:
    testrun(eval(realargs[0]), realargs[1])
else:
    drawicon(eval(realargs[0]), int(realargs[1]), realargs[2], orangebackground)
