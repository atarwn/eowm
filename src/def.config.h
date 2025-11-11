// appearance
static const int border_width = 2;
static const int padding = 10;
static const int min_window_size = 100;
static const int max_stack_size = 512;
static const double default_master_size = 0.6;

static const char col_border_focused[] = "#ececec";
static const char col_border_normal[] = "#999999";
static const char root_bg[] = "#1a1a1a";

// modifier key: Mod1Mask = Alt, Mod4Mask = Super
#define MOD Mod1Mask

#define VERSION "1.0"

// key definitions
static Key keys[] = {
    /* modifier	key	function	argument */
    // General navigation & management
    { MOD,	XK_j,	nextwin,	{0} },
    { MOD,	XK_k,	prevwin,	{0} },
    { MOD,	XK_f,	fullscreen,	{0} },
    { MOD,	XK_q,	killclient,	{0} },
    { MOD,	XK_c,	quit,	{0} },
    { MOD|ShiftMask,	XK_j,	movewindown,	{0} },
    { MOD|ShiftMask,	XK_k,	movewinup,	{0} },

    // Master window
    { MOD,	XK_h,	incmaster,	{0} },
    { MOD,	XK_l,	decmaster,	{0} },
    { MOD,	XK_space,	togglemaster,	{0} },

    // Applications
    { MOD,	XK_Return,	spawn,	{.cmd = "alacritty"} },
    { MOD,	XK_p,	spawn,	{.cmd = "dmenu_run"} },

    // Utilities (uncomment #include <X11/XF86keysym.h> in eowm.c to use XF86 keys)
    { 0,	XK_Print,	spawn,	{.cmd = "scrot ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
    { ShiftMask,	XK_Print,	spawn,	{.cmd = "scrot -s ~/Pictures/Screenshots/$(date +%Y.%m.%d_%H.%M).png"} },
    // { 0,	XF86XK_MonBrightnessUp,	spawn,	{.cmd = "brightnessctl set 10%+"} },
    // { 0,	XF86XK_MonBrightnessDown,	spawn,	{.cmd = "brightnessctl set 10%-"} },
    // { 0,	XF86XK_AudioLowerVolume,	spawn,	{.cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-"} },
    // { 0,	XF86XK_AudioRaiseVolume,	spawn,	{.cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+"} },
    // { 0,	XF86XK_AudioMute,	spawn,	{.cmd = "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"} },

    // Workspaces
    { MOD,	XK_1,	switchws,	{.i = 0} },
    { MOD,	XK_2,	switchws,	{.i = 1} },
    { MOD,	XK_3,	switchws,	{.i = 2} },
    { MOD,	XK_4,	switchws,	{.i = 3} },
    { MOD,	XK_5,	switchws,	{.i = 4} },
    { MOD,	XK_6,	switchws,	{.i = 5} },
    { MOD,	XK_7,	switchws,	{.i = 6} },
    { MOD,	XK_8,	switchws,	{.i = 7} },
    { MOD,	XK_9,	switchws,	{.i = 8} },

    // Move window to workspace
    { MOD|ShiftMask,	XK_1,	movewin_to_ws,	{.i = 0} },
    { MOD|ShiftMask,	XK_2,	movewin_to_ws,	{.i = 1} },
    { MOD|ShiftMask,	XK_3,	movewin_to_ws,	{.i = 2} },
    { MOD|ShiftMask,	XK_4,	movewin_to_ws,	{.i = 3} },
    { MOD|ShiftMask,	XK_5,	movewin_to_ws,	{.i = 4} },
    { MOD|ShiftMask,	XK_6,	movewin_to_ws,	{.i = 5} },
    { MOD|ShiftMask,	XK_7,	movewin_to_ws,	{.i = 6} },
    { MOD|ShiftMask,	XK_8,	movewin_to_ws,	{.i = 7} },
    { MOD|ShiftMask,	XK_9,	movewin_to_ws,	{.i = 8} },
};