#define L_BRACKET 257
#define R_BRACKET 258
#define WORD 259
#define P_RUNNER 260
#define STRING_CONSTANT 261
#define TRANSCRASH 262
#define SEMICOLON 263
#define COMMA 264
#define L_ASSIGN 265
#define L_APPEND 266
#define L_SYMLINK 267
#define PLUS 268
#define SLASH 269
#define INV_L 270
#define INV_R 271
#define EQ 272
#define NE 273
#define LE 274
#define GE 275
#define LT 276
#define GT 277
#define IS 278
#define AND 279
#define OR 280
#define NOT 281
#define IF 282
#define THEN 283
#define ELSE 284
#define EXIST 285
#define NAME 286
#define UNNAME 287
#define NAMED 288
#define ROOT 289
typedef union
{
	long charType;
	expr_v4_t * expr;
	wrd_t * wrd;
} YYSTYPE;
extern YYSTYPE yylval;
