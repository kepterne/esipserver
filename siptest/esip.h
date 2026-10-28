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
	char		cseq[64];		// "1 INVITE" — oldugu gibi
	int		cseqnum;
	char		cseqmethod[16];
	int		content_length;		// yoksa -1
	char		*body;
	int		body_len;
} sipmsg;

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
	time_t		tstart;
	time_t		tlast;
	char		lastresp[2048];	// retransmit icin son yanit
	int		lastresp_len;
	struct		sockaddr_in src;
} sipdialog;

int	esip_parse(sipmsg *m, char *buf, int len);
int	cbSIP(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);
void	esip_timer(void);
int64_t	esip_conf(const char *key, int64_t defval);

#endif
