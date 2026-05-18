#!/usr/bin/env python3
"""new_task.py — scaffold a new task YAML
Usage:
  python3 new_task.py <task_id> <type> [--depends T0010,T0011] [--priority P1] [--deadline 60]

Example:
  python3 new_task.py T0020 hls_csynth --depends T0012 --priority P1 --deadline 60
"""
import argparse, datetime, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PEND = os.path.join(REPO, "coord", "tasks", "pending")

TEMPLATES = {
    "env_probe": {"params": {}, "acceptance_criteria":[
        {"status":"== DONE"}, {"result.gpu_name":"!= none"}]},
    "vitis_install_check": {"params":{}, "acceptance_criteria":[
        {"status":"== DONE"}, {"result.hls_ok":True}, {"result.vivado_ok":True}]},
    "hls_blank_smoke": {"params":{"target_part":"xcvc1902-vsva2197-2MP-e-S","clock_period_ns":4.0},
        "acceptance_criteria":[{"status":"== DONE"},{"result.csim_pass":True},{"result.csynth_pass":True}]},
    "hls_csynth": {"params":{
        "source_files":["empirical/hw/src/example.cpp"], "top_function":"example",
        "target_part":"xcvc1902-vsva2197-2MP-e-S", "clock_period_ns":4.0},
        "acceptance_criteria":[{"status":"== DONE"},{"result.csynth_pass":True}]},
    "vivado_synth": {"params":{
        "rtl_files":["empirical/hw/rtl/top.v"], "top_module":"top",
        "target_part":"xcvc1902-vsva2197-2MP-e-S"},
        "acceptance_criteria":[{"status":"== DONE"},{"result.synth_ok":True}]},
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("task_id")
    ap.add_argument("type")
    ap.add_argument("--depends", default="")
    ap.add_argument("--priority", default="P1")
    ap.add_argument("--deadline", type=int, default=30)
    ap.add_argument("--notes", default="")
    args = ap.parse_args()

    deps = [d.strip() for d in args.depends.split(",") if d.strip()] if args.depends else []
    tpl = TEMPLATES.get(args.type, {"params":{}, "acceptance_criteria":[{"status":"== DONE"}]})

    now = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    yaml_text = f"""task_id: {args.task_id}
created_by: orchestrator
created_at: {now}
type: {args.type}
priority: {args.priority}
depends_on: {deps}
deadline_minutes: {args.deadline}
params:
"""
    import json as _j
    for k, v in tpl["params"].items():
        yaml_text += f"  {k}: {_j.dumps(v)}\n"
    yaml_text += "acceptance_criteria:\n"
    for c in tpl["acceptance_criteria"]:
        k, v = next(iter(c.items()))
        yaml_text += f"  - {k}: {_j.dumps(v) if not isinstance(v,str) else repr(v)}\n"
    yaml_text += "human_ack_required_on: []\n"
    if args.notes:
        yaml_text += f"notes: |\n  {args.notes}\n"

    os.makedirs(PEND, exist_ok=True)
    fname = f"{args.task_id}_{args.type}.yaml"
    path = os.path.join(PEND, fname)
    if os.path.exists(path):
        print(f"refusing to overwrite {path}", file=sys.stderr); sys.exit(1)
    with open(path, "w") as f:
        f.write(yaml_text)
    print(f"wrote {path}")
    print("review then: git add coord/tasks && git commit -m 'add {args.task_id}' && git push")

if __name__ == "__main__":
    main()
