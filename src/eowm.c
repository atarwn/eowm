/* eowm - eet owter winvow manade */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>

#define LENGTH(X) (sizeof(X) / sizeof(X[0]))
#define NUM_WS 9
#define CLEANMASK(mask) (mask & ~(LockMask | Mod2Mask))

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int ws, isfullscreen, ishidden, isfloating;
    Client *next;
};

typedef struct StrutWindow StrutWindow;
struct StrutWindow {
    Window win;
    long struts[4];
    StrutWindow *next;
};

typedef struct Monitor Monitor;
struct Monitor {
    int x, y, w, h, num;
    Monitor *next;
};

typedef union {
    int i;
    const char *cmd;
} Arg;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

static Display *dpy;
static Window root;
static Client *focused, *workspaces[NUM_WS], *last_focused[NUM_WS];
static Monitor *monitors, *current_monitor;
static StrutWindow *strut_windows;
static int screen, sw, sh, current_ws, monitor_count;
static int global_struts[4];
static double master_size;
static unsigned long border_normal, border_focused;
static Atom net_wm_strut, net_wm_strut_partial, net_wm_window_type;

static void focus_monitor(const Arg *arg);
static void movewin_to_monitor(const Arg *arg);
static void killclient(const Arg *arg);
static void togglemaster(const Arg *arg);
static void incmaster(const Arg *arg);
static void decmaster(const Arg *arg);
static void nextwin(const Arg *arg);
static void prevwin(const Arg *arg);
static void movewin(const Arg *arg);
static void switchws(const Arg *arg);
static void movewin_to_ws(const Arg *arg);
static void fullscreen(const Arg *arg);
static void quit(const Arg *arg);
static void spawn(const Arg *arg);

#include "config.h"

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static unsigned long getcolor(const char *hex) {
    Colormap cmap = DefaultColormap(dpy, screen);
    XColor color;
    return (XParseColor(dpy, cmap, hex, &color) && XAllocColor(dpy, cmap, &color)) ? color.pixel : 0;
}

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static int xerror(Display *dpy, XErrorEvent *ee) {
    char msg[256];
    XGetErrorText(dpy, ee->error_code, msg, sizeof(msg));
    fprintf(stderr, "X Error: %s\n", msg);
    return 0;
}

static void update_struts(void) {
    memset(global_struts, 0, sizeof(global_struts));
    for (StrutWindow *s = strut_windows; s; s = s->next)
        for (int i = 0; i < 4; i++)
            if (s->struts[i] > global_struts[i])
                global_struts[i] = s->struts[i];
}

static void remove_strut_window(Window win) {
    for (StrutWindow **p = &strut_windows; *p; p = &(*p)->next) {
        if ((*p)->win == win) {
            StrutWindow *tmp = *p;
            *p = tmp->next;
            free(tmp);
            update_struts();
            return;
        }
    }
}

static int get_window_struts(Window win, long struts[4]) {
    Atom types[] = {net_wm_strut_partial, net_wm_strut};
    for (int t = 0; t < 2; t++) {
        Atom actual;
        int fmt;
        unsigned long n, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, win, types[t], 0, 4, False, XA_CARDINAL,
                               &actual, &fmt, &n, &after, &data) == Success && data) {
            if (actual == XA_CARDINAL && fmt == 32 && n >= 4) {
                int has = 0;
                for (int i = 0; i < 4; i++) {
                    struts[i] = ((long *)data)[i];
                    if (struts[i] > 0) has = 1;
                }
                XFree(data);
                return has;
            }
            XFree(data);
        }
    }
    return 0;
}

static int check_window_type(Window win, const char *type_name) {
    Atom actual;
    int fmt;
    unsigned long n, after;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(dpy, win, net_wm_window_type, 0, 1, False, XA_ATOM,
                           &actual, &fmt, &n, &after, &prop) == Success && prop) {
        int match = (*(Atom *)prop == XInternAtom(dpy, type_name, False));
        XFree(prop);
        return match;
    }
    return 0;
}

static Monitor* get_monitor_at(int x, int y) {
    for (Monitor *m = monitors; m; m = m->next)
        if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
            return m;
    return monitors;
}

static Monitor* get_monitor_for_window(Client *c) {
    return get_monitor_at(c->x + c->w / 2, c->y + c->h / 2);
}

static Monitor* cycle_monitor(Monitor *cur, int dir) {
    if (!monitors || !monitors->next) return monitors;
    if (dir > 0) return (cur && cur->next) ? cur->next : monitors;
    Monitor *m = monitors, *prev = NULL;
    while (m->next) {
        if (m->next == cur) prev = m;
        m = m->next;
    }
    return (!cur || cur == monitors) ? m : prev;
}

static void update_monitors(void) {
    while (monitors) {
        Monitor *m = monitors->next;
        free(monitors);
        monitors = m;
    }
    monitor_count = 0;
    XRRScreenResources *sr = XRRGetScreenResources(dpy, root);
    if (sr) {
        for (int i = 0; i < sr->ncrtc; i++) {
            XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]);
            if (ci && ci->noutput > 0 && ci->width > 0 && ci->height > 0) {
                Monitor *m = calloc(1, sizeof(Monitor));
                m->num = monitor_count++;
                m->x = ci->x; m->y = ci->y; m->w = ci->width; m->h = ci->height;
                m->next = monitors;
                monitors = m;
            }
            if (ci) XRRFreeCrtcInfo(ci);
        }
        XRRFreeScreenResources(sr);
    }
    if (!monitors) {
        monitors = calloc(1, sizeof(Monitor));
        monitors->w = sw;
        monitors->h = sh;
        monitor_count = 1;
    }
    current_monitor = monitors;
}

static int can_focus(Client *c) {
    XWindowAttributes wa;
    return c && !c->ishidden && c->ws == current_ws && XGetWindowAttributes(dpy, c->win, &wa);
}

static void focus(Client *c) {
    if (!can_focus(c)) return;
    if (focused && focused != c) XSetWindowBorder(dpy, focused->win, border_normal);
    focused = last_focused[current_ws] = c;
    XSetWindowBorder(dpy, c->win, border_focused);
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

static void resize(Client *c, int x, int y, int w, int h) {
    c->x = x; c->y = y; c->w = w; c->h = h;
    int bw = c->isfullscreen ? 0 : border_width;
    XSetWindowBorderWidth(dpy, c->win, bw);
    XMoveResizeWindow(dpy, c->win, x, y, w - 2 * bw, h - 2 * bw);
}

static void arrange_monitor(Monitor *mon) {
    int x0 = mon->x + global_struts[0] + padding;
    int y0 = mon->y + global_struts[2] + padding;
    int usable_w = mon->w - global_struts[0] - global_struts[1] - 2 * padding;
    int usable_h = mon->h - global_struts[2] - global_struts[3] - 2 * padding;

    int n = 0;
    Client *master = NULL;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (!c->isfloating && get_monitor_for_window(c) == mon) {
            if (!master) master = c;
            n++;
        }
    }
    if (!n) return;

    if (n == 1) {
        resize(master, x0, y0, usable_w, usable_h);
        XMapWindow(dpy, master->win);
    } else {
        int mw = (int)(usable_w * master_size);
        int stack_w = usable_w - mw - padding;
        resize(master, x0 + usable_w - mw, y0, mw, usable_h);
        XMapWindow(dpy, master->win);

        int th = usable_h / (n - 1), y = y0, stacked = 0;
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            if (c->isfloating || c == master || get_monitor_for_window(c) != mon) continue;
            stacked++;
            int h = (stacked < n - 1) ? th : (usable_h - (y - y0));
            if (h < min_window_size) h = min_window_size;
            resize(c, x0, y, stack_w, h);
            XMapWindow(dpy, c->win);
            y += h + padding;
        }
    }
}

static void arrange(void) {
    if (!workspaces[current_ws]) return;

    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->isfullscreen) {
            Monitor *m = get_monitor_for_window(c);
            XSetWindowBorderWidth(dpy, c->win, 0);
            resize(c, m->x, m->y, m->w, m->h);
            XMapWindow(dpy, c->win);
            XRaiseWindow(dpy, c->win);
            for (Client *o = workspaces[current_ws]; o; o = o->next) {
                if (o != c) {
                    o->ishidden = 1;
                    XUnmapWindow(dpy, o->win);
                }
            }
            return;
        }
    }

    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        c->ishidden = 0;
        XSetWindowBorderWidth(dpy, c->win, border_width);
        XMapWindow(dpy, c->win);
    }

    for (Monitor *m = monitors; m; m = m->next)
        arrange_monitor(m);

    for (Client *c = workspaces[current_ws]; c; c = c->next)
        if (c->isfloating) XRaiseWindow(dpy, c->win);
    if (focused) XRaiseWindow(dpy, focused->win);
}

static Client* create_client(Window win, int floating) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) return NULL;
    c->win = win;
    c->ws = current_ws;
    c->isfloating = floating;
    c->next = workspaces[current_ws];
    workspaces[current_ws] = c;

    if (current_monitor) {
        c->x = floating ? current_monitor->x + current_monitor->w / 4 : current_monitor->x;
        c->y = floating ? current_monitor->y + current_monitor->h / 4 : current_monitor->y;
        c->w = floating ? current_monitor->w / 2 : current_monitor->w;
        c->h = floating ? current_monitor->h / 2 : current_monitor->h;
    }

    XSetWindowBorderWidth(dpy, c->win, border_width);
    XSetWindowBorder(dpy, c->win, border_normal);
    XSelectInput(dpy, c->win, EnterWindowMask | LeaveWindowMask | FocusChangeMask | StructureNotifyMask);
    XMapWindow(dpy, c->win);
    return c;
}

static void removeclient(Window win) {
    for (Client **p = &workspaces[current_ws]; *p; p = &(*p)->next) {
        if ((*p)->win == win) {
            Client *c = *p;
            int was_focused = (focused == c);
            *p = c->next;
            XSelectInput(dpy, c->win, NoEventMask);
            if (last_focused[current_ws] == c) last_focused[current_ws] = NULL;
            free(c);
            if (!workspaces[current_ws]) focused = NULL;
            else if (was_focused) focus(workspaces[current_ws]);
            arrange();
            return;
        }
    }
}

static void buttonpress(XEvent *e) {
    for (Client *c = workspaces[current_ws]; c; c = c->next)
        if (c->win == e->xbutton.subwindow) {
            focus(c);
            break;
        }
}

static void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    Client *c = NULL;

    for (int i = 0; i < NUM_WS; i++) {
        for (Client *curr = workspaces[i]; curr; curr = curr->next) {
            if (curr->win == ev->window) {
                c = curr;
                break;
            }
        }
        if (c) break;
    }

    if (c) {
        int bw = c->isfullscreen ? 0 : border_width;
        XConfigureEvent ce = {
            .type = ConfigureNotify,
            .display = dpy,
            .event = c->win,
            .window = c->win,
            .x = c->x,
            .y = c->y,
            .width = c->w - 2 * bw,
            .height = c->h - 2 * bw,
            .border_width = bw,
            .above = None,
            .override_redirect = False
        };
        XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
    } else {
        XWindowChanges wc = {
            .x = ev->x,
            .y = ev->y,
            .width = ev->width,
            .height = ev->height,
            .border_width = ev->border_width
        };
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
}

static void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect) return;

    for (int i = 0; i < NUM_WS; i++)
        for (Client *c = workspaces[i]; c; c = c->next)
            if (c->win == ev->window) {
                XMapWindow(dpy, ev->window);
                return;
            }

    if (check_window_type(ev->window, "_NET_WM_WINDOW_TYPE_NOTIFICATION") ||
        check_window_type(ev->window, "_NET_WM_WINDOW_TYPE_SPLASH")) {
        XMapWindow(dpy, ev->window);
        return;
    }

    long struts[4] = {0};
    if (get_window_struts(ev->window, struts)) {
        StrutWindow *swin = calloc(1, sizeof(StrutWindow));
        if (swin) {
            swin->win = ev->window;
            memcpy(swin->struts, struts, sizeof(struts));
            swin->next = strut_windows;
            strut_windows = swin;
            update_struts();
        }
        XMapWindow(dpy, ev->window);
        arrange();
        return;
    }

    Window trans = None;
    int floating = (XGetTransientForHint(dpy, ev->window, &trans) && trans != None);
    Client *c = create_client(ev->window, floating);
    if (c) {
        if (floating) {
            XRaiseWindow(dpy, c->win);
            focus(c);
        } else {
            focus(c);
            arrange();
        }
    }
}

static void unmapnotify(XEvent *e) {
    if (e->xunmap.send_event) return;
    for (StrutWindow *swin = strut_windows; swin; swin = swin->next) {
        if (swin->win == e->xunmap.window) {
            remove_strut_window(e->xunmap.window);
            arrange();
            return;
        }
    }
    for (int i = 0; i < NUM_WS; i++) {
        for (Client *c = workspaces[i]; c; c = c->next) {
            if (c->win == e->xunmap.window) {
                if (!c->ishidden && i == current_ws) removeclient(e->xunmap.window);
                return;
            }
        }
    }
}

static void destroynotify(XEvent *e) {
    remove_strut_window(e->xdestroywindow.window);
    removeclient(e->xdestroywindow.window);
}

static void enternotify(XEvent *e) {
    if (e->xcrossing.mode != NotifyNormal || e->xcrossing.detail == NotifyInferior) return;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->win == e->xcrossing.window) {
            focus(c);
            break;
        }
    }
}

static void keypress(XEvent *e) {
    KeySym keysym = XLookupKeysym(&e->xkey, 0);
    unsigned int state = CLEANMASK(e->xkey.state);
    for (size_t i = 0; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym && state == keys[i].mod && keys[i].func) {
            keys[i].func(&keys[i].arg);
            break;
        }
    }
}

static void screenchange(XEvent *e) {
    XRRUpdateConfiguration(e);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    update_monitors();
    arrange();
}

static void scan(void) {
    unsigned int num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;
    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (unsigned int i = 0; i < num; i++) {
            if (XGetWindowAttributes(dpy, wins[i], &wa) && !wa.override_redirect && wa.map_state == IsViewable) {
                XEvent e = {.type = MapRequest, .xmaprequest.window = wins[i]};
                maprequest(&e);
            }
        }
        if (wins) XFree(wins);
    }
}

void focus_monitor(const Arg *arg) {
    if (!monitors || monitor_count <= 1) return;
    current_monitor = cycle_monitor(current_monitor, arg->i);
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (!c->ishidden && !c->isfloating && get_monitor_for_window(c) == current_monitor) {
            focus(c);
            break;
        }
    }
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, current_monitor->x + current_monitor->w / 2, current_monitor->y + current_monitor->h / 2);
    XFlush(dpy);
}

void movewin_to_monitor(const Arg *arg) {
    if (!focused || !monitors || monitor_count <= 1 || focused->isfloating) return;
    Monitor *target = cycle_monitor(get_monitor_for_window(focused), arg->i);
    if (!target || target == get_monitor_for_window(focused)) return;
    focused->x = target->x + padding;
    focused->y = target->y + padding;
    arrange();
    focus(focused);
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, focused->x + focused->w / 2, focused->y + focused->h / 2);
    XFlush(dpy);
}

void killclient(const Arg *arg) {
    (void)arg;
    if (focused) XKillClient(dpy, focused->win);
}

void togglemaster(const Arg *arg) {
    (void)arg;
    if (!focused || !workspaces[current_ws] || focused == workspaces[current_ws]) return;
    for (Client **p = &workspaces[current_ws]; *p; p = &(*p)->next) {
        if (*p == focused) {
            *p = focused->next;
            focused->next = workspaces[current_ws];
            workspaces[current_ws] = focused;
            arrange();
            return;
        }
    }
}

void incmaster(const Arg *arg) {
    (void)arg;
    master_size = (master_size + 0.05 > 0.9) ? 0.9 : master_size + 0.05;
    arrange();
}

void decmaster(const Arg *arg) {
    (void)arg;
    master_size = (master_size - 0.05 < 0.1) ? 0.1 : master_size - 0.05;
    arrange();
}

void nextwin(const Arg *arg) {
    (void)arg;
    if (!focused || !workspaces[current_ws]) return;
    Client *c = focused->next;
    while (c && !can_focus(c)) c = c->next;
    focus(c ? c : workspaces[current_ws]);
}

void prevwin(const Arg *arg) {
    (void)arg;
    if (!focused || !workspaces[current_ws]) return;
    Client *prev = NULL, *last = workspaces[current_ws];
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->next == focused && can_focus(c)) prev = c;
        if (can_focus(c)) last = c;
    }
    focus(prev ? prev : last);
}

void movewin(const Arg *arg) {
    if (!focused || !workspaces[current_ws] || focused == workspaces[current_ws]) return;
    int count = 0, idx = -1;
    for (Client *c = workspaces[current_ws]->next; c; c = c->next) {
        if (c == focused) idx = count;
        count++;
    }
    int target_idx = idx + arg->i;
    if (idx < 0 || target_idx < 0 || target_idx >= count) return;

    Client **arr = malloc(count * sizeof(Client *));
    if (!arr) return;
    Client *c = workspaces[current_ws]->next;
    for (int i = 0; i < count; i++) {
        arr[i] = c;
        c = c->next;
    }

    Client *tmp = arr[idx];
    arr[idx] = arr[target_idx];
    arr[target_idx] = tmp;

    c = workspaces[current_ws];
    for (int i = 0; i < count; i++) {
        c->next = arr[i];
        c = c->next;
    }
    c->next = NULL;
    free(arr);
    arrange();
}

void switchws(const Arg *arg) {
    int ws = arg->i;
    if (ws < 0 || ws >= NUM_WS || ws == current_ws) return;
    int old = current_ws;
    current_ws = ws;

    for (Client *c = workspaces[old]; c; c = c->next) {
        c->ishidden = 1;
        XUnmapWindow(dpy, c->win);
    }
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        c->ishidden = 0;
        XMapWindow(dpy, c->win);
        XSetWindowBorder(dpy, c->win, border_normal);
    }

    focused = last_focused[current_ws];
    if (!can_focus(focused)) focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

void movewin_to_ws(const Arg *arg) {
    int ws = arg->i;
    if (!focused || ws < 0 || ws >= NUM_WS || ws == current_ws) return;
    Client *m = focused;

    for (Client **p = &workspaces[current_ws]; *p; p = &(*p)->next) {
        if (*p == m) {
            *p = m->next;
            break;
        }
    }

    m->ws = ws;
    m->next = workspaces[ws];
    m->ishidden = m->isfullscreen = 0;
    workspaces[ws] = m;
    XUnmapWindow(dpy, m->win);

    focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

void fullscreen(const Arg *arg) {
    (void)arg;
    if (!focused) return;
    focused->isfullscreen = !focused->isfullscreen;
    if (!focused->isfullscreen) {
        XSetWindowBorderWidth(dpy, focused->win, border_width);
        XSetWindowBorder(dpy, focused->win, border_focused);
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            c->ishidden = 0;
            XMapWindow(dpy, c->win);
        }
        focus(focused);
    }
    arrange();
}

void quit(const Arg *arg) {
    (void)arg;
    for (int i = 0; i < NUM_WS; i++) {
        while (workspaces[i]) {
            Client *tmp = workspaces[i]->next;
            free(workspaces[i]);
            workspaces[i] = tmp;
        }
    }
    while (strut_windows) {
        StrutWindow *tmp = strut_windows->next;
        free(strut_windows);
        strut_windows = tmp;
    }
    while (monitors) {
        Monitor *tmp = monitors->next;
        free(monitors);
        monitors = tmp;
    }
    XCloseDisplay(dpy);
    exit(0);
}

void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", arg->cmd, (char *)NULL);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    XEvent ev;
    void (*handlers[LASTEvent])(XEvent *) = {
        [ButtonPress] = buttonpress,
        [ConfigureRequest] = configurerequest,
        [MapRequest] = maprequest,
        [UnmapNotify] = unmapnotify,
        [DestroyNotify] = destroynotify,
        [EnterNotify] = enternotify,
        [KeyPress] = keypress,
        [RRScreenChangeNotify + RRNotify] = screenchange
    };

    if (argc == 2 && !strcmp("-v", argv[1])) die("eowm v" VERSION);
    if (argc != 1) die("Usage: eowm [-v]");
    if (!getenv("DISPLAY")) die("DISPLAY environment variable not set");
    if (!(dpy = XOpenDisplay(NULL))) die("cannot open X11 display");

    XSetErrorHandler(xerror);
    signal(SIGCHLD, sigchld_handler);

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    master_size = default_master_size;

    border_normal = getcolor(col_border_normal);
    border_focused = getcolor(col_border_focused);
    XClearWindow(dpy, root);
    XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));

    net_wm_strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
    net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);

    update_monitors();
    XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask |
                            EnterWindowMask | LeaveWindowMask | FocusChangeMask |
                            StructureNotifyMask | PropertyChangeMask);

    for (size_t i = 0; i < LENGTH(keys); i++)
        XGrabKey(dpy, XKeysymToKeycode(dpy, keys[i].keysym), keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);

    scan();
    while (1) {
        XNextEvent(dpy, &ev);
        if (handlers[ev.type])
            handlers[ev.type](&ev);
    }
}
