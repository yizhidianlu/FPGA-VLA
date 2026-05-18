#!/bin/bash
# Shared utilities for worker scripts
# Source this: source $REPO/coord_worker/lib_common.sh

# Logging with timestamp
log() {
    echo "[$(date '+%F %T')] $*" >&2
}

# Emit JSON via python (avoids escaping hell)
json_set() {
    # json_set <file> <key.path> <value>  — uses python to merge
    local file=$1 keypath=$2 value=$3
    python3 -c "
import json, sys
try:
    with open('$file') as f: d = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
    d = {}
keys = '$keypath'.split('.')
obj = d
for k in keys[:-1]:
    obj = obj.setdefault(k, {})
try:
    obj[keys[-1]] = json.loads('''$value''')
except json.JSONDecodeError:
    obj[keys[-1]] = '''$value'''
with open('$file','w') as f: json.dump(d, f, indent=2)
"
}

# Read a YAML field via python (PyYAML must be available)
yaml_get() {
    # yaml_get <file> <key.path>
    python3 -c "
import yaml, sys
with open('$1') as f: d = yaml.safe_load(f)
ks = '$2'.split('.')
v = d
for k in ks:
    if v is None: print(''); sys.exit(0)
    v = v.get(k) if isinstance(v, dict) else None
print('' if v is None else (v if isinstance(v,str) else __import__('json').dumps(v)))
"
}

# Generate ISO8601 UTC timestamp
iso_now() { date -u +%FT%TZ; }

# Filename-safe ISO timestamp
iso_now_safe() { date -u +%FT%H-%M-%SZ; }

# Compute file checksum for artefact provenance
file_sha256() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
