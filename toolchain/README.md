# toolchain/

What Ambitious is built with, and how to get it.

| file | what it is |
|---|---|
| `pins` | the commits and versions of Cicili, ZiguratIP, cocolog and cicilang that Ambitious was verified on |
| `bootstrap.sh` | from nothing to a working toolchain on Debian or Ubuntu: packages, the four checkouts at their pins, their builds |
| `env.sh` | sourced by the Makefile and every script: where the checkouts are, and the variables their tools read |
| `check.sh` | holds a box against `pins`, then smoke-tests what Ambitious uses |
| `REPORT.md` | the Phase 0 compatibility report: what was built, which gates ran, the findings |
| `lisp/str/` | a shim of the ten functions of cl-str that Cicili calls, so Cicili builds from distribution packages alone |

```sh
sh toolchain/bootstrap.sh                       # ~/Projects/GitHub/{cicili,ZiguratIP,cocolog,cicilang}
AMB_DEPS=/elsewhere sh toolchain/bootstrap.sh   # the checkouts somewhere else
sh toolchain/check.sh                           # GREEN or RED, a line a point
```

The checkouts are the owner's repositories, used and never edited. A
limitation met in one of them is worked around here and written into
`REPORT.md` as a finding for that repository's owner.
