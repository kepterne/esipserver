#define	esip_c
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<strings.h>
#include	<ctype.h>
#include	<errno.h>
#include	<math.h>
#include	<time.h>
#include	<unistd.h>
#include	<arpa/inet.h>
#include	<sys/socket.h>
#include	<sys/stat.h>
#include	<sys/time.h>

#include	"esip.h"
#include	"cson.h"

static	sipdialog	dialogs[ESIP_MAX_DIALOGS];
static	uint32_t	tagcnt = 0;

static	void	ehif_write(sipdialog *d, int iri_type);
static	void	hi3_open(sipdialog *d);
static	void	hi3_close(sipdialog *d);
static	void	hi3_write(sipdialog *d, histream *st, uint32_t rtpts,
		uint16_t rtpseq, unsigned char *payload, int plen);
static	int	etlv(unsigned char *out, int tag, const void *p, int plen);
static	short	alaw2linear(unsigned char a_val);
static	void	ws_push(sipdialog *d, histream *st, unsigned char *payload, int plen);
static	void	ws_status(sipdialog *d);

//	64-bit tekrarli su isareti (watermark) kancasi: dogrusal (linear) PCM
//	uzerinde calisir, cagri korelasyon numarasini tasir. Gercek uygulama
//	baska projeden buraya tasinir — varsayilan no-op.
//	.ehif arsivi HAM payload'i korur; su isareti yalniz calinabilir eslik
//	dosyasina ve canli yayina uygulanir.
void	esipwm_embed(short *pcm, int n, uint64_t id);

static	unsigned char	linear2alaw(short pcm_val);
static	unsigned char	linear2ulaw(short pcm_val);

//	AMR cerceve uzunluklari (octet-aligned, TOC haric) — RFC 4867
static	int	amr_flen[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };

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
		} else if ((v = hval(line, "P-Asserted-Identity"))) {
			sncpy(m->pai, sizeof(m->pai), v);
			lastdst = m->pai;
			lastsize = sizeof(m->pai);
		} else if ((v = hval(line, "Content-Type"))) {
			sncpy(m->content_type, sizeof(m->content_type), v);
			lastdst = m->content_type;
			lastsize = sizeof(m->content_type);
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
	hi3_close(d);
	LogPrint("CALL-END callid=%s from=%s to=%s reason=%s dur=%ld rtp=%upkt/%ubyte\r\n",
		d->callid, d->from, d->to, reason, (long) (time(NULL) - d->tstart),
		d->rtp_pkts, d->rtp_bytes);
	ehif_write(d, 2);
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

//	binary-guvenli arama: buf icinde needle'in ilk konumu
static	char	*memfind(char *buf, int blen, char *needle, int nlen) {
	int	i;

	if (nlen <= 0 || blen < nlen)
		return NULL;
	for (i = 0; i <= blen - nlen; i++)
		if (!memcmp(buf + i, needle, nlen))
			return buf + i;
	return NULL;
}

//	baslik degerinden (To/From/PAI) isub= parametresini cikar
static	void	xisub(char *dst, int dsize, char *hdr) {
	char	*p, *e;
	int	l;

	dst[0] = 0;
	if (!hdr || !(p = strstr(hdr, "isub=")))
		return;
	p += 5;
	for (e = p; *e && *e != ';' && *e != '>' && *e != '"' && *e != ' '; e++);
	l = (int) (e - p);
	if (l >= dsize)
		l = dsize - 1;
	memcpy(dst, p, l);
	dst[l] = 0;
}

//	URI'den kullanici kismini cikar: "sip:NUM@...", "tel:NUM;..."
static	void	uri_user(char *dst, int dsize, char *hdr) {
	char	*p, *e;
	int	l;

	dst[0] = 0;
	if (!hdr || !(p = strchr(hdr, ':')))
		return;
	p++;
	for (e = p; *e && *e != '@' && *e != ';' && *e != '>'; e++);
	l = (int) (e - p);
	if (l >= dsize)
		l = dsize - 1;
	memcpy(dst, p, l);
	dst[l] = 0;
}

//	INVITE govdesinden SDP'yi cikar: multipart/mixed ise ilk application/sdp
//	parcasinin govdesi, degilse govdenin kendisi. Donus: out'a yazilan uzunluk.
static	int	extract_sdp(sipmsg *m, char *out, int outsize) {
	char	ct[160], bnd[96], needle[100];
	char	*p, *part, *partend, *hdrend, *nl;
	int	i, l, hl, bl, bndlen;

	if (!m->body || m->body_len <= 0 || outsize < 64)
		return 0;
	for (i = 0; i < (int) sizeof(m->content_type) - 1 && m->content_type[i]; i++)
		ct[i] = tolower((unsigned char) m->content_type[i]);
	ct[i] = 0;

	//	tek parca govde
	if (!strstr(ct, "multipart")) {
		if (ct[0] && !strstr(ct, "application/sdp"))
			return 0;
		l = m->body_len < outsize - 1 ? m->body_len : outsize - 1;
		memcpy(out, m->body, l);
		out[l] = 0;
		return l;
	}

	//	multipart: boundary= degeri — orijinal (buyuk/kucuk harfli) degerden alinir,
	//	cunku MIME boundary BUYUK/kucuk harfe duyarlidir
	for (p = m->content_type; p && *p; p++) {
		if (!strncasecmp(p, "boundary=", 9))
			break;
	}
	if (!p || !*p)
		return 0;
	p += 9;
	i = 0;
	if (*p == '"') {
		p++;
		while (p[i] && p[i] != '"' && i < (int) sizeof(bnd) - 1) { bnd[i] = p[i]; i++; }
	} else
		while (p[i] && p[i] != ';' && p[i] != ' ' && i < (int) sizeof(bnd) - 1) { bnd[i] = p[i]; i++; }
	bnd[i] = 0;
	if (!bnd[0])
		return 0;
	bndlen = strlen(bnd);
	snprintf(needle, sizeof(needle), "--%s", bnd);

	//	parcalari gez; application/sdp iceren ilk parcanin govdesini al
	part = m->body;
	partend = m->body + m->body_len;
	while ((p = memfind(part, (int) (partend - part), needle, bndlen + 2)) != NULL) {
		if (!(nl = memchr(p, '\n', partend - p)))
			return 0;
		part = nl + 1;
		p = memfind(part, (int) (partend - part), needle, bndlen + 2);
		if (!p)
			p = partend;
		l = (int) (p - part);
		if (l >= 2 && part[l - 1] == '\n' && part[l - 2] == '\r')
			l -= 2;
		if ((hdrend = memfind(part, l, "\r\n\r\n", 4)) != NULL) {
			char	hb[256];

			hl = (int) (hdrend - part);
			if (hl > (int) sizeof(hb) - 1)
				hl = sizeof(hb) - 1;
			for (i = 0; i < hl; i++)
				hb[i] = tolower((unsigned char) part[i]);
			hb[hl] = 0;
			if (strstr(hb, "content-type") && strstr(hb, "application/sdp")) {
				bl = l - (int) (hdrend + 4 - part);
				if (bl > outsize - 1)
					bl = outsize - 1;
				if (bl <= 0)
					return 0;
				memcpy(out, hdrend + 4, bl);
				out[bl] = 0;
				return bl;
			}
		}
	}
	return 0;
}

//	callid'den FNV-1a korelasyon numarasi (8 bayt, big endian)
static	void	corr_of(sipdialog *d, unsigned char *corr) {
	uint64_t	h = 1469598103934665603ULL;
	int		i;

	for (i = 0; d->callid[i]; i++) {
		h ^= (unsigned char) d->callid[i];
		h *= 1099511628211ULL;
	}
	for (i = 0; i < 8; i++)
		corr[i] = (unsigned char) (h >> (56 - 8 * i));
}

//	u-law -> linear (RMS olcumu icin)
static	short	ulaw2linear(unsigned char u_val) {
	short	t;

	u_val = ~u_val;
	t = ((u_val & 0x0F) << 3) + 0x84;
	t <<= (u_val & 0x70) >> 4;
	return (u_val & 0x80) ? (0x84 - t) : (t - 0x84);
}

//	linear -> A-law (su isaretli kopyayi yeniden kodlamak icin)
static	unsigned char	linear2alaw(short pcm_val) {
	short		mask, seg;
	unsigned char	aval;

	pcm_val = pcm_val >> 3;
	if (pcm_val >= 0)
		mask = 0xD5;
	else {
		mask = 0x55;
		pcm_val = -pcm_val - 1;
		if (pcm_val < 0)
			pcm_val = 0;
	}
	seg = 0;
	while (pcm_val > 0x7F) {
		pcm_val >>= 4;
		seg++;
	}
	if (seg > 7)
		seg = 7;
	aval = (unsigned char) ((seg << 4) | ((pcm_val >> 1) & 0x0F));
	return (aval ^ mask);
}

//	varsayilan no-op su isareti (gercek uygulama baska projeden gelir)
void	esipwm_embed(short *pcm, int n, uint64_t id) {
	(void) pcm; (void) n; (void) id;
}

//	HI3 kaydi: cagri basina bir .ehif (paket basina BER kayit: LIID + us
//	zaman damgasi + seq + ssrc + codec/rtp-ts/rtp-seq + payload) ve akis
//	(taraf) basina calinabilir eslik dosyasi (.au/.amr/.amrbe/.raw).
//	Ayri SSRC = ayri taraf: 'a' (ilk gorulen), 'b' (ikinci), ...
static	uint32_t	hi3_seq = 0;

static	void	hi3_open(sipdialog *d) {
	char	ts[32], folder[128], corrhex[20];
	struct	timeval	tv;
	struct	tm	tm;
	unsigned char	corr[8];

	if (d->hi3_fp || !esip_conf("esipserv.hi3.enable", 1))
		return;
	cson_getStringf(NULL, folder, "hi3", "esipserv.hi3.folder");
	mkdir(folder, 0755);
	corr_of(d, corr);
	for (int i = 0; i < 8; i++)
		sprintf(corrhex + i * 2, "%02x", corr[i]);
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", &tm);
	snprintf(d->hi3_file, sizeof(d->hi3_file), "%s/ESIP_%s%03u_%s.ehif",
		folder, ts, (unsigned) (tv.tv_usec / 1000), corrhex);
	if ((d->hi3_fp = fopen(d->hi3_file, "wb")) == NULL) {
		LogPrint("HI3 dosya acilamadi: %s\r\n", d->hi3_file);
		return;
	}
	fwrite("handover-interface-stream,v1.1,ehif", 1, 35, d->hi3_fp);
	fputc(0, d->hi3_fp);
	LogPrint("HI3-OPEN file=%s codec=%s pt=%d\r\n",
		d->hi3_file, d->hi3_codec, d->hi3_pt);
}

static	void	hi3_open_stream(sipdialog *d, histream *st) {
	char	ts[32], folder[128], corrhex[20], base[200];
	struct	timeval	tv;
	struct	tm	tm;
	unsigned char	au[24] = {
		'.', 's', 'n', 'd', 0, 0, 0, 24, 0xFF, 0xFF, 0xFF, 0xFF,
		0, 0, 0, 27, 0, 0, 0x1F, 0x40, 0, 0, 0, 1
	};
	unsigned char	corr[8];
	char	*ext = "raw";
	int	auenc = 0;

	if (st->fp || !esip_conf("esipserv.hi3.au", 1))
		return;
	cson_getStringf(NULL, folder, "hi3", "esipserv.hi3.folder");
	mkdir(folder, 0755);
	corr_of(d, corr);
	for (int i = 0; i < 8; i++)
		sprintf(corrhex + i * 2, "%02x", corr[i]);
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", &tm);
	snprintf(base, sizeof(base), "%s/ESIP_%s%03u_%s",
		folder, ts, (unsigned) (tv.tv_usec / 1000), corrhex);
	if (!strcmp(d->hi3_codec, "PCMU")) { ext = "au"; auenc = 1; au[15] = 1; }
	else if (!strcmp(d->hi3_codec, "PCMA")) { ext = "au"; auenc = 27; }
	else if (!strcmp(d->hi3_codec, "AMR")) ext = "amr";
	else if (!strcmp(d->hi3_codec, "AMRBE")) ext = "amrbe";
	snprintf(st->file, sizeof(st->file), "%s_%c.%s", base, st->party, ext);
	if ((st->fp = fopen(st->file, "wb")) == NULL)
		return;
	if (auenc)
		fwrite(au, 1, 24, st->fp);
	else if (!strcmp(ext, "amr"))
		fwrite("#!AMR\n", 1, 6, st->fp);
	LogPrint("HI3-STREAM %c ssrc=%08x file=%s\r\n", st->party, st->ssrc, st->file);
}

static	void	hi3_close(sipdialog *d) {
	int	i;

	for (i = 0; i < 4; i++) {
		if (d->str[i].fp) {
			fclose(d->str[i].fp);
			d->str[i].fp = NULL;
			LogPrint("HI3-STREAM-CLOSE %c bytes=%u lost=%u\r\n",
				d->str[i].party, d->str[i].bytes, d->str[i].lost);
		}
	}
	if (d->hi3_fp) {
		fclose(d->hi3_fp);
		d->hi3_fp = NULL;
		LogPrint("HI3-CLOSE file=%s bytes=%u\r\n", d->hi3_file, d->hi3_bytes);
	}
}

//	bir RTP paketini kaydet: .ehif BER kaydi + akis eslig dosyasi + WS canli
static	void	hi3_write(sipdialog *d, histream *st, uint32_t rtpts,
		uint16_t rtpseq, unsigned char *payload, int plen) {
	unsigned char	liid[8], corr[8], seq3[3], ts4[4], sq2[2], ssrc4[4];
	unsigned char	rec[512], hdr[128], media[384], ident[160], content[384];
	char	liids[48], op[48], neid[48], ts1[32], ts2[32];
	struct	timeval	tv;
	struct	tm	tm;
	int	i, n, hn, mn, idtot = 0, total;

	if (!d->hi3_fp)
		hi3_open(d);
	if (!d->hi3_fp)
		return;
	hi3_open_stream(d, st);
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts1, sizeof(ts1), "%y%m%d%H%M%S", &tm);
	snprintf(ts1 + 12, sizeof(ts1) - 12, "%06ld", (long) tv.tv_usec);
	strftime(ts2, sizeof(ts2), "%Y%m%d%H%M%S", &tm);
	snprintf(ts2 + 14, sizeof(ts2) - 14, ".%d", (int) (tv.tv_usec / 100000));
	hi3_seq++;
	seq3[0] = hi3_seq >> 16; seq3[1] = hi3_seq >> 8; seq3[2] = hi3_seq;
	ts4[0] = rtpts >> 24; ts4[1] = rtpts >> 16; ts4[2] = rtpts >> 8; ts4[3] = rtpts;
	sq2[0] = rtpseq >> 8; sq2[1] = rtpseq;
	ssrc4[0] = st->ssrc >> 24; ssrc4[1] = st->ssrc >> 16;
	ssrc4[2] = st->ssrc >> 8; ssrc4[3] = st->ssrc;

	//	baslik: [1] { [0]liid [1]ts1 [4]seq [5]ts2 } (+ ilk kayitta [3]tanimlama)
	cson_getStringf(NULL, liids, "0400020204030701", "esipserv.hi2.liid");
	memset(liid, 0, sizeof(liid));
	{
		char	hb[3];
		for (i = 0; i < 8 && liids[i * 2] && liids[i * 2 + 1]; i++) {
			memcpy(hb, liids + i * 2, 2);
			hb[2] = 0;
			liid[i] = (unsigned char) strtoul(hb, NULL, 16);
		}
	}
	hn = etlv(hdr, 0x80, liid, sizeof(liid));
	hn += etlv(hdr + hn, 0x81, ts1, 18);
	if (hi3_seq == 1) {
		//	ilk kayit: operator/neid/korelasyon tanimlamasi
		unsigned char	corrtlv[16], opneid[128], inner0[160];
		int	m;
		corr_of(d, corr);
		cson_getStringf(NULL, op, "TurkTelekom", "esipserv.hi2.operator");
		cson_getStringf(NULL, neid, "esipserv", "esipserv.hi2.neid");
		m = etlv(corrtlv, 0x81, corr, sizeof(corr));
		n = etlv(opneid, 0x80, op, strlen(op));
		n += etlv(opneid + n, 0x81, neid, strlen(neid));
		n += etlv(opneid + n, 0xA2, corrtlv, m);
		m = etlv(inner0, 0xA0, opneid, n);
		idtot = etlv(ident, 0xA3, inner0, m);
		memcpy(hdr + hn, ident, idtot);
		hn += idtot;
	}
	hn += etlv(hdr + hn, 0x84, seq3, 3);
	hn += etlv(hdr + hn, 0x85, ts2, strlen(ts2));

	//	medya: [2] { [0] { SEQ { [0]codec [1]rtp-ts [2]rtp-seq [3]payload [4]ssrc } } }
	{
		unsigned char	*pp = payload;
		int	remaining = plen;
		//	kayit toplami 255 bayti asmamali: payload parcasi buna gore
		int	budget = 250 - hn - 50;

		if (budget < 1)
			budget = 1;
		while (remaining > 0) {
			unsigned char	c0[256], sq[320];
			int	l = remaining > budget ? budget : remaining;

			n = etlv(c0, 0x80, d->hi3_codec, strlen(d->hi3_codec));
			n += etlv(c0 + n, 0x81, ts4, 4);
			n += etlv(c0 + n, 0x82, sq2, 2);
			n += etlv(c0 + n, 0x83, pp, l);
			n += etlv(c0 + n, 0x84, ssrc4, 4);
			n = etlv(sq, 0x30, c0, n);
			n = etlv(content, 0xA0, sq, n);
			mn = etlv(media, 0xA2, content, n);
			n = etlv(rec, 0xA1, hdr, hn);
			memcpy(rec + n, media, mn);
			n += mn;
			total = etlv(content, 0x30, rec, n);
			if (total > 255) {
				LogPrint("HI3 kayit cok buyuk (%d B) — atlandi\r\n", total);
				break;
			}
			fputc(total, d->hi3_fp);
			fwrite(content, 1, total, d->hi3_fp);
			d->hi3_bytes += l;
			pp += l;
			remaining -= l;
		}
	}

	//	akis eslig dosyasi: G.711 (istege bagli su isaretli), AMR-OA: CMR at
	if (st->fp) {
		if ((!strcmp(d->hi3_codec, "PCMA") || !strcmp(d->hi3_codec, "PCMU"))) {
			//	G.711: coz -> (su isareti) -> RMS -> yeniden kodla
			unsigned char	outb[256];
			unsigned char	*wp = payload;
			short		lin[256];
			uint64_t	wmid = 0;
			int		wm = esip_conf("esipserv.hi3.watermark", 0) != 0;

			if (plen > 256)
				plen = 256;
			for (i = 0; i < plen; i++)
				lin[i] = !strcmp(d->hi3_codec, "PCMA")
					? alaw2linear(payload[i]) : ulaw2linear(payload[i]);
			if (wm) {
				unsigned char	corr8[8];

				corr_of(d, corr8);
				for (i = 0; i < 8; i++)
					wmid = (wmid << 8) | corr8[i];
				esipwm_embed(lin, plen, wmid);
				for (i = 0; i < plen; i++)
					outb[i] = !strcmp(d->hi3_codec, "PCMA")
						? linear2alaw(lin[i]) : linear2ulaw(lin[i]);
				wp = outb;
			}
			{
				double	sq = 0.0;
				for (i = 0; i < plen; i++)
					sq += (double) lin[i] * lin[i];
				st->rms = 0.7 * st->rms + 0.3 * sqrt(sq / plen);
			}
			fwrite(wp, 1, plen, st->fp);
			st->bytes += plen;
			st->pkts++;
			ws_push(d, st, wp, plen);
			if ((st->pkts % 25) == 0)
				ws_status(d);
			return;
		}
		if (!strcmp(d->hi3_codec, "AMR") && plen >= 2) {
			unsigned char	*q = payload + 1;	// CMR baytini atla
			int	rem = plen - 1;

			while (rem > 0) {
				int	toc = *(q++);
				int	ft = (toc >> 3) & 0x0F;
				int	fl = ft < 16 ? amr_flen[ft] : 0;

				rem--;
				if (fl > rem)
					fl = rem;
				fputc(toc, st->fp);
				fwrite(q, 1, fl, st->fp);
				q += fl;
				rem -= fl;
				if (!(toc & 0x80))	// F=0: son cerceve
					break;
			}
		} else {
			fwrite(payload, 1, plen, st->fp);
			st->bytes += plen;
		}
	}
	st->pkts++;
	ws_push(d, st, payload, plen);
	if ((st->pkts % 25) == 0)
		ws_status(d);
}

//	SDP'den karsi tarafin c= IP'si, m=audio portu ve ses codec'u cikarilir.
//	rtpmap/fmtp satirlarindan payload-tip haritasi kurulur; codec secim
//	onceligi: PCMA, PCMU, AMR(octet-aligned), AMR(bandwidth-efficient), RAW.
static	void	sdp_rtp(char *sdp, int slen, char *ip, int iplen, int *port,
		int *pt, char *codec, int csize) {
	char	*p = sdp, *end = sdp + slen, *eol, tmp[64];
	static	char	map[128][16];
	int	ll, i, l, ptv, mpt[32], nmpt = 0, octetalign = 0, chosen = -1;

	for (i = 0; i < 128; i++)
		map[i][0] = 0;
	ip[0] = 0;
	*port = -1;
	*pt = -1;
	snprintf(codec, csize, "RAW");
	while (p < end) {
		eol = memchr(p, '\n', end - p);
		ll = eol ? (int) (eol - p) : (int) (end - p);
		if (ll >= 9 && !strncmp(p, "c=IN IP4 ", 9)) {
			l = ll - 9;
			if (l > iplen - 1)
				l = iplen - 1;
			while (l > 0 && (p[9 + l - 1] == '\r' || p[9 + l - 1] == ' '))
				l--;
			memcpy(ip, p + 9, l);
			ip[l] = 0;
		} else if (ll > 8 && !strncmp(p, "m=audio ", 8)) {
			//	m=audio <port> <proto> <pt> <pt> ...
			l = ll - 8;
			if (l > (int) sizeof(tmp) - 1)
				l = sizeof(tmp) - 1;
			memcpy(tmp, p + 8, l);
			tmp[l] = 0;
			{
				char	*t = tmp, *e;
				long	v = strtol(t, &e, 10);
				if (v > 0 && e > t)
					*port = (int) v;
				t = e;
				for (i = 0; i < 3 && t; i++)	// protokol belirtecini atla
					t = strchr(t + (i ? 0 : 1), ' ');
				while (t && nmpt < 32) {
					v = strtol(t, &e, 10);
					if (e == t)
						break;
					mpt[nmpt++] = (int) v;
					t = e;
				}
			}
		} else if (ll > 9 && !strncmp(p, "a=rtpmap:", 9)) {
			//	a=rtpmap:<pt> <isim>/rate
			l = ll - 9;
			if (l > (int) sizeof(tmp) - 1)
				l = sizeof(tmp) - 1;
			memcpy(tmp, p + 9, l);
			tmp[l] = 0;
			ptv = atoi(tmp);
			{
				char	*sp = strchr(tmp, ' ');
				char	*sl2 = sp ? strchr(sp, '/') : NULL;
				if (sp && ptv >= 0 && ptv < 128) {
					if (sl2)
						*sl2 = 0;
					for (l = 0; sp[l + 1]; l++)	// sola kaydir
						sp[l] = sp[l + 1];
					sp[l] = 0;
					snprintf(map[ptv], 16, "%s", sp);
				}
			}
		} else if (ll > 7 && !strncmp(p, "a=fmtp:", 7)) {
			//	AMR icin octet-align=1 var mi? (yoksa bandwidth-efficient)
			l = ll - 7;
			if (l > (int) sizeof(tmp) - 1)
				l = sizeof(tmp) - 1;
			memcpy(tmp, p + 7, l);
			tmp[l] = 0;
			if (atoi(tmp) >= 0 && atoi(tmp) < 128 && strstr(tmp, "octet-align=1"))
				octetalign = 1;
		}
		p = eol ? eol + 1 : end;
	}

	//	statik tipler (rtpmap olmasa da)
	if (!map[0][0]) snprintf(map[0], 16, "PCMU");
	if (!map[8][0]) snprintf(map[8], 16, "PCMA");

	//	oncelik sirasina gore codec sec
	for (i = 0; i < nmpt && chosen < 0; i++) {
		if (strstr(map[mpt[i]], "telephone-event"))
			continue;
		if (!strcmp(map[mpt[i]], "PCMA"))
			chosen = mpt[i];
	}
	for (i = 0; i < nmpt && chosen < 0; i++) {
		if (strstr(map[mpt[i]], "telephone-event"))
			continue;
		if (!strcmp(map[mpt[i]], "PCMU"))
			chosen = mpt[i];
	}
	for (i = 0; i < nmpt && chosen < 0; i++) {
		if (!strncmp(map[mpt[i]], "AMR", 3))
			chosen = mpt[i];
	}
	for (i = 0; i < nmpt && chosen < 0; i++) {
		if (strstr(map[mpt[i]], "telephone-event"))
			continue;
		chosen = mpt[i];	// bilinmeyen tur — pass-through
	}
	if (chosen >= 0) {
		*pt = chosen;
		if (!strncmp(map[chosen], "AMR", 3))
			snprintf(codec, csize, "%s", octetalign ? "AMR" : "AMRBE");
		else
			snprintf(codec, csize, "%s", map[chosen]);
	}
}

//	TLV yaz: tag + uzunluk (kisa form, <256) + icerik; donen deger toplam uzunluk
static	int	etlv(unsigned char *out, int tag, const void *p, int plen) {
	int	n = 0;

	out[n++] = (unsigned char) tag;
	if (plen >= 128)
		out[n++] = 0x81;
	out[n++] = (unsigned char) plen;
	memcpy(out + n, p, plen);
	return n + plen;
}

//	ETSI HI2 IRI kaydi (.ehif) yaz — voida receiver ciktisi ile ayni bicim:
//	"handover-interface-stream,v1.1,ehif" NUL <uzunluk> <BER: SEQ{ [1]header, [2]IRI }>
static	uint32_t	ehif_seq = 0;

static	void	ehif_write(sipdialog *d, int iri_type) {
	unsigned char	liid[8], corr[8], seq3[3], endc[2] = { 0x80, 0x90 };
	unsigned char	idtlv[224], c0[224], sq[256], a0[288], a2[288];
	unsigned char	hdr[336], body[640], full[640];
	char		content[200];
	char	folder[256], op[48], neid[48], liids[48], prefix[32];
	char	ts1[32], ts2[32], caller[64], callee[64], fn[32], path[512], hb[3];
	struct	timeval	tv;
	struct	tm	tm;
	FILE	*f;
	uint64_t	h = 1469598103934665603ULL;
	int	i, n, m, hn, bn, ctlen, idtot, irilen, total;

	if (!esip_conf("esipserv.hi2.enable", 1))
		return;
	cson_getStringf(NULL, folder, "hi2", "esipserv.hi2.folder");
	cson_getStringf(NULL, op, "TurkTelekom", "esipserv.hi2.operator");
	cson_getStringf(NULL, neid, "esipserv", "esipserv.hi2.neid");
	cson_getStringf(NULL, liids, "0400020204030701", "esipserv.hi2.liid");
	cson_getStringf(NULL, prefix, "ESIP", "esipserv.hi2.prefix");

	//	liid: konfigurasyondaki hex; korelasyon: callid'den FNV-1a
	memset(liid, 0, sizeof(liid));
	for (i = 0; i < 8 && liids[i * 2] && liids[i * 2 + 1]; i++) {
		memcpy(hb, liids + i * 2, 2);
		hb[2] = 0;
		liid[i] = (unsigned char) strtoul(hb, NULL, 16);
	}
	for (i = 0; d->callid[i]; i++) {
		h ^= (unsigned char) d->callid[i];
		h *= 1099511628211ULL;
	}
	for (i = 0; i < 8; i++)
		corr[i] = (unsigned char) (h >> (56 - 8 * i));

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts1, sizeof(ts1), "%y%m%d%H%M%S", &tm);
	snprintf(ts1 + 12, sizeof(ts1) - 12, "%06ld", (long) tv.tv_usec);
	strftime(ts2, sizeof(ts2), "%Y%m%d%H%M%S", &tm);
	snprintf(ts2 + 14, sizeof(ts2) - 14, ".%d", (int) (tv.tv_usec / 100000));
	ehif_seq++;
	seq3[0] = (unsigned char) (ehif_seq >> 16);
	seq3[1] = (unsigned char) (ehif_seq >> 8);
	seq3[2] = (unsigned char) ehif_seq;

	//	tanimlama blogu: [3] { [0] { [0]operator [1]neid [2]{ [1]korelasyon } }, [1] 3B }
	{
		unsigned char	corrtlv[16], opneid[160], inner0[192];

		m = etlv(corrtlv, 0x81, corr, sizeof(corr));
		n = etlv(opneid, 0x80, op, strlen(op));
		n += etlv(opneid + n, 0x81, neid, strlen(neid));
		n += etlv(opneid + n, 0xA2, corrtlv, m);
		m = etlv(inner0, 0xA0, opneid, n);
		m += etlv(inner0 + m, 0x81, "\x0a\x84\xb6", 3);
		idtot = etlv(idtlv, 0xA3, inner0, m);
	}

	//	IRI blogu: [2] { [0] { SEQ { [0]tip [6]0 [2]{ [0]icerik } } } }
	uri_user(caller, sizeof(caller), d->from);
	uri_user(callee, sizeof(callee), d->to);
	ctlen = iri_type == 1
		? snprintf(content, sizeof(content), "caller: %s callee: %s", caller, callee)
		: (int) sizeof(endc);
	if (ctlen > (int) sizeof(content))
		ctlen = sizeof(content);
	n = etlv(c0, 0x80, iri_type == 1 ? (unsigned char *) content : endc, ctlen);
	n = etlv(sq, 0x80, &iri_type, 1);
	n += etlv(sq + n, 0x86, "\0", 1);
	n += etlv(sq + n, 0xA2, c0, ctlen + (ctlen >= 128 ? 3 : 2));
	n = etlv(a0, 0x30, sq, n);
	n = etlv(a2, 0xA0, a0, n);
	irilen = etlv(c0, 0xA2, a2, n);

	//	baslik blogu: [1] { [0]liid [1]ts1 [3]tanimlama [4]seq [5]ts2 [9]0 }
	hn = etlv(hdr, 0x80, liid, sizeof(liid));
	hn += etlv(hdr + hn, 0x81, ts1, 18);
	memcpy(hdr + hn, idtlv, idtot);
	hn += idtot;
	hn += etlv(hdr + hn, 0x84, seq3, 3);
	hn += etlv(hdr + hn, 0x85, ts2, strlen(ts2));
	hn += etlv(hdr + hn, 0x89, "\0", 1);

	//	tum kayit: SEQ { [1]baslik [2]IRI }
	bn = etlv(body, 0xA1, hdr, hn);
	memcpy(body + bn, c0, irilen);
	bn += irilen;
	total = etlv(full, 0x30, body, bn);
	if (total > 255) {
		LogPrint("HI2-IRI kayit cok buyuk (%d B), yazilmadi\r\n", total);
		return;
	}

	//	dosya: <folder>/<prefix>_<yyyyMMddHHmmssSSS>_<seq>.ehif
	mkdir(folder, 0755);
	strftime(fn, sizeof(fn), "%Y%m%d%H%M%S", &tm);
	snprintf(path, sizeof(path), "%s/%s_%s%03u%04u.ehif", folder, prefix, fn,
		(unsigned) (tv.tv_usec / 1000), ehif_seq & 0xffff);
	if ((f = fopen(path, "wb")) == NULL) {
		LogPrint("HI2-IRI dosya acilamadi: %s\r\n", path);
		return;
	}
	fwrite("handover-interface-stream,v1.1,ehif", 1, 35, f);
	fputc(0, f);
	fputc(total, f);
	fwrite(full, 1, total, f);
	fclose(f);
	LogPrint("HI2-IRI-%s file=%s corr=%02x%02x%02x%02x%02x%02x%02x%02x\r\n",
		iri_type == 1 ? "BEGIN" : "END", path,
		corr[0], corr[1], corr[2], corr[3], corr[4], corr[5], corr[6], corr[7]);
}

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

	//	X3/LI: isub korelasyon + karsi tarafin RTP adresi (multipart destekli)
	xisub(d->isub_to, sizeof(d->isub_to), m->to);
	xisub(d->isub_from, sizeof(d->isub_from), m->from);
	if (!d->isub_from[0])
		xisub(d->isub_from, sizeof(d->isub_from), m->pai);
	d->rtp_port = -1;
	d->hi3_pt = -1;
	d->nstr = 0;
	snprintf(d->hi3_codec, sizeof(d->hi3_codec), "RAW");
	{
		char	sdpb[1400];

		if (extract_sdp(m, sdpb, sizeof(sdpb)) > 0)
			sdp_rtp(sdpb, strlen(sdpb), d->rtp_ip, sizeof(d->rtp_ip),
				&d->rtp_port, &d->hi3_pt, d->hi3_codec, sizeof(d->hi3_codec));
	}
	ehif_write(d, 1);

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

	LogPrint("CALL-START callid=%s from=%s to=%s uri=%s rtp=%s:%d isub_to=%s isub_from=%s rtpport=%ld\r\n",
		m->callid, m->from, m->to, m->requri,
		d->rtp_ip, d->rtp_port, d->isub_to, d->isub_from,
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

//	RTP alici: karsi adresle dialog eslestir, basligi coz, HI3'e kaydet.
//	SDP'den codec gelmemisse paketin payload tipinden tespit edilir.
int	cbRTP(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData) {
	struct	sockaddr_in	*src;
	unsigned char	*p;
	int	plen, i, hdrlen, pt;
	uint16_t	seq;
	uint32_t	ts, ssrc;
	sipdialog	*d = NULL;

	if (cmd != EVENT_DATA)
		return 0;
	if (len <= (int) (sizeof(int) + sizeof(struct sockaddr_in)))
		return 0;
	src = (struct sockaddr_in *) (data + sizeof(int));
	p = (unsigned char *) (data + sizeof(int) + sizeof(struct sockaddr_in));
	plen = len - sizeof(int) - sizeof(struct sockaddr_in);
	if (src->sin_family != AF_INET || plen < 12)
		return 0;
	for (i = 0; i < ESIP_MAX_DIALOGS; i++) {
		if (!dialogs[i].inuse || dialogs[i].rtp_port <= 0)
			continue;
		if (dialogs[i].rtp_port != ntohs(src->sin_port))
			continue;
		if (strcmp(dialogs[i].rtp_ip, inet_ntoa(src->sin_addr)))
			continue;
		d = dialogs + i;
		break;
	}
	if (!d)
		return 0;
	d->rtp_pkts++;
	d->rtp_bytes += plen;

	//	RTP basligi: v=2, CC, X, M, PT, seq, ts, ssrc (+CSRC/extension)
	if ((p[0] >> 6) != 2)
		return 0;
	hdrlen = 12 + 4 * (p[0] & 0x0F);
	if (p[0] & 0x10) {					// extension var: uzunlugu oku
		if (hdrlen + 4 > plen)
			return 0;
		hdrlen += 4 * (((p[hdrlen + 2] << 8) | p[hdrlen + 3]) + 1);
	}
	if (hdrlen + 1 >= plen)
		return 0;
	pt = p[1] & 0x7F;
	seq = (uint16_t) ((p[2] << 8) | p[3]);
	ts = ((uint32_t) p[4] << 24) | ((uint32_t) p[5] << 16)
		| ((uint32_t) p[6] << 8) | p[7];
	ssrc = ((uint32_t) p[8] << 24) | ((uint32_t) p[9] << 16)
		| ((uint32_t) p[10] << 8) | p[11];

	//	kayip takibi (ilk paket haric seq ardisik olmali)
	//	codec yoksa (SDP'den) statik tipe bakarak benimse
	if (d->hi3_pt < 0) {
		if (pt == 0) { d->hi3_pt = pt; snprintf(d->hi3_codec, 16, "PCMU"); }
		else if (pt == 8) { d->hi3_pt = pt; snprintf(d->hi3_codec, 16, "PCMA"); }
		else if (pt != 101)
			{ d->hi3_pt = pt; snprintf(d->hi3_codec, 16, "RAW"); }
	}
	if (pt != d->hi3_pt)			// telephone-event vb. diger tipler sessiz
		return 0;

	//	SSRC bazli akis (taraf) demux: yeni SSRC yeni taraf
	{
		histream	*st = NULL;
		int		i;

		for (i = 0; i < d->nstr; i++) {
			if (d->str[i].ssrc == ssrc) {
				st = d->str + i;
				break;
			}
		}
		if (!st) {
			if (d->nstr >= 4)
				return 0;
			st = d->str + d->nstr;
			bzero(st, sizeof(histream));
			st->ssrc = ssrc;
			st->party = 'a' + d->nstr;
			d->nstr++;
		}
		if (st->lastseq && seq != (uint16_t) (st->lastseq + 1))
			st->lost += (uint16_t) (seq - st->lastseq - 1);
		st->lastseq = seq;

		hi3_write(d, st, ts, seq, p + hdrlen, plen - hdrlen);
	}
	return 0;
}

//	G.711: canli akis icin A-law -> u-law donusumu (audio/basic, /live)
static	short	alaw2linear(unsigned char a_val) {
	int	t, seg;

	a_val ^= 0x55;
	t = (a_val & 0x0F) << 4;
	seg = ((unsigned) a_val & 0x70) >> 4;
	switch (seg) {
	case 0: t += 8; break;
	case 1: t += 0x108; break;
	default: t += 0x108; t <<= seg - 1;
	}
	return (a_val & 0x80) ? t : -t;
}

static	unsigned char	linear2ulaw(short pcm_val) {
	short		mask, seg;
	unsigned char	uval;

	pcm_val = pcm_val >> 2;
	if (pcm_val < 0) {
		pcm_val = -pcm_val;
		mask = 0x7F;
	} else
		mask = 0xFF;
	if (pcm_val > 8159)
		pcm_val = 8159;
	pcm_val += (0x84 >> 2);
	seg = 0;
	while (pcm_val > 0x3F) {
		pcm_val >>= 1;
		seg++;
	}
	uval = (unsigned char) ((seg << 4) | ((pcm_val >> 1) & 0x0F));
	return uval ^ mask;
}

static	unsigned char	alaw2ulaw(unsigned char a) {
	return linear2ulaw(alaw2linear(a));
}

//	non-blocking fd'ye gonder; EAGAIN'de kisa bekle, hata olursa -1
static	int	web_send(HandleEntry *he, char *b, int n) {
	int	off = 0;

	while (off < n) {
		int	w = (int) write(he->fhout, b + off, n - off);

		if (w <= 0) {
			if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				usleep(2000);
				continue;
			}
			return -1;
		}
		off += w;
	}
	return off;
}

static	void	web_resp(HandleEntry *he, int code, char *status, char *ctype,
		char *extra, char *body, int blen) {
	char	h[512];
	int	hn = snprintf(h, sizeof(h),
		"HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
		"Connection: close\r\nCache-Control: no-store\r\n%s\r\n",
		code, status, ctype, blen, extra ? extra : "");

	if (web_send(he, h, hn) < 0)
		return;
	if (body && blen > 0)
		web_send(he, body, blen);
}

//	==================== WebSocket canli yayin (/ws/<callid>) ====================
//	Protokol: 101 sonrasi TEXT {"codec":...} ; binary cerceve = [taraf][ses];
//	periyodik TEXT {"rms":[a,b],"pkts":n,"lost":n} duzey grafigi icin.

#define	WS_MAGIC	"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define	ESIP_MAX_WS	16

typedef	struct wsclient_st {
	HandleEntry	*he;
	char		callid[128];
} wsclient;

static	wsclient	wsclients[ESIP_MAX_WS];
static	HandleList	*g_hl = NULL;

static	wsclient	*ws_find(HandleEntry *he) {
	int	i;
	for (i = 0; i < ESIP_MAX_WS; i++)
		if (wsclients[i].he == he)
			return wsclients + i;
	return NULL;
}

static	void	ws_deregister(HandleEntry *he) {
	wsclient	*w = ws_find(he);
	if (w) {
		LogPrint("WS-CLOSE %s\r\n", w->callid);
		w->he = NULL;
	}
}

//	tek parca (maskesiz, sunucu) WS cercevesi gonder — dogrudan write
static	int	ws_frame(HandleEntry *he, int opcode, unsigned char *p, int plen) {
	unsigned char	f[8];
	int		fl = 0;

	f[fl++] = 0x80 | (unsigned char) opcode;
	if (plen < 126) {
		f[fl++] = (unsigned char) plen;
	} else {
		f[fl++] = 126;
		f[fl++] = (unsigned char) (plen >> 8);
		f[fl++] = (unsigned char) (plen & 0xFF);
	}
	if (web_send(he, (char *) f, fl) < 0) {
		LogPrint("WS-SEND-FAIL hdr opcode=%d errno=%d\r\n", opcode, errno);
		return -1;
	}
	if (plen > 0 && web_send(he, (char *) p, plen) < 0) {
		LogPrint("WS-SEND-FAIL payload opcode=%d errno=%d\r\n", opcode, errno);
		return -1;
	}
	return 0;
}

//	Upgrade istegini yanitla ve istemciyi kaydet; codec duyurusu gonder
static	void	ws_accept(HandleEntry *he, char *data, char *callid) {
	char	key[64], accept[64], resp[512];
	unsigned char	sha[20], hello[128];
	char	*p, *e;
	int	n;
	sha1nfo	s;

	if ((p = strcasestr(data, "Sec-WebSocket-Key:")) == NULL)
		return;
	p += 18;
	while (*p == ' ')
		p++;
	for (e = p; *e && *e != '\r' && *e != '\n' && (e - p) < (int) sizeof(key) - 1; e++);
	memcpy(key, p, e - p);
	key[e - p] = 0;
	sha1_init(&s);
	sha1_write(&s, key, strlen(key));
	sha1_write(&s, WS_MAGIC, strlen(WS_MAGIC));
	memcpy(sha, sha1_result(&s), 20);
	Base64encode(accept, (char *) sha, 20);
	n = snprintf(resp, sizeof(resp),
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n\r\n", accept);
	if (web_send(he, resp, n) < 0)
		return;
	{
		wsclient	*w = NULL;
		int		i;

		for (i = 0; i < ESIP_MAX_WS; i++)
			if (!wsclients[i].he) { w = wsclients + i; break; }
		if (!w)
			return;
		w->he = he;
		sncpy(w->callid, sizeof(w->callid), callid);
		LogPrint("WS-OPEN %s from %s\r\n", callid, he->IpAddress);
	}
	n = snprintf((char *) hello, sizeof(hello) - 1,
		"{\"codec\":\"%s\"}", "RAW");
	//	codec cagrilara ozel: hello'yu ws_push tarafinda doldurmak yerine
	//	burada diyalogdan alalim
	{
		sipdialog	*d = esip_find(callid);

		if (d)
			n = snprintf((char *) hello, sizeof(hello) - 1,
				"{\"codec\":\"%s\",\"parties\":%d}",
				d->hi3_codec, d->nstr);
	}
	if (ws_frame(he, 0x1, hello, n) < 0)
		LogPrint("WS-HELLO-FAIL len=%d\r\n", n);
	else
		LogPrint("WS-HELLO-SENT len=%d\r\n", n);
}

//	RTP paketi geldikce: [taraf bayti + ses] binary cerceve (native codec,
//	donusum YOK — cozumleme tarayicida JS ile yapilir)
static	void	ws_push(sipdialog *d, histream *st, unsigned char *payload, int plen) {
	unsigned char	buf[300];
	int	i;

	if (!g_hl)
		return;
	if (strcmp(d->hi3_codec, "PCMA") && strcmp(d->hi3_codec, "PCMU"))
		return;				// canli yayin yalniz G.711
	if (plen > 250)
		plen = 250;
	buf[0] = st->party;
	memcpy(buf + 1, payload, plen);
	for (i = 0; i < ESIP_MAX_WS; i++) {
		if (!wsclients[i].he || strcmp(wsclients[i].callid, d->callid))
			continue;
		ws_frame(wsclients[i].he, 0x2, buf, plen + 1);
	}
}

//	duzey/durum duyurusu (RMS grafigi): her 25 pakette bir
static	void	ws_status(sipdialog *d) {
	char	j[256];
	int	n, i;

	if (!g_hl)
		return;
	n = snprintf(j, sizeof(j),
		"{\"rms\":[%.0f,%.0f,%.0f,%.0f],\"pkts\":%u,\"lost\":%u}",
		d->nstr > 0 ? d->str[0].rms : 0.0, d->nstr > 1 ? d->str[1].rms : 0.0,
		d->nstr > 2 ? d->str[2].rms : 0.0, d->nstr > 3 ? d->str[3].rms : 0.0,
		d->rtp_pkts, d->nstr > 0 ? d->str[0].lost : 0);
	for (i = 0; i < ESIP_MAX_WS; i++) {
		if (!wsclients[i].he || strcmp(wsclients[i].callid, d->callid))
			continue;
		ws_frame(wsclients[i].he, 0x1, (unsigned char *) j, n);
	}
}

static	char	*web_index =
	"<!doctype html><html><head><meta charset='utf-8'><title>esipserv HI3</title></head>"
	"<body style='font-family:monospace;background:#111;color:#9f9'>"
	"<h3>esipserv &mdash; live intercepts</h3>"
	"<div id='now' style='color:#cfc'></div>"
	"<canvas id='rms' width='600' height='80' style='background:#000'></canvas><br>"
	"A vol <input id='va' type='range' min='0' max='200' value='100' oninput='setvol(0,this.value)'> "
	"B vol <input id='vb' type='range' min='0' max='200' value='100' oninput='setvol(1,this.value)'>"
	"<table id='t' border='1' cellpadding='4' style='border-collapse:collapse;margin-top:8px'></table>"
	"<script>\n"
	"var cur=null,off=0,ctx=null,nt=0,timer=null,ws=null,codec='PCMU',gains=[],hist=[];"
	"function udec(u){u=~u&255;var t=((u&15)<<3)+132;t<<=(u&112)>>4;return(u&128)?132-t:t-132;}\n"
	"function adec(a){a^=0x55;var t=(a&15)<<4,s=(a&112)>>4;"
	"var v=s==0?t+8:s==1?t+0x108:(t+0x108)<<(s-1);return a&0x80?v:-v;}\n"
	"function dec(b){return codec=='PCMA'?adec(b):udec(b);}\n"
	"function mkbuf(b){var f=new Float32Array(b.length);"
	"for(var i=0;i<b.length;i++)f[i]=dec(b[i])/32768;"
	"var s=ctx.createBuffer(1,f.length,8000);s.getChannelData(0).set(f);return s;}\n"
	"function sched(b,party){if(!gains[party]){gains[party]=ctx.createGain();"
	"gains[party].connect(ctx.destination);}"
	"var src=ctx.createBufferSource();src.buffer=mkbuf(b);"
	"src.connect(gains[party]);if(nt<ctx.currentTime)nt=ctx.currentTime+0.05;"
	"src.start(nt);nt+=b.length/8000;}\n"
	"function setvol(i,v){if(gains[i])gains[i].gain.value=v/100;}\n"
	"function draw(r){hist.push(r);if(hist.length>300)hist.shift();"
	"var c=document.getElementById('rms').getContext('2d');"
	"c.fillStyle='#000';c.fillRect(0,0,600,80);"
	"for(var x=0;x<hist.length;x++){var h;"
	"c.fillStyle='#3c3';h=Math.min(80,hist[x][0]/400);c.fillRect(x*2,80-h,2,h);"
	"c.fillStyle='#3cc';h=Math.min(80,hist[x][1]/400);c.fillRect(x*2+1,80-h,1,h);}}\n"
	"async function tick(){if(!cur)return;try{"
	"var r=await fetch('/live/'+encodeURIComponent(cur)+'?off='+off);"
	"if(r.ok){off=+(r.getHeader('X-Next-Offset')||off);"
	"var b=new Uint8Array(await r.arrayBuffer());if(b.length)sched(b,0);}}catch(e){}}\n"
	"function wsinit(){try{"
	"ws=new WebSocket((location.protocol=='https:'?'wss://':'ws://')+location.host+'/ws/'+encodeURIComponent(cur));"
	"ws.binaryType='arraybuffer';"
	"ws.onopen=function(){if(timer){clearInterval(timer);timer=null;}};"
	"ws.onmessage=function(ev){"
	"if(typeof ev.data=='string'){var j;try{j=JSON.parse(ev.data);}catch(e){return;}"
	"if(j.codec)codec=j.codec;if(j.rms)draw(j.rms);return;}"
	"var b=new Uint8Array(ev.data);if(b.length>1)sched(b.subarray(1),b[0]-97);};"
	"ws.onclose=ws.onerror=function(){ws=null;if(cur&&!timer)timer=setInterval(tick,250);};"
	"}catch(e){ws=null;}}\n"
	"function play(id){if(timer)clearInterval(timer);timer=null;if(ws){try{ws.close();}catch(e){}ws=null;}"
	"cur=id;off=0;nt=0;gains=[];hist=[];"
	"ctx=ctx||new(window.AudioContext||window.webkitAudioContext)();"
	"if(ctx.resume)ctx.resume();"
	"wsinit();if(!ws)timer=setInterval(tick,250);"
	"document.getElementById('now').textContent='LIVE: '+id;}\n"
	"function stop(){if(timer)clearInterval(timer);timer=null;if(ws){try{ws.close();}catch(e){}ws=null;}"
	"cur=null;document.getElementById('now').textContent='stopped';}\n"
	"async function refresh(){try{var r=await fetch('/calls');var j=await r.json();"
	"var h='<tr><th>callid</th><th>codec</th><th>parties</th><th>pkts</th><th>lost</th><th>dur</th><th></th></tr>';"
	"for(var i=0;i<j.length;i++){var c=j[i];"
	"h+='<tr><td>'+c.callid+'</td><td>'+c.codec+'</td><td>'+c.parties+'</td><td>'+c.pkts+'</td><td>'+c.lost+'</td><td>'+c.dur+'</td>';"
	"h+='<td>'+(c.live?'<button onclick=\"play(\\''+c.callid+'\\')\">LIVE</button> <button onclick=\"stop()\">stop</button> ':'');"
	"h+=\"<a href='/dl/\"+encodeURIComponent(c.callid)+\"'>dl</a></td></tr>\";}"
	"document.getElementById('t').innerHTML=h;}catch(e){}}\n"
	"setInterval(refresh,2000);refresh();"
	"</script></body></html>";

//	aktif cagrilarin JSON listesi (taraflar + RMS ile)
static	void	web_calls(HandleEntry *he) {
	static	char	json[16384];
	int	n = 0, i, live;

	n += snprintf(json + n, sizeof(json) - n, "[");
	for (i = 0; i < ESIP_MAX_DIALOGS && n < (int) sizeof(json) - 512; i++) {
		if (!dialogs[i].inuse)
			continue;
		live = dialogs[i].hi3_fp != NULL
			&& (!strcmp(dialogs[i].hi3_codec, "PCMA")
			||  !strcmp(dialogs[i].hi3_codec, "PCMU"));
		n += snprintf(json + n, sizeof(json) - n,
			"%s{\"callid\":\"%s\",\"codec\":\"%s\",\"parties\":%d,"
			"\"pkts\":%u,\"bytes\":%u,\"lost\":%u,\"dur\":%ld,"
			"\"rms\":[%.0f,%.0f],\"live\":%s}",
			n > 1 ? "," : "", dialogs[i].callid,
			dialogs[i].hi3_codec, dialogs[i].nstr,
			dialogs[i].rtp_pkts, dialogs[i].rtp_bytes,
			dialogs[i].nstr ? dialogs[i].str[0].lost : 0,
			(long) (time(NULL) - dialogs[i].tstart),
			dialogs[i].nstr > 0 ? dialogs[i].str[0].rms : 0.0,
			dialogs[i].nstr > 1 ? dialogs[i].str[1].rms : 0.0,
			live ? "true" : "false");
	}
	snprintf(json + n, sizeof(json) - n, "]");
	web_resp(he, 200, "OK", "application/json", NULL, json, strlen(json));
}

//	cagri 'a' akisindan canli akis (polling yedek): PCMU dogrudan, PCMA -> u-law
static	void	web_live(HandleEntry *he, char *callid, long off) {
	sipdialog	*d = esip_find(callid);
	unsigned char	buf[16384];
	FILE		*f;
	char		extra[128];
	int		n, i;

	if (!d || d->nstr < 1 || !d->str[0].fp
	|| (strcmp(d->hi3_codec, "PCMA") && strcmp(d->hi3_codec, "PCMU"))) {
		web_resp(he, 415, "Unsupported", "text/plain", NULL,
			"live: G.711 calls only", 20);
		return;
	}
	fflush(d->str[0].fp);
	if ((f = fopen(d->str[0].file, "rb")) == NULL) {
		web_resp(he, 404, "Not Found", "text/plain", NULL, "no file", 8);
		return;
	}
	if (off < 0)
		off = 0;
	fseek(f, 24 + off, SEEK_SET);			// .au basligi 24 bayt
	n = (int) fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (!strcmp(d->hi3_codec, "PCMA"))
		for (i = 0; i < n; i++)
			buf[i] = alaw2ulaw(buf[i]);
	snprintf(extra, sizeof(extra), "X-Next-Offset: %ld\r\n", off + n);
	web_resp(he, 200, "OK", "audio/basic", extra, (char *) buf, n);
}

//	kayit dosyasinin tamami (indirme) — 'a' akisi
static	void	web_dl(HandleEntry *he, char *callid) {
	sipdialog	*d = esip_find(callid);
	char		buf[16384], extra[384], *fname, *ct = "application/octet-stream";
	FILE		*f;
	int		n;

	if (!d || d->nstr < 1 || !d->str[0].file) {
		web_resp(he, 404, "Not Found", "text/plain", NULL, "no file", 8);
		return;
	}
	if (d->str[0].fp)
		fflush(d->str[0].fp);
	if ((f = fopen(d->str[0].file, "rb")) == NULL) {
		web_resp(he, 404, "Not Found", "text/plain", NULL, "no file", 8);
		return;
	}
	if ((fname = strrchr(d->str[0].file, '/')) == NULL)
		fname = d->str[0].file;
	if (strstr(fname, ".au"))
		ct = "audio/basic";
	else if (strstr(fname, ".amr"))
		ct = "audio/AMR";
	n = (int) fread(buf, 1, sizeof(buf), f);
	fclose(f);
	snprintf(extra, sizeof(extra),
		"Content-Disposition: attachment; filename=\"%s\"\r\n", fname + 1);
	web_resp(he, 200, "OK", ct, extra, buf, n);
}

//	minik HTTP + WS sunucusu: GET / | /calls | /live/<id>?off=N | /dl/<id> |
//	GET /ws/<id> + Upgrade: websocket

//	yol parametresinde %XX cozumu (callid'ler encodeURIComponent ile gelir)
static	void	url_decode(char *s) {
	char	*r = s;
	int	hi, lo;

	while (*s) {
		if (*s == '%' && s[1] && s[2]
		&&  (hi = strchr("0123456789abcdefABCDEF", s[1]) ? 1 : 0)
		&&  (lo = strchr("0123456789abcdefABCDEF", s[2]) ? 1 : 0)) {
			char	hb[2] = { s[1], 0 }, lb[2] = { s[2], 0 };
			*r++ = (unsigned char) (strtoul(hb, NULL, 16) * 16 + strtoul(lb, NULL, 16));
			s += 3;
		} else if (*s == '+') {
			*r++ = ' ';
			s++;
		} else
			*r++ = *s++;
	}
	*r = 0;
}

int	cbWeb(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData) {
	char	method[8], path[300], arg[300], *q, *offp;
	long	off = 0;

	if (!g_hl)
		g_hl = hl;
	switch (cmd) {
	case EVENT_CONNECT:
		AcceptTCPClient(hl, he, "web");
		break;
	case EVENT_FREE:
		ws_deregister(he);
		break;
	case EVENT_DATA:
		if (len <= 0 || len > 4000)
			return 1;
		//	kayitli WS istemcisi: cerceve (ping/pong/close) isleme
		if (ws_find(he)) {
			int	op = ((unsigned char) data[0]) & 0x0F;

			if (op == 0x8) {		// close
				ws_deregister(he);
				return 1;
			}
			if (op == 0x9)			// ping -> pong
				ws_frame(he, 0xA, (unsigned char *) (data + 2), 0);
			return 0;
		}
		data[len] = 0;
		if (sscanf(data, "%7s %299s", method, path) != 2
		||  strcmp(method, "GET")) {
			web_resp(he, 405, "Method Not Allowed", "text/plain", NULL, "GET only", 8);
			return 1;
		}
		if ((q = strchr(path, '?')) != NULL) {
			*q = 0;
			if ((offp = strstr(q + 1, "off=")) != NULL)
				off = atol(offp + 4);
		}
		if (!strcmp(path, "/"))
			web_resp(he, 200, "OK", "text/html", NULL,
				web_index, strlen(web_index));
		else if (!strcmp(path, "/calls"))
			web_calls(he);
		else if (!strncmp(path, "/live/", 6)) {
			snprintf(arg, sizeof(arg), "%s", path + 6);
			url_decode(arg);
			web_live(he, arg, off);
		} else if (!strncmp(path, "/dl/", 4)) {
			snprintf(arg, sizeof(arg), "%s", path + 4);
			url_decode(arg);
			web_dl(he, arg);
		} else if (!strncmp(path, "/ws/", 4)
		&&  strcasestr(data, "Upgrade: websocket")) {
			snprintf(arg, sizeof(arg), "%s", path + 4);
			url_decode(arg);
			ws_accept(he, data, arg);
			return 0;			// baglanti acik kalir
		}
		else
			web_resp(he, 404, "Not Found", "text/plain", NULL, "not found", 8);
		return 1;				// HTTP/1.0: yanit sonrasi kapat
	default:
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
