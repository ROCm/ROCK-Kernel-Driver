/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */


#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <asm-generic/errno.h>

#if defined(CONFIG_REISER4_FS_SYSCALL)

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "kassign.h"
#include "coord.h"
#include "seal.h"
#include "plugin/item/item.h"
#include "plugin/security/perm.h"
#include "plugin/plugin.h"
#include "plugin/object.h"
#include "znode.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"
#include "reiser4.h"

#include "lnode.h"

#include "parser/parser.h"

#define YYREISER4_DEF

#include "parser/parser.code.c"


/* @p_string is a command string for parsing
this function allocates work area for yacc,
initializes fields, calls yacc, free space
and call for execute the generated code */

asmlinkage long
sys_reiser4(char *p_string)
{
	long ret;
	int *Gencode;
	char * str;
	struct reiser4_syscall_w_space * work_space ;
	str=getname(p_string);
	if (!IS_ERR(str)) {
		/* allocate work space for parser
		   working variables, attached to this call */
		if ( (work_space = reiser4_pars_init() ) == NULL ) {
			return -ENOMEM;
		}
		/* initialize fields */
		/* this field used for parsing string, one (inline) stay on begin of string*/
		work_space->ws_pline  = str;
		work_space->ws_inline = work_space->ws_pline;
		PTRACE(work_space, "%s", "begin parsing");
		ret = yyparse(work_space);	/* parse command */
		reiser4_pars_free(work_space);
		putname(str);
	}
	else {
		ret = PTR_ERR(str);
	}
	return ret;
}

#else
asmlinkage long
sys_reiser4(void *p_string)
{
	return -ENOSYS;
}
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
