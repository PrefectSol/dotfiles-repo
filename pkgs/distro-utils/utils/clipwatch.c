#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>

int
main(void)
{
	Display *dpy;
	XEvent ev;
	int base, err;

	if (!(dpy = XOpenDisplay(NULL)) || !XFixesQueryExtension(dpy, &base, &err))
		return 1;
	XFixesSelectSelectionInput(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "CLIPBOARD", False),
	                           XFixesSetSelectionOwnerNotifyMask | XFixesSelectionWindowDestroyNotifyMask |
	                           XFixesSelectionClientCloseNotifyMask);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type != base + XFixesSelectionNotify)
			continue;
		puts(((XFixesSelectionNotifyEvent *)&ev)->subtype == XFixesSetSelectionOwnerNotify ? "set" : "gone");
		fflush(stdout);
	}
}
