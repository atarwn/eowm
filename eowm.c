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
    Client *next;
};

static Display *dpy;
static Window root;
static Client *clients = NULL;
static Client *focused = NULL;
static int screen;
static int sw, sh; // screen width/height
static double master_size = MASTER_SIZE;
static int num_clients = 0;

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
static void focusup();
static void focusdown();
static void focusleft();
static void focusright();
static void fullscreen();
static void quit();
static void spawn(const char *cmd);

// Key bindings
static struct {
    KeySym keysym;
    unsigned int mod;
    void (*func)(const char*);
    const char *arg;
} keys[] = {
    { XK_j, MOD, (void(*)(const char*))nextwin, NULL },
    { XK_k, MOD, (void(*)(const char*))prevwin, NULL },
    { XK_h, MOD, (void(*)(const char*))incmaster, NULL },
    { XK_l, MOD, (void(*)(const char*))decmaster, NULL },
    { XK_Return, MOD, spawn, "alacritty" },
    { XK_p, MOD, spawn, "dmenu_run" },
    { XK_f, MOD, (void(*)(const char*))fullscreen, NULL },
    { XK_space, MOD, (void(*)(const char*))togglemaster, NULL },
    { XK_q, MOD, (void(*)(const char*))killclient, NULL },
    { XK_c, MOD, (void(*)(const char*))quit, NULL }
};

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
    for (Client *c = clients; c; c = c->next) {
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
    c->next = clients;
    clients = c;
    num_clients++;
    
    XSelectInput(dpy, c->win, 
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
}

void unmapnotify(XEvent *e) {
    Client *c, **prev;
    for (prev = &clients; (c = *prev); prev = &c->next) {
        if (c->win == e->xunmap.window) {
            *prev = c->next;
            free(c);
            num_clients--;
            break;
        }
    }
    if (!clients) focused = NULL;
    else if (focused == c) {
        if (clients) focus(clients);
        else focused = NULL;
    }
    arrange();
}

void destroynotify(XEvent *e) {
    Client *c, **prev;
    for (prev = &clients; (c = *prev); prev = &c->next) {
        if (c->win == e->xdestroywindow.window) {
            *prev = c->next;
            free(c);
            num_clients--;
            break;
        }
    }
    if (!clients) focused = NULL;
    else if (focused == c) {
        // Focus next available client
        if (clients) focus(clients);
        else focused = NULL;
    }
    arrange();
}

void enternotify(XEvent *e) {
    if ((e->xcrossing.mode != NotifyNormal) || 
        (e->xcrossing.detail == NotifyInferior)) return;
        
    for (Client *c = clients; c; c = c->next) {
        if (c->win == e->xcrossing.window) {
            focus(c);
            break;
        }
    }
}

void keypress(XEvent *e) {
    KeySym keysym = XLookupKeysym(&e->xkey, 0);
    for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        if (keysym == keys[i].keysym && 
            (e->xkey.state & MOD) == keys[i].mod) {
            if (keys[i].func) {
                if (keys[i].arg)
                    keys[i].func(keys[i].arg);
                else
                    ((void(*)(void))keys[i].func)();
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
    if (!clients) return;
    
    // Count non-fullscreen clients
    int n = 0;
    for (Client *c = clients; c; c = c->next) {
        if (!c->isfullscreen) n++;
    }
    
    if (n == 0) return;
    
    int mw = sw * master_size;  // Master width
    int th = sh / (n > 1 ? n - 1 : 1);  // Tile height
    
    Client *master = NULL;
    Client *stack = NULL;
    int stack_count = 0;
    
    // Find master (first client) and stack clients
    master = clients;
    for (Client *c = clients->next; c; c = c->next) {
        if (!c->isfullscreen) {
            if (!stack) stack = c;
            stack_count++;
        }
    }
    
    // Arrange master
    if (master && !master->isfullscreen) {
        resize(master, sw - mw, 0, mw, sh);
    }
    
    // Arrange stack
    int i = 0;
    for (Client *c = clients; c; c = c->next) {
        if (c == master || c->isfullscreen) continue;
        
        int ty = i * th;
        int th_actual = (i == stack_count - 1) ? sh - ty : th;
        if (th_actual < MIN_WIDTH) th_actual = MIN_WIDTH;
        resize(c, 0, ty, sw - mw, th_actual);
        i++;
    }
    
    // Handle fullscreen
    for (Client *c = clients; c; c = c->next) {
        if (c->isfullscreen) {
            resize(c, 0, 0, sw, sh);
        }
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
    if (!clients || !clients->next) return;
    
    // Swap first and second client
    Client *first = clients;
    Client *second = clients->next;
    
    // Update links
    first->next = second->next;
    second->next = first;
    clients = second;
    
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
        if (clients) focus(clients);
        return;
    }
    focus(focused->next);
}

void prevwin() {
    if (!focused) return;
    Client *prev = NULL;
    for (Client *c = clients; c; c = c->next) {
        if (c->next == focused) {
            prev = c;
            break;
        }
    }
    if (prev) focus(prev);
    else {
        // Find last client
        Client *last = clients;
        while (last && last->next) last = last->next;
        if (last) focus(last);
    }
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
