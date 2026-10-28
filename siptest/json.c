#define	json_c

#include	<stdio.h>
#include	<string.h>
#include	<stdarg.h>
#include	<ctype.h>

#include	"json.h"


char	*_SkipSpace(char *l) {
	while (*l == 13 || *l == 10 || *l == 9 || *l == 32
	// || *l == ','
	)
		l++;
	return l;
}

char	*_SkipStr(char *s) {
	while (*s) {
		if (*s == '\"') {
			return ++s;
		} else if (*s == '\\') {
			s++;
			if (*s)
				s++;
			continue;
		}
		s++;
	}
	return s;
}

char	*_ParseStr(char *d, char *s) {
	while (*s) {
		if (*s == '\"') {
			*d = 0;
			return ++s;
		} else if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n': *(d++) = '\n'; break;
			case 'r': *(d++) = '\r'; break;
			case 't': *(d++) = '\t'; break;
			default: *(d++) = '\n'; break;
			}
			if (*s)
				s++;
			continue;
		}
		*(d++) = *(s++);
	}
	return s;
}

char	*_ParseStr2(char *d, char *s) {
	while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')) {
		*(d++) = *(s++);
	}
	*d = 0;
	return ++s;
}
char	*_ParseInt(char *d, char *s) {
	
	if (*s == '-')
		*(d++) = *(s++);
	while (*s) {
		if (*s < '0' || *s > '9') {
			*d = 0;
			break;
		} 
		*(d++) = *(s++);
	}
	if (*s != '.') 
		return s;
	*(d++) = *(s++);
	while (*s) {
		if (*s < '0' || *s > '9') {
			*d = 0;
			break;
		} 
		*(d++) = *(s++);
	}
	return s;
}


char	*_GetJSONTok(json_callback cb, char *nm, char *l, int isarray) {
	char	Name[256];
	char	*name,  
		*value;
	char	ival[256] = "";
l_start:
	name = NULL;
	value = NULL;
//l0:
	strcpy(Name,  nm);
	if (isarray) {
		sprintf(Name+strlen(Name), "[%03d]", isarray-1);
	}
	l = _SkipSpace(l);
	switch (*l) {
	case '[':
		l++;
		l = _GetJSONTok(cb, Name, l, 1);
		goto l_start;
	case ',':
		l++;
		if (isarray)
			isarray++;
		goto l_start;
	case 0:
		(*cb)(".", NULL);
		return l;
	case '}': case ']':
		if (cb) {
			(*cb)(Name, ".");
		}
		return ++l;
	case '\"':
		name = ++l;
		l = _ParseStr(name, l);
		l = _SkipSpace(l);
		if (*l == ':') {
			l++;
		}

		break;
	case '{':
		*(l++) = ' ';
		strcat(Name, ".");
		l = _GetJSONTok(cb, Name, l, 0);
			
		//l = _ParsePar(l);
		goto l_start;
	default:
		if (isarray) 
			goto l_2;
		name = l;
		while (*l) {
			if (*l == ':' || *l == 32 || *l == 13 || *l == 10 || *l == 9)
				break;
			l++;
		}

		if (*l != ':') {
			*l = 0;
			l = _SkipSpace(l+1);
		}
		if (*l != ':')
			if (isarray) {
				goto l_2;
			}
			return l;
		*l = 0;
		l++;
	}
	l = _SkipSpace(l);
l_2:
	switch (*l) {
		case 0:
			return l;
		case '\"':
			value = ++l;
			l = _ParseStr(value, l);
			if (cb) {
				strcat(Name, name);
				(*cb)(Name, value);
			}
			goto l_start;
		case '{':
			value = l;
			//l = _ParsePar2(l+1);
			l++;
			strcat(Name, name);
			if (cb) {
				(*cb)(Name, ".");
			}
			strcat(Name, ".");
			l = _GetJSONTok(cb, Name, l, 0);
			
			goto l_start;
		case '[':
			//value = l;
			//l = _ParsePar2(l+1);
			l++;
			strcat(Name, name);
			l = _GetJSONTok(cb, Name, l, 1);
			
			goto l_start;
		default:
			value = ival;
			if ((*l >= 'a' && *l <= 'z') || (*l >= 'A' && *l <= 'Z') ) {
				l = _ParseStr2(value, l);
				if (cb) {
					strcat(Name, name);
					(*cb)(Name, value);
				}
				goto l_start;
			}
			l = _ParseInt(value, l);
			if (cb) {
				if (name)
				 	strcat(Name, name);
				(*cb)(Name, value);
			}
			goto l_start;
	}
	return l;
}


int	json_name_match(char *t, char *f) {
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

int	json_name_scanf(char *text, char *format, ...) {
	va_list		ap;
    int			result = 0;
	if (json_name_match(text, format)) {
		va_start(ap, format);
		result = vsscanf(text, format, ap);
		va_end(ap);
	}
	return result;
}