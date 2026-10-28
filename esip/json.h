#ifndef	json_h
#define	json_h

#ifdef	json_c
#else
#endif

typedef		void (*json_callback)(char *name, char *value);

char	*_GetJSONTok(json_callback cb, char *nm, char *l, int isarray);

int	json_name_match(char *t, char *f);
int	json_name_scanf(char *text, char *format, ...);

#endif