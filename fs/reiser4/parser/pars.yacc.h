/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of work space for yacc generated  from
 * parser.y
 */

#define MAXLEVELCO 500
#define BEGIN_FROM_ROOT 222
#define BEGIN_FROM_CURRENT 333

struct reiser4_syscall_w_space {
	char * ws_inline;    /* this two field used for parsing string, one (inline) stay on begin */
	char * ws_pline;     /*   of token, second (pline) walk to end to token                   */
#ifdef yyacc
	                     /* next field need for yacc                   */
	                     /* accesing to this fields from rules: ws->... */
	int ws_yystacksize; /*500*/
	int ws_yymaxdepth ; /*500*/
	int ws_yydebug;
	int ws_yynerrs;
	int ws_yyerrflag;
	int ws_yychar;
	int * ws_yyssp;
	YYSTYPE * ws_yyvsp;
	YYSTYPE ws_yyval;
	YYSTYPE ws_yylval;
	int     ws_yyss[YYSTACKSIZE];
	YYSTYPE ws_yyvs[YYSTACKSIZE];
#else
	/* declare for bison */
#endif
	int	ws_yyerrco;
	int	ws_level;              /* current level            */
	int	ws_errco;              /* number of errors         */
	                               /* working fields  */
	char        * tmpWrdEnd;       /* pointer for parsing input string */
	char        * yytext;          /* pointer for parsing input string */
	                               /* space for   */
	free_space_t * freeSpHead;      /* work spaces list Header */
	free_space_t * freeSpCur;       /* current work space */
	wrd_t       * wrdHead;         /* names list Header */
	pars_var_t  * Head_pars_var;   /* parsed variables Header */
	streg_t     * Head_level;      /* parsers level list Header */
	streg_t     * cur_level;       /* current level */

	expr_v4_t   * root_e;          /* root expression  for this task */
	struct nameidata nd;           /* work field for pass to VFS mount points */
};



#define printf prink

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
