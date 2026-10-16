#!/usr/bin/env python3
"""Independent make processes, same identity and flags, distinct real products."""
import hashlib,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(sys.argv[1]).resolve();cc=sys.argv[2]
def identities():return {n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ('divinus-owned','divinus-owned-fixture','owned_build_id.h')}
before=identities();processes=[]
for target in ('divinus-owned','divinus-owned-fixture'):
 log=(out/(target+'-concurrent.log')).open('w')
 p=subprocess.Popen(['make','-C',str(root/'src'),target,'CC='+cc,'OWNED_OUT='+str(out)],stdout=log,stderr=subprocess.STDOUT)
 processes.append((p,log))
for p,log in processes:
 try:assert p.wait(timeout=60)==0
 finally:log.close()
assert identities()==before,'concurrent same-flags rebuild changed real product/header identities'
print('independent production/fixture make processes: identical header and binary hashes')
