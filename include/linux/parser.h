struct match_token {
	int token;
	char *pattern;
};

typedef struct match_token match_table_t[];

enum {MAX_OPT_ARGS = 3};

typedef struct {
	char *from;
	char *to;
} substring_t;

int match_token(char *s, match_table_t table, substring_t args[]);

int match_int(substring_t *, int *result);
int match_octal(substring_t *, int *result);
int match_hex(substring_t *, int *result);
void match_strcpy(char *, substring_t *);
char *match_strdup(substring_t *);
