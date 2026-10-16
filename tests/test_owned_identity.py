#!/usr/bin/env python3
"""Real generator refusal/closure tests in disposable checkout copies."""
import json,pathlib,shutil,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
def generate(p):
 return subprocess.run(['python3',str(p/'tools/owned_source_id.py')],text=True,capture_output=True)
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);m=json.loads((ROOT/'tools/owned_sources.json').read_text())
 assert all(not n.startswith(('source/','deps/')) for n in m['files']), 'manifest must name normal checkout paths'
 for n in m['files']:
  d=p/n;d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/n,d)
 first=generate(p);assert first.returncode==0, first.stderr
 assert first.stdout==generate(ROOT).stdout
 (p/'src/hal/unrelated.c').write_text('not compiled\n');assert generate(p).stdout==first.stdout
 for name in m['files']:
  if name in ('tools/owned_source_id.py','tools/owned_sources.json'):continue
  f=p/name;b=f.read_bytes();f.unlink();assert generate(p).returncode!=0,name;f.write_bytes(b)
 manifest=p/'tools/owned_sources.json';original=manifest.read_bytes()
 for bad in ('../escape','/tmp/escape','src/../escape','src//escape'):
  copy=json.loads(original);copy['files'][0]=bad;manifest.write_text(json.dumps(copy));assert generate(p).returncode!=0,bad
 manifest.write_bytes(original)
 copy=json.loads(original);copy['files'].append('src/hal/unrelated.c');manifest.write_text(json.dumps(copy));assert generate(p).returncode!=0
 manifest.write_bytes(original)
 f=p/'src/main.c';f.unlink();f.symlink_to(ROOT/'src/main.c');assert generate(p).returncode!=0,'escape symlink'
 print('normal/trimmed identity; unrelated input exclusion; missing, extra and unsafe paths refused')

bad=subprocess.run(['make','-n','-C',str(ROOT/'src'),'divinus-owned','OWNED_DEPS=/tmp'],capture_output=True)
assert bad.returncode!=0, 'outside dependency override must be refused'
