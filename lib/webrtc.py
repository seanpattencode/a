# experimental: approved commands from /webrtc
import sys,base64,json,subprocess as sp
if len(sys.argv)<3 or sys.argv[-2]!='run':sys.exit('Open http://localhost:1111/webrtc on both devices')
try:
 p=sp.run(['sh','-c',base64.b64decode(sys.argv[-1]).decode()],stdout=sp.PIPE,stderr=sp.STDOUT,timeout=10);r={'out':p.stdout.decode(errors='replace')[:1000],'exit':p.returncode}
except Exception as e:r={'out':str(e),'exit':1}
print(json.dumps(r,ensure_ascii=False))
