/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants for lex component of parser.y
 */



#define ERR  -128


typedef enum {
    OK   ,
    Blk  ,   /* blank */
    Wrd  ,   /* any symbol exept spec symbl */
    Int  ,   /* numeric */

    Ptr  ,   /* pointer */

    Pru  ,   /* _pruner */

    Stb  ,   /* ` string begin */
    Ste  ,   /* ' string end */
    Lpr  ,   /* ( [ { */
    Rpr  ,   /* ) ] } */
    Com  ,   /* , */
    Mns  ,   /* - */


    Les  ,   /* < */
    Slh  ,   /* / */

    Lsq  ,   /* [ */
    Rsq  ,   /* ] */

    Bsl  ,   /* \ */

    Lfl  ,   /* { */
    Rfl  ,   /* } */

    Pip  ,   /* | */
    Sp1  ,   /* : */
    Sp2  ,   /* ; */

    Dot  ,   /* . */

    Sp4  ,   /* = */
    Sp5  ,   /* > */
    Sp6  ,   /* ? */
    Pls  ,   /* +  ???*/
    Res  ,   /*  */

    Str  ,
    ASG  ,
    App  ,
    Lnk  ,
    Ap2  ,
    Nam  ,
    LastState
} state;

#define STRING_CONSTANT_EMPTY STRING_CONSTANT   /* tmp */

static char   ncl     [256] = {
	Blk,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	/* 32*/
      /*        !     "    #     $     %     &     ' */
	Blk,  Res,  Res,  Res,  Res,  Res,  Res,  Ste,
      /* (      )     *    +     ,     -     .     / */
        Lpr,  Rpr,  Res,  Pls,  Com,  Mns,  Dot,  Slh,
      /* 0      1     2    3     4     5     6     7 */
	Int,  Int,  Int,  Int,  Int,  Int,  Int,  Int,
      /* 8      9     :    ;     <     =     >     ? */
	Int,  Int,  Sp1,  Sp2,  Les,  Sp4,  Sp5,  Sp6,

	/* 64*/
      /* @      A     B    C     D     E     F     G */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* H      I     J    K     L     M     N     O */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* P      Q     R    S     T     U     V     W */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* X      Y     Z    [     \     ]     ^     _ */
	Wrd,  Wrd,  Wrd,  Lsq,  Bsl,  Rsq,  Res,  Pru,
	/* 96*/
      /* `      a     b    c     d     e     f     g */
        Stb,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* h      i     j    k     l     m     n     o */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* p      q     r    s     t     u     v     w */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* x      y     z    {     |     }     ~       */
	Wrd,  Wrd,  Wrd,  Lfl,  Pip,  Rfl,  Wrd,  ERR,

	/*128*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*160*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*192*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*224*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  ERR
};

struct lexcls {
  int term;
  char c[32];
} ;

static struct {
	char    *       wrd;
	int             class;
}
pars_key [] = {
  { "and"         ,    AND            },
  { "else"        ,    ELSE           },
  { "eq"          ,    EQ             },
  { "ge"          ,    GE             },
  { "gt"          ,    GT             },
  { "if"          ,    IF             },
  { "le"          ,    LE             },
  { "lt"          ,    LT             },
  { "ne"          ,    NE             },
  { "not"         ,    NOT            },
  { "or"          ,    OR             },
  { "then"        ,    THEN           },
  { "tw/"         ,    TRANSCRASH     }
};


struct lexcls lexcls[] = {
/*
..   a   1       _   `   '     (   )   ,   -   <   /   [   ]     \   {   }   |   ;   :   .   =     >   ?   +
Blk Wrd Int Ptr Pru Stb Ste   Lpr Rpr Com Mns Les Slh Lsq Rsq   Bsl Lfl Rfl Pip Sp1 Sp2 Dot Sp4   Sp5 Sp6 Pls ...  */
[Blk]={ 0, {0,
Blk,Wrd,Int,Ptr,Pru,Str,ERR,  Lpr,Rpr,Com,Mns,Les,Slh,Lsq,Rsq,  Bsl,Lfl,Rfl,Pip,Sp1,Sp2,Dot,Sp4,  Sp5,Sp6,ERR,ERR,ERR,ERR,ERR,ERR}},
[Wrd]={  WORD, {0,
OK ,Wrd,Wrd,Wrd,Wrd,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  Bsl,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Int]={  WORD, {0,
OK ,Wrd,Int,Wrd,Wrd,OK ,OK ,  OK ,OK ,OK ,Wrd,OK ,OK ,OK ,OK ,  Wrd,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Ptr]={  WORD,{0,
OK ,Wrd,Wrd,Wrd,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  Wrd,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pru]={  P_RUNNER,{0,
OK ,Pru,Pru,Pru,Pru,OK ,OK ,  OK ,OK ,OK ,Pru,OK ,OK ,OK ,OK ,  Pru,OK ,OK ,OK ,OK ,OK ,Pru,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
/*
[Stb]={  STRING_CONSTANT_EMPTY, {1,
Str,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
*/
[Stb]={  0, {1,
Stb,Stb,Stb,Stb,Stb,Stb,Str,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb}},

[Ste]={  0, {0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},
[Lpr]={  L_BRACKET /*L_PARENT*/,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rpr]={  R_BRACKET,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Com]={  COMMA,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Mns]={  0,{0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  Lnk,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},
[Les]{  LT,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,ASG,App,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,Nam,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Slh]={  SLASH,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,Slh,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Lsq]={  L_BRACKET,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rsq]={  R_BRACKET,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Bsl]={  0,{0,
Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd}},
[Lfl]={  L_BRACKET,{0,            /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rfl]={  R_BRACKET,{0,            /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pip]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp1]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp2]={  SEMICOLON,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Dot]={ WORD,{0,
OK ,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,Dot,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp4]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp5]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Sp6]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pls]={  PLUS,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Res]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
/*
[Str]={  STRING_CONSTANT,{1,
Str,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
*/
[Str]={  STRING_CONSTANT,{1,
OK ,OK ,OK ,OK ,OK ,OK ,Stb,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},


[ASG]={  L_ASSIGN,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[App]={  L_ASSIGN,{0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,Ap2,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},
/*
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,Ap2,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
*/
[Lnk]={ L_SYMLINK,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  ERR ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Ap2]={  L_APPEND,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Nam]={  NAMED,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }}

};


