/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define LT(M)                   ((M)->lt[viewidx(M)])

/* enums */
enum { CurNormal, CurMove, CurN, CurS, CurW, CurE, CurNW, CurNE, CurSW, CurSE, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel, SchemeDim, SchemeSep, SchemeUrg }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, DwmClient, DwmState, DwmTrees, DwmBlocks, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct Node Node;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	int spos;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Node {
	Node *parent, *a, *b;
	Client *c;
	int vert;
	float r;
	int x, y, w, h;
};

struct Monitor {
	char ltsymbol[16];
	int num;
	int barx, by, barw;   /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int tagset[2];
	int showbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout **lt;
	Node **tree;
};

/* function declarations */
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void centerclient(Client *c);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static Client *clientat(int x, int y, Client *skip);
static void comboview(const Arg *arg);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static Node *edgenode(Node *l, int vert, int after);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void floatsize(Client *c);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static int intree(Client *c, Monitor *m);
static void keypress(XEvent *e);
static void keyrelease(XEvent *e);
static void killclient(const Arg *arg);
static Node *leafof(Node *n, Client *c);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void mapnotify(XEvent *e);
static void popupgone(Window w);
static void maprequest(XEvent *e);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void nodeinsert(Node **root, Client *c, Node *t);
static void noderemove(Node **root, Node *l);
static void place(Node *n, int x, int y, int w, int h, int apply);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void restoretrees(void);
static void savetrees(void);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setclienttags(Client *c);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setup(void);
static void sigstatusbar(const Arg *arg);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Node *stale(Node *n, Monitor *m);
static void swapclients(Client *a, Client *b);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatetitle(Client *c);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static unsigned int viewidx(Monitor *m);
static void warp(Client *c);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);

/* variables */
static const char broken[] = "broken";
static char rawstext[256];
static int statussig;
static int statusw;
static struct { int x, w, sig; } blocks[16];
static int nblocks;
static pid_t statuspid = -1;
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[KeyRelease] = keyrelease,
	[MappingNotify] = mappingnotify,
	[MapNotify] = mapnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static int combo = 0;
static Window popup;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !LT(c->mon)->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		if (interact) {
			baseismin = c->basew == c->minw && c->baseh == c->minh;
			if (!baseismin) {
				*w -= c->basew;
				*h -= c->baseh;
			}
			if (c->mina > 0 && c->maxa > 0) {
				if (c->maxa < (float)*w / *h)
					*w = *h * c->maxa + 0.5;
				else if (c->mina < (float)*h / *w)
					*h = *w * c->mina + 0.5;
			}
			if (baseismin) {
				*w -= c->basew;
				*h -= c->baseh;
			}
			if (c->incw)
				*w -= *w % c->incw;
			if (c->inch)
				*h -= *h % c->inch;
			*w += c->basew;
			*h += c->baseh;
		}
		*w = MAX(*w, c->minw);
		*h = MAX(*h, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, LT(m)->symbol, sizeof m->ltsymbol - 1);
	if (LT(m)->arrange)
		LT(m)->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click;
	int j;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += TEXTW(alttags[i]);
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + TEXTW(selmon->ltsymbol))
			click = ClkLtSymbol;
		else if (ev->x >= selmon->barw - statusw) {
			click = ClkStatusText;
			statussig = 0;
			for (j = 0; j < nblocks; j++)
				if (ev->x >= blocks[j].x && ev->x < blocks[j].x + blocks[j].w)
					statussig = blocks[j].sig;
		} else
			click = ClkWinTitle;
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	if (click == ClkTagBar && ev->button == Button1 && !CLEANMASK(ev->state)) {
		static Time last;
		static unsigned int lasttag;

		if (arg.ui == selmon->tagset[selmon->seltags]) {
			if (arg.ui == lasttag && ev->time - last < dblclick) {
				last = 0;
				spawn(&(Arg){ .v = termcmd });
				return;
			}
			last = ev->time;
			lasttag = arg.ui;
		} else
			last = 0;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
centerclient(Client *c)
{
	Monitor *m = c->mon;

	c->x = m->wx + (m->ww - WIDTH(c)) / 2;
	c->y = m->wy + (m->wh - HEIGHT(c)) / 2;
	c->x = MAX(c->x, m->wx);
	c->y = MAX(c->y, m->wy);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	savetrees();
	view(&a);
	for (m = mons; m; m = m->next)
		LT(m) = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;
	size_t i;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	for (i = 0; i <= LENGTH(tags); i++)
		while (mon->tree[i])
			noderemove(&mon->tree[i], stale(mon->tree[i], NULL));
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon->tree);
	free(mon->lt);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

Client *
clientat(int x, int y, Client *skip)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c != skip && intree(c, m) && !c->isfullscreen
			&& x >= c->x && x < c->x + WIDTH(c) && y >= c->y && y < c->y + HEIGHT(c))
				return c;
	return NULL;
}

void
comboview(const Arg *arg)
{
	unsigned int newtags = arg->ui & TAGMASK;

	if (combo)
		selmon->tagset[selmon->seltags] |= newtags;
	else {
		combo = 1;
		if (newtags == selmon->tagset[selmon->seltags])
			return;
		selmon->seltags ^= 1;
		selmon->tagset[selmon->seltags] = newtags;
	}
	focus(NULL);
	arrange(selmon);
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				XMoveResizeWindow(dpy, m->barwin, m->barx, m->by, m->barw, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !LT(c->mon)->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;
	size_t i;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->showbar = showbar;
	m->lt = ecalloc(LENGTH(tags) + 1, sizeof(Layout *));
	m->tree = ecalloc(LENGTH(tags) + 1, sizeof(Node *));
	for (i = 0; i <= LENGTH(tags); i++)
		m->lt[i] = &layouts[0];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol - 1);
	return m;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	popupgone(ev->window);
	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

static void
exportblocks(Monitor *m)
{
	static long last[3 * LENGTH(blocks)];
	static int nlast = -1;
	long v[3 * LENGTH(blocks)];
	int i;

	for (i = 0; i < nblocks; i++) {
		v[3 * i] = m->barx + blocks[i].x;
		v[3 * i + 1] = blocks[i].w;
		v[3 * i + 2] = blocks[i].sig;
	}
	if (nblocks == nlast && !memcmp(v, last, 3 * nblocks * sizeof *v))
		return;
	memcpy(last, v, 3 * nblocks * sizeof *v);
	nlast = nblocks;
	XChangeProperty(dpy, root, netatom[DwmBlocks], XA_CARDINAL, 32, PropModeReplace,
		(unsigned char *)v, 3 * nblocks);
}

static int
drawstatus(Monitor *m)
{
	char buf[sizeof rawstext], *seg[LENGTH(blocks)], *p;
	int sig[LENGTH(blocks)], n = 0, i, w, x, sh = bh * sepfrac, sepw = sepchars * (TEXTW("0") - lrpad);

	strcpy(buf, rawstext);
	for (p = buf; *p && n < (int)LENGTH(blocks); p++) {
		if ((unsigned char)*p < ' ') {
			sig[n] = *p;
			*p = '\0';
			seg[n++] = p + 1;
		} else if (p == buf) {
			sig[n] = 0;
			seg[n++] = p;
		}
	}
	for (i = w = 0; i < n; i++)
		if (*seg[i]) {
			seg[w] = seg[i];
			sig[w++] = sig[i];
		}
	n = w;
	for (w = lrpad, i = 0; i < n; i++)
		w += TEXTW(seg[i]) - lrpad + (i ? sepw : 0);
	x = m->barw - w + lrpad / 2;
	for (nblocks = i = 0; i < n; i++) {
		if (i) {
			drw_setscheme(drw, scheme[SchemeSep]);
			drw_rect(drw, x, 0, sepw, bh, 1, 1);
			drw_rect(drw, x + (sepw - sepwidth) / 2, (bh - sh) / 2, sepwidth, sh, 1, 0);
			x += sepw;
		}
		blocks[nblocks].x = x;
		blocks[nblocks].w = TEXTW(seg[i]) - lrpad;
		blocks[nblocks++].sig = sig[i];
		drw_setscheme(drw, scheme[SchemeNorm]);
		x = drw_text(drw, x, 0, TEXTW(seg[i]) - lrpad, bh, 0, seg[i], 0);
	}
	exportblocks(m);
	return w;
}

void
drawbar(Monitor *m)
{
	int x, w, tw = 0, end;
	unsigned int i, occ = 0, urg = 0, sel = m->tagset[m->seltags];
	const char *t;
	Client *c;

	if (!m->showbar)
		return;
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, m->barw, bh, 1, 1);
	if (m == selmon)
		tw = statusw = drawstatus(m);
	for (c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	for (x = i = 0; i < LENGTH(tags); i++) {
		w = TEXTW(alttags[i]);
		t = occ & 1 << i ? alttags[i] : tags[i];
		drw_setscheme(drw, scheme[sel & 1 << i ? SchemeSel : urg & 1 << i ? SchemeUrg
			: occ & 1 << i ? SchemeNorm : SchemeDim]);
		x = drw_text(drw, x, 0, w, bh, (w - TEXTW(t) + lrpad) / 2, t, 0);
	}
	w = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeDim]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);
	end = m->barw - tw;
	if (m == selmon && m->sel && end > x) {
		w = MIN((int)TEXTW(m->sel->name), end - x);
		drw_setscheme(drw, scheme[popup ? SchemeNorm : SchemeSel]);
		drw_text(drw, x, 0, end - x, bh, (end - x - w + lrpad) / 2, m->sel->name, 0);
	}
	drw_map(drw, m->barwin, 0, 0, m->barw, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

Node *
edgenode(Node *l, int vert, int after)
{
	Node *n;

	for (n = l; n->parent; n = n->parent)
		if (n->parent->vert == vert && (n->parent->a == n) == after)
			return n->parent;
	return NULL;
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
floatsize(Client *c)
{
	Monitor *m = c->mon;

	if (c->isfixed)
		return;
	c->w = m->ww * floatfrac - 2 * c->bw;
	c->h = m->wh * floatfrac - 2 * c->bw;
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[popup ? SchemeNorm : SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
	warp(selmon->sel);
}

static int
leaves(Node *n, Client **v, int k)
{
	if (!n)
		return k;
	if (n->c) {
		v[k] = n->c;
		return k + 1;
	}
	return leaves(n->b, v, leaves(n->a, v, k));
}

void
focusstack(const Arg *arg)
{
	int n = 0, k, i;
	Client *c;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	for (c = selmon->clients; c; c = c->next)
		n++;
	Client *v[n];
	k = LT(selmon)->arrange ? leaves(selmon->tree[viewidx(selmon)], v, 0) : 0;
	for (c = selmon->clients; c; c = c->next)
		if (ISVISIBLE(c) && (!LT(selmon)->arrange || !intree(c, selmon)))
			v[k++] = c;
	for (i = 0; i < k && v[i] != selmon->sel; i++);
	if (k < 2 || i == k)
		return;
	focus(v[(i + arg->i + k) % k]);
	restack(selmon);
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long nitems, dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &nitems, &dl, &p) == Success && p) {
		if (nitems > 0)
			atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

int
intree(Client *c, Monitor *m)
{
	return m && c->mon == m && ISVISIBLE(c) && !(c->isfullscreen ? c->oldstate : c->isfloating);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
keyrelease(XEvent *e)
{
	char km[32];
	size_t i;
	KeyCode kc;

	XQueryKeymap(dpy, km);
	for (i = 0; i < LENGTH(keys); i++)
		if (keys[i].func == comboview && (kc = XKeysymToKeycode(dpy, keys[i].keysym))
		&& km[kc / 8] & 1 << kc % 8)
			return;
	combo = 0;
}

void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

Node *
leafof(Node *n, Client *c)
{
	Node *r;

	if (!n)
		return NULL;
	if (n->c)
		return n->c == c ? n : NULL;
	return (r = leafof(n->a, c)) ? r : leafof(n->b, c);
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;
	Monitor *m;
	long *p = NULL, st[6] = { 0 };
	int di, saved = 0;
	unsigned long n, dl;
	Atom da;

	c = ecalloc(1, sizeof(Client));
	c->spos = -1;
	c->win = w;
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updatetitle(c);
	c->mon = selmon;
	c->tags = selmon->tagset[selmon->seltags];
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else if (XGetWindowProperty(dpy, w, netatom[DwmClient], 0L, 2L, False, XA_CARDINAL,
		&da, &di, &n, &dl, (unsigned char **)&p) == Success && p) {
		if (n == 2) {
			for (m = mons; m && m->num != p[1]; m = m->next);
			if (m)
				c->mon = m;
			if (p[0] & TAGMASK)
				c->tags = p[0] & TAGMASK;
		}
		XFree(p);
	}
	p = NULL;
	if (XGetWindowProperty(dpy, w, netatom[DwmState], 0L, 6L, True, XA_CARDINAL,
		&da, &di, &n, &dl, (unsigned char **)&p) == Success && p) {
		if ((saved = n == 6))
			memcpy(st, p, sizeof st);
		XFree(p);
	}
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c);
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed || (saved && st[0] & 1);
	if (saved) {
		c->spos = st[5];
		if (!c->isfullscreen && !(st[0] & 2)) {
			c->x = st[1];
			c->y = st[2];
			c->w = st[3];
			c->h = st[4];
		}
	} else if (!c->isfullscreen && (c->isfloating || !LT(c->mon)->arrange)) {
		if (!c->isfloating)
			floatsize(c);
		centerclient(c);
	}
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	setclienttags(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h);
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
mapnotify(XEvent *e)
{
	XMapEvent *ev = &e->xmap;
	XClassHint ch = { NULL, NULL };
	size_t i;

	if (!ev->override_redirect || ev->window == popup || !XGetClassHint(dpy, ev->window, &ch))
		return;
	for (i = 0; i < LENGTH(popups); i++)
		if (ch.res_class && !strcmp(ch.res_class, popups[i])) {
			popup = ev->window;
			if (selmon->sel)
				XSetWindowBorder(dpy, selmon->sel->win, scheme[SchemeNorm][ColBorder].pixel);
			drawbars();
		}
	XFree(ch.res_class);
	XFree(ch.res_name);
}

void
popupgone(Window w)
{
	if (!popup || w != popup)
		return;
	popup = 0;
	if (selmon->sel)
		XSetWindowBorder(dpy, selmon->sel->win, scheme[SchemeSel][ColBorder].pixel);
	drawbars();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny, px, py, floating;
	Client *c, *t = NULL, *h;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel) || c->isfullscreen)
		return;
	floating = c->isfloating || !LT(selmon)->arrange;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	px = x;
	py = y;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
			px = ev.xmotion.x;
			py = ev.xmotion.y;
			if (!floating) {
				if ((h = clientat(px, py, c)) != t) {
					if (t)
						XSetWindowBorder(dpy, t->win, scheme[SchemeNorm][ColBorder].pixel);
					if (h)
						XSetWindowBorder(dpy, h->win, scheme[SchemeSel][ColBorder].pixel);
					t = h;
				}
				break;
			}
			nx = ocx + (px - x);
			ny = ocy + (py - y);
			if (abs(selmon->wx + (int)gappx - nx) < snap)
				nx = selmon->wx + gappx;
			else if (abs((selmon->wx + selmon->ww - (int)gappx) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - gappx - WIDTH(c);
			if (abs(selmon->wy + (int)gappx - ny) < snap)
				ny = selmon->wy + gappx;
			else if (abs((selmon->wy + selmon->wh - (int)gappx) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - gappx - HEIGHT(c);
			resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if (!floating) {
		if (t) {
			XSetWindowBorder(dpy, t->win, scheme[SchemeNorm][ColBorder].pixel);
			swapclients(c, t);
		} else if ((m = recttomon(px, py, 1, 1)) != c->mon) {
			sendmon(c, m);
			selmon = m;
			focus(c);
		}
		while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
		return;
	}
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
nodeinsert(Node **root, Client *c, Node *t)
{
	Node *l = ecalloc(1, sizeof(Node)), *p;

	l->c = c;
	if (!t) {
		*root = l;
		return;
	}
	p = ecalloc(1, sizeof(Node));
	p->parent = t->parent;
	if (!p->parent)
		*root = p;
	else if (p->parent->a == t)
		p->parent->a = p;
	else
		p->parent->b = p;
	p->a = t;
	p->b = l;
	t->parent = l->parent = p;
	p->vert = t->w >= t->h;
	p->r = 0.5;
	p->x = t->x;
	p->y = t->y;
	p->w = t->w;
	p->h = t->h;
}

void
noderemove(Node **root, Node *l)
{
	Node *p = l->parent, *s;

	if (!p) {
		free(l);
		*root = NULL;
		return;
	}
	s = p->a == l ? p->b : p->a;
	free(l);
	s->parent = p->parent;
	if (!p->parent)
		*root = s;
	else if (p->parent->a == p)
		p->parent->a = s;
	else
		p->parent->b = s;
	free(p);
}

void
place(Node *n, int x, int y, int w, int h, int apply)
{
	int s;

	n->x = x;
	n->y = y;
	n->w = w;
	n->h = h;
	if (n->c) {
		if (apply && !n->c->isfullscreen)
			resize(n->c, x, y, w - 2 * n->c->bw, h - 2 * n->c->bw, 0);
		return;
	}
	if (n->vert) {
		s = (w - (int)gappx) * n->r + 0.5;
		place(n->a, x, y, s, h, apply);
		place(n->b, x + s + gappx, y, w - s - gappx, h, apply);
	} else {
		s = (h - (int)gappx) * n->r + 0.5;
		place(n->a, x, y, w, s, apply);
		place(n->b, x, y + s + gappx, w, h - s - gappx, apply);
	}
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return;
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

static int
handlexy(Client *c, int g, int vert)
{
	if (vert)
		return g < 0 ? c->y : g > 0 ? c->y + HEIGHT(c) - 1 : c->y + HEIGHT(c) / 2;
	return g < 0 ? c->x : g > 0 ? c->x + WIDTH(c) - 1 : c->x + WIDTH(c) / 2;
}

void
resizemouse(const Arg *arg)
{
	static const int curs[3][3] = {
		{ CurNW, CurW, CurSW }, { CurN, CurMove, CurS }, { CurNE, CurE, CurSE }
	};
	int px, py, gx, gy, i, j, dx, dy, d, bd = -1, floating;
	int ox, oy, ow, oh, nx, ny, nw, nh, ofx = 0, ofy = 0, s;
	Client *c;
	Node *l = NULL, *vx = NULL, *hy = NULL, **tr = NULL;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel) || c->isfullscreen || !getrootptr(&px, &py))
		return;
	floating = c->isfloating || !LT(selmon)->arrange;
	if (!floating) {
		tr = &selmon->tree[viewidx(selmon)];
		if (!(l = leafof(*tr, c)))
			return;
	}
	gx = px < c->x + WIDTH(c) / 3 ? -1 : px >= c->x + 2 * WIDTH(c) / 3 ? 1 : 0;
	gy = py < c->y + HEIGHT(c) / 3 ? -1 : py >= c->y + 2 * HEIGHT(c) / 3 ? 1 : 0;
	if (!floating) {
		if (gx && !edgenode(l, 1, gx > 0))
			gx = 0;
		if (gy && !edgenode(l, 0, gy > 0))
			gy = 0;
	}
	if (!gx && !gy) {
		for (i = -1; i <= 1; i++)
			for (j = -1; j <= 1; j++) {
				if ((!i && !j) || (!floating && ((i && !edgenode(l, 1, i > 0)) || (j && !edgenode(l, 0, j > 0)))))
					continue;
				dx = handlexy(c, i, 0) - px;
				dy = handlexy(c, j, 1) - py;
				d = dx * dx + dy * dy;
				if (bd < 0 || d < bd) {
					bd = d;
					gx = i;
					gy = j;
				}
			}
		if (bd < 0)
			return;
	}
	restack(selmon);
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[curs[gx + 1][gy + 1]]->cursor, CurrentTime) != GrabSuccess)
		return;
	px = handlexy(c, gx, 0);
	py = handlexy(c, gy, 1);
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, px, py);
	ox = c->x;
	oy = c->y;
	ow = c->w;
	oh = c->h;
	if (!floating) {
		if (gx && (vx = edgenode(l, 1, gx > 0)))
			ofx = px - vx->x - (int)((vx->w - (int)gappx) * vx->r + 0.5);
		if (gy && (hy = edgenode(l, 0, gy > 0)))
			ofy = py - hy->y - (int)((hy->h - (int)gappx) * hy->r + 0.5);
	}
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
			if (!floating) {
				if (vx && (s = vx->w - gappx) > 2 * minsplit) {
					d = MAX(minsplit, MIN(s - minsplit, ev.xmotion.x_root - vx->x - ofx));
					vx->r = (float)d / s;
				}
				if (hy && (s = hy->h - gappx) > 2 * minsplit) {
					d = MAX(minsplit, MIN(s - minsplit, ev.xmotion.y_root - hy->y - ofy));
					hy->r = (float)d / s;
				}
				place(*tr, (*tr)->x, (*tr)->y, (*tr)->w, (*tr)->h, 1);
				break;
			}
			nx = ox;
			ny = oy;
			nw = ow + gx * (ev.xmotion.x_root - px);
			nh = oh + gy * (ev.xmotion.y_root - py);
			applysizehints(c, &nx, &ny, &nw, &nh, 1);
			if (gx < 0)
				nx = ox + ow - nw;
			if (gy < 0)
				ny = oy + oh - nh;
			if (nx != c->x || ny != c->y || nw != c->w || nh != c->h)
				resizeclient(c, nx, ny, nw, nh);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !LT(m)->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (LT(m)->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

static Node *
parsetree(char **tok, Monitor *m)
{
	Node *n, *a, *b;
	Client *c;
	char *t = strtok_r(NULL, " ", tok);
	float r;

	if (!t)
		return NULL;
	if (*t == 'w') {
		if (!(c = wintoclient(strtoul(t + 1, NULL, 16))) || c->mon != m)
			return NULL;
		n = ecalloc(1, sizeof(Node));
		n->c = c;
		return n;
	}
	r = strtof(t + 1, NULL);
	a = parsetree(tok, m);
	b = parsetree(tok, m);
	if (!a || !b)
		return a ? a : b;
	n = ecalloc(1, sizeof(Node));
	n->vert = *t == 'v';
	n->r = r > 0.05 && r < 0.95 ? r : 0.5;
	n->a = a;
	n->b = b;
	a->parent = b->parent = n;
	return n;
}

static void
freetree(Node *n)
{
	if (!n)
		return;
	freetree(n->a);
	freetree(n->b);
	free(n);
}

void
restoretrees(void)
{
	char *text = NULL, *line, *tok, *ltok, *t;
	Monitor *m;
	XTextProperty tp;
	XEvent ev;
	unsigned int v;

	if (!XGetTextProperty(dpy, root, &tp, netatom[DwmTrees]) || !tp.nitems)
		return;
	text = strndup((char *)tp.value, tp.nitems);
	XFree(tp.value);
	XDeleteProperty(dpy, root, netatom[DwmTrees]);
	for (line = strtok_r(text, ";", &ltok); line; line = strtok_r(NULL, ";", &ltok)) {
		if (!(t = strtok_r(line, " ", &tok)))
			continue;
		for (m = mons; m && m->num != atoi(t + 1); m = m->next);
		if (!m)
			continue;
		if (*t == 's')
			selmon = m;
		else if (*t == 'm') {
			unsigned int ts = (t = strtok_r(NULL, " ", &tok)) ? strtoul(t, NULL, 10) & TAGMASK : 0;
			unsigned int fl = (t = strtok_r(NULL, " ", &tok)) ? strtoul(t, NULL, 10) : 0;
			if (ts)
				m->tagset[m->seltags] = ts;
			for (v = 0; v <= LENGTH(tags); v++)
				m->lt[v] = &layouts[fl >> v & 1];
		} else if (*t == 't' && (t = strtok_r(NULL, " ", &tok)) && (v = atoi(t)) <= LENGTH(tags)) {
			freetree(m->tree[v]);
			m->tree[v] = parsetree(&tok, m);
		}
	}
	free(text);
	for (m = mons; m; m = m->next) {
		Client *c, *d, *sorted = NULL, **pp;

		while ((c = m->stack)) {
			m->stack = c->snext;
			for (pp = &sorted; (d = *pp) && (unsigned int)d->spos <= (unsigned int)c->spos; pp = &d->snext);
			c->snext = d;
			*pp = c;
		}
		m->stack = sorted;
		for (c = m->stack; c && !ISVISIBLE(c); c = c->snext);
		m->sel = c;
	}
	focus(NULL);
	arrange(NULL);
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

static void
puttree(Node *n, char *buf, size_t size, size_t *len)
{
	if (!n || *len >= size)
		return;
	if (n->c)
		*len += snprintf(buf + *len, size - *len, " w%lx", n->c->win);
	else {
		*len += snprintf(buf + *len, size - *len, " %c%.4f", n->vert ? 'v' : 'h', n->r);
		puttree(n->a, buf, size, len);
		puttree(n->b, buf, size, len);
	}
}

void
savetrees(void)
{
	static char buf[16384];
	size_t len = 0;
	unsigned int v, fl;
	Monitor *m;
	Client *c;
	long st[6];

	for (m = mons; m; m = m->next)
		for (v = 0, c = m->stack; c; c = c->snext, v++) {
			st[0] = (c->isfullscreen ? c->oldstate : c->isfloating) | c->isfullscreen << 1;
			st[1] = c->x;
			st[2] = c->y;
			st[3] = c->w;
			st[4] = c->h;
			st[5] = v;
			XChangeProperty(dpy, c->win, netatom[DwmState], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)st, 6);
		}
	len += snprintf(buf, sizeof buf, "s%d;", selmon->num);
	for (m = mons; m && len < sizeof buf; m = m->next) {
		for (fl = v = 0; v <= LENGTH(tags); v++)
			fl |= (m->lt[v] == &layouts[1]) << v;
		len += snprintf(buf + len, sizeof buf - len, "m%d %u %u;", m->num, m->tagset[m->seltags], fl);
		for (v = 0; v <= LENGTH(tags) && len < sizeof buf; v++)
			if (m->tree[v]) {
				len += snprintf(buf + len, sizeof buf - len, "t%d %u", m->num, v);
				puttree(m->tree[v], buf, sizeof buf, &len);
				if (len < sizeof buf)
					len += snprintf(buf + len, sizeof buf - len, ";");
			}
	}
	if (len < sizeof buf)
		XChangeProperty(dpy, root, netatom[DwmTrees], XA_STRING, 8, PropModeReplace,
			(unsigned char *)buf, len);
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags];
	attach(c);
	attachstack(c);
	setclienttags(c);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

void
setclienttags(Client *c)
{
	long data[] = { c->tags, c->mon->num };

	XChangeProperty(dpy, c->win, netatom[DwmClient], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

void
setlayout(const Arg *arg)
{
	const Layout **lt = &LT(selmon);

	*lt = arg && arg->v ? arg->v : *lt == &layouts[0] ? &layouts[1] : &layouts[0];
	strncpy(selmon->ltsymbol, (*lt)->symbol, sizeof selmon->ltsymbol - 1);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

static pid_t
getstatusbarpid(void)
{
	char buf[64], *str = buf, *c;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%d/comm", statuspid);
		if ((fp = fopen(buf, "r"))) {
			if (fgets(buf, sizeof(buf), fp) && (c = strchr(str, '\n')))
				*c = '\0';
			fclose(fp);
			if (!strcmp(str, "dwmblocks"))
				return statuspid;
		}
	}
	if (!(fp = popen("pidof -s dwmblocks", "r")))
		return -1;
	if (!fgets(buf, sizeof(buf), fp))
		buf[0] = '\0';
	pclose(fp);
	return strtol(buf, NULL, 10);
}

void
sigstatusbar(const Arg *arg)
{
	union sigval sv;

	if (!statussig || (statuspid = getstatusbarpid()) <= 0)
		return;
	sv.sival_int = arg->i;
	sigqueue(statuspid, SIGRTMIN + statussig, sv);
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = drw->fonts->h + barpad;
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[DwmClient] = XInternAtom(dpy, "_DWM_CLIENT", False);
	netatom[DwmState] = XInternAtom(dpy, "_DWM_STATE", False);
	netatom[DwmTrees] = XInternAtom(dpy, "_DWM_TREES", False);
	netatom[DwmBlocks] = XInternAtom(dpy, "_DWM_BLOCKS", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	cursor[CurN] = drw_cur_create(drw, XC_top_side);
	cursor[CurS] = drw_cur_create(drw, XC_bottom_side);
	cursor[CurW] = drw_cur_create(drw, XC_left_side);
	cursor[CurE] = drw_cur_create(drw, XC_right_side);
	cursor[CurNW] = drw_cur_create(drw, XC_top_left_corner);
	cursor[CurNE] = drw_cur_create(drw, XC_top_right_corner);
	cursor[CurSW] = drw_cur_create(drw, XC_bottom_left_corner);
	cursor[CurSE] = drw_cur_create(drw, XC_bottom_right_corner);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, DwmClient);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	XkbSetDetectableAutoRepeat(dpy, True, NULL);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!LT(c->mon)->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

Node *
stale(Node *n, Monitor *m)
{
	Node *r;

	if (!n)
		return NULL;
	if (n->c)
		return intree(n->c, m) ? NULL : n;
	return (r = stale(n->a, m)) ? r : stale(n->b, m);
}

void
swapclients(Client *a, Client *b)
{
	Monitor *ma = a->mon, *mb = b->mon;
	Node *la = leafof(ma->tree[viewidx(ma)], a), *lb = leafof(mb->tree[viewidx(mb)], b);
	unsigned int t = a->tags;

	if (!la || !lb)
		return;
	la->c = b;
	lb->c = a;
	if (ma != mb) {
		detach(a);
		detachstack(a);
		detach(b);
		detachstack(b);
		a->mon = mb;
		a->tags = b->tags;
		b->mon = ma;
		b->tags = t;
		attach(a);
		attachstack(a);
		attach(b);
		attachstack(b);
		setclienttags(a);
		setclienttags(b);
	}
	focus(a);
	arrange(NULL);
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		setclienttags(selmon->sel);
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
tile(Monitor *m)
{
	Node **tr = &m->tree[viewidx(m)], *l, *t;
	Client *c, *s;
	int x = m->wx + gappx, y = m->wy + gappx, w = m->ww - 2 * gappx, h = m->wh - 2 * gappx;

	while ((l = stale(*tr, m)))
		noderemove(tr, l);
	if (*tr)
		place(*tr, x, y, w, h, 0);
	for (c = m->clients; c; c = c->next) {
		if (!intree(c, m) || leafof(*tr, c))
			continue;
		for (s = m->stack, t = NULL; s && !t; s = s->snext)
			if (s != c)
				t = leafof(*tr, s);
		if (!t && *tr)
			for (t = *tr; !t->c; t = t->a);
		nodeinsert(tr, c, t);
		place(*tr, x, y, w, h, 0);
	}
	if (*tr)
		place(*tr, x, y, w, h, 1);
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->barx, selmon->by, selmon->barw, bh);
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!c || c->isfullscreen)
		return;
	c->isfloating = !c->isfloating || c->isfixed;
	arrange(selmon);
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon, *t;
	XWindowChanges wc;
	Node *l;
	size_t i;

	detach(c);
	detachstack(c);
	for (t = mons; t; t = t->next)
		for (i = 0; i <= LENGTH(tags); i++)
			if ((l = leafof(t->tree[i], c)))
				noderemove(&t->tree[i], l);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	popupgone(ev->window);
	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatetitle(Client *c)
{
	XClassHint ch = { NULL, NULL };
	size_t i, n, k;
	char *p;

	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	n = strlen(c->name);
	for (i = 0; i < LENGTH(titlesuffix); i++) {
		k = strlen(titlesuffix[i]);
		if (n > k && !strcmp(c->name + n - k, titlesuffix[i]))
			c->name[n - k] = '\0';
	}
	if (c->name[0])
		return;
	XGetClassHint(dpy, c->win, &ch);
	strncpy(c->name, ch.res_class ? ch.res_class : broken, sizeof c->name - 1);
	for (p = c->name; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p += 'a' - 'A';
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		m->barwin = XCreateWindow(dpy, root, m->barx, m->by, m->barw, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	m->barx = m->mx + gappx;
	m->barw = m->mw - 2 * gappx;
	if (m->showbar) {
		m->by = m->my + gappx;
		m->wy += gappx + bh;
		m->wh -= gappx + bh;
	} else
		m->by = -bh;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, rawstext, sizeof(rawstext)))
		strcpy(rawstext, "dwm-"VERSION);
	drawbar(selmon);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1;
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange(selmon);
}

unsigned int
viewidx(Monitor *m)
{
	unsigned int t = m->tagset[m->seltags], i;

	if (!t || t & (t - 1))
		return 0;
	for (i = 0; !(t & 1 << i); i++);
	return i + 1;
}

void
warp(Client *c)
{
	XEvent ev;

	if (c)
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, c->x + WIDTH(c) / 2, c->y + HEIGHT(c) / 2);
	else
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->wx + selmon->ww / 2, selmon->wy + selmon->wh / 2);
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	restoretrees();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
