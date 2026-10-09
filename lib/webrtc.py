# experimental, preliminary — a webrtc o|a CODE [chrome]: p2p DataChannel echo between two devices, SAME code required on both ends (else channel refused); stdio carries only the 2 SDP lines — pipe them over ssh (pair recipe: wastebook 20261008T222335-webrtc-echo-code.py)
import sys,json;from playwright.sync_api import sync_playwright as S
a=[x for x in sys.argv[1:]if x!='webrtc']+[None]*3;o=a[0]=='o';C=a[1]or'TEST'
if a[0]not in('o','a'):sys.exit('a webrtc o|a CODE [chrome] — o on one device, a on the other, SAME code both ends; pipe stdio over ssh')
E=lambda m:print(('HOST| 'if o else'PEER| ')+m,file=sys.stderr,flush=True)
J='''async([o,s])=>{self.p||(O=o,p=new RTCPeerConnection({iceServers:[{urls:'stun:stun.cloudflare.com'}]}),dp=new Promise(r=>o?r(p.createDataChannel('')):p.ondatachannel=e=>r(e.channel)))
if(s)await p.setRemoteDescription(s);if(!o||!s)return p.setLocalDescription(),await new Promise(r=>setTimeout(r,1e3)),p.localDescription.toJSON()}'''
K="""async c=>{const d=await dp,q=[];let w;d.onmessage=e=>{q.push(e.data);w&&w()}
const rx=async()=>{while(!q.length)await new Promise(x=>w=x);return q.shift()}
await new Promise(x=>d.readyState=='open'?x():d.onopen=x)
d.send('c:'+c);if(await rx()!='c:'+c)return['CODE MISMATCH - peer entered a different code, channel refused']
const L=['code '+c+' CONFIRMED by both ends']
if(O){const t=performance.now();d.send('hello over webrtc');L.push('peer echoed "'+await rx()+'" in '+(performance.now()-t).toFixed(0)+'ms')}
else{const m=await rx();d.send(m+' [echoed]');L.push('got "'+m+'" - echoed it back')}
try{const s=await p.getStats(),n=[...s.values()].find(x=>x.nominated),r=s.get(n.remoteCandidateId)
L.push('path: '+r.candidateType+' '+r.address+' udp - p2p, pipe carried only 2 SDP lines')}catch(e){L.push('path: n/a')}
return L}"""
E('code '+C+' - the SAME code must be entered on the other end')
with S()as pw:
 g=pw.chromium.launch(**{'executable_path'if(a[2]or'')[:1]=='/'else'channel':a[2]}).new_page()
 if o:print(json.dumps(g.evaluate(J,[1,0])),flush=True);g.evaluate(J,[1,json.loads(input())])
 else:print(json.dumps(g.evaluate(J,[0,json.loads(input())])),flush=True)
 for l in g.evaluate(K,C):E(l)
 g.wait_for_timeout(2000)
