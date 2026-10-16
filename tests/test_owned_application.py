#!/usr/bin/env python3
"""Real foreground main + RTSP host integration; only capture uses fixture IO.
All listeners are newly-created 127.0.0.1:0, no camera/external network access.
"""
import base64, os, pathlib, re, select, signal, socket, struct, subprocess, sys, tempfile, time
binary=pathlib.Path(sys.argv[1]).resolve()
def line(p,token,timeout=8):
 end=time.monotonic()+timeout;seen=[]
 while time.monotonic()<end:
  if select.select([p.stderr],[],[],.1)[0]:
   b=p.stderr.readline();seen.append(b)
   if token in b:return b
   if not b:break
 raise AssertionError((token,seen,p.poll()))
def response(s,request):
 s.sendall(request);data=b''
 while b'\r\n\r\n' not in data:data+=s.recv(8192)
 head,body=data.split(b'\r\n\r\n',1);length=0
 for v in head.split(b'\r\n'):
  if v.lower().startswith(b'content-length:'):length=int(v.split(b':')[1])
 while len(body)<length:body+=s.recv(8192)
 return head+b'\r\n\r\n'+body
with tempfile.TemporaryDirectory(prefix='divinus-app-') as td:
 cfg=pathlib.Path(td)/'owned.conf';cfg.write_text('bind=127.0.0.1\nport=0\nuser=owner\npassword=host-test-only-secret\ntimestamp=linux-monotonic-arrival\n');cfg.chmod(0o600)
 r,w=os.pipe();env=dict(os.environ,OWNED_FIXTURE_FD=str(r))
 p=subprocess.Popen([str(binary),'--input','nt98513-owned','--config',str(cfg)],env=env,pass_fds=(r,),stderr=subprocess.PIPE,stdout=subprocess.DEVNULL,bufsize=0);os.close(r)
 try:
  os.write(w,struct.pack('<II',1,0));msg=line(p,b'LISTEN ');port=int(msg.split(b'port=')[1].split()[0]);assert port>0
  s=socket.create_connection(('127.0.0.1',port),timeout=3);s.settimeout(3)
  req=b'DESCRIBE rtsp://127.0.0.1/video RTSP/1.0\r\nCSeq: 1\r\n\r\n';assert b'401 Unauthorized' in response(s,req)
  auth=b'Authorization: Basic '+base64.b64encode(b'owner:host-test-only-secret')+b'\r\n'
  good=b'DESCRIBE rtsp://127.0.0.1/video RTSP/1.0\r\nCSeq: 2\r\n'+auth+b'\r\n'
  desc=response(s,good);assert b'200 OK' in desc and b'sprop-parameter-sets=' in desc
  malformed=good.replace(auth,auth.rstrip(b'\r\n')+b'junk\r\n');assert b'401 Unauthorized' in response(s,malformed)
  setup=response(s,b'SETUP rtsp://127.0.0.1/video/track=0 RTSP/1.0\r\nCSeq: 3\r\n'+auth+b'Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n');assert b'200 OK' in setup
  session=re.search(rb'Session: ([0-9a-fA-F]+)',setup).group(1)
  play=response(s,b'PLAY rtsp://127.0.0.1/video RTSP/1.0\r\nCSeq: 4\r\n'+auth+b'Session: '+session+b'\r\n\r\n');assert b'200 OK' in play
  os.write(w,struct.pack('<II',1,0));wire=b'';packets=[]
  while len(packets)<4:
   wire+=s.recv(8192)
   while len(wire)>=4:
    assert wire[0]==36;size=int.from_bytes(wire[2:4],'big')
    if len(wire)<size+4:break
    if wire[1]==0:packets.append(wire[4:4+size])
    wire=wire[size+4:]
  fixture=pathlib.Path(__file__).with_name('owned_h264_fixture.h').read_text()
  raw=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',fixture))
  for packet,(off,n) in zip(packets,[(4,20),(28,6),(37,605),(645,504)]):assert packet[0]==0x80 and packet[1]&127==96 and packet[12:]==raw[off:off+n]
  # One authenticated playing peer resets its TCP connection. Healthy peer
  # must continue receiving; no application exit or capture restart is allowed.
  bad=socket.create_connection(('127.0.0.1',port),timeout=3);bad.settimeout(3)
  bsetup=response(bad,b'SETUP rtsp://127.0.0.1/video/track=0 RTSP/1.0\r\nCSeq: 5\r\n'+auth+b'Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n')
  bsess=re.search(rb'Session: ([0-9a-fA-F]+)',bsetup).group(1)
  assert b'200 OK' in response(bad,b'PLAY rtsp://127.0.0.1/video RTSP/1.0\r\nCSeq: 6\r\n'+auth+b'Session: '+bsess+b'\r\n\r\n')
  bad.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0));bad.close()
  os.write(w,struct.pack('<II',1,0));wire=b'';got=0
  while got<4:
   wire+=s.recv(8192)
   while len(wire)>=4:
    assert wire[0]==36;size=int.from_bytes(wire[2:4],'big')
    if len(wire)<size+4:break
    if wire[1]==0:got+=1
    wire=wire[size+4:]
  assert p.poll() is None
  os.write(w,struct.pack('<II',2,0));msg=line(p,b'LISTEN ');newport=int(msg.split(b'port=')[1].split()[0]);assert s.recv(1)==b'';s.close()
  s=socket.create_connection(('127.0.0.1',newport),timeout=3);s.settimeout(3);desc2=response(s,good);assert desc2!=desc and b'200 OK' in desc2
  p.send_signal(signal.SIGTERM);assert p.wait(timeout=5)==0;assert s.recv(1)==b'';s.close()
 finally:
  os.close(w)
  if p.poll() is None:p.kill();p.wait()
 # Stop while awaiting the first record: no prepared server or listener.
 r,w=os.pipe();q=subprocess.Popen([str(binary),'--input','nt98513-owned','--config',str(cfg)],env=dict(os.environ,OWNED_FIXTURE_FD=str(r)),pass_fds=(r,),stderr=subprocess.PIPE,stdout=subprocess.DEVNULL,bufsize=0);os.close(r)
 line(q,b'STARTING');q.send_signal(signal.SIGINT);assert q.wait(timeout=5)==0;tail=q.stderr.read();assert b'LISTEN' not in tail;os.close(w)
 # Real session read error after listening propagates to process status.
 r,w=os.pipe();q=subprocess.Popen([str(binary),'--input','nt98513-owned','--config',str(cfg)],env=dict(os.environ,OWNED_FIXTURE_FD=str(r)),pass_fds=(r,),stderr=subprocess.PIPE,stdout=subprocess.DEVNULL,bufsize=0);os.close(r)
 os.write(w,struct.pack('<II',1,0));line(q,b'LISTEN');os.write(w,struct.pack('<II',1,1));assert q.wait(timeout=5)!=0;assert b'FAILED' in q.stderr.read();os.close(w)
 # Unsupported options must fail before capture open/listen.
 cfg.write_text(cfg.read_text()+'audio=true\n')
 q=subprocess.run([str(binary),'--input','nt98513-owned','--config',str(cfg)],capture_output=True,timeout=3);assert q.returncode!=0 and b'LISTEN' not in q.stderr
print('production main/authenticated RTSP/generation reset/signal join passed')
