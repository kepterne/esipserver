#define	cobj_c

#include	<sys/types.h>
#include	<sys/stat.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<stdint.h>
#include	<strings.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<stdint.h>
#include	<errno.h>
#include	<inttypes.h>
#include	"cobj.h"
#include	"jsonparser.h"

char		*current_time(void);
uint64_t	ts_monotonic(void);


char	*cson_escape(char *in, char *out) {
	char	*r = out;
	for ( ; *in; in++) {
		if (*in == '\t') {
			*(out++) = '\\';
			*(out++) = 't';

		} else if (*in == '\r') {
			*(out++) = '\\';
			*(out++) = 'r';

		} else if (*in == '\n') {
			*(out++) = '\\';
			*(out++) = 'n';

		} else if (*in == '\b') {
			*(out++) = '\\';
			*(out++) = 'b';

		} else if (*in == '\"') {
			*(out++) = '\\';
			*(out++) = '"';
		} /*else if (*in == '\\') {
			*(out++) = '\\';
			*(out++) = '\\';
		} */ else
			*(out++) = *in;
	}
	*(out) = 0;
	return r;
}

void	_saveFileNamed(char *name) {
	FILE		*fp;
	cobj		*obj = NULL, *o2;
	int			rv = -2;
	int			level = 0;
	char		key[128];
	char		fname[128];
	obj = cobj_find(NULL, name);
	if (!obj)
		return;
	obj->objtype |= CO_CHANGED;
	return;
	sprintf(key, "system.files.%s.filename", name);
	if (!(o2 = cobj_find(root, key))) {
		return;
	}
	strcpy(fname, o2->v);

	fp = fopen(fname, "wb");
	if (!fp)
		goto l1;


	obj = cobj_find(NULL, name);
	if (!obj)
		goto l1;
	if (obj->objtype & CO_JSON) {
		//printf("SAVING JSON : %s\n", fname);
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
	} else if (obj->objtype & CO_TEXTF)  {
		printf("SAVING TEXT : %s\n", fname);
		cobj_printtext(fp, obj, 0, 0);
	}
	fclose(fp);
	sprintf(key, "system.files.%s.at", name);
	if (!(o2 = cobj_find(root, key))) {
		return;
	}
	strcpy(o2->v, current_time());
	return;
	if (fp != stdout)
		fclose(fp);
l1:
	return;
}

void	saveFile(cobj *o) {
	FILE		*fp;
	cobj		*obj = NULL, *o2;
	int			rv = -2;
	int			level = 0;
	char		key[128];
	char		fname[128];

	sprintf(key, "system.files.%s.filename", o->name);
	if (!(o2 = cobj_find(root, key))) {
		return;
	}
	strcpy(fname, o2->v);

	fp = fopen(fname, "wb");
	if (!fp)
		goto l1;


	obj = o;
	if (obj->objtype & CO_JSON) {
		//printf("SAVING JSON : %s\n", fname);
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
	} else {
		printf("SAVING TEXT : %s\n", fname);
		cobj_printtext(fp, obj, 0, 0);
	}
	fclose(fp);
	sprintf(key, "system.files.%s.at", o->name);
	if (!(o2 = cobj_find(root, key))) {
		return;
	}
	strcpy(o2->v, current_time());
	return;
	if (fp != stdout)
		fclose(fp);
l1:
	return;
}

void	set_ts(cobj *o, uint64_t ts) {
	if (!root)
		return;
	do {
		if (o->objtype & (CO_JSON | CO_TEXTF)) {
			if (o->ts != ts) {
				//saveFile(o);
				o->objtype |= CO_CHANGED;
			}
		}
		o->ts = ts;
		o = &root[o->parent];
	} while (o != root);
	o->ts = ts;
}

void	cobj_init(cobj *r, int n) {
	cobj	*co;
	r->count = 0;
	r->first = 0;
	r->last = 0;
	r->firstfree = 1;
	r->lastfree = n;
	co = &r[1];
	for (int i = 1; i < n ; i++, co++) {
		if (i < n)
			co->next = i + 1;
		else
			co->next = 0;
		co->prev = i - 1;
		co->idx = i;
		co->index = 0;
		co->count = 0;
		co->first = co->last = co->parent = 0;
		co->objtype = CO_NULL;
		co->v[0] = 0;
		co->ts = ts_monotonic();
	}
	root = r;
}

cobj	*cobj_new(void) {
	cobj	*co = NULL;

	if (!root)
		return NULL;

	if (!root->firstfree)
		return co;

	co = &root[root->firstfree];

	if (co->prev) {
		root[co->prev].next = co->next;
	} else
		root->firstfree = co->next;

	if (co->next) {
		root[co->next].prev = co->prev;
	} else
		root->lastfree = co->prev;
	co->name[0] = co->v[0] = 0;
	co->ts = ts_monotonic();
	co->index = 0;
	return co;
}
void	cobj_touch(cobj *o, uint64_t ts) {
	int	n;
	if (!o)
		return;
	n = o->first;
	for ( ; n; n = root[n].next) {
		root[n].ts = ts;
		cobj_touch(&root[n], ts);
	}
}
void	cobj_free(cobj *co) {
	cobj	*p;
	int	n;

	if (!root || !co)
		return;

	p = &root[co->parent];
	p->count--;

	for (n = co->next; n; n = root[n].next) {
		root[n].index--;
		if (p->objtype & CO_ARRAY)
			cobj_touch(&root[n], ts_monotonic());
	}
	n = co->next;

	if (co->next) {
		root[co->next].prev = co->prev;
	} else if (p) {
		p->last = co->prev;
	}
	if (co->prev) {
		root[co->prev].next = co->next;
	} else if (p) {
		p->first = co->next;
	}

	while (co->first) {
		cobj_free(&root[co->first]);
	}
	co->objtype = 0;
	co->count = 0;
	co->index = 0;
	co->parent = co->first = co->last = 0;

	co->prev = root->lastfree;
	co->next = 0;
	if (root->lastfree) {
		root[root->lastfree].next = co->idx;
	}
	root->lastfree = co->idx;
	if (!root->firstfree)
		root->firstfree = co->idx;
	root->count++;
	if (p) {
		p->ts = ts_monotonic();
		if (n)
			for (; n; n = root[n].next) {
				root[n].ts = p->ts;
				set_ts(&root[n], p->ts);
			}
		//else
		set_ts(p, p->ts);
		if (p->objtype & (CO_JSON | CO_TEXTF))
			p->objtype |= CO_CHANGED;
	}
}


static	int	repair_fixes;

static	void	repair_walk(cobj *o, unsigned char *seen, int maxp) {
	int	n, prev = 0, cnt = 0, index = 0;

	for (n = o->first; n; ) {
		cobj	*c = &root[n];
		if (n < 1 || n > maxp || seen[n] || !c->objtype || c->parent != o->idx) {
			// a writer died mid-mutation here: cut the chain at the damage
			if (prev)
				root[prev].next = 0;
			else
				o->first = 0;
			repair_fixes++;
			break;
		}
		seen[n] = 1;
		if (c->prev != prev) {
			c->prev = prev;
			repair_fixes++;
		}
		c->index = index;
		repair_walk(c, seen, maxp);
		prev = n;
		cnt++;
		index++;
		n = c->next;
	}
	if (o->last != prev) {
		o->last = prev;
		repair_fixes++;
	}
	if (o->count != cnt) {
		o->count = cnt;
		repair_fixes++;
	}
}

// Audit the store after a lock holder died mid-critical-section: keep every
// node still reachable from the root, cut damaged sibling chains, and rebuild
// the free list from all unreachable nodes (recovers leaks and any half-spliced
// free-list state in one pass). Caller must hold the cson lock.
int	cobj_repair(int maxpairs) {
	unsigned char	*seen;
	int		n, ff = 0, lf = 0;

	if (!root)
		return 0;
	seen = (unsigned char *) calloc(maxpairs + 1, 1);
	if (!seen)
		return 0;
	repair_fixes = 0;
	repair_walk(root, seen, maxpairs);

	for (n = 1; n <= maxpairs; n++) {
		if (seen[n])
			continue;
		cobj	*c = &root[n];
		c->idx = n;
		c->objtype = CO_NULL;
		c->parent = c->first = c->last = 0;
		c->count = c->index = 0;
		c->next = 0;
		c->prev = lf;
		if (lf)
			root[lf].next = n;
		else
			ff = n;
		lf = n;
	}
	root->firstfree = ff;
	root->lastfree = lf;
	free(seen);
	return repair_fixes;
}

void	cobj_detach(cobj *co) {
	cobj	*p;
	int	n;

	if (!root || !co)
		return;

	p = &root[co->parent];
	p->count--;

	for (n = co->next; n; n = root[n].next) {
		root[n].index--;
	}
	n = co->next;

	if (co->next) {
		root[co->next].prev = co->prev;
	} else if (p) {
		p->last = co->prev;
	}
	if (co->prev) {
		root[co->prev].next = co->next;
	} else if (p) {
		p->first = co->next;
	}
}

void	cobj_attach(cobj *parent, cobj *child) {
	if (!parent || !child)
		return;

	if (!parent->first)
		parent->first = child->idx;

	if (parent->last)
		root[parent->last].next = child->idx;

	child->prev = parent->last;
	child->next = 0;
	child->parent = parent->idx;
	child->index = parent->count;
	parent->count++;
	parent->last = child->idx;
}


cobj	*cobj_nth(cobj *obj, int index) {
	int	idx = 0, pos = 0;
	if (!root || !obj)
		return NULL;
	for (idx = obj->first; idx; idx = root[idx].next, pos++) {
		if (pos != root[idx].index) {
			printf("INDEX ERROR!\n");
		}
		if (pos == index)
			return &root[idx];
	}
	return NULL;
}

cobj	*cobj_findname(cobj *obj, const char *name) {
	int	idx;
	if (!root || !obj)
		return NULL;
	for (idx = obj->first; idx; idx = root[idx].next)
		if (strncasecmp(root[idx].name, name, MAX_NAME_LENGTH) == 0)
			return &root[idx];
	return NULL;
}
/*
cobj	*cobj_findname_local(cobj *root, cobj *obj, const char *name) {
	int	idx;
	if (!root || !obj)
		return NULL;
	for (idx = obj->first; idx; idx = root[idx].next)
		if (strncasecmp(root[idx].name, name, MAX_NAME_LENGTH) == 0)
			return &root[idx];
	return NULL;
}
*/
cobj	*cobj_find(cobj *obj, const char *name) {
	char		key[MAX_NAME_LENGTH], *p;
	int		index;
	cobj		*o;

	if (!obj)
		o = root;
	else
		o = obj;
	if (!root)
		return NULL;

	for ( ; ; ) {
		for (p = key; ; name++) {
			if (*name == '.') {
				name++;
				break;
			}
			if (!*name)
				break;
			if (*name == '[') {
				if (p != key)
					break;
				for (index = 0, name++; *name >= '0' && *name <= '9'; name++) {
					index = index * 10 + *name - '0';
				}
				if (*name != ']')
					return NULL;
				if (index >= o->count)
					return NULL;
				if (!(o = cobj_nth(o, index)))
					return NULL;
				p = key;
				name++;
				if (!*name)
					return o;
				continue;
			} else if (p - key < MAX_NAME_LENGTH - 1)
				*(p++) = *name;
		}
		*p = 0;
		if ((o = cobj_findname(o, key))) {
			if (*name)
				continue;
		}
		break;
	}
	return o;
}

int	parent_isarray(cobj *obj) {
	if (!root || !obj)
		return 0;
	return obj->objtype & CO_ARRAY;
}

void	dump_tree(char *data, int *l, cobj *obj, int level) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = level;

	if (!obj)
		obj = root;
	if (!root)
		return;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}

			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);
			*l += sprintf(data + *l, "{\n");
			dump_tree(data, l, o, level + 1);
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			*l += sprintf(data + *l, "}");
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		case CO_ARRAY:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);
			*l += sprintf(data + *l, "[\n");
			dump_tree(data, l, o, level + 1);
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			*l += sprintf(data + *l, "]");
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		case CO_TEXT:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);
			char	tmp_text[512];
			cson_escape(o->v, tmp_text);
			*l += sprintf(data + *l, "\"%s\"", tmp_text);
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		case CO_BOOLEAN:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);

			*l += sprintf(data + *l, "%s", strcasecmp(o->v, "true") ? "false" : "true");
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		case CO_INT:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);
			int64_t	v = 0;
			sscanf(o->v, "%" SCNd64, &v);
			*l += sprintf(data + *l, "%" PRId64, v);
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		case CO_FLOAT:
			for (int i = 0; i < level; i++) {
				*l += sprintf(data + *l, "\t");
			}
			if (o->name[0])
				*l += sprintf(data + *l, "\"%s\" : ", o->name);
			float	f = 0;
			sscanf(o->v, "%f", &f);
			*l += sprintf(data + *l, "%f", f);
			if (ix)
				*l += sprintf(data + *l, ",");
			*l += sprintf(data + *l, "\n");
			break;
		}
	}
}

void	print_tree(FILE *fp, cobj *obj, int level) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = level;
	int64_t	v;

	if (!obj)
		obj = root;
	if (!root)
		return;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}

			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			fprintf(fp, "{\n");
			print_tree(fp, o, level + 1);
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			fprintf(fp, "}");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_ARRAY:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			fprintf(fp, "[\n");
			print_tree(fp, o, level + 1);
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			fprintf(fp, "]");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_TEXT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);

			fprintf(fp, "\"%s\"", o->v);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_BOOLEAN:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);

			fprintf(fp, "%s", strcasecmp(o->v, "true") ? "false" : "true");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_INT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			int	int64_t = 0;
			sscanf(o->v, "%" SCNd64, &v);
			fprintf(fp, "%" PRId64, v);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_FLOAT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			float	f = 0;
			sscanf(o->v, "%f", &f);
			fprintf(fp, "%f", f);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		}
	}
}

cobj	*__obj = NULL;

int	cbJSON(JSONParser *j, int cmd, char *value) {
	char	nm[512];
	int	idx;
	cobj	*o;

	int	isa = parent_isarray(__obj);
	switch (cmd){
	case JP_ARREND:
	case JP_OBJEND:
		if (__obj == root) {
			;//printf("Some kind of error\n");
		}
        	__obj = &root[__obj->parent];
		break;
	case JP_ARRSTART:
	case JP_OBJSTART:
		if (isa)
			o = cobj_nth(__obj, j->indexes[j->name_level - 1]);
		else
			o = cobj_find(__obj, j->names[j->name_level - 1]);
		if (!o) {
			if (!(o = cobj_new()))
				return 1;
			cobj_attach(__obj, o);
			strncpy(o->name, isa ? "" : j->names[j->name_level - 1], sizeof(o->name)); o->name[sizeof(o->name) - 1] = 0;
			o->objtype = cmd == JP_ARRSTART ? CO_ARRAY : CO_OBJECT;
		}
		__obj = o;
		break;
	case JP_NAME_VALUE:
		if (isa)
			o = cobj_nth(__obj, j->index);
		else
			o = cobj_find(__obj, j->vname);
		if (!o) {
			if (!(o = cobj_new()))
				return 1;
			cobj_attach(__obj, o);
			strncpy(o->name, isa ? "" : j->vname, sizeof(o->name)); o->name[sizeof(o->name) - 1] = 0;

			//strncpy(o->name, j->vname, sizeof(o->name));
		}

		if (strncasecmp(o->v, value, sizeof(o->v))) {
			strncpy(o->v, value, sizeof(o->v));
			o->ts = ts_monotonic();
			set_ts(o, o->ts);
		}
		if (j->curtype == 1) {
			o->objtype = CO_TEXT;
		} else if (j->curtype == 2) {
			if (strcasecmp(value, "true") == 0
			|| strcasecmp(value, "false") == 0) {
				o->objtype = CO_BOOLEAN;
			} else {
				float	f;
				if (sscanf(value, "%f", &f) == 1) {
					if (strchr(value, '.')) {
						o->objtype = CO_FLOAT;
					} else {
						o->objtype = CO_INT;
					}
				}
			}
		}
		break;
	}
	return 0;
}

int	cobj_loadjson(const char *fname, cobj *r, char *dname) {
	int				rv = 1;
	const char		*namep = fname, *cp;
	char	 		*p;
	int				l;
	char			name[MAX_NAME_LENGTH];

	for (cp = namep, l = 0; *cp; cp++) {
		if (*cp == '/') {
			namep = cp + 1;
			l = 0;
		} else
			l++;
	}

	strncpy(name, namep, sizeof(name)); name[sizeof(name) - 1] = 0;

	for (p = name; *p; p++) {
		if (*p == '.') {
			*p = 0;
			p++;
		}
	}
	__obj = r;
	rv = LoadJSON(fname, name, cbJSON, NULL);
	if (dname)
		strcpy(dname, name);
	return rv;
}

int	cobj_parsejson(const char *name, cobj *r, const char *data) {
	int				rv = 1;


	__obj = r;
	rv = ParseJSON(data, name, cbJSON, NULL);
	return rv;
}

cobj	*cobj_createobj(cobj *obj, const char *name) {
	char		key[MAX_NAME_LENGTH], *p;
	int			index;
	char		nextc;
	cobj		*o, *onew;

	if (!obj)
		o = root;
	else
		o = obj;
	if (!root)
		return NULL;

	for ( ; ; ) {
		for (p = key; ; name++) {
			nextc = *name;
			if (*name == '.') {
				name++;
				break;
			}
			if (!*name) {
				break;
			}
			if (*name == '[') {
				if (p != key)
					break;
				if (o == root)
					return NULL;
				o->objtype = (o->objtype & 0xff00) | CO_ARRAY;

				for (index = 0, name++; *name >= '0' && *name <= '9'; name++) {
					index = index * 10 + *name - '0';
				}
				if (*name != ']')
					return NULL;
				name++;
				if (index < o->count) {
					if (!(onew = cobj_nth(o, index)))
						return NULL;
					o = onew;
				} else if (index > o->count)
					return NULL;
				else {
					if (!(onew = cobj_new()))
						return NULL;
					cobj_attach(o, onew);
					o = onew;
					if (*name == '.') {
						o->objtype = CO_OBJECT;
					} else if (*name == '[')
						o->objtype = CO_ARRAY;
					else
						o->objtype = CO_TEXT;
					if (!*name)
						return o;
				}
				if (*name == '[')
					name--;
				continue;
			} else if (p - key < MAX_NAME_LENGTH - 1)
				*(p++) = *name;
		}
		*p = 0;
		if ((onew = cobj_findname(o, key))) {
			o = onew;
			int	z = o->objtype & (CO_TEXTF | CO_JSON | CO_CHANGED);
			o->objtype = CO_OBJECT | z;
			if (*name)
				continue;
		} else {
			if (!(onew = cobj_new()))
				return NULL;
			cobj_attach(o, onew);
			o = onew;
			strncpy(o->name, key, sizeof(o->name));
			if (nextc == '.') {
				int	z= o->objtype & (CO_TEXTF | CO_JSON | CO_CHANGED);
				o->objtype = CO_OBJECT;

			} else if (nextc == '[') {
				o->objtype = CO_ARRAY;
			} else
				o->objtype = CO_TEXT;
			if (nextc)
				continue;
		}
		break;
	}
	return o;
}

void	cobj_printname(FILE *fp, cobj *o, int level) {
	if (level && o->parent)
		cobj_printname(fp, &root[o->parent], level - 1);
	if (o->parent) {
		if (root[o->parent].objtype & CO_ARRAY)
			fprintf(fp, "[%d]", o->index);
		else if (o->name[0] && level)
			fprintf(fp, ".");
	}
	if (o->name[0])
		fprintf(fp, "%s", o->name);
}

void	cobj_dumpname(char *data, int *l, cobj *o, int level) {
	if (level && o->parent)
		cobj_dumpname(data, l, &root[o->parent], level - 1);
	if (o->parent) {
		if (root[o->parent].objtype & CO_ARRAY)
			*l += sprintf(data + *l, "[%d]", o->index);
		else if (o->name[0] && level)
			*l += sprintf(data + *l, ".");
	}
	if (o->name[0])
		*l += sprintf(data + *l, "%s", o->name);
}

int	cobj_printtext(FILE *fp, cobj *obj, int level, uint64_t ts) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = 0;
	int64_t	v = 0;
	float	f = 0;

	if (!obj)
		obj = root;
	if (!root)
		return 1;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
		case CO_ARRAY:
			cobj_printtext(fp, o, level+1, ts);
			break;
		case CO_TEXT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_printname(fp, o, level);
				fprintf(fp, "=%s\n", o->v);
			}
			break;
		case CO_BOOLEAN:
			if (o->ts >= ts || o->ts == 0) {
				cobj_printname(fp, o, level);
				fprintf(fp, "=%s\n", strcasecmp(o->v, "true") ? "false" : "true");
			}
			break;
		case CO_INT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_printname(fp, o, level);
				sscanf(o->v, "%" SCNd64, &v);
				fprintf(fp, "=%" PRId64 "\n", v);
			}
			break;
		case CO_FLOAT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_printname(fp, o, level);
				sscanf(o->v, "%f", &f);
				fprintf(fp, "=%f\n", f);
			}
			break;

		}
	}
	return 0;
}

int	cobj_dumptext(char *data, int *l, cobj *obj, int level, uint64_t ts) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = 0;
	int64_t	v = 0;
	float	f = 0;

	if (!obj)
		obj = root;
	if (!root)
		return 1;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
		case CO_ARRAY:
			cobj_dumptext(data, l, o, level+1, ts);
			break;
		case CO_TEXT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_dumpname(data, l, o, level);
				*l += sprintf(data + *l, "=\"%s\"\n", o->v);
			}
			break;
		case CO_BOOLEAN:
			if (o->ts >= ts || o->ts == 0) {
				cobj_dumpname(data, l, o, level);
				*l += sprintf(data + *l, "=%s\n", strcasecmp(o->v, "true") ? "false" : "true");
			}
			break;
		case CO_INT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_dumpname(data, l, o, level);
				sscanf(o->v, "%" SCNd64, &v);
				*l += sprintf(data + *l, "=%" PRId64 "\n", v);
			}
			break;
		case CO_FLOAT:
			if (o->ts >= ts || o->ts == 0) {
				cobj_dumpname(data, l, o, level);
				sscanf(o->v, "%f", &f);
				*l += sprintf(data + *l, "=%f\n", f);
			}
			break;

		}
	}
	return 0;
}

int	cobj_loadtext(const char *fname, char *dname) {
	int	rv = 1;
	char		name[MAX_TEXT_LENGTH];
	const char	*namep = fname, *cp;
	char	 	*p;
	int			l;

	for (cp = namep, l = 0; *cp; cp++) {
		if (*cp == '/') {
			namep = cp + 1;
			l = 0;
		} else
			l++;
	}

	strncpy(name, namep, sizeof(name)); name[sizeof(name) - 1] = 0;

	for (p = name; *p; p++) {
		if (*p == '.') {
			*p = 0;
			p++;
		}
	}

	FILE	*fp = fopen(fname, "rb");
	if (!fp) {
		fprintf(stderr, "LOAD TEXT ERROR : %s, %d\r\n", fname, errno);
		fflush(stderr);
	}
	if (!fp)
		return 3;
	if (!(__obj = cobj_createobj(root, name))) {
		fclose(fp);
		fprintf(stderr, "COBJ CREATEOBJ ERROR : %s\r\n", name);
		fflush(stderr);
		return 1;
	}
	__obj->objtype = CO_OBJECT | CO_TEXTF;
	char	t[MAX_TEXT_LENGTH], *v;
	while (fgets(t, sizeof(t), fp)) {
		for (int l = strlen(t) - 1; l; l--)
			if (t[l] == '\n' || t[l] == '\r')
				t[l] = 0;
			else
				break;
		if ((v = strchr(t, '='))) {
			*(v++) = 0;
		} else
			continue;
		if (!t[0])
			continue;

		char *n = t, *p;
		while ((p = strchr(n, '.'))) {
			n = ++p;
		}
		if (!*n || !*v)
			continue;

		cobj *o = cobj_createobj(__obj, t);
		if (!o)
			break;
		o->objtype = CO_TEXT;
		strcpy(o->name, n);
		if (strcasecmp(o->v, v)) {
			strcpy(o->v, v);
			//o->ts = ts_monotonic();
			set_ts(o, ts_monotonic());
		}
	}
	__obj->objtype = CO_OBJECT | CO_TEXTF;
	fclose(fp);
	if (dname)
		strcpy(dname, name);
	return 0;
}

int	cobj_loadtextto(const char *fname, cobj *o) {
	int			rv = 1;
	char		name[MAX_TEXT_LENGTH];
	const char	*namep = fname, *cp;
	char	 	*p;
	int			l;

	for (cp = namep, l = 0; *cp; cp++) {
		if (*cp == '/') {
			namep = cp + 1;
			l = 0;
		} else
			l++;
	}

	strncpy(name, namep, sizeof(name)); name[sizeof(name) - 1] = 0;

	for (p = name; *p; p++) {
		if (*p == '.') {
			*p = 0;
			p++;
		}
	}

	FILE	*fp = fopen(fname, "rb");
	if (!fp) {
		fprintf(stderr, "LOAD TEXT ERROR : %s, %d\r\n", fname, errno);
		fflush(stderr);
	}
	if (!fp)
		return 3;
	if (!(__obj = cobj_createobj(o, name))) {
		fclose(fp);
		fprintf(stderr, "COBJ CREATEOBJ ERROR : %s\r\n", name);
		fflush(stderr);
		return 1;
	}
	__obj->objtype = CO_OBJECT | CO_TEXTF;
	char	t[MAX_TEXT_LENGTH], *v;
	while (fgets(t, sizeof(t), fp)) {
		for (int l = strlen(t) - 1; l; l--)
			if (t[l] == '\n' || t[l] == '\r')
				t[l] = 0;
			else
				break;
		if ((v = strchr(t, '='))) {
			*(v++) = 0;
		} else
			continue;
		if (!t[0])
			continue;

		char *n = t, *p;
		while ((p = strchr(n, '.'))) {
			n = ++p;
		}
		if (!*n || !*v)
			continue;

		cobj *o = cobj_createobj(__obj, t);
		if (!o)
			break;
		o->objtype = CO_TEXT;
		strcpy(o->name, n);
		if (strcasecmp(o->v, v)) {
			strcpy(o->v, v);
			//o->ts = ts_monotonic();
			set_ts(o, ts_monotonic());
		}
	}
	__obj->objtype = CO_OBJECT | CO_TEXTF;
	fclose(fp);
	return 0;
}

void	cobj_dumpnamenl(char *data, int *l, cobj *o, int level) {
	if (level && o->parent)
		cobj_dumpnamenl(data, l, &root[o->parent], level - 1);
	if (o->parent) {
		if (root[o->parent].objtype & CO_ARRAY)
			*l += sprintf(data + *l, "[%d]", o->index);
		else if (o->name[0] && level)
			*l += sprintf(data + *l, ".");
	}
	if (o->name[0])
		*l += sprintf(data + *l, "%s", o->name);
}

int	cobj_dumpindex(char *data, int *l, cobj *obj, int level, int maxl) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = 0;
	int64_t	v = 0;
	float	f = 0;

	if (maxl <= 128)
		return -1;
	if (!obj)
		obj = root;
	if (!root)
		return 1;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
		case CO_ARRAY:
			cobj_dumpindex(data, l, o, level+1, maxl - *l);
			break;
		case CO_TEXT:
		case CO_BOOLEAN:
		case CO_INT:
		case CO_FLOAT:
			cobj_dumpnamenl(data, l, o, level);
			*l += sprintf(data + *l, "\n");
			break;

		}
	}
	return 0;
}

/*
cobj	*cobj_new_local(cobj *root) {
	cobj	*co = NULL;

	if (!root)
		return NULL;

	if (!root->firstfree)
		return co;

	co = &root[root->firstfree];

	if (co->prev) {
		root[co->prev].next = co->next;
	} else
		root->firstfree = co->next;

	if (co->next) {
		root[co->next].prev = co->prev;
	} else
		root->lastfree = co->prev;
	co->name[0] = co->v[0] = 0;
	co->ts = ts_monotonic();
	co->index = 0;
	return co;
}


void	set_ts_local(cobj *root, cobj *o, uint64_t ts) {
	if (!root)
		return;
	do {
		if (o->objtype & (CO_JSON | CO_TEXTF)) {
			if (o->ts != ts) {
				//saveFile(o);
				o->objtype |= CO_CHANGED;
			}
		}
		o->ts = ts;
		o = &root[o->parent];
	} while (o != root);
	o->ts = ts;
}


cobj	*cobj_nth_local(cobj *root, cobj *obj, int index) {
	int	idx = 0, pos = 0;
	if (!root || !obj)
		return NULL;
	for (idx = obj->first; idx; idx = root[idx].next, pos++) {
		if (pos != root[idx].index) {
			printf("INDEX ERROR!\n");
		}
		if (pos == index)
			return &root[idx];
	}
	return NULL;
}


cobj	*cobj_find_local(cobj *root, cobj *obj, const char *name) {
	char		key[MAX_NAME_LENGTH], *p;
	int		index;
	cobj		*o;

	if (!obj)
		o = root;
	else
		o = obj;
	if (!root)
		return NULL;

	for ( ; ; ) {
		for (p = key; ; name++) {
			if (*name == '.') {
				name++;
				break;
			}
			if (!*name)
				break;
			if (*name == '[') {
				if (p != key)
					break;
				for (index = 0, name++; *name >= '0' && *name <= '9'; name++) {
					index = index * 10 + *name - '0';
				}
				if (*name != ']')
					return NULL;
				if (index >= o->count)
					return NULL;
				if (!(o = cobj_nth_local(root, o, index)))
					return NULL;
				p = key;
				name++;
				if (!*name)
					return o;
				continue;
			} else
				*(p++) = *name;
		}
		*p = 0;
		if ((o = cobj_findname_local(root, o, key))) {
			if (*name)
				continue;
		}
		break;
	}
	return o;
}


void	print_tree_local(FILE *fp, cobj *root, cobj *obj, int level) {
	int	ix;
	cobj	*o, *oobj = obj;
	int	olevel = level;
	int64_t	v;

	if (!obj)
		obj = root;
	if (!root)
		return;


	for (ix = obj->first; ix; ) {
		o = &root[ix];
		ix = o->next;

		switch (o->objtype & 0xfff) {
		case CO_OBJECT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}

			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			fprintf(fp, "{\n");
			print_tree_local(fp, root, o, level + 1);
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			fprintf(fp, "}");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_ARRAY:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			fprintf(fp, "[\n");
			print_tree_local(fp, root, o, level + 1);
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			fprintf(fp, "]");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_TEXT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);

			fprintf(fp, "\"%s\"", o->v);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_BOOLEAN:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);

			fprintf(fp, "%s", strcasecmp(o->v, "true") ? "false" : "true");
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_INT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			int	int64_t = 0;
			sscanf(o->v, "%" SCNd64, &v);
			fprintf(fp, "%" PRId64, v);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		case CO_FLOAT:
			for (int i = 0; i < level; i++) {
				fprintf(fp, "\t");
			}
			if (o->name[0])
				fprintf(fp, "\"%s\" : ", o->name);
			float	f = 0;
			sscanf(o->v, "%f", &f);
			fprintf(fp, "%f", f);
			if (ix)
				fprintf(fp, ",");
			fprintf(fp, "\n");
			break;
		}
	}
}

void	cobj_free_local(cobj *root, cobj *co) {
	cobj	*p;
	int	n;

	if (!root || !co)
		return;

	p = &root[co->parent];
	p->count--;

	for (n = co->next; n; n = root[n].next) {
		root[n].index--;
	}
	n = co->next;

	if (co->next) {
		root[co->next].prev = co->prev;
	} else if (p) {
		p->last = co->prev;
	}
	if (co->prev) {
		root[co->prev].next = co->next;
	} else if (p) {
		p->first = co->next;
	}

	while (co->first) {
		cobj_free_local(root, &root[co->first]);
	}
	co->objtype = 0;
	co->count = 0;
	co->index = 0;
	co->parent = co->first = co->last = 0;

	co->prev = root->lastfree;
	co->next = 0;
	if (root->lastfree) {
		root[root->lastfree].next = co->idx;
	}
	root->lastfree = co->idx;
	if (!root->firstfree)
		root->firstfree = co->idx;
	root->count++;
	if (p) {
		p->ts = ts_monotonic();
		if (n)
			for (; n; n = root[n].next) {
				root[n].ts = p->ts;
				set_ts_local(root, &root[n], p->ts);
			}
		//else
		set_ts_local(root, p, p->ts);
		if (p->objtype & (CO_JSON | CO_TEXTF))
			p->objtype |= CO_CHANGED;
	}
}
*/