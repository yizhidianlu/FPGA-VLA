#!/bin/bash
# Remote Worker daemon — main loop
# Deployed at /root/FPGA-VLA/coord_worker/remote_worker.sh
# Run via systemd unit fpga-vla-worker.service
set -u

REPO=${FPGA_VLA_REPO:-/root/FPGA-VLA}
COORD=$REPO/coord
WORKER_ID=${FPGA_VLA_WORKER_ID:-remote_vitis_box}
TICK_INTERVAL=${FPGA_VLA_TICK_SEC:-30}
PROTOCOL_VERSION_EXPECTED="1.0"

source $REPO/coord_worker/lib_common.sh

mkdir -p $COORD/{tasks/{pending,claimed,completed,_failed},reports,inbox/_processed,ack/_processed,heartbeat}
cd $REPO

START_TS=$(date +%s)
TASKS_COMPLETED=0

git_safe_sync() {
    # pull --rebase --autostash, with retry
    for i in 1 2 3; do
        if git pull --rebase --autostash --quiet 2>/dev/null; then return 0; fi
        log "git pull failed (attempt $i), retrying in 5s"
        sleep 5
    done
    log "ERROR: git pull failed after 3 attempts"
    return 1
}

git_safe_push() {
    git add coord/ 2>/dev/null
    if ! git diff --cached --quiet; then
        local msg="${1:-worker $WORKER_ID tick}"
        git commit -m "$msg" --quiet
        for i in 1 2 3; do
            if git push --quiet 2>/dev/null; then return 0; fi
            log "git push failed (attempt $i), pulling+rebasing"
            git pull --rebase --autostash --quiet 2>/dev/null
            sleep 3
        done
        log "ERROR: git push failed after 3 attempts"
        return 1
    fi
    return 0
}

write_heartbeat() {
    local current_task=${1:-}
    local uptime=$(( $(date +%s) - START_TS ))
    local vitis_ok="MISSING"; command -v vitis_hls &>/dev/null && vitis_ok="OK"
    local vivado_ok="MISSING"; command -v vivado &>/dev/null && vivado_ok="OK"
    local gpu=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)
    [ -z "$gpu" ] && gpu="none"
    local load=$(cut -d' ' -f1 /proc/loadavg 2>/dev/null)
    cat > $COORD/heartbeat/remote_claude.json <<EOF
{
  "worker": "$WORKER_ID",
  "ts": "$(date -u +%FT%TZ)",
  "pid": $$,
  "uptime_sec": $uptime,
  "vitis": "$vitis_ok",
  "vivado": "$vivado_ok",
  "gpu": "$gpu",
  "load_avg": $load,
  "current_task": "$current_task",
  "tasks_completed_session": $TASKS_COMPLETED,
  "protocol_version": "$PROTOCOL_VERSION_EXPECTED"
}
EOF
}

check_protocol_version() {
    local v=$(grep -oP 'PROTOCOL_VERSION = "\K[^"]+' $COORD/PROTOCOL.md 2>/dev/null | head -1)
    if [ -z "$v" ]; then return 0; fi
    if [ "$v" != "$PROTOCOL_VERSION_EXPECTED" ]; then
        log "ERROR: PROTOCOL.md version=$v but worker expects $PROTOCOL_VERSION_EXPECTED — stopping"
        write_heartbeat "protocol_mismatch_halted"
        return 1
    fi
    return 0
}

process_acks() {
    for ack in $COORD/ack/*.json; do
        [ -f "$ack" ] || continue
        log "ACK: $(basename $ack)"
        bash $REPO/coord_worker/handle_ack.sh "$ack" || log "  ACK handler returned non-zero"
        mv "$ack" $COORD/ack/_processed/
    done
}

try_claim_and_run() {
    # Find first pending task whose deps are satisfied
    for task in $(ls $COORD/tasks/pending/*.yaml 2>/dev/null | sort); do
        if python3 $REPO/coord_worker/deps_ok.py "$task" 2>/dev/null; then
            local fname=$(basename "$task")
            local tid=$(echo "$fname" | cut -d_ -f1)
            # Atomic claim via mv (works because mv is atomic on same FS)
            if mv "$task" "$COORD/tasks/claimed/$fname" 2>/dev/null; then
                log "CLAIMED $tid ($fname)"
                write_heartbeat "$tid"
                git_safe_push "claim $tid by $WORKER_ID"
                # Execute
                bash $REPO/coord_worker/execute_task.sh "$COORD/tasks/claimed/$fname"
                local rc=$?
                if [ $rc -eq 0 ]; then
                    TASKS_COMPLETED=$((TASKS_COMPLETED+1))
                    log "DONE $tid"
                else
                    log "FAILED $tid (rc=$rc)"
                fi
                git_safe_push "complete $tid (rc=$rc)"
                write_heartbeat ""
                return 0
            fi
        fi
    done
    return 1
}

log "=== FPGA-VLA Worker $WORKER_ID starting ==="
log "Repo: $REPO  Coord: $COORD  Tick: ${TICK_INTERVAL}s  Protocol: $PROTOCOL_VERSION_EXPECTED"

while true; do
    git_safe_sync || { sleep $TICK_INTERVAL; continue; }
    check_protocol_version || { sleep 60; continue; }
    write_heartbeat ""
    process_acks
    try_claim_and_run
    git_safe_push "heartbeat $WORKER_ID" 2>/dev/null
    sleep $TICK_INTERVAL
done
