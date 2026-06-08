#!/usr/bin/env bash
# a-webview.sh [url]  — native GTK4+WebKit window on localhost:1111 (A_FRAMELESS=1 = no titlebar)
set -e
export PATH=/usr/bin:/bin GSK_RENDERER="${GSK_RENDERER:-gl}"
B=$(mktemp -d)/a-webview; trap 'rm -rf "$(dirname "$B")"' EXIT
pkg-config --exists webkitgtk-6.0 || sudo apt-get install -y libwebkitgtk-6.0-dev
gcc -xc - $(pkg-config --cflags --libs gtk4 webkitgtk-6.0) -o "$B" <<'EOF'
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <stdlib.h>
static void on_button(GtkGestureClick *g, int n, double x, double y, gpointer w) {
    guint b = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    if (b != 8 && b != 9) return;                         // mouse back / forward only
    if (b == 8) webkit_web_view_go_back(WEBKIT_WEB_VIEW(w));
    else        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(w));
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
}
int main(int c, char **v) {
    gtk_init();
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "a");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
    gtk_window_set_decorated(GTK_WINDOW(win), getenv("A_FRAMELESS") ? FALSE : TRUE);
    GtkWidget *web = webkit_web_view_new();
    GtkGesture *clk = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clk), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(clk), GTK_PHASE_CAPTURE);
    g_signal_connect(clk, "pressed", G_CALLBACK(on_button), web);
    gtk_widget_add_controller(web, GTK_EVENT_CONTROLLER(clk));
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web), c > 1 ? v[1] : "http://localhost:1111");
    gtk_window_set_child(GTK_WINDOW(win), web);
    gtk_window_present(GTK_WINDOW(win));
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect_swapped(win, "destroy", G_CALLBACK(g_main_loop_quit), loop);
    g_main_loop_run(loop);
    return 0;
}
EOF
"$B" "${1:-http://localhost:1111}"
