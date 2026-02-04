#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../.. && pwd)"

cleanup() {
  rm -f out.c out.h out__runtime.c out.lowered.vx out.analysis.txt
  rm -rf megalinker
}
trap cleanup EXIT

if ! make -s -C "$ROOT" frontend >/tmp/frontend_build.out 2>/tmp/frontend_build.err; then
  cat /tmp/frontend_build.out /tmp/frontend_build.err
  exit 1
fi

if ! make -s -C "$ROOT/ext/vexel_megalinker_backend" >/tmp/megalinker_build.out 2>/tmp/megalinker_build.err; then
  cat /tmp/megalinker_build.out /tmp/megalinker_build.err
  exit 1
fi

if ! "$ROOT/build/vexel-megalinker" --backend-opt caller_limit=1 -o out input.vx \
  >/tmp/megalinker_compile.out 2>/tmp/megalinker_compile.err; then
  cat /tmp/megalinker_compile.out /tmp/megalinker_compile.err
  exit 1
fi

if [[ ! -f out__runtime.c ]]; then
  echo "missing runtime output"
  exit 1
fi
if [[ ! -d megalinker ]]; then
  echo "missing megalinker output dir"
  exit 1
fi
if [[ ! -f megalinker/rom_vx_G1.c || ! -f megalinker/rom_vx_G2.c ]]; then
  echo "missing rom globals"
  exit 1
fi
if [[ ! -f megalinker/ram_globals.c ]]; then
  echo "missing ram globals"
  exit 1
fi

if ! rg -q "__tramp" out__runtime.c; then
  echo "missing trampoline"
  exit 1
fi
if ! rg --no-ignore -q "__tramp" megalinker; then
  echo "trampoline not used in call sites"
  exit 1
fi

reader_file="$(rg --no-ignore -l "reader" megalinker 2>/dev/null | head -n 1 || true)"
if [[ -z "$reader_file" ]]; then
  echo "reader function not found"
  exit 1
fi
if ! rg -U "vx_load_module_id_[ab]\\([^\\n]*\\);\\n\\s*return" "$reader_file" >/dev/null; then
  echo "missing restore before return"
  exit 1
fi

if rg -q "if \\(!?seg\\)" out__runtime.c; then
  echo "unexpected branch in load helper"
  exit 1
fi

echo "ok"
