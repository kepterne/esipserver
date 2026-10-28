/*
	Copyright (C) 2015+ Ekrem Karacan
	ekrem.karacan@gmail.com
*/
#ifndef utils_h
#define	utils_h

/*
#if ( __WORDSIZE == 64 )
#define BUILD_64   1
#endif
*/
#if UINTPTR_MAX == 0xffffffffffffffffULL
# define BUILD_64   1
#endif

#include	<netinet/in.h>
#include	<sys/time.h>
#include	<time.h>
#include	<stdint.h>
#include	<signal.h>
#include	<semaphore.h>
#include	<pthread.h>
#include	<sys/signalfd.h>
#include	"raspberry_pi_revision.h"


typedef	uint8_t	BYTE;
typedef	uint16_t	WORD;
typedef	uint32_t	DWORD;
typedef 	int32_t	int32;
typedef	uint64_t	QWORD;

#define	diff(a, b)	((a) >= (b) || (a) <= -(b))

#include	"version.h"

// { filesdirsnames.c
char    *timetohttptime(time_t *tt, char *d);
char	*GetFileExt(char *p, char *ext);
char	*GetProgramName(char *p, char *ProgramName);
char	*GetProgramPath(char *p, char *ProgramPath);
int		DirExists(char *path);
int		CreateDir(char *p);
int		CreateDirOf(char *p);

// } filesdirsnames.c

// { params.c
typedef	struct {
	char	paramkey[128];
	char	readfmt[16];
	char	*paramptr;
	char	oldvalue[256];
} cmdline_param;
int	ParseParameters(int argc, char **argv, cmdline_param *clp);
int	ParseAyarParam( cmdline_param *clp, char *name, char *value);
int	ParseParametersFromFile(char *FName, cmdline_param *clp);
int	SaveParameters(char *FName, cmdline_param *clp);
int	PrintParameters(cmdline_param *clp);
// } params.c

// { log.c
void	LogInit(void);
void	LogClose(void);
void	LogPush(char *s);
void	LogPop(void);
void	LogPutPrefix(char *p);
void	LogPrint(char *Str, ...);
// } log.c

// { llist.c
typedef struct st_llist_entry {
	struct st_llist_entry *n, *p;
	int	ix, filla;
} llist_entry;
typedef	struct st_llist_hdr {
	struct st_llist_entry *f, *l, *ff, *lf;
	int	fc, c, n;
} llist_hdr;
llist_entry	*get_entry(llist_hdr *hdr);
int		del_entry(llist_hdr *hdr, llist_entry *he);
llist_entry *init_llist(int n, llist_hdr *hdr, int size);
// } llist.c

// { writepool.c
struct stHandleList;
struct stHandleEntry;
#define	WRITE_POOL_GRANULARITY	1480
#define	WPADD_STATIC_DATA		1
#define	WPADD_DYNAMIC_DATA	2
#define	WPADD_FILE_DATA		3

typedef	struct {
	char		file_name[1024];
	uint32_t	file_size, posstart, posend;
} send_file_data;
typedef	struct e_writepool_entry_st {
	llist_entry	le;
	struct		stHandleEntry	*owner;
	struct		e_writepool_entry_st	*next, *prev;
	int			type;
	DWORD		ofs;
	DWORD		len;
	DWORD		ttl;
	BYTE		*dp;
	send_file_data	sf;
	BYTE		data[WRITE_POOL_GRANULARITY + 16];
} e_writepool_entry;

typedef	struct {
	llist_hdr	lh;
	llist_entry	*entries;
} e_writepool;

typedef	struct {
	DWORD		TotalWrite, TotalSent;
	e_writepool_entry	*first;
	e_writepool_entry	*last;
} e_writepool_hdr;
// } writepool.c

// { handles.c
typedef int (*timerhandler_t)(void *p, int delta, struct timeval tv);
typedef	struct stHandleEntry {
	llist_entry	le;
	DWORD	__idx;
	struct	sockaddr_in	SockAddr;
	int	pollfd;
	e_writepool_hdr		wph;
	char	IpAddress[64];
	char	RemoteAddress[64];
	WORD	rPort;
	DWORD	rIP;
	uint8_t	rIP6[16];
	WORD	Port;
	char	name[512];
	int	Connecting;
	int	Writable;
	int	inuse;
	int	fhin, fhout, pid;
	int	handletype;
	uint64_t	TimeoutValue;
	uint64_t	Timeout;
	uint64_t	EventTimeoutValue;
	uint64_t	EventTimeout;
#define	SERVER_HANDLE		0
#define	CLIENT_HANDLE		1
#define	PROCESS_HANDLE		2
#define	COM_HANDLE			3
#define	UDP_HANDLE			4
#define	TS_HANDLE			5
#define	FILE_HANDLE			6
#define	DEV_HANDLE			10
#define	GPS_HANDLE			11
#define	RAW_HANDLE			12
#define	ETH_HANDLE			13
#define	SIGNAL_HANDLE		14
#define	SSL_HANDLE			15
#define	SSL_SERVER_HANDLE		16
#define	UDP_HANDLE_TOMEM		24
#define	DRM_HANDLE			25

	struct	epoll_event	*pf;
	int	(*CallBack)(struct stHandleList *, struct stHandleEntry *he, int, char *, int, char *);
	char	*ExtraData;
	char	*ExtraData2;
	int	writecnt;
	struct	stHandleEntry	*Parent;
	int	(*writecb)(struct stHandleEntry *, const struct iovec *, int );
	DWORD	wait_queue, ByteLimit;
	QWORD TotalBytes;
#define	EVENT_CONNECT	0
#define	EVENT_CLOSE		1
#define	EVENT_ERROR		2
#define	EVENT_TIMEOUT	3
#define	EVENT_TIMER		4
#define	EVENT_FREE		5
#define	EVENT_DATA		6
#define	EVENT_RUN		7
#define	EVENT_CREATE	8
#define	EVENT_EXECUTE	16
#define	EVENT_WRITE		32
#define	EVENT_GPS		64
#define	EVENT_RAW		128
#define	EVENT_DNS		256
#define	EVENT_RESOLVED	512
#define	EVENT_RESOLVEDONE	1024
#define	EVENT_DATA_TOMEM	2048
#define	EVENT_DRM		4096
} HandleEntry;

typedef	int	(*e_CallBack)(struct stHandleList *, struct stHandleEntry *he, int, char *, int, char *);

typedef	struct stHandleList {
	llist_hdr	lh;
	unsigned long	totalcount;
	int		MaxHandles;
	e_writepool	WP;
	int		startidx;
	int		TimerInterval;
	timerhandler_t	TimerEvent;
	int		pollfd;
	int		pollcount;
	int		MaxWPEntries;
	HandleEntry	*Handles;
	struct	epoll_event	*polls;
} HandleList;

HandleList	*InitHandles(int MaxHandles, int MaxWPEntries, int timerinterval, timerhandler_t th);
void		CloseHandle(HandleList *hl, HandleEntry *he);
void		CloseHandles(HandleList *hl);
HandleEntry	*GetHandle(HandleList *hl);
void	PollHandles(HandleList *hl);
int		HandleWrite(HandleList *hl, HandleEntry *he, char *buf, int a);
int		HandlePrint(HandleList *hl, HandleEntry *he, char *fmt, ...);
int		ClrWaitToWrite(HandleList *hl, HandleEntry *he);
int		WaitToWrite(HandleList *hl, HandleEntry *he);
int		WaitToRead(HandleList *hl, HandleEntry *he);
int		ClrWaitToRead(HandleList *hl, HandleEntry *he);
// } handles.c

// { writepool.c
int	WPWrite(HandleList *hl, int type, HandleEntry *he, char *data, int len);
int	WPDoWrite(HandleList *hl, HandleEntry *he);
// } writepool.c

// { init.c
int	LinMain(int argc, char **argv, int (*frun)(int, char **, int), int (*fcleanup)(int), timerhandler_t timer, cmdline_param *params);
// } init.c
#ifdef	utils_c
int	(*DoHandleSignal)(int, struct signalfd_siginfo *) = NULL;
#else
extern	int	(*DoHandleSignal)(int, struct signalfd_siginfo *);
#endif

typedef	struct {
	struct	tm		last_tm; // = {0, 0, 0, 0, 0, 0};
	struct	tm		cur_tm;
	int				IsDaemon; // = 0;
	volatile sig_atomic_t	DoStop; // = 0;
	int				SignalId;
	time_t			Now;
	suseconds_t			uNow;
	struct			timeval	tv;
	char				ProgramName[128];  // = "";
	char				AppName[128];
	char				ProgramPath[256];
	char				ParamFileName[1024]; // = "";
	char				CompleteVersion[64]; // = "";
	int				MaxHandles;
	int				Interval;
	int				WritePoolSize;
	int				UpdateStatus;
	char				MAC[32]; // = "";
	char				CPUID[32];
	int				nProcessors;
	DWORD				downtotal, downcount;
	char				downtable[64];
	HandleList			*Handles;
	RASPBERRY_PI_INFO_T
					RPI_Info;
	int				NoSyslog;
	int				RunAsRoot;
	int				NoFancy;
	uint64_t			StartTime;
	uint64_t			Seconds;
	uint64_t			ts_start, ts_current;
	uint64_t			UptimeAtStart;
	uint16_t			isupdated;
} eGlobals;

#ifdef	main_c
	eGlobals	Sys;
	HandleList	*Handles = NULL;
	int		_led = 0;
	int		_blink = 0;
	int		_blinkc = 0;
	int		gIsFirstRun = 1;
#else
	extern	eGlobals	Sys;
	extern	struct	tm	__olt;
	extern	struct	tm	__lt;
	extern	HandleList	*Handles;

	extern	int		gIsFirstRun;
#endif


#define MARKERCOLOR	RGB16(0x20, 0xF0, 0x20)
//#define	BORDERCOLOR	RGB16(0xC0, 0xff, 0xFF)
//#define	BORDERCOLOR	RGB16(0x60, 0x20, 0x20)

#define	BORDERCOLOR	RGB16(0x10, 0x20, 0x10)
//#define	HEADERFONTCOLOR	RGB16(0, 0, 0x80)
//#define	HEADERCOLOR	RGB16(0xFF, 0xFF, 0xc0)
#define	HEADERCOLOR	RGB16(0, 0, 0)
#define	HEADERFONTCOLOR	RGB16(0xFF, 0xFF, 0xFF)
void	SendSSIn(void);
void	SendSSOut(void);
#define	MAX(a, b)	((a) > (b) ? (a) : b)
#define	MIN(a, b)	((a) < (b) ? (a) : b)


typedef	struct {
	int		(*cb)(HandleList *, HandleEntry *, int, char *, int, char *);
	char		*command;
	char		**params;
	char		commandline[512];
	char		l[4 * 1024];
	int		p;
	int		parcnt;
	int		qcnt;
	int		state;
	int		ncnt, zcnt;
	int		max;
	int		zline;
} _processlinecontext;
int     _ProcessLineCB(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);
char	*GetJSONTok(char *l, char **name, char **value);
int	GetMACAddress(char *intf, char *hwaddr);
int	GetDevIP(char *dev, char *ip);
int	GetDevIPRaw(char *dev, char *ip);
int GetDevIPv6Raw(char *dev, char *ip);

#define HASH_LENGTH 20
#define BLOCK_LENGTH 64

union _buffer {
	uint8_t b[BLOCK_LENGTH];
	uint32_t w[BLOCK_LENGTH/4];
};

union _state {
	uint8_t b[HASH_LENGTH];
	uint32_t w[HASH_LENGTH/4];
};

typedef struct sha1nfo {
	union _buffer buffer;
	uint8_t bufferOffset;
	union _state state;
	uint32_t byteCount;
	uint8_t keyBuffer[BLOCK_LENGTH];
	uint8_t innerHash[HASH_LENGTH];
} sha1nfo;

void sha1_init(sha1nfo *s);
/**
 */
void sha1_writebyte(sha1nfo *s, uint8_t data);
/**
 */
void sha1_write(sha1nfo *s, const char *data, size_t len);
/**
 */
uint8_t* sha1_result(sha1nfo *s);
/**
 */
void sha1_initHmac(sha1nfo *s, const uint8_t* key, int keyLength);
/**
 */
uint8_t* sha1_resultHmac(sha1nfo *s);


int Base64encode(char *encoded, const char *string, int len);
int Base64encode_len(int len);
int Base64decode(char *bufplain, const char *bufcoded);
int Base64decode_len(const char *bufcoded);

char *repl_str(const char *str, const char *old, const char *new);
int	InterfaceStatus(char *dev, char *ip, char *operstate, int *carrier);
int	CheckNet(char *dev, char *ip);
double	CheckTemp(void);
int	detect_gdb(void);

#ifndef	utils_c
	extern	int	ignoreChild;
	extern	pthread_mutex_t	__ssync;
	extern	int	underDebugger;
	extern	int	gIsDaemon;
#else
	int	ignoreChild;
	int	underDebugger = 0;
	pthread_mutex_t		__ssync;
	int	gIsDaemon = 0;
#endif

extern	char	*days[7];
extern	char	*months[12];

void	GetCPUID(char *CPUID, int *processors);
void	GetVZID(char *p);

#define STR_VALUE(val) #val
#define STR(name) STR_VALUE(name)

#define PATH_LEN 256
#define MD5_LEN 32

int CalcFileMD5(char *file_name, char *md5_sum);

void DelMyPid(void);
int GetMyOldPid(void);
int SaveMyPid(int pid);

void	health(void);
int	WPPrintf(HandleList *hl, HandleEntry *he, char *fmt, ...);
char *GetNameValue(char *p, char *namep, char *valp);
uint64_t	current_ts(void);
uint64_t	current_time64(void);

char *GetSplitValue(char *p, char *part, char c);
uint64_t	getu64(char *p);

int GetDevIPv6(char *dev, char *ip);
int     GetRawMAC(char *intf, char *hwaddr, char *hwaddr_raw);

#endif
