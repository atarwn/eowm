/* appearance */
static const int border_width           = 2;
static const int padding                = 10;
static const char col_border_focused[]  = "#ececec";
static const char col_border_normal[]   = "#999999";
static const char root_bg[]             = "#1a1a1a";

#define MOD Mod1Mask
#define VERSION "1.0"

/* rules */
static Rule rules[] = {
	/* class      instance    workspace  floating  monitor */
	{ "Gimp",     NULL,       -1,        1,        -1 },
	{ "Toolbox",  NULL,       -1,        1,        -1 },
	{ NULL,       "spterm",   -1,        1,        -1 },
};

/* key bindings */
static Key keys[] = {
	/* modifier           key         function        argument */
	{ MOD,                XK_j,       nextwin,        {0} },
	{ MOD,                XK_k,       prevwin,        {0} },
	{ MOD,                XK_h,       focuscolumn,    {.i = -1} },
	{ MOD,                XK_l,       focuscolumn,    {.i = 1} },
	{ MOD|ShiftMask,      XK_h,       movecolumn,     {.i = -1} },
	{ MOD|ShiftMask,      XK_l,       movecolumn,     {.i = 1} },
	{ MOD,                XK_f,       fullscreen,     {0} },
	{ MOD,                XK_q,       killclient,     {0} },
	{ MOD|ShiftMask,      XK_c,       quit,           {0} },
	{ MOD|ControlMask,    XK_h,       focusmonitor,   {.i = -1} },
	{ MOD|ControlMask,    XK_l,       focusmonitor,   {.i = 1} },
	{ MOD|ControlMask|ShiftMask, XK_h, movemonitor,  {.i = -1} },
	{ MOD|ControlMask|ShiftMask, XK_l, movemonitor,  {.i = 1} },
	{ MOD,                XK_Return,  spawn,          {.cmd = "alacritty"} },
	{ MOD,                XK_p,       spawn,          {.cmd = "dmenu_run"} },
	{ 0,                  XK_Print,   spawn,          {.cmd = "scrot ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
	{ ShiftMask,          XK_Print,   spawn,          {.cmd = "scrot -s ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
	{ MOD,                XK_1,       switchws,       {.i = 0} },
	{ MOD,                XK_2,       switchws,       {.i = 1} },
	{ MOD,                XK_3,       switchws,       {.i = 2} },
	{ MOD,                XK_4,       switchws,       {.i = 3} },
	{ MOD,                XK_5,       switchws,       {.i = 4} },
	{ MOD,                XK_6,       switchws,       {.i = 5} },
	{ MOD,                XK_7,       switchws,       {.i = 6} },
	{ MOD,                XK_8,       switchws,       {.i = 7} },
	{ MOD,                XK_9,       switchws,       {.i = 8} },
	{ MOD|ShiftMask,      XK_1,       movews,         {.i = 0} },
	{ MOD|ShiftMask,      XK_2,       movews,         {.i = 1} },
	{ MOD|ShiftMask,      XK_3,       movews,         {.i = 2} },
	{ MOD|ShiftMask,      XK_4,       movews,         {.i = 3} },
	{ MOD|ShiftMask,      XK_5,       movews,         {.i = 4} },
	{ MOD|ShiftMask,      XK_6,       movews,         {.i = 5} },
	{ MOD|ShiftMask,      XK_7,       movews,         {.i = 6} },
	{ MOD|ShiftMask,      XK_8,       movews,         {.i = 7} },
	{ MOD|ShiftMask,      XK_9,       movews,         {.i = 8} },
};