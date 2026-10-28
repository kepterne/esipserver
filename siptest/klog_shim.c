//	bagimsiz derleme icin klog.c yerine gecici Log* uyarlamasi:
//	stderr'e zaman damgali yazar, dosya/syslog yok
#include	<stdio.h>
#include	<stdarg.h>
#include	<string.h>
#include	<sys/time.h>
#include	<time.h>

#include	"utils.h"

void	LogPush(char *s) {
}

void	LogPop(void) {
}

void	LogPrint(char *Str, ...) {
	va_list		ap;
	struct		timeval	tv;
	struct		tm	tm;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	fprintf(stderr, "%02d:%02d:%02d.%03d ",
		tm.tm_hour, tm.tm_min, tm.tm_sec, (int) (tv.tv_usec / 1000));
	va_start(ap, Str);
	vfprintf(stderr, Str, ap);
	va_end(ap);
	fflush(stderr);
}
