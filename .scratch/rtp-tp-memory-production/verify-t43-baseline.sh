#!/usr/bin/env bash
set -euo pipefail

feature_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
manifest="$feature_root/.scratch/rtp-tp-memory-production/t43-baseline-manifest.json"
reference_root="$feature_root/$(jq -er '.reference_root' "$manifest")"
evidence_archive="$feature_root/$(jq -er '.evidence.archive' "$manifest")"

expected_revision=$(jq -er '.source_revision' "$manifest")
expected_cache=$(jq -er '.build.cmake_cache_sha256' "$manifest")
expected_binary=$(jq -er '.build.binary_sha256' "$manifest")
expected_library=$(jq -er '.build.unified_bus_library_sha256' "$manifest")
expected_archive=$(jq -er '.build.runtime_archive_sha256' "$manifest")
expected_evidence=$(jq -er '.evidence.archive_sha256' "$manifest")
expected_rss_limit=$(jq -er '.guards.rss_limit_bytes' "$manifest")
expected_wall_limit=$(jq -er '.guards.wall_limit_seconds' "$manifest")
expected_runlog=$(jq -er '.stable_hashes.p001_task_trace_tree_sha256' "$manifest")
expected_snapshot=$(jq -er '.stable_hashes.p001_storage_snapshot_canonical_sha256' "$manifest")
expected_mtp_snapshot=$(jq -er '.stable_hashes.p001_mtp16_storage_snapshot_raw_sha256' "$manifest")

check_hash() {
    local path=$1
    local expected=$2
    test "$(shasum -a 256 "$path" | awk '{print $1}')" = "$expected"
}

runtime_archive="$feature_root/.scratch/rtp-tp-memory-production/baseline/$(jq -er '.build.runtime_archive' "$manifest")"
check_hash "$runtime_archive" "$expected_archive"
check_hash "$evidence_archive" "$expected_evidence"

evidence_root=$(mktemp -d)
runtime_root=$(mktemp -d)
actual_inventory=$(mktemp)
trap 'rm -rf "$evidence_root" "$runtime_root"; rm -f "$actual_inventory"' EXIT
tar -xzf "$evidence_archive" -C "$evidence_root"
tar -xzf "$runtime_archive" -C "$runtime_root"
check_hash "$runtime_root/cmake-cache/CMakeCache.txt" "$expected_cache"
check_hash "$runtime_root/build/scratch/ns3.44-ub-quick-example" "$expected_binary"
check_hash "$runtime_root/build/lib/libns3.44-unified-bus.dylib" "$expected_library"
grep -qx 'CMAKE_BUILD_TYPE:STRING=release' "$runtime_root/cmake-cache/CMakeCache.txt"
grep -qx 'NS3_EXAMPLES:BOOL=OFF' "$runtime_root/cmake-cache/CMakeCache.txt"
grep -qx 'NS3_MPI:BOOL=OFF' "$runtime_root/cmake-cache/CMakeCache.txt"
grep -qx 'NS3_MTP:BOOL=ON' "$runtime_root/cmake-cache/CMakeCache.txt"
grep -qx 'NS3_TESTS:BOOL=OFF' "$runtime_root/cmake-cache/CMakeCache.txt"
check_hash "$evidence_root/p001-task-trace-sha256.txt" "$expected_runlog"
jq -e '.guards.serial_execution == true and .guards.largest_authorized_case == "p001"' \
    "$manifest" >/dev/null
(
    cd "$evidence_root"
    shasum -a 256 -c payload-sha256.txt >/dev/null
)

while IFS=$'\t' read -r name expected; do
    check_hash "$evidence_root/inputs/p001/$name" "$expected"
done < <(jq -r '.input_sha256 | to_entries[] | [.key, .value] | @tsv' "$manifest")

(
    cd "$evidence_root"
    find inputs runs task-trace-checksums -type f -print
    find . -maxdepth 1 -name p001-task-trace-sha256.txt -print
) > "$actual_inventory"
sort -o "$actual_inventory" "$actual_inventory"
cmp -s "$actual_inventory" "$evidence_root/artifact-inventory.txt"

for result in t43-aa2b26a-local-a t43-aa2b26a-local-b t43-aa2b26a-mtp16-a t43-aa2b26a-mtp16-b; do
    run_root="$evidence_root/runs/$result"
    run_manifest="$run_root/manifest.json"
    command="$run_root/command.json"
    snapshot="$run_root/runtime-storage-snapshot.csv"
    jq -e --arg revision "$expected_revision" --arg binary "$expected_binary" \
        --argjson rss "$expected_rss_limit" --argjson wall "$expected_wall_limit" \
        '.source_revision == $revision and .binary_sha256 == $binary and
         .return_code == 0 and .stop_reason == "completed" and .input_task_count == 1024 and
         .rss_limit_bytes == $rss and .wall_limit_seconds == $wall' \
        "$run_manifest" >/dev/null
    jq -e '
        type == "array" and
        index("--rng-run=1") != null and
        index("--dependency-visibility-delay=10ns") != null and
        index("--initial-task-start-offset-window=32ps") != null and
        index("--timing-offset-seed=1") != null and
        any(.[]; startswith("--case-path=")) and
        any(.[]; startswith("--runtime-storage-snapshot="))
    ' "$command" >/dev/null
    while IFS=$'\t' read -r name expected; do
        test "$(jq -er --arg name "$name" '.input_sha256[$name]' "$run_manifest")" = "$expected"
    done < <(jq -r '.input_sha256 | to_entries[] | [.key, .value] | @tsv' "$manifest")
    test "$(tr -d '\n' < "$run_root/exit-code.txt")" = 0
    digest=$(awk -F, 'NR == 1 || $2 != "trace_writer"' "$snapshot" | shasum -a 256 | awk '{print $1}')
    test "$digest" = "$expected_snapshot"
    trace_list="$evidence_root/task-trace-checksums/${result#t43-aa2b26a-}.sha256"
    test "$(wc -l < "$trace_list" | tr -d ' ')" = 1024
    check_hash "$trace_list" "$expected_runlog"
done

for result in t43-aa2b26a-mtp16-a t43-aa2b26a-mtp16-b; do
    check_hash "$evidence_root/runs/$result/runtime-storage-snapshot.csv" "$expected_mtp_snapshot"
    test "$(jq -er '.runtime_mode' "$evidence_root/runs/$result/manifest.json")" = mtp-16
    jq -e 'index("--mtp-threads=16") != null' "$evidence_root/runs/$result/command.json" >/dev/null
done
for result in t43-aa2b26a-local-a t43-aa2b26a-local-b; do
    test "$(jq -er '.runtime_mode' "$evidence_root/runs/$result/manifest.json")" = single-thread
    jq -e 'all(.[]; startswith("--mtp-threads=") | not)' \
        "$evidence_root/runs/$result/command.json" >/dev/null
done

if test -d "$reference_root"; then
    test "$(git -C "$reference_root" rev-parse HEAD)" = "$expected_revision"
    git -C "$reference_root" diff --quiet -- . ':(exclude)scratch/20260721-rtp-tp-memory-probe'
    check_hash "$reference_root/cmake-cache/CMakeCache.txt" "$expected_cache"
    check_hash "$reference_root/build/scratch/ns3.44-ub-quick-example" "$expected_binary"
    check_hash "$reference_root/build/lib/libns3.44-unified-bus.dylib" "$expected_library"
fi

echo "T43 baseline verified: $expected_revision"
