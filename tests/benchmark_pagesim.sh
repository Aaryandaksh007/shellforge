#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make -s

output_file="benchmark_results.csv"
traces=(
    "traces/basic.txt"
    "traces/loop.txt"
    "traces/frequency.txt"
    "traces/phase-shift.txt"
)
frame_counts=(2 3 4)

printf 'trace,frames,algorithm,hits,faults,hit_ratio_percent\n' \
    > "$output_file"

for trace in "${traces[@]}"; do
    if [[ ! -f "$trace" ]]; then
        echo "FAIL: missing trace: $trace"
        exit 1
    fi

    for frames in "${frame_counts[@]}"; do
        command_text="pagesim --frames $frames --algo compare --trace $trace"

        result=$(printf '%s\nexit\n' "$command_text" |
            ./shellforge 2>&1)

        rows=$(printf '%s\n' "$result" | awk \
            -v trace="$trace" -v frames="$frames" '
            $1 ~ /^(FIFO|LRU|LFU|Clock|Hybrid|AWSF|Optimal)$/ &&
            $2 ~ /^[0-9]+$/ &&
            $3 ~ /^[0-9]+$/ {
                ratio = $4
                sub(/%$/, "", ratio)
                printf "%s,%s,%s,%s,%s,%s\n",
                    trace, frames, $1, $2, $3, ratio
            }
        ')

        row_count=$(printf '%s\n' "$rows" |
            awk 'NF { count++ } END { print count+0 }')

        if [[ "$row_count" -ne 7 ]]; then
            echo "FAIL: expected 7 algorithm rows for $trace, $frames frames"
            printf '%s\n' "$result"
            exit 1
        fi

        printf '%s\n' "$rows" >> "$output_file"
        echo "PASS: $trace with $frames frames"
    done
done

echo
echo "All 12 workload/frame configurations completed."
echo "CSV results saved to $output_file"
