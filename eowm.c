/* eowm - eet owter winvow manader (vertical stack, master on right) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <signal.h>

// CONFIGURATION
#define MOD Mod1Mask
#define MASTER_SIZE 0.6
#define MIN_WIDTH 100
#define PADDING 10
#define BORDER_WIDTH 2
#define MAX_WORKSPACES 9
#define MAX_STACK_SIZE 512
// CONFIGURATION END

// STRUCTURES
typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int isfullscreen;
    int workspace;
    Client *next;
};

typedef void (*CmdFunc)(void);
// STRUCTURES END

// GLOBAL VARIABLES
static Display *dpy;
static Window root;
static Client *focused = NULL;
static int screen;
static int sw, sh; // screen width/height
static double master_size = MASTER_SIZE;
static Client *workspaces[MAX_WORKSPACES] = {NULL};
static int current_ws = 0;
static unsigned long border_normal, border_focused;
// GLOBAL VARIABLES END

// FUNCTION DECLARATIONS
//   Event handlers
static void buttonpress(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void keypress(XEvent *e);

//   Helper functions
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
static void cleanup();
static void removeclient(Window win);
static int get_stack_clients(Client *stack[], int max);
static void move_in_stack(int delta);
// FUNCTION DECLARATIONS END

// EVENT HANDLERS
static void (*handlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ConfigureRequest] = configurerequest,
    [MapRequest] = maprequest,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [KeyPress] = keypress
};
// EVENT HANDLERS END

// WORKSPACE MACROS AND FUNCTIONS
#define WS_FUNC(n) \
    static void ws##n() { switchws((n)-1); } \
    static void movews##n() { movewin_to_ws((n)-1); }

WS_FUNC(1) WS_FUNC(2) WS_FUNC(3) WS_FUNC(4)
WS_FUNC(5) WS_FUNC(6) WS_FUNC(7) WS_FUNC(8)
WS_FUNC(9)

#undef WS_FUNC

#define WS_KEY(n) \
    { XK_##n, MOD, (CmdFunc)ws##n, NULL }, \
    { XK_##n, MOD | ShiftMask, (CmdFunc)movews##n, NULL },
// WORKSPACE MACROS AND FUNCTIONS END

// KEY BINDINGS
static struct {
    KeySym keysym;
    unsigned int mod;
    CmdFunc func;
    const char *arg;
} keys[] = {
    // General navigation & management
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

    // Workspaces
    WS_KEY(1) WS_KEY(2) WS_KEY(3) WS_KEY(4) WS_KEY(5)
    WS_KEY(6) WS_KEY(7) WS_KEY(8) WS_KEY(9)
};

#undef WS_KEY
// KEY BINDINGS END

// UTILITY FUNCTIONS
static void setup_colors(void) {
    Colormap cmap = DefaultColormap(dpy, screen);
    XColor color;

    XParseColor(dpy, cmap, "#444444", &color);
    XAllocColor(dpy, cmap, &color);
    border_normal = color.pixel;

    XParseColor(dpy, cmap, "#5588ff", &color);
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
// UTILITY FUNCTIONS END

// MAIN FUNCTION
int main() {
    XEvent ev;
    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    XSetErrorHandler(xerrorHandler);
    signal(SIGCHLD, sigchld_handler);
    
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    setup_colors();
    
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
    
    Client *c = calloc(1, sizeof(Client));
    if (!c) {
        fprintf(stderr, "Failed to allocate client\n");
        return;
    }
    
    c->win = ev->window;
    c->workspace = current_ws;
    c->next = workspaces[current_ws];
    workspaces[current_ws] = c;
    
    XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
    XSetWindowBorder(dpy, c->win, border_normal); 
    
    XSelectInput(dpy, c->win, 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
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
            if (keys[i].arg) {
                spawn(keys[i].arg);
            } else {
                keys[i].func();
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
    XMoveResizeWindow(dpy, c->win, x, y, w - 2 * BORDER_WIDTH, h - 2 * BORDER_WIDTH);
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
        XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
        XMapWindow(dpy, c->win);
    }
    
    // Count clients
    int n = 0;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        n++;
    }
    if (n == 0) return;

    int usable_w = sw - PADDING * 2;
    int usable_h = sh - PADDING * 2;

    // Special case: single window fills entire usable area
    if (n == 1) {
        Client *only = workspaces[current_ws];
        resize(only, PADDING, PADDING, usable_w, usable_h);
        XMapWindow(dpy, only->win);
        if (focused) XRaiseWindow(dpy, focused->win);
        return;
    }

    // Normal master-stack layout for n >= 2
    int mw = (int)(usable_w * master_size);
    int stack_w = usable_w - mw - PADDING;

    Client *master = workspaces[current_ws];
    if (master) {
        int x = sw - mw - PADDING;  // master on right
        int y = PADDING;
        resize(master, x, y, mw, usable_h);
        XMapWindow(dpy, master->win);
    }

    // Arrange stack (n-1 windows on left)
    int stack_count = n - 1;
    int th = usable_h / stack_count;
    int y = PADDING;
    for (Client *c = workspaces[current_ws]->next; c; c = c->next) {
        int h = (c->next) ? th : (usable_h - (y - PADDING));
        if (h < MIN_WIDTH) h = MIN_WIDTH;
        resize(c, PADDING, y, stack_w, h);
        XMapWindow(dpy, c->win);
        y += h + PADDING;
    }

    if (focused) {
        XRaiseWindow(dpy, focused->win);
    }
}
// CLIENT MANAGEMENT FUNCTIONS END

// WINDOW MANAGEMENT FUNCTIONS
void killclient() {
    if (focused) XKillClient(dpy, focused->win);
}

void togglemaster() {
    if (!workspaces[current_ws] || !workspaces[current_ws]->next) return;
    
    Client *first = workspaces[current_ws];
    Client *second = workspaces[current_ws]->next;
    
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

    Client *stack[MAX_STACK_SIZE];
    int n = get_stack_clients(stack, MAX_STACK_SIZE);
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

void movewinup()   { move_in_stack(-1); }
void movewindown() { move_in_stack(+1); }
// WINDOW MANAGEMENT FUNCTIONS END

// WORKSPACE MANAGEMENT FUNCTIONS
static void switchws(int ws) {
    if (ws < 0 || ws >= MAX_WORKSPACES || ws == current_ws) return;
    
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

static void movewin_to_ws(int ws) {
    if (!focused || ws < 0 || ws >= MAX_WORKSPACES || ws == current_ws) return;
    
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
void fullscreen() {
    if (!focused) return;
    
    if (focused->isfullscreen) {
        // Exit fullscreen
        focused->isfullscreen = 0;
        XSetWindowBorderWidth(dpy, focused->win, BORDER_WIDTH);
        XSetWindowBorder(dpy, focused->win, border_focused);
        arrange();  // arrange() now handles remapping all windows
    } else {
        // Enter fullscreen
        focused->isfullscreen = 1;
        arrange();  // arrange() handles hiding others and setting up fullscreen
    }
}
// SPECIAL WINDOW FUNCTIONS END

// SYSTEM FUNCTIONS
static void cleanup() {
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        Client *c = workspaces[i];
        while (c) {
            Client *next = c->next;
            free(c);
            c = next;
        }
    }
}

void quit() {
    cleanup();
    XCloseDisplay(dpy);
    exit(0);
}

void spawn(const char *cmd) {
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Fork failed\n");
        return;
    }
    if (pid == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        fprintf(stderr, "Exec failed\n");
        exit(1);
    }
}
// SYSTEM FUNCTIONS END