# Ambitious -- build and gates. Every target sources toolchain/env.sh, which says
# where the toolchain is (AMB_DEPS, default ~/Projects/GitHub; see toolchain/).
#
#   make                the modules and the ambitious binary, in build/
#   make check          the toolchain against toolchain/pins
#   make test           every gate, in order, stopping at the first RED
#   make golden         write tests/golden/ from the current generator (review the diff)
#   make schema doc     the reasoning schemas and the contracts' references, in build/
#
# Every C unit is built by cicilang in memory (--no-kb: hermetic, and the user's
# ~/.cicilang/KB is left alone) under a time cap, with the include directory by
# its absolute path (a relative -I loses a macro file's NAME.pl.h: toolchain/REPORT.md).
SHELL := /bin/sh
AMB_ROOT := $(CURDIR)
export AMB_ROOT
ENV := . $(AMB_ROOT)/toolchain/env.sh &&
CAP := timeout 300
AMB_CC := $(ENV) $(CAP) "$$AMB_CICILANG" --no-kb -I $(AMB_ROOT)/include
MODULES := $(basename $(notdir $(wildcard contracts/*.pl)))
GEN := include/amb.pl include/amb.pl.h prolog/amb/gen_c.pl prolog/amb/contract.pl prolog/amb/gen_schema.pl

# the units of the engine: the runtime, then each module's own; the tests link
# these, and the command adds the shell's own
UNITS := rt/amb_rt rt/amb_proc dom/dom html/tokenizer html/tree
OBJS := $(UNITS:%=build/obj/%.o)
export AMB_OBJS := $(OBJS:%=$(AMB_ROOT)/%)

.PHONY: all check test test-contract test-gen test-exec test-safe test-reason test-html test-integ golden schema doc clean
all: build/ambitious schema

build/obj/%.o: src/%.c $(GEN) $(wildcard contracts/*.pl)
	@mkdir -p $(@D)
	$(AMB_CC) -c $< -o $@ < /dev/null

build/ambitious: build/obj/shell/main.o $(OBJS)
	$(AMB_CC) $^ -o $@ < /dev/null

check:
	sh toolchain/check.sh

test: test-contract test-gen test-exec test-safe test-reason test-html test-integ

test-contract:
	@$(ENV) $(CAP) "$$AMB_COCOLOG" -s tests/contract/run.pl < /dev/null

test-gen:
	@sh tests/run.sh gen

test-exec: $(OBJS)
	@sh tests/run.sh exec

test-safe:
	@sh tests/run.sh safe

test-reason: schema
	@sh tests/run.sh reason

test-integ: all
	@sh tests/run.sh integ

golden:
	@mkdir -p tests/golden
	@for m in $(MODULES); do sh tools/amb ctext $$m > tests/golden/$$m.ctext && sh tools/amb schema $$m > tests/golden/$$m.schema && echo "wrote tests/golden/$$m.ctext and $$m.schema"; done

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
