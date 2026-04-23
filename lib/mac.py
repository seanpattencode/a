#!/opt/homebrew/bin/python3
"""macOS GUI automation. click/key/type need Accessibility (System Settings → Privacy → Accessibility).
Fan-out: screenshot → crop → aim (crosshair+dot) → LLM confirms on target → click. Never guess coords blind."""
import subprocess, sys, time
from Quartz import (CGEventCreateMouseEvent as _M, CGEventCreateKeyboardEvent as _K,
    CGEventPost as _P, CGEventSetFlags, CGEventKeyboardSetUnicodeString as _KS,
    kCGEventLeftMouseDown as _D, kCGEventLeftMouseUp as _U, kCGEventMouseMoved as _V,
    kCGHIDEventTap as _T, CGPointMake, CGMainDisplayID, CGDisplayCopyDisplayMode,
    CGDisplayModeGetPixelWidth, CGDisplayModeGetWidth,
    CGImageSourceCreateWithURL, CGImageSourceCreateImageAtIndex, CGBitmapContextCreate,
    CGBitmapContextCreateImage, CGImageDestinationCreateWithURL, CGImageDestinationAddImage,
    CGImageDestinationFinalize, CGContextSetRGBFillColor, CGContextFillEllipseInRect,
    CGContextDrawImage, CGContextFillRect, kCGImageAlphaPremultipliedLast, CGRectMake,
    CGImageGetWidth, CGImageGetHeight, CGImageGetColorSpace)
from Foundation import CFURLCreateWithFileSystemPath as _U2, kCFURLPOSIXPathStyle as _PS, kCFAllocatorDefault as _AD

def _scale(): m=CGDisplayCopyDisplayMode(CGMainDisplayID()); return CGDisplayModeGetPixelWidth(m)/CGDisplayModeGetWidth(m)
def _url(p): return _U2(_AD, p, _PS, False)

def screenshot(p='/tmp/mac_screen.png'): subprocess.run(['screencapture','-x',p],check=True); return p
def activate(app): subprocess.run(['osascript','-e',f'tell application "{app}" to activate'],check=True); time.sleep(0.3)

def click(x, y, pause=0.05):
    p = CGPointMake(x, y)
    for t in (_V, _D, _U): _P(_T, _M(None, t, p, 0)); time.sleep(pause)

def type_text(s, delay=0.02):
    for ch in s:
        for d in (True, False): e = _K(None, 0, d); _KS(e, 1, ch); _P(_T, e)
        time.sleep(delay)

_KEYS = {'return':36,'tab':48,'escape':53,'delete':51,'space':49,'up':126,'down':125,'left':123,'right':124}
def key(name):
    c = _KEYS.get(name.lower(), 0)
    for d in (True, False): _P(_T, _K(None, c, d)); time.sleep(0.02)

def select_all():
    e = _K(None, 0, True); CGEventSetFlags(e, 1<<20); _P(_T, e)
    _P(_T, _K(None, 0, False)); time.sleep(0.05)

def fill(x, y, text): click(x, y); time.sleep(0.1); select_all(); type_text(text)

def aim(x, y, path='/tmp/mac_screen.png', out='/tmp/mac_aim.png', r=12):
    """Green crosshair+dot at logical (x,y) on PNG. Review before click."""
    s = _scale(); px, py, rr = int(x*s), int(y*s), int(r*s)
    img = CGImageSourceCreateImageAtIndex(CGImageSourceCreateWithURL(_url(path), None), 0, None)
    w, h = CGImageGetWidth(img), CGImageGetHeight(img)
    ctx = CGBitmapContextCreate(None, w, h, 8, w*4, CGImageGetColorSpace(img), kCGImageAlphaPremultipliedLast)
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img)
    CGContextSetRGBFillColor(ctx, 0, 1, 0, 0.7)
    CGContextFillRect(ctx, CGRectMake(0, h-py-1, w, 3)); CGContextFillRect(ctx, CGRectMake(px-1, 0, 3, h))
    CGContextSetRGBFillColor(ctx, 0, 1, 0, 1); CGContextFillEllipseInRect(ctx, CGRectMake(px-rr, h-py-rr, rr*2, rr*2))
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1); CGContextFillEllipseInRect(ctx, CGRectMake(px-4, h-py-4, 8, 8))
    d = CGImageDestinationCreateWithURL(_url(out), 'public.png', 1, None)
    CGImageDestinationAddImage(d, CGBitmapContextCreateImage(ctx), None); CGImageDestinationFinalize(d)
    return out

def crop(path, x, y, w, h, out='/tmp/mac_crop.png'):
    """Crop PNG region at logical (x,y,w,h)."""
    s = _scale()
    subprocess.run(['sips','-c',str(int(h*s)),str(int(w*s)),'--cropOffset',str(int(y*s)),str(int(x*s)),path,'--out',out],capture_output=True)
    return out

def check():
    """True if Accessibility granted (clicks will actually reach other apps)."""
    try: from ApplicationServices import AXIsProcessTrusted; return bool(AXIsProcessTrusted())
    except Exception: return False

def windows(app=None):
    a = f'tell application "System Events" to get {{position, size}} of every window of process "{app}"' if app else 'tell application "System Events" to get name of every process whose visible is true'
    try: return subprocess.run(['osascript','-e',a],capture_output=True,text=True,timeout=5).stdout.strip()
    except: return ''

if __name__ == '__main__':
    a = sys.argv; n = len(a)
    try:
        if n<2: raise KeyError
        c = a[1]
        if c=='check': ok=check(); print('ok' if ok else 'DENIED: add host app to Privacy → Accessibility'); sys.exit(0 if ok else 1)
        elif c=='screenshot': print(screenshot(a[2] if n>2 else '/tmp/mac_screen.png'))
        elif c=='activate':   activate(a[2])
        elif c=='click':      click(float(a[2]), float(a[3]))
        elif c=='type':       type_text(a[2])
        elif c=='key':        key(a[2])
        elif c=='fill':       fill(float(a[2]), float(a[3]), a[4])
        elif c=='aim':        print(aim(float(a[2]), float(a[3])))
        elif c=='crop':       print(crop(a[2], float(a[3]), float(a[4]), float(a[5]), float(a[6])))
        elif c=='windows':    print(windows(a[2] if n>2 else None))
        else: raise KeyError
    except (KeyError, IndexError):
        print('mac.py {check|screenshot [p]|activate app|click X Y|type T|key N|fill X Y T|aim X Y|crop P X Y W H|windows [app]}'); sys.exit(1)
