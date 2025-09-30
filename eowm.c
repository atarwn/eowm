/* eowm - eet owter winvow manader (vertical stack, master on right) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define MOD Mod1Mask
#define MASTER_SIZE 0.6
#define MIN_WIDTH 100

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int isfullscreen;
    int workspace;
    Client *next;
};
typedef void (*CmdFunc)(void);

static Display *dpy;
static Window root;
// static Client *clients = NULL;
static Client *focused = NULL;
static int screen;
static int sw, sh; // screen width/height
static double master_size = MASTER_SIZE;
static int num_clients = 0;
static Client *workspaces[9] = {NULL}; // 9 workspaces
static int current_ws = 0;

// Event handlers
static void buttonpress(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);
static void unmapnotify(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void keypress(XEvent *e);

static void (*handlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ConfigureRequest] = configurerequest,
    [MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [KeyPress] = keypress
};

// Helper functions
static void focus(Client *c);
static void arrange();
static void resize(Client *c, int x, int y, int w, int h);
static void killclient();
static void togglemaster();
static void incmaster();
static void decmaster();
static void nextwin();
static void prevwin();
static void movewinup();
static void movewindown();
static void switchws(int ws);
static void movewin_to_ws(int ws);
static void fullscreen();
static void quit();
static void spawn(const char *cmd);

// Define the workspace functions
#define WS_FUNC(n) \
    static void ws##n() { switchws((n)-1); } \
    static void movews##n() { movewin_to_ws((n)-1); }

WS_FUNC(1)
WS_FUNC(2)
WS_FUNC(3)
WS_FUNC(4)
WS_FUNC(5)
WS_FUNC(6)
WS_FUNC(7)
WS_FUNC(8)
WS_FUNC(9)

#undef WS_FUNC

// Workspaces macros
#define WS_KEY(n) \
    { XK_##n, MOD, (CmdFunc)ws##n, NULL }, \
    { XK_##n, MOD | ShiftMask, (CmdFunc)movews##n, NULL },

// Key bindings
static struct {
    KeySym keysym;
    unsigned int mod;
    CmdFunc func;          // All commands are void(*)(void)
    const char *arg;       // If non-NULL, it's a spawn command
} keys[] = {
    // General navidation & management
    { XK_j, MOD, (CmdFunc)nextwin, NULL },
    { XK_k, MOD, (CmdFunc)prevwin, NULL },
    { XK_f, MOD, (CmdFunc)fullscreen, NULL },
    { XK_q, MOD, (CmdFunc)killclient, NULL },
    { XK_c, MOD, (CmdFunc)quit, NULL },
    { XK_j, MOD | ShiftMask, (CmdFunc)movewindown, NULL },
    { XK_k, MOD | ShiftMask, (CmdFunc)movewinup, NULL },

    // Master window
    { XK_h, MOD, (CmdFunc)incmaster, NULL },
    { XK_l, MOD, (CmdFunc)decmaster, NULL },
    { XK_space, MOD, (CmdFunc)togglemaster, NULL },

    // Applications
    { XK_Return, MOD, (CmdFunc)spawn, "alacritty" },
    { XK_p, MOD, (CmdFunc)spawn, "dmenu_run" },

    // Workspaces - DRY with macros!
    WS_KEY(1)
    WS_KEY(2)
    WS_KEY(3)
    WS_KEY(4)
    WS_KEY(5)
    WS_KEY(6)
    WS_KEY(7)
    WS_KEY(8)
    WS_KEY(9)
};

#undef WS_KEY

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static int xerrorHandler(Display *dpy, XErrorEvent *ee) {
    return 0; // Ignore for now
}

int main() {
    XEvent ev;
    if (!(dpy = XOpenDisplay(NULL))) exit(1);

    // Set error handler early
    XSetErrorHandler(xerrorHandler);
    
    // Handle child processes to prevent zombie processes
    signal(SIGCHLD, sigchld_handler);
    
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    
    // Redirect window management
    XSelectInput(dpy, root, 
        SubstructureRedirectMask | SubstructureNotifyMask | 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    
    // Grab keys
    for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
        XGrabKey(dpy, code, keys[i].mod, root, True,
                 GrabModeAsync, GrabModeAsync);
    }
    
    // Main event loop
    while (1) {
        XNextEvent(dpy, &ev);
        if (handlers[ev.type])
            handlers[ev.type](&ev);
    }
}

// Event handlers
void buttonpress(XEvent *e) {
    XButtonPressedEvent *be = &e->xbutton;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->win == be->subwindow) {
            focus(c);
            XRaiseWindow(dpy, c->win);
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
    
    Client *c = calloc(1, sizeof(Client));
    c->win = ev->window;
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;
    c->workspace = current_ws;
    c->next = workspaces[current_ws];
    workspaces[current_ws] = c;
    num_clients++;
    
    XSelectInput(dpy, c->win, 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
}

static void removeclient(Window win) {
    Client *c, **prev;
    for (prev = &workspaces[current_ws]; (c = *prev); prev = &c->next) {
        if (c->win == win) {
            *prev = c->next;
            free(c);
            num_clients--;
            break;
        }
    }
    if (!workspaces[current_ws]) {
        focused = NULL;
    } else if (focused == c) {
        focus(workspaces[current_ws]); // clients guaranteed non-NULL here
    }
    arrange();
}

void unmapnotify(XEvent *e) {
    removeclient(e->xunmap.window);
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
    // Mask out irrelevant modifiers: CapsLock & NumLock.
    unsigned int state = e->xkey.state & ~(LockMask | Mod2Mask);

    for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        if (keysym == keys[i].keysym && state == keys[i].mod) {
            if (keys[i].arg) {
                spawn(keys[i].arg);  // Explicit call for spawn
            } else {
                keys[i].func();      // All others are void(void)
            }
            break;
        }
    }
}

// Core functionality
void focus(Client *c) {
    if (!c) return;
    focused = c;
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

void arrange() {
    if (!workspaces[current_ws]) return;
    
    // Handle fullscreen windows first (they get full screen regardless)
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->isfullscreen) {
            resize(c, 0, 0, sw, sh);
            XMapWindow(dpy, c->win);  // Ensure visible
            XRaiseWindow(dpy, c->win); // Ensure fullscreen window is on top
            return; // Only one fullscreen window should be visible
        }
    }
    
    // Count non-fullscreen clients
    int n = 0;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (!c->isfullscreen) n++;
    }
    
    if (n == 0) return;
    
    int mw = sw * master_size;  // Master width
    int th = sh / (n > 1 ? n - 1 : 1);  // Tile height
    
    Client *master = workspaces[current_ws];
    
    // Arrange master
    if (master && !master->isfullscreen) {
        resize(master, sw - mw, 0, mw, sh);
        XMapWindow(dpy, master->win);  // Ensure visible
    }
    
    // Arrange stack
    int stack_count = 0;
    // Count stack clients first
    for (Client *c = workspaces[current_ws]->next; c; c = c->next) {
        if (!c->isfullscreen) stack_count++;
    }
    
    int i = 0;
    for (Client *c = workspaces[current_ws]->next; c; c = c->next) {
        if (c->isfullscreen) continue;
        
        int ty = i * th;
        int th_actual = (i == stack_count - 1) ? sh - ty : th;
        if (th_actual < MIN_WIDTH) th_actual = MIN_WIDTH;
        resize(c, 0, ty, sw - mw, th_actual);
        XMapWindow(dpy, c->win);  // Ensure it's visible
        i++;
    }

    if (focused && !focused->isfullscreen) {
        XRaiseWindow(dpy, focused->win);
    }
}

void resize(Client *c, int x, int y, int w, int h) {
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    XMoveResizeWindow(dpy, c->win, x, y, w, h);
}

// Commands
void killclient() {
    if (focused) XKillClient(dpy, focused->win);
}

void togglemaster() {
    if (!workspaces[current_ws] || !workspaces[current_ws]->next) return;
    
    // Swap first and second client
    Client *first = workspaces[current_ws];
    Client *second = workspaces[current_ws]->next;
    
    // Update links
    first->next = second->next;
    second->next = first;
    workspaces[current_ws] = second;
    
    arrange();
}

void incmaster() {
    master_size += 0.05;
    if (master_size > 0.9) master_size = 0.9;
    arrange();
}

void decmaster() {
    master_size -= 0.05;
    if (master_size < 0.1) master_size = 0.1;
    arrange();
}

void nextwin() {
    if (!focused || !focused->next) {
        // Wrap to first
        if (workspaces[current_ws]) focus(workspaces[current_ws]);
        return;
    }
    focus(focused->next);
}

void prevwin() {
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
        focus(last); // wrap to last
    }
}

// Returns number of stack clients filled into the 'stack' array (which must be pre-allocated)
static int get_stack_clients(Client *stack[], int max) {
    if (!workspaces[current_ws]) return 0;
    int n = 0;
    Client *c = workspaces[current_ws]->next; // stack starts after master
    while (c && n < max) {
        stack[n] = c;          // stack[n] is Client*, c is Client* → OK
        n++;
        c = c->next;
    }
    return n;
}

static void move_in_stack(int delta) {
    if (!focused || focused == workspaces[current_ws] || num_clients < 3) return;

    Client *stack[256];
    int n = get_stack_clients(stack, 256);
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

void movewinup()    { move_in_stack(-1); }
void movewindown()  { move_in_stack(+1); }

static void switchws(int ws) {
    if (ws < 0 || ws >= 9) return;
    current_ws = ws;
    
    // Hide all windows
    for (int i = 0; i < 9; i++) {
        for (Client *c = workspaces[i]; c; c = c->next) {
            XUnmapWindow(dpy, c->win);
        }
    }
    
    // Show current workspace windows
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        XMapWindow(dpy, c->win);
    }
    
    focused = workspaces[current_ws];
    if (focused) focus(focused);
    arrange();
}

static void movewin_to_ws(int ws) {
    if (!focused || ws < 0 || ws >= 9 || ws == current_ws) return;
    
    Client *moving = focused;
    
    // Remove from current workspace
    Client **prev;
    for (prev = &workspaces[current_ws]; *prev; prev = &(*prev)->next) {
        if (*prev == moving) {
            *prev = moving->next;
            num_clients--;  // Decrease count for current workspace
            break;
        }
    }
    
    // Add to target workspace (at the front, becomes master)
    moving->workspace = ws;
    moving->next = workspaces[ws];
    workspaces[ws] = moving;
    moving->isfullscreen = 0;  // Reset fullscreen state
    num_clients++;  // Increase count (this is actually workspace-specific, see note below)
    
    // Hide the window we just moved
    XUnmapWindow(dpy, moving->win);
    
    // Update focus to next available window in current workspace
    focused = workspaces[current_ws];
    if (focused) {
        focus(focused);
    } else {
        // No windows left, clear focus
        focused = NULL;
    }
    
    // Re-arrange current workspace
    arrange();
}

void fullscreen() {
    if (!focused) return;
    focused->isfullscreen = !focused->isfullscreen;
    arrange();
}

void quit() {
    exit(0);
}

void spawn(const char *cmd) {
    if (fork() == 0) {
        // Close display connection in child
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(1);
    }
}
