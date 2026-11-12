#include <X11/XF86keysym.h>
#include "colors.h"

static const unsigned int borderpx = 2;
static const unsigned int gappx    = 5;
static const unsigned int snap     = 16;
static const int showbar           = 1;
static const int barpad            = 10;
static const int minsplit          = 64;
static const float floatfrac       = 0.85;
static const unsigned int dblclick = 300;
static const int sepchars          = 3;
static const float sepfrac         = 0.55;
static const int sepwidth          = 2;
static const char *popups[]        = { "dmenu", "distro-panel" };
static const char *fonts[]         = { "monospace:size=12" };
static const char *colors[][3]     = {
	/*               fg         bg      border     */
	[SchemeNorm] = { col_fg,    col_bg,  col_sel    },
	[SchemeSel]  = { col_fgsel, col_sel, col_accent },
	[SchemeDim]  = { col_dim,   col_bg,  col_sel    },
	[SchemeSep]  = { col_line,  col_bg,  col_bg     },
	[SchemeUrg]  = { col_urg,   col_bg,  col_urg    },
};

static const char *tags[]    = { "dev", "cmd", "web", "gen", "chat" };
static const char *alttags[] = { "[dev]", "[cmd]", "[web]", "[gen]", "[chat]" };

static const char *titlesuffix[] = { " — Mozilla Firefox", " - Mozilla Firefox" };

static const int resizehints    = 0;
static const int lockfullscreen = 1;
static const int refreshrate    = 165;

static const Layout layouts[] = {
	{ "[]=", tile },
	{ "><>", NULL },
};

#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,           KEY, comboview, {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask, KEY, tag,       {.ui = 1 << TAG} },

static char dmenumon[2] = "0";
static const char *dmenucmd[]   = { "distro-run", "-m", dmenumon, NULL };
static const char *termcmd[]    = { "st", NULL };
static const char *logoutcmd[]  = { "distro-logout", NULL };
static const char *clipcmd[]    = { "distro-clip", NULL };
static const char *datecmd[]    = { "distro-panel", NULL };
static const char *syscmd[]     = { "distro-panel", "sys", NULL };
static const char *shotcmd[]    = { "flameshot", "gui", NULL };
static const char *notifclose[] = { "dunstctl", "close-all", NULL };
static const char *notifhist[]  = { "dunstctl", "history-pop", NULL };
static const char *volup[]      = { "distro-volume", "up", NULL };
static const char *voldown[]    = { "distro-volume", "down", NULL };
static const char *volmute[]    = { "distro-volume", "mute", NULL };

static const Key keys[] = {
	/* modifier                     key                       function        argument */
	{ MODKEY,                       XK_space,                 spawn,          {.v = dmenucmd } },
	{ MODKEY,                       XK_Return,                spawn,          {.v = termcmd } },
	{ MODKEY,                       XK_v,                     spawn,          {.v = clipcmd } },
	{ MODKEY,                       XK_s,                     spawn,          {.v = syscmd } },
	{ MODKEY,                       XK_d,                     spawn,          {.v = datecmd } },
	{ MODKEY,                       XK_l,                     spawn,          {.v = logoutcmd } },
	{ 0,                            XK_Print,                 spawn,          {.v = shotcmd } },
	{ MODKEY,                       XK_n,                     spawn,          {.v = notifclose } },
	{ MODKEY|ShiftMask,             XK_n,                     spawn,          {.v = notifhist } },
	{ MODKEY,                       XK_F10,                   spawn,          {.v = volmute } },
	{ MODKEY,                       XK_F11,                   spawn,          {.v = voldown } },
	{ MODKEY,                       XK_F12,                   spawn,          {.v = volup } },
	{ 0,                            XF86XK_AudioMute,         spawn,          {.v = volmute } },
	{ 0,                            XF86XK_AudioLowerVolume,  spawn,          {.v = voldown } },
	{ 0,                            XF86XK_AudioRaiseVolume,  spawn,          {.v = volup } },
	{ MODKEY,                       XK_b,                     togglebar,      {0} },
	{ MODKEY,                       XK_j,                     focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,                     focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_Tab,                   view,           {0} },
	{ MODKEY,                       XK_q,                     killclient,     {0} },
	{ MODKEY,                       XK_t,                     setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_w,                     setlayout,      {.v = &layouts[1]} },
	{ MODKEY|ShiftMask,             XK_space,                 togglefloating, {0} },
	{ MODKEY,                       XK_comma,                 focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period,                focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,                 tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period,                tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_1,                                     0)
	TAGKEYS(                        XK_2,                                     1)
	TAGKEYS(                        XK_3,                                     2)
	TAGKEYS(                        XK_4,                                     3)
	TAGKEYS(                        XK_5,                                     4)
	{ MODKEY|ShiftMask,             XK_r,                     quit,           {0} },
};

static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkStatusText,        0,              Button1,        sigstatusbar,   {.i = 1} },
	{ ClkStatusText,        0,              Button2,        sigstatusbar,   {.i = 2} },
	{ ClkStatusText,        0,              Button3,        sigstatusbar,   {.i = 3} },
	{ ClkStatusText,        0,              Button4,        sigstatusbar,   {.i = 4} },
	{ ClkStatusText,        0,              Button5,        sigstatusbar,   {.i = 5} },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
};
