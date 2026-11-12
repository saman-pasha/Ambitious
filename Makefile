# Ambitious -- build and gates. Every target sources toolchain/env.sh, which says
# where the toolchain is (AMB_DEPS, default ~/Projects/GitHub; see toolchain/).
#
#   make                the modules and the ambitious binary, in build/
#   make check          the toolchain against toolchain/pins
#   make test           every gate, in order, stopping at the first RED
#   make golden         write tests/golden/ from the current generator and renderer (review the diff)
#   make schema doc     the reasoning schemas and the contracts' references, in build/
#
# Every C unit is built by cicilang in memory (--no-kb: hermetic, and the user's
# ~/.cicilang/KB is left alone) under a time cap, for this machine named
# (--target: the arch's generic CPU, not the host's every feature), with the
# include directory by its absolute path (a relative -I loses a macro file's
# NAME.pl.h). toolchain/env.sh sets AMB_CICILANG_FLAGS; toolchain/REPORT.md says why.
SHELL := /bin/sh
AMB_ROOT := $(CURDIR)
export AMB_ROOT
ENV := . $(AMB_ROOT)/toolchain/env.sh &&
CAP := timeout 300
AMB_CC := $(ENV) $(CAP) "$$AMB_CICILANG" $$AMB_CICILANG_FLAGS
MODULES := $(basename $(notdir $(wildcard contracts/*.pl)))
GEN := include/amb.pl include/amb.pl.h prolog/amb/gen_c.pl prolog/amb/contract.pl prolog/amb/gen_schema.pl

# the units of the engine: the runtime, then each module's own; the tests link
# these, and the command adds the shell's own
UNITS := rt/amb_rt rt/amb_proc dom/dom html/tokenizer html/tree html/encoding css/css layout/layout paint/font paint/brotli paint/image paint/webp paint/paint net/url net/zstd net/hpack net/h2 net/net a11y/a11y \
         js/vm js/compile js/builtins js/regexp js/normalize js/proxy js/typed js/bigint js/intl js/intl_data js/module js/gc script/script script/view script/fetch script/input script/observe \
         script/custom script/window
OBJS := $(UNITS:%=build/obj/%.o)
# the system libraries the units link with: OpenSSL's, for https: (src/net/net.c);
# the tests' links take them after the units
AMB_LIBS := -lssl -lcrypto
export AMB_OBJS := $(OBJS:%=$(AMB_ROOT)/%) $(AMB_LIBS)

.PHONY: all check test test-contract test-gen test-exec test-js test-safe test-reason test-html test-integ golden schema doc clean bench
all: build/ambitious schema

build/obj/%.o: src/%.c $(GEN) $(wildcard contracts/*.pl) $(wildcard src/*/*.h)
	@mkdir -p $(@D)
	$(AMB_CC) -c $< -o $@ < /dev/null

build/ambitious: build/obj/shell/main.o $(OBJS)
	$(AMB_CC) $^ $(AMB_LIBS) -o $@ < /dev/null

check:
	sh toolchain/check.sh

test: test-contract test-gen test-exec test-js test-safe test-reason test-html test-integ

test-contract:
	@$(ENV) $(CAP) "$$AMB_COCOLOG" -s tests/contract/run.pl < /dev/null

test-gen:
	@sh tests/run.sh gen

test-exec: $(OBJS)
	@sh tests/run.sh exec

test-js: build/ambitious
	@sh tests/run.sh js

test-safe:
	@sh tests/run.sh safe

test-reason: schema
	@sh tests/run.sh reason

test-integ: all
	@sh tests/run.sh integ

golden: build/ambitious
	@mkdir -p tests/golden/render
	@for m in $(MODULES); do sh tools/amb ctext $$m > tests/golden/$$m.ctext && sh tools/amb schema $$m > tests/golden/$$m.schema && echo "wrote tests/golden/$$m.ctext and $$m.schema"; done
	@for p in $(basename $(notdir $(wildcard tests/pages/*.html))); do sh bin/ambitious render tests/pages/$$p.html tests/golden/render/$$p.png > /dev/null && echo "wrote tests/golden/render/$$p.png"; done

schema: $(MODULES:%=build/schema/%.pl)
doc: $(MODULES:%=build/doc/%.md)

build/schema/%.pl: contracts/%.pl prolog/amb/contract.pl prolog/amb/gen_schema.pl
	@mkdir -p $(@D)
	sh tools/amb schema $* > $@.tmp && mv $@.tmp $@

build/doc/%.md: contracts/%.pl prolog/amb/contract.pl prolog/amb/gen_schema.pl
	@mkdir -p $(@D)
	sh tools/amb doc $* > $@.tmp && mv $@.tmp $@

clean:
	rm -rf build

test-html: $(OBJS)
	@sh tests/run.sh html

# the performance gate (PLAN.md §11): Ambitious against Chromium, page by page, into docs/benchmark.md's tables;
# axe-core is fetched into build/bench the first time. RUNS=5 by default.
bench: all
	@mkdir -p build/bench
	@test -f build/bench/node_modules/axe-core/axe.min.js || (cd build/bench && npm install --no-save --no-audit --no-fund axe-core@4.14.0 > /dev/null)
	@python3 tools/bench/compare.py $${RUNS:-5} > build/bench/report.md && cat build/bench/report.md
