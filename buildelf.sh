#!/bin/sh
# Build beeper as loadable ELFs for the BreezyBox elf_loader, linked
# against the vendored breezy_tui sources (tuilib/*.c):
#
#   ESP32-S3 (Xtensa)  -> build/elf/beeper.xtensa.elf
#   ESP32-P4 (RISC-V)  -> build/elf/beeper.rv32.elf
#
#   Usage:
#     ./buildelf.sh          # both targets
#     ./buildelf.sh s3       # S3 only   (aliases: xtensa, esp32s3)
#     ./buildelf.sh p4       # P4 only   (aliases: rv32, riscv, esp32p4)
#
# Set LINK_LIBGCC=1 to statically link libgcc; off by default (symbols come
# from the firmware's elf_loader symbol table at load time).
set -e

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"

OUTDIR="$ROOT/build/elf"
mkdir -p "$OUTDIR"

TC_VER="esp-14.2.0_20241119"

DO_S3=0
DO_P4=0
case "${1:-all}" in
  s3|xtensa|esp32s3)     DO_S3=1 ;;
  p4|rv32|riscv|esp32p4) DO_P4=1 ;;
  all|"")                DO_S3=1; DO_P4=1 ;;
  *) echo "unknown target: $1" >&2; exit 1 ;;
esac

SRCS="$(ls tuilib/*.c src/*.c)"

build_s3() {
  TC="$HOME/.espressif/tools/xtensa-esp-elf/$TC_VER/xtensa-esp-elf/bin"
  GCC="$TC/xtensa-esp32s3-elf-gcc"
  STRIP="$TC/xtensa-esp32s3-elf-strip"
  LIBGCC=
  [ "$LINK_LIBGCC" = 1 ] && LIBGCC="$HOME/.espressif/tools/xtensa-esp-elf/$TC_VER/xtensa-esp-elf/lib/gcc/xtensa-esp-elf/14.2.0/esp32s3/libgcc.a"
  OUT="$OUTDIR/beeper.xtensa.elf"
  "$GCC" \
    -Ituilib -Ilocal_include -Isrc -O2 \
    -Dmain=app_main \
    -nostartfiles -nostdlib \
    -fPIC -shared \
    -fvisibility=hidden \
    -Wl,-e,app_main \
    -Wl,--gc-sections \
    $SRCS $LIBGCC -o "$OUT"
  "$STRIP" --strip-all --remove-section=.xt.prop "$OUT"
  echo "built $OUT"
}

build_p4() {
  TC="$HOME/.espressif/tools/riscv32-esp-elf/$TC_VER/riscv32-esp-elf/bin"
  GCC="$TC/riscv32-esp-elf-gcc"
  STRIP="$TC/riscv32-esp-elf-strip"
  # Match the P4 firmware ABI (rv32imafc, hard-float ilp32f).
  MARCH=rv32imafc_zicsr_zifencei
  MABI=ilp32f
  LIBGCC=
  [ "$LINK_LIBGCC" = 1 ] && LIBGCC=$("$GCC" -march=$MARCH -mabi=$MABI -print-libgcc-file-name)
  OUT="$OUTDIR/beeper.rv32.elf"
  "$GCC" \
    -march=$MARCH -mabi=$MABI \
    -Ituilib -Ilocal_include -Isrc -O2 \
    -Dmain=app_main \
    -nostartfiles -nostdlib \
    -fPIC -shared \
    -fvisibility=hidden \
    -Wl,-e,app_main \
    -Wl,--gc-sections \
    $SRCS $LIBGCC -o "$OUT"
  "$STRIP" --strip-all "$OUT"
  echo "built $OUT"
}

[ $DO_S3 = 1 ] && build_s3
[ $DO_P4 = 1 ] && build_p4
echo "done"
