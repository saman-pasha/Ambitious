#!/usr/bin/env python3
"""tools/dict-encode.py br|zstd DICTIONARY [QUALITY [LGWIN]] < IN > OUT -- IN compressed against a
raw dictionary, as Compression Dictionary Transport's dcb and dcz codings compress (RFC 9842):

    br    Brotli with DICTIONARY attached as a raw (LZ77 prefix) dictionary, by the system's
          libbrotlienc (1.1.0: BrotliEncoderPrepareDictionary, BrotliEncoderAttachPrepared-
          Dictionary), at QUALITY (11) and a window of LGWIN bits (22)
    zstd  Zstandard with DICTIONARY as raw content (a prefix: no dictionary ID in the frame), by
          the system's libzstd (ZSTD_CCtx_refPrefix), at level QUALITY (19) and a window of
          LGWIN bits (the level's own when 0)

Only the compressed stream is written: the dcb and dcz headers (a magic number and the
dictionary's SHA-256) are the caller's. ctypes reaches the libraries, so no header of
theirs is needed (the system's libzstd has none installed). tests/net/reference.sh and the
gates' servers (tests/net/h2serve.js) run it; tools/gen-dict-vectors.py makes
tests/exec/dict/ with it."""
import ctypes
import ctypes.util
import sys


def lib(names):
    for n in names:
        try:
            return ctypes.CDLL(n)
        except OSError:
            continue
    sys.exit("dict-encode: none of %s could be loaded" % ", ".join(names))


def brotli(data, dictionary, quality, lgwin):
    enc = lib(["libbrotlienc.so.1", ctypes.util.find_library("brotlienc") or "libbrotlienc.so"])
    enc.BrotliEncoderPrepareDictionary.restype = ctypes.c_void_p
    enc.BrotliEncoderPrepareDictionary.argtypes = [ctypes.c_int, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int,
                                                   ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    enc.BrotliEncoderCreateInstance.restype = ctypes.c_void_p
    enc.BrotliEncoderCreateInstance.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    enc.BrotliEncoderSetParameter.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]
    enc.BrotliEncoderAttachPreparedDictionary.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    enc.BrotliEncoderCompressStream.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t),
                                                ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
                                                ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
    enc.BrotliEncoderIsFinished.argtypes = [ctypes.c_void_p]
    enc.BrotliEncoderDestroyInstance.argtypes = [ctypes.c_void_p]
    enc.BrotliEncoderDestroyPreparedDictionary.argtypes = [ctypes.c_void_p]
    d = enc.BrotliEncoderPrepareDictionary(0, len(dictionary), dictionary, quality, None, None, None)
    s = enc.BrotliEncoderCreateInstance(None, None, None)
    if not d or not s:
        sys.exit("dict-encode: brotli could not be set up")
    enc.BrotliEncoderSetParameter(s, 1, quality)       # BROTLI_PARAM_QUALITY
    enc.BrotliEncoderSetParameter(s, 2, lgwin)         # BROTLI_PARAM_LGWIN
    if not enc.BrotliEncoderAttachPreparedDictionary(s, d):
        sys.exit("dict-encode: the dictionary was not attached")
    inbuf = ctypes.create_string_buffer(data, len(data))
    avail_in = ctypes.c_size_t(len(data))
    next_in = ctypes.c_void_p(ctypes.addressof(inbuf))
    out = bytearray()
    chunk = ctypes.create_string_buffer(1 << 16)
    while True:
        avail_out = ctypes.c_size_t(len(chunk))
        next_out = ctypes.c_void_p(ctypes.addressof(chunk))
        if not enc.BrotliEncoderCompressStream(s, 2, ctypes.byref(avail_in), ctypes.byref(next_in), ctypes.byref(avail_out),
                                               ctypes.byref(next_out), None):      # BROTLI_OPERATION_FINISH
            sys.exit("dict-encode: brotli failed")
        out += chunk.raw[:len(chunk) - avail_out.value]
        if enc.BrotliEncoderIsFinished(s):
            break
    enc.BrotliEncoderDestroyInstance(s)
    enc.BrotliEncoderDestroyPreparedDictionary(d)
    return bytes(out)


def zstd(data, dictionary, level, lgwin):
    z = lib(["libzstd.so.1", ctypes.util.find_library("zstd") or "libzstd.so"])
    z.ZSTD_createCCtx.restype = ctypes.c_void_p
    z.ZSTD_CCtx_setParameter.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    z.ZSTD_CCtx_setParameter.restype = ctypes.c_size_t
    z.ZSTD_CCtx_refPrefix.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    z.ZSTD_CCtx_refPrefix.restype = ctypes.c_size_t
    z.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
    z.ZSTD_compressBound.restype = ctypes.c_size_t
    z.ZSTD_compress2.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    z.ZSTD_compress2.restype = ctypes.c_size_t
    z.ZSTD_isError.argtypes = [ctypes.c_size_t]
    z.ZSTD_freeCCtx.argtypes = [ctypes.c_void_p]
    c = z.ZSTD_createCCtx()
    z.ZSTD_CCtx_setParameter(c, 100, level)           # ZSTD_c_compressionLevel
    z.ZSTD_CCtx_setParameter(c, 201, 1)               # ZSTD_c_checksumFlag
    if lgwin:
        z.ZSTD_CCtx_setParameter(c, 101, lgwin)       # ZSTD_c_windowLog
    if z.ZSTD_isError(z.ZSTD_CCtx_refPrefix(c, dictionary, len(dictionary))):
        sys.exit("dict-encode: the prefix was not referenced")
    cap = z.ZSTD_compressBound(len(data))
    out = ctypes.create_string_buffer(cap)
    n = z.ZSTD_compress2(c, out, cap, data, len(data))
    if z.ZSTD_isError(n):
        sys.exit("dict-encode: zstd failed")
    z.ZSTD_freeCCtx(c)
    return out.raw[:n]


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("br", "zstd"):
        sys.exit(__doc__)
    kind, dictionary = sys.argv[1], open(sys.argv[2], "rb").read()
    q = int(sys.argv[3]) if len(sys.argv) > 3 else (11 if kind == "br" else 19)
    w = int(sys.argv[4]) if len(sys.argv) > 4 else (22 if kind == "br" else 0)
    data = sys.stdin.buffer.read()
    sys.stdout.buffer.write(brotli(data, dictionary, q, w) if kind == "br" else zstd(data, dictionary, q, w))


main()
