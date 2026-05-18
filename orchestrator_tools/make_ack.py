#!/usr/bin/env python3
"""make_ack.py — generate an ACK JSON for the remote worker
Usage:
  python3 make_ack.py <task_id> <verb> [--reason TEXT] [--next-task TID] [--by NAME]
verbs: proceed | retry | abort | release | scope_change
"""
import argparse, json, datetime, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACK_DIR = os.path.join(REPO, "coord", "ack")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("task_id")
    ap.add_argument("verb", choices=["proceed","retry","abort","release","scope_change"])
    ap.add_argument("--reason", default="")
    ap.add_argument("--next-task", default="")
    ap.add_argument("--by", default="human:jielu")
    args = ap.parse_args()

    ack = {
        "task_id": args.task_id,
        "verb": args.verb,
        "decided_by": args.by,
        "decided_at": datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reason": args.reason,
    }
    if args.next_task:
        ack["next_task_to_unblock"] = args.next_task

    os.makedirs(ACK_DIR, exist_ok=True)
    fname = f"{args.task_id}_ACK_{args.verb}.json"
    path = os.path.join(ACK_DIR, fname)
    with open(path, "w") as f:
        json.dump(ack, f, indent=2)
    print(f"wrote {path}")
    print("Don't forget: git add coord/ack && git commit -m 'ack' && git push")

if __name__ == "__main__":
    main()
