#!/bin/bash
# Set our repositories private through the REST API.  gh auth login wants read:org, but the API
# only needs repo scope for this change.
set -u
TOKEN="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"
for repo in UniXNU-dandelion uefi-dandelion UniXNU UniXNU-mt6765-common; do
  printf '%-24s ' "$repo"
  body=$(curl -s -m 30 -X PATCH \
    -H "Authorization: token $TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -d '{"private":true}' \
    "https://api.github.com/repos/licg89/$repo")
  echo "$body" | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception:
    print("unreadable response")
    raise SystemExit
if "private" in d:
    print("private =", d["private"], "|", d.get("full_name"))
else:
    print("failed:", d.get("message", "?"))
'
done
