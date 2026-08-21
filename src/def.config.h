// modifier key: Mod1Mask = Alt, Mod4Mask = Super
#define MOD Mod1Mask
#define VERSION "1.0"

static const int border_width = 2;
static const int padding = 10;
static const int min_window_size = 100;
static const double default_master_size = 0.6;

static const char col_border_focused[] = "#ececec";
static const char col_border_normal[]  = "#999999";

#define WSKEYS(KEY, WS) \
    { MOD,           KEY, switchws,      {.i = WS} }, \
    { MOD|ShiftMask, KEY, movewin_to_ws, {.i = WS} }

static Key keys[] = {
    { MOD,           XK_j,      nextwin,            {0} },
    { MOD,           XK_k,      prevwin,            {0} },
    { MOD,           XK_f,      fullscreen,         {0} },
    { MOD,           XK_q,      killclient,         {0} },
    { MOD,           XK_c,      quit,               {0} },
    { MOD|ShiftMask, XK_j,      movewin,            {.i = 1} },
    { MOD|ShiftMask, XK_k,      movewin,            {.i = -1} },
    { MOD,           XK_u,      focus_monitor,      {.i = -1} },
    { MOD,           XK_i,      focus_monitor,      {.i = 1} },
    { MOD|ShiftMask, XK_u,      movewin_to_monitor, {.i = -1} },
    { MOD|ShiftMask, XK_i,      movewin_to_monitor, {.i = 1} },
    { MOD,           XK_h,      incmaster,          {0} },
    { MOD,           XK_l,      decmaster,          {0} },
    { MOD,           XK_space,  togglemaster,       {0} },
    { MOD,           XK_Return, spawn,              {.cmd = "alacritty"} },
    { MOD,           XK_p,      spawn,              {.cmd = "dmenu_run"} },
    { 0,             XK_Print,  spawn,              {.cmd = "scrot ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
    { ShiftMask,     XK_Print,  spawn,              {.cmd = "scrot -s ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
    WSKEYS(XK_1, 0),
    WSKEYS(XK_2, 1),
    WSKEYS(XK_3, 2),
    WSKEYS(XK_4, 3),
    WSKEYS(XK_5, 4),
    WSKEYS(XK_6, 5),
    WSKEYS(XK_7, 6),
    WSKEYS(XK_8, 7),
    WSKEYS(XK_9, 8),
};
