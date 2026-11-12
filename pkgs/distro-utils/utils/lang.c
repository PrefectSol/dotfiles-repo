#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
label(Display *dpy, char *out, size_t n)
{
	XkbStateRec st;
	Atom prop = XInternAtom(dpy, "_XKB_RULES_NAMES", False), type;
	unsigned char *data = NULL;
	unsigned long items, after;
	char *layouts = "us", *p, *s;
	int fmt, i, len;

	XkbGetState(dpy, XkbUseCoreKbd, &st);
	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), prop, 0, 1024, False, XA_STRING,
	                       &type, &fmt, &items, &after, &data) == Success && data) {
		for (p = (char *)data, i = 0; i < 2 && p < (char *)data + items; i++)
			p += strlen(p) + 1;
		if (p < (char *)data + items && *p)
			layouts = p;
	}
	for (s = layouts, i = 0; i < st.group && (p = strchr(s, ',')); i++)
		s = p + 1;
	len = strcspn(s, ",(");
	if ((len == 2 && !strncmp(s, "us", 2)) || (len == 2 && !strncmp(s, "gb", 2)))
		snprintf(out, n, "en");
	else
		snprintf(out, n, "%.2s", len >= 2 ? s : "??");
	if (st.locked_mods & LockMask)
		for (p = out; *p; p++)
			*p = toupper((unsigned char)*p);
	if (data)
		XFree(data);
}

static void
notify(int sig)
{
	char path[300], comm[32];
	struct dirent *e;
	FILE *fp;
	DIR *d;

	if (!(d = opendir("/proc")))
		return;
	while ((e = readdir(d))) {
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
		if (!(fp = fopen(path, "r")))
			continue;
		if (fgets(comm, sizeof(comm), fp) && !strcmp(comm, "dwmblocks\n"))
			kill(atoi(e->d_name), SIGRTMIN + sig);
		fclose(fp);
	}
	closedir(d);
}

int
main(int argc, char *argv[])
{
	Display *dpy;
	XEvent ev;
	char cur[8], last[8] = "";
	int base, op, err, maj = XkbMajorVersion, min = XkbMinorVersion, sig;

	if (!(dpy = XkbOpenDisplay(NULL, &base, &err, &maj, &min, &op))) {
		fputs("distro-lang: cannot open display\n", stderr);
		return 1;
	}
	if (argc < 2 || strcmp(argv[1], "-w")) {
		label(dpy, cur, sizeof(cur));
		puts(cur);
		return 0;
	}
	sig = argc > 2 ? atoi(argv[2]) : 3;
	XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
	                      XkbGroupStateMask | XkbModifierLockMask, XkbGroupStateMask | XkbModifierLockMask);
	label(dpy, last, sizeof(last));
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type != base || ((XkbEvent *)&ev)->any.xkb_type != XkbStateNotify)
			continue;
		label(dpy, cur, sizeof(cur));
		if (strcmp(cur, last)) {
			strcpy(last, cur);
			notify(sig);
		}
	}
}
