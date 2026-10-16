#!/usr/bin/env python3
"""Owned Linux application C, native host sockets only; immutable run labels."""
import argparse, hashlib, json, os, pathlib, subprocess, sys, platform
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SRC=ROOT/'src'
p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
def run(cmd,path,env=None):
 with path.open('w') as log:
  r=subprocess.run([str(x) for x in cmd],stdout=log,stderr=subprocess.STDOUT,env=env,timeout=45)
 if r.returncode: print(path.read_text()[-3000:]);raise RuntimeError((cmd,r.returncode))
 return (path.read_text().strip().splitlines() or ['PASS'])[-1]
results=[]
for mode in ('native','sanitizer'):
 flags=['-std=gnu11','-ffunction-sections','-fdata-sections','-pthread','-I'+str(SRC),'-g','-O1']
 if mode=='sanitizer':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 for name in ('protocol','packet','write','peer','rtcp','fault','budget','legacy_auth'):
  exe=out/(name+'-'+mode);cmd=[os.environ.get('CC','cc'),*flags];files=[HERE/(name+'_test.c')]
  if name=='legacy_auth':
   cmd+=['-DSO_SNDBUFFORCE=SO_SNDBUF','-DCLOCK_MONOTONIC_COARSE=CLOCK_MONOTONIC',('-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections')];files+=[SRC/'rtsp/mime.c',SRC/'hal/tools.c']
  else:
   cmd+=['-DDIVINUS_OWNED_INPUT'];files+=[SRC/'rtsp/owned_rtcp.c']
   if name!='fault':files += [SRC/'rtsp'/n for n in (('rtp.c','mime.c') if name=='budget' else ('rtsp.c','rtp.c','mime.c'))]
  run([*cmd,*files,'-o',exe],out/(name+'-'+mode+'-compile.log'))
  result=run([exe],out/(name+'-'+mode+'.log'),env)
  results.append(dict(test=name,mode=mode,result=result));print(mode,name,result,flush=True)
inputs=[HERE/n for n in ('protocol_test.c','packet_test.c','write_test.c','peer_test.c','rtcp_test.c','fault_test.c','budget_test.c','legacy_auth_test.c','run_tests.py')]
inputs += [SRC/'rtsp'/n for n in ('rtsp.c','rtsp.h','rtsp_server.h','rtp.c','rtp.h','rtcp.h','common.h','list.h','bufpool.h','thread.h','hash.h','mime.c','mime.h','owned_rtcp.c','owned_rtcp.h')]
inputs += [SRC/'fmt/nal.h',SRC/'hal/tools.c',SRC/'hal/tools.h',SRC/'hal/types.h']
bindings=[dict(path=str(f.relative_to(ROOT)),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in inputs]
(out/'receipt.json').write_text(json.dumps(dict(scope='Actual owned Linux RTSP host tests; no target/device execution or packaging',results=results,inputs=bindings,leak_sanitizer='disabled on macOS; ASan+UBSan active'),indent=2)+'\n')
