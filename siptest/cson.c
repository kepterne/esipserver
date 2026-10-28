#define	cson_c

#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/mman.h>
#include	<fcntl.h>
#include	<stdio.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<string.h>
#include	<time.h>
#include	<sys/ipc.h>
#include	<sys/shm.h>
#include	<stdarg.h>
#include	<sys/time.h>
#include	<string.h>
#include    <ctype.h>
#include	<sys/sysinfo.h>
#include	<pthread.h>
#include	<errno.h>
#include	<inttypes.h>
#include	<stddef.h>

#include	<stdarg.h>
#include	"jsonparser.h"
#include	"cson.h"
#include	"cobj.h"
#include	"versio.h"

#define	CSON_ID	0xE11F2909

#define	LogPrint	printf


uint64_t	sync_timer = 0;

int		__cson_initialized = 0;
shm_storage	*__cson_data = NULL;

void	set_ts(cobj *o, uint64_t ts);

shm_storage	*cson_init(uint32_t _shmkey);

uint64_t	startTimer(uint64_t *v) {
	return *v = ts_monotonic();
}

uint64_t	checkTimer(uint64_t *v) {
	return ts_monotonic() - *v;
}

uint64_t	startTimer64(uint64_t *v) {
	return *v = ts_monotonic64();
}

uint64_t	checkTimer64(uint64_t *v) {
	return ts_monotonic64() - *v;
}

void	_cson_sync(void);

static	pthread_mutex_t	__cson_initlock = PTHREAD_MUTEX_INITIALIZER;

int	cson_lock(char *locker) {
	struct	timespec	ts;

	if (!__cson_initialized) {
		// serialize threads racing the first use; retry on failed init
		pthread_mutex_lock(&__cson_initlock);
		if (!__cson_initialized)
			__cson_data = cson_init(CSON_ID);
		pthread_mutex_unlock(&__cson_initlock);
	}
	if (!__cson_data)
		return 1;
	int	i = 0;
	while (1) {
		// pthread_mutex_timedlock deadline is absolute CLOCK_REALTIME
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
		int	rc = pthread_mutex_timedlock(&__cson_data->mut, &ts);
		if (rc == 0)
			break;
		if (rc == EOWNERDEAD) {
			// Holder died; robust mutex handed it to us, mark it usable again
			pthread_mutex_consistent(&__cson_data->mut);
			if (__cson_data->csonlock) {
				// it died inside the critical section: audit the tree
				int	fixes = cobj_repair(__cson_data->maxpairs);
				fprintf(stderr, "CSON RECOVERED from dead %s (%d repairs)\r\n",
					__cson_data->locker[0] ? __cson_data->locker : "unknown",
					fixes);
				fflush(stderr);
			}
			break;
		}
		if (++i >= 3) {
			fprintf(stderr, "CSON LOCK FAILED for %s (%s holds it)\r\n",
				locker,
				__cson_data->locker[0] ? __cson_data->locker : "unknown");
			fflush(stderr);
			return 1;
		}
	}

	__cson_data->csonlock = 1;
	strcpy(__cson_data->locker, locker);
	return 0;
}

int	cson_unlock(void) {
//	if (!__cson_initialized) {
//		__cson_data = cson_init(CSON_ID);
//	}
	if (!__cson_data)
		return 1;
	if (checkTimer(&__cson_data->changed) > 2000) {
		startTimer(&__cson_data->changed);
		_cson_sync();
	}
	// Clear the bookkeeping while we still own the mutex
	__cson_data->csonlock = 0;
	__cson_data->locker[0] = 0;
	pthread_mutex_unlock(&__cson_data->mut);
	return 0;
}

uint64_t	ts_monotonic(void) {
//static	uint64_t v = 0;
//return ++v;
	struct timespec	ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + (ts.tv_nsec / 1000000);
}



uint64_t	ts_monotonic64(void) {
//static	uint64_t v = 0;
//return ++v;
	struct timespec	ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000L + (ts.tv_nsec);
}

size_t	getsize(void) {
	size_t	res = MAX_SHARED_MEMORY;
	unsigned long	sz;
	FILE		*fp = fopen("/etc/movita/shm_size.txt", "r");
	if (!fp)
		return res;
	if (fscanf(fp, "%lu", &sz) == 1) {
		res = sz * 1024 * 1024;
	}
	fclose(fp);
	return res;
}

// /etc/movita/shmsize.txt

shm_storage	*cson_init(uint32_t _shmkey) {
	int				shmkey = _shmkey;
	int				shmid;
	shm_storage		*r 	= NULL;
	int				owner = 1;
	int				PAGE_SIZE = getpagesize();
	size_t			az = getsize();

	if (az % PAGE_SIZE) {
		az -= az % PAGE_SIZE;
		az += PAGE_SIZE;
	}

	shmid = shmget(shmkey, az, IPC_CREAT | IPC_EXCL | 0777);
	if (shmid < 0) {
	//	printf("SHM ALREADY EXISTS, CONNECTING\r\n");
		shmid = shmget(shmkey, az, 0777);
		if (shmid < 0) {
			char	cd[128];
			sprintf(cd, "Could not get <%zu>!", az);
			perror(cd);
			//char	rmkey[64];
			//sprintf(rmkey, "ipcrm -M %u", shmkey);
			goto l0;
		}
		owner = 0;
	} else {

	//	printf("SHM CREATED\r\n");
	}

	//printf("SHM ID : %d\n", shmid);
	r = (shm_storage *) shmat(shmid, NULL, 0);
	if (r == (shm_storage *) -1) {
		perror("shmat");
		r = NULL;
		goto l1;
	}
	if (!owner) {
		// Freshly created segments are zero-filled; wait for the creator
		// to finish initializing (it writes version[] last)
		int	w;
		for (w = 0; !r->version[0] && w < 5000; w++)
			usleep(1000);
		if (!r->version[0]) {
			// Creator died mid-init. If nobody else is attached, adopt it.
			struct	shmid_ds	ds;
			if (shmctl(shmid, IPC_STAT, &ds) == 0 && ds.shm_nattch == 1) {
				fprintf(stderr, "CSON ADOPTING ORPHANED SEGMENT\r\n");
				fflush(stderr);
				owner = 1;
			} else {
				fprintf(stderr, "CSON SHM NEVER INITIALIZED\r\n");
				fflush(stderr);
				goto l1;
			}
		} else if (!(version_ok = (strcmp(LIBVER, r->version) == 0))) {
			fprintf(stderr, "VERSION MISMATCH: %s != %s\r\n", LIBVER, r->version);
			fflush(stderr);
		}
	}
	if (owner) {
		bzero(r, az);
		r->ownerid = shmid;
		r->changed = ts_monotonic();
		r->totalsize = az;
		r->maxpairs =  (r->totalsize - sizeof(shm_storage)) / sizeof(cobj) - 2;
		cobj_init(&r->root[0], r->maxpairs);

		pthread_mutexattr_t attr;
		if (pthread_mutexattr_init(&attr)) {
			perror("pthread_mutexattr_init");
			goto l1;
		}
		if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) {
			perror("pthread_mutexattr_setpshared");
			goto l1;
		}
		// Robust: if a holder dies, the next locker gets EOWNERDEAD instead of hanging
		if (pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST)) {
			perror("pthread_mutexattr_setrobust");
			goto l1;
		}
		if (pthread_mutex_init(&r->mut, &attr)) {
			perror("pthread_mutex_init");
			goto l1;
		}
		r->csonlock = 0;
		r->root[0].objtype = CO_OBJECT;
		// version[] doubles as the "segment ready" flag for attaching
		// processes, so it must be written last
		__sync_synchronize();
		strcpy(r->version, LIBVER);
		version_ok = 1;
	}
	// attachers must not touch shared state before taking the lock:
	// csonlock==1 here means a holder died mid-write (repaired on next lock)
	root = &r->root[0];
	__cson_initialized = 1;
	return r;
l1:
	if (r) {
		shmdt(r);
		r = NULL;
	}
	if (owner)
		shmctl(shmid, IPC_RMID, 0);
l0:
	return r;
}

int	cson_loadJson(const char *fname) {
static	char	keyname[MAX_NAME_LENGTH];
static	char	key[MAX_NAME_LENGTH];
	int		rv = 1;
	//cson_unlock();
	if (cson_lock("cson_loadJson"))
		return rv;
	rv = cobj_loadjson(fname, root, keyname);

	if (cson_unlock())
		rv = 1;

	sprintf(key, "system.files.%s.filename", keyname);
	cson_setString(NULL, key, fname);
	sprintf(key, "system.files.%s.at", keyname);
	cson_setString(NULL, key, current_time());
	cobj *o = cson_find(root, keyname);
	if (o) {
		o->objtype |= CO_JSON;
	}
	return rv;
}

int	cson_loadJsonTo(const char *fname, cobj *c) {
	int	rv = 1;
	//cson_unlock();
	if (cson_lock("cson_loadJson"))
		return rv;
	rv = cobj_loadjson(fname, c, NULL);
	if (cson_unlock())
		rv = 1;
	return rv;
}

int		cson_setStringf(cobj *o, const char *key, char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_setString(o, key, v);
}


int		cson_setfString(cobj *o, const char *val, char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_setString(o, v, val);
}

int		cson_setfInt(cobj *o, uint64_t val, char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_setInt(o, v, val);
}
int	cson_parseJson(const char *data, char *name) {
	int	rv = 1;
	cobj	*obj;

	obj = cson_find(NULL, name);
	if (obj)
		cson_delete(obj);

	if (cson_lock("cson_parseJson"))
		return rv;

	rv = cobj_parsejson(name, root, data);

	if (cson_unlock())
		rv = 1;

	obj = cson_find(NULL, name);
	if (obj) {
		char	key[128], fname[128];
		sprintf(key, "system.files.%s.filename", name);
		sprintf(fname, "/root/%s.json", name);
		cson_setString(root, key, fname);
		sprintf(key, "system.files.%s.at", name);
		cson_setString(root, key, current_time());
		cson_saveJson(fname, name);
//		cson_delete(obj);
	}
	return rv;
}

int	cson_parseHTTPResponse(const char *data, char *name) {
	int	rv = 1;
	cobj	*obj;
	char	_name[128];

	obj = cson_find(NULL, name);
	if (obj)
		cson_delete(obj);
	char	*p = data, *pp;
	char	method[128];
	int	result = 0;
	char	resultstr[128];
	char	fname[256], value[256];
	if (sscanf(p, "%s %d %s",
		method,
		&result,
		resultstr
	) == 3) {
		cson_setfString(root, method, "%s.hdr.method", name);
		cson_setfString(root, resultstr, "%s.hdr.resultstr", name);
		cson_setfInt(root, result, "%s.hdr.result", name);
	} else {
		return 0;
	}
	for (int i = 0; ; i++) {
		p = strstr(p, "\r\n");
		if (!p)
			return 0;
		p += 2;
		if (strncmp(p, "\r\n", 2) == 0) {
			p += 2;
			break;
		}
		if (sscanf(p, "%[^:]: %[^\r]", fname, value)) {
			cson_setfString(root, value, "%s.hdr.%s", name, fname);
		}
	}

	if ((obj = cson_find(NULL, name))) {
		if (cson_lock("cson_parseJson"))
			return rv;
		rv = cobj_parsejson("body", obj, p);

		if (cson_unlock())
			rv = 1;
	}
	return rv;
}

void    __cson_delete(cobj *ob) {
        if (!ob)
                return;
        if (!root)
                return;
        if (ob == root)
                return;

        cobj_free(ob);
        return;
}

void	cson_delete(cobj *ob) {
	if (!ob)
		return;
	if (!root)
		return;
	if (ob == root)
		return;
	if (cson_lock("cson_delete"))
		return;
	cobj_free(ob);
	cson_unlock();
	return;
}

int	cson_loadText(const char *fname) {
	char	keyname[MAX_NAME_LENGTH];
	char	key[MAX_NAME_LENGTH];
	int	rv = 100;
	//cson_unlock();
	if (cson_lock("cson_loadJson"))
		return rv;
	rv = 1;
	rv = cobj_loadtext(fname, keyname);
	if (cson_unlock())
		rv = 1;

	sprintf(key, "system.files.%s.filename", keyname);
	cson_setString(root, key, fname);
	sprintf(key, "system.files.%s.at", keyname);
	cson_setString(root, key, current_time());
	cobj *o = cson_find(root, keyname);
	if (o) {
		o->objtype |= CO_TEXTF;
		rv = 0;
	}
	return rv;
}

int	cson_loadTextTo(const char *fname, cobj *c) {
	int	rv = 1;
	//cson_unlock();
	if (cson_lock("cson_loadTextTo"))
		return rv;
	rv = cobj_loadtextto(fname, c);
	if (cson_unlock())
		rv = 1;
	return rv;
}

cobj	*cson_find(cobj *o, const char *key) {
	cobj	*obj;
	if (cson_lock("cson_find"))
		return NULL;

	obj = cobj_find(o, key);
	if (cson_unlock())
		return NULL;
	return obj;
}

cobj	*cson_findf(cobj *o, const char *fmt, ...) {
	char	key[128];
	va_list	vl;
	va_start(vl, fmt);
	vsprintf(key, fmt, vl);
	va_end(vl);
	return cson_find(o, key);
}

cobj	*__cson_next(cobj *o) {
	cobj	*rv = NULL;

	if (!o)
		goto l1;
	if (!o->next)
		goto l1;
	rv = &root[o->next];
l1:
	return rv;
}

cobj	*cson_next(cobj *o) {
	cobj	*rv = NULL;

	if (cson_lock("cson_next"))
		return NULL;

	if (!o)
		goto l1;
	if (!o->next)
		goto l1;
	rv = &root[o->next];
l1:
	if (cson_unlock())
		return NULL;
	return rv;
}


cobj	*cson_first(cobj *o) {
	cobj	*rv = NULL;

	if (cson_lock("cson_first"))
		return NULL;

	if (!o)
		goto l1;
	if (!o->first)
		goto l1;
	rv = &root[o->first];
l1:
	if (cson_unlock())
		return NULL;

	return rv;
}


cobj	*cson_last(cobj *o) {
	cobj	*rv = NULL;

	if (cson_lock("cson_last"))
		return NULL;

	if (!o)
		goto l1;
	if (!o->last)
		goto l1;
	rv = &root[o->last];
l1:
	if (cson_unlock())
		return NULL;

	return rv;
}

cobj	*__cson_first(cobj *o) {
	cobj	*rv = NULL;

	if (!o)
		goto l1;
	if (!o->first)
		goto l1;
	rv = &root[o->first];
l1:

	return rv;
}
cobj	*cson_prev(cobj *o) {
	cobj	*rv = NULL;

	if (cson_lock("cson_prev"))
		return NULL;

	if (!o)
		goto l1;
	if (!o->prev)
		goto l1;
	rv = &root[o->prev];
l1:
	if (cson_unlock())
		return NULL;

	return rv;
}
int	cson_getStringf(cobj *o, char *dest, const char *defval, const char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_getString(o, v, dest, defval);
}

int64_t	cson_getIntf(cobj *o, int64_t defval, const char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_getInt(o, v, defval);
}

int	cson_setIntf(cobj *o, int64_t defval, const char *fmt, ...) {
	va_list	vl;
	char	v[MAX_TEXT_LENGTH];
	va_start(vl, fmt);
	vsnprintf(v, MAX_TEXT_LENGTH, fmt, vl);
	va_end(vl);
	return cson_setInt(o, v, defval);
}

int	cson_getString(cobj *o, const char *key, char *dest, const char *defval) {
	cobj	*obj = NULL;
	if (cson_lock("cson_getString"))
		goto l2;
	if (key)
	    obj = cobj_find(o, key);
	if (!obj) {
        if (!key) {
            if (o)
                obj = o;
        }
    }
    if (obj)
        strcpy(dest, obj->v);
	else
		strcpy(dest, defval);
	if (cson_unlock())
		;
	return (obj != NULL);

l2:
	strcpy(dest, defval);
	return 1;
}

int64_t	cson_getInt(cobj *o, const char *key, int64_t defval) {
	cobj	*obj;
	int64_t	val = defval;

	if (cson_lock("cson_getInt"))
		goto l1;

    obj = cobj_find(o, key);
	if (!obj) {
        if (!key) {
            if (o)
                obj = o;
        }
    }
	if (obj) {
		sscanf(obj->v, "%" SCNd64, &val);
	} else
		val = defval;
	if (cson_unlock())
		;
l1:
	return val;
}

int	cson_getBool(cobj *o, const char *key, int defval) {
	cobj	*obj;
	int	val = defval;

	if (cson_lock("cson_getBool"))
		goto l1;

	obj = cobj_find(o, key);
    if (!obj) {
        if (!key) {
            if (o)
                obj = o;
        }
    }
	if (obj) {
		val = strcasecmp(obj->v, "true") == 0;
	}
	if (cson_unlock())
		;
l1:
	return val;
}

float	cson_getFloat(cobj *o, const char *key, float defval) {
	cobj	*obj;
	float	val = defval;

	if (cson_lock("cson_getFloat"))
		goto l1;

	obj = cobj_find(o, key);

    if (!obj) {
        if (!key) {
            if (o)
                obj = o;
        }
    }
	if (obj) {
		sscanf(obj->v, "%f", &val);
	}
	if (cson_unlock())
		;
l1:
	return val;
}

int     cson_setStringForce(cobj *o, const char *key, const char *val) {
        cobj    *obj;
        int             rv = 1;
        int             isbool = 0;
        int             isnum = 0;

        if (cson_lock("cson_setString"))
                goto l1;

        obj = cobj_find(o, key);


        if (!obj) {
                if (!key) {
                        if (o)
                                obj = o;
                }
        }
        if (!obj) {
                obj = cobj_createobj(o, key);
                //set_ts(obj, ts_monotonic());
        }
        if (obj) {
                if (strcasecmp(obj->v, val)
                //|| (obj->v[0] && val[0])
                ) {
                        strncpy(obj->v, val, MAX_TEXT_LENGTH - 1);
                        obj->v[MAX_TEXT_LENGTH - 1] = 0;
                        obj->objtype = isbool ? CO_BOOLEAN : (isnum ? CO_INT : CO_TEXT);
                        set_ts(obj, ts_monotonic());
                }
                rv = 0;
        }

        if (cson_unlock())
                rv = 1;
l1:
        return rv;
}

int	cson_setString(cobj *o, const char *key, const char *val) {
	cobj	*obj;
	int		rv = 1;
	int		isbool = 0;
	int		isnum = 0;
	int		vlen = 0;
	char	tmp[MAX_TEXT_LENGTH];

	if (cson_lock("cson_setString"))
		goto l1;

	// bounded copy, cut at the first non-printable char; val itself
	// is never written (it may live in read-only memory)
	isnum = 1;
	for (const char *pp = val; *pp && vlen < MAX_TEXT_LENGTH - 1; pp++) {
		if (!isprint((unsigned char) *pp))
			break;
		isnum &= (*pp >= '0') && (*pp <= '9');
		tmp[vlen++] = *pp;
	}
	tmp[vlen] = 0;

	isbool = (strcasecmp(tmp, "true") == 0) || (strcasecmp(tmp, "false") == 0);

	obj = cobj_find(o, key);

	if (!obj) {
		if (!key) {
			if (o)
				obj = o;
		}
    	}
	if (!obj) {
		obj = cobj_createobj(o, key);
		//set_ts(obj, ts_monotonic());
	}
	if (obj) {
		if (strcasecmp(obj->v, tmp)
		//|| (obj->v[0] && val[0])
		) {
			strcpy(obj->v, tmp);
			if (tmp[0])
				obj->objtype = isbool ? CO_BOOLEAN : (isnum ? CO_INT : CO_TEXT);
			else
				obj->objtype = CO_TEXT;
			set_ts(obj, ts_monotonic());
		}
		rv = 0;
	}

	if (cson_unlock())
		rv = 1;
l1:
	return rv;
}
/*
int	cson_setString(cobj *o, const char *key, const char *val) {
	cobj	*obj;
	int		rv = 1;

	if (cson_lock("cson_setString"))
		goto l1;

	obj = cobj_find(o, key);

	if (!obj) {
		if (!key) {
			if (o)
				obj = o;
		}
    	}
	if (!obj) {
		obj = cobj_createobj(o, key);
		//set_ts(obj, ts_monotonic());
	}
	if (obj) {
		if (strcasecmp(obj->v, val) || (obj->v[0] && val[0])) {
			strncpy(obj->v, val, MAX_TEXT_LENGTH);
			obj->objtype = CO_TEXT;
			set_ts(obj, ts_monotonic());
		}
		rv = 0;
	}

	if (cson_unlock())
		rv = 1;
l1:
	return rv;
}
*/
int	cson_setInt(cobj *o, const char *key, int64_t val) {
	char	v[MAX_TEXT_LENGTH];
	sprintf(v, "%" PRId64, val);
	return cson_setString(o, key, v);
}

int	cson_setFloat(cobj *o, const char *key, float val) {
	char	v[MAX_TEXT_LENGTH];
	sprintf(v, "%f", val);
	return cson_setString(o, key, v);
}

int	cson_setBool(cobj *o, const char *key, int val) {
	char	v[MAX_TEXT_LENGTH];
	sprintf(v, "%s", val ? "true" : "false");
	return cson_setString(o, key, v);
}

int		cson_saveJson(const char *fname, const char *key) {
	FILE		*fp;
	cobj		*obj = NULL;
	int		rv = -2;
	int		level = 0;

	if (fname)
		fp = fopen(fname, "wb");
	else
		fp = stdout;
	if (!fp)
		goto l1;
	if (cson_lock("cson_saveJson"))
		goto l11;
	if (key) {
		obj = cobj_find(root, key);
		if (!obj)
			goto l2;
	} else
		obj = root;

	if (obj) {
		if (obj->objtype & CO_OBJECT)
			fprintf(fp, "{\n");
		else if (obj->objtype & CO_ARRAY)
			fprintf(fp, "[\n");
		level++;
	}

	print_tree(fp, obj, level);
	if (obj) {
		if (obj->objtype & CO_OBJECT)
			fprintf(fp, "}\n");
		else if (obj->objtype & CO_ARRAY)
			fprintf(fp, "]\n");
	}
	if (fp != stdout)
		fclose(fp);
	if (cson_unlock())
		goto l1;
	return 0;
l2:
	cson_unlock();
l11:
	if (fp != stdout)
		fclose(fp);
l1:
	return rv;
}


int		cson_sendJson(char *data, int *l, const char *key) {
	cobj		*obj = NULL;
	int		rv = -2;
	int		level = 0;
	if (!data)
		return 0;
	*data = 0;
	if (cson_lock("cson_sendJson"))
		goto l11;
	if (key) {
		obj = cobj_find(root, key);
		if (!obj)
			goto l2;
	} else
		obj = root;

	if (obj) {
		if (obj->objtype & CO_OBJECT)
			*l += sprintf(data + *l, "{\n");
		else if (obj->objtype & CO_ARRAY)
			*l += sprintf(data + *l, "[\n");
		level++;
	}

	//print_tree(fp, obj, level);
	dump_tree(data, l, obj, level);
	if (obj) {
		if (obj->objtype & CO_OBJECT)
			*l += sprintf(data + *l, "}\n");
		else if (obj->objtype & CO_ARRAY)
			*l += sprintf(data + *l, "]\n");
	}

	if (cson_unlock())
		goto l1;
	return 0;
l2:
	cson_unlock();
l11:
	;
l1:
	return rv;
}

int		cson_saveText(const char *fname, const char *key, uint64_t ts) {
	FILE		*fp;
	cobj		*obj = NULL;
	int		rv = -2;
	int		level = 0;
	if (!fname)
		fp = stdout;
	else
		fp = fopen(fname, "wb");
	if (!fp)
		goto l1;
	if (cson_lock("cson_saveText"))
		goto l2;
	if (key) {
		obj = cobj_find(root, key);
		if (!obj)
			goto l11;
	} else
		obj = root;



	cobj_printtext(fp, obj, level, ts);
	if (fp != stdout)
		fclose(fp);
	if (cson_unlock())
		goto l1;
	return 0;
l11:
	cson_unlock();
l2:
	if (fp != stdout)
		fclose(fp);
l1:
	return rv;
}


int		cson_dumpText(char *data, int *l, cobj *obj, uint64_t ts) {

	int		rv = -2;
	int		level = 0;
	if (!data)
		goto l1;
	if (cson_lock("cson_saveText"))
		goto l2;

	cobj_dumptext(data, l, obj, level, ts);

	if (cson_unlock())
		goto l1;
	return 0;
//l11:
	cson_unlock();
l2:
	;
l1:
	return rv;
}


char    *cson_ucase(char *p) {
    char    *r = p;
    for ( ; *p; p++) {
        *p = toupper(*p);
    }
    return r;
}

/*
void	GetVersion(char *p) {
	unsigned char	version[32] = {
		VERSION_MAJOR_INIT,
		'.',
		VERSION_MINOR_INIT,
		'-', 'D',
//		'-',
		BUILD_YEAR_CH0, BUILD_YEAR_CH1, BUILD_YEAR_CH2, BUILD_YEAR_CH3,
//		'-',
		BUILD_MONTH_CH0, BUILD_MONTH_CH1,
//		'-',
		BUILD_DAY_CH0, BUILD_DAY_CH1,
		'T',
		BUILD_HOUR_CH0, BUILD_HOUR_CH1,
//		':',
		BUILD_MIN_CH0, BUILD_MIN_CH1,
//		':',
		BUILD_SEC_CH0, BUILD_SEC_CH1,
		'\0'
	};
	strcpy(p, (char *) version);
}
*/
int	cson_reset(void) {
	int	totalsize;
	int	shmid;
	if (cson_lock("cson_reset"))
		return 1;
	shm_storage	*r = __cson_data;
	shmid = r->ownerid;
	totalsize = r->totalsize;
	// Zero everything except the mutex: we are holding it, and other
	// processes' waiters (and our robust list entry) reference it in place
	bzero(r, offsetof(shm_storage, mut));
	bzero((char *) (&r->mut + 1),
		totalsize - offsetof(shm_storage, mut) - sizeof(r->mut));
	strcpy(r->version, LIBVER);
	r->ownerid = shmid;
	r->changed = ts_monotonic();
	r->totalsize = totalsize;
	r->maxpairs =  (r->totalsize - sizeof(shm_storage)) / sizeof(cobj) - 2;
	cobj_init(&r->root[0], r->maxpairs);
	root = &r->root[0];
	root->objtype = CO_OBJECT;
	return cson_unlock();
}

char	*getDeviceModel(void) {
static	char	model[256] = "";
	if (!model[0]) {
		FILE	*fp = fopen("/proc/device-tree/model", "rb");
		if (fp) {
			fgets(model, sizeof(model), fp);
			fclose(fp);
		}
	}
	return model;
}

int	countLines(char *fname) {
	FILE	*fp;
	int		c, lines = 0;

	if (!(fp = fopen(fname, "rb")))
		return 0;
	while ((c = fgetc(fp)) != EOF)
		lines += c == '\n';
	fclose(fp);
	return lines;
}

char	*current_time(void) {
	static	char	cur_time[128];
	time_t	t;
	struct tm *tmp;
	t = time(NULL);
	tmp = localtime(&t);
	strftime(cur_time, sizeof(cur_time), "%Y-%m-%d - %T", tmp);
	return cur_time;
}

int	current_wday(void) {
	time_t	t;
	struct tm *tmp;
	t = time(NULL);
	tmp = localtime(&t);
	//strftime(cur_time, sizeof(cur_time), "%Y-%m-%d - %T", tmp);
	return tmp->tm_wday;
}

int	current_seconds(void) {
	time_t	t;
	struct tm *tmp;
	t = time(NULL);
	tmp = localtime(&t);
	//strftime(cur_time, sizeof(cur_time), "%Y-%m-%d - %T", tmp);
	return tmp->tm_hour * 3600 + tmp->tm_min * 60 + tmp->tm_sec;
}

char	*string_time(time_t t) {
	static	char	cur_time[128];
	struct tm *tmp;
	tmp = localtime(&t);
	strftime(cur_time, sizeof(cur_time), "%Y-%m-%d - %T", tmp);
	return cur_time;
}

int	gpio_initialized = 0;

volatile uint32_t *gpio;
volatile uint8_t 	*bgpio;

int	init_gpio(void) {
	if (gpio_initialized)
		return 0;
	gpio_initialized = 1;
	int fdgpio=open("/dev/gpiomem",O_RDWR);
	if (fdgpio<0) {
		fprintf(stderr, "Error opening /dev/gpiomem\r\n");
		fflush(stdout);
		return 1;
	}

	gpio = (uint32_t *) mmap(
			0, 4096, PROT_READ+PROT_WRITE, MAP_SHARED, fdgpio, 0
	);
	bgpio = (uint8_t *) gpio;
	return 0;
}

int	read_gpio(int k) {
	uint32_t	g;
	if (init_gpio())
		return -1;
	g = gpio[(0x34 / 4) + (k >= 32 ? 1 : 0)];
	if (k >= 32)
		k -= 32;
	return (g & (1 << k)) ? 1 : 0;
}




int	write_gpio(int k, int v) {
	volatile uint32_t	*g;
	if (init_gpio())
		return -1;
	g = gpio + ((v ? 0x1C : 0x28) / 4) + (k >= 32 ? 1 : 0);
	if (k >= 32)
		k -= 32;
	*g = (1 << k);

	return (gpio[(0x34 / 4) + (k >= 32 ? 1 : 0)]) & (1 << k) ? 1 : 0;
}


int	cson_loadOrCreateJsonWithDate(const char *fname, int created) {
	if (cson_loadJson(fname)) {
		FILE	*fp = fopen(fname, "wb");
		if (fp) {
			if (created == CREATE_JSON_WITH_DATE)
				fprintf(fp, "{\n\"created\":\"%s\"\n}\n", current_time());
			else if (created == CREATE_ARRAY)
				fprintf(fp, "[]\n");
			else
				fprintf(fp, "{}");

			fclose(fp);
			return cson_loadJson(fname);
		}
	} else
		return 0;
	return 1;
}

int	cson_loadOrCreateJson(const char *fname) {
	return cson_loadOrCreateJsonWithDate(fname, CREATE_JSON_WITH_DATE);
}


int	cson_loadOrCreateText(const char *fname) {
	int	rv;
	l0:
	rv =  cson_loadText(fname);

	if (rv) {
		FILE	*fp = fopen(fname, "rb");
		if (fp) {
			fclose(fp);
			goto l0;
		}
		fp = fopen(fname, "wb");
		if (fp) {
			fprintf(fp, "created=%s\n", current_time());
			fclose(fp);
			cson_loadText(fname);
			return 0;
		}
	} else
		return 0;
	return 1;
}

int	cson_checkuykumodu(int uyandirmadakika, const char *uyandirmatarih) {
	struct  tm 	*t;
	int		ts;
	int		uykumodu;

	int		yr, mn, dy, h, m, s;
	time_t	wakeup_time	= 0;
	time_t	wakeup_end = 0;
	time_t	now;

	if (sscanf(uyandirmatarih, "%u-%u-%uT%u:%u:%u", &yr, &mn, &dy, &h, &m, &s) != 6)
		goto l1;

	time_t	twakeup;

	now = time(NULL);
	t = localtime(&now);
	t->tm_year = yr - 1900;
	t->tm_mon = mn - 1;
	t->tm_mday = dy;
	t->tm_hour = h;
	t->tm_min = m;
	t->tm_sec = s;
	twakeup = mktime(t);
	wakeup_time = twakeup;
	wakeup_end = twakeup + 60 * uyandirmadakika;

	if (now >= twakeup && now <= wakeup_end) {
		uykumodu = cson_getInt(NULL, "system.uykuModu", 0); //getuykumodu_eshm();
		if (uykumodu != 0) {
			fprintf(stderr, "!!uyanma>> %lu >? %lu >? %lu\r\n", (unsigned long) wakeup_time, (unsigned long) now, (unsigned long) wakeup_end);
			fflush(stderr);

			cson_setInt(NULL, "system.uykuModu", 0); //setuykumodu_eshm(0);
		}
		return 1;
	}
l1:
	uykumodu = cson_getInt(NULL, "system.uykuModu", 0);  //uykumodu = getuykumodu_eshm();
	if (uykumodu != 1) {
		fprintf(stderr, "!!uyanma>> %lu >? %lu >? %lu\r\n", (unsigned long) wakeup_time, (unsigned long) now, (unsigned long) wakeup_end);
		fflush(stderr);

		//setuykumodu_eshm(1);
		cson_setInt(NULL, "system.uykuModu", 1);
	}
	return 0;
}
struct sysinfo	__sysinfo;

unsigned long cson_upTime(void) {
	sysinfo(&__sysinfo);
	return __sysinfo.uptime;
}
void	saveFile(cobj *o);
void	_cson_sync(void) {
	cobj	*o;
	for (o = __cson_first(root); o; o = __cson_next(o)) {

		if (o->objtype & (CO_JSON | CO_TEXTF)) {
			if (o->objtype & CO_CHANGED) {
				if (!(o->objtype & CO_READONLY))
					saveFile(o);

				o->objtype &= (0xffff ^ CO_CHANGED);
			}
		}
	}
}

void	cson_syncreset(char *key) {
	cobj	*o;
	if (cson_lock("cson_syncreset"))
		goto l1;
	if ((o = cobj_find(NULL, key)))
		if (o->objtype & (CO_JSON | CO_TEXTF)) {
			if (o->objtype & CO_CHANGED) {
				o->objtype &= (0xffff ^ CO_CHANGED);
			}
		}

	cson_unlock();
l1:
	;
}
void	cson_save(char *name) {
	if (cson_lock("cson_save"))
		goto l1;
	_saveFileNamed(name);
	cson_unlock();
l1:
	;
}

void	cson_sync(void) {
	cobj	*o;
	if (cson_lock("cson_sync"))
		goto l1;
	for (o = __cson_first(root); o; o = __cson_next(o)) {

		if (o->objtype & (CO_JSON | CO_TEXTF)) {
			if (o->objtype & CO_CHANGED) {
				if (!(o->objtype & CO_READONLY))
					saveFile(o);
				o->objtype &= (0xffff ^ CO_CHANGED);
			}
		}
	}

	cson_unlock();
l1:
	;
}


int     json_name_match(char *t, char *f) {
        if ((!f) || (!t))
                return 0;
        for ( ; *f; f++) {
                if (*f == '\%') {
                        f++;
                        if (*f == 'd') {
                                if ((*t < '0') || (*t > '9'))
                                        return 0;
                                while ((*t >= '0') && (*t <= '9'))
                                        t++;
                        }
                } else if (*f == '*')
                        return 1;
                else if (toupper(*f) == toupper(*t)) {
                        t++;
                        continue;
                } else
                        return 0;
        }
        if (*f)
                return 0;
        return 1;
}


int     json_name_scanf(char *text, char *format, ...) {
        va_list         ap;
    int                 result = 0;
        if (json_name_match(text, format)) {
                va_start(ap, format);
                result = vsscanf(text, format, ap);
                va_end(ap);
        }
        return result;
}
int	cson_switch(cobj *o, cobj *oo) {
	cobj	*p;
	int	ixo, ixoo;
	int	t;
	int	onext, oprev, oonext, ooprev;
	cobj *co;
	if (!o || !oo)
		return 0;
	if (o->parent != oo->parent)
		return 0;

	for (co = cson_first(&root[o->parent]); co; co = cson_next(co)) {
		char	b1[512];

		cson_getString(co, "cameraIp", b1, "");
		printf("[ %d | %d | %s]\r\n", co->index, co->idx, b1);
	}
	printf(" %d > %d | %d | %d < %d \r\n",
		o->prev ? root[o->prev].next : 0, o->prev, o->idx, o->next, o->next ? root[o->next].prev : 0
	);

	printf(" %d > %d | %d | %d < %d \r\n",
		oo->prev ? root[oo->prev].next : 0, oo->prev, oo->idx, oo->next, oo->next ? root[oo->next].prev : 0
	);
	printf(" - - - - - - - \r\n");
	if (cson_lock("cson_sync"))
		goto l1;


	ixo = o->idx;
	ixoo = oo->idx;
	onext = o->next;
	oprev = o->prev;
	oonext = oo->next;
	ooprev = oo->prev;
	p = &root[o->parent];

	if (oonext) {
		if (oonext == ixo)
			o->prev = ooprev;
		else  {
			root[oonext].prev = ixo;
			o->next = oonext;
		}
	} else {
		o->next = 0;
		p->last = ixo;
	}
	if (ooprev) {
		if (ooprev == ixo)
			o->next = onext;
		else {
			root[ooprev].next = ixo;
			o->prev = ooprev;
		}
	} else {
		o->prev = 0;
		p->first = ixo;
	}
	if (onext) {
		if (onext == ixoo)
			root[ixoo].prev = oprev;
		else {
			oo->next = onext;
			root[onext].prev = ixoo;
		}
	} else {
		oo->next = 0;
		p->last = ixoo;
	}
	if (oprev) {
		if (oprev == ixoo)
			root[ixoo].next = onext;
		else {
			oo->prev = oprev;
			root[oprev].next = ixoo;
		}
	} else {
		oo->prev = 0;
		p->first = ixoo;
	}
	t = o->index; o->index = oo->index; oo->index = t;
	cson_unlock();
	printf(" %d > %d | %d | %d < %d \r\n",
		o->prev ? root[o->prev].next : 0, o->prev, o->idx, o->next, o->next ? root[o->next].prev : 0
	);

	printf(" %d > %d | %d | %d < %d \r\n",
		oo->prev ? root[oo->prev].next : 0, oo->prev, oo->idx, oo->next, oo->next ? root[oo->next].prev : 0
	);

	for (co = cson_first(&root[o->parent]); co; co = cson_next(co)) {
		char	b1[512];

		cson_getString(co, "cameraIp", b1, "");
		printf("[ %d | %d | %s]\r\n", co->index, co->idx, b1);
	}


	printf(" = = = = = = = \r\n");
	return 1;
l1:
	return 0;
}

int	cson_sort(char *name, int (*f)(cobj *, cobj *)) {
	cobj	*o = cson_find(NULL, name);
	cobj	*oo, *p;
	int		rv = 0, i, j, t, switches;
	if (!o)
		return 0;
	p = o;

	for (i = 0; i < p->count; i++) {
		o = &root[p->first];
		switches = 0;
		for (j = 0; j < p->count - i - 1; j++) {
			if (!o->next)
				break;
			oo = &root[o->next];
			if ((*f)(o, oo) > 0) {
				switches++;
				if (!(o->next = oo->next))
					p->last = o->idx;
				else
					root[o->next].prev = o->idx;
				if (!(oo->prev = o->prev))
					p->first = oo->idx;
				else
					root[oo->prev].next = oo->idx;
				o->prev = oo->idx;
				oo->next = o->idx;
				t = o->index;
				o->index = oo->index;
				oo->index = t;
			} else
				o = oo;
		}
		if (!switches)
			break;
		rv += switches;
	}
	cson_unlock();
	return rv;
}

void	cson_print(cobj *o, int ix) {
	cobj	*c;
	int	n = 0;
	for (int i = 0; i < ix; i++)
		printf(" ");
	printf("%3d : [%3d] (%3d:%3d) <%3d:%3d> %s\r\n",
		o->idx,
		o->count,
		o->first,
		o->last,
		o->firstfree,
		o->lastfree,
		o->name[0] ? o->name : ""
	);
	n = o->first;
	c = &root[n];
	for ( ; n; n = c->next) {
		c = &root[n];
		cson_print(c, ix + 1);
	}
}

void	cson_check(void) {
	cobj	*o = root;
	shm_storage	*s = __cson_data;
	if (cson_lock("cson_check"))
		goto l1;
	printf("TOTAL SIZE: %d\r\n", s->totalsize);
	printf("MAX PAIRS : %d\r\n", s->maxpairs);
	cson_print(o, 0);
	cson_unlock();
l1:
	return;
}
/*
shm_storage	*cson_init_local(uint32_t _shmkey, size_t az) {
	int				shmkey = _shmkey;

	shm_storage		*r 	= NULL;
	int				owner = 1;

	size_t	zz = az - sizeof(shm_storage);
	if (zz % sizeof(cobj)) {
		az -= zz % sizeof(cobj);
		az += sizeof(cobj);
	}

	r = (shm_storage *) calloc(1, az);
	if (!r)
		goto l1;
	bzero(r, az);
	strcpy(r->version, LIBVER);
	version_ok = 1;
	r->ownerid = 0;
	r->changed = ts_monotonic();
	r->totalsize = az;
	r->maxpairs =  (r->totalsize - sizeof(shm_storage)) / sizeof(cobj) - 2;
	cobj_init(&r->root[0], r->maxpairs);

	pthread_mutexattr_t attr;
	if (pthread_mutexattr_init(&attr)) {
		perror("pthread_mutexattr_init");
		goto l0;
	}
	if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) {
		perror("pthread_mutexattr_setpshared");
		goto l0;
	}
	if (pthread_mutex_init(&r->mut, &attr)) {
		perror("pthread_mutex_init");
		goto l0;
	}

	r->csonlock = 0;
	//root = &r->root[0];
	r->root[0].objtype = CO_OBJECT;
l1:
	return r;
l0:
	free(r);
	r = NULL;
	return NULL;
}


cobj	*cson_first_local(cobj *root, cobj *o) {
	cobj	*rv = NULL;

	if (cson_lock("cson_first"))
		return NULL;

	if (!o)
		goto l1;
	if (!o->first)
		goto l1;
	rv = &root[o->first];
l1:
	if (cson_unlock())
		return NULL;

	return rv;
}


void	cson_delete_local(cobj *root, cobj *ob) {
	if (!ob)
		return;
	if (!root)
		return;
	if (ob == root)
		return;

	cobj_free(ob);

	return;
}

*/