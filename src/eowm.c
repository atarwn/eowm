/* eowm - eet owter winvow manader (vertical stack, master on right) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
// #include <X11/XF86keysym.h>
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

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int workspace;
    int isfullscreen;
    int ishidden;
    int isfloating;
    Client *next;
};

typedef struct StrutWindow StrutWindow;
struct StrutWindow {
    Window win;
    long struts[4]; // left, right, top, bottom
    StrutWindow *next;
};

typedef struct Monitor Monitor;
struct Monitor {
    int x, y, w, h;
    int num;
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
static Client *focused = NULL;
static int screen;
static int sw, sh; // screen width/height
static double master_size;
static Client *workspaces[9] = {NULL};
static Client *last_focused[9] = {NULL};
static int current_ws = 0;
static Monitor *monitors = NULL;
static Monitor *current_monitor = NULL;
static int monitor_count = 0;
static unsigned long border_normal, border_focused;
static Atom wm_protocols, wm_delete_window, wm_state, wm_take_focus;
static Atom net_wm_strut, net_wm_strut_partial;
static StrutWindow *strut_windows = NULL;
static int global_strut_left = 0;
static int global_strut_right = 0;
static int global_strut_top = 0;
static int global_strut_bottom = 0;

static void buttonpress(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);
static void unmapnotify(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void keypress(XEvent *e);
static void screenchange(XEvent *e);
static int can_focus(Client *c);
static void arrange_monitor(Monitor *mon);

static void focus(Client *c);
static void arrange(void);
static void scan(void);
static void resize(Client *c, int x, int y, int w, int h);
static void removeclient(Window win);
static int get_stack_clients(Client *stack[], int max);
static void move_in_stack(int delta);
static void die(const char *fmt, ...);
static void update_struts(void);
static void remove_strut_window(Window win);
static int get_window_struts(Window win, long struts[4]);

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
static void cleanup(void);

static Monitor* get_monitor_at(int x, int y);
static void update_monitors(void);

#include "config.h"

static void (*handlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ConfigureRequest] = configurerequest,
    [MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [KeyPress] = keypress,
    [RRScreenChangeNotify + RRNotify] = screenchange
};

static void setup_colors(void) {
    Colormap cmap = DefaultColormap(dpy, screen);
    XColor color;
    XParseColor(dpy, cmap, col_border_normal, &color);
    XAllocColor(dpy, cmap, &color);
    border_normal = color.pixel;
    XParseColor(dpy, cmap, col_border_focused, &color);
    XAllocColor(dpy, cmap, &color);
    border_focused = color.pixel;
}

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static int xerrorHandler(Display *dpy, XErrorEvent *ee) {
    char msg[256];
    XGetErrorText(dpy, ee->error_code, msg, sizeof(msg));
    fprintf(stderr, "X Error: %s\n", msg);
    return 0;
}

static void setrootbackground(void) {
    Colormap cmap = DefaultColormap(dpy, screen);
    XColor color;
    if (XParseColor(dpy, cmap, root_bg, &color) && XAllocColor(dpy, cmap, &color)) {
        XSetWindowBackground(dpy, root, color.pixel);
        XClearWindow(dpy, root);
    }
}

static void setup_icccm(void) {
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
    wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    net_wm_strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
    net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void update_struts(void) {
    global_strut_left = global_strut_right = global_strut_top = global_strut_bottom = 0;
    for (StrutWindow *sw = strut_windows; sw; sw = sw->next) {
        if (sw->struts[0] > global_strut_left) global_strut_left = sw->struts[0];
        if (sw->struts[1] > global_strut_right) global_strut_right = sw->struts[1];
        if (sw->struts[2] > global_strut_top) global_strut_top = sw->struts[2];
        if (sw->struts[3] > global_strut_bottom) global_strut_bottom = sw->struts[3];
    }
}

static void remove_strut_window(Window win) {
    StrutWindow **prev = &strut_windows;
    for (StrutWindow *sw = strut_windows; sw; sw = sw->next) {
        if (sw->win == win) {
            *prev = sw->next;
            free(sw);
            update_struts();
            return;
        }
        prev = &sw->next;
    }
}

static int get_window_struts(Window win, long struts[4]) {
    Atom types[] = {net_wm_strut_partial, net_wm_strut};
    for (int t = 0; t < 2; t++) {
        Atom actual_type;
        int fmt;
        unsigned long n, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, win, types[t], 0, 4, False, XA_CARDINAL,
                              &actual_type, &fmt, &n, &after, &data) == Success) {
            if (actual_type == XA_CARDINAL && fmt == 32 && n >= 4) {
                long *vals = (long*)data;
                int has_struts = 0;
                for (int i = 0; i < 4; i++) {
                    struts[i] = vals[i];
                    if (vals[i] > 0) has_struts = 1;
                }
                XFree(data);
                return has_struts;
            }
            if (data) XFree(data);
        }
    }
    return 0;
}

static int check_window_type(Window win, const char *type_name) {
    Atom actual_type;
    int fmt;
    unsigned long n, after;
    unsigned char *prop = NULL;
    Atom net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    
    if (XGetWindowProperty(dpy, win, net_wm_window_type, 0, 1, False, XA_ATOM,
                          &actual_type, &fmt, &n, &after, &prop) == Success && prop) {
        Atom type = *(Atom*)prop;
        Atom check = XInternAtom(dpy, type_name, False);
        XFree(prop);
        return type == check;
    }
    return 0;
}

static Client* create_client(Window win, int floating) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) return NULL;
    c->win = win;
    c->workspace = current_ws;
    c->isfloating = floating;
    c->next = workspaces[current_ws];
    workspaces[current_ws] = c;
    
    if (current_monitor) {
        if (floating) {
            c->x = current_monitor->x + current_monitor->w / 4;
            c->y = current_monitor->y + current_monitor->h / 4;
            c->w = current_monitor->w / 2;
            c->h = current_monitor->h / 2;
        } else {
            c->x = current_monitor->x;
            c->y = current_monitor->y;
        }
    }
    
    XSetWindowBorderWidth(dpy, c->win, border_width);
    XSetWindowBorder(dpy, c->win, border_normal);
    XSelectInput(dpy, c->win, EnterWindowMask | LeaveWindowMask | 
                 FocusChangeMask | StructureNotifyMask);
    XMapWindow(dpy, c->win);
    return c;
}

static void update_monitors(void) {
    Monitor *m;
    while (monitors) {
        m = monitors->next;
        free(monitors);
        monitors = m;
    }
    monitors = NULL;
    current_monitor = NULL;
    monitor_count = 0;

    XRRScreenResources *sr = XRRGetScreenResources(dpy, root);
    if (!sr) return;

    for (int i = 0; i < sr->ncrtc; i++) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]);
        if (!ci || ci->noutput == 0 || ci->width == 0 || ci->height == 0) {
            if (ci) XRRFreeCrtcInfo(ci);
            continue;
        }

        Monitor *mon = calloc(1, sizeof(Monitor));
        mon->num = monitor_count++;
        mon->x = ci->x;
        mon->y = ci->y;
        mon->w = ci->width;
        mon->h = ci->height;
        mon->next = monitors;
        monitors = mon;

        XRRFreeCrtcInfo(ci);
    }
    XRRFreeScreenResources(sr);

    if (!monitors) {
        monitors = calloc(1, sizeof(Monitor));
        monitors->x = 0;
        monitors->y = 0;
        monitors->w = sw;
        monitors->h = sh;
        monitors->num = 0;
        monitor_count = 1;
    }
    current_monitor = monitors;
}

static Monitor* get_monitor_at(int x, int y) {
    for (Monitor *m = monitors; m; m = m->next) {
        if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
            return m;
    }
    return monitors;
}

static Monitor* get_monitor_for_window(Client *c) {
    return get_monitor_at(c->x + c->w / 2, c->y + c->h / 2);
}

static void focus_monitor(const Arg *arg) {
    if (!monitors || monitor_count <= 1) return;
    
    int direction = arg->i;
    Monitor *target = NULL;
    Monitor *m = monitors;
    
    if (direction > 0) {
        target = current_monitor ? (current_monitor->next ? current_monitor->next : monitors) : monitors;
    } else {
        Monitor *last = monitors;
        while (last && last->next) last = last->next;
        
        if (!current_monitor || current_monitor == monitors) {
            target = last;
        } else {
            m = monitors;
            while (m && m->next != current_monitor) m = m->next;
            target = m;
        }
    }
    
    if (!target) target = monitors;
    current_monitor = target;
    
    Client *to_focus = NULL;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->ishidden || c->isfloating) continue;
        
        Monitor *win_mon = get_monitor_for_window(c);
        if (win_mon == target) {
            to_focus = c;
            break;
        }
    }
    
    if (to_focus) {
        focus(to_focus);
    }
    
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, 
                 target->x + target->w / 2, 
                 target->y + target->h / 2);
    XFlush(dpy);
}

static void movewin_to_monitor(const Arg *arg) {
    if (!focused || !monitors || monitor_count <= 1) return;
    if (focused->isfloating) return;
    
    int direction = arg->i;
    Monitor *current = get_monitor_for_window(focused);
    Monitor *target = NULL;
    
    if (direction > 0) {
        target = current ? (current->next ? current->next : monitors) : monitors;
    } else {
        Monitor *last = monitors;
        while (last && last->next) last = last->next;
        
        if (!current || current == monitors) {
            target = last;
        } else {
            Monitor *m = monitors;
            while (m && m->next != current) m = m->next;
            target = m;
        }
    }
    
    if (!target || target == current) return;
    
    focused->x = target->x + padding;
    focused->y = target->y + padding;
    
    arrange();
    
    focus(focused);
    
    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                 focused->x + focused->w / 2,
                 focused->y + focused->h / 2);
    XFlush(dpy);
}

int main(int argc, char *argv[]) {
    XEvent ev;
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("eowm v"VERSION);
    else if (argc != 1)
        die("Usage: eowm [-v]");
    if (!getenv("DISPLAY"))
        die("DISPLAY environment variable not set");
    if (!(dpy = XOpenDisplay(NULL)))
        die("cannot open X11 display (is X running?)");

    XSetErrorHandler(xerrorHandler);
    signal(SIGCHLD, sigchld_handler);
    
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    master_size = default_master_size;

    setup_colors();
    setrootbackground();
    setup_icccm();
    update_monitors();
    if (!current_monitor && monitors) {
        current_monitor = monitors;
    }
    XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
    
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | 
                            EnterWindowMask | LeaveWindowMask | FocusChangeMask |
                            StructureNotifyMask | PropertyChangeMask);
    
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
        XGrabKey(dpy, code, keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
    }

    scan();
    while (1) {
        XNextEvent(dpy, &ev);
        if (handlers[ev.type])
            handlers[ev.type](&ev);
    }
}

void buttonpress(XEvent *e) {
    XButtonPressedEvent *be = &e->xbutton;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->win == be->subwindow) {
            focus(c);
            break;
        }
    }
}

void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc = {ev->x, ev->y, ev->width, ev->height, 
                         ev->border_width, ev->above, ev->detail};
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, ev->window, &wa)) return;

    if (wa.override_redirect) {
        XMapWindow(dpy, ev->window);
        return;
    }

    for (int i = 0; i < 9; i++) {
        for (Client *c = workspaces[i]; c; c = c->next) {
            if (c->win == ev->window) {
                XMapWindow(dpy, ev->window);
                return;
            }
        }
    }
    
    if (check_window_type(ev->window, "_NET_WM_WINDOW_TYPE_NOTIFICATION") ||
        check_window_type(ev->window, "_NET_WM_WINDOW_TYPE_SPLASH")) {
        XMapWindow(dpy, ev->window);
        return;
    }

    if (check_window_type(ev->window, "_NET_WM_WINDOW_TYPE_DOCK")) {
        long struts[4] = {0};
        if (get_window_struts(ev->window, struts)) {
            StrutWindow *sw = calloc(1, sizeof(StrutWindow));
            if (sw) {
                sw->win = ev->window;
                memcpy(sw->struts, struts, sizeof(struts));
                sw->next = strut_windows;
                strut_windows = sw;
                update_struts();
            }
        }
        XMapWindow(dpy, ev->window);
        arrange();
        return;
    }

    long struts[4] = {0};
    if (get_window_struts(ev->window, struts)) {
        StrutWindow *sw = calloc(1, sizeof(StrutWindow));
        if (sw) {
            sw->win = ev->window;
            memcpy(sw->struts, struts, sizeof(struts));
            sw->next = strut_windows;
            strut_windows = sw;
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

void unmapnotify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    if (ev->send_event) return;
    
    for (StrutWindow *sw = strut_windows; sw; sw = sw->next) {
        if (sw->win == ev->window) {
            remove_strut_window(ev->window);
            arrange();
            return;
        }
    }
    
    Client *found = NULL;
    int found_ws = -1;
    for (int i = 0; i < 9; i++) {
        for (Client *c = workspaces[i]; c; c = c->next) {
            if (c->win == ev->window) {
                found = c;
                found_ws = i;
                break;
            }
        }
        if (found) break;
    }

    if (!found || found->ishidden || found_ws != current_ws) return;
    removeclient(ev->window);
}

void destroynotify(XEvent *e) {
    Window win = e->xdestroywindow.window;
    for (StrutWindow *sw = strut_windows; sw; sw = sw->next) {
        if (sw->win == win) {
            remove_strut_window(win);
            arrange();
            return;
        }
    }
    removeclient(win);
}

void enternotify(XEvent *e) {
    if ((e->xcrossing.mode != NotifyNormal) || 
        (e->xcrossing.detail == NotifyInferior)) return;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->win == e->xcrossing.window) {
            focus(c);
            break;
        }
    }
}

void keypress(XEvent *e) {
    KeySym keysym = XLookupKeysym(&e->xkey, 0);
    unsigned int state = e->xkey.state & ~(LockMask | Mod2Mask);
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        if (keysym == keys[i].keysym && state == keys[i].mod) {
            if (keys[i].func) keys[i].func(&keys[i].arg);
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

static void removeclient(Window win) {
    Client *c, **prev;
    for (prev = &workspaces[current_ws]; (c = *prev); prev = &c->next) {
        if (c->win == win) {
            int was_focused = (focused == c);
            *prev = c->next;
            XSelectInput(dpy, c->win, NoEventMask);
            if (last_focused[current_ws] == c) last_focused[current_ws] = NULL;
            free(c);
            if (!workspaces[current_ws]) {
                focused = NULL;
            } else if (was_focused) {
                focus(workspaces[current_ws]);
            }
            arrange();
            return;
        }
    }
}

void focus(Client *c) {
    if (!c || c->ishidden || c->workspace != current_ws) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, c->win, &wa)) return;
    if (focused && focused != c) XSetWindowBorder(dpy, focused->win, border_normal);
    focused = c;
    last_focused[current_ws] = c;
    XSetWindowBorder(dpy, c->win, border_focused);
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

static void resize(Client *c, int x, int y, int w, int h) {
    c->x = x; c->y = y; c->w = w; c->h = h;
    XMoveResizeWindow(dpy, c->win, x, y, w - 2 * border_width, h - 2 * border_width);
}

void arrange() {
    if (!workspaces[current_ws]) return;
    
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->isfullscreen) {
            Monitor *m = get_monitor_for_window(c);
            XSetWindowBorderWidth(dpy, c->win, 0);
            resize(c, m->x, m->y, m->w, m->h);
            XMapWindow(dpy, c->win);
            XRaiseWindow(dpy, c->win);
            for (Client *other = workspaces[current_ws]; other; other = other->next) {
                if (other != c) {
                    other->ishidden = 1;
                    XUnmapWindow(dpy, other->win);
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
    
    if (current_monitor) {
        arrange_monitor(current_monitor);
    }
    
    for (Monitor *mon = monitors; mon; mon = mon->next) {
        if (mon != current_monitor) {
            arrange_monitor(mon);
        }
    }

    for (Client *c = workspaces[current_ws]; c; c = c->next)
        if (c->isfloating) XRaiseWindow(dpy, c->win);
    if (focused) XRaiseWindow(dpy, focused->win);
}

static void arrange_monitor(Monitor *mon) {
    int x0 = mon->x + global_strut_left + padding;
    int y0 = mon->y + global_strut_top + padding;
    int usable_w = mon->w - global_strut_left - global_strut_right - 2 * padding;
    int usable_h = mon->h - global_strut_top - global_strut_bottom - 2 * padding;

    int n = 0;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (!c->isfloating && get_monitor_for_window(c) == mon) n++;
    }

    if (n == 0) return;

    if (n == 1) {
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            if (!c->isfloating && get_monitor_for_window(c) == mon) {
                resize(c, x0, y0, usable_w, usable_h);
                XMapWindow(dpy, c->win);
                break;
            }
        }
    } else if (n >= 2) {
        int mw = (int)(usable_w * master_size);
        int stack_w = usable_w - mw - padding;

        Client *master = NULL;
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            if (!c->isfloating && get_monitor_for_window(c) == mon) {
                master = c;
                break;
            }
        }
        
        if (master) {
            resize(master, x0 + usable_w - mw, y0, mw, usable_h);
            XMapWindow(dpy, master->win);
        }

        int stack_count = n - 1;
        if (stack_count > 0) {
            int th = usable_h / stack_count;
            int y = y0;
            int stacked = 0;
            for (Client *c = workspaces[current_ws]; c; c = c->next) {
                if (c->isfloating || c == master || get_monitor_for_window(c) != mon) continue;
                stacked++;
                int h = (stacked < stack_count) ? th : (usable_h - (y - y0));
                if (h < min_window_size) h = min_window_size;
                resize(c, x0, y, stack_w, h);
                XMapWindow(dpy, c->win);
                y += h + padding;
            }
        }
    }
}

static void scan(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect) 
                continue;
            if (wa.map_state == IsViewable) {
                XEvent e = {.type = MapRequest, .xmaprequest.window = wins[i]};
                maprequest(&e);
            }
        }
        if (wins) XFree(wins);
    }
}

void killclient(const Arg *arg) {
    if (focused) XKillClient(dpy, focused->win);
}

void togglemaster(const Arg *arg) {
    if (!focused || !workspaces[current_ws] || focused == workspaces[current_ws])
        return;

    Client **prev = &workspaces[current_ws];
    while (*prev && *prev != focused)
        prev = &(*prev)->next;
    if (*prev) {
        *prev = focused->next;
        focused->next = workspaces[current_ws];
        workspaces[current_ws] = focused;
    }
    arrange();
}

void incmaster(const Arg *arg) {
    master_size += 0.05;
    if (master_size > 0.9) master_size = 0.9;
    arrange();
}

void decmaster(const Arg *arg) {
    master_size -= 0.05;
    if (master_size < 0.1) master_size = 0.1;
    arrange();
}

static int can_focus(Client *c) {
    if (!c || c->ishidden || c->workspace != current_ws) return 0;
    XWindowAttributes wa;
    return XGetWindowAttributes(dpy, c->win, &wa);
}

void nextwin(const Arg *arg) {
    if (!focused || !workspaces[current_ws]) return;
    Client *next = focused->next;
    while (next && !can_focus(next)) next = next->next;
    if (next) focus(next);
    else if (can_focus(workspaces[current_ws])) focus(workspaces[current_ws]);
}

void prevwin(const Arg *arg) {
    if (!focused || !workspaces[current_ws]) return;
    Client *prev = NULL, *last = workspaces[current_ws];
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->next == focused && can_focus(c)) prev = c;
        if (c->next && can_focus(c->next)) last = c->next;
    }
    if (prev) focus(prev);
    else if (can_focus(last)) focus(last);
}

// static int get_stack_clients(Client *stack[], int max) {
//     if (!workspaces[current_ws]) return 0;
//     int n = 0;
//     Client *c = workspaces[current_ws]->next;
//     while (c && n < max) {
//         stack[n++] = c;
//         c = c->next;
//     }
//     return n;
// }

static void move_in_stack(int delta) {
    if (!focused || !workspaces[current_ws] || focused == workspaces[current_ws])
        return;

    Client *stack[max_stack_size];
    int n = 0;
    Client *c = workspaces[current_ws]->next;
    while (c && n < max_stack_size) {
        stack[n++] = c;
        c = c->next;
    }
    if (n < 2) return;

    int idx = -1;
    for (int i = 0; i < n; i++) {
        if (stack[i] == focused) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;

    int new_idx = idx + delta;
    if (new_idx < 0 || new_idx >= n) return;

    // swap
    Client *tmp = stack[idx];
    stack[idx] = stack[new_idx];
    stack[new_idx] = tmp;

    Client *cur = workspaces[current_ws];
    for (int i = 0; i < n; i++) {
        cur->next = stack[i];
        cur = cur->next;
    }
    cur->next = NULL;
    arrange();
}

void movewin(const Arg *arg) { move_in_stack(arg->i); }

static void switchws(const Arg *arg) {
    int ws = arg->i;
    if (ws < 0 || ws >= 9 || ws == current_ws) return;
    int old_ws = current_ws;
    current_ws = ws;
    
    for (Client *c = workspaces[old_ws]; c; c = c->next) {
        c->ishidden = 1;
        XUnmapWindow(dpy, c->win);
    }
    
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        c->ishidden = 0;
        XMapWindow(dpy, c->win);
        XSetWindowBorder(dpy, c->win, border_normal);
    }

    focused = last_focused[current_ws];
    if (!focused || !can_focus(focused)) focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

static void movewin_to_ws(const Arg *arg) {
    int ws = arg->i;
    if (!focused || ws < 0 || ws >= 9 || ws == current_ws) return;
    Client *moving = focused;
    
    Client **prev;
    for (prev = &workspaces[current_ws]; *prev; prev = &(*prev)->next) {
        if (*prev == moving) {
            *prev = moving->next;
            break;
        }
    }
    
    moving->workspace = ws;
    moving->next = workspaces[ws];
    moving->ishidden = 0;
    workspaces[ws] = moving;
    moving->isfullscreen = 0;
    XUnmapWindow(dpy, moving->win);
    
    focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

void fullscreen(const Arg *arg) {
    if (!focused) return;
    if (focused->isfullscreen) {
        focused->isfullscreen = 0;
        XSetWindowBorderWidth(dpy, focused->win, border_width);
        XSetWindowBorder(dpy, focused->win, border_focused);
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            c->ishidden = 0;
            XMapWindow(dpy, c->win);
        }
        focus(focused);
        arrange();
    } else {
        focused->isfullscreen = 1;
        arrange();
    }
}

static void cleanup() {
    for (int i = 0; i < 9; i++) {
        Client *c = workspaces[i];
        while (c) {
            Client *next = c->next;
            free(c);
            c = next;
        }
    }
    while (strut_windows) {
        StrutWindow *next = strut_windows->next;
        free(strut_windows);
        strut_windows = next;
    }
}

void quit(const Arg *arg) {
    cleanup();
    XCloseDisplay(dpy);
    exit(0);
}

void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", arg->cmd, (char *)NULL);
        fprintf(stderr, "Exec failed\n");
        exit(1);
    }
}