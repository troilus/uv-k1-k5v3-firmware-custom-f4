#!/usr/bin/env bash
# Build the TDCS RX overlay-app blob (.app). Invoked by ../../../compile-app.sh
# inside the uvk1-uvk5v3 Docker image, run from this app's directory.
#
# The 4 KiB overlay is full: the decoder, the 18-field window and the Chinese
# labels leave a handful of bytes free. Anything added here has to be paid for.
set -euo pipefail

APP="$(basename "$PWD")"            # tdcsrx
APP_NAME="TDCS RX"                  # <-- the only per-app line
APP_VER="0.2"
APP_API_MIN=3
APP_VMA=${APP_VMA:-0x20000280}      # pinned overlay VMA (Core/pyf071xb.ld)
OUT="${APP_NAME// /}-cn"            # blob basename ("TDCS RX" -> TDCSRX-cn)

CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
command -v arm-none-eabi-gcc >/dev/null 2>&1 && { CC=arm-none-eabi-gcc; OBJCOPY=arm-none-eabi-objcopy; }
PY=python3
command -v python3 >/dev/null 2>&1 || PY=python

# GCC 13 supports -Oz; the 10.3 toolchain some hosts use does not, and -Os then
# lands 4 bytes inside the limit - keep the flag conditional.
OPT=-Os
echo 'int main(void){return 0;}' | "$CC" -mcpu=cortex-m0plus -mthumb -Oz -x c -c - -o /dev/null 2>/dev/null && OPT=-Oz

CFLAGS="-mcpu=cortex-m0plus -mthumb $OPT -fno-jump-tables -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-inline-functions-called-once -Wall -Wextra \
  -fexec-charset=GB2312 -I.."
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} \
  -Wl,--gc-sections -Wl,-Map=${APP}.map -Wl,--build-id=none"

rm -f ./*.app ./*.elf ./*.bin

step() { printf '\r  ?? %-13s [%d/3] %-8s' "$APP_NAME" "$1" "$2"; }

trap 'printf "\r  ? %-13s build failed            \n" "$APP_NAME"' ERR
step 1 compile ; "$CC" $CFLAGS $LDFLAGS "${APP}_app.c" -lgcc -o "${APP}.elf" \
                   || { "$CC" $CFLAGS -c "${APP}_app.c" -o "${APP}.o"
                        SIZE="${OBJCOPY%objcopy}size"; NM="${OBJCOPY%objcopy}nm"
                        echo; echo "--- ${APP}.o size ---"; "$SIZE" -A "${APP}.o" 2>/dev/null || true
                        echo "--- largest symbols ---"; "$NM" --print-size --size-sort --radix=d "${APP}.o" 2>/dev/null | tail -40 || true
                        false; }
step 2 objcopy ; "$OBJCOPY" -O binary "${APP}.elf" "${APP}.bin"
step 3 pack    ; "$PY" ../pack_app.py "${APP}.bin" "${OUT}.app" \
                   --name "$APP_NAME" --ver "$APP_VER" --api-min "$APP_API_MIN" --vma "${APP_VMA}" \
                   --shortcut none >/dev/null
trap - ERR

BYTES=$(wc -c < "${APP}.bin")
if [ "$BYTES" -gt 4096 ]; then
  printf '\r  ?? %-13s OVERFLOWS 4 KiB (%d B)     \n' "$APP_NAME" "$BYTES"
  exit 1
fi
printf '\r  ? %-13s %4d B  (%d%% of 4 KiB)  ->  %s.app   \n' \
       "$APP_NAME" "$BYTES" "$(( BYTES * 100 / 4096 ))" "$OUT"
