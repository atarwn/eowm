/* eowm - eet owter winvow manader */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define HASH_SIZE       256
#define MAX_COLUMNS     16
#define CLIENTS_PER_COL 256

#define VERSION "2.0"

typedef union {
	int i;
	const char *cmd;
} Arg;

typedef struct Client Client;
struct Client {
	Window win;
	int x, y, w, h;
	int workspace;
	int column;
	unsigned int isfullscreen:1;
	unsigned int ishidden:1;
	unsigned int isfloating:1;
	Client *next;
	Client *hash_next;
};

typedef struct {
	int count;
	Client **clients;
} Column;

typedef struct {
	Column columns[MAX_COLUMNS];
	int num_columns;
	int focused_column;
} Workspace;

typedef struct {
	const char *class;
	const char *instance;
	int workspace;
	int isfloating;
	int monitor;
} Rule;

typedef struct StrutWindow StrutWindow;
struct StrutWindow {
	Window win;
	long struts[4];
	StrutWindow *next;
};

typedef struct Monitor Monitor;
struct Monitor {
	int x, y, w, h;
	int num;
	Monitor *next;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* function declarations */
static void arrange(void);
static void buttonpress(XEvent *e);
static void cleanup(void);
static void columnadd(Workspace *ws, int col, Client *c);
static void columnremove(Workspace *ws, Client *c);
static void compact(Workspace *ws);
static void configurerequest(XEvent *e);
static Client *createclient(Window win, int floating);
static void destroynotify(XEvent *e);
static void die(const char *fmt, ...);
static void enternotify(XEvent *e);
static void focus(Client *c);
static void focuscolumn(const Arg *arg);
static void focusmonitor(const Arg *arg);
static void fullscreen(const Arg *arg);
static Client *hashfind(Window win);
static void hashinsert(Client *c);
static void hashremove(Window win);
static void initworkspaces(void);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void maprequest(XEvent *e);
static Rule *matchrule(Window win);
static void movecolumn(const Arg *arg);
static void movemonitor(const Arg *arg);
static void movews(const Arg *arg);
static void nextwin(const Arg *arg);
static void prevwin(const Arg *arg);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static void removeclient(Window win);
static void removestrutwin(Window win);
static void resize(Client *c, int x, int y, int w, int h);
static void scan(void);
static void screenchange(XEvent *e);
static void setcolors(void);
static void setrootbg(void);
static void setupatoms(void);
static void spawn(const Arg *arg);
static void switchws(const Arg *arg);
static void unmapnotify(XEvent *e);
static void updateclientlist(void);
static void updateewmh(void);
static Monitor *updatemonitors(void);
static void updatestruts(void);

static Monitor *getat(int x, int y);
static Monitor *getforwin(Client *c);
static int getstruts(Window win, long struts[4]);
static int wintype(Window win, const char *type_name);

#include "config.h"

/* variables */
static Display *dpy;
static Window root;
static Client *focused;
static Client *hashtable[HASH_SIZE];
static int screen;
static int sw, sh;
static Workspace workspaces[9];
static int currentws;
static Monitor *monitors;
static Monitor *currentmon;
static int moncount;
static unsigned long bordernormal, borderfocused;
static Atom wmatoms[10];
static Atom netatoms[10];
static StrutWindow *strutwindows;
static int strutleft, strutright, struttop, strutbottom;

static void (*handler[LASTEvent])(XEvent *) = {
	[ButtonPress]      = buttonpress,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify]    = destroynotify,
	[EnterNotify]      = enternotify,
	[KeyPress]         = keypress,
	[MapRequest]       = maprequest,
	[PropertyNotify]   = propertynotify,
	[UnmapNotify]      = unmapnotify,
	[RRScreenChangeNotify + RRNotify] = screenchange,
};

void
arrange(void)
{
	Workspace *ws;
	Column *col;
	Client *c;
	Monitor *m;
	int i, j, x, y, x0, y0, usablew, usableh;
	int totalclients, activecols, colwidth, winheight;

	ws = &workspaces[currentws];

	/* handle fullscreen */
	for (i = 0; i < ws->num_columns; i++) {
		col = &ws->columns[i];
		for (j = 0; j < col->count; j++) {
			c = col->clients[j];
			if (!c->isfullscreen)
				continue;
			m = getforwin(c);
			XSetWindowBorderWidth(dpy, c->win, 0);
			resize(c, m->x, m->y, m->w, m->h);
			XMapWindow(dpy, c->win);
			XRaiseWindow(dpy, c->win);
			/* hide all other windows */
			for (int ci = 0; ci < ws->num_columns; ci++) {
				for (int cj = 0; cj < ws->columns[ci].count; cj++) {
					Client *other = ws->columns[ci].clients[cj];
					if (other != c) {
						other->ishidden = 1;
						XUnmapWindow(dpy, other->win);
					}
				}
			}
			return;
		}
	}

	/* restore all windows */
	for (i = 0; i < ws->num_columns; i++) {
		col = &ws->columns[i];
		for (j = 0; j < col->count; j++) {
			c = col->clients[j];
			c->ishidden = 0;
			XSetWindowBorderWidth(dpy, c->win, border_width);
			XMapWindow(dpy, c->win);
		}
	}

	if (!currentmon)
		return;

	x0 = currentmon->x + strutleft + padding;
	y0 = currentmon->y + struttop + padding;
	usablew = currentmon->w - strutleft - strutright - 2 * padding;
	usableh = currentmon->h - struttop - strutbottom - 2 * padding;

	totalclients = 0;
	for (i = 0; i < ws->num_columns; i++)
		totalclients += ws->columns[i].count;

	if (totalclients == 0)
		return;

	activecols = 0;
	for (i = 0; i < ws->num_columns; i++)
		if (ws->columns[i].count > 0)
			activecols++;

	if (activecols == 0)
		return;

	colwidth = (usablew - (activecols - 1) * padding) / activecols;
	x = x0;

	for (i = 0; i < ws->num_columns; i++) {
		col = &ws->columns[i];
		if (col->count == 0)
			continue;

		y = y0;
		winheight = (usableh - (col->count - 1) * padding) / col->count;

		for (j = 0; j < col->count; j++) {
			c = col->clients[j];
			if (c->isfloating)
				continue;

			int h = (j == col->count - 1)
			    ? (usableh - (y - y0))
			    : winheight;
			resize(c, x, y, colwidth, h);
			y += h + padding;
		}

		x += colwidth + padding;
	}

	/* raise floating windows */
	for (i = 0; i < ws->num_columns; i++)
		for (j = 0; j < ws->columns[i].count; j++)
			if (ws->columns[i].clients[j]->isfloating)
				XRaiseWindow(dpy, ws->columns[i].clients[j]->win);

	if (focused)
		XRaiseWindow(dpy, focused->win);
}

void
buttonpress(XEvent *e)
{
	Client *c;

	c = hashfind(e->xbutton.subwindow);
	if (c)
		focus(c);
}

void
cleanup(void)
{
	Workspace *ws;
	Monitor *m;
	StrutWindow *sw;
	int i, j;

	for (i = 0; i < 9; i++) {
		ws = &workspaces[i];
		for (j = 0; j < ws->num_columns; j++)
			free(ws->columns[j].clients);
	}

	while (strutwindows) {
		sw = strutwindows->next;
		free(strutwindows);
		strutwindows = sw;
	}

	while (monitors) {
		m = monitors->next;
		free(monitors);
		monitors = m;
	}
}

void
columnadd(Workspace *ws, int col, Client *c)
{
	Column *column;

	if (col < 0)
		col = 0;
	if (col >= ws->num_columns)
		col = ws->num_columns - 1;

	column = &ws->columns[col];
	column->clients[column->count++] = c;
	c->column = col;
}

void
columnremove(Workspace *ws, Client *c)
{
	Column *column;
	int col, idx, i;

	col = idx = -1;
	for (i = 0; i < ws->num_columns; i++) {
		column = &ws->columns[i];
		for (int j = 0; j < column->count; j++) {
			if (column->clients[j] == c) {
				col = i;
				idx = j;
				break;
			}
		}
		if (col >= 0)
			break;
	}

	if (col < 0)
		return;

	column = &ws->columns[col];
	for (i = idx; i < column->count - 1; i++)
		column->clients[i] = column->clients[i + 1];
	column->count--;

	compact(ws);
}

void
compact(Workspace *ws)
{
	int i, j, write;

	write = 0;
	for (i = 0; i < ws->num_columns; i++) {
		if (ws->columns[i].count > 0) {
			if (write != i) {
				ws->columns[write] = ws->columns[i];
				ws->columns[i].clients = NULL;
				ws->columns[i].count = 0;
			}
			/* update column indices */
			for (j = 0; j < ws->columns[write].count; j++)
				ws->columns[write].clients[j]->column = write;
			write++;
		} else {
			if (ws->columns[i].clients)
				free(ws->columns[i].clients);
			ws->columns[i].clients = NULL;
		}
	}

	ws->num_columns = write;
	if (ws->num_columns == 0) {
		ws->num_columns = 1;
		ws->columns[0].count = 0;
		ws->columns[0].clients = calloc(CLIENTS_PER_COL, sizeof(Client *));
	}

	if (ws->focused_column >= ws->num_columns)
		ws->focused_column = ws->num_columns - 1;
}

void
configurerequest(XEvent *e)
{
	XConfigureRequestEvent *ev;
	XWindowChanges wc;

	ev = &e->xconfigurerequest;
	wc.x = ev->x;
	wc.y = ev->y;
	wc.width = ev->width;
	wc.height = ev->height;
	wc.border_width = ev->border_width;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;
	XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
}

Client *
createclient(Window win, int floating)
{
	Client *c;
	Rule *rule;
	Workspace *ws;

	c = calloc(1, sizeof(Client));
	if (!c)
		return NULL;

	rule = matchrule(win);

	c->win = win;
	c->workspace = (rule && rule->workspace >= 0) ? rule->workspace : currentws;
	c->isfloating = rule ? rule->isfloating : floating;
	c->column = 0;

	hashinsert(c);

	if (currentmon) {
		if (c->isfloating) {
			c->x = currentmon->x + currentmon->w / 4;
			c->y = currentmon->y + currentmon->h / 4;
			c->w = currentmon->w / 2;
			c->h = currentmon->h / 2;
		} else {
			c->x = currentmon->x;
			c->y = currentmon->y;
		}
	}

	XSetWindowBorderWidth(dpy, c->win, border_width);
	XSetWindowBorder(dpy, c->win, bordernormal);
	XSelectInput(dpy, c->win, EnterWindowMask | PropertyChangeMask | StructureNotifyMask);
	XMapWindow(dpy, c->win);

	ws = &workspaces[c->workspace];
	columnadd(ws, ws->focused_column, c);

	return c;
}

void
destroynotify(XEvent *e)
{
	StrutWindow *sw;

	for (sw = strutwindows; sw; sw = sw->next) {
		if (sw->win == e->xdestroywindow.window) {
			removestrutwin(e->xdestroywindow.window);
			arrange();
			return;
		}
	}
	removeclient(e->xdestroywindow.window);
}

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void
enternotify(XEvent *e)
{
	Client *c;

	if (e->xcrossing.mode != NotifyNormal || e->xcrossing.detail == NotifyInferior)
		return;
	c = hashfind(e->xcrossing.window);
	if (c)
		focus(c);
}

void
focus(Client *c)
{
	Workspace *ws;
	XWindowAttributes wa;

	if (!c || c->ishidden || c->workspace != currentws)
		return;

	if (!XGetWindowAttributes(dpy, c->win, &wa))
		return;

	if (focused && focused != c)
		XSetWindowBorder(dpy, focused->win, bordernormal);

	focused = c;
	ws = &workspaces[currentws];
	ws->focused_column = c->column;

	XSetWindowBorder(dpy, c->win, borderfocused);
	XRaiseWindow(dpy, c->win);
	XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	updateewmh();
}

void
focuscolumn(const Arg *arg)
{
	Workspace *ws;
	int dir, newcol;

	ws = &workspaces[currentws];
	dir = arg->i;
	newcol = ws->focused_column;

	do {
		newcol += dir;
		if (newcol < 0 || newcol >= ws->num_columns)
			return;
	} while (ws->columns[newcol].count == 0);

	ws->focused_column = newcol;
	focus(ws->columns[newcol].clients[0]);
}

void
focusmonitor(const Arg *arg)
{
	Monitor *target, *last, *m;
	int dir;

	if (!monitors || moncount <= 1)
		return;

	dir = arg->i;
	target = NULL;

	if (dir > 0) {
		target = currentmon
		    ? (currentmon->next ? currentmon->next : monitors)
		    : monitors;
	} else {
		last = monitors;
		while (last && last->next)
			last = last->next;

		if (!currentmon || currentmon == monitors) {
			target = last;
		} else {
			m = monitors;
			while (m && m->next != currentmon)
				m = m->next;
			target = m;
		}
	}

	if (!target)
		target = monitors;
	currentmon = target;

	XWarpPointer(dpy, None, root, 0, 0, 0, 0,
	    target->x + target->w / 2, target->y + target->h / 2);
	XFlush(dpy);
}

void
fullscreen(const Arg *arg)
{
	if (!focused)
		return;

	focused->isfullscreen = !focused->isfullscreen;

	if (focused->isfullscreen) {
		XSetWindowBorderWidth(dpy, focused->win, 0);
	} else {
		XSetWindowBorderWidth(dpy, focused->win, border_width);
		XSetWindowBorder(dpy, focused->win, borderfocused);
	}

	arrange();
}

Client *
hashfind(Window win)
{
	Client *c;
	unsigned int h;

	h = (unsigned int)win % HASH_SIZE;
	for (c = hashtable[h]; c; c = c->hash_next)
		if (c->win == win)
			return c;
	return NULL;
}

void
hashinsert(Client *c)
{
	unsigned int h;

	h = (unsigned int)c->win % HASH_SIZE;
	c->hash_next = hashtable[h];
	hashtable[h] = c;
}

void
hashremove(Window win)
{
	Client *c, **pp;
	unsigned int h;

	h = (unsigned int)win % HASH_SIZE;
	pp = &hashtable[h];
	for (c = hashtable[h]; c; c = c->hash_next) {
		if (c->win == win) {
			*pp = c->hash_next;
			return;
		}
		pp = &c->hash_next;
	}
}

void
initworkspaces(void)
{
	int i;

	for (i = 0; i < 9; i++) {
		workspaces[i].num_columns = 1;
		workspaces[i].focused_column = 0;
		workspaces[i].columns[0].count = 0;
		workspaces[i].columns[0].clients = calloc(CLIENTS_PER_COL, sizeof(Client *));
	}
}

void
keypress(XEvent *e)
{
	KeySym keysym;
	unsigned int state;
	size_t i;

	keysym = XLookupKeysym(&e->xkey, 0);
	state = e->xkey.state & ~(LockMask | Mod2Mask);
	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (keysym == keys[i].keysym && state == keys[i].mod) {
			if (keys[i].func)
				keys[i].func(&keys[i].arg);
			break;
		}
	}
}

void
killclient(const Arg *arg)
{
	if (focused)
		XKillClient(dpy, focused->win);
}

void
maprequest(XEvent *e)
{
	XMapRequestEvent *ev;
	XWindowAttributes wa;
	Window trans;
	Client *c;
	StrutWindow *sw;
	long struts[4];
	int floating;

	ev = &e->xmaprequest;
	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;

	if (wa.override_redirect) {
		XMapWindow(dpy, ev->window);
		return;
	}

	if (hashfind(ev->window)) {
		XMapWindow(dpy, ev->window);
		return;
	}

	if (wintype(ev->window, "_NET_WM_WINDOW_TYPE_NOTIFICATION") ||
	    wintype(ev->window, "_NET_WM_WINDOW_TYPE_SPLASH")) {
		XMapWindow(dpy, ev->window);
		return;
	}

	if (wintype(ev->window, "_NET_WM_WINDOW_TYPE_DOCK")) {
		if (getstruts(ev->window, struts)) {
			sw = calloc(1, sizeof(StrutWindow));
			if (sw) {
				sw->win = ev->window;
				memcpy(sw->struts, struts, sizeof(struts));
				sw->next = strutwindows;
				strutwindows = sw;
				updatestruts();
			}
		}
		XMapWindow(dpy, ev->window);
		arrange();
		return;
	}

	if (getstruts(ev->window, struts)) {
		sw = calloc(1, sizeof(StrutWindow));
		if (sw) {
			sw->win = ev->window;
			memcpy(sw->struts, struts, sizeof(struts));
			sw->next = strutwindows;
			strutwindows = sw;
			updatestruts();
		}
		XMapWindow(dpy, ev->window);
		arrange();
		return;
	}

	trans = None;
	floating = (XGetTransientForHint(dpy, ev->window, &trans) && trans != None);
	c = createclient(ev->window, floating);
	if (c) {
		if (floating) {
			XRaiseWindow(dpy, c->win);
			focus(c);
		} else {
			focus(c);
			arrange();
		}
		updateclientlist();
	}
}

Rule *
matchrule(Window win)
{
	XClassHint ch;
	size_t i;

	ch.res_class = ch.res_name = NULL;
	if (!XGetClassHint(dpy, win, &ch))
		return NULL;

	for (i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
		if ((rules[i].class && ch.res_class &&
		    !strcmp(ch.res_class, rules[i].class)) ||
		    (rules[i].instance && ch.res_name &&
		    !strcmp(ch.res_name, rules[i].instance))) {
			if (ch.res_class)
				XFree(ch.res_class);
			if (ch.res_name)
				XFree(ch.res_name);
			return &rules[i];
		}
	}

	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	return NULL;
}

void
movecolumn(const Arg *arg)
{
    Workspace *ws;
    Client *moving;
    int dir, targetcol;

    if (!focused || focused->isfloating)
        return;

    ws = &workspaces[currentws];
    moving = focused;
    dir = arg->i;
    
    columnremove(ws, moving);

    targetcol = moving->column + dir;

    if (targetcol < 0) 
        targetcol = 0;

    if (targetcol >= ws->num_columns) {
        if (ws->num_columns < MAX_COLUMNS) {
            targetcol = ws->num_columns;
            ws->num_columns++;
            ws->columns[targetcol].count = 0;
            ws->columns[targetcol].clients = calloc(CLIENTS_PER_COL, sizeof(Client *));
        } else {
            targetcol = ws->num_columns - 1;
        }
    }

    columnadd(ws, targetcol, moving);

    ws->focused_column = moving->column;
    arrange();
}

void
movemonitor(const Arg *arg)
{
	Monitor *current, *target, *last, *m;
	int dir;

	if (!focused || !monitors || moncount <= 1 || focused->isfloating)
		return;

	dir = arg->i;
	current = getforwin(focused);
	target = NULL;

	if (dir > 0) {
		target = current
		    ? (current->next ? current->next : monitors)
		    : monitors;
	} else {
		last = monitors;
		while (last && last->next)
			last = last->next;

		if (!current || current == monitors) {
			target = last;
		} else {
			m = monitors;
			while (m && m->next != current)
				m = m->next;
			target = m;
		}
	}

	if (!target || target == current)
		return;

	focused->x = target->x + padding;
	focused->y = target->y + padding;

	arrange();
	focus(focused);

	XWarpPointer(dpy, None, root, 0, 0, 0, 0,
	    focused->x + focused->w / 2, focused->y + focused->h / 2);
	XFlush(dpy);
}

void
movews(const Arg *arg)
{
	Workspace *src, *dst;
	Client *moving;
	int ws, i;

	ws = arg->i;
	if (!focused || ws < 0 || ws >= 9 || ws == currentws)
		return;

	moving = focused;
	src = &workspaces[currentws];
	dst = &workspaces[ws];

	columnremove(src, moving);
	moving->workspace = ws;
	moving->isfullscreen = 0;
	XUnmapWindow(dpy, moving->win);

	columnadd(dst, 0, moving);

	focused = NULL;
	for (i = 0; i < src->num_columns; i++) {
		if (src->columns[i].count > 0) {
			focus(src->columns[i].clients[0]);
			break;
		}
	}

	arrange();
	updateclientlist();
}

void
nextwin(const Arg *arg)
{
	Workspace *ws;
	Column *col;
	int i, next;

	if (!focused)
		return;

	ws = &workspaces[currentws];
	col = &ws->columns[focused->column];

	if (col->count <= 1)
		return;

	for (i = 0; i < col->count; i++) {
		if (col->clients[i] == focused) {
			next = (i + 1) % col->count;
			focus(col->clients[next]);
			return;
		}
	}
}

void
prevwin(const Arg *arg)
{
	Workspace *ws;
	Column *col;
	int i, prev;

	if (!focused)
		return;

	ws = &workspaces[currentws];
	col = &ws->columns[focused->column];

	if (col->count <= 1)
		return;

	for (i = 0; i < col->count; i++) {
		if (col->clients[i] == focused) {
			prev = (i - 1 + col->count) % col->count;
			focus(col->clients[prev]);
			return;
		}
	}
}

void
propertynotify(XEvent *e)
{
	StrutWindow *sw;

	if (e->xproperty.atom == netatoms[6] ||
	    e->xproperty.atom == netatoms[7]) {
		for (sw = strutwindows; sw; sw = sw->next) {
			if (sw->win == e->xproperty.window) {
				if (getstruts(sw->win, sw->struts)) {
					updatestruts();
					arrange();
				}
				return;
			}
		}
	}
}

void
quit(const Arg *arg)
{
	cleanup();
	XCloseDisplay(dpy);
	exit(0);
}

void
removeclient(Window win)
{
	Workspace *ws;
	Client *c;
	int wasfocused, i;

	c = hashfind(win);
	if (!c)
		return;

	wasfocused = (focused == c);
	ws = &workspaces[c->workspace];

	columnremove(ws, c);
	hashremove(win);
	XSelectInput(dpy, c->win, NoEventMask);
	free(c);

	if (wasfocused) {
		focused = NULL;
		for (i = 0; i < ws->num_columns; i++) {
			if (ws->columns[i].count > 0) {
				focus(ws->columns[i].clients[0]);
				break;
			}
		}
	}

	arrange();
	updateclientlist();
}

void
removestrutwin(Window win)
{
	StrutWindow *sw, **prev;

	prev = &strutwindows;
	for (sw = strutwindows; sw; sw = sw->next) {
		if (sw->win == win) {
			*prev = sw->next;
			free(sw);
			updatestruts();
			return;
		}
		prev = &sw->next;
	}
}

void
resize(Client *c, int x, int y, int w, int h)
{
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	XMoveResizeWindow(dpy, c->win, x, y,
	    w - 2 * border_width, h - 2 * border_width);
}

void
scan(void)
{
	XWindowAttributes wa;
	XEvent e;
	Window d1, d2, *wins;
	unsigned int i, num;

	wins = NULL;
	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa) ||
			    wa.override_redirect)
				continue;
			if (wa.map_state == IsViewable) {
				e.type = MapRequest;
				e.xmaprequest.window = wins[i];
				maprequest(&e);
			}
		}
		if (wins)
			XFree(wins);
	}
}

void
screenchange(XEvent *e)
{
	XRRUpdateConfiguration(e);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	updatemonitors();
	arrange();
}

void
setcolors(void)
{
	Colormap cmap;
	XColor color;

	cmap = DefaultColormap(dpy, screen);
	XParseColor(dpy, cmap, col_border_normal, &color);
	XAllocColor(dpy, cmap, &color);
	bordernormal = color.pixel;
	XParseColor(dpy, cmap, col_border_focused, &color);
	XAllocColor(dpy, cmap, &color);
	borderfocused = color.pixel;
}

void
setrootbg(void)
{
	Colormap cmap;
	XColor color;

	cmap = DefaultColormap(dpy, screen);
	if (XParseColor(dpy, cmap, root_bg, &color) &&
	    XAllocColor(dpy, cmap, &color)) {
		XSetWindowBackground(dpy, root, color.pixel);
		XClearWindow(dpy, root);
	}
}

void
setupatoms(void)
{
	wmatoms[0] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatoms[1] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatoms[2] = XInternAtom(dpy, "WM_STATE", False);
	wmatoms[3] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);

	netatoms[0] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatoms[1] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatoms[2] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatoms[3] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	netatoms[4] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatoms[5] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatoms[6] = XInternAtom(dpy, "_NET_WM_STRUT", False);
	netatoms[7] = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
	netatoms[8] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execl("/bin/sh", "sh", "-c", arg->cmd, (char *)NULL);
		fprintf(stderr, "eowm: exec failed\n");
		exit(1);
	}
}

void
switchws(const Arg *arg)
{
	Workspace *old, *new;
	int ws, i, j;

	ws = arg->i;
	if (ws < 0 || ws >= 9 || ws == currentws)
		return;

	old = &workspaces[currentws];
	currentws = ws;

	for (i = 0; i < old->num_columns; i++) {
		for (j = 0; j < old->columns[i].count; j++) {
			old->columns[i].clients[j]->ishidden = 1;
			XUnmapWindow(dpy, old->columns[i].clients[j]->win);
		}
	}

	new = &workspaces[currentws];
	for (i = 0; i < new->num_columns; i++) {
		for (j = 0; j < new->columns[i].count; j++) {
			new->columns[i].clients[j]->ishidden = 0;
			XMapWindow(dpy, new->columns[i].clients[j]->win);
			XSetWindowBorder(dpy, new->columns[i].clients[j]->win,
			    bordernormal);
		}
	}

	focused = NULL;
	for (i = 0; i < new->num_columns; i++) {
		if (new->columns[i].count > 0) {
			focus(new->columns[i].clients[0]);
			break;
		}
	}

	arrange();
	updateewmh();
}

void
unmapnotify(XEvent *e)
{
	XUnmapEvent *ev;
	StrutWindow *sw;
	Client *c;

	ev = &e->xunmap;
	if (ev->send_event)
		return;

	for (sw = strutwindows; sw; sw = sw->next) {
		if (sw->win == ev->window) {
			removestrutwin(ev->window);
			arrange();
			return;
		}
	}

	c = hashfind(ev->window);
	if (!c || c->ishidden || c->workspace != currentws)
		return;

	removeclient(ev->window);
}

void
updateclientlist(void)
{
	Workspace *ws;
	Window *wins;
	int i, j, k, count, idx;

	wins = NULL;
	count = 0;

	for (i = 0; i < 9; i++) {
		ws = &workspaces[i];
		for (j = 0; j < ws->num_columns; j++)
			count += ws->columns[j].count;
	}

	if (count > 0) {
		wins = malloc(count * sizeof(Window));
		idx = 0;
		for (i = 0; i < 9; i++) {
			ws = &workspaces[i];
			for (j = 0; j < ws->num_columns; j++) {
				for (k = 0; k < ws->columns[j].count; k++)
					wins[idx++] = ws->columns[j].clients[k]->win;
			}
		}
	}

	XChangeProperty(dpy, root, netatoms[4], XA_WINDOW, 32,
	    PropModeReplace, (unsigned char *)wins, count);
	if (wins)
		free(wins);
}

void
updateewmh(void)
{
	long data;
	char names[] = "1\0002\0003\0004\0005\0006\0007\0008\0009";

	data = currentws;
	XChangeProperty(dpy, root, netatoms[1], XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)&data, 1);
	data = 9;
	XChangeProperty(dpy, root, netatoms[2], XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)&data, 1);

	XChangeProperty(dpy, root, netatoms[3],
	    XInternAtom(dpy, "UTF8_STRING", False), 8,
	    PropModeReplace, (unsigned char *)names, sizeof(names) - 1);

	if (focused) {
		XChangeProperty(dpy, root, netatoms[0], XA_WINDOW, 32,
		    PropModeReplace, (unsigned char *)&focused->win, 1);
	}
}

Monitor *
updatemonitors(void)
{
	XRRScreenResources *sr;
	XRRCrtcInfo *ci;
	Monitor *m, *mon;
	int i;

	while (monitors) {
		m = monitors->next;
		free(monitors);
		monitors = m;
	}
	monitors = NULL;
	currentmon = NULL;
	moncount = 0;

	sr = XRRGetScreenResources(dpy, root);
	if (!sr)
		goto fallback;

	for (i = 0; i < sr->ncrtc; i++) {
		ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]);
		if (!ci || ci->noutput == 0 || ci->width == 0 || ci->height == 0) {
			if (ci)
				XRRFreeCrtcInfo(ci);
			continue;
		}

		mon = calloc(1, sizeof(Monitor));
		mon->num = moncount++;
		mon->x = ci->x;
		mon->y = ci->y;
		mon->w = ci->width;
		mon->h = ci->height;
		mon->next = monitors;
		monitors = mon;

		XRRFreeCrtcInfo(ci);
	}
	XRRFreeScreenResources(sr);

fallback:
	if (!monitors) {
		monitors = calloc(1, sizeof(Monitor));
		monitors->x = 0;
		monitors->y = 0;
		monitors->w = sw;
		monitors->h = sh;
		monitors->num = 0;
		moncount = 1;
	}
	currentmon = monitors;
	return monitors;
}

void
updatestruts(void)
{
	StrutWindow *sw;

	strutleft = strutright = struttop = strutbottom = 0;
	for (sw = strutwindows; sw; sw = sw->next) {
		if (sw->struts[0] > strutleft)
			strutleft = sw->struts[0];
		if (sw->struts[1] > strutright)
			strutright = sw->struts[1];
		if (sw->struts[2] > struttop)
			struttop = sw->struts[2];
		if (sw->struts[3] > strutbottom)
			strutbottom = sw->struts[3];
	}
}

Monitor *
getat(int x, int y)
{
	Monitor *m;

	for (m = monitors; m; m = m->next)
		if (x >= m->x && x < m->x + m->w &&
		    y >= m->y && y < m->y + m->h)
			return m;
	return monitors;
}

Monitor *
getforwin(Client *c)
{
	return getat(c->x + c->w / 2, c->y + c->h / 2);
}

int
getstruts(Window win, long struts[4])
{
	Atom types[] = {netatoms[7], netatoms[6]};
	Atom actual;
	unsigned long n, after;
	unsigned char *data;
	long *vals;
	int fmt, t, i, hasstruts;

	data = NULL;
	for (t = 0; t < 2; t++) {
		if (XGetWindowProperty(dpy, win, types[t], 0, 4, False,
		    XA_CARDINAL, &actual, &fmt, &n, &after, &data) == Success) {
			if (actual == XA_CARDINAL && fmt == 32 && n >= 4) {
				vals = (long *)data;
				hasstruts = 0;
				for (i = 0; i < 4; i++) {
					struts[i] = vals[i];
					if (vals[i] > 0)
						hasstruts = 1;
				}
				XFree(data);
				return hasstruts;
			}
			if (data)
				XFree(data);
		}
	}
	return 0;
}

int
wintype(Window win, const char *type_name)
{
	Atom actual, type, check;
	unsigned long n, after;
	unsigned char *prop;
	int fmt;

	prop = NULL;
	if (XGetWindowProperty(dpy, win, netatoms[8], 0, 1, False, XA_ATOM,
	    &actual, &fmt, &n, &after, &prop) == Success && prop) {
		type = *(Atom *)prop;
		check = XInternAtom(dpy, type_name, False);
		XFree(prop);
		return type == check;
	}
	return 0;
}

static void
sigchld(int sig)
{
	(void)sig;
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static int
xerror(Display *dpy, XErrorEvent *ee)
{
	char msg[256];

	XGetErrorText(dpy, ee->error_code, msg, sizeof(msg));
	fprintf(stderr, "eowm: X error: %s\n", msg);
	return 0;
}

int
main(int argc, char *argv[])
{
	XEvent ev;
	Cursor cursor;
	KeyCode code;
	size_t i;

	if (argc == 2 && !strcmp("-v", argv[1]))
		die("eowm v" VERSION);
	else if (argc != 1)
		die("usage: eowm [-v]");
	if (!getenv("DISPLAY"))
		die("DISPLAY not set");
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");

	XSetErrorHandler(xerror);
	signal(SIGCHLD, sigchld);

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	cursor = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, root, cursor);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	setcolors();
	setrootbg();
	setupatoms();
	initworkspaces();
	updatemonitors();
	updateewmh();

	if (!currentmon && monitors)
		currentmon = monitors;

	XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	XSelectInput(dpy, root, SubstructureRedirectMask |
	    SubstructureNotifyMask | PropertyChangeMask | StructureNotifyMask);

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		code = XKeysymToKeycode(dpy, keys[i].keysym);
		XGrabKey(dpy, code, keys[i].mod, root, True,
		    GrabModeAsync, GrabModeAsync);
	}

	scan();
	while (1) {
		XNextEvent(dpy, &ev);
		if (handler[ev.type])
			handler[ev.type](&ev);
	}
}
