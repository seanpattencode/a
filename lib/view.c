/* ── gui: visual interaction. Priority: terminal>key>uia>fan>CDP ──
   macOS: click/key require Accessibility permission or events silently fail.
   User must enable: System Settings → Privacy & Security → Accessibility → add Terminal.
   LLM: if click() has no effect, tell user to grant Accessibility permission. */
static int vwsl(void){static int r=-1;if(r<0){char v[8];pcmd("grep -ci microsoft /proc/version 2>/dev/null",v,8);r=atoi(v)>0;}return r;}
static int vps(const char*s,char*o,int n){writef("/mnt/c/tmp/v_cmd.ps1",s);
    return pcmd("powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:/tmp/v_cmd.ps1",o,n);}
static void vgrid(int x,int y,int w,int h,const char*p,int z,int*nx,int*ny,int*nw,int*nh){
    int fx=50,fy=50;char a=p[0],b=p[1]?p[1]:0;
    if(a=='T')fy=25;if(a=='B'&&b!='A')fy=75;if(b=='L'||a=='L')fx=25;if(b=='R'||a=='R')fx=75;
    *nw=w*z/100;*nh=h*z/100;if(*nw<60)*nw=60;if(*nh<60)*nh=60;
    *nx=x+w*fx/100-*nw/2;*ny=y+h*fy/100-*nh/2;if(*nx<0)*nx=0;if(*ny<0)*ny=0;}
static void vcrop(const char*td,int x,int y,int w,int h,const char*out){char c[B];
    if(vwsl()){snprintf(c,B,"Add-Type -AssemblyName System.Drawing\n$b=[Drawing.Bitmap]::FromFile('C:/tmp/v_full.png')\n"
        "$c=$b.Clone((New-Object Drawing.Rectangle %d,%d,%d,%d),$b.PixelFormat)\n$g=[Drawing.Graphics]::FromImage($c);"
        "$g.FillEllipse([Drawing.Brushes]::Red,($c.Width/2-4),($c.Height/2-4),8,8)\n"
        "$g.Dispose();$c.Save('C:/tmp/%s');$b.Dispose();$c.Dispose()\n",x,y,w,h,out);vps(c,NULL,0);
    }else{snprintf(c,B,"convert %s/v_full.png -crop %dx%d+%d+%d -fill red -draw 'circle %d,%d %d,%d' %s/%s 2>/dev/null",
        td,w,h,x,y,w/2,h/2,w/2+4,h/2,td,out);system(c);}}
static void vjoin(char*out,int sz,int argc,char**argv,int from){out[0]=0;
    for(int i=from;i<argc;i++){int l=(int)strlen(out);snprintf(out+l,sz-l,"%s%s",l?" ":"",argv[i]);}}

/* CDP: execute JS in browser launched with --remote-debugging-port=9222 */
static int vcdp(const char*js,char*out,int osz){
    writef("/mnt/c/tmp/v_js.txt",js); /* write JS to file, avoids all escaping */
    if(vwsl()){return vps(
        "$t=Invoke-RestMethod 'http://localhost:9222/json'\n"
        "$sw=Get-Content C:/tmp/v_cdp_ws.txt -EA 0\n"
        "if($sw){$p=$t|Where-Object{$_.webSocketDebuggerUrl -eq $sw}|Select-Object -First 1}\n"
        "if(-not $p){$p=$t|Where-Object{$_.type -eq 'page' -and $_.url -notmatch 'edge://|chrome://'}|Select-Object -Last 1}\n"
        "if(-not $p){$p=$t|Where-Object{$_.type -eq 'page'}|Select-Object -First 1}\n"
        "$p.webSocketDebuggerUrl|Set-Content C:/tmp/v_cdp_ws.txt\n"
        "$ws=New-Object Net.WebSockets.ClientWebSocket\n"
        "$ws.ConnectAsync([uri]$p.webSocketDebuggerUrl,[Threading.CancellationToken]::None).Wait()\n"
        "$js=(Get-Content C:/tmp/v_js.txt -Raw) -replace '\"','\\\"'\n"
        "$m='{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":\"'+$js+'\"}}'  \n"
        "$b=[Text.Encoding]::UTF8.GetBytes($m)\n"
        "$ws.SendAsync([ArraySegment[byte]]::new($b),[Net.WebSockets.WebSocketMessageType]::Text,$true,[Threading.CancellationToken]::None).Wait()\n"
        "$buf=New-Object byte[] 65536\n"
        "$r=$ws.ReceiveAsync([ArraySegment[byte]]::new($buf),[Threading.CancellationToken]::None).Result\n"
        "Write-Output([Text.Encoding]::UTF8.GetString($buf,0,$r.Count))\n"
        "$ws.CloseAsync([Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'',[Threading.CancellationToken]::None).Wait()\n",out,osz);
    }else{char c[B];snprintf(c,B,"python3 -c \""
        "import json;from websocket import create_connection;"
        "t=json.loads(__import__('urllib.request',fromlist=['urlopen']).urlopen('http://localhost:9222/json').read());"
        "ws=create_connection(t[0]['webSocketDebuggerUrl']);"
        "ws.send(json.dumps({'id':1,'method':'Runtime.evaluate','params':{'expression':open('/tmp/v_js.txt').read()}}));"
        "print(ws.recv());ws.close()\" 2>&1");
        return pcmd(c,out,osz);}}

static int cmd_gui(int argc,char**argv){AB;perf_disarm();
    const char*s=argc>2?argv[2]:NULL,*td=vwsl()?"/mnt/c/tmp":"/tmp";
    if(!s){puts("a gui — visual interaction (priority: terminal>key>uia>fan>CDP)\n"
        "  shot  fan X Y W H [Z]  grid X Y W H P Z  crop X Y W H\n"
        "  click X Y  key \"text\"  uia \"name\"  open URI\n"
        "  launch       start browser with CDP (port 9222)\n"
        "  js \"code\"    execute JS in browser via CDP\n"
        "  nav URL      navigate browser to URL");return 0;}
    if(*s=='s'&&s[1]=='h'){char o[256];mkdirp((char*)td); /* shot */
        if(vwsl()){vps("Add-Type -AssemblyName System.Windows.Forms,System.Drawing\n$s=[Windows.Forms.Screen]::PrimaryScreen.Bounds\n"
            "$b=New-Object Drawing.Bitmap $s.Width,$s.Height\n[Drawing.Graphics]::FromImage($b).CopyFromScreen(0,0,0,0,$b.Size)\n"
            "$b.Save('C:/tmp/v_full.png');$b.Dispose()\nWrite-Output \"$($s.Width) $($s.Height)\"\n",o,256);
            system("cp /mnt/c/tmp/v_full.png /mnt/c/tmp/v.png");
#ifdef __APPLE__
        }else{pcmd("screencapture -x /tmp/v_full.png;cp /tmp/v_full.png /tmp/v.png;"
            "sips -g pixelWidth -g pixelHeight /tmp/v_full.png 2>/dev/null|awk '/pixel/{printf $2\" \"}'",o,256);
#else
        }else{pcmd("scrot /tmp/v_full.png 2>/dev/null||grim /tmp/v_full.png 2>/dev/null;"
            "cp /tmp/v_full.png /tmp/v.png;identify -format '%w %h' /tmp/v_full.png 2>/dev/null",o,256);
#endif
        }printf("%s\n%s/v.png\n",o,td);return 0;}
    if(*s=='f'&&argc>=7){ /* fan */
        int x=atoi(argv[3]),y=atoi(argv[4]),w=atoi(argv[5]),h=atoi(argv[6]),z=argc>7?atoi(argv[7]):40;
        const char*GP[]={"TL","T","TR","L","C","R","BL","B","BR"};
        for(int i=0;i<9;i++){int a,b,c,d;char f[32];vgrid(x,y,w,h,GP[i],z,&a,&b,&c,&d);
            snprintf(f,32,"v_%s.png",GP[i]);vcrop(td,a,b,c,d,f);
            printf("%s: %d %d %d %d → %s/v_%s.png\n",GP[i],a,b,c,d,td,GP[i]);}return 0;}
    if(*s=='g'&&s[1]=='r'&&argc>=8){int a,b,c,d; /* grid */
        vgrid(atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),argv[7],atoi(argv[8]),&a,&b,&c,&d);
        vcrop(td,a,b,c,d,"v.png");printf("%d %d %d %d\n%s/v.png\n",a,b,c,d,td);return 0;}
    if(*s=='c'&&s[1]=='l'&&argc>=5){int x=atoi(argv[3]),y=atoi(argv[4]);char c[B]; /* click */
        if(vwsl()){snprintf(c,B,"Add-Type -MemberDefinition '\n[DllImport(\"user32.dll\")]public static extern bool SetCursorPos(int x,int y);\n"
            "[DllImport(\"user32.dll\")]public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr i);\n' -Name U -Namespace W\n"
            "[W.U]::SetCursorPos(%d,%d);Start-Sleep -m 150\n[W.U]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -m 50\n"
            "[W.U]::mouse_event(4,0,0,0,[UIntPtr]::Zero)\n",x,y);vps(c,NULL,0);
#ifdef __APPLE__
        }else{snprintf(c,B,"cliclick c:%d,%d 2>/dev/null",x,y);system(c);
#else
        }else{snprintf(c,B,"xdotool mousemove %d %d click 1 2>/dev/null||ydotool mousemove -a %d %d click 1",x,y,x,y);system(c);
#endif
        }printf("clicked (%d,%d)\n",x,y);return 0;}
    if(*s=='k'&&argc>=4){char k[512],c[B];vjoin(k,512,argc,argv,3); /* key */
        if(vwsl()){snprintf(c,B,"Add-Type -AssemblyName System.Windows.Forms\n[System.Windows.Forms.SendKeys]::SendWait('%s')\n",k);vps(c,NULL,0);
#ifdef __APPLE__
        }else{snprintf(c,B,"osascript -e 'tell app \"System Events\" to keystroke \"%s\"'",k);system(c);
#else
        }else{snprintf(c,B,"xdotool type -- '%s' 2>/dev/null||ydotool type -- '%s'",k,k);system(c);
#endif
        }printf("sent: %s\n",k);return 0;}
    if(*s=='u'&&argc>=4){if(!vwsl()){puts("uia: WSL only");return 1;} /* uia */
        char nm[512],c[B],o[64];vjoin(nm,512,argc,argv,3);
        snprintf(c,B,"Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes\n"
            "$r=[System.Windows.Automation.AutomationElement]::RootElement\n"
            "$c=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'%s')\n"
            "$b=$r.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c)\n"
            "if($b){$b.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke();'ok'}else{'not found'}\n",nm);
        vps(c,o,64);printf("%s\n",o);return 0;}
    if(*s=='o'&&argc>=4){char c[B]; /* open */
#ifdef __APPLE__
        snprintf(c,B,"open '%s'",argv[3]);
#else
        if(vwsl())snprintf(c,B,"powershell.exe -NoProfile -c \"Start-Process '%s'\"",argv[3]);
        else snprintf(c,B,"xdg-open '%s' 2>/dev/null",argv[3]);
#endif
        system(c);printf("opened: %s\n",argv[3]);return 0;}
    if(*s=='c'&&s[1]=='r'&&argc>=7){int x=atoi(argv[3]),y=atoi(argv[4]),w=atoi(argv[5]),h=atoi(argv[6]); /* crop */
        vcrop(td,x,y,w,h,"v.png");printf("%d %d\n%s/v.png\n",x+w/2,y+h/2,td);return 0;}
    if(*s=='l'){ /* launch browser with CDP */
        char c[B];
        if(vwsl()){mkdirp("/mnt/c/tmp/a_web");
            snprintf(c,B,"powershell.exe -NoProfile -c \"Start-Process msedge '--remote-debugging-port=9222 --user-data-dir=C:/tmp/a_web'\"");
#ifdef __APPLE__
        }else{snprintf(c,B,"'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' --remote-debugging-port=9222 --user-data-dir=/tmp/a_web >/dev/null 2>&1 &");
#else
        }else{snprintf(c,B,"google-chrome --remote-debugging-port=9222 --user-data-dir=/tmp/a_web >/dev/null 2>&1 &||"
            "chromium --remote-debugging-port=9222 --user-data-dir=/tmp/a_web >/dev/null 2>&1 &");
#endif
        }system(c);puts("launched CDP:9222");return 0;}
    if(!strcmp(s,"js")&&argc>=4){char js[B],o[B];vjoin(js,B,argc,argv,3); /* js eval */
        vcdp(js,o,B);printf("%s\n",o);return 0;}
    if(!strcmp(s,"nav")&&argc>=4){char c[B],o[B]; /* navigate via Page.navigate */
        snprintf(c,B,
            "$t=Invoke-RestMethod 'http://localhost:9222/json'\n"
            "$p=$t|Where-Object{$_.type -eq 'page' -and $_.url -notmatch 'edge://|chrome://'}|Select-Object -Last 1\n"
            "if(-not $p){$p=$t|Where-Object{$_.type -eq 'page'}|Select-Object -First 1}\n"
            "$p.webSocketDebuggerUrl|Set-Content C:/tmp/v_cdp_ws.txt\n"
            "$ws=New-Object Net.WebSockets.ClientWebSocket\n"
            "$ws.ConnectAsync([uri]$p.webSocketDebuggerUrl,[Threading.CancellationToken]::None).Wait()\n"
            "$m='{\"id\":1,\"method\":\"Page.navigate\",\"params\":{\"url\":\"%s\"}}'\n"
            "$b=[Text.Encoding]::UTF8.GetBytes($m)\n"
            "$ws.SendAsync([ArraySegment[byte]]::new($b),[Net.WebSockets.WebSocketMessageType]::Text,$true,[Threading.CancellationToken]::None).Wait()\n"
            "$buf=New-Object byte[] 4096;$ws.ReceiveAsync([ArraySegment[byte]]::new($buf),[Threading.CancellationToken]::None).Wait()|Out-Null\n"
            "$ws.CloseAsync([Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'',[Threading.CancellationToken]::None).Wait()\n",argv[3]);
        vps(c,o,B);printf("nav: %s\n",argv[3]);return 0;}
    if(!strcmp(s,"text")){char o[B];vcdp("document.body.innerText",o,B);printf("%s\n",o);return 0;} /* page text */
    if(!strcmp(s,"links")){char o[B]; /* clickable elements */
        vcdp("[...document.querySelectorAll('a,button,input,[role=button],[onclick]')].map((e,i)=>i+':'+e.tagName+':'+(e.textContent||e.value||e.alt||e.title||'').trim().substring(0,50)).filter(s=>s.split(':')[2]).join('\\n')",o,B);
        printf("%s\n",o);return 0;}
    if(!strcmp(s,"find")&&argc>=4){char js[B],o[B],q[512];vjoin(q,512,argc,argv,3); /* find+click by text */
        snprintf(js,B,"(()=>{let e=[...document.querySelectorAll('a,button,input,[role=button]')].find(e=>(e.textContent||e.value||'').includes('%s'));if(e){e.click();return 'clicked: '+e.textContent.trim().substring(0,50)}return 'not found'})()",q);
        vcdp(js,o,B);printf("%s\n",o);return 0;}
    printf("unknown: a gui %s\n",s);return 1;}

