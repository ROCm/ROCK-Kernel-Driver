/* A Bison parser, made from zconf.y
   by GNU bison 1.35.  */

#define YYBISON 1  /* Identify Bison output.  */

#define yyparse zconfparse
#define yylex zconflex
#define yyerror zconferror
#define yylval zconflval
#define yychar zconfchar
#define yydebug zconfdebug
#define yynerrs zconfnerrs
# define	T_MAINMENU	257
# define	T_MENU	258
# define	T_ENDMENU	259
# define	T_SOURCE	260
# define	T_CHOICE	261
# define	T_ENDCHOICE	262
# define	T_COMMENT	263
# define	T_CONFIG	264
# define	T_HELP	265
# define	T_HELPTEXT	266
# define	T_IF	267
# define	T_ENDIF	268
# define	T_DEPENDS	269
# define	T_REQUIRES	270
# define	T_OPTIONAL	271
# define	T_PROMPT	272
# define	T_DEFAULT	273
# define	T_TRISTATE	274
# define	T_BOOLEAN	275
# define	T_INT	276
# define	T_HEX	277
# define	T_WORD	278
# define	T_STRING	279
# define	T_UNEQUAL	280
# define	T_EOF	281
# define	T_EOL	282
# define	T_CLOSE_PAREN	283
# define	T_OPEN_PAREN	284
# define	T_ON	285
# define	T_OR	286
# define	T_AND	287
# define	T_EQUAL	288
# define	T_NOT	289

#line 1 "zconf.y"

/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(int token, int starttoken, int endtoken);

struct symbol *symbol_hash[257];

#define YYERROR_VERBOSE

#line 32 "zconf.y"
#ifndef YYSTYPE
typedef union
{
	int token;
	char *string;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
} yystype;
# define YYSTYPE yystype
# define YYSTYPE_IS_TRIVIAL 1
#endif
#line 83 "zconf.y"

#define LKC_DIRECT_LINK
#include "lkc.h"
#ifndef YYDEBUG
# define YYDEBUG 1
#endif



#define	YYFINAL		145
#define	YYFLAG		-32768
#define	YYNTBASE	36

/* YYTRANSLATE(YYLEX) -- Bison token number corresponding to YYLEX. */
#define YYTRANSLATE(x) ((unsigned)(x) <= 289 ? yytranslate[x] : 74)

/* YYTRANSLATE[YYLEX] -- Bison token number corresponding to YYLEX. */
static const char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35
};

#if YYDEBUG
static const short yyprhs[] =
{
       0,     0,     1,     4,     6,     8,    10,    14,    16,    18,
      20,    23,    25,    27,    29,    31,    33,    36,    40,    41,
      45,    49,    52,    55,    58,    61,    64,    67,    70,    74,
      78,    80,    84,    86,    91,    94,    95,    99,   103,   106,
     109,   113,   115,   118,   119,   122,   125,   127,   133,   137,
     138,   141,   144,   147,   150,   154,   156,   161,   164,   165,
     168,   171,   174,   178,   181,   184,   187,   191,   194,   197,
     198,   202,   205,   209,   212,   215,   216,   218,   222,   224,
     226,   228,   230,   232,   234,   236,   237,   240,   242,   246,
     250,   254,   257,   261,   265,   267
};
static const short yyrhs[] =
{
      -1,    36,    37,     0,    38,     0,    46,     0,    57,     0,
       3,    68,    70,     0,     5,     0,    14,     0,     8,     0,
       1,    70,     0,    52,     0,    62,     0,    40,     0,    60,
       0,    70,     0,    10,    24,     0,    39,    28,    41,     0,
       0,    41,    42,    28,     0,    41,    66,    28,     0,    41,
      64,     0,    41,    28,     0,    20,    67,     0,    21,    67,
       0,    22,    67,     0,    23,    67,     0,    25,    67,     0,
      18,    68,    71,     0,    19,    73,    71,     0,     7,     0,
      43,    28,    47,     0,    69,     0,    44,    49,    45,    28,
       0,    44,    49,     0,     0,    47,    48,    28,     0,    47,
      66,    28,     0,    47,    64,     0,    47,    28,     0,    18,
      68,    71,     0,    17,     0,    19,    73,     0,     0,    49,
      38,     0,    13,    72,     0,    69,     0,    50,    28,    53,
      51,    28,     0,    50,    28,    53,     0,     0,    53,    38,
       0,    53,    57,     0,    53,    46,     0,     4,    68,     0,
      54,    28,    65,     0,    69,     0,    55,    58,    56,    28,
       0,    55,    58,     0,     0,    58,    38,     0,    58,    57,
       0,    58,    46,     0,    58,     1,    28,     0,     6,    68,
       0,    59,    28,     0,     9,    68,     0,    61,    28,    65,
       0,    11,    28,     0,    63,    12,     0,     0,    65,    66,
      28,     0,    65,    28,     0,    15,    31,    72,     0,    15,
      72,     0,    16,    72,     0,     0,    68,     0,    68,    13,
      72,     0,    24,     0,    25,     0,     5,     0,     8,     0,
      14,     0,    28,     0,    27,     0,     0,    13,    72,     0,
      73,     0,    73,    34,    73,     0,    73,    26,    73,     0,
      30,    72,    29,     0,    35,    72,     0,    72,    32,    72,
       0,    72,    33,    72,     0,    24,     0,    25,     0
};

#endif

#if YYDEBUG
/* YYRLINE[YYN] -- source line where rule number YYN was defined. */
static const short yyrline[] =
{
       0,    88,    89,    92,    93,    94,    95,    96,    97,    98,
      99,   102,   104,   105,   106,   107,   113,   121,   127,   129,
     130,   131,   132,   135,   141,   147,   153,   159,   165,   171,
     179,   188,   194,   202,   204,   210,   212,   213,   214,   215,
     218,   224,   230,   237,   239,   244,   254,   262,   264,   270,
     272,   273,   274,   279,   286,   292,   300,   302,   308,   310,
     311,   312,   313,   316,   322,   329,   336,   343,   349,   356,
     357,   358,   361,   366,   371,   379,   381,   385,   390,   391,
     394,   395,   396,   399,   400,   402,   403,   406,   407,   408,
     409,   410,   411,   412,   415,   416
};
#endif


#if (YYDEBUG) || defined YYERROR_VERBOSE

/* YYTNAME[TOKEN_NUM] -- String name of the token TOKEN_NUM. */
static const char *const yytname[] =
{
  "$", "error", "$undefined.", "T_MAINMENU", "T_MENU", "T_ENDMENU", 
  "T_SOURCE", "T_CHOICE", "T_ENDCHOICE", "T_COMMENT", "T_CONFIG", 
  "T_HELP", "T_HELPTEXT", "T_IF", "T_ENDIF", "T_DEPENDS", "T_REQUIRES", 
  "T_OPTIONAL", "T_PROMPT", "T_DEFAULT", "T_TRISTATE", "T_BOOLEAN", 
  "T_INT", "T_HEX", "T_WORD", "T_STRING", "T_UNEQUAL", "T_EOF", "T_EOL", 
  "T_CLOSE_PAREN", "T_OPEN_PAREN", "T_ON", "T_OR", "T_AND", "T_EQUAL", 
  "T_NOT", "input", "block", "common_block", "config_entry_start", 
  "config_stmt", "config_option_list", "config_option", "choice", 
  "choice_entry", "choice_end", "choice_stmt", "choice_option_list", 
  "choice_option", "choice_block", "if", "if_end", "if_stmt", "if_block", 
  "menu", "menu_entry", "menu_end", "menu_stmt", "menu_block", "source", 
  "source_stmt", "comment", "comment_stmt", "help_start", "help", 
  "depends_list", "depends", "prompt_stmt_opt", "prompt", "end", 
  "nl_or_eof", "if_expr", "expr", "symbol", 0
};
#endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives. */
static const short yyr1[] =
{
       0,    36,    36,    37,    37,    37,    37,    37,    37,    37,
      37,    38,    38,    38,    38,    38,    39,    40,    41,    41,
      41,    41,    41,    42,    42,    42,    42,    42,    42,    42,
      43,    44,    45,    46,    46,    47,    47,    47,    47,    47,
      48,    48,    48,    49,    49,    50,    51,    52,    52,    53,
      53,    53,    53,    54,    55,    56,    57,    57,    58,    58,
      58,    58,    58,    59,    60,    61,    62,    63,    64,    65,
      65,    65,    66,    66,    66,    67,    67,    67,    68,    68,
      69,    69,    69,    70,    70,    71,    71,    72,    72,    72,
      72,    72,    72,    72,    73,    73
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN. */
static const short yyr2[] =
{
       0,     0,     2,     1,     1,     1,     3,     1,     1,     1,
       2,     1,     1,     1,     1,     1,     2,     3,     0,     3,
       3,     2,     2,     2,     2,     2,     2,     2,     3,     3,
       1,     3,     1,     4,     2,     0,     3,     3,     2,     2,
       3,     1,     2,     0,     2,     2,     1,     5,     3,     0,
       2,     2,     2,     2,     3,     1,     4,     2,     0,     2,
       2,     2,     3,     2,     2,     2,     3,     2,     2,     0,
       3,     2,     3,     2,     2,     0,     1,     3,     1,     1,
       1,     1,     1,     1,     1,     0,     2,     1,     3,     3,
       3,     2,     3,     3,     1,     1
};

/* YYDEFACT[S] -- default rule to reduce with in state S when YYTABLE
   doesn't specify something else to do.  Zero means the default is an
   error. */
static const short yydefact[] =
{
       1,     0,     0,     0,     0,     7,     0,    30,     9,     0,
       0,     0,     8,    84,    83,     2,     3,     0,    13,     0,
      43,     4,     0,    11,     0,    58,     5,     0,    14,     0,
      12,    15,    10,    78,    79,     0,    53,    63,    65,    16,
      94,    95,     0,     0,    45,    87,    18,    35,    34,    49,
      69,     0,    64,    69,     6,     0,    91,     0,     0,     0,
       0,    17,    31,    80,    81,    82,    44,     0,    32,    48,
      54,     0,    59,    61,     0,    60,    55,    66,    90,    92,
      93,    89,    88,     0,     0,     0,     0,     0,    75,    75,
      75,    75,    75,    22,     0,     0,    21,     0,    41,     0,
       0,    39,     0,    38,     0,    33,    50,    52,     0,    51,
      46,    71,     0,    62,    56,    67,     0,    73,    74,    85,
      85,    23,    76,    24,    25,    26,    27,    19,    68,    20,
      85,    42,    36,    37,    47,    70,    72,     0,    28,    29,
       0,    40,    86,    77,     0,     0
};

static const short yydefgoto[] =
{
       1,    15,    16,    17,    18,    61,    94,    19,    20,    67,
      21,    62,   102,    48,    22,   108,    23,    69,    24,    25,
      74,    26,    51,    27,    28,    29,    30,    95,    96,    70,
     112,   121,   122,    68,    31,   138,    44,    45
};

static const short yypact[] =
{
  -32768,    17,    41,    65,    65,-32768,    65,-32768,-32768,    65,
     -11,    40,-32768,-32768,-32768,-32768,-32768,    13,-32768,    23,
  -32768,-32768,    66,-32768,    72,-32768,-32768,    77,-32768,    81,
  -32768,-32768,-32768,-32768,-32768,    41,-32768,-32768,-32768,-32768,
  -32768,-32768,    40,    40,    64,    59,-32768,-32768,    98,-32768,
  -32768,    49,-32768,-32768,-32768,     7,-32768,    40,    40,    67,
      67,    99,   117,-32768,-32768,-32768,-32768,    85,-32768,    74,
      18,    88,-32768,-32768,    95,-32768,-32768,    18,-32768,    96,
  -32768,-32768,-32768,   102,    36,    40,    65,    67,    65,    65,
      65,    65,    65,-32768,   103,   129,-32768,   114,-32768,    65,
      67,-32768,   115,-32768,   116,-32768,-32768,-32768,   118,-32768,
  -32768,-32768,   119,-32768,-32768,-32768,    40,    64,    64,   135,
     135,-32768,   136,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
     135,-32768,-32768,-32768,-32768,-32768,    64,    40,-32768,-32768,
      40,-32768,    64,    64,   150,-32768
};

static const short yypgoto[] =
{
  -32768,-32768,   -37,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
     -41,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
  -32768,   -32,-32768,-32768,-32768,-32768,-32768,-32768,    89,   100,
      11,    48,     0,   -22,     3,  -118,   -42,   -52
};


#define	YYLAST		153


static const short yytable[] =
{
      55,    56,   139,    35,    36,    32,    37,    81,    82,    38,
      73,    66,   141,    39,    72,    79,    80,   144,     2,    75,
       3,     4,     5,     6,     7,     8,     9,    10,   107,    76,
      11,    12,   106,    84,    85,   120,    78,   109,    54,    57,
      58,    46,   117,   118,    13,    14,   111,   110,   131,   -57,
      71,    47,   -57,     4,    63,     6,     7,    64,     9,    10,
      40,    41,    11,    65,    40,    41,    42,   116,    13,    14,
      42,    43,    97,   104,   136,    43,    13,    14,     4,    63,
       6,     7,    64,     9,    10,    59,   119,    11,    65,    33,
      34,    40,    41,    60,    49,   142,    57,    58,   143,   130,
      50,    13,    14,    63,     6,    52,    64,     9,    10,    53,
      83,    11,    65,   105,    84,    85,   113,    86,    87,    88,
      89,    90,    91,   114,    92,    13,    14,    93,    83,    58,
     115,   127,    84,    85,    98,    99,   100,   123,   124,   125,
     126,   128,   129,   132,   133,   101,   134,   135,   137,   140,
     145,   103,     0,    77
};

static const short yycheck[] =
{
      42,    43,   120,     3,     4,     2,     6,    59,    60,     9,
      51,    48,   130,    24,    51,    57,    58,     0,     1,    51,
       3,     4,     5,     6,     7,     8,     9,    10,    69,    51,
      13,    14,    69,    15,    16,    87,    29,    69,    35,    32,
      33,    28,    84,    85,    27,    28,    28,    69,   100,     0,
       1,    28,     3,     4,     5,     6,     7,     8,     9,    10,
      24,    25,    13,    14,    24,    25,    30,    31,    27,    28,
      30,    35,    61,    62,   116,    35,    27,    28,     4,     5,
       6,     7,     8,     9,    10,    26,    86,    13,    14,    24,
      25,    24,    25,    34,    28,   137,    32,    33,   140,    99,
      28,    27,    28,     5,     6,    28,     8,     9,    10,    28,
      11,    13,    14,    28,    15,    16,    28,    18,    19,    20,
      21,    22,    23,    28,    25,    27,    28,    28,    11,    33,
      28,    28,    15,    16,    17,    18,    19,    89,    90,    91,
      92,    12,    28,    28,    28,    28,    28,    28,    13,    13,
       0,    62,    -1,    53
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/share/bison/bison.simple"

/* Skeleton output parser for bison,

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software
   Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser when
   the %semantic_parser declaration is not specified in the grammar.
   It was written by Richard Stallman by simplifying the hairy parser
   used when %semantic_parser is specified.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

#if ! defined (yyoverflow) || defined (YYERROR_VERBOSE)

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || defined (YYERROR_VERBOSE) */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYLTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
# if YYLSP_NEEDED
  YYLTYPE yyls;
# endif
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAX (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# if YYLSP_NEEDED
#  define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAX)
# else
#  define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAX)
# endif

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAX;	\
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif


#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");			\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).

   When YYLLOC_DEFAULT is run, CURRENT is set the location of the
   first token.  By default, to implement support for ranges, extend
   its range to the last symbol.  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)       	\
   Current.last_line   = Rhs[N].last_line;	\
   Current.last_column = Rhs[N].last_column;
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#if YYPURE
# if YYLSP_NEEDED
#  ifdef YYLEX_PARAM
#   define YYLEX		yylex (&yylval, &yylloc, YYLEX_PARAM)
#  else
#   define YYLEX		yylex (&yylval, &yylloc)
#  endif
# else /* !YYLSP_NEEDED */
#  ifdef YYLEX_PARAM
#   define YYLEX		yylex (&yylval, YYLEX_PARAM)
#  else
#   define YYLEX		yylex (&yylval)
#  endif
# endif /* !YYLSP_NEEDED */
#else /* !YYPURE */
# define YYLEX			yylex ()
#endif /* !YYPURE */


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)
/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
#endif /* !YYDEBUG */

/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif

#ifdef YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif
#endif

#line 315 "/usr/share/bison/bison.simple"


/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
#  define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#  define YYPARSE_PARAM_DECL
# else
#  define YYPARSE_PARAM_ARG YYPARSE_PARAM
#  define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
# endif
#else /* !YYPARSE_PARAM */
# define YYPARSE_PARAM_ARG
# define YYPARSE_PARAM_DECL
#endif /* !YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
# ifdef YYPARSE_PARAM
int yyparse (void *);
# else
int yyparse (void);
# endif
#endif

/* YY_DECL_VARIABLES -- depending whether we use a pure parser,
   variables are global, or local to YYPARSE.  */

#define YY_DECL_NON_LSP_VARIABLES			\
/* The lookahead symbol.  */				\
int yychar;						\
							\
/* The semantic value of the lookahead symbol. */	\
YYSTYPE yylval;						\
							\
/* Number of parse errors so far.  */			\
int yynerrs;

#if YYLSP_NEEDED
# define YY_DECL_VARIABLES			\
YY_DECL_NON_LSP_VARIABLES			\
						\
/* Location data for the lookahead symbol.  */	\
YYLTYPE yylloc;
#else
# define YY_DECL_VARIABLES			\
YY_DECL_NON_LSP_VARIABLES
#endif


/* If nonreentrant, generate the variables here. */

#if !YYPURE
YY_DECL_VARIABLES
#endif  /* !YYPURE */

int
yyparse (YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  /* If reentrant, generate the variables here. */
#if YYPURE
  YY_DECL_VARIABLES
#endif  /* !YYPURE */

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yychar1 = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack. */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;

#if YYLSP_NEEDED
  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
#endif

#if YYLSP_NEEDED
# define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
# define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  YYSIZE_T yystacksize = YYINITDEPTH;


  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
#if YYLSP_NEEDED
  YYLTYPE yyloc;
#endif

  /* When reducing, the number of symbols on the RHS of the reduced
     rule. */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
#if YYLSP_NEEDED
  yylsp = yyls;
#endif
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  */
# if YYLSP_NEEDED
	YYLTYPE *yyls1 = yyls;
	/* This used to be a conditional around just the two extra args,
	   but that might be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
# else
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yystacksize);
# endif
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
# if YYLSP_NEEDED
	YYSTACK_RELOCATE (yyls);
# endif
# undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
#if YYLSP_NEEDED
      yylsp = yyls + yysize - 1;
#endif

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yychar1 = YYTRANSLATE (yychar);

#if YYDEBUG
     /* We have to keep this `#if YYDEBUG', since we use variables
	which are defined only if `YYDEBUG' is set.  */
      if (yydebug)
	{
	  YYFPRINTF (stderr, "Next token is %d (%s",
		     yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise
	     meaning of a token, for further debugging info.  */
# ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
# endif
	  YYFPRINTF (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %d (%s), ",
	      yychar, yytname[yychar1]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#if YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to the semantic value of
     the lookahead token.  This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

#if YYLSP_NEEDED
  /* Similarly for the default location.  Let the user run additional
     commands if for instance locations are ranges.  */
  yyloc = yylsp[1-yylen];
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
#endif

#if YYDEBUG
  /* We have to keep this `#if YYDEBUG', since we use variables which
     are defined only if `YYDEBUG' is set.  */
  if (yydebug)
    {
      int yyi;

      YYFPRINTF (stderr, "Reducing via rule %d (line %d), ",
		 yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (yyi = yyprhs[yyn]; yyrhs[yyi] > 0; yyi++)
	YYFPRINTF (stderr, "%s ", yytname[yyrhs[yyi]]);
      YYFPRINTF (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif

  switch (yyn) {

case 7:
#line 96 "zconf.y"
{ zconfprint("unexpected 'endmenu' statement"); ;
    break;}
case 8:
#line 97 "zconf.y"
{ zconfprint("unexpected 'endif' statement"); ;
    break;}
case 9:
#line 98 "zconf.y"
{ zconfprint("unexpected 'endchoice' statement"); ;
    break;}
case 10:
#line 99 "zconf.y"
{ zconfprint("syntax error"); yyerrok; ;
    break;}
case 16:
#line 114 "zconf.y"
{
	struct symbol *sym = sym_lookup(yyvsp[0].string, 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", zconf_curname(), zconf_lineno(), yyvsp[0].string);
;
    break;}
case 17:
#line 122 "zconf.y"
{
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;
    break;}
case 22:
#line 133 "zconf.y"
{ ;
    break;}
case 23:
#line 136 "zconf.y"
{
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:tristate\n", zconf_curname(), zconf_lineno());
;
    break;}
case 24:
#line 142 "zconf.y"
{
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:boolean\n", zconf_curname(), zconf_lineno());
;
    break;}
case 25:
#line 148 "zconf.y"
{
	menu_set_type(S_INT);
	printd(DEBUG_PARSE, "%s:%d:int\n", zconf_curname(), zconf_lineno());
;
    break;}
case 26:
#line 154 "zconf.y"
{
	menu_set_type(S_HEX);
	printd(DEBUG_PARSE, "%s:%d:hex\n", zconf_curname(), zconf_lineno());
;
    break;}
case 27:
#line 160 "zconf.y"
{
	menu_set_type(S_STRING);
	printd(DEBUG_PARSE, "%s:%d:string\n", zconf_curname(), zconf_lineno());
;
    break;}
case 28:
#line 166 "zconf.y"
{
	menu_add_prop(P_PROMPT, yyvsp[-1].string, NULL, yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;
    break;}
case 29:
#line 172 "zconf.y"
{
	menu_add_prop(P_DEFAULT, NULL, yyvsp[-1].symbol, yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
;
    break;}
case 30:
#line 180 "zconf.y"
{
	struct symbol *sym = sym_lookup(NULL, 0);
	sym->flags |= SYMBOL_CHOICE;
	menu_add_entry(sym);
	menu_add_prop(P_CHOICE, NULL, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", zconf_curname(), zconf_lineno());
;
    break;}
case 31:
#line 189 "zconf.y"
{
	menu_end_entry();
	menu_add_menu();
;
    break;}
case 32:
#line 195 "zconf.y"
{
	if (zconf_endtoken(yyvsp[0].token, T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
;
    break;}
case 34:
#line 205 "zconf.y"
{
	printf("%s:%d: missing 'endchoice' for this 'choice' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;
    break;}
case 40:
#line 219 "zconf.y"
{
	menu_add_prop(P_PROMPT, yyvsp[-1].string, NULL, yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;
    break;}
case 41:
#line 225 "zconf.y"
{
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
;
    break;}
case 42:
#line 231 "zconf.y"
{
	menu_add_prop(P_DEFAULT, NULL, yyvsp[0].symbol, NULL);
	//current_choice->prop->def = $2;
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
;
    break;}
case 45:
#line 245 "zconf.y"
{
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	//current_entry->prompt = menu_add_prop(T_IF, NULL, NULL, $2);
	menu_add_dep(yyvsp[0].expr);
	menu_end_entry();
	menu_add_menu();
;
    break;}
case 46:
#line 255 "zconf.y"
{
	if (zconf_endtoken(yyvsp[0].token, T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
;
    break;}
case 48:
#line 265 "zconf.y"
{
	printf("%s:%d: missing 'endif' for this 'if' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;
    break;}
case 53:
#line 280 "zconf.y"
{
	menu_add_entry(NULL);
	menu_add_prop(P_MENU, yyvsp[0].string, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
;
    break;}
case 54:
#line 287 "zconf.y"
{
	menu_end_entry();
	menu_add_menu();
;
    break;}
case 55:
#line 293 "zconf.y"
{
	if (zconf_endtoken(yyvsp[0].token, T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
;
    break;}
case 57:
#line 303 "zconf.y"
{
	printf("%s:%d: missing 'endmenu' for this 'menu' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;
    break;}
case 62:
#line 313 "zconf.y"
{ zconfprint("invalid menu option"); yyerrok; ;
    break;}
case 63:
#line 317 "zconf.y"
{
	yyval.string = yyvsp[0].string;
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), yyvsp[0].string);
;
    break;}
case 64:
#line 323 "zconf.y"
{
	zconf_nextfile(yyvsp[-1].string);
;
    break;}
case 65:
#line 330 "zconf.y"
{
	menu_add_entry(NULL);
	menu_add_prop(P_COMMENT, yyvsp[0].string, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", zconf_curname(), zconf_lineno());
;
    break;}
case 66:
#line 337 "zconf.y"
{
	menu_end_entry();
;
    break;}
case 67:
#line 344 "zconf.y"
{
	printd(DEBUG_PARSE, "%s:%d:help\n", zconf_curname(), zconf_lineno());
	zconf_starthelp();
;
    break;}
case 68:
#line 350 "zconf.y"
{
	current_entry->sym->help = yyvsp[0].string;
;
    break;}
case 71:
#line 359 "zconf.y"
{ ;
    break;}
case 72:
#line 362 "zconf.y"
{
	menu_add_dep(yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
;
    break;}
case 73:
#line 367 "zconf.y"
{
	menu_add_dep(yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:depends\n", zconf_curname(), zconf_lineno());
;
    break;}
case 74:
#line 372 "zconf.y"
{
	menu_add_dep(yyvsp[0].expr);
	printd(DEBUG_PARSE, "%s:%d:requires\n", zconf_curname(), zconf_lineno());
;
    break;}
case 76:
#line 382 "zconf.y"
{
	menu_add_prop(P_PROMPT, yyvsp[0].string, NULL, NULL);
;
    break;}
case 77:
#line 386 "zconf.y"
{
	menu_add_prop(P_PROMPT, yyvsp[-2].string, NULL, yyvsp[0].expr);
;
    break;}
case 80:
#line 394 "zconf.y"
{ yyval.token = T_ENDMENU; ;
    break;}
case 81:
#line 395 "zconf.y"
{ yyval.token = T_ENDCHOICE; ;
    break;}
case 82:
#line 396 "zconf.y"
{ yyval.token = T_ENDIF; ;
    break;}
case 85:
#line 402 "zconf.y"
{ yyval.expr = NULL; ;
    break;}
case 86:
#line 403 "zconf.y"
{ yyval.expr = yyvsp[0].expr; ;
    break;}
case 87:
#line 406 "zconf.y"
{ yyval.expr = expr_alloc_symbol(yyvsp[0].symbol); ;
    break;}
case 88:
#line 407 "zconf.y"
{ yyval.expr = expr_alloc_comp(E_EQUAL, yyvsp[-2].symbol, yyvsp[0].symbol); ;
    break;}
case 89:
#line 408 "zconf.y"
{ yyval.expr = expr_alloc_comp(E_UNEQUAL, yyvsp[-2].symbol, yyvsp[0].symbol); ;
    break;}
case 90:
#line 409 "zconf.y"
{ yyval.expr = yyvsp[-1].expr; ;
    break;}
case 91:
#line 410 "zconf.y"
{ yyval.expr = expr_alloc_one(E_NOT, yyvsp[0].expr); ;
    break;}
case 92:
#line 411 "zconf.y"
{ yyval.expr = expr_alloc_two(E_OR, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 93:
#line 412 "zconf.y"
{ yyval.expr = expr_alloc_two(E_AND, yyvsp[-2].expr, yyvsp[0].expr); ;
    break;}
case 94:
#line 415 "zconf.y"
{ yyval.symbol = sym_lookup(yyvsp[0].string, 0); free(yyvsp[0].string); ;
    break;}
case 95:
#line 416 "zconf.y"
{ yyval.symbol = sym_lookup(yyvsp[0].string, 1); free(yyvsp[0].string); ;
    break;}
}

#line 705 "/usr/share/bison/bison.simple"


  yyvsp -= yylen;
  yyssp -= yylen;
#if YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG
  if (yydebug)
    {
      short *yyssp1 = yyss - 1;
      YYFPRINTF (stderr, "state stack now");
      while (yyssp1 != yyssp)
	YYFPRINTF (stderr, " %d", *++yyssp1);
      YYFPRINTF (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;
#if YYLSP_NEEDED
  *++yylsp = yyloc;
#endif

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("parse error, unexpected ") + 1;
	  yysize += yystrlen (yytname[YYTRANSLATE (yychar)]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "parse error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[YYTRANSLATE (yychar)]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exhausted");
	}
      else
#endif /* defined (YYERROR_VERBOSE) */
	yyerror ("parse error");
    }
  goto yyerrlab1;


/*--------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action |
`--------------------------------------------------*/
yyerrlab1:
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;
      YYDPRINTF ((stderr, "Discarding token %d (%s).\n",
		  yychar, yytname[yychar1]));
      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;


/*-------------------------------------------------------------------.
| yyerrdefault -- current state does not do anything special for the |
| error token.                                                       |
`-------------------------------------------------------------------*/
yyerrdefault:
#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */

  /* If its default is to accept any token, ok.  Otherwise pop it.  */
  yyn = yydefact[yystate];
  if (yyn)
    goto yydefault;
#endif


/*---------------------------------------------------------------.
| yyerrpop -- pop the current state because it cannot handle the |
| error token                                                    |
`---------------------------------------------------------------*/
yyerrpop:
  if (yyssp == yyss)
    YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#if YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG
  if (yydebug)
    {
      short *yyssp1 = yyss - 1;
      YYFPRINTF (stderr, "Error: state stack now");
      while (yyssp1 != yyssp)
	YYFPRINTF (stderr, " %d", *++yyssp1);
      YYFPRINTF (stderr, "\n");
    }
#endif

/*--------------.
| yyerrhandle.  |
`--------------*/
yyerrhandle:
  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;
#if YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

/*---------------------------------------------.
| yyoverflowab -- parser overflow comes here.  |
`---------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}
#line 419 "zconf.y"


void conf_parse(const char *name)
{
	zconf_initscan(name);

	sym_init();
	menu_init();
	rootmenu.prompt = menu_add_prop(P_MENU, "Linux Kernel Configuration", NULL, NULL);

	//zconfdebug = 1;
	zconfparse();
	if (zconfnerrs)
		exit(1);
	menu_finalize(&rootmenu);

	modules_sym = sym_lookup("MODULES", 0);

	sym_change_count = 1;
}

const char *zconf_tokenname(int token)
{
	switch (token) {
	case T_MENU:		return "menu";
	case T_ENDMENU:		return "endmenu";
	case T_CHOICE:		return "choice";
	case T_ENDCHOICE:	return "endchoice";
	case T_IF:		return "if";
	case T_ENDIF:		return "endif";
	}
	return "<token>";
} 

static bool zconf_endtoken(int token, int starttoken, int endtoken)
{
	if (token != endtoken) {
		zconfprint("unexpected '%s' within %s block", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	if (current_menu->file != current_file) {
		zconfprint("'%s' in different file than '%s'", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfprint("location of the '%s'", zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconferror(const char *err)
{
	fprintf(stderr, "%s:%d: %s\n", zconf_curname(), zconf_lineno(), err);
}

void print_quoted_string(FILE *out, const char *str)
{
	const char *p;
	int len;

	putc('"', out);
	while ((p = strchr(str, '"'))) {
		len = p - str;
		if (len)
			fprintf(out, "%.*s", len, str);
		fputs("\\\"", out);
		str = p + 1;
	}
	fputs(str, out);
	putc('"', out);
}

void print_symbol(FILE *out, struct menu *menu)
{
	struct symbol *sym = menu->sym;
	struct property *prop;

	//sym->flags |= SYMBOL_PRINTED;

	if (sym_is_choice(sym))
		fprintf(out, "choice\n");
	else
		fprintf(out, "config %s\n", sym->name);
	switch (sym->type) {
	case S_BOOLEAN:
		fputs("  boolean\n", out);
		break;
	case S_TRISTATE:
		fputs("  tristate\n", out);
		break;
	case S_STRING:
		fputs("  string\n", out);
		break;
	case S_INT:
		fputs("  integer\n", out);
		break;
	case S_HEX:
		fputs("  hex\n", out);
		break;
	default:
		fputs("  ???\n", out);
		break;
	}
#if 0
	if (!expr_is_yes(sym->dep)) {
		fputs("  depends ", out);
		expr_fprint(sym->dep, out);
		fputc('\n', out);
	}
#endif
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu != menu)
			continue;
		switch (prop->type) {
		case P_PROMPT:
			fputs("  prompt ", out);
			print_quoted_string(out, prop->text);
			if (prop->def) {
				fputc(' ', out);
				if (prop->def->flags & SYMBOL_CONST)
					print_quoted_string(out, prop->def->name);
				else
					fputs(prop->def->name, out);
			}
			if (!expr_is_yes(E_EXPR(prop->visible))) {
				fputs(" if ", out);
				expr_fprint(E_EXPR(prop->visible), out);
			}
			fputc('\n', out);
			break;
		case P_DEFAULT:
			fputs( "  default ", out);
			print_quoted_string(out, prop->def->name);
			if (!expr_is_yes(E_EXPR(prop->visible))) {
				fputs(" if ", out);
				expr_fprint(E_EXPR(prop->visible), out);
			}
			fputc('\n', out);
			break;
		case P_CHOICE:
			fputs("  #choice value\n", out);
			break;
		default:
			fprintf(out, "  unknown prop %d!\n", prop->type);
			break;
		}
	}
	if (sym->help) {
		int len = strlen(sym->help);
		while (sym->help[--len] == '\n')
			sym->help[len] = 0;
		fprintf(out, "  help\n%s\n", sym->help);
	}
	fputc('\n', out);
}

void zconfdump(FILE *out)
{
	//struct file *file;
	struct property *prop;
	struct symbol *sym;
	struct menu *menu;

	menu = rootmenu.list;
	while (menu) {
		if ((sym = menu->sym))
			print_symbol(out, menu);
		else if ((prop = menu->prompt)) {
			switch (prop->type) {
			//case T_MAINMENU:
			//	fputs("\nmainmenu ", out);
			//	print_quoted_string(out, prop->text);
			//	fputs("\n", out);
			//	break;
			case P_COMMENT:
				fputs("\ncomment ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			case P_MENU:
				fputs("\nmenu ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			//case T_SOURCE:
			//	fputs("\nsource ", out);
			//	print_quoted_string(out, prop->text);
			//	fputs("\n", out);
			//	break;
			//case T_IF:
			//	fputs("\nif\n", out);
			default:
				;
			}
			if (!expr_is_yes(E_EXPR(prop->visible))) {
				fputs("  depends ", out);
				expr_fprint(E_EXPR(prop->visible), out);
				fputc('\n', out);
			}
			fputs("\n", out);
		}

		if (menu->list)
			menu = menu->list;
		else if (menu->next)
			menu = menu->next;
		else while ((menu = menu->parent)) {
			if (menu->prompt && menu->prompt->type == P_MENU)
				fputs("\nendmenu\n", out);
			if (menu->next) {
				menu = menu->next;
				break;
			}
		}
	}
}

#include "lex.zconf.c"
#include "confdata.c"
#include "expr.c"
#include "symbol.c"
#include "menu.c"
