#define	esip_c
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<strings.h>
#include	<time.h>
#include	<unistd.h>
#include	<arpa/inet.h>
#include	<sys/socket.h>

#include	"esip.h"

static	sipdialog	dialogs[ESIP_MAX_DIALOGS];
static	uint32_t	tagcnt = 0;

//	guvenli kopya: hedefe sigdir, hep NUL ile kapat
static	void	sncpy(char *dst, int size, char *src) {
	int	l = strlen(src);
	if (l >= size)
		l = size - 1;
	memcpy(dst, src, l);
	dst[l] = 0;
}

//	"Name:" basligi ise deger kismini (WSP sonrasi) dondur, yoksa NULL
static	char	*hval(char *line, char *name) {
	int	l = strlen(name);
	char	*p;

	if (strncasecmp(line, name, l))
		return NULL;
	p = line + l;
	if (*p != ':')
		return NULL;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

//	NUL iceren govdeye dokunmadan basliklari ayristir.
//	baslik bolgesi hcopy'ye tasinir (NUL garantili), govde m->body ile isaretlenir
int	esip_parse(sipmsg *m, char *buf, int len) {
	static	__thread char	hcopy[ESIP_MAX_PACKET + 2];
	char	*p, *line, *eol, *v, *body = NULL;
	int	hlen, ll, first = 1;
	char	*lastdst = NULL;
	int	lastsize = 0;

	bzero(m, sizeof(*m));
	m->content_length = -1;
	if (len <= 0 || len > ESIP_MAX_PACKET)
		return 1;

	//	baslik sonu: "\r\n\r\n" veya "\n\n"
	for (p = buf; p <= buf + len - 2; p++) {
		if (p[0] != '\n')
			continue;
		if (p[1] == '\n') {
			body = p + 2;
			break;
		}
		if ((p + 2 < buf + len) && (p[1] == '\r') && (p[2] == '\n')) {
			body = p + 3;
			break;
		}
	}

	hlen = body ? (int) (body - buf) : len;
	memcpy(hcopy, buf, hlen);
	hcopy[hlen] = 0;

	if (body) {
		m->body = body;
		m->body_len = len - hlen;
	}

	line = hcopy;
	while (line && *line) {
		eol = strchr(line, '\n');
		if (eol) {
			*eol = 0;
			eol++;
		}
		ll = strlen(line);
		if (ll && line[ll - 1] == '\r')
			line[ll - 1] = 0;

		if (!*line) {
			line = eol;
			continue;
		}

		//	katlanmis (WSP ile baslayan) satir: onceki degere ekle
		if (*line == ' ' || *line == '\t') {
			if (lastdst && lastsize > (int) strlen(lastdst) + 2) {
				v = line;
				while (*v == ' ' || *v == '\t')
					v++;
				strcat(lastdst, " ");
				strncat(lastdst, v, lastsize - strlen(lastdst) - 1);
			}
			line = eol;
			continue;
		}

		lastdst = NULL;
		lastsize = 0;

		if (first) {
			first = 0;
			if (!strncmp(line, "SIP/", 4)) {
				m->isresponse = 1;
				if ((v = strchr(line, ' ')))
					m->code = atoi(v + 1);
			} else {
				char	*sp = strchr(line, ' ');
				char	*sp2 = sp ? strchr(sp + 1, ' ') : NULL;
				if (!sp || !sp2 || strncmp(sp2 + 1, "SIP/", 4))
					return 1;
				*sp = 0;
				*sp2 = 0;
				sncpy(m->method, sizeof(m->method), line);
				sncpy(m->requri, sizeof(m->requri), sp + 1);
			}
			line = eol;
			continue;
		}

		if ((v = hval(line, "Via")) && !m->via[0]) {
			sncpy(m->via, sizeof(m->via), v);
			lastdst = m->via;
			lastsize = sizeof(m->via);
		} else if ((v = hval(line, "From"))) {
			sncpy(m->from, sizeof(m->from), v);
			lastdst = m->from;
			lastsize = sizeof(m->from);
		} else if ((v = hval(line, "To"))) {
			sncpy(m->to, sizeof(m->to), v);
			lastdst = m->to;
			lastsize = sizeof(m->to);
		} else if ((v = hval(line, "Call-ID"))) {
			sncpy(m->callid, sizeof(m->callid), v);
			lastdst = m->callid;
			lastsize = sizeof(m->callid);
		} else if ((v = hval(line, "CSeq"))) {
			sncpy(m->cseq, sizeof(m->cseq), v);
			sscanf(v, "%d %15s", &m->cseqnum, m->cseqmethod);
			lastdst = NULL;
		} else if ((v = hval(line, "Contact"))) {
			sncpy(m->contact, sizeof(m->contact), v);
			lastdst = m->contact;
			lastsize = sizeof(m->contact);
		} else if ((v = hval(line, "Content-Length"))) {
			m->content_length = atoi(v);
			lastdst = NULL;
		}

		line = eol;
	}

	if (!m->isresponse && !m->method[0])
		return 1;
	if (m->body && m->content_length >= 0 && m->content_length <= m->body_len)
		m->body_len = m->content_length;
	return 0;
}

static	sipdialog	*esip_find(char *callid) {
	int	i;
	for (i = 0; i < ESIP_MAX_DIALOGS; i++)
		if (dialogs[i].inuse && !strcmp(dialogs[i].callid, callid))
			return dialogs + i;
	return NULL;
}

static	sipdialog	*esip_new(char *callid) {
	int	i;
	for (i = 0; i < ESIP_MAX_DIALOGS; i++)
		if (!dialogs[i].inuse) {
			bzero(dialogs + i, sizeof(sipdialog));
			dialogs[i].inuse = 1;
			sncpy(dialogs[i].callid, sizeof(dialogs[i].callid), callid);
			sprintf(dialogs[i].totag, "es%08lx%04x",
				(unsigned long) time(NULL),
				(uint16_t) (tagcnt++ & 0xffff));
			return dialogs + i;
		}
	return NULL;
}

static	void	esip_end(sipdialog *d, char *reason) {
	LogPrint("CALL-END callid=%s from=%s to=%s reason=%s dur=%ld\r\n",
		d->callid, d->from, d->to, reason, (long) (time(NULL) - d->tstart));
	d->inuse = 0;
}

static	int	esip_send(HandleEntry *he, struct sockaddr_in *src, char *buf, int n) {
	return sendto(he->fhin, buf, n, 0, (struct sockaddr *) src, sizeof(*src));
}

//	yanit kur ve gonder; cache doluysa retransmit icin sakla
static	int	esip_reply(HandleEntry *he, struct sockaddr_in *src, sipmsg *m,
		sipdialog *d, int code, char *reason, char *extra,
		char *body, int bodylen, char *cache, int *cachelen)
{
	char	out[2048];
	int	n = 0;

	#define	EAPP(fmt, args...) do { \
		int	r = snprintf(out + n, sizeof(out) - n, fmt , ##args); \
		if (r < 0 || n + r >= (int) sizeof(out)) return -1; \
		n += r; \
	} while (0)

	EAPP("SIP/2.0 %d %s\r\n", code, reason);
	if (m->via[0])
		EAPP("Via: %s\r\n", m->via);
	if (m->from[0])
		EAPP("From: %s\r\n", m->from);
	if (m->to[0]) {
		if (d && d->totag[0] && !strcasestr(m->to, ";tag="))
			EAPP("To: %s;tag=%s\r\n", m->to, d->totag);
		else
			EAPP("To: %s\r\n", m->to);
	}
	if (m->callid[0])
		EAPP("Call-ID: %s\r\n", m->callid);
	if (m->cseq[0])
		EAPP("CSeq: %s\r\n", m->cseq);
	if (extra)
		EAPP("%s", extra);
	EAPP("Content-Length: %d\r\n\r\n", body ? bodylen : 0);
	if (body && bodylen > 0)
		EAPP("%.*s", bodylen, body);
	#undef	EAPP

	if (cache && cachelen) {
		memcpy(cache, out, n);
		*cachelen = n;
	}
	return esip_send(he, src, out, n);
}

//	SDP cevabindaki yerel IP: src'ye UDP "connect" ile secilen arayuz
static	void	esip_localip(struct sockaddr_in *src, char *out, int outlen) {
	struct	sockaddr_in a;
	socklen_t	l = sizeof(a);
	int	s = socket(AF_INET, SOCK_DGRAM, 0);

	strcpy(out, "0.0.0.0");
	if (s < 0)
		return;
	if (!connect(s, (struct sockaddr *) src, sizeof(*src))
	&&  !getsockname(s, (struct sockaddr *) &a, &l))
		inet_ntop(AF_INET, &a.sin_addr, out, outlen);
	close(s);
}

static	int	esip_sdp(struct sockaddr_in *src, char *out, int outlen) {
	char	ip[64];
	int64_t	rtpport = esip_conf("esipserv.sip.rtpport", 5072);
	static	uint32_t	sess = 0;
	int	n;

	sess++;
	esip_localip(src, ip, sizeof(ip));
	n = snprintf(out, outlen,
		"v=0\r\n"
		"o=- %u %u IN IP4 %s\r\n"
		"s=esipserv\r\n"
		"c=IN IP4 %s\r\n"
		"t=0 0\r\n"
		"m=audio %ld RTP/AVP 8 0 101\r\n"
		"a=rtpmap:8 PCMA/8000\r\n"
		"a=rtpmap:0 PCMU/8000\r\n"
		"a=rtpmap:101 telephone-event/8000\r\n"
		"a=fmtp:101 0-15\r\n"
		"a=sendrecv\r\n",
		sess, sess, ip, ip, (long) rtpport
	);
	return n > outlen ? outlen : n;
}

static	char	*esip_allow =
	"Allow: INVITE, ACK, CANCEL, BYE, OPTIONS, REGISTER, INFO\r\n";

static	void	esip_invite(HandleEntry *he, struct sockaddr_in *src, sipmsg *m) {
	sipdialog	*d = esip_find(m->callid);
	char		sdp[512];
	int		sdplen;

	//	retransmit: ayni callid + ayni CSeq numarasi → son yaniti tekrar gonder
	if (d && (d->cseqnum == m->cseqnum) && !strcmp(d->cseqmethod, "INVITE") && d->lastresp_len) {
		esip_send(he, src, d->lastresp, d->lastresp_len);
		d->tlast = time(NULL);
		return;
	}

	if (!d) {
		d = esip_new(m->callid);
		if (!d) {
			esip_reply(he, src, m, NULL, 500, "Server Internal Error", NULL, NULL, 0, NULL, NULL);
			return;
		}
	}

	sncpy(d->from, sizeof(d->from), m->from);
	sncpy(d->to, sizeof(d->to), m->to);
	sncpy(d->requri, sizeof(d->requri), m->requri);
	sncpy(d->cseq, sizeof(d->cseq), m->cseq);
	d->cseqnum = m->cseqnum;
	sncpy(d->cseqmethod, sizeof(d->cseqmethod), "INVITE");
	d->src = *src;
	d->tstart = d->tlast = time(NULL);
	d->state = ESIP_DLG_RINGING;

	esip_reply(he, src, m, d, 100, "Trying", NULL, NULL, 0, NULL, NULL);
	esip_reply(he, src, m, d, 180, "Ringing", NULL, NULL, 0, NULL, NULL);

	sdplen = esip_sdp(src, sdp, sizeof(sdp));
	{
		char	extra[512];
		char	ip[64];
		esip_localip(src, ip, sizeof(ip));
		snprintf(extra, sizeof(extra),
			"User-Agent: esipserv/1.0\r\n"
			"Contact: <sip:%s:%ld>\r\n%s",
			ip,
			(long) esip_conf("esipserv.sip.port", 5070),
			esip_allow);
		esip_reply(he, src, m, d, 200, "OK", extra, sdp, sdplen, d->lastresp, &d->lastresp_len);
	}

	LogPrint("CALL-START callid=%s from=%s to=%s uri=%s rtpport=%ld\r\n",
		m->callid, m->from, m->to, m->requri,
		(long) esip_conf("esipserv.sip.rtpport", 5072));
}

static	void	esip_process(HandleEntry *he, char *payload, int plen, struct sockaddr_in *src) {
	sipmsg		m;
	sipdialog	*d;

	if (esip_parse(&m, payload, plen)) {
		LogPrint("SIP-PARSE-ERROR len=%d from %s:%d\r\n",
			plen, inet_ntoa(src->sin_addr), ntohs(src->sin_port));
		return;
	}

	if (m.isresponse) {
		if (esip_conf("esipserv.sip.verbose", 0))
			LogPrint("SIP-RESP %d callid=%s from %s:%d\r\n",
				m.code, m.callid, inet_ntoa(src->sin_addr), ntohs(src->sin_port));
		if ((d = esip_find(m.callid)))
			d->tlast = time(NULL);
		return;
	}

	if (esip_conf("esipserv.sip.verbose", 0))
		LogPrint("SIP-REQ %s callid=%s from %s:%d\r\n",
			m.method, m.callid, inet_ntoa(src->sin_addr), ntohs(src->sin_port));

	if (!strcmp(m.method, "OPTIONS")) {
		esip_reply(he, src, &m, NULL, 200, "OK", esip_allow, NULL, 0, NULL, NULL);
	} else if (!strcmp(m.method, "REGISTER")) {
		char	extra[256];
		snprintf(extra, sizeof(extra), "Expires: 3600\r\n%s", esip_allow);
		esip_reply(he, src, &m, NULL, 200, "OK", extra, NULL, 0, NULL, NULL);
	} else if (!strcmp(m.method, "INVITE")) {
		esip_invite(he, src, &m);
	} else if (!strcmp(m.method, "ACK")) {
		if ((d = esip_find(m.callid)) && d->state == ESIP_DLG_RINGING) {
			d->state = ESIP_DLG_CONFIRMED;
			d->tlast = time(NULL);
			LogPrint("CALL-CONFIRMED callid=%s from=%s to=%s\r\n",
				d->callid, d->from, d->to);
		}
	} else if (!strcmp(m.method, "BYE")) {
		if ((d = esip_find(m.callid))) {
			esip_reply(he, src, &m, d, 200, "OK", NULL, NULL, 0, NULL, NULL);
			esip_end(d, "bye");
		} else
			esip_reply(he, src, &m, NULL, 481, "Call/Transaction Does Not Exist", NULL, NULL, 0, NULL, NULL);
	} else if (!strcmp(m.method, "CANCEL")) {
		if ((d = esip_find(m.callid))) {
			sipmsg	im = m;
			char	csq[64];

			esip_reply(he, src, &m, d, 200, "OK", NULL, NULL, 0, NULL, NULL);
			//	INVITE'ye 487: CSeq dialogdaki (INVITE) degeriyle kurulur
			snprintf(csq, sizeof(csq), "%d INVITE", d->cseqnum);
			sncpy(im.cseq, sizeof(im.cseq), csq);
			esip_reply(he, src, &im, d, 487, "Request Terminated", NULL, NULL, 0, NULL, NULL);
			esip_end(d, "cancel");
		} else
			esip_reply(he, src, &m, NULL, 481, "Call/Transaction Does Not Exist", NULL, NULL, 0, NULL, NULL);
	} else if (!strcmp(m.method, "NOTIFY") || !strcmp(m.method, "INFO")) {
		esip_reply(he, src, &m, NULL, 200, "OK", NULL, NULL, 0, NULL, NULL);
	} else {
		esip_reply(he, src, &m, NULL, 405, "Method Not Allowed", esip_allow, NULL, 0, NULL, NULL);
	}
}

int	cbSIP(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData) {
	struct	sockaddr_in	*src;
	char		*payload;
	int		plen;

	switch (cmd) {
		case EVENT_CREATE:
			LogPrint("esip: udp handle ready\r\n");
		break;
		case EVENT_DATA: {
			if (len <= (int) (sizeof(int) + sizeof(struct sockaddr_in)))
				return 0;
			src = (struct sockaddr_in *) (data + sizeof(int));
			payload = data + sizeof(int) + sizeof(struct sockaddr_in);
			plen = len - sizeof(int) - sizeof(struct sockaddr_in);
			if (src->sin_family != AF_INET || plen <= 0)
				return 0;
			esip_timer();
			esip_process(he, payload, plen, src);
		}
		break;
		case EVENT_FREE:
			LogPrint("esip: udp handle freed\r\n");
		break;
	}
	return 0;
}

void	esip_timer(void) {
	int	i;
	time_t	t = time(NULL);

	for (i = 0; i < ESIP_MAX_DIALOGS; i++) {
		if (!dialogs[i].inuse)
			continue;
		if (dialogs[i].state == ESIP_DLG_RINGING && (t - dialogs[i].tlast) > ESIP_RING_TIMEOUT)
			esip_end(dialogs + i, "noack");
		else if (dialogs[i].state == ESIP_DLG_CONFIRMED && (t - dialogs[i].tlast) > ESIP_STALE_TIMEOUT)
			esip_end(dialogs + i, "stale");
	}
}
