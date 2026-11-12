#!/usr/bin/env python3
# tools/gen-lossless-jpeg.py -- lossless JPEG files (ITU T.81, Annex H: the process of
# SOF3) for the image fixtures, which no encoder on this machine writes:
#
#     python3 tools/gen-lossless-jpeg.py [options] IN.png OUT.jpg
#
#   --space grey|rgb|ycc   grey: one component; rgb: three, with Adobe's APP14 of transform
#                          0 (no colour transform); ycc: three, the image made YCbCr (JFIF's
#                          equations, rounded) under a JFIF APP0, as a decoder turns it back
#   --predictor N          the predictor, 1 to 7 (H.1.2.1)
#   --pt N                 the point transform: each sample shifted right by N first
#   --restart-rows N       a restart marker every N rows of MCUs (0: none)
#   --precision P          the sample precision, 2 to 16 (the image's 8 bits scaled to it)
#   --separate             one scan a component, where they are interleaved by default
#
# Each difference is coded as a DC coefficient is: its category SSSS (0 to 16) by a
# Huffman code, then SSSS bits (a negative difference less one). The Huffman table is
# made from the categories' counts, as an optimizing encoder makes one (K.2), lengths at
# most 16.
import sys, struct
from PIL import Image


def category(d):
    d = abs(d)
    s = 0
    while d:
        s += 1
        d >>= 1
    return s


def huff_lengths(freq):
    # K.2's procedure: code lengths from the symbols' counts, the all-ones code kept out
    freq = list(freq) + [1]           # a reserved symbol, 256 of K.2, which no code ends in
    n = len(freq)
    codesize = [0] * n
    others = [-1] * n
    f = [x if x > 0 else 0 for x in freq]
    while True:
        cand = [i for i in range(n) if f[i] > 0]
        if len(cand) < 2:
            break
        cand.sort(key=lambda i: (f[i], -i))
        v1, v2 = cand[0], cand[1]
        f[v1] += f[v2]
        f[v2] = 0
        codesize[v1] += 1
        while others[v1] != -1:
            v1 = others[v1]
            codesize[v1] += 1
        others[v1] = v2
        codesize[v2] += 1
        while others[v2] != -1:
            v2 = others[v2]
            codesize[v2] += 1
    bits = [0] * 33
    for i in range(n):
        if codesize[i]:
            bits[codesize[i]] += 1
    i = 32
    while i > 16:
        while bits[i] > 0:
            j = i - 2
            while bits[j] == 0:
                j -= 1
            bits[i] -= 2
            bits[i - 1] += 1
            bits[j + 1] += 2
            bits[j] -= 1
        i -= 1
    while bits[i] == 0:
        i -= 1
    bits[i] -= 1                      # the reserved symbol's code taken away
    order = sorted([i for i in range(n - 1) if codesize[i]], key=lambda i: (codesize[i], i))
    return bits[1:17], order


def main():
    args = sys.argv[1:]
    opt = {'space': 'grey', 'predictor': 1, 'pt': 0, 'restart-rows': 0, 'precision': 8, 'separate': False}
    pos = []
    while args:
        a = args.pop(0)
        if a == '--separate':
            opt['separate'] = True
        elif a.startswith('--'):
            k = a[2:]
            v = args.pop(0)
            opt[k] = v if k == 'space' else int(v)
        else:
            pos.append(a)
    src, out = pos
    im = Image.open(src).convert('RGB')
    w, h = im.size
    px = im.load()
    prec, pt, pred = opt['precision'], opt['pt'], opt['predictor']
    scale = lambda v: (v * ((1 << prec) - 1) + 127) // 255
    if opt['space'] == 'grey':
        comps = [[[scale((299 * px[x, y][0] + 587 * px[x, y][1] + 114 * px[x, y][2] + 500) // 1000) for x in range(w)] for y in range(h)]]
        ids = [1]
    elif opt['space'] == 'rgb':
        comps = [[[scale(px[x, y][c]) for x in range(w)] for y in range(h)] for c in range(3)]
        ids = [1, 2, 3]
    else:
        def ycc(r, g, b):
            yy = 0.299 * r + 0.587 * g + 0.114 * b
            cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128
            cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128
            return [min(255, max(0, int(round(v)))) for v in (yy, cb, cr)]
        planes = [[[0] * w for _ in range(h)] for _ in range(3)]
        for y in range(h):
            for x in range(w):
                v = ycc(*px[x, y])
                for c in range(3):
                    planes[c][y][x] = scale(v[c])
        comps = planes
        ids = [1, 2, 3]
    nc = len(comps)
    mask = (1 << 16) - 1
    # each component's differences, row by row, as the decoder will predict them
    diffs = []
    for c in range(nc):
        s = [[v >> pt for v in row] for row in comps[c]]
        dc = [[0] * w for _ in range(h)]
        rr = opt['restart-rows']
        for y in range(h):
            first_line = y == 0 or (rr and y % rr == 0)
            for x in range(w):
                if first_line:
                    p = (1 << (prec - pt - 1)) if x == 0 else s[y][x - 1]
                elif x == 0:
                    p = s[y - 1][x]
                else:
                    ra, rb, rc = s[y][x - 1], s[y - 1][x], s[y - 1][x - 1]
                    p = [0, ra, rb, rc, ra + rb - rc, ra + ((rb - rc) >> 1), rb + ((ra - rc) >> 1), (ra + rb) >> 1][pred]
                d = (s[y][x] - p) & mask
                if d >= 32768:
                    d -= 65536
                dc[y][x] = d
        diffs.append(dc)
    freq = [0] * 17
    for dc in diffs:
        for row in dc:
            for d in row:
                freq[16 if d == -32768 else category(d)] += 1
    bits, order = huff_lengths(freq)
    code = 0
    codes = {}
    k = 0
    for ln in range(1, 17):
        for _ in range(bits[ln - 1]):
            codes[order[k]] = (code, ln)
            code += 1
            k += 1
        code <<= 1
    data = bytearray()

    def seg(marker, body):
        data.extend(struct.pack('>BBH', 0xFF, marker, len(body) + 2))
        data.extend(body)

    data.extend(b'\xFF\xD8')
    if opt['space'] == 'ycc':
        seg(0xE0, b'JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00')
    if opt['space'] == 'rgb':
        seg(0xEE, b'Adobe\x00\x64\x00\x00\x00\x00\x00')    # transform 0: RGB as it is
    seg(0xC4, bytes([0x00]) + bytes(bits) + bytes(order))
    sof = struct.pack('>BHHB', prec, h, w, nc)
    for i in ids:
        sof += bytes([i, 0x11, 0])
    seg(0xC3, sof)
    if opt['restart-rows']:
        ri = opt['restart-rows'] * w
        seg(0xDD, struct.pack('>H', ri))
    scans = [[c] for c in range(nc)] if opt['separate'] else [list(range(nc))]
    for sc in scans:
        body = bytes([len(sc)])
        for c in sc:
            body += bytes([ids[c], 0x00])
        body += bytes([pred, 0, pt])
        seg(0xDA, body)
        acc, cnt = 0, 0
        out_bytes = bytearray()

        def put(v, n):
            nonlocal acc, cnt
            acc = (acc << n) | (v & ((1 << n) - 1))
            cnt += n
            while cnt >= 8:
                cnt -= 8
                b = (acc >> cnt) & 0xFF
                out_bytes.append(b)
                if b == 0xFF:
                    out_bytes.append(0)

        def flush():
            nonlocal acc, cnt
            if cnt:
                put((1 << (8 - cnt)) - 1, 8 - cnt)
            acc, cnt = 0, 0

        rst = 0
        rr = opt['restart-rows']
        for y in range(h):
            if rr and y and y % rr == 0:
                flush()
                out_bytes.extend(bytes([0xFF, 0xD0 + rst]))
                rst = (rst + 1) & 7
            for x in range(w):
                for c in sc:
                    d = diffs[c][y][x]
                    s = 16 if d == -32768 else category(d)
                    cc, ln = codes[s]
                    put(cc, ln)
                    if 0 < s < 16:
                        put(d if d >= 0 else d - 1, s)
        flush()
        data.extend(out_bytes)
    data.extend(b'\xFF\xD9')
    open(out, 'wb').write(bytes(data))


main()
