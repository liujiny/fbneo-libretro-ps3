#!/usr/bin/env bash
set -e

mkdir -p build-host-bench
INC="-Istandalone/gen -Istandalone/runtime"
CFLAGS="-O2 -fomit-frame-pointer -ffast-math"

echo "Compiling host benchmark in parallel on $(nproc) cores..."

compile_tu() {
    local src="$1"
    local obj="$2"
    gcc $CFLAGS $INC -c "$src" -o "$obj"
}
export -f compile_tu

PIDS=()
for i in {0..15}; do
    compile_tu "standalone/gen/sh3_code_${i}.c" "build-host-bench/sh3_code_${i}.o" &
    PIDS+=($!)
done

compile_tu "standalone/gen/sh3_dispatch_table.c" "build-host-bench/sh3_dispatch_table.o" &
PIDS+=($!)

compile_tu "standalone/runtime/cv1k_runtime.c" "build-host-bench/cv1k_runtime.o" &
PIDS+=($!)

compile_tu "standalone/runtime/sh3_interpret.c" "build-host-bench/sh3_interpret.o" &
PIDS+=($!)

compile_tu "standalone/benchmark_host.c" "build-host-bench/benchmark_host.o" &
PIDS+=($!)

for pid in "${PIDS[@]}"; do
    wait "$pid"
done

echo "Linking bin_bench_sh3..."
gcc build-host-bench/*.o -o bin_bench_sh3
echo "Done! bin_bench_sh3 is ready."
