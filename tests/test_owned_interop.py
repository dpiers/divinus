"""Bounded localhost-only FFmpeg smoke test using exact authored device fixture.
Runs a supplied newly-built host application; never contacts a camera.
"""
from pathlib import Path
import argparse,hashlib,json,os,queue,re,signal,struct,subprocess,threading,time
parser=argparse.ArgumentParser();parser.add_argument('--binary',required=True,type=Path);parser.add_argument('--out',required=True,type=Path);parser.add_argument('--ffmpeg',default='ffmpeg');parser.add_argument('--ffprobe',default='ffprobe');args=parser.parse_args()
OUT=args.out.resolve();OUT.mkdir(parents=True,exist_ok=False)
BIN=args.binary.resolve();SRC=Path(__file__).resolve().parents[1];FFMPEG=args.ffmpeg;FFPROBE=args.ffprobe
commands=[];results={}
def command(name,argv,timeout=12):
 started=time.monotonic();p=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:stdout,stderr=p.communicate(timeout=timeout);timedout=False
 except subprocess.TimeoutExpired:p.kill();stdout,stderr=p.communicate();timedout=True
 (OUT/(name+'.stdout')).write_bytes(stdout);(OUT/(name+'.stderr')).write_bytes(stderr)
 row={'name':name,'argv':argv,'exit':p.returncode,'timeout':timedout,'elapsed_seconds':time.monotonic()-started};commands.append(row)
 return row,stdout,stderr
for tool in [FFMPEG,FFPROBE]:
 row,stdout,stderr=command(Path(tool).name+'-version',[tool,'-version']);assert row['exit']==0
raw=bytes(int(x,16)for x in re.findall(r'0x([0-9a-f]{2})',(SRC/'tests/owned_h264_fixture.h').read_text()))
fixture=OUT/'authored.h264';fixture.write_bytes(raw)
row,stdout,stderr=command('direct-probe',[FFPROBE,'-v','error','-show_streams','-of','json',str(fixture)]);assert row['exit']==0
results['direct_probe']=json.loads(stdout)
row,stdout,stderr=command('direct-decode',[FFMPEG,'-v','error','-i',str(fixture),'-map','0:v:0','-frames:v','1','-pix_fmt','yuv420p','-f','framehash','-hash','sha256','-']);assert row['exit']==0
expected=[line.split(b',')[-1].strip().decode()for line in stdout.splitlines()if line and not line.startswith(b'#')];assert len(expected)==1
results['direct_frame_sha256']=expected[0]
cfg=OUT/'synthetic.conf';cfg.write_text('bind=127.0.0.1\nport=0\nuser=owner\npassword=host-test-only-secret\ntimestamp=linux-monotonic-arrival\n');cfg.chmod(0o600)
r,w=os.pipe();os.set_blocking(w,False);env=dict(os.environ,OWNED_FIXTURE_FD=str(r));log=open(OUT/'application.stderr','wb');lines=queue.Queue();stop=threading.Event()
app=subprocess.Popen([str(BIN),'--input','nt98513-owned','--config',str(cfg)],env=env,pass_fds=(r,),stdout=subprocess.PIPE,stderr=subprocess.PIPE);os.close(r)
commands.append({'name':'application','argv':[str(BIN),'--input','nt98513-owned','--config',str(cfg)],'fixture_boundary':'device open/read/ioctl only','pid':app.pid})
def drain():
 for line in iter(app.stderr.readline,b''):log.write(line);log.flush();lines.put(line)
def feed():
 while not stop.is_set():
  try:os.write(w,struct.pack('<II',1,0))
  except BlockingIOError:pass
  except BrokenPipeError:return
  stop.wait(.1)
reader=threading.Thread(target=drain);reader.start();feeder=threading.Thread(target=feed);feeder.start()
try:
 deadline=time.monotonic()+8;port=None
 while time.monotonic()<deadline:
  try:line=lines.get(timeout=.1)
  except queue.Empty:continue
  m=re.search(rb'LISTEN port=(\d+)',line)
  if m:port=int(m.group(1));break
 assert port and app.poll()is None,'application did not start'
 results['owned_listener']={'bind':'127.0.0.1','port':port,'generation':1}
 url=f'rtsp://owner:host-test-only-secret@127.0.0.1:{port}/video'
 for name,password in [('bad-password','host-test-only-secretX'),('bad-password-case','Host-test-only-secret')]:
  bad=f'rtsp://owner:{password}@127.0.0.1:{port}/video'
  row,stdout,stderr=command(name,[FFPROBE,'-v','trace','-rtsp_transport','tcp','-timeout','3000000','-show_streams','-of','json',bad])
  results[name]={'rejected':row['exit']!=0 and b'401' in stderr,'exit':row['exit']};assert results[name]['rejected'],name
 row,stdout,stderr=command('rtsp-probe',[FFPROBE,'-v','trace','-rtsp_transport','tcp','-timeout','3000000','-read_intervals','%+#3','-show_packets','-show_streams','-of','json',url])
 results['rtsp_probe_exit']=row['exit'];assert row['exit']==0,stderr[-2000:]
 results['rtsp_probe']=json.loads(stdout)
 for name in ['rtsp-decode','rtsp-reconnect-decode']:
  row,stdout,stderr=command(name,[FFMPEG,'-v','trace','-rtsp_transport','tcp','-timeout','3000000','-i',url,'-map','0:v:0','-frames:v','3','-fps_mode','passthrough','-pix_fmt','yuv420p','-f','framehash','-hash','sha256','-'])
  hashes=[line.split(b',')[-1].strip().decode()for line in stdout.splitlines()if line and not line.startswith(b'#')]
  results[name]={'exit':row['exit'],'decoded_hashes':hashes,'matches_direct':hashes==expected*3}
  assert row['exit']==0 and hashes==expected*3,(name,stderr[-2000:])
  assert app.poll()is None
 results['status']='PASS_OWNED_LOOPBACK_AUTH_PROBE_DECODE_RECONNECT'
finally:
 stop.set();feeder.join(timeout=2)
 if app.poll()is None:app.send_signal(signal.SIGTERM)
 try:app.wait(timeout=5)
 except subprocess.TimeoutExpired:app.kill();app.wait();results['forced_application_kill']=True
 os.close(w);reader.join(timeout=2);log.close();results['application_exit']=app.returncode
 (OUT/'application.stdout').write_bytes(app.stdout.read());(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
assert results['application_exit']==0,results
print(json.dumps(results))
