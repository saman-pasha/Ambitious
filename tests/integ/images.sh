#!/bin/sh
# tests/integ/images.sh -- a page of JPEGs, GIFs and WebPs (tests/pages/images.html, drawn from
# the decoders' fixtures in tests/images/): loaded, each image with the size the loader
# read from its header; laid out, each image's box; and drawn, its pixels' checksum,
# which tests/golden/render/images.png shows (make golden writes it). The pixels of each
# image are Pillow's (tests/exec/jpeg_decode, gif_decode and webp_decode), an animation's
# the frame it shows at rest; here they are drawn. The arithmetic-coded and 12-bit JPEGs
# keep the boxes their headers give and are not drawn, as in Chromium.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-images.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s#file://$AMB_ROOT/#file:///ROOT/#g"; }
page=$AMB_ROOT/tests/pages/images.html
amb load "$page"
echo "-- the images, as laid out:"
amb query "$page" "dom_element(N, img), dom_attr(N, alt, A), layout_box_of(N, B), layout_box(B, _, _, X, Y, W, H)"
echo "-- drawn:"
amb render "$page" "$work/images.png"
st=$?
rm -rf "$work"
echo "exit $st"
