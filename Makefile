# Ambitious -- build and gates. Every target sources toolchain/env.sh, which says
# where the toolchain is (AMB_DEPS, default ~/Projects/GitHub; see toolchain/).
#
#   make check          the toolchain against toolchain/pins
#   make test           every gate, in order, stopping at the first RED
#   make schema doc     the reasoning schemas and the contracts' references, in build/
SHELL := /bin/sh
AMB_ROOT := $(CURDIR)
export AMB_ROOT
ENV := . $(AMB_ROOT)/toolchain/env.sh &&
CAP := timeout 300
MODULES := $(basename $(notdir $(wildcard contracts/*.pl)))

.PHONY: all check test test-contract schema doc clean
all: schema

check:
	sh toolchain/check.sh

test: test-contract

test-contract:
	@$(ENV) $(CAP) "$$AMB_COCOLOG" -s tests/contract/run.pl < /dev/null

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
