#ifndef	esip_h
#define	esip_h

#include	"utils.h"
#include	"tcp.h"

#define	ESIP_MAX_DIALOGS	64
#define	ESIP_MAX_PACKET		4096
#define	ESIP_RING_TIMEOUT	64		// INVITE cevapsiz (ACK yok) bekleme sn
#define	ESIP_STALE_TIMEOUT	(4 * 3600)	// dogrulanmis dialog icin ust sinir

enum {
	ESIP_DLG_RINGING = 1,
	ESIP_DLG_CONFIRMED = 2
};

typedef	struct sipmsg_st {
	int		isresponse;
	int		code;
	char		method[16];
	char		requri[256];
	char		via[512];		// ust (tek) Via satiri — deger kismi
	char		from[256];
	char		to[256];
	char		callid[128];
	char		contact[256];
	char		pai[256];		// P-Asserted-Identity — deger kismi
	char		content_type[128];	// Content-Type — deger kismi, yoksa bos
	char		cseq[64];		// "1 INVITE" — oldugu gibi
	int		cseqnum;
	char		cseqmethod[16];
	int		content_length;		// yoksa -1
	char		*body;
	int		body_len;
} sipmsg;

//	cagri ici RTP akisi: her ayri SSRC bir taraf (a/b/c/d) — HI3 kaydi,
//	RMS olcumu ve canli yayin akis bazinda tutulur
typedef	struct histream_st {
	uint32_t	ssrc;
	char		file[256];		// eslik dosyasi; bos = henuz paket yok
	FILE		*fp;
	uint32_t	bytes;
	uint32_t	pkts;
	uint16_t	lastseq;
	uint32_t	lost;
	double		rms;			// kisa pencere RMS (linear)
	char		party;			// 'a' (aranan), 'b' (arayan), ...
} histream;

typedef	struct sipdialog_st {
	int		inuse;
	int		state;
	char		callid[128];
	char		totag[32];
	char		from[256];
	char		to[256];
	char		requri[256];
	char		cseq[64];
	int		cseqnum;
	char		cseqmethod[16];
	char		isub_to[64];		// To URI icindeki isub= degeri
	char		isub_from[64];		// From/PAI URI icindeki isub= degeri
	char		rtp_ip[64];		// karsi tarafin SDP c=/m=audio adresi
	int		rtp_port;		// yoksa -1
	uint32_t	rtp_pkts;		// cbRTP sayaclari
	uint32_t	rtp_bytes;
	//	HI3 kaydi (cagri bazli)
	int		hi3_pt;			// secilen ses payload tipi; -1 = SDP'den yok
	char		hi3_codec[16];		// PCMA/PCMU/AMR/AMRBE/RAW
	char		hi3_file[256];		// .ehif tam yolu; bos = henuz paket yok
	FILE		*hi3_fp;
	uint32_t	hi3_bytes;		// .ehif'e yazilan payload bayti
	histream	str[4];			// SSRC bazli akislar (a/b/c/d)
	int		nstr;
	time_t		tstart;
	time_t		tlast;
	char		lastresp[2048];	// retransmit icin son yanit
	int		lastresp_len;
	struct		sockaddr_in src;
} sipdialog;

int	esip_parse(sipmsg *m, char *buf, int len);
int	cbSIP(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);
int	cbRTP(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);
int	cbWeb(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);
void	esip_timer(void);
int64_t	esip_conf(const char *key, int64_t defval);

#endif
