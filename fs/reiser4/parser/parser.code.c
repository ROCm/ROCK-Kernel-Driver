#ifndef lint
/*static char yysccsid[] = "from: @(#)yaccpar	1.9 (Berkeley) 02/21/93";*/
static char yyrcsid[] = "$Id: skeleton.c,v 1.4 1993/12/21 18:45:32 jtc Exp $\n 2002/10/22 VD reiser4";
#endif
#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define yyclearin (yychar=(-1))
#define yyerrok (yyerrflag=0)
#define YYRECOVERING (yyerrflag!=0)
#define YYPREFIX "yy"
#line 9 "fs/reiser4/parser/parser.y"
typedef union
{
	long charType;
	expr_v4_t * expr;
	wrd_t * wrd;
} YYSTYPE;
#line 20 "fs/reiser4/parser/parser.code.c"
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
#define YYERRCODE 256
#define YYTABLESIZE 422
#define YYFINAL 5
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 289
#if defined(YYREISER4_DEF)
#define extern static
#endif
extern short yylhs[];
extern short yylen[];
extern short yydefred[];
extern short yydgoto[];
extern short yysindex[];
extern short yyrindex[];
extern short yygindex[];
extern short yytable[];
extern short yycheck[];
#if YYDEBUG
extern char *yyname[];
extern char *yyrule[];
#endif
#if defined(YYREISER4_DEF)
#define YYSTACKSIZE 500
#define YYMAXDEPTH 500
#define yydebug ws->ws_yydebug
#define yynerrs ws->ws_yynerrs
#define yyerrflag ws->ws_yyerrflag
#define yychar ws->ws_yychar
#define yyssp ws->ws_yyssp
#define yyvsp ws->ws_yyvsp
#define yyval ws->ws_yyval
#define yylval ws->ws_yylval
#define yyss ws->ws_yyss
#define yyvs ws->ws_yyvs
#define yystacksize ws->ws_yystacksize
#else
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 500
#define YYMAXDEPTH 500
#endif
#endif
int yydebug;
int yynerrs;
int yyerrflag;
int yychar;
short *yyssp;
YYSTYPE *yyvsp;
YYSTYPE yyval;
YYSTYPE yylval;
short yyss[YYSTACKSIZE];
YYSTYPE yyvs[YYSTACKSIZE];
#define yystacksize YYSTACKSIZE
#endif
#line 160 "fs/reiser4/parser/parser.y"


#define yyversion "4.0.0"
#include "pars.cls.h"
#include "parser.tab.c"
#include "pars.yacc.h"
#include "lib.c"

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   End:
*/
#line 132 "fs/reiser4/parser/parser.code.c"
#define YYABORT goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR goto yyerrlab
int
#if defined(YYREISER4_DEF)
yyparse(struct reiser4_syscall_w_space  * ws)
#else
#if defined(__STDC__)
yyparse(void)
#else
yyparse()
#endif
#endif
{
    register int yym, yyn, yystate;
#if YYDEBUG
    register char *yys;
    static char *getenv();

    if (yys = getenv("YYDEBUG"))
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = (-1);

    yyssp = yyss;
    yyvsp = yyvs;
    *yyssp = yystate = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yyssp >= yyss + yystacksize - 1)
        {
            goto yyoverflow;
        }
        *++yyssp = yystate = yytable[yyn];
        *++yyvsp = yylval;
        yychar = (-1);
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;
#if defined(YYREISER4_DEF)
    yyerror(ws,11111,yystate,yychar);
#else
    yyerror("syntax error");
#endif
#ifdef lint
    goto yyerrlab;
#endif
yyerrlab:
    ++yynerrs;
yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yyssp]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yyssp, yytable[yyn]);
#endif
                if (yyssp >= yyss + yystacksize - 1)
                {
                    goto yyoverflow;
                }
                *++yyssp = yystate = yytable[yyn];
                *++yyvsp = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yyssp);
#endif
                if (yyssp <= yyss) goto yyabort;
                --yyssp;
                --yyvsp;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = (-1);
        goto yyloop;
    }
yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    yyval = yyvsp[1-yym];
    switch (yyn)
    {
case 1:
#line 79 "fs/reiser4/parser/parser.y"
{ yyval.charType = free_expr( ws, yyvsp[0].expr ); }
break;
case 2:
#line 83 "fs/reiser4/parser/parser.y"
{ yyval.expr = yyvsp[0].expr;}
break;
case 3:
#line 84 "fs/reiser4/parser/parser.y"
{ yyval.expr = const_to_expr( ws, yyvsp[0].wrd ); }
break;
case 4:
#line 85 "fs/reiser4/parser/parser.y"
{ yyval.expr = unname( ws, yyvsp[0].expr ); }
break;
case 5:
#line 86 "fs/reiser4/parser/parser.y"
{ yyval.expr = concat_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 6:
#line 87 "fs/reiser4/parser/parser.y"
{ yyval.expr = list_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 7:
#line 88 "fs/reiser4/parser/parser.y"
{ yyval.expr = list_async_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 8:
#line 89 "fs/reiser4/parser/parser.y"
{ yyval.expr = yyvsp[0].expr; level_down( ws, IF_STATEMENT, IF_STATEMENT ); }
break;
case 9:
#line 91 "fs/reiser4/parser/parser.y"
{ yyval.expr = assign( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 10:
#line 92 "fs/reiser4/parser/parser.y"
{ yyval.expr = assign( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 11:
#line 93 "fs/reiser4/parser/parser.y"
{ yyval.expr = assign_invert( ws, yyvsp[-4].expr, yyvsp[-1].expr ); }
break;
case 12:
#line 94 "fs/reiser4/parser/parser.y"
{ yyval.expr = symlink( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 13:
#line 103 "fs/reiser4/parser/parser.y"
{ yyval.expr = if_then_else( ws, yyvsp[-3].expr, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 14:
#line 104 "fs/reiser4/parser/parser.y"
{ yyval.expr = if_then( ws, yyvsp[-1].expr, yyvsp[0].expr) ;         }
break;
case 15:
#line 108 "fs/reiser4/parser/parser.y"
{ yyval.expr = yyvsp[0].expr; }
break;
case 16:
#line 111 "fs/reiser4/parser/parser.y"
{ level_up( ws, IF_STATEMENT ); }
break;
case 17:
#line 115 "fs/reiser4/parser/parser.y"
{ yyval.expr = not_expression( ws, yyvsp[0].expr ); }
break;
case 18:
#line 116 "fs/reiser4/parser/parser.y"
{ yyval.expr = check_exist( ws, yyvsp[0].expr ); }
break;
case 19:
#line 117 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_EQ_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 20:
#line 118 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_NE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 21:
#line 119 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_LE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 22:
#line 120 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_GE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 23:
#line 121 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_LT_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 24:
#line 122 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_GT_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 25:
#line 123 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_OR_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 26:
#line 124 "fs/reiser4/parser/parser.y"
{ yyval.expr = compare_AND_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); }
break;
case 27:
#line 128 "fs/reiser4/parser/parser.y"
{ goto_end( ws );}
break;
case 28:
#line 132 "fs/reiser4/parser/parser.y"
{ yyval.expr = yyvsp[0].expr;}
break;
case 29:
#line 133 "fs/reiser4/parser/parser.y"
{ yyval.expr = target_name( yyvsp[-2].expr, yyvsp[0].expr );}
break;
case 30:
#line 137 "fs/reiser4/parser/parser.y"
{ yyval.expr = pars_expr( ws, yyvsp[-1].expr, yyvsp[0].expr ) ; }
break;
case 31:
#line 138 "fs/reiser4/parser/parser.y"
{ yyval.expr = pars_expr( ws, yyvsp[-2].expr, yyvsp[0].expr ) ; }
break;
case 32:
#line 142 "fs/reiser4/parser/parser.y"
{ yyval.expr = pars_lookup_root( ws ) ; }
break;
case 33:
#line 143 "fs/reiser4/parser/parser.y"
{ yyval.expr = pars_lookup_curr( ws ) ; }
break;
case 34:
#line 147 "fs/reiser4/parser/parser.y"
{ yyval.expr = lookup_word( ws, yyvsp[0].wrd ); }
break;
case 35:
#line 148 "fs/reiser4/parser/parser.y"
{ yyval.expr = yyvsp[-1].expr; level_down( ws, yyvsp[-2].charType, yyvsp[0].charType );}
break;
case 36:
#line 152 "fs/reiser4/parser/parser.y"
{ yyval.charType = yyvsp[0].charType; level_up( ws, yyvsp[0].charType ); }
break;
#line 425 "fs/reiser4/parser/parser.code.c"
    }
    yyssp -= yym;
    yystate = *yyssp;
    yyvsp -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yyssp = YYFINAL;
        *++yyvsp = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yyssp, yystate);
#endif
    if (yyssp >= yyss + yystacksize - 1)
    {
        goto yyoverflow;
    }
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;
yyoverflow:
#if defined(YYREISER4_DEF)
    yyerror(ws,101); /*yacc stack overflow*/
#else
    yyerror("yacc stack overflow");
#endif
yyabort:
    return (1);
yyaccept:
    return (0);
}
