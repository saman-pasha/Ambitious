#!/bin/sh
# tests/net/canon.sh -- a server's request log (tests/net/serve.py --log) on stdin, as
# the cookie reference compares it: each visit's requests sorted under its `-- N URL'
# line (a browser fetches a page's parts in an order of its own), a favicon's dropped
awk '/^-- / { close("sort"); print; fflush(); next } /favicon/ { next } { print | "sort" } END { close("sort") }'
