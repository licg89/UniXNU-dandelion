#!/bin/bash
set -u
TOKEN="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"
for repo in UniXNU-dandelion uefi-dandelion UniXNU-mt6765-common UniXNU; do
  printf '%-22s ' "$repo"
  curl -s -m 30 -H "Authorization: token $TOKEN" "https://api.github.com/repos/licg89/$repo" \
    | python3 -c 'import json,sys
d = json.load(sys.stdin)
print("private=", d.get("private"), " fork=", d.get("fork"), " full_name=", d.get("full_name"))'
done
