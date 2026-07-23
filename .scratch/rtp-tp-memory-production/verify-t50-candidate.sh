#!/usr/bin/env bash
set -euo pipefail

feature_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
manifest="$feature_root/.scratch/rtp-tp-memory-production/t50-comparison-manifest.json"
evidence="$feature_root/.scratch/rtp-tp-memory-production/t50-evidence"
baseline="$feature_root/.scratch/rtp-tp-memory-production/t43-baseline-manifest.json"

"$feature_root/.scratch/rtp-tp-memory-production/verify-t43-baseline.sh" >/dev/null
(
    cd "$feature_root"
    shasum -a 256 -c "$evidence/payload-sha256.txt" >/dev/null
)

candidate_revision=$(jq -er '.candidate.source_revision' "$manifest")
candidate_binary=$(jq -er '.candidate.binary_sha256' "$manifest")
rss_limit=$(jq -er '.guards.rss_limit_bytes' "$manifest")
wall_limit=$(jq -er '.guards.wall_limit_seconds' "$manifest")
baseline_trace=$(jq -er '.stable_hashes.p001_task_trace_tree_sha256' "$baseline")

jq -e '
    .schema_version == 1 and
    .guards.serial_execution == true and
    .guards.largest_attempted_case == "p016" and
    .guards.p064_run == false and
    (.runs | length) == 4 and
    ([.runs[].case_id] | index("p064")) == null
' "$manifest" >/dev/null

for run in t50-36334c0-p001-local t50-36334c0-p001-mtp16 t50-36334c0-p004-mtp16; do
    run_root="$evidence/runs/$run"
    jq -e --arg revision "$candidate_revision" --arg binary "$candidate_binary" \
        --argjson rss "$rss_limit" --argjson wall "$wall_limit" '
        .source_revision == $revision and .binary_sha256 == $binary and
        .return_code == 0 and .stop_reason == "completed" and
        .rss_limit_bytes == $rss and .wall_limit_seconds == $wall and
        .runtime_storage_snapshot.exists == true
    ' "$run_root/manifest.json" >/dev/null
    test "$(tr -d '\n' < "$run_root/exit-code.txt")" = 0
    awk -F, 'NF != 5 { exit 1 }' "$run_root/runtime-storage-snapshot.csv"
done

p016="$evidence/runs/t50-36334c0-p016-mtp16"
jq -e --arg revision "$candidate_revision" --arg binary "$candidate_binary" \
    --argjson rss "$rss_limit" --argjson wall "$wall_limit" '
    .source_revision == $revision and .binary_sha256 == $binary and
    .return_code == -15 and .stop_reason == "wall_limit" and
    .rss_limit_bytes == $rss and .wall_limit_seconds == $wall and
    .runtime_storage_snapshot.exists == false
' "$p016/manifest.json" >/dev/null
test "$(tr -d '\n' < "$p016/exit-code.txt")" = -15

for run in t50-36334c0-p001-local t50-36334c0-p001-mtp16; do
    run_manifest="$evidence/runs/$run/manifest.json"
    while IFS=$'\t' read -r name expected; do
        test "$(jq -er --arg name "$name" '.input_sha256[$name]' "$run_manifest")" = "$expected"
    done < <(jq -r '.input_sha256 | to_entries[] | [.key, .value] | @tsv' "$baseline")
done

for list in p001-local.sha256 p001-mtp16.sha256; do
    test "$(wc -l < "$evidence/task-trace-checksums/$list" | tr -d ' ')" = 1024
    test "$(shasum -a 256 "$evidence/task-trace-checksums/$list" | awk '{print $1}')" = \
        "$baseline_trace"
done
test "$(wc -l < "$evidence/task-trace-checksums/p004-mtp16.sha256" | tr -d ' ')" = 1024
test "$(shasum -a 256 "$evidence/task-trace-checksums/p004-mtp16.sha256" | awk '{print $1}')" = \
    "5b88205313efcc7ef5b24478ed8281ae379fe27696c4f8a476073fb59de55ef0"

metric() {
    local snapshot=$1
    local name=$2
    awk -F, -v name="$name" '$2 == "runtime" && $3 == name { print $5 }' "$snapshot"
}

p001_mtp="$evidence/runs/t50-36334c0-p001-mtp16/runtime-storage-snapshot.csv"
p004_mtp="$evidence/runs/t50-36334c0-p004-mtp16/runtime-storage-snapshot.csv"
test "$(metric "$p001_mtp" live_tp_backing_objects)" = 0
test "$(metric "$p001_mtp" dormant_tp_records)" = 2048
test "$(metric "$p001_mtp" peak_live_tp_backing_objects)" = 2048
test "$(metric "$p004_mtp" live_tp_backing_objects)" = 0
test "$(metric "$p004_mtp" dormant_tp_records)" = 8192
test "$(metric "$p004_mtp" peak_live_tp_backing_objects)" = 2048

projection=$(awk -F, '
    NR == 1 || ($2 != "trace_writer" &&
                $3 != "authoritative_local_connection_views" &&
                $3 != "secondary_connection_index_entries" &&
                $3 != "reserved_endpoint_slots" &&
                $3 != "live_tp_backing_objects" &&
                $3 != "dormant_tp_records" &&
                $3 != "tp_backing_rehydrations" &&
                $3 != "peak_live_tp_backing_objects" &&
                $3 != "task_scoped_jetties" &&
                $3 != "reusable_jetties" &&
                $3 != "simulator_event_count")
' "$p001_mtp" | shasum -a 256 | awk '{print $1}')
test "$projection" = "$(jq -er '.comparisons.p001_legacy_projection_sha256' "$manifest")"

test ! -e "$evidence/runs/t50-36334c0-p064-mtp16"
echo "T50 candidate verified: $candidate_revision"
