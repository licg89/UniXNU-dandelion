"""Recompute every hunk header in a patch after editing its body.

Editing a patch by hand changes the number of added lines, and git apply rejects a hunk whose
header count disagrees with its body.  This walks the file, counts the old/new lines of each hunk
and rewrites the @@ line accordingly.
"""
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text().splitlines(keepends=True)

out = []
index = 0
fixed = 0
while index < len(lines):
    line = lines[index]
    match = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', line)
    if not match:
        out.append(line)
        index += 1
        continue
    old_start = match.group(1)
    new_start = match.group(3)
    # collect the body
    body = []
    index += 1
    while index < len(lines) and not lines[index].startswith('@@'):
        if lines[index].startswith('diff --git') or lines[index].startswith('--- a/'):
            break
        body.append(lines[index])
        index += 1
    old_count = sum(1 for entry in body if entry.startswith('-') or entry.startswith(' '))
    new_count = sum(1 for entry in body if entry.startswith('+') or entry.startswith(' '))
    previous = line.rstrip('\n')
    line = '@@ -%s,%d +%s,%d @@\n' % (old_start, old_count, new_start, new_count)
    if line.rstrip('\n') != previous:
        fixed += 1
    out.append(line)
    out.extend(body)

path.write_text(''.join(out), encoding='utf-8', newline='\n')
print(f'{path.name}: recomputed {fixed} hunk header(s)')
