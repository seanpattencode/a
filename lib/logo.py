"""a logo [size] [out] | svg — the white-on-black 'a' mark. rsvg|magick."""
import sys, subprocess as S
SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108"><rect width="108" height="108" rx="20"/><text x="54" y="78" font-family="monospace" font-size="74" fill="#fff" text-anchor="middle">a</text></svg>'
def png(n, o, s=SVG):
 try: S.run(["rsvg-convert", "-w", str(n), "-h", str(n), "-o", o], input=s.encode(), check=True, stderr=S.DEVNULL)
 except Exception: S.run(["magick", "-background", "none", "svg:-", "-resize", f"{n}x{n}", o], input=s.encode(), check=True)
 return o
if __name__ == "__main__":
 a = [x for x in sys.argv[1:] if x != "logo"]
 print(SVG if a[:1] == ['svg'] else png(int(a[0]) if a and a[0].isdigit() else 512, a[1] if len(a) > 1 else "/tmp/a-logo.png"))
