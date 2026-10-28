#ifndef	cson_h
#define	cson_h

#ifdef __cplusplus
extern	"C" {
#endif
#include	<time.h>
//#define	MAX_SHARED_MEMORY	(1024 * 1024 * 128)
#define	MAX_SHARED_MEMORY	(1024 * 1024 * 32)

#ifndef	LIBVER
	#define	LIBVER	"0.0.0"
#endif

#include	"cobj.h"

typedef	struct {
	char				version[128];
	int		    		csonlock;
	uint64_t			changed;
	pthread_mutex_t		mut;
	int		    		ownerid;
	int		    		totalsize;
	int		    		maxpairs;
	char				locker[128];
	cobj				root[1];
} shm_storage;

void	cson_sync(void);
void	cson_syncreset(char *key);

cobj    *__cson_first(cobj *o);
void    __cson_delete(cobj *ob);
int     cson_lock(char *locker);
int     cson_unlock(void);
int     cson_setStringForce(cobj *o, const char *key, const char *val);

int		cson_setfString(cobj *o, const char *val, char *fmt, ...);

int		cson_setfInt(cobj *o, uint64_t val, char *fmt, ...);
int		cson_dumpText(char *data, int *l, cobj *obj, uint64_t ts);
int		cson_sendJson(char *data, int *l, const char *key);
void		cson_delete(cobj *ob);
int		cson_loadJson(const char*fname);
int		cson_loadOrCreateJson(const char *fname);
int		cson_loadOrCreateText(const char *fname);
int		cson_loadJsonTo(const char *fname, cobj *c);
int		cson_loadText(const char*fname);
int		cson_loadTextTo(const char *fname, cobj *c);
cobj	*cson_find(cobj *o, const char*key);
cobj	*cson_find_child(cobj *o, const char*key);
cobj	*cson_next(cobj *o);
cobj	*cson_prev(cobj *o);
cobj	*cson_first(cobj *o);
cobj	*cson_last(cobj *o);
int		cson_getString(cobj *o, const char*key, char *dest, const char*defval);
int		cson_getStringf(cobj *o, char *dest, const char *defval, const char *fmt, ...);
int		cson_setIntf(cobj *o, int64_t defval, const char *fmt, ...);
int64_t		cson_getIntf(cobj *o, int64_t defval, const char *fmt, ...);
int64_t		cson_getInt(cobj *o, const char*key, int64_t defval);
int		cson_getBool(cobj *o, const char*key, int defval);
float	cson_getFloat(cobj *o, const char*key, float defval);
int		cson_setString(cobj *o, const char*key, const char*val);
int		cson_setInt(cobj *o, const char*key, int64_t val);
int		cson_setFloat(cobj *o, const char*key, float val);
int		cson_setBool(cobj *o, const char*key, int val);
int		cson_saveJson(const char*fname, const char*key);
int		cson_saveText(const char *fname, const char *key, uint64_t ts);

int		cson_setStringf(cobj *o, const char*key, char *fmt, ...);
char    *cson_ucase(char *p);
uint64_t	ts_monotonic(void);

uint64_t	startTimer(uint64_t *v);
uint64_t	checkTimer(uint64_t *v);

cobj	*cson_findf(cobj *o, const char *fmt, ...);

void	cson_save(char *name);
void	cson_sync(void);
char	*string_time(time_t t);
//void	GetVersion(char *p);
int		cson_reset(void);
char	*getDeviceModel(void);
int		countLines(char *fname);
char	*current_time(void);
int		read_gpio(int k);
int		write_gpio(int k, int v);
int	cson_checkuykumodu(int uyandirmadakika, const char *uyandirmatarih);
unsigned long cson_upTime(void);
int	cson_parseJson(const char *data, char *name);

int	cson_parseHTTPResponse(const char *data, char *name);
int	current_wday(void);
int	current_seconds(void);
int     json_name_match(char *t, char *f);
int     json_name_scanf(char *text, char *format, ...);
int	cson_sort(char *name, int (*f)(cobj *, cobj *));
#define	CREATE_JSON_WITH_DATE		1
#define	CREATE_ARRAY			2
#define	CREATE_JSON_WITHOUT_DATE	3
int	cson_loadOrCreateJsonWithDate(const char *fname, int created);

uint64_t	ts_monotonic64(void);
uint64_t	startTimer64(uint64_t *v);
uint64_t	checkTimer64(uint64_t *v);
void		cson_check(void);

/* // Functions working local  {
shm_storage	*cson_init_local(uint32_t _shmkey, size_t az);
void	cson_delete_local(cobj *root, cobj *ob);
// } */
#ifdef	cson_c
			int	version_ok = 0;
#else
	extern	int	version_ok;
#endif

#ifdef __cplusplus
}
#endif
#endif
