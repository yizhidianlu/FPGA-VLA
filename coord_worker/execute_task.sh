#!/bin/bash
# execute_task.sh — dispatch a claimed task to its type-specific handler
# Called by remote_worker.sh with one arg: path to claimed task YAML
# Responsibilities:
#   1. Create run dir + reports dir
#   2. Resolve handler from task type
#   3. Run handler with timeout
#   4. Collect result.json
#   5. Evaluate acceptance_criteria
#   6. Write inbox DONE/FAIL notification
#   7. Move task pending→completed or _failed
set -u

TASK_YAML=$1
REPO=${FPGA_VLA_REPO:-/root/FPGA-VLA}
COORD=$REPO/coord
source $REPO/coord_worker/lib_common.sh

if [ ! -f "$TASK_YAML" ]; then log "ERROR: task yaml not found: $TASK_YAML"; exit 2; fi

TID=$(basename "$TASK_YAML" .yaml | cut -d_ -f1)
TYPE=$(yaml_get "$TASK_YAML" type)
DEADLINE_MIN=$(yaml_get "$TASK_YAML" deadline_minutes); DEADLINE_MIN=${DEADLINE_MIN:-60}
DEADLINE_SEC=$((DEADLINE_MIN*60))
RUN_DIR=$REPO/runs/$TID
REPORT_DIR=$COORD/reports/$TID
HANDLER=$REPO/coord_worker/handlers/${TYPE}.sh

mkdir -p $RUN_DIR $REPORT_DIR
CLAIMED_AT=$(iso_now)

if [ ! -x "$HANDLER" ]; then
    log "ERROR: no handler for type=$TYPE at $HANDLER"
    cat > $REPORT_DIR/result.json <<EOF
{
  "task_id": "$TID",
  "status": "FAILED",
  "claimed_at": "$CLAIMED_AT",
  "finished_at": "$(iso_now)",
  "machine": "${FPGA_VLA_WORKER_ID:-remote}",
  "error": "no handler for type=$TYPE"
}
EOF
    mv "$TASK_YAML" $COORD/tasks/_failed/
    cat > $COORD/inbox/$(iso_now_safe)_${TID}_FAIL.json <<EOF
{"task_id":"$TID","status":"FAILED","reason":"no_handler","type":"$TYPE"}
EOF
    return 1 2>/dev/null || exit 1
fi

log "[$TID] executing handler $TYPE (timeout ${DEADLINE_MIN}min)"

# Run handler with timeout, tee stdout
START_TS=$(date +%s)
(
    cd $RUN_DIR
    timeout ${DEADLINE_SEC}s bash "$HANDLER" "$TASK_YAML" "$RUN_DIR" "$REPORT_DIR"
)
RC=$?
END_TS=$(date +%s)
DURATION=$((END_TS - START_TS))

FINISHED_AT=$(iso_now)

# Ensure result.json exists (handler should have written one)
if [ ! -f $REPORT_DIR/result.json ]; then
    log "[$TID] WARNING: handler did not write result.json, synthesizing minimal one"
    cat > $REPORT_DIR/result.json <<EOF
{
  "task_id": "$TID",
  "status": "FAILED",
  "claimed_at": "$CLAIMED_AT",
  "finished_at": "$FINISHED_AT",
  "duration_sec": $DURATION,
  "machine": "${FPGA_VLA_WORKER_ID:-remote}",
  "error": "handler produced no result.json (rc=$RC)"
}
EOF
fi

# Merge envelope fields into result.json
python3 - <<PYEOF
import json
p = "$REPORT_DIR/result.json"
with open(p) as f: r = json.load(f)
r.setdefault("task_id", "$TID")
r.setdefault("claimed_at", "$CLAIMED_AT")
r["finished_at"] = "$FINISHED_AT"
r["duration_sec"] = $DURATION
r["machine"] = "${FPGA_VLA_WORKER_ID:-remote}"
r["worker_version"] = "v1.0"
if "status" not in r:
    r["status"] = "DONE" if $RC == 0 else ("TIMEOUT" if $RC == 124 else "FAILED")
with open(p,"w") as f: json.dump(r, f, indent=2)
PYEOF

# Evaluate acceptance criteria via helper
ACCEPT_PASS="true"
if [ -f $REPO/coord_worker/check_acceptance.py ]; then
    python3 $REPO/coord_worker/check_acceptance.py "$TASK_YAML" "$REPORT_DIR/result.json" || ACCEPT_PASS="false"
fi
python3 -c "
import json
p='$REPORT_DIR/result.json'
with open(p) as f: r=json.load(f)
r['acceptance_criteria_passed'] = $ACCEPT_PASS
with open(p,'w') as f: json.dump(r,f,indent=2)
"

# Decide final status and inbox suffix
STATUS=$(python3 -c "import json; print(json.load(open('$REPORT_DIR/result.json')).get('status','FAILED'))")
if [ "$STATUS" = "DONE" ] && [ "$ACCEPT_PASS" = "true" ]; then
    SUFFIX="DONE"
    mv "$TASK_YAML" $COORD/tasks/completed/
else
    SUFFIX="FAIL"
    mv "$TASK_YAML" $COORD/tasks/_failed/ 2>/dev/null || true
fi

# Write inbox notification
INBOX_FILE=$COORD/inbox/$(iso_now_safe)_${TID}_${SUFFIX}.json
cp $REPORT_DIR/result.json $INBOX_FILE
log "[$TID] -> $SUFFIX (duration ${DURATION}s, accept_pass=$ACCEPT_PASS)"

exit $RC
