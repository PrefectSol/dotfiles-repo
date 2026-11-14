#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/XTest.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "colors.h"

#define SINK "@DEFAULT_AUDIO_SINK@"
#define SOURCE "@DEFAULT_AUDIO_SOURCE@"
#define STR_(x) #x
#define STR(x) STR_(x)
#define SETVOL "wpctl set-volume -l " STR(VOLMAX) " "

enum { CBg, CSel, CFg, CFgSel, CDim, CAccent, CHead, COk, CErr, CWarn, CMag, CLast };
enum { RTabs, RCal, RLang, RCaps, ROut, RVol, RIn, RMic, RApp };
enum { PVitals, PSound, PDatesys, PLast };

typedef struct {
	int x, y, w, h;
} Rect;

typedef struct {
	const char *node;
	float vol;
	int muted, valid;
	Rect label, bar;
} Audio;

typedef struct {
	Rect row, prev, next;
} Picker;

typedef struct {
	Rect row, prev, next, item[4];
} Opts;

static Display *dpy;
static Window root, win;
static int screen;
static Pixmap buf;
static GC gc;
static XftDraw *xd;
static XftFont *font;
static XftColor col[CLast];
static int W, H, cw, lh, pad, tshift, vcol;
static volatile sig_atomic_t running = 1;
static char pidpath[256];
static int calmon, calyear, ticks;
static Audio sink = { .node = SINK }, source = { .node = SOURCE };
static Rect rprev, rnext, rcal;
static Opts otabs;
static Picker playout, pcase;
static char langs[4][8];
static int nlang, xkbev;
static Picker psink, psource;
static int sel;
static unsigned long long cputotal[65], cpuidle[65];
static int cpupct[65], ncpu;
static int painting = 1, page = PDatesys, bary, closing, replay, rightx, winy;
static unsigned long long netrx, nettx;
static double nett, rxrate, txrate;

typedef struct {
	int id, def;
	char name[96];
} Node;

typedef struct {
	int id, capture, muted, valid;
	float vol;
	char name[64];
	Rect label, bar;
} Stream;

static Node sinks[8], sources[8];
static int nsinks, nsources;
static Stream streams[12];
static int nstreams;
static Audio *dragaudio;
static int dragid = -1;
static char gpuname[96], gpuinfo[96];
static int gpuutil = -1, gpumemused, gpumemtotal, gputemp;


typedef struct {
	Node sinks[8], sources[8];
	Stream streams[12];
	int nsinks, nsources, nstreams;
	Audio sink, source;
	char gpuname[96], gpuinfo[96];
	int gpuutil, gpumemused, gpumemtotal, gputemp;
} Poll;

static Poll ready;
static pthread_mutex_t plock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcond = PTHREAD_COND_INITIALIZER;
static int pfd[2], fresh, kicked, regrab, shot, shotticks;

static void place(int *x, int *y);

static void
die(const char *msg)
{
	fprintf(stderr, "distro-panel: %s\n", msg);
	exit(1);
}

static void
onsignal(int sig)
{
	(void)sig;
	running = 0;
}

static int
inrect(Rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int
textw(const char *s)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)s, strlen(s), &ext);
	return ext.xOff;
}

static void
text(int x, int y, int c, const char *s)
{
	int fh = font->ascent + font->descent;

	if (!painting)
		return;
	XftDrawStringUtf8(xd, &col[c], font, x, y + (lh - fh) / 2 + font->ascent - tshift,
	                  (const FcChar8 *)s, strlen(s));
}

static void
textr(int xr, int y, int c, const char *s)
{
	text(xr - textw(s), y, c, s);
}

static void
fill(int x, int y, int w, int h, int c)
{
	if (!painting)
		return;
	XftDrawRect(xd, &col[c], x, y, w, h);
}

static void
fit(char *s, int maxw)
{
	size_t n = strlen(s);

	if (textw(s) <= maxw)
		return;
	while (n > 0) {
		do
			n--;
		while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80);
		s[n] = '\0';
		if (textw(s) + textw("…") <= maxw)
			break;
	}
	strcat(s, "…");
}

static void
run(const char *fmt, const char *node, const char *arg)
{
	char cmd[256];

	snprintf(cmd, sizeof(cmd), fmt, node, arg);
	if (system(cmd) == -1)
		perror("system");
}

static void
runid(const char *fmt, int id, const char *arg)
{
	char cmd[256];

	snprintf(cmd, sizeof(cmd), fmt, id, arg);
	if (system(cmd) == -1)
		perror("system");
}

static int
volread(const char *node, float *vol, int *muted)
{
	char cmd[128], line[256];
	FILE *fp;
	int ok = 0;

	snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", node);
	if (!(fp = popen(cmd, "r")))
		return 0;
	if (fgets(line, sizeof(line), fp) && sscanf(line, "Volume: %f", vol) == 1) {
		ok = 1;
		*muted = strstr(line, "MUTED") != NULL;
	}
	pclose(fp);
	return ok;
}

static void
medianame(Stream *st)
{
	char cmd[64], line[512], *p, *q;
	FILE *fp;

	snprintf(cmd, sizeof(cmd), "wpctl inspect %d 2>/dev/null", st->id);
	if (!(fp = popen(cmd, "r")))
		return;
	while (fgets(line, sizeof(line), fp))
		if ((p = strstr(line, "media.name = \"")) && (q = strrchr(p, '"')) > p + 14) {
			*q = '\0';
			if (strcmp(p + 14, "AudioStream"))
				snprintf(st->name, sizeof(st->name), "%s", p + 14);
		}
	pclose(fp);
}

static void
audiostatus(Poll *d)
{
	char line[512], dup[sizeof(d->streams) / sizeof(d->streams[0])] = { 0 }, *p, *q, *e;
	FILE *fp;
	int section = 0, inaudio = 0, indent, top = -1, i, j;
	Node *n;

	d->nsinks = d->nsources = d->nstreams = 0;
	if (!(fp = popen("wpctl status 2>/dev/null", "r")))
		return;
	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, "Audio", 5)) {
			inaudio = 1;
			continue;
		}
		if (inaudio && (line[0] >= 'A' && line[0] <= 'Z'))
			break;
		if (!inaudio)
			continue;
		if (strstr(line, "Sinks:")) {
			section = 1;
			continue;
		} else if (strstr(line, "Sources:")) {
			section = 2;
			continue;
		} else if (strstr(line, "Streams:")) {
			section = 3;
			top = -1;
			continue;
		} else if (strstr(line, "Devices:") || strstr(line, "Filters:")) {
			section = 0;
			continue;
		}
		if (!section)
			continue;
		for (p = line; *p && !(*p >= '0' && *p <= '9'); p++)
			;
		if (!*p || !(q = strstr(p, ". ")))
			continue;
		indent = (int)(p - line);
		for (e = line; e < p; e++)
			if (*e == '*')
				break;
		if (section == 3) {
			if (top >= 0 && indent > top) {
				if (d->nstreams && strstr(q, " < "))
					d->streams[d->nstreams - 1].capture = 1;
				continue;
			}
			top = indent;
			if (d->nstreams >= (int)(sizeof(d->streams) / sizeof(d->streams[0])))
				continue;
			d->streams[d->nstreams].id = atoi(p);
			d->streams[d->nstreams].capture = 0;
			q += 2;
			while (*q == ' ')
				q++;
			snprintf(d->streams[d->nstreams].name, sizeof(d->streams[d->nstreams].name), "%s", q);
			for (i = (int)strlen(d->streams[d->nstreams].name) - 1; i >= 0 && (d->streams[d->nstreams].name[i] == ' ' || d->streams[d->nstreams].name[i] == '\n'); i--)
				d->streams[d->nstreams].name[i] = '\0';
			d->nstreams++;
			continue;
		}
		if (section == 1 && d->nsinks < 8)
			n = &d->sinks[d->nsinks++];
		else if (section == 2 && d->nsources < 8)
			n = &d->sources[d->nsources++];
		else
			continue;
		n->id = atoi(p);
		n->def = e < p;
		q += 2;
		while (*q == ' ')
			q++;
		if ((e = strstr(q, "[vol:")))
			*e = '\0';
		snprintf(n->name, sizeof(n->name) - 4, "%s", q);
		for (i = (int)strlen(n->name) - 1; i >= 0 && (n->name[i] == ' ' || n->name[i] == '\n'); i--)
			n->name[i] = '\0';
	}
	pclose(fp);
	for (i = 0; i < d->nstreams; i++)
		for (j = i + 1; j < d->nstreams; j++)
			if (!strcmp(d->streams[i].name, d->streams[j].name))
				dup[i] = dup[j] = 1;
	for (i = 0; i < d->nstreams; i++) {
		char id[16];

		if (dup[i])
			medianame(&d->streams[i]);

		snprintf(id, sizeof(id), "%d", d->streams[i].id);
		d->streams[i].valid = volread(id, &d->streams[i].vol, &d->streams[i].muted);
	}
}

static void
gpuread(Poll *d)
{
	char line[256], name[96];
	int util = 0, used = 0, total = 0, temp = 0;
	FILE *fp;

	d->gpuname[0] = d->gpuinfo[0] = '\0';
	if (access("/usr/bin/nvidia-smi", X_OK))
		return;
	if (!(fp = popen("nvidia-smi --query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu "
	                 "--format=csv,noheader,nounits 2>/dev/null", "r")))
		return;
	if (fgets(line, sizeof(line), fp) &&
	    sscanf(line, "%95[^,], %d, %d, %d, %d", name, &util, &used, &total, &temp) == 5) {
		snprintf(d->gpuname, sizeof(d->gpuname) - 4, "%s", name);
		snprintf(d->gpuinfo, sizeof(d->gpuinfo), "%d%%  %.1f / %.1f GiB  %d°C", util, used / 1024.0, total / 1024.0, temp);
		d->gpuutil = util;
		d->gpumemused = used;
		d->gpumemtotal = total;
		d->gputemp = temp;
	}
	pclose(fp);
}

static void
audioread(Audio *a)
{
	char cmd[128], line[256];
	FILE *fp;

	a->valid = 0;
	snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", a->node);
	if (!(fp = popen(cmd, "r")))
		return;
	if (fgets(line, sizeof(line), fp) && sscanf(line, "Volume: %f", &a->vol) == 1) {
		a->valid = 1;
		a->muted = strstr(line, "MUTED") != NULL;
	}
	pclose(fp);
}

static void
pollonce(Poll *d, int gpu)
{
	audioread(&d->sink);
	audioread(&d->source);
	audiostatus(d);
	if (gpu)
		gpuread(d);
}

static void *
poller(void *arg)
{
	static Poll w;
	struct timespec ts;
	int n = 0;

	(void)arg;
	w.sink.node = SINK;
	w.source.node = SOURCE;
	w.gpuutil = -1;
	for (;;) {
		pthread_mutex_lock(&plock);
		if (!kicked) {
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1;
			pthread_cond_timedwait(&pcond, &plock, &ts);
		}
		kicked = 0;
		pthread_mutex_unlock(&plock);
		pollonce(&w, n++ % 2 == 0);
		pthread_mutex_lock(&plock);
		ready = w;
		fresh = 1;
		pthread_mutex_unlock(&plock);
		if (write(pfd[1], "", 1) < 0)
			continue;
	}
	return NULL;
}

static void
kick(void)
{
	pthread_mutex_lock(&plock);
	kicked = 1;
	pthread_cond_signal(&pcond);
	pthread_mutex_unlock(&plock);
}

static void
publish(void)
{
	char c[16];

	while (read(pfd[0], c, sizeof(c)) > 0)
		;
	pthread_mutex_lock(&plock);
	if (fresh && !dragaudio && dragid < 0) {
		fresh = 0;
		memcpy(sinks, ready.sinks, sizeof(sinks));
		memcpy(sources, ready.sources, sizeof(sources));
		memcpy(streams, ready.streams, sizeof(streams));
		nsinks = ready.nsinks;
		nsources = ready.nsources;
		nstreams = ready.nstreams;
		sink.vol = ready.sink.vol;
		sink.muted = ready.sink.muted;
		sink.valid = ready.sink.valid;
		source.vol = ready.source.vol;
		source.muted = ready.source.muted;
		source.valid = ready.source.valid;
		memcpy(gpuname, ready.gpuname, sizeof(gpuname));
		memcpy(gpuinfo, ready.gpuinfo, sizeof(gpuinfo));
		gpuutil = ready.gpuutil;
		gpumemused = ready.gpumemused;
		gpumemtotal = ready.gpumemtotal;
		gputemp = ready.gputemp;
	}
	pthread_mutex_unlock(&plock);
}

static void
cpusample(void)
{
	unsigned long long v[8], total, idle;
	char line[256];
	FILE *fp;
	int k, n = 0;

	if (!(fp = fopen("/proc/stat", "r")))
		return;
	while (fgets(line, sizeof(line), fp) && !strncmp(line, "cpu", 3) && n < 65) {
		memset(v, 0, sizeof(v));
		if (sscanf(line + strcspn(line, " "), "%llu %llu %llu %llu %llu %llu %llu %llu",
		           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 4)
			continue;
		for (total = 0, k = 0; k < 8; k++)
			total += v[k];
		idle = v[3] + v[4];
		cpupct[n] = cputotal[n] && total > cputotal[n]
			? (int)(100 * (1.0 - (double)(idle - cpuidle[n]) / (total - cputotal[n])) + 0.5) : -1;
		cputotal[n] = total;
		cpuidle[n++] = idle;
	}
	ncpu = n - 1;
	fclose(fp);
}

static int
physical(const char *iface)
{
	char path[512];

	snprintf(path, sizeof(path), "/sys/class/net/%s/device", iface);
	return access(path, F_OK) == 0;
}

static unsigned long long
netstat(const char *iface, const char *what)
{
	unsigned long long v = 0;
	char path[512];
	FILE *fp;

	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", iface, what);
	if ((fp = fopen(path, "r"))) {
		if (fscanf(fp, "%llu", &v) != 1)
			v = 0;
		fclose(fp);
	}
	return v;
}

static void
netsample(void)
{
	unsigned long long rx = 0, tx = 0;
	struct timespec ts;
	struct dirent *e;
	double t;
	DIR *d;

	if (!(d = opendir("/sys/class/net")))
		return;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.' && physical(e->d_name)) {
			rx += netstat(e->d_name, "rx_bytes");
			tx += netstat(e->d_name, "tx_bytes");
		}
	closedir(d);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t = ts.tv_sec + ts.tv_nsec / 1e9;
	if (nett > 0 && t > nett && rx >= netrx && tx >= nettx) {
		rxrate = (rx - netrx) / (t - nett);
		txrate = (tx - nettx) / (t - nett);
	}
	netrx = rx;
	nettx = tx;
	nett = t;
}

static void
seed(void)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	char path[256];
	double t;
	unsigned long long a, b;
	FILE *fp;

	snprintf(path, sizeof(path), "%s/sb-sys-cpu", dir ? dir : "/tmp");
	if ((fp = fopen(path, "r"))) {
		if (fscanf(fp, "%lf %llu %llu", &t, &a, &b) == 3) {
			cputotal[0] = a;
			cpuidle[0] = b;
		}
		fclose(fp);
	}
	snprintf(path, sizeof(path), "%s/sb-sys-net", dir ? dir : "/tmp");
	if ((fp = fopen(path, "r"))) {
		if (fscanf(fp, "%lf %llu %llu", &t, &a, &b) == 3) {
			nett = t;
			netrx = a;
			nettx = b;
		}
		fclose(fp);
	}
}

static void
refresh(void)
{
	ticks++;
	cpusample();
	netsample();
}

static int
mdays(int y, int m)
{
	static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	return d[m] + (m == 1 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
}

static void
month(int delta)
{
	calmon += delta;
	while (calmon < 0) {
		calmon += 12;
		calyear--;
	}
	while (calmon > 11) {
		calmon -= 12;
		calyear++;
	}
}

static void
today(void)
{
	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	calmon = t->tm_mon;
	calyear = t->tm_year + 1900;
}

static void
separator(int *y)
{
	fill(pad, *y + lh / 2, W - 2 * pad, 1, CSel);
	*y += lh;
}

static void
mark(int y, int row)
{
	if (row == sel)
		fill((border + pad) / 2 - 1, y + 2, 3, lh - 4, CAccent);
}

static void
drawheader(int *y)
{
	static const char *wd[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
	char tbuf[32], dbuf[32];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	strftime(tbuf, sizeof(tbuf), "%H:%M:%S", t);
	strftime(dbuf, sizeof(dbuf), "%d/%m/%y", t);
	text(pad, *y, CFgSel, tbuf);
	textr(W - pad, *y, CDim, dbuf);
	textr(W - pad - textw(dbuf) - cw, *y, CDim, wd[t->tm_wday]);
	*y += lh + lh / 2;
}

static void
drawtabs(int *y)
{
	static const char *names[] = { "vitals", "sound", "datesys" };
	char s[16];
	int i, w, x = W - pad - cw;

	mark(*y, RTabs);
	text(x, *y, CDim, ">");
	otabs.next = (Rect){ x - cw, *y, 3 * cw, lh };
	x -= 2 * cw;
	for (i = PLast - 1; i >= 0; i--) {
		w = textw(names[i]) + 2 * cw;
		x -= w;
		snprintf(s, sizeof(s), i == page ? "[%s]" : " %s ", names[i]);
		text(x, *y, i == page ? CFgSel : CDim, s);
		otabs.item[i] = (Rect){ x, *y, w, lh };
		x -= cw;
	}
	x -= cw;
	text(x, *y, CDim, "<");
	otabs.prev = (Rect){ x - cw, *y, 3 * cw, lh };
	otabs.row = (Rect){ 0, *y, W, lh };
	*y += lh;
	separator(y);
}

static void
loadlangs(void)
{
	Atom prop = XInternAtom(dpy, "_XKB_RULES_NAMES", False), type;
	unsigned char *data = NULL;
	unsigned long items, after;
	char *p, *s = "us";
	int fmt, i, len;

	if (XGetWindowProperty(dpy, root, prop, 0, 1024, False, XA_STRING, &type, &fmt, &items, &after, &data) == Success &&
	    data) {
		for (p = (char *)data, i = 0; i < 2 && p < (char *)data + items; i++)
			p += strlen(p) + 1;
		if (p < (char *)data + items && *p)
			s = p;
	}
	for (nlang = 0; nlang < 4 && *s; nlang++) {
		len = strcspn(s, ",(");
		if (len == 2 && (!strncmp(s, "us", 2) || !strncmp(s, "gb", 2)))
			snprintf(langs[nlang], sizeof(langs[nlang]), "en");
		else
			snprintf(langs[nlang], sizeof(langs[nlang]), "%.2s", len >= 2 ? s : "??");
		s += strcspn(s, ",");
		if (*s)
			s++;
	}
	if (!nlang)
		snprintf(langs[nlang++], sizeof(langs[0]), "en");
	if (data)
		XFree(data);
}

static int
kbstate(int *caps)
{
	XkbStateRec st;

	XkbGetState(dpy, XkbUseCoreKbd, &st);
	*caps = (st.locked_mods & LockMask) != 0;
	return st.group;
}

static void
drawcal(int *y)
{
	static const char *months[] = { "january", "february", "march", "april", "may", "june", "july",
	                                "august", "september", "october", "november", "december" };
	static const char *wd[] = { "mo", "tu", "we", "th", "fr", "sa", "su" };
	struct tm t = { .tm_mday = 1, .tm_hour = 12 };
	time_t now = time(NULL);
	struct tm tn = *localtime(&now);
	char tbuf[48];
	int i, d, first, days, gx, tw, sw, xs;

	t.tm_year = calyear - 1900;
	t.tm_mon = calmon;
	mktime(&t);
	first = (t.tm_wday + 6) % 7;
	days = mdays(calyear, calmon);
	gx = (W - 20 * cw) / 2;

	text(pad, *y, CHead, "calendar");
	*y += lh;
	mark(*y, RCal);
	snprintf(tbuf, sizeof(tbuf), "%s %d", months[calmon], calyear);
	tw = textw(tbuf);
	sw = textw("september 0000");
	xs = W - pad - 2 * cw - sw;
	text(xs - 2 * cw, *y, CDim, "<");
	text(xs + (sw - tw) / 2, *y, CFg, tbuf);
	text(xs + sw + cw, *y, CDim, ">");
	rprev = (Rect){ xs - 3 * cw, *y, 3 * cw, lh };
	rnext = (Rect){ xs + sw, *y, 3 * cw, lh };
	rcal = (Rect){ 0, *y, W, 8 * lh };
	*y += lh;

	for (i = 0; i < 7; i++)
		text(gx + i * 3 * cw, *y, CDim, wd[i]);
	*y += lh;
	for (d = 1; d <= days; d++) {
		int cell = first + d - 1, c = cell % 7;
		int x = gx + c * 3 * cw, yy = *y + (cell / 7) * lh;

		snprintf(tbuf, sizeof(tbuf), "%2d", d);
		if (d == tn.tm_mday && calmon == tn.tm_mon && calyear == tn.tm_year + 1900) {
			fill(x - cw / 2, yy + 2, 3 * cw, lh - 4, CSel);
			text(x, yy, CAccent, tbuf);
		} else {
			text(x, yy, c >= 5 ? CDim : CFg, tbuf);
		}
	}
	*y += 6 * lh;
}

static void
volbar(int x, int y, int w, float vol, int muted)
{
	int h = lh / 5 > 4 ? lh / 5 : 4, one = (int)(w / VOLMAX + 0.5);
	float v = vol > VOLMAX ? VOLMAX : vol;

	y += (lh - h) / 2;
	fill(x, y, w, h, CSel);
	fill(x, y, (int)((v > 1 ? 1 : v) * one + 0.5), h, muted ? CDim : CAccent);
	if (v > 1)
		fill(x + one, y, (int)((v - 1) * one + 0.5), h, muted ? CDim : CWarn);
	else
		fill(x + one, y, 1, h, CDim);
}

static void
drawaudio(int *y, Audio *a, const char *label, int row)
{
	char pct[16];
	int bx = vcol, bw = W - pad - bx - textw("100%") - cw;

	mark(*y, row);
	text(pad, *y, row == sel ? CFgSel : CDim, label);
	a->label = (Rect){ 0, *y, bx, lh };
	a->bar = (Rect){ 0 };
	if (a->valid) {
		volbar(bx, *y, bw, a->vol, a->muted);
		snprintf(pct, sizeof(pct), "%d%%", (int)(a->vol * 100 + 0.5));
		textr(W - pad, *y, a->muted ? CDim : CFg, a->muted ? "mute" : pct);
		a->bar = (Rect){ bx, *y, bw, lh };
	} else {
		text(bx, *y, CDim, "unavailable");
	}
	*y += lh;
}

static void
drawchoice(int *y, const char *label, const char *value, int w, int active, int row, Picker *p)
{
	char name[128];
	int x = vcol, xr = x + w + 4 * cw;

	mark(*y, row);
	text(pad, *y, row == sel ? CFgSel : CDim, label);
	snprintf(name, sizeof(name) - 4, "%s", value);
	fit(name, w);
	text(x, *y, CDim, "<");
	text(x + 2 * cw + (w - textw(name)) / 2, *y, active ? CFg : CDim, name);
	textr(xr, *y, CDim, ">");
	p->prev = (Rect){ x - cw, *y, 3 * cw, lh };
	p->next = (Rect){ xr - 2 * cw, *y, 3 * cw, lh };
	p->row = (Rect){ 0, *y, W, lh };
	*y += lh;
}

static void
drawpicker(int *y, Node *nodes, int n, const char *label, int row, Picker *p)
{
	const char *name = "none";
	int i;

	for (i = 0; i < n; i++)
		if (nodes[i].def)
			name = nodes[i].name;
	drawchoice(y, label, name, W - pad - vcol - 4 * cw, n > 0, row, p);
}

static void
drawlang(int *y)
{
	int i, c, g = kbstate(&c), w = textw("abc");

	for (i = 0; i < nlang; i++)
		if (textw(langs[i]) > w)
			w = textw(langs[i]);
	text(pad, *y, CHead, "language");
	*y += lh;
	drawchoice(y, "layout", g < nlang ? langs[g] : "??", w, 1, RLang, &playout);
	drawchoice(y, "case", c ? "ABC" : "abc", w, 1, RCaps, &pcase);
}

static void
drawstreams(int *y)
{
	char name[96], pct[16];
	int i, nw = 32 * cw, bx = pad + 34 * cw, bw = W - pad - bx - textw("100%") - cw;

	if (!nstreams)
		return;
	*y += lh / 2;
	text(pad, *y, CDim, "applications");
	*y += lh;
	for (i = 0; i < nstreams; i++) {
		Stream *st = &streams[i];

		snprintf(name, sizeof(name) - 4, "%s%s", st->capture ? "in: " : "", st->name);
		fit(name, nw);
		mark(*y, RApp + i);
		text(pad + cw, *y, RApp + i == sel ? CFgSel : st->muted ? CDim : CFg, name);
		volbar(bx, *y, bw, st->valid ? st->vol : 0, st->muted);
		if (st->valid) {
			snprintf(pct, sizeof(pct), "%d%%", (int)(st->vol * 100 + 0.5));
			textr(W - pad, *y, st->muted ? CDim : CFg, st->muted ? "mute" : pct);
		}
		st->label = (Rect){ 0, *y, bx, lh };
		st->bar = (Rect){ bx, *y, bw, lh };
		*y += lh;
	}
}

static void
row(int *y, const char *label, const char *value)
{
	text(pad, *y, CDim, label);
	text(vcol, *y, CFg, value);
	*y += lh;
}

static void
human(char *b, size_t n, double v, const char *suffix)
{
	static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	int i = 0;

	while (v >= 1024 && i < 4) {
		v /= 1024;
		i++;
	}
	snprintf(b, n, i && v < 10 ? "%.1f %s%s" : "%.0f %s%s", v, u[i], suffix);
}

static void
meter(int *y, const char *label, double frac, int c, const char *right, const char *reserve)
{
	int bx = vcol, bw = W - pad - bx - textw(reserve ? reserve : right) - cw, bh = lh / 5 > 4 ? lh / 5 : 4;

	text(pad, *y, CDim, label);
	fill(bx, *y + (lh - bh) / 2, bw, bh, CSel);
	if (frac >= 0)
		fill(bx, *y + (lh - bh) / 2, (int)(bw * (frac > 1 ? 1 : frac) + 0.5), bh, c);
	textr(W - pad, *y, CFg, right);
	*y += lh;
}

static double
uptime(char *v, size_t n)
{
	double up = 0;
	FILE *fp;

	if ((fp = fopen("/proc/uptime", "r"))) {
		if (fscanf(fp, "%lf", &up) != 1)
			up = 0;
		fclose(fp);
	}
	if (up >= 86400)
		snprintf(v, n, "%dd %dh %dm", (int)up / 86400, (int)up % 86400 / 3600, (int)up % 3600 / 60);
	else
		snprintf(v, n, "%dh %dm", (int)up / 3600, (int)up % 3600 / 60);
	return up;
}

static void
meminfo(unsigned long long *total, unsigned long long *avail, unsigned long long *swtotal, unsigned long long *swfree)
{
	unsigned long long val;
	char line[128];
	FILE *fp;

	*total = *avail = *swtotal = *swfree = 0;
	if (!(fp = fopen("/proc/meminfo", "r")))
		return;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "MemTotal: %llu kB", &val) == 1)
			*total = val * 1024;
		else if (sscanf(line, "MemAvailable: %llu kB", &val) == 1)
			*avail = val * 1024;
		else if (sscanf(line, "SwapTotal: %llu kB", &val) == 1)
			*swtotal = val * 1024;
		else if (sscanf(line, "SwapFree: %llu kB", &val) == 1)
			*swfree = val * 1024;
	}
	fclose(fp);
}

static int
packages(void)
{
	struct dirent *e;
	DIR *d;
	int n = 0;

	if (!(d = opendir("/var/lib/pacman/local")))
		return 0;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.' && e->d_type == DT_DIR)
			n++;
	closedir(d);
	return n;
}

static int
processes(void)
{
	struct dirent *e;
	DIR *d;
	int n = 0;

	if (!(d = opendir("/proc")))
		return 0;
	while ((e = readdir(d)))
		if (e->d_name[0] >= '0' && e->d_name[0] <= '9')
			n++;
	closedir(d);
	return n;
}

static void
cpumodel(char *b, size_t n)
{
	static const char *junk[] = { "(R)", "(TM)", "(tm)" };
	char line[256], *p, *q;
	FILE *fp;
	size_t i, k;

	snprintf(b, n, "cpu");
	if (!(fp = fopen("/proc/cpuinfo", "r")))
		return;
	while (fgets(line, sizeof(line), fp))
		if (!strncmp(line, "model name", 10) && (p = strchr(line, ':'))) {
			p[strcspn(p, "\n")] = '\0';
			for (i = 0; i < sizeof(junk) / sizeof(*junk); i++)
				while ((q = strstr(p, junk[i])))
					memmove(q, q + strlen(junk[i]), strlen(q + strlen(junk[i])) + 1);
			for (p++, q = p, k = 0; *q; q++)
				if (*q != ' ' || (k && p[k - 1] != ' '))
					p[k++] = *q;
			while (k && p[k - 1] == ' ')
				k--;
			p[k] = '\0';
			snprintf(b, n, "%s", p);
			break;
		}
	fclose(fp);
}

static void
row2(int *y, const char *l1, const char *v1, const char *l2, const char *v2)
{
	int mid = pad + 20 * cw;

	text(pad, *y, CDim, l1);
	text(vcol, *y, CFg, v1);
	text(mid, *y, CDim, l2);
	text(mid + 9 * cw, *y, CFg, v2);
	*y += lh;
}

static void
drawsystem(int *y)
{
	char v[128];
	struct utsname u;

	text(pad, *y, CHead, "system");
	*y += lh;
	uname(&u);
	row(y, "host", u.nodename);
	row(y, "kernel", u.release);
	uptime(v, sizeof(v));
	row(y, "uptime", v);
	snprintf(v, sizeof(v), "%d (pacman)", packages());
	row(y, "packages", v);
}

static void
diskrow(int *y, const char *mnt, const char *label)
{
	struct statvfs fs;
	char a[32], b[32], v[80];
	double used, total;

	if (statvfs(mnt, &fs) || !fs.f_blocks)
		return;
	total = (double)fs.f_blocks * fs.f_frsize;
	used = (double)(fs.f_blocks - fs.f_bfree) * fs.f_frsize;
	human(a, sizeof(a), used, "");
	human(b, sizeof(b), total, "");
	snprintf(v, sizeof(v), "%s / %s", a, b);
	meter(y, label, used / total, used / total > 0.9 ? CErr : CAccent, v, NULL);
}

static int
layoutsys(int y)
{
	char v[128], a[32], b[32], line[128];
	unsigned long long mt, ma, st, sf;
	struct statvfs r, h;
	struct ifaddrs *ifs, *i;
	int k, cols4, gw, bh = lh / 5 > 4 ? lh / 5 : 4, threads = 0;
	FILE *fp;

	text(pad, y, CHead, "cpu");
	cpumodel(v, sizeof(v));
	fit(v, W - 2 * pad - 4 * cw);
	textr(W - pad, y, CDim, v);
	y += lh;
	snprintf(v, sizeof(v), "%d%%", cpupct[0] < 0 ? 0 : cpupct[0]);
	meter(&y, "usage", cpupct[0] < 0 ? 0 : cpupct[0] / 100.0, CAccent, v, "100%");
	cols4 = 4;
	gw = (W - 2 * pad - 10 * cw - (cols4 - 1) * cw) / cols4;
	text(pad, y, CDim, "cores");
	for (k = 0; k < ncpu; k++) {
		int cx = vcol + (k % cols4) * (gw + cw), cy = y + (k / cols4) * (lh / 2);
		double f = cpupct[k + 1] < 0 ? 0 : cpupct[k + 1] / 100.0;

		fill(cx, cy + (lh - bh) / 2, gw, bh, CSel);
		fill(cx, cy + (lh - bh) / 2, (int)(gw * f + 0.5), bh, f > 0.85 ? CErr : CAccent);
	}
	y += lh + (ncpu > cols4 ? (ncpu - 1) / cols4 * (lh / 2) : 0);
	if ((fp = fopen("/proc/loadavg", "r"))) {
		if (fscanf(fp, "%*f %*f %*f %*d/%d", &threads) != 1)
			threads = 0;
		fclose(fp);
	}
	snprintf(a, sizeof(a), "%d", processes());
	snprintf(b, sizeof(b), "%d", threads);
	row2(&y, "tasks", a, "threads", b);
	separator(&y);

	text(pad, y, CHead, "memory");
	y += lh;
	meminfo(&mt, &ma, &st, &sf);
	human(a, sizeof(a), mt - ma, "");
	human(b, sizeof(b), mt, "");
	snprintf(v, sizeof(v), "%s / %s", a, b);
	meter(&y, "ram", mt ? (double)(mt - ma) / mt : 0, CAccent, v, NULL);
	if (st) {
		human(a, sizeof(a), st - sf, "");
		human(b, sizeof(b), st, "");
		snprintf(v, sizeof(v), "%s / %s", a, b);
		meter(&y, "swap", (double)(st - sf) / st, CMag, v, NULL);
	}
	separator(&y);

	text(pad, y, CHead, "disk");
	y += lh;
	diskrow(&y, "/", "/");
	if (!statvfs("/", &r) && !statvfs("/home", &h) && r.f_fsid != h.f_fsid)
		diskrow(&y, "/home", "/home");
	separator(&y);

	text(pad, y, CHead, "gpu");
	if (gpuutil >= 0) {
		snprintf(v, sizeof(v) - 4, "%s", gpuname);
		fit(v, W - 2 * pad - 6 * cw);
		textr(W - pad, y, CDim, v);
		y += lh;
		snprintf(v, sizeof(v), "%d%%", gpuutil);
		meter(&y, "usage", gpuutil / 100.0, CAccent, v, "100%");
		snprintf(v, sizeof(v), "%.1f / %.1f GiB", gpumemused / 1024.0, gpumemtotal / 1024.0);
		meter(&y, "vram", gpumemtotal ? (double)gpumemused / gpumemtotal : 0, CMag, v, NULL);
		snprintf(v, sizeof(v), "%d°C", gputemp);
		row(&y, "temp", v);
	} else {
		textr(W - pad, y, CDim, "none");
		y += lh;
	}
	separator(&y);

	text(pad, y, CHead, "net");
	y += lh;
	if (getifaddrs(&ifs) == 0) {
		for (k = 0, i = ifs; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !physical(i->ifa_name))
				continue;
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, line, sizeof(line));
			row(&y, i->ifa_name, line);
			k++;
		}
		freeifaddrs(ifs);
		if (!k)
			row(&y, "link", "offline");
	}
	human(a, sizeof(a), rxrate, "/s");
	human(b, sizeof(b), txrate, "/s");
	row2(&y, "download", a, "upload", b);
	human(a, sizeof(a), netrx, "");
	human(b, sizeof(b), nettx, "");
	row2(&y, "received", a, "sent", b);
	return y + pad - lh / 2;
}

static int
rowok(int r)
{
	if (r == RTabs)
		return 1;
	if (page == PDatesys)
		return r >= RCal && r <= RCaps;
	return page == PSound && r >= ROut && r < RApp + nstreams;
}

static void
nextrow(int dir)
{
	int r;

	for (r = sel + dir; r >= RTabs && r < RApp + nstreams; r += dir)
		if (rowok(r)) {
			sel = r;
			return;
		}
}

static int
layout(void)
{
	int y = pad;

	if (page == PSound && sel >= RApp + nstreams)
		sel = RApp + nstreams - 1;
	if (!rowok(sel))
		sel = RTabs;
	drawtabs(&y);
	if (page == PVitals)
		return layoutsys(y);
	if (page == PSound) {
		drawpicker(&y, sinks, nsinks, "output", ROut, &psink);
		drawaudio(&y, &sink, "volume", RVol);
		drawpicker(&y, sources, nsources, "input", RIn, &psource);
		drawaudio(&y, &source, "microphone", RMic);
		drawstreams(&y);
	} else {
		drawheader(&y);
		drawcal(&y);
		separator(&y);
		drawlang(&y);
		separator(&y);
		drawsystem(&y);
	}
	return y + pad - lh / 2;
}

static void
resize(int h)
{
	H = h;
	XMoveResizeWindow(dpy, win, rightx - W, winy, W, H);
	XftDrawDestroy(xd);
	XFreePixmap(dpy, buf);
	buf = XCreatePixmap(dpy, win, W, H, DefaultDepth(dpy, screen));
	xd = XftDrawCreate(dpy, buf, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
}

static void
draw(void)
{
	int w = pagecols[page] * cw + 2 * pad, h, ow = W;

	W = w;
	vcol = pad + (page == PVitals ? 10 : 14) * cw;
	painting = 0;
	h = layout();
	painting = 1;
	if (h != H || w != ow)
		resize(h);
	fill(0, 0, W, H, CAccent);
	fill(border, border, W - 2 * border, H - 2 * border, CBg);
	layout();
	XCopyArea(dpy, buf, win, gc, 0, 0, W, H, 0, 0);
	XFlush(dpy);
}

static void
setvolume(Audio *a, const char *arg)
{
	run(SETVOL "%s %s >/dev/null 2>&1", a->node, arg);
	kick();
}

static void
togglemute(Audio *a)
{
	run("wpctl set-mute %s %s >/dev/null 2>&1", a->node, "toggle");
	kick();
}

static double
barfrac(Rect r, int x)
{
	double v = (double)(x - r.x) / r.w;

	return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void
drag(int x)
{
	char arg[16];
	int i;

	if (dragaudio) {
		dragaudio->vol = barfrac(dragaudio->bar, x) * VOLMAX;
		snprintf(arg, sizeof(arg), "%.2f", dragaudio->vol);
		run(SETVOL "%s %s >/dev/null 2>&1", dragaudio->node, arg);
		return;
	}
	for (i = 0; i < nstreams; i++) {
		if (streams[i].id == dragid) {
			streams[i].vol = barfrac(streams[i].bar, x) * VOLMAX;
			snprintf(arg, sizeof(arg), "%.2f", streams[i].vol);
			runid(SETVOL "%d %s >/dev/null 2>&1", dragid, arg);
		}
	}
}

static void
dragend(void)
{
	dragaudio = NULL;
	dragid = -1;
	kick();
}

static void
setdefault(Node *nodes, int n, int delta, int capture)
{
	int i, cur = 0;

	if (n < 2)
		return;
	for (i = 0; i < n; i++)
		if (nodes[i].def)
			cur = i;
	nodes[cur].def = 0;
	cur = (cur + delta + n) % n;
	nodes[cur].def = 1;
	runid("wpctl set-default %d >/dev/null 2>&1%s", nodes[cur].id, "");
	for (i = 0; i < nstreams; i++)
		if (streams[i].capture == capture) {
			runid("pw-metadata -d %d %s >/dev/null 2>&1", streams[i].id, "target.object");
			runid("pw-metadata -d %d %s >/dev/null 2>&1", streams[i].id, "target.node");
		}
	kick();
}

static void
setpage(int p)
{
	page = (p + PLast) % PLast;
	sel = RTabs;
}

static void
setgroup(int g)
{
	XkbLockGroup(dpy, XkbUseCoreKbd, g);
	XSync(dpy, False);
}

static void
setcaps(int on)
{
	XkbLockModifiers(dpy, XkbUseCoreKbd, LockMask, on ? LockMask : 0);
	XSync(dpy, False);
}

static void
adjust(int delta)
{
	const char *arg = delta > 0 ? "5%+" : "5%-";
	int c, g = kbstate(&c);

	switch (sel) {
	case RTabs:
		setpage(page + delta);
		break;
	case RCal:
		month(delta);
		break;
	case RLang:
		setgroup((g + delta + nlang) % nlang);
		break;
	case RCaps:
		setcaps(!c);
		break;
	case RVol:
		setvolume(&sink, arg);
		break;
	case ROut:
		setdefault(sinks, nsinks, delta, 0);
		break;
	case RMic:
		setvolume(&source, arg);
		break;
	case RIn:
		setdefault(sources, nsources, delta, 1);
		break;
	default:
		runid(SETVOL "%d %s >/dev/null 2>&1", streams[sel - RApp].id, arg);
		kick();
	}
}

static void
mute(void)
{
	if (sel < ROut)
		return;
	if (sel < RApp) {
		togglemute(sel >= RIn ? &source : &sink);
		return;
	}
	runid("wpctl set-mute %d %s >/dev/null 2>&1", streams[sel - RApp].id, "toggle");
	kick();
}

static int
audioclick(Audio *a, int row, int b, int x, int y)
{
	if (!inrect(a->label, x, y) && !inrect(a->bar, x, y))
		return 0;
	sel = row;
	if (b == Button4)
		setvolume(a, "5%+");
	else if (b == Button5)
		setvolume(a, "5%-");
	else if (b == Button3 || (b == Button1 && inrect(a->label, x, y)))
		togglemute(a);
	else if (b == Button1) {
		dragaudio = a;
		drag(x);
	}
	return 1;
}

static int
choicedelta(Picker *p, int b, int x, int y)
{
	if (b == Button4 || (b == Button1 && inrect(p->prev, x, y)))
		return -1;
	if (b == Button5 || (b == Button1 && inrect(p->next, x, y)))
		return 1;
	return 0;
}

static int
pickerclick(Picker *p, Node *nodes, int n, int row, int b, int x, int y)
{
	int d;

	if (!inrect(p->row, x, y))
		return 0;
	sel = row;
	if ((d = choicedelta(p, b, x, y)))
		setdefault(nodes, n, d, row == RIn);
	return 1;
}

static int
streamclick(int b, int x, int y)
{
	int i;

	for (i = 0; i < nstreams; i++) {
		Stream *st = &streams[i];

		if (!inrect(st->label, x, y) && !inrect(st->bar, x, y))
			continue;
		sel = RApp + i;
		if (b == Button4)
			runid(SETVOL "%d %s >/dev/null 2>&1", st->id, "5%+");
		else if (b == Button5)
			runid(SETVOL "%d %s >/dev/null 2>&1", st->id, "5%-");
		else if (b == Button3 || (b == Button1 && inrect(st->label, x, y)))
			runid("wpctl set-mute %d %s >/dev/null 2>&1", st->id, "toggle");
		else if (b == Button1) {
			dragid = st->id;
			drag(x);
			return 1;
		}
		kick();
		return 1;
	}
	return 0;
}

static int
statusarea(int x, int y)
{
	Atom prop = XInternAtom(dpy, "_DWM_BLOCKS", False), type;
	unsigned long n, after, i;
	unsigned char *data = NULL;
	int fmt, hit = 0;
	long *v;

	if (y >= bary || XGetWindowProperty(dpy, root, prop, 0, 96, False, XA_CARDINAL, &type, &fmt,
	                                    &n, &after, &data) != Success || !data)
		return 0;
	v = (long *)data;
	if (n >= 3)
		hit = x >= v[0] && x < v[n - 3] + v[n - 2];
	for (i = 0; i + 2 < n; i += 3)
		hit |= x >= v[i] && x < v[i] + v[i + 1];
	XFree(data);
	return hit;
}

static int
optclick(Opts *o, int n, int cur, int b, int x, int y)
{
	int i;

	if (!inrect(o->row, x, y))
		return -1;
	if (b == Button4 || (b == Button1 && inrect(o->prev, x, y)))
		return (cur + n - 1) % n;
	if (b == Button5 || (b == Button1 && inrect(o->next, x, y)))
		return (cur + 1) % n;
	for (i = 0; b == Button1 && i < n; i++)
		if (inrect(o->item[i], x, y))
			return i;
	return cur;
}

static void
buttonpress(XButtonEvent *e)
{
	int x = e->x, y = e->y, b = e->button, i;

	if (x < 0 || y < 0 || x >= W || y >= H) {
		if (b <= Button3 && !statusarea(e->x_root, e->y_root)) {
			closing = b;
			replay = e->y_root < bary;
		}
		return;
	}
	if ((i = optclick(&otabs, PLast, page, b, x, y)) >= 0) {
		setpage(i);
		return;
	}
	if (page == PVitals)
		return;
	if (page == PSound) {
		if (audioclick(&sink, RVol, b, x, y) || audioclick(&source, RMic, b, x, y) || streamclick(b, x, y))
			return;
		if (!pickerclick(&psink, sinks, nsinks, ROut, b, x, y))
			pickerclick(&psource, sources, nsources, RIn, b, x, y);
		return;
	}
	if (inrect(playout.row, x, y) || inrect(pcase.row, x, y)) {
		sel = inrect(playout.row, x, y) ? RLang : RCaps;
		if ((i = choicedelta(sel == RLang ? &playout : &pcase, b, x, y)))
			adjust(i);
		return;
	}
	if (b == Button1 && inrect(rprev, x, y))
		month(-1);
	else if (b == Button1 && inrect(rnext, x, y))
		month(1);
	else if (b == Button4 && inrect(rcal, x, y))
		month(-1);
	else if (b == Button5 && inrect(rcal, x, y))
		month(1);
	else
		return;
	sel = RCal;
}

static int
xerror(Display *d, XErrorEvent *e)
{
	(void)d;
	(void)e;
	return 0;
}

static int
shotwindow(void)
{
	Window r, p, *c = NULL;
	XWindowAttributes a;
	XClassHint h;
	unsigned int n, i;
	int found = 0;

	if (!XQueryTree(dpy, DefaultRootWindow(dpy), &r, &p, &c, &n))
		return 0;
	for (i = 0; i < n && !found; i++) {
		if (!XGetWindowAttributes(dpy, c[i], &a) || a.map_state != IsViewable || !XGetClassHint(dpy, c[i], &h))
			continue;
		found = h.res_class && !strcmp(h.res_class, shotclass);
		XFree(h.res_name);
		XFree(h.res_class);
	}
	if (c)
		XFree(c);
	return found;
}

static void
passkey(XKeyEvent *e, int close)
{
	XUngrabKeyboard(dpy, CurrentTime);
	XTestFakeKeyEvent(dpy, e->keycode, False, CurrentTime);
	XTestFakeKeyEvent(dpy, e->keycode, True, CurrentTime);
	XSync(dpy, False);
	if (close)
		running = 0;
	else
		regrab = 1;
}

static void
keypress(XKeyEvent *e)
{
	KeySym k = XLookupKeysym(e, 0);

	if (k >= 0x1008ff00 && k <= 0x1008ffff) {
		passkey(e, 0);
		return;
	}
	if (k == XK_Print) {
		XUngrabPointer(dpy, CurrentTime);
		passkey(e, 0);
		regrab = 0;
		shot = 1;
		shotticks = 0;
		return;
	}
	if (e->state & Mod4Mask) {
		if (k == panelkey)
			running = 0;
		else if (k >= XK_F1 && k <= XK_F12)
			passkey(e, 0);
		else if (!IsModifierKey(k))
			passkey(e, 1);
		return;
	}
	if (k == XK_Escape || k == XK_q) {
		running = 0;
		return;
	}
	switch (k) {
	case XK_Left:
	case XK_h:
		adjust(-1);
		break;
	case XK_Right:
	case XK_l:
		adjust(1);
		break;
	case XK_Home:
	case XK_t:
		if (page == PDatesys)
			today();
		break;
	case XK_Up:
	case XK_k:
		nextrow(-1);
		break;
	case XK_Down:
	case XK_j:
		nextrow(1);
		break;
	case XK_m:
		mute();
		break;
	}
}

static void
grab(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			break;
		nanosleep(&ts, NULL);
	}
	for (i = 0; i < 1000; i++) {
		if (XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask | Button1MotionMask, GrabModeAsync, GrabModeAsync,
		                 None, None, CurrentTime) == GrabSuccess)
			break;
		nanosleep(&ts, NULL);
	}
}

static void
toggle(void)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	char comm[32] = "", path[64];
	FILE *fp;
	int pid = 0;

	snprintf(pidpath, sizeof(pidpath), "%s/distro-panel.pid", dir ? dir : "/tmp");
	if (!(fp = fopen(pidpath, "r")))
		return;
	if (fscanf(fp, "%d", &pid) != 1)
		pid = 0;
	fclose(fp);
	snprintf(path, sizeof(path), "/proc/%d/comm", pid);
	if (pid <= 0 || pid == getpid() || !(fp = fopen(path, "r")))
		return;
	if (!fgets(comm, sizeof(comm), fp))
		comm[0] = '\0';
	fclose(fp);
	if (strncmp(comm, "distro-panel", 12))
		return;
	kill(pid, SIGTERM);
	exit(0);
}

static void
claim(void)
{
	FILE *fp;

	if ((fp = fopen(pidpath, "w"))) {
		fprintf(fp, "%d\n", getpid());
		fclose(fp);
	}
}

static void
place(int *x, int *y)
{
	XineramaScreenInfo *info;
	Window dw;
	int n, i, px = 0, py = 0, di, mx = 0, my = 0, mw, mh;
	unsigned int du;

	mw = DisplayWidth(dpy, screen);
	mh = DisplayHeight(dpy, screen);
	XQueryPointer(dpy, root, &dw, &dw, &px, &py, &di, &di, &du);
	if ((info = XineramaQueryScreens(dpy, &n))) {
		for (i = 0; i < n; i++) {
			if (px >= info[i].x_org && px < info[i].x_org + info[i].width &&
			    py >= info[i].y_org && py < info[i].y_org + info[i].height) {
				mx = info[i].x_org;
				my = info[i].y_org;
				mw = info[i].width;
				mh = info[i].height;
				break;
			}
		}
		XFree(info);
	}
	(void)mh;
	*x = mx + mw - W - gap;
	*y = my + font->ascent + font->descent + barpad + 2 * gap;
	bary = *y - gap;
	rightx = mx + mw - gap;
	winy = *y;
}

int
main(void)
{
	XSetWindowAttributes wa;
	XClassHint ch = { "distro-panel", "distro-panel" };
	Visual *vis;
	Colormap cmap;
	XEvent ev;
	fd_set fds;
	FILE *fp;
	int i, x, y, fd, depth;
	sigset_t mask;
	pthread_t thread;
	struct timespec now;
	time_t tick;

	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
	toggle();
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	XSetErrorHandler(xerror);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	vis = DefaultVisual(dpy, screen);
	cmap = DefaultColormap(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	if (!(font = XftFontOpenName(dpy, screen, fontname)))
		die("cannot load font");
	for (i = 0; i < CLast; i++)
		if (!XftColorAllocName(dpy, vis, cmap, colors[i], &col[i]))
			die("cannot allocate color");

	{
		XGlyphInfo gh, gx;

		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)"H", 1, &gh);
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)"x", 1, &gx);
		tshift = (gh.y - gx.y) / 2;
	}
	{
		int op, err, maj = XkbMajorVersion, min = XkbMinorVersion;

		if (XkbQueryExtension(dpy, &op, &xkbev, &err, &maj, &min))
			XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify, XkbAllStateComponentsMask,
			                      XkbGroupLockMask | XkbModifierLockMask);
		else
			xkbev = -1;
	}
	loadlangs();
	cw = textw("0");
	lh = font->ascent + font->descent + 8;
	pad = 2 * cw;
	vcol = pad + 14 * cw;
	W = pagecols[page] * cw + 2 * pad;
	today();
	seed();
	refresh();
	if (pipe(pfd) < 0)
		die("cannot create pipe");
	fcntl(pfd[0], F_SETFL, O_NONBLOCK);
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);
	ready.sink.node = SINK;
	ready.source.node = SOURCE;
	ready.gpuutil = -1;
	pollonce(&ready, 0);
	fresh = 1;
	publish();

	painting = 0;
	H = layout();
	painting = 1;
	place(&x, &y);

	wa.override_redirect = True;
	wa.background_pixel = col[CBg].pixel;
	wa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;
	win = XCreateWindow(dpy, root, x, y, W, H, 0, depth, CopyFromParent, vis,
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XSetClassHint(dpy, win, &ch);
	buf = XCreatePixmap(dpy, win, W, H, depth);
	xd = XftDrawCreate(dpy, buf, vis, cmap);
	gc = XCreateGC(dpy, win, 0, NULL);

	signal(SIGTERM, onsignal);
	signal(SIGINT, onsignal);
	draw();
	XMapRaised(dpy, win);
	XSync(dpy, False);
	claim();
	grab();
	if (pthread_create(&thread, NULL, poller, NULL))
		die("cannot start poller");
	kick();

	fd = ConnectionNumber(dpy);
	clock_gettime(CLOCK_REALTIME, &now);
	tick = now.tv_sec;
	while (running) {
		struct timeval tv;

		while (running && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == Expose)
				XCopyArea(dpy, buf, win, gc, 0, 0, W, H, 0, 0);
			else if (ev.type == KeyPress)
				keypress(&ev.xkey);
			else if (ev.type == ButtonPress)
				buttonpress(&ev.xbutton);
			else if (ev.type == MotionNotify && (dragaudio || dragid >= 0)) {
				while (XCheckTypedEvent(dpy, MotionNotify, &ev))
					;
				drag(ev.xmotion.x);
			} else if (ev.type == ButtonRelease && ev.xbutton.button == Button1 && (dragaudio || dragid >= 0))
				dragend();
			else if (ev.type == ButtonRelease && closing && ev.xbutton.button == (unsigned)closing) {
				int b = closing;

				running = 0;
				closing = 0;
				if (replay) {
					XUngrabPointer(dpy, CurrentTime);
					XTestFakeButtonEvent(dpy, b, True, CurrentTime);
					XTestFakeButtonEvent(dpy, b, False, CurrentTime);
					XSync(dpy, False);
				}
				continue;
			} else if (ev.type != xkbev)
				continue;
			if (running && ev.type != Expose)
				draw();
		}
		if (!running)
			break;
		if (regrab && XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
			regrab = 0;
			kick();
		}
		if (shot) {
			if (shotwindow())
				shot = 2;
			else if (shot == 2 || ++shotticks > 60) {
				shot = 0;
				grab();
			}
		}
		clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec != tick) {
			tick = now.tv_sec;
			refresh();
			draw();
		}
		tv.tv_sec = 0;
		tv.tv_usec = regrab || shot ? 50000 : 1000000 - now.tv_nsec / 1000;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(pfd[0], &fds);
		if (select((fd > pfd[0] ? fd : pfd[0]) + 1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(pfd[0], &fds)) {
			publish();
			draw();
		}
	}

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XftDrawDestroy(xd);
	XFreePixmap(dpy, buf);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	if ((fp = fopen(pidpath, "r"))) {
		if (fscanf(fp, "%d", &i) == 1 && i == getpid())
			unlink(pidpath);
		fclose(fp);
	}
	return 0;
}
