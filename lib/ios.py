# experimental
"""a ios [sim] — build+install+launch+shot the a iOS app (WKWebView on `a serve`). One file, no xcodeproj:
swiftc + hand-built bundle + codesign, 3s. UDID/IP/cert/profile auto-found. Profile: mint once in Xcode, 7-day life."""
import os,sys,glob,plistlib,subprocess as s
R=os.path.expanduser("~/a/adata/local/ios");A=R+"/aios.app";B="com.seanpatten.aios"
SW='''import SwiftUI
import WebKit
@main struct A:App{var body:some Scene{WindowGroup{V().ignoresSafeArea()}}}
struct V:UIViewRepresentable{
 func makeUIView(context:Context)->WKWebView{let w=WKWebView();w.allowsBackForwardNavigationGestures=true;w.load(URLRequest(url:URL(string:"http://HOST:1111")!));return w}
 func updateUIView(_ v:WKWebView,context:Context){}}'''
def x(c):return s.run(c,shell=1,capture_output=1,text=1).stdout.strip()
def r(c):return s.run(c,shell=1,capture_output=1).returncode
def run():
 m="sim" in sys.argv;ip=x("ipconfig getifaddr en0")or"127.0.0.1";os.makedirs(A,exist_ok=1)
 open(R+"/App.swift","w").write(SW.replace("HOST",ip))
 if r(f"swiftc -parse-as-library -O -sdk $(xcrun --sdk {'iphonesimulator' if m else 'iphoneos'} --show-sdk-path) -target arm64-apple-ios17.0{'-simulator' if m else ''} {R}/App.swift -o {A}/aios"):sys.exit("x compile")
 plistlib.dump({"CFBundleIdentifier":B,"CFBundleExecutable":"aios","CFBundleName":"a","CFBundleVersion":"1","MinimumOSVersion":"17.0","CFBundleSupportedPlatforms":["iPhoneSimulator" if m else "iPhoneOS"],"UIDeviceFamily":[1],"UILaunchScreen":{},"NSAppTransportSecurity":{"NSAllowsArbitraryLoads":True}},open(A+"/Info.plist","wb"))
 if m:
  s.run('xcrun simctl boot "iPhone 17" 2>/dev/null;open -a Simulator',shell=1)
  while r(f"xcrun simctl install booted {A}"):pass
  s.run(f"xcrun simctl launch booted {B}",shell=1);o="/tmp/aios-sim.png"
  s.run(f"sleep 3;xcrun simctl io booted screenshot {o}",shell=1,capture_output=1)
 else:
  u=x("idevice_id -l").split("\n")[0]or sys.exit("x no iPhone (usb+unlocked)")
  p=max([f for f in glob.glob(os.path.expanduser("~/Library/Developer/Xcode/UserData/Provisioning Profiles/*.mobileprovision"))if B in open(f,"rb").read().decode("latin1")],key=os.path.getmtime,default="")or sys.exit(f"x no profile for {B} — build once in Xcode-beta to mint one")
  i=x("security find-identity -v -p codesigning|awk '/Apple Development/{print $2;exit}'")or sys.exit("x no cert — Xcode-beta > Settings > Accounts")
  s.run(f'cp "{p}" {A}/embedded.mobileprovision;security cms -D -i "{p}"|plutil -extract Entitlements xml1 -o {R}/e.plist -',shell=1)
  if r(f"codesign -f -s {i} --entitlements {R}/e.plist {A}"):sys.exit("x sign")
  if r(f"xcrun devicectl device install app --device {u} {A}"):sys.exit("x install")
  if r(f"xcrun devicectl device process launch --device {u} {B}"):sys.exit("x launch denied — on phone: Settings > General > VPN & Device Management > trust developer")
  o="/tmp/aios-phone.png";s.run(f"sleep 3;xcrun devicectl device capture screenshot --device {u} --destination {o}",shell=1,capture_output=1)
 print(f"✓ a on iOS{' sim' if m else ''} → http://{ip}:1111  shot: {o}")
run()
