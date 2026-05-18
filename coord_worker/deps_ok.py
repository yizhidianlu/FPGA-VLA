#!/usr/bin/env python3
"""deps_ok.py <task_yaml>
Exit 0 if all dependencies in task.depends_on are completed; else exit 1.
A dep T0040 is satisfied iff coord/tasks/completed/T0040*.yaml exists."""
import sys, os, glob, yaml

if len(sys.argv) != 2:
    print("usage: deps_ok.py <task_yaml>", file=sys.stderr); sys.exit(2)

task_path = sys.argv[1]
repo_root = os.path.dirname(os.path.dirname(os.path.abspath(task_path)))
# climb out: task at .../coord/tasks/pending/T0042.yaml -> repo is .../
coord_root = task_path
for _ in range(3):
    coord_root = os.path.dirname(coord_root)
completed_dir = os.path.join(coord_root, "tasks", "completed")

with open(task_path) as f:
    t = yaml.safe_load(f) or {}
deps = t.get("depends_on", []) or []

missing = []
for d in deps:
    pat = os.path.join(completed_dir, f"{d}*.yaml")
    if not glob.glob(pat):
        missing.append(d)

if missing:
    print(f"deps not met for {t.get('task_id','?')}: {missing}", file=sys.stderr)
    sys.exit(1)
sys.exit(0)
