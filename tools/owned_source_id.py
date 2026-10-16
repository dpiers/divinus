#!/usr/bin/env python3
"""Hash the checked normal-checkout production closure; no Git or downloads."""
import hashlib
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
manifest = root / 'tools/owned_sources.json'
data = json.loads(manifest.read_text())
files = data['files']
units = data['translation_units']
if (data['schema'] != 'owned-production-closure-v2' or len(files) != 39
        or files != sorted(set(files)) or len(set(units)) != 12
        or not set(units) <= set(files)
        or not {'tools/owned_sources.json', 'tools/owned_source_id.py', 'src/Makefile'} <= set(files)):
    raise ValueError('Invalid production closure')
identity = hashlib.sha256()
for name in files:
    relative = pathlib.PurePosixPath(name)
    if (relative.is_absolute() or '..' in relative.parts or str(relative) != name
            or not name.startswith(('src/', 'tools/', 'third_party/nt98513-owned-capture/'))):
        raise ValueError('Unsafe source path: ' + name)
    path = root / name
    if not path.resolve().is_relative_to(root) or not path.is_file():
        raise ValueError('Missing or escaped source: ' + name)
    identity.update((name + '\0' + hashlib.sha256(path.read_bytes()).hexdigest() + '\n').encode())
print('#define OWNED_BUILD_ID "sha256:' + identity.hexdigest() + '"')
print('#define OWNED_BASE_REV "b47abc428d6058731fbc256fe73e05e118d61326"')
