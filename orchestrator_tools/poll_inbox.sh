#!/bin/bash
# poll_inbox.sh — run locally by Orchestrator to read new DONE/FAIL notifications
# Usage: bash orchestrator_tools/poll_inbox.sh [--pull]
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd $REPO

if [ "${1:-}" = "--pull" ]; then
    git pull --quiet
fi

INBOX=$REPO/coord/inbox
HB=$REPO/coord/heartbeat/remote_claude.json

echo "==== Heartbeat ===="
if [ -f $HB ]; then
    python3 -c "
import json, time, datetime
h = json.load(open('$HB'))
ts = h['ts']
t = datetime.datetime.fromisoformat(ts.replace('Z','+00:00'))
age = (datetime.datetime.now(datetime.timezone.utc) - t).total_seconds()
status = 'ALIVE' if age < 180 else ('STALE' if age < 600 else 'DEAD')
print(f\"  worker:    {h['worker']}\")
print(f\"  last seen: {ts}  ({age:.0f}s ago) [{status}]\")
print(f\"  vitis:     {h.get('vitis','?')}\")
print(f\"  vivado:    {h.get('vivado','?')}\")
print(f\"  current:   {h.get('current_task','idle')}\")
print(f\"  completed: {h.get('tasks_completed_session',0)} (this session)\")
"
else
    echo "  (no heartbeat file yet — worker not started?)"
fi

echo
echo "==== New inbox ===="
new=$(ls -1 $INBOX/*.json 2>/dev/null | grep -v '_processed' || true)
if [ -z "$new" ]; then
    echo "  (empty)"
else
    for f in $new; do
        echo "----- $(basename $f) -----"
        python3 -c "
import json
r = json.load(open('$f'))
print(f\"  task:      {r.get('task_id')}\")
print(f\"  status:    {r.get('status')}\")
print(f\"  accept:    {r.get('acceptance_criteria_passed')}\")
print(f\"  duration:  {r.get('duration_sec')}s\")
res = r.get('result', {})
for k, v in res.items():
    s = str(v)
    if len(s) > 80: s = s[:77] + '...'
    print(f\"    {k}: {s}\")
"
        echo
    done
fi

echo "==== Pending tasks (waiting to be claimed) ===="
ls -1 $REPO/coord/tasks/pending/*.yaml 2>/dev/null | xargs -I{} basename {} .yaml || echo "  (none)"

echo
echo "==== Claimed tasks (in flight) ===="
ls -1 $REPO/coord/tasks/claimed/*.yaml 2>/dev/null | xargs -I{} basename {} .yaml || echo "  (none)"

echo
echo "==== Last 3 completed ===="
ls -1t $REPO/coord/tasks/completed/*.yaml 2>/dev/null | head -3 | xargs -I{} basename {} .yaml
