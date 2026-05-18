#!/bin/bash
# handle_ack.sh <ack_json> — apply orchestrator's decision
set -u
ACK=$1
REPO=${FPGA_VLA_REPO:-/root/FPGA-VLA}
COORD=$REPO/coord
source $REPO/coord_worker/lib_common.sh

TID=$(python3 -c "import json; print(json.load(open('$ACK')).get('task_id',''))")
VERB=$(python3 -c "import json; print(json.load(open('$ACK')).get('verb',''))")
NEXT=$(python3 -c "import json; print(json.load(open('$ACK')).get('next_task_to_unblock',''))")
log "ACK $TID verb=$VERB next=$NEXT"

case "$VERB" in
    proceed)
        # Nothing structural to do — just mark heartbeat
        log "  proceed acknowledged for $TID"
        ;;
    retry)
        src=$(ls $COORD/tasks/_failed/${TID}*.yaml 2>/dev/null | head -1)
        if [ -n "$src" ]; then
            # mint new id with _retry suffix; keep numeric T-id but append
            new_tid="${TID}r$(date +%s)"
            new_file=$COORD/tasks/pending/${new_tid}_retry.yaml
            cp "$src" "$new_file"
            python3 -c "
import yaml
with open('$new_file') as f: d=yaml.safe_load(f)
d['task_id']='$new_tid'
d['notes']=(d.get('notes','') or '')+'\nretry of $TID per ACK'
with open('$new_file','w') as f: yaml.safe_dump(d,f,sort_keys=False)
"
            log "  retry: created $new_file"
        else
            log "  retry: no _failed/$TID found"
        fi
        ;;
    abort)
        for d in pending claimed; do
            f=$(ls $COORD/tasks/$d/${TID}*.yaml 2>/dev/null | head -1)
            [ -n "$f" ] && mv "$f" $COORD/tasks/_failed/ && log "  aborted: moved $f to _failed"
        done
        ;;
    release)
        f=$(ls $COORD/tasks/claimed/${TID}*.yaml 2>/dev/null | head -1)
        [ -n "$f" ] && mv "$f" $COORD/tasks/pending/ && log "  released: $f back to pending"
        ;;
    scope_change)
        # ack should contain a 'new_acceptance_criteria' array
        f=$(ls $COORD/tasks/pending/${TID}*.yaml 2>/dev/null | head -1)
        [ -z "$f" ] && f=$(ls $COORD/tasks/claimed/${TID}*.yaml 2>/dev/null | head -1)
        if [ -n "$f" ]; then
            python3 - <<PYEOF
import json, yaml
ack = json.load(open("$ACK"))
new_crit = ack.get("new_acceptance_criteria")
if new_crit is not None:
    with open("$f") as g: t = yaml.safe_load(g)
    t["acceptance_criteria"] = new_crit
    with open("$f","w") as g: yaml.safe_dump(t, g, sort_keys=False)
    print("scope_change applied")
PYEOF
            log "  scope_change applied to $f"
        fi
        ;;
    *)
        log "  unknown verb: $VERB"
        ;;
esac
exit 0
