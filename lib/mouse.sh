# a mouse: reverse mouse wheel only, trackpad kept - negate 11/12 only, os derives px; 88=trackpad
P=/tmp/a_mouse
pkill -x a_mouse&&{ echo ✓ OFF;exit;}
[ -x $P ]||cc -w -x c -o $P - -framework ApplicationServices <<'EOF'
#include <CoreGraphics/CoreGraphics.h>
#define F(f) CGEventGetIntegerValueField(e,f)
#define S(f) CGEventSetIntegerValueField(e,f,-F(f))
CFMachPortRef T;
CGEventRef cb(CGEventTapProxy p,CGEventType t,CGEventRef e,void*u){if(t>=0xFFFFFFFE)CGEventTapEnable(T,1);else if(!F(88)){S(11);S(12);}return e;}
int main(){T=CGEventTapCreate(1,0,0,1<<22,cb,0);if(!T)return 1;CFRunLoopAddSource(CFRunLoopGetMain(),CFMachPortCreateRunLoopSource(0,T,0),kCFRunLoopDefaultMode);CFRunLoopRun();}
EOF
nohup $P &>/dev/null &sleep .2
pgrep -xq a_mouse&&echo ✓ on||echo x need Accessibility
