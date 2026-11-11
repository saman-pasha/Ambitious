# toolchain/env.sh -- sourced (never run) by the Makefile and every script of
# Ambitious: where the four neighbours are, and the variables their tools read.
#
#   AMB_DEPS   the directory holding the checkouts (default ~/Projects/GitHub,
#              the layout cicilang's own scripts assume)
#   CICILI COCOLOG ZIGURATIP CICILANG   each checkout, if not under AMB_DEPS
#
# Nothing here builds anything; toolchain/bootstrap.sh does that. The caller
# sets AMB_ROOT, the Ambitious checkout, before sourcing: a sourced file has
# no $0 of its own.
: "${AMB_ROOT:?set AMB_ROOT to the Ambitious checkout before sourcing toolchain/env.sh}"
AMB_DEPS=${AMB_DEPS:-$HOME/Projects/GitHub}

amb_dep() {   # $1 the name; the checkout under AMB_DEPS, either spelling of ZiguratIP
  for d in "$AMB_DEPS/$1" "$AMB_DEPS/$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"; do
    [ -d "$d" ] && { printf '%s\n' "$d"; return 0; }
  done
  printf '%s\n' "$AMB_DEPS/$1"
}
CICILI=${CICILI:-$(amb_dep cicili)}
COCOLOG=${COCOLOG:-$(amb_dep cocolog)}
ZIGURATIP=${ZIGURATIP:-$(amb_dep ZiguratIP)}
CICILANG=${CICILANG:-$(amb_dep cicilang)}
ZIGURATIP_HOME=${ZIGURATIP_HOME:-$ZIGURATIP/home}
# html5lib's tree-construction tests (the html gate; toolchain/bootstrap.sh checks them out)
AMB_HTML5LIB=${AMB_HTML5LIB:-$(amb_dep html5lib-tests)/tree-construction}

# the commands
AMB_COCOLOG="$COCOLOG/cocolog"
AMB_CICILANG="$CICILANG/bin/cicilang"
# the machine cicilang builds for: this one, named. Without --target cicilang
# compiles for the host CPU and every feature it has (LLVMGetHostCPUName): an
# AVX-512 instruction then stops valgrind (toolchain/REPORT.md, finding 9), and a
# binary built on one machine may not run on another. Named, the CPU is the
# generic one of the arch; the headers and the linker stay the host's.
amb_target() {
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) echo x86_64-linux-gnu ;;
    Linux-aarch64|Linux-arm64) echo aarch64-linux-gnu ;;
    Darwin-arm64) echo arm64-apple-darwin ;;
    Darwin-x86_64) echo x86_64-apple-darwin ;;
    *) echo "" ;;
  esac
}
AMB_TARGET=${AMB_TARGET-$(amb_target)}
AMB_CICILANG_FLAGS="--no-kb${AMB_TARGET:+ --target=$AMB_TARGET} -I $AMB_ROOT/include"

# cocolog finds Ambitious's Prolog (prolog/amb/...) and cicilang's library on
# this path; cicilang puts its own library first when it runs
COCOLOG_LIBRARY="$AMB_ROOT/prolog:$CICILANG/library"
# the ZiguratIP libraries the cocolog binary and its modules load
LD_LIBRARY_PATH="$ZIGURATIP_HOME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Cicili's build loads its system through ASDF: this checkout's, first
CL_SOURCE_REGISTRY="$CICILI/:${CL_SOURCE_REGISTRY:-}"

export AMB_ROOT AMB_DEPS CICILI COCOLOG ZIGURATIP CICILANG ZIGURATIP_HOME AMB_HTML5LIB \
       AMB_COCOLOG AMB_CICILANG AMB_TARGET AMB_CICILANG_FLAGS COCOLOG_LIBRARY LD_LIBRARY_PATH CL_SOURCE_REGISTRY
