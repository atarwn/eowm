/* eowm - grid-based tiling window manager */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define MOD Mod4Mask
#define GRID_ROWS 4
#define GRID_COLS 7
#define PADDING 8

// Grid 4 by 7
static const char grid_chars[GRID_ROWS][GRID_COLS+1] = {
    "1234567",
    "qwertyu",
    "asdfghj",
    "zxcvbnm"
};

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int isfullscreen;
    Client *next;
};

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

static Display *dpy;
static Window root;
static Client *clients = NULL;
static Client *focused = NULL;
static int sw, sh;
static int overlay_mode = 0;
static char overlay_input[3] = {0};
static Window overlay_win = 0;
static GC gc;
static XftDraw *xftdraw = NULL;
static XftFont *font = NULL;
static XftColor col_bg, col_fg, col_sel;

// Forward decls
static void arrange(void);
static void resize(Client *c, int x, int y, int w, int h);
static void focus(Client *c);
static void spawn(const Arg *arg);
static void killclient(const Arg *arg);
static void toggle_fullscreen(const Arg *arg);
static void enter_overlay(const Arg *arg);
static void process_overlay_input(void);
static void draw_overlay(void);
static void hide_overlay(void);
static void quit(const Arg *arg);
static void focusnext(const Arg *arg);
static void focusprev(const Arg *arg);
static void grabkeys(void);

// Commands
static const char *termcmd[] = { "alacritty", NULL };
static const char *menucmd[] = { "dmenu_run", NULL };

// Key bindings
static Key keys[] = {
    /* modifier         key              function         argument */
    { MOD,              XK_t,            enter_overlay,   {0} },
    { MOD,              XK_Return,       spawn,           {.v = termcmd} },
    { MOD,              XK_d,            spawn,           {.v = menucmd} },
    { MOD,              XK_q,            killclient,      {0} },
    { MOD,              XK_f,            toggle_fullscreen, {0} },
    { MOD,              XK_j,            focusnext,       {0} },
    { MOD,              XK_k,            focusprev,       {0} },
    { MOD|ShiftMask,    XK_q,            quit,            {0} },
};

// Event handlers
static void buttonpress(XEvent *e) {
    for (Client *c = clients; c; c = c->next)
        if (c->win == e->xbutton.subwindow) {
            focus(c);
            break;
        }
}

static void maprequest(XEvent *e) {
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, e->xmaprequest.window, &wa)) return;
    if (wa.override_redirect) return;

    Client *c = calloc(1, sizeof(Client));
    c->win = e->xmaprequest.window;
    c->next = clients;
    clients = c;
    XSelectInput(dpy, c->win, EnterWindowMask | FocusChangeMask);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
}

static void unmapnotify(XEvent *e) {
    Client **p;
    for (p = &clients; *p && (*p)->win != e->xunmap.window; p = &(*p)->next);
    if (*p) {
        Client *c = *p;
        if (focused == c) {
            focused = c->next ? c->next : clients;
        }
        *p = c->next;
        free(c);
        arrange();
    }
}

static void destroynotify(XEvent *e) {
    unmapnotify(e);
}

static void enternotify(XEvent *e) {
    if (e->xcrossing.mode != NotifyNormal || e->xcrossing.detail == NotifyInferior)
        return;
    for (Client *c = clients; c; c = c->next)
        if (c->win == e->xcrossing.window) {
            focus(c);
            break;
        }
}

static void expose(XEvent *e) {
    if (e->xexpose.window == overlay_win && overlay_mode) {
        draw_overlay();
    }
}

static void keypress(XEvent *e) {
    if (overlay_mode) {
        KeySym k = XLookupKeysym(&e->xkey, 0);
        if (k == XK_Escape) {
            hide_overlay();
            return;
        }

        char ch = 0;
        if (k >= '0' && k <= '9') ch = (char)k;
        else if (k >= 'a' && k <= 'z') ch = (char)k;
        else if (k >= 'A' && k <= 'Z') ch = (char)(k - 'A' + 'a');
        else return;

        int found = 0;
        for (int r = 0; r < GRID_ROWS && !found; r++)
            for (int c = 0; c < GRID_COLS && !found; c++)
                if (grid_chars[r][c] == ch) found = 1;
        if (!found) return;

        if (overlay_input[0] == 0) {
            overlay_input[0] = ch;
            draw_overlay();
        } else if (overlay_input[1] == 0) {
            overlay_input[1] = ch;
            draw_overlay();
            usleep(150000);
            process_overlay_input();
            hide_overlay();
        }
        return;
    }

    // Обработка обычных клавиш
    KeySym keysym = XLookupKeysym(&e->xkey, 0);
    unsigned int state = e->xkey.state & ~(LockMask | Mod2Mask);

    for (unsigned int i = 0; i < sizeof(keys) / sizeof(Key); i++) {
        if (keysym == keys[i].keysym && state == keys[i].mod && keys[i].func) {
            keys[i].func(&keys[i].arg);
            return;
        }
    }
}

// Core logic
static void resize(Client *c, int x, int y, int w, int h) {
    c->x = x; c->y = y; c->w = w; c->h = h;
    XMoveResizeWindow(dpy, c->win, x, y, w, h);
}

static void focus(Client *c) {
    if (!c) return;
    focused = c;
    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
}

static void arrange(void) {
    if (!clients) return;

    if (!focused) focused = clients;

    if (focused->isfullscreen) {
        resize(focused, 0, 0, sw, sh);
        for (Client *c = clients; c; c = c->next) {
            if (c != focused)
                XMoveWindow(dpy, c->win, sw + 100, sh + 100);
        }
        return;
    }

    // Default window location
    int cell_w = (sw - PADDING * (GRID_COLS + 1)) / GRID_COLS;
    int cell_h = (sh - PADDING * (GRID_ROWS + 1)) / GRID_ROWS;
    int x = PADDING, y = PADDING;

    if (focused->w == 0 || focused->h == 0) {
        resize(focused, x, y, cell_w, cell_h);
    }
}

static void draw_overlay(void) {
    if (!overlay_win) return;

    XClearWindow(dpy, overlay_win);

    int cell_w = (sw - PADDING * (GRID_COLS + 1)) / GRID_COLS;
    int cell_h = (sh - PADDING * (GRID_ROWS + 1)) / GRID_ROWS;

    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    if (overlay_input[0]) {
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++)
                if (grid_chars[r][c] == overlay_input[0]) { r1 = r; c1 = c; }
    }
    if (overlay_input[1]) {
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++)
                if (grid_chars[r][c] == overlay_input[1]) { r2 = r; c2 = c; }
    }

    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int x = PADDING + c * (cell_w + PADDING);
            int y = PADDING + r * (cell_h + PADDING);

            int is_selected = 0;
            if (r1 >= 0 && c1 >= 0) {
                if (r2 >= 0 && c2 >= 0) {
                    int min_r = r1 < r2 ? r1 : r2;
                    int max_r = r1 > r2 ? r1 : r2;
                    int min_c = c1 < c2 ? c1 : c2;
                    int max_c = c1 > c2 ? c1 : c2;
                    if (r >= min_r && r <= max_r && c >= min_c && c <= max_c)
                        is_selected = 1;
                } else if (r == r1 && c == c1) {
                    is_selected = 1;
                }
            }

            if (is_selected) {
                XSetForeground(dpy, gc, col_sel.pixel);
                XFillRectangle(dpy, overlay_win, gc, x, y, cell_w, cell_h);
            }

            XSetForeground(dpy, gc, col_fg.pixel);
            XDrawRectangle(dpy, overlay_win, gc, x, y, cell_w, cell_h);

            // Рисуем символ в центре
            if (font && xftdraw) {
                char txt[2] = {grid_chars[r][c], 0};
                XGlyphInfo extents;
                XftTextExtentsUtf8(dpy, font, (FcChar8*)txt, strlen(txt), &extents);

                int tx = x + (cell_w - extents.width) / 2;
                int ty = y + (cell_h + extents.height) / 2;

                XftDrawStringUtf8(xftdraw, &col_fg, font, tx, ty,
                                (FcChar8*)txt, strlen(txt));
            }
        }
    }

    if (overlay_input[0] || overlay_input[1]) {
        char status[64];
        snprintf(status, sizeof(status), "Input: %s%s",
                overlay_input[0] ? (char[]){overlay_input[0], 0} : "",
                overlay_input[1] ? (char[]){overlay_input[1], 0} : "");

        if (font && xftdraw) {
            XftDrawStringUtf8(xftdraw, &col_fg, font, 20, sh - 20,
                            (FcChar8*)status, strlen(status));
        }
    }

    XFlush(dpy);
}

static void enter_overlay(const Arg *arg) {
    if (!focused) return;

    overlay_mode = 1;
    memset(overlay_input, 0, sizeof(overlay_input));

    if (!overlay_win) {
        XSetWindowAttributes wa = {
            .override_redirect = True,
            .background_pixel = BlackPixel(dpy, DefaultScreen(dpy)),
            .event_mask = ExposureMask | KeyPressMask
        };
        overlay_win = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

        gc = XCreateGC(dpy, overlay_win, 0, NULL);

        unsigned long opacity = (unsigned long)(0.85 * 0xffffffff);
        Atom atom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
        XChangeProperty(dpy, overlay_win, atom, XA_CARDINAL, 32,
                       PropModeReplace, (unsigned char *)&opacity, 1);
    }

    XMapRaised(dpy, overlay_win);
    XSetInputFocus(dpy, overlay_win, RevertToPointerRoot, CurrentTime);
    draw_overlay();
}

static void hide_overlay(void) {
    overlay_mode = 0;
    memset(overlay_input, 0, sizeof(overlay_input));
    if (overlay_win) {
        XUnmapWindow(dpy, overlay_win);
    }
    if (focused) {
        XSetInputFocus(dpy, focused->win, RevertToPointerRoot, CurrentTime);
    }
}

static void process_overlay_input(void) {
    if (!focused || overlay_input[0] == 0 || overlay_input[1] == 0) return;

    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            if (grid_chars[r][c] == overlay_input[0]) { r1 = r; c1 = c; }
            if (grid_chars[r][c] == overlay_input[1]) { r2 = r; c2 = c; }
        }
    }
    if (r1 == -1 || r2 == -1) return;

    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
    if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }

    int cols_span = c2 - c1 + 1;
    int rows_span = r2 - r1 + 1;

    int cell_w = (sw - PADDING * (GRID_COLS + 1)) / GRID_COLS;
    int cell_h = (sh - PADDING * (GRID_ROWS + 1)) / GRID_ROWS;

    int x = PADDING + c1 * (cell_w + PADDING);
    int y = PADDING + r1 * (cell_h + PADDING);
    int w = cols_span * cell_w + (cols_span - 1) * PADDING;
    int h = rows_span * cell_h + (rows_span - 1) * PADDING;

    resize(focused, x, y, w, h);
}

// Action functions
static void killclient(const Arg *arg) {
    if (!focused) return;
    XKillClient(dpy, focused->win);
}

static void toggle_fullscreen(const Arg *arg) {
    if (!focused) return;
    focused->isfullscreen = !focused->isfullscreen;
    arrange();
}

static void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "eowm: execvp %s failed\n", ((char **)arg->v)[0]);
        exit(1);
    }
}

static void quit(const Arg *arg) {
    exit(0);
}

static void focusnext(const Arg *arg) {
    if (!focused || !focused->next) return;
    focus(focused->next);
}

static void focusprev(const Arg *arg) {
    if (!focused || !clients) return;
    Client *c;
    for (c = clients; c && c->next != focused; c = c->next);
    if (c) focus(c);
}

static void grabkeys(void) {
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(Key); i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
        if (code) {
            XGrabKey(dpy, code, keys[i].mod, root, True,
                     GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, code, keys[i].mod | Mod2Mask, root, True,
                     GrabModeAsync, GrabModeAsync);
        }
    }
}

static void sigchld(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int xerror_handler(Display *dpy, XErrorEvent *ee) {
    return 0;
}

static void setup_colors(void) {
    Visual *visual = DefaultVisual(dpy, DefaultScreen(dpy));
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));

    XftColorAllocName(dpy, visual, cmap, "#000000", &col_bg);
    XftColorAllocName(dpy, visual, cmap, "#ffffff", &col_fg);
    XftColorAllocName(dpy, visual, cmap, "#4a90e2", &col_sel);

    font = XftFontOpenName(dpy, DefaultScreen(dpy), "monospace:size=48");
}

int main(void) {
    signal(SIGCHLD, sigchld);
    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "eowm: cannot open display\n");
        exit(1);
    }
    XSetErrorHandler(xerror_handler);

    sw = DisplayWidth(dpy, DefaultScreen(dpy));
    sh = DisplayHeight(dpy, DefaultScreen(dpy));
    root = RootWindow(dpy, DefaultScreen(dpy));

    setup_colors();

    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask |
        EnterWindowMask | LeaveWindowMask | FocusChangeMask);

    grabkeys();

    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case ButtonPress: buttonpress(&ev); break;
            case MapRequest: maprequest(&ev); break;
            case UnmapNotify: unmapnotify(&ev); break;
            case DestroyNotify: destroynotify(&ev); break;
            case EnterNotify: enternotify(&ev); break;
            case KeyPress: keypress(&ev); break;
            case Expose: expose(&ev); break;
        }
    }

    XCloseDisplay(dpy);
    return 0;
}