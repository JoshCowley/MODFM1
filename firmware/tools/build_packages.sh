#!/bin/sh
# Builds the two FM-1 packages from this repo, on a Mac (or Linux) with internet access:
#   fm1bench.fwsc  the benchmark (install this first)
#   fm1rack.fwsc   FM1 Rack Lite, first stage (only after the benchmark has run and said FPU OK)
#
#   sh firmware/tools/build_packages.sh     builds both, checks them on the computer, copies them to firmware/packages/
#   WORK=~/somewhere sh firmware/tools/build_packages.sh   (default work folder: ~/fm1-work)
#
# Needs: git, python3, curl, a C/C++ compiler (Xcode command line tools) and, on a Mac or ARM Linux, Docker
# (fm1-base runs JieLi's Linux x86-64 compiler in a linux/amd64 container; on x86-64 Linux it runs natively). It installs nothing on the FM-1: installing is a
# separate, manual step (TASKS.md). It downloads fm1-base and the Mutable code at the commits this was tested with,
# JieLi's toolchain (~/.jieli) and three files of JieLi's AC79 SDK, which fm1-base's get_sdk.sh checks by hash.
set -eu
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$HOME/fm1-work}
FM1BASE_REV=2d8ed748cdade582c65d9fc8d8cf17da4ddadec8    # fm1-base, as built and checked
EURORACK_REV=f4cfa7697095a28d292bff758beb51deb49b5a86   # pichenettes/eurorack

say() { printf '\n== %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "build_packages: $1 is missing ($2)"; exit 1; }; }
need git "Xcode command line tools: xcode-select --install"
need python3 "python.org or Homebrew"
need curl "comes with macOS"
need c++ "Xcode command line tools: xcode-select --install"
if ! { [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; }; then
  need docker "Docker Desktop"
  docker info >/dev/null 2>&1 || { echo "build_packages: Docker isn't running: open Docker Desktop, wait for it, try again"; exit 1; }
fi

# fm1-base's get_sdk.sh checks hashes with sha256sum, which macOS doesn't have: shasum does the same
if ! command -v sha256sum >/dev/null 2>&1; then
  SHIM=$(mktemp -d)
  printf '#!/bin/sh\nexec shasum -a 256 "$@"\n' > "$SHIM/sha256sum"
  chmod +x "$SHIM/sha256sum"
  PATH="$SHIM:$PATH"
fi

mkdir -p "$WORK"
cd "$WORK"
say "fm1-base at ${FM1BASE_REV%${FM1BASE_REV#???????}}"
[ -d fm1-base/.git ] || git clone -q https://github.com/charlesvestal/fm1-base
(cd fm1-base && git fetch -q origin && git checkout -q "$FM1BASE_REV" && git submodule update -q --init --recursive)
say "the Mutable code at ${EURORACK_REV%${EURORACK_REV#???????}}"
[ -d eurorack/.git ] || git clone -q https://github.com/pichenettes/eurorack
(cd eurorack && git fetch -q origin && git checkout -q "$EURORACK_REV" && git submodule update -q --init stmlib)
say "MODFM1 from this repo"
rm -rf MODFM1
mkdir MODFM1
(cd "$REPO" && git ls-files -z 2>/dev/null || find . -type f -not -path './.git/*' -print0) | (cd "$REPO" && xargs -0 tar cf -) | (cd MODFM1 && tar xf -)

say "JieLi's toolchain"
[ -e "$HOME/.jieli/toolchain" ] || sh fm1-base/tools/get_toolchain.sh
say "the AC79 SDK files (hash-checked)"
# JieLi's GitHub copy of the AC79 SDK is no longer public (Nov 2026); the same revision is on JieLi's own Gitee.
# get_sdk.sh still checks every file against fm1-base's reference hashes.
SDK_URL=${SDK_URL:-https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK} sh fm1-base/bsp/tools/get_sdk.sh "$HOME/.jieli/ac79-sdk"
export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain" AC79_SDK="$HOME/.jieli/ac79-sdk"

say "computer checks (the engine as the FM-1 runs it)"
(cd MODFM1/firmware/fm1/bench && EURORACK="$WORK/eurorack" sh build_host.sh >/dev/null && build/host/bench_host | tail -2)
(cd MODFM1/firmware/fm1/rack && EURORACK="$WORK/eurorack" sh build_host.sh >/dev/null && build/host/rack_host | tail -1)

LOG="$WORK/build.log"
: > "$LOG"
say "the benchmark"
(cd fm1-base && python3 tools/build.py ../MODFM1/firmware/fm1/bench) >> "$LOG" 2>&1 || { tail -20 "$LOG"; exit 1; }
say "FM1 Rack Lite"
(cd fm1-base && python3 tools/build.py ../MODFM1/firmware/fm1/rack) >> "$LOG" 2>&1 || { tail -20 "$LOG"; exit 1; }
grep -E '^  (ok|FAIL)|^package' "$LOG" || true
if grep -q 'warning: SDK' "$LOG"; then
  echo "build_packages: the SDK files are not the ones fm1-base was tested with (see $LOG): no packages copied. Don't install these."
  exit 1
fi

OUTP="$REPO/firmware/packages"
mkdir -p "$OUTP"
for f in MODFM1/firmware/fm1/bench/build/fm1bench.fwsc MODFM1/firmware/fm1/rack/build/fm1rack.fwsc; do
  [ -f "$f" ] || { echo "build_packages: $f wasn't built: see the lines above"; exit 1; }
  cp "$f" "$OUTP/"
done
( cd "$OUTP" && sha256sum *.fwsc > SHA256SUMS )
say "done: $OUTP"
cat "$OUTP/SHA256SUMS"
