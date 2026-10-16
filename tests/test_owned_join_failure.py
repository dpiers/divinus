#!/usr/bin/env python3
import os,pathlib,select,signal,struct,subprocess,sys,tempfile,time
binary=pathlib.Path(sys.argv[1]).resolve()
for fault in [1,2]:
 with tempfile.TemporaryDirectory(prefix='owned-join-fault-') as td:
  cfg=pathlib.Path(td)/'owned.conf';cfg.write_text('bind=127.0.0.1\nport=0\nuser=owner\npassword=host-test-only-secret\ntimestamp=linux-monotonic-arrival\n');cfg.chmod(0o600)
  r,w=os.pipe();p=subprocess.Popen([str(binary),'--input','nt98513-owned','--config',str(cfg)],pass_fds=(r,),env=dict(os.environ,OWNED_FIXTURE_FD=str(r),OWNED_TEST_JOIN_FAIL_AT=str(fault)),stderr=subprocess.PIPE,stdout=subprocess.DEVNULL,bufsize=0);os.close(r)
  try:
   os.write(w,struct.pack('<II',1,0));deadline=time.monotonic()+8;logs=b''
   while b'LISTEN' not in logs:
    assert time.monotonic()<deadline
    if select.select([p.stderr],[],[],.1)[0]:
     q=p.stderr.readline();assert q;logs+=q
   p.send_signal(signal.SIGTERM);assert p.wait(timeout=5)==1;logs+=p.stderr.read();assert b'TERMINAL join failure' in logs
   if fault==1:assert b'STOPPED' not in logs
  finally:
   os.close(w)
   if p.poll() is None:p.kill();p.wait()
print('server/capture join failure => terminal process before context invalidation')
