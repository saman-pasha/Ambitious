/* src/paint/svg.c -- SVG drawn (svg.h): an svg element's subtree made a scene, the scene
   drawn, and an SVG document's bytes read, made a scene and drawn as an image.

   THE SCENE (amb_svg_scene). The svg element's subtree is walked with each element's
   computed values (the cascade's: presentation attributes, style attributes, the page's
   sheets). Its inherited SVG properties are taken from the parent the walk came from, so
   that a use element's tree inherits from the use (SVG 2, 5.6). Drawn: g, a, switch (its
   first child element whose conditional processing attributes hold, as Blink picks it),
   nested svg (a viewport: x, y, width, height, viewBox, preserveAspectRatio, clipped unless
   its overflow is visible), use (href or xlink:href, x and y; a symbol or an svg as a
   viewport its width and height make), rect (rx, ry), circle, ellipse, line, polyline,
   polygon, path (every command; an arc made cubics, its centre found as SVG 2's appendix B
   finds it, a quarter turn a cubic at most), image (its viewport, preserveAspectRatio, the
   loader's picture placed when drawn; in an SVG document drawn as an image, which fetches
   nothing, a data: URL's bytes carried in its line). The geometry properties (cx, cy, r, rx, ry, x, y,
   width, height, d) are a rule's declaration of them where there is one, else the
   attribute. An element whose systemLanguage or requiredExtensions fails is not drawn.
   Never drawn of themselves: defs, symbol, clipPath, mask, marker, pattern, filter, the
   gradients, stop, style, title, desc, metadata, and what is not known. Transforms: the
   transform attribute (matrix, translate, scale, rotate about a centre, skewX, skewY), a
   declared CSS transform over it (translate, scale, rotate, skew, matrix: px and angles),
   transform-origin 0 0 as Chromium's sheet has it for SVG elements. Paints: none, a
   colour, currentcolor (each element's own colour). Strokes: stroke-width (a percentage
   of the viewport's normalized diagonal), linecap, linejoin (miter-clip and arcs drawn as
   miter), miterlimit, dasharray and dashoffset (scaled by the shape's pathLength, as Blink
   scales them), vector-effect: non-scaling-stroke (an N in its line; its other values
   Chromium does not read). Opacity: a group of its own. Fill, stroke and markers in the
   paint-order.
   Paints also: url(#id) of a linearGradient or radialGradient (gradientUnits,
   gradientTransform, spreadMethod, its stops' offsets, stop-color and stop-opacity, every
   attribute and the stops inherited through href), or of a pattern (patternUnits,
   patternContentUnits, viewBox and preserveAspectRatio, patternTransform, x, y, width and
   height, its attributes and content inherited through href: a block, P ... E, of its
   tile's content), and a fallback. Clip-path: url(#id) of a clipPath (clipPathUnits, its
   transform, its shapes' and uses' clip-rule, a clip-path on the clipPath itself), a
   group of its own. Mask: url(#id) of a mask (maskUnits, maskContentUnits, x, y, width,
   height, mask-type: a block, M ... E, after its group's clip; a shape, text or image of its
   content whose color-interpolation is linearRGB a group made linear as it ends, as Blink
   paints it). Filter: url(#id) of a filter (filterUnits, primitiveUnits, its region, each
   primitive's in, in2, result, subregion and color-interpolation-filters; feGaussianBlur,
   feOffset, feFlood, feComposite, feMerge, feColorMatrix, feBlend, feDropShadow,
   feComponentTransfer, feTurbulence, feMorphology, feConvolveMatrix, feDisplacementMap,
   feTile, feImage (an element of the document drawn as a use draws it, moved to the
   subregion's corner as Blink moves it, or the loader's picture of its href: a block, I ...
   E), feDiffuseLighting and feSpecularLighting (lighting-color, the first light element in
   them): a block, F ... E; a filter's href, which Chromium does not follow, and
   feGaussianBlur's edgeMode, which it does not read, are not read); or CSS's filter
   functions (blur, drop-shadow, grayscale, sepia, saturate,
   hue-rotate, brightness, contrast, invert, opacity: each its shorthand primitive, in sRGB,
   the region what they are given outset by their reach); a list of url()s and functions a
   group for each, chained in its order. A reference to no such element is passed over, as
   Chromium passes it. A url(#id) or an href names the element of that id in the tree of the
   element whose value or attribute it is, the document's or a shadow tree's (a ::part() rule
   of the document's looks in the part's tree), as Chromium looks; never in a template's
   contents. An element's bounding box (objectBoundingBox units) is
   its geometry's, a text's glyph cells', an image's viewport, a group's children's.
   Markers: marker-start, -mid and -end on a path, line, polyline or polygon, at the places
   Blink's SVGMarkerDataBuilder finds (an arc one segment, a closed subpath's first and
   last place bisecting its closing segment and its first), each marker's content through
   its viewBox, orient, markerUnits and refX, refY, clipped (not anti-aliased) unless its
   overflow is visible.
   Text: text, tspan and textPath (x, y, dx, dy and rotate lists, textLength and
   lengthAdjust, text-anchor by chunks, a textPath's path attribute (over its href), href
   (a path element's alone, as Blink takes it) and startOffset (scaled by its path's
   pathLength), dominant-baseline, font-size, -family,
   -weight, -style, letter- and word-spacing, fill, stroke), shaped as Blink shapes it
   (src/paint/shape.c: ligatures, kerning, marks, Arabic's joining; runs split at an
   absolutely placed character, a textPath's edge and a change of face, size,
   letter-spacing or direction, a right-to-left run's clusters in visual order), each
   cluster's glyphs drawn by the character it begins at: upright glyphs drawn as the page's
   text is, others (turned, scaled across, on a path, under a matrix that is no uniform
   scale) and every stroke as their outlines. textPath's side, method and spacing Chromium
   does not read.

   THE DRAWING (amb_svg_draw). Every coordinate is mapped to the device's px, an svg's
   content placed at whole CSS px as Chromium places it. Curves are flattened at the
   device's px (0.05 px at most from the curve, a point just inside each end, so that a
   join there meets the curve's own tangent, as Skia's does). A stroke is made in user
   space, as Skia makes one: the union of its segments' quads, its joins (a miter within its
   limit, else a bevel; round) and its caps (butt, round, square; a zero-length subpath's
   upright), each piece turned to wind one way so that the non-zero rule unites them. Its
   dashes are cut first, where SkDashPath cuts them over SkContourMeasure's chords (each
   curve halved while it bends more than half a device px from its chord), a closed
   subpath's last dash joined to its first. A stroke no wider than a device px is drawn as
   Skia draws an anti-aliased hairline (its curves in Skia's pieces, each segment a px of
   its major axis at a time in 26.6 fixed point, its alpha its width). A shape's coverage
   is found over sixteen rows a pixel, each span's ends exact across it, under its fill
   rule, then painted (a gradient's colour found at each pixel's centre, its stops
   interpolated unpremultiplied, as Blink asks Skia for SVG's; a pattern's tile drawn as
   Skia rasterizes a picture shader's, at its matrix's scale rounded up to whole px, and
   sampled bilinearly, repeated) and blended over what its layer holds. A group with an
   opacity, a clip, a mask or a filter is drawn in a layer of its own and blended down:
   first through its filter (each primitive in its colour space, eight bits a channel as
   Skia's image filters keep them, Gaussian blurs as Skia's three box passes; turbulence as
   Skia's SkPerlinNoiseShader makes it, in the filter's user space at each px's centre half
   a unit on; morphology's radii rounded in device px; convolution's kernel turned about,
   bias added, its edge mode about the subregion; displacement's nearest px; a tile from
   the px inside its input's subregion; lighting's normals by Sobel's kernels, the bounds'
   edge px repeated, as Skia's lighting filter finds them; results cropped to their
   subregions), then by its clip's shapes' coverage (their union: a
   clip-path's times its bounding box's, as Blink clips by both and Skia multiplies the two;
   a rectangle along the axes clips as itself alone) and its mask's (its content's
   luminance or alpha within its region, the region rounded to whole px), and its opacity.
   An image element's raster is sampled bilinearly from the mipmap level Chromium's decode
   cache picks; an SVG picture is drawn as its own scene; a picture that fails to load or
   decode is Blink's broken image (14 x 16 px), drawn as a raster is.
   Glyphs are drawn from font.c's masks at their device size, placed as the page's text is
   (a quarter of a px across, a whole px down), or filled and stroked as their outlines.

   THE DOCUMENT (amb_svg_image). An SVG file's bytes are read as XML (elements, attributes,
   text, CDATA, the five predefined entities and character references; the XML declaration,
   comments, processing instructions and a DOCTYPE skipped; namespaces by their xmlns
   attributes, the xlink one's attributes kept in it) into a dom_document of their own,
   which the cascade styles (its style elements too). Its natural size is its root's width
   and height (one from the other through its viewBox); it is drawn at that size scaled
   into its box, or, with none, at the box's size (its viewBox fitted into it).

   SVG'S EFFECTS ON A CSS BOX (amb_svg_effects, amb_svg_fx). The filters, the clipPath and the
   mask an HTML element's (or an outermost svg's) filter, clip-path and mask name by url() are
   written for its border box as groups of a scene are -- its user space the border box's,
   from its top left, a CSS px a unit -- and paint draws the box's layer through them, as a
   group's layer is drawn (a clip the union of its shapes alone, not held to their bounding
   box as an SVG element's is: Chromium's on a box, measured).

   Not done: foreignObject, SMIL animation, a reference into another document, an feImage
   element whose lengths are percentages (Blink maps its viewport into the subregion),
   systemLanguage (no tag holds, as in the headless Chromium the gates are measured in), bidi
   in SVG text beyond a right-to-left script's runs in a left-to-right chunk, and web fonts
   in SVG text (the built-in faces and the machine's fonts they fall back to alone). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "font.h"
#include "shape.h"

amb_interface(dom);
amb_interface(css);
amb_interface(layout);
amb_interface(net);

#include "paint.h"
#include "image.h"
#include "svg.h"
#include "../css/css.h"
#include "../dom/dom.h"

/* ---- matrices: x' = a x + c y + e, y' = b x + d y + f ---- */

typedef struct sv_mat { double a, b, c, d, e, f; } sv_mat;

static sv_mat sv_make(double a, double b, double c, double d, double e, double f) {
    sv_mat m;
    m.a = a; m.b = b; m.c = c; m.d = d; m.e = e; m.f = f;
    return m;
}
static sv_mat sv_id(void) { return sv_make(1, 0, 0, 1, 0, 0); }
/* p after q: q applied first */
static sv_mat sv_mul(sv_mat p, sv_mat q) {
    return sv_make(p.a * q.a + p.c * q.b, p.b * q.a + p.d * q.b, p.a * q.c + p.c * q.d, p.b * q.c + p.d * q.d, p.a * q.e + p.c * q.f + p.e,
                   p.b * q.e + p.d * q.f + p.f);
}
static int sv_inv(sv_mat m, sv_mat *out) {
    double det = m.a * m.d - m.b * m.c;
    if (fabs(det) < 1e-18) return 0;
    double k = 1 / det;
    *out = sv_make(m.d * k, -m.b * k, -m.c * k, m.a * k, (m.c * m.f - m.d * m.e) * k, (m.b * m.e - m.a * m.f) * k);
    return 1;
}
static double sv_ax(sv_mat m, double x, double y) { return m.a * x + m.c * y + m.e; }
static double sv_ay(sv_mat m, double x, double y) { return m.b * x + m.d * y + m.f; }
/* how far the matrix stretches a length at most */
static double sv_scale(sv_mat m) {
    double p = m.a * m.a + m.b * m.b, q = m.c * m.c + m.d * m.d, r = m.a * m.c + m.b * m.d;
    double mid = (p + q) / 2, dif = sqrt((p - q) * (p - q) / 4 + r * r);
    return sqrt(mid + dif);
}

/* ---- numbers, as SVG's attributes and path data write them ---- */

static int sv_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static size_t sv_skip_ws(const char *s, size_t n, size_t i) {
    while (i < n && sv_ws(s[i])) i++;
    return i;
}
/* white space, perhaps a comma, white space */
static size_t sv_skip_sep(const char *s, size_t n, size_t i) {
    i = sv_skip_ws(s, n, i);
    if (i < n && s[i] == ',') i = sv_skip_ws(s, n, i + 1);
    return i;
}
/* a number from s[*i]: its sign, digits, point and exponent (an e not followed by digits is
   no exponent); 1 and *i past it */
static int sv_number(const char *s, size_t n, size_t *i, double *out) {
    size_t p = *i;
    int neg = 0, digits = 0;
    if (p < n && (s[p] == '+' || s[p] == '-')) { neg = s[p] == '-'; p++; }
    double v = 0;
    while (p < n && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); p++; digits++; }
    if (p < n && s[p] == '.') {
        p++;
        double k = 0.1;
        while (p < n && s[p] >= '0' && s[p] <= '9') { v += (s[p] - '0') * k; k *= 0.1; p++; digits++; }
    }
    if (!digits) return 0;
    if (p < n && (s[p] == 'e' || s[p] == 'E')) {
        size_t q = p + 1;
        int eneg = 0;
        if (q < n && (s[q] == '+' || s[q] == '-')) { eneg = s[q] == '-'; q++; }
        if (q < n && s[q] >= '0' && s[q] <= '9') {
            int ex = 0;
            while (q < n && s[q] >= '0' && s[q] <= '9') { if (ex < 400) ex = ex * 10 + (s[q] - '0'); q++; }
            v *= pow(10.0, eneg ? -ex : ex);
            p = q;
        }
    }
    /* as a float holds it, at most: past that, none (Chromium's numbers are floats) */
    if (!(v <= 3.4e38)) return 0;
    *out = neg ? -v : v;
    *i = p;
    return 1;
}
/* a path's flag: 0 or 1, a character alone */
static int sv_flag(const char *s, size_t n, size_t *i, int *out) {
    if (*i < n && (s[*i] == '0' || s[*i] == '1')) { *out = s[*i] == '1'; (*i)++; return 1; }
    return 0;
}

/* ---- path data (SVG 2, 9.3): read into absolute moves, lines, curves and closes ----
   A curve is a cubic (C), a quadratic (Q), or a cubic that stands for an elliptical arc's
   piece (K): drawn as a cubic, measured as Skia measures the conic it draws an arc with */

enum { SV_M = 'M', SV_L = 'L', SV_C = 'C', SV_Q = 'Q', SV_K = 'K', SV_Z = 'Z' };

static void sv_put2(vector<double> *out, int cmd, double x, double y) {
    push_back(out, (double)cmd);
    push_back(out, x);
    push_back(out, y);
}
static void sv_put_c(vector<double> *out, int cmd, double x1, double y1, double x2, double y2, double x, double y) {
    push_back(out, (double)cmd);
    push_back(out, x1);
    push_back(out, y1);
    push_back(out, x2);
    push_back(out, y2);
    push_back(out, x);
    push_back(out, y);
}
static void sv_put_cubic(vector<double> *out, double x1, double y1, double x2, double y2, double x, double y) {
    sv_put_c(out, SV_C, x1, y1, x2, y2, x, y);
}
static void sv_put_quad(vector<double> *out, double x1, double y1, double x, double y) {
    push_back(out, (double)SV_Q);
    push_back(out, x1);
    push_back(out, y1);
    push_back(out, x);
    push_back(out, y);
}
/* the values a command takes after its letter */
static int sv_args(int c) { return c == SV_C || c == SV_K ? 6 : c == SV_Q ? 4 : c == SV_Z ? 0 : 2; }
/* the curve command at p[i], from (x0, y0), as a cubic's four points (a quadratic raised) */
static void sv_curve_at(const vector<double> *p, size_t i, double x0, double y0, double *q) {
    q[0] = x0;
    q[1] = y0;
    if ((int)at(*p, i) == SV_Q) {
        double qx = at(*p, i + 1), qy = at(*p, i + 2), x = at(*p, i + 3), y = at(*p, i + 4);
        q[2] = x0 + 2.0 / 3 * (qx - x0);
        q[3] = y0 + 2.0 / 3 * (qy - y0);
        q[4] = x + 2.0 / 3 * (qx - x);
        q[5] = y + 2.0 / 3 * (qy - y);
        q[6] = x;
        q[7] = y;
        return;
    }
    for (int k = 0; k < 6; k++) q[2 + k] = at(*p, i + 1 + (size_t)k);
}

/* an elliptical arc from (x1, y1) to (x2, y2) as cubics (SVG 2, B.2.4's centre found, then
   a cubic a quarter turn at most) */
static void sv_arc(vector<double> *out, double x1, double y1, double rx, double ry, double rot, int large, int sweep, double x2, double y2) {
    if (x1 == x2 && y1 == y2) return;
    rx = fabs(rx);
    ry = fabs(ry);
    if (rx == 0 || ry == 0) { sv_put2(out, SV_L, x2, y2); return; }
    double phi = rot * M_PI / 180, cp = cos(phi), sp = sin(phi);
    double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    double lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lam > 1) { double s = sqrt(lam); rx *= s; ry *= s; }
    double num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, den = rx * rx * yp * yp + ry * ry * xp * xp;
    double coef = den > 0 && num > 0 ? sqrt(num / den) : 0;
    if (large == sweep) coef = -coef;
    double cxp = coef * rx * yp / ry, cyp = -coef * ry * xp / rx;
    double cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry, vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    double t1 = atan2(uy, ux);
    double dt = atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && dt > 0) dt -= 2 * M_PI;
    else if (sweep && dt < 0) dt += 2 * M_PI;
    double fsegs = ceil(fabs(dt) / (M_PI / 2) - 1e-9);
    int segs = fsegs >= 1 && fsegs <= 4 ? (int)fsegs : 1;
    double d = dt / segs, k = 4.0 / 3 * tan(d / 4);
    double a = t1;
    for (int i = 0; i < segs; i++) {
        double b = a + d;
        double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b);
        double p0x = cx + rx * ca * cp - ry * sa * sp, p0y = cy + rx * ca * sp + ry * sa * cp;
        double d0x = -rx * sa * cp - ry * ca * sp, d0y = -rx * sa * sp + ry * ca * cp;
        double p1x = cx + rx * cb * cp - ry * sb * sp, p1y = cy + rx * cb * sp + ry * sb * cp;
        double d1x = -rx * sb * cp - ry * cb * sp, d1y = -rx * sb * sp + ry * cb * cp;
        if (i == segs - 1) { p1x = x2; p1y = y2; }
        sv_put_c(out, SV_K, p0x + k * d0x, p0y + k * d0y, p1x - k * d1x, p1y - k * d1y, p1x, p1y);
        a = b;
    }
}

/* a segment's features for markers (SVG 2, 13.7.3, as Blink's SVGMarkerDataBuilder takes
   them): its kind (SV_M, SV_L, SV_C, SV_Q, SV_Z), its end point, and its tangents at its
   start and its end, appended to feat */
static void sv_feat(vector<double> *feat, int kind, double x, double y, double sx, double sy, double ex, double ey) {
    push_back(feat, (double)kind);
    push_back(feat, x);
    push_back(feat, y);
    push_back(feat, sx);
    push_back(feat, sy);
    push_back(feat, ex);
    push_back(feat, ey);
}
/* a quadratic's tangents, from (x0, y0) by its control (qx, qy) to (x, y): a zero one the other's */
static void sv_feat_quad(vector<double> *feat, int kind, double x0, double y0, double qx, double qy, double x, double y) {
    double sx = qx - x0, sy = qy - y0, ex = x - qx, ey = y - qy;
    if (sx == 0 && sy == 0) { sx = ex; sy = ey; }
    else if (ex == 0 && ey == 0) { ex = sx; ey = sy; }
    sv_feat(feat, kind, x, y, sx, sy, ex, ey);
}
/* a cubic's: its controls' directions, a zero one taken from the quadratic its other points make */
static void sv_feat_cubic(vector<double> *feat, double x0, double y0, double x1, double y1, double x2, double y2, double x, double y) {
    double sx = x1 - x0, sy = y1 - y0, ex = x - x2, ey = y - y2;
    if (sx == 0 && sy == 0) { sv_feat_quad(feat, SV_C, x1, y1, x2, y2, x, y); return; }
    if (ex == 0 && ey == 0) { sv_feat_quad(feat, SV_C, x0, y0, x1, y1, x2, y2); (*feat)[len(*feat) - 6] = x; (*feat)[len(*feat) - 5] = y; return; }
    sv_feat(feat, SV_C, x, y, sx, sy, ex, ey);
}

static size_t sv_path_data(const char *s, size_t n, vector<double> *out, vector<double> *feat);

size_t amb_svg_path(const char *s, size_t n, vector<double> *out) {
    vector<double> feat;
    return sv_path_data(s, n, out, &feat);
}

/* path data read into out (amb_svg_path's commands), and each segment's features into feat
   (sv_feat: an arc one segment, its tangents at its ends; a close after which another command
   comes no move) */
static size_t sv_path_data(const char *s, size_t n, vector<double> *out, vector<double> *feat) {
    size_t i = sv_skip_ws(s, n, 0), count = 0;
    double cx = 0, cy = 0, sx = 0, sy = 0, qx = 0, qy = 0, kx = 0, ky = 0;   /* current, subpath start, last Q and C controls */
    char cmd = 0, prev = 0;
    int open = 0, closed = 0;   /* a subpath begun; a close just made, which the next command starts from */
    while (i < n) {
        char c = s[i];
        int letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (letter) {
            if (!strchr("MmLlHhVvCcSsQqTtAaZz", c)) break;
            cmd = c;
            i = sv_skip_ws(s, n, i + 1);
        } else {
            /* a command repeated: its letter left out (a move's further pairs are lines) */
            if (!cmd || cmd == 'Z' || cmd == 'z') break;
            if (cmd == 'M') cmd = 'L';
            else if (cmd == 'm') cmd = 'l';
        }
        if (!open && cmd != 'M' && cmd != 'm') break;   /* path data begins with a move */
        int rel = cmd >= 'a';
        char up = (char)(rel ? cmd - 32 : cmd);
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double a[7];
        int need = up == 'M' || up == 'L' || up == 'T' ? 2 : up == 'H' || up == 'V' ? 1 : up == 'C' ? 6 : up == 'S' || up == 'Q' ? 4 : up == 'A' ? 7 : 0;
        int ok = 1;
        size_t j = i;
        for (int k = 0; k < need && ok; k++) {
            if (k > 0) j = sv_skip_sep(s, n, j);
            if (up == 'A' && (k == 3 || k == 4)) {
                int fl;
                ok = sv_flag(s, n, &j, &fl);
                a[k] = fl;
            } else ok = sv_number(s, n, &j, &a[k]);
        }
        if (!ok) break;
        i = sv_skip_sep(s, n, j);
        if (up != 'M' && up != 'Z' && closed) { sv_put2(out, SV_M, sx, sy); }
        closed = 0;
        if (up == 'M') {
            double px = cx, py = cy;
            cx = a[0] + ox; cy = a[1] + oy;
            sx = cx; sy = cy;
            sv_put2(out, SV_M, cx, cy);
            sv_feat(feat, SV_M, cx, cy, cx - px, cy - py, cx - px, cy - py);
            open = 1;
            prev = 'M';
        } else if (up == 'L' || up == 'H' || up == 'V') {
            double px = cx, py = cy;
            if (up == 'L') { cx = a[0] + ox; cy = a[1] + oy; }
            else if (up == 'H') cx = a[0] + ox;
            else cy = a[0] + oy;
            sv_put2(out, SV_L, cx, cy);
            sv_feat(feat, SV_L, cx, cy, cx - px, cy - py, cx - px, cy - py);
            prev = 'L';
        } else if (up == 'C' || up == 'S') {
            double x1, y1, x2, y2, x, y;
            if (up == 'C') { x1 = a[0] + ox; y1 = a[1] + oy; x2 = a[2] + ox; y2 = a[3] + oy; x = a[4] + ox; y = a[5] + oy; }
            else {
                x1 = prev == 'C' ? 2 * cx - kx : cx;
                y1 = prev == 'C' ? 2 * cy - ky : cy;
                x2 = a[0] + ox; y2 = a[1] + oy; x = a[2] + ox; y = a[3] + oy;
            }
            sv_put_cubic(out, x1, y1, x2, y2, x, y);
            sv_feat_cubic(feat, cx, cy, x1, y1, x2, y2, x, y);
            kx = x2; ky = y2;
            cx = x; cy = y;
            prev = 'C';
        } else if (up == 'Q' || up == 'T') {
            double x1, y1, x, y;
            if (up == 'Q') { x1 = a[0] + ox; y1 = a[1] + oy; x = a[2] + ox; y = a[3] + oy; }
            else {
                x1 = prev == 'Q' ? 2 * cx - qx : cx;
                y1 = prev == 'Q' ? 2 * cy - qy : cy;
                x = a[0] + ox; y = a[1] + oy;
            }
            sv_put_quad(out, x1, y1, x, y);
            sv_feat_quad(feat, SV_Q, cx, cy, x1, y1, x, y);
            qx = x1; qy = y1;
            cx = x; cy = y;
            prev = 'Q';
        } else if (up == 'A') {
            double x = a[5] + ox, y = a[6] + oy;
            size_t from = len(*out);
            sv_arc(out, cx, cy, a[0], a[1], a[2], (int)a[3], (int)a[4], x, y);
            /* one segment: the arc's tangents at its ends, its first and last pieces' */
            double ts[4] = { x - cx, y - cy, x - cx, y - cy };
            if (len(*out) > from && (int)at(*out, from) == SV_K) {
                size_t last = len(*out) - 7;
                ts[0] = at(*out, from + 1) - cx;
                ts[1] = at(*out, from + 2) - cy;
                ts[2] = x - at(*out, last + 3);
                ts[3] = y - at(*out, last + 4);
            }
            sv_feat(feat, SV_C, x, y, ts[0], ts[1], ts[2], ts[3]);
            cx = x; cy = y;
            prev = 'A';
        } else {
            push_back(out, (double)SV_Z);
            sv_feat(feat, SV_Z, sx, sy, sx - cx, sy - cy, sx - cx, sy - cy);
            cx = sx; cy = sy;
            closed = 1;
            prev = 'Z';
        }
        count++;
    }
    return count;
}

/* ---- paths measured (dashes, text on a path): their pieces ---- */

enum { SV_TMAX = 0x3fffffff };
/* a contour's measured piece: how far along the contour it ends, its curve (its record's
   place in the curves: its kind, then four points), and the curve's t there, of SV_TMAX */
typedef struct sv_mseg { double dist; uint32_t ci, t; } sv_mseg;

static void sv_bez_pt(const double *q, int kind, double t, double *x, double *y);
static void sv_mseg_add(vector<sv_mseg> *ms, double *dist, double d, uint32_t ci, int t);
static void sv_meas_bez(const double *q, int quad, double *dist, int mint, int maxt, uint32_t ci, double tol, vector<sv_mseg> *ms);
static void sv_meas_arc(const double *q, double *dist, int mint, double mx, double my, int maxt, double nx, double ny, uint32_t ci,
                        double tol, vector<sv_mseg> *ms);
static size_t sv_dist_at(const vector<sv_mseg> *ms, size_t m0, size_t m1, double dist, double *t);

/* ---- markers' places (SVG 2, 13.7.3, as Blink's SVGMarkerDataBuilder finds them) ---- */

static double sv_slope(double x, double y) { return atan2(y, x) * 180 / M_PI; }
/* the bisector of two directions (degrees), as Blink takes it: the incoming one turned a
   whole turn when the two are more than half a turn apart */
static double sv_bisect(double in, double out) {
    if (fabs(in - out) > 180) in += 360;
    return (in + out) / 2;
}
/* the angle at the last place pushed (and, a subpath closed, at its first place too): ends 1
   when it ends an open subpath; the slopes in and out (out the subpath's first segment's when
   the last segment closed it) */
static void sv_mark_angle(vector<double> *pos, int last, int ends, double *in, double *out, const double *first_out, size_t first_at) {
    int type;   /* 0 bisecting, 1 inbound, 2 outbound */
    if (last == SV_Z) { out[0] = first_out[0]; out[1] = first_out[1]; type = 0; }
    else if (ends) type = 1;
    else if (last == SV_M) type = 2;
    else type = 0;
    double a = type == 1 ? sv_slope(in[0], in[1]) : type == 2 ? sv_slope(out[0], out[1]) : sv_bisect(sv_slope(in[0], in[1]), sv_slope(out[0], out[1]));
    a = (double)(float)a;
    (*pos)[len(*pos) - 1] = a;
    if (last == SV_Z && first_at * 4 + 3 < len(*pos)) (*pos)[first_at * 4 + 3] = a;
}
/* the marker places of a path's segments (sv_feat's records): each its type (0 start, 1 mid,
   2 end), its point and its angle, into pos. Each segment's start records the place before it
   (the first the start), the end the last; a vertex bisects its segments' slopes, a subpath's
   first takes its first segment's, an open subpath's last its last's, and a closed one's
   first and last bisect its closing segment and its first */
static void sv_marker_places(const vector<double> *feat, vector<double> *pos) {
    size_t nf = len(*feat) / 7;
    double in[2] = { 0, 0 }, out[2] = { 0, 0 }, first_out[2] = { 0, 0 }, ox = 0, oy = 0;
    int last = SV_M;
    size_t first_at = 0;
    for (size_t e = 0; e < nf; e++) {
        const double *r = data(*feat) + 7 * e;
        int kind = (int)r[0];
        out[0] = r[3];
        out[1] = r[4];
        if (e > 0) {
            push_back(pos, e == 1 ? 0.0 : 1.0);
            push_back(pos, ox);
            push_back(pos, oy);
            push_back(pos, 0.0);
            sv_mark_angle(pos, last, kind == SV_M, in, out, first_out, first_at);
        }
        in[0] = r[5];
        in[1] = r[6];
        ox = r[1];
        oy = r[2];
        if (kind == SV_M) first_at = len(*pos) / 4;
        else if (last == SV_M) { first_out[0] = out[0]; first_out[1] = out[1]; }
        last = kind;
    }
    if (!nf) return;
    push_back(pos, 2.0);
    push_back(pos, ox);
    push_back(pos, oy);
    push_back(pos, 0.0);
    sv_mark_angle(pos, last, 1, in, out, first_out, first_at);
}
/* the segments' features of a path of moves, lines and closes alone (a polyline's, a
   polygon's, a line's) */
static void sv_feat_lines(const vector<double> *p, vector<double> *feat) {
    size_t i = 0, n = len(*p);
    double cx = 0, cy = 0, sx = 0, sy = 0;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_Z) { sv_feat(feat, SV_Z, sx, sy, sx - cx, sy - cy, sx - cx, sy - cy); cx = sx; cy = sy; i++; continue; }
        if (c != SV_M && c != SV_L) { i += (size_t)sv_args(c) + 1; continue; }
        double x = at(*p, i + 1), y = at(*p, i + 2);
        sv_feat(feat, c, x, y, x - cx, y - cy, x - cx, y - cy);
        if (c == SV_M) { sx = x; sy = y; }
        cx = x;
        cy = y;
        i += 3;
    }
}

/* ---- the scene's text: numbers, words, matrices, paths ---- */

static void sv_w(vector<char> *out, const char *s) { append_range(out, s, strlen(s)); }
static void sv_n(vector<char> *out, double v) {
    char buf[40];
    if (fabs(v) < 1e-12 || v != v) v = 0;
    int k = snprintf(buf, sizeof buf, " %.10g", v);
    if (k > 0) append_range(out, buf, (size_t)k);
}
static void sv_hex(vector<char> *out, uint32_t v) {
    char buf[16];
    int k = snprintf(buf, sizeof buf, " %08x", v);
    if (k > 0) append_range(out, buf, (size_t)k);
}
static void sv_m(vector<char> *out, sv_mat m) {
    sv_n(out, m.a); sv_n(out, m.b); sv_n(out, m.c); sv_n(out, m.d); sv_n(out, m.e); sv_n(out, m.f);
}
static void sv_path_out(vector<char> *out, const vector<double> *p) {
    size_t i = 0, n = len(*p);
    while (i < n) {
        int c = (int)at(*p, i);
        int k = sv_args(c);
        sv_w(out, c == SV_M ? " M" : c == SV_L ? " L" : c == SV_C ? " C" : c == SV_Q ? " Q" : c == SV_K ? " K" : " Z");
        for (int q = 1; q <= k; q++) sv_n(out, at(*p, i + (size_t)q));
        i += (size_t)k + 1;
    }
}

/* ---- the scene made: the subtree walked, each element's values from the cascade ---- */

/* the inherited properties a walk carries: for each, the computed value (st's cval) that
   gives it -- the element's own when a declaration gave it, else the one its parent in the
   walk had (a use's tree inherits from the use) */
enum { SI_FILL, SI_FILL_OPACITY, SI_FILL_RULE, SI_STROKE, SI_STROKE_WIDTH, SI_LINECAP, SI_LINEJOIN, SI_MITER, SI_DASHARRAY, SI_DASHOFFSET,
       SI_STROKE_OPACITY, SI_CLIP_RULE, SI_ANCHOR, SI_COLOR, SI_VISIBILITY, SI_FONT_SIZE, SI_FONT_WEIGHT, SI_FONT_STYLE, SI_FONT_FAMILY,
       SI_LETTER_SPACING, SI_WORD_SPACING, SI_MARKER_START, SI_MARKER_MID, SI_MARKER_END, SI_PAINT_ORDER, SI_BASELINE, SI_CIF, SI_N };
static const int sv_iprops[SI_N] = { CSS_PROP_FILL, CSS_PROP_FILL_OPACITY, CSS_PROP_FILL_RULE, CSS_PROP_STROKE, CSS_PROP_STROKE_WIDTH,
                                     CSS_PROP_STROKE_LINECAP, CSS_PROP_STROKE_LINEJOIN, CSS_PROP_STROKE_MITERLIMIT, CSS_PROP_STROKE_DASHARRAY,
                                     CSS_PROP_STROKE_DASHOFFSET, CSS_PROP_STROKE_OPACITY, CSS_PROP_CLIP_RULE, CSS_PROP_TEXT_ANCHOR,
                                     CSS_PROP_COLOR, CSS_PROP_VISIBILITY, CSS_PROP_FONT_SIZE, CSS_PROP_FONT_WEIGHT, CSS_PROP_FONT_STYLE,
                                     CSS_PROP_FONT_FAMILY, CSS_PROP_LETTER_SPACING, CSS_PROP_WORD_SPACING, CSS_PROP_MARKER_START,
                                     CSS_PROP_MARKER_MID, CSS_PROP_MARKER_END, CSS_PROP_PAINT_ORDER, CSS_PROP_DOMINANT_BASELINE,
                                     CSS_PROP_COLOR_INTERPOLATION_FILTERS };

/* where a walk is: the nearest viewport's size (percentages), and the inherited values */
typedef struct sv_walk { double vw, vh; uint32_t src[SI_N]; } sv_walk;

/* the scene's context: the document, its style, each node's first value, the fonts, the
   scene's text, the gradients' count so far, the uses being expanded (no use is expanded
   inside itself) and the elements visited so far */
#define SB const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, vector<char> *out, uint32_t *ids, \
           vector<uint32_t> *uses, uint32_t *work
#define SA d, st, firsts, f, out, ids, uses, work
/* the elements a scene visits at most, uses' copies counted (a use of uses of uses ... stops) */
enum { SV_WORK = 100000 };

static uint32_t sv_first(const vector<uint32_t> *firsts, uint32_t x) { return x < len(*firsts) ? at(*firsts, x) : 0; }

static int sv_word_is(const css_style *st, uint32_t cv, const char *w) {
    uint32_t a = st->cval[cv].word;
    return a && strcmp(names_ptr(st, a), w) == 0;
}

/* the walk below the element x, from its parent's */
static void sv_inherit(const css_style *st, const vector<uint32_t> *firsts, uint32_t x, const sv_walk *pw, sv_walk *w) {
    *w = *pw;
    uint32_t c0 = sv_first(firsts, x);
    if (!c0) return;
    for (int k = 0; k < SI_N; k++) {
        uint32_t cv = c0 + (uint32_t)sv_iprops[k];
        css_how how = st->cval[cv].how;
        if (how == CSS_HOW_DECLARED || how == CSS_HOW_ANIMATED || (how == CSS_HOW_INITIAL && st->cval[cv].winner)) w->src[k] = cv;
    }
}

static int sv_is(const dom_document *d, uint32_t x, const char *name) {
    return d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_SVG && names_is(d, d->node[x].name, name, strlen(name));
}
static uint32_t sv_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t q = d->node[x].first_attr; q; q = d->attr[q].next) if (d->attr[q].name == a) return q;
    return 0;
}
/* an attribute's value copied into v; 0 when the element has none */
static int sv_attr_text(const dom_document *d, uint32_t x, const char *name, vector<char> *v) {
    uint32_t q = sv_attr(d, x, name);
    clear(*v);
    if (!q) return 0;
    append_range(v, text_ptr(d, d->attr[q].value), d->attr[q].value.len);
    return 1;
}
/* the element whose id is id[0, n) in the tree the element x is in (the document's, or a
   shadow tree's: CSS Scoping 1's tree-scoped references, as Chromium looks), the first in the
   document's order, or 0 */
static uint32_t sv_by_id(const dom_document *d, uint32_t x, const char *id, size_t n) {
    uint32_t ida = names_find(d, "id", 2);
    if (!ida || n == 0 || x >= d->node_len) return 0;
    uint32_t tree = dom_tree_root(d, x);
    for (uint32_t y = 1; y < d->node_len; y++) {
        if (!d->node[y].live || d->node[y].kind != DOM_NODE_KIND_ELEMENT) continue;
        for (uint32_t q = d->node[y].first_attr; q; q = d->attr[q].next)
            if (d->attr[q].name == ida && d->attr[q].value.len == n && memcmp(text_ptr(d, d->attr[q].value), id, n) == 0) {
                if (dom_tree_root(d, y) == tree) return y;
                break;
            }
    }
    return 0;
}
/* the element an IRI that the element x makes names (#id, white space about it): 0 for none */
static uint32_t sv_iri(const dom_document *d, uint32_t x, const char *s, size_t n) {
    size_t a = sv_skip_ws(s, n, 0), b = n;
    while (b > a && sv_ws(s[b - 1])) b--;
    if (b > a + 1 && (s[a] == '"' || s[a] == '\'') && s[b - 1] == s[a]) { a++; b--; }
    if (b <= a + 1 || s[a] != '#') return 0;
    return sv_by_id(d, x, s + a + 1, b - a - 1);
}
/* the element a use's or a gradient's href names (SVG 2's href over xlink:href) */
static uint32_t sv_href(const dom_document *d, uint32_t x) {
    vector<char> v;
    if (!sv_attr_text(d, x, "href", &v) && !sv_attr_text(d, x, "xlink:href", &v)) return 0;
    return sv_iri(d, x, data(v), len(v));
}
/* the element a url() at s, a value of the element x, names, and where the url() ends */
static uint32_t sv_url(const dom_document *d, uint32_t x, const char *s, size_t n, size_t *end) {
    size_t i = sv_skip_ws(s, n, 0);
    *end = i;
    if (i + 4 > n || memcmp(s + i, "url(", 4) != 0) return 0;
    size_t j = i + 4;
    while (j < n && s[j] != ')') j++;
    if (j >= n) return 0;
    *end = j + 1;
    return sv_iri(d, x, s + i + 4, j - i - 4);
}

/* a length an attribute writes, in user units: a number, px or another absolute unit, em
   and ex of the font size, a percentage of basis; *ok 0 when it is none */
static double sv_length(const char *s, size_t n, double basis, double fs, int *ok) {
    size_t i = sv_skip_ws(s, n, 0);
    double v;
    *ok = 0;
    if (!sv_number(s, n, &i, &v)) return 0;
    size_t u = i;
    while (u < n && !sv_ws(s[u])) u++;
    if (sv_skip_ws(s, n, u) != n) return 0;
    size_t un = u - i;
    const char *q = s + i;
    double k = 0;
    if (un == 0) k = 1;
    else if (un == 1 && q[0] == '%') k = basis / 100;
    else if (un == 2) {
        if (memcmp(q, "px", 2) == 0) k = 1;
        else if (memcmp(q, "in", 2) == 0) k = 96;
        else if (memcmp(q, "cm", 2) == 0) k = 96 / 2.54;
        else if (memcmp(q, "mm", 2) == 0) k = 96 / 25.4;
        else if (memcmp(q, "pt", 2) == 0) k = 4.0 / 3;
        else if (memcmp(q, "pc", 2) == 0) k = 16;
        else if (memcmp(q, "em", 2) == 0) k = fs;
        else if (memcmp(q, "ex", 2) == 0) k = fs / 2;
    }
    if (k == 0) return 0;
    *ok = 1;
    return v * k;
}
static double sv_len_attr(const dom_document *d, uint32_t x, const char *name, double basis, double fs, double dflt, int *given) {
    vector<char> v;
    *given = 0;
    if (!sv_attr_text(d, x, name, &v)) return dflt;
    int ok;
    double r = sv_length(data(v), len(v), basis, fs, &ok);
    if (!ok) return dflt;
    *given = 1;
    return r;
}
/* a number an attribute writes, or dflt */
static double sv_num_attr(const dom_document *d, uint32_t x, const char *name, double dflt) {
    vector<char> v;
    if (!sv_attr_text(d, x, name, &v)) return dflt;
    size_t i = sv_skip_ws(data(v), len(v), 0);
    double r;
    if (!sv_number(data(v), len(v), &i, &r)) return dflt;
    return r;
}

/* the font size a walk is at, px */
static double sv_fs(const css_style *st, const sv_walk *w) {
    double fs = st->cval[w->src[SI_FONT_SIZE]].num;
    return fs > 0 ? fs : 16;
}
static double sv_diag(const sv_walk *w) { return sqrt((w->vw * w->vw + w->vh * w->vh) / 2); }

/* a computed length (stroke-width's, stroke-dashoffset's): px, a number of user units, a
   percentage of basis */
static double sv_css_len(const css_style *st, uint32_t cv, double basis) {
    css_unit u = st->cval[cv].unit;
    double v = st->cval[cv].num;
    if (u == CSS_UNIT_PERCENT) return v * basis / 100;
    if (u == CSS_UNIT_CALC) return v + st->cval[cv].pct * basis / 100;
    return v;
}

/* ---- transforms ---- */

/* an angle CSS writes, in degrees (deg, rad, grad, turn; a bare number degrees) */
static int sv_angle(const char *s, size_t n, size_t *i, double *deg) {
    if (!sv_number(s, n, i, deg)) return 0;
    if (*i + 3 <= n && memcmp(s + *i, "deg", 3) == 0) *i += 3;
    else if (*i + 4 <= n && memcmp(s + *i, "grad", 4) == 0) { *deg *= 0.9; *i += 4; }
    else if (*i + 3 <= n && memcmp(s + *i, "rad", 3) == 0) { *deg *= 180 / M_PI; *i += 3; }
    else if (*i + 4 <= n && memcmp(s + *i, "turn", 4) == 0) { *deg *= 360; *i += 4; }
    return 1;
}
/* a transform list: SVG's attribute (css 0: numbers, rotate's centre) or CSS's (css 1: px
   lengths and angles); 1 and its matrix, or 0 when it does not parse (then none) */
static int sv_transform_list(const char *s, size_t n, int css, sv_mat *out) {
    sv_mat m = sv_id();
    size_t i = sv_skip_ws(s, n, 0);
    int any = 0;
    while (i < n) {
        size_t b = i;
        while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (css && s[i] == '3'))) i++;
        size_t nl = i - b;
        const char *nm = s + b;
        i = sv_skip_ws(s, n, i);
        if (nl == 0 || i >= n || s[i] != '(') return 0;
        i = sv_skip_ws(s, n, i + 1);
        double a[6];
        int k = 0;
        int angle = css && ((nl == 6 && memcmp(nm, "rotate", 6) == 0) || (nl >= 4 && memcmp(nm, "skew", 4) == 0));
        while (i < n && s[i] != ')' && k < 6) {
            int ok = angle ? sv_angle(s, n, &i, &a[k]) : sv_number(s, n, &i, &a[k]);
            if (!ok) return 0;
            if (css && i + 2 <= n && memcmp(s + i, "px", 2) == 0) i += 2;
            k++;
            i = sv_skip_sep(s, n, i);
        }
        if (i >= n || s[i] != ')') return 0;
        i = sv_skip_sep(s, n, i + 1);
        sv_mat t;
        if (nl == 6 && memcmp(nm, "matrix", 6) == 0 && k == 6) t = sv_make(a[0], a[1], a[2], a[3], a[4], a[5]);
        else if (nl == 9 && memcmp(nm, "translate", 9) == 0 && (k == 1 || k == 2)) t = sv_make(1, 0, 0, 1, a[0], k == 2 ? a[1] : 0);
        else if (css && nl == 10 && memcmp(nm, "translateX", 10) == 0 && k == 1) t = sv_make(1, 0, 0, 1, a[0], 0);
        else if (css && nl == 10 && memcmp(nm, "translateY", 10) == 0 && k == 1) t = sv_make(1, 0, 0, 1, 0, a[0]);
        else if (nl == 5 && memcmp(nm, "scale", 5) == 0 && (k == 1 || k == 2)) t = sv_make(a[0], 0, 0, k == 2 ? a[1] : a[0], 0, 0);
        else if (css && nl == 6 && memcmp(nm, "scaleX", 6) == 0 && k == 1) t = sv_make(a[0], 0, 0, 1, 0, 0);
        else if (css && nl == 6 && memcmp(nm, "scaleY", 6) == 0 && k == 1) t = sv_make(1, 0, 0, a[0], 0, 0);
        else if (nl == 6 && memcmp(nm, "rotate", 6) == 0 && (k == 1 || (!css && k == 3))) {
            double r = a[0] * M_PI / 180, cr = cos(r), sr = sin(r);
            t = sv_make(cr, sr, -sr, cr, 0, 0);
            if (k == 3) t = sv_mul(sv_make(1, 0, 0, 1, a[1], a[2]), sv_mul(t, sv_make(1, 0, 0, 1, -a[1], -a[2])));
        } else if (nl == 5 && memcmp(nm, "skewX", 5) == 0 && k == 1) t = sv_make(1, 0, tan(a[0] * M_PI / 180), 1, 0, 0);
        else if (nl == 5 && memcmp(nm, "skewY", 5) == 0 && k == 1) t = sv_make(1, tan(a[0] * M_PI / 180), 0, 1, 0, 0);
        else if (css && nl == 4 && memcmp(nm, "skew", 4) == 0 && (k == 1 || k == 2))
            t = sv_make(1, k == 2 ? tan(a[1] * M_PI / 180) : 0, tan(a[0] * M_PI / 180), 1, 0, 0);
        else return 0;
        m = sv_mul(m, t);
        any = 1;
    }
    *out = m;
    return any || n == 0 || sv_skip_ws(s, n, 0) == n;
}
/* an element's transform: a declared CSS transform (about its transform-origin, 0 0 unless
   declared, as Chromium's sheet has it for SVG elements), else its transform attribute */
static sv_mat sv_transform_of(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, uint32_t x, const sv_walk *w) {
    sv_mat m = sv_id();
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && st->cval[c0 + CSS_PROP_TRANSFORM].how == CSS_HOW_DECLARED && st->cval[c0 + CSS_PROP_TRANSFORM].winner) {
        if (st->cval[c0 + CSS_PROP_TRANSFORM].unit != CSS_UNIT_TEXT) return m;
        amb_span sp = st->cval[c0 + CSS_PROP_TRANSFORM].text;
        vector<char> t;
        append_range(t, text_ptr(st, sp), sp.len);
        if (!sv_transform_list(data(t), len(t), 1, &m)) return sv_id();
        uint32_t oc = c0 + CSS_PROP_TRANSFORM_ORIGIN;
        if (st->cval[oc].how == CSS_HOW_DECLARED && st->cval[oc].unit == CSS_UNIT_TEXT) {
            amb_span op = st->cval[oc].text;
            vector<char> o;
            append_range(o, text_ptr(st, op), op.len);
            double ox = 0, oy = 0;
            size_t i = sv_skip_ws(data(o), len(o), 0);
            int ok;
            size_t e = i;
            while (e < len(o) && !sv_ws(at(o, e))) e++;
            ox = sv_length(data(o) + i, e - i, w->vw, 16, &ok);
            i = sv_skip_ws(data(o), len(o), e);
            e = i;
            while (e < len(o) && !sv_ws(at(o, e))) e++;
            if (e > i) oy = sv_length(data(o) + i, e - i, w->vh, 16, &ok);
            m = sv_mul(sv_make(1, 0, 0, 1, ox, oy), sv_mul(m, sv_make(1, 0, 0, 1, -ox, -oy)));
        }
        return m;
    }
    vector<char> v;
    if (sv_attr_text(d, x, "transform", &v) && !sv_transform_list(data(v), len(v), 0, &m)) return sv_id();
    return m;
}

/* preserveAspectRatio (SVG 2, 8.8) mapping the box vb (x, y, w, h) into the viewport vp: x's
   attribute (none, x 0: its initial xMidYMid meet) */
static sv_mat sv_viewbox(const dom_document *d, uint32_t x, const double *vb, const double *vp) {
    vector<char> v;
    int ax = 1, ay = 1, slice = 0, none = 0;   /* 0 min, 1 mid, 2 max */
    if (x && sv_attr_text(d, x, "preserveAspectRatio", &v)) {
        size_t i = sv_skip_ws(data(v), len(v), 0), n = len(v);
        if (i + 5 <= n && memcmp(data(v) + i, "defer", 5) == 0) i = sv_skip_ws(data(v), n, i + 5);
        if (i + 4 <= n && memcmp(data(v) + i, "none", 4) == 0) { none = 1; i += 4; }
        else if (i + 8 <= n && data(v)[i] == 'x' && data(v)[i + 4] == 'Y') {
            const char *q = data(v) + i;
            ax = memcmp(q + 1, "Min", 3) == 0 ? 0 : memcmp(q + 1, "Mid", 3) == 0 ? 1 : 2;
            ay = memcmp(q + 5, "Min", 3) == 0 ? 0 : memcmp(q + 5, "Mid", 3) == 0 ? 1 : 2;
            i += 8;
        }
        i = sv_skip_ws(data(v), n, i);
        if (i + 5 <= n && memcmp(data(v) + i, "slice", 5) == 0) slice = 1;
    }
    double sx = vp[2] / vb[2], sy = vp[3] / vb[3];
    if (!none) {
        double s = slice ? fmax(sx, sy) : fmin(sx, sy);
        sx = s;
        sy = s;
    }
    double tx = vp[0] - vb[0] * sx, ty = vp[1] - vb[1] * sy;
    if (!none) {
        double rw = vp[2] - vb[2] * sx, rh = vp[3] - vb[3] * sy;
        tx += ax == 1 ? rw / 2 : ax == 2 ? rw : 0;
        ty += ay == 1 ? rh / 2 : ay == 2 ? rh : 0;
    }
    return sv_make(sx, 0, 0, sy, tx, ty);
}
/* a viewBox: 1 and its four numbers (a width and height above 0) */
static int sv_viewbox_of(const dom_document *d, uint32_t x, double *vb) {
    vector<char> v;
    if (!sv_attr_text(d, x, "viewBox", &v)) return 0;
    size_t i = sv_skip_ws(data(v), len(v), 0);
    for (int k = 0; k < 4; k++) {
        if (k) i = sv_skip_sep(data(v), len(v), i);
        if (!sv_number(data(v), len(v), &i, &vb[k])) return 0;
    }
    return vb[2] > 0 && vb[3] > 0;
}

/* ---- shapes ---- */

/* a cubic's coordinate's extremes in (0, 1), its bounds widened to them */
static void sv_cubic_bounds(double p0, double p1, double p2, double p3, double *lo, double *hi) {
    double a = -p0 + 3 * p1 - 3 * p2 + p3, b = 2 * (p0 - 2 * p1 + p2), c = p1 - p0;
    double ts[2];
    int nt = 0;
    if (fabs(a) < 1e-12) { if (fabs(b) > 1e-12) ts[nt++] = -c / b; }
    else {
        double disc = b * b - 4 * a * c;
        if (disc >= 0) { double q = sqrt(disc); ts[nt++] = (-b + q) / (2 * a); ts[nt++] = (-b - q) / (2 * a); }
    }
    for (int k = 0; k < nt; k++) {
        double t = ts[k];
        if (t <= 0 || t >= 1) continue;
        double u = 1 - t, v = u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
        if (v < *lo) *lo = v;
        if (v > *hi) *hi = v;
    }
}
/* a path's tight bounds (x0, y0, x1, y1) under m: 0 when it has no point */
static int sv_path_bounds(const vector<double> *p, sv_mat m, double *bb) {
    size_t i = 0, n = len(*p);
    double cx = 0, cy = 0, sx = 0, sy = 0;
    int any = 0;
    bb[0] = bb[1] = 1e300;
    bb[2] = bb[3] = -1e300;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_Z) { cx = sx; cy = sy; i++; continue; }
        double pts[8];
        int np = 0;
        if (c == SV_M || c == SV_L) {
            double x = at(*p, i + 1), y = at(*p, i + 2);
            pts[0] = sv_ax(m, x, y);
            pts[1] = sv_ay(m, x, y);
            np = 1;
            if (c == SV_M) { sx = x; sy = y; }
            cx = x; cy = y;
            i += 3;
        } else {
            double q[8];
            sv_curve_at(p, i, cx, cy, q);
            for (int k = 0; k < 4; k++) { pts[2 * k] = sv_ax(m, q[2 * k], q[2 * k + 1]); pts[2 * k + 1] = sv_ay(m, q[2 * k], q[2 * k + 1]); }
            double lox = fmin(pts[0], pts[6]), hix = fmax(pts[0], pts[6]), loy = fmin(pts[1], pts[7]), hiy = fmax(pts[1], pts[7]);
            sv_cubic_bounds(pts[0], pts[2], pts[4], pts[6], &lox, &hix);
            sv_cubic_bounds(pts[1], pts[3], pts[5], pts[7], &loy, &hiy);
            if (lox < bb[0]) bb[0] = lox;
            if (hix > bb[2]) bb[2] = hix;
            if (loy < bb[1]) bb[1] = loy;
            if (hiy > bb[3]) bb[3] = hiy;
            any = 1;
            cx = q[6]; cy = q[7];
            i += (size_t)sv_args(c) + 1;
            continue;
        }
        for (int k = 0; k < np; k++) {
            if (pts[2 * k] < bb[0]) bb[0] = pts[2 * k];
            if (pts[2 * k] > bb[2]) bb[2] = pts[2 * k];
            if (pts[2 * k + 1] < bb[1]) bb[1] = pts[2 * k + 1];
            if (pts[2 * k + 1] > bb[3]) bb[3] = pts[2 * k + 1];
            any = 1;
        }
    }
    return any;
}

/* a quarter ellipse's cubic from (x0, y0) to (x1, y1), its corner at (kx, ky) */
static void sv_corner(vector<double> *p, double x0, double y0, double kx, double ky, double x1, double y1) {
    const double k = 0.55228474983079;
    sv_put_c(p, SV_K, x0 + (kx - x0) * k, y0 + (ky - y0) * k, x1 + (kx - x1) * k, y1 + (ky - y1) * k, x1, y1);
}
/* an ellipse as Skia adds an oval: from its right, clockwise */
static void sv_ellipse_path(vector<double> *p, double cx, double cy, double rx, double ry) {
    sv_put2(p, SV_M, cx + rx, cy);
    sv_corner(p, cx + rx, cy, cx + rx, cy + ry, cx, cy + ry);
    sv_corner(p, cx, cy + ry, cx - rx, cy + ry, cx - rx, cy);
    sv_corner(p, cx - rx, cy, cx - rx, cy - ry, cx, cy - ry);
    sv_corner(p, cx, cy - ry, cx + rx, cy - ry, cx + rx, cy);
    push_back(p, (double)SV_Z);
}
/* a list of points (polyline, polygon): pairs of numbers, read up to an error; an odd
   number's last dropped */
static void sv_points(const char *s, size_t n, int close, vector<double> *p) {
    size_t i = sv_skip_ws(s, n, 0);
    int k = 0;
    while (i < n) {
        double x, y;
        if (!sv_number(s, n, &i, &x)) break;
        i = sv_skip_sep(s, n, i);
        if (!sv_number(s, n, &i, &y)) break;
        i = sv_skip_sep(s, n, i);
        sv_put2(p, k ? SV_L : SV_M, x, y);
        k++;
    }
    if (close && k) push_back(p, (double)SV_Z);
}

/* a geometry property of x (SVG 2, 9.2-9.6: cx, cy, r, rx, ry, x, y, width, height): a rule's
   declaration of it (the cascade's computed value; a presentation attribute is not read as
   one), else its attribute; in user units, a percentage of basis. *given is 0 when neither
   gives one (an auto, from a rule, gives dflt) */
static double sv_geo(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, uint32_t x, int prop, const char *name,
                     double basis, double fs, double dflt, int *given) {
    uint32_t c0 = sv_first(firsts, x);
    if (c0) {
        uint32_t cv = c0 + (uint32_t)prop;
        css_how how = st->cval[cv].how;
        if (st->cval[cv].winner && (how == CSS_HOW_DECLARED || how == CSS_HOW_ANIMATED || how == CSS_HOW_INITIAL)) {
            css_unit u = st->cval[cv].unit;
            *given = !(u == CSS_UNIT_AUTO || u == CSS_UNIT_KEYWORD);
            return *given ? sv_css_len(st, cv, basis) : dflt;
        }
    }
    return sv_len_attr(d, x, name, basis, fs, dflt, given);
}
/* a path's data: the string of its d property's path() when a rule declares it (none: 0), else
   its d attribute's; 0 when it has none */
static int sv_path_text(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, uint32_t x, vector<char> *v) {
    uint32_t c0 = sv_first(firsts, x);
    clear(*v);
    if (c0) {
        uint32_t cv = c0 + CSS_PROP_D;
        css_how how = st->cval[cv].how;
        if (st->cval[cv].winner && (how == CSS_HOW_DECLARED || how == CSS_HOW_ANIMATED || how == CSS_HOW_INITIAL)) {
            if (st->cval[cv].unit != CSS_UNIT_TEXT) return 0;
            amb_span sp = st->cval[cv].text;
            const char *t = text_ptr(st, sp);
            size_t a = 0, b = sp.len;
            while (a < b && t[a] != '"' && t[a] != '\'') a++;
            while (b > a + 1 && t[b - 1] != t[a]) b--;
            if (b <= a + 1) return 0;
            append_range(v, t + a + 1, b - a - 2);
            return 1;
        }
    }
    return sv_attr_text(d, x, "d", v);
}

/* a shape's geometry as a path (SVG 2, 10's equivalent paths, as Blink makes them): 1, or 0
   when it draws nothing (a width, height or radius not above 0) */
static int sv_geometry(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, uint32_t x, const sv_walk *w,
                       vector<double> *p) {
    double fs = sv_fs(st, w), dg = sv_diag(w);
    int g1, g2;
    vector<char> v;
    if (sv_is(d, x, "rect")) {
        double rx0 = sv_geo(d, st, firsts, x, CSS_PROP_X, "x", w->vw, fs, 0, &g1), ry0 = sv_geo(d, st, firsts, x, CSS_PROP_Y, "y", w->vh, fs, 0, &g1);
        double rw = sv_geo(d, st, firsts, x, CSS_PROP_WIDTH, "width", w->vw, fs, 0, &g1);
        double rh = sv_geo(d, st, firsts, x, CSS_PROP_HEIGHT, "height", w->vh, fs, 0, &g2);
        if (rw <= 0 || rh <= 0) return 0;
        double rx = sv_geo(d, st, firsts, x, CSS_PROP_RX, "rx", w->vw, fs, 0, &g1), ry = sv_geo(d, st, firsts, x, CSS_PROP_RY, "ry", w->vh, fs, 0, &g2);
        if (g1 && rx < 0) g1 = 0;
        if (g2 && ry < 0) g2 = 0;
        if (g1 && !g2) ry = rx;
        else if (g2 && !g1) rx = ry;
        else if (!g1 && !g2) rx = ry = 0;
        if (rx > rw / 2) rx = rw / 2;
        if (ry > rh / 2) ry = rh / 2;
        if (rx <= 0 || ry <= 0) {
            sv_put2(p, SV_M, rx0, ry0);
            sv_put2(p, SV_L, rx0 + rw, ry0);
            sv_put2(p, SV_L, rx0 + rw, ry0 + rh);
            sv_put2(p, SV_L, rx0, ry0 + rh);
            push_back(p, (double)SV_Z);
            return 1;
        }
        /* as Skia adds a round rect: from its left side's lower end, clockwise */
        double x1 = rx0 + rw, y1 = ry0 + rh;
        sv_put2(p, SV_M, rx0, y1 - ry);
        sv_put2(p, SV_L, rx0, ry0 + ry);
        sv_corner(p, rx0, ry0 + ry, rx0, ry0, rx0 + rx, ry0);
        sv_put2(p, SV_L, x1 - rx, ry0);
        sv_corner(p, x1 - rx, ry0, x1, ry0, x1, ry0 + ry);
        sv_put2(p, SV_L, x1, y1 - ry);
        sv_corner(p, x1, y1 - ry, x1, y1, x1 - rx, y1);
        sv_put2(p, SV_L, rx0 + rx, y1);
        sv_corner(p, rx0 + rx, y1, rx0, y1, rx0, y1 - ry);
        push_back(p, (double)SV_Z);
        return 1;
    }
    if (sv_is(d, x, "circle")) {
        double cx = sv_geo(d, st, firsts, x, CSS_PROP_CX, "cx", w->vw, fs, 0, &g1), cy = sv_geo(d, st, firsts, x, CSS_PROP_CY, "cy", w->vh, fs, 0, &g1);
        double r = sv_geo(d, st, firsts, x, CSS_PROP_R, "r", dg, fs, 0, &g1);
        if (r <= 0) return 0;
        sv_ellipse_path(p, cx, cy, r, r);
        return 1;
    }
    if (sv_is(d, x, "ellipse")) {
        double cx = sv_geo(d, st, firsts, x, CSS_PROP_CX, "cx", w->vw, fs, 0, &g1), cy = sv_geo(d, st, firsts, x, CSS_PROP_CY, "cy", w->vh, fs, 0, &g1);
        double rx = sv_geo(d, st, firsts, x, CSS_PROP_RX, "rx", w->vw, fs, 0, &g1), ry = sv_geo(d, st, firsts, x, CSS_PROP_RY, "ry", w->vh, fs, 0, &g2);
        if (g1 && !g2) ry = rx;
        else if (g2 && !g1) rx = ry;
        if (rx <= 0 || ry <= 0) return 0;
        sv_ellipse_path(p, cx, cy, rx, ry);
        return 1;
    }
    if (sv_is(d, x, "line")) {
        double x1 = sv_len_attr(d, x, "x1", w->vw, fs, 0, &g1), y1 = sv_len_attr(d, x, "y1", w->vh, fs, 0, &g1);
        double x2 = sv_len_attr(d, x, "x2", w->vw, fs, 0, &g1), y2 = sv_len_attr(d, x, "y2", w->vh, fs, 0, &g1);
        sv_put2(p, SV_M, x1, y1);
        sv_put2(p, SV_L, x2, y2);
        return 1;
    }
    if (sv_is(d, x, "polyline") || sv_is(d, x, "polygon")) {
        if (!sv_attr_text(d, x, "points", &v)) return 0;
        sv_points(data(v), len(v), sv_is(d, x, "polygon"), p);
        return len(*p) > 0;
    }
    if (sv_is(d, x, "path")) {
        if (!sv_path_text(d, st, firsts, x, &v)) return 0;
        amb_svg_path(data(v), len(v), p);
        return len(*p) > 0;
    }
    return 0;
}

/* ---- paints ---- */

/* a paint (fill's or stroke's): none, a colour, or a gradient (its element) with a
   fallback (none or a colour, fb_kind 0 none) */
enum { SV_PAINT_NONE, SV_PAINT_COLOR, SV_PAINT_URL };
typedef struct sv_paint { int kind; uint32_t rgba, ref; int fb_kind; uint32_t fb; } sv_paint;

/* s[0, n) is the word w, its case aside */
static int sv_eq_ci(const char *s, size_t n, const char *w) {
    if (strlen(w) != n) return 0;
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = w[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return 0;
    }
    return 1;
}
/* a colour CSS writes, or currentcolor's (cur) */
static int sv_colour(const char *s, size_t n, uint32_t cur, uint32_t *rgba) {
    size_t a = sv_skip_ws(s, n, 0), b = n;
    while (b > a && sv_ws(s[b - 1])) b--;
    if (sv_eq_ci(s + a, b - a, "currentcolor")) { *rgba = cur; return 1; }
    return css_color_parse(s + a, b - a, rgba);
}
/* the paint the value cv gives the element x */
static void sv_paint_of(const dom_document *d, const css_style *st, uint32_t x, uint32_t cv, uint32_t cur, sv_paint *p) {
    memset(p, 0, sizeof *p);
    css_unit u = st->cval[cv].unit;
    if (u == CSS_UNIT_KEYWORD) {
        if (sv_word_is(st, cv, "currentcolor")) { p->kind = SV_PAINT_COLOR; p->rgba = cur; }
        return;
    }
    if (u == CSS_UNIT_COLOR) { p->kind = SV_PAINT_COLOR; p->rgba = st->cval[cv].color; return; }
    if (u != CSS_UNIT_TEXT) return;
    amb_span sp = st->cval[cv].text;
    vector<char> t;
    append_range(t, text_ptr(st, sp), sp.len);
    size_t end = 0;
    uint32_t ref = sv_url(d, x, data(t), len(t), &end);
    if (end > 0 && len(t) >= 4 && memcmp(data(t) + sv_skip_ws(data(t), len(t), 0), "url(", 4) == 0) {
        p->kind = SV_PAINT_URL;
        p->ref = ref;
        size_t i = sv_skip_ws(data(t), len(t), end);
        if (i < len(t)) {
            if (len(t) - i >= 4 && memcmp(data(t) + i, "none", 4) == 0) p->fb_kind = SV_PAINT_NONE;
            else if (sv_colour(data(t) + i, len(t) - i, cur, &p->fb)) p->fb_kind = SV_PAINT_COLOR;
        }
        return;
    }
    if (sv_colour(data(t), len(t), cur, &p->rgba)) p->kind = SV_PAINT_COLOR;
}

/* a gradient element and those its href names, at most 16 deep without a cycle: chain[0..n) */
static int sv_chain(const dom_document *d, uint32_t g, uint32_t *chain) {
    int n = 0;
    while (g && n < 16) {
        if (!sv_is(d, g, "linearGradient") && !sv_is(d, g, "radialGradient")) break;
        for (int k = 0; k < n; k++) if (chain[k] == g) return n;
        chain[n++] = g;
        g = sv_href(d, g);
    }
    return n;
}
static uint32_t sv_chain_attr(const dom_document *d, const uint32_t *chain, int n, const char *name) {
    for (int k = 0; k < n; k++) if (sv_attr(d, chain[k], name)) return chain[k];
    return 0;
}
/* a gradient's coordinate: in its bounding box a number (a percentage a fraction), else a
   length of the viewport */
static double sv_gcoord(const dom_document *d, const uint32_t *chain, int n, const char *name, int bbox, double basis, double fs,
                        double dflt_frac) {
    uint32_t g = sv_chain_attr(d, chain, n, name);
    int given;
    if (!g) return bbox ? dflt_frac : dflt_frac * basis;
    double v = sv_len_attr(d, g, name, bbox ? 1 : basis, fs, bbox ? dflt_frac : dflt_frac * basis, &given);
    return v;
}

static void sv_node(SB, uint32_t x, const sv_walk *pw, sv_mat m, int depth);

/* the walk an element that is no child of the walk's begins (a pattern's content, a marker's,
   a mask's, a clipPath's): its own computed values, which inherit from its ancestors in the
   document, in a viewport of vw x vh; 0 when it has no style */
static int sv_root_walk(const css_style *st, const vector<uint32_t> *firsts, uint32_t x, double vw, double vh, sv_walk *w) {
    memset(w, 0, sizeof *w);
    uint32_t c0 = sv_first(firsts, x);
    if (!c0) return 0;
    for (int k = 0; k < SI_N; k++) w->src[k] = c0 + (uint32_t)sv_iprops[k];
    w->vw = vw;
    w->vh = vh;
    return 1;
}

/* an element named name and those of that name its href names, at most 16 deep without a
   cycle: chain[0..n) */
static int sv_named_chain(const dom_document *d, uint32_t g, const char *name, uint32_t *chain) {
    int n = 0;
    while (g && n < 16 && sv_is(d, g, name)) {
        for (int k = 0; k < n; k++) if (chain[k] == g) return n;
        chain[n++] = g;
        g = sv_href(d, g);
    }
    return n;
}
/* an attribute is the word w */
static int sv_attr_is(const dom_document *d, uint32_t x, const char *name, const char *w) {
    vector<char> v;
    return x && sv_attr_text(d, x, name, &v) && len(v) == strlen(w) && memcmp(data(v), w, len(v)) == 0;
}

/* a pattern (SVG 2, 14.3) as a paint for a shape whose bounds are bb, as Blink makes one: its
   attributes and its content (the first in its href chain with an element child) each from
   the first of the chain that has them; its tile (x, y, width, height, of the bounding box
   unless patternUnits is userSpaceOnUse) in the space patternTransform makes; its content
   in the tile's space, which its viewBox (and preserveAspectRatio) maps into the tile, else
   the bounding box's when patternContentUnits is objectBoundingBox, drawn with the values it
   inherits in the document (not the shape's). Its block written into the scene, its number
   in *gid: 2; or 0 when it draws nothing (no content, a tile or a box of no area, an empty
   viewBox, a pattern already being drawn) */
static int sv_pattern(SB, uint32_t pat, const double *bb, const sv_walk *w, uint32_t *gid) {
    uint32_t chain[16];
    int n = sv_named_chain(d, pat, "pattern", chain);
    if (!n || len(*uses) >= 16) return 0;
    for (size_t k = 0; k < len(*uses); k++) if (at(*uses, k) == chain[0]) return 0;
    double fs = sv_fs(st, w), bw = bb[2] - bb[0], bh = bb[3] - bb[1];
    int ubox = !sv_attr_is(d, sv_chain_attr(d, chain, n, "patternUnits"), "patternUnits", "userSpaceOnUse");
    int cbox = sv_attr_is(d, sv_chain_attr(d, chain, n, "patternContentUnits"), "patternContentUnits", "objectBoundingBox");
    if (ubox && (bw <= 0 || bh <= 0)) return 0;
    double tx = sv_gcoord(d, chain, n, "x", ubox, w->vw, fs, 0), ty = sv_gcoord(d, chain, n, "y", ubox, w->vh, fs, 0);
    double tw = sv_gcoord(d, chain, n, "width", ubox, w->vw, fs, 0), th = sv_gcoord(d, chain, n, "height", ubox, w->vh, fs, 0);
    if (ubox) {
        tx = bb[0] + tx * bw;
        ty = bb[1] + ty * bh;
        tw *= bw;
        th *= bh;
    }
    if (!(tw > 0 && th > 0)) return 0;
    sv_mat tt = sv_id();
    uint32_t vbel = sv_chain_attr(d, chain, n, "viewBox");
    if (vbel) {
        double vb[4], vp[4] = { 0, 0, tw, th };
        if (!sv_viewbox_of(d, vbel, vb)) return 0;
        tt = sv_viewbox(d, sv_chain_attr(d, chain, n, "preserveAspectRatio"), vb, vp);
    } else if (cbox) tt = sv_make(bw, 0, 0, bh, 0, 0);
    sv_mat pm = sv_id();
    vector<char> v;
    uint32_t tg = sv_chain_attr(d, chain, n, "patternTransform");
    if (tg && sv_attr_text(d, tg, "patternTransform", &v) && !sv_transform_list(data(v), len(v), 0, &pm)) pm = sv_id();
    pm = sv_mul(pm, sv_make(1, 0, 0, 1, tx, ty));
    uint32_t holder = 0;
    for (int k = 0; k < n && !holder; k++)
        for (uint32_t c = d->node[chain[k]].first_child; c && !holder; c = d->node[c].next_sibling)
            if (d->node[c].kind == DOM_NODE_KIND_ELEMENT) holder = chain[k];
    if (!holder) return 0;
    sv_walk root;
    if (!sv_root_walk(st, firsts, holder, w->vw, w->vh, &root)) return 0;
    vector<char> body;
    push_back(uses, chain[0]);
    for (uint32_t c = d->node[holder].first_child; c; c = d->node[c].next_sibling) sv_node(d, st, firsts, f, &body, ids, uses, work, c, &root, tt, 1);
    resize(uses, len(*uses) - 1);
    uint32_t id = ++*ids;
    sv_w(out, "P");
    sv_n(out, id);
    sv_m(out, pm);
    sv_n(out, tw);
    sv_n(out, th);
    sv_w(out, "\n");
    append_range(out, data(body), len(body));
    sv_w(out, "E\n");
    *gid = id;
    return 2;
}

/* the paint p resolved for a shape whose bounds (in its user space) are bb: a gradient's
   line or a pattern's block written into the scene (its number in *gid), a colour (*rgba),
   or none: 2, 1, 0 */
static int sv_resolve(SB, const sv_paint *p, const double *bb, const sv_walk *w, uint32_t *gid, uint32_t *rgba) {
    if (p->kind == SV_PAINT_NONE) return 0;
    if (p->kind == SV_PAINT_COLOR) { *rgba = p->rgba; return 1; }
    if (p->ref && sv_is(d, p->ref, "pattern")) {
        int k = sv_pattern(SA, p->ref, bb, w, gid);
        if (k) return k;
        if (p->fb_kind == SV_PAINT_COLOR) { *rgba = p->fb; return 1; }
        return 0;
    }
    uint32_t chain[16];
    int n = sv_chain(d, p->ref, chain);
    if (n == 0) {
        if (p->fb_kind == SV_PAINT_COLOR) { *rgba = p->fb; return 1; }
        return 0;
    }
    int radial = sv_is(d, chain[0], "radialGradient");
    double fs = sv_fs(st, w), dg = sv_diag(w);
    vector<char> v;
    uint32_t ug = sv_chain_attr(d, chain, n, "gradientUnits");
    int bbox = 1;
    if (ug && sv_attr_text(d, ug, "gradientUnits", &v) && len(v) == 14 && memcmp(data(v), "userSpaceOnUse", 14) == 0) bbox = 0;
    sv_mat gt = sv_id();
    uint32_t tg = sv_chain_attr(d, chain, n, "gradientTransform");
    if (tg && sv_attr_text(d, tg, "gradientTransform", &v) && !sv_transform_list(data(v), len(v), 0, &gt)) gt = sv_id();
    int spread = 0;
    uint32_t sg = sv_chain_attr(d, chain, n, "spreadMethod");
    if (sg && sv_attr_text(d, sg, "spreadMethod", &v)) {
        if (len(v) == 7 && memcmp(data(v), "reflect", 7) == 0) spread = 1;
        else if (len(v) == 6 && memcmp(data(v), "repeat", 6) == 0) spread = 2;
    }
    /* the stops: those of the first in the chain that has any */
    uint32_t holder = 0;
    for (int k = 0; k < n && !holder; k++)
        for (uint32_t c = d->node[chain[k]].first_child; c && !holder; c = d->node[c].next_sibling) if (sv_is(d, c, "stop")) holder = chain[k];
    if (!holder) return 0;
    vector<double> stops;
    double last = 0;
    for (uint32_t c = d->node[holder].first_child; c; c = d->node[c].next_sibling) {
        if (!sv_is(d, c, "stop")) continue;
        double off = 0;
        if (sv_attr_text(d, c, "offset", &v)) {
            size_t i = sv_skip_ws(data(v), len(v), 0);
            if (sv_number(data(v), len(v), &i, &off) && i < len(v) && at(v, i) == '%') off /= 100;
        }
        off = off < 0 ? 0 : off > 1 ? 1 : off;
        if (off < last) off = last;
        last = off;
        uint32_t c0 = sv_first(firsts, c);
        uint32_t col = c0 ? st->cval[c0 + CSS_PROP_STOP_COLOR].color : 0x000000ff;
        double op = c0 ? st->cval[c0 + CSS_PROP_STOP_OPACITY].num : 1;
        uint32_t a = (uint32_t)floor((col & 0xff) * (op < 0 ? 0 : op > 1 ? 1 : op) + 0.5);
        push_back(stops, off);
        push_back(stops, (double)((col & 0xffffff00u) | a));
    }
    size_t ns = len(stops) / 2;
    if (ns == 1) { *rgba = (uint32_t)at(stops, 1); return 1; }
    sv_mat gm = gt;
    if (bbox) {
        double bw = bb[2] - bb[0], bh = bb[3] - bb[1];
        if (bw <= 0 || bh <= 0) return 0;
        gm = sv_mul(sv_make(bw, 0, 0, bh, bb[0], bb[1]), gt);
    }
    uint32_t id = ++*ids;
    sv_w(out, radial ? "r" : "l");
    sv_n(out, id);
    sv_m(out, gm);
    if (radial) {
        double cx = sv_gcoord(d, chain, n, "cx", bbox, w->vw, fs, 0.5), cy = sv_gcoord(d, chain, n, "cy", bbox, w->vh, fs, 0.5);
        double r = sv_gcoord(d, chain, n, "r", bbox, dg, fs, 0.5);
        double fx = sv_chain_attr(d, chain, n, "fx") ? sv_gcoord(d, chain, n, "fx", bbox, w->vw, fs, 0.5) : cx;
        double fy = sv_chain_attr(d, chain, n, "fy") ? sv_gcoord(d, chain, n, "fy", bbox, w->vh, fs, 0.5) : cy;
        double fr = sv_gcoord(d, chain, n, "fr", bbox, dg, fs, 0);
        if (r < 0 || fr < 0) return 0;
        sv_n(out, cx); sv_n(out, cy); sv_n(out, r); sv_n(out, fx); sv_n(out, fy); sv_n(out, fr);
    } else {
        sv_n(out, sv_gcoord(d, chain, n, "x1", bbox, w->vw, fs, 0));
        sv_n(out, sv_gcoord(d, chain, n, "y1", bbox, w->vh, fs, 0));
        sv_n(out, sv_gcoord(d, chain, n, "x2", bbox, w->vw, fs, 1));
        sv_n(out, sv_gcoord(d, chain, n, "y2", bbox, w->vh, fs, 0));
    }
    sv_n(out, spread);
    sv_n(out, (double)ns);
    for (size_t k = 0; k < ns; k++) { sv_n(out, at(stops, 2 * k)); sv_hex(out, (uint32_t)at(stops, 2 * k + 1)); }
    sv_w(out, "\n");
    *gid = id;
    return 2;
}
/* a resolved paint's words in a path's line */
static void sv_paint_words(vector<char> *line, int kind, uint32_t gid, uint32_t rgba) {
    if (kind == 2) { sv_w(line, " u"); sv_n(line, gid); }
    else if (kind == 1) { sv_w(line, " c"); sv_hex(line, rgba); }
    else sv_w(line, " n");
}

/* ---- elements ---- */

static void sv_node(SB, uint32_t x, const sv_walk *pw, sv_mat m, int depth);
static int sv_text_bounds(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t x, const sv_walk *w,
                          sv_mat mm, double *bb);

/* the bounds an element's content covers under m (with its own transform when withself is 1, as
   a child's are; without, its user space's: an objectBoundingBox's): a shape's geometry's, a
   text's glyph cells', an image's viewport, a group's or a use's children's together; 0 when
   none */
static int sv_bounds(SB, uint32_t x, const sv_walk *pw, sv_mat m, double *bb, int depth, int withself) {
    if (depth > 32 || ++*work > SV_WORK || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_SVG) return 0;
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none")) return 0;
    sv_walk w;
    sv_inherit(st, firsts, x, pw, &w);
    sv_mat mm = withself ? sv_mul(m, sv_transform_of(d, st, firsts, x, &w)) : m;
    vector<double> p;
    if (sv_geometry(d, st, firsts, x, &w, &p)) return sv_path_bounds(&p, mm, bb);
    if (sv_is(d, x, "text")) return sv_text_bounds(d, st, firsts, f, x, &w, mm, bb);
    if (sv_is(d, x, "image")) {
        /* an image's viewport, when its width and height are given (an auto one's picture is
           the loader's, unknown here) */
        double fs = sv_fs(st, &w);
        int g, gw, gh;
        double ix = sv_geo(d, st, firsts, x, CSS_PROP_X, "x", w.vw, fs, 0, &g), iy = sv_geo(d, st, firsts, x, CSS_PROP_Y, "y", w.vh, fs, 0, &g);
        double iw = sv_geo(d, st, firsts, x, CSS_PROP_WIDTH, "width", w.vw, fs, 0, &gw), ih = sv_geo(d, st, firsts, x, CSS_PROP_HEIGHT, "height", w.vh, fs, 0, &gh);
        if (!gw || !gh || iw <= 0 || ih <= 0) return 0;
        sv_put2(&p, SV_M, ix, iy);
        sv_put2(&p, SV_L, ix + iw, iy + ih);
        return sv_path_bounds(&p, mm, bb);
    }
    int any = 0;
    if (sv_is(d, x, "g") || sv_is(d, x, "a") || sv_is(d, x, "switch") || sv_is(d, x, "use")) {
        bb[0] = bb[1] = 1e300;
        bb[2] = bb[3] = -1e300;
        uint32_t ref = sv_is(d, x, "use") ? sv_href(d, x) : 0;
        if (sv_is(d, x, "use")) {
            int g;
            double ux = sv_len_attr(d, x, "x", w.vw, sv_fs(st, &w), 0, &g), uy = sv_len_attr(d, x, "y", w.vh, sv_fs(st, &w), 0, &g);
            mm = sv_mul(mm, sv_make(1, 0, 0, 1, ux, uy));
        }
        for (uint32_t c = ref ? ref : d->node[x].first_child; c; c = ref ? 0 : d->node[c].next_sibling) {
            double cb[4];
            if (!sv_bounds(SA, c, &w, mm, cb, depth + 1, 1)) continue;
            bb[0] = fmin(bb[0], cb[0]);
            bb[1] = fmin(bb[1], cb[1]);
            bb[2] = fmax(bb[2], cb[2]);
            bb[3] = fmax(bb[3], cb[3]);
            any = 1;
        }
    }
    return any;
}

/* a clipPath's shapes, each a line of its own into lines under m (the referencing element's
   user space, in a viewport vw x vh; bb its bounds there, for objectBoundingBox): their
   count */
static int sv_clip_shapes(SB, uint32_t cp, const double *bb, int hasbb, sv_mat m, double vw, double vh, vector<char> *lines) {
    sv_walk root;
    memset(&root, 0, sizeof root);
    uint32_t c0 = sv_first(firsts, cp);
    if (!c0) return 0;
    for (int k = 0; k < SI_N; k++) root.src[k] = c0 + (uint32_t)sv_iprops[k];
    root.vw = vw;
    root.vh = vh;
    sv_mat cm = sv_mul(m, sv_transform_of(d, st, firsts, cp, &root));
    vector<char> v;
    if (sv_attr_text(d, cp, "clipPathUnits", &v) && len(v) == 17 && memcmp(data(v), "objectBoundingBox", 17) == 0) {
        if (!hasbb || bb[2] - bb[0] <= 0 || bb[3] - bb[1] <= 0) return 0;
        cm = sv_mul(cm, sv_make(bb[2] - bb[0], 0, 0, bb[3] - bb[1], bb[0], bb[1]));
    }
    int count = 0;
    for (uint32_t c = d->node[cp].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || d->node[c].ns != DOM_NAMESPACE_SVG) continue;
        uint32_t s0 = sv_first(firsts, c);
        if (s0 && sv_word_is(st, s0 + CSS_PROP_DISPLAY, "none")) continue;
        sv_walk w;
        sv_inherit(st, firsts, c, &root, &w);
        sv_mat sm = sv_mul(cm, sv_transform_of(d, st, firsts, c, &w));
        uint32_t shape = c;
        if (sv_is(d, c, "use")) {
            shape = sv_href(d, c);
            if (!shape) continue;
            int g;
            double ux = sv_len_attr(d, c, "x", w.vw, sv_fs(st, &w), 0, &g), uy = sv_len_attr(d, c, "y", w.vh, sv_fs(st, &w), 0, &g);
            sm = sv_mul(sm, sv_make(1, 0, 0, 1, ux, uy));
            sv_walk uw = w;
            sv_inherit(st, firsts, shape, &uw, &w);
            sm = sv_mul(sm, sv_transform_of(d, st, firsts, shape, &w));
        }
        if (sv_word_is(st, w.src[SI_VISIBILITY], "hidden") || sv_word_is(st, w.src[SI_VISIBILITY], "collapse")) continue;
        vector<double> p;
        if (!sv_geometry(d, st, firsts, shape, &w, &p)) continue;
        sv_w(lines, "k");
        sv_m(lines, sm);
        sv_n(lines, sv_word_is(st, w.src[SI_CLIP_RULE], "evenodd"));
        sv_path_out(lines, &p);
        sv_w(lines, "\n");
        count++;
    }
    return count;
}

/* the element a url() value (cv) of the element x names, when it is one named name: else 0 */
static uint32_t sv_ref_of(const dom_document *d, const css_style *st, uint32_t x, uint32_t cv, const char *name) {
    if (st->cval[cv].unit != CSS_UNIT_TEXT) return 0;
    amb_span sp = st->cval[cv].text;
    vector<char> t;
    append_range(t, text_ptr(st, sp), sp.len);
    size_t end;
    uint32_t e = sv_url(d, x, data(t), len(t), &end);
    return e && sv_is(d, e, name) ? e : 0;
}
/* the clipPath a clip-path value (cv) of the element x names: 0 for none, or one that is no clipPath */
static uint32_t sv_clip_of(const dom_document *d, const css_style *st, uint32_t x, uint32_t cv) { return sv_ref_of(d, st, x, cv, "clipPath"); }

/* a mask's region (SVG 2's mask element, CSS Masking 1, 7.1): x, y, width and height (-10%,
   -10%, 120%, 120% when not given) of the bounding box bb unless its maskUnits is
   userSpaceOnUse (then lengths of the viewport); 0 when it has no area */
static int sv_mask_region(const dom_document *d, uint32_t mk, const double *bb, int hb, const sv_walk *w, double fs, double *r) {
    int ubox = !sv_attr_is(d, mk, "maskUnits", "userSpaceOnUse"), g;
    double bw = bb[2] - bb[0], bh = bb[3] - bb[1];
    if (ubox && (!hb || bw <= 0 || bh <= 0)) return 0;
    r[0] = sv_len_attr(d, mk, "x", ubox ? 1 : w->vw, fs, ubox ? -0.1 : -0.1 * w->vw, &g);
    r[1] = sv_len_attr(d, mk, "y", ubox ? 1 : w->vh, fs, ubox ? -0.1 : -0.1 * w->vh, &g);
    r[2] = sv_len_attr(d, mk, "width", ubox ? 1 : w->vw, fs, ubox ? 1.2 : 1.2 * w->vw, &g);
    r[3] = sv_len_attr(d, mk, "height", ubox ? 1 : w->vh, fs, ubox ? 1.2 : 1.2 * w->vh, &g);
    if (ubox) {
        r[0] = bb[0] + r[0] * bw;
        r[1] = bb[1] + r[1] * bh;
        r[2] *= bw;
        r[3] *= bh;
    }
    return r[2] > 0 && r[3] > 0;
}

/* a mask's block (after its group's clip lines): M, its type (1 alpha, 0 luminance: its
   mask-type), its region and the matrix it is in (the masked element's user space); its
   content's lines, in that space (the bounding box's when maskContentUnits is
   objectBoundingBox), drawn with the values they inherit in the document; then E. 0 when it
   masks everything away (a region of no area, a mask inside itself) */
static int sv_mask_block(SB, uint32_t mk, const double *bb, int hb, const sv_walk *w, sv_mat mm, int depth) {
    double r[4];
    if (len(*uses) >= 16) return 0;
    for (size_t k = 0; k < len(*uses); k++) if (at(*uses, k) == mk) return 0;
    if (!sv_mask_region(d, mk, bb, hb, w, sv_fs(st, w), r)) return 0;
    sv_mat cm = mm;
    if (sv_attr_is(d, mk, "maskContentUnits", "objectBoundingBox")) {
        if (!hb || bb[2] - bb[0] <= 0 || bb[3] - bb[1] <= 0) return 0;
        cm = sv_mul(mm, sv_make(bb[2] - bb[0], 0, 0, bb[3] - bb[1], bb[0], bb[1]));
    }
    sv_walk root;
    if (!sv_root_walk(st, firsts, mk, w->vw, w->vh, &root)) return 0;
    uint32_t c0 = sv_first(firsts, mk);
    sv_w(out, "M");
    sv_n(out, c0 && sv_word_is(st, c0 + CSS_PROP_MASK_TYPE, "alpha"));
    sv_n(out, r[0]);
    sv_n(out, r[1]);
    sv_n(out, r[2]);
    sv_n(out, r[3]);
    sv_m(out, mm);
    sv_w(out, "\n");
    push_back(uses, mk);
    for (uint32_t c = d->node[mk].first_child; c; c = d->node[c].next_sibling) sv_node(SA, c, &root, cm, depth + 1);
    resize(uses, len(*uses) - 1);
    sv_w(out, "E\n");
    return 1;
}

/* a filter primitive's kind (the scene's numbers): one this does not draw (transparent black),
   feGaussianBlur, feOffset, feFlood, feComposite, feMerge, feColorMatrix, feBlend,
   feDropShadow, feComponentTransfer, feTurbulence, feMorphology, feConvolveMatrix,
   feDisplacementMap, feTile, feImage, feDiffuseLighting, feSpecularLighting */
enum { SV_FE_NONE, SV_FE_BLUR, SV_FE_OFFSET, SV_FE_FLOOD, SV_FE_COMPOSITE, SV_FE_MERGE, SV_FE_MATRIX, SV_FE_BLEND, SV_FE_SHADOW, SV_FE_TRANSFER,
       SV_FE_TURBULENCE, SV_FE_MORPHOLOGY, SV_FE_CONVOLVE, SV_FE_DISPLACE, SV_FE_TILE, SV_FE_IMAGE, SV_FE_DIFFUSE, SV_FE_SPECULAR };
static const char *const sv_fe_names[18] = { "", "feGaussianBlur", "feOffset", "feFlood", "feComposite", "feMerge", "feColorMatrix", "feBlend",
                                             "feDropShadow", "feComponentTransfer", "feTurbulence", "feMorphology", "feConvolveMatrix",
                                             "feDisplacementMap", "feTile", "feImage", "feDiffuseLighting", "feSpecularLighting" };
/* a primitive's input: SourceGraphic, SourceAlpha, none (transparent black: a primitive this
   does not draw, or one not yet made); else an earlier primitive's result, by its index */
enum { SV_IN_SOURCE = -1, SV_IN_ALPHA = -2, SV_IN_NONE = -3 };

/* an in or in2 attribute: the input it names (named results the closest before; an unknown name
   or none the previous primitive's result, or SourceGraphic for the first, as Blink takes them) */
static int sv_fe_input(const dom_document *d, uint32_t fe, const char *attr, const vector<uint32_t> *prims, int at_index) {
    vector<char> v, r;
    int prev = at_index > 0 ? at_index - 1 : SV_IN_SOURCE;
    if (!sv_attr_text(d, fe, attr, &v)) return prev;
    size_t a = sv_skip_ws(data(v), len(v), 0), b = len(v);
    while (b > a && sv_ws(at(v, b - 1))) b--;
    const char *s = data(v) + a;
    size_t n = b - a;
    if (n == 0) return prev;
    if (n == 13 && memcmp(s, "SourceGraphic", 13) == 0) return SV_IN_SOURCE;
    if (n == 11 && memcmp(s, "SourceAlpha", 11) == 0) return SV_IN_ALPHA;
    /* BackgroundImage, BackgroundAlpha, FillPaint and StrokePaint are no inputs Blink keeps:
       names it knows not, so the previous result */
    for (int k = at_index - 1; k >= 0; k--) {
        if (!sv_attr_text(d, at(*prims, (size_t)k), "result", &r)) continue;
        size_t ra = sv_skip_ws(data(r), len(r), 0), rb = len(r);
        while (rb > ra && sv_ws(at(r, rb - 1))) rb--;
        if (rb - ra == n && memcmp(data(r) + ra, s, n) == 0) return k;
    }
    return prev;
}
/* a number list attribute: up to max numbers into v (white space or commas between); how many */
static int sv_num_list(const dom_document *d, uint32_t x, const char *name, double *v, int max) {
    vector<char> t;
    if (!sv_attr_text(d, x, name, &t)) return 0;
    size_t i = sv_skip_ws(data(t), len(t), 0);
    int k = 0;
    while (i < len(t) && k < max) {
        if (!sv_number(data(t), len(t), &i, &v[k])) break;
        k++;
        i = sv_skip_sep(data(t), len(t), i);
    }
    return k;
}
static void sv_par_of(const dom_document *d, uint32_t x, int *ax, int *ay, int *slice, int *none);

/* a primitive's colour from a colour property and an opacity one of its computed values,
   RGBA */
static uint32_t sv_fe_colour(const css_style *st, uint32_t c0, int cprop, int oprop) {
    if (!c0) return 0x000000ff;
    uint32_t col = st->cval[c0 + (uint32_t)cprop].color;
    double op = st->cval[c0 + (uint32_t)oprop].num;
    op = op < 0 ? 0 : op > 1 ? 1 : op;
    return (col & 0xffffff00u) | (uint32_t)floor((col & 0xff) * op + 0.5);
}

/* a filter's block (Filter Effects 1, as Blink builds one): F, its region (x, y, width and
   height, -10%, -10%, 120%, 120% when not given, of the bounding box unless its filterUnits
   is userSpaceOnUse) and the matrix it is in (the filtered element's user space), its
   primitives' count; a line for each primitive -- x, its kind, its inputs, its colour space
   (1 linearRGB, color-interpolation-filters' initial value; 0 sRGB), its subregion (the
   union of its inputs' subregions, a source's the region, unless its x, y, width and height
   say: of the bounding box when primitiveUnits is objectBoundingBox) and its values, lengths
   in the user space -- then E; its region (x0, y0, x1, y1) into reg. 0 when it draws nothing (a region of no area, no
   primitive) */
static int sv_filter_block(SB, uint32_t flt, const double *bb, int hb, const sv_walk *w, sv_mat mm, double *reg) {
    double r[4], fs = sv_fs(st, w), bw = bb[2] - bb[0], bh = bb[3] - bb[1];
    int ubox = !sv_attr_is(d, flt, "filterUnits", "userSpaceOnUse"), pbox = sv_attr_is(d, flt, "primitiveUnits", "objectBoundingBox"), g;
    if ((ubox || pbox) && (!hb || bw <= 0 || bh <= 0)) return 0;
    r[0] = sv_len_attr(d, flt, "x", ubox ? 1 : w->vw, fs, ubox ? -0.1 : -0.1 * w->vw, &g);
    r[1] = sv_len_attr(d, flt, "y", ubox ? 1 : w->vh, fs, ubox ? -0.1 : -0.1 * w->vh, &g);
    r[2] = sv_len_attr(d, flt, "width", ubox ? 1 : w->vw, fs, ubox ? 1.2 : 1.2 * w->vw, &g);
    r[3] = sv_len_attr(d, flt, "height", ubox ? 1 : w->vh, fs, ubox ? 1.2 : 1.2 * w->vh, &g);
    if (ubox) {
        r[0] = bb[0] + r[0] * bw;
        r[1] = bb[1] + r[1] * bh;
        r[2] *= bw;
        r[3] *= bh;
    }
    if (!(r[2] > 0 && r[3] > 0)) return 0;
    vector<uint32_t> prims;
    for (uint32_t c = d->node[flt].first_child; c; c = d->node[c].next_sibling)
        if (d->node[c].kind == DOM_NODE_KIND_ELEMENT && d->node[c].ns == DOM_NAMESPACE_SVG && len(prims) < 64) push_back(prims, c);
    if (!len(prims)) return 0;
    vector<double> subs;   /* each primitive's subregion */
    vector<char> lines;
    /* a length of the primitives' units: x (0) or y (1) kind, a box's share or the viewport's */
    double sx = pbox ? bw : 1, sy = pbox ? bh : 1;
    for (size_t k = 0; k < len(prims); k++) {
        uint32_t fe = at(prims, k), c0 = sv_first(firsts, fe);
        int kind = SV_FE_NONE;
        for (int q = 1; q < 18 && kind == SV_FE_NONE; q++) if (sv_is(d, fe, sv_fe_names[q])) kind = q;
        int in1 = sv_fe_input(d, fe, "in", &prims, (int)k), in2 = sv_fe_input(d, fe, "in2", &prims, (int)k);
        int two = kind == SV_FE_COMPOSITE || kind == SV_FE_BLEND || kind == SV_FE_DISPLACE;
        int none_in = kind == SV_FE_FLOOD || kind == SV_FE_NONE || kind == SV_FE_TURBULENCE || kind == SV_FE_IMAGE;
        vector<int32_t> merged;
        if (kind == SV_FE_MERGE)
            for (uint32_t mn = d->node[fe].first_child; mn; mn = d->node[mn].next_sibling)
                if (sv_is(d, mn, "feMergeNode") && len(merged) < 64) push_back(merged, (int32_t)sv_fe_input(d, mn, "in", &prims, (int)k));
        /* the subregion: the union of the inputs' (a source's, or none's, the region) */
        double u[4] = { 1e300, 1e300, -1e300, -1e300 };
        int ins[66], nin = 0;
        if (!none_in && kind != SV_FE_MERGE) { ins[nin++] = in1; if (two) ins[nin++] = in2; }
        for (size_t q = 0; q < len(merged); q++) ins[nin++] = at(merged, q);
        /* (feTile's the region, as Blink resets it) */
        if (nin == 0 || kind == SV_FE_TILE) { u[0] = r[0]; u[1] = r[1]; u[2] = r[0] + r[2]; u[3] = r[1] + r[3]; nin = 0; }
        for (int q = 0; q < nin; q++) {
            double e0 = r[0], e1 = r[1], e2 = r[0] + r[2], e3 = r[1] + r[3];
            if (ins[q] >= 0) { e0 = at(subs, 4 * (size_t)ins[q]); e1 = at(subs, 4 * (size_t)ins[q] + 1); e2 = e0 + at(subs, 4 * (size_t)ins[q] + 2); e3 = e1 + at(subs, 4 * (size_t)ins[q] + 3); }
            u[0] = fmin(u[0], e0); u[1] = fmin(u[1], e1); u[2] = fmax(u[2], e2); u[3] = fmax(u[3], e3);
        }
        double sr[4] = { u[0], u[1], u[2] - u[0], u[3] - u[1] };
        int given;
        double val = sv_len_attr(d, fe, "x", pbox ? 1 : w->vw, fs, 0, &given);
        if (given) sr[0] = pbox ? bb[0] + val * bw : val;
        val = sv_len_attr(d, fe, "y", pbox ? 1 : w->vh, fs, 0, &given);
        if (given) sr[1] = pbox ? bb[1] + val * bh : val;
        val = sv_len_attr(d, fe, "width", pbox ? 1 : w->vw, fs, 0, &given);
        if (given) sr[2] = pbox ? val * bw : val;
        val = sv_len_attr(d, fe, "height", pbox ? 1 : w->vh, fs, 0, &given);
        if (given) sr[3] = pbox ? val * bh : val;
        for (int q = 0; q < 4; q++) push_back(subs, sr[q]);
        /* (feImage's result its picture's, sRGB, whatever space it is asked in) */
        int linear = kind != SV_FE_IMAGE &&
                     !(c0 && (sv_word_is(st, c0 + CSS_PROP_COLOR_INTERPOLATION_FILTERS, "srgb") || sv_word_is(st, c0 + CSS_PROP_COLOR_INTERPOLATION_FILTERS, "auto")));
        sv_w(&lines, "x");
        sv_n(&lines, kind);
        sv_n(&lines, in1);
        sv_n(&lines, in2);
        sv_n(&lines, linear);
        for (int q = 0; q < 4; q++) sv_n(&lines, sr[q]);
        vector<char> v;
        double nv[20];
        if (kind == SV_FE_BLUR || kind == SV_FE_SHADOW) {
            double dev[2] = { kind == SV_FE_SHADOW ? 2 : 0, 0 };
            int k2 = sv_num_list(d, fe, "stdDeviation", dev, 2);
            if (k2 == 1 || (k2 == 0 && kind == SV_FE_SHADOW)) dev[1] = dev[0];
            double dx = kind == SV_FE_SHADOW ? sv_num_attr(d, fe, "dx", 2) : 0, dy = kind == SV_FE_SHADOW ? sv_num_attr(d, fe, "dy", 2) : 0;
            /* a negative deviation none (the input as it is), as Blink takes it */
            if (dev[0] < 0) dev[0] = 0;
            if (dev[1] < 0) dev[1] = 0;
            sv_n(&lines, dev[0] * sx);
            sv_n(&lines, dev[1] * sy);
            if (kind == SV_FE_SHADOW) {
                sv_n(&lines, dx * sx);
                sv_n(&lines, dy * sy);
                sv_hex(&lines, sv_fe_colour(st, c0, CSS_PROP_FLOOD_COLOR, CSS_PROP_FLOOD_OPACITY));
            }
        } else if (kind == SV_FE_OFFSET) {
            sv_n(&lines, sv_num_attr(d, fe, "dx", 0) * sx);
            sv_n(&lines, sv_num_attr(d, fe, "dy", 0) * sy);
        } else if (kind == SV_FE_FLOOD) {
            sv_hex(&lines, sv_fe_colour(st, c0, CSS_PROP_FLOOD_COLOR, CSS_PROP_FLOOD_OPACITY));
        } else if (kind == SV_FE_COMPOSITE) {
            static const char *const ops[7] = { "over", "in", "out", "atop", "xor", "arithmetic", "lighter" };
            int op = 0;
            if (sv_attr_text(d, fe, "operator", &v))
                for (int q = 0; q < 7; q++) if (sv_eq_ci(data(v), len(v), ops[q])) op = q;
            sv_n(&lines, op);
            sv_n(&lines, sv_num_attr(d, fe, "k1", 0));
            sv_n(&lines, sv_num_attr(d, fe, "k2", 0));
            sv_n(&lines, sv_num_attr(d, fe, "k3", 0));
            sv_n(&lines, sv_num_attr(d, fe, "k4", 0));
        } else if (kind == SV_FE_MERGE) {
            sv_n(&lines, (double)len(merged));
            for (size_t q = 0; q < len(merged); q++) sv_n(&lines, at(merged, q));
        } else if (kind == SV_FE_MATRIX) {
            /* the matrix, rows R G B A, five columns (the last a constant, 0 to 1) */
            double mx[20] = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0 };
            int got = sv_num_list(d, fe, "values", nv, 20);
            int type = 0;
            if (sv_attr_text(d, fe, "type", &v))
                type = sv_eq_ci(data(v), len(v), "saturate") ? 1 : sv_eq_ci(data(v), len(v), "huerotate") ? 2 : sv_eq_ci(data(v), len(v), "luminancetoalpha") ? 3 : 0;
            if (type == 0 && got == 20) memcpy(mx, nv, sizeof mx);
            else if (type == 1) {
                double s = got >= 1 ? nv[0] : 1;
                double m1[20] = { 0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s, 0, 0, 0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s, 0, 0,
                                  0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s, 0, 0, 0, 0, 0, 1, 0 };
                memcpy(mx, m1, sizeof mx);
            } else if (type == 2) {
                double a = (got >= 1 ? nv[0] : 0) * M_PI / 180, cs = cos(a), sn = sin(a);
                double m2[20] = { 0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928, 0, 0,
                                  0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283, 0, 0,
                                  0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072, 0, 0,
                                  0, 0, 0, 1, 0 };
                memcpy(mx, m2, sizeof mx);
            } else if (type == 3) {
                double m3[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.2125, 0.7154, 0.0721, 0, 0 };
                memcpy(mx, m3, sizeof mx);
            }
            for (int q = 0; q < 20; q++) sv_n(&lines, mx[q]);
        } else if (kind == SV_FE_BLEND) {
            static const char *const modes[16] = { "normal", "multiply", "screen", "darken", "lighten", "overlay", "color-dodge", "color-burn",
                                                   "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity" };
            int mode = 0;
            if (sv_attr_text(d, fe, "mode", &v))
                for (int q = 0; q < 16; q++) if (sv_eq_ci(data(v), len(v), modes[q])) mode = q;
            sv_n(&lines, mode);
        } else if (kind == SV_FE_TRANSFER) {
            /* each channel's function: R, G, B, A -- its type (0 identity, 1 table, 2 discrete, 3
               linear, 4 gamma), its values' count and values */
            static const char *const fns[4] = { "feFuncR", "feFuncG", "feFuncB", "feFuncA" };
            for (int ch = 0; ch < 4; ch++) {
                uint32_t fn = 0;
                for (uint32_t q = d->node[fe].first_child; q; q = d->node[q].next_sibling) if (sv_is(d, q, fns[ch])) fn = q;
                int type = 0, cnt = 0;
                double tv[20];
                if (fn && sv_attr_text(d, fn, "type", &v)) {
                    if (sv_eq_ci(data(v), len(v), "table")) { type = 1; cnt = sv_num_list(d, fn, "tableValues", tv, 20); }
                    else if (sv_eq_ci(data(v), len(v), "discrete")) { type = 2; cnt = sv_num_list(d, fn, "tableValues", tv, 20); }
                    else if (sv_eq_ci(data(v), len(v), "linear")) { type = 3; cnt = 2; tv[0] = sv_num_attr(d, fn, "slope", 1); tv[1] = sv_num_attr(d, fn, "intercept", 0); }
                    else if (sv_eq_ci(data(v), len(v), "gamma")) {
                        type = 4;
                        cnt = 3;
                        tv[0] = sv_num_attr(d, fn, "amplitude", 1);
                        tv[1] = sv_num_attr(d, fn, "exponent", 1);
                        tv[2] = sv_num_attr(d, fn, "offset", 0);
                    }
                    if ((type == 1 || type == 2) && cnt == 0) type = 0;
                }
                sv_n(&lines, type);
                sv_n(&lines, cnt);
                for (int q = 0; q < cnt; q++) sv_n(&lines, tv[q]);
            }
        } else if (kind == SV_FE_TURBULENCE) {
            /* its type (1 turbulence, the initial one; 0 fractalNoise), base frequencies (a
               negative one 0, as Chromium takes it: measured), octaves, seed and stitching */
            double bf[2] = { 0, 0 };
            int k2 = sv_num_list(d, fe, "baseFrequency", bf, 2);
            if (k2 == 1) bf[1] = bf[0];
            if (bf[0] < 0 || bf[1] < 0) bf[0] = bf[1] = 0;
            sv_n(&lines, sv_attr_is(d, fe, "type", "fractalNoise") ? 0 : 1);
            sv_n(&lines, bf[0]);
            sv_n(&lines, bf[1]);
            double no = floor(sv_num_attr(d, fe, "numOctaves", 1));
            sv_n(&lines, no < -1 ? -1 : no > 1000 ? 1000 : no);
            sv_n(&lines, sv_num_attr(d, fe, "seed", 0));
            sv_n(&lines, sv_attr_is(d, fe, "stitchTiles", "stitch"));
        } else if (kind == SV_FE_MORPHOLOGY) {
            /* its operator (0 erode, the initial one; 1 dilate) and radii, a negative one 0
               (measured: Chromium's) */
            double rad[2] = { 0, 0 };
            int k2 = sv_num_list(d, fe, "radius", rad, 2);
            if (k2 == 1) rad[1] = rad[0];
            sv_n(&lines, sv_attr_is(d, fe, "operator", "dilate"));
            sv_n(&lines, rad[0] > 0 ? rad[0] * sx : 0);
            sv_n(&lines, rad[1] > 0 ? rad[1] * sy : 0);
        } else if (kind == SV_FE_CONVOLVE) {
            /* its order, target, divisor (the kernel's sum, 1 for none), bias, edge mode (0
               duplicate, the initial one; 1 wrap; 2 none), preserveAlpha, and its kernel's
               values; an order of none, a kernel of another size, a target outside it or a
               divisor of 0 draw transparent black, as Blink's ParametersValid has it (order 0) */
            double ord[2] = { 3, 3 }, kv[400];
            int k2 = sv_num_list(d, fe, "order", ord, 2);
            if (k2 == 1) ord[1] = ord[0];
            int ox = ord[0] >= 1 && ord[0] <= 20 ? (int)ord[0] : 0, oy = ord[1] >= 1 && ord[1] <= 20 ? (int)ord[1] : 0;
            int nk = sv_num_list(d, fe, "kernelMatrix", kv, 400);
            double tx = sv_attr(d, fe, "targetX") ? floor(sv_num_attr(d, fe, "targetX", 0)) : ox / 2;
            double ty = sv_attr(d, fe, "targetY") ? floor(sv_num_attr(d, fe, "targetY", 0)) : oy / 2;
            double sum = 0;
            for (int q = 0; q < nk; q++) sum += kv[q];
            double div = sv_attr(d, fe, "divisor") ? sv_num_attr(d, fe, "divisor", 1) : sum != 0 ? sum : 1;
            if (!ox || !oy || nk != ox * oy || tx < 0 || tx >= ox || ty < 0 || ty >= oy || div == 0) {
                ox = oy = 0;
                tx = ty = 0;
            }
            int mode = 0;
            if (sv_attr_is(d, fe, "edgeMode", "wrap")) mode = 1;
            else if (sv_attr_is(d, fe, "edgeMode", "none")) mode = 2;
            sv_n(&lines, ox);
            sv_n(&lines, oy);
            sv_n(&lines, tx);
            sv_n(&lines, ty);
            sv_n(&lines, div);
            sv_n(&lines, sv_num_attr(d, fe, "bias", 0));
            sv_n(&lines, mode);
            sv_n(&lines, sv_attr_is(d, fe, "preserveAlpha", "true"));
            for (int q = 0; q < ox * oy; q++) sv_n(&lines, kv[q]);
        } else if (kind == SV_FE_DISPLACE) {
            /* its scale (a box's width's share for objectBoundingBox, both ways: Blink scales it
               horizontally alone) and channels (0 R, 1 G, 2 B, 3 A, the initial one) */
            static const char *const chans[4] = { "R", "G", "B", "A" };
            int sel[2] = { 3, 3 };
            for (int a = 0; a < 2; a++)
                for (int q = 0; q < 4; q++) if (sv_attr_is(d, fe, a ? "yChannelSelector" : "xChannelSelector", chans[q])) sel[a] = q;
            sv_n(&lines, sv_num_attr(d, fe, "scale", 0) * sx);
            sv_n(&lines, sel[0]);
            sv_n(&lines, sel[1]);
        } else if (kind == SV_FE_TILE) {
            /* the tile: its input's subregion (a source's the region) */
            double t0 = r[0], t1 = r[1], t2 = r[2], t3 = r[3];
            if (in1 >= 0) { t0 = at(subs, 4 * (size_t)in1); t1 = at(subs, 4 * (size_t)in1 + 1); t2 = at(subs, 4 * (size_t)in1 + 2); t3 = at(subs, 4 * (size_t)in1 + 3); }
            sv_n(&lines, t0);
            sv_n(&lines, t1);
            sv_n(&lines, t2);
            sv_n(&lines, t3);
        } else if (kind == SV_FE_IMAGE) {
            /* its picture: an element of the document drawn as a use draws it, in the user space
               moved to the subregion's corner (as Blink's FEImage places one), or the loader's
               picture of its href in the subregion by its preserveAspectRatio; a block, I ... E */
            uint32_t ref = sv_href(d, fe);
            int ext = !ref && (sv_attr_text(d, fe, "href", &v) || sv_attr_text(d, fe, "xlink:href", &v)) && len(v) > 0 && at(v, 0) != '#';
            sv_w(&lines, "\nI\n");
            if (ref && len(*uses) < 16 && !sv_is(d, ref, "filter")) {
                int self = 0;
                for (size_t q = 0; q < len(*uses); q++) if (at(*uses, q) == ref) self = 1;
                for (uint32_t a = d->node[fe].parent; a; a = d->node[a].parent) if (a == ref) self = 1;
                sv_walk rw;
                if (!self && sv_root_walk(st, firsts, d->node[ref].parent ? d->node[ref].parent : ref, w->vw, w->vh, &rw)) {
                    push_back(uses, ref);
                    sv_node(d, st, firsts, f, &lines, ids, uses, work, ref, &rw, sv_mul(mm, sv_make(1, 0, 0, 1, sr[0], sr[1])), 1);
                    resize(uses, len(*uses) - 1);
                }
            } else if (ext) {
                int ax, ay, slice, none;
                sv_par_of(d, fe, &ax, &ay, &slice, &none);
                sv_w(&lines, "i");
                sv_m(&lines, mm);
                for (int q = 0; q < 4; q++) sv_n(&lines, sr[q]);
                sv_w(&lines, " 0 0");
                sv_n(&lines, ax);
                sv_n(&lines, ay);
                sv_n(&lines, slice);
                sv_n(&lines, none);
                sv_w(&lines, " 2");
                sv_n(&lines, fe);
                sv_w(&lines, "\n");
            }
            sv_w(&lines, "E");
        } else if (kind == SV_FE_DIFFUSE || kind == SV_FE_SPECULAR) {
            /* its surface scale, constant, specular exponent (1 to 128, as Blink clamps it),
               lighting-color, and its light, the first light element in it (none draws
               transparent black): 1 distant (azimuth, elevation), 2 point (x, y, z), 3 spot
               (x, y, z, its pointsAt, its specular exponent, its cone's angle, 90 for none or
               one past 90), each place of the primitives' units */
            int spec = kind == SV_FE_SPECULAR;
            double ex = spec ? sv_num_attr(d, fe, "specularExponent", 1) : 1;
            sv_n(&lines, sv_num_attr(d, fe, "surfaceScale", 1));
            sv_n(&lines, sv_num_attr(d, fe, spec ? "specularConstant" : "diffuseConstant", 1));
            sv_n(&lines, ex < 1 ? 1 : ex > 128 ? 128 : ex);
            sv_hex(&lines, c0 ? st->cval[c0 + CSS_PROP_LIGHTING_COLOR].color : 0xffffffffu);
            uint32_t lt = 0;
            for (uint32_t q = d->node[fe].first_child; q && !lt; q = d->node[q].next_sibling)
                if (sv_is(d, q, "feDistantLight") || sv_is(d, q, "fePointLight") || sv_is(d, q, "feSpotLight")) lt = q;
            double zk = pbox ? sqrt((bw * bw + bh * bh) / 2) : 1;
            if (!lt) sv_w(&lines, " 0");
            else if (sv_is(d, lt, "feDistantLight")) {
                sv_w(&lines, " 1");
                sv_n(&lines, sv_num_attr(d, lt, "azimuth", 0));
                sv_n(&lines, sv_num_attr(d, lt, "elevation", 0));
            } else {
                int spot = sv_is(d, lt, "feSpotLight");
                static const char *const at3[6] = { "x", "y", "z", "pointsAtX", "pointsAtY", "pointsAtZ" };
                sv_w(&lines, spot ? " 3" : " 2");
                for (int q = 0; q < (spot ? 6 : 3); q++) {
                    double p = sv_num_attr(d, lt, at3[q], 0);
                    if (pbox) p = q % 3 == 0 ? bb[0] + p * bw : q % 3 == 1 ? bb[1] + p * bh : p * zk;
                    sv_n(&lines, p);
                }
                if (spot) {
                    double se = sv_num_attr(d, lt, "specularExponent", 1), cone = sv_num_attr(d, lt, "limitingConeAngle", 0);
                    sv_n(&lines, se < 1 ? 1 : se > 128 ? 128 : se);
                    sv_n(&lines, cone == 0 || cone > 90 || cone < -90 ? 90 : cone);
                }
            }
        }
        sv_w(&lines, "\n");
    }
    sv_w(out, "F");
    for (int q = 0; q < 4; q++) sv_n(out, r[q]);
    sv_m(out, mm);
    sv_n(out, (double)len(prims));
    sv_w(out, "\n");
    append_range(out, data(lines), len(lines));
    sv_w(out, "E\n");
    reg[0] = r[0];
    reg[1] = r[1];
    reg[2] = r[0] + r[2];
    reg[3] = r[1] + r[3];
    return 1;
}

/* a run of CSS's filter functions (the text s, n of a filter's list) on an SVG element (Filter
   Effects 1, 13: each its shorthand's primitive, in sRGB, as Blink builds them): a filter's
   block of them; its region what the run is given (ext: x0, y0, x1, y1 -- the bounding box,
   or the region of the filter before it in the list) outset by how far the functions reach
   (three deviations of a blur and a px, a shadow's offset too), each primitive's subregion the
   region, which then is ext. 0 when there is none to draw */
typedef struct sv_fop { int kind; double v[20]; uint32_t col; } sv_fop;
static int sv_css_filter_run(const char *s, size_t n, double *ext, sv_mat mm, vector<char> *out) {
    if (ext[2] < ext[0] || ext[3] < ext[1]) return 0;
    vector<char> t;
    vector<sv_fop> ops;
    append_range(t, s, n);
    size_t i = 0, pa, pl;
    double reach = 0;
    while (css_next_part(data(t), len(t), &i, &pa, &pl) && len(ops) < 64) {
        const char *fn = data(t) + pa;
        size_t open = 0;
        while (open < pl && fn[open] != '(') open++;
        if (open >= pl || fn[pl - 1] != ')') continue;
        const char *args = fn + open + 1;
        size_t al = pl - open - 2, j = 0, xa, xl;
        sv_fop op;
        memset(&op, 0, sizeof op);
        if (sv_eq_ci(fn, open, "drop-shadow")) {
            int nv = 0;
            uint32_t cc;
            op.kind = SV_FE_SHADOW;
            op.col = 0x000000ff;
            while (css_next_part(args, al, &j, &xa, &xl)) {
                double x;
                if (nv < 3 && css_length_eval(args + xa, xl, 0, &x)) op.v[nv++] = x;
                else if (css_color_parse(args + xa, xl, &cc)) op.col = cc;
            }
            if (op.v[2] < 0) op.v[2] = 0;
            reach += fabs(op.v[0]) + fabs(op.v[1]) + 3 * op.v[2] + 1;
            push_back(ops, op);
            continue;
        }
        /* the one argument: a number, a percentage, a length (px) or an angle (degrees) */
        double a = 1;
        int have = css_next_part(args, al, &j, &xa, &xl);
        if (have) {
            char num[64];
            size_t k = xl < 63 ? xl : 63;
            memcpy(num, args + xa, k);
            num[k] = 0;
            a = strtod(num, 0);
            if (k > 0 && num[k - 1] == '%') a /= 100;
            else if (k > 4 && sv_eq_ci(num + k - 4, 4, "turn")) a *= 360;
            else if (k > 4 && sv_eq_ci(num + k - 4, 4, "grad")) a *= 0.9;
            else if (k > 3 && sv_eq_ci(num + k - 3, 3, "rad")) a *= 180 / M_PI;
        }
        double o = 1 - (a > 1 ? 1 : a < 0 ? 0 : a);
        if (sv_eq_ci(fn, open, "blur")) {
            op.kind = SV_FE_BLUR;
            op.v[0] = have && a > 0 ? a : 0;
            reach += 3 * op.v[0] + 1;
        } else if (sv_eq_ci(fn, open, "brightness") || sv_eq_ci(fn, open, "contrast") || sv_eq_ci(fn, open, "invert") ||
                   sv_eq_ci(fn, open, "opacity")) {
            op.kind = SV_FE_TRANSFER;
            op.v[0] = sv_eq_ci(fn, open, "brightness") ? 0 : sv_eq_ci(fn, open, "contrast") ? 1 : sv_eq_ci(fn, open, "invert") ? 2 : 3;
            op.v[1] = op.v[0] >= 2 && a > 1 ? 1 : a;
        } else {
            double m[20] = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0 };
            if (sv_eq_ci(fn, open, "grayscale")) {
                double g[15] = { 0.2126 + 0.7874 * o, 0.7152 - 0.7152 * o, 0.0722 - 0.0722 * o, 0, 0, 0.2126 - 0.2126 * o, 0.7152 + 0.2848 * o,
                                 0.0722 - 0.0722 * o, 0, 0, 0.2126 - 0.2126 * o, 0.7152 - 0.7152 * o, 0.0722 + 0.9278 * o, 0, 0 };
                memcpy(m, g, sizeof g);
            } else if (sv_eq_ci(fn, open, "sepia")) {
                double g[15] = { 0.393 + 0.607 * o, 0.769 - 0.769 * o, 0.189 - 0.189 * o, 0, 0, 0.349 - 0.349 * o, 0.686 + 0.314 * o, 0.168 - 0.168 * o,
                                 0, 0, 0.272 - 0.272 * o, 0.534 - 0.534 * o, 0.131 + 0.869 * o, 0, 0 };
                memcpy(m, g, sizeof g);
            } else if (sv_eq_ci(fn, open, "saturate")) {
                double s = a;
                double g[15] = { 0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s, 0, 0, 0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s,
                                 0, 0, 0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s, 0, 0 };
                memcpy(m, g, sizeof g);
            } else if (sv_eq_ci(fn, open, "hue-rotate")) {
                double r = (have ? a : 0) * M_PI / 180, cs = cos(r), sn = sin(r);
                double g[15] = { 0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928, 0, 0,
                                 0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283, 0, 0,
                                 0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072, 0, 0 };
                memcpy(m, g, sizeof g);
            } else continue;
            op.kind = SV_FE_MATRIX;
            memcpy(op.v, m, sizeof m);
        }
        push_back(ops, op);
    }
    if (!len(ops)) return 0;
    double r[4] = { ext[0] - reach, ext[1] - reach, ext[2] - ext[0] + 2 * reach, ext[3] - ext[1] + 2 * reach };
    ext[0] = r[0];
    ext[1] = r[1];
    ext[2] = r[0] + r[2];
    ext[3] = r[1] + r[3];
    sv_w(out, "F");
    for (int q = 0; q < 4; q++) sv_n(out, r[q]);
    sv_m(out, mm);
    sv_n(out, (double)len(ops));
    sv_w(out, "\n");
    for (size_t k = 0; k < len(ops); k++) {
        sv_fop op = at(ops, k);
        int in = k ? (int)k - 1 : SV_IN_SOURCE;
        sv_w(out, "x");
        sv_n(out, op.kind);
        sv_n(out, in);
        sv_n(out, in);
        sv_n(out, 0);
        for (int q = 0; q < 4; q++) sv_n(out, r[q]);
        if (op.kind == SV_FE_SHADOW) {
            sv_n(out, op.v[2]);
            sv_n(out, op.v[2]);
            sv_n(out, op.v[0]);
            sv_n(out, op.v[1]);
            sv_hex(out, op.col);
        } else if (op.kind == SV_FE_BLUR) {
            sv_n(out, op.v[0]);
            sv_n(out, op.v[0]);
        } else if (op.kind == SV_FE_MATRIX) {
            for (int q = 0; q < 20; q++) sv_n(out, op.v[q]);
        } else {
            /* R, G and B: brightness and contrast linear, invert a table; opacity's alpha a table */
            int which = (int)op.v[0];
            double a = op.v[1];
            for (int ch = 0; ch < 3; ch++) {
                if (which == 3) sv_w(out, " 0 0");
                else if (which == 2) { sv_w(out, " 1 2"); sv_n(out, a); sv_n(out, 1 - a); }
                else { sv_w(out, " 3 2"); sv_n(out, a); sv_n(out, which == 1 ? 0.5 - 0.5 * a : 0); }
            }
            if (which == 3) { sv_w(out, " 1 2 0"); sv_n(out, a); }
            else sv_w(out, " 0 0");
        }
        sv_w(out, "\n");
    }
    sv_w(out, "E\n");
    return 1;
}

/* a group's lines, its opacity, its clip (cp a clipPath, 0 for none), its mask (mk a mask) and
   its filter's list (fcv, the filter's computed value when it is one; else 0): its g line,
   followed by its clip's shapes, its mask's block and its filter's (a clipPath's own clip-path
   a group about it). A list of more than one filter -- url()s naming filter elements, runs of
   CSS's functions -- is a group for each, the first innermost, the last the group's own (each
   applied to what the one before it made, as Chromium chains them); a url() that names no
   filter is passed over. How many groups were written (the caller ends each); -1 when the
   element draws nothing (its mask masks all of it, a filter has no region) */
static int sv_group_open(SB, uint32_t x, uint32_t cp, uint32_t mk, uint32_t fcv, double op, const sv_walk *pw, const sv_walk *w, sv_mat mm,
                         int depth) {
    if (op >= 1 && !cp && !mk && !fcv) return 0;
    double bb[4] = { 0, 0, 0, 0 };
    int hb = 0, groups = 0;
    vector<char> v;
    if ((cp && sv_attr_text(d, cp, "clipPathUnits", &v)) || mk || fcv) hb = sv_bounds(SA, x, pw, sv_id(), bb, depth, 0);
    /* the filter's list: its stages' blocks one after another (fblk), where each begins (fat) */
    vector<char> fblk;
    vector<size_t> fat;
    if (fcv) {
        vector<char> t, run;
        amb_span sp = st->cval[fcv].text;
        append_range(t, text_ptr(st, sp), sp.len);
        /* what each stage is given: the bounding box, then the region of the stage before it */
        double ext[4] = { bb[0], bb[1], bb[2], bb[3] };
        size_t i = 0, pa, pl, end;
        int ok = 1;
        for (;;) {
            int more = css_next_part(data(t), len(t), &i, &pa, &pl);
            int url = more && pl >= 4 && memcmp(data(t) + pa, "url(", 4) == 0;
            if ((!more || url) && len(run)) {
                size_t from = len(fblk);
                if (hb && sv_css_filter_run(data(run), len(run), ext, mm, &fblk)) push_back(fat, from);
                clear(run);
            }
            if (!more) break;
            if (!url) {
                if (len(run)) push_back(run, ' ');
                append_range(run, data(t) + pa, pl);
                continue;
            }
            uint32_t flt = sv_url(d, x, data(t) + pa, pl, &end);
            if (!flt || !sv_is(d, flt, "filter")) continue;
            size_t from = len(fblk);
            if (!sv_filter_block(d, st, firsts, f, &fblk, ids, uses, work, flt, bb, hb, w, mm, ext)) { ok = 0; break; }
            push_back(fat, from);
        }
        if (!ok) return -1;
    }
    uint32_t c0 = cp ? sv_first(firsts, cp) : 0;
    uint32_t outer = c0 ? sv_clip_of(d, st, cp, c0 + CSS_PROP_CLIP_PATH) : 0;
    if (outer && outer != cp) {
        vector<char> ol;
        int ok = sv_clip_shapes(SA, outer, bb, hb, mm, w->vw, w->vh, &ol);
        sv_w(out, "g 1 2");
        sv_n(out, ok);
        sv_w(out, "\n");
        append_range(out, data(ol), len(ol));
        groups++;
    }
    vector<char> lines, blocks;
    int k = cp ? sv_clip_shapes(SA, cp, bb, hb, mm, w->vw, w->vh, &lines) : 0;
    if (mk && !sv_mask_block(d, st, firsts, f, &blocks, ids, uses, work, mk, bb, hb, w, mm, depth)) {
        for (int q = 0; q < groups; q++) sv_w(out, "e\n");
        return -1;
    }
    size_t ns = len(fat);
    int filtered = ns > 0;
    if (filtered) append_range(blocks, data(fblk) + at(fat, ns - 1), len(fblk) - at(fat, ns - 1));
    if (op >= 1 && !cp && !mk && !filtered) {
        for (int q = 0; q < groups; q++) sv_w(out, "e\n");
        return 0;
    }
    sv_w(out, "g");
    sv_n(out, op);
    sv_n(out, cp ? 2 : 0);
    sv_n(out, k);
    if (mk || filtered) {
        sv_n(out, mk ? 1 : 0);
        sv_n(out, filtered ? 1 : 0);
    }
    sv_w(out, "\n");
    append_range(out, data(lines), len(lines));
    append_range(out, data(blocks), len(blocks));
    groups++;
    /* the list's earlier stages, each a group inside the next's */
    for (size_t q = ns; q >= 2; q--) {
        sv_w(out, "g 1 0 0 0 1\n");
        append_range(out, data(fblk) + at(fat, q - 2), at(fat, q - 1) - at(fat, q - 2));
        groups++;
    }
    return groups;
}

/* paint-order's computed value (cv) as the order of fill (0), stroke (1) and markers (2): those
   it names first, the rest after in that order */
static void sv_paint_order(const css_style *st, uint32_t cv, int *order) {
    int n = 0, seen = 0;
    if (st->cval[cv].unit == CSS_UNIT_TEXT) {
        amb_span sp = st->cval[cv].text;
        vector<char> t;
        append_range(t, text_ptr(st, sp), sp.len);
        size_t i = 0;
        while (i < len(t) && n < 3) {
            i = sv_skip_ws(data(t), len(t), i);
            size_t b = i;
            while (i < len(t) && !sv_ws(at(t, i))) i++;
            int k = sv_eq_ci(data(t) + b, i - b, "fill") ? 0 : sv_eq_ci(data(t) + b, i - b, "stroke") ? 1 : sv_eq_ci(data(t) + b, i - b, "markers") ? 2 : -1;
            if (k >= 0 && !(seen >> k & 1)) { order[n++] = k; seen |= 1 << k; }
        }
    }
    for (int k = 0; k < 3; k++) if (!(seen >> k & 1)) order[n++] = k;
}

static void sv_markers(SB, uint32_t x, const sv_walk *w, sv_mat m, const vector<double> *p, double sw, int depth);

/* a stroke's words in a path's line, after its paint: its opacity, width (sw), cap, join, miter
   limit, dash offset, and its dashes (none, or the list, a percentage of the viewport's
   diagonal, its count first), the offset and dashes scaled by dk (a shape's pathLength's: none
   when 0, a solid stroke) */
static void sv_stroke_words(const css_style *st, const sv_walk *w, double sw, double dk, vector<char> *sline) {
    sv_n(sline, st->cval[w->src[SI_STROKE_OPACITY]].num);
    sv_n(sline, sw);
    uint32_t lc = w->src[SI_LINECAP], lj = w->src[SI_LINEJOIN];
    sv_n(sline, sv_word_is(st, lc, "round") ? 1 : sv_word_is(st, lc, "square") ? 2 : 0);
    sv_n(sline, sv_word_is(st, lj, "round") ? 1 : sv_word_is(st, lj, "bevel") ? 2 : 0);
    double ml = st->cval[w->src[SI_MITER]].num;
    sv_n(sline, ml >= 1 ? ml : 4);
    sv_n(sline, sv_css_len(st, w->src[SI_DASHOFFSET], sv_diag(w)) * dk);
    vector<double> dash;
    uint32_t dc = w->src[SI_DASHARRAY];
    if (st->cval[dc].unit == CSS_UNIT_TEXT) {
        amb_span sp = st->cval[dc].text;
        vector<char> t;
        append_range(t, text_ptr(st, sp), sp.len);
        size_t i = 0;
        while (i < len(t)) {
            i = sv_skip_sep(data(t), len(t), i);
            if (i >= len(t)) break;
            double v;
            if (!sv_number(data(t), len(t), &i, &v)) { clear(dash); break; }
            if (i < len(t) && at(t, i) == '%') { v = v * sv_diag(w) / 100; i++; }
            else if (i + 1 < len(t) && at(t, i) == 'p' && at(t, i + 1) == 'x') i += 2;
            push_back(dash, v * dk);
        }
    }
    if (dk == 0) clear(dash);
    sv_n(sline, (double)len(dash));
    for (size_t k = 0; k < len(dash); k++) sv_n(sline, at(dash, k));
}

static double sv_measure(const vector<double> *p, double tol, vector<double> *cv, vector<sv_mseg> *ms);

/* what a shape's pathLength scales its dashes and a textPath's offset by (SVG 2, 9.4, as Blink's
   PathLengthScaleFactor has it): its length (known, or p's measured as Skia's SkPathMeasure
   measures it, every contour) over pathLength; 1 without one or for a negative one, 0 for 0 */
static double sv_path_scale(const dom_document *d, uint32_t x, const vector<double> *p, double known) {
    vector<char> v;
    if (!sv_attr_text(d, x, "pathLength", &v)) return 1;
    size_t i = sv_skip_ws(data(v), len(v), 0);
    double pl;
    if (!sv_number(data(v), len(v), &i, &pl) || sv_skip_ws(data(v), len(v), i) != len(v) || pl < 0) return 1;
    if (pl == 0) return 0;
    if (known >= 0) return known / pl;
    vector<double> cv;
    vector<sv_mseg> ms;
    return sv_measure(p, 0.5, &cv, &ms) / pl;
}

/* a shape's lines (SVG 2, 13.3): its fill and its stroke, each its matrix, paints, stroke and
   path (one line for both when the fill comes just before the stroke), and its markers, in its
   paint-order */
static void sv_shape(SB, uint32_t x, const sv_walk *w, sv_mat m, int depth) {
    vector<double> p;
    if (!sv_geometry(d, st, firsts, x, w, &p)) return;
    if (sv_word_is(st, w->src[SI_VISIBILITY], "hidden") || sv_word_is(st, w->src[SI_VISIBILITY], "collapse")) return;
    double bb[4];
    if (!sv_path_bounds(&p, sv_id(), bb)) return;
    uint32_t cur = st->cval[w->src[SI_COLOR]].color;
    sv_paint fp, spt;
    sv_paint_of(d, st, x, w->src[SI_FILL], cur, &fp);
    sv_paint_of(d, st, x, w->src[SI_STROKE], cur, &spt);
    uint32_t fg = 0, frgba = 0, sg = 0, srgba = 0;
    int fk = sv_is(d, x, "line") ? 0 : sv_resolve(SA, &fp, bb, w, &fg, &frgba);
    int sk = sv_resolve(SA, &spt, bb, w, &sg, &srgba);
    double sw = sv_css_len(st, w->src[SI_STROKE_WIDTH], sv_diag(w));
    int marked = sv_is(d, x, "path") || sv_is(d, x, "line") || sv_is(d, x, "polyline") || sv_is(d, x, "polygon");
    if (sw <= 0) sk = 0;
    int order[3];
    sv_paint_order(st, w->src[SI_PAINT_ORDER], order);
    uint32_t c0 = sv_first(firsts, x);
    int fixed = c0 && sv_word_is(st, c0 + CSS_PROP_VECTOR_EFFECT, "non-scaling-stroke");
    /* the stroke's words: its paint's after the fill's */
    vector<char> sline;
    sv_stroke_words(st, w, sw, sk ? sv_path_scale(d, x, &p, -1) : 1, &sline);
    if (fixed) sv_w(&sline, " N");
    for (int q = 0; q < 3; q++) {
        int what = order[q];
        if (what == 2) {
            if (marked) sv_markers(SA, x, w, m, &p, sw, depth);
            continue;
        }
        /* the fill, and the stroke with it when it comes next; or the stroke alone */
        int both = what == 0 && q + 1 < 3 && order[q + 1] == 1;
        int f1 = what == 0 ? fk : 0, s1 = what == 1 || both ? sk : 0;
        if (both) q++;
        if (!f1 && !s1) continue;
        vector<char> line;
        sv_w(&line, "p");
        sv_m(&line, m);
        sv_paint_words(&line, f1, fg, frgba);
        sv_n(&line, st->cval[w->src[SI_FILL_OPACITY]].num);
        sv_n(&line, sv_word_is(st, w->src[SI_FILL_RULE], "evenodd"));
        sv_paint_words(&line, s1, sg, srgba);
        append_range(line, data(sline), len(sline));
        sv_path_out(&line, &p);
        sv_w(&line, "\n");
        append_range(out, data(line), len(line));
    }
}

/* a marker element drawn at a place of a shape (SVG 2, 13.7, as Blink's SVGMarkerPainter
   draws one): moved to the place, turned by its orient (auto: the place's angle;
   auto-start-reverse: a start's turned back; else its angle), scaled by the stroke's width
   unless its markerUnits is userSpaceOnUse, its reference point (refX, refY through its
   viewBox) at the place; its content through its viewBox (and preserveAspectRatio) into its
   markerWidth x markerHeight, clipped to that (not anti-aliased) unless its overflow is
   visible, drawn with the values it inherits in the document. Not drawn: a width or height
   of no area, an empty viewBox, a marker inside itself */
static void sv_marker(SB, uint32_t me, int type, double px, double py, double angle, double sw, const sv_walk *w, sv_mat m, int depth) {
    if (len(*uses) >= 16 || depth > 32) return;
    for (size_t k = 0; k < len(*uses); k++) if (at(*uses, k) == me) return;
    double fs = sv_fs(st, w);
    int g;
    double mw = sv_len_attr(d, me, "markerWidth", w->vw, fs, 3, &g), mh = sv_len_attr(d, me, "markerHeight", w->vh, fs, 3, &g);
    if (!(mw > 0 && mh > 0)) return;
    double vb[4], vp[4] = { 0, 0, mw, mh };
    sv_mat vt = sv_id();
    int hasvb = sv_attr(d, me, "viewBox") != 0;
    if (hasvb) {
        if (!sv_viewbox_of(d, me, vb)) return;
        vt = sv_viewbox(d, me, vb, vp);
    }
    vector<char> v;
    double turn = 0;
    if (sv_attr_text(d, me, "orient", &v)) {
        size_t i = sv_skip_ws(data(v), len(v), 0), e = len(v);
        while (e > i && sv_ws(at(v, e - 1))) e--;
        if (sv_eq_ci(data(v) + i, e - i, "auto")) turn = angle;
        else if (sv_eq_ci(data(v) + i, e - i, "auto-start-reverse")) turn = type == 0 ? angle + 180 : angle;
        else {
            size_t j = i;
            double a;
            if (sv_angle(data(v), e, &j, &a) && j == e) turn = a;
        }
    }
    double scale = sv_attr_is(d, me, "markerUnits", "userSpaceOnUse") ? 1 : sw;
    double rx = sv_len_attr(d, me, "refX", hasvb ? vb[2] : mw, fs, 0, &g), ry = sv_len_attr(d, me, "refY", hasvb ? vb[3] : mh, fs, 0, &g);
    double rmx = sv_ax(vt, rx, ry), rmy = sv_ay(vt, rx, ry);
    double r = turn * M_PI / 180, cr = cos(r), sr = sin(r);
    sv_mat mt = sv_mul(sv_make(cr, sr, -sr, cr, px, py), sv_make(scale, 0, 0, scale, -scale * rmx, -scale * rmy));
    sv_mat mm = sv_mul(m, mt), cm = sv_mul(mm, vt);
    sv_walk root;
    if (!sv_root_walk(st, firsts, me, hasvb ? vb[2] : mw, hasvb ? vb[3] : mh, &root)) return;
    uint32_t c0 = sv_first(firsts, me);
    int clip = !(c0 && st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    if (clip) {
        /* the viewport's clip, aliased: a rule of 2 (non-zero, each px in or out by its centre) */
        sv_w(out, "g 1 1 1\nk");
        sv_m(out, mm);
        sv_w(out, " 2 M 0 0 L");
        sv_n(out, mw);
        sv_w(out, " 0 L");
        sv_n(out, mw);
        sv_n(out, mh);
        sv_w(out, " L 0");
        sv_n(out, mh);
        sv_w(out, " Z\n");
    }
    push_back(uses, me);
    for (uint32_t c = d->node[me].first_child; c; c = d->node[c].next_sibling) sv_node(SA, c, &root, cm, depth + 1);
    resize(uses, len(*uses) - 1);
    if (clip) sv_w(out, "e\n");
}

/* a path's, line's, polyline's or polygon's markers: at each place of its segments the
   marker its marker-start, -mid or -end names for the place's kind (its stroke's width sw) */
static void sv_markers(SB, uint32_t x, const sv_walk *w, sv_mat m, const vector<double> *p, double sw, int depth) {
    uint32_t mk[3];
    for (int k = 0; k < 3; k++) mk[k] = sv_ref_of(d, st, x, w->src[SI_MARKER_START + k], "marker");
    if (!mk[0] && !mk[1] && !mk[2]) return;
    vector<double> feat, pos;
    vector<char> v;
    if (sv_is(d, x, "path")) {
        vector<double> again;
        if (sv_path_text(d, st, firsts, x, &v)) sv_path_data(data(v), len(v), &again, &feat);
    } else sv_feat_lines(p, &feat);
    sv_marker_places(&feat, &pos);
    for (size_t k = 0; k + 3 < len(pos); k += 4) {
        int type = (int)at(pos, k);
        if (mk[type]) sv_marker(SA, mk[type], type, at(pos, k + 1), at(pos, k + 2), at(pos, k + 3), sw, w, m, depth);
    }
}

/* a viewport (a nested svg, or a use's symbol or svg): its rectangle in its parent's user
   space, its viewBox mapped into it, its clip unless its overflow is visible; its children
   walked from the walk w (its own) */
static void sv_viewport(SB, uint32_t x, const sv_walk *w, sv_mat m, double vx, double vy, double vw, double vh, int depth) {
    if (vw <= 0 || vh <= 0) return;
    double vb[4], vp[4] = { vx, vy, vw, vh };
    sv_walk cw = *w;
    sv_mat cm = sv_mul(m, sv_make(1, 0, 0, 1, vx, vy));
    cw.vw = vw;
    cw.vh = vh;
    if (sv_viewbox_of(d, x, vb)) {
        cm = sv_mul(m, sv_viewbox(d, x, vb, vp));
        cw.vw = vb[2];
        cw.vh = vb[3];
    }
    uint32_t c0 = sv_first(firsts, x);
    int clip = !(c0 && st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    if (clip) {
        sv_w(out, "g 1 1 1\nk");
        sv_m(out, m);
        sv_w(out, " 0 M");
        sv_n(out, vx); sv_n(out, vy);
        sv_w(out, " L"); sv_n(out, vx + vw); sv_n(out, vy);
        sv_w(out, " L"); sv_n(out, vx + vw); sv_n(out, vy + vh);
        sv_w(out, " L"); sv_n(out, vx); sv_n(out, vy + vh);
        sv_w(out, " Z\n");
    }
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) sv_node(SA, c, &cw, cm, depth + 1);
    if (clip) sv_w(out, "e\n");
}

/* a use: the element its href names drawn as if it were the use's child, moved by x and y
   (a symbol or an svg a viewport of the use's width and height) */
static void sv_use(SB, uint32_t x, const sv_walk *w, sv_mat m, int depth) {
    uint32_t ref = sv_href(d, x);
    if (!ref || len(*uses) >= 16) return;
    for (size_t k = 0; k < len(*uses); k++) if (at(*uses, k) == ref) return;
    for (uint32_t a = d->node[x].parent; a; a = d->node[a].parent) if (a == ref) return;
    double fs = sv_fs(st, w);
    int g;
    double ux = sv_len_attr(d, x, "x", w->vw, fs, 0, &g), uy = sv_len_attr(d, x, "y", w->vh, fs, 0, &g);
    sv_mat um = sv_mul(m, sv_make(1, 0, 0, 1, ux, uy));
    push_back(uses, ref);
    if (sv_is(d, ref, "symbol") || sv_is(d, ref, "svg")) {
        uint32_t c0 = sv_first(firsts, ref);
        if (!(c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none"))) {
            sv_walk rw;
            sv_inherit(st, firsts, ref, w, &rw);
            int gw, gh;
            double vw = sv_len_attr(d, x, "width", w->vw, fs, 0, &gw), vh = sv_len_attr(d, x, "height", w->vh, fs, 0, &gh);
            if (!gw) vw = sv_is(d, ref, "svg") ? sv_len_attr(d, ref, "width", w->vw, fs, w->vw, &gw) : w->vw;
            if (!gh) vh = sv_is(d, ref, "svg") ? sv_len_attr(d, ref, "height", w->vh, fs, w->vh, &gh) : w->vh;
            sv_viewport(SA, ref, &rw, um, 0, 0, vw, vh, depth + 1);
        }
    } else sv_node(SA, ref, w, um, depth + 1);
    resize(uses, len(*uses) - 1);
}

static void sv_text(SB, uint32_t x, const sv_walk *w, sv_mat m);

/* an element's conditional processing attributes hold (SVG 2, 5.8, as Blink tests them): no
   systemLanguage (Blink matches its tags against its default language, which in the headless
   Chromium the gates are measured in matches none: measured, every tag fails); no
   requiredExtensions but XHTML's and MathML's namespaces (an empty one fails);
   requiredFeatures is not read */
static int sv_tests_hold(const dom_document *d, uint32_t x) {
    vector<char> v;
    if (sv_attr(d, x, "systemLanguage")) return 0;
    if (sv_attr_text(d, x, "requiredExtensions", &v)) {
        size_t i = 0;
        int any = 0;
        while (i < len(v)) {
            i = sv_skip_ws(data(v), len(v), i);
            size_t b = i;
            while (i < len(v) && !sv_ws(at(v, i))) i++;
            if (i == b) break;
            any = 1;
            int xhtml = i - b == 28 && memcmp(data(v) + b, "http://www.w3.org/1999/xhtml", 28) == 0;
            int mathml = i - b == 34 && memcmp(data(v) + b, "http://www.w3.org/1998/Math/MathML", 34) == 0;
            if (!xhtml && !mathml) return 0;
        }
        if (!any) return 0;
    }
    return 1;
}

static void sv_image(SB, uint32_t x, const sv_walk *w, sv_mat m);

/* preserveAspectRatio's value on x: its alignment along each axis (0 min, 1 mid, 2 max), slice,
   and none (initial: xMidYMid meet) */
static void sv_par_of(const dom_document *d, uint32_t x, int *ax, int *ay, int *slice, int *none) {
    vector<char> v;
    *ax = *ay = 1;
    *slice = *none = 0;
    if (!sv_attr_text(d, x, "preserveAspectRatio", &v)) return;
    size_t i = sv_skip_ws(data(v), len(v), 0), n = len(v);
    if (i + 5 <= n && memcmp(data(v) + i, "defer", 5) == 0) i = sv_skip_ws(data(v), n, i + 5);
    if (i + 4 <= n && memcmp(data(v) + i, "none", 4) == 0) { *none = 1; i += 4; }
    else if (i + 8 <= n && data(v)[i] == 'x' && data(v)[i + 4] == 'Y') {
        const char *q = data(v) + i;
        *ax = memcmp(q + 1, "Min", 3) == 0 ? 0 : memcmp(q + 1, "Mid", 3) == 0 ? 1 : 2;
        *ay = memcmp(q + 5, "Min", 3) == 0 ? 0 : memcmp(q + 5, "Mid", 3) == 0 ? 1 : 2;
        i += 8;
    }
    i = sv_skip_ws(data(v), n, i);
    if (i + 5 <= n && memcmp(data(v) + i, "slice", 5) == 0) *slice = 1;
}

/* an image element's line (SVG 2, 12.3): i, its matrix, its viewport (x, y, width and height,
   each a geometry property, an auto one 1 in its flag after: the picture's natural size),
   preserveAspectRatio (alignments, slice, none), whether it clips to its viewport (its
   overflow, hidden unless declared visible), and the element, whose resource the loader
   holds; the picture is placed when it is drawn. Nothing for a width or height of no area */
static void sv_image(SB, uint32_t x, const sv_walk *w, sv_mat m) {
    if (sv_word_is(st, w->src[SI_VISIBILITY], "hidden") || sv_word_is(st, w->src[SI_VISIBILITY], "collapse")) return;
    double fs = sv_fs(st, w);
    int g, gw, gh;
    double ix = sv_geo(d, st, firsts, x, CSS_PROP_X, "x", w->vw, fs, 0, &g), iy = sv_geo(d, st, firsts, x, CSS_PROP_Y, "y", w->vh, fs, 0, &g);
    double iw = sv_geo(d, st, firsts, x, CSS_PROP_WIDTH, "width", w->vw, fs, 0, &gw), ih = sv_geo(d, st, firsts, x, CSS_PROP_HEIGHT, "height", w->vh, fs, 0, &gh);
    if ((gw && iw <= 0) || (gh && ih <= 0)) return;
    int ax, ay, slice, none;
    sv_par_of(d, x, &ax, &ay, &slice, &none);
    uint32_t c0 = sv_first(firsts, x);
    int clip = !(c0 && st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    sv_w(out, "i");
    sv_m(out, m);
    sv_n(out, ix);
    sv_n(out, iy);
    sv_n(out, gw ? iw : 0);
    sv_n(out, gh ? ih : 0);
    sv_n(out, !gw);
    sv_n(out, !gh);
    sv_n(out, ax);
    sv_n(out, ay);
    sv_n(out, slice);
    sv_n(out, none);
    sv_n(out, clip);
    sv_n(out, x);
    sv_w(out, "\n");
}

/* the walk is drawing a mask's content: of the elements it is expanding, the last mask, pattern
   or marker is a mask (a pattern's tile in a mask is its own drawing, its shape the mask's) */
static int sv_in_mask(const dom_document *d, const vector<uint32_t> *uses) {
    for (size_t k = len(*uses); k > 0; k--) {
        uint32_t u = at(*uses, k - 1);
        if (sv_is(d, u, "mask")) return 1;
        if (sv_is(d, u, "pattern") || sv_is(d, u, "marker")) return 0;
    }
    return 0;
}

static void sv_node(SB, uint32_t x, const sv_walk *pw, sv_mat m, int depth) {
    if (depth > 64 || ++*work > SV_WORK || d->node[x].kind != DOM_NODE_KIND_ELEMENT || d->node[x].ns != DOM_NAMESPACE_SVG) return;
    uint32_t c0 = sv_first(firsts, x);
    if (c0 && sv_word_is(st, c0 + CSS_PROP_DISPLAY, "none")) return;
    int shape = sv_is(d, x, "rect") || sv_is(d, x, "circle") || sv_is(d, x, "ellipse") || sv_is(d, x, "line") || sv_is(d, x, "polyline") ||
                sv_is(d, x, "polygon") || sv_is(d, x, "path");
    int group = sv_is(d, x, "g") || sv_is(d, x, "a") || sv_is(d, x, "switch");
    int nested = sv_is(d, x, "svg"), use = sv_is(d, x, "use"), text = sv_is(d, x, "text"), image = sv_is(d, x, "image");
    if (!shape && !group && !nested && !use && !text && !image) return;
    if (!sv_tests_hold(d, x)) return;
    sv_walk w;
    sv_inherit(st, firsts, x, pw, &w);
    sv_mat mm = sv_mul(m, sv_transform_of(d, st, firsts, x, &w));
    double op = c0 ? st->cval[c0 + CSS_PROP_OPACITY].num : 1;
    if (op <= 0) return;
    uint32_t cp = c0 ? sv_clip_of(d, st, x, c0 + CSS_PROP_CLIP_PATH) : 0;
    /* its mask (a url() that names no mask ignored, as Chromium ignores it) and its filter's list */
    uint32_t mk = c0 ? sv_ref_of(d, st, x, c0 + CSS_PROP_MASK_IMAGE, "mask") : 0;
    uint32_t fcv = c0 && st->cval[c0 + CSS_PROP_FILTER].unit == CSS_UNIT_TEXT ? c0 + CSS_PROP_FILTER : 0;
    int grouped = sv_group_open(SA, x, cp, mk, fcv, op, pw, &w, mm, depth);
    if (grouped < 0) return;
    /* in a mask's content, a shape, text or image whose color-interpolation is linearRGB is
       painted in linearRGB: a group of its own made linear as it ends (as Blink paints a mask's
       content, its paint's colours made linear; measured, element by element) */
    int lin = (shape || text || image) && c0 && sv_word_is(st, c0 + CSS_PROP_COLOR_INTERPOLATION, "linearrgb") && sv_in_mask(d, uses);
    if (lin) {
        sv_w(out, "g 1 0 0 0 0 1\n");
        grouped++;
    }
    if (shape) sv_shape(SA, x, &w, mm, depth);
    else if (group) {
        for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
            /* a switch draws its first SVG child element whose tests hold, as Blink takes it:
               one that is never drawn (a desc, a title) too, so then nothing */
            if (sv_is(d, x, "switch") && d->node[c].kind == DOM_NODE_KIND_ELEMENT && d->node[c].ns == DOM_NAMESPACE_SVG) {
                if (!sv_tests_hold(d, c)) continue;
                sv_node(SA, c, &w, mm, depth + 1);
                break;
            }
            sv_node(SA, c, &w, mm, depth + 1);
        }
    } else if (nested) {
        double fs = sv_fs(st, &w);
        int g;
        double vx = sv_geo(d, st, firsts, x, CSS_PROP_X, "x", pw->vw, fs, 0, &g), vy = sv_geo(d, st, firsts, x, CSS_PROP_Y, "y", pw->vh, fs, 0, &g);
        double vw = sv_geo(d, st, firsts, x, CSS_PROP_WIDTH, "width", pw->vw, fs, pw->vw, &g);
        double vh = sv_geo(d, st, firsts, x, CSS_PROP_HEIGHT, "height", pw->vh, fs, pw->vh, &g);
        sv_viewport(SA, x, &w, mm, vx, vy, vw, vh, depth);
    } else if (use) sv_use(SA, x, &w, mm, depth);
    else if (image) sv_image(SA, x, &w, mm);
    else sv_text(SA, x, &w, mm);
    for (int k = 0; k < grouped; k++) sv_w(out, "e\n");
}

/* ---- text (SVG 2, 11): its glyphs placed ---- */

/* a text's characters, each with what places and draws it: its code point; the element it is
   in (its style's); its x, y, dx, dy and rotate as the lists set them (set: bit k for each of
   the five) */
typedef struct sv_char { uint32_t cp, el, set; double x, y, dx, dy, rot; } sv_char;

/* the characters of x's text nodes and tspans into cs, white space collapsed as CSS's normal
   collapses it (a run of spaces, tabs and newlines one space, none at the text's start);
   each element's own walk into ws (its index in els, with its style) */
static void sv_text_chars(const dom_document *d, uint32_t x, vector<sv_char> *cs, vector<uint32_t> *els, int *space, int depth) {
    if (depth > 32) return;
    push_back(els, x);
    uint32_t ei = (uint32_t)len(*els) - 1;
    for (uint32_t c = d->node[x].first_child; c; c = d->node[c].next_sibling) {
        if (d->node[c].kind == DOM_NODE_KIND_TEXT) {
            amb_span sp = d->node[c].data;
            vector<char> t;
            append_range(t, text_ptr(d, sp), sp.len);
            size_t i = 0;
            while (i < len(t)) {
                uint32_t cp = amb_utf8_next(data(t), len(t), &i);
                if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
                    if (*space) continue;
                    *space = 1;
                    cp = ' ';
                } else *space = 0;
                sv_char ch;
                ch.cp = cp;
                ch.el = ei;
                ch.set = 0;
                ch.x = ch.y = ch.dx = ch.dy = ch.rot = 0;
                push_back(cs, ch);
            }
        } else if (sv_is(d, c, "tspan") || sv_is(d, c, "a") || sv_is(d, c, "textPath")) sv_text_chars(d, c, cs, els, space, depth + 1);
    }
}

/* an element's x, y, dx or dy list set on its characters from the first, k (0 x, 1 y, 2 dx,
   3 dy), for those of its own and its descendants' (the descendants' set after) */
static void sv_text_list(const dom_document *d, uint32_t x, const char *name, int k, uint32_t ei, const vector<uint32_t> *els,
                         vector<sv_char> *cs, double basis, double fs) {
    vector<char> v;
    if (!sv_attr_text(d, x, name, &v)) return;
    /* the element's characters: those whose element is it or below it */
    size_t i = 0, ci = 0, n = len(*cs);
    while (ci < n) {
        uint32_t e = at(*cs, ci).el;
        int mine = 0;
        for (uint32_t a = at(*els, e); a; a = d->node[a].parent) if (a == x) { mine = 1; break; }
        if (mine) break;
        ci++;
    }
    (void)ei;
    double val = 0;
    int got = 0;
    while (ci < n) {
        if (i < len(v)) i = sv_skip_sep(data(v), len(v), i);
        if (i >= len(v)) {
            /* the list spent: a rotate's last value holds for the element's other characters */
            if (k != 4 || !got) break;
        } else {
            size_t b = i;
            while (i < len(v) && !sv_ws(at(v, i)) && at(v, i) != ',') i++;
            int ok;
            if (k == 4) {
                size_t j = b;
                ok = sv_number(data(v), i, &j, &val) && j == i;
            } else val = sv_length(data(v) + b, i - b, basis, fs, &ok);
            if (!ok) break;
            got = 1;
        }
        uint32_t e = at(*cs, ci).el;
        int mine = 0;
        for (uint32_t a = at(*els, e); a; a = d->node[a].parent) if (a == x) { mine = 1; break; }
        if (!mine) break;
        sv_char ch = at(*cs, ci);
        if (k == 0) ch.x = val;
        else if (k == 1) ch.y = val;
        else if (k == 2) ch.dx = val;
        else if (k == 3) ch.dy = val;
        else ch.rot = val;
        ch.set |= 1u << k;
        (*cs)[ci] = ch;
        ci++;
    }
}

/* the built-in face a walk's font names: its family, weight and style (italic or oblique the
   family's italic face) */
static int sv_face(const css_style *st, const sv_walk *w) {
    uint32_t fc = w->src[SI_FONT_FAMILY], sc = w->src[SI_FONT_STYLE];
    const char *fam = st->cval[fc].word ? names_ptr(st, st->cval[fc].word) : "serif";
    int bold = st->cval[w->src[SI_FONT_WEIGHT]].num >= 600;
    int italic = sv_word_is(st, sc, "italic") || sv_word_is(st, sc, "oblique");
    int face = amb_face_named(fam, bold, italic);
    return face < 0 ? AMB_FACE_SERIF + (bold ? 1 : 0) + (italic ? 2 : 0) : face;
}

static void sv_path_map(const vector<double> *p, sv_mat m, vector<double> *out);

/* a path measured as Skia's SkContourMeasure measures one (sv_meas_*, its tolerance tol), its
   contours one after another (a move adds no length): its curves into cv (each its kind and
   four points), its pieces into ms; its length */
static double sv_measure(const vector<double> *p, double tol, vector<double> *cv, vector<sv_mseg> *ms) {
    clear(*cv);
    clear(*ms);
    size_t i = 0, n = len(*p);
    double sx = 0, sy = 0, cx = 0, cy = 0, dist = 0;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_M) {
            sx = cx = at(*p, i + 1);
            sy = cy = at(*p, i + 2);
            i += 3;
            continue;
        }
        double q[8] = { cx, cy, 0, 0, 0, 0, 0, 0 };
        int kind = c, k = sv_args(c);
        if (c == SV_Z) { kind = SV_L; q[2] = sx; q[3] = sy; }
        else for (int j = 0; j < k; j++) q[2 + j] = at(*p, i + 1 + (size_t)j);
        uint32_t ci = (uint32_t)len(*cv);
        push_back(cv, (double)kind);
        for (int j = 0; j < 8; j++) push_back(cv, q[j]);
        if (kind == SV_L) sv_mseg_add(ms, &dist, hypot(q[2] - q[0], q[3] - q[1]), ci, SV_TMAX);
        else if (kind == SV_Q) sv_meas_bez(q, 1, &dist, 0, SV_TMAX, ci, tol, ms);
        else if (kind == SV_K) sv_meas_arc(q, &dist, 0, q[0], q[1], SV_TMAX, q[6], q[7], ci, tol, ms);
        else sv_meas_bez(q, 0, &dist, 0, SV_TMAX, ci, tol, ms);
        if (c == SV_Z) { cx = sx; cy = sy; i++; }
        else { cx = at(*p, i + (size_t)k - 1); cy = at(*p, i + (size_t)k); i += (size_t)k + 1; }
    }
    return dist;
}
/* the point a distance along a measured path falls on, and the path's direction there (degrees,
   its curve's derivative, a chord's where that has none): 0 when the path has no length */
static int sv_pos_tan(const vector<double> *cv, const vector<sv_mseg> *ms, double dist, double *x, double *y, double *ang) {
    size_t m1 = len(*ms);
    if (!m1) return 0;
    double t;
    size_t s = sv_dist_at(ms, 0, m1, dist, &t);
    uint32_t ci = at(*ms, s).ci;
    int kind = (int)at(*cv, ci);
    double q[8];
    for (int k = 0; k < 8; k++) q[k] = at(*cv, ci + 1 + (size_t)k);
    sv_bez_pt(q, kind == SV_K ? SV_C : kind, t, x, y);
    double dx, dy, u = 1 - t;
    if (kind == SV_L) { dx = q[2] - q[0]; dy = q[3] - q[1]; }
    else if (kind == SV_Q) { dx = 2 * u * (q[2] - q[0]) + 2 * t * (q[4] - q[2]); dy = 2 * u * (q[3] - q[1]) + 2 * t * (q[5] - q[3]); }
    else {
        dx = 3 * u * u * (q[2] - q[0]) + 6 * u * t * (q[4] - q[2]) + 3 * t * t * (q[6] - q[4]);
        dy = 3 * u * u * (q[3] - q[1]) + 6 * u * t * (q[5] - q[3]) + 3 * t * t * (q[7] - q[5]);
    }
    if (dx == 0 && dy == 0) {
        dx = q[kind == SV_L ? 2 : kind == SV_Q ? 4 : 6] - q[0];
        dy = q[kind == SV_L ? 3 : kind == SV_Q ? 5 : 7] - q[1];
    }
    *ang = atan2(dy, dx) * 180 / M_PI;
    return 1;
}

/* a shaped glyph of a character (amb_shape's): its glyph and the face that draws it, its flags,
   and its pen's offset from the character's place (across, and down from the baseline) */
typedef struct sv_sglyph { uint32_t gid; uint16_t face, flags; double ox, oy; } sv_sglyph;

/* a glyph's outline (amb_glyph_path_id's) placed in the text's space: its character's pen at
   (gx, gy), the glyph at its offset from it, turned by rot degrees about the pen, scaled across
   by hs; appended to p */
static void sv_glyph_outline(const amb_fonts *f, const sv_sglyph *sg, double fs, int italic, double gx, double gy, double rot, double hs,
                             vector<double> *p) {
    vector<double> g;
    if ((sg->flags & AMB_SHAPED_HIDDEN) || !amb_glyph_path_id(f, sg->face, sg->gid, fs, italic, &g)) return;
    double r = rot * M_PI / 180, c = cos(r), s = sin(r);
    sv_mat t = sv_mul(sv_make(c, s, -s, c, gx, gy), sv_make(hs, 0, 0, 1, sg->ox * hs, sg->oy));
    size_t i = 0;
    while (i < len(g)) {
        int cmd = (int)at(g, i);
        int k = cmd == SV_C ? 6 : cmd == SV_Q ? 4 : cmd == SV_Z ? 0 : 2;
        push_back(p, (double)cmd);
        for (int q = 0; q < k; q += 2) {
            double x = at(g, i + 1 + (size_t)q), y = at(g, i + 2 + (size_t)q);
            push_back(p, sv_ax(t, x, y));
            push_back(p, sv_ay(t, x, y));
        }
        i += (size_t)k + 1;
    }
}

/* how far down the alphabetic baseline a dominant-baseline moves the glyphs (as Blink takes the
   face's metrics, its ascent and descent rounded): middle half the x-height, central halfway
   between the ascent and the descent, mathematical half the ascent, hanging 0.8 of it,
   text-before-edge all of it, ideographic and text-after-edge the descent up */
static double sv_baseline_shift(const css_style *st, const amb_fonts *f, const sv_walk *w, int face, double fs) {
    uint32_t b = w->src[SI_BASELINE];
    double asc, desc, gap;
    amb_face_metrics(f, face, fs, &asc, &desc, &gap);
    if (sv_word_is(st, b, "middle")) return amb_x_height(face) * fs / 2;
    if (sv_word_is(st, b, "central")) return (asc - desc) / 2;
    if (sv_word_is(st, b, "mathematical")) return asc / 2;
    if (sv_word_is(st, b, "hanging")) return asc * 0.8;
    if (sv_word_is(st, b, "text-before-edge")) return asc;
    if (sv_word_is(st, b, "ideographic") || sv_word_is(st, b, "text-after-edge")) return -desc;
    return 0;
}

/* the textPath a character's element is in (itself or an ancestor below the text x), or 0 */
static uint32_t sv_text_path_of(const dom_document *d, uint32_t e, uint32_t x) {
    for (uint32_t a = e; a && a != x; a = d->node[a].parent) if (sv_is(d, a, "textPath")) return a;
    return 0;
}

/* a text element's glyphs (SVG 2, 11): its characters placed by their x, y, dx and dy lists and
   their advances (kerning, letter- and word-spacing), each element's textLength then met
   (spacing: the difference spread between its characters; spacingAndGlyphs: its characters'
   places and glyphs scaled across), each chunk (from an absolutely placed character) moved by
   its text-anchor; a textPath's characters set along its path (its href's element's geometry
   and transform; startOffset, a percentage of its length; its anchor), each glyph's middle
   on the path and the glyph turned to its direction there, one past either end not drawn;
   each glyph turned by its rotate (a list's last value holding for the element's later
   characters) and moved by its dominant-baseline: each character's element (its index in
   the elements, whose walks are wso), place, advance, turn, scale across, and whether it is
   dropped or on a path, into the vectors given. 0 when it has no character */
static void sv_utf8(vector<char> *out, uint32_t cp);

/* a text's characters shaped (as Blink shapes an SVG text's runs: HarfBuzz's shaping for
   Chromium, amb_shape): runs of one face, size, letter-spacing and direction (a right-to-left
   script's letters, and the neutral characters between two of them, right to left), none across
   an absolutely placed character (a chunk's start, where Blink splits its items) or a textPath's
   edge, in the whole text as context; each cluster's glyphs given to the character it begins at
   (gl, from gf[i]), its advance theirs and the spacing's -- each of its code points' letter-
   spacing, a space's word-spacing -- the others (a ligature's later letters, marks) none; a
   right-to-left run's clusters in visual order, each character's offset from the run's left
   (vo) and the run's width (rw, at its first character) */
static void sv_text_shape(const css_style *st, const amb_fonts *f, const vector<sv_char> *cs, const vector<sv_walk> *ws, const vector<uint32_t> *tp,
                          vector<double> *adv, vector<sv_sglyph> *gl, vector<uint32_t> *gf, vector<uint32_t> *gn, vector<uint8_t> *rtl,
                          vector<double> *vo, vector<double> *rw, vector<uint8_t> *rs) {
    size_t n = len(*cs);
    vector<char> tx;
    vector<uint32_t> bo, bchar;
    for (size_t i = 0; i < n; i++) {
        push_back(bo, (uint32_t)len(tx));
        sv_utf8(&tx, at(*cs, i).cp);
    }
    push_back(bo, (uint32_t)len(tx));
    resize(bchar, len(tx) + 1);
    for (size_t i = 0; i < n; i++) bchar[at(bo, i)] = (uint32_t)i;
    /* each character's direction: strong (a script's letter; a mark its base's), or a neutral's
       between two right-to-left ones */
    vector<uint8_t> strong;
    resize(strong, n);
    clear(*rtl);
    resize(rtl, n);
    for (size_t i = 0; i < n; i++) {
        int sc = amb_script_of(at(*cs, i).cp);
        strong[i] = sc == AMB_SC_INHERITED ? (i ? at(strong, i - 1) : 2) : sc == AMB_SC_COMMON || sc == 0 ? 2 : amb_script_rtl(sc) ? 1 : 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (at(strong, i) != 2) { (*rtl)[i] = at(strong, i); continue; }
        size_t a = i, b = i;
        while (b < n && at(strong, b) == 2) b++;
        int r = a > 0 && at(strong, a - 1) == 1 && b < n && at(strong, b) == 1;
        for (size_t q = a; q < b; q++) (*rtl)[q] = (uint8_t)r;
        i = b - 1;
    }
    clear(*adv);
    clear(*vo);
    clear(*rw);
    clear(*gf);
    clear(*gn);
    resize(adv, n);
    resize(vo, n);
    resize(rw, n);
    resize(gf, n);
    resize(gn, n);
    clear(*rs);
    resize(rs, n);
    vector<uint8_t> start;
    resize(start, n);
    vector<amb_shaped> sh;
    size_t a = 0;
    while (a < n) {
        sv_walk wa = at(*ws, at(*cs, a).el);
        int face = sv_face(st, &wa);
        double fs = sv_fs(st, &wa), ls = st->cval[wa.src[SI_LETTER_SPACING]].unit == CSS_UNIT_PX ? st->cval[wa.src[SI_LETTER_SPACING]].num : 0;
        size_t b = a + 1;
        while (b < n) {
            sv_walk wb = at(*ws, at(*cs, b).el);
            double lb = st->cval[wb.src[SI_LETTER_SPACING]].unit == CSS_UNIT_PX ? st->cval[wb.src[SI_LETTER_SPACING]].num : 0;
            if ((at(*cs, b).set & 3) || at(*tp, b) != at(*tp, a) || at(*rtl, b) != at(*rtl, a) || sv_face(st, &wb) != face || sv_fs(st, &wb) != fs ||
                lb != ls)
                break;
            b++;
        }
        clear(sh);
        int flags = (at(*rtl, a) ? AMB_SHAPE_RTL : 0) | (ls != 0 ? AMB_SHAPE_NOLIGA : 0);
        amb_shape(f, face, data(tx), len(tx), at(bo, a), at(bo, b), flags, &sh);
        /* the clusters' starts first, so that each knows its characters */
        for (size_t k = 0; k < len(sh); k++) {
            uint32_t cl = at(sh, k).cluster;
            size_t c = cl < len(bchar) ? at(bchar, cl) : a;
            start[c >= a && c < b ? c : a] = 1;
        }
        start[a] = 1;
        /* each cluster in the order shaped (visual): its glyphs, from the pen at its start */
        double pen = 0;
        size_t k = 0;
        while (k < len(sh)) {
            uint32_t cl = at(sh, k).cluster;
            size_t c = cl < len(bchar) ? at(bchar, cl) : a;
            if (c < a || c >= b) c = a;
            double p0 = pen;
            (*gf)[c] = (uint32_t)len(*gl);
            while (k < len(sh) && at(sh, k).cluster == cl) {
                amb_shaped g = at(sh, k);
                double sc = amb_face_scale(f, g.face, fs);
                sv_sglyph sg;
                sg.gid = g.gid;
                sg.face = g.face;
                sg.flags = g.flags;
                sg.ox = pen - p0 + g.dx * sc;
                sg.oy = -g.dy * sc;
                push_back(gl, sg);
                pen += g.ax * sc;
                k++;
            }
            (*gn)[c] = (uint32_t)(len(*gl) - at(*gf, c));
            /* its spacing: its characters', to the next cluster's start (logically) */
            double sp = 0;
            for (size_t q = c; q < b && (q == c || !at(start, q)); q++) {
                sv_walk wq = at(*ws, at(*cs, q).el);
                sp += ls;
                if (at(*cs, q).cp == ' ' && st->cval[wq.src[SI_WORD_SPACING]].unit == CSS_UNIT_PX) sp += st->cval[wq.src[SI_WORD_SPACING]].num;
            }
            (*adv)[c] = at(*adv, c) + pen - p0 + sp;
            (*vo)[c] = p0;
            pen += sp;
        }
        /* a cluster's later characters at its start's offset; the run's width at its first */
        for (size_t q = a + 1; q < b; q++)
            if (!at(start, q)) (*vo)[q] = at(*vo, q - 1);
        (*rw)[a] = pen;
        (*rs)[a] = 1;
        a = b;
    }
}

static int sv_text_layout(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t x, const sv_walk *w,
                          vector<sv_char> *cso, vector<uint32_t> *elso, vector<sv_walk> *wso, vector<double> *gxo, vector<double> *gyo,
                          vector<double> *advo, vector<double> *roto, vector<double> *hso, vector<uint8_t> *hideo, vector<uint8_t> *onpatho,
                          vector<sv_sglyph> *glo, vector<uint32_t> *gfo, vector<uint32_t> *gno) {
    vector<sv_char> cs;
    vector<uint32_t> els;
    int space = 1;
    sv_text_chars(d, x, &cs, &els, &space, 0);
    while (len(cs) && at(cs, len(cs) - 1).cp == ' ') resize(cs, len(cs) - 1);
    if (!len(cs)) return 0;
    /* each element's walk, its parent's first */
    size_t ne = len(els);
    vector<sv_walk> ws;
    for (size_t k = 0; k < ne; k++) {
        uint32_t e = at(els, k);
        sv_walk pw = *w, cw;
        if (k > 0) {
            for (size_t q = 0; q < k; q++) if (at(els, q) == d->node[e].parent) pw = at(ws, q);
            sv_inherit(st, firsts, e, &pw, &cw);
        } else cw = *w;
        push_back(ws, cw);
    }
    /* the lists: the text's, then each tspan's over its own characters */
    for (size_t k = 0; k < ne; k++) {
        uint32_t e = at(els, k);
        sv_walk ew = at(ws, k);
        double fs = sv_fs(st, &ew);
        sv_text_list(d, e, "x", 0, (uint32_t)k, &els, &cs, w->vw, fs);
        sv_text_list(d, e, "y", 1, (uint32_t)k, &els, &cs, w->vh, fs);
        sv_text_list(d, e, "dx", 2, (uint32_t)k, &els, &cs, w->vw, fs);
        sv_text_list(d, e, "dy", 3, (uint32_t)k, &els, &cs, w->vh, fs);
        sv_text_list(d, e, "rotate", 4, (uint32_t)k, &els, &cs, 0, fs);
    }
    /* the pen: each character's place and advance */
    size_t n = len(cs);
    vector<double> gx, gy, adv, rot, hs;
    vector<uint8_t> hide, onpath;
    resize(gx, n);
    resize(gy, n);
    resize(adv, n);
    resize(rot, n);
    resize(hs, n);
    resize(hide, n);
    resize(onpath, n);
    /* each character's textPath (none: 0), and the text shaped (sv_text_shape) */
    vector<uint32_t> tps, gf, gn;
    vector<sv_sglyph> gl;
    vector<uint8_t> rtl, rs;
    vector<double> vo, rw;
    for (size_t i = 0; i < n; i++) push_back(tps, sv_text_path_of(d, at(els, at(cs, i).el), x));
    sv_text_shape(st, f, &cs, &ws, &tps, &adv, &gl, &gf, &gn, &rtl, &vo, &rw, &rs);
    /* a cluster's later characters (a ligature's, marks) are no typographic characters of their
       own: placed with it, never drawn */
    vector<uint8_t> mid;
    resize(mid, n);
    for (size_t i = 1; i < n; i++) mid[i] = at(gn, i) == 0 && at(adv, i) == 0 && !at(rs, i);
    /* the pen: a left-to-right run's characters one after another, a right-to-left run's each
       at its cluster's offset from where the run begins, the run's width on after it; dx and dy
       moving the pen at every character */
    double px = 0, py = 0;
    for (size_t i = 0; i < n; i++) {
        sv_char ch = at(cs, i);
        if (ch.set & 1) px = ch.x;
        if (ch.set & 2) py = ch.y;
        if (ch.set & 4) px += ch.dx;
        if (ch.set & 8) py += ch.dy;
        int r1 = at(rtl, i);
        gx[i] = px + (r1 ? at(vo, i) : 0);
        gy[i] = py;
        rot[i] = (ch.set & 16) ? ch.rot : 0;
        hs[i] = 1;
        if (!r1) px += at(adv, i);
        else if (i + 1 >= n || at(rs, i + 1)) {
            /* the run's end: its width on from its first character */
            size_t a0 = i;
            while (a0 > 0 && !at(rs, a0)) a0--;
            px += at(rw, a0);
        }
    }
    /* a middle character takes its cluster's turn */
    for (size_t i = 1; i < n; i++) if (at(mid, i)) rot[i] = at(rot, i - 1);
    /* each element's textLength, the outer's first: its characters' run made that long, the
       characters after it in their chunk moved on by the difference */
    for (size_t k = 0; k < ne; k++) {
        uint32_t e = at(els, k);
        sv_walk ew = at(ws, k);
        int g;
        double tl = sv_len_attr(d, e, "textLength", w->vw, sv_fs(st, &ew), 0, &g);
        if (!g || tl < 0) continue;
        size_t a = n, b = 0;
        for (size_t i = 0; i < n; i++) {
            int mine = 0;
            for (uint32_t q = at(els, at(cs, i).el); q; q = d->node[q].parent) if (q == e) { mine = 1; break; }
            if (mine) { if (a == n) a = i; b = i; }
        }
        if (a == n) continue;
        double lo = 1e300, hi = -1e300;
        for (size_t i = a; i <= b; i++) { lo = fmin(lo, at(gx, i)); hi = fmax(hi, at(gx, i) + at(adv, i)); }
        double natural = hi - lo, delta = tl - natural;
        if (sv_attr_is(d, e, "lengthAdjust", "spacingAndGlyphs")) {
            double k2 = natural > 0 ? tl / natural : 1;
            for (size_t i = a; i <= b; i++) {
                gx[i] = lo + (at(gx, i) - lo) * k2;
                adv[i] = at(adv, i) * k2;
                hs[i] = at(hs, i) * k2;
            }
        } else {
            /* the difference spread between its typographic characters (a ligature one) */
            size_t m = 0, kk = 0;
            double add = 0;
            for (size_t i = a; i <= b; i++) if (!at(mid, i)) m++;
            for (size_t i = a; i <= b; i++) {
                if (!at(mid, i)) { add = m > 1 ? (double)kk * delta / (double)(m - 1) : 0; kk++; }
                gx[i] = at(gx, i) + add;
            }
        }
        for (size_t i = b + 1; i < n && !(at(cs, i).set & 1); i++) gx[i] = at(gx, i) + delta;
    }
    /* the chunks, each moved by its anchor (its first character's element's): its extent's
       middle or end at where it begins; a textPath's characters are anchored along their path */
    size_t chunk = 0;
    for (size_t i = 1; i <= n; i++) {
        if (i < n && !(at(cs, i).set & 3)) continue;
        uint32_t tp = sv_text_path_of(d, at(els, at(cs, chunk).el), x);
        sv_walk aw = at(ws, at(cs, chunk).el);
        int an = sv_word_is(st, aw.src[SI_ANCHOR], "middle") ? 1 : sv_word_is(st, aw.src[SI_ANCHOR], "end") ? 2 : 0;
        if (an && !tp) {
            double x0 = at(gx, chunk) - (at(rtl, chunk) ? at(vo, chunk) : 0), lo = 1e300, hi = -1e300;
            for (size_t q = chunk; q < i; q++) { lo = fmin(lo, at(gx, q)); hi = fmax(hi, at(gx, q) + at(adv, q)); }
            double shift = an == 1 ? (lo + hi) / 2 - x0 : hi - x0;
            for (size_t q = chunk; q < i; q++) gx[q] = at(gx, q) - shift;
        }
        chunk = i;
    }
    /* the textPaths' characters on their paths */
    vector<double> cvv;
    vector<sv_mseg> msv;
    size_t i0 = 0;
    while (i0 < n) {
        uint32_t tp = sv_text_path_of(d, at(els, at(cs, i0).el), x);
        size_t i1 = i0 + 1;
        while (i1 < n && sv_text_path_of(d, at(els, at(cs, i1).el), x) == tp) i1++;
        if (tp) {
            /* its path: its own path attribute's (in the text's space; Chromium's over its href,
               measured), else the path element its href names (another shape none, as Blink's
               LayoutSVGTextPath takes it: measured), under that element's transform */
            uint32_t pe = 0;
            vector<double> pp, mp, feat;
            vector<char> pa;
            int ok = 0;
            if (sv_attr_text(d, tp, "path", &pa) && sv_path_data(data(pa), len(pa), &mp, &feat) && len(mp)) ok = 1;
            else {
                clear(mp);
                pe = sv_href(d, tp);
                sv_walk pw2;
                ok = pe && sv_is(d, pe, "path") && sv_root_walk(st, firsts, pe, w->vw, w->vh, &pw2) && sv_geometry(d, st, firsts, pe, &pw2, &pp);
                if (ok) sv_path_map(&pp, sv_transform_of(d, st, firsts, pe, &pw2), &mp);
            }
            double plen = ok ? sv_measure(&mp, 0.5, &cvv, &msv) : 0;
            /* its start: startOffset (a percentage of the path's length; a length scaled by the
               path's pathLength, as Blink's LayoutSVGTextPath scales it), its anchor's shift */
            size_t tpi = 0;
            for (size_t k = 0; k < ne; k++) if (at(els, k) == tp) tpi = k;
            sv_walk tw = at(ws, tpi);
            int g;
            double so = sv_len_attr(d, tp, "startOffset", plen, sv_fs(st, &tw), 0, &g);
            vector<char> sov;
            if (pe && ok && sv_attr_text(d, tp, "startOffset", &sov) && len(sov) && at(sov, len(sov) - 1) != '%') so *= sv_path_scale(d, pe, &pp, plen);
            int an = sv_word_is(st, tw.src[SI_ANCHOR], "middle") ? 1 : sv_word_is(st, tw.src[SI_ANCHOR], "end") ? 2 : 0;
            double x0 = 1e300, x1 = -1e300;
            for (size_t i = i0; i < i1; i++) { x0 = fmin(x0, at(gx, i)); x1 = fmax(x1, at(gx, i) + at(adv, i)); }
            double width = x1 - x0, base = so - (an == 1 ? width / 2 : an == 2 ? width : 0);
            for (size_t i = i0; i < i1; i++) {
                double mid = base + at(gx, i) - x0 + at(adv, i) / 2, qx, qy, ang;
                if (!ok || plen <= 0 || mid < 0 || mid > plen || !sv_pos_tan(&cvv, &msv, mid, &qx, &qy, &ang)) { hide[i] = 1; continue; }
                double r = ang * M_PI / 180, half = at(adv, i) / 2;
                gx[i] = qx - half * cos(r);
                gy[i] = qy - half * sin(r) + (at(cs, i).set & 8 ? at(cs, i).dy : 0);
                rot[i] = at(rot, i) + ang;
                onpath[i] = 1;
            }
        }
        i0 = i1;
    }
    /* each character's dominant baseline's shift (its element's): down the page, or across a
       path's direction */
    for (size_t q = 0; q < n; q++) {
        sv_walk cwv = at(ws, at(cs, q).el);
        double shift = sv_baseline_shift(st, f, &cwv, sv_face(st, &cwv), sv_fs(st, &cwv)), r = at(rot, q) * M_PI / 180;
        gx[q] = at(gx, q) - shift * sin(r);
        gy[q] = at(gy, q) + shift * cos(r);
    }
    append_range(cso, data(cs), len(cs));
    append_range(elso, data(els), len(els));
    append_range(wso, data(ws), len(ws));
    append_range(gxo, data(gx), n);
    append_range(gyo, data(gy), n);
    append_range(advo, data(adv), n);
    append_range(roto, data(rot), n);
    append_range(hso, data(hs), n);
    append_range(hideo, data(hide), n);
    append_range(onpatho, data(onpath), n);
    append_range(glo, data(gl), len(gl));
    append_range(gfo, data(gf), n);
    append_range(gno, data(gn), n);
    return 1;
}

/* a text's bounds under mm (its user space's matrix): each drawn character's cell -- its
   advance across, the face's ascent above its baseline and descent below -- turned as its
   glyph is: 0 when it has none */
static int sv_text_bounds(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t x, const sv_walk *w,
                          sv_mat mm, double *bb) {
    vector<sv_char> cs;
    vector<uint32_t> els, gf, gn;
    vector<sv_walk> ws;
    vector<double> gx, gy, adv, rot, hs;
    vector<uint8_t> hide, onpath;
    vector<sv_sglyph> gl;
    if (!sv_text_layout(d, st, firsts, f, x, w, &cs, &els, &ws, &gx, &gy, &adv, &rot, &hs, &hide, &onpath, &gl, &gf, &gn)) return 0;
    int any = 0;
    bb[0] = bb[1] = 1e300;
    bb[2] = bb[3] = -1e300;
    for (size_t q = 0; q < len(cs); q++) {
        if (at(hide, q)) continue;
        sv_walk cwv = at(ws, at(cs, q).el);
        double asc, desc, gap, r = at(rot, q) * M_PI / 180, c = cos(r), s = sin(r);
        amb_face_metrics(f, sv_face(st, &cwv), sv_fs(st, &cwv), &asc, &desc, &gap);
        double xs[4] = { 0, at(adv, q), at(adv, q), 0 }, ys[4] = { -asc, -asc, desc, desc };
        for (int k = 0; k < 4; k++) {
            double ux = at(gx, q) + xs[k] * c - ys[k] * s, uy = at(gy, q) + xs[k] * s + ys[k] * c;
            double px = sv_ax(mm, ux, uy), py = sv_ay(mm, ux, uy);
            bb[0] = fmin(bb[0], px);
            bb[1] = fmin(bb[1], py);
            bb[2] = fmax(bb[2], px);
            bb[3] = fmax(bb[3], py);
            any = 1;
        }
    }
    return any;
}

/* a text element's glyphs (sv_text_layout's): runs of one element's characters each a line,
   each character's shaped glyphs (a ligature's and a mark's drawn by the character its
   cluster begins at): upright glyphs (a uniform scale, no turn) filled as the page's text is
   ('t': its matrix, face, size, slant, paint, opacity, each glyph's id, face and place), others
   filled as their outlines ('p'); a stroke, outlines' too; fill and stroke in the paint-order */
static void sv_text(SB, uint32_t x, const sv_walk *w, sv_mat m) {
    vector<sv_char> cs;
    vector<uint32_t> els, gf, gn;
    vector<sv_walk> ws;
    vector<double> gx, gy, adv, rot, hs;
    vector<uint8_t> hide, onpath;
    vector<sv_sglyph> gl;
    if (!sv_text_layout(d, st, firsts, f, x, w, &cs, &els, &ws, &gx, &gy, &adv, &rot, &hs, &hide, &onpath, &gl, &gf, &gn)) return;
    size_t n = len(cs);
    int upright_m = fabs(m.b) < 1e-9 && fabs(m.c) < 1e-9 && m.a > 0 && fabs(m.a - m.d) < 1e-9 * fabs(m.a);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && at(cs, j).el == at(cs, i).el) j++;
        sv_walk cwv = at(ws, at(cs, i).el);
        const sv_walk *cw = &cwv;
        if (sv_word_is(st, cw->src[SI_VISIBILITY], "hidden") || sv_word_is(st, cw->src[SI_VISIBILITY], "collapse")) { i = j; continue; }
        int face = sv_face(st, cw);
        double fs = sv_fs(st, cw);
        double asc, desc, gap;
        amb_face_metrics(f, face, fs, &asc, &desc, &gap);
        uint32_t sc = cw->src[SI_FONT_STYLE];
        int italic = sv_word_is(st, sc, "italic") || sv_word_is(st, sc, "oblique");
        double bb[4] = { 1e300, 1e300, -1e300, -1e300 };
        int upright = upright_m;
        for (size_t q = i; q < j; q++) {
            if (at(hide, q)) continue;
            bb[0] = fmin(bb[0], at(gx, q));
            bb[2] = fmax(bb[2], at(gx, q) + at(adv, q));
            bb[1] = fmin(bb[1], at(gy, q) - asc);
            bb[3] = fmax(bb[3], at(gy, q) + desc);
            if (at(rot, q) != 0 || at(hs, q) != 1 || at(onpath, q)) upright = 0;
        }
        if (bb[0] > bb[2]) { i = j; continue; }
        uint32_t cur = st->cval[cw->src[SI_COLOR]].color;
        sv_paint fp, spt;
        sv_paint_of(d, st, x, cw->src[SI_FILL], cur, &fp);
        sv_paint_of(d, st, x, cw->src[SI_STROKE], cur, &spt);
        uint32_t fg = 0, frgba = 0, sg = 0, srgba = 0;
        int fk = sv_resolve(SA, &fp, bb, cw, &fg, &frgba);
        int sk = sv_resolve(SA, &spt, bb, cw, &sg, &srgba);
        double sw = sv_css_len(st, cw->src[SI_STROKE_WIDTH], sv_diag(cw));
        if (sw <= 0) sk = 0;
        /* the glyphs' outlines, for what is not filled as upright text */
        vector<double> outline;
        if (sk || (fk && !upright))
            for (size_t q = i; q < j; q++)
                if (!at(hide, q))
                    for (uint32_t g = at(gf, q); g < at(gf, q) + at(gn, q); g++) {
                        sv_sglyph sgl = at(gl, g);
                        sv_glyph_outline(f, &sgl, fs, italic, at(gx, q), at(gy, q), at(rot, q), at(hs, q), &outline);
                    }
        int order[3];
        sv_paint_order(st, cw->src[SI_PAINT_ORDER], order);
        for (int o = 0; o < 3; o++) {
            vector<char> line;
            if (order[o] == 0 && fk && upright) {
                sv_w(&line, "t");
                sv_m(&line, m);
                sv_n(&line, face);
                sv_n(&line, fs);
                sv_n(&line, italic);
                sv_paint_words(&line, fk, fg, frgba);
                sv_n(&line, st->cval[cw->src[SI_FILL_OPACITY]].num);
                size_t count = 0;
                for (size_t q = i; q < j; q++)
                    if (!at(hide, q))
                        for (uint32_t g = at(gf, q); g < at(gf, q) + at(gn, q); g++) count += !(at(gl, g).flags & AMB_SHAPED_HIDDEN);
                sv_n(&line, (double)count);
                for (size_t q = i; q < j; q++) {
                    if (at(hide, q)) continue;
                    for (uint32_t g = at(gf, q); g < at(gf, q) + at(gn, q); g++) {
                        sv_sglyph sgl = at(gl, g);
                        if (sgl.flags & AMB_SHAPED_HIDDEN) continue;
                        sv_n(&line, sgl.gid);
                        sv_n(&line, sgl.face);
                        sv_n(&line, at(gx, q) + sgl.ox);
                        sv_n(&line, at(gy, q) + sgl.oy);
                    }
                }
            } else if ((order[o] == 0 && fk) || (order[o] == 1 && sk)) {
                if (!len(outline)) continue;
                int fill = order[o] == 0;
                sv_w(&line, "p");
                sv_m(&line, m);
                sv_paint_words(&line, fill ? fk : 0, fg, frgba);
                sv_n(&line, st->cval[cw->src[SI_FILL_OPACITY]].num);
                sv_n(&line, 0);
                sv_paint_words(&line, fill ? 0 : sk, sg, srgba);
                sv_stroke_words(st, cw, sw, 1, &line);
                sv_path_out(&line, &outline);
            } else continue;
            sv_w(&line, "\n");
            append_range(out, data(line), len(line));
        }
        i = j;
    }
}

void amb_svg_scene(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t el, double vw,
                   double vh, vector<char> *out) {
    if (vw <= 0 || vh <= 0 || el >= d->node_len || !sv_is(d, el, "svg")) return;
    uint32_t c0 = sv_first(firsts, el);
    if (!c0) return;
    sv_walk w;
    memset(&w, 0, sizeof w);
    for (int k = 0; k < SI_N; k++) w.src[k] = c0 + (uint32_t)sv_iprops[k];
    w.vw = vw;
    w.vh = vh;
    sv_mat m = sv_id();
    double vb[4], vp[4] = { 0, 0, vw, vh };
    if (sv_viewbox_of(d, el, vb)) {
        m = sv_viewbox(d, el, vb, vp);
        w.vw = vb[2];
        w.vh = vb[3];
    }
    int clip = !(st->cval[c0 + CSS_PROP_OVERFLOW_X].how == CSS_HOW_DECLARED &&
                 (sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "visible") || sv_word_is(st, c0 + CSS_PROP_OVERFLOW_X, "auto")));
    vector<char> body;
    uint32_t ids = 0, work = 0;
    vector<uint32_t> uses;
    for (uint32_t c = d->node[el].first_child; c; c = d->node[c].next_sibling) sv_node(d, st, firsts, f, &body, &ids, &uses, &work, c, &w, m, 0);
    if (!len(body)) return;
    sv_w(out, "v");
    sv_n(out, vw);
    sv_n(out, vh);
    sv_n(out, clip);
    sv_w(out, "\n");
    append_range(out, data(body), len(body));
}

/* ---- SVG's effects on a CSS box: the filters, clip and mask its url()s name ---- */

/* the number of items of a comma list (a computed value's: commas inside parentheses and quotes not its) */
static size_t sv_list_count(const char *s, size_t n) {
    size_t k = 1;
    int depth = 0;
    char q = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '(') depth++;
        else if (c == ')' && depth > 0) depth--;
        else if (c == ',' && depth == 0) k++;
    }
    return k;
}
/* the item k of a comma list, its white space about it left out: where it begins, its length */
static void sv_list_item(const char *s, size_t n, size_t k, size_t *a, size_t *l) {
    size_t i = 0, at0 = 0, item = 0;
    int depth = 0;
    char q = 0;
    for (; i <= n; i++) {
        char c = i < n ? s[i] : ',';
        if (q) { if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') { q = c; continue; }
        if (c == '(') { depth++; continue; }
        if (c == ')' && depth > 0) { depth--; continue; }
        if ((c == ',' && depth == 0) || i == n) {
            if (item == k) break;
            item++;
            at0 = i + 1;
        }
    }
    size_t e = i < n ? i : n, b = at0;
    while (b < e && sv_ws(s[b])) b++;
    while (e > b && sv_ws(s[e - 1])) e--;
    *a = b;
    *l = e > b ? e - b : 0;
}

void amb_svg_effects(const dom_document *d, const css_style *st, const vector<uint32_t> *firsts, const amb_fonts *f, uint32_t el, double bw,
                     double bh, vector<char> *out) {
    if (el >= d->node_len || !(bw >= 0 && bh >= 0)) return;
    uint32_t c0 = sv_first(firsts, el);
    sv_walk w;
    if (!c0 || !sv_root_walk(st, firsts, el, bw, bh, &w)) return;
    double bb[4] = { 0, 0, bw, bh };
    int hb = bw > 0 && bh > 0;
    uint32_t ids = 0, work = 0;
    vector<uint32_t> uses;
    sv_mat mm = sv_id();
    /* the filter's list: each url() in turn (its functions are paint's own) */
    uint32_t fcv = c0 + CSS_PROP_FILTER;
    if (st->cval[fcv].unit == CSS_UNIT_TEXT) {
        vector<char> t;
        amb_span sp = st->cval[fcv].text;
        append_range(t, text_ptr(st, sp), sp.len);
        size_t i = 0, pa, pl, end;
        while (css_next_part(data(t), len(t), &i, &pa, &pl)) {
            if (pl < 4 || memcmp(data(t) + pa, "url(", 4) != 0) continue;
            uint32_t flt = sv_url(d, el, data(t) + pa, pl, &end);
            vector<char> blk;
            double reg[4];
            int ok = flt && sv_is(d, flt, "filter") && sv_filter_block(d, st, firsts, f, &blk, &ids, &uses, &work, flt, bb, hb, &w, mm, reg);
            sv_w(out, ok ? "f 1\ng 1 0 0 0 1\n" : "f 0\n");
            append_range(out, data(blk), len(blk));
        }
    }
    /* the clip-path's clipPath: its own clip-path's shapes a group about its own; their union the clip, not held to its
       bounding box (Chromium's on a box, measured: an ellipse's top row half covered, not a quarter, as an SVG element's) */
    uint32_t cp = sv_clip_of(d, st, el, c0 + CSS_PROP_CLIP_PATH);
    if (cp) {
        uint32_t k0 = sv_first(firsts, cp), outer = k0 ? sv_clip_of(d, st, cp, k0 + CSS_PROP_CLIP_PATH) : 0;
        vector<char> lines, blk;
        sv_w(out, "c 1\n");
        if (outer && outer != cp) {
            int ko = sv_clip_shapes(d, st, firsts, f, &blk, &ids, &uses, &work, outer, bb, hb, mm, bw, bh, &lines);
            sv_w(out, "g 1 1");
            sv_n(out, ko);
            sv_w(out, "\n");
            append_range(out, data(lines), len(lines));
            clear(lines);
        }
        int k = sv_clip_shapes(d, st, firsts, f, &blk, &ids, &uses, &work, cp, bb, hb, mm, bw, bh, &lines);
        sv_w(out, "g 1 1");
        sv_n(out, k);
        sv_w(out, "\n");
        append_range(out, data(lines), len(lines));
    }
    /* the mask's layers (CSS Masking 1, 7), each its kind, its mode and how it composites with those under it */
    uint32_t mcv = c0 + CSS_PROP_MASK_IMAGE;
    if (st->cval[mcv].unit != CSS_UNIT_TEXT) return;
    vector<char> img, pos, siz, rep, org, clp, cmp, mod;
    amb_span sp = st->cval[mcv].text;
    append_range(img, text_ptr(st, sp), sp.len);
    const int lists[7] = { CSS_PROP_MASK_POSITION, CSS_PROP_MASK_SIZE, CSS_PROP_MASK_REPEAT, CSS_PROP_MASK_ORIGIN, CSS_PROP_MASK_CLIP,
                           CSS_PROP_MASK_COMPOSITE, CSS_PROP_MASK_MODE };
    for (int q = 0; q < 7; q++) {
        vector<char> *l = q == 0 ? &pos : q == 1 ? &siz : q == 2 ? &rep : q == 3 ? &org : q == 4 ? &clp : q == 5 ? &cmp : &mod;
        uint32_t cv = c0 + (uint32_t)lists[q];
        if (st->cval[cv].unit == CSS_UNIT_TEXT) { amb_span lp = st->cval[cv].text; append_range(*l, text_ptr(st, lp), lp.len); }
        else if (st->cval[cv].unit == CSS_UNIT_KEYWORD && st->cval[cv].word) { const char *wd = names_ptr(st, st->cval[cv].word); append_range(*l, wd, strlen(wd)); }
    }
    size_t nl = sv_list_count(data(img), len(img));
    for (size_t k = 0; k < nl && k < 32; k++) {
        size_t ia, il, ma, ml, ca, cl;
        sv_list_item(data(img), len(img), k, &ia, &il);
        sv_list_item(data(mod), len(mod), k % sv_list_count(data(mod), len(mod)), &ma, &ml);
        sv_list_item(data(cmp), len(cmp), k % sv_list_count(data(cmp), len(cmp)), &ca, &cl);
        const char *ms = data(mod) + ma, *cs = data(cmp) + ca, *is = data(img) + ia;
        int mode = sv_eq_ci(ms, ml, "alpha") ? 1 : sv_eq_ci(ms, ml, "luminance") ? 2 : 0;
        int comp = sv_eq_ci(cs, cl, "subtract") ? 1 : sv_eq_ci(cs, cl, "intersect") ? 2 : sv_eq_ci(cs, cl, "exclude") ? 3 : 0;
        int grad = il > 9 && (memcmp(is, "linear-", 7) == 0 || memcmp(is, "radial-", 7) == 0 || memcmp(is, "repeating-", 10) == 0);
        if (grad) {
            /* a gradient: its own lines -- its image (i), position (p), size (s), repeat (r), origin (o) and clip (q) --
               which paint draws as a background layer's */
            sv_w(out, "m 3");
            sv_n(out, mode);
            sv_n(out, comp);
            sv_w(out, "\ni ");
            append_range(out, is, il);
            static const char *const tags[5] = { "\np ", "\ns ", "\nr ", "\no ", "\nq " };
            for (int q = 0; q < 5; q++) {
                vector<char> *l = q == 0 ? &pos : q == 1 ? &siz : q == 2 ? &rep : q == 3 ? &org : &clp;
                size_t la, ll;
                sv_list_item(data(*l), len(*l), k % sv_list_count(data(*l), len(*l)), &la, &ll);
                sv_w(out, tags[q]);
                append_range(out, data(*l) + la, ll);
            }
            sv_w(out, "\n");
            continue;
        }
        /* a mask element's url(): its group, its type the mode's when the mode is not match-source; anything else (none, a
           url() naming no mask, a picture, which is not fetched) a layer of transparent black, which masks all away (7.1) */
        size_t end;
        uint32_t mk = il > 4 && memcmp(is, "url(", 4) == 0 ? sv_url(d, el, is, il, &end) : 0;
        vector<char> blk;
        int ok = mk && sv_is(d, mk, "mask") && sv_mask_block(d, st, firsts, f, &blk, &ids, &uses, &work, mk, bb, hb, &w, mm, 0);
        if (ok && mode && len(blk) > 2) blk[2] = mode == 1 ? '1' : '0';
        sv_w(out, ok ? "m 1" : "m 2");
        sv_n(out, mode);
        sv_n(out, comp);
        sv_w(out, ok ? "\ng 1 0 0 1 0\n" : "\n");
        append_range(out, data(blk), len(blk));
    }
}

/* ---- the drawing: the scene read ---- */

/* the scene's next word from *i (on its line when line is 1): its place */
static int sv_tok(const char *s, size_t n, size_t *i, int line, size_t *a, size_t *k) {
    size_t p = *i;
    while (p < n && (s[p] == ' ' || (!line && s[p] == '\n'))) p++;
    if (p >= n || s[p] == '\n') { *i = p; return 0; }
    size_t b = p;
    while (p < n && s[p] != ' ' && s[p] != '\n') p++;
    *a = b;
    *k = p - b;
    *i = p;
    return 1;
}
static double sv_rnum(const char *s, size_t n, size_t *i) {
    size_t a, k, j;
    double v = 0;
    if (!sv_tok(s, n, i, 1, &a, &k)) return 0;
    j = a;
    if (!sv_number(s, a + k, &j, &v)) return 0;
    return v;
}
static uint32_t sv_rhex(const char *s, size_t n, size_t *i) {
    size_t a, k;
    uint32_t v = 0;
    if (!sv_tok(s, n, i, 1, &a, &k)) return 0;
    for (size_t j = a; j < a + k; j++) {
        char c = s[j];
        v = v * 16 + (uint32_t)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
    }
    return v;
}
static sv_mat sv_rmat(const char *s, size_t n, size_t *i) {
    double v[6];
    for (int k = 0; k < 6; k++) v[k] = sv_rnum(s, n, i);
    return sv_make(v[0], v[1], v[2], v[3], v[4], v[5]);
}
/* the rest of the line's path: its commands into p */
static void sv_rpath(const char *s, size_t n, size_t *i, vector<double> *p) {
    clear(*p);
    size_t a, k;
    while (sv_tok(s, n, i, 1, &a, &k)) {
        char c = s[a];
        if (c == 'M' || c == 'L') { double x = sv_rnum(s, n, i), y = sv_rnum(s, n, i); sv_put2(p, c, x, y); }
        else if (c == 'C' || c == 'K') {
            double v[6];
            for (int q = 0; q < 6; q++) v[q] = sv_rnum(s, n, i);
            sv_put_c(p, c, v[0], v[1], v[2], v[3], v[4], v[5]);
        } else if (c == 'Q') {
            double v[4];
            for (int q = 0; q < 4; q++) v[q] = sv_rnum(s, n, i);
            sv_put_quad(p, v[0], v[1], v[2], v[3]);
        } else if (c == 'Z') push_back(p, (double)SV_Z);
    }
}
static void sv_skip_line(const char *s, size_t n, size_t *i) {
    while (*i < n && s[*i] != '\n') (*i)++;
}

/* ---- curves flattened, strokes outlined ---- */

/* the path's subpaths under m, flattened (each curve's points within tol of it): their
   points into pts (x, y), each one's corner flag (1 a point the path names, 0 a curve's
   own) and subs: each subpath's first point, its count, and its flags (1 closed, 2 drawn:
   a command after its move) */
static void sv_flatten(const vector<double> *p, sv_mat m, double tol, vector<double> *pts, vector<uint8_t> *corner, vector<uint32_t> *subs) {
    clear(*pts);
    clear(*corner);
    clear(*subs);
    size_t i = 0, n = len(*p);
    uint32_t start = 0, flags = 0;
    int open = 0;
    double cx = 0, cy = 0;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_M) {
            if (open) { push_back(subs, start); push_back(subs, (uint32_t)(len(*pts) / 2) - start); push_back(subs, flags); }
            cx = at(*p, i + 1);
            cy = at(*p, i + 2);
            start = (uint32_t)(len(*pts) / 2);
            push_back(pts, sv_ax(m, cx, cy));
            push_back(pts, sv_ay(m, cx, cy));
            push_back(corner, (uint8_t)1);
            open = 1;
            flags = 0;
            i += 3;
        } else if (c == SV_L) {
            cx = at(*p, i + 1);
            cy = at(*p, i + 2);
            if (open) {
                push_back(pts, sv_ax(m, cx, cy));
                push_back(pts, sv_ay(m, cx, cy));
                push_back(corner, (uint8_t)1);
                flags |= 2;
            }
            i += 3;
        } else if (c == SV_C || c == SV_K || c == SV_Q) {
            double q[8];
            sv_curve_at(p, i, cx, cy, q);
            double z[8];
            for (int k = 0; k < 4; k++) { z[2 * k] = sv_ax(m, q[2 * k], q[2 * k + 1]); z[2 * k + 1] = sv_ay(m, q[2 * k], q[2 * k + 1]); }
            double ddx = fmax(fabs(z[0] - 2 * z[2] + z[4]), fabs(z[2] - 2 * z[4] + z[6]));
            double ddy = fmax(fabs(z[1] - 2 * z[3] + z[5]), fabs(z[3] - 2 * z[5] + z[7]));
            double dd = sqrt(ddx * ddx + ddy * ddy);
            double fsegs = ceil(sqrt(0.75 * dd / tol));
            int segs = fsegs >= 1 && fsegs <= 1000 ? (int)fsegs : fsegs > 1000 ? 1000 : 1;
            if (open) {
                /* a point just inside each end too, so that the curve's first and last chords
                   run along its tangents there: a join at its ends is then the one Skia makes
                   from the curve's own tangents (a miter's limit is met or not as there) */
                for (int k = 0; k <= segs + 1; k++) {
                    double t = k == 0 ? 1e-4 : k == segs + 1 ? 1 : k == segs ? 1 - 1e-4 : (double)k / segs, u = 1 - t;
                    double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
                    push_back(pts, b0 * z[0] + b1 * z[2] + b2 * z[4] + b3 * z[6]);
                    push_back(pts, b0 * z[1] + b1 * z[3] + b2 * z[5] + b3 * z[7]);
                    push_back(corner, (uint8_t)(k == segs + 1));
                }
                flags |= 2;
            }
            cx = q[6];
            cy = q[7];
            i += (size_t)sv_args(c) + 1;
        } else {
            if (open) {
                push_back(subs, start);
                push_back(subs, (uint32_t)(len(*pts) / 2) - start);
                push_back(subs, flags | 3);
            }
            open = 0;
            i++;
        }
    }
    if (open) { push_back(subs, start); push_back(subs, (uint32_t)(len(*pts) / 2) - start); push_back(subs, flags); }
}

/* a polygon of k points (q) added to edges under m, wound one way: its area made positive,
   so that the non-zero rule unites the pieces of a stroke */
static void sv_poly(const double *q, int k, sv_mat m, vector<double> *edges) {
    double area = 0;
    for (int j = 0; j < k; j++) {
        int l = j + 1 == k ? 0 : j + 1;
        area += q[2 * j] * q[2 * l + 1] - q[2 * l] * q[2 * j + 1];
    }
    for (int j = 0; j < k; j++) {
        int l = j + 1 == k ? 0 : j + 1;
        int a0 = area >= 0 ? j : l, a1 = area >= 0 ? l : j;
        double x0 = sv_ax(m, q[2 * a0], q[2 * a0 + 1]), y0 = sv_ay(m, q[2 * a0], q[2 * a0 + 1]);
        double x1 = sv_ax(m, q[2 * a1], q[2 * a1 + 1]), y1 = sv_ay(m, q[2 * a1], q[2 * a1 + 1]);
        if (y0 == y1) continue;
        push_back(edges, x0);
        push_back(edges, y0);
        push_back(edges, x1);
        push_back(edges, y1);
    }
}
/* a disc of radius r about (cx, cy), its sides within 0.05 of the device's px of its circle
   (rd: its radius there) */
static void sv_disc(double cx, double cy, double r, double rd, sv_mat m, vector<double> *edges, vector<double> *tmp) {
    double fk = rd > 0.05 ? ceil(M_PI / acos(1 - 0.05 / rd)) : 8;
    int k = fk >= 8 && fk <= 720 ? (int)fk : fk > 720 ? 720 : 8;
    clear(*tmp);
    for (int j = 0; j < k; j++) {
        double t = 2 * M_PI * j / k;
        push_back(tmp, cx + r * cos(t));
        push_back(tmp, cy + r * sin(t));
    }
    sv_poly(data(*tmp), k, m, edges);
}
static void sv_quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, sv_mat m, vector<double> *edges) {
    double q[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
    sv_poly(q, 4, m, edges);
}

/* the stroke's style: half its width, cap (0 butt, 1 round, 2 square), join (0 miter, 1
   round, 2 bevel), miter limit, and the device's px a unit (for round pieces) */
typedef struct sv_pen { double hw, miter, rd; int cap, join; } sv_pen;

/* a polyline's stroke (k points at q, their corner flags at cf; closed or open; (tx, ty) the
   direction of one that is a point alone) as pieces into edges under m: each segment's
   quad, each joint's join (the path's own at its corners, a smooth one inside a curve), the
   caps of an open one */
static void sv_stroke_line(const double *q, const uint8_t *cf, int k, int closed, double tx, double ty, const sv_pen *pen, sv_mat m,
                           vector<double> *edges, vector<double> *tmp, vector<double> *cl, vector<uint8_t> *ccf) {
    clear(*cl);
    clear(*ccf);
    for (int j = 0; j < k; j++) {
        double x = q[2 * j], y = q[2 * j + 1];
        size_t c = len(*cl);
        if (c >= 2 && fabs(at(*cl, c - 2) - x) < 1e-9 && fabs(at(*cl, c - 1) - y) < 1e-9) {
            if (cf[j]) (*ccf)[c / 2 - 1] = 1;
            continue;
        }
        push_back(cl, x);
        push_back(cl, y);
        push_back(ccf, cf[j]);
    }
    int np = (int)(len(*cl) / 2);
    if (closed && np > 1 && fabs(at(*cl, 0) - at(*cl, 2 * np - 2)) < 1e-9 && fabs(at(*cl, 1) - at(*cl, 2 * np - 1)) < 1e-9) {
        np--;
        (*ccf)[0] = 1;
    }
    if (np <= 0) return;
    double hw = pen->hw;
    if (np == 1) {
        double px = at(*cl, 0), py = at(*cl, 1);
        if (pen->cap == 1) sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp);
        else if (pen->cap == 2) {
            double l = sqrt(tx * tx + ty * ty);
            double ux = l > 0 ? tx / l : 1, uy = l > 0 ? ty / l : 0;
            double nx = -uy * hw, ny = ux * hw;
            sv_quad(px - ux * hw + nx, py - uy * hw + ny, px + ux * hw + nx, py + uy * hw + ny, px + ux * hw - nx, py + uy * hw - ny,
                    px - ux * hw - nx, py - uy * hw - ny, m, edges);
        }
        return;
    }
    int segs = closed ? np : np - 1;
    for (int j = 0; j < segs; j++) {
        int l = j + 1 == np ? 0 : j + 1;
        double x0 = at(*cl, 2 * j), y0 = at(*cl, 2 * j + 1), x1 = at(*cl, 2 * l), y1 = at(*cl, 2 * l + 1);
        double dx = x1 - x0, dy = y1 - y0, len0 = sqrt(dx * dx + dy * dy);
        double nx = -dy / len0 * hw, ny = dx / len0 * hw;
        sv_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, m, edges);
    }
    /* the joins */
    for (int j = closed ? 0 : 1; j < (closed ? np : np - 1); j++) {
        int pj = j == 0 ? np - 1 : j - 1, nj = j + 1 == np ? 0 : j + 1;
        double px = at(*cl, 2 * j), py = at(*cl, 2 * j + 1);
        double ax = px - at(*cl, 2 * pj), ay = py - at(*cl, 2 * pj + 1), bx = at(*cl, 2 * nj) - px, by = at(*cl, 2 * nj + 1) - py;
        double la = sqrt(ax * ax + ay * ay), lb = sqrt(bx * bx + by * by);
        ax /= la; ay /= la; bx /= lb; by /= lb;
        double cross = ax * by - ay * bx, dot = ax * bx + ay * by;
        if (fabs(cross) < 1e-9 && dot > 0) continue;
        int smooth = !at(*ccf, j);
        int jt = smooth ? (dot < 0.5 ? 1 : 0) : pen->join;
        if (jt == 1) { sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp); continue; }
        double sg = cross > 0 ? -1 : 1;
        double n1x = -ay * hw * sg, n1y = ax * hw * sg, n2x = -by * hw * sg, n2y = bx * hw * sg;
        if (jt == 0 && dot > -1 + 1e-9) {
            double ratio = sqrt(2 / (1 + dot));
            if (smooth || ratio <= pen->miter) {
                double tipx = px + (n1x + n2x) / (1 + dot), tipy = py + (n1y + n2y) / (1 + dot);
                sv_quad(px, py, px + n1x, py + n1y, tipx, tipy, px + n2x, py + n2y, m, edges);
                continue;
            }
        }
        double t3[6] = { px, py, px + n1x, py + n1y, px + n2x, py + n2y };
        sv_poly(t3, 3, m, edges);
    }
    /* the caps */
    if (!closed && pen->cap) {
        for (int e = 0; e < 2; e++) {
            int j = e ? np - 1 : 0, o = e ? np - 2 : 1;
            double px = at(*cl, 2 * j), py = at(*cl, 2 * j + 1);
            double ux = px - at(*cl, 2 * o), uy = py - at(*cl, 2 * o + 1), l = sqrt(ux * ux + uy * uy);
            ux /= l;
            uy /= l;
            if (pen->cap == 1) sv_disc(px, py, hw, hw * pen->rd, m, edges, tmp);
            else {
                double nx = -uy * hw, ny = ux * hw;
                sv_quad(px + nx, py + ny, px + nx + ux * hw, py + ny + uy * hw, px - nx + ux * hw, py - ny + uy * hw, px - nx, py - ny, m, edges);
            }
        }
    }
}

/* ---- dashes as Skia cuts them: SkDashPath over SkContourMeasure ----
   A contour is measured by its curves' chords: each curve halved (in t, kept as Skia keeps
   it in 30 bits) while it bends more than the tolerance (half a device px) from its chord --
   a cubic by its control points against its chord's thirds, a quadratic by its control
   point, an arc's piece (Skia's conic) by its middle -- and a distance along the contour
   is a t interpolated within its chord. A dash is then the curves between two such
   places, cut exactly. So a dash on a curve sits where Skia puts it, a little short of
   where the curve's true length would. */


static void sv_bez_pt(const double *q, int kind, double t, double *x, double *y) {
    double u = 1 - t;
    if (kind == SV_L) { *x = q[0] + (q[2] - q[0]) * t; *y = q[1] + (q[3] - q[1]) * t; return; }
    if (kind == SV_Q) {
        *x = u * u * q[0] + 2 * u * t * q[2] + t * t * q[4];
        *y = u * u * q[1] + 2 * u * t * q[3] + t * t * q[5];
        return;
    }
    double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    *x = b0 * q[0] + b1 * q[2] + b2 * q[4] + b3 * q[6];
    *y = b0 * q[1] + b1 * q[3] + b2 * q[5] + b3 * q[7];
}
/* a cubic (or a quadratic, its three points) cut at t: its two parts' points into a and b */
static void sv_cut(const double *q, int quad, double t, double *a, double *b) {
    for (int c = 0; c < 2; c++) {
        if (quad) {
            double p01 = q[c] + (q[2 + c] - q[c]) * t, p12 = q[2 + c] + (q[4 + c] - q[2 + c]) * t, m = p01 + (p12 - p01) * t;
            a[c] = q[c]; a[2 + c] = p01; a[4 + c] = m;
            b[c] = m; b[2 + c] = p12; b[4 + c] = q[4 + c];
            a[6 + c] = b[6 + c] = 0;
        } else {
            double p01 = q[c] + (q[2 + c] - q[c]) * t, p12 = q[2 + c] + (q[4 + c] - q[2 + c]) * t, p23 = q[4 + c] + (q[6 + c] - q[4 + c]) * t;
            double p012 = p01 + (p12 - p01) * t, p123 = p12 + (p23 - p12) * t, m = p012 + (p123 - p012) * t;
            a[c] = q[c]; a[2 + c] = p01; a[4 + c] = p012; a[6 + c] = m;
            b[c] = m; b[2 + c] = p123; b[4 + c] = p23; b[6 + c] = q[6 + c];
        }
    }
}
static double sv_cheap(double ax, double ay, double bx, double by) { return fmax(fabs(ax - bx), fabs(ay - by)); }
static void sv_mseg_add(vector<sv_mseg> *ms, double *dist, double d, uint32_t ci, int t) {
    double prev = *dist;
    *dist += d;
    if (*dist > prev) {
        sv_mseg m;
        m.dist = *dist;
        m.ci = ci;
        m.t = (uint32_t)t;
        push_back(ms, m);
    }
}
/* a cubic's or a quadratic's pieces (q: the piece from mint to maxt), halved as Skia halves them */
static void sv_meas_bez(const double *q, int quad, double *dist, int mint, int maxt, uint32_t ci, double tol, vector<sv_mseg> *ms) {
    int curvy;
    if (quad) curvy = sv_cheap(q[2] / 2, q[3] / 2, (q[0] + q[4]) / 4, (q[1] + q[5]) / 4) > tol;
    else curvy = sv_cheap(q[2], q[3], q[0] + (q[6] - q[0]) / 3, q[1] + (q[7] - q[1]) / 3) > tol ||
                 sv_cheap(q[4], q[5], q[0] + (q[6] - q[0]) * 2 / 3, q[1] + (q[7] - q[1]) * 2 / 3) > tol;
    if (((maxt - mint) >> 10) && curvy) {
        double a[8], b[8];
        int half = (mint + maxt) >> 1;
        sv_cut(q, quad, 0.5, a, b);
        sv_meas_bez(a, quad, dist, mint, half, ci, tol, ms);
        sv_meas_bez(b, quad, dist, half, maxt, ci, tol, ms);
        return;
    }
    sv_mseg_add(ms, dist, quad ? hypot(q[4] - q[0], q[5] - q[1]) : hypot(q[6] - q[0], q[7] - q[1]), ci, maxt);
}
/* an arc's piece (its cubic q, whole), halved as Skia halves a conic: by the curve's middle
   between the two points it has at mint and maxt */
static void sv_meas_arc(const double *q, double *dist, int mint, double mx, double my, int maxt, double nx, double ny, uint32_t ci,
                        double tol, vector<sv_mseg> *ms) {
    int half = (mint + maxt) >> 1;
    double hx, hy;
    sv_bez_pt(q, SV_C, (double)half / SV_TMAX, &hx, &hy);
    if (((maxt - mint) >> 10) && sv_cheap(hx, hy, (mx + nx) / 2, (my + ny) / 2) > tol) {
        sv_meas_arc(q, dist, mint, mx, my, half, hx, hy, ci, tol, ms);
        sv_meas_arc(q, dist, half, hx, hy, maxt, nx, ny, ci, tol, ms);
        return;
    }
    sv_mseg_add(ms, dist, hypot(nx - mx, ny - my), ci, maxt);
}

/* the place a distance along a contour (its pieces ms[m0, m1)) falls: its piece, and the
   curve's t there */
static size_t sv_dist_at(const vector<sv_mseg> *ms, size_t m0, size_t m1, double dist, double *t) {
    size_t lo = m0, hi = m1 - 1;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (at(*ms, mid).dist < dist) lo = mid + 1;
        else hi = mid;
    }
    sv_mseg sg = at(*ms, lo);
    double t0 = 0, d0 = 0;
    if (lo > m0) {
        d0 = at(*ms, lo - 1).dist;
        if (at(*ms, lo - 1).ci == sg.ci) t0 = (double)at(*ms, lo - 1).t / SV_TMAX;
    }
    double t1 = (double)sg.t / SV_TMAX;
    *t = sg.dist > d0 ? t0 + (t1 - t0) * (dist - d0) / (sg.dist - d0) : t1;
    return lo;
}

/* a curve (cv at ci: its kind, then its points) from t0 to t1 appended to out as a command */
static void sv_seg_to(const vector<double> *cv, uint32_t ci, double t0, double t1, vector<double> *out) {
    int kind = (int)at(*cv, ci);
    double q[8];
    for (int k = 0; k < 8; k++) q[k] = at(*cv, ci + 1 + (size_t)k);
    if (t0 == t1) {
        /* a dash of no length: a line of none, which the stroke caps */
        size_t n = len(*out);
        if (n >= 2) sv_put2(out, SV_L, at(*out, n - 2), at(*out, n - 1));
        return;
    }
    if (kind == SV_L) {
        double x, y;
        sv_bez_pt(q, SV_L, t1, &x, &y);
        sv_put2(out, SV_L, x, y);
        return;
    }
    int quad = kind == SV_Q;
    /* the piece from 0 to t1, then of it the part from t0 / t1 (de Casteljau's) */
    double a[8], b[8];
    if (t1 < 1) {
        sv_cut(q, quad, t1, a, b);
        for (int k = 0; k < 8; k++) q[k] = a[k];
    }
    if (t0 > 0 && t1 > 0) {
        sv_cut(q, quad, t0 / t1, a, b);
        for (int k = 0; k < 8; k++) q[k] = b[k];
    }
    if (quad) sv_put_quad(out, q[2], q[3], q[4], q[5]);
    else sv_put_c(out, kind, q[2], q[3], q[4], q[5], q[6], q[7]);
}
/* a contour's part from distance d0 to d1 appended to out, a move first when move is 1 */
static void sv_get_segment(const vector<double> *cv, const vector<sv_mseg> *ms, size_t m0, size_t m1, double d0, double d1, int move,
                           vector<double> *out) {
    double length = at(*ms, m1 - 1).dist;
    if (d0 < 0) d0 = 0;
    if (d1 > length) d1 = length;
    if (!(d0 <= d1)) return;
    double t0, t1;
    size_t s0 = sv_dist_at(ms, m0, m1, d0, &t0), s1 = sv_dist_at(ms, m0, m1, d1, &t1);
    uint32_t c0 = at(*ms, s0).ci, c1 = at(*ms, s1).ci;
    if (move) {
        double x, y, q[8];
        for (int k = 0; k < 8; k++) q[k] = at(*cv, c0 + 1 + (size_t)k);
        sv_bez_pt(q, (int)at(*cv, c0), t0, &x, &y);
        sv_put2(out, SV_M, x, y);
    }
    if (c0 == c1) { sv_seg_to(cv, c0, t0, t1, out); return; }
    sv_seg_to(cv, c0, t0, 1, out);
    for (uint32_t c = c0 + 9; c < c1; c += 9) sv_seg_to(cv, c, 0, 1, out);
    sv_seg_to(cv, c1, 0, t1, out);
}

/* the path p dashed (each subpath a contour of its own; nd lengths, an odd count's repeated;
   the pattern begun offset into it), its dashes as subpaths into out; tol the measure's.
   0 when the lengths sum to nothing (the path is not dashed) */
static int sv_dash_path(const vector<double> *p, const double *dash, int nd, double offset, double tol, vector<double> *out, vector<double> *cv,
                        vector<sv_mseg> *ms) {
    clear(*out);
    int count = nd % 2 ? 2 * nd : nd;
    double total = 0;
    for (int k = 0; k < count; k++) total += dash[k % nd];
    if (!(total > 0)) return 0;
    /* the phase, and the dash it begins in, as SkDashPath finds them */
    double phase = offset;
    if (phase < 0) {
        phase = -phase;
        if (phase > total) phase = fmod(phase, total);
        phase = total - phase;
        if (phase == total) phase = 0;
    } else if (phase >= total) phase = fmod(phase, total);
    int first = 0;
    double first_len = dash[0];
    for (int k = 0; k < count; k++) {
        double gap = dash[k % nd];
        if (phase > gap || (phase == gap && gap > 0)) phase -= gap;
        else { first = k; first_len = gap - phase; break; }
    }
    size_t i = 0, n = len(*p);
    long budget = 100000;
    while (i < n) {
        /* a contour: its curves and their measure */
        clear(*cv);
        clear(*ms);
        double sx = 0, sy = 0, cx = 0, cy = 0, dist = 0;
        int closed = 0;
        if ((int)at(*p, i) == SV_M) { sx = cx = at(*p, i + 1); sy = cy = at(*p, i + 2); i += 3; }
        while (i < n && (int)at(*p, i) != SV_M) {
            int c = (int)at(*p, i);
            if (c == SV_Z) { closed = 1; i++; break; }
            double q[8];
            uint32_t ci = (uint32_t)len(*cv);
            q[0] = cx;
            q[1] = cy;
            for (int k = 2; k < 8; k++) q[k] = 0;
            int k = sv_args(c);
            for (int j = 0; j < k; j++) q[2 + j] = at(*p, i + 1 + (size_t)j);
            push_back(cv, (double)c);
            for (int j = 0; j < 8; j++) push_back(cv, q[j]);
            if (c == SV_L) sv_mseg_add(ms, &dist, hypot(q[2] - q[0], q[3] - q[1]), ci, SV_TMAX);
            else if (c == SV_Q) sv_meas_bez(q, 1, &dist, 0, SV_TMAX, ci, tol, ms);
            else if (c == SV_K) sv_meas_arc(q, &dist, 0, q[0], q[1], SV_TMAX, q[6], q[7], ci, tol, ms);
            else sv_meas_bez(q, 0, &dist, 0, SV_TMAX, ci, tol, ms);
            cx = at(*p, i + (size_t)k - 1);
            cy = at(*p, i + (size_t)k);
            i += (size_t)k + 1;
        }
        if (closed) {
            uint32_t ci = (uint32_t)len(*cv);
            double q[8] = { cx, cy, sx, sy, 0, 0, 0, 0 };
            push_back(cv, (double)SV_L);
            for (int k = 0; k < 8; k++) push_back(cv, q[k]);
            sv_mseg_add(ms, &dist, hypot(sx - cx, sy - cy), ci, SV_TMAX);
        }
        size_t m1 = len(*ms);
        if (!m1 || !(dist < 1e9)) continue;
        double length = dist, d = 0, dl = first_len;
        int index = first, skip = closed, added = 0;
        while (d < length && budget-- > 0) {
            added = 0;
            if (index % 2 == 0 && !skip) {
                added = 1;
                sv_get_segment(cv, ms, 0, m1, d, d + dl, 1, out);
            }
            d += dl;
            skip = 0;
            index = index + 1 == count ? 0 : index + 1;
            dl = dash[index % nd];
        }
        /* a closed contour's first dash, skipped, joined to its last when that reaches it */
        if (closed && first % 2 == 0 && first_len >= 0) sv_get_segment(cv, ms, 0, m1, 0, first_len, !added, out);
    }
    return 1;
}

/* ---- coverage: sixteen rows a pixel, each span's ends exact across it ---- */

enum { SV_SUB = 16 };

/* the coverage of what edges bound (x0 y0 x1 y1 each, the layer's px) under the non-zero
   rule or the even-odd, in the layer's w x h: the rectangle it reaches into b (x0, y0, x1,
   y1), each of its pixels' coverage into cov (b's width a row); 0 when it reaches none */
static int sv_cover(vector<double> *edges, int evenodd, int w, int h, int *b, vector<float> *cov, vector<double> *xs,
                    vector<uint32_t> *order, vector<uint32_t> *act, vector<float> *acc) {
    /* edges past any canvas (or not numbers at all) clamped far outside it, their slopes kept
       finite */
    for (size_t q = 0; q < len(*edges); q++) {
        double v = at(*edges, q);
        if (!(v > -1e7)) (*edges)[q] = -1e7;
        else if (v > 1e7) (*edges)[q] = 1e7;
    }
    size_t ne = len(*edges) / 4;
    if (!ne) return 0;
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    for (size_t e = 0; e < ne; e++) {
        double x0 = at(*edges, 4 * e), y0 = at(*edges, 4 * e + 1), x1 = at(*edges, 4 * e + 2), y1 = at(*edges, 4 * e + 3);
        mnx = fmin(mnx, fmin(x0, x1));
        mxx = fmax(mxx, fmax(x0, x1));
        mny = fmin(mny, fmin(y0, y1));
        mxy = fmax(mxy, fmax(y0, y1));
    }
    if (!(mnx < 1e299) || mxx < 0 || mxy < 0 || mnx > w || mny > h) return 0;
    int bx0 = mnx < 0 ? 0 : (int)floor(mnx), by0 = mny < 0 ? 0 : (int)floor(mny);
    int bx1 = mxx >= w ? w : (int)ceil(mxx) + 1, by1 = mxy >= h ? h : (int)ceil(mxy);
    if (bx1 > w) bx1 = w;
    if (by1 > h) by1 = h;
    if (bx1 <= bx0 || by1 <= by0) return 0;
    b[0] = bx0; b[1] = by0; b[2] = bx1; b[3] = by1;
    int bw = bx1 - bx0, bh = by1 - by0;
    clear(*cov);
    resize(cov, (size_t)bw * (size_t)bh);
    /* the edges by the row each begins in (above the box: its first) */
    clear(*order);
    resize(order, (size_t)bh + 1 + ne);
    for (size_t e = 0; e < ne; e++) {
        double y0 = fmin(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3));
        int r = y0 < by0 ? 0 : (int)floor(y0) - by0;
        if (r >= bh) continue;
        (*order)[(size_t)r + 1]++;
    }
    for (int r = 0; r < bh; r++) (*order)[(size_t)r + 1] += at(*order, (size_t)r);
    for (size_t e = 0; e < ne; e++) {
        double y0 = fmin(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3));
        int r = y0 < by0 ? 0 : (int)floor(y0) - by0;
        if (r >= bh) continue;
        uint32_t slot = at(*order, (size_t)r);
        (*order)[(size_t)r]++;
        (*order)[(size_t)bh + 1 + slot] = (uint32_t)e;
    }
    /* order[r] is now where row r's edges end; they begin where row r-1's end */
    clear(*act);
    clear(*acc);
    resize(acc, 2 * ((size_t)bw + 2));
    size_t base = (size_t)bh + 1;
    float wt = 1.0f / SV_SUB;
    for (int r = 0; r < bh; r++) {
        int y = by0 + r;
        uint32_t from = r == 0 ? 0 : at(*order, (size_t)r - 1), to = at(*order, (size_t)r);
        for (uint32_t s = from; s < to; s++) push_back(act, at(*order, base + s));
        /* those that end above this row leave */
        size_t keep = 0;
        for (size_t q = 0; q < len(*act); q++) {
            uint32_t e = at(*act, q);
            if (fmax(at(*edges, 4 * e + 1), at(*edges, 4 * e + 3)) > y) (*act)[keep++] = e;
        }
        resize(act, keep);
        if (!keep) continue;
        for (size_t q = 0; q < len(*acc); q++) (*acc)[q] = 0;
        for (int sub = 0; sub < SV_SUB; sub++) {
            double sy = y + (sub + 0.5) / SV_SUB;
            clear(*xs);
            for (size_t q = 0; q < keep; q++) {
                uint32_t e = at(*act, q);
                double x0 = at(*edges, 4 * e), y0 = at(*edges, 4 * e + 1), x1 = at(*edges, 4 * e + 2), y1 = at(*edges, 4 * e + 3);
                double lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
                if (sy < lo || sy >= hi) continue;
                double xx = x0 + (sy - y0) * (x1 - x0) / (y1 - y0);
                /* in order by x: the crossings kept sorted as they come */
                push_back(xs, xx);
                push_back(xs, y1 > y0 ? 1.0 : -1.0);
                size_t m = len(*xs) / 2 - 1;
                while (m > 0 && at(*xs, 2 * m - 2) > xx) {
                    (*xs)[2 * m] = at(*xs, 2 * m - 2);
                    (*xs)[2 * m + 1] = at(*xs, 2 * m - 1);
                    m--;
                }
                (*xs)[2 * m] = xx;
                (*xs)[2 * m + 1] = y1 > y0 ? 1.0 : -1.0;
            }
            size_t nx = len(*xs) / 2;
            int wind = 0;
            for (size_t q = 0; q + 1 < nx; q++) {
                wind += (int)at(*xs, 2 * q + 1);
                int in = evenodd ? (wind & 1) : wind != 0;
                if (!in) continue;
                double fa = at(*xs, 2 * q) - bx0, fb = at(*xs, 2 * q + 2) - bx0;
                if (fa < 0) fa = 0;
                if (fb > bw) fb = bw;
                if (fb <= fa) continue;
                int ia = (int)floor(fa), ib = (int)floor(fb);
                if (ia == ib) (*acc)[(size_t)ia] += (float)(fb - fa) * wt;
                else {
                    (*acc)[(size_t)ia] += (float)(ia + 1 - fa) * wt;
                    (*acc)[(size_t)bw + 2 + (size_t)ia + 1] += wt;
                    (*acc)[(size_t)bw + 2 + (size_t)ib] -= wt;
                    (*acc)[(size_t)ib] += (float)(fb - ib) * wt;
                }
            }
        }
        float run = 0;
        size_t row = (size_t)r * (size_t)bw;
        for (int x = 0; x < bw; x++) {
            run += at(*acc, (size_t)bw + 2 + (size_t)x);
            float v = at(*acc, (size_t)x) + run;
            (*cov)[row + (size_t)x] = v > 1 ? 1 : v < 0 ? 0 : v;
        }
    }
    return 1;
}

/* ---- paints drawn ---- */

/* what a shape is painted with: none (kind 0), a colour (1: premultiplied, its opacity in), a
   gradient (2: its record in the gradients, the matrix from the layer's px to its space, its
   opacity) or a pattern (3: its tile's image in the tiles from toff, tw x th px, the matrix from
   the layer's px to the tile's, its opacity) */
typedef struct sv_ink { int kind; float r, g, b, a; size_t goff, toff; int tw, th; sv_mat inv; } sv_ink;

/* a gradient's colour at t (its record at o: its kind, matrix, geometry, spread, stops), premultiplied */
static void sv_stop_at(const vector<double> *gr, size_t o, double t, float *rgba) {
    int kind = (int)at(*gr, o);
    size_t sp = o + 7 + (kind ? 6 : 4);
    int spread = (int)at(*gr, sp), ns = (int)at(*gr, sp + 1);
    size_t s0 = sp + 2;
    if (spread == 1) { t = fmod(t, 2); if (t < 0) t += 2; if (t > 1) t = 2 - t; }
    else if (spread == 2) t = t - floor(t);
    else t = t < 0 ? 0 : t > 1 ? 1 : t;
    int k = 0;
    while (k + 1 < ns && at(*gr, s0 + 2 * ((size_t)k + 1)) <= t) k++;
    uint32_t c0 = (uint32_t)at(*gr, s0 + 2 * (size_t)k + 1), c1 = c0;
    double f = 0;
    if (k + 1 < ns && t > at(*gr, s0 + 2 * (size_t)k)) {
        double a = at(*gr, s0 + 2 * (size_t)k), b = at(*gr, s0 + 2 * ((size_t)k + 1));
        c1 = (uint32_t)at(*gr, s0 + 2 * ((size_t)k + 1) + 1);
        f = b > a ? (t - a) / (b - a) : 1;
    } else if (t < at(*gr, s0)) c1 = c0;
    double ch[4];
    for (int q = 0; q < 4; q++) {
        double u = (double)(c0 >> (24 - 8 * q) & 0xff), v = (double)(c1 >> (24 - 8 * q) & 0xff);
        ch[q] = (u + (v - u) * f) / 255;
    }
    rgba[0] = (float)(ch[0] * ch[3]);
    rgba[1] = (float)(ch[1] * ch[3]);
    rgba[2] = (float)(ch[2] * ch[3]);
    rgba[3] = (float)ch[3];
}
/* a gradient's colour at the layer's point (px, py), premultiplied: 0 where it paints nothing */
static int sv_grad_at(const vector<double> *gr, const sv_ink *ink, double px, double py, float *rgba) {
    size_t o = ink->goff;
    int kind = (int)at(*gr, o);
    double gx = sv_ax(ink->inv, px, py), gy = sv_ay(ink->inv, px, py), t;
    size_t g = o + 7;
    if (!kind) {
        double x1 = at(*gr, g), y1 = at(*gr, g + 1), dx = at(*gr, g + 2) - x1, dy = at(*gr, g + 3) - y1, l2 = dx * dx + dy * dy;
        t = l2 > 0 ? ((gx - x1) * dx + (gy - y1) * dy) / l2 : 1;
    } else {
        double cx = at(*gr, g), cy = at(*gr, g + 1), r = at(*gr, g + 2), fx = at(*gr, g + 3), fy = at(*gr, g + 4), fr = at(*gr, g + 5);
        if (r <= 0) t = 1;
        else {
            /* the two-point conical gradient from the focal circle to the end one: the
               greatest t whose circle passes through the point */
            double cdx = cx - fx, cdy = cy - fy, dr = r - fr, pdx = gx - fx, pdy = gy - fy;
            double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + fr * dr, c = pdx * pdx + pdy * pdy - fr * fr;
            if (fabs(a) < 1e-12) {
                if (fabs(b) < 1e-12) return 0;
                t = c / (2 * b);
                if (fr + t * dr < 0) return 0;
            } else {
                double disc = b * b - a * c;
                if (disc < 0) return 0;
                double sq = sqrt(disc), t1 = (b + sq) / a, t2 = (b - sq) / a;
                double hi = fmax(t1, t2), lo = fmin(t1, t2);
                if (fr + hi * dr >= 0) t = hi;
                else if (fr + lo * dr >= 0) t = lo;
                else return 0;
            }
        }
    }
    sv_stop_at(gr, o, t, rgba);
    for (int q = 0; q < 4; q++) rgba[q] *= ink->a;
    return 1;
}
/* a pattern's colour at the layer's point (px, py), premultiplied: its tile's image sampled
   bilinearly at texel centres, repeated each way, as Skia samples the image a picture shader
   rasterizes its tile into */
static int sv_pat_at(const vector<float> *tiles, const sv_ink *ink, double px, double py, float *rgba) {
    double u = sv_ax(ink->inv, px, py) - 0.5, v = sv_ay(ink->inv, px, py) - 0.5;
    if (!(fabs(u) < 1e9 && fabs(v) < 1e9) || ink->tw <= 0 || ink->th <= 0) return 0;
    double fu = floor(u), fv = floor(v), fx = u - fu, fy = v - fv;
    long W = ink->tw, H = ink->th;
    long x0 = (long)fmod(fu, (double)W), y0 = (long)fmod(fv, (double)H);
    if (x0 < 0) x0 += W;
    if (y0 < 0) y0 += H;
    long x1 = x0 + 1 == W ? 0 : x0 + 1, y1 = y0 + 1 == H ? 0 : y0 + 1;
    size_t r0 = ink->toff + (size_t)y0 * (size_t)W * 4, r1 = ink->toff + (size_t)y1 * (size_t)W * 4;
    for (int q = 0; q < 4; q++) {
        double a = at(*tiles, r0 + (size_t)x0 * 4 + (size_t)q), b = at(*tiles, r0 + (size_t)x1 * 4 + (size_t)q);
        double c = at(*tiles, r1 + (size_t)x0 * 4 + (size_t)q), e = at(*tiles, r1 + (size_t)x1 * 4 + (size_t)q);
        double top = a + (b - a) * fx, bot = c + (e - c) * fx;
        rgba[q] = (float)((top + (bot - top) * fy) * ink->a);
    }
    return 1;
}
/* an ink's colour at the layer's point (px, py), premultiplied: 0 where it paints nothing */
static int sv_ink_at(const vector<double> *gr, const vector<float> *tiles, const sv_ink *ink, double px, double py, float *c) {
    if (ink->kind == 2) return sv_grad_at(gr, ink, px, py, c);
    if (ink->kind == 3) return sv_pat_at(tiles, ink, px, py, c);
    c[0] = ink->r;
    c[1] = ink->g;
    c[2] = ink->b;
    c[3] = ink->a;
    return 1;
}

/* the coverage cov over b painted into the layer at loff (w px a row) with ink */
static void sv_paint_cov(vector<float> *lay, size_t loff, int w, const vector<float> *cov, const int *b, const sv_ink *ink, const vector<double> *gr,
                         const vector<float> *tiles) {
    int bw = b[2] - b[0];
    for (int y = b[1]; y < b[3]; y++)
        for (int x = b[0]; x < b[2]; x++) {
            float cv = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)(x - b[0]));
            if (cv <= 0) continue;
            float c[4];
            if (!sv_ink_at(gr, tiles, ink, x + 0.5, y + 0.5, c)) continue;
            size_t q = loff + ((size_t)y * (size_t)w + (size_t)x) * 4;
            float a = c[3] * cv;
            if (a <= 0) continue;
            float keep = 1 - a;
            (*lay)[q] = c[0] * cv + at(*lay, q) * keep;
            (*lay)[q + 1] = c[1] * cv + at(*lay, q + 1) * keep;
            (*lay)[q + 2] = c[2] * cv + at(*lay, q + 2) * keep;
            (*lay)[q + 3] = a + at(*lay, q + 3) * keep;
        }
}

/* ---- the drawing's state: its layers, masks, gradients and scratch ---- */

/* where the drawing is: the layer's size and its place in the canvas's px, the matrix from
   the viewport's CSS px to its px, and how deep in tiles, masks and images drawn for others
   it is (8 at most). The drawing's context (RC) holds it, the layers and their masks, the
   gradients and patterns, scratch, and the loader an image element's resource comes from
   (none: images are not drawn) */
typedef struct sv_dev { int w, h, x0, y0, depth; sv_mat dev; } sv_dev;

#define RC const sv_dev *dv, vector<float> *lay, vector<double> *lop, vector<float> *masks, vector<int32_t> *lmk, vector<double> *gr, \
           vector<uint32_t> *gix, vector<double> *path, vector<double> *edges, vector<float> *cov, vector<double> *pts, vector<uint8_t> *corner, \
           vector<uint32_t> *subs, vector<double> *dp, vector<sv_mseg> *dcf, vector<double> *ds, vector<double> *tmp, vector<double> *cl, \
           vector<uint8_t> *ccf, vector<double> *xs, vector<uint32_t> *order, vector<uint32_t> *act, vector<float> *acc, vector<float> *tiles, \
           const net_loader *nt
#define RA dv, lay, lop, masks, lmk, gr, gix, path, edges, cov, pts, corner, subs, dp, dcf, ds, tmp, cl, ccf, xs, order, act, acc, tiles, nt

static size_t sv_top(RC) { return len(*lay) - (size_t)dv->w * (size_t)dv->h * 4; }

static void sv_render(const char *s, size_t n, size_t i, int w, int h, sv_mat dev, int depth, const net_loader *nt, vector<float> *layer);

/* a pattern's tile drawn for a paint under the matrix full (the layer's px from the pattern's
   space; its record at o: the matrix into its referencing element's space, the tile's size,
   its content's place in the scene), as Skia rasterizes a picture shader's tile: at the scale
   the matrix's columns give it, rounded up to whole px (4M px at most), into the tiles; the
   ink then maps the layer's px into the tile's image. 0 when it draws nothing */
static int sv_tile(const char *s, RC, size_t o, sv_mat full, sv_ink *ink) {
    double tw = at(*gr, o + 7), th = at(*gr, o + 8);
    size_t from = (size_t)at(*gr, o + 9), to = (size_t)at(*gr, o + 10);
    if (dv->depth >= 8 || !(tw > 0 && th > 0)) return 0;
    /* the scale in floats, as Skia's matrix reckons it: a column's length a hair over 1 in
       doubles would round the tile up a px */
    float fa = (float)full.a, fb = (float)full.b, fc = (float)full.c, fd = (float)full.d;
    float lx = (float)sqrt((double)(fa * fa + fb * fb)), ly = (float)sqrt((double)(fc * fc + fd * fd));
    double sw = (double)(lx * (float)tw), sh = (double)(ly * (float)th);
    if (!(sw > 0 && sh > 0 && sw < 1e7 && sh < 1e7)) return 0;
    if (sw * sh > 2048.0 * 2048.0) {
        double k = sqrt(2048.0 * 2048.0 / (sw * sh));
        sw *= k;
        sh *= k;
    }
    int W = (int)ceil(sw), H = (int)ceil(sh);
    if (W < 1 || H < 1) return 0;
    double rx = W / tw, ry = H / th;
    if (!sv_inv(sv_mul(full, sv_make(1 / rx, 0, 0, 1 / ry, 0, 0)), &ink->inv)) return 0;
    vector<float> tile;
    sv_render(s, to, from, W, H, sv_make(rx, 0, 0, ry, 0, 0), dv->depth + 1, nt, &tile);
    /* kept as Skia keeps the tile's image: eight bits a channel */
    for (size_t q = 0; q < len(tile); q++) tile[q] = (float)(floor(at(tile, q) * 255 + 0.5) / 255);
    ink->toff = len(*tiles);
    append_range(tiles, data(tile), len(tile));
    ink->tw = W;
    ink->th = H;
    ink->kind = 3;
    ink->a = 1;
    return 1;
}

/* a paint's words read: n, c RRGGBBAA, u N; with its opacity, under the path's matrix m */
static void sv_rink(const char *s, size_t n, size_t *i, RC, sv_mat m, sv_ink *ink) {
    memset(ink, 0, sizeof *ink);
    size_t a, k;
    if (!sv_tok(s, n, i, 1, &a, &k)) return;
    if (s[a] == 'c') {
        uint32_t c = sv_rhex(s, n, i);
        ink->kind = 1;
        float al = (float)(c & 0xff) / 255;
        ink->r = (float)(c >> 24) / 255 * al;
        ink->g = (float)(c >> 16 & 0xff) / 255 * al;
        ink->b = (float)(c >> 8 & 0xff) / 255 * al;
        ink->a = al;
    } else if (s[a] == 'u') {
        uint32_t id = (uint32_t)sv_rnum(s, n, i);
        if (id < len(*gix) && at(*gix, id)) {
            size_t o = at(*gix, id) - 1;
            sv_mat gm = sv_make(at(*gr, o + 1), at(*gr, o + 2), at(*gr, o + 3), at(*gr, o + 4), at(*gr, o + 5), at(*gr, o + 6));
            if ((int)at(*gr, o) == 2) {
                if (!sv_tile(s, RA, o, sv_mul(dv->dev, sv_mul(m, gm)), ink)) memset(ink, 0, sizeof *ink);
            } else if (sv_inv(sv_mul(dv->dev, sv_mul(m, gm)), &ink->inv)) {
                ink->kind = 2;
                ink->goff = o;
                ink->a = 1;
            }
        }
    }
}
static void sv_ink_opacity(sv_ink *ink, double op) {
    float o = (float)(op < 0 ? 0 : op > 1 ? 1 : op);
    ink->r *= o; ink->g *= o; ink->b *= o; ink->a *= o;
}

/* the path's fill: its subpaths closed, under the full matrix */
static void sv_fill_edges(RC, sv_mat full) {
    sv_flatten(path, full, 0.05, pts, corner, subs);
    clear(*edges);
    for (size_t s = 0; s + 2 < len(*subs); s += 3) {
        uint32_t b = at(*subs, s), c = at(*subs, s + 1);
        for (uint32_t j = 0; j < c; j++) {
            uint32_t l = j + 1 == c ? 0 : j + 1;
            double x0 = at(*pts, 2 * (b + j)), y0 = at(*pts, 2 * (b + j) + 1), x1 = at(*pts, 2 * (b + l)), y1 = at(*pts, 2 * (b + l) + 1);
            if (y0 == y1) continue;
            push_back(edges, x0);
            push_back(edges, y0);
            push_back(edges, x1);
            push_back(edges, y1);
        }
    }
}

/* the path's stroke as pieces under full (its user space's matrix): dashed first, as Skia
   cuts dashes (its measure's tolerance half a device px), then each subpath outlined in
   user space; a stroke no wider than a device px outlined a px wide in the device's, its
   alpha its width (*alpha), as Skia draws a hairline */
static void sv_stroke_edges(RC, sv_mat full, const sv_pen *pen0, const double *dash, int nd, double offset, double *alpha) {
    sv_pen pen = *pen0;
    double w = 2 * pen.hw;
    double c0 = sqrt(full.a * full.a + full.b * full.b), c1 = sqrt(full.c * full.c + full.d * full.d);
    double l0 = c0 * w, l1 = c1 * w, rs = fmax(c0, c1);
    double scale = sv_scale(full);
    int hair = l0 <= 1 && l1 <= 1;
    *alpha = hair ? (l0 + l1) / 2 : 1;
    int dashed = nd > 0 && sv_dash_path(path, dash, nd, offset, 0.5 / (rs > 0 ? rs : 1), dp, ds, dcf);
    sv_flatten(dashed ? dp : path, hair ? full : sv_id(), hair ? 0.05 : 0.05 / (scale > 0 ? scale : 1), pts, corner, subs);
    clear(*edges);
    sv_mat lm = hair ? sv_id() : full;
    pen.rd = hair ? 1 : scale;
    if (hair) pen.hw = 0.5;
    for (size_t s = 0; s + 2 < len(*subs); s += 3) {
        uint32_t b = at(*subs, s), c = at(*subs, s + 1), fl = at(*subs, s + 2);
        if (!(fl & 2)) continue;
        /* a subpath of no length capped upright, as Skia caps one */
        sv_stroke_line(data(*pts) + 2 * b, data(*corner) + b, (int)c, (fl & 1) != 0, 1, 0, &pen, lm, edges, tmp, cl, ccf);
    }
}

/* ---- hairlines as Skia draws them (SkScan_Antihair, SkScan_Hairline) ---- */

/* an alpha (0 to 255) blitted onto a px of the coverage cov (its box b): united with what is
   there, as one blit over another blends */
static void sv_hair_px(vector<float> *cov, const int *b, int x, int y, int a) {
    if (a <= 0 || x < b[0] || x >= b[2] || y < b[1] || y >= b[3]) return;
    size_t q = (size_t)(y - b[1]) * (size_t)(b[2] - b[0]) + (size_t)(x - b[0]);
    float v = (a > 255 ? 255 : a) / 255.0f, c = at(*cov, q);
    (*cov)[q] = c + v * (1 - c);
}
static int sv_fdot6_floor(int32_t v) { return (int)(v >= 0 ? v / 64 : -((-v + 63) / 64)); }
/* one end's or one run's px across a hairline (its blitters' drawCap and drawLine): horiz the
   line runs across (fy its y, 16.16, at the px's middle), else down (its x); a run from i to
   stop, each its two px shared by where the line passes; a cap's alphas scaled by mod64 */
static int64_t sv_hair_run(vector<float> *cov, const int *b, int horiz, int flat, int i, int stop, int64_t f, int64_t slope, int mod64) {
    f += 32768;
    for (; i < stop; i++) {
        int k = (int)(f >> 16), a = (int)((f >> 8) & 0xff);
        int lo = mod64 >= 0 ? (a * mod64) >> 6 : a, hi = mod64 >= 0 ? ((255 - a) * mod64) >> 6 : 255 - a;
        if (horiz) { sv_hair_px(cov, b, i, k, lo); sv_hair_px(cov, b, i, k - 1, hi); }
        else { sv_hair_px(cov, b, k, i, lo); sv_hair_px(cov, b, k - 1, i, hi); }
        if (!flat) f += slope;
    }
    return f - 32768;
}
/* a hairline's segment, its ends in 26.6 fixed point (Skia's SkFDot6), drawn as
   do_anti_hairline draws it: a px of the major axis at a time from the first end's px to the
   last's, the ends' px covered by the share of them the line reaches across */
static void sv_hair_seg(vector<float> *cov, const int *b, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int depth) {
    if (depth < 16 && (labs((long)x1 - x0) > 511 * 64 || labs((long)y1 - y0) > 511 * 64)) {
        int32_t hx = (x0 >> 1) + (x1 >> 1), hy = (y0 >> 1) + (y1 >> 1);
        sv_hair_seg(cov, b, x0, y0, hx, hy, depth + 1);
        sv_hair_seg(cov, b, hx, hy, x1, y1, depth + 1);
        return;
    }
    int horiz = labs((long)x1 - x0) > labs((long)y1 - y0);
    if (horiz ? x0 > x1 : y0 > y1) {
        int32_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    int32_t a0 = horiz ? x0 : y0, a1 = horiz ? x1 : y1, c0 = horiz ? y0 : x0, c1 = horiz ? y1 : x1;
    if (a0 == a1 && c0 == c1) return;
    int istart = sv_fdot6_floor(a0), istop = sv_fdot6_floor(a1 + 63);
    int64_t f = (int64_t)c0 << 10, slope = 0;
    int flat = c0 == c1;
    if (!flat) {
        slope = ((int64_t)(c1 - c0) << 16) / (a1 - a0);
        f += (slope * (32 - (a0 & 63)) + 32) >> 6;
    }
    int scale0, scale1;
    if (istop - istart == 1) { scale0 = a1 - a0; scale1 = 0; }
    else { scale0 = 64 - (a0 & 63); scale1 = a1 & 63; }
    f = sv_hair_run(cov, b, horiz, flat, istart, istart + 1, f, slope, scale0);
    istart++;
    int full = istop - istart - (scale1 > 0);
    if (full > 0) f = sv_hair_run(cov, b, horiz, flat, istart, istart + full, f, slope, -1);
    if (scale1 > 0) sv_hair_run(cov, b, horiz, flat, istop - 1, istop, f, slope, scale1);
}
static int32_t sv_fdot6(double v) { return (int32_t)(v * 64); }
/* a curve's pieces as Skia's hairlines take them: a quadratic halved while its middle is a px
   or more off its chord (2^level lines), a cubic in 2^k lines while its controls stray an
   eighth of a px, then a quarter fourfold each step, from its chord's thirds */
static int sv_hair_pieces(const double *z, int quad) {
    if (quad) {
        double dx = fabs((z[0] + z[4]) / 2 - z[2]), dy = fabs((z[1] + z[5]) / 2 - z[3]);
        uint32_t ix = (uint32_t)ceil(dx), iy = (uint32_t)ceil(dy), dd = ix > iy ? ix + (iy >> 1) : iy + (ix >> 1);
        int bits = 0;
        while (bits < 32 && (dd >> bits)) bits++;
        int level = (33 - (32 - bits)) >> 1;
        if (level > 5) level = 5;
        return 1 << level;
    }
    double d = 0;
    for (int c = 0; c < 2; c++) {
        double p13 = z[6 + c] / 3 + z[c] * 2 / 3, p23 = z[c] / 3 + z[6 + c] * 2 / 3;
        d = fmax(d, fmax(fabs(z[2 + c] - p13), fabs(z[4 + c] - p23)));
    }
    double tol = 1.0 / 8;
    for (int k = 0; k < 9; k++) {
        if (d < tol) return 1 << k;
        tol *= 4;
    }
    return 1 << 9;
}
/* a path stroked as a hairline under full (the layer's px from its space): each subpath's
   lines and curves (in Skia's pieces) a polyline, its ends moved half a px out for a round or
   square cap, each segment drawn as Skia's anti-aliased hairline: their coverage, united,
   into cov over b (the polyline's bounds, held to the layer); 0 when it covers nothing */
static int sv_hairline(const vector<double> *p, sv_mat full, int cap, int w, int h, int *b, vector<float> *cov, vector<double> *line) {
    size_t i = 0, n = len(*p);
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    /* the polylines: each point (x, y), a subpath's start marked by its count before it */
    clear(*line);
    double cx = 0, cy = 0, sx = 0, sy = 0;
    vector<uint32_t> starts;
    while (i < n) {
        int c = (int)at(*p, i);
        if (c == SV_M) {
            cx = sx = at(*p, i + 1);
            cy = sy = at(*p, i + 2);
            push_back(starts, (uint32_t)(len(*line) / 2));
            push_back(line, sv_ax(full, cx, cy));
            push_back(line, sv_ay(full, cx, cy));
            i += 3;
            continue;
        }
        if (!len(starts)) { i += (size_t)sv_args(c) + 1; continue; }
        if (c == SV_Z) {
            push_back(line, sv_ax(full, sx, sy));
            push_back(line, sv_ay(full, sx, sy));
            cx = sx;
            cy = sy;
            i++;
            continue;
        }
        if (c == SV_L) {
            cx = at(*p, i + 1);
            cy = at(*p, i + 2);
            push_back(line, sv_ax(full, cx, cy));
            push_back(line, sv_ay(full, cx, cy));
            i += 3;
            continue;
        }
        double q[8], z[8];
        sv_curve_at(p, i, cx, cy, q);
        int quad = c == SV_Q;
        if (quad) {
            double qq[6] = { cx, cy, at(*p, i + 1), at(*p, i + 2), at(*p, i + 3), at(*p, i + 4) };
            for (int k = 0; k < 3; k++) { z[2 * k] = sv_ax(full, qq[2 * k], qq[2 * k + 1]); z[2 * k + 1] = sv_ay(full, qq[2 * k], qq[2 * k + 1]); }
        } else for (int k = 0; k < 4; k++) { z[2 * k] = sv_ax(full, q[2 * k], q[2 * k + 1]); z[2 * k + 1] = sv_ay(full, q[2 * k], q[2 * k + 1]); }
        int pieces = sv_hair_pieces(z, quad);
        for (int k = 1; k <= pieces; k++) {
            double t = (double)k / pieces, u = 1 - t, x, y;
            if (k == pieces) { x = quad ? z[4] : z[6]; y = quad ? z[5] : z[7]; }
            else if (quad) { x = u * u * z[0] + 2 * u * t * z[2] + t * t * z[4]; y = u * u * z[1] + 2 * u * t * z[3] + t * t * z[5]; }
            else {
                x = u * u * u * z[0] + 3 * u * u * t * z[2] + 3 * u * t * t * z[4] + t * t * t * z[6];
                y = u * u * u * z[1] + 3 * u * u * t * z[3] + 3 * u * t * t * z[5] + t * t * t * z[7];
            }
            push_back(line, x);
            push_back(line, y);
        }
        cx = q[6];
        cy = q[7];
        i += (size_t)sv_args(c) + 1;
    }
    size_t np = len(*line) / 2;
    push_back(starts, (uint32_t)np);
    for (size_t k = 0; k < np; k++) {
        lo[0] = fmin(lo[0], at(*line, 2 * k)); hi[0] = fmax(hi[0], at(*line, 2 * k));
        lo[1] = fmin(lo[1], at(*line, 2 * k + 1)); hi[1] = fmax(hi[1], at(*line, 2 * k + 1));
    }
    if (np < 2 || !(lo[0] > -1e6 && lo[1] > -1e6 && hi[0] < 1e6 && hi[1] < 1e6)) return 0;
    b[0] = (int)floor(lo[0]) - 2; b[1] = (int)floor(lo[1]) - 2; b[2] = (int)ceil(hi[0]) + 3; b[3] = (int)ceil(hi[1]) + 3;
    if (b[0] < 0) b[0] = 0;
    if (b[1] < 0) b[1] = 0;
    if (b[2] > w) b[2] = w;
    if (b[3] > h) b[3] = h;
    if (b[2] <= b[0] || b[3] <= b[1]) return 0;
    clear(*cov);
    resize(cov, (size_t)(b[2] - b[0]) * (size_t)(b[3] - b[1]));
    for (size_t s = 0; s + 1 < len(starts); s++) {
        size_t a = at(starts, s), e = at(starts, s + 1);
        if (e - a < 2) continue;
        /* an open polyline's ends moved half a px out for a round or square cap */
        double first[2] = { at(*line, 2 * a), at(*line, 2 * a + 1) }, last[2] = { at(*line, 2 * (e - 1)), at(*line, 2 * (e - 1) + 1) };
        int closed = first[0] == last[0] && first[1] == last[1];
        if (cap && !closed) {
            double dx = first[0] - at(*line, 2 * a + 2), dy = first[1] - at(*line, 2 * a + 3), l = hypot(dx, dy);
            if (l > 0) { first[0] += dx / l * 0.5; first[1] += dy / l * 0.5; }
            dx = last[0] - at(*line, 2 * (e - 2)); dy = last[1] - at(*line, 2 * (e - 2) + 1); l = hypot(dx, dy);
            if (l > 0) { last[0] += dx / l * 0.5; last[1] += dy / l * 0.5; }
        }
        for (size_t k = a; k + 1 < e; k++) {
            double xa = k == a ? first[0] : at(*line, 2 * k), ya = k == a ? first[1] : at(*line, 2 * k + 1);
            double xb = k + 2 == e ? last[0] : at(*line, 2 * k + 2), yb = k + 2 == e ? last[1] : at(*line, 2 * k + 3);
            sv_hair_seg(cov, b, sv_fdot6(xa), sv_fdot6(ya), sv_fdot6(xb), sv_fdot6(yb), 0);
        }
    }
    return 1;
}

/* a path's commands with each point mapped by m, into out */
static void sv_path_map(const vector<double> *p, sv_mat m, vector<double> *out) {
    clear(*out);
    size_t i = 0, n = len(*p);
    while (i < n) {
        int c = (int)at(*p, i);
        int k = sv_args(c);
        push_back(out, (double)c);
        for (int q = 0; q < k; q += 2) {
            double x = at(*p, i + 1 + (size_t)q), y = at(*p, i + 2 + (size_t)q);
            push_back(out, sv_ax(m, x, y));
            push_back(out, sv_ay(m, x, y));
        }
        i += (size_t)k + 1;
    }
}

/* a path's line: its matrix, fill (paint, opacity, rule), stroke (paint, opacity, width,
   cap, join, miter limit, dash offset, dashes), N when its stroke does not scale
   (vector-effect: non-scaling-stroke: outlined, and dashed, in the viewport's CSS px, as
   Blink outlines it in its svg's), path */
static void sv_draw_path(const char *s, size_t n, size_t *i, RC) {
    sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
    sv_ink fi, si;
    clear(*tiles);
    sv_rink(s, n, i, RA, m, &fi);
    sv_ink_opacity(&fi, sv_rnum(s, n, i));
    int evenodd = (int)sv_rnum(s, n, i);
    sv_rink(s, n, i, RA, m, &si);
    sv_ink_opacity(&si, sv_rnum(s, n, i));
    sv_pen pen;
    pen.hw = sv_rnum(s, n, i) / 2;
    pen.cap = (int)sv_rnum(s, n, i);
    pen.join = (int)sv_rnum(s, n, i);
    pen.miter = sv_rnum(s, n, i);
    pen.rd = 1;
    double offset = sv_rnum(s, n, i);
    int nd = (int)sv_rnum(s, n, i);
    double dash[64];
    int kd = 0, bad = 0;
    for (int k = 0; k < nd; k++) {
        double v = sv_rnum(s, n, i);
        if (v < 0) bad = 1;
        if (kd < 64) dash[kd++] = v;
    }
    if (bad) kd = 0;
    size_t save = *i, ta, tk;
    int fixed = sv_tok(s, n, i, 1, &ta, &tk) && tk == 1 && s[ta] == 'N';
    if (!fixed) *i = save;
    sv_rpath(s, n, i, path);
    size_t top = sv_top(RA);
    int b[4];
    if (fi.kind) {
        sv_fill_edges(RA, full);
        if (sv_cover(edges, evenodd, dv->w, dv->h, b, cov, xs, order, act, acc)) sv_paint_cov(lay, top, dv->w, cov, b, &fi, gr, tiles);
    }
    if (si.kind && pen.hw > 0) {
        double alpha = 1;
        if (fixed) {
            vector<double> host;
            sv_path_map(path, m, &host);
            clear(*path);
            append_range(path, data(host), len(host));
            full = dv->dev;
        }
        /* a stroke no wider than a device px each way (Skia's fast lengths: the longer side and
           half the shorter) is a hairline, its alpha its width's average */
        double w2 = 2 * pen.hw, v0x = fabs(full.a * w2), v0y = fabs(full.b * w2), v1x = fabs(full.c * w2), v1y = fabs(full.d * w2);
        double l0 = fmax(v0x, v0y) + fmin(v0x, v0y) / 2, l1 = fmax(v1x, v1y) + fmin(v1x, v1y) / 2;
        if (l0 <= 1 && l1 <= 1) {
            double rs = fmax(hypot(full.a, full.b), hypot(full.c, full.d));
            int dashed = kd > 0 && sv_dash_path(path, dash, kd, offset, 0.5 / (rs > 0 ? rs : 1), dp, ds, dcf);
            sv_ink_opacity(&si, (l0 + l1) / 2);
            if (sv_hairline(dashed ? dp : path, full, pen.cap, dv->w, dv->h, b, cov, tmp)) sv_paint_cov(lay, top, dv->w, cov, b, &si, gr, tiles);
            return;
        }
        sv_stroke_edges(RA, full, &pen, dash, kd, offset, &alpha);
        sv_ink_opacity(&si, alpha);
        if (sv_cover(edges, 0, dv->w, dv->h, b, cov, xs, order, act, acc)) sv_paint_cov(lay, top, dv->w, cov, b, &si, gr, tiles);
    }
}

/* a gradient's line kept: kind, its matrix, its geometry, spread and stops */
static void sv_read_grad(const char *s, size_t n, size_t *i, int radial, RC) {
    uint32_t id = (uint32_t)sv_rnum(s, n, i);
    if (id == 0 || id > 100000) { sv_skip_line(s, n, i); return; }
    if (len(*gix) <= id) resize(gix, (size_t)id + 1);
    (*gix)[id] = (uint32_t)len(*gr) + 1;
    push_back(gr, (double)radial);
    for (int k = 0; k < 6 + (radial ? 6 : 4) + 1; k++) push_back(gr, sv_rnum(s, n, i));
    int ns = (int)sv_rnum(s, n, i);
    if (ns < 0 || ns > 4096) ns = 0;
    push_back(gr, (double)ns);
    for (int k = 0; k < ns; k++) {
        push_back(gr, sv_rnum(s, n, i));
        push_back(gr, (double)sv_rhex(s, n, i));
    }
    sv_skip_line(s, n, i);
}

/* the block whose first line ends at *i (a pattern's P, a mask's M, a filter's F; E ends
   one, blocks nest): where its body begins and ends (its E line's start); *i left at the
   end of its E line */
static void sv_block(const char *s, size_t n, size_t *i, size_t *from, size_t *to) {
    size_t p = *i;
    while (p < n && s[p] != '\n') p++;
    if (p < n) p++;
    *from = p;
    int depth = 1;
    while (p < n) {
        size_t ls = p;
        char c = s[p];
        int one = p + 1 >= n || s[p + 1] == ' ' || s[p + 1] == '\n';
        if (one && (c == 'P' || c == 'M' || c == 'F' || c == 'I')) depth++;
        while (p < n && s[p] != '\n') p++;
        if (one && c == 'E' && --depth == 0) {
            *to = ls;
            *i = p;
            return;
        }
        if (p < n) p++;
    }
    *to = n;
    *i = n;
}

/* a pattern's block kept: P, its number, the matrix from its tile's space into its
   referencing element's, the tile's width and height; its content's lines (the tile's
   drawing, in the tile's space), then E. Its record: 2, the matrix, the size, and where its
   content is in the scene */
static void sv_read_pattern(const char *s, size_t n, size_t *i, RC) {
    uint32_t id = (uint32_t)sv_rnum(s, n, i);
    double v[8];
    for (int k = 0; k < 8; k++) v[k] = sv_rnum(s, n, i);
    size_t from, to;
    sv_block(s, n, i, &from, &to);
    if (id == 0 || id > 100000) return;
    if (len(*gix) <= id) resize(gix, (size_t)id + 1);
    (*gix)[id] = (uint32_t)len(*gr) + 1;
    push_back(gr, 2.0);
    for (int k = 0; k < 8; k++) push_back(gr, v[k]);
    push_back(gr, (double)from);
    push_back(gr, (double)to);
}

/* the flattened path (pts, subs) is one rectangle along the axes, as SkPath::isRect finds one */
static int sv_is_rect(const vector<double> *pts, const vector<uint32_t> *subs) {
    if (len(*subs) != 3) return 0;
    uint32_t b = at(*subs, 0), c = at(*subs, 1);
    if (c == 5 && at(*pts, 2 * b) == at(*pts, 2 * (b + 4)) && at(*pts, 2 * b + 1) == at(*pts, 2 * (b + 4) + 1)) c = 4;
    if (c != 4) return 0;
    double x[4], y[4];
    for (uint32_t j = 0; j < 4; j++) { x[j] = at(*pts, 2 * (b + j)); y[j] = at(*pts, 2 * (b + j) + 1); }
    int a = x[0] == x[1] && y[1] == y[2] && x[2] == x[3] && y[3] == y[0];
    int h = y[0] == y[1] && x[1] == x[2] && y[2] == y[3] && x[3] == x[0];
    return (a || h) && x[0] != x[2] && y[0] != y[2];
}

/* a group's line: a layer of its own, its clip's shapes (their union) its mask; a
   clip-path's (hasclip 2) times its bounding box's own coverage, as Blink clips one: a
   rectangle about it, then its path, each anti-aliased, Skia multiplying the two (a path
   that is a rectangle along the axes a rectangle's clip alone) */
/* ---- masks and filters drawn ---- */

/* a channel kept as Skia keeps a layer's: eight bits */
static float sv_q8(double v) { return (float)(floor((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5) / 255); }

/* a rectangle (x, y, w, h) under m in the layer's px, rounded out (as Skia rounds a crop rect)
   and held to the layer: r (x0, y0, x1, y1); 0 when it holds none */
static int sv_rect_px(const sv_dev *dv, sv_mat m, double x, double y, double w, double h, int *r) {
    double xs[4] = { x, x + w, x + w, x }, ys[4] = { y, y, y + h, y + h };
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int k = 0; k < 4; k++) {
        double px = sv_ax(m, xs[k], ys[k]), py = sv_ay(m, xs[k], ys[k]);
        lo[0] = fmin(lo[0], px); lo[1] = fmin(lo[1], py); hi[0] = fmax(hi[0], px); hi[1] = fmax(hi[1], py);
    }
    if (!(lo[0] < 1e8 && lo[1] < 1e8 && hi[0] > -1e8 && hi[1] > -1e8)) return 0;
    double fx0 = floor(lo[0] + 1e-6), fy0 = floor(lo[1] + 1e-6), fx1 = ceil(hi[0] - 1e-6), fy1 = ceil(hi[1] - 1e-6);
    r[0] = fx0 < 0 ? 0 : fx0 > dv->w ? dv->w : (int)fx0;
    r[1] = fy0 < 0 ? 0 : fy0 > dv->h ? dv->h : (int)fy0;
    r[2] = fx1 < 0 ? 0 : fx1 > dv->w ? dv->w : (int)fx1;
    r[3] = fy1 < 0 ? 0 : fy1 > dv->h ? dv->h : (int)fy1;
    return r[2] > r[0] && r[3] > r[1];
}

/* a mask's block (its M line read to its type): its content drawn into a layer of its own, as
   its group's is, made coverage -- its luminance (Rec. 709's weights on the colours
   premultiplied, as Skia's luma filter takes them) or its alpha, eight bits a channel --
   within its region, and the group's mask (its clip's, else one of its own) multiplied by it */
static void sv_mask_apply(const char *s, size_t n, size_t *i, RC, int32_t *mk) {
    int alpha = (int)sv_rnum(s, n, i);
    double rx = sv_rnum(s, n, i), ry = sv_rnum(s, n, i), rw = sv_rnum(s, n, i), rh = sv_rnum(s, n, i);
    sv_mat m = sv_rmat(s, n, i);
    size_t from, to;
    sv_block(s, n, i, &from, &to);
    size_t px = (size_t)dv->w * (size_t)dv->h;
    if (*mk < 0) {
        *mk = (int32_t)(len(*masks) / (px ? px : 1));
        resize(masks, len(*masks) + px);
        for (size_t q = len(*masks) - px; q < len(*masks); q++) (*masks)[q] = 1;
    }
    size_t mo = (size_t)*mk * px;
    vector<float> ml;
    if (dv->depth < 8) sv_render(s, to, from, dv->w, dv->h, dv->dev, dv->depth + 1, nt, &ml);
    /* the region, a clip without anti-aliasing: along the axes its edges rounded (Skia's
       SkRect::round), else each px in by its centre */
    sv_mat full = sv_mul(dv->dev, m);
    int b[4] = { 0, 0, 0, 0 }, any = len(ml) == px * 4, axes = fabs(full.b) < 1e-9 && fabs(full.c) < 1e-9;
    if (axes) {
        double ex[2] = { sv_ax(full, rx, ry), sv_ax(full, rx + rw, ry + rh) }, ey[2] = { sv_ay(full, rx, ry), sv_ay(full, rx + rw, ry + rh) };
        double x0 = floor(fmin(ex[0], ex[1]) + 0.5), x1 = floor(fmax(ex[0], ex[1]) + 0.5), y0 = floor(fmin(ey[0], ey[1]) + 0.5), y1 = floor(fmax(ey[0], ey[1]) + 0.5);
        b[0] = x0 < 0 ? 0 : x0 > dv->w ? dv->w : (int)x0;
        b[1] = y0 < 0 ? 0 : y0 > dv->h ? dv->h : (int)y0;
        b[2] = x1 < 0 ? 0 : x1 > dv->w ? dv->w : (int)x1;
        b[3] = y1 < 0 ? 0 : y1 > dv->h ? dv->h : (int)y1;
        clear(*cov);
        resize(cov, (size_t)(b[2] > b[0] ? b[2] - b[0] : 0) * (size_t)(b[3] > b[1] ? b[3] - b[1] : 0));
        for (size_t q = 0; q < len(*cov); q++) (*cov)[q] = 1;
    } else {
        clear(*path);
        sv_put2(path, SV_M, rx, ry);
        sv_put2(path, SV_L, rx + rw, ry);
        sv_put2(path, SV_L, rx + rw, ry + rh);
        sv_put2(path, SV_L, rx, ry + rh);
        push_back(path, (double)SV_Z);
        sv_fill_edges(RA, full);
        any = any && sv_cover(edges, 0, dv->w, dv->h, b, cov, xs, order, act, acc);
        for (size_t q = 0; q < len(*cov); q++) (*cov)[q] = at(*cov, q) >= 0.5f ? 1.0f : 0.0f;
    }
    int bw = b[2] - b[0];
    for (int y = 0; y < dv->h; y++)
        for (int x = 0; x < dv->w; x++) {
            size_t q = (size_t)y * (size_t)dv->w + (size_t)x;
            float v = 0;
            if (any && x >= b[0] && x < b[2] && y >= b[1] && y < b[3]) {
                float c = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)(x - b[0]));
                const float *p = data(ml) + 4 * q;
                double lum = alpha ? sv_q8(p[3]) : 0.2126 * sv_q8(p[0]) + 0.7152 * sv_q8(p[1]) + 0.0722 * sv_q8(p[2]);
                v = c * sv_q8(lum);
            }
            (*masks)[mo + q] = at(*masks, mo + q) * v;
        }
}

/* sRGB's transfer function and its inverse */
static double sv_to_lin(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double sv_to_srgb(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055; }

/* a colour c (premultiplied) at alpha a into the other colour space (to 1: linearRGB from sRGB;
   0: back), as Skia's colour filter takes it: unpremultiplied through the transfer function,
   premultiplied again, kept in eight bits */
static float sv_space1(float c, float a, int to_linear) {
    double u = c / a;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return sv_q8((to_linear ? sv_to_lin(u) : sv_to_srgb(u)) * a);
}
/* sv_space1 of each eight-bit colour at each eight-bit alpha, either way (sv_lut[to_linear][a * 256 + c],
   c and a of 255), made the first time it is asked for, once a process: a filter's images are of eight
   bits, so that converting one is a lookup a channel, not a power */
static float sv_lut[2][65536];
static uint8_t sv_lut_ready[2];

/* an image (np px of RGBA premultiplied from off) into the other colour space (sv_space1 each
   colour; transparent black where it is transparent) */
static void sv_space(vector<float> *img, size_t off, size_t np, int to_linear) {
    int t = to_linear ? 1 : 0;
    if (!sv_lut_ready[t]) {
        for (int ka = 1; ka < 256; ka++)
            for (int kc = 0; kc < 256; kc++) sv_lut[t][ka * 256 + kc] = sv_space1((float)(kc / 255.0), (float)(ka / 255.0), t);
        sv_lut_ready[t] = 1;
    }
    for (size_t p = 0; p < np; p++) {
        size_t q = off + 4 * p;
        float a = at(*img, q + 3);
        if (a <= 0) {
            (*img)[q] = (*img)[q + 1] = (*img)[q + 2] = 0;
            continue;
        }
        /* an alpha and colours of eight bits (sv_q8's) by the table, any other by the function */
        int ka = a < 1.0001f ? (int)(a * 255 + 0.5f) : 256;
        int byte = ka >= 1 && ka <= 255 && (float)(ka / 255.0) == a;
        for (int c = 0; c < 3; c++) {
            float v = at(*img, q + (size_t)c);
            int kc = v >= 0 && v < 1.0001f ? (int)(v * 255 + 0.5f) : -1;
            if (byte && kc >= 0 && kc <= 255 && (float)(kc / 255.0) == v) (*img)[q + (size_t)c] = sv_lut[t][ka * 256 + kc];
            else (*img)[q + (size_t)c] = sv_space1(v, a, t);
        }
    }
}

/* a row (or column) of n values, each of eight bits (0 to 255), blurred as Skia's GaussPass
   blurs one: the three box blurs of the window d the deviation gives (SVG's: an odd one
   three centred; an even one two of d, a px to the left and to the right, and one of d + 1),
   summed whole and divided once, rounded; out of the row reads as 0 */
static void sv_blur_run(const double *v, size_t n, int d, double *out, double *t1, double *t2) {
    int l1, r1, l2, r2, l3, r3;
    double div;
    if (d & 1) { l1 = l2 = l3 = (d - 1) / 2; r1 = r2 = r3 = (d - 1) / 2; div = (double)d * d * d; }
    else { l1 = d / 2; r1 = d / 2 - 1; l2 = d / 2 - 1; r2 = d / 2; l3 = d / 2; r3 = d / 2; div = (double)d * d * (d + 1); }
    /* each pass a window's sums, by prefix sums over the row and its margins */
    long m = 3 * d + 2, total = (long)n + 2 * m;
    const double *src = v;
    for (int pass = 0; pass < 3; pass++) {
        int lo = pass == 0 ? l1 : pass == 1 ? l2 : l3, hi = pass == 0 ? r1 : pass == 1 ? r2 : r3;
        double *dst = pass == 0 ? t1 : pass == 1 ? t2 : t1;
        /* src and dst index the extended row: -m .. n + m */
        double run = 0;
        for (long x = -m; x < (long)n + m; x++) {
            /* the window [x - lo, x + hi] of src */
            if (x == -m) {
                run = 0;
                for (long q = x - lo; q <= x + hi; q++) {
                    long k = pass == 0 ? q : q + m;
                    if (pass == 0 ? (q >= 0 && q < (long)n) : (k >= 0 && k < total)) run += src[k];
                }
            } else {
                long qin = x + hi, qout = x - lo - 1;
                long kin = pass == 0 ? qin : qin + m, kout = pass == 0 ? qout : qout + m;
                if (pass == 0 ? (qin >= 0 && qin < (long)n) : (kin >= 0 && kin < total)) run += src[kin];
                if (pass == 0 ? (qout >= 0 && qout < (long)n) : (kout >= 0 && kout < total)) run -= src[kout];
            }
            dst[x + m] = run;
        }
        src = dst;
    }
    for (size_t x = 0; x < n; x++) out[x] = floor(t1[x + (size_t)m] / div + 0.5);
}
/* a deviation's window, as SVG gives it (Skia's BoxBlurWindow) */
static int sv_blur_window(double sigma) {
    double w = floor(sigma * 3 * sqrt(2 * M_PI) / 4 + 0.5);
    return w < 1 ? 1 : w > 2000 ? 2000 : (int)w;
}
/* an image (w x h px of RGBA from off) blurred, sx across and sy down (the layer's px), each
   pass on eight bits: across, then down */
static void sv_blur_img(vector<float> *img, size_t off, int w, int h, double sx, double sy) {
    int dx = sv_blur_window(sx), dy = sv_blur_window(sy);
    int longest = w > h ? w : h;
    long m = 3 * (dx > dy ? dx : dy) + 2;
    vector<double> v, o, t1, t2;
    resize(v, (size_t)longest);
    resize(o, (size_t)longest);
    resize(t1, (size_t)(longest + 2 * m));
    resize(t2, (size_t)(longest + 2 * m));
    for (int axis = 0; axis < 2; axis++) {
        int d = axis == 0 ? dx : dy;
        if (d <= 1) continue;
        int lines = axis == 0 ? h : w, n = axis == 0 ? w : h;
        for (int ln = 0; ln < lines; ln++)
            for (int c = 0; c < 4; c++) {
                int any = 0;
                for (int k = 0; k < n; k++) {
                    size_t q = off + 4 * (axis == 0 ? (size_t)ln * (size_t)w + (size_t)k : (size_t)k * (size_t)w + (size_t)ln) + (size_t)c;
                    v[(size_t)k] = floor(at(*img, q) * 255 + 0.5);
                    if (at(v, (size_t)k) != 0) any = 1;
                }
                if (!any) continue;
                sv_blur_run(data(v), (size_t)n, d, data(o), data(t1), data(t2));
                for (int k = 0; k < n; k++) {
                    size_t q = off + 4 * (axis == 0 ? (size_t)ln * (size_t)w + (size_t)k : (size_t)k * (size_t)w + (size_t)ln) + (size_t)c;
                    (*img)[q] = (float)(at(o, (size_t)k) / 255);
                }
            }
    }
}

/* a blend mode's function of the backdrop's colour b and the source's s, unpremultiplied
   (Compositing and Blending 1, 5.3: the separable modes) */
static double sv_blend_fn(int mode, double b, double s) {
    switch (mode) {
    case 1: return b * s;
    case 2: return b + s - b * s;
    case 3: return fmin(b, s);
    case 4: return fmax(b, s);
    case 5: return b <= 0.5 ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
    case 6: return b == 0 ? 0 : s >= 1 ? 1 : fmin(1, b / (1 - s));
    case 7: return b >= 1 ? 1 : s <= 0 ? 0 : 1 - fmin(1, (1 - b) / s);
    case 8: return s <= 0.5 ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
    case 9: {
        double dd = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : sqrt(b);
        return s <= 0.5 ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (dd - b);
    }
    case 10: return fabs(b - s);
    case 11: return b + s - 2 * b * s;
    default: return s;
    }
}
/* the non-separable modes' helpers (5.8): luminosity, saturation, and setting them */
static double sv_lum(const double *c) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }
static void sv_clip_color(double *c) {
    double l = sv_lum(c), n = fmin(c[0], fmin(c[1], c[2])), x = fmax(c[0], fmax(c[1], c[2]));
    for (int k = 0; k < 3; k++) {
        if (n < 0 && l - n > 0) c[k] = l + (c[k] - l) * l / (l - n);
        if (x > 1 && x - l > 0) c[k] = l + (c[k] - l) * (1 - l) / (x - l);
    }
}
static void sv_set_lum(double *c, double l) {
    double dl = l - sv_lum(c);
    for (int k = 0; k < 3; k++) c[k] += dl;
    sv_clip_color(c);
}
static void sv_set_sat(double *c, double s) {
    int mx = 0, mn = 0;
    for (int k = 1; k < 3; k++) { if (c[k] > c[mx]) mx = k; if (c[k] < c[mn]) mn = k; }
    if (mx == mn) { c[0] = c[1] = c[2] = 0; return; }
    int md = 3 - mx - mn;
    double range = c[mx] - c[mn];
    c[md] = (c[md] - c[mn]) * s / range;
    c[mx] = s;
    c[mn] = 0;
}

/* the inputs a primitive reads, from the source (src: sRGB), the results so far (res, each in
   its space), or none: a copy of input k in the colour space asked (linear 1) into img */
static void sv_fe_input_img(const vector<float> *src, const vector<float> *res, const vector<int32_t> *spaces, int k, int linear, size_t np,
                            vector<float> *img) {
    clear(*img);
    resize(img, np * 4);
    if (k == SV_IN_NONE) return;
    if (k == SV_IN_SOURCE || k == SV_IN_ALPHA) {
        for (size_t q = 0; q < np * 4; q++) (*img)[q] = at(*src, q);
        if (k == SV_IN_ALPHA) {
            for (size_t p = 0; p < np; p++) (*img)[4 * p] = (*img)[4 * p + 1] = (*img)[4 * p + 2] = 0;
            return;
        }
        if (linear) sv_space(img, 0, np, 1);
        return;
    }
    if (k < 0 || (size_t)k >= len(*spaces)) return;
    size_t o = (size_t)k * np * 4;
    for (size_t q = 0; q < np * 4; q++) (*img)[q] = at(*res, o + q);
    int sp = at(*spaces, (size_t)k);
    if (sp != linear) sv_space(img, 0, np, linear);
}

/* an image moved by (dx, dy) of the layer's px: whole ones as they are, else sampled
   bilinearly; transparent from outside it */
static void sv_offset_img(const vector<float> *in, int w, int h, double dx, double dy, vector<float> *out) {
    size_t np = (size_t)w * (size_t)h;
    clear(*out);
    resize(out, np * 4);
    int whole = fabs(dx - floor(dx + 0.5)) < 1e-6 && fabs(dy - floor(dy + 0.5)) < 1e-6;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t q = ((size_t)y * (size_t)w + (size_t)x) * 4;
            double sx = x - dx, sy = y - dy;
            if (whole) {
                long ix = (long)floor(sx + 0.5), iy = (long)floor(sy + 0.5);
                if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
                size_t r = ((size_t)iy * (size_t)w + (size_t)ix) * 4;
                for (int c = 0; c < 4; c++) (*out)[q + (size_t)c] = at(*in, r + (size_t)c);
                continue;
            }
            double fx = floor(sx), fy = floor(sy), ax = sx - fx, ay = sy - fy;
            for (int c = 0; c < 4; c++) {
                double v = 0;
                for (int j = 0; j < 4; j++) {
                    long ix = (long)fx + (j & 1), iy = (long)fy + (j >> 1);
                    if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
                    double wt = ((j & 1) ? ax : 1 - ax) * ((j >> 1) ? ay : 1 - ay);
                    v += wt * at(*in, ((size_t)iy * (size_t)w + (size_t)ix) * 4 + (size_t)c);
                }
                (*out)[q + (size_t)c] = sv_q8(v);
            }
        }
}

/* feTurbulence's noise (Filter Effects 1, 15.21's reference code, as Skia's SkPerlinNoiseShader
   keeps it): the lattice selector and each channel's gradients, permuted by the selector,
   normalized and kept in sixteen bits */
typedef struct sv_perlin { int lat[256]; double grad[4][256][2]; } sv_perlin;
static long sv_rand(long s) {
    long r = 16807 * (s % 127773) - 2836 * (s / 127773);
    return r <= 0 ? r + 2147483647 : r;
}
static void sv_perlin_init(sv_perlin *pn, double seed) {
    /* the seed truncated, as the spec's setup_seed takes it */
    double t = trunc(seed);
    long s = t > 2147483646 ? 2147483646 : t < -2147483646 ? -2147483646 : (long)t;
    if (s <= 0) s = -(s % 2147483646) + 1;
    int noise[4][256][2];
    for (int ch = 0; ch < 4; ch++)
        for (int i = 0; i < 256; i++) {
            pn->lat[i] = i;
            s = sv_rand(s);
            noise[ch][i][0] = (int)(s % 512);
            s = sv_rand(s);
            noise[ch][i][1] = (int)(s % 512);
        }
    for (int i = 255; i > 0; i--) {
        int k = pn->lat[i];
        s = sv_rand(s);
        int j = (int)(s % 256);
        pn->lat[i] = pn->lat[j];
        pn->lat[j] = k;
    }
    for (int ch = 0; ch < 4; ch++)
        for (int i = 0; i < 256; i++) {
            double gx = (noise[ch][pn->lat[i]][0] - 256) / 256.0, gy = (noise[ch][pn->lat[i]][1] - 256) / 256.0, l = sqrt(gx * gx + gy * gy);
            if (l > 0) { gx /= l; gy /= l; }
            pn->grad[ch][i][0] = floor((gx + 1) * 32767.5 + 0.5) * (2.0 / 65535) - 1;
            pn->grad[ch][i][1] = floor((gy + 1) * 32767.5 + 0.5) * (2.0 / 65535) - 1;
        }
}
/* the noise of a channel at (vx, vy), its lattice wrapped at the stitch's width and height when
   st is given (st[0] the width, st[1] the height, each the tile's at the octave) */
static double sv_noise2(const sv_perlin *pn, int ch, double vx, double vy, const double *st) {
    if (!(fabs(vx) < 1e15 && fabs(vy) < 1e15)) return 0;
    double fx = floor(vx), fy = floor(vy), rx0 = vx - fx, ry0 = vy - fy;
    long bx0 = (long)fx, by0 = (long)fy, bx1 = bx0 + 1, by1 = by0 + 1;
    if (st) {
        if (bx0 >= st[0]) bx0 -= (long)st[0];
        if (bx1 >= st[0]) bx1 -= (long)st[0];
        if (by0 >= st[1]) by0 -= (long)st[1];
        if (by1 >= st[1]) by1 -= (long)st[1];
    }
    int i = pn->lat[bx0 & 255], j = pn->lat[bx1 & 255];
    int b00 = (int)((i + by0) & 255), b10 = (int)((j + by0) & 255), b01 = (int)((i + by1) & 255), b11 = (int)((j + by1) & 255);
    double sx = rx0 * rx0 * (3 - 2 * rx0), sy = ry0 * ry0 * (3 - 2 * ry0);
    const double *q = pn->grad[ch][b00];
    double u = rx0 * q[0] + ry0 * q[1];
    q = pn->grad[ch][b10];
    double v = (rx0 - 1) * q[0] + ry0 * q[1], a = u + sx * (v - u);
    q = pn->grad[ch][b01];
    u = rx0 * q[0] + (ry0 - 1) * q[1];
    q = pn->grad[ch][b11];
    v = (rx0 - 1) * q[0] + (ry0 - 1) * q[1];
    double b = u + sx * (v - u);
    return a + sy * (b - a);
}

/* a row (or column) of n values eroded or dilated: each the least (or greatest) of the window of
   2r + 1 about it, outside the row 0 (transparent), as van Herk and Gil and Werman find them
   (the window's blocks' running extremes from either side); into out */
static void sv_morph_run(const float *v, int n, int r, int dilate, float *out, vector<float> *g, vector<float> *hh) {
    int k = 2 * r + 1, m = n + 2 * r, nb = (m + k - 1) / k, len2 = nb * k;
    clear(*g);
    clear(*hh);
    resize(g, (size_t)len2);
    resize(hh, (size_t)len2);
    /* the padded row: r of 0 each side, then 0 to whole blocks */
    for (int b = 0; b < nb; b++) {
        int base = b * k;
        for (int q = 0; q < k; q++) {
            int x = base + q - r;
            float val = x >= 0 && x < n ? v[x] : 0;
            float prev = q ? at(*g, (size_t)(base + q - 1)) : val;
            (*g)[(size_t)(base + q)] = q == 0 ? val : dilate ? fmaxf(prev, val) : fminf(prev, val);
        }
        for (int q = k - 1; q >= 0; q--) {
            int x = base + q - r;
            float val = x >= 0 && x < n ? v[x] : 0;
            float next = q < k - 1 ? at(*hh, (size_t)(base + q + 1)) : val;
            (*hh)[(size_t)(base + q)] = q == k - 1 ? val : dilate ? fmaxf(next, val) : fminf(next, val);
        }
    }
    /* the window of x (padded x .. x + 2r): h at its start, g at its end */
    for (int x = 0; x < n; x++) {
        float a = at(*hh, (size_t)x), b = at(*g, (size_t)(x + 2 * r));
        out[x] = dilate ? fmaxf(a, b) : fminf(a, b);
    }
}
/* an image (w x h px of RGBA) eroded or dilated by rx across and ry down, each channel alone */
static void sv_morph_img(vector<float> *img, int w, int h, int rx, int ry, int dilate) {
    int longest = w > h ? w : h;
    vector<float> v, o, g, hh;
    resize(v, (size_t)longest);
    resize(o, (size_t)longest);
    for (int axis = 0; axis < 2; axis++) {
        int r = axis == 0 ? rx : ry;
        if (r <= 0) continue;
        int lines = axis == 0 ? h : w, n = axis == 0 ? w : h;
        for (int ln = 0; ln < lines; ln++)
            for (int c = 0; c < 4; c++) {
                for (int k = 0; k < n; k++)
                    v[(size_t)k] = at(*img, 4 * (axis == 0 ? (size_t)ln * (size_t)w + (size_t)k : (size_t)k * (size_t)w + (size_t)ln) + (size_t)c);
                sv_morph_run(data(v), n, r, dilate, data(o), &g, &hh);
                for (int k = 0; k < n; k++)
                    (*img)[4 * (axis == 0 ? (size_t)ln * (size_t)w + (size_t)k : (size_t)k * (size_t)w + (size_t)ln) + (size_t)c] = at(o, (size_t)k);
            }
    }
}

/* a pixel's surface normal for the lighting primitives (Filter Effects 1, 15.10, as Skia's
   lighting filter finds it): Sobel's kernels over the alpha (0 to 255) about (x, y), a px
   outside the bounds b (x0, y0, x1, y1) the nearest inside (Skia's clamp, where the spec has
   kernels of its own at the edges: measured), a quarter of each difference; the normal
   (-ss Nx, -ss Ny, 1) made of length 1 into n, ss the surface scale over 255 */
static void sv_light_normal(const vector<float> *in, int w, const int *b, int x, int y, double ss, double *n) {
    double m[3][3];
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int xx = x + i, yy = y + j;
            if (xx < b[0]) xx = b[0];
            if (xx >= b[2]) xx = b[2] - 1;
            if (yy < b[1]) yy = b[1];
            if (yy >= b[3]) yy = b[3] - 1;
            m[j + 1][i + 1] = floor(at(*in, 4 * ((size_t)yy * (size_t)w + (size_t)xx) + 3) * 255 + 0.5);
        }
    double sx = (m[0][2] + 2 * m[1][2] + m[2][2]) - (m[0][0] + 2 * m[1][0] + m[2][0]);
    double sy = (m[2][0] + 2 * m[2][1] + m[2][2]) - (m[0][0] + 2 * m[0][1] + m[0][2]);
    double nx = -ss * sx / 4, ny = -ss * sy / 4, len = sqrt(nx * nx + ny * ny + 1);
    n[0] = nx / len;
    n[1] = ny / len;
    n[2] = 1 / len;
}

/* a filter's block (from its F line to its E) drawn: the group's layer at top through its
   primitives, in the layer's px (Filter Effects 1, as Blink builds them into Skia's image
   filters): the layer is the source graphic, kept in eight bits, its alpha the source alpha;
   each primitive reads its inputs in its colour space (each converted there, in eight bits,
   from the space it was made in), its deviations and offsets mapped through the matrix, and
   its result is cropped to its subregion (rounded out to whole px); the last result, back in
   sRGB and cropped to the filter's region, is the layer from then on */
static void sv_filter_apply(const char *s, size_t from, size_t to, RC, size_t top) {
    size_t i = from, a, k;
    if (!sv_tok(s, to, &i, 0, &a, &k) || k != 1 || s[a] != 'F') return;
    double rx = sv_rnum(s, to, &i), ry = sv_rnum(s, to, &i), rw = sv_rnum(s, to, &i), rh = sv_rnum(s, to, &i);
    sv_mat m = sv_rmat(s, to, &i), full = sv_mul(dv->dev, m);
    int np = (int)sv_rnum(s, to, &i);
    sv_skip_line(s, to, &i);
    int W = dv->w, H = dv->h;
    size_t NP = (size_t)W * (size_t)H;
    int reg[4];
    if (np <= 0 || np > 64 || (double)np * NP > 8e6 || !sv_rect_px(dv, full, rx, ry, rw, rh, reg)) {
        /* nothing drawn: a region of no px, or too many to keep */
        for (size_t q = 0; q < NP * 4; q++) (*lay)[top + q] = 0;
        return;
    }
    double sxs = hypot(full.a, full.b), sys = hypot(full.c, full.d);
    vector<float> src, res, in1, in2, tmpi;
    vector<int32_t> spaces;
    resize(src, NP * 4);
    for (size_t q = 0; q < NP * 4; q++) src[q] = sv_q8(at(*lay, top + q));
    for (int p = 0; p < np; p++) {
        if (!sv_tok(s, to, &i, 0, &a, &k) || k != 1 || s[a] != 'x') break;
        int kind = (int)sv_rnum(s, to, &i), i1 = (int)sv_rnum(s, to, &i), i2 = (int)sv_rnum(s, to, &i), linear = (int)sv_rnum(s, to, &i);
        double sr[4];
        for (int q = 0; q < 4; q++) sr[q] = sv_rnum(s, to, &i);
        size_t base = len(res);
        resize(res, base + NP * 4);
        push_back(spaces, linear);
        if (i1 >= p) i1 = SV_IN_NONE;
        if (i2 >= p) i2 = SV_IN_NONE;
        /* the subregion's px (rounded out), which crops the result */
        int sub[4];
        if (!sv_rect_px(dv, full, sr[0], sr[1], sr[2], sr[3], sub)) sub[0] = sub[1] = sub[2] = sub[3] = 0;
        if (kind == SV_FE_BLUR || kind == SV_FE_SHADOW) {
            double sdx = sv_rnum(s, to, &i) * sxs, sdy = sv_rnum(s, to, &i) * sys;
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            if (kind == SV_FE_BLUR) {
                sv_blur_img(&in1, 0, W, H, sdx, sdy);
                for (size_t q = 0; q < NP * 4; q++) res[base + q] = at(in1, q);
            } else {
                double ox = sv_rnum(s, to, &i), oy = sv_rnum(s, to, &i);
                uint32_t col = sv_rhex(s, to, &i);
                double ca = (col & 0xff) / 255.0, cc[3];
                for (int c = 0; c < 3; c++) {
                    double u = (col >> (24 - 8 * c) & 0xff) / 255.0;
                    cc[c] = linear ? sv_to_lin(u) : u;
                }
                /* the shadow: the input blurred, its alpha in the colour, moved */
                vector<float> sh;
                resize(sh, NP * 4);
                for (size_t q = 0; q < NP * 4; q++) sh[q] = at(in1, q);
                sv_blur_img(&sh, 0, W, H, sdx, sdy);
                for (size_t q = 0; q < NP; q++) {
                    double al = at(sh, 4 * q + 3) * ca;
                    for (int c = 0; c < 3; c++) sh[4 * q + (size_t)c] = sv_q8(cc[c] * al);
                    sh[4 * q + 3] = sv_q8(al);
                }
                sv_offset_img(&sh, W, H, full.a * ox + full.c * oy, full.b * ox + full.d * oy, &tmpi);
                for (size_t q = 0; q < NP; q++) {
                    double ia = at(in1, 4 * q + 3);
                    for (int c = 0; c < 4; c++) res[base + 4 * q + (size_t)c] = sv_q8(at(in1, 4 * q + (size_t)c) + at(tmpi, 4 * q + (size_t)c) * (1 - ia));
                }
            }
        } else if (kind == SV_FE_OFFSET) {
            double ox = sv_rnum(s, to, &i), oy = sv_rnum(s, to, &i);
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            sv_offset_img(&in1, W, H, full.a * ox + full.c * oy, full.b * ox + full.d * oy, &tmpi);
            for (size_t q = 0; q < NP * 4; q++) res[base + q] = at(tmpi, q);
        } else if (kind == SV_FE_FLOOD) {
            uint32_t col = sv_rhex(s, to, &i);
            double ca = (col & 0xff) / 255.0;
            float fc[4];
            for (int c = 0; c < 3; c++) {
                double u = (col >> (24 - 8 * c) & 0xff) / 255.0;
                fc[c] = sv_q8((linear ? sv_to_lin(u) : u) * ca);
            }
            fc[3] = sv_q8(ca);
            for (size_t q = 0; q < NP; q++)
                for (int c = 0; c < 4; c++) res[base + 4 * q + (size_t)c] = fc[c];
        } else if (kind == SV_FE_COMPOSITE) {
            int op = (int)sv_rnum(s, to, &i);
            double k1 = sv_rnum(s, to, &i), k2 = sv_rnum(s, to, &i), k3 = sv_rnum(s, to, &i), k4 = sv_rnum(s, to, &i);
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            sv_fe_input_img(&src, &res, &spaces, i2, linear, NP, &in2);
            for (size_t q = 0; q < NP; q++) {
                const float *A = data(in1) + 4 * q, *B = data(in2) + 4 * q;
                double o[4];
                for (int c = 0; c < 4; c++) {
                    double x = A[c], y = B[c];
                    o[c] = op == 1 ? x * B[3] : op == 2 ? x * (1 - B[3]) : op == 3 ? x * B[3] + y * (1 - A[3]) : op == 4 ? x * (1 - B[3]) + y * (1 - A[3]) :
                           op == 5 ? k1 * x * y + k2 * x + k3 * y + k4 : op == 6 ? x + y : x + y * (1 - A[3]);
                    o[c] = o[c] < 0 ? 0 : o[c] > 1 ? 1 : o[c];
                }
                for (int c = 0; c < 3; c++) if (op == 5 && o[c] > o[3]) o[c] = o[3];
                for (int c = 0; c < 4; c++) res[base + 4 * q + (size_t)c] = sv_q8(o[c]);
            }
        } else if (kind == SV_FE_MERGE) {
            int cnt = (int)sv_rnum(s, to, &i);
            for (int j = 0; j < cnt && j < 64; j++) {
                int mi = (int)sv_rnum(s, to, &i);
                if (mi >= p) mi = SV_IN_NONE;
                sv_fe_input_img(&src, &res, &spaces, mi, linear, NP, &in1);
                for (size_t q = 0; q < NP; q++) {
                    double ia = at(in1, 4 * q + 3);
                    for (int c = 0; c < 4; c++) res[base + 4 * q + (size_t)c] = sv_q8(at(in1, 4 * q + (size_t)c) + at(res, base + 4 * q + (size_t)c) * (1 - ia));
                }
            }
        } else if (kind == SV_FE_MATRIX) {
            double mx[20];
            for (int q = 0; q < 20; q++) mx[q] = sv_rnum(s, to, &i);
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            for (size_t q = 0; q < NP; q++) {
                const float *A = data(in1) + 4 * q;
                double u[4] = { 0, 0, 0, A[3] }, o[4];
                if (A[3] > 0) for (int c = 0; c < 3; c++) u[c] = A[c] / A[3];
                for (int row = 0; row < 4; row++) {
                    double v = mx[5 * row] * u[0] + mx[5 * row + 1] * u[1] + mx[5 * row + 2] * u[2] + mx[5 * row + 3] * u[3] + mx[5 * row + 4];
                    o[row] = v < 0 ? 0 : v > 1 ? 1 : v;
                }
                for (int c = 0; c < 3; c++) res[base + 4 * q + (size_t)c] = sv_q8(o[c] * o[3]);
                res[base + 4 * q + 3] = sv_q8(o[3]);
            }
        } else if (kind == SV_FE_BLEND) {
            int mode = (int)sv_rnum(s, to, &i);
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            sv_fe_input_img(&src, &res, &spaces, i2, linear, NP, &in2);
            for (size_t q = 0; q < NP; q++) {
                const float *S = data(in1) + 4 * q, *B = data(in2) + 4 * q;
                double sa = S[3], ba = B[3], o[4];
                o[3] = sa + ba - sa * ba;
                double su[3] = { 0, 0, 0 }, bu[3] = { 0, 0, 0 }, f[3];
                for (int c = 0; c < 3; c++) {
                    su[c] = sa > 0 ? S[c] / sa : 0;
                    bu[c] = ba > 0 ? B[c] / ba : 0;
                }
                if (mode >= 12) {
                    double t[3];
                    for (int c = 0; c < 3; c++) t[c] = mode == 13 ? bu[c] : su[c];
                    if (mode == 12 || mode == 13) {
                        /* hue: the source's hue at the backdrop's saturation and luminosity;
                           saturation: the backdrop's hue and luminosity, the source's saturation */
                        double sat_of[3];
                        for (int c = 0; c < 3; c++) sat_of[c] = mode == 12 ? bu[c] : su[c];
                        double sat = fmax(sat_of[0], fmax(sat_of[1], sat_of[2])) - fmin(sat_of[0], fmin(sat_of[1], sat_of[2]));
                        sv_set_sat(t, sat);
                        sv_set_lum(t, sv_lum(bu));
                    } else if (mode == 14) sv_set_lum(t, sv_lum(bu));
                    else {
                        for (int c = 0; c < 3; c++) t[c] = bu[c];
                        sv_set_lum(t, sv_lum(su));
                    }
                    for (int c = 0; c < 3; c++) f[c] = t[c];
                } else for (int c = 0; c < 3; c++) f[c] = sv_blend_fn(mode, bu[c], su[c]);
                for (int c = 0; c < 3; c++) o[c] = S[c] * (1 - ba) + B[c] * (1 - sa) + sa * ba * f[c];
                for (int c = 0; c < 4; c++) res[base + 4 * q + (size_t)c] = sv_q8(o[c]);
            }
        } else if (kind == SV_FE_TRANSFER) {
            int types[4], cnts[4];
            double vals[4][20];
            for (int ch = 0; ch < 4; ch++) {
                types[ch] = (int)sv_rnum(s, to, &i);
                cnts[ch] = (int)sv_rnum(s, to, &i);
                if (cnts[ch] < 0) cnts[ch] = 0;
                if (cnts[ch] > 20) cnts[ch] = 20;
                for (int q = 0; q < cnts[ch]; q++) vals[ch][q] = sv_rnum(s, to, &i);
            }
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            for (size_t q = 0; q < NP; q++) {
                const float *A = data(in1) + 4 * q;
                double u[4] = { 0, 0, 0, A[3] };
                if (A[3] > 0) for (int c = 0; c < 3; c++) u[c] = A[c] / A[3];
                for (int ch = 0; ch < 4; ch++) {
                    double c = u[ch], v = c;
                    int t = types[ch], nn = cnts[ch];
                    const double *tv = vals[ch];
                    if (t == 1 && nn > 0) {
                        double pos = c * (nn - 1);
                        int kk = (int)floor(pos);
                        if (kk >= nn - 1) v = tv[nn - 1];
                        else if (kk < 0) v = tv[0];
                        else v = tv[kk] + (pos - kk) * (tv[kk + 1] - tv[kk]);
                    } else if (t == 2 && nn > 0) {
                        int kk = (int)floor(c * nn);
                        v = tv[kk >= nn ? nn - 1 : kk < 0 ? 0 : kk];
                    } else if (t == 3 && nn >= 2) v = tv[0] * c + tv[1];
                    else if (t == 4 && nn >= 3) v = tv[0] * pow(c, tv[1]) + tv[2];
                    u[ch] = v < 0 ? 0 : v > 1 ? 1 : v;
                }
                for (int c = 0; c < 3; c++) res[base + 4 * q + (size_t)c] = sv_q8(u[c] * u[3]);
                res[base + 4 * q + 3] = sv_q8(u[3]);
            }
        } else if (kind == SV_FE_TURBULENCE) {
            /* the noise in the filter's user space, each px's at its centre mapped there and half a
               unit on (Skia's perlin_noise stage), each channel's turbulence (or fractal sum) of
               its octaves; stitched, the tile the subregion's size (in whole units) from the
               space's origin, as Skia stitches it; premultiplied, in eight bits */
            int type = (int)sv_rnum(s, to, &i);
            double bfx = sv_rnum(s, to, &i), bfy = sv_rnum(s, to, &i);
            int oct = (int)sv_rnum(s, to, &i);
            double seed = sv_rnum(s, to, &i);
            int stitch = (int)sv_rnum(s, to, &i);
            sv_mat inv;
            if (oct > 0 && sv_inv(full, &inv)) {
                /* (past 24 octaves a term is below what eight bits keep) */
                if (oct > 24) oct = 24;
                sv_perlin pn;
                sv_perlin_init(&pn, seed);
                double st0[2] = { 0, 0 };
                if (stitch) {
                    double tw = floor(sr[2]), th = floor(sr[3]);
                    if (bfx != 0 && tw > 0) {
                        double lo = floor(tw * bfx) / tw, hi = ceil(tw * bfx) / tw;
                        bfx = (lo > 0 ? bfx / lo : 1e300) < hi / bfx ? lo : hi;
                    }
                    if (bfy != 0 && th > 0) {
                        double lo = floor(th * bfy) / th, hi = ceil(th * bfy) / th;
                        bfy = (lo > 0 ? bfy / lo : 1e300) < hi / bfy ? lo : hi;
                    }
                    st0[0] = floor(tw * bfx + 0.5);
                    st0[1] = floor(th * bfy + 0.5);
                }
                for (int y = sub[1]; y < sub[3]; y++)
                    for (int x = sub[0]; x < sub[2]; x++) {
                        double ux = sv_ax(inv, x + 0.5, y + 0.5) + 0.5, uy = sv_ay(inv, x + 0.5, y + 0.5) + 0.5, cv[4];
                        for (int ch = 0; ch < 4; ch++) {
                            double sum = 0, ratio = 1, vx = ux * bfx, vy = uy * bfy, st[2] = { st0[0], st0[1] };
                            for (int o2 = 0; o2 < oct; o2++) {
                                double nv = sv_noise2(&pn, ch, vx, vy, stitch ? st : 0);
                                sum += (type ? fabs(nv) : nv) / ratio;
                                vx *= 2;
                                vy *= 2;
                                ratio *= 2;
                                st[0] *= 2;
                                st[1] *= 2;
                            }
                            if (!type) sum = (sum + 1) / 2;
                            cv[ch] = sum < 0 ? 0 : sum > 1 ? 1 : sum;
                        }
                        size_t q = base + 4 * ((size_t)y * (size_t)W + (size_t)x);
                        for (int c = 0; c < 3; c++) res[q + (size_t)c] = sv_q8(cv[c] * cv[3]);
                        res[q + 3] = sv_q8(cv[3]);
                    }
            } else if (oct <= 0 && !type) {
                /* no octave: a fractal sum of nothing, a half grey half opaque */
                for (int y = sub[1]; y < sub[3]; y++)
                    for (int x = sub[0]; x < sub[2]; x++)
                        for (int c = 0; c < 4; c++) res[base + 4 * ((size_t)y * (size_t)W + (size_t)x) + (size_t)c] = sv_q8(c < 3 ? 0.25 : 0.5);
            }
        } else if (kind == SV_FE_MORPHOLOGY) {
            /* each channel the least (erode) or greatest (dilate) about the px, its radii mapped
               into the layer and rounded (Skia's), outside the image transparent; radii of none
               the input itself */
            int dil = (int)sv_rnum(s, to, &i);
            double rxu = sv_rnum(s, to, &i), ryu = sv_rnum(s, to, &i), rxd = floor(rxu * sxs + 0.5), ryd = floor(ryu * sys + 0.5);
            int mrx = rxd > 1000 ? 1000 : rxd > 0 ? (int)rxd : 0, mry = ryd > 1000 ? 1000 : ryd > 0 ? (int)ryd : 0;
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            sv_morph_img(&in1, W, H, mrx, mry, dil);
            for (size_t q = 0; q < NP * 4; q++) res[base + q] = at(in1, q);
        } else if (kind == SV_FE_CONVOLVE) {
            /* the kernel over the input about each px (Filter Effects 1, 15.11, as Skia's matrix
               convolution: the kernel turned about, its target the px), divided, the bias added;
               the colours premultiplied, or, preserving alpha, unpremultiplied and the alpha the
               px's own; the edge mode's px for those outside the subregion (repeated, wrapped
               about it, or transparent) */
            int ox = (int)sv_rnum(s, to, &i), oy = (int)sv_rnum(s, to, &i), tx = (int)sv_rnum(s, to, &i), ty = (int)sv_rnum(s, to, &i);
            double div = sv_rnum(s, to, &i), bias = sv_rnum(s, to, &i);
            int mode = (int)sv_rnum(s, to, &i), keep = (int)sv_rnum(s, to, &i);
            double kv[400];
            if (ox < 0 || oy < 0 || ox * oy > 400) ox = oy = 0;
            for (int q = 0; q < ox * oy; q++) kv[q] = sv_rnum(s, to, &i);
            if (ox > 0 && oy > 0 && div != 0 && sub[2] > sub[0] && sub[3] > sub[1]) {
                sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
                if (keep)
                    for (size_t q = 0; q < NP; q++) {
                        float al = at(in1, 4 * q + 3);
                        for (int c = 0; c < 3; c++) in1[4 * q + (size_t)c] = al > 0 ? at(in1, 4 * q + (size_t)c) / al : 0;
                    }
                int bw = sub[2] - sub[0], bh = sub[3] - sub[1];
                for (int y = sub[1]; y < sub[3]; y++)
                    for (int x = sub[0]; x < sub[2]; x++) {
                        double sum[4] = { 0, 0, 0, 0 };
                        for (int ky = 0; ky < oy; ky++)
                            for (int kx = 0; kx < ox; kx++) {
                                double kval = kv[(oy - 1 - ky) * ox + (ox - 1 - kx)];
                                if (kval == 0) continue;
                                int px = x - tx + kx, py = y - ty + ky;
                                if (px < sub[0] || px >= sub[2] || py < sub[1] || py >= sub[3]) {
                                    if (mode == 2) continue;
                                    if (mode == 1) {
                                        px = sub[0] + ((px - sub[0]) % bw + bw) % bw;
                                        py = sub[1] + ((py - sub[1]) % bh + bh) % bh;
                                    } else {
                                        px = px < sub[0] ? sub[0] : px >= sub[2] ? sub[2] - 1 : px;
                                        py = py < sub[1] ? sub[1] : py >= sub[3] ? sub[3] - 1 : py;
                                    }
                                }
                                const float *pp = data(in1) + 4 * ((size_t)py * (size_t)W + (size_t)px);
                                for (int c = 0; c < 4; c++) sum[c] += kval * pp[c];
                            }
                        size_t q = base + 4 * ((size_t)y * (size_t)W + (size_t)x);
                        if (keep) {
                            double al = at(in1, 4 * ((size_t)y * (size_t)W + (size_t)x) + 3);
                            for (int c = 0; c < 3; c++) {
                                double v = sum[c] / div + bias;
                                v = v < 0 ? 0 : v > 1 ? 1 : v;
                                res[q + (size_t)c] = sv_q8(v * al);
                            }
                            res[q + 3] = sv_q8(al);
                        } else {
                            double al = sum[3] / div + bias;
                            al = al < 0 ? 0 : al > 1 ? 1 : al;
                            for (int c = 0; c < 3; c++) {
                                double v = sum[c] / div + bias;
                                res[q + (size_t)c] = sv_q8(v < 0 ? 0 : v > al ? al : v);
                            }
                            res[q + 3] = sv_q8(al);
                        }
                    }
            }
        } else if (kind == SV_FE_DISPLACE) {
            /* each px the input's at the px moved by the map's channels (unpremultiplied, less a
               half) times the scale in the layer's px: the nearest px to where it lands (one just
               on an edge the one before it, as Skia samples it), transparent outside the image */
            double scale = sv_rnum(s, to, &i);
            int selx = (int)sv_rnum(s, to, &i), sely = (int)sv_rnum(s, to, &i);
            if (selx < 0 || selx > 3) selx = 3;
            if (sely < 0 || sely > 3) sely = 3;
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            sv_fe_input_img(&src, &res, &spaces, i2, linear, NP, &in2);
            double kx = scale * sxs, ky = scale * sys;
            for (int y = sub[1]; y < sub[3]; y++)
                for (int x = sub[0]; x < sub[2]; x++) {
                    const float *dp = data(in2) + 4 * ((size_t)y * (size_t)W + (size_t)x);
                    double al = dp[3], cx = selx == 3 ? al : al > 0 ? dp[selx] / al : 0, cy = sely == 3 ? al : al > 0 ? dp[sely] / al : 0;
                    double fx = x + 0.5 + kx * (cx - 0.5), fy = y + 0.5 + ky * (cy - 0.5);
                    if (!(fabs(fx) < 1e9 && fabs(fy) < 1e9)) continue;
                    long ix = (long)ceil(fx) - 1, iy = (long)ceil(fy) - 1;
                    if (ix < 0 || iy < 0 || ix >= W || iy >= H) continue;
                    size_t q = base + 4 * ((size_t)y * (size_t)W + (size_t)x), r2 = 4 * ((size_t)iy * (size_t)W + (size_t)ix);
                    for (int c = 0; c < 4; c++) res[q + (size_t)c] = at(in1, r2 + (size_t)c);
                }
        } else if (kind == SV_FE_TILE) {
            /* the input's tile (its subregion, its whole px within it) repeated over the subregion
               from where it is */
            double t0 = sv_rnum(s, to, &i), t1 = sv_rnum(s, to, &i), t2 = sv_rnum(s, to, &i), t3 = sv_rnum(s, to, &i);
            sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
            /* the tile's px: those inside its rectangle (Skia's rounded in), wherever they are (a part
               outside the layer transparent) */
            double xs4[2] = { sv_ax(full, t0, t1), sv_ax(full, t0 + t2, t1 + t3) }, ys4[2] = { sv_ay(full, t0, t1), sv_ay(full, t0 + t2, t1 + t3) };
            double ex0 = ceil(fmin(xs4[0], xs4[1]) - 1e-6), ey0 = ceil(fmin(ys4[0], ys4[1]) - 1e-6);
            double ex1 = floor(fmax(xs4[0], xs4[1]) + 1e-6), ey1 = floor(fmax(ys4[0], ys4[1]) + 1e-6);
            if (ex1 > ex0 && ey1 > ey0 && fabs(ex0) < 1e8 && fabs(ey0) < 1e8 && ex1 - ex0 < 1e8 && ey1 - ey0 < 1e8) {
                long tx0 = (long)ex0, ty0 = (long)ey0, tw = (long)(ex1 - ex0), th = (long)(ey1 - ey0);
                for (int y = sub[1]; y < sub[3]; y++)
                    for (int x = sub[0]; x < sub[2]; x++) {
                        long px = tx0 + ((x - tx0) % tw + tw) % tw, py = ty0 + ((y - ty0) % th + th) % th;
                        if (px < 0 || py < 0 || px >= W || py >= H) continue;
                        size_t q = base + 4 * ((size_t)y * (size_t)W + (size_t)x), r2 = 4 * ((size_t)py * (size_t)W + (size_t)px);
                        for (int c = 0; c < 4; c++) res[q + (size_t)c] = at(in1, r2 + (size_t)c);
                    }
            }
        } else if (kind == SV_FE_IMAGE) {
            /* its picture: its block drawn into a layer of its own, as a mask's content is */
            sv_skip_line(s, to, &i);
            size_t ia, ik, bf, bt;
            if (sv_tok(s, to, &i, 0, &ia, &ik) && ik == 1 && s[ia] == 'I') {
                sv_block(s, to, &i, &bf, &bt);
                vector<float> pic;
                if (dv->depth < 8 && bt > bf) sv_render(s, bt, bf, W, H, dv->dev, dv->depth + 1, nt, &pic);
                if (len(pic) == NP * 4)
                    for (size_t q = 0; q < NP * 4; q++) res[base + q] = sv_q8(at(pic, q));
            }
        } else if (kind == SV_FE_DIFFUSE || kind == SV_FE_SPECULAR) {
            /* the input's alpha a surface, lit (Filter Effects 1, 15.10 and 15.22, as Skia lights
               one): each px's normal (sv_light_normal) within the subregion, the light's direction
               from the px to its place (mapped into the layer: its z by the average of the scales)
               or a distant one's, its colour (in the primitive's space, a spot's falling off
               within its cone, Skia's 0.016 of a cosine smoothing its edge); diffuse kd N.L times
               it, opaque; specular ks (N.H)^exponent times it, its alpha its greatest channel */
            double ss = sv_rnum(s, to, &i), kc = sv_rnum(s, to, &i), ex = sv_rnum(s, to, &i);
            uint32_t col = sv_rhex(s, to, &i);
            int lt = (int)sv_rnum(s, to, &i);
            double lp[6] = { 0, 0, 0, 0, 0, 0 }, sexp = 1, cone = 90;
            if (lt == 1) { lp[0] = sv_rnum(s, to, &i); lp[1] = sv_rnum(s, to, &i); }
            else if (lt == 2 || lt == 3)
                for (int q = 0; q < (lt == 3 ? 6 : 3); q++) lp[q] = sv_rnum(s, to, &i);
            if (lt == 3) { sexp = sv_rnum(s, to, &i); cone = sv_rnum(s, to, &i); }
            if (lt >= 1 && lt <= 3 && sub[2] > sub[0] && sub[3] > sub[1]) {
                sv_fe_input_img(&src, &res, &spaces, i1, linear, NP, &in1);
                double lc[3];
                for (int c = 0; c < 3; c++) {
                    double u = (col >> (24 - 8 * c) & 0xff) / 255.0;
                    lc[c] = floor((linear ? sv_to_lin(u) : u) * 255 + 0.5);
                }
                double dir[3] = { 0, 0, 1 }, pos[3] = { 0, 0, 0 }, aim[3] = { 0, 0, 0 }, sdir[3] = { 0, 0, 0 };
                if (lt == 1) {
                    double az = lp[0] * M_PI / 180, el = lp[1] * M_PI / 180;
                    dir[0] = cos(az) * cos(el);
                    dir[1] = sin(az) * cos(el);
                    dir[2] = sin(el);
                } else {
                    double zk = ((full.a + full.c) + (full.b + full.d)) / 2;
                    pos[0] = sv_ax(full, lp[0], lp[1]);
                    pos[1] = sv_ay(full, lp[0], lp[1]);
                    pos[2] = lp[2] * zk;
                    if (lt == 3) {
                        aim[0] = sv_ax(full, lp[3], lp[4]);
                        aim[1] = sv_ay(full, lp[3], lp[4]);
                        aim[2] = lp[5] * zk;
                        double sl = 0;
                        for (int c = 0; c < 3; c++) { sdir[c] = aim[c] - pos[c]; sl += sdir[c] * sdir[c]; }
                        sl = sqrt(sl);
                        for (int c = 0; c < 3; c++) sdir[c] = sl > 0 ? sdir[c] / sl : 0;
                    }
                }
                double cos_out = cos(cone * M_PI / 180), cos_in = cos_out + 0.016;
                for (int y = sub[1]; y < sub[3]; y++)
                    for (int x = sub[0]; x < sub[2]; x++) {
                        double nrm[3], L[3], k = 1;
                        sv_light_normal(&in1, W, sub, x, y, ss / 255, nrm);
                        double z = floor(at(in1, 4 * ((size_t)y * (size_t)W + (size_t)x) + 3) * 255 + 0.5) * ss / 255;
                        if (lt == 1) for (int c = 0; c < 3; c++) L[c] = dir[c];
                        else {
                            /* from the px's centre (Skia's shader's coordinate) */
                            L[0] = pos[0] - (x + 0.5);
                            L[1] = pos[1] - (y + 0.5);
                            L[2] = pos[2] - z;
                            double ll = sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
                            for (int c = 0; c < 3; c++) L[c] = ll > 0 ? L[c] / ll : 0;
                            if (lt == 3) {
                                double ca = -(L[0] * sdir[0] + L[1] * sdir[1] + L[2] * sdir[2]);
                                k = 0;
                                if (ca >= cos_out) {
                                    k = pow(ca, sexp);
                                    if (ca < cos_in) k *= (ca - cos_out) / 0.016;
                                }
                            }
                        }
                        double f;
                        if (kind == SV_FE_DIFFUSE) f = kc * (nrm[0] * L[0] + nrm[1] * L[1] + nrm[2] * L[2]);
                        else {
                            double hv[3] = { L[0], L[1], L[2] + 1 }, hl = sqrt(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);
                            double nh = hl > 0 ? (nrm[0] * hv[0] + nrm[1] * hv[1] + nrm[2] * hv[2]) / hl : 0;
                            f = kc * pow(nh < 0 ? 0 : nh, ex);
                        }
                        double o[3], mx = 0;
                        for (int c = 0; c < 3; c++) {
                            double v = lc[c] * k * f;
                            if (v > mx) mx = v;
                            v = floor(v + 0.5);
                            o[c] = v < 0 ? 0 : v > 255 ? 255 : v;
                        }
                        double oa = kind == SV_FE_DIFFUSE ? 255 : floor(mx + 0.5);
                        if (oa > 255) oa = 255;
                        size_t q = base + 4 * ((size_t)y * (size_t)W + (size_t)x);
                        for (int c = 0; c < 3; c++) res[q + (size_t)c] = (float)(o[c] / 255);
                        res[q + 3] = (float)(oa / 255);
                    }
            }
        }
        sv_skip_line(s, to, &i);
        /* the result cropped to the subregion */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (x < sub[0] || x >= sub[2] || y < sub[1] || y >= sub[3])
                    for (int c = 0; c < 4; c++) res[base + 4 * ((size_t)y * (size_t)W + (size_t)x) + (size_t)c] = 0;
    }
    /* the last result, in sRGB, within the region, is the layer */
    size_t nr = len(spaces);
    if (!nr) return;
    size_t o = (nr - 1) * NP * 4;
    vector<float> outv;
    resize(outv, NP * 4);
    for (size_t q = 0; q < NP * 4; q++) outv[q] = at(res, o + q);
    if (at(spaces, nr - 1)) sv_space(&outv, 0, NP, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int in = x >= reg[0] && x < reg[2] && y >= reg[1] && y < reg[3];
            size_t q = 4 * ((size_t)y * (size_t)W + (size_t)x);
            for (int c = 0; c < 4; c++) (*lay)[top + q + (size_t)c] = in ? at(outv, q + (size_t)c) : 0.0f;
        }
}

static void sv_push(const char *s, size_t n, size_t *i, RC) {
    double op = sv_rnum(s, n, i);
    int hasclip = (int)sv_rnum(s, n, i), nk = (int)sv_rnum(s, n, i), hasmask = (int)sv_rnum(s, n, i), hasfilter = (int)sv_rnum(s, n, i);
    int lin = (int)sv_rnum(s, n, i);
    sv_skip_line(s, n, i);
    size_t px = (size_t)dv->w * (size_t)dv->h;
    int32_t mk = -1;
    if (hasclip) {
        mk = (int32_t)(len(*masks) / (px ? px : 1));
        resize(masks, len(*masks) + px);
    }
    size_t mo = mk >= 0 ? (size_t)mk * px : 0;
    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    int rect = 0;
    for (int k = 0; k < nk; k++) {
        size_t a, kl;
        if (!sv_tok(s, n, i, 0, &a, &kl) || s[a] != 'k') { sv_skip_line(s, n, i); continue; }
        sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
        int rule = (int)sv_rnum(s, n, i), evenodd = rule & 1, aliased = (rule & 2) != 0;
        sv_rpath(s, n, i, path);
        int b[4];
        sv_fill_edges(RA, full);
        rect = nk == 1 && sv_is_rect(pts, subs);
        for (size_t q = 0; q + 3 < len(*edges); q += 2) {
            bx0 = fmin(bx0, at(*edges, q));
            bx1 = fmax(bx1, at(*edges, q));
            by0 = fmin(by0, at(*edges, q + 1));
            by1 = fmax(by1, at(*edges, q + 1));
        }
        if (mk >= 0 && sv_cover(edges, evenodd, dv->w, dv->h, b, cov, xs, order, act, acc)) {
            int bw = b[2] - b[0];
            for (int y = b[1]; y < b[3]; y++)
                for (int x = b[0]; x < b[2]; x++) {
                    float v = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)(x - b[0]));
                    if (aliased) v = v >= 0.5f ? 1.0f : 0.0f;
                    size_t q = mo + (size_t)y * (size_t)dv->w + (size_t)x;
                    if (v > at(*masks, q)) (*masks)[q] = v;
                }
        }
    }
    if (hasclip == 2 && !rect && mk >= 0 && bx1 > bx0 && by1 > by0)
        for (int y = 0; y < dv->h; y++) {
            double cy = fmin(y + 1, by1) - fmax(y, by0);
            for (int x = 0; x < dv->w; x++) {
                size_t q = mo + (size_t)y * (size_t)dv->w + (size_t)x;
                if (at(*masks, q) <= 0) continue;
                double cx = fmin(x + 1, bx1) - fmax(x, bx0);
                double k = (cx < 0 ? 0 : cx > 1 ? 1 : cx) * (cy < 0 ? 0 : cy > 1 ? 1 : cy);
                (*masks)[q] = (float)(at(*masks, q) * k);
            }
        }
    /* its mask: the mask's content drawn and made coverage, which the clip's is multiplied by */
    if (hasmask) {
        size_t a, kl, save = *i;
        if (sv_tok(s, n, i, 0, &a, &kl) && kl == 1 && s[a] == 'M') sv_mask_apply(s, n, i, RA, &mk);
        else *i = save;
    }
    /* its filter: its block kept, drawn when the group ends */
    double ffrom = 0, fto = 0;
    if (hasfilter) {
        size_t a, kl, save = *i;
        if (sv_tok(s, n, i, 0, &a, &kl) && kl == 1 && s[a] == 'F') {
            size_t from, to;
            ffrom = (double)a;
            sv_block(s, n, i, &from, &to);
            fto = (double)to;
        } else *i = save;
    }
    resize(lay, len(*lay) + px * 4);
    push_back(lop, op);
    push_back(lop, ffrom);
    push_back(lop, fto);
    push_back(lop, (double)lin);
    push_back(lmk, mk);
}

static void sv_filter_apply(const char *s, size_t from, size_t to, RC, size_t top);

/* a group's end: its layer through its filter (or made linearRGB, a mask's content's group whose
   color-interpolation asks it), then blended into the one under it, by its opacity and its
   clip's and mask's coverage */
static void sv_pop(const char *s, RC) {
    size_t px = (size_t)dv->w * (size_t)dv->h;
    if (len(*lop) < 4 || len(*lay) < 2 * px * 4) return;
    double op = at(*lop, len(*lop) - 4);
    size_t ffrom = (size_t)at(*lop, len(*lop) - 3), fto = (size_t)at(*lop, len(*lop) - 2);
    int lin = at(*lop, len(*lop) - 1) != 0;
    int32_t mk = at(*lmk, len(*lmk) - 1);
    if (fto > ffrom) sv_filter_apply(s, ffrom, fto, RA, len(*lay) - px * 4);
    if (lin) sv_space(lay, len(*lay) - px * 4, px, 1);
    size_t top = len(*lay) - px * 4, under = top - px * 4, mo = mk >= 0 ? (size_t)mk * px : 0;
    float o = (float)(op < 0 ? 0 : op > 1 ? 1 : op);
    for (size_t p = 0; p < px; p++) {
        float k = mk >= 0 ? o * at(*masks, mo + p) : o;
        size_t q = top + 4 * p, u = under + 4 * p;
        float a = at(*lay, q + 3) * k;
        if (a <= 0) continue;
        float keep = 1 - a;
        (*lay)[u] = at(*lay, q) * k + at(*lay, u) * keep;
        (*lay)[u + 1] = at(*lay, q + 1) * k + at(*lay, u + 1) * keep;
        (*lay)[u + 2] = at(*lay, q + 2) * k + at(*lay, u + 2) * keep;
        (*lay)[u + 3] = a + at(*lay, u + 3) * keep;
    }
    resize(lay, top);
    resize(lop, len(*lop) - 4);
    resize(lmk, len(*lmk) - 1);
    if (mk >= 0) resize(masks, mo);
}

static void sv_draw_glyphs(const char *s, size_t n, size_t *i, RC, const amb_fonts *f);
static int sv_doc_scene(const uint8_t *bytes, size_t n, double vw, double vh, vector<char> *scene);
static int sv_head(const char *s, size_t n, double *vw, double *vh, int *clip, size_t *i);
static int sv_doc_viewbox(const uint8_t *bytes, size_t n);

/* the picture Chromium draws for an SVG image element whose picture fails to load or decode
   (Blink's broken image resource, as its screenshots show it drawn at its natural size, 14 x 16
   px), each pixel 0xRRGGBBAA */
static const uint32_t sv_broken_px[224] = {
    0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0xa3a3a3ff, 0x0000000a, 0x00000000, 0x00000000, 0x00000000,
    0xa2a2a3ff, 0xf3f7feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf1f6feff, 0xf2f6fdff, 0x969696ff, 0xa2a2a3ff, 0x0000000a, 0x00000000, 0x00000000,
    0xa1a1a2ff, 0xe4edfdff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xdae6fbff, 0xe0eafaff, 0x969696ff, 0xffffffff, 0xa1a1a2ff, 0x0000000a, 0x00000000,
    0xa0a0a0ff, 0xdce9fdff, 0xcedef9ff, 0xcedef9ff, 0xffffffff, 0xffffffff, 0xcedef9ff, 0xcedef9ff, 0xd8e3f8ff, 0x969696ff, 0xffffffff, 0xffffffff, 0xa0a0a0ff, 0x0000000a,
    0x9e9e9fff, 0xdbe8fdff, 0xcdddf8ff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xcdddf8ff, 0xd7e3f8ff, 0x919191ff, 0x919191ff, 0x919191ff, 0x919191ff, 0x9e9e9fff,
    0x9d9d9eff, 0xdbe8fdff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xcbdbf7ff, 0xcedbf3ff, 0xe5edfbff, 0xeef3fcff, 0xeef3fcff, 0xf3f7feff, 0x9d9d9eff,
    0x9b9b9cff, 0xdbe7fdff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xc9daf5ff, 0xccdcf6ff, 0xd2e0f7ff, 0xd5e2f7ff, 0xd5e2f7ff, 0xe3ecfdff, 0x9b9b9cff,
    0x9a9a9bff, 0xd9e6fcff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xc6d8f4ff, 0xd9e6fcff, 0x9a9a9bff,
    0x989899ff, 0xd8e6fcff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xc5d6f3ff, 0xd8e6fcff, 0x00000000,
    0x969697ff, 0xd8e5fcff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc2d4f1ff, 0xb1ced4ff, 0xb4cfd9ff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc3d4f3ff, 0xc3d4f3ff, 0x00000000, 0x00000000,
    0x959596ff, 0xd7e5fcff, 0xc0d1f0ff, 0xc0d1f0ff, 0x9fc6b7ff, 0x56a938ff, 0x58ae39ff, 0x58ae39ff, 0x6db35fff, 0xb2ccd8ff, 0xc0d1f0ff, 0x00000000, 0x00000000, 0x00000000,
    0x939394ff, 0xd6e4fcff, 0xbed0f0ff, 0x91c1a0ff, 0x58ae3aff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x5dae42ff, 0x00000000, 0x00000000, 0x00000000, 0x939394ff,
    0x929293ff, 0xd6e4fcff, 0x9dc4b4ff, 0x58ae3aff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x00000000, 0x00000000, 0x00000000, 0xd6e4fcff, 0x929293ff,
    0x909092ff, 0xc7dde6ff, 0x5fb046ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x00000000, 0x00000000, 0x00000000, 0x6eb463ff, 0xd4e3faff, 0x909092ff,
    0x8f8f90ff, 0x7dbe72ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x58ae39ff, 0x00000000, 0x00000000, 0x00000000, 0x58ae39ff, 0x58ae39ff, 0x8dc58cff, 0x8d908eff,
    0x868687ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x00000000, 0x00000000, 0x00000000, 0x838384ff, 0x838384ff, 0x838384ff, 0x838384ff, 0x8a8b8bff,
};

static int sv_hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* base64 (RFC 4648's alphabet): bytes appended as its text, and its text's bytes appended
   (white space and anything else passed over, '=' ending it) */
static const char sv_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void sv_b64_put(vector<char> *out, const uint8_t *p, size_t n) {
    for (size_t k = 0; k < n; k += 3) {
        uint32_t v = (uint32_t)p[k] << 16 | (k + 1 < n ? (uint32_t)p[k + 1] << 8 : 0) | (k + 2 < n ? p[k + 2] : 0);
        push_back(out, sv_b64[v >> 18 & 63]);
        push_back(out, sv_b64[v >> 12 & 63]);
        push_back(out, k + 1 < n ? sv_b64[v >> 6 & 63] : '=');
        push_back(out, k + 2 < n ? sv_b64[v & 63] : '=');
    }
}
static void sv_b64_get(const char *s, size_t n, vector<uint8_t> *out) {
    uint32_t acc = 0;
    int bits = 0;
    for (size_t k = 0; k < n && s[k] != '='; k++) {
        char c = s[k];
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 :
                c == '/' || c == '_' ? 63 : -1;
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            push_back(out, (uint8_t)(acc >> bits & 0xff));
        }
    }
}
/* a data: URL's body (Fetch's data: URL processor: its data percent-decoded, then base64-decoded
   when its type says ;base64): 1, or 0 for no data: URL */
static int sv_data_url(const char *s, size_t n, vector<uint8_t> *out) {
    size_t a = sv_skip_ws(s, n, 0);
    if (a + 5 > n || !sv_eq_ci(s + a, 5, "data:")) return 0;
    size_t comma = a + 5;
    while (comma < n && s[comma] != ',') comma++;
    if (comma >= n) return 0;
    int b64 = comma >= a + 12 && sv_eq_ci(s + comma - 7, 7, ";base64");
    vector<uint8_t> raw;
    for (size_t k = comma + 1; k < n; k++) {
        int hi, lo;
        if (s[k] == '%' && k + 2 < n && (hi = sv_hexval(s[k + 1])) >= 0 && (lo = sv_hexval(s[k + 2])) >= 0) {
            push_back(raw, (uint8_t)(hi << 4 | lo));
            k += 2;
        } else push_back(raw, (uint8_t)s[k]);
    }
    clear(*out);
    if (b64) sv_b64_get((const char *)data(raw), len(raw), out);
    else append_range(out, data(raw), len(raw));
    return 1;
}

/* a raster's mipmap level k (Skia's: each level half the last, rounded down, of its 2 x 2
   px's sum shifted down two bits; the colours premultiplied, eight bits): its pixels into
   out, its size into *w, *h */
static void sv_mip(const vector<uint8_t> *rgba, uint32_t pw, uint32_t ph, int k, vector<uint8_t> *out, uint32_t *w, uint32_t *h) {
    clear(*out);
    resize(out, (size_t)pw * ph * 4);
    for (size_t q = 0; q < (size_t)pw * ph; q++) {
        uint32_t a = at(*rgba, 4 * q + 3);
        for (int c = 0; c < 3; c++) (*out)[4 * q + (size_t)c] = (uint8_t)((at(*rgba, 4 * q + (size_t)c) * a + 127) / 255);
        (*out)[4 * q + 3] = (uint8_t)a;
    }
    uint32_t cw = pw, ch = ph;
    vector<uint8_t> next;
    for (int lv = 0; lv < k && (cw > 1 || ch > 1); lv++) {
        uint32_t nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1;
        clear(next);
        resize(next, (size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; y++)
            for (uint32_t x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    uint32_t x0 = cw > 1 ? 2 * x : x, y0 = ch > 1 ? 2 * y : y, x1 = cw > 1 ? x0 + 1 : x0, y1 = ch > 1 ? y0 + 1 : y0;
                    uint32_t sum = at(*out, ((size_t)y0 * cw + x0) * 4 + (size_t)c) + at(*out, ((size_t)y0 * cw + x1) * 4 + (size_t)c) +
                                   at(*out, ((size_t)y1 * cw + x0) * 4 + (size_t)c) + at(*out, ((size_t)y1 * cw + x1) * 4 + (size_t)c);
                    next[((size_t)y * nw + x) * 4 + (size_t)c] = (uint8_t)(sum >> 2);
                }
        clear(*out);
        append_range(out, data(next), len(next));
        cw = nw;
        ch = nh;
    }
    *w = cw;
    *h = ch;
}

/* an image element's line drawn (SVG 2, 12.3, as Blink's SVGImagePainter draws one): the
   picture of its element's resource from the loader -- a raster image decoded, or an SVG
   document drawn as its scene -- its natural size an auto width's or height's (the other's
   through its ratio), placed in its viewport by preserveAspectRatio (meet fitted in it,
   slice cropped to it), its edges anti-aliased; a raster's pixels sampled bilinearly at each
   px's centre, its edge's px repeated */
static void sv_draw_image(const char *s, size_t n, size_t *i, RC) {
    sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
    double ix = sv_rnum(s, n, i), iy = sv_rnum(s, n, i), iw = sv_rnum(s, n, i), ih = sv_rnum(s, n, i);
    int autow = (int)sv_rnum(s, n, i), autoh = (int)sv_rnum(s, n, i), ax = (int)sv_rnum(s, n, i), ay = (int)sv_rnum(s, n, i);
    int slice = (int)sv_rnum(s, n, i), none = (int)sv_rnum(s, n, i), clip = (int)sv_rnum(s, n, i);
    uint32_t el = (uint32_t)sv_rnum(s, n, i);
    (void)clip;
    /* the picture: the bytes the scene carries (a data: URL's, in an SVG document drawn as an
       image, which fetches nothing else), else the loader's for the element */
    vector<uint8_t> bytes, rgba;
    int given = 0, broken = 0;
    double rw0 = 0, rh0 = 0;
    {
        size_t a, k, save = *i;
        if (sv_tok(s, n, i, 1, &a, &k) && k == 1 && s[a] == 'd' && sv_tok(s, n, i, 1, &a, &k)) {
            sv_b64_get(s + a, k, &bytes);
            given = 1;
        } else *i = save;
    }
    if (dv->depth >= 8 || el == 0 || (!given && !nt)) return;
    if (!given) {
        uint32_t r = 0;
        for (uint32_t q = 1; q < nt->resource_len && !r; q++)
            if (nt->resource[q].live && nt->resource[q].elem.index == el && nt->resource[q].kind == NET_KIND_IMAGE) r = q;
        if (!r) return;
        if (nt->resource[r].outcome != NET_OUTCOME_OK) broken = 1;
        else {
            amb_span bsp = nt->resource[r].body;
            append_range(bytes, (const uint8_t *)bytes_ptr(nt, bsp), bsp.len);
            rw0 = nt->resource[r].width;
            rh0 = nt->resource[r].height;
        }
    }
    double nw = 0, nh = 0, ratio = 0;
    uint32_t pw = 0, ph = 0;
    int svg = !broken && amb_svg_size(data(bytes), len(bytes), &nw, &nh, &ratio);
    if (!svg) {
        if (broken || !amb_image_decode(data(bytes), len(bytes), &rgba, &pw, &ph) || !pw || !ph) {
            /* a picture that failed to load or decode: Chromium's broken image (14 x 16 px, of no
               natural size for an auto width or height, which draws nothing; nor under
               preserveAspectRatio none, whose viewport Blink's SVGImagePainter makes empty for a
               broken picture; measured) */
            if (given || autow || autoh || none) return;
            clear(rgba);
            for (int q = 0; q < 224; q++)
                for (int c = 0; c < 4; c++) push_back(rgba, (uint8_t)(sv_broken_px[q] >> (24 - 8 * c)));
            pw = 14;
            ph = 16;
            rw0 = rh0 = 0;
        }
        nw = rw0 > 0 ? rw0 : pw;
        nh = rh0 > 0 ? rh0 : ph;
        ratio = nw / nh;
    } else {
        /* an SVG document's concrete size (CSS Images 3's default sizing): its natural size; one
           side and its ratio; else its ratio contained in the viewport (300 x 150 for an auto
           side); else the viewport */
        double vw = autow ? 300 : iw, vh = autoh ? 150 : ih;
        if (nw <= 0 || nh <= 0) {
            if (nw > 0 && ratio > 0) nh = nw / ratio;
            else if (nh > 0 && ratio > 0) nw = nh * ratio;
            else if (ratio > 0) {
                if (vw / vh > ratio) { nh = vh; nw = vh * ratio; }
                else { nw = vw; nh = vw / ratio; }
            } else {
                if (nw <= 0) nw = vw;
                if (nh <= 0) nh = vh;
            }
        }
    }
    if (!(nw > 0 && nh > 0)) return;
    if (autow && autoh) { iw = nw; ih = nh; }
    else if (autow) iw = ih * nw / nh;
    else if (autoh) ih = iw * nh / nw;
    if (!(iw > 0 && ih > 0)) return;
    /* the picture's rectangle (dx, dy, dw, dh) in the viewport; an SVG document's laid out in
       the viewport itself unless preserveAspectRatio is none (Blink's container size), its
       viewBox fitted there by its own, a document with none stretched to it from its natural
       size */
    double dx = ix, dy = iy, dw = iw, dh = ih;
    /* (an feImage's picture, clip 2, an SVG one too, from its natural size by its
       preserveAspectRatio, as Blink's FEImage draws one: measured) */
    int fe = clip == 2;
    int fitted = svg && !fe && !none && sv_doc_viewbox(data(bytes), len(bytes));
    if (fitted) { nw = iw; nh = ih; }
    if (!none && (!svg || fe)) {
        double k = slice ? fmax(iw / nw, ih / nh) : fmin(iw / nw, ih / nh);
        dw = nw * k;
        dh = nh * k;
        dx = ix + (iw - dw) * ax / 2;
        dy = iy + (ih - dh) * ay / 2;
    }
    /* what is drawn: the picture within the viewport, its coverage */
    double cx0 = fmax(dx, ix), cy0 = fmax(dy, iy), cx1 = fmin(dx + dw, ix + iw), cy1 = fmin(dy + dh, iy + ih);
    if (cx1 <= cx0 || cy1 <= cy0) return;
    clear(*path);
    sv_put2(path, SV_M, cx0, cy0);
    sv_put2(path, SV_L, cx1, cy0);
    sv_put2(path, SV_L, cx1, cy1);
    sv_put2(path, SV_L, cx0, cy1);
    push_back(path, (double)SV_Z);
    sv_fill_edges(RA, full);
    int b[4];
    if (!sv_cover(edges, 0, dv->w, dv->h, b, cov, xs, order, act, acc)) return;
    size_t top = sv_top(RA);
    int bw = b[2] - b[0];
    sv_mat pic = sv_mul(full, sv_make(dw / nw, 0, 0, dh / nh, dx, dy)), inv;
    vector<float> svl;
    vector<uint8_t> mip;
    uint32_t mw = pw, mh = ph;
    if (svg) {
        vector<char> scene;
        if (!sv_doc_scene(data(bytes), len(bytes), nw, nh, &scene)) return;
        size_t si;
        double sw0, sh0;
        int sclip;
        if (!sv_head(data(scene), len(scene), &sw0, &sh0, &sclip, &si)) return;
        sv_render(data(scene), len(scene), si, dv->w, dv->h, pic, dv->depth + 1, 0, &svl);
        if (len(svl) != (size_t)dv->w * (size_t)dv->h * 4) return;
    } else {
        /* a raster made smaller is sampled from a mipmap level, as Chromium's decode cache picks
           one (MipMapUtil): the deepest whose size is still at least the drawn size each way */
        sv_mat fwd = sv_mul(pic, sv_make(nw / pw, 0, 0, nh / ph, 0, 0));
        double tw = ceil(pw * hypot(fwd.a, fwd.b) - 1e-6), th = ceil(ph * hypot(fwd.c, fwd.d) - 1e-6);
        int level = 0;
        uint32_t lw = pw, lh = ph;
        while (level < 12) {
            lw = lw / 2 > 1 ? lw / 2 : 1;
            lh = lh / 2 > 1 ? lh / 2 : 1;
            if (lw < tw || lh < th) break;
            level++;
            if (lw == 1 && lh == 1) break;
        }
        sv_mip(&rgba, pw, ph, level, &mip, &mw, &mh);
        if (!sv_inv(sv_mul(fwd, sv_make((double)pw / mw, 0, 0, (double)ph / mh, 0, 0)), &inv)) return;
    }
    for (int y = b[1]; y < b[3]; y++)
        for (int x = b[0]; x < b[2]; x++) {
            float cv = at(*cov, (size_t)(y - b[1]) * (size_t)bw + (size_t)x - (size_t)b[0]);
            if (cv <= 0) continue;
            float c[4];
            size_t q = ((size_t)y * (size_t)dv->w + (size_t)x) * 4;
            if (svg) for (int k = 0; k < 4; k++) c[k] = at(svl, q + (size_t)k);
            else {
                double u = sv_ax(inv, x + 0.5, y + 0.5) - 0.5, v = sv_ay(inv, x + 0.5, y + 0.5) - 0.5;
                if (u < 0) u = 0;
                if (v < 0) v = 0;
                if (u > mw - 1) u = mw - 1;
                if (v > mh - 1) v = mh - 1;
                uint32_t xa = (uint32_t)u, ya = (uint32_t)v, xb = xa + 1 < mw ? xa + 1 : xa, yb = ya + 1 < mh ? ya + 1 : ya;
                double fx = u - xa, fy = v - ya;
                for (int k = 0; k < 4; k++) {
                    double t0 = at(mip, ((size_t)ya * mw + xa) * 4 + (size_t)k), t1 = at(mip, ((size_t)ya * mw + xb) * 4 + (size_t)k);
                    double b0 = at(mip, ((size_t)yb * mw + xa) * 4 + (size_t)k), b1 = at(mip, ((size_t)yb * mw + xb) * 4 + (size_t)k);
                    double top0 = t0 + (t1 - t0) * fx, bot0 = b0 + (b1 - b0) * fx;
                    c[k] = (float)((top0 + (bot0 - top0) * fy) / 255);
                }
            }
            float a = c[3] * cv;
            if (a <= 0) continue;
            float keep = 1 - a;
            (*lay)[top + q] = c[0] * cv + at(*lay, top + q) * keep;
            (*lay)[top + q + 1] = c[1] * cv + at(*lay, top + q + 1) * keep;
            (*lay)[top + q + 2] = c[2] * cv + at(*lay, top + q + 2) * keep;
            (*lay)[top + q + 3] = a + at(*lay, top + q + 3) * keep;
        }
}

/* a scene's commands from s[i] (to n) drawn into a layer of w x h px (premultiplied RGBA,
   transparent to begin with; into layer), dev mapping the viewport's CSS px into it; depth:
   how deep in tiles, masks and images it is; nt the loader images come from (or none) */
#define RV dv, &layv, &lopv, &masksv, &lmkv, &grv, &gixv, &pathv, &edgesv, &covv, &ptsv, &cornerv, &subsv, &dpv, &dcfv, &dsv, &tmpv, &clv, &ccfv, \
           &xsv, &orderv, &actv, &accv, &tilesv, nt
static void sv_render(const char *s, size_t n, size_t i, int w, int h, sv_mat dev, int depth, const net_loader *nt, vector<float> *layer) {
    size_t a, k;
    sv_dev dvv;
    dvv.w = w;
    dvv.h = h;
    dvv.x0 = 0;
    dvv.y0 = 0;
    dvv.depth = depth;
    dvv.dev = dev;
    const sv_dev *dv = &dvv;
    vector<float> layv, masksv, covv, accv, tilesv;
    vector<double> lopv, grv, pathv, edgesv, ptsv, dpv, dsv, tmpv, clv, xsv;
    vector<int32_t> lmkv;
    vector<uint32_t> gixv, subsv, orderv, actv;
    vector<uint8_t> cornerv, ccfv;
    vector<sv_mseg> dcfv;
    resize(layv, (size_t)dvv.w * (size_t)dvv.h * 4);
    amb_fonts fonts = {0};
    defer(fonts) { AMB_FONTS_DROP(fonts); }
    int fonts_loaded = 0;
    while (sv_tok(s, n, &i, 0, &a, &k)) {
        char cmd = s[a];
        if (cmd == 'p' && k == 1) sv_draw_path(s, n, &i, RV);
        else if ((cmd == 'l' || cmd == 'r') && k == 1) sv_read_grad(s, n, &i, cmd == 'r', RV);
        else if (cmd == 'P' && k == 1) sv_read_pattern(s, n, &i, RV);
        else if (cmd == 'i' && k == 1) sv_draw_image(s, n, &i, RV);
        else if (cmd == 'g' && k == 1) sv_push(s, n, &i, RV);
        else if (cmd == 'e' && k == 1) sv_pop(s, RV);
        else if (cmd == 't' && k == 1) {
            if (!fonts_loaded) { amb_fonts_load(&fonts); fonts_loaded = 1; }
            sv_draw_glyphs(s, n, &i, RV, &fonts);
        } else if ((cmd == 'M' || cmd == 'F') && k == 1) {
            /* a block no group took: passed over */
            size_t from, to;
            sv_block(s, n, &i, &from, &to);
        }
        sv_skip_line(s, n, &i);
    }
    /* groups left open end here */
    while (len(lopv)) sv_pop(s, RV);
    clear(*layer);
    append_range(layer, data(layv), len(layv));
}

/* the groups at the start of s (each g line with its clip's shapes and its blocks) opened over a layer of nothing, the
   layer given drawn into the innermost, and each closed: the layer through their filters, clips and masks */
void amb_svg_fx(const char *s, size_t n, int w, int h, const double *m, const net_loader *nt, vector<float> *layer) {
    size_t px = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0);
    if (!px || len(*layer) != px * 4 || (double)px > 3e7) return;
    sv_dev dvv;
    dvv.w = w;
    dvv.h = h;
    dvv.x0 = 0;
    dvv.y0 = 0;
    dvv.depth = 0;
    dvv.dev = sv_make(m[0], m[1], m[2], m[3], m[4], m[5]);
    const sv_dev *dv = &dvv;
    vector<float> layv, masksv, covv, accv, tilesv;
    vector<double> lopv, grv, pathv, edgesv, ptsv, dpv, dsv, tmpv, clv, xsv;
    vector<int32_t> lmkv;
    vector<uint32_t> gixv, subsv, orderv, actv;
    vector<uint8_t> cornerv, ccfv;
    vector<sv_mseg> dcfv;
    resize(layv, px * 4);
    size_t i = 0, a, k;
    int groups = 0;
    while (groups < 8 && sv_tok(s, n, &i, 0, &a, &k)) {
        if (k == 1 && s[a] == 'g') {
            sv_push(s, n, &i, RV);
            groups++;
        }
        sv_skip_line(s, n, &i);
    }
    if (!groups) return;
    size_t top = len(layv) - px * 4;
    for (size_t q = 0; q < px * 4; q++) layv[top + q] = at(*layer, q);
    while (len(lopv)) sv_pop(s, RV);
    for (size_t q = 0; q < px * 4; q++) (*layer)[q] = at(layv, q);
}
#undef RV

/* a scene's first line: its viewport's size and whether it clips to it; where its
   commands begin */
static int sv_head(const char *s, size_t n, double *vw, double *vh, int *clip, size_t *i) {
    size_t a, k;
    *i = 0;
    if (!sv_tok(s, n, i, 0, &a, &k) || k != 1 || s[a] != 'v') return 0;
    *vw = sv_rnum(s, n, i);
    *vh = sv_rnum(s, n, i);
    *clip = (int)sv_rnum(s, n, i);
    sv_skip_line(s, n, i);
    return *vw > 0 && *vh > 0;
}

void amb_svg_draw(amb_canvas *c, const char *s, size_t n, double x, double y, double sx, double sy) { amb_svg_draw_with(c, 0, s, n, x, y, sx, sy); }

void amb_svg_draw_with(amb_canvas *c, const net_loader *nt, const char *s, size_t n, double x, double y, double sx, double sy) {
    size_t i;
    double vw, vh;
    int clip;
    if (!sv_head(s, n, &vw, &vh, &clip, &i)) return;
    /* the canvas's clip, its px */
    int kx0 = (int)floor(c->cx0 + 0.5), kx1 = (int)floor(c->cx1 + 0.5), ky0 = (int)floor(c->cy0 - c->oy + 0.5), ky1 = (int)floor(c->cy1 - c->oy + 0.5);
    if (kx0 < 0) kx0 = 0;
    if (ky0 < 0) ky0 = 0;
    if (kx1 > (int)c->w) kx1 = (int)c->w;
    if (ky1 > (int)c->h) ky1 = (int)c->h;
    /* the viewport's place rounded to whole CSS px, as Chromium places an svg's content (then
       scaled to the device's px, which a zoom may leave between two) */
    double z = c->zoom > 0 ? c->zoom : 1;
    double X0 = floor(x / z + 0.5) * z, Y0 = floor(y / z + 0.5) * z - c->oy;
    if (!(fabs(X0) < 1e8 && fabs(Y0) < 1e8 && sx > 0 && sy > 0 && sx < 1e4 && sy < 1e4)) return;
    int vx0 = kx0, vy0 = ky0, vx1 = kx1, vy1 = ky1;
    if (clip) {
        double ex = fmin(fmax(X0 + vw * sx, -1e8), 1e8), ey = fmin(fmax(Y0 + vh * sy, -1e8), 1e8);
        int ax0 = (int)floor(X0 + 1e-6), ay0 = (int)floor(Y0 + 1e-6), ax1 = (int)ceil(ex - 1e-6), ay1 = (int)ceil(ey - 1e-6);
        if (ax0 > vx0) vx0 = ax0;
        if (ay0 > vy0) vy0 = ay0;
        if (ax1 < vx1) vx1 = ax1;
        if (ay1 < vy1) vy1 = ay1;
    }
    if (vx1 <= vx0 || vy1 <= vy0 || (double)(vx1 - vx0) * (vy1 - vy0) > 3e7) return;
    int lw = vx1 - vx0, lh = vy1 - vy0;
    vector<float> layv;
    sv_render(s, n, i, lw, lh, sv_make(sx, 0, 0, sy, X0 - vx0, Y0 - vy0), 0, nt, &layv);
    /* the layer blended onto the canvas, its viewport's clip anti-aliased where a zoom leaves
       its edges between two px */
    double ex = X0 + vw * sx, ey = Y0 + vh * sy;
    for (int yy = 0; yy < lh; yy++) {
        double py = vy0 + yy, cy = clip ? fmin(py + 1, ey) - fmax(py, Y0) : 1;
        if (cy <= 0) continue;
        for (int xx = 0; xx < lw; xx++) {
            size_t q = ((size_t)yy * (size_t)lw + (size_t)xx) * 4;
            float al = at(layv, q + 3);
            if (al <= 0) continue;
            double px = vx0 + xx, cx = clip ? fmin(px + 1, ex) - fmax(px, X0) : 1;
            if (cx <= 0) continue;
            double k = (cx > 1 ? 1 : cx) * (cy > 1 ? 1 : cy);
            size_t p = ((size_t)(vy0 + yy) * c->w + (size_t)(vx0 + xx)) * 3;
            for (int ch = 0; ch < 3; ch++) {
                double v = at(layv, q + (size_t)ch) * 255 * k + c->px[p + (size_t)ch] * (1 - al * k);
                c->px[p + (size_t)ch] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
            }
        }
    }
}

/* a run of glyphs' line: its matrix, face, size, slant, paint and opacity, then each glyph's
   code point and place */
static void sv_draw_glyphs(const char *s, size_t n, size_t *i, RC, const amb_fonts *f) {
    sv_mat m = sv_rmat(s, n, i), full = sv_mul(dv->dev, m);
    int face = (int)sv_rnum(s, n, i);
    double size = sv_rnum(s, n, i);
    int italic = (int)sv_rnum(s, n, i);
    sv_ink ink;
    clear(*tiles);
    sv_rink(s, n, i, RA, m, &ink);
    sv_ink_opacity(&ink, sv_rnum(s, n, i));
    int count = (int)sv_rnum(s, n, i);
    if (face < 0 || face >= (int)f->nfaces) face = AMB_FACE_SERIF;
    /* the device's size: the matrix's scale (a rotated or skewed run drawn upright, at its
       area's scale) */
    double dsize = size * sqrt(fabs(full.a * full.d - full.b * full.c));
    if (!ink.kind || dsize <= 0 || dsize > 2000) return;
    size_t top = sv_top(RA);
    vector<float> facc;
    vector<uint8_t> mask;
    for (int g = 0; g < count; g++) {
        uint32_t gid = (uint32_t)sv_rnum(s, n, i);
        int gface = (int)sv_rnum(s, n, i);
        double gx = sv_rnum(s, n, i), gy = sv_rnum(s, n, i);
        double X = sv_ax(full, gx, gy), Y = sv_ay(full, gx, gy);
        if (!(fabs(X) < 1e7 && fabs(Y) < 1e7)) continue;
        /* as the page's text is set: its pen at the nearest quarter of a px across (Skia's: an eighth added, the
           quarter below), its baseline at a whole px */
        double fx = floor(X + 0.125);
        int quarter = (int)((X + 0.125 - fx) * 4);
        int by = (int)floor(Y + 0.5);
        int w, h, ox, oy;
        if (!amb_glyph_mask_id(f, gface, gid, dsize, quarter / 4.0, italic, &facc, &mask, &w, &h, &ox, &oy)) continue;
        for (int yy = 0; yy < h; yy++) {
            int py = by + oy + yy;
            if (py < 0 || py >= dv->h) continue;
            for (int xx = 0; xx < w; xx++) {
                int px = (int)fx + ox + xx;
                if (px < 0 || px >= dv->w) continue;
                uint8_t mv = at(mask, (size_t)yy * (size_t)w + (size_t)xx);
                if (!mv) continue;
                float cv = mv / 255.0f;
                float c[4];
                if (!sv_ink_at(gr, tiles, &ink, px + 0.5, py + 0.5, c)) continue;
                size_t q = top + ((size_t)py * (size_t)dv->w + (size_t)px) * 4;
                float a = c[3] * cv, keep = 1 - a;
                (*lay)[q] = c[0] * cv + at(*lay, q) * keep;
                (*lay)[q + 1] = c[1] * cv + at(*lay, q + 1) * keep;
                (*lay)[q + 2] = c[2] * cv + at(*lay, q + 2) * keep;
                (*lay)[q + 3] = a + at(*lay, q + 3) * keep;
            }
        }
    }
}

int amb_path_coverage(const vector<double> *path, const double *m, int evenodd, int w, int h, int *box, vector<float> *cov) {
    vector<double> pts, edges, xs;
    vector<uint8_t> corner;
    vector<uint32_t> subs, order, act;
    vector<float> acc;
    sv_mat full = sv_make(m[0], m[1], m[2], m[3], m[4], m[5]);
    sv_flatten(path, full, 0.05, &pts, &corner, &subs);
    for (size_t s = 0; s + 2 < len(subs); s += 3) {
        uint32_t b = at(subs, s), c = at(subs, s + 1);
        for (uint32_t j = 0; j < c; j++) {
            uint32_t l = j + 1 == c ? 0 : j + 1;
            double x0 = at(pts, 2 * (b + j)), y0 = at(pts, 2 * (b + j) + 1), x1 = at(pts, 2 * (b + l)), y1 = at(pts, 2 * (b + l) + 1);
            if (y0 == y1) continue;
            push_back(edges, x0);
            push_back(edges, y0);
            push_back(edges, x1);
            push_back(edges, y1);
        }
    }
    return sv_cover(&edges, evenodd, w, h, box, cov, &xs, &order, &act, &acc);
}

void amb_svg_coverage(const char *scene, size_t n, uint32_t w, uint32_t h, vector<uint8_t> *out) {
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    c.w = w;
    c.h = h;
    c.oy = 0;
    c.cx0 = 0;
    c.cy0 = 0;
    c.cx1 = w;
    c.cy1 = h;
    c.zoom = 1;
    c.background = 0xffffffff;
    resize(c.px, (size_t)w * h * 3);
    for (size_t k = 0; k < len(c.px); k++) c.px[k] = 255;
    amb_svg_draw(&c, scene, n, 0, 0, 1, 1);
    clear(*out);
    for (size_t k = 0; k < (size_t)w * h; k++) push_back(out, (uint8_t)(255 - c.px[3 * k]));
}

/* ---- SVG documents: their natural size, and their drawing ---- */

/* the first element's start tag of an XML document (a BOM, the XML declaration, comments,
   processing instructions, a DOCTYPE and white space before it skipped): its name's place,
   and where its tag ends */
static int sv_xml_root(const char *p, size_t n, size_t *name, size_t *nl, size_t *tag_end) {
    size_t i = 0;
    if (n >= 3 && (uint8_t)p[0] == 0xef && (uint8_t)p[1] == 0xbb && (uint8_t)p[2] == 0xbf) i = 3;
    while (i < n) {
        i = sv_skip_ws(p, n, i);
        if (i >= n || p[i] != '<') return 0;
        if (i + 4 <= n && memcmp(p + i, "<!--", 4) == 0) {
            size_t j = i + 4;
            while (j + 3 <= n && memcmp(p + j, "-->", 3) != 0) j++;
            if (j + 3 > n) return 0;
            i = j + 3;
        } else if (i + 2 <= n && p[i + 1] == '?') {
            size_t j = i + 2;
            while (j + 2 <= n && memcmp(p + j, "?>", 2) != 0) j++;
            if (j + 2 > n) return 0;
            i = j + 2;
        } else if (i + 2 <= n && p[i + 1] == '!') {
            /* a DOCTYPE, its internal subset in brackets */
            size_t j = i + 2;
            int depth = 0;
            while (j < n && (p[j] != '>' || depth > 0)) {
                if (p[j] == '[') depth++;
                else if (p[j] == ']') depth--;
                j++;
            }
            if (j >= n) return 0;
            i = j + 1;
        } else {
            size_t j = i + 1;
            while (j < n && !sv_ws(p[j]) && p[j] != '>' && p[j] != '/') j++;
            *name = i + 1;
            *nl = j - i - 1;
            char q = 0;
            while (j < n && (q || p[j] != '>')) {
                if (q && p[j] == q) q = 0;
                else if (!q && (p[j] == '"' || p[j] == '\'')) q = p[j];
                j++;
            }
            *tag_end = j;
            return j < n;
        }
    }
    return 0;
}
/* an attribute's value in the start tag p[from, to): its place; 0 when it has none */
static int sv_tag_attr(const char *p, size_t from, size_t to, const char *name, size_t *va, size_t *vl) {
    size_t nn = strlen(name), i = from;
    while (i < to) {
        i = sv_skip_ws(p, to, i);
        size_t a = i;
        while (i < to && !sv_ws(p[i]) && p[i] != '=' && p[i] != '>' && p[i] != '/') i++;
        size_t al = i - a;
        i = sv_skip_ws(p, to, i);
        if (i >= to || p[i] != '=') { if (al == 0) i++; continue; }
        i = sv_skip_ws(p, to, i + 1);
        if (i >= to || (p[i] != '"' && p[i] != '\'')) return 0;
        char q = p[i];
        size_t b = i + 1, e = b;
        while (e < to && p[e] != q) e++;
        if (al == nn && memcmp(p + a, name, nn) == 0) { *va = b; *vl = e - b; return 1; }
        i = e + 1;
    }
    return 0;
}

int amb_svg_size(const uint8_t *bytes, size_t n, double *w, double *h, double *ratio) {
    *w = *h = *ratio = 0;
    vector<char> t;
    append_range(t, (const char *)bytes, n);
    size_t name, nl, end;
    if (!sv_xml_root(data(t), len(t), &name, &nl, &end)) return 0;
    int prefixed = nl == 7 && memcmp(data(t) + name, "svg:svg", 7) == 0;
    if (!(nl == 3 && memcmp(data(t) + name, "svg", 3) == 0) && !prefixed) return 0;
    size_t va, vl;
    /* in SVG's namespace, as an SVG image's root must be */
    if (!sv_tag_attr(data(t), name + nl, end, prefixed ? "xmlns:svg" : "xmlns", &va, &vl) || vl != 26 ||
        memcmp(data(t) + va, "http://www.w3.org/2000/svg", 26) != 0) return 0;
    double nw = -1, nh = -1, vb[4];
    int ok;
    if (sv_tag_attr(data(t), name + nl, end, "width", &va, &vl)) {
        double v = sv_length(data(t) + va, vl, -1, 16, &ok);
        if (ok && v >= 0 && !memchr(data(t) + va, '%', vl)) nw = v;
    }
    if (sv_tag_attr(data(t), name + nl, end, "height", &va, &vl)) {
        double v = sv_length(data(t) + va, vl, -1, 16, &ok);
        if (ok && v >= 0 && !memchr(data(t) + va, '%', vl)) nh = v;
    }
    if (sv_tag_attr(data(t), name + nl, end, "viewBox", &va, &vl)) {
        size_t i = sv_skip_ws(data(t) + va, vl, 0);
        int got = 1;
        for (int k = 0; k < 4 && got; k++) {
            if (k) i = sv_skip_sep(data(t) + va, vl, i);
            got = sv_number(data(t) + va, vl, &i, &vb[k]);
        }
        if (got && vb[2] > 0 && vb[3] > 0) *ratio = vb[2] / vb[3];
    }
    if (*ratio <= 0 && nw > 0 && nh > 0) *ratio = nw / nh;
    if (nw >= 0 && nh < 0 && *ratio > 0) nh = nw / *ratio;
    if (nh >= 0 && nw < 0 && *ratio > 0) nw = nh * *ratio;
    *w = nw > 0 ? nw : 0;
    *h = nh > 0 ? nh : 0;
    return 1;
}

/* an SVG document's root has a viewBox attribute */
static int sv_doc_viewbox(const uint8_t *bytes, size_t n) {
    vector<char> t;
    append_range(t, (const char *)bytes, n);
    size_t name, nl, end, va, vl;
    if (!sv_xml_root(data(t), len(t), &name, &nl, &end)) return 0;
    return sv_tag_attr(data(t), name + nl, end, "viewBox", &va, &vl);
}

int amb_svg_size_of(const net_loader *nt, uint32_t r, double *w, double *h, double *ratio) {
    *w = *h = *ratio = 0;
    if (!nt || r == 0 || r >= nt->resource_len || nt->resource[r].outcome != NET_OUTCOME_OK) return 0;
    amb_span b = nt->resource[r].body;
    vector<uint8_t> bytes;
    append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
    return amb_svg_size(data(bytes), len(bytes), w, h, ratio);
}

/* ---- SVG documents read: a small XML parser, for well-formed files ---- */

static const char sv_ns_svg[] = "http://www.w3.org/2000/svg";
static const char sv_ns_xlink[] = "http://www.w3.org/1999/xlink";

static void sv_utf8(vector<char> *out, uint32_t cp) {
    if (cp < 0x80) push_back(out, (char)cp);
    else if (cp < 0x800) { push_back(out, (char)(0xc0 | cp >> 6)); push_back(out, (char)(0x80 | (cp & 0x3f))); }
    else if (cp < 0x10000) {
        push_back(out, (char)(0xe0 | cp >> 12));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3f)));
        push_back(out, (char)(0x80 | (cp & 0x3f)));
    } else {
        push_back(out, (char)(0xf0 | cp >> 18));
        push_back(out, (char)(0x80 | (cp >> 12 & 0x3f)));
        push_back(out, (char)(0x80 | (cp >> 6 & 0x3f)));
        push_back(out, (char)(0x80 | (cp & 0x3f)));
    }
}
/* text from s[a, b), its five predefined entities and character references made what they
   name, appended to out; 0 on a reference it does not know */
static int sv_xml_text(const char *s, size_t a, size_t b, vector<char> *out) {
    size_t i = a;
    while (i < b) {
        if (s[i] != '&') { push_back(out, s[i]); i++; continue; }
        size_t e = i + 1;
        while (e < b && s[e] != ';' && e - i < 12) e++;
        if (e >= b || s[e] != ';') return 0;
        size_t r = i + 1, rl = e - i - 1;
        uint32_t cp = 0;
        if (rl == 2 && memcmp(s + r, "lt", 2) == 0) cp = '<';
        else if (rl == 2 && memcmp(s + r, "gt", 2) == 0) cp = '>';
        else if (rl == 3 && memcmp(s + r, "amp", 3) == 0) cp = '&';
        else if (rl == 4 && memcmp(s + r, "quot", 4) == 0) cp = '"';
        else if (rl == 4 && memcmp(s + r, "apos", 4) == 0) cp = '\'';
        else if (rl >= 2 && s[r] == '#') {
            int hex = s[r + 1] == 'x';
            size_t q = r + 1 + (size_t)hex;
            if (q >= e) return 0;
            for (; q < e; q++) {
                char c = s[q];
                int dg = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (dg < 0 || cp > 0x10ffff) return 0;
                cp = cp * (hex ? 16 : 10) + (uint32_t)dg;
            }
            if (cp == 0 || cp > 0x10ffff) return 0;
        } else return 0;
        sv_utf8(out, cp);
        i = e + 1;
    }
    return 1;
}

/* a namespace bound: its prefix's place in the file (none for the default), its URI's in
   uris, and the depth of the element that bound it */
typedef struct sv_bind { size_t pa, pl, ua, ul; int depth; } sv_bind;

/* the URI a prefix (p[a, a + l), none for the default) is bound to at this depth: its place in uris */
static int sv_ns_of(const char *p, size_t a, size_t l, const vector<sv_bind> *binds, size_t *ua, size_t *ul) {
    for (size_t k = len(*binds); k > 0; k--) {
        sv_bind b = at(*binds, k - 1);
        if (b.pl == l && (l == 0 || memcmp(p + b.pa, p + a, l) == 0)) { *ua = b.ua; *ul = b.ul; return 1; }
    }
    return 0;
}

/* an SVG document's bytes read as XML into d: elements of SVG's namespace (another's and
   theirs left out), their attributes (an XLink one's under its prefix xlink), text and
   CDATA; the XML declaration, processing instructions, comments and a DOCTYPE skipped.
   The root svg element's index, or 0 when the bytes are not well formed or it is none */
static uint32_t sv_xml_parse(const char *p, size_t n, dom_document *d, vector<char> *uris) {
    vector<sv_bind> binds;
    vector<uint32_t> open;          /* the elements open: their node's index, 0 for one left out */
    vector<char> txt, nm, val;
    uint32_t root = 0;
    int skip = 0;                   /* within an element left out: how deep */
    size_t i = 0;
    if (n >= 3 && (uint8_t)p[0] == 0xef && (uint8_t)p[1] == 0xbb && (uint8_t)p[2] == 0xbf) i = 3;
    dom_node_h doc = dom_root(d);
    while (i < n) {
        if (p[i] != '<') {
            size_t e = i;
            while (e < n && p[e] != '<') e++;
            if (len(open) && !skip && at(open, len(open) - 1)) {
                clear(txt);
                if (!sv_xml_text(p, i, e, &txt)) return 0;
                dom_node_h t = dom_create_text(d, data(txt), len(txt));
                dom_append_child(d, node_handle(d, at(open, len(open) - 1)), t);
            }
            i = e;
            continue;
        }
        if (i + 4 <= n && memcmp(p + i, "<!--", 4) == 0) {
            size_t e = i + 4;
            while (e + 3 <= n && memcmp(p + e, "-->", 3) != 0) e++;
            if (e + 3 > n) return 0;
            i = e + 3;
            continue;
        }
        if (i + 9 <= n && memcmp(p + i, "<![CDATA[", 9) == 0) {
            size_t e = i + 9;
            while (e + 3 <= n && memcmp(p + e, "]]>", 3) != 0) e++;
            if (e + 3 > n) return 0;
            if (len(open) && !skip && at(open, len(open) - 1)) {
                dom_node_h t = dom_create_text(d, p + i + 9, e - i - 9);
                dom_append_child(d, node_handle(d, at(open, len(open) - 1)), t);
            }
            i = e + 3;
            continue;
        }
        if (i + 2 <= n && (p[i + 1] == '?' || p[i + 1] == '!')) {
            /* the declaration, a processing instruction, a DOCTYPE (its subset in brackets) */
            size_t e = i + 2;
            int depth = 0;
            char end = p[i + 1] == '?' ? '?' : 0;
            while (e < n && !(end ? (p[e] == '?' && e + 1 < n && p[e + 1] == '>') : (p[e] == '>' && depth == 0))) {
                if (!end && p[e] == '[') depth++;
                else if (!end && p[e] == ']') depth--;
                e++;
            }
            if (e >= n) return 0;
            i = e + (end ? 2 : 1);
            continue;
        }
        if (i + 2 <= n && p[i + 1] == '/') {
            size_t e = i + 2;
            while (e < n && p[e] != '>') e++;
            if (e >= n || !len(open)) return 0;
            size_t depth = len(open);
            while (len(binds) && at(binds, len(binds) - 1).depth >= (int)depth) resize(binds, len(binds) - 1);
            resize(open, len(open) - 1);
            if (skip) skip--;
            i = e + 1;
            continue;
        }
        /* a start tag: its name, its attributes, whether it closes itself */
        size_t na = i + 1, ne = na;
        while (ne < n && !sv_ws(p[ne]) && p[ne] != '>' && p[ne] != '/') ne++;
        if (ne == na || ne >= n || len(open) > 256) return 0;
        size_t te = ne;
        char q = 0;
        while (te < n && (q || p[te] != '>')) {
            if (q && p[te] == q) q = 0;
            else if (!q && (p[te] == '"' || p[te] == '\'')) q = p[te];
            te++;
        }
        if (te >= n) return 0;
        int closes = p[te - 1] == '/';
        size_t ae = closes ? te - 1 : te;
        int depth = (int)len(open) + 1;
        /* its namespace declarations first */
        size_t k = ne;
        while (k < ae) {
            k = sv_skip_ws(p, ae, k);
            size_t a0 = k;
            while (k < ae && !sv_ws(p[k]) && p[k] != '=') k++;
            size_t al = k - a0;
            k = sv_skip_ws(p, ae, k);
            if (k >= ae || p[k] != '=') { if (al == 0) break; return 0; }
            k = sv_skip_ws(p, ae, k + 1);
            if (k >= ae || (p[k] != '"' && p[k] != '\'')) return 0;
            char qq = p[k];
            size_t v0 = k + 1, v1 = v0;
            while (v1 < ae && p[v1] != qq) v1++;
            if (v1 >= ae) return 0;
            if ((al == 5 && memcmp(p + a0, "xmlns", 5) == 0) || (al > 6 && memcmp(p + a0, "xmlns:", 6) == 0)) {
                sv_bind b;
                b.pa = al == 5 ? 0 : a0 + 6;
                b.pl = al == 5 ? 0 : al - 6;
                b.ua = len(*uris);
                clear(val);
                if (!sv_xml_text(p, v0, v1, &val)) return 0;
                append_range(uris, data(val), len(val));
                b.ul = len(val);
                b.depth = depth;
                push_back(binds, b);
            }
            k = v1 + 1;
        }
        /* the element's namespace, from its prefix */
        size_t colon = na;
        while (colon < ne && p[colon] != ':') colon++;
        size_t ua = 0, ul = 0, la = colon < ne ? colon + 1 : na;
        int bound = sv_ns_of(p, na, colon < ne ? colon - na : 0, &binds, &ua, &ul);
        int svg = bound && ul == strlen(sv_ns_svg) && memcmp(data(*uris) + ua, sv_ns_svg, ul) == 0;
        uint32_t el = 0;
        if (!skip && svg && (len(open) == 0 || at(open, len(open) - 1))) {
            dom_node_h h = dom_create_element_ns(d, DOM_NAMESPACE_SVG, p + la, ne - la);
            el = h.index;
            if (len(open) == 0) {
                if (root) return 0;
                root = el;
                dom_append_child(d, doc, h);
            } else dom_append_child(d, node_handle(d, at(open, len(open) - 1)), h);
            /* its other attributes */
            k = ne;
            while (k < ae) {
                k = sv_skip_ws(p, ae, k);
                size_t a0 = k;
                while (k < ae && !sv_ws(p[k]) && p[k] != '=') k++;
                size_t al = k - a0;
                k = sv_skip_ws(p, ae, k);
                if (k >= ae || p[k] != '=') break;
                k = sv_skip_ws(p, ae, k + 1);
                char qq = p[k];
                size_t v0 = k + 1, v1 = v0;
                while (v1 < ae && p[v1] != qq) v1++;
                k = v1 + 1;
                if ((al == 5 && memcmp(p + a0, "xmlns", 5) == 0) || (al > 6 && memcmp(p + a0, "xmlns:", 6) == 0)) continue;
                clear(val);
                if (!sv_xml_text(p, v0, v1, &val)) return 0;
                size_t ac = a0;
                while (ac < a0 + al && p[ac] != ':') ac++;
                if (ac < a0 + al) {
                    size_t xa, xl;
                    int xb = sv_ns_of(p, a0, ac - a0, &binds, &xa, &xl);
                    if (xb && xl == strlen(sv_ns_xlink) && memcmp(data(*uris) + xa, sv_ns_xlink, xl) == 0) {
                        clear(nm);
                        append_range(nm, "xlink:", 6);
                        append_range(nm, p + ac + 1, a0 + al - ac - 1);
                        dom_set_attribute_ns(d, h, DOM_ATTR_NAMESPACE_XLINK, data(nm), len(nm), data(val), len(val));
                        continue;
                    }
                }
                dom_set_attribute(d, h, p + a0, al, data(val), len(val));
            }
        } else if (len(open) == 0) return 0;
        if (!el) skip++;
        if (closes) {
            while (len(binds) && at(binds, len(binds) - 1).depth >= depth) resize(binds, len(binds) - 1);
            if (!el) skip--;
        } else push_back(open, el);
        i = te + 1;
    }
    return len(open) == 0 ? root : 0;
}

/* an SVG document's bytes made a scene for a viewport vw x vh: read, cascaded (its own
   style elements), walked; 0 when they are none */
static int sv_doc_scene(const uint8_t *bytes, size_t n, double vw, double vh, vector<char> *scene) {
    vector<char> t, uris;
    append_range(t, (const char *)bytes, n);
    dom_document d;
    dom_init(&d, 1);
    uint32_t root = sv_xml_parse(data(t), len(t), &d, &uris);
    if (!root) return 0;
    css_style st;
    css_init(&st, 1);
    css_cascade(&st, &d, 0);
    vector<uint32_t> firsts;
    resize(firsts, d.node_len);
    for (uint32_t e = 1; e < st.estyle_len; e++) {
        uint32_t x = st.estyle[e].node.index;
        if (x < d.node_len && !at(firsts, x)) firsts[x] = st.estyle[e].first;
    }
    amb_fonts f = {0};
    defer(f) { AMB_FONTS_DROP(f); }
    amb_fonts_load(&f);
    vector<char> sc;
    amb_svg_scene(&d, &st, &firsts, &f, root, vw, vh, &sc);
    /* a document drawn as an image fetches nothing (Chromium's, measured): an image element's (or
       an feImage's) picture is drawn when its href is a data: URL, whose bytes its line then
       carries, as base64 after a d; any other draws nothing, not even the broken image */
    size_t i = 0, n2 = len(sc);
    while (i < n2) {
        size_t e = i;
        while (e < n2 && at(sc, e) != '\n') e++;
        append_range(scene, data(sc) + i, e - i);
        if (e - i > 2 && at(sc, i) == 'i' && at(sc, i + 1) == ' ') {
            size_t b = e;
            while (b > i && at(sc, b - 1) != ' ') b--;
            char num[24];
            size_t k = e - b < 23 ? e - b : 23;
            memcpy(num, data(sc) + b, k);
            num[k] = 0;
            uint32_t el = (uint32_t)strtoul(num, 0, 10);
            vector<char> v;
            vector<uint8_t> bytes;
            if (el > 0 && el < d.node_len && (sv_attr_text(&d, el, "href", &v) || sv_attr_text(&d, el, "xlink:href", &v)) &&
                sv_data_url(data(v), len(v), &bytes) && len(bytes)) {
                sv_w(scene, " d ");
                sv_b64_put(scene, data(bytes), len(bytes));
            }
        }
        if (e < n2) push_back(scene, '\n');
        i = e + 1;
    }
    return 1;
}

/* the viewport an SVG image is drawn in, w x h px for its box (z px a CSS px): its natural
   size, or the box's */
static int sv_image_viewport(const uint8_t *p, size_t n, double w, double h, double z, double *vw, double *vh) {
    double nw, nh, r;
    if (w <= 0 || h <= 0 || !amb_svg_size(p, n, &nw, &nh, &r)) return 0;
    if (nw > 0 && nh > 0) { *vw = nw; *vh = nh; }
    else { *vw = w / z; *vh = h / z; }
    return 1;
}

int amb_svg_image(amb_canvas *c, const uint8_t *p, size_t n, double x, double y, double w, double h) {
    double vw, vh;
    if (!sv_image_viewport(p, n, w, h, c->zoom > 0 ? c->zoom : 1, &vw, &vh)) return 0;
    vector<char> scene;
    if (!sv_doc_scene(p, n, vw, vh, &scene)) return 0;
    amb_svg_draw(c, data(scene), len(scene), x, y, w / vw, h / vh);
    return 1;
}

int amb_svg_image_of(amb_canvas *c, const net_loader *nt, uint32_t el, double x, double y, double w, double h) {
    if (!nt) return 0;
    for (uint32_t r = 1; r < nt->resource_len; r++) {
        if (!nt->resource[r].live || nt->resource[r].elem.index != el || nt->resource[r].kind != NET_KIND_IMAGE) continue;
        if (nt->resource[r].outcome != NET_OUTCOME_OK) return 0;
        amb_span b = nt->resource[r].body;
        vector<uint8_t> bytes;
        append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
        return amb_svg_image(c, data(bytes), len(bytes), x, y, w, h);
    }
    return 0;
}

int amb_svg_raster_of(const net_loader *nt, uint32_t r, uint32_t w, uint32_t h, vector<uint8_t> *rgba) {
    if (!nt || r == 0 || r >= nt->resource_len || nt->resource[r].outcome != NET_OUTCOME_OK || w == 0 || h == 0 ||
        (uint64_t)w * h > AMB_IMAGE_MAX_PIXELS) return 0;
    amb_span b = nt->resource[r].body;
    vector<uint8_t> bytes;
    append_range(bytes, (const uint8_t *)bytes_ptr(nt, b), b.len);
    double vw, vh;
    if (!sv_image_viewport(data(bytes), len(bytes), w, h, 1, &vw, &vh)) return 0;
    vector<char> scene;
    if (!sv_doc_scene(data(bytes), len(bytes), vw, vh, &scene)) return 0;
    clear(*rgba);
    resize(rgba, (size_t)w * h * 4);
    size_t i;
    double sw, sh;
    int clip;
    if (!sv_head(data(scene), len(scene), &sw, &sh, &clip, &i)) return 1;
    vector<float> layv;
    sv_render(data(scene), len(scene), i, (int)w, (int)h, sv_make(w / vw, 0, 0, h / vh, 0, 0), 0, 0, &layv);
    /* unpremultiplied, as an image's pixels are */
    for (size_t p = 0; p < (size_t)w * h; p++) {
        float a = at(layv, 4 * p + 3);
        if (a <= 0) continue;
        for (int k = 0; k < 3; k++) {
            double v = at(layv, 4 * p + (size_t)k) / a * 255;
            (*rgba)[4 * p + (size_t)k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
        }
        (*rgba)[4 * p + 3] = (uint8_t)(a > 1 ? 255 : a * 255 + 0.5);
    }
    return 1;
}
