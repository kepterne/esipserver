#define	main_c
#define	esipserv_c
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<signal.h>
#include	<unistd.h>
#include	<time.h>

#include	"utils.h"
#include	"tcp.h"
#include	"cson.h"
#include	"esip.h"

#define	ESIP_DEFAULT_SIPPORT	5070
#define	ESIP_DEFAULT_RTPPORT	5072

static	HandleList	*hl = NULL;

static	void	_stop(int sig) {
	Sys.DoStop = 1;
}

static	int	_timer(void *p, int delta, struct timeval tv) {
	esip_timer();
	return 0;
}

//	cson agacindan oku; agac yoksa/kapaliysa varsayilana dus
int64_t	esip_conf(const char *key, int64_t defval) {
	return cson_getIntf(NULL, defval, key);
}

static	void	esip_config(void) {
	//	esipserv.json yoksa bos agac olarak olusturulur; varsayilanlar
	//	yalniz agacta yoksa yazilir — agac tek gercek kaynak (kapan kurali)
	cson_loadOrCreateJson("esipserv.json");
	if (!cson_find(NULL, "esipserv.sip.port"))
		cson_setfInt(NULL, ESIP_DEFAULT_SIPPORT, "esipserv.sip.port");
	if (!cson_find(NULL, "esipserv.sip.rtpport"))
		cson_setfInt(NULL, ESIP_DEFAULT_RTPPORT, "esipserv.sip.rtpport");
	if (!cson_find(NULL, "esipserv.sip.verbose"))
		cson_setfInt(NULL, 0, "esipserv.sip.verbose");
	if (!cson_find(NULL, "esipserv.sip.rtprecv"))
		cson_setfInt(NULL, 1, "esipserv.sip.rtprecv");
	if (!cson_find(NULL, "esipserv.hi2.enable"))
		cson_setfInt(NULL, 1, "esipserv.hi2.enable");
	if (!cson_find(NULL, "esipserv.hi2.folder"))
		cson_setfString(NULL, "hi2", "esipserv.hi2.folder");
	if (!cson_find(NULL, "esipserv.hi3.au"))
		cson_setfInt(NULL, 1, "esipserv.hi3.au");
	if (!cson_find(NULL, "esipserv.hi3.watermark"))
		cson_setfInt(NULL, 0, "esipserv.hi3.watermark");
	if (!cson_find(NULL, "esipserv.hi3.folder"))
		cson_setfString(NULL, "hi3", "esipserv.hi3.folder");
	if (!cson_find(NULL, "esipserv.web.enable"))
		cson_setfInt(NULL, 1, "esipserv.web.enable");
	if (!cson_find(NULL, "esipserv.web.port"))
		cson_setfInt(NULL, 8080, "esipserv.web.port");
	if (!cson_find(NULL, "esipserv.hi2.operator"))
		cson_setfString(NULL, "TurkTelekom", "esipserv.hi2.operator");
	if (!cson_find(NULL, "esipserv.hi2.neid"))
		cson_setfString(NULL, "esipserv", "esipserv.hi2.neid");
	if (!cson_find(NULL, "esipserv.hi2.liid"))
		cson_setfString(NULL, "0400020204030701", "esipserv.hi2.liid");
	if (!cson_find(NULL, "esipserv.hi2.prefix"))
		cson_setfString(NULL, "ESIP", "esipserv.hi2.prefix");
	cson_saveJson("esipserv.json", "esipserv");

	LogPrint("esipserv: config sip.port=%ld sip.rtpport=%ld verbose=%ld rtprecv=%ld hi2.enable=%ld\r\n",
		(long) esip_conf("esipserv.sip.port", ESIP_DEFAULT_SIPPORT),
		(long) esip_conf("esipserv.sip.rtpport", ESIP_DEFAULT_RTPPORT),
		(long) esip_conf("esipserv.sip.verbose", 0),
		(long) esip_conf("esipserv.sip.rtprecv", 1),
		(long) esip_conf("esipserv.hi2.enable", 1));
}

int	main(int argc, char **argv) {
	int64_t	port;
	int	i, wantdaemon = 0, argport = -1;

	signal(SIGINT, _stop);
	signal(SIGTERM, _stop);

	strcpy(Sys.ProgramName, "esipserv");
	strcpy(Sys.AppName, "esipserv");
	Sys.Interval = 1000;
	Sys.NoFancy = 1;
	LogInit();

	//	argumanlar: -d | daemon=1 → arkaplan (syslog); sayi → udp port
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemon")
		||  !strcmp(argv[i], "daemon=1"))
			wantdaemon = 1;
		else if (!strcmp(argv[i], "daemon=0"))
			wantdaemon = 0;
		else if (atoi(argv[i]) > 0)
			argport = atoi(argv[i]);
	}

	esip_config();

	if (wantdaemon) {
		Sys.IsDaemon = 1;
		if (daemon(1, 1) < 0) {
			LogPrint("esipserv: daemon() hatasi\r\n");
			return 1;
		}
	}

	//	test kolayligi: port argumani json degerini gecersiz kilar
	port = esip_conf("esipserv.sip.port", ESIP_DEFAULT_SIPPORT);
	if (argport > 0)
		port = argport;

	hl = InitHandles(1024, 256, 1000, _timer);
	if (!hl) {
		LogPrint("esipserv: InitHandles hatasi\r\n");
		return 1;
	}

	if (!AddUDPServer(hl, (int) port, cbSIP)) {
		LogPrint("esipserv: UDP %ld acilamadi (port musait mi?)\r\n", (long) port);
		return 1;
	}

	//	RTP: karsi tarafin medyasi icin tek (static) port dinleyici
	if (esip_conf("esipserv.sip.rtprecv", 1)) {
		int64_t	rtpport = esip_conf("esipserv.sip.rtpport", ESIP_DEFAULT_RTPPORT);

		if (!AddUDPServer(hl, (int) rtpport, cbRTP))
			LogPrint("esipserv: RTP udp %ld acilamadi\r\n", (long) rtpport);
	}

	//	Web: canli dinleme / kayit listesi / indirme
	if (esip_conf("esipserv.web.enable", 1)) {
		int64_t	webport = esip_conf("esipserv.web.port", 8080);

		if (!AddTCPServerToIP(hl, "0.0.0.0", (int) webport, cbWeb))
			LogPrint("esipserv: web tcp %ld acilamadi\r\n", (long) webport);
		else
			LogPrint("esipserv: web http://0.0.0.0:%ld/\r\n", (long) webport);
	}

	LogPrint("esipserv: dinleniyor udp://%ld (rtp %ld%s)\r\n",
		(long) port,
		(long) esip_conf("esipserv.sip.rtpport", ESIP_DEFAULT_RTPPORT),
		esip_conf("esipserv.sip.rtprecv", 1) ? "" : ", rtp kapali");

	while (!Sys.DoStop)
		PollHandles(hl);

	LogPrint("esipserv: kapatiliyor\r\n");
	CloseHandles(hl);
	return 0;
}
