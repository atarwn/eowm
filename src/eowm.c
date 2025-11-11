/* eowm - eet owter winvow manader (vertical stack, master on right) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
// #include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>

// STRUCTURES
typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int isfullscreen;
    int workspace;
    Client *next;
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
// STRUCTURES END

// GLOBAL VARIABLES
static Display *dpy;
static Window root;
static Client *focused = NULL;
static int screen;
static int sw, sh; // screen width/height
static double master_size;
static Client *workspaces[9] = {NULL};
static int current_ws = 0;
static unsigned long border_normal, border_focused;
static Atom wm_protocols, wm_delete_window, wm_state, wm_take_focus;
// GLOBAL VARIABLES END

// FUNCTION DECLARATIONS
// Event Handlers
static void buttonpress(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);
static void unmapnotify(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void keypress(XEvent *e);

// Helper functions
static void focus(Client *c);
static void arrange(void);
static void resize(Client *c, int x, int y, int w, int h);
static void removeclient(Window win);
static int get_stack_clients(Client *stack[], int max);
static void move_in_stack(int delta);
static void die(const char *fmt, ...);

// Managers
static void killclient(const Arg *arg);
static void togglemaster(const Arg *arg);
static void incmaster(const Arg *arg);
static void decmaster(const Arg *arg);
static void nextwin(const Arg *arg);
static void prevwin(const Arg *arg);
static void movewinup(const Arg *arg);
static void movewindown(const Arg *arg);
static void switchws(const Arg *arg);
static void movewin_to_ws(const Arg *arg);
static void fullscreen(const Arg *arg);
static void quit(const Arg *arg);
static void spawn(const Arg *arg);
static void cleanup(void);
// FUNCTION DECLARATIONS END

// CONFIGURATION
#include "config.h"
// CONFIGURATION END

// EVENT HANDLERS
static void (*handlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ConfigureRequest] = configurerequest,
    [MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [KeyPress] = keypress
};
// EVENT HANDLERS END

// UTILITY FUNCTIONS
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

    if (XParseColor(dpy, cmap, root_bg, &color) &&
        XAllocColor(dpy, cmap, &color)) {
        XSetWindowBackground(dpy, root, color.pixel);
        XClearWindow(dpy, root);
    }
}

static void setup_icccm(void) {
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
    wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
// UTILITY FUNCTIONS END

// MAIN FUNCTION
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
    
    XSelectInput(dpy, root, 
        SubstructureRedirectMask | SubstructureNotifyMask | 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
        XGrabKey(dpy, code, keys[i].mod, root, True,
                 GrabModeAsync, GrabModeAsync);
    }
    
    while (1) {
        XNextEvent(dpy, &ev);
        if (handlers[ev.type])
            handlers[ev.type](&ev);
    }
}
// MAIN FUNCTION END

// EVENT HANDLER IMPLEMENTATIONS
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
    XWindowChanges wc = {0};
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
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
    
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    
    Atom net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    if (XGetWindowProperty(dpy, ev->window, net_wm_window_type,
                          0, 1, False, XA_ATOM, &actual_type,
                          &actual_format, &nitems, &bytes_after, &prop) == Success) {
        if (prop) {
            Atom type = *(Atom*)prop;
            Atom notification = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
            Atom splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
            XFree(prop);
            
            if (type == notification || type == splash) {
                XMapWindow(dpy, ev->window);
                return;
            }
        }
    }

    Client *c = calloc(1, sizeof(Client));
    if (!c) {
        fprintf(stderr, "Failed to allocate client\n");
        return;
    }

    c->win = ev->window;
    c->workspace = current_ws;
    c->next = workspaces[current_ws];
    workspaces[current_ws] = c;
    
    XSetWindowBorderWidth(dpy, c->win, border_width);
    XSetWindowBorder(dpy, c->win, border_normal); 
    
    XSelectInput(dpy, c->win, 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
}

static void unmapnotify(XEvent *e);

void unmapnotify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    
    if (ev->send_event) return;
    
    Client *found = NULL;
    for (int i = 0; i < 9; i++) {
        for (Client *c = workspaces[i]; c; c = c->next) {
            if (c->win == ev->window) {
                found = c;
                break;
            }
        }
        if (found) break;
    }
    if (found && found->workspace != current_ws) return;
    
    if (found) {
        removeclient(ev->window);
    }
}

void destroynotify(XEvent *e) {
    removeclient(e->xdestroywindow.window);
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
            if (keys[i].func) {
                keys[i].func(&keys[i].arg);
            }
            break;
        }
    }
}
// EVENT HANDLER IMPLEMENTATIONS END

// CLIENT MANAGEMENT FUNCTIONS
static void removeclient(Window win) {
    Client *c, **prev;
    for (prev = &workspaces[current_ws]; (c = *prev); prev = &c->next) {
        if (c->win == win) {
            int was_focused = (focused == c);
            *prev = c->next;
            free(c);
            
            if (!workspaces[current_ws]) {
                focused = NULL;
            } else if (was_focused) {
                focus(workspaces[current_ws]);
            }
            arrange();
            break;
        }
    }
}

void focus(Client *c) {
    if (!c) return;
    if (focused && focused != c) {
        XSetWindowBorder(dpy, focused->win, border_normal);
    }
    focused = c;
    XSetWindowBorder(dpy, c->win, border_focused);
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

static void resize(Client *c, int x, int y, int w, int h) {
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    XMoveResizeWindow(dpy, c->win, x, y, w - 2 * border_width, h - 2 * border_width);
}

void arrange() {
    if (!workspaces[current_ws]) return;
    
    // Handle fullscreen windows first
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->isfullscreen) {
            XSetWindowBorderWidth(dpy, c->win, 0);
            resize(c, 0, 0, sw, sh);
            XMapWindow(dpy, c->win);
            XRaiseWindow(dpy, c->win);
            // Hide other windows
            for (Client *other = workspaces[current_ws]; other; other = other->next) {
                if (other != c) {
                    XUnmapWindow(dpy, other->win);
                }
            }
            return;
        }
    }
    
    // No fullscreen windows - ensure all windows have borders and are mapped
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        XSetWindowBorderWidth(dpy, c->win, border_width);
        XMapWindow(dpy, c->win);
    }
    
    // Count clients
    int n = 0;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        n++;
    }
    if (n == 0) return;

    int usable_w = sw - padding * 2;
    int usable_h = sh - padding * 2;

    // Special case: single window fills entire usable area
    if (n == 1) {
        Client *only = workspaces[current_ws];
        resize(only, padding, padding, usable_w, usable_h);
        XMapWindow(dpy, only->win);
        if (focused) XRaiseWindow(dpy, focused->win);
        return;
    }

    // Normal master-stack layout for n >= 2
    int mw = (int)(usable_w * master_size);
    int stack_w = usable_w - mw - padding;

    Client *master = workspaces[current_ws];
    if (master) {
        int x = sw - mw - padding;  // master on right
        int y = padding;
        resize(master, x, y, mw, usable_h);
        XMapWindow(dpy, master->win);
    }

    // Arrange stack (n-1 windows on left)
    int stack_count = n - 1;
    int th = usable_h / stack_count;
    int y = padding;
    for (Client *c = workspaces[current_ws]->next; c; c = c->next) {
        int h = (c->next) ? th : (usable_h - (y - padding));
        if (h < min_window_size) h = min_window_size;
        resize(c, padding, y, stack_w, h);
        XMapWindow(dpy, c->win);
        y += h + padding;
    }

    if (focused) {
        XRaiseWindow(dpy, focused->win);
    }
}
// CLIENT MANAGEMENT FUNCTIONS END

// WINDOW MANAGEMENT FUNCTIONS
void killclient(const Arg *arg) {
    if (focused) XKillClient(dpy, focused->win);
}

void togglemaster(const Arg *arg) {
    if (!workspaces[current_ws] || !workspaces[current_ws]->next) return;
    
    Client *first = workspaces[current_ws];
    Client *second = workspaces[current_ws]->next;
    
    first->next = second->next;
    second->next = first;
    workspaces[current_ws] = second;
    
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

void nextwin(const Arg *arg) {
    if (!focused || !focused->next) {
        if (workspaces[current_ws]) focus(workspaces[current_ws]);
        return;
    }
    focus(focused->next);
}

void prevwin(const Arg *arg) {
    if (!focused || !workspaces[current_ws]) return;

    Client *prev = NULL;
    Client *last = workspaces[current_ws];
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->next == focused) {
            prev = c;
        }
        if (c->next) last = c->next;
    }

    if (prev) {
        focus(prev);
    } else {
        focus(last);
    }
}

static int get_stack_clients(Client *stack[], int max) {
    if (!workspaces[current_ws]) return 0;
    int n = 0;
    Client *c = workspaces[current_ws]->next;
    while (c && n < max) {
        stack[n++] = c;
        c = c->next;
    }
    return n;
}

static void move_in_stack(int delta) {
    if (!focused || focused == workspaces[current_ws]) return;

    Client *stack[max_stack_size];
    int n = get_stack_clients(stack, max_stack_size);
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

    // Swap
    Client *temp = stack[new_idx];
    stack[new_idx] = stack[idx];
    stack[idx] = temp;

    // Re-link
    Client *current = workspaces[current_ws];
    for (int i = 0; i < n; i++) {
        current->next = stack[i];
        current = stack[i];
    }
    current->next = NULL;

    arrange();
}

void movewinup(const Arg *arg)   { move_in_stack(-1); }
void movewindown(const Arg *arg) { move_in_stack(+1); }
// WINDOW MANAGEMENT FUNCTIONS END

// WORKSPACE MANAGEMENT FUNCTIONS
static void switchws(const Arg *arg) {
    int ws = arg->i;
    if (ws < 0 || ws >= 9 || ws == current_ws) return;
    
    int old_ws = current_ws;
    current_ws = ws;
    
    // Unmap old workspace windows
    for (Client *c = workspaces[old_ws]; c; c = c->next) {
        XUnmapWindow(dpy, c->win);
    }
    
    // Map current workspace windows
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        XMapWindow(dpy, c->win);
    }
    
    focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

static void movewin_to_ws(const Arg *arg) {
    int ws = arg->i;
    if (!focused || ws < 0 || ws >= 9 || ws == current_ws) return;
    
    Client *moving = focused;
    
    // Remove from current workspace
    Client **prev;
    for (prev = &workspaces[current_ws]; *prev; prev = &(*prev)->next) {
        if (*prev == moving) {
            *prev = moving->next;
            break;
        }
    }
    
    // Add to target workspace
    moving->workspace = ws;
    moving->next = workspaces[ws];
    workspaces[ws] = moving;
    moving->isfullscreen = 0;
    
    XUnmapWindow(dpy, moving->win);
    
    focused = workspaces[current_ws];
    if (focused) {
        focus(focused);
    }
    
    arrange();
}
// WORKSPACE MANAGEMENT FUNCTIONS END

// SPECIAL WINDOW FUNCTIONS
void fullscreen(const Arg *arg) {
    if (!focused) return;
    
    if (focused->isfullscreen) {
        // Exit fullscreen
        focused->isfullscreen = 0;
        XSetWindowBorderWidth(dpy, focused->win, border_width);
        XSetWindowBorder(dpy, focused->win, border_focused);
        arrange();
    } else {
        // Enter fullscreen
        focused->isfullscreen = 1;
        arrange();
    }
}
// SPECIAL WINDOW FUNCTIONS END

// SYSTEM FUNCTIONS
static void cleanup() {
    for (int i = 0; i < 9; i++) {
        Client *c = workspaces[i];
        while (c) {
            Client *next = c->next;
            free(c);
            c = next;
        }
    }
}

void quit(const Arg *arg) {
    cleanup();
    XCloseDisplay(dpy);
    exit(0);
}

void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", arg->cmd, (char *)NULL);
        fprintf(stderr, "Exec failed\n");
        exit(1);
    }
}
// SYSTEM FUNCTIONS END
