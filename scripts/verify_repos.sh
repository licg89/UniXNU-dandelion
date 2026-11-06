#!/bin/bash
set -u
# Never commit a token: read it from the environment.
: "${GITHUB_TOKEN:?set GITHUB_TOKEN to a PAT with repo scope}"
TOKEN="$GITHUB_TOKEN"
for repo in UniXNU-dandelion uefi-dandelion UniXNU-mt6765-common UniXNU; do
  printf '%-22s ' "$repo"
  curl -s -m 30 -H "Authorization: token $TOKEN" "https://api.github.com/repos/licg89/$repo" \
    | python3 -c 'import json,sys
d = json.load(sys.stdin)
print("private=", d.get("private"), " fork=", d.get("fork"), " full_name=", d.get("full_name"))'
done
