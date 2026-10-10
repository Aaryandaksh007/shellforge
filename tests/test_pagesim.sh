#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "Building ShellForge..."
make -s

run_shell() {
    printf '%s\nexit\n' "$1" | ./shellforge 2>&1
}

assert_contains() {
    local label="$1"
    local output="$2"
    local expected="$3"

    if grep -Fq -- "$expected" <<< "$output"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        echo "Expected: $expected"
        printf '%s\n' "$output"
        exit 1
    fi
}

assert_row() {
    local label="$1"
    local output="$2"
    local pattern="$3"

    if grep -Eq "$pattern" <<< "$output"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        printf '%s\n' "$output"
        exit 1
    fi
}

if [[ ! -f traces/basic.txt ]]; then
    echo "FAIL: traces/basic.txt not found"
    exit 1
fi

echo
echo "=== Basic policy regression tests ==="

for test_case in \
    "fifo 2 9" \
    "lru 2 9" \
    "lfu 3 8" \
    "clock 2 9" \
    "hybrid 3 8" \
    "optimal 4 7"
do
    read -r algo expected_hits expected_faults <<< "$test_case"

    output=$(run_shell \
        "pagesim --frames 3 --algo $algo --trace traces/basic.txt")

    assert_contains "$algo hits" "$output" \
        "Page Hits   : $expected_hits"

    assert_contains "$algo faults" "$output" \
        "Page Faults : $expected_faults"
done

echo

echo
echo "=== AWSF baseline test ==="

output=$(run_shell \
    "pagesim --frames 3 --algo awsf --trace traces/basic.txt")

assert_contains "AWSF basic hits" "$output" \
    "Page Hits   : 3"

assert_contains "AWSF basic faults" "$output" \
    "Page Faults : 8"

echo
echo "=== AWSF workload comparisons ==="

for trace in traces/loop.txt traces/frequency.txt; do
    if [[ ! -f "$trace" ]]; then
        echo "FAIL: missing workload $trace"
        exit 1
    fi

    output=$(run_shell \
        "pagesim --frames 3 --algo compare --trace $trace")

    assert_row "$trace includes AWSF" "$output" \
        'AWSF[[:space:]]+[0-9]+[[:space:]]+[0-9]+'

    assert_row "$trace includes Optimal" "$output" \
        'Optimal[[:space:]]+[0-9]+[[:space:]]+[0-9]+'
done

echo "=== Document Benchmark A ==="

output=$(run_shell \
    "pagesim --frames 3 --algo compare --ref 3 0 0 3 4 2 2 1 0")

assert_row "Benchmark A FIFO: 3 hits, 6 faults" "$output" \
    'FIFO[[:space:]]+3[[:space:]]+6'

assert_row "Benchmark A Hybrid: 4 hits, 5 faults" "$output" \
    'Hybrid[[:space:]]+4[[:space:]]+5'

assert_row "Benchmark A Optimal: 4 hits, 5 faults" "$output" \
    'Optimal[[:space:]]+4[[:space:]]+5'

echo
echo "=== Document Benchmark B ==="

output=$(run_shell \
    "pagesim --frames 3 --algo compare --ref 1 2 3 4 1 2 5 1 2 3 4 5")

assert_row "Benchmark B FIFO: 9 faults" "$output" \
    'FIFO[[:space:]]+3[[:space:]]+9'

assert_row "Benchmark B LRU: 10 faults" "$output" \
    'LRU[[:space:]]+2[[:space:]]+10'

assert_row "Benchmark B LFU: 10 faults" "$output" \
    'LFU[[:space:]]+2[[:space:]]+10'

assert_row "Benchmark B Clock: 9 faults observed" "$output" \
    'Clock[[:space:]]+3[[:space:]]+9'

assert_row "Benchmark B Hybrid: 10 faults" "$output" \
    'Hybrid[[:space:]]+2[[:space:]]+10'

assert_row "Benchmark B Optimal: 7 faults" "$output" \
    'Optimal[[:space:]]+5[[:space:]]+7'

echo
echo "=== Document Benchmark C ==="

output=$(run_shell \
    "pagesim --frames 4 --algo anomaly --ref 1 2 3 4 1 2 5 1 2 3 4 5")

assert_contains "Bélády anomaly detected" "$output" \
    "Belady anomaly: faults increased at 4 frames."

assert_row "Three frames produce 9 faults" "$output" \
    '^3[[:space:]]+9[[:space:]]+3'

assert_row "Four frames produce 10 faults" "$output" \
    '^4[[:space:]]+10[[:space:]]+2'

echo
echo "=== Working-set tests ==="

output=$(run_shell \
    "pagesim --algo working-set --window 3 --trace traces/basic.txt")

assert_contains "Working-set average" "$output" \
    "Average working-set size: 2.73"

assert_contains "Working-set maximum" "$output" \
    "Maximum working-set size: 3"

echo
echo "=== Trace-file error handling ==="

output=$(run_shell \
    "pagesim --frames 3 --algo fifo --trace /tmp/pagesim_missing_test_987321.txt")

assert_contains "Missing trace file handled" "$output" \
    "cannot open trace file"

echo
echo "========================================"
echo "All PageSim regression tests passed."
echo "========================================"
