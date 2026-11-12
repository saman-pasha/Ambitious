#!/usr/bin/env python3
# tools/gen-bmp.py -- BMP and ICO files of every kind Chromium's decoders read, for the
# image fixtures (tests/pages/bmp*), made from one source picture:
#
#     python3 tools/gen-bmp.py [options] IN.png OUT.bmp
#
#   --header core|info|v2|v3|v4|v5   the DIB header: OS/2's 12 bytes, BITMAPINFOHEADER's 40,
#                                    52 and 56 (the masks after it), V4's 108, V5's 124
#   --bits 1|4|8|16|24|32            bits a pixel (1, 4 and 8 through a palette of the
#                                    picture's colours, quantized)
#   --rle                            RLE8 (8 bits) or RLE4 (4 bits) compression
#   --masks R,G,B[,A]                BI_BITFIELDS with these masks (hex), 16 or 32 bits
#   --alpha                          32 bits: the picture's alpha in the fourth byte
#   --topdown                        rows from the top (a negative height)
#   --ico [png]                      an ICO of the BMP (its AND mask from the alpha) or,
#                                    with png, of the picture as a PNG
#   --sizes 16,32                    an ICO's entries: the picture scaled to each size
#
# The rows are padded to four bytes; an RLE stream uses runs, absolute runs and, where the
# picture is transparent, delta escapes (pixels left out, transparent in Chromium's reader).
import io
import struct
import sys
from PIL import Image


def pal_image(im, bits):
    """the picture as palette indices of at most 2**bits colours; the palette's RGB"""
    n = 1 << bits
    rgb = im.convert('RGB')
    q = rgb.quantize(colors=n, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:3 * n]
    pal += [0] * (3 * n - len(pal))
    return q, [tuple(pal[3 * i:3 * i + 3]) for i in range(n)]


def rows_of(im, bits, masks, alpha):
    """each row's bytes, unpadded, bottom-up order chosen by the caller"""
    w, h = im.size
    out = []
    if bits <= 8:
        q, pal = pal_image(im, bits)
        px = q.load()
        for y in range(h):
            acc, nb, row = 0, 0, bytearray()
            for x in range(w):
                acc = (acc << bits) | px[x, y]
                nb += bits
                if nb == 8:
                    row.append(acc)
                    acc, nb = 0, 0
            if nb:
                row.append(acc << (8 - nb))
            out.append(bytes(row))
        return out, pal
    rgba = im.convert('RGBA')
    px = rgba.load()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[x, y]
            if masks:
                v = 0
                for c, m in zip((r, g, b, a), masks):
                    if not m:
                        continue
                    shift = (m & -m).bit_length() - 1
                    width = bin(m).count('1')
                    v |= ((c * ((1 << width) - 1) + 127) // 255) << shift
                row += struct.pack('<H' if bits == 16 else '<I', v)
            elif bits == 16:
                v = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                row += struct.pack('<H', v)
            elif bits == 24:
                row += bytes((b, g, r))
            else:
                row += bytes((b, g, r, a if alpha else 0))
        out.append(bytes(row))
    return out, None


def rle_rows(q, bits, im_alpha):
    """RLE8 or RLE4 of the palette picture q, bottom-up; a transparent pixel left out by a delta"""
    w, h = q.size
    px = q.load()
    data = bytearray()
    for yy in range(h):
        y = h - 1 - yy
        x = 0
        while x < w:
            if im_alpha is not None and im_alpha[x, y] < 128:
                k = x
                while k < w and im_alpha[k, y] < 128 and k - x < 255:
                    k += 1
                data += bytes((0, 2, k - x, 0))
                x = k
                continue
            v = px[x, y]
            k = x
            while k < w and px[k, y] == v and k - x < 255 and (im_alpha is None or im_alpha[k, y] >= 128):
                k += 1
            if k - x >= 3 or bits == 4:
                if bits == 8:
                    data += bytes((k - x, v))
                else:
                    data += bytes((k - x, (v << 4) | v))
                x = k
            else:
                # an absolute run of up to the next repeat
                k = x
                while k < w and k - x < 255 and (im_alpha is None or im_alpha[k, y] >= 128):
                    if k + 2 < w and px[k, y] == px[k + 1, y] == px[k + 2, y]:
                        break
                    k += 1
                cnt = k - x
                if cnt < 3:
                    for i in range(cnt):
                        data += bytes((1, px[x + i, y]))
                    x = k
                    continue
                data += bytes((0, cnt))
                run = bytes(px[x + i, y] for i in range(cnt))
                data += run
                if cnt & 1:
                    data.append(0)
                x = k
        data += bytes((0, 0))
    data[-2:] = bytes((0, 1))
    return bytes(data)


def bmp_bytes(im, header, bits, rle, masks, alpha, topdown, file_header=True, and_mask=False):
    w, h = im.size
    comp = 0
    pal = None
    if rle:
        comp = 1 if bits == 8 else 2
        q, pal = pal_image(im, bits)
        a = im.convert('RGBA').split()[3].load() if im.mode in ('RGBA', 'LA', 'P') else None
        pixels = rle_rows(q, bits, a)
    else:
        rows, pal = rows_of(im, bits, masks, alpha)
        if masks:
            comp = 3 if len(masks) == 3 or header in ('v2',) else (6 if header == 'info' and len(masks) == 4 else 3)
        order = rows if topdown else rows[::-1]
        pixels = b''.join(r + b'\0' * (-len(r) % 4) for r in order)
    if and_mask:
        a = im.convert('RGBA').split()[3].load()
        mask_rows = []
        for y in range(h):
            acc, nb, row = 0, 0, bytearray()
            for x in range(w):
                acc = (acc << 1) | (1 if a[x, y] < 128 else 0)
                nb += 1
                if nb == 8:
                    row.append(acc)
                    acc, nb = 0, 0
            if nb:
                row.append(acc << (8 - nb))
            mask_rows.append(bytes(row) + b'\0' * (-len(row) % 4))
        pixels += b''.join(mask_rows[::-1])
    hh = -h if topdown else h
    if and_mask:
        hh = 2 * h
    if header == 'core':
        dib = struct.pack('<IHHHH', 12, w, hh & 0xFFFF, 1, bits)
        palb = b''.join(bytes((b, g, r)) for (r, g, b) in pal) if pal else b''
    else:
        size = {'info': 40, 'v2': 52, 'v3': 56, 'v4': 108, 'v5': 124}[header]
        dib = struct.pack('<IiiHHIIiiII', size, w, hh, 1, bits, comp, len(pixels), 2835, 2835, len(pal) if pal else 0, 0)
        if size >= 52:
            m = list(masks or (0, 0, 0)) + [0] * 4
            dib += struct.pack('<III', m[0], m[1], m[2])
            if size >= 56:
                dib += struct.pack('<I', m[3] if masks and len(masks) == 4 else 0)
            if size >= 108:
                dib += struct.pack('<I', 0x73524742) + b'\0' * 36 + struct.pack('<III', 0, 0, 0)
            if size == 124:
                dib += struct.pack('<IIII', 4, 0, 0, 0)
        elif masks and comp in (3, 6):
            dib += b''.join(struct.pack('<I', m) for m in masks)
        palb = b''.join(bytes((b, g, r, 0)) for (r, g, b) in pal) if pal else b''
    body = dib + palb
    if not file_header:
        return body + pixels
    off = 14 + len(body)
    return b'BM' + struct.pack('<IHHI', off + len(pixels), 0, 0, off) + body + pixels


def ico_bytes(im, sizes, png):
    entries, blobs = [], []
    for s in sizes:
        pic = im.resize((s, s), Image.LANCZOS) if im.size != (s, s) else im
        if png:
            buf = io.BytesIO()
            pic.save(buf, 'PNG')
            blob = buf.getvalue()
            bits = 32
        else:
            blob = bmp_bytes(pic.convert('RGBA'), 'info', 32, False, None, True, False, file_header=False, and_mask=True)
            bits = 32
        entries.append((s, bits, blob))
    out = struct.pack('<HHH', 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    dirs = b''
    for s, bits, blob in entries:
        dirs += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, bits, len(blob), off)
        off += len(blob)
    return out + dirs + b''.join(b for _, _, b in entries)


def main():
    args = sys.argv[1:]
    opt = {'header': 'info', 'bits': 24, 'rle': False, 'masks': None, 'alpha': False, 'topdown': False, 'ico': None, 'sizes': None}
    pos = []
    while args:
        a = args.pop(0)
        if a in ('--rle', '--alpha', '--topdown'):
            opt[a[2:]] = True
        elif a == '--ico':
            opt['ico'] = args.pop(0) if args and args[0] in ('png', 'bmp') else 'bmp'
        elif a == '--masks':
            opt['masks'] = [int(x, 16) for x in args.pop(0).split(',')]
        elif a == '--sizes':
            opt['sizes'] = [int(x) for x in args.pop(0).split(',')]
        elif a.startswith('--'):
            v = args.pop(0)
            opt[a[2:]] = int(v) if a == '--bits' else v
        else:
            pos.append(a)
    src, out = pos
    im = Image.open(src)
    if opt['ico']:
        data = ico_bytes(im.convert('RGBA'), opt['sizes'] or [im.size[0]], opt['ico'] == 'png')
    else:
        data = bmp_bytes(im.convert('RGBA'), opt['header'], opt['bits'], opt['rle'], opt['masks'], opt['alpha'], opt['topdown'])
    open(out, 'wb').write(data)


main()
