#!/bin/bash
# CAS Filesystem Benchmark Script
# Tests read/write performance at various file sizes

MOUNT_POINT="/tmp/cas_mount"
BENCHMARK_DIR="$MOUNT_POINT/benchmark"
RESULTS_DIR="/tmp/cas_benchmark_results"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    rm -rf "$BENCHMARK_DIR"/*
}

# Create results directory
mkdir -p "$RESULTS_DIR"

# Data files for xmgrace
WRITE_DATA="$RESULTS_DIR/write_throughput.dat"
READ_DATA="$RESULTS_DIR/read_throughput.dat"
LATENCY_DATA="$RESULTS_DIR/latency.dat"

# Initialize data files with headers
echo "# File Size (KB)    Throughput (MB/s)" > "$WRITE_DATA"
echo "# File Size (KB)    Throughput (MB/s)" > "$READ_DATA"
echo "# Operation    Latency (ms)" > "$LATENCY_DATA"

# Test sizes in KB
SIZES=(1 10 100 1024 10240)

echo "=== CAS Filesystem Benchmark ==="
echo "Mount point: $MOUNT_POINT"
echo "Results directory: $RESULTS_DIR"
echo ""

# Check if filesystem is mounted
if ! mount | grep -q "$MOUNT_POINT"; then
    echo "ERROR: Filesystem not mounted at $MOUNT_POINT"
    echo "Please mount it first:"
    echo "  /home/flkrm/CAS-Engine/build/cas_fs /tmp/cas_storage $MOUNT_POINT"
    exit 1
fi

mkdir -p "$BENCHMARK_DIR"

# Benchmark 1: Sequential Write Throughput
echo "--- Benchmark 1: Sequential Write Throughput ---"
for size_kb in "${SIZES[@]}"; do
    size_bytes=$((size_kb * 1024))
    file="$BENCHMARK_DIR/write_${size_kb}k.bin"
    
    # Run 5 iterations and average
    total_time=0
    for i in {1..5}; do
        rm -f "$file"
        start=$(date +%s.%N)
        dd if=/dev/urandom of="$file" bs=1K count=$size_kb 2>/dev/null
        end=$(date +%s.%N)
        elapsed=$(echo "$end - $start" | bc)
        total_time=$(echo "$total_time + $elapsed" | bc)
    done
    
    avg_time=$(echo "scale=6; $total_time / 5" | bc)
    throughput=$(echo "scale=2; ($size_kb / 1024) / $avg_time" | bc)
    
    echo "  ${size_kb}KB: ${throughput} MB/s (avg ${avg_time}s)"
    echo "$size_kb $throughput" >> "$WRITE_DATA"
done

# Benchmark 2: Sequential Read Throughput
echo ""
echo "--- Benchmark 2: Sequential Read Throughput ---"
for size_kb in "${SIZES[@]}"; do
    size_bytes=$((size_kb * 1024))
    file="$BENCHMARK_DIR/read_${size_kb}k.bin"
    
    # Create test file first
    dd if=/dev/urandom of="$file" bs=1K count=$size_kb 2>/dev/null
    
    # Run 5 iterations and average
    total_time=0
    for i in {1..5}; do
        start=$(date +%s.%N)
        dd if="$file" of=/dev/null bs=1K 2>/dev/null
        end=$(date +%s.%N)
        elapsed=$(echo "$end - $start" | bc)
        total_time=$(echo "$total_time + $elapsed" | bc)
    done
    
    avg_time=$(echo "scale=6; $total_time / 5" | bc)
    throughput=$(echo "scale=2; ($size_kb / 1024) / $avg_time" | bc)
    
    echo "  ${size_kb}KB: ${throughput} MB/s (avg ${avg_time}s)"
    echo "$size_kb $throughput" >> "$READ_DATA"
done

# Benchmark 3: Latency Tests
echo ""
echo "--- Benchmark 3: Operation Latency ---"

# Small file write latency
start=$(date +%s.%N)
for i in {1..100}; do
    dd if=/dev/urandom of="$BENCHMARK_DIR/latency_small.bin" bs=1K count=1 2>/dev/null
done
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
avg_latency=$(echo "scale=3; ($elapsed / 100) * 1000" | bc)
echo "  Small file write (1KB): ${avg_latency} ms avg"
echo "Write_1KB $avg_latency" >> "$LATENCY_DATA"

# Small file read latency
start=$(date +%s.%N)
for i in {1..100}; do
    dd if="$BENCHMARK_DIR/latency_small.bin" of=/dev/null bs=1K 2>/dev/null
done
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
avg_latency=$(echo "scale=3; ($elapsed / 100) * 1000" | bc)
echo "  Small file read (1KB): ${avg_latency} ms avg"
echo "Read_1KB $avg_latency" >> "$LATENCY_DATA"

# Directory creation latency
start=$(date +%s.%N)
for i in {1..100}; do
    mkdir -p "$BENCHMARK_DIR/dir_$i" 2>/dev/null
done
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
avg_latency=$(echo "scale=3; ($elapsed / 100) * 1000" | bc)
echo "  Directory creation: ${avg_latency} ms avg"
echo "Mkdir $avg_latency" >> "$LATENCY_DATA"

# File listing latency
start=$(date +%s.%N)
for i in {1..100}; do
    ls -la "$BENCHMARK_DIR" > /dev/null 2>&1
done
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
avg_latency=$(echo "scale=3; ($elapsed / 100) * 1000" | bc)
echo "  Directory listing: ${avg_latency} ms avg"
echo "Ls $avg_latency" >> "$LATENCY_DATA"

# Benchmark 4: Random Read Performance
echo ""
echo "--- Benchmark 4: Random Read Performance ---"
LARGE_FILE="$BENCHMARK_DIR/random_test.bin"
dd if=/dev/urandom of="$LARGE_FILE" bs=1M count=10 2>/dev/null

start=$(date +%s.%N)
for i in {1..100}; do
    offset=$((RANDOM % 9000))
    dd if="$LARGE_FILE" of=/dev/null bs=1K skip=$offset count=100 2>/dev/null
done
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
avg_latency=$(echo "scale=3; ($elapsed / 100) * 1000" | bc)
echo "  Random 100KB reads: ${avg_latency} ms avg"
echo "Random_Read $avg_latency" >> "$LATENCY_DATA"

echo ""
echo "=== Benchmark Complete ==="
echo "Results saved to: $RESULTS_DIR"
echo ""
echo "Data files:"
echo "  $WRITE_DATA"
echo "  $READ_DATA"
echo "  $LATENCY_DATA"
