/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * parser.y
 */

                                 /* level type defines */

#include "../forward.h"
#include "../debug.h"
#include "../dformat.h"
#include "../key.h"
#include "../type_safe_list.h"
#include "../plugin/plugin_header.h"
#include "../plugin/item/static_stat.h"
#include "../plugin/item/internal.h"
#include "../plugin/item/sde.h"
#include "../plugin/item/cde.h"
#include "../plugin/item/extent.h"
#include "../plugin/item/tail.h"
#include "../plugin/file/file.h"
#include "../plugin/symlink.h"
#include "../plugin/dir/hashed_dir.h"
#include "../plugin/dir/dir.h"
#include "../plugin/item/item.h"
#include "../plugin/node/node.h"
#include "../plugin/node/node40.h"
#include "../plugin/security/perm.h"
#include "../plugin/space/bitmap.h"
#include "../plugin/space/space_allocator.h"
#include "../plugin/disk_format/disk_format40.h"
#include "../plugin/disk_format/disk_format.h"

#include <linux/fs.h>		/* for struct super_block, address_space  */
#include <linux/mm.h>		/* for struct page */
#include <linux/buffer_head.h>	/* for struct buffer_head */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/types.h>

typedef enum {
	TW_BEGIN,
	ASYN_BEGIN,
	CD_BEGIN,
	OP_LEVEL,
	NOT_HEAD,
	IF_STATEMENT,
	UNORDERED
} def;

//#define printf(p1,...) PTRACE(ws,p1,...)
#define yylex()  reiser4_lex(ws)
#define register
#define  yyacc
//#define  bizon

#define  PARSER_DEBUG


#if 1
#define PTRACE(ws, format, ... )						\
({										\
	ON_TRACE(TRACE_PARSE, "parser:%s %p %s: " format "\n",	                \
		 __FUNCTION__, ws, (ws)->ws_pline, __VA_ARGS__);		\
})
#else
#define PTRACE(ws, format, ... )						\
({										\
	printk("parser:%s %p %s: " format "\n",	                \
		 __FUNCTION__, ws, (ws)->ws_pline, __VA_ARGS__);		\
})
#endif

#define PTRACE1( format, ... )				        		\
({										\
	ON_TRACE(TRACE_PARSE, "parser:%s  " format "\n",	                \
		 __FUNCTION__,  __VA_ARGS__);					\
})


#define ASSIGN_RESULT "assign_result"
#define ASSIGN_LENGTH "assign_length"

#define SIZEFOR_ASSIGN_RESULT 16
#define SIZEFOR_ASSIGN_LENGTH 16





typedef struct pars_var pars_var_t;
typedef union expr_v4  expr_v4_t;
typedef struct wrd wrd_t;
typedef struct tube tube_t;
typedef struct sourece_stack sourece_stack_t;

typedef enum {
	ST_FILE,
	ST_EXPR,
	ST_DE,
	ST_WD,
	ST_DATA
} stack_type;

typedef enum {
	noV4Space,
	V4Space,
	V4Plugin
} SpaceType;

typedef enum {
	CONCAT,
	COMPARE_EQ,
	COMPARE_NE,
	COMPARE_LE,
	COMPARE_GE,
	COMPARE_LT,
	COMPARE_GT,
	COMPARE_OR,
	COMPARE_AND,
	COMPARE_NOT
} expr_code_type;


                                 /* sizes defines      */
#define FREESPACESIZE_DEF PAGE_SIZE*4
#define FREESPACESIZE (FREESPACESIZE_DEF - sizeof(char*)*2 - sizeof(int) )

#define _ROUND_UP_MASK(n) ((1UL<<(n))-1UL)

#define _ROUND_UP(x,n) (((long)(x)+_ROUND_UP_MASK(n)) & ~_ROUND_UP_MASK(n))

// to be ok for alpha and others we have to align structures to 8 byte  boundary.


#define ROUND_UP(x) _ROUND_UP((x),3)



struct tube {
	int type_offset;
	char * offset;       /* pointer to reading position */
	size_t len;            /* lenth of current operation
                               (min of (max_of_read_lenth and max_of_write_lenth) )*/
	long used;
	char * buf;          /* pointer to bufer */
	loff_t readoff;      /* reading offset   */
	loff_t writeoff;     /* writing offset   */

 	sourece_stack_t * last;        /* work. for special case to push list of expressions */
	sourece_stack_t * next;        /* work. for special case to push list of expressions */
	sourece_stack_t * st_current;  /* stack of source expressions */
	pars_var_t * target;
	struct file *dst;    /* target file      */
};

struct wrd {
	wrd_t * next ;                /* next word                   */
	struct qstr u ;               /* u.name  is ptr to space     */
};


struct path_walk {
	struct dentry *dentry;
	struct vfsmount *mnt;
};


/* types for vtype of struct pars_var */
typedef enum {
	VAR_EMPTY,
	VAR_LNODE,
	VAR_TMP
};

typedef struct pars_var_value pars_var_value_t;

struct pars_var {
	pars_var_t * next ;         /* next                                */
	pars_var_t * parent;        /* parent                              */
	wrd_t * w ;                 /* name: pair (parent,w) is unique     */
	pars_var_value_t * val;
};

struct pars_var_value {
	pars_var_value_t * prev;
	pars_var_value_t * next_level;
	pars_var_t * host;
	pars_var_t * associated;
	int vtype;                  /* Type of value                       */
	union {
	lnode * ln;         /* file/dir name lnode                 */
	char *data;         /*  ptr to data in mem (for result of assign) */
	} u;
	int count;                  /* ref counter                         */
	size_t off;	            /* current offset read/write of object */
	size_t len;		    /* length of sequence of bytes for read/write (-1 no limit) */
	int vSpace  ;               /* v4  space name or not ???           */
	int vlevel  ;               /* level :     lives of the name       */
} ;

typedef struct expr_common {
	__u8          type;
	__u8          exp_code;
} expr_common_t;

typedef struct expr_lnode {
	expr_common_t   h;
	lnode  *lnode;
} expr_lnode_t;

typedef struct expr_flow {
	expr_common_t    h;
	flow_t     *   flw;
} expr_flow_t;

typedef struct expr_pars_var {
	expr_common_t   h;
	pars_var_t  *  v;
} expr_pars_var_t;


typedef struct expr_wrd {
	expr_common_t   h;
	wrd_t  *  s;
} expr_wrd_t;

typedef struct expr_op3 {
	expr_common_t   h;
	expr_v4_t  *  op;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op3_t;

typedef struct expr_op2 {
	expr_common_t   h;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op2_t;

typedef struct expr_op {
	expr_common_t   h;
	expr_v4_t  *  op;
} expr_op_t;

typedef struct expr_assign {
	expr_common_t   h;
	pars_var_t       *  target;
	expr_v4_t       *  source;
//	expr_v4_t       *  (* construct)( lnode *, expr_v4_t *  );
} expr_assign_t;

typedef struct expr_list expr_list_t;
struct expr_list {
	expr_common_t   h;
	expr_list_t     *  next;
	expr_v4_t       *  source;
} ;

typedef enum {
	EXPR_WRD,
	EXPR_PARS_VAR,
	EXPR_LIST,
	EXPR_ASSIGN,
	EXPR_LNODE,
	EXPR_FLOW,
	EXPR_OP3,
	EXPR_OP2,
	EXPR_OP
} expr_v4_type;

union expr_v4 {
	expr_common_t   h;
	expr_wrd_t      wd;
	expr_pars_var_t pars_var;
	expr_list_t     list;
        expr_assign_t   assgn;
	expr_lnode_t    lnode;
	expr_flow_t     flow;
//	expr_op3_t      op3;
	expr_op2_t      op2;
	expr_op_t       op;
};

/* ok this is space for names, constants and tmp*/
typedef struct free_space free_space_t;

struct free_space {
	free_space_t * free_space_next;                /* next buffer   */
	char         * freeSpace;                      /* pointer to free space */
	char         * freeSpaceMax;                   /* for overflow control */
	char           freeSpaceBase[FREESPACESIZE];   /* current buffer */
};

struct sourece_stack {
	sourece_stack_t * prev;
	long type;                     /* type of current stack head */
	union {
		struct file   * file;
		expr_v4_t     * expr;
//		struct dentry * de;    /*  ??????? what for  */
		wrd_t         * wd;
		long          * pointer;
	} u;
};

typedef struct streg  streg_t;

struct streg {
        streg_t * next;
        streg_t * prev;
	expr_v4_t * cur_exp;          /* current (pwd)  expression for this level */
	expr_v4_t * wrk_exp;          /* current (work) expression for this level */
	pars_var_value_t * val_level;
	int stype;                  /* cur type of level        */
	int level;                  /* cur level                */
};


static struct {
	unsigned char numOfParam;
	unsigned char typesOfParam[4]       ;
} typesOfCommand[] = {
	{0,{0,0,0,0}}
};

static struct {
	void (*	call_function)(void) ;
	unsigned char type;            /* describe parameters, and its types */
} 	Code[] = {
};


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
