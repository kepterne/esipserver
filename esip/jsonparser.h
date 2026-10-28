#ifndef	jsonparser_h
#define	jsonparser_h

#include	<stdint.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>

#define	MAX_LEX_STACK	64
#define	MAX_NAME_LEVEL	64

typedef	struct JSONParser_st {
	char	name[32];
	char	names[MAX_NAME_LEVEL][128];
	int		indexes[MAX_NAME_LEVEL];
	int		(*cb)(struct JSONParser_st *j, int cmd, char *value);

#define		JP_NAME_VALUE		01
#define		JP_OBJEND			02
#define		JP_ARREND			03
#define		JP_JSONEND			04

#define		JP_OBJSTART			05
#define		JP_ARRSTART			06

	int		index;
	int		name_level;
	uint32_t	lexstate;
	uint32_t	lex_sp;
	uint32_t	lex_stack[MAX_LEX_STACK];
	uint32_t	colpos, rowpos;
	char	vname[128];
	char	value[512];
	int		valuepos;
	int		namepos;
	void		*extra;
	int		curtype;
	
} JSONParser;

typedef	struct {
	int	type;
#define	SINGLE_VALUE	1
	char	*key;
	char	*format;
	char	*value;
	int	*change_flag;
	char	oldvalue[128];
	int	idx;
} json_keys;

int	PrintName(JSONParser *j, char *n);
int	processJSON(JSONParser *j, const char *d, int l);
#define	JSON_ENDED			1
#define	JSON_CONTINUE		0
JSONParser	*startJSON(const char *name);
JSONParser	*restartJSON(const char *name, JSONParser	*jsp);

int	LoadJSON(const char *filename, char *name, int	(*cb)(struct JSONParser_st *j, int cmd, char *value), void *extra);
void	GetJsonData(char *filename, char *name, json_keys *jk);
int	ParseJSON(const char *data, const char *name, int	(*cb)(struct JSONParser_st *j, int cmd, char *value), void *extra);

json_keys	*find_jk(char *nm, json_keys *jk);

#endif