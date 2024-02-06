#!/usr/bin/env python3

import argparse
import itertools
import math
import os
import sys
from fractions import Fraction

import xml.etree.cElementTree as ET

# Python code which draws the PuTTY icon components in SVG.

def makegroup(*objects):
    if len(objects) == 1:
        return objects[0]
    g = ET.Element("g")
    for obj in objects:
        g.append(obj)
    return g

class Container:
    "Empty class for keeping things in."
    pass

class SVGthing(object):
    def __init__(self):
        self.fillc = "none"
        self.strokec = "none"
        self.strokewidth = 0
        self.strokebehind = False
        self.clipobj = None
        self.props = Container()
    def fmt_colour(self, rgb):
        return "#{0:02x}{1:02x}{2:02x}".format(*rgb)
    def fill(self, colour):
        self.fillc = self.fmt_colour(colour)
    def stroke(self, colour, width=1, behind=False):
        self.strokec = self.fmt_colour(colour)
        self.strokewidth = width
        self.strokebehind = behind
    def clip(self, obj):
        self.clipobj = obj
    def styles(self, elt, styles):
        elt.attrib["style"] = ";".join("{}:{}".format(k,v)
                                       for k,v in sorted(styles.items()))
    def add_clip_paths(self, container, idents, X, Y):
        if self.clipobj:
            self.clipobj.identifier = next(idents)
            clipelt = self.clipobj.render_thing(X, Y)
            clippath = ET.Element("clipPath")
            clippath.attrib["id"] = self.clipobj.identifier
            clippath.append(clipelt)
            container.append(clippath)
            return True
        return False
    def render(self, X, Y, with_styles=True):
        elt = self.render_thing(X, Y)
        if self.clipobj:
            elt.attrib["clip-path"] = "url(#{})".format(
                self.clipobj.identifier)
        estyles = {"fill": self.fillc}
        sstyles = {"stroke": self.strokec}
        if self.strokewidth:
            sstyles["stroke-width"] = "{:g}".format(self.strokewidth)
            sstyles["stroke-linecap"] = "round"
            sstyles["stroke-linejoin"] = "round"
        if not self.strokebehind:
            estyles.update(sstyles)
        if with_styles:
            self.styles(elt, estyles)
        if not self.strokebehind:
            return elt
        selt = self.render_thing(X, Y)
        if with_styles:
            self.styles(selt, sstyles)
        return makegroup(selt, elt)
    def bbox(self):
        it = self.bb_iter()
        xmin, ymin = xmax, ymax = next(it)
        for x, y in it:
            xmin = min(x, xmin)
            xmax = max(x, xmax)
            ymin = min(y, ymin)
            ymax = max(y, ymax)
        r = self.strokewidth / 2.0
        xmin -= r
        ymin -= r
        xmax += r
        ymax += r
        if self.clipobj:
            x0, y0, x1, y1 = self.clipobj.bbox()
            xmin = max(x0, xmin)
            xmax = min(x1, xmax)
            ymin = max(y0, ymin)
            ymax = min(y1, ymax)
        return xmin, ymin, xmax, ymax

class SVGpath(SVGthing):
    def __init__(self, pointlists, closed=True):
        super().__init__()
        self.pointlists = pointlists
        self.closed = closed
    def bb_iter(self):
        for points in self.pointlists:
            for x,y,on in points:
                yield x,y
    def render_thing(self, X, Y):
        pathcmds = []

        for points in self.pointlists:
            while not points[-1][2]:
                points = points[1:] + [points[0]]

            piter = iter(points)

            if self.closed:
                xp, yp, _ = points[-1]
                pathcmds.extend(["M", X+xp, Y-yp])
            else:
                xp, yp, on = next(piter)
                assert on, "Open paths must start with an on-curve point"
                pathcmds.extend(["M", X+xp, Y-yp])

            for x, y, on in piter:
                if isinstance(on, type(())):
                    assert on[0] == "arc"
                    _, rx, ry, rotation, large, sweep = on
                    pathcmds.extend(["a",
                                     rx, ry, rotation,
                                     1 if large else 0,
                                     1 if sweep else 0,
                                     x-xp, -(y-yp)])
                elif not on:
                    x0, y0 = x, y
                    x1, y1, on = next(piter)
                    assert not on
                    x, y, on = next(piter)
                    assert on
                    pathcmds.extend(["c", x0-xp, -(y0-yp),
                                     ",", x1-xp, -(y1-yp),
                                     ",", x-xp, -(y-yp)])
                elif x == xp:
                    pathcmds.extend(["v", -(y-yp)])
                elif x == xp:
                    pathcmds.extend(["h", x-xp])
                else:
                    pathcmds.extend(["l", x-xp, -(y-yp)])

                xp, yp = x, y

            if self.closed:
                pathcmds.append("z")

        path = ET.Element("path")
        path.attrib["d"] = " ".join(str(cmd) for cmd in pathcmds)
        return path

class SVGrect(SVGthing):
    def __init__(self, x0, y0, x1, y1):
        super().__init__()
        self.points = x0, y0, x1, y1
    def bb_iter(self):
        x0, y0, x1, y1 = self.points
        return iter([(x0,y0), (x1,y1)])
    def render_thing(self, X, Y):
        x0, y0, x1, y1 = self.points
        rect = ET.Element("rect")
        rect.attrib["x"] = "{:g}".format(min(X+x0,X+x1))
        rect.attrib["y"] = "{:g}".format(min(Y-y0,Y-y1))
        rect.attrib["width"] = "{:g}".format(abs(x0-x1))
        rect.attrib["height"] = "{:g}".format(abs(y0-y1))
        return rect

class SVGpoly(SVGthing):
    def __init__(self, points):
        super().__init__()
        self.points = points
    def bb_iter(self):
        return iter(self.points)
    def render_thing(self, X, Y):
        poly = ET.Element("polygon")
        poly.attrib["points"] = " ".join("{:g},{:g}".format(X+x,Y-y)
                                         for x,y in self.points)
        return poly

class SVGgroup(object):
    def __init__(self, objects, translations=[]):
        translations = translations + (
            [(0,0)] * (len(objects)-len(translations)))
        self.contents = list(zip(objects, translations))
        self.props = Container()
    def render(self, X, Y):
        return makegroup(*[obj.render(X+x, Y-y) 
                           for obj, (x,y) in self.contents])
    def add_clip_paths(self, container, idents, X, Y):
        toret = False
        for obj, (x,y) in self.contents:
            if obj.add_clip_paths(container, idents, X+x, Y-y):
                toret = True
        return toret
    def bbox(self):
        it = ((x,y) + obj.bbox() for obj, (x,y) in self.contents)
        x, y, xmin, ymin, xmax, ymax = next(it)
        xmin = x+xmin
        ymin = y+ymin
        xmax = x+xmax
        ymax = y+ymax
        for x, y, x0, y0, x1, y1 in it:
            xmin = min(x+x0, xmin)
            xmax = max(x+x1, xmax)
            ymin = min(y+y0, ymin)
            ymax = max(y+y1, ymax)
        return (xmin, ymin, xmax, ymax)

class SVGtranslate(object):
    def __init__(self, obj, translation):
        self.obj = obj
        self.tx, self.ty = translation
    def render(self, X, Y):
        return self.obj.render(X+self.tx, Y+self.ty)
    def add_clip_paths(self, container, idents, X, Y):
        return self.obj.add_clip_paths(container, idents, X+self.tx, Y-self.ty)
    def bbox(self):
        xmin, ymin, xmax, ymax = self.obj.bbox()
        return xmin+self.tx, ymin+self.ty, xmax+self.tx, ymax+self.ty

# Code to actually draw pieces of icon. These don't generally worry
# about positioning within a rectangle; they just draw at a standard
# location, return some useful coordinates, and leave composition
# to other pieces of code.

def sysbox(size):
    # The system box of the computer.

    height = 3.6*size
    width = 16.51*size
    depth = 2*size
    highlight = 1*size

    floppystart = 19*size # measured in half-pixels
    floppyend = 29*size # measured in half-pixels
    floppybottom = highlight
    floppyrheight = 0.7 * size
    floppyheight = floppyrheight
    if floppyheight < 1:
        floppyheight = 1
    floppytop = floppybottom + floppyheight

    background_coords = [
        (0,0), (width,0), (width+depth,depth),
        (width+depth,height+depth), (depth,height+depth), (0,height)]
    background = SVGpoly(background_coords)
    background.fill(greypix(0.75))

    hl_dark = SVGpoly([
        (highlight,0), (highlight,highlight), (width-highlight,highlight),
        (width-highlight,height-highlight), (width+depth,height+depth),
        (width+depth,depth), (width,0)])
    hl_dark.fill(greypix(0.5))

    hl_light = SVGpoly([
        (0,highlight), (highlight,highlight), (highlight,height-highlight),
        (width-highlight,height-highlight), (width+depth,height+depth),
        (width+depth-highlight,height+depth), (width-highlight,height),
        (0,height)])
    hl_light.fill(cW)

    floppy = SVGrect(floppystart/2.0, floppybottom,
                     floppyend/2.0, floppytop)
    floppy.fill(cK)

    outline = SVGpoly(background_coords)
    outline.stroke(cK, width=0.5)

    toret = SVGgroup([background, hl_dark, hl_light, floppy, outline])
    toret.props.sysboxheight = height
    toret.props.borderthickness = 1 # FIXME
    return toret

def monitor(size):
    # The computer's monitor.

    height = 9.5*size
    width = 11.5*size
    surround = 1*size
    botsurround = 2*size
    sheight = height - surround - botsurround
    swidth = width - 2*surround
    depth = 2*size
    highlight = surround/2
    shadow = 0.5*size

    background_coords = [
        (0,0), (width,0), (width+depth,depth),
        (width+depth,height+depth), (depth,height+depth), (0,height)]
    background = SVGpoly(background_coords)
    background.fill(greypix(0.75))

    hl0_dark = SVGpoly([
        (0,0), (highlight,highlight), (width-highlight,highlight),
        (width-highlight,height-highlight), (width+depth,height+depth),
        (width+depth,depth), (width,0)])
    hl0_dark.fill(greypix(0.5))

    hl0_light = SVGpoly([
        (0,0), (highlight,highlight), (highlight,height-highlight),
        (width-highlight,height-highlight), (width,height), (0,height)])
    hl0_light.fill(greypix(1))

    hl1_dark = SVGpoly([
        (surround-highlight,botsurround-highlight), (surround,botsurround),
        (surround,height-surround), (width-surround,height-surround),
        (width-surround+highlight,height-surround+highlight),
        (surround-highlight,height-surround+highlight)])
    hl1_dark.fill(greypix(0.5))

    hl1_light = SVGpoly([
        (surround-highlight,botsurround-highlight), (surround,botsurround),
        (width-surround,botsurround), (width-surround,height-surround),
        (width-surround+highlight,height-surround+highlight),
        (width-surround+highlight,botsurround-highlight)])
    hl1_light.fill(greypix(1))

    screen = SVGrect(surround, botsurround, width-surround, height-surround)
    screen.fill(bluepix(1))

    screenshadow = SVGpoly([
        (surround,botsurround), (surround+shadow,botsurround),
        (surround+shadow,height-surround-shadow),
        (width-surround,height-surround-shadow), 
        (width-surround,height-surround), (surround,height-surround)])
    screenshadow.fill(bluepix(0.5))

    outline = SVGpoly(background_coords)
    outline.stroke(cK, width=0.5)

    toret = SVGgroup([background, hl0_dark, hl0_light, hl1_dark, hl1_light,
                      screen, screenshadow, outline])
    # Give the centre of the screen (for lightning-bolt positioning purposes)
    # as the centre of the _light_ area of the screen, not counting the
    # shadow on the top and left. I think that looks very slightly nicer.
    sbb = (surround+shadow, botsurround, width-surround, height-surround-shadow)
    toret.props.screencentre = ((sbb[0]+sbb[2])/2, (sbb[1]+sbb[3])/2)
    return toret

def computer(size):
    # Monitor plus sysbox.
    m = monitor(size)
    s = sysbox(size)
    x = (2+size/(size+1))*size
    y = int(s.props.sysboxheight + s.props.borderthickness)
    mb = m.bbox()
    sb = s.bbox()
    xoff = mb[0] - sb[0] + x
    yoff = mb[1] - sb[1] + y
    toret = SVGgroup([s, m], [(0,0), (xoff,yoff)])
    toret.props.screencentre = (m.props.screencentre[0]+xoff,
                                m.props.screencentre[1]+yoff)
    return toret

def lightning(size):
    # The lightning bolt motif.

    # Compute the right size of a lightning bolt to exactly connect
    # the centres of the two screens in the main PuTTY icon. We'll use
    # that size of bolt for all the other icons too, for consistency.
    iconw = iconh = 32 * size
    cbb = computer(size).bbox()
    assert cbb[2]-cbb[0] <= iconw and cbb[3]-cbb[1] <= iconh
    width, height = iconw-(cbb[2]-cbb[0]), iconh-(cbb[3]-cbb[1])

    degree = math.pi/180

    centrethickness = 2*size # top-to-bottom thickness of centre bar
    innerangle = 46 * degree # slope of the inner slanting line
    outerangle = 39 * degree # slope of the outer one

    innery = (height - centrethickness) / 2
    outery = (height + centrethickness) / 2
    innerx = innery / math.tan(innerangle)
    outerx = outery / math.tan(outerangle)

    points = [(innerx, innery), (0,0), (outerx, outery)]
    points.extend([(width-x, height-y) for x,y in points])

    # Fill and stroke the lightning bolt.
    #
    # Most of the filled-and-stroked objects in these icons are filled
    # first, and then stroked with width 0.5, so that the edge of the
    # filled area runs down the centre line of the stroke. Put another
    # way, half the stroke covers what would have been the filled
    # area, and the other half covers the background. This seems like
    # the normal way to fill-and-stroke a shape of a given size, and
    # SVG makes it easy by allowing us to specify the polygon just
    # once with both 'fill' and 'stroke' CSS properties.
    #
    # But if we did that in this case, then the tips of the lightning
    # bolt wouldn't have lightning-colour anywhere near them, because
    # the two edges are so close together in angle that the point
    # where the strokes would first _not_ overlap would be miles away
    # from the logical endpoint.
    #
    # So, for this one case, we stroke the polygon first at double the
    # width, and then fill it on top of that, requiring two copies of
    # it in the SVG (though my construction class here hides that
    # detail). The effect is that we still get a stroke of visible
    # width 0.5, but it's entirely outside the filled area of the
    # polygon, so the tips of the yellow interior of the lightning
    # bolt are exactly at the logical endpoints.
    poly = SVGpoly(points)
    poly.fill(cY)
    poly.stroke(cK, width=1, behind=True)
    poly.props.end1 = (0,0)
    poly.props.end2 = (width,height)
    return poly

def document(size):
    # The document used in the PSCP/PSFTP icon.

    width = 13*size
    height = 16*size

    lineht = 0.875*size
    linespc = 1.125*size
    nlines = int((height-linespc)/(lineht+linespc))
    height = nlines*(lineht+linespc)+linespc # round this so it fits better

    paper = SVGrect(0, 0, width, height)
    paper.fill(cW)
    paper.stroke(cK, width=0.5)

    objs = [paper]

    # Now draw lines of text.
    for line in range(nlines):
        # Decide where this line of text begins.
        if line == 0:
            start = 4*size
        elif line < 5*nlines/7:
            start = (line * 4/5) * size
        else:
            start = 1*size
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
        end = end * size

        liney = (lineht+linespc) * (line+1)
        line = SVGrect(start, liney-lineht, end, liney)
        line.fill(cK)
        objs.append(line)

    return SVGgroup(objs)

def hat(size):
    # The secret-agent hat in the Pageant icon.

    leftend = (0, -6*size)
    rightend = (28*size, -12*size)
    dx = rightend[0]-leftend[0]
    dy = rightend[1]-leftend[1]
    tcentre = (leftend[0] + 0.5*dx - 0.3*dy, leftend[1] + 0.5*dy + 0.3*dx)

    hatpoints = [leftend + (True,),
                 (7.5*size, -6*size, True),
                 (12*size, 0, True),
                 (14*size, 3*size, False),
                 (tcentre[0] - 0.1*dx, tcentre[1] - 0.1*dy, False),
                 tcentre + (True,)]
    for x, y, on in list(reversed(hatpoints))[1:]:
        vx, vy = x-tcentre[0], y-tcentre[1]
        coeff = float(vx*dx + vy*dy) / float(dx*dx + dy*dy)
        rx, ry = x - 2*coeff*dx, y - 2*coeff*dy
        hatpoints.append((rx, ry, on))

    mainhat = SVGpath([hatpoints])
    mainhat.fill(cK)

    band = SVGpoly([
        (leftend[0] - 0.1*dy, leftend[1] + 0.1*dx),
        (rightend[0] - 0.1*dy, rightend[1] + 0.1*dx),
        (rightend[0] - 0.15*dy, rightend[1] + 0.15*dx),
        (leftend[0] - 0.15*dy, leftend[1] + 0.15*dx)])
    band.fill(cW)
    band.clip(SVGpath([hatpoints]))

    outline = SVGpath([hatpoints])
    outline.stroke(cK, width=1)

    return SVGgroup([mainhat, band, outline])

def key(size):
    # The key in the PuTTYgen icon.

    keyheadw = 9.5*size
    keyheadh = 12*size
    keyholed = 4*size
    keyholeoff = 2*size
    # Ensure keyheadh and keyshafth have the same parity.
    keyshafth = (2*size - (int(keyheadh)&1)) / 2 * 2 + (int(keyheadh)&1)
    keyshaftw = 18.5*size
    keyheaddetail = [x*size for x in [12,11,8,10,9,8,11,12]]

    squarepix = []

    keyheadcx = keyshaftw + keyheadw / 2.0
    keyheadcy = keyheadh / 2.0
    keyshafttop = keyheadcy + keyshafth / 2.0
    keyshaftbot = keyheadcy - keyshafth / 2.0

    keyhead = [(0, keyshafttop, True), (keyshaftw, keyshafttop, True),
               (keyshaftw, keyshaftbot,
                ("arc", keyheadw/2.0, keyheadh/2.0, 0, True, True)),
               (len(keyheaddetail)*size, keyshaftbot, True)]
    for i, h in reversed(list(enumerate(keyheaddetail))):
        keyhead.append(((i+1)*size, keyheadh-h, True))
        keyhead.append(((i)*size, keyheadh-h, True))

    keyholecx = keyheadcx + keyholeoff
    keyholecy = keyheadcy
    keyholer = keyholed / 2.0

    keyhole = [(keyholecx + keyholer, keyholecy,
                ("arc", keyholer, keyholer, 0, False, False)),
               (keyholecx - keyholer, keyholecy,
                ("arc", keyholer, keyholer, 0, False, False))]

    outline = SVGpath([keyhead, keyhole])
    outline.fill(cy)
    outline.stroke(cK, width=0.5)
    return outline

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
    # The spanner in the config box icon.

    # Coordinate definitions.
    headcentre = 0.5 + 4*size
    headradius = headcentre + 0.1
    headhighlight = 1.5*size
    holecentre = 0.5 + 3*size
    holeradius = 2*size
    holehighlight = 1.5*size
    shaftend = 0.5 + 25*size
    shaftwidth = 2*size
    shafthighlight = 1.5*size
    cmax = shaftend + shaftwidth

    # The spanner head is a circle centred at headcentre*(1,1) with
    # radius headradius, minus a circle at holecentre*(1,1) with
    # radius holeradius, and also minus every translate of that circle
    # by a negative real multiple of (1,1).
    #
    # The spanner handle is a diagonally oriented rectangle, of width
    # shaftwidth, with the centre of the far end at shaftend*(1,1),
    # and the near end terminating somewhere inside the spanner head
    # (doesn't really matter exactly where).
    #
    # Hence, in SVG we can represent the shape using a path of
    # straight lines and circular arcs. But first we need to calculate
    # the points where the straight lines meet the spanner head circle.
    headpt = lambda a, on=True: (headcentre+headradius*math.cos(a),
                                 -headcentre+headradius*math.sin(a), on)
    holept = lambda a, on=True: (holecentre+holeradius*math.cos(a),
                                 -holecentre+holeradius*math.sin(a), on)

    # Now we can specify the path.
    spannercoords = [[
        holept(math.pi*5/4),
        holept(math.pi*1/4, ("arc", holeradius,holeradius,0, False, False)),
        headpt(math.pi*3/4 - math.asin(holeradius/headradius)),
        headpt(math.pi*7/4 + math.asin(shaftwidth/headradius),
               ("arc", headradius,headradius,0, False, True)),
        (shaftend+math.sqrt(0.5)*shaftwidth,
         -shaftend+math.sqrt(0.5)*shaftwidth, True),
        (shaftend-math.sqrt(0.5)*shaftwidth,
         -shaftend-math.sqrt(0.5)*shaftwidth, True),
        headpt(math.pi*7/4 - math.asin(shaftwidth/headradius)),
        headpt(math.pi*3/4 + math.asin(holeradius/headradius),
               ("arc", headradius,headradius,0, False, True)),
    ]]

    base = SVGpath(spannercoords)
    base.fill(cY)

    shadowthickness = 2*size
    sx, sy, _ = holept(math.pi*5/4)
    sx += math.sqrt(0.5) * shadowthickness/2
    sy += math.sqrt(0.5) * shadowthickness/2
    sr = holeradius - shadowthickness/2

    shadow = SVGpath([
        [(sx, sy, sr),
         holept(math.pi*1/4, ("arc", sr, sr, 0, False, False)),
         headpt(math.pi*3/4 - math.asin(holeradius/headradius))],
        [(shaftend-math.sqrt(0.5)*shaftwidth,
          -shaftend-math.sqrt(0.5)*shaftwidth, True),
         headpt(math.pi*7/4 - math.asin(shaftwidth/headradius)),
         headpt(math.pi*3/4 + math.asin(holeradius/headradius),
                ("arc", headradius,headradius,0, False, True))],
    ], closed=False)
    shadow.clip(SVGpath(spannercoords))
    shadow.stroke(cy, width=shadowthickness)

    outline = SVGpath(spannercoords)
    outline.stroke(cK, width=0.5)

    return SVGgroup([base, shadow, outline])

def box(size, wantback):
    # The back side of the cardboard box in the installer icon.

    boxwidth = 15 * size
    boxheight = 12 * size
    boxdepth = 4 * size
    boxfrontflapheight = 5 * size
    boxrightflapheight = 3 * size

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

    # The back of the box.
    if wantback:
        back = SVGpoly([
            (0,0), (boxwidth,0), (boxwidth+boxdepth,boxdepth),
            (boxwidth+boxdepth,boxheight+boxdepth),
            (boxdepth,boxheight+boxdepth), (0,boxheight)])
        back.fill(dark)
        back.stroke(cK, width=0.5)
        return back

    # The front face of the box.
    front = SVGrect(0, 0, boxwidth, boxheight)
    front.fill(med)
    front.stroke(cK, width=0.5)
    # The right face of the box.
    right = SVGpoly([
        (boxwidth,0), (boxwidth+boxdepth,boxdepth),
        (boxwidth+boxdepth,boxheight+boxdepth), (boxwidth,boxheight)])
    right.fill(dark)
    right.stroke(cK, width=0.5)
    frontflap = SVGpoly([
        (0,boxheight), (boxwidth,boxheight),
        (boxwidth-boxfrontflapheight/2, boxheight-boxfrontflapheight),
        (-boxfrontflapheight/2, boxheight-boxfrontflapheight)])
    frontflap.stroke(cK, width=0.5)
    frontflap.fill(light)
    rightflap = SVGpoly([
        (boxwidth,boxheight), (boxwidth+boxdepth,boxheight+boxdepth),
        (boxwidth+boxdepth+boxrightflapheight,
         boxheight+boxdepth-boxrightflapheight),
        (boxwidth+boxrightflapheight,boxheight-boxrightflapheight)])
    rightflap.stroke(cK, width=0.5)
    rightflap.fill(med)

    return SVGgroup([front, right, frontflap, rightflap])

def boxback(size):
    return box(size, 1)
def boxfront(size):
    return box(size, 0)

# Functions to draw entire icons by composing the above components.

def xybolt(c1, c2, size, boltoffx=0, boltoffy=0, c1bb=None, c2bb=None):
    # Two unspecified objects and a lightning bolt.

    w = h = 32 * size

    bolt = lightning(size)

    objs = [c2, c1, bolt]
    origins = [None] * 3

    # Position c2 against the top right of the icon.
    bb = c2bb if c2bb is not None else c2.bbox()
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    origins[0] = w-bb[2], h-bb[3]
    # Position c1 against the bottom left of the icon.
    bb = c1bb if c1bb is not None else c1.bbox()
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    origins[1] = 0-bb[0], 0-bb[1]

    # Place the lightning bolt so that it ends precisely at the centre
    # of the monitor, in whichever of the two sub-pictures has one.
    # (In the case of the PuTTY icon proper, in which _both_
    # sub-pictures are computers, it should line up correctly for both.)
    origin1 = origin2 = None
    if hasattr(c1.props, "screencentre"):
        origin1 = (
            c1.props.screencentre[0] + origins[1][0] - bolt.props.end1[0],
            c1.props.screencentre[1] + origins[1][1] - bolt.props.end1[1])
    if hasattr(c2.props, "screencentre"):
        origin2 = (
            c2.props.screencentre[0] + origins[0][0] - bolt.props.end2[0],
            c2.props.screencentre[1] + origins[0][1] - bolt.props.end2[1])
    if origin1 is not None and origin2 is not None:
        assert math.hypot(origin1[0]-origin2[0],origin1[1]-origin2[1]<1e-5), (
            "Lightning bolt didn't line up! Off by {}*size".format(
                ((origin1[0]-origin2[0])/size,
                 (origin1[1]-origin2[1])/size)))
    origins[2] = origin1 if origin1 is not None else origin2
    assert origins[2] is not None, "Need at least one computer to line up bolt"

    toret = SVGgroup(objs, origins)
    toret.props.c1pos = origins[1]
    toret.props.c2pos = origins[0]
    return toret

def putty_icon(size):
    return xybolt(computer(size), computer(size), size)

def puttycfg_icon(size):
    w = h = 32 * size
    s = spanner(size)
    b = putty_icon(size)
    bb = s.bbox()
    return SVGgroup([b, s], [(0,0), ((w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2)])

def puttygen_icon(size):
    k = key(size)
    # Manually move the key around, by pretending to xybolt that its
    # bounding box is offset from where it really is.
    kbb = SVGtranslate(k,(2*size,5*size)).bbox()
    return xybolt(computer(size), k, size, boltoffx=2, c2bb=kbb)

def pscp_icon(size):
    return xybolt(document(size), computer(size), size)

def puttyins_icon(size):
    boxfront = box(size, False)
    boxback = box(size, True)
    # The box back goes behind the lightning bolt.
    most = xybolt(boxback, computer(size), size, c1bb=boxfront.bbox(),
                  boltoffx=-2, boltoffy=+1)
    # But the box front goes over the top, so that the lightning
    # bolt appears to come _out_ of the box. Here it's useful to
    # know the exact coordinates where xybolt placed the box back,
    # so we can overlay the box front exactly on top of it.
    c1x, c1y = most.props.c1pos
    return SVGgroup([most, boxfront], [(0,0), most.props.c1pos])

def pterm_icon(size):
    # Just a really big computer.

    w = h = 32 * size

    c = computer(size * 1.4)

    # Centre c in the output rectangle.
    bb = c.bbox()
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h

    return SVGgroup([c], [((w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2)])

def ptermcfg_icon(size):
    w = h = 32 * size
    s = spanner(size)
    b = pterm_icon(size)
    bb = s.bbox()
    return SVGgroup([b, s], [(0,0), ((w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2)])

def pageant_icon(size):
    # A biggish computer, in a hat.

    w = h = 32 * size

    c = computer(size * 1.2)
    ht = hat(size)

    cbb = c.bbox()
    hbb = ht.bbox()

    # Determine the relative coordinates of the computer and hat. We
    # do this by first centring one on the other, then adjusting by
    # hand.
    xrel = (cbb[0]+cbb[2]-hbb[0]-hbb[2])/2 + 2*size
    yrel = (cbb[1]+cbb[3]-hbb[1]-hbb[3])/2 + 12*size

    both = SVGgroup([c, ht], [(0,0), (xrel,yrel)])

    # Mostly-centre the result in the output rectangle. We want
    # everything to fit in frame, but we also want to make it look as
    # if the computer is more x-centred than the hat.

    # Coordinates that would centre the whole group.
    bb = both.bbox()
    assert bb[2]-bb[0] <= w and bb[3]-bb[1] <= h
    grx, gry = (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2

    # Coords that would centre just the computer.
    bb = c.bbox()
    crx, cry = (w-bb[0]-bb[2])/2, (h-bb[1]-bb[3])/2

    # Use gry unchanged, but linear-combine grx with crx.
    return SVGgroup([both], [(grx+0.6*(crx-grx), gry)])

# Test and output functions.

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
def halftone(col1, col2):
    r1,g1,b1,a1 = col1
    r2,g2,b2,a2 = col2
    return ((r1+r2)//2, (g1+g2)//2, (b1+b2)//2, (a1+a2)//2)

def drawicon(func, width, fname):
    icon = func(width / 32.0)
    minx, miny, maxx, maxy = icon.bbox()
    #assert minx >= 0 and miny >= 0 and maxx <= width and maxy <= width

    svgroot = ET.Element("svg")
    svgroot.attrib["xmlns"] = "http://www.w3.org/2000/svg"
    svgroot.attrib["viewBox"] = "0 0 {w:d} {w:d}".format(w=width)

    defs = ET.Element("defs")
    idents = ("iconid{:d}".format(n) for n in itertools.count())
    if icon.add_clip_paths(defs, idents, 0, width):
        svgroot.append(defs)

    svgroot.append(icon.render(0,width))

    ET.ElementTree(svgroot).write(fname)

def main():
    parser = argparse.ArgumentParser(description='Generate PuTTY SVG icons.')
    parser.add_argument("icon", help="Which icon to generate.")
    parser.add_argument("-s", "--size", type=int, default=48,
                        help="Notional pixel size to base the SVG on.")
    parser.add_argument("-o", "--output", required=True,
                        help="Output file name.")
    args = parser.parse_args()

    drawicon(eval(args.icon), args.size, args.output)

if __name__ == '__main__':
    main()
