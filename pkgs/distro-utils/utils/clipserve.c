#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXT 8

typedef struct {
	Atom atom;
	char *data;
	long len;
} Target;

static Target targets[MAXT];
static int ntargets;

static void
die(const char *msg)
{
	fprintf(stderr, "distro-clipserve: %s\n", msg);
	exit(1);
}

static char *
slurp(const char *path, long *len)
{
	FILE *fp = fopen(path, "rb");
	char *buf;

	if (!fp)
		die("cannot open input file");
	fseek(fp, 0, SEEK_END);
	*len = ftell(fp);
	rewind(fp);
	if (!(buf = malloc(*len + 1)) || fread(buf, 1, *len, fp) != (size_t)*len)
		die("cannot read input file");
	fclose(fp);
	return buf;
}

static void
reply(Display *dpy, XSelectionRequestEvent *req, Atom atargets)
{
	XSelectionEvent ev = { 0 };
	Atom prop = req->property != None ? req->property : req->target;
	Atom list[MAXT + 1];
	int i;

	ev.type = SelectionNotify;
	ev.requestor = req->requestor;
	ev.selection = req->selection;
	ev.target = req->target;
	ev.time = req->time;
	ev.property = None;
	if (req->target == atargets) {
		list[0] = atargets;
		for (i = 0; i < ntargets; i++)
			list[i + 1] = targets[i].atom;
		XChangeProperty(dpy, req->requestor, prop, XA_ATOM, 32, PropModeReplace,
		                (unsigned char *)list, ntargets + 1);
		ev.property = prop;
	} else {
		for (i = 0; i < ntargets; i++) {
			if (targets[i].atom != req->target)
				continue;
			XChangeProperty(dpy, req->requestor, prop, targets[i].atom, 8, PropModeReplace,
			                (unsigned char *)targets[i].data, targets[i].len);
			ev.property = prop;
			break;
		}
	}
	XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&ev);
}

int
main(int argc, char *argv[])
{
	Display *dpy;
	Window win;
	Atom clipboard, atargets;
	XEvent ev;
	int i;

	if (argc < 3 || argc % 2 == 0 || (argc - 1) / 2 > MAXT)
		die("usage: distro-clipserve TARGET FILE [TARGET FILE ...]");
	for (i = 1; i < argc; i += 2) {
		targets[ntargets].data = slurp(argv[i + 1], &targets[ntargets].len);
		ntargets++;
	}
	if (fork() > 0)
		return 0;
	setsid();
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	for (i = 0; i < ntargets; i++)
		targets[i].atom = XInternAtom(dpy, argv[1 + 2 * i], False);
	clipboard = XInternAtom(dpy, "CLIPBOARD", False);
	atargets = XInternAtom(dpy, "TARGETS", False);
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	XSetSelectionOwner(dpy, clipboard, win, CurrentTime);
	if (XGetSelectionOwner(dpy, clipboard) != win)
		die("cannot own clipboard");
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == SelectionRequest)
			reply(dpy, &ev.xselectionrequest, atargets);
		else if (ev.type == SelectionClear)
			break;
	}
	XCloseDisplay(dpy);
	return 0;
}
