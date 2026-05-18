#!/usr/bin/env python3
"""check_acceptance.py <task_yaml> <result_json>
Evaluate task.acceptance_criteria against result.json.
Exit 0 = all pass; 1 = at least one fails or comparison error.
Each criterion is a single-key dict: {field: "<comparison>"}
Supported comparisons:
  "== value"  "!= value"
  "< value"   "<= value"  "> value"  ">= value"
  "true"  "false"
  "in [v1, v2]"
  "contains substr"
"""
import sys, json, yaml, re

if len(sys.argv) != 3: sys.exit(2)
task = yaml.safe_load(open(sys.argv[1]))
result = json.load(open(sys.argv[2]))
crit = task.get("acceptance_criteria") or []

def lookup(obj, dotted):
    cur = obj
    for k in dotted.split('.'):
        if isinstance(cur, dict) and k in cur: cur = cur[k]
        elif k.startswith('result.') and 'result' in obj: return lookup(obj['result'], k[7:])
        else: return None
    return cur

def evaluate(actual, expr):
    expr = str(expr).strip()
    if expr in ("true", "True"):  return actual is True or str(actual).lower() == "true"
    if expr in ("false", "False"): return actual is False or str(actual).lower() == "false"
    m = re.match(r'^(==|!=|<=|>=|<|>)\s*(.+)$', expr)
    if m:
        op, v = m.group(1), m.group(2).strip()
        try: v = float(v) if not v.startswith('"') else v.strip('"')
        except ValueError: pass
        try: a = float(actual) if op in ('<','<=','>','>=') else actual
        except (ValueError, TypeError): return False
        if op == '==': return a == v
        if op == '!=': return a != v
        if op == '<':  return a <  v
        if op == '<=': return a <= v
        if op == '>':  return a >  v
        if op == '>=': return a >= v
    if expr.startswith("in "):
        lst = yaml.safe_load(expr[3:])
        return actual in lst
    if expr.startswith("contains "):
        return expr[9:].strip().strip('"') in str(actual)
    return False

all_pass = True
report = []
for c in crit:
    if not isinstance(c, dict) or len(c) != 1:
        all_pass = False; report.append(f"BAD criterion {c}"); continue
    field, expr = next(iter(c.items()))
    # field may be top-level OR nested under "result"
    actual = lookup(result, field)
    if actual is None and 'result' in result:
        actual = lookup(result['result'], field)
    ok = evaluate(actual, expr)
    report.append(f"  {field}={actual} {expr} -> {'PASS' if ok else 'FAIL'}")
    if not ok: all_pass = False

for line in report: print(line, file=sys.stderr)
sys.exit(0 if all_pass else 1)
