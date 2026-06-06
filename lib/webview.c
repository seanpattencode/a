/* a webview [url] — native GTK4+WebKit window (default localhost:1111); viewer compiled on demand, never linked into a. */
static const char WV[] =
"#include <gtk/gtk.h>\n#include <webkit/webkit.h>\n#include <stdlib.h>\n"
"static void ob(GtkGestureClick*g,int n,double x,double y,gpointer w){(void)n;(void)x;(void)y;\n"
" guint b=gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));if(b!=8&&b!=9)return;\n"
" if(b==8)webkit_web_view_go_back(WEBKIT_WEB_VIEW(w));else webkit_web_view_go_forward(WEBKIT_WEB_VIEW(w));\n"
" gtk_gesture_set_state(GTK_GESTURE(g),GTK_EVENT_SEQUENCE_CLAIMED);}\n"
"int main(int c,char**v){gtk_init();GtkWidget*win=gtk_window_new();\n"
" gtk_window_set_title(GTK_WINDOW(win),\"a\");gtk_window_set_default_size(GTK_WINDOW(win),1200,800);\n"
" gtk_window_set_decorated(GTK_WINDOW(win),getenv(\"A_FRAMELESS\")?FALSE:TRUE);\n"
" GtkWidget*web=webkit_web_view_new();GtkGesture*k=gtk_gesture_click_new();\n"
" gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(k),0);\n"
" gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(k),GTK_PHASE_CAPTURE);\n"
" g_signal_connect(k,\"pressed\",G_CALLBACK(ob),web);gtk_widget_add_controller(web,GTK_EVENT_CONTROLLER(k));\n"
" webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web),c>1?v[1]:\"http://localhost:1111\");\n"
" gtk_window_set_child(GTK_WINDOW(win),web);gtk_window_present(GTK_WINDOW(win));\n"
" GMainLoop*l=g_main_loop_new(NULL,FALSE);g_signal_connect_swapped(win,\"destroy\",G_CALLBACK(g_main_loop_quit),l);\n"
" g_main_loop_run(l);return 0;}\n";
static int cmd_webview(int c,char**v){perf_disarm();
    char b[P];snprintf(b,P,"%s/webview.bin",DDIR);
    if(access(b,X_OK)){char s[P];snprintf(s,P,"%s/webview.gen.c",DDIR);
        FILE*f=fopen(s,"w");if(!f){perror("webview");return 1;}fputs(WV,f);fclose(f);
        char m[B];snprintf(m,B,"PATH=/usr/bin:/bin gcc '%s' $(/usr/bin/pkg-config --cflags --libs gtk4 webkitgtk-6.0) -o '%s' 2>&1",s,b);
        if(system(m)){puts("x webview: need libwebkitgtk-6.0-dev");return 1;}}
    (void)setenv("GSK_RENDERER","gl",0);
    pid_t p=fork();if(p<0){perror("fork");return 1;}
    if(!p){setsid();int d=open("/dev/null",O_WRONLY);if(d>=0){(void)dup2(d,1);(void)dup2(d,2);if(d>2)close(d);}
        execl(b,"a-webview",c>2?v[2]:"http://localhost:1111",(char*)NULL);_exit(127);}
    return 0;}
