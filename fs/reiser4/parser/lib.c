/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * functions for parser.y
 */


#include "lib.h"

#include <linux/mount.h>

#define my_mntget(mess,s)  mntget(s); printk ("mntget  %d,  %s\n",(s)->mnt_count, mess);
#define my_dget(mess,s)    dget(s)  ; printk ("dget    %d,  %s\n",(s)->d_count, mess);

#define path4_release( pointer )  { dput( (pointer).dentry ); \
                                     mntput((pointer).mnt);   \
           printk ("       mntput %d\n",((pointer).mnt)->mnt_count);\
           printk ("         dput %d\n",((pointer).dentry)->d_count);\
}


#define LEX_XFORM  1001
#define LEXERR2    1002
#define LEX_Ste    1003

/* printing errors for parsing */
static void yyerror( struct reiser4_syscall_w_space *ws  /* work space ptr */,
		                             int msgnum  /* message number */, ...)
{
	char errstr[120]={"\nreiser4 parser:"};
	char * s;
	va_list args;
	va_start(args, msgnum);
	switch (msgnum) {
	case   101:
		strcat(errstr,"yacc stack overflow");
		break;
	case LEX_XFORM:
		strcat(errstr,"x format has odd number of symbols");
		break;
	case LEXERR2:
/*			int state = va_arg(args, int);*/
		strcat(errstr,"internal lex table error");
		break;
	case LEX_Ste:
		strcat(errstr,"wrong lexem");
		break;
	case 11111:
		{
			int state = va_arg(args, int);
			{
				char ss[16];
				/*				int s = va_arg(args, int);*/
				sprintf( ss,"%4d ", state);
				strcat( errstr, ss );
			}
			strcat( errstr, " syntax error:" );
			switch(state) {
				//		case 4:
				//			strcat(errstr," wrong operation");
				//			break;
			case 6:
				strcat(errstr," wrong assign operation");
				break;
			case 7:
			case 12:
				strcat(errstr," wrong name");
				break;
			case 27:
				strcat(errstr," wrong logical operation");
				break;
			case 10:
				strcat(errstr," wrong THEN keyword");
				break;
			case 34:
			case 50:
				strcat(errstr," wrong separatop");
				break;
			default:
				strcat(errstr," strange error");
				break;
			}
		}
		break;
	}
	va_end(args);
	printk( "\n%s\n", ws->ws_inline );
	for (s=ws->ws_inline; s<curr_symbol(ws); s++)
		{
			if (*s=='\t' ) {
				printk("\t");
			} else {
				printk(" ");
			}
		}
	printk("^");
	printk(errstr);
	printk("\n");
//	printk("\n%s",curr_symbol(ws));
}

/* free lists of work space*/
static void freeList(free_space_t * list /* head of list to be fee */)
{
	free_space_t * curr,* next;
	next = list;
	while (next) {
		curr = next;
		next = curr->free_space_next;
		kfree(curr);
	}
}

/* free work space*/
static int reiser4_pars_free(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	assert("VD-reiser4_pars_free:ws_level",        ws->ws_level >= 0);
	assert("VD-reiser4_pars_free:cur_exp" ,        ws->cur_level->cur_exp != NULL);

	free_expr( ws, ws->cur_level->cur_exp );
	free_expr( ws, ws->root_e );
	if ( ws->freeSpHead ) {
		freeList(ws->freeSpHead);
	}

	kfree(ws);
	return 0;
}




#if 0
static inline void path4_release( struct dentry *de, struct vfsmount *mnt)
{
	assert("VD-path4_release: dentry",    de != NULL);
	assert("VD-path4_release: mnt",       mnt != NULL);
	my_dput(   de  );
	my_mntput( mnt );
}
#endif

/* FIXME:NIKITA->VOVA code below looks like custom made memory allocator. Why
 * not to use slab? */
#define INITNEXTFREESPACE(fs)	(fs)->free_space_next = NULL;                                      \
                                (fs)->freeSpaceMax   = (fs)->freeSpaceBase+FREESPACESIZE;         \
			        (fs)->freeSpace      = (fs)->freeSpaceBase


/* allocate work space */
static free_space_t * free_space_alloc()
{
	free_space_t * fs;
	fs = ( free_space_t * ) kmalloc( sizeof( free_space_t ),GFP_KERNEL ) ;
	assert("VD kmalloc work space",fs!=NULL);
	memset( fs , 0, sizeof( free_space_t ));
	INITNEXTFREESPACE(fs);
	return fs;
}

#define GET_FIRST_FREESPHEAD(ws) (ws)->freeSpHead
#define GET_NEXT_FREESPHEAD(curr) (curr)->free_space_next


/* allocate next work space */
static free_space_t * freeSpaceNextAlloc(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	free_space_t * curr,* next;
	curr=NULL;
	next = GET_FIRST_FREESPHEAD(ws);
	while (next) {
		curr = next;
		next = GET_NEXT_FREESPHEAD(curr);
	}
	next = free_space_alloc();
	if(curr==NULL) 		{
		ws->freeSpHead=next;
	}
	else {
		curr->free_space_next=next;
	}
	next->free_space_next=NULL;
	return next;
}

/* allocate field lenth=len in work space */
static char* list_alloc(struct reiser4_syscall_w_space * ws/* work space ptr */,
			int len/* lenth of structures to be allocated in bytes */)
{
	char * rez;
	if( (ws->freeSpCur->freeSpace+len) > (ws->freeSpCur->freeSpaceMax) ) {
		ws->freeSpCur = freeSpaceNextAlloc(ws);
	}
	rez = ws->freeSpCur->freeSpace;
	ws->freeSpCur->freeSpace += ROUND_UP(len);
	return rez;
}

/* allocate new level of parsing in work space */
static streg_t *alloc_new_level(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	return ( streg_t *)  list_alloc(ws,sizeof(streg_t));
}

/* allocate structure of new variable of input expression */
static pars_var_t * alloc_pars_var(struct reiser4_syscall_w_space * ws /* work space ptr */,
			     pars_var_t * last_pars_var /* last of allocated pars_var or NULL if list is empty */)
{
	pars_var_t * pars_var;
	pars_var = (pars_var_t *)list_alloc(ws,sizeof(pars_var_t));
	if ( last_pars_var == NULL ) {
		ws->Head_pars_var = pars_var;
	}
	else {
		last_pars_var->next = pars_var;
	}
	pars_var->next = NULL;
	return pars_var;
}

/* free lnodes used in expression */
static int free_expr( struct reiser4_syscall_w_space * ws,  expr_v4_t * expr)
{
	expr_list_t * tmp;
	int ret = 0;
	assert("VD-free_expr", expr!=NULL);

	printk("free_expr: %d\n", expr->h.type );
	switch (expr->h.type) {
	case EXPR_WRD:
		break;
	case EXPR_PARS_VAR:
		pop_var_val_stack( ws, expr->pars_var.v->val );




		break;
	case EXPR_LIST:
		tmp=&expr->list;
		while (tmp) {
			assert("VD-free_expr.EXPR_LIST", tmp->h.type==EXPR_LIST);
			ret |= free_expr( ws, tmp->source );
			tmp = tmp->next;
		}
		break;
	case EXPR_ASSIGN:
		ret = pop_var_val_stack( ws, expr->assgn.target->val );



		ret |= free_expr( ws, expr->assgn.source );
		break;
	case EXPR_LNODE:
		assert("VD-free_expr.lnode.lnode", expr->lnode.lnode!=NULL);
		path4_release( expr->lnode.lnode->dentry );
		lput( expr->lnode.lnode );
		break;
	case EXPR_FLOW:
		break;
	case EXPR_OP2:
		ret  = free_expr( ws, expr->op2.op_r );
		ret |= free_expr( ws, expr->op2.op_l );
		break;
	case EXPR_OP:
		ret = free_expr( ws, expr->op.op );
		break;
	}
	return ret;
}


//ln->inode.inode->i_op->lookup(struct inode *,struct dentry *);
//current->fs->pwd->d_inode->i_op->lookup(struct inode *,struct dentry *);

#if 0
/* alloca te space for lnode */
static lnode * alloc_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	lnode * ln;
	ln = ( lnode * ) kmalloc( sizeof( lnode ), GFP_KERNEL);
	assert("VD-alloc_pars_var", ln != NULL );
	memset( ln , 0, sizeof( lnode ));
	return ln;
}
#endif

/* make lnode_dentry from inode, except reiser4 inode */
static lnode * get_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	lnode * ln;
	reiser4_key key, * k_rez,* l_rez;

#if 0                      /*def NOT_YET*/
	if ( is_reiser4_inode( ws->nd.dentry->inode ) ) {

		k_rez             = build_sd_key( ws->nd.dentry->inode, &key);
		ln                = lget(  LNODE_REISER4_INODE, get_inode_oid( ws->nd.dentry->inode) );
		//			ln->lw.lw_sb = ws->nd.dentry->inode->isb;
		ln->reiser4_inode.inode = /*????*/  ws->nd.dentry->inode->isb;
		ln->reiser4_inode.inode = /*????*/  ws->nd.dentry->inode->isb;
		PTRACE( ws, "r4: lnode=%p", ln );
	}
	else
#endif
		{
			ln                = lget( LNODE_DENTRY, get_inode_oid( ws->nd.dentry->d_inode) );
			{
				read_lock(&current->fs->lock);
				ln->dentry.mnt    = my_mntget("lget", ws->nd.mnt);
				ln->dentry.dentry = my_dget("lget", ws->nd.dentry);
				read_unlock(&current->fs->lock);
			}
		}
	return ln;
}

/*  allocate work space, initialize work space, tables, take root inode and PWD inode */
static struct reiser4_syscall_w_space * reiser4_pars_init(void)
{
	struct reiser4_syscall_w_space * ws;

	/* allocate work space for parser working variables, attached to this call */
	ws = kmalloc( sizeof( struct reiser4_syscall_w_space ), GFP_KERNEL );
	assert("VD_allock work space", ws != NULL);
	memset( ws, 0, sizeof( struct reiser4_syscall_w_space ));
	ws->ws_yystacksize = MAXLEVELCO; /* must be 500 by default */
	ws->ws_yymaxdepth  = MAXLEVELCO; /* must be 500 by default */
	                                                    /* allocate first part of working tables
							       and initialise headers */
	ws->freeSpHead          = free_space_alloc();
	ws->freeSpCur           = ws->freeSpHead;
	ws->wrdHead             = NULL;
	ws->cur_level           = alloc_new_level(ws);
	ws->root_e              = init_root(ws);
	ws->cur_level->cur_exp  = init_pwd(ws);
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
	ws->cur_level->prev     = NULL;
	ws->cur_level->next     = NULL;
	ws->cur_level->level    = 0;
	ws->cur_level->stype    = 0;
	return ws;
}

#if 0
static expr_v4_t * named_level_down(struct reiser4_syscall_w_space *ws /* work space ptr */,
			     expr_v4_t * e /* name for expression  */,
			     expr_v4_t * e1,
			     long type /* type of level we going to */)
{

	static int push_var_val_stack( ws, struct pars_var * var, long type )

	rezult->u.data  = kmalloc( SIZEFOR_ASSIGN_RESULT, GFP_KERNEL ) ;
	sprintf( rezult->u.data, "%d", ret_code );

	level_down( ws, , type2 );
	return e1;
}


/* level up of parsing level */
static void level_up_named(struct reiser4_syscall_w_space *ws /* work space ptr */,
			   expr_v4_t * e1 /* name for expression  */,
			   long type /* type of level we going to */)
{
	pars_var_t * rezult;

	assert("wrong type of named expression", type == CD_BEGIN );

	rezult =  e1->pars_var.v;
	switch ( e1->pars_var.v->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		break;
	case VAR_TMP:
		break;
	}

	/* make name for w in this level. ????????
	   not yet worked */


	rezult =  lookup_pars_var_word( ws , sink, make_new_word(ws, ASSIGN_RESULT ), VAR_TMP);
	rezult->u.data  = kmalloc( SIZEFOR_ASSIGN_RESULT, GFP_KERNEL ) ;
	sprintf( rezult->u.data, "%d", ret_code );

?????

	level_up( ws, type );
}

#endif


static expr_v4_t *target_name( expr_v4_t *assoc_name, expr_v4_t *target )
{
	target->pars_var.v->val->associated = assoc_name->pars_var.v;
	return target;
}

/* level up of parsing level */
static void level_up(struct reiser4_syscall_w_space *ws /* work space ptr */,
		     long type /* type of level we going to */)
{
	if (ws->cur_level->next==NULL) {
		ws->cur_level->next        = alloc_new_level(ws);
		ws->cur_level->next->next  = NULL;
		ws->cur_level->next->prev  = ws->cur_level;
		ws->cur_level->next->level = ws->cur_level->level+1;
	}
	ws->cur_level           = ws->cur_level->next;
	ws->cur_level->stype    = type;
	ws->cur_level->cur_exp  = ws->cur_level->prev->wrk_exp;                  /* current pwd for new level */
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
}


/* level down of parsing level */
static  void  level_down(struct reiser4_syscall_w_space * ws /* work space ptr */,
			 long type1 /* type of level that was up( for checking) */,
			 long type2 /* type of level that is down(for checking)*/)
{
	pars_var_value_t * ret,*next;
	assert("VD-level_down: type mithmatch", type1 == type2 );
	assert("VD-level_down: type mithmatch with level", type1 == ws->cur_level->stype );
	assert("VD-level_down: This is top level, prev == NULL", ws->cur_level->prev != NULL);
	ret = ws->cur_level->val_level;
	while( ret != NULL )
		{
			next = ret->next_level;
			assert("VD: level down: not top value was pop", ret == ret->host->val);
			pop_var_val_stack( ws, ret );
			ret = next;
		}
	free_expr( ws, ws->cur_level->prev->wrk_exp );
	ws->cur_level->prev->wrk_exp = ws->cur_level->wrk_exp ;           /* current wrk for prev level */
	ws->cur_level                = ws->cur_level->prev;
}

/* copy name from param to free space,*/
static  wrd_t * make_new_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
	      char *txt /* string to put in name table */)
{
	ws->tmpWrdEnd = ws->freeSpCur->freeSpace;
	strcat( ws->tmpWrdEnd, txt );
	ws->tmpWrdEnd += strlen(txt) ;
	*ws->tmpWrdEnd++ = 0;
	return _wrd_inittab( ws );
}


/* move_selected_word - copy term from input bufer to free space.
 * if it need more, move freeSpace to the end.
 * otherwise next term will owerwrite it
 *  freeSpace is a kernel space no need make getnam().
 * exclude is for special for string: store without ''
 */
static void move_selected_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
			       int exclude  /* TRUE - for storing string without first and last symbols
					       FALS - for storing names */ )
{
	int i;
	/*	char * s= ws->ws_pline;*/
	if (exclude) {
		ws->yytext++;
	}
	for( ws->tmpWrdEnd = ws->freeSpCur->freeSpace; ws->yytext < curr_symbol(ws); ) {
		i=0;
		//			while( *ws->yytext == '\'' )
		//				{
		//					ws->yytext++;
		//					i++;
		//				}
		//			while ( ws->yytext >  curr_symbol(ws) )
		//				{
		//					i--;
		//					ws->yytext--;
		//				}
		//			if ( i ) for ( i/=2; i; i-- )      *ws->tmpWrdEnd++='\'';    /*   in source text for each '' - result will '   */
		/*         \????????   */
		if ( *ws->yytext == '\\' ) {
			int tmpI;
			ws->yytext++;
			switch ( tolower( (int)*(ws->yytext) ) ) {
			case 'x':                       /*  \x01..9a..e  */
				i = 0;
				tmpI = 1;
				while( tmpI) {
					if (isdigit( (int)*(ws->yytext) ) ) {
						i = (i << 4) + ( *ws->yytext++ - '0' );
					}
					else if( tolower( (int) *(ws->yytext) ) >= 'a' && tolower( (int)*(ws->yytext) ) <= 'e' ) {
						i = (i << 4) + ( *ws->yytext++ - 'a' + 10 );
						}
					else {
						if ( tmpI & 1 ) {
							yyerror( ws, LEX_XFORM ); /* x format has odd number of symbols */
						}
						tmpI = 0;
					}
					if ( tmpI && !( tmpI++ & 1 ) ) {
						*ws->tmpWrdEnd++ = (unsigned char) i;
						i = 0;
					}
				}
				break;
			}
		}
		else *ws->tmpWrdEnd++ = *ws->yytext++;
		if( ws->tmpWrdEnd > (ws->freeSpCur->freeSpaceMax - sizeof(wrd_t)) ) {
			free_space_t * tmp;
			int i;
			assert ("VD sys_reiser4. selectet_word:Internal space buffer overflow: input token exceed size of bufer",
				ws->freeSpCur->freeSpace > ws->freeSpCur->freeSpaceBase);
			/* we can reallocate new space and copy all
			   symbols of current token inside it */
			tmp=ws->freeSpCur;
			ws->freeSpCur = freeSpaceNextAlloc(ws);
			assert ("VD sys_reiser4:Internal text buffer overflow: no enouse mem", ws->freeSpCur !=NULL);
			i = ws->tmpWrdEnd - tmp->freeSpace;
			memmove( ws->freeSpCur->freeSpace, tmp->freeSpace, i );
			ws->tmpWrdEnd = ws->freeSpCur->freeSpace + i;
		}
	}
	if (exclude) {
		ws->tmpWrdEnd--;
	}
	*ws->tmpWrdEnd++ = '\0';
}


/* compare parsed word with keywords*/
static int b_check_word(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	int i, j, l;
	j=sizeof(pars_key)/(sizeof(char*)+sizeof(int))-1;
	l=0;
	while( ( j - l ) >= 0 ) {
		i  =  ( j + l /*+ 1*/ ) >> 1;
		switch( strcmp( pars_key[i].wrd, ws->freeSpCur->freeSpace ) ) {
		case  0:
			return( pars_key[i].class );
			break;
		case  1: j = i - 1;               break;
		default: l = i + 1;               break;
		}
	}
	return(0);
}


/* comparing parsed word with already stored words, if not compared, storing it */
static
//__inline__
wrd_t * _wrd_inittab(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	wrd_t * cur_wrd;
	wrd_t * new_wrd;
	int len;
	new_wrd =  ws->wrdHead;
#if 0
	len = strlen( ws->freeSpCur->freeSpace) ;
#else
	len = ws->tmpWrdEnd - ws->freeSpCur->freeSpace - 1 ;
#endif
	cur_wrd = NULL;
	while ( !( new_wrd == NULL ) ) {
		cur_wrd = new_wrd;
		if ( cur_wrd->u.len == len ) {
			if( !memcmp( cur_wrd->u.name, ws->freeSpCur->freeSpace, cur_wrd->u.len ) ) {
				return cur_wrd;
			}
		}
		new_wrd = cur_wrd->next;
	}
	new_wrd         = ( wrd_t *)(ws->freeSpCur->freeSpace + ROUND_UP( len+1 ));
	new_wrd->u.name = ws->freeSpCur->freeSpace;
	new_wrd->u.len  = len;
	ws->freeSpCur->freeSpace= (char*)new_wrd + ROUND_UP(sizeof(wrd_t));
	new_wrd->next   = NULL;
	if (cur_wrd==NULL) {
		ws->wrdHead   = new_wrd;
	}
	else {
		cur_wrd->next = new_wrd;
	}
	return new_wrd;
}

/* lexical analisator for yacc automat */
static int reiser4_lex( struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	char term, n, i = 0;
	int ret = 0;
	char lcls;
//	char * s ;

//	s = curr_symbol(ws);              /* first symbol or Last readed symbol of the previous token parsing */
	if ( *curr_symbol(ws) == 0 ) return  0;        /* end of string is EOF */

	while(ncl[(int)*curr_symbol(ws)]==Blk) {
		next_symbol(ws);
		if ( *curr_symbol(ws) == 0 ) return  0;  /* end of string is EOF */
	}


	lcls    =       ncl[(int)*curr_symbol(ws)];
	ws->yytext  = curr_symbol(ws);
	term = 1;
	while( term ) {
		n=lcls;
		while (  n > 0   ) {
			next_symbol(ws);
			lcls=n;
			n = lexcls[ (int)lcls ].c[ (int)i=ncl[ (int)*curr_symbol(ws) ] ];
		}
		if ( n == OK ) {
			term=0;
		}
		else {
			yyerror ( ws, LEXERR2, (lcls-1)* 20+i );
			return(0);
		}
	}
	switch (lcls) {
	case Blk:
	case Ste:
		yyerror(ws,LEX_Ste);
		break;
	case Wrd:
		move_selected_word( ws, lexcls[(int) lcls ].c[0] );
		                                                    /* if ret>0 this is keyword */
		if ( !(ret = b_check_word(ws)) ) {                          /*  this is not keyword. tray check in worgs. ret = Wrd */
			ret=lexcls[(int) lcls ].term;
			ws->ws_yylval.wrd = _wrd_inittab(ws);
		}
		break;
	case Int:
	case Ptr:
	case Pru:
	case Str: /*`......"*/
		move_selected_word( ws, lexcls[(int) lcls ].c[0] );
		ret=lexcls[(int) lcls ].term;
		ws->ws_yylval.wrd = _wrd_inittab(ws);
		break;
		/*
		  move_selected_word( ws, lexcls[ lcls ].c[0] );
		  ret=lexcls[ lcls ].term;
		  ws->ws_yyval.w = _wrd_inittab(ws);
		  break;
		*/
	case Stb:
	case Com:
	case Mns:
	case Les:
	case Slh:
	case Bsl: /*\ */
	case Sp1: /*;*/
	case Sp2: /*:*/
	case Dot: /*.*/
	case Sp4: /*=*/
	case Sp5: /*>*/
	case Sp6: /*?*/
	case ASG:/*<-*/
	case App:/*<<-*/ /*???*/
	case Lnk:/*->*/
	case Pls:/*+*/
	case Nam:/*<=*/
		ret=lexcls[(int) lcls ].term;
		break;
	case Lpr:
	case Rpr:
		ws->ws_yylval.charType = CD_BEGIN ;
		ret=lexcls[(int) lcls ].term;
		break;
	case Lsq:
	case Rsq:
		ws->ws_yylval.charType = UNORDERED ;
		ret=lexcls[(int) lcls ].term;
		break;
	case Lfl:
	case Rfl:
		ws->ws_yylval.charType = ASYN_BEGIN ;
		ret=lexcls[(int) lcls ].term;
		break;
	default :                                /*  others  */
		ret=*ws->yytext;
		break;
	}
	printk("lexer:%d\n", ret );
	return ret;
}



/*==========================================================*/

/* allocate new expression @type */
static expr_v4_t * alloc_new_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
				  int type /* type of new expression */)
{
	expr_v4_t * e;
	e         = ( expr_v4_t *)  list_alloc( ws, sizeof(expr_v4_t));
	e->h.type = type;
	return e;
}

/* store NULL name in word table */
wrd_t * nullname(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	return make_new_word(ws,"");
}


/* initialize node  for root lnode */
static expr_v4_t *  init_root(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	expr_v4_t * e;
	e                     = alloc_new_expr( ws, EXPR_PARS_VAR );
	e->pars_var.v         = alloc_pars_var( ws, NULL );
	e->pars_var.v->w      = nullname(ws) ; /* or '/' ????? */
	e->pars_var.v->parent = NULL;
	ws->nd.flags          = LOOKUP_NOALT;

//	walk_init_root( "/", (&ws->nd));   /* from namei.c walk_init_root */
	{
		read_lock(&current->fs->lock);
		ws->nd.mnt = my_mntget("root", current->fs->rootmnt);
		ws->nd.dentry = my_dget("root", current->fs->root);
		read_unlock(&current->fs->lock);
	}
	if (push_var_val_stack( ws, e->pars_var.v, VAR_LNODE )) {
		printk("VD-init_root: push_var_val_stack error\n");
	}
	else e->pars_var.v->val->u.ln = get_lnode( ws );
	return e;
}


/* initialize node  for PWD lnode */
static expr_v4_t *  init_pwd(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	expr_v4_t * e;
	e                     = alloc_new_expr(ws,EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var(ws,ws->root_e->pars_var.v);
	e->pars_var.v->w      = nullname(ws) ;  /* better if it will point to full pathname for pwd */
	e->pars_var.v->parent = ws->root_e->pars_var.v;

//	path_lookup(".",,&(ws->nd));   /* from namei.c path_lookup */
	{
		read_lock(&current->fs->lock);
		ws->nd.mnt = my_mntget("pwd",current->fs->pwdmnt);
		ws->nd.dentry = my_dget("pwd",current->fs->pwd);
		read_unlock(&current->fs->lock);
	}
	current->total_link_count = 0;
	if (push_var_val_stack( ws, e->pars_var.v, VAR_LNODE )) {
		printk("VD-init_pwd: push_var_val_stack error\n");
	}
	else e->pars_var.v->val->u.ln=get_lnode( ws );
	return e;
}


/* initialize node  for PWD lnode */
static expr_v4_t *  init_pseudo_name(struct reiser4_syscall_w_space * ws /* work space ptr */,
				     char *name /* name of pseudo */)
{
	expr_v4_t * e;
	e                     = alloc_new_expr(ws,EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var(ws,ws->root_e->pars_var.v);
	e->pars_var.v->w      = make_new_word(ws, name);
	e->pars_var.v->parent = ws->root_e->pars_var.v;

	current->total_link_count = 0;
	push_var_val_stack( ws, e->pars_var.v, VAR_LNODE );
	e->pars_var.v->val->u.ln = get_lnode( ws );
	return e;
}


#if 0
static expr_v4_t *  pars_lookup(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	not ready;
	pars_var_t * rez_pars_var;
	pars_var_t * this_l;
	this_l = getFirstPars_Var(e1);
	while(this_l != NULL ) {
	}
	assert("pars_lookup:lnode is null",rez_pars_var->ln!=NULL);
	memcpy( &curent_dentry.d_name   , w, sizeof(struct qstr));<---------------
		if( ( rez_pars_var->ln = pars_var->ln->d_inode->i_op->lookup( pars_var->ln->d_inode, &curent_dentry) ) == NULL ) {
			/* lnode not exist: we will not need create it. this is error*/
		}
}
#endif

/*    Object_Name : begin_from name                 %prec ROOT       { $$ = pars_expr( ws, $1, $2 ) ; }
                  | Object_Name SLASH name                           { $$ = pars_expr( ws, $1, $3 ) ; }  */
static expr_v4_t *  pars_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
			      expr_v4_t * e1 /* first expression ( not yet used)*/,
			      expr_v4_t * e2 /* second expression*/)
{
	ws->cur_level->wrk_exp = e2;
	return e2;
}

/* not yet */
static pars_var_t * getFirstPars_VarFromExpr(struct reiser4_syscall_w_space * ws )
{
	pars_var_t * ret = 0;
	expr_v4_t * e = ws->cur_level->wrk_exp;
	switch (e->h.type) {
	case EXPR_PARS_VAR:
		ret = e->pars_var.v;
		break;
		//	default:

	}
	return ret;
}

/* seach @parent/w in internal table. if found return it, else @parent->lookup(@w) */
static pars_var_t * lookup_pars_var_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
					 pars_var_t * parent /* parent for w       */,
					 wrd_t * w        /* to lookup for word */,
					 int type)
{
	pars_var_t * rez_pars_var;
	struct dentry *de;
	int rez;
	pars_var_t * last_pars_var;
	last_pars_var  = NULL;
	rez_pars_var   = ws->Head_pars_var;
	printk("lookup_pars_var_word: parent: %p \n", parent);
	while ( rez_pars_var != NULL ) {
		if( rez_pars_var->parent == parent &&
		    rez_pars_var->w      == w ) {
			rez_pars_var->val->count++;
			return rez_pars_var;
		}
		last_pars_var = rez_pars_var;
		rez_pars_var  = rez_pars_var->next;
	}
//	reiser4_fs        = 0;
	rez_pars_var         = alloc_pars_var(ws, last_pars_var);
	rez_pars_var->w      = w;
	rez_pars_var->parent = parent;

	switch (parent->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		switch (parent->val->u.ln->h.type) {
		case LNODE_INODE:  /* not use it ! */
			de = d_alloc_anon(parent->val->u.ln->inode.inode);
			break;
		case LNODE_DENTRY:
			ws->nd.dentry = parent->val->u.ln->dentry.dentry;
			ws->nd.mnt    = parent->val->u.ln->dentry.mnt;
			ws->nd.flags  = LOOKUP_NOALT ;
			if ( link_path_walk( w->u.name, &(ws->nd) ) ) /* namei.c */ {
				printk("lookup error\n");
				push_var_val_stack( ws, rez_pars_var, VAR_TMP );
				rez_pars_var->val->u.ln = NULL;
			}
			else {
				push_var_val_stack( ws, rez_pars_var, VAR_LNODE );
				rez_pars_var->val->u.ln = lget( LNODE_DENTRY, get_inode_oid( ws->nd.dentry->d_inode) );
				{
					read_lock(&current->fs->lock);
					rez_pars_var->val->u.ln->dentry.mnt    = my_mntget("pars_v",ws->nd.mnt);
					rez_pars_var->val->u.ln->dentry.dentry = my_dget("pars_v",ws->nd.dentry);
					read_unlock(&current->fs->lock);
				}
			}
			break;
			/*
			  case LNODE_PSEUDO:
			  PTRACE(ws, "parent pseudo=%p",parent->ln->pseudo.host);
			  break;
			*/
		case LNODE_LW:
			break;
		case LNODE_REISER4_INODE:
			rez_pars_var->val->u.ln->h.type        = LNODE_REISER4_INODE /* LNODE_LW */;
#if 0                   /*   NOT_YET  ???? */
			//			ln                = lget( LNODE_DENTRY, get_key_objectid(&key ) );
			result = coord_by_key(get_super_private(parent->val->ln->lw.lw_sb)->tree,
					      parent->val->ln->lw.key,
					      &coord,
					      &lh,
					      ZNODE_READ_LOCK,
					      FIND_EXACT,
					      LEAF_LEVEL,
					      LEAF_LEVEL,
					      CBK_UNIQUE,
					      0);
			//			if (REISER4_DEBUG && result == 0)
			//				check_sd_coord(coord, key);
			if (result != 0) {
				lw_key_warning(parent->val->ln->lw.key, result);
			}
			else {
				switch(item_type_by_coord(coord)) {
				case STAT_DATA_ITEM_TYPE:
					printk("VD-item type is STAT_DATA\n");
				case DIR_ENTRY_ITEM_TYPE:
					printk("VD-item type is DIR_ENTRY\n");
					iplug = item_plugin_by_coord(coord);
					if (iplug->b.lookup != NULL) {
						iplug->b.lookup();   /*????*/
					}

				case INTERNAL_ITEM_TYPE:
					printk("VD-item type is INTERNAL\n");
				case ORDINARY_FILE_METADATA_TYPE:
				case OTHER_ITEM_TYPE:
					printk("VD-item type is OTHER\n");
				}
			}
			/*??  lookup_sd     find_item_obsolete */
#endif
		case LNODE_NR_TYPES:
			break;
		}
		break;
	case VAR_TMP:
		push_var_val_stack( ws, rez_pars_var, VAR_TMP );
		rez_pars_var->val->u.ln = NULL;
		break;
	}

	return rez_pars_var;
}


/* search pars_var for @w */
static expr_v4_t *  lookup_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
				wrd_t * w /* word to search for */)
{
	expr_v4_t * e;
	pars_var_t * cur_pars_var;
#if 1           /* tmp.  this is fist version.  for II we need do "while" throus expression for all pars_var */
	cur_pars_var        = ws->cur_level->wrk_exp->pars_var.v;
#else
	cur_pars_var       = getFirstPars_VarFromExpr(ws);
	while(cur_pars_var!=NULL) {
#endif
		e                = alloc_new_expr( ws, EXPR_PARS_VAR );
		e->pars_var.v    = lookup_pars_var_word( ws, cur_pars_var, w , VAR_LNODE);
#if 0
		cur_pars_var=getNextPars_VarFromExpr(ws);
	}
	all rezult mast be connected to expression.
#endif
	return e;
}

/* set work path in level to current in level */
static inline expr_v4_t * pars_lookup_curr(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	printk("lookup curr\n");
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for pwd of level */
	return ws->cur_level->wrk_exp;
}

/* set work path in level to root */
static inline expr_v4_t * pars_lookup_root(struct reiser4_syscall_w_space * ws)
{
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return ws->cur_level->wrk_exp;
}



#if 0
/*?????*/

/* implementation of lookup_name() method for hashed directories

   it looks for name specified in @w in reiser4_inode @parent and if name is found - key of object found entry points
   to is stored in @key */
reiser4_internal int
lookup_name_hashed_reiser4(reiser4_inode *parent /* reiser4 inode of directory to lookup for name in */,
			    wrd_t *w             /* name to look for */,
			    reiser4_key *key     /* place to store key */)
{
	int result;
	coord_t *coord;
	lock_handle lh;
	const char *name;
	int len;
	reiser4_dir_entry_desc entry;

	assert("nikita-1247", parent != NULL);
	assert("nikita-1248", w != NULL);

??	assert("vs-1486", dentry->d_op == &reiser4_dentry_operations);

	result = reiser4_perm_chk(parent, lookup, parent, &w->u);


	if (result != 0)
		return 0;

	name = w->u.name;
	len = w->u.len;

	if ( len > parent->pset->dir_item)
		/* some arbitrary error code to return */
		return RETERR(-ENAMETOOLONG);

	coord = &reiser4_get_dentry_fsdata(dentry)->dec.entry_coord; ???????
	coord_clear_iplug(coord);




	init_lh(&lh);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "lookup inode: %lli \"%s\"\n", get_inode_oid(parent), dentry->d_name.name);

	/* find entry in a directory. This is plugin method. */


	//	result = find_entry(parent, dentry, &lh, ZNODE_READ_LOCK, &entry);


	if (result == 0) {
		/* entry was found, extract object key from it. */
		result = WITH_COORD(coord, item_plugin_by_coord(coord)->s.dir.extract_key(coord, key));
	}
	done_lh(&lh);
	return result;

}

node_plugin_by_node(coord->node)->lookup(coord->node, key, FIND_MAX_NOT_MORE_THAN, &twin);
item_type_by_coord(coord)

/*
 * try to look up built-in pseudo file by its name.
 */
reiser4_internal int
lookup_pseudo_file(reiser4_inode *parent /* reiser4 inode of directory to lookup for name in */,
			    wrd_t *w             /* name to look for */,
			    reiser4_key *key     /* place to store key */)
     //		   struct dentry * dentry)
{
	reiser4_plugin *plugin;
	const char     *name;
	struct inode   *pseudo;
	int             result;






	assert("nikita-2999", parent != NULL);
	assert("nikita-3000", dentry != NULL);

	/* if pseudo files are disabled for this file system bail out */
	if (reiser4_is_set(parent->i_sb, REISER4_NO_PSEUDO))
		return RETERR(-ENOENT);

	name = dentry->d_name.name;
	pseudo = ERR_PTR(-ENOENT);
	/* scan all pseudo file plugins and check each */
	for_all_plugins(REISER4_PSEUDO_PLUGIN_TYPE, plugin) {
		pseudo_plugin *pplug;

		pplug = &plugin->pseudo;
		if (pplug->try != NULL && pplug->try(pplug, parent, name)) {
			pseudo = add_pseudo(parent, pplug, dentry);
			break;
		}
	}
	if (!IS_ERR(pseudo))
		result = 0;
	else
		result = PTR_ERR(pseudo);
	return result;
}

#endif

static int lookup_pars_var_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */,
				    pars_var_t * parent /* parent for w       */,
				    wrd_t * w        /* to lookup for word */)
{
	struct dentry  * de, * de_rez;
	int rez;
	pars_var_t * rez_pars_var;
	reiser4_key key,* k_rez;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;

//		case EXPR_PARS_VAR:
//			/* not yet */
//			ws->nd.dentry=parent->ln->dentry.dentry;
//			de_rez = link_path_walk( w->u.name, &(ws->nd) ); /* namei.c */
//			break;

	PTRACE(ws, " w->u.name= %p, u.name->%s, u.len=%d",w->u.name,w->u.name,w->u.len);

	return rez;

}




/* if_then_else procedure */
static expr_v4_t * if_then_else(struct reiser4_syscall_w_space * ws /* work space ptr */,
				expr_v4_t * e1 /* expression of condition */,
				expr_v4_t * e2 /* expression of then */,
				expr_v4_t * e3 /* expression of else */ )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

/* not yet */
static expr_v4_t * if_then(struct reiser4_syscall_w_space * ws /* work space ptr */,
			   expr_v4_t * e1 /**/,
			   expr_v4_t * e2 /**/ )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

/* not yet */
static void goto_end(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
}


/* STRING_CONSTANT to expression */
static expr_v4_t * const_to_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
			       wrd_t * e1 /* constant for convert to expression */)
{
	expr_v4_t * new_expr = alloc_new_expr(ws, EXPR_WRD );
	new_expr->wd.s = e1;
	return new_expr;
}

/* allocate EXPR_OP2  */
static expr_v4_t * allocate_expr_op2(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr */,
				      expr_v4_t * e2 /* second expr */,
				      int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr( ws, EXPR_OP2 );
	assert("VD alloc op2", ret!=NULL);
	ret->h.exp_code = op;
	ret->op2.op_l = e1;
	ret->op2.op_r = e2;
	return ret;
}

/* allocate EXPR_OP  */
static expr_v4_t * allocate_expr_op(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr */,
 				      int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr(ws, EXPR_OP2 );
	assert("VD alloc op2", ret!=NULL);
	ret->h.exp_code = op;
	ret->op.op = e1;
	return ret;
}


/* concatenate expressions */
static expr_v4_t * concat_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of concating */,
				      expr_v4_t * e2 /* second expr of concating */)
{
	return allocate_expr_op2( ws, e1, e2, CONCAT );
}


/* compare expressions */
static expr_v4_t * compare_EQ_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_EQ );
}


static expr_v4_t * compare_NE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_NE );
}


static expr_v4_t * compare_LE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LE );
}


static expr_v4_t * compare_GE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GE );
}


static expr_v4_t * compare_LT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LT );
}


static expr_v4_t * compare_GT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GT );
}


static expr_v4_t * compare_OR_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_OR );
}


static expr_v4_t * compare_AND_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_AND );
}


static expr_v4_t * not_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */)
{
	return allocate_expr_op( ws, e1, COMPARE_NOT );
}


/**/
static expr_v4_t * check_exist(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */)
{
	return e1;
}

/* union lists */
static expr_v4_t * union_lists(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of connecting */,
				      expr_v4_t * e2 /* second expr of connecting */)
{
	expr_list_t *next, *last;
	assert("VD-connect_list", e1->h.type == EXPR_LIST);

	last = (expr_list_t *)e1;
	next = e1->list.next;
                   /* find last in list */
	while ( next ) {
		last = next;
		next = next->next;
	}
	if ( e2->h.type == EXPR_LIST ) {                       /* connect 2 lists */
		last->next = (expr_list_t *) e2;
	}
	else {                      /* add 2 EXPR to 1 list */
		next = (expr_list_t *) alloc_new_expr(ws, EXPR_LIST );
		assert("VD alloct list", next!=NULL);
		next->next = NULL;
		next->source = e2;
		last->next = next;
	}
	return e1;
}


/*  make list from expressions */
static expr_v4_t * list_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of list */,
				      expr_v4_t * e2 /* second expr of list */)
{
	expr_v4_t * ret;

	if ( e1->h.type == EXPR_LIST ) {
		ret = union_lists( ws, e1, e2);
	}
	else {

		if ( e2->h.type == EXPR_LIST ) {
			ret = union_lists( ws, e2, e1);
		}
		else {
			ret = alloc_new_expr(ws, EXPR_LIST );
			assert("VD alloct list 1", ret!=NULL);
			ret->list.source = e1;
			ret->list.next = (expr_list_t *)alloc_new_expr(ws, EXPR_LIST );
			assert("VD alloct list 2",ret->list.next!=NULL);
			ret->list.next->next = NULL;
			ret->list.next->source = e2;
		}
	}
	return ret;
}



static expr_v4_t * list_async_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return list_expression( ws, e1 , e2  );
}


static expr_v4_t * unname( struct reiser4_syscall_w_space * ws, expr_v4_t * e1 )
{
	return e1;
}



static expr_v4_t * assign(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	/* while for each pars_var in e1 */
	return pump( ws, e1->pars_var.v, e2 );   /* tmp.  */
}



static expr_v4_t * assign_invert(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e2;
}

/* not yet */
static expr_v4_t * symlink(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e2;
}



/*
 A flow is a source from which data can be obtained. A Flow can be one of these types:

   1. memory area in user space. (char *area, size_t length)
   2. memory area in kernel space. (caddr_t *area, size_t length)
   3. file-system object (lnode *obj, loff_t offset, size_t length)
*/
#if 0
typedef struct connect connect_t;

struct connect
{
	expr_v4_t * (*u)(pars_var_t *dst, expr_v4_t *src);
};

static expr_v4_t * reiser4_assign( pars_var_t *dst, expr_v4_t *src )
{
    int           ret_code;
    file_plugin  *src_fplug;
    file_plugin  *dst_fplug;
    connect_t     connection;

    /*
     * select how to transfer data from @src to @dst.
     *
     * Default implementation of this is common_transfer() (see below).
     *
     * Smart file plugin can choose connection based on type of @dst.
     *
     */
#if 0
    connection = dst->v->fplug -> select_connection( src, dst );
#else
    /*    connection.u=common_transfer;*/
#endif

    /* do transfer */
    return common_transfer( &dst, &src );
}

#endif


static  int source_not_empty(expr_v4_t *source)
{
	return 0;
}

static mm_segment_t __ski_old_fs;


#define START_KERNEL_IO_GLOB	                \
		__ski_old_fs = get_fs();	\
		set_fs( KERNEL_DS )

#define END_KERNEL_IO_GLOB			\
		set_fs( __ski_old_fs );

#define PUMP_BUF_SIZE (PAGE_CACHE_SIZE)


static int push_tube_stack( tube_t * tube, long type, void * pointer )
{
	sourece_stack_t * ret;
	ret = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(ret)) {
		ret->prev        = tube->st_current;
		ret->type        = type;
		ret->u.pointer   = pointer;
		tube->st_current = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static int push_tube_list_stack_done(tube_t * tube)
{
	tube->next->prev = tube->st_current;
	tube->st_current = tube->last;
	tube->last       = NULL;
	tube->next       = NULL;
	return 0;
}


static int push_tube_list_stack_init( tube_t * tube, long type, void * pointer )
{
	sourece_stack_t * ret;
	tube->last = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(tube->last)) {
		tube->next       = tube->last;
		ret->type        = type;
		ret->u.pointer   = pointer;
		return 0;
	}
	else {
		return PTR_ERR(tube->last);
	}
}

static int push_tube_list_stack(tube_t * tube, long type, void * pointer )
{
	sourece_stack_t * ret;
	ret = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(ret)) {
		tube->next->prev = ret;
		ret->type        = type;
		ret->u.pointer   = pointer;
		tube->next       = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static int change_tube_stack(tube_t * tube, long type, void * pointer )
{
	tube->st_current->type       = type;
	tube->st_current->u.pointer  = pointer;
	return 0;
}

static int pop_tube_stack( tube_t * tube )
{
	sourece_stack_t * ret;
	if ( tube->st_current == NULL ) {
		return -1;
	}
	else {
		ret              = tube->st_current;
		tube->st_current = tube->st_current->prev;
		kfree( ret );
		return 0;
	}
}

static int push_var_val_stack(struct reiser4_syscall_w_space *ws /* work space ptr */,
			      struct pars_var * var,
			      long type)
{
	pars_var_value_t * ret;
	ret = kmalloc( sizeof(pars_var_value_t), GFP_KERNEL );
	memset( ret , 0, sizeof( pars_var_value_t ));
	if (!IS_ERR(ret)) {
		ret->prev       = var->val;
		ret->vtype      = type;
		ret->host       = var;
		ret->count      = 1;
		ret->len        = -11;
		ret->off        = 0;
		ret->next_level = ws->cur_level->val_level;
		ws->cur_level->val_level = ret;
		var->val        = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static int pop_var_val_stack( struct reiser4_syscall_w_space *ws /* work space ptr */,
			      pars_var_value_t * val )
{
	pars_var_value_t * ret;
	if ( val == NULL ) {
		return -1;
	}
	else {
		switch(val->vtype) {
#if 0
		case VAR_EMPTY:
			break;
		case VAR_LNODE:
			assert("VD-pop_var_val_stack.VAR_LNODE", val->u.ln!=NULL);
			printk("var->val->count %d\n", val->count );
			//		if ( !--var->val->count )
			{
				path4_release( val->u.ln->dentry );
				lput( val->u.ln );
			}
			break;
#endif
		case VAR_TMP:
			if (val->u.data!=NULL) {
				kfree( val->u.data );
			}
			val->host->val           = val->prev;
			ws->cur_level->val_level = val->next_level;
			kfree( val );
			break;
		}
		return 0;
	}
}

/*  pop onto stack for calculate expressions one step */
static void put_tube_src(tube_t * tube)
{
	/*  close readed file and pop stack */
	switch (tube->st_current->type) {
	case 	ST_FILE:
		filp_close(tube->st_current->u.file, current->files );
	case 	ST_DE:
	case 	ST_WD:
	case 	ST_DATA:
		pop_tube_stack(tube);
		break;
	}
}

/* push & pop onto stack for calculate expressions one step */
static int get_tube_next_src(tube_t * tube)
{
	expr_v4_t * s;
	expr_list_t * tmp;
	int ret;
	struct file * fl;

	tube->readoff=0;
	assert ("VD stack is empty", tube->st_current != NULL );

	/* check stack and change its head */
	switch (tube->st_current->type) {
	case 	ST_FILE:
	case 	ST_DE:
	case 	ST_WD:
		ret = 0;
		break;
	case 	ST_EXPR:
		s = tube->st_current->u.expr;
		switch (s->h.type) {
		case EXPR_WRD:
			change_tube_stack( tube, ST_WD , s->wd.s );
			break;
		case EXPR_PARS_VAR:
			assert("VD-free_expr.EXPR_PARS_VAR", s->pars_var.v!=NULL);
			switch( s->pars_var.v->val->vtype) {
			case VAR_EMPTY:
				break;
			case VAR_LNODE:
				assert("VD-free_expr.EXPR_PARS_VAR.ln", s->pars_var.v->val->u.ln!=NULL);
				//					if ( S_ISREG(s->pars_var.v->val->u.ln->dentry.dentry->d_inode) )
				{
					fl=  dentry_open( s->pars_var.v->val->u.ln->dentry.dentry,
							  s->pars_var.v->val->u.ln->dentry.mnt, O_RDONLY ) ;
					if ( !IS_ERR(fl) ) {
						change_tube_stack( tube, ST_FILE , fl);
					}
					else printk("error for open source\n");
				}
#if 0          // not yet
				else if ( S_ISDIR(s->pars_var.v->val->u.ln->dentry.dentry->d_inode) ) {
					while(NOT EOF)
						{
							readdir();
						}
				}
#endif
				ret = 0;
				break;
			case VAR_TMP:
				if ( s->pars_var.v->val->u.data == NULL )
					{
						pop_tube_stack( tube );
						ret = 1;
					}
				else
					{
						change_tube_stack( tube, ST_DATA , s->pars_var.v->val->u.data);
						ret = 0;
					}
				break;
			}
			break;
		case EXPR_LIST:
			tmp = &s->list;
			push_tube_list_stack_init( tube, ST_EXPR , tmp->source );
			while (tmp) {
				tmp = tmp->next;
				push_tube_list_stack( tube, ST_EXPR, tmp->source );
			}
			pop_tube_stack( tube );
			push_tube_list_stack_done( tube );
			ret = 1;
			break;
		case EXPR_ASSIGN:
#if 0   // not yet
			assert("VD-free_expr.EXPR_ASSIGN", s->assgn.target!=NULL);
			assert("VD-free_expr.EXPR_ASSIGN.ln", s->assgn.target->ln!=NULL);
			assert("VD-free_expr.EXPR_ASSIGN.count", s->assgn.target->val->count>0);
			( s->assgn.target->ln);
			( s->assgn.source );
#endif
			break;
		case EXPR_LNODE:
			assert("VD-free_expr.lnode.lnode", s->lnode.lnode!=NULL);
//					if ( S_ISREG(s->lnode.lnode->dentry.dentry->d_inode) )
			{
				change_tube_stack( tube, ST_FILE ,
						   dentry_open( s->lnode.lnode->dentry.dentry,
								s->lnode.lnode->dentry.mnt, O_RDONLY ) );
			}
			ret = 0;
			break;
		case EXPR_FLOW:
			break;
		case EXPR_OP2:
			change_tube_stack( tube, ST_EXPR , s->op2.op_r );
			push_tube_stack( tube, ST_EXPR , s->op2.op_l );
			ret = 1;
			break;
		case EXPR_OP:
			change_tube_stack( tube, ST_EXPR , s->op.op );
			ret = 1;
			break;
		}
		break;
	}
	return ret;
}


static tube_t *  get_tube_general(tube_t * tube, pars_var_t *sink, expr_v4_t *source)
{

	char * buf;
	tube = kmalloc( sizeof(struct tube), GFP_KERNEL);
	if (!IS_ERR(tube)) {
		START_KERNEL_IO_GLOB;
		memset( tube , 0, sizeof( struct tube ));
		assert("VD get_tube_general: no tube",!IS_ERR(tube));
		printk("get_tube_general:%d\n",sink->val->vtype);
		tube->target = sink;
		switch( sink->val->vtype ) {
		case VAR_EMPTY:
			break;
		case VAR_LNODE:
			printk("VAR_LNODE\n");
			assert("VD get_tube_general: dst no dentry",sink->val->u.ln->h.type== LNODE_DENTRY);
			tube->dst         = dentry_open( sink->val->u.ln->dentry.dentry,
							 sink->val->u.ln->dentry.mnt,
							 O_WRONLY|O_TRUNC );
			tube->writeoff    = 0;
			tube->st_current  = NULL;
			push_tube_stack( tube, ST_EXPR, (long *)source );
			break;
		case VAR_TMP:
			printk("VAR_TMP\n");
			break;
		}
	}
	return tube;
}

static size_t reserv_space_in_sink(tube_t * tube )
{
	tube->buf = kmalloc( PUMP_BUF_SIZE, GFP_KERNEL);
	if (!IS_ERR(tube->buf)) {
		memset( tube->buf  , 0, PUMP_BUF_SIZE);
		return PUMP_BUF_SIZE;
	}
	else {
		return 0;
	}
}

static size_t get_available_src_len(tube_t * tube)
{
	size_t len;
	size_t s_len;
	int ret = 1;
	len = PUMP_BUF_SIZE;
	while ( tube->st_current != NULL && ret ) {
		ret = 0;
		switch( tube->st_current->type ) {
		case 	ST_FILE:
			s_len = tube->st_current->u.file->f_dentry->d_inode->i_size;
			/* for reiser4 find_file_size() */
			break;
		case 	ST_DE:
			break;
		case 	ST_WD:
			s_len = tube->st_current->u.wd->u.len;
			break;
		case 	ST_EXPR:
			while( tube->st_current != NULL && get_tube_next_src( tube ) ) ;
			len = -1;
			ret = 1;
			break;
		case ST_DATA:
			s_len = strlen((char *)tube->st_current->u.pointer);
			break;
			}
	}
	s_len -= tube->readoff;
	if (tube->st_current == NULL) {
		len = 0;
	}
	else {
		if ( len > s_len ) len = s_len;
	}
	return len;
}

static size_t prep_tube_general(tube_t * tube)
{
	size_t ret;
	if ( tube->st_current != NULL ) {
		ret = get_available_src_len( tube ) ;
	}
	else {
		ret = 0;
	}
	tube->len = ret;
	return ret;
}



static size_t source_to_tube_general(tube_t * tube)
{
	//	tube->source->fplug->read(tube->offset,tube->len);
	size_t ret;
	switch( tube->st_current->type ) {
	case 	ST_FILE:
		ret = vfs_read(tube->st_current->u.file, tube->buf, tube->len, &tube->readoff);
		tube->len = ret;
		break;
	case 	ST_DE:
		break;
	case 	ST_WD:
		if ( tube->readoff < tube->st_current->u.wd->u.len ) {
			assert ("VD source to tube(wd)", tube->readoff+tube->len <= tube->st_current->u.wd->u.len);
			memcpy( tube->buf,  tube->st_current->u.wd->u.name + tube->readoff, ret = tube->len );
			tube->readoff += ret;
		}
		else ret = 0;
	case ST_DATA:
		if ( tube->readoff < strlen((char *)tube->st_current->u.pointer) ) {
			memcpy( tube->buf,  tube->st_current->u.pointer + tube->readoff, ret = tube->len );
			tube->readoff += ret;
		}
		else ret = 0;
		break;
	}
	return ret;
}

static size_t tube_to_sink_general(tube_t * tube)
{
	size_t ret;
//	tube->sink->fplug->write(tube->offset,tube->len);
//	tube->offset += tube->len;
	switch(tube->target->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		ret = vfs_write(tube->dst, tube->buf, tube->len, &tube->writeoff);
		break;
	case VAR_TMP:
		// memncpy(tube->target->data,tube->buf,tube->len);
		// or
		// tube->target->data=tube->buf;
		// ?
		printk("write to pseudo not yet work\n");
		tube->writeoff+=tube->len;
		break;
	}
	return ret;

}

static void put_tube(tube_t * tube)
{
	PTRACE1( "%s\n", "begin");
	END_KERNEL_IO_GLOB;
	assert("VD :stack not empty ",tube->st_current == NULL);
	switch(tube->target->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		do_truncate( tube->dst->f_dentry, tube->writeoff);
		filp_close(tube->dst, current->files );
		break;
	case VAR_TMP:
		break;
	}
	kfree(tube->buf);
	kfree(tube);
}

static int create_result_field(struct reiser4_syscall_w_space * ws,
			       pars_var_t *parent,   /* parent for name */
			       char * name ,         /* created name    */
			       int result_len,       /* length of allocated space for value */
			       int result)
{
	int ret;
	wrd_t * tmp_wrd;
	pars_var_t * rezult;

	tmp_wrd = make_new_word(ws, name );
	rezult =  lookup_pars_var_word( ws , parent, tmp_wrd, VAR_TMP);
	if ( rezult != NULL ) {
		rezult->val->u.data  = kmalloc( result_len, GFP_KERNEL ) ;
		if ( rezult->val->u.data ) {
			sprintf( rezult->val->u.data, "%d", result );
			ret=0;
		}
	}
	else {
		ret = 1;
	}
	return ret;
}

static int create_result(struct reiser4_syscall_w_space * ws,
			      pars_var_t *parent,   /* parent for name       */
			      int err_code ,        /* error code of assign  */
			      int length)           /* length of assign      */
{
	int ret;
	ret  = create_result_field( ws, parent,
			     ASSIGN_RESULT, SIZEFOR_ASSIGN_RESULT, err_code );
	ret += create_result_field( ws, parent,
			     ASSIGN_LENGTH, SIZEFOR_ASSIGN_LENGTH, length );
	return ret;
}



/*
  Often connection() will be a method that employs memcpy(). Sometimes
  copying data from one file plugin to another will mean transforming
  the data. What reiser4_assign does depends on the type of the flow
  and sink. If @flow is based on the kernel-space area, memmove() is
  used to copy data. If @flow is based on the user-space area,
  copy_from_user() is used. If @flow is based on a file-system object,
  flow_place() uses the page cache as a universal translator, loads
  the object's data into the page cache, and then copies them into
  @area. Someday methods will be written to copy objects more
  efficiently than using the page cache (e.g. consider copying holes
  [add link to definition of a hole]), but this will not be
  implemented in V4.0.
*/
static expr_v4_t *  pump(struct reiser4_syscall_w_space * ws, pars_var_t *sink, expr_v4_t *source )
{
	pars_var_t * assoc;
	expr_v4_t * ret;
	tube_t * tube;
	int ret_code;
	size_t (*prep_tube)(tube_t *);
	size_t (*source_to_tube)(tube_t *);
	size_t (*tube_to_sink)(tube_t *);
	PTRACE1( "%s", "begin");

	/* remember to write code for freeing tube, error handling, etc. */
#if 0
	ret_code = sink->fplug -> get_tube( tube, sink, source);
	prep_tube = sink->fplug->prep_tube (tube);
	source_to_tube = source->fplug->source_to_tube;
	tube_to_sink = sink->fplug->tube_to_sink;
#else
	tube       = get_tube_general( tube, sink, source);
	if ( tube == NULL ) {
		ret_code = -1;
	}
	else {
		prep_tube      = prep_tube_general;
		source_to_tube = source_to_tube_general;
		tube_to_sink   = tube_to_sink_general;
#endif
		reserv_space_in_sink( tube );

		while ( tube->st_current != NULL ) {
			if ( ret_code = prep_tube( tube ) >0 ) {
				while ( ret_code = source_to_tube( tube ) > 0 ) {
					ret_code = tube_to_sink( tube );
				}
				if ( ret_code < 0 ) {
					printk("IO error\n");
				}
				put_tube_src( tube );
			}
		}


		ret=alloc_new_expr( ws, EXPR_PARS_VAR );

		if ( sink->val->associated == NULL) {
			create_result( ws, sink, ret_code, tube->writeoff );
			ret->pars_var.v = sink;
		}
		else {
			create_result( ws, sink->val->associated, ret_code, tube->writeoff );
			ret->pars_var.v = sink->val->associated;
			sink->val->associated == NULL;
		}

#if 0
		tmp_wrd = make_new_word(ws, ASSIGN_RESULT );
		rezult =  lookup_pars_var_word( ws , assoc, tmp_wrd, VAR_TMP);
		rezult->val->u.data  = kmalloc( SIZEFOR_ASSIGN_RESULT, GFP_KERNEL ) ;
		sprintf( rezult->val->u.data, "%d", ret_code );

		tmp_wrd = make_new_word(ws, ASSIGN_LENGTH );
		length =  lookup_pars_var_word( ws , assoc, tmp_wrd, VAR_TMP);
		length->val->u.data  = kmalloc( SIZEFOR_ASSIGN_LENGTH, GFP_KERNEL ) ;
		sprintf( length->val->u.data, "%d", tube->writeoff );


		/*
		ret1=alloc_new_expr( ws, EXPR_PARS_VAR );
		ret->pars_var.v = rezult;
		ret1=alloc_new_expr( ws, EXPR_PARS_VAR );
		ret->pars_var.v = length;
		ret = list_expression( ws, list_expression( ws, ret, ret1 ), ret2 ) ;
		 */
#endif
		put_tube( tube );
      }
      return ret;
}




/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
