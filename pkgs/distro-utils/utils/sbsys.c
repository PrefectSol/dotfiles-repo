#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double
now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void
cpu(unsigned long long *total, unsigned long long *idle)
{
	unsigned long long v[8] = { 0 };
	FILE *fp;
	int i;

	*total = *idle = 0;
	if (!(fp = fopen("/proc/stat", "r")))
		return;
	if (fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
	           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
		for (i = 0; i < 8; i++)
			*total += v[i];
		*idle = v[3] + v[4];
	}
	fclose(fp);
}

static int
mem(void)
{
	unsigned long long total = 0, avail = 0, v;
	char line[128];
	FILE *fp;

	if (!(fp = fopen("/proc/meminfo", "r")))
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "MemTotal: %llu kB", &v) == 1)
			total = v;
		else if (sscanf(line, "MemAvailable: %llu kB", &v) == 1)
			avail = v;
	}
	fclose(fp);
	return total ? (int)((total - avail) * 100.0 / total + 0.5) : 0;
}

static unsigned long long
readull(const char *iface, const char *what)
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
net(unsigned long long *rx, unsigned long long *tx)
{
	char path[512];
	struct dirent *e;
	DIR *d;

	*rx = *tx = 0;
	if (!(d = opendir("/sys/class/net")))
		return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/net/%s/device", e->d_name);
		if (access(path, F_OK))
			continue;
		*rx += readull(e->d_name, "rx_bytes");
		*tx += readull(e->d_name, "tx_bytes");
	}
	closedir(d);
}

static void
human(char *b, size_t n, double v)
{
	static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	char num[8];
	int i = 0;

	while (v >= 999.5 && i < 4) {
		v /= 1024;
		i++;
	}
	snprintf(num, sizeof(num), i && v < 9.95 ? "%.1f" : "%.0f", v);
	snprintf(b, n, "%s%s", num, u[i]);
}

int
main(int argc, char *argv[])
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	const char *what = argc > 1 ? argv[1] : "mem";
	unsigned long long a, b, pa = 0, pb = 0;
	double t = now(), pt = 0, dt;
	char path[256], down[16], up[16];
	int have = 0, pct = 0;
	FILE *fp;

	if (!strcmp(what, "mem")) {
		printf("mem: %3d%%\n", mem());
		return 0;
	}
	snprintf(path, sizeof(path), "%s/sb-sys-%s", dir ? dir : "/tmp", what);
	if ((fp = fopen(path, "r"))) {
		have = fscanf(fp, "%lf %llu %llu", &pt, &pa, &pb) == 3;
		fclose(fp);
	}
	if (!strcmp(what, "cpu"))
		cpu(&a, &b);
	else
		net(&a, &b);
	if ((fp = fopen(path, "w"))) {
		fprintf(fp, "%f %llu %llu\n", t, a, b);
		fclose(fp);
	}
	if (!strcmp(what, "cpu")) {
		if (have && a > pa && b >= pb)
			pct = (int)(100 * (1.0 - (double)(b - pb) / (a - pa)) + 0.5);
		printf("cpu: %3d%%\n", pct < 0 ? 0 : pct > 100 ? 100 : pct);
		return 0;
	}
	dt = t - pt;
	if (!have || dt <= 0.05 || a < pa || b < pb)
		pa = a, pb = b, dt = 1;
	human(down, sizeof(down), (a - pa) / dt);
	human(up, sizeof(up), (b - pb) / dt);
	printf("net: %5s/%5s\n", down, up);
	return 0;
}
