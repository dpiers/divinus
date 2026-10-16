#!/usr/bin/env python3
"""Self-contained Linux host verification; optional explicit pinned MIPS compile.
Only newly owned loopback/socketpairs execute. Target executables never run.
"""
import argparse,hashlib,json,os,pathlib,shutil,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=pathlib.Path);p.add_argument('--cc',default='cc');p.add_argument('--mips-cc',type=pathlib.Path);p.add_argument('--toolchain-pin',type=pathlib.Path);p.add_argument('--rtsp',action='store_true');p.add_argument('--ffmpeg');p.add_argument('--ffprobe');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd,name,env=None):
 with (out/name).open('w') as f:r=subprocess.run(list(map(str,cmd)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=300)
 if r.returncode:raise RuntimeError(str(cmd)+' failed: '+str(out/name))
 return (out/name).read_text()
def scan(cc,name,extra=()):
 m=json.loads((ROOT/'tools/owned_sources.json').read_text())
 text=run([cc,'-pthread','-O2','-std=gnu11','-D_GNU_SOURCE','-DDIVINUS_OWNED_INPUT','-Isrc','-Ithird_party/nt98513-owned-capture/include',*extra,'-MM',*m['translation_units']],name)
 paths=set()
 for line in text.replace('\\\n',' ').splitlines():paths.update(os.path.normpath(s) for s in line.split(':',1)[1].split())
 assert paths|{'src/Makefile','tools/owned_source_id.py','tools/owned_sources.json'}==set(m['files'])
 return m
results={'scope':'host source tests and inert target build only','passed':False,'target_executed':False}
try:
 run([a.cc,'--version'],'compiler.txt')
 run([sys.executable,'tests/test_owned_identity.py'],'identity.log')
 m=scan(a.cc,'native-dependencies.d')
 for mode in ('native','sanitizer'):
  dest=out/mode;dest.mkdir();flags='' if mode=='native' else '-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
  # Both real goals concurrently, one shared generated header, distinct products.
  run(['make','-j4','-C','src','divinus-owned','divinus-owned-fixture','CC='+a.cc,'OWNED_OUT='+str(dest),'OWNED_OPT='+flags],mode+'-build.log',env)
  run([sys.executable,'tests/test_owned_application.py',dest/'divinus-owned-fixture'],mode+'-application.log',env)
  for name,srcs in [('config',['tests/test_owned_config.c','src/owned_config.c']),('mime',['tests/test_owned_mime.c','src/rtsp/mime.c']),('rtcp',['tests/test_owned_rtcp.c','src/rtsp/owned_rtcp.c']),('legacy-rtp',['tests/test_legacy_rtp.c','src/rtsp/mime.c'])]:
   run([a.cc,'-std=gnu11','-D_GNU_SOURCE','-g','-O2',*flags.split(),'-ffunction-sections','-fdata-sections','-Isrc','-Isrc/rtsp',*srcs,'-pthread','-Wl,--gc-sections','-o',dest/name],mode+'-'+name+'-build.log',env)
   run([dest/name],mode+'-'+name+'.log',env)
  fault=dest/'join-fault'
  run(['make','-C','src','divinus-owned-fixture','CC='+a.cc,'OWNED_OUT='+str(fault),'OWNED_OPT='+flags,'OWNED_TEST_EXTRA=../tests/join_fault.c -Wl,--wrap=pthread_join'],mode+'-join-build.log',env)
  run([sys.executable,'tests/test_owned_join_failure.py',fault/'divinus-owned-fixture'],mode+'-join.log',env)
 run([sys.executable,'tests/test_owned_parallel.py',out/'native',a.cc],'parallel-processes.log')
 if a.rtsp:run([sys.executable,'tests/rtsp/run_tests.py','--out',out/'rtsp'],'rtsp.log',dict(os.environ,CC=a.cc))
 if a.ffmpeg or a.ffprobe:
  if not (a.ffmpeg and a.ffprobe):raise ValueError('Supply both FFmpeg tools')
  run([sys.executable,'tests/test_owned_interop.py','--binary',out/'native/divinus-owned-fixture','--out',out/'interop','--ffmpeg',a.ffmpeg,'--ffprobe',a.ffprobe],'interop.log')
 if a.mips_cc:
  if not a.toolchain_pin:raise ValueError('Explicit cross toolchain pin required')
  pin=json.loads(a.toolchain_pin.read_text());cc=a.mips_cc.absolute() # Preserve compiler symlink basename: wrapper uses argv[0].
  tools={'wrapper':cc,'compiler':pathlib.Path(str(cc)+'.br_real')}
  for key in ('cc1','ld'):tools[key]=pathlib.Path(subprocess.check_output([str(cc),'-print-prog-name='+key],text=True).strip())
  for key,path in tools.items():assert sha(path)==pin[key],key
  results['toolchain']={k:{'path':str(v),'sha256':sha(v)} for k,v in tools.items()}
  run([cc,'--version'],'mips-compiler.txt');scan(cc,'mips-dependencies.d',['-static'])
  run(['make','-C','src','divinus-owned','CC='+str(cc),'OWNED_OUT='+str(out/'mips'),'OWNED_OPT=-static'],'mips-build.log')
  prefix=str(cc).removesuffix('gcc');elf=run([prefix+'readelf','-h','-l','-A',out/'mips/divinus-owned'],'mips-elf.txt')
  assert all(x in elf for x in ('ELF32','little endian','MIPS32r2','Soft float','o32'))
  assert 'INTERP' not in elf and 'DYNAMIC' not in elf
  symbols=run([prefix+'nm',out/'mips/divinus-owned'],'mips-symbols.txt')
  assert not any(s in symbols for s in (' hal_identify',' sdk_start',' sdk_stop',' request_idr',' owned_test_device_',' __wrap_pthread_join'))
  assert 'nt98513_capture_session_posix' in symbols and 'rtsp_owned_start' in symbols
  assert (out/'native/owned_build_id.h').read_bytes()==(out/'mips/owned_build_id.h').read_bytes()
 results['passed']=True
finally:
 results['inputs']={n:sha(ROOT/n) for n in json.loads((ROOT/'tools/owned_sources.json').read_text())['files']}
 results['outputs']={str(f.relative_to(out)):sha(f) for f in sorted(out.rglob('*')) if f.is_file()}
 (out/'receipt.json').write_text(json.dumps(results,sort_keys=True,indent=2)+'\n')
print(out/'receipt.json')
