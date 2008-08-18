/*
 *
 * Most of this code is borrowed and adapted from the lkcd command "lcrash"
 * and its supporting libarary.
 *
 * This kdb commands for casting memory structures.
 * It provides
 *  "print" "px", "pd" *
 *
 * Careful of porting the klib KL_XXX functions (they call thru a jump table
 * that we don't use here)
 *
 * The kernel type information is added be insmod'g the kdb debuginfo module
 * It loads symbolic debugging info (provided from lcrash -o),
 * (this information originally comes from the lcrash "kerntypes" file)
 *
 */

#define VMALLOC_START_IA64 0xa000000200000000
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/fs.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/fcntl.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/err.h>
#include "modules/lcrash/klib.h"
#include "modules/lcrash/kl_stringtab.h"
#include "modules/lcrash/kl_btnode.h"
#include "modules/lcrash/lc_eval.h"

#undef next_node /* collision with nodemask.h */
int		have_debug_file = 0;
dbg_sym_t *types_tree_head;
dbg_sym_t *typedefs_tree_head;
kltype_t	*kltype_array;
dbg_sym_t	*dsym_types_array;


EXPORT_SYMBOL(types_tree_head);
EXPORT_SYMBOL(typedefs_tree_head);
EXPORT_SYMBOL(kltype_array);
EXPORT_SYMBOL(dsym_types_array);

#define C_HEX		0x0002
#define C_WHATIS	0x0004
#define C_NOVARS	0x0008
#define C_SIZEOF	0x0010
#define C_SHOWOFFSET	0x0020
#define	C_LISTHEAD      0x0040
#define C_LISTHEAD_N    0x0080 /* walk using list_head.next */
#define C_LISTHEAD_P    0x0100 /* walk using list_head.prev */
#define C_BINARY	0x0200
#define MAX_LONG_LONG	0xffffffffffffffffULL
klib_t   kdb_klib;
klib_t   *KLP = &kdb_klib;
k_error_t klib_error = 0;
dbg_sym_t *type_tree = (dbg_sym_t *)NULL;
dbg_sym_t *typedef_tree = (dbg_sym_t *)NULL;
dbg_sym_t *func_tree = (dbg_sym_t *)NULL;
dbg_sym_t *srcfile_tree = (dbg_sym_t *)NULL;
dbg_sym_t *var_tree = (dbg_sym_t *)NULL;
dbg_sym_t *xtype_tree = (dbg_sym_t *)NULL;
dbg_hashrec_t *dbg_hash[TYPE_NUM_SLOTS];
int all_count, deall_count;
void single_type(char *str);
void sizeof_type(char *str);
typedef struct chunk_s {
	struct chunk_s  *next;	/* Must be first */
	struct chunk_s  *prev;	/* Must be second */
	void		*addr;
	struct bucket_s *bucketp;
	uint32_t	chunksz;  /* size of memory chunk (via malloc()) */
	uint32_t	blksz;	/* Not including header */
	short		blkcount; /* Number of blksz blocks in chunk */
} chunk_t;

typedef struct blkhdr_s {
	struct blkhdr_s *next;
	union {
		struct blkhdr_s *prev;
	chunk_t	*chunkp;
	} b_un;
	int	flg;
	int	size;
} blkhdr_t;

int ptrsz64 = ((int)sizeof(void *) == 8);
alloc_functions_t alloc_functions;

/*
 * return 1 if addr is invalid
 */
static int
invalid_address(kaddr_t addr, int count)
{
	unsigned char c;
	unsigned long lcount;
	/* FIXME: untested? */
	lcount = count;
	/* FIXME: use kdb_verify_area */
	while (count--) {
		if (kdb_getarea(c, addr))
			return 1;
	}
	return 0;
}

/*
 * wrappers for calls to kernel-style allocation/deallocation
 */
static void *
kl_alloc_block(int size)
{
	void	*vp;

	vp = kmalloc(size, GFP_KERNEL);
	if (!vp) {
		kdb_printf ("kmalloc of %d bytes failed\n", size);
	}
	/* important: the lcrash code sometimes assumes that the
	 *            allocation is zeroed out
	 */
	memset(vp, 0, size);
	all_count++;
	return vp;
}
static void
kl_free_block(void *vp)
{
	kfree(vp);
	deall_count++;
	return;
}

int
get_value(char *s, uint64_t *value)
{
	return kl_get_value(s, NULL, 0, value);
}

/*
 * kl_get_block()
 *
 *   Read a size block from virtual address addr in the system memory image.
 */
k_error_t
kl_get_block(kaddr_t addr, unsigned size, void *bp, void *mmap)
{
	if (!bp) {
		return(KLE_NULL_BUFF);
	} else if (!size) {
		return(KLE_ZERO_SIZE);
	}

	memcpy(bp, (void *)addr, size);

	return(0);
}

/*
 * print_value()
 */
void
print_value(char *ldstr, uint64_t value, int width)
{
	int w = 0;
	char fmtstr[12], f, s[2]="\000\000";

	if (ldstr) {
		kdb_printf("%s", ldstr);
	}
        s[0] = '#';
	f = 'x';
	if (width) {
		if (ptrsz64) {
			w = 18; /* due to leading "0x" */
		} else {
		        w = 10; /* due to leading "0x" */
		}
	}
	if (w) {
		sprintf(fmtstr, "%%%s%d"FMT64"%c", s, w, f);
	} else {
		sprintf(fmtstr, "%%%s"FMT64"%c", s, f);
	}
	kdb_printf(fmtstr, value);
}

/*
 * print_list_head()
 */
void
print_list_head(kaddr_t saddr)
{
	print_value("STRUCT ADDR: ", (uint64_t)saddr, 8);
	kdb_printf("\n");
}

/*
 * check_prev_ptr()
 */
void
check_prev_ptr(kaddr_t ptr, kaddr_t prev)
{
	if(ptr != prev) {
		kdb_printf("\nWARNING: Pointer broken. %#"FMTPTR"x,"
			" SHOULD BE: %#"FMTPTR"x\n", prev, ptr);
	}
}

/*
 * kl_kaddr() -- Return a kernel virtual address stored in a structure
 *
 *   Pointer 'p' points to a kernel structure
 *   of type 's.' Get the kernel address located in member 'm.'
 */
kaddr_t
kl_kaddr(void *p, char *s, char *m)
{
	uint64_t *u64p;
	int	offset;

	offset = kl_member_offset(s, m);
	u64p = (uint64_t *)(p + offset);
	return((kaddr_t)*u64p);
}

/*
 * walk_structs() -- walk linked lists of kernel data structures
 */
int
walk_structs(char *s, char *f, char *member, kaddr_t addr, int flags)
{
	int size, offset, mem_offset=0;
	kaddr_t last = 0, next;
	kltype_t *klt = (kltype_t *)NULL, *memklt=(kltype_t *)NULL;
	unsigned long long iter_threshold = 10000;

	int counter = 0;
	kaddr_t head=0, head_next=0, head_prev=0, entry=0;
	kaddr_t entry_next=0, entry_prev;

	/* field name of link pointer, determine its offset in the struct.  */
	if ((offset = kl_member_offset(s, f)) == -1) {
		kdb_printf("Could not determine offset for member %s of %s.\n",
			f, s);
		return 0;
	}

	/* Get the type of the enclosing structure */
	if (!(klt = kl_find_type(s, (KLT_STRUCT|KLT_UNION)))) {
		kdb_printf("Could not find the type of %s\n", s);
		return(1);
	}

	/* Get the struct size */
	if ((size = kl_struct_len(s)) == 0) {
		kdb_printf ("could not get the length of %s\n", s);
		return(1);
	}

	/* test for a named member of the structure that should be displayed */
	if (member) {
		memklt = kl_get_member(klt, member);
		if (!memklt) {
			kdb_printf ("%s has no member %s\n", s, member);
			return 1;
		}
		mem_offset = kl_get_member_offset(klt, member);
	}

	if ((next = addr)) {
		/* get head of list (anchor) when struct list_head is used */
		if (flags & C_LISTHEAD) {
			head = next;
			if (invalid_address(head, sizeof(head))) {
				kdb_printf ("invalid address %#lx\n",
					head);
				return 1;
			}
			/* get contents of addr  struct member */
			head_next = kl_kaddr((void *)head, "list_head", "next");
			if (invalid_address(head, sizeof(head_next))) {
				kdb_printf ("invalid address %#lx\n",
					head_next);
				return 1;
			}
			/* get prev field of anchor */
			head_prev = kl_kaddr((void *)head, "list_head", "prev");
			if (invalid_address(head, sizeof(head_prev))) {
				kdb_printf ("invalid address %#lx\n",
					head_prev);
				return 1;
			}
			entry = 0;
		}
	}

	while(next && counter < iter_threshold) {
		counter++;
		if (counter > iter_threshold) {
                	kdb_printf("\nWARNING: Iteration threshold reached.\n");
                        kdb_printf("Current threshold: %lld\n", iter_threshold);
			break;
		}
		if(flags & C_LISTHEAD) {
			if(!(entry)){
				if(flags & C_LISTHEAD_N){
					entry = head_next;
				} else {
					entry = head_prev;
				}
				last = head;
			}

			if(head == entry) {
				if(flags & C_LISTHEAD_N){
					check_prev_ptr(last, head_prev);
				} else {
					check_prev_ptr(last, head_next);
				}
				break;
			}

			next = entry - offset; /* next structure */
			/* check that the whole structure can be addressed */
			if (invalid_address(next, size)) {
				kdb_printf(
				"invalid struct address %#lx\n", next);
				return 1;
			}
			/* and validate that it points to valid addresses */
			entry_next = kl_kaddr((void *)entry,"list_head","next");
			if (invalid_address(entry_next, sizeof(entry_next))) {
				kdb_printf("invalid address %#lx\n",
					entry_next);
				return 1;
			}
			entry_prev = kl_kaddr((void *)entry,"list_head","prev");
			if (invalid_address(entry_prev, sizeof(entry_prev))) {
				kdb_printf("invalid address %#lx\n",
					entry_prev);
				return 1;
			}
			if(flags & C_LISTHEAD_N){
				check_prev_ptr(last, entry_prev);
			} else {
				check_prev_ptr(last, entry_next);
			}
			print_list_head(next);
			last = entry;
			if(flags & C_LISTHEAD_N){
				entry = entry_next; /* next list_head */
			} else {
				entry = entry_prev; /* next list_head */
			}
		}

		if (memklt) {
			/* print named sub-structure in C-like struct format. */
			kl_print_member(
				(void *)((unsigned long)next+mem_offset),
				memklt, 0, C_HEX);
		} else {
			/* print entire structure in C-like struct format. */
			kl_print_type((void *)next, klt, 0, C_HEX);
		}

		if(!(flags & C_LISTHEAD)) {
			last = next;
			next = (kaddr_t) (*(uint64_t*)(next + offset));
		}
	}

	return(0);
}

/*
 * Implement the lcrash walk -s command
 *  see lcrash cmd_walk.c
 */
int
kdb_walk(int argc, const char **argv)
{
	int	i, nonoptc=0, optc=0, flags=0, init_len=0;
	char	*cmd, *arg, *structp=NULL, *forwp=NULL, *memberp=NULL;
	char	*addrp=NULL;
	uint64_t value;
	kaddr_t start_addr;

	all_count=0;
	deall_count=0;
	if (!have_debug_file) {
		kdb_printf("no debuginfo file\n");
		return 0;
	}
	/* If there is nothing to evaluate, just return */
	if (argc == 0) {
		return 0;
	}
	cmd = (char *)*argv; /* s/b "walk" */
	if (strcmp(cmd,"walk")) {
		kdb_printf("got %s, not \"walk\"\n", cmd);
		return 0;
	}

	for (i=1; i<=argc; i++) {
		arg = (char *)*(argv+i);
		if (*arg == '-') {
			optc++;
			if (optc > 2) {
				kdb_printf("too many options\n");
				kdb_printf("see 'walkhelp'\n");
				return 0;
			}
			if (*(arg+1) == 's') {
				continue; /* ignore -s */
			} else if (*(arg+1) == 'h') {
				if ((init_len=kl_struct_len("list_head"))
								== 0) {
					kdb_printf(
						"could not find list_head\n");
					return 0;
				}
				if (*(arg+2) == 'p') {
					flags = C_LISTHEAD;
					flags |= C_LISTHEAD_P;
				} else if (*(arg+2) == 'n') {
					flags = C_LISTHEAD;
					flags |= C_LISTHEAD_N;
				} else {
					kdb_printf("invalid -h option <%s>\n",
						arg);
					kdb_printf("see 'walkhelp'\n");
					return 0;
				}
			} else {
				kdb_printf("invalid option <%s>\n", arg);
				kdb_printf("see 'walkhelp'\n");
				return 0;
			}
		}  else {
			nonoptc++;
			if (nonoptc > 4) {
				kdb_printf("too many arguments\n");
				kdb_printf("see 'walkhelp'\n");
				return 0;
			}
			if (nonoptc == 1) {
				structp = arg;
			} else if (nonoptc == 2) {
				forwp = arg;
			} else if (nonoptc == 3) {
				addrp = arg;
			} else if (nonoptc == 4) {
				/* the member is optional; if we get
				   a fourth, the previous was the member */
				memberp = addrp;
				addrp = arg;
			} else {
				kdb_printf("invalid argument <%s>\n", arg);
				kdb_printf("see 'walkhelp'\n");
				return 0;
			}
		}
	}
	if (nonoptc < 3) {
		kdb_printf("too few arguments\n");
		kdb_printf("see 'walkhelp'\n");
		return 0;
	}
	if (!(flags & C_LISTHEAD)) {
		if ((init_len=kl_struct_len(structp)) == 0) {
			kdb_printf("could not find %s\n", structp);
			return 0;
		}
	}

	/* Get the start address of the structure */
	if (get_value(addrp, &value)) {
		kdb_printf ("address %s invalid\n", addrp);
		return 0;
	}
	start_addr = (kaddr_t)value;
	if (invalid_address(start_addr, init_len)) {
		kdb_printf ("address %#lx invalid\n", start_addr);
		return 0;
	}

	if (memberp) {
	}

	if (walk_structs(structp, forwp, memberp, start_addr, flags)) {
		kdb_printf ("walk_structs failed\n");
		return 0;
	}
	/* kdb_printf("ptc allocated:%d deallocated:%d\n",
		 all_count, deall_count); */
	return 0;
}

/*
 * Implement the lcrash px (print, pd) command
 *  see lcrash cmd_print.c
 *
 *     px <expression>
 *       e.g. px *(task_struct *) <address>
 */
int
kdb_debuginfo_print(int argc, const char **argv)
{
	/* argc does not count the command itself, which is argv[0] */
	char		*cmd, *next, *end, *exp, *cp;
	unsigned char	*buf;
	int		i, j, iflags;
	node_t		*np;
	uint64_t	flags = 0;

	/* If there is nothing to evaluate, just return */
	if (argc == 0) {
		return 0;
	}
	all_count=0;
	deall_count=0;

	cmd = (char *)*argv;

	/* Set up the flags value. If this command was invoked via
	 * "pd" or "px", then make sure the appropriate flag is set.
	 */
	flags = 0;
	if (!strcmp(cmd, "pd") || !strcmp(cmd, "print")) {
		flags = 0;
	} else if (!strcmp(cmd, "px")) {
		flags |= C_HEX;
	} else if (!strcmp(cmd, "whatis")) {
		if (argc != 1) {
			kdb_printf("usage: whatis <symbol | type>\n");
			return 0;
		}
		cp = (char *)*(argv+1);
		single_type(cp);
		/* kdb_printf("allocated:%d deallocated:%d\n",
			 all_count, deall_count); */
		return 0;
	} else if (!strcmp(cmd, "sizeof")) {
		if (!have_debug_file) {
			kdb_printf("no debuginfo file\n");
			return 0;
		}
		if (argc != 1) {
			kdb_printf("usage: sizeof type\n");
			return 0;
		}
		cp = (char *)*(argv+1);
		sizeof_type(cp);
		return 0;
	} else {
		kdb_printf("command error: %s\n", cmd);
		return 0;
	}

	/*
	 * Count the number of bytes necessary to hold the entire expression
	 * string.
	 */
	for (i=1, j=0; i <= argc; i++) {
		j += (strlen(*(argv+i)) + 1);
	}

	/*
	 * Allocate space for the expression string and copy the individual
	 * arguments into it.
	 */
	buf = kl_alloc_block(j);
	if (!buf) {
		return 0;
	}

	for (i=1; i <= argc; i++) {
		strcat(buf, *(argv+i));
		/* put spaces between arguments */
		if (i < argc) {
			strcat(buf, " ");
		}
	}

	/* Walk through the expression string, expression by expression.
	 * Note that a comma (',') is the delimiting character between
	 * expressions.
	 */
	next = buf;
	while (next) {
		if ((end = strchr(next, ','))) {
			*end = (char)0;
		}

		/* Copy the next expression to a separate expression string.
		 * A separate expresison string is necessary because it is
		 * likely to get freed up in eval() when variables get expanded.
		 */
		i = strlen(next)+1;
		exp = (char *)kl_alloc_block(i);
		if (!exp) {
			return 0;
		}
		strcpy(exp, next);

		/* Evaluate the expression */
		np = eval(&exp, 0);
		if (!np || eval_error) {
			print_eval_error(cmd, exp,
				(error_token ? error_token : (char*)NULL),
				eval_error, CMD_NAME_FLG);
			if (np) {
				free_nodes(np);
			}
			kl_free_block(buf);
			kl_free_block(exp);
			free_eval_memory();
			return 0;
		}
		iflags = flags;
		if (print_eval_results(np, iflags)) {
			free_nodes(np);
			kl_free_block(buf);
			free_eval_memory();
			return 0;
		}
		kl_free_block(exp);

		if (end) {
			next = end + 1;
			kdb_printf(" ");
		} else {
			next = (char*)NULL;
			kdb_printf("\n");
		}
		free_nodes(np);
	}
	free_eval_memory();
	kl_free_block(buf);
	/* kdb_printf("allocated:%d deallocated:%d\n",
			 all_count, deall_count); */
	return 0;
}

/*
 * Display help for the px command
 */
int
kdb_pxhelp(int argc, const char **argv)
{
	if (have_debug_file) {
 kdb_printf ("Some examples of using the px command:\n");
 kdb_printf (" the whole structure:\n");
 kdb_printf ("  px *(task_struct *)0xe0000...\n");
 kdb_printf (" one member:\n");
 kdb_printf ("  px (*(task_struct *)0xe0000...)->comm\n");
 kdb_printf (" the address of a member\n");
 kdb_printf ("  px &((task_struct *)0xe0000...)->children\n");
 kdb_printf (" a structure pointed to by a member:\n");
 kdb_printf ("  px ((*(class_device *)0xe0000...)->class)->name\n");
 kdb_printf (" array element:\n");
 kdb_printf ("  px (cache_sizes *)0xa0000...[0]\n");
 kdb_printf ("  px (task_struct *)(0xe0000...)->cpus_allowed.bits[0]\n");
	} else {
 		kdb_printf ("There is no debug info file.\n");
 		kdb_printf ("The px/pd/print commands can only evaluate ");
 		kdb_printf ("arithmetic expressions.\n");
	}
 return 0;
}

/*
 * Display help for the walk command
 */
int
kdb_walkhelp(int argc, const char **argv)
{
	if (!have_debug_file) {
		kdb_printf("no debuginfo file\n");
		return 0;
	}
 kdb_printf ("Using the walk command:\n");
 kdb_printf (" (only the -s (symbolic) form is supported, so -s is ignored)\n");
 kdb_printf ("\n");
 kdb_printf (" If the list is not linked with list_head structures:\n");
 kdb_printf ("  walk [-s] struct name-of-forward-pointer address\n");
 kdb_printf ("  example: walk xyz_struct next 0xe00....\n");
 kdb_printf ("\n");
 kdb_printf (" If the list is linked with list_head structures, use -hn\n");
 kdb_printf (" to walk the 'next' list, -hp for the 'prev' list\n");
 kdb_printf ("  walk -h[n|p] struct name-of-forward-pointer [member-to-show] address-of-list-head\n");
 kdb_printf ("  example, to show the entire task_struct:\n");
 kdb_printf ("   walk -hn task_struct tasks 0xe000....\n");
 kdb_printf ("  example, to show the task_struct member comm:\n");
 kdb_printf ("   walk -hn task_struct tasks comm 0xe000....\n");
 kdb_printf ("  (address is not the address of first member's list_head, ");
 kdb_printf     ("but of the anchoring list_head\n");
 return 0;
}

/*
 * dup_block()
 */
void *
dup_block(void *b, int len)
{
	void *b2;

	if ((b2 = kl_alloc_block(len))) {
		memcpy(b2, b, len); /* dst, src, sz */
	}
	return(b2);
}

/*
 * kl_reset_error()
 */
void
kl_reset_error(void)
{
        klib_error = 0;
}

/*
 * given a symbol name, look up its address
 *
 * in lcrash, this would return a pointer to the syment_t in
 * a binary tree of them
 *
 * In this one, look up the symbol in the standard kdb way,
 * which fills in the kdb_symtab_t.
 * Then fill in the  global syment_t "lkup_syment" -- assuming
 * we'll only need one at a time!
 *
 * kl_lkup_symname returns the address of syment_t if the symbol is
 * found, else null.
 *
 * Note: we allocate a syment_t   the caller should kfree it
 */
syment_t *
kl_lkup_symname (char *cp)
{
	syment_t  *sp;
	kdb_symtab_t kdb_symtab;

	if (kdbgetsymval(cp, &kdb_symtab)) {
		sp = (syment_t *)kl_alloc_block(sizeof(syment_t));
		sp->s_addr = (kaddr_t)kdb_symtab.sym_start;
		KL_ERROR = 0;
		return (sp);
	} else {
		/* returns 0 if the symbol is not found */
		KL_ERROR = KLE_INVALID_VALUE;
		return ((syment_t *)0);
	}
}

/*
 * kl_get_ra()
 *
 * This function returns its own return address.
 * Usefule when trying to capture where we came from.
 */
void*
kl_get_ra(void)
{
	return (__builtin_return_address(0));
}

/* start kl_util.c */
/*
 * Definitions for the do_math() routine.
 */
#define M_ADD      '+'
#define M_SUBTRACT '-'
#define M_MULTIPLY '*'
#define M_DIVIDE   '/'

/*
 * do_math() -- Calculate some math values based on a string argument
 *              passed into the function.  For example, if you use:
 *
 *              0xffffc000*2+6/5-3*19-8
 *
 *              And you will get the value 0xffff7fc0 back.  I could
 *              probably optimize this a bit more, but right now, it
 *              works, which is good enough for me.
 */
static uint64_t
do_math(char *str)
{
	int i = 0;
	char *buf, *loc;
	uint64_t value1, value2;
	syment_t *sp;

	buf = (char *)kl_alloc_block((strlen(str) + 1));
	sprintf(buf, "%s", str);
	for (i = strlen(str); i >= 0; i--) {
		if ((str[i] == M_ADD) || (str[i] == M_SUBTRACT)) {
			buf[i] = '\0';
			value1 = do_math(buf);
			value2 = do_math(&str[i+1]);
			kl_free_block((void *)buf);
			if (str[i] == M_SUBTRACT) {
				return value1 - value2;
			} else {
				return value1 + value2;
			}
		}
	}

	for (i = strlen(str); i >= 0; i--) {
		if ((str[i] == M_MULTIPLY) || (str[i] == M_DIVIDE)) {
			buf[i] = '\0';
			value1 = do_math(buf);
			value2 = do_math(&str[i+1]);
			kl_free_block((void *)buf);
			if (str[i] == M_MULTIPLY) {
				return (value1 * value2);
			} else {
				if (value2 == 0) {
					/* handle divide by zero */
					/* XXX -- set proper error code */
					klib_error = 1;
					return (0);
				} else {
					return (value1 / value2);
				}
			}
		}
	}

	/*
	 * Otherwise, just process the value, and return it.
	 */
	sp = kl_lkup_symname(buf);
	if (KL_ERROR) {
		KL_ERROR = 0;
		value2 = kl_strtoull(buf, &loc, 10);
		if (((!value2) && (buf[0] != '0')) || (*loc) ||
			(!strncmp(buf, "0x", 2)) || (!strncmp(buf, "0X", 2))) {
			value1 = (kaddr_t)kl_strtoull(buf, (char**)NULL, 16);
		} else {
			value1 = (unsigned)kl_strtoull(buf, (char**)NULL, 10);
		}
	} else {
		value1 = (kaddr_t)sp->s_addr;
		kl_free_block((void *)sp);
	}
	kl_free_block((void *)buf);
	return (value1);
}
/*
 * kl_get_value() -- Translate numeric input strings
 *
 *   A generic routine for translating an input string (param) in a
 *   number of dfferent ways. If the input string is an equation
 *   (contains the characters '+', '-', '/', and '*'), then perform
 *   the math evaluation and return one of the following modes (if
 *   mode is passed):
 *
 *   0 -- if the resulting value is <= elements, if elements (number
 *        of elements in a table) is passed.
 *
 *   1 -- if the first character in param is a pound sign ('#').
 *
 *   3 -- the numeric result of an equation.
 *
 *   If the input string is NOT an equation, mode (if passed) will be
 *   set in one of the following ways (depending on the contents of
 *   param and elements).
 *
 *   o When the first character of param is a pound sign ('#'), mode
 *     is set equal to one and the trailing numeric value (assumed to
 *     be decimal) is returned.
 *
 *   o When the first two characters in param are "0x" or "0X," or
 *     when when param contains one of the characers "abcdef," or when
 *     the length of the input value is eight characters. mode is set
 *     equal to two and the numeric value contained in param is
 *     translated as hexadecimal and returned.
 *
 *   o The value contained in param is translated as decimal and mode
 *     is set equal to zero. The resulting value is then tested to see
 *     if it exceeds elements (if passed). If it does, then value is
 *     translated as hexadecimal and mode is set equal to two.
 *
 *   Note that mode is only set when a pointer is passed in the mode
 *   paramater. Also note that when elements is set equal to zero, any
 *   non-hex (as determined above) value not starting with a pound sign
 *   will be translated as hexadecimal (mode will be set equal to two) --
 *   IF the length of the string of characters is less than 16 (kaddr_t).
 *
 */
int
kl_get_value(char *param, int *mode, int elements, uint64_t *value)
{
	char *loc;
	uint64_t v;

	kl_reset_error();

	/* Check to see if we are going to need to do any math
	 */
	if (strpbrk(param, "+-/*")) {
		if (!strncmp(param, "#", 1)) {
			v = do_math(&param[1]);
			if (mode) {
				*mode = 1;
			}
		} else {
			v = do_math(param);
			if (mode) {
				if (elements && (*value <= elements)) {
					*mode = 0;
				} else {
					*mode = 3;
				}
			}
		}
	} else {
		if (!strncmp(param, "#", 1)) {
			if (!strncmp(param, "0x", 2)
					|| !strncmp(param, "0X", 2)
					|| strpbrk(param, "abcdef")) {
				v = kl_strtoull(&param[1], &loc, 16);
			} else {
				v = kl_strtoull(&param[1], &loc, 10);
			}
			if (loc) {
				KL_ERROR = KLE_INVALID_VALUE;
				return (1);
			}
			if (mode) {
				*mode = 1;
			}
		} else if (!strncmp(param, "0x", 2) || !strncmp(param, "0X", 2)
					|| strpbrk(param, "abcdef")) {
			v = kl_strtoull(param, &loc, 16);
			if (loc) {
				KL_ERROR = KLE_INVALID_VALUE;
				return (1);
			}
			if (mode) {
				*mode = 2; /* HEX VALUE */
			}
		} else if (elements || (strlen(param) < 16) ||
				(strlen(param) > 16)) {
			v = kl_strtoull(param, &loc, 10);
			if (loc) {
				KL_ERROR = KLE_INVALID_VALUE;
				return (1);
			}
			if (elements && (v >= elements)) {
				v = (kaddr_t)kl_strtoull(param,
						(char**)NULL, 16);
				if (mode) {
					*mode = 2; /* HEX VALUE */
				}
			} else if (mode) {
				*mode = 0;
			}
		} else {
			v = kl_strtoull(param, &loc, 16);
			if (loc) {
				KL_ERROR = KLE_INVALID_VALUE;
				return (1);
			}
			if (mode) {
				*mode = 2; /* ASSUME HEX VALUE */
			}
		}
	}
	*value = v;
	return (0);
}
/* end kl_util.c */

/* start kl_libutil.c */
static int
valid_digit(char c, int base)
{
	switch(base) {
		case 2:
			if ((c >= '0') && (c <= '1')) {
				return(1);
			} else {
				return(0);
			}
		case 8:
			if ((c >= '0') && (c <= '7')) {
				return(1);
			} else {
				return(0);
			}
		case 10:
			if ((c >= '0') && (c <= '9')) {
				return(1);
			} else {
				return(0);
			}
		case 16:
			if (((c >= '0') && (c <= '9'))
					|| ((c >= 'a') && (c <= 'f'))
					|| ((c >= 'A') && (c <= 'F'))) {
				return(1);
			} else {
				return(0);
			}
	}
	return(0);
}

static int
digit_value(char c, int base, int *val)
{
	if (!valid_digit(c, base)) {
		return(1);
	}
	switch (base) {
		case 2:
		case 8:
		case 10:
			*val = (int)((int)(c - 48));
			break;
		case 16:
			if ((c >= 'a') && (c <= 'f')) {
				*val = ((int)(c - 87));
			} else if ((c >= 'A') && (c <= 'F')) {
				*val = ((int)(c - 55));
			} else {
				*val = ((int)(c - 48));
			}
	}
	return(0);
}

uint64_t
kl_strtoull(char *str, char **loc, int base)
{
	int dval;
	uint64_t i = 1, v, value = 0;
	char *c, *cp = str;

	*loc = (char *)NULL;
	if (base == 0) {
		if (!strncmp(cp, "0x", 2) || !strncmp(cp, "0X", 2)) {
			base = 16;
		} else if (cp[0] == '0') {
			if (cp[1] == 'b') {
				base = 2;
			} else {
				base = 8;
			}
		} else if (strpbrk(cp, "abcdefABCDEF")) {
			base = 16;
		} else {
			base = 10;
		}
	}
	if ((base == 8) && (*cp == '0')) {
		cp += 1;
	} else if ((base == 2) && !strncmp(cp, "0b", 2)) {
		cp += 2;
	} else if ((base == 16) &&
			(!strncmp(cp, "0x", 2) || !strncmp(cp, "0X", 2))) {
		cp += 2;
	}
	c = &cp[strlen(cp) - 1];
	while (c >= cp) {

		if (digit_value(*c, base, &dval)) {
			if (loc) {
				*loc = c;
			}
			return(value);
		}
		v = dval * i;
		if ((MAX_LONG_LONG - value) < v) {
			return(MAX_LONG_LONG);
		}
		value += v;
		i *= (uint64_t)base;
		c--;
	}
	return(value);
}
/* end kl_libutil.c */

/*
 * dbg_hash_sym()
 */
void
dbg_hash_sym(uint64_t typenum, dbg_sym_t *stp)
{
	dbg_hashrec_t *shp, *hshp;

	if ((typenum == 0) || (!stp)) {
		return;
	}
	shp = (dbg_hashrec_t *)kl_alloc_block(sizeof(dbg_hashrec_t));
	shp->h_typenum = typenum;
	shp->h_ptr = stp;
	shp->h_next = (dbg_hashrec_t *)NULL;
	if ((hshp = dbg_hash[TYPE_NUM_HASH(typenum)])) {
		while (hshp->h_next) {
			hshp = hshp->h_next;
		}
		hshp->h_next = shp;
	} else {
		dbg_hash[TYPE_NUM_HASH(typenum)] = shp;
	}
}

/*
 * dbg_find_sym()
 */
dbg_sym_t *
dbg_find_sym(char *name, int type, uint64_t typenum)
{
	dbg_sym_t *stp = (dbg_sym_t *)NULL;

	if (name && strlen(name)) {
		/* Cycle through the type flags and see if any records are
		 * present. Note that if multiple type flags or DBG_ALL is
		 * passed in, only the first occurance of 'name' will be
		 * found and returned. If name exists in multiple trees,
		 * then multiple searches are necessary to find them.
		 */
		if (type & DBG_TYPE) {
			if ((stp = (dbg_sym_t *)kl_find_btnode((btnode_t *)
					type_tree, name, (int *)NULL))) {
				goto found_sym;
			}
		}
		if (type & DBG_TYPEDEF) {
			if ((stp = (dbg_sym_t *)kl_find_btnode((btnode_t *)
					typedef_tree, name, (int *)NULL))) {
				goto found_sym;
			}
		}
		if (!stp) {
			return((dbg_sym_t*)NULL);
		}
	}
found_sym:
	if (typenum) {
		dbg_hashrec_t *hshp;

		if (stp) {
			if (stp->sym_typenum == typenum) {
				return(stp);
			}
		} else if ((hshp = dbg_hash[TYPE_NUM_HASH(typenum)])) {
			while (hshp) {
				if (hshp->h_typenum == typenum) {
					return(hshp->h_ptr);
				}
				hshp = hshp->h_next;
			}
		}
	}
	return(stp);
}

/*
 * kl_find_type() -- find a KLT type by name.
 */
kltype_t *
kl_find_type(char *name, int tnum)
{
	dbg_sym_t *stp;
	kltype_t *kltp = (kltype_t *)NULL;

	if (!have_debug_file) {
		kdb_printf("no debuginfo file\n");
		return kltp;
	}

	if (!tnum || IS_TYPE(tnum)) {
		if ((stp = dbg_find_sym(name, DBG_TYPE, 0))) {
			kltp = (kltype_t *)stp->sym_kltype;
			if (tnum && !(kltp->kl_type & tnum)) {
				/* We have found a type by this name
				 * but it does not have the right
				 * type number (e.g., we're looking
				 * for a struct and we don't find
				 * a KLT_STRUCT type by this name).
				 */
				return((kltype_t *)NULL);
			}
		}
	}
	if (!tnum || IS_TYPEDEF(tnum)) {
		if ((stp = dbg_find_sym(name, DBG_TYPEDEF, 0))) {
			kltp = (kltype_t *)stp->sym_kltype;
		}
	}
	return(kltp);
}

/*
 * kl_first_btnode() -- non-recursive implementation.
 */
btnode_t *
kl_first_btnode(btnode_t *np)
{
	if (!np) {
		return((btnode_t *)NULL);
	}

	/* Walk down the left side 'til the end...
	 */
	while (np->bt_left) {
		np = np->bt_left;
	}
	return(np);
}

/*
 * kl_next_btnode() -- non-recursive implementation.
 */
btnode_t *
kl_next_btnode(btnode_t *node)
{
	btnode_t *np = node, *parent;

	if (np) {
		if (np->bt_right) {
			return(kl_first_btnode(np->bt_right));
		} else {
			parent = np->bt_parent;
next:
			if (parent) {
				if (parent->bt_left == np) {
					return(parent);
				}
				np = parent;
				parent = parent->bt_parent;
				goto next;
			}
		}
	}
	return((btnode_t *)NULL);
}

/*
 * dbg_next_sym()
 */
dbg_sym_t *
dbg_next_sym(dbg_sym_t *stp)
{
	dbg_sym_t *next_stp;

	next_stp = (dbg_sym_t *)kl_next_btnode((btnode_t *)stp);
	return(next_stp);
}

/*
 * kl_prev_btnode() -- non-recursive implementation.
 */
btnode_t *
kl_prev_btnode(btnode_t *node)
{
	btnode_t *np = node, *parent;

	if (np) {
		if (np->bt_left) {
			np = np->bt_left;
			while (np->bt_right) {
				np = np->bt_right;
			}
			return(np);
		}
		parent = np->bt_parent;
next:
		if (parent) {
			if (parent->bt_right == np) {
				return(parent);
			}
			np = parent;
			parent = parent->bt_parent;
			goto next;
		}
	}
	return((btnode_t *)NULL);
}

/*
 * dbg_prev_sym()
 */
dbg_sym_t *
dbg_prev_sym(dbg_sym_t *stp)
{
	dbg_sym_t *prev_stp;

	prev_stp = (dbg_sym_t *)kl_prev_btnode((btnode_t *)stp);
	return(prev_stp);
}

/*
 * kl_find_next_type() -- find next KLT type
 */
kltype_t *
kl_find_next_type(kltype_t *kltp, int type)
{
	kltype_t *nkltp = NULL;
	dbg_sym_t *nstp;

	if (kltp && kltp->kl_ptr) {
		nstp = (dbg_sym_t *)kltp->kl_ptr;
		nkltp = (kltype_t *)nstp->sym_kltype;
		if (type) {
			while(nkltp && !(nkltp->kl_type & type)) {
				if ((nstp = dbg_next_sym(nstp))) {
					nkltp = (kltype_t *)nstp->sym_kltype;
				} else {
					nkltp = (kltype_t *)NULL;
				}
			}
		}
	}
	return(nkltp);
}

/*
 * dbg_first_sym()
 */
dbg_sym_t *
dbg_first_sym(int type)
{
	dbg_sym_t *stp = (dbg_sym_t *)NULL;

	switch(type) {
		case DBG_TYPE:
			stp = (dbg_sym_t *)
				kl_first_btnode((btnode_t *)type_tree);
			break;
		case DBG_TYPEDEF:
			stp = (dbg_sym_t *)
				kl_first_btnode((btnode_t *)typedef_tree);
			break;
	}
	return(stp);
}

/*
 * kl_first_type()
 */
kltype_t *
kl_first_type(int tnum)
{
	kltype_t *kltp = NULL;
	dbg_sym_t *stp;

	if (IS_TYPE(tnum)) {
		/* If (tnum == KLT_TYPE), then return the first type
		 * record, regardless of the type. Otherwise, search
		 * for the frst type that mapps into tnum.
		 */
		if ((stp = dbg_first_sym(DBG_TYPE))) {
			kltp = (kltype_t *)stp->sym_kltype;
			if (tnum != KLT_TYPE) {
				while (kltp && !(kltp->kl_type & tnum)) {
					if ((stp = dbg_next_sym(stp))) {
						kltp = (kltype_t *)stp->sym_kltype;
					} else {
						kltp = (kltype_t *)NULL;
					}
				}
			}
		}
	} else if (IS_TYPEDEF(tnum)) {
		if ((stp = dbg_first_sym(DBG_TYPEDEF))) {
			kltp = (kltype_t *)stp->sym_kltype;
		}
	}
	return(kltp);
}

/*
 * kl_next_type()
 */
kltype_t *
kl_next_type(kltype_t *kltp)
{
	dbg_sym_t *stp, *nstp;
	kltype_t *nkltp = (kltype_t *)NULL;

	if (!kltp) {
		return((kltype_t *)NULL);
	}
	stp = (dbg_sym_t *)kltp->kl_ptr;
	if ((nstp = dbg_next_sym(stp))) {
		nkltp = (kltype_t *)nstp->sym_kltype;
	}
	return(nkltp);
}

/*
 * kl_prev_type()
 */
kltype_t *
kl_prev_type(kltype_t *kltp)
{
	dbg_sym_t *stp, *pstp;
	kltype_t *pkltp = (kltype_t *)NULL;

	if (!kltp) {
		return((kltype_t *)NULL);
	}
	stp = (dbg_sym_t *)kltp->kl_ptr;
	if ((pstp = dbg_prev_sym(stp))) {
		pkltp = (kltype_t *)pstp->sym_kltype;
	}
	return(pkltp);
}

/*
 * kl_realtype()
 */
kltype_t *
kl_realtype(kltype_t *kltp, int tnum)
{
	kltype_t *rkltp = kltp;

	while (rkltp) {
		if (tnum && (rkltp->kl_type == tnum)) {
			break;
		}
		if (!rkltp->kl_realtype) {
			break;
		}
		if (rkltp->kl_realtype == rkltp) {
			break;
		}
		rkltp = rkltp->kl_realtype;
		if (rkltp == kltp) {
			break;
		}
	}
	return(rkltp);
}

/*
 * dbg_find_typenum()
 */
dbg_type_t *
dbg_find_typenum(uint64_t typenum)
{
        dbg_sym_t *stp;
        dbg_type_t *sp = (dbg_type_t *)NULL;

        if ((stp = dbg_find_sym(0, DBG_TYPE, typenum))) {
                sp = (dbg_type_t *)stp->sym_kltype;
        }
        return(sp);
}

/*
 * find type by typenum
 */
kltype_t *
kl_find_typenum(uint64_t typenum)
{
	kltype_t *kltp;

	kltp = (kltype_t *)dbg_find_typenum(typenum);
	return(kltp);
}

/*
 * kl_find_btnode() -- non-recursive implementation.
 */
btnode_t *
_kl_find_btnode(btnode_t *np, char *key, int *max_depth, size_t len)
{
	int ret;
        btnode_t *next, *prev;

	if (np) {
		if (max_depth) {
			(*max_depth)++;
		}
		next = np;
again:
		if (len) {
			ret = strncmp(key, next->bt_key, len);
		} else {
			ret = strcmp(key, next->bt_key);
		}
		if (ret == 0) {
			if ((prev = kl_prev_btnode(next))) {
				if (len) {
					ret = strncmp(key, prev->bt_key, len);
				} else {
					ret = strcmp(key, prev->bt_key);
				}
				if (ret == 0) {
					next = prev;
					goto again;
				}
			}
			return(next);
		} else if (ret < 0) {
			if ((next = next->bt_left)) {
				goto again;
			}
		} else {
			if ((next = next->bt_right)) {
				goto again;
			}
		}
	}
	return((btnode_t *)NULL);
}

/*
 * kl_type_size()
 */
int
kl_type_size(kltype_t *kltp)
{
	kltype_t *rkltp;

	if (!kltp) {
		return(0);
	}
	if (!(rkltp = kl_realtype(kltp, 0))) {
		return(0);
	}
	return(rkltp->kl_size);
}

/*
 * kl_struct_len()
 */
int
kl_struct_len(char *s)
{
	kltype_t *kltp;

	if ((kltp = kl_find_type(s, (KLT_TYPES)))) {
		return kl_type_size(kltp);
	}
	return(0);
}

/*
 * kl_get_member()
 */
kltype_t *
kl_get_member(kltype_t *kltp, char *f)
{
	kltype_t *mp;

	if ((mp = kltp->kl_member)) {
		while (mp) {
			if (mp->kl_flags & TYP_ANONYMOUS_FLG) {
				kltype_t *amp;

				if ((amp = kl_get_member(mp->kl_realtype, f))) {
					return(amp);
				}
			} else if (!strcmp(mp->kl_name, f)) {
				break;
			}
			mp = mp->kl_member;
		}
	}
	return(mp);
}

/*
 * kl_member()
 */
kltype_t *
kl_member(char *s, char *f)
{
	kltype_t *kltp, *mp = NULL;

	if (!(kltp = kl_find_type(s, (KLT_STRUCT|KLT_UNION)))) {
		if ((kltp = kl_find_type(s, KLT_TYPEDEF))) {
			kltp = kl_realtype(kltp, 0);
		}
	}
	if (kltp) {
		mp = kl_get_member(kltp, f);
	}
	return(mp);
}


/*
 * kl_get_member_offset()
 */
int
kl_get_member_offset(kltype_t *kltp, char *f)
{
	kltype_t *mp;

	if ((mp = kltp->kl_member)) {
		while (mp) {
			if (mp->kl_flags & TYP_ANONYMOUS_FLG) {
				int off;

				/* Drill down to see if the member we are looking for is in
				 * an anonymous union or struct. Since this call is recursive,
				 * the drill down may actually be multi-layer.
				 */
				off = kl_get_member_offset(mp->kl_realtype, f);
				if (off >= 0) {
					return(mp->kl_offset + off);
				}
			} else if (!strcmp(mp->kl_name, f)) {
				return(mp->kl_offset);
			}
			mp = mp->kl_member;
		}
	}
	return(-1);
}

/*
 * kl_member_offset()
 */
int
kl_member_offset(char *s, char *f)
{
	int off = -1;
	kltype_t *kltp;

    if (!(kltp = kl_find_type(s, (KLT_STRUCT|KLT_UNION)))) {
        if ((kltp = kl_find_type(s, KLT_TYPEDEF))) {
            kltp = kl_realtype(kltp, 0);
        }
    }
	if (kltp) {
		off = kl_get_member_offset(kltp, f);
	}
	return(off);
}

/*
 * kl_is_member()
 */
int
kl_is_member(char *s, char *f)
{
	kltype_t *mp;

	if ((mp = kl_member(s, f))) {
		return(1);
	}
	return(0);
}

/*
 * kl_member_size()
 */
int
kl_member_size(char *s, char *f)
{
	kltype_t *mp;

	if ((mp = kl_member(s, f))) {
		return(mp->kl_size);
	}
	return(0);
}

#define TAB_SPACES		     8
#define LEVEL_INDENT(level, flags) {\
	int i, j; \
	if (!(flags & NO_INDENT)) { \
		for (i = 0; i < level; i++) { \
			for (j = 0; j < TAB_SPACES; j++) { \
				kdb_printf(" "); \
			} \
		}\
	} \
}
#define PRINT_NL(flags) \
	if (!(flags & SUPPRESS_NL)) { \
		kdb_printf("\n"); \
	}
#define PRINT_SEMI_COLON(level, flags) \
	if (level && (!(flags & SUPPRESS_SEMI_COLON))) { \
		kdb_printf(";"); \
	}

/*
 * print_realtype()
 */
static void
print_realtype(kltype_t *kltp)
{
	kltype_t *rkltp;

	if ((rkltp = kltp->kl_realtype)) {
		while (rkltp && rkltp->kl_realtype) {
			rkltp = rkltp->kl_realtype;
		}
		if (rkltp->kl_type == KLT_BASE) {
			kdb_printf(" (%s)", rkltp->kl_name);
		}
	}
}

int align_chk = 0;
/*
 *  kl_print_uint16()
 *
 */
void
kl_print_uint16(void *ptr, int flags)
{
	unsigned long long a;

	/* Make sure the pointer is properly aligned (or we will
	 *          * dump core)
	 *                   */
	if (align_chk && (uaddr_t)ptr % 16) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(unsigned long long *) ptr;
	if (flags & C_HEX) {
		kdb_printf("%#llx", a);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(a);
	} else {
		kdb_printf("%llu", a);
	}
}

#if 0
/*
 * kl_print_float16()
 *
 */
void
kl_print_float16(void *ptr, int flags)
{
	double a;

	/* Make sure the pointer is properly aligned (or we will
	 *          * dump core)
	 *                   */
	if (align_chk && (uaddr_t)ptr % 16) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(double*) ptr;
	kdb_printf("%f", a);
}
#endif

/*
 * kl_print_int16()
 *
 */
void
kl_print_int16(void *ptr, int flags)
{
	long long a;

	/* Make sure the pointer is properly aligned (or we will
	 *          * dump core)
	 *                   */
	if (align_chk && (uaddr_t)ptr % 16) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(long long *) ptr;
	if (flags & C_HEX) {
		kdb_printf("%#llx", a);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(a);
	} else {
		kdb_printf("%lld", a);
	}
}

/*
 * kl_print_int8()
 */
void
kl_print_int8(void *ptr, int flags)
{
	long long a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core)
	 */
	if (align_chk && (uaddr_t)ptr % 8) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(long long *) ptr;
	if (flags & C_HEX) {
		kdb_printf("%#llx", a);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(a);
	} else {
		kdb_printf("%lld", a);
	}
}

#if 0
/*
 * kl_print_float8()
 */
void
kl_print_float8(void *ptr, int flags)
{
	double a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core)
	 */
	if (align_chk && (uaddr_t)ptr % 8) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(double*) ptr;
	kdb_printf("%f", a);
}
#endif

/*
 * kl_print_uint8()
 */
void
kl_print_uint8(void *ptr, int flags)
{
	unsigned long long a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core)
	 */
	if (align_chk && (uaddr_t)ptr % 8) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(unsigned long long *) ptr;
	if (flags & C_HEX) {
		kdb_printf("%#llx", a);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(a);
	} else {
		kdb_printf("%llu", a);
	}
}

/*
 * kl_print_int4()
 */
void
kl_print_int4(void *ptr, int flags)
{
	int32_t a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core
	 */
	if (align_chk && (uaddr_t)ptr % 4) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(int32_t*) ptr;
	if (flags & C_HEX) {
		kdb_printf("0x%x", a);
	} else if (flags & C_BINARY) {
		uint64_t value = a & 0xffffffff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%d", a);
	}
}

#if 0
/*
 * kl_print_float4()
 */
void
kl_print_float4(void *ptr, int flags)
{
	float a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core)
	 */
	if (align_chk && (uaddr_t)ptr % 4) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(float*) ptr;
	kdb_printf("%f", a);
}
#endif

/*
 * kl_print_uint4()
 */
void
kl_print_uint4(void *ptr, int flags)
{
	uint32_t a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core)
	 */
	if (align_chk && (uaddr_t)ptr % 4) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(uint32_t*) ptr;
	if (flags & C_HEX) {
		kdb_printf("0x%x", a);
	} else if (flags & C_BINARY) {
		uint64_t value = a & 0xffffffff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%u", a);
	}
}

/*
 * kl_print_int2()
 */
void
kl_print_int2(void *ptr, int flags)
{
	int16_t a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core
	 */
	if (align_chk && (uaddr_t)ptr % 2) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(int16_t*) ptr;
	if (flags & C_HEX) {
		kdb_printf("0x%hx", a);
	} else if (flags & C_BINARY) {
		uint64_t value = a & 0xffff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%hd", a);
	}
}

/*
 * kl_print_uint2()
 */
void
kl_print_uint2(void *ptr, int flags)
{
	uint16_t a;

	/* Make sure the pointer is properly aligned (or we will
	 * dump core
	 */
	if (align_chk && (uaddr_t)ptr % 2) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	a = *(uint16_t*) ptr;
	if (flags & C_HEX) {
		kdb_printf("0x%hx", a);
	} else if (flags & C_BINARY) {
		uint64_t value = a & 0xffff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%hu", a);
	}
}

/*
 * kl_print_char()
 */
void
kl_print_char(void *ptr, int flags)
{
	char c;

	if (flags & C_HEX) {
		kdb_printf("0x%x", (*(char *)ptr) & 0xff);
	} else if (flags & C_BINARY) {
		uint64_t value = (*(char *)ptr) & 0xff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		c = *(char *)ptr;

		kdb_printf("\'\\%03o\'", (unsigned char)c);
		switch (c) {
			case '\a' :
				kdb_printf(" = \'\\a\'");
				break;
			case '\b' :
				kdb_printf(" = \'\\b\'");
				break;
			case '\t' :
				kdb_printf(" = \'\\t\'");
				break;
			case '\n' :
				kdb_printf(" = \'\\n\'");
				break;
			case '\f' :
				kdb_printf(" = \'\\f\'");
				break;
			case '\r' :
				kdb_printf(" = \'\\r\'");
				break;
			case '\e' :
				kdb_printf(" = \'\\e\'");
				break;
			default :
				if( !iscntrl((unsigned char) c) ) {
					kdb_printf(" = \'%c\'", c);
				}
				break;
		}
	}
}

/*
 * kl_print_uchar()
 */
void
kl_print_uchar(void *ptr, int flags)
{
	if (flags & C_HEX) {
		kdb_printf("0x%x", *(unsigned char *)ptr);
	} else if (flags & C_BINARY) {
		uint64_t value = (*(unsigned char *)ptr) & 0xff;
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%u", *(unsigned char *)ptr);
	}
}

/*
 * kl_print_base()
 */
void
kl_print_base(void *ptr, int size, int encoding, int flags)
{
	/* FIXME: untested */
	if (invalid_address((kaddr_t)ptr, size)) {
		kdb_printf("ILLEGAL ADDRESS (%lx)", (uaddr_t)ptr);
		return;
	}
	switch (size) {

		case 1:
			if (encoding == ENC_UNSIGNED) {
				kl_print_uchar(ptr, flags);
			} else {
				kl_print_char(ptr, flags);
			}
			break;

		case 2:
			if (encoding == ENC_UNSIGNED) {
				kl_print_uint2(ptr, flags);
			} else {
				kl_print_int2(ptr, flags);
			}
			break;

		case 4:
			if (encoding == ENC_UNSIGNED) {
				kl_print_uint4(ptr, flags);
			} else if (encoding == ENC_FLOAT) {
				printk("error: print of 4-byte float\n");
				/* kl_print_float4(ptr, flags); */
			} else {
				kl_print_int4(ptr, flags);
			}
			break;

		case 8:
			if (encoding == ENC_UNSIGNED) {
				kl_print_uint8(ptr, flags);
			} else if (encoding == ENC_FLOAT) {
				printk("error: print of 8-byte float\n");
				/* kl_print_float8(ptr, flags); */
			} else {
				kl_print_int8(ptr, flags);
			}
			break;

		case 16:
			if (encoding == ENC_UNSIGNED) {
				/* Ex: unsigned long long */
				kl_print_uint16(ptr, flags);
			} else if (encoding == ENC_FLOAT) {
				printk("error: print of 16-byte float\n");
				/* Ex: long double */
				/* kl_print_float16(ptr, flags); */
			} else {
				/* Ex: long long */
				kl_print_int16(ptr, flags);
			}
			break;

		default:
			break;
	}
}

/*
 * kl_print_base_value()
 */
void
kl_print_base_value(void *ptr, kltype_t *kltp, int flags)
{
	kltype_t *rkltp=NULL;

	if (kltp->kl_type != KLT_BASE) {
		if (!(rkltp = kltp->kl_realtype)) {
			return;
		}
		if (rkltp->kl_type != KLT_BASE) {
			return;
		}
	} else {
		rkltp = kltp;
	}
	kl_print_base(ptr, rkltp->kl_size, rkltp->kl_encoding, flags);
}

/*
 * kl_print_typedef_type()
 */
void
kl_print_typedef_type(
	void *ptr,
	kltype_t *kltp,
	int level,
	int flags)
{
	char *name;
	kltype_t *rkltp;

	if (ptr) {
		rkltp = kltp->kl_realtype;
		while (rkltp->kl_type == KLT_TYPEDEF) {
			if (rkltp->kl_realtype) {
				rkltp = rkltp->kl_realtype;
			}
		}
		if (rkltp->kl_type == KLT_POINTER) {
			kl_print_pointer_type(ptr, kltp, level, flags);
			return;
		}
		switch (rkltp->kl_type) {
			case KLT_BASE:
				kl_print_base_type(ptr, kltp,
					level, flags);
				break;

			case KLT_UNION:
			case KLT_STRUCT:
				kl_print_struct_type(ptr, kltp,
					level, flags);
				break;

			case KLT_ARRAY:
				kl_print_array_type(ptr, kltp,
					level, flags);
				break;

			case KLT_ENUMERATION:
				kl_print_enumeration_type(ptr,
					kltp, level, flags);
				break;

			default:
				kl_print_base_type(ptr, kltp,
					level, flags);
				break;
		}
	} else {
		LEVEL_INDENT(level, flags);
		if (flags & NO_REALTYPE) {
			rkltp = kltp;
		} else {
			rkltp = kltp->kl_realtype;
			while (rkltp && rkltp->kl_type == KLT_POINTER) {
				rkltp = rkltp->kl_realtype;
			}
		}
		if (!rkltp) {
			if (SUPPRESS_NAME) {
				kdb_printf("<UNKNOWN>");
			} else {
				kdb_printf( "typedef <UNKNOWN>%s;",
					kltp->kl_name);
			}
			return;
		}
		if (rkltp->kl_type == KLT_FUNCTION) {
			if (kltp->kl_realtype->kl_type == KLT_POINTER) {
				kdb_printf("typedef %s(*%s)();",
					kltp->kl_typestr, kltp->kl_name);
			} else {
				kdb_printf( "typedef %s(%s)();",
					kltp->kl_typestr, kltp->kl_name);
			}
		} else if (rkltp->kl_type == KLT_ARRAY) {
			kl_print_array_type(ptr, rkltp, level, flags);
		} else if (rkltp->kl_type == KLT_TYPEDEF) {
			if (!(name = rkltp->kl_name)) {
				name = rkltp->kl_typestr;
			}

			if (SUPPRESS_NAME) {
				kdb_printf("%s", name);
			} else {
				kdb_printf("typedef %s%s;",
					name, kltp->kl_name);
			}
			print_realtype(rkltp);
		} else {
			kl_print_type(ptr, rkltp, level, flags);
		}
		PRINT_NL(flags);
	}
}

/*
 * kl_print_pointer_type()
 */
void
kl_print_pointer_type(
	void *ptr,
	kltype_t *kltp,
	int level,
	int flags)
{
	kltype_t *itp;

	if (kltp->kl_type == KLT_MEMBER) {
		itp = kltp->kl_realtype;
	} else {
		itp = kltp;
	}

	/* See if this is a pointer to a function. If it is, then it
	 * has to be handled differently...
	 */
	while (itp->kl_type == KLT_POINTER) {
		if ((itp = itp->kl_realtype)) {
			if (itp->kl_type == KLT_FUNCTION) {
				kl_print_function_type(ptr,
					kltp, level, flags);
				return;
			}
		} else {
			LEVEL_INDENT(level, flags);
			kdb_printf("%s%s;\n",
				kltp->kl_typestr, kltp->kl_name);
			return;
		}
	}

	LEVEL_INDENT(level, flags);
	if (ptr) {
		kaddr_t tmp;
		tmp = *(kaddr_t *)ptr;
		flags |= SUPPRESS_SEMI_COLON;
		if(kltp->kl_name){
			if (*(kaddr_t *)ptr) {
				kdb_printf("%s = 0x%"FMTPTR"x",
					kltp->kl_name, tmp);
			} else {
				kdb_printf("%s = (nil)", kltp->kl_name);
			}
		} else {
			if (tmp != 0) {
				kdb_printf("0x%"FMTPTR"x", tmp);
			} else {
				kdb_printf( "(nil)");
			}
		}
	} else {
		if (kltp->kl_typestr) {
			if (kltp->kl_name && !(flags & SUPPRESS_NAME)) {
				kdb_printf("%s%s",
					kltp->kl_typestr, kltp->kl_name);
			} else {
				kdb_printf("%s", kltp->kl_typestr);
			}
		} else {
			kdb_printf("<UNKNOWN>");
		}
	}
	PRINT_SEMI_COLON(level, flags);
	PRINT_NL(flags);
}

/*
 * kl_print_function_type()
 */
void
kl_print_function_type(
	void *ptr,
	kltype_t *kltp,
	int level,
	int flags)
{
	LEVEL_INDENT(level, flags);
	if (ptr) {
		kaddr_t a;

		a = *(kaddr_t *)ptr;
		kdb_printf("%s = 0x%"FMTPTR"x", kltp->kl_name, a);
	} else {
		if (flags & SUPPRESS_NAME) {
			kdb_printf("%s(*)()", kltp->kl_typestr);
		} else {
			kdb_printf("%s(*%s)();",
				kltp->kl_typestr, kltp->kl_name);
		}
	}
	PRINT_NL(flags);
}

/*
 * kl_print_array_type()
 */
void
kl_print_array_type(void *ptr, kltype_t *kltp, int level, int flags)
{
	int i, count = 0, anon = 0, size, low, high, multi = 0;
	char typestr[128], *name, *p;
	kltype_t *rkltp, *etp, *retp;

	if (kltp->kl_type != KLT_ARRAY) {
		if ((rkltp = kltp->kl_realtype)) {
			while (rkltp->kl_type != KLT_ARRAY) {
				if (!(rkltp = rkltp->kl_realtype)) {
					break;
				}
			}
		}
		if (!rkltp) {
			LEVEL_INDENT(level, flags);
			kdb_printf("<ARRAY_TYPE>");
			PRINT_SEMI_COLON(level, flags);
			PRINT_NL(flags);
			return;
		}
	} else {
		rkltp = kltp;
	}

	etp = rkltp->kl_elementtype;
	if (!etp) {
		LEVEL_INDENT(level, flags);
		kdb_printf("<BAD_ELEMENT_TYPE> %s", rkltp->kl_name);
		PRINT_SEMI_COLON(level, flags);
		PRINT_NL(flags);
		return;
	}

	/* Set retp to point to the actual element type. This is necessary
	 * for multi-dimensional arrays, which link using the kl_elementtype
	 * member.
	 */
	retp = etp;
	while (retp->kl_type == KLT_ARRAY) {
		retp = retp->kl_elementtype;
	}
	low = rkltp->kl_low_bounds + 1;
	high = rkltp->kl_high_bounds;

	if (ptr) {

		p = ptr;

		if ((retp->kl_size == 1) && (retp->kl_encoding == ENC_CHAR)) {
			if (kltp->kl_type == KLT_MEMBER) {
				LEVEL_INDENT(level, flags);
			}
			if (flags & SUPPRESS_NAME) {
				kdb_printf("\"");
				flags &= ~SUPPRESS_NAME;
			} else {
				kdb_printf("%s = \"", kltp->kl_name);
			}
			for (i = 0; i < high; i++) {
				if (*(char*)p == 0) {
					break;
				}
				kdb_printf("%c", *(char *)p);
				p++;
			}
			kdb_printf("\"");
			PRINT_NL(flags);
		} else {
			if (kltp->kl_type == KLT_MEMBER) {
				LEVEL_INDENT(level, flags);
			}

			if (flags & SUPPRESS_NAME) {
				kdb_printf("{\n");
				flags &= ~SUPPRESS_NAME;
			} else {
				kdb_printf("%s = {\n", kltp->kl_name);
			}

			if (retp->kl_type == KLT_POINTER) {
				size = sizeof(void *);
			} else {
				while (retp->kl_realtype) {
					retp = retp->kl_realtype;
				}
				size = retp->kl_size;
			}
			if ((retp->kl_type != KLT_STRUCT) &&
					(retp->kl_type != KLT_UNION)) {
				/* Turn off the printing of names for all
				 * but structs and unions.
				 */
				flags |= SUPPRESS_NAME;
			}
			for (i = low; i <= high; i++) {

				LEVEL_INDENT(level + 1, flags);
				kdb_printf("[%d] ", i);

				switch (retp->kl_type) {
					case KLT_POINTER :
						kl_print_pointer_type(
							p, retp, level,
							flags|NO_INDENT);
						break;

					case KLT_TYPEDEF:
						kl_print_typedef_type(
							p, retp, level,
							flags|NO_INDENT);
						break;

					case KLT_BASE:
						kl_print_base_value(p,
							retp, flags|NO_INDENT);
						kdb_printf("\n");
						break;

					case KLT_ARRAY:
						kl_print_array_type(p, retp,
							level + 1,
							flags|SUPPRESS_NAME);
						break;

					case KLT_STRUCT:
					case KLT_UNION:
						kl_print_struct_type(p,
							retp, level + 1,
							flags|NO_INDENT);
						break;

					default:
						kl_print_base_value(
							p, retp,
							flags|NO_INDENT);
						kdb_printf("\n");
						break;
				}
				p = (void *)((uaddr_t)p + size);
			}
			LEVEL_INDENT(level, flags);
			kdb_printf("}");
			PRINT_SEMI_COLON(level, flags);
			PRINT_NL(flags);
		}
	} else {
		if (rkltp) {
			count = (rkltp->kl_high_bounds -
					rkltp->kl_low_bounds) + 1;
		} else {
			count = 1;
		}

		if (!strcmp(retp->kl_typestr, "struct ") ||
				!strcmp(retp->kl_typestr, "union ")) {
			anon = 1;
		}
next_dimension:
		switch (retp->kl_type) {

                        case KLT_UNION:
                        case KLT_STRUCT:
				if (anon) {
					if (multi) {
						kdb_printf("[%d]", count);
						break;
					}
					kl_print_struct_type(ptr, retp, level,
						flags|
						SUPPRESS_NL|
						SUPPRESS_SEMI_COLON);
					if (kltp->kl_type == KLT_MEMBER) {
						kdb_printf(" %s[%d]",
							kltp->kl_name, count);
					} else {
						kdb_printf(" [%d]", count);
					}
					break;
				}
				/* else drop through */

			default:
				LEVEL_INDENT(level, flags);
				if (multi) {
					kdb_printf("[%d]", count);
					break;
				}
				name = kltp->kl_name;
				if (retp->kl_type == KLT_TYPEDEF) {
					strcpy(typestr, retp->kl_name);
					strcat(typestr, " ");
				} else {
					strcpy(typestr, retp->kl_typestr);
				}
				if (!name || (flags & SUPPRESS_NAME)) {
					kdb_printf("%s[%d]", typestr, count);
				} else {
					kdb_printf("%s%s[%d]",
						typestr, name, count);
				}
		}
		if (etp->kl_type == KLT_ARRAY) {
			count = etp->kl_high_bounds - etp->kl_low_bounds + 1;
			etp = etp->kl_elementtype;
			multi++;
			goto next_dimension;
		}
		PRINT_SEMI_COLON(level, flags);
		PRINT_NL(flags);
	}
}

/*
 * kl_print_enumeration_type()
 */
void
kl_print_enumeration_type(
	void *ptr,
	kltype_t *kltp,
	int level,
	int flags)
{
	unsigned long long val = 0;
	kltype_t *mp, *rkltp;

	rkltp = kl_realtype(kltp, KLT_ENUMERATION);
	if (ptr) {
		switch (kltp->kl_size) {
			case 1:
				val = *(unsigned long long *)ptr;
				break;

			case 2:
				val = *(uint16_t *)ptr;
				break;

			case 4:
				val = *(uint32_t *)ptr;
				break;

			case 8:
				val = *(uint64_t *)ptr;
				break;
		}
		mp = rkltp->kl_member;
		while (mp) {
			if (mp->kl_value == val) {
				break;
			}
			mp = mp->kl_member;
		}
		LEVEL_INDENT(level, flags);
		if (mp) {
			kdb_printf("%s = (%s=%lld)",
				kltp->kl_name, mp->kl_name, val);
		} else {
			kdb_printf("%s = %lld", kltp->kl_name, val);
		}
		PRINT_NL(flags);
	} else {
		LEVEL_INDENT(level, flags);
		kdb_printf ("%s {", kltp->kl_typestr);
		mp = rkltp->kl_member;
		while (mp) {
			kdb_printf("%s = %d", mp->kl_name, mp->kl_value);
			if ((mp = mp->kl_member)) {
				kdb_printf(", ");
			}
		}
		mp = kltp;
		if (level) {
			kdb_printf("} %s;", mp->kl_name);
		} else {
			kdb_printf("};");
		}
		PRINT_NL(flags);
	}
}

/*
 * kl_binary_print()
 */
void
kl_binary_print(uint64_t num)
{
	int i, pre = 1;

	for (i = 63; i >= 0; i--) {
		if (num & ((uint64_t)1 << i)) {
			kdb_printf("1");
			if (pre) {
				pre = 0;
			}
		} else {
			if (!pre) {
				kdb_printf("0");
			}
		}
	}
	if (pre) {
		kdb_printf("0");
	}
}

/*
 * kl_get_bit_value()
 *
 * x = byte_size, y = bit_size, z = bit_offset
 */
uint64_t
kl_get_bit_value(void *ptr, unsigned int x, unsigned int y, unsigned int z)
{
	uint64_t value=0, mask;

	/* handle x bytes of buffer -- doing just memcpy won't work
	 * on big endian architectures
	 */
        switch (x) {
	case 5:
	case 6:
	case 7:
	case 8:
		x = 8;
		value = *(uint64_t*) ptr;
		break;
	case 3:
	case 4:
		x = 4;
		value = *(uint32_t*) ptr;
		break;
	case 2:
		value = *(uint16_t*) ptr;
		break;
	case 1:
		value = *(uint8_t *)ptr;
		break;
	default:
		/* FIXME: set KL_ERROR */
		return(0);
        }
	/*
	    o FIXME: correct handling of overlapping fields
	*/

	/* goto bit offset */
	value = value >> z;

	/* mask bit size bits */
	mask = (((uint64_t)1 << y) - 1);
 	return (value & mask);
}

/*
 * kl_print_bit_value()
 *
 * x = byte_size, y = bit_size, z = bit_offset
 */
void
kl_print_bit_value(void *ptr, int x, int y, int z, int flags)
{
	unsigned long long value;

	value = kl_get_bit_value(ptr, x, y, z);
	if (flags & C_HEX) {
		kdb_printf("%#llx", value);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		kdb_printf("%lld", value);
	}
}

/*
 * kl_print_base_type()
 */
void
kl_print_base_type(void *ptr, kltype_t *kltp, int level, int flags)
{
	LEVEL_INDENT(level, flags);
	if (ptr) {
		if (!(flags & SUPPRESS_NAME))  {
			kdb_printf ("%s = ", kltp->kl_name);
		}
	}
	if (kltp->kl_type == KLT_MEMBER) {
		if (kltp->kl_bit_size < (kltp->kl_size * 8)) {
			if (ptr) {
				kl_print_bit_value(ptr, kltp->kl_size,
					kltp->kl_bit_size,
					kltp->kl_bit_offset, flags);
			} else {
				if (kltp->kl_name) {
					kdb_printf ("%s%s :%d;",
						kltp->kl_typestr,
						kltp->kl_name,
						kltp->kl_bit_size);
				} else {
					kdb_printf ("%s :%d;",
						kltp->kl_typestr,
						kltp->kl_bit_size);
				}
			}
			PRINT_NL(flags);
			return;
		}
	}
	if (ptr) {
		kltype_t *rkltp;

		rkltp = kl_realtype(kltp, 0);
		if (rkltp->kl_encoding == ENC_UNDEFINED) {
			/* This is a void value
			 */
			kdb_printf("<VOID>");
		} else {
			kl_print_base(ptr, kltp->kl_size,
				rkltp->kl_encoding, flags);
		}
	} else {
		if (kltp->kl_type == KLT_MEMBER) {
			if (flags & SUPPRESS_NAME) {
				kdb_printf ("%s", kltp->kl_typestr);
			} else {
				if (kltp->kl_name) {
					kdb_printf("%s%s;", kltp->kl_typestr,
						kltp->kl_name);
				} else {
					kdb_printf ("%s :%d;",
						kltp->kl_typestr,
						kltp->kl_bit_size);
				}
			}
		} else {
			if (SUPPRESS_NAME) {
				kdb_printf("%s", kltp->kl_name);
			} else {
				kdb_printf("%s;", kltp->kl_name);
			}
		}
	}
	PRINT_NL(flags);
}

/*
 * kl_print_member()
 */
void
kl_print_member(void *ptr, kltype_t *mp, int level, int flags)
{
	int kl_type = 0;
	kltype_t *rkltp;

	if (flags & C_SHOWOFFSET) {
		kdb_printf("%#x ", mp->kl_offset);
	}

	if ((rkltp = mp->kl_realtype)) {
		kl_type = rkltp->kl_type;
	} else
		kl_type = mp->kl_type;
	switch (kl_type) {
		case KLT_STRUCT:
		case KLT_UNION:
			kl_print_struct_type(ptr, mp, level, flags);
			break;
		case KLT_ARRAY:
			kl_print_array_type(ptr, mp, level, flags);
			break;
		case KLT_POINTER:
			kl_print_pointer_type(ptr, mp, level, flags);
			break;
		case KLT_FUNCTION:
			kl_print_function_type(ptr, mp, level, flags);
			break;
		case KLT_BASE:
			kl_print_base_type(ptr, mp, level, flags);
			break;
	        case KLT_ENUMERATION:
			kl_print_enumeration_type(ptr, mp, level, flags);
			break;
		case KLT_TYPEDEF:
			while (rkltp && rkltp->kl_realtype) {
				if (rkltp->kl_realtype == rkltp) {
					break;
				}
				rkltp = rkltp->kl_realtype;
			}
			if (ptr) {
				kl_print_typedef_type(ptr, mp,
					level, flags);
				break;
			}
			LEVEL_INDENT(level, flags);
			if (flags & SUPPRESS_NAME) {
				if (rkltp && (mp->kl_bit_size <
						(rkltp->kl_size * 8))) {
					kdb_printf ("%s :%d",
						mp->kl_typestr,
						mp->kl_bit_size);
				} else {
					kdb_printf("%s",
						mp->kl_realtype->kl_name);
				}
				print_realtype(mp->kl_realtype);
			} else {
				if (rkltp && (mp->kl_bit_size <
						(rkltp->kl_size * 8))) {
					if (mp->kl_name) {
						kdb_printf ("%s%s :%d;",
							mp->kl_typestr,
							mp->kl_name,
							mp->kl_bit_size);
					} else {
						kdb_printf ("%s :%d;",
							mp->kl_typestr,
							mp->kl_bit_size);
					}
				} else {
					kdb_printf("%s %s;",
						mp->kl_realtype->kl_name,
						mp->kl_name);
				}
			}
			PRINT_NL(flags);
			break;

		default:
			LEVEL_INDENT(level, flags);
			if (mp->kl_typestr) {
				kdb_printf("%s%s;",
					mp->kl_typestr, mp->kl_name);
			} else {
				kdb_printf("<\?\?\? kl_type:%d> %s;",
					kl_type, mp->kl_name);
			}
			PRINT_NL(flags);
			break;
	}
}

/*
 * kl_print_struct_type()
 */
void
kl_print_struct_type(void *buf, kltype_t *kltp, int level, int flags)
{
	int orig_flags = flags;
	void *ptr = NULL;
	kltype_t *mp, *rkltp;

	/* If we are printing out an actual struct, then don't print any
	 * semi colons.
	 */
	if (buf) {
		flags |= SUPPRESS_SEMI_COLON;
	}

	LEVEL_INDENT(level, flags);
	if ((level == 0) || (flags & NO_INDENT)) {
		kdb_printf("%s{\n", kltp->kl_typestr);
	} else {
		if (buf) {
			if (level && !(kltp->kl_flags & TYP_ANONYMOUS_FLG)) {
				kdb_printf("%s = %s{\n",
					kltp->kl_name, kltp->kl_typestr);
			} else {
				kdb_printf("%s{\n", kltp->kl_typestr);
			}
			flags &= (~SUPPRESS_NL);
		} else {
			if (kltp->kl_typestr) {
				kdb_printf("%s{\n", kltp->kl_typestr);
			} else {
				kdb_printf("<UNKNOWN> {\n");
			}
		}
	}

	/* If the SUPPRESS_NL, SUPPRESS_SEMI_COLON, and SUPPRESS_NAME flags
	 * are set and buf is NULL, then turn them off as they only apply
	 * at the end of the struct. We save the original flags for that
	 * purpose.
	 */
	if (!buf) {
		flags &= ~(SUPPRESS_NL|SUPPRESS_SEMI_COLON|SUPPRESS_NAME);
	}

	/* If the NO_INDENT is set, we need to turn it off at this
	 * point -- just in case we come across a member of this struct
	 * that is also a struct.
	 */
	if (flags & NO_INDENT) {
		flags &= ~(NO_INDENT);
	}

	if (kltp->kl_type == KLT_MEMBER) {
		rkltp = kl_realtype(kltp, 0);
	} else {
		rkltp = kltp;
	}
	level++;
	if ((mp = rkltp->kl_member)) {
		while (mp) {
			if (buf) {
				ptr = buf + mp->kl_offset;
			}
			kl_print_member(ptr, mp, level, flags);
			mp = mp->kl_member;
		}
	} else {
		if (kltp->kl_flags & TYP_INCOMPLETE_FLG) {
			LEVEL_INDENT(level, flags);
			kdb_printf("<INCOMPLETE TYPE>\n");
		}
	}
	level--;
	LEVEL_INDENT(level, flags);

	/* kl_size = 0 for empty structs */
	if (ptr || ((kltp->kl_size == 0) && buf)) {
		kdb_printf("}");
	} else if ((kltp->kl_type == KLT_MEMBER) &&
			!(orig_flags & SUPPRESS_NAME) &&
			!(kltp->kl_flags & TYP_ANONYMOUS_FLG)) {
		kdb_printf("} %s", kltp->kl_name);
	} else {
		kdb_printf("}");
	}
	PRINT_SEMI_COLON(level, orig_flags);
	PRINT_NL(orig_flags);
}

/*
 * kl_print_type()
 */
void
kl_print_type(void *buf, kltype_t *kltp, int level, int flags)
{
	void *ptr;

	if (buf) {
		if (kltp->kl_offset) {
			ptr = (void *)((uaddr_t)buf + kltp->kl_offset);
		} else {
			ptr = buf;
		}
	} else {
		ptr = 0;
	}

	/* Only allow binary printing for base types
	 */
	if (kltp->kl_type != KLT_BASE) {
		flags &= (~C_BINARY);
	}
	switch (kltp->kl_type) {

		case KLT_TYPEDEF:
			kl_print_typedef_type(ptr, kltp, level, flags);
			break;

		case KLT_STRUCT:
		case KLT_UNION:
			kl_print_struct_type(ptr, kltp, level, flags);
			break;

		case KLT_MEMBER:
			kl_print_member(ptr, kltp, level, flags);
			break;

		case KLT_POINTER:
			kl_print_pointer_type(ptr, kltp, level, flags);
			break;

		case KLT_FUNCTION:
			LEVEL_INDENT(level, flags);
			kl_print_function_type(ptr, kltp, level, flags);
			break;

		case KLT_ARRAY:
			kl_print_array_type(ptr, kltp, level, flags);
			break;

		case KLT_ENUMERATION:
			kl_print_enumeration_type(ptr,
				kltp, level, flags);
			break;

		case KLT_BASE:
			kl_print_base_type(ptr, kltp, level, flags);
			break;

		default:
			LEVEL_INDENT(level, flags);
			if (flags & SUPPRESS_NAME) {
				kdb_printf ("%s", kltp->kl_name);
			} else {
				kdb_printf ("%s %s;",
					kltp->kl_name, kltp->kl_name);
			}
			PRINT_NL(flags);
	}
}

/*
 * eval is from lcrash eval.c
 */

/* Forward declarations */
static void free_node(node_t *);
static node_t *make_node(token_t *, int);
static node_t *get_node_list(token_t *, int);
static node_t *do_eval(int);
static int is_unary(int);
static int is_binary(int);
static int precedence(int);
static node_t *get_sizeof(void);
static int replace_cast(node_t *, int);
static int replace_unary(node_t *, int);
static node_t *replace(node_t *, int);
static void array_to_element(node_t*, node_t*);
static int type_to_number(node_t *);
kltype_t *number_to_type(node_t *);
static type_t *eval_type(node_t *);
static type_t *get_type(char *, int);
static int add_rchild(node_t *, node_t *);
static void free_nodelist(node_t *);

/* Global variables
 */
static int logical_flag;
static node_t *node_list = (node_t *)NULL;
uint64_t eval_error;
char *error_token;

/*
 * set_eval_error()
 */
static void
set_eval_error(uint64_t ecode)
{
	eval_error = ecode;
}

/*
 * is_typestr()
 *
 * We check for "struct", "union", etc. separately because they
 * would not be an actual part of the type name. We also assume
 * that the string passed in
 *
 * - does not have any leading blanks or tabs
 * - is NULL terminated
 * - contains only one type name to check
 * - does not contain any '*' characters
 */
static int
is_typestr(char *str)
{
	int len;

	len = strlen(str);
	if ((len >= 6) && !strncmp(str, "struct", 6)) {
		return(1);
	} else if ((len >= 5) &&!strncmp(str, "union", 5)) {
		return(1);
	} else if ((len >= 5) &&!strncmp(str, "short", 5)) {
		return(1);
	} else if ((len >= 8) &&!strncmp(str, "unsigned", 8)) {
		return(1);
	} else if ((len >= 6) &&!strncmp(str, "signed", 6)) {
		return(1);
	} else if ((len >= 4) &&!strncmp(str, "long", 4)) {
		return(1);
	}
	/* Strip off any trailing blanks
	 */
	while(*str && ((str[strlen(str) - 1] == ' ')
			|| (str[strlen(str) - 1] == '\t'))) {
		str[strlen(str) - 1] = 0;
	}
	if (kl_find_type(str, KLT_TYPES)) {
		return (1);
	}
	return(0);
}

/*
 * free_tokens()
 */
static void
free_tokens(token_t *tp)
{
	token_t *t, *tnext;

	t = tp;
	while (t) {
		tnext = t->next;
		if (t->string) {
			kl_free_block((void *)t->string);
		}
		kl_free_block((void *)t);
		t = tnext;
	}
}

/*
 * process_text()
 */
static int
process_text(char **str, token_t *tok)
{
	char *cp = *str;
	char *s = NULL;
	int len = 0;

	/* Check and see if this token is a STRING or CHARACTER
	 * type (beginning with a single or double quote).
	 */
	if (*cp == '\'') {
		/* make sure that only a single character is between
		 * the single quotes (it can be an escaped character
		 * too).
		 */
		s = strpbrk((cp + 1), "\'");
		if (!s) {
			set_eval_error(E_SINGLE_QUOTE);
			error_token = tok->ptr;
			return(1);
		}
		len = (uaddr_t)s - (uaddr_t)cp;
		if ((*(cp+1) == '\\')) {
			if (*(cp+2) == '0') {
				long int val;
				unsigned long uval;
				char *ep;

				uval = kl_strtoull((char*)(cp+2),
						(char **)&ep, 8);
				val = uval;
				if ((val > 255) || (*ep != '\'')) {
					set_eval_error(E_BAD_CHAR);
					error_token = tok->ptr;
					return(1);
				}
			} else if (*(cp+3) != '\'') {
				set_eval_error(E_BAD_CHAR);
				error_token = tok->ptr;
				return(1);
			}
			tok->type = CHARACTER;
		} else if (len == 2) {
			tok->type = CHARACTER;
		} else {

			/* Treat as a single token entry. It's possible
			 * that what's between the single quotes is a
			 * type name. That will be determined later on.
			 */
			tok->type = STRING;
		}
		*str = cp + len;
	} else if (*cp == '\"') {
		s = strpbrk((cp + 1), "\"");
		if (!s) {
			set_eval_error(E_BAD_STRING);
			error_token = tok->ptr;
			return(1);
		}
		len = (uaddr_t)s - (uaddr_t)cp;
		tok->type = TEXT;
		*str = cp + len;
	}
	if ((tok->type == STRING) || (tok->type == TEXT)) {

		if ((tok->type == TEXT) && (strlen(cp) > (len + 1))) {

			/* Check to see if there is a comma or semi-colon
			 * directly following the string. If there is,
			 * then the string is OK (the following characters
			 * are part of the next expression). Also, it's OK
			 * to have trailing blanks as long as that's all
			 * threre is.
			 */
			char *c;

			c = s + 1;
			while (*c) {
				if ((*c == ',') || (*c == ';')) {
					break;
				} else if (*c != ' ') {
					set_eval_error(E_END_EXPECTED);
					tok->ptr = c;
					error_token = tok->ptr;
					return(1);
				}
				c++;
			}
			/* Truncate the trailing blanks (they are not
			 * part of the string).
			 */
			if (c != (s + 1)) {
				*(s + 1) = 0;
			}
		}
		tok->string = (char *)kl_alloc_block(len);
		memcpy(tok->string, (cp + 1), len - 1);
		tok->string[len - 1] = 0;
	}
	return(0);
}

/*
 * get_token_list()
 */
static token_t *
get_token_list(char *str)
{
	int paren_count = 0;
	char *cp;
	token_t *tok = (token_t*)NULL, *tok_head = (token_t*)NULL;
	token_t *tok_last = (token_t*)NULL;

	cp = str;
	eval_error = 0;

	while (*cp) {

		/* Skip past any "white space" (spaces and tabs).
		 */
		switch (*cp) {
			case ' ' :
			case '\t' :
			case '`' :
				cp++;
				continue;
			default :
				break;
		}

		/* Allocate space for the next token */
		tok = (token_t *)kl_alloc_block(sizeof(token_t));
		tok->ptr = cp;

		switch(*cp) {

			/* Check for operators
			 */
			case '+' :
				if (*((char*)cp + 1) == '+') {

					/* We aren't doing asignment here,
					 * so the ++ operator is not
					 * considered valid.
					 */
					set_eval_error(E_BAD_OPERATOR);
					error_token = tok_last->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				} else if (!tok_last ||
					(tok_last->operator &&
					(tok_last->operator != CLOSE_PAREN))) {
					tok->operator = UNARY_PLUS;
				} else {
					tok->operator = ADD;
				}
				break;

			case '-' :
				if (*((char*)cp + 1) == '-') {

					/* We aren't doing asignment here, so
					 * the -- operator is not considered
					 * valid.
					 */
					set_eval_error(E_BAD_OPERATOR);
					error_token = tok_last->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				} else if (*((char*)cp + 1) == '>') {
					tok->operator = RIGHT_ARROW;
					cp++;
				} else if (!tok_last || (tok_last->operator &&
					(tok_last->operator != CLOSE_PAREN))) {
					tok->operator = UNARY_MINUS;
				} else {
					tok->operator = SUBTRACT;
				}
				break;

			case '.' :
				/* XXX - need to check to see if this is a
				 * decimal point in the middle fo a floating
				 * point value.
				 */
				tok->operator = DOT;
				break;

			case '*' :
				/* XXX - need a better way to tell if this is
				 * an INDIRECTION. perhaps check the next
				 * token?
				 */
				if (!tok_last || (tok_last->operator &&
					((tok_last->operator != CLOSE_PAREN) &&
					(tok_last->operator != CAST)))) {
					tok->operator = INDIRECTION;
				} else {
					tok->operator = MULTIPLY;
				}
				break;

			case '/' :
				tok->operator = DIVIDE;
				break;

			case '%' :
				tok->operator = MODULUS;
				break;

			case '(' : {
				char *s, *s1, *s2;
				int len;

				/* Make sure the previous token is an operator
				 */
				if (tok_last && !tok_last->operator) {
					set_eval_error(E_SYNTAX_ERROR);
					error_token = tok_last->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				}

				if (tok_last &&
					((tok_last->operator == RIGHT_ARROW) ||
						(tok_last->operator == DOT))) {
					set_eval_error(E_SYNTAX_ERROR);
					error_token = tok_last->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				}

				/* Check here to see if following tokens
				 * constitute a cast.
				 */

				/* Skip past any "white space" (spaces
				 * and tabs)
				 */
				while ((*(cp+1) == ' ') || (*(cp+1) == '\t')) {
					cp++;
				}
				if ((*(cp+1) == '(') || isdigit(*(cp+1)) ||
					(*(cp+1) == '+') || (*(cp+1) == '-') ||
					(*(cp+1) == '*') || (*(cp+1) == '&') ||
						(*(cp+1) == ')')){
					tok->operator = OPEN_PAREN;
					paren_count++;
					break;
				}

				/* Make sure we have a CLOSE_PAREN.
				 */
				if (!(s1 = strchr(cp+1, ')'))) {
					set_eval_error(E_OPEN_PAREN);
					error_token = tok->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				}
				/* Check to see if this is NOT a simple
				 * typecast.
				 */
				if (!(s2 = strchr(cp+1, '.'))) {
					s2 = strstr(cp+1, "->");
				}
				if (s2 && (s2 < s1)) {
					tok->operator = OPEN_PAREN;
					paren_count++;
					break;
				}

				if ((s = strpbrk(cp+1, "*)"))) {
					char str[128];

					len = (uaddr_t)s - (uaddr_t)(cp+1);
					strncpy(str, cp+1, len);
					str[len] = 0;
					if (!is_typestr(str)) {
						set_eval_error(E_BAD_TYPE);
						error_token = tok->ptr;
						free_tokens(tok_head);
						free_tokens(tok);
						return ((token_t*)NULL);
					}
					if (!(s = strpbrk((cp+1), ")"))) {
						set_eval_error(E_OPEN_PAREN);
						error_token = tok->ptr;
						free_tokens(tok_head);
						free_tokens(tok);
						return ((token_t*)NULL);
					}
					len = (uaddr_t)s - (uaddr_t)(cp+1);
					tok->string = (char *)
						kl_alloc_block(len + 1);
					memcpy(tok->string, (cp+1), len);
					tok->string[len] = 0;
					tok->operator = CAST;
					cp = (char *)((uaddr_t)(cp+1) + len);
					break;
				}
				tok->operator = OPEN_PAREN;
				paren_count++;
				break;
			}

			case ')' :
				if (tok_last && ((tok_last->operator ==
						RIGHT_ARROW) ||
						(tok_last->operator == DOT))) {
					set_eval_error(E_SYNTAX_ERROR);
					error_token = tok_last->ptr;
					free_tokens(tok_head);
					free_tokens(tok);
					return ((token_t*)NULL);
				}
				tok->operator = CLOSE_PAREN;
				paren_count--;
				break;

			case '&' :
				if (*((char*)cp + 1) == '&') {
					tok->operator = LOGICAL_AND;
					cp++;
				} else if (!tok_last || (tok_last &&
					(tok_last->operator &&
						tok_last->operator !=
						CLOSE_PAREN))) {
					tok->operator = ADDRESS;
				} else {
					tok->operator = BITWISE_AND;
				}
				break;

			case '|' :
				if (*((char*)cp + 1) == '|') {
					tok->operator = LOGICAL_OR;
					cp++;
				} else {
					tok->operator = BITWISE_OR;
				}
				break;

			case '=' :
				if (*((char*)cp + 1) == '=') {
					tok->operator = EQUAL;
					cp++;
				} else {
					/* ASIGNMENT -- NOT IMPLEMENTED
					 */
					tok->operator = NOT_YET;
				}
				break;

			case '<' :
				if (*((char*)cp + 1) == '<') {
					tok->operator = LEFT_SHIFT;
					cp++;
				} else if (*((char*)cp + 1) == '=') {
					tok->operator = LESS_THAN_OR_EQUAL;
					cp++;
				} else {
					tok->operator = LESS_THAN;
				}
				break;

			case '>' :
				if (*((char*)(cp + 1)) == '>') {
					tok->operator = RIGHT_SHIFT;
					cp++;
				} else if (*((char*)cp + 1) == '=') {
					tok->operator = GREATER_THAN_OR_EQUAL;
					cp++;
				} else {
					tok->operator = GREATER_THAN;
				}
				break;

			case '!' :
				if (*((char*)cp + 1) == '=') {
					tok->operator = NOT_EQUAL;
					cp++;
				} else {
					tok->operator = LOGICAL_NEGATION;
				}
				break;

			case '$' :
				set_eval_error(E_NOT_IMPLEMENTED);
				error_token = tok->ptr;
				free_tokens(tok_head);
				free_tokens(tok);
				return((token_t*)NULL);
			case '~' :
				tok->operator = ONES_COMPLEMENT;
				break;

			case '^' :
				tok->operator = BITWISE_EXCLUSIVE_OR;
				break;

			case '?' :
				set_eval_error(E_NOT_IMPLEMENTED);
				error_token = tok->ptr;
				free_tokens(tok_head);
				free_tokens(tok);
				return((token_t*)NULL);
			case ':' :
				set_eval_error(E_NOT_IMPLEMENTED);
				error_token = tok->ptr;
				free_tokens(tok_head);
				free_tokens(tok);
				return((token_t*)NULL);
			case '[' :
				tok->operator = OPEN_SQUARE_BRACKET;;
				break;

			case ']' :
				tok->operator = CLOSE_SQUARE_BRACKET;;
				break;

			default: {

				char *s;
				int len;

				/* See if the last token is a RIGHT_ARROW
				 * or a DOT. If it is, then this token must
				 * be the name of a struct/union member.
				 */
				if (tok_last &&
					((tok_last->operator == RIGHT_ARROW) ||
						 (tok_last->operator == DOT))) {
					tok->type = MEMBER;
				} else if (process_text(&cp, tok)) {
					free_tokens(tok_head);
					free_tokens(tok);
					return((token_t*)NULL);
				}
				if (tok->type == TEXT) {
					return(tok);
				} else if (tok->type == STRING) {
					if (is_typestr(tok->string)) {
						tok->type = TYPE_DEF;
					} else {
						tok->operator = TEXT;
						return(tok);
					}
					break;
				} else if (tok->type == CHARACTER) {
					break;
				}

				/* Check and See if the entire string is
				 * a typename (valid only for whatis case).
				 */
				s = strpbrk(cp,
					".\t+-*/()[]|~!$&%^<>?:&=^\"\'");
				if (!s && !tok->type && is_typestr(cp)) {
					tok->type = TYPE_DEF;
					len = strlen(cp) + 1;
					tok->string = (char *)
						kl_alloc_block(len);
					memcpy(tok->string, cp, len - 1);
					tok->string[len - 1] = 0;
					cp = (char *)((uaddr_t)cp + len - 2);
					break;
				}

				/* Now check for everything else
				 */
				if ((s = strpbrk(cp,
					" .\t+-*/()[]|~!$&%^<>?:&=^\"\'"))) {
					len = (uaddr_t)s - (uaddr_t)cp + 1;
				} else {
					len = strlen(cp) + 1;
				}

				tok->string =
					(char *)kl_alloc_block(len);
				memcpy(tok->string, cp, len - 1);
				tok->string[len - 1] = 0;

				cp = (char *)((uaddr_t)cp + len - 2);

				/* Check to see if this is the keyword
				 * "sizeof". If not, then check to see if
				 * the string is a member name.
				 */
				if (!strcmp(tok->string, "sizeof")) {
					tok->operator = SIZEOF;
					kl_free_block((void *)tok->string);
					tok->string = 0;
				} else if (tok_last &&
					((tok_last->operator == RIGHT_ARROW) ||
					 (tok_last->operator == DOT))) {
					tok->type = MEMBER;
				} else {
					tok->type = STRING;
				}
				break;
			}
		}
		if (!(tok->type)) {
			tok->type = OPERATOR;
		}
		if (!tok_head) {
			tok_head = tok_last = tok;
		} else {
			tok_last->next = tok;
			tok_last = tok;
		}
		cp++;
	}
	if (paren_count < 0) {
		set_eval_error(E_CLOSE_PAREN);
		error_token = tok->ptr;
		free_tokens(tok_head);
		return((token_t*)NULL);
	} else if (paren_count > 0) {
		set_eval_error(E_OPEN_PAREN);
		error_token = tok->ptr;
		free_tokens(tok_head);
		return((token_t*)NULL);
	}
	return(tok_head);
}

/*
 * valid_binary_args()
 */
int
valid_binary_args(node_t *np, node_t *left, node_t *right)
{
	int op = np->operator;

	if ((op == RIGHT_ARROW) || (op == DOT)) {
		if (!left) {
			set_eval_error(E_MISSING_STRUCTURE);
			error_token = np->tok_ptr;
			return(0);
		} else if (!(left->node_type == TYPE_DEF) &&
				!(left->node_type == MEMBER) &&
				!(left->operator == CLOSE_PAREN) &&
				!(left->operator == CLOSE_SQUARE_BRACKET)) {
			set_eval_error(E_BAD_STRUCTURE);
			error_token = left->tok_ptr;
			return(0);
		}
		if (!right || (!(right->node_type == MEMBER))) {
			set_eval_error(E_BAD_MEMBER);
			error_token = np->tok_ptr;
			return(0);
		}
		return(1);
	}
	if (!left || !right) {
		set_eval_error(E_MISSING_OPERAND);
		error_token = np->tok_ptr;
		return(0);
	}
	switch (left->operator) {
		case CLOSE_PAREN:
		case CLOSE_SQUARE_BRACKET:
			break;
		default:
			switch(left->node_type) {
				case NUMBER:
				case STRING:
				case TEXT:
				case CHARACTER:
				case EVAL_VAR:
				case MEMBER:
					break;
				default:
					set_eval_error(E_BAD_OPERAND);
					error_token = np->tok_ptr;
					return(0);
			}
	}
	switch (right->operator) {
		case OPEN_PAREN:
			break;
		default:
			switch(right->node_type) {
				case NUMBER:
				case STRING:
				case TEXT:
				case CHARACTER:
				case EVAL_VAR:
				case MEMBER:
					break;
				default:
					set_eval_error(E_BAD_OPERAND);
					error_token = np->tok_ptr;
					return(0);
			}
	}
	return(1);
}

/*
 * get_node_list()
 */
static node_t *
get_node_list(token_t *tp, int flags)
{
	node_t *root = (node_t *)NULL;
	node_t *np = (node_t *)NULL;
	node_t *last = (node_t *)NULL;

	/* Loop through the tokens and convert them to nodes.
	 */
	while (tp) {
		np = make_node(tp, flags);
		if (eval_error) {
			return((node_t *)NULL);
		}
		if (root) {
			last->next = np;
			last = np;
		} else {
			root = last = np;
		}
		tp = tp->next;
	}
	last->next = (node_t *)NULL; /* cpw patch */
	last = (node_t *)NULL;
	for (np = root; np; np = np->next) {
		if (is_binary(np->operator)) {
			if (!valid_binary_args(np, last, np->next)) {
				free_nodelist(root);
				return((node_t *)NULL);
			}
		}
		last = np;
	}
	return(root);
}

/*
 * next_node()
 */
static node_t *
next_node(void)
{
	node_t *np;
	if ((np = node_list)) {
		node_list = node_list->next;
		np->next = (node_t*)NULL;
	}
	return(np);
}

/*
 * eval_unary()
 */
static node_t *
eval_unary(node_t *curnp, int flags)
{
	node_t *n0, *n1;

	n0 = curnp;

	/* Peek ahead and make sure there is a next node.
	 * Also check to see if the next node requires
	 * a recursive call to do_eval(). If it does, we'll
	 * let the do_eval() call take care of pulling it
	 * off the list.
	 */
	if (!node_list) {
		set_eval_error(E_SYNTAX_ERROR);
		error_token = n0->tok_ptr;
		free_nodes(n0);
		return((node_t*)NULL);
	}
	if (n0->operator == CAST) {
		if (node_list->operator == CLOSE_PAREN) {

			/* Free the CLOSE_PAREN and return
			 */
			free_node(next_node());
			return(n0);
		}
		if (!(node_list->node_type == NUMBER) &&
				!(node_list->node_type == VADDR) &&
				!((node_list->operator == ADDRESS) ||
				(node_list->operator == CAST) ||
				(node_list->operator == UNARY_MINUS) ||
				(node_list->operator == UNARY_PLUS) ||
				(node_list->operator == INDIRECTION) ||
				(node_list->operator == OPEN_PAREN))) {
			set_eval_error(E_SYNTAX_ERROR);
			error_token = node_list->tok_ptr;
			free_nodes(n0);
			return((node_t*)NULL);
		}
	}
	if ((n0->operator == INDIRECTION) ||
			(n0->operator == ADDRESS) ||
			(n0->operator == OPEN_PAREN) ||
			is_unary(node_list->operator)) {
		n1 = do_eval(flags);
		if (eval_error) {
			free_nodes(n0);
			free_nodes(n1);
			return((node_t*)NULL);
		}
	} else {
		n1 = next_node();
	}

	if (n1->operator == OPEN_PAREN) {
		/* Get the value contained within the parenthesis.
		 * If there was an error, just return.
		 */
		free_node(n1);
		n1 = do_eval(flags);
		if (eval_error) {
			free_nodes(n1);
			free_nodes(n0);
			return((node_t*)NULL);
		}
	}

	n0->right = n1;
	if (replace_unary(n0, flags) == -1) {
		if (!eval_error) {
			set_eval_error(E_SYNTAX_ERROR);
			error_token = n0->tok_ptr;
		}
		free_nodes(n0);
		return((node_t*)NULL);
	}
	return(n0);
}

/*
 * do_eval() -- Reduces an equation to a single value.
 *
 *   Any parenthesis (and nested parenthesis) within the equation will
 *   be solved first via recursive calls to do_eval().
 */
static node_t *
do_eval(int flags)
{
	node_t *root = (node_t*)NULL, *curnp, *n0, *n1;

	/* Loop through the list of nodes until we run out of nodes
	 * or we hit a CLOSE_PAREN. If we hit an OPEN_PAREN, make a
	 * recursive call to do_eval().
	 */
	curnp = next_node();
	while (curnp) {
		n0 = n1 = (node_t *)NULL;

		if (curnp->operator == OPEN_PAREN) {
			/* Get the value contained within the parenthesis.
			 * If there was an error, just return.
			 */
			free_node(curnp);
			n0 = do_eval(flags);
			if (eval_error) {
				free_nodes(n0);
				free_nodes(root);
				return((node_t *)NULL);
			}

		} else if (curnp->operator == SIZEOF) {
			/* Free the SIZEOF node and then make a call
			 * to the get_sizeof() function (which will
			 * get the next node off the list).
			 */
			n0 = get_sizeof();
			if (eval_error) {
				if (!error_token) {
					error_token = curnp->tok_ptr;
				}
				free_node(curnp);
				free_nodes(root);
				return((node_t *)NULL);
			}
			free_node(curnp);
			curnp = (node_t *)NULL;
		} else if (is_unary(curnp->operator)) {
			n0 = eval_unary(curnp, flags);
		} else {
			n0 = curnp;
			curnp = (node_t *)NULL;
		}
		if (eval_error) {
			free_nodes(n0);
			free_nodes(root);
			return((node_t *)NULL);
		}

		/* n0 should now contain a non-operator node. Check to see if
		 * there is a next token. If there isn't, just add the last
		 * rchild and return.
		 */
		if (!node_list) {
			if (root) {
				add_rchild(root, n0);
			} else {
				root = n0;
			}
			replace(root, flags);
			if (eval_error) {
				free_nodes(root);
				return((node_t *)NULL);
			}
			return(root);
		}

		/* Make sure the next token is an operator.
		 */
		if (!node_list->operator) {
			free_nodes(root);
			free_node(n0);
			set_eval_error(E_SYNTAX_ERROR);
			error_token = node_list->tok_ptr;
			return((node_t *)NULL);
		} else if ((node_list->operator == CLOSE_PAREN) ||
			(node_list->operator == CLOSE_SQUARE_BRACKET)) {

			if (root) {
				add_rchild(root, n0);
			} else {
				root = n0;
			}

			/* Reduce the resulting tree to a single value
			 */
			replace(root, flags);
			if (eval_error) {
				free_nodes(root);
				return((node_t *)NULL);
			}

			/* Step over the CLOSE_PAREN or CLOSE_SQUARE_BRACKET
			 * and then return.
			 */
			free_node(next_node());
			return(root);
		} else if (node_list->operator == OPEN_SQUARE_BRACKET) {
next_dimension1:
			/* skip over the OPEN_SQUARE_BRACKET token
			 */
			free_node(next_node());

			/* Get the value contained within the brackets. This
			 * value must represent an array index (value or
			 * equation).
			 */
			n1 = do_eval(0);
			if (eval_error) {
				free_nodes(root);
				free_node(n0);
				free_node(n1);
				return((node_t *)NULL);
			}

			/* Convert the array (or pointer type) to an
			 * element type using the index value obtained
			 * above. Make sure that n0 contains some sort
			 * of type definition first, however.
			 */
			if (n0->node_type != TYPE_DEF) {
				set_eval_error(E_BAD_TYPE);
				error_token = n0->tok_ptr;
				free_nodes(n0);
				free_nodes(n1);
				free_nodes(root);
				return((node_t *)NULL);
			}
			array_to_element(n0, n1);
			free_node(n1);
			if (eval_error) {
				free_nodes(root);
				free_nodes(n0);
				return((node_t *)NULL);
			}

			/* If there aren't any more nodes, just
			 * return.
			 */
			if (!node_list) {
				return(n0);
			}
			if (node_list->operator == OPEN_SQUARE_BRACKET) {
				goto next_dimension1;
			}
		} else if (!is_binary(node_list->operator)) {
			set_eval_error(E_BAD_OPERATOR);
			error_token = node_list->tok_ptr;
			free_nodes(root);
			free_nodes(n0);
			return((node_t *)NULL);
		}

		/* Now get the operator node
		 */
		if (!(n1 = next_node())) {
			set_eval_error(E_SYNTAX_ERROR);
			error_token = n0->tok_ptr;
			free_nodes(n0);
			free_nodes(root);
			return((node_t *)NULL);
		}

		/* Check to see if this binary operator is RIGHT_ARROW or DOT.
		 * If it is, we need to reduce it to a single value node now.
		 */
		while ((n1->operator == RIGHT_ARROW) || (n1->operator == DOT)) {

			/* The next node must contain the name of the
			 * struct|union member.
			 */
			if (!node_list || (node_list->node_type != MEMBER)) {
				set_eval_error(E_BAD_MEMBER);
				error_token = n1->tok_ptr;
				free_nodes(n0);
				free_nodes(n1);
				free_nodes(root);
				return((node_t *)NULL);
			}
			n1->left = n0;

			/* Now get the next node and link it as the
			 * right child.
			 */
			if (!(n0 = next_node())) {
				set_eval_error(E_SYNTAX_ERROR);
				error_token = n1->tok_ptr;
				free_nodes(n1);
				free_nodes(root);
				return((node_t *)NULL);
			}
			n1->right = n0;
			if (!(n0 = replace(n1, flags))) {
				if (!(eval_error)) {
					set_eval_error(E_SYNTAX_ERROR);
					error_token = n1->tok_ptr;
				}
				free_nodes(n1);
				free_nodes(root);
				return((node_t *)NULL);
			}
			n1 = (node_t *)NULL;

			/* Check to see if there is a next node. If there
			 * is, check to see if it is the operator CLOSE_PAREN.
			 * If it is, then return (skipping over the
			 * CLOSE_PAREN first).
			 */
			if (node_list && ((node_list->operator == CLOSE_PAREN)
						|| (node_list->operator ==
						CLOSE_SQUARE_BRACKET))) {
				if (root) {
					add_rchild(root, n0);
				} else {
					root = n0;
				}

				/* Reduce the resulting tree to a single
				 * value
				 */
				replace(root, flags);
				if (eval_error) {
					free_nodes(root);
					return((node_t *)NULL);
				}

				/* Advance the token pointer past the
				 * CLOSE_PAREN and then return.
				 */
				free_node(next_node());
				return(root);
			}

			/* Check to see if the next node is an
			 * OPEN_SQUARE_BRACKET. If it is, then we have to
			 * reduce the contents of the square brackets to
			 * an index array.
			 */
			if (node_list && (node_list->operator
						== OPEN_SQUARE_BRACKET)) {

				/* Advance the token pointer and call
				 * do_eval() again.
				 */
				free_node(next_node());
next_dimension2:
				n1 = do_eval(0);
				if (eval_error) {
					free_node(n0);
					free_node(n1);
					free_nodes(root);
					return((node_t *)NULL);
				}

				/* Convert the array (or pointer type) to
				 * an element type using the index value
				 * obtained above. Make sure that n0
				 * contains some sort of type definition
				 * first, however.
				 */
				if (n0->node_type != TYPE_DEF) {
					set_eval_error(E_BAD_TYPE);
					error_token = n0->tok_ptr;
					free_node(n0);
					free_node(n1);
					free_node(root);
					return((node_t *)NULL);
				}
				array_to_element(n0, n1);
				free_node(n1);
				if (eval_error) {
					free_node(n0);
					free_node(root);
					return((node_t *)NULL);
				}
			}

			/* Now get the next operator node (if there is one).
			 */
			if (!node_list) {
				if (root) {
					add_rchild(root, n0);
				} else {
					root = n0;
				}
				return(root);
			}
			n1 = next_node();
			if (n1->operator == OPEN_SQUARE_BRACKET) {
				goto next_dimension2;
			}
		}

		if (n1 && ((n1->operator == CLOSE_PAREN) ||
				(n1->operator == CLOSE_SQUARE_BRACKET))) {
			free_node(n1);
			if (root) {
				add_rchild(root, n0);
			} else {
				root = n0;
			}
			replace(root, flags);
			if (eval_error) {
				free_nodes(root);
				return((node_t *)NULL);
			}
			return(root);
		}

		if (!root) {
			root = n1;
			n1->left = n0;
		} else if (precedence(root->operator)
				>= precedence(n1->operator)) {
			add_rchild(root, n0);
			n1->left = root;
			root = n1;
		} else {
			if (!root->right) {
				n1->left = n0;
				root->right = n1;
			} else {
				add_rchild(root, n0);
				n1->left = root->right;
				root->right = n1;
			}
		}
		curnp = next_node();
	} /* while(curnp) */
	return(root);
}

/*
 * is_unary()
 */
static int
is_unary(int op)
{
	switch (op) {
		case LOGICAL_NEGATION :
		case ADDRESS :
		case INDIRECTION :
		case UNARY_MINUS :
		case UNARY_PLUS :
		case ONES_COMPLEMENT :
		case CAST :
			return(1);

		default :
			return(0);
	}
}


/*
 * is_binary()
 */
static int
is_binary(int op)
{
	switch (op) {

		case BITWISE_OR :
		case BITWISE_EXCLUSIVE_OR :
		case BITWISE_AND :
		case RIGHT_SHIFT :
		case LEFT_SHIFT :
		case ADD :
		case SUBTRACT :
		case MULTIPLY :
		case DIVIDE :
		case MODULUS :
		case LOGICAL_OR :
		case LOGICAL_AND :
		case EQUAL :
		case NOT_EQUAL :
		case LESS_THAN :
		case GREATER_THAN :
		case LESS_THAN_OR_EQUAL :
		case GREATER_THAN_OR_EQUAL :
		case RIGHT_ARROW :
		case DOT :
			return(1);

		default :
			return(0);
	}
}

/*
 * precedence()
 */
static int
precedence(int a)
{
	if ((a >= CONDITIONAL) && (a <= CONDITIONAL_ELSE)) {
		return(1);
	} else if (a == LOGICAL_OR) {
		return(2);
	} else if (a == LOGICAL_AND) {
		return(3);
	} else if (a == BITWISE_OR) {
		return(4);
	} else if (a == BITWISE_EXCLUSIVE_OR) {
		return(5);
	} else if (a == BITWISE_AND) {
		return(6);
	} else if ((a >= EQUAL) && (a <= NOT_EQUAL)) {
		return(7);
	} else if ((a >= LESS_THAN) && (a <= GREATER_THAN_OR_EQUAL)) {
		return(8);
	} else if ((a >= RIGHT_SHIFT) && (a <= LEFT_SHIFT)) {
		return(9);
	} else if ((a >= ADD) && (a <= SUBTRACT)) {
		return(10);
	} else if ((a >= MULTIPLY) && (a <= MODULUS)) {
		return(11);
	} else if ((a >= LOGICAL_NEGATION) && (a <= SIZEOF)) {
		return(12);
	} else if ((a >= RIGHT_ARROW) && (a <= DOT)) {
		return(13);
	} else {
		return(0);
	}
}

/*
 * esc_char()
 */
char
esc_char(char *str)
{
	long int val;
	unsigned long uval;
	char ch;

	if (strlen(str) > 1) {
		uval = kl_strtoull(str, (char **)NULL, 8);
		val = uval;
		ch = (char)val;
	} else {
		ch = str[0];
	}
	switch (ch) {
		case 'a' :
			return((char)7);
		case 'b' :
			return((char)8);
		case 't' :
			return((char)9);
		case 'n' :
			return((char)10);
		case 'f' :
			return((char)12);
		case 'r' :
			return((char)13);
		case 'e' :
			return((char)27);
		default:
			return(ch);
	}
}

/*
 * make_node()
 */
static node_t *
make_node(token_t *t, int flags)
{
	node_t *np;

	set_eval_error(0);
	np = (node_t*)kl_alloc_block(sizeof(*np));

	if (t->type == OPERATOR) {

		/* Check to see if this token represents a typecast
		 */
		if (t->operator == CAST) {
			type_t *tp;

			if (!(np->type = get_type(t->string, flags))) {
				set_eval_error(E_BAD_CAST);
				error_token = t->ptr;
				free_nodes(np);
				return((node_t*)NULL);
			}

			/* Determin if this is a pointer to a type
			 */
			tp = np->type;
			if (tp->flag == POINTER_FLAG) {
				np->flags = POINTER_FLAG;
				tp = tp->t_next;
				while (tp->flag == POINTER_FLAG) {
					tp = tp->t_next;
				}
			}
			switch(tp->flag) {
				case KLTYPE_FLAG:
					np->flags |= KLTYPE_FLAG;
					break;

				default:
					free_nodes(np);
					set_eval_error(E_BAD_CAST);
					error_token = t->ptr;
					return((node_t*)NULL);
			}
			if (!t->next) {
				if (flags & C_WHATIS) {
					np->node_type = TYPE_DEF;
				} else {
					set_eval_error(E_BAD_CAST);
					error_token = t->ptr;
					return((node_t*)NULL);
				}
			} else {
				np->node_type = OPERATOR;
				np->operator = CAST;
			}
		} else {
			np->node_type = OPERATOR;
			np->operator = t->operator;
		}
	} else if (t->type == MEMBER) {
		np->name = (char *)dup_block((void *)t->string, strlen(t->string)+1);
		np->node_type = MEMBER;
	} else if ((t->type == STRING) || (t->type == TYPE_DEF)) {
		syment_t *sp;
		dbg_sym_t *stp;
		dbg_type_t *sttp;

		if ((sp = kl_lkup_symname(t->string))) {
		    if (!(flags & C_NOVARS)) {
			int has_type = 0;

			/* The string is a symbol name. We'll treat it as
			 * a global kernel variable and, at least, gather in
			 * the address of the symbol and the value it points
			 * to.
			 */
			np->address = sp->s_addr;
			np->flags |= ADDRESS_FLAG;
			np->name = t->string;
			t->string = (char*)NULL;

			/* Need to see if there is type information available
			 * for this variable. Since this mapping is not
			 * available yet, we will just attach a type struct
			 * for either uint32_t or uint64_t (depending on the
			 * size of a kernel pointer).  That will at least let
			 * us do something and will prevent the scenario where
			 * we have a type node with out a pointer to a type
			 * struct!
			 */
			np->node_type = TYPE_DEF;
			np->flags |= KLTYPE_FLAG;
			np->value = *((kaddr_t *)np->address);
			/* try to get the actual type info for the variable */
			if(((stp = dbg_find_sym(sp->s_name, DBG_VAR,
						(uint64_t)0)) != NULL)){
				if((sttp = (dbg_type_t *)
					kl_find_typenum(stp->sym_typenum))
						!= NULL){
					/* kl_get_typestring(sttp); */
					has_type = 1;
					if(sttp->st_klt.kl_type == KLT_POINTER){
						np->flags ^= KLTYPE_FLAG;
						np->flags |= POINTER_FLAG;
						np->type =
						  get_type(sttp->st_typestr,
								flags);
					} else {
						np->type =
						 kl_alloc_block(sizeof(type_t));
						np->type->un.kltp =
							&sttp->st_klt;
					}
				}
			}
			/* no type info for the variable found */
			if(!has_type){
				if (ptrsz64) {
					np->type = get_type("uint64_t", flags);
				} else {
					np->type = get_type("uint32_t", flags);
				}
			}
		    }
		    kl_free_block((void *)sp);
		} else if (flags & (C_WHATIS|C_SIZEOF)) {

			kltype_t *kltp;

			if ((kltp = kl_find_type(t->string, KLT_TYPES))) {

				np->node_type = TYPE_DEF;
				np->flags = KLTYPE_FLAG;
				np->type = (type_t*)
					kl_alloc_block(sizeof(type_t));
				np->type->flag = KLTYPE_FLAG;
				np->type->t_kltp = kltp;
			} else {
				if (get_value(t->string,
					(uint64_t *)&np->value)) {
					set_eval_error(E_BAD_VALUE);
					error_token = t->ptr;
					free_nodes(np);
					return((node_t*)NULL);
				}
				if (!strncmp(t->string, "0x", 2) ||
						!strncmp(t->string, "0X", 2)) {
					np->flags |= UNSIGNED_FLAG;
				}
				np->node_type = NUMBER;
			}
			np->tok_ptr = t->ptr;
			return(np);
		} else {
			if (get_value(t->string, (uint64_t *)&np->value)) {
				set_eval_error(E_BAD_VALUE);
				error_token = t->ptr;
				free_nodes(np);
				return((node_t*)NULL);
			}
			if (np->value > 0xffffffff) {
				np->byte_size = 8;
			} else {
				np->byte_size = 4;
			}
			if (!strncmp(t->string, "0x", 2) ||
					!strncmp(t->string, "0X", 2)) {
				np->flags |= UNSIGNED_FLAG;
			}
			np->node_type = NUMBER;
		}
	} else if (t->type == CHARACTER) {
		char *cp;

		/* Step over the single quote
		 */
		cp = (t->ptr + 1);
		if (*cp == '\\') {
			int i = 0;
			char str[16];

			/* Step over the back slash
		 	 */
			cp++;
			while (*cp != '\'') {
				str[i++] = *cp++;
			}
			str[i] = 0;
			np->value = esc_char(str);
		} else {
			np->value = *cp;
		}
		np->type = get_type("char", flags);
		np->node_type = TYPE_DEF;
		np->flags |= KLTYPE_FLAG;
	} else if (t->type == TEXT) {
		np->node_type = TEXT;
		np->name = t->string;
		/* So the block doesn't get freed twice */
		t->string = (char*)NULL;
	} else {
		set_eval_error(E_SYNTAX_ERROR);
		error_token = t->ptr;
		return((node_t*)NULL);
	}
	np->tok_ptr = t->ptr;
	return(np);
}

/*
 * add_node()
 */
static int
add_node(node_t *root, node_t *new_node)
{
	node_t *n = root;

	/* Find the most lower-right node
	 */
	while (n->right) {
		n = n->right;
	}

	/* If the node we found is a leaf node, return an error (we will
	 * have to insert the node instead).
	 */
	if (n->node_type == NUMBER) {
		return(-1);
	} else {
		n->right = new_node;
	}
	return(0);
}

/*
 * add_rchild()
 */
static int
add_rchild(node_t *root, node_t *new_node)
{
	if (add_node(root, new_node) == -1) {
		return(-1);
	}
	return(0);
}

/*
 * free_type()
 */
static void
free_type(type_t *head)
{
	type_t *t0, *t1;

	t0 = head;
	while(t0) {
		if (t0->flag == POINTER_FLAG) {
			t1 = t0->t_next;
			kl_free_block((void *)t0);
			t0 = t1;
		} else {
			if (t0->flag != KLTYPE_FLAG) {
				kl_free_block((void *)t0->t_kltp);
			}
			kl_free_block((void *)t0);
			t0 = (type_t *)NULL;
		}
	}
	return;
}

/*
 * get_type() -- Convert a typecast string into a type.
 *
 *   Returns a pointer to a struct containing type information.
 *   The type of struct returned is indicated by the contents
 *   of type. If the typecast contains an asterisk, set ptr_type
 *   equal to one, otherwise set it equal to zero.
 */
static type_t *
get_type(char *s, int flags)
{
	int len, type = 0;
	char *cp, typename[128];
	type_t *t, *head, *last;
	kltype_t *kltp;

	head = last = (type_t *)NULL;

	/* Get the type string
	 */
	if (!strncmp(s, "struct", 6)) {
		if ((cp = strpbrk(s + 7, " \t*"))) {
			len = cp - (s + 7);
		} else {
			len = strlen(s + 7);
		}
		memcpy(typename, s + 7, len);
	} else if (!strncmp(s, "union", 5)) {
		if ((cp = strpbrk(s + 6, " \t*"))) {
			len = cp - (s + 6);
		} else {
			len = strlen(s + 6);
		}
		memcpy(typename, s + 6, len);
	} else {
		if ((cp = strpbrk(s, "*)"))) {
			len = cp - s;
		} else {
			len = strlen(s);
		}
		memcpy(typename, s, len);
	}

	/* Strip off any trailing spaces
	 */
	while (len && ((typename[len - 1] == ' ') ||
			(typename[len - 1] == '\t'))) {
		len--;
	}
	typename[len] = 0;

	if (!(kltp = kl_find_type(typename, KLT_TYPES))) {
		return ((type_t *)NULL);
	}
	type = KLTYPE_FLAG;

	/* check to see if this cast is a pointer to a type, a pointer
	 * to a pointer to a type, etc.
	 */
	cp = s;
	while ((cp = strpbrk(cp, "*"))) {
		t = (type_t *)kl_alloc_block(sizeof(type_t));
		t->flag = POINTER_FLAG;
		if (last) {
			last->t_next = t;
			last = t;
		} else {
			head = last = t;
		}
		cp++;
	}

	/* Allocate a type block that will point to the type specific
	 * record.
	 */
	t = (type_t *)kl_alloc_block(sizeof(type_t));
	t->flag = type;

	switch (t->flag) {

		case KLTYPE_FLAG:
			t->t_kltp = kltp;
			break;

		default:
			free_type(head);
			return((type_t*)NULL);
	}
	if (last) {
		last->t_next = t;
	} else {
		head = t;
	}
	return(head);
}

/*
 * free_node()
 */
static void
free_node(node_t *np)
{
	/* If there is nothing to free, just return.
	 */
	if (!np) {
		return;
	}
	if (np->name) {
		kl_free_block((void *)np->name);
	}
	free_type(np->type);
	kl_free_block((void *)np);
}

/*
 * free_nodes()
 */
void
free_nodes(node_t *np)
{
	node_t *q;

	/* If there is nothing to free, just return.
	 */
	if (!np) {
		return;
	}
	if ((q = np->left)) {
		free_nodes(q);
	}
	if ((q = np->right)) {
		free_nodes(q);
	}
	if (np->name) {
		kl_free_block((void *)np->name);
	}
	free_type(np->type);
	kl_free_block((void *)np);
}

/*
 * free_nodelist()
 */
static void
free_nodelist(node_t *np)
{
	node_t *nnp;

	while(np) {
		nnp = np->next;
		free_node(np);
		np = nnp;
	}
}

extern int alloc_debug;

/*
 * free_eval_memory()
 */
void
free_eval_memory(void)
{
	free_nodelist(node_list);
	node_list = (node_t*)NULL;
}

/*
 * get_sizeof()
 */
static node_t *
get_sizeof()
{
	node_t *curnp, *n0 = NULL;

	if (!(curnp = next_node())) {
		set_eval_error(E_SYNTAX_ERROR);
		return((node_t*)NULL);
	}

	/* The next token should be a CAST or an open paren.
 	 * If it's something else, then return an error.
	 */
	if (curnp->operator == OPEN_PAREN) {
		free_nodes(curnp);
		n0 = do_eval(C_SIZEOF);
		if (eval_error) {
			error_token = n0->tok_ptr;
			free_nodes(n0);
			return((node_t*)NULL);
		}
	} else if (curnp->operator == CAST) {
		n0 = curnp;
	} else {
		set_eval_error(E_BAD_TYPE);
		error_token = n0->tok_ptr;
		free_nodes(n0);
		return((node_t*)NULL);
	}

	if (!n0->type) {
		set_eval_error(E_NOTYPE);
		error_token = n0->tok_ptr;
		free_nodes(n0);
		return((node_t*)NULL);
	}

	if (n0->type->flag & POINTER_FLAG) {
		n0->value = sizeof(void *);
	} else if (n0->type->flag & KLTYPE_FLAG) {
		kltype_t *kltp;

		kltp = kl_realtype(n0->type->t_kltp, 0);

		if (kltp->kl_bit_size) {
			n0->value = kltp->kl_bit_size / 8;
			if (kltp->kl_bit_size % 8) {
				n0->value += 1;
			}
		} else {
			n0->value = kltp->kl_size;
		}
	} else {
		set_eval_error(E_BAD_TYPE);
		error_token = n0->tok_ptr;
		free_nodes(n0);
		return((node_t*)NULL);
	}
	n0->node_type = NUMBER;
	n0->flags = 0;
	n0->operator = 0;
	n0->byte_size = 0;
	n0->address = 0;
	if (n0->type) {
		free_type(n0->type);
		n0->type = 0;
	}
	return(n0);
}

/*
 * apply_unary()
 */
static int
apply_unary(node_t *n, uint64_t *value)
{
	if (!n || !n->right) {
		return(-1);
	}

	switch (n->operator) {

		case UNARY_MINUS :
			*value = (0 - n->right->value);
			break;

		case UNARY_PLUS :
			*value = (n->right->value);
			break;

		case ONES_COMPLEMENT :
			*value = ~(n->right->value);
			break;

		case LOGICAL_NEGATION :
			if (n->right->value) {
				*value = 0;
			} else {
				*value = 1;
			}
			logical_flag++;
			break;

		default :
			break;
	}
	return(0);
}

/*
 * pointer_math()
 */
static int
pointer_math(node_t *np, uint64_t *value, int type, int flags)
{
	int size;
	uint64_t lvalue, rvalue;
	type_t *tp = NULL, *tp1;

	if (type < 0) {
		if (np->left->flags & POINTER_FLAG) {

			/* Since we only allow pointer math,
			 * anything other than a pointer causes
			 * failure.
			 */
			tp = (type_t*)np->left->type;
			if (tp->flag != POINTER_FLAG) {
				set_eval_error(E_SYNTAX_ERROR);
				error_token = np->left->tok_ptr;
				return(-1);
			}

			tp = tp->t_next;

			switch (tp->flag) {

				case POINTER_FLAG :
					size = sizeof(void *);
					break;

				case KLTYPE_FLAG : {
					/* Get the size of the real type,
					 * not just the size of a pointer
					 * If there isn't any type info,
					 * then just set size equal to the
					 * size of a pointer.
					 */
					kltype_t *kltp, *rkltp;

					kltp = tp->t_kltp;
					rkltp = kl_realtype(kltp, 0);
					if (!(size = rkltp->kl_size)) {
						if (kltp != rkltp) {
							size = kltp->kl_size;
						} else {
							size = sizeof(void *);
						}
					}
					break;
				}

				default :
					set_eval_error(E_SYNTAX_ERROR);
					error_token = np->left->tok_ptr;
					return(-1);
			}
			lvalue = np->left->value;
		} else {
			size = sizeof(void *);
			lvalue = np->left->address;
		}
		switch (np->operator) {
			case ADD :
				*value = lvalue + (np->right->value * size);
				break;

			case SUBTRACT :
				*value = lvalue - (np->right->value * size);
				break;

			default :
				set_eval_error(E_BAD_OPERATOR);
				error_token = np->tok_ptr;
				return(-1);
		}
	} else if (type > 0) {
		if (np->right->flags & POINTER_FLAG) {

			/* Since we only allow pointer math,
			 * anything other than a pointer causes
			 * failure.
			 */
			tp = (type_t*)np->right->type;
			if (tp->flag != POINTER_FLAG) {
				set_eval_error(E_SYNTAX_ERROR);
				error_token = np->right->tok_ptr;
				return(-1);
			}

			tp = tp->t_next;

			switch (tp->flag) {

				case POINTER_FLAG :
					size = sizeof(void *);
					break;

				case KLTYPE_FLAG :
					size = tp->t_kltp->kl_size;
					break;

				default :
					set_eval_error(E_SYNTAX_ERROR);
					error_token = np->right->tok_ptr;
					return(-1);
			}
			rvalue = np->right->value;
		} else {
			size = sizeof(void *);
			rvalue = np->right->address;
		}
		switch (np->operator) {
			case ADD :
				*value = rvalue + (np->left->value * size);
				break;

			case SUBTRACT :
				*value = rvalue - (np->left->value * size);
				break;

			default :
				set_eval_error(E_BAD_OPERATOR);
				error_token = np->tok_ptr;
				return(-1);
		}
	} else {
		return(-1);
	}
	tp1 = (type_t *)kl_alloc_block(sizeof(type_t));
	tp1->flag = POINTER_FLAG;
	np->type = tp1;
	while (tp->flag == POINTER_FLAG) {
		tp1->t_next = (type_t *)kl_alloc_block(sizeof(type_t));
		tp1->flag = POINTER_FLAG;
		tp1 = tp1->t_next;
		tp = tp->t_next;
	}
	if (tp) {
		tp1->t_next = (type_t *)kl_alloc_block(sizeof(type_t));
		tp1 = tp1->t_next;
		tp1->flag = KLTYPE_FLAG;
		tp1->t_kltp = tp->t_kltp;
		if (type < 0) {
			if (np->left->flags & POINTER_FLAG) {
				np->flags |= POINTER_FLAG;
			} else {
				np->flags |= VADDR;
			}
		} else {
			if (np->right->flags & POINTER_FLAG) {
				np->flags |= POINTER_FLAG;
			} else {
				np->flags |= VADDR;
			}
		}
	}
	return(0);
}

/*
 * check_unsigned()
 */
int
check_unsigned(node_t *np)
{
	kltype_t *kltp, *rkltp;

	if (np->flags & UNSIGNED_FLAG) {
		return(1);
	}
	if (!np->type) {
		return(0);
	}
	if (np->type->flag == POINTER_FLAG) {
		return(0);
	}
	kltp = np->type->t_kltp;
	if ((rkltp = kl_realtype(kltp, 0))) {
		if (rkltp->kl_encoding == ENC_UNSIGNED) {
			np->flags |= UNSIGNED_FLAG;
			return(1);
		}
	}
	return(0);
}

/*
 * apply()
 */
static int
apply(node_t *np, uint64_t *value, int flags)
{
	int ltype, rtype, do_signed = 0;

	/* There must be two operands
	 */
	if (!np->right || !np->left) {
		set_eval_error(E_MISSING_OPERAND);
		error_token = np->tok_ptr;
		return(-1);
	}

	if (np->right->node_type == OPERATOR) {
		replace(np->right, flags);
		if (eval_error) {
			return(-1);
		}
	}

	ltype = np->left->node_type;
	rtype = np->right->node_type;
	if ((ltype == TYPE_DEF) || (ltype == VADDR)) {
		if ((rtype == TYPE_DEF) || (rtype == VADDR)) {
			set_eval_error(E_NO_VALUE);
			error_token = np->tok_ptr;
			return(-1);
		}
		if (check_unsigned(np->left)) {
			np->flags |= UNSIGNED_FLAG;
		} else {
			do_signed++;
		}
		if (!type_to_number(np->left)) {
			return(pointer_math(np, value, -1, flags));
		}
		np->byte_size = np->left->byte_size;
	} else if ((rtype == TYPE_DEF) || (rtype == VADDR)) {
		if ((ltype == TYPE_DEF) || (ltype == VADDR)) {
			error_token = np->tok_ptr;
			set_eval_error(E_NO_VALUE);
			return(-1);
		}
		if (check_unsigned(np->right)) {
			np->flags |= UNSIGNED_FLAG;
		} else {
			do_signed++;
		}
		if (!type_to_number(np->right)) {
			return(pointer_math(np, value, 1, flags));
		}
		np->byte_size = np->right->byte_size;
	} else if ((np->left->flags & UNSIGNED_FLAG) ||
			(np->right->flags & UNSIGNED_FLAG)) {
		np->flags |= UNSIGNED_FLAG;
	} else {
		do_signed++;
	}

	if (do_signed) {
		switch (np->operator) {
			case ADD :
				*value = (int64_t)np->left->value +
					(int64_t)np->right->value;
				break;

			case SUBTRACT :
				*value = (int64_t)np->left->value -
					(int64_t)np->right->value;
				break;

			case MULTIPLY :
				*value = (int64_t)np->left->value *
					(int64_t)np->right->value;
				break;

			case DIVIDE :
				if ((int64_t)np->right->value == 0) {
					set_eval_error(E_DIVIDE_BY_ZERO);
					error_token = np->right->tok_ptr;
					return(-1);
				}
				*value = (int64_t)np->left->value /
					(int64_t)np->right->value;
				break;

			case BITWISE_OR :
				*value = (int64_t)np->left->value |
					(int64_t)np->right->value;
				break;

			case BITWISE_AND :
				*value = (int64_t)np->left->value &
					(int64_t)np->right->value;
				break;

			case MODULUS :
				if ((int64_t)np->right->value == 0) {
					set_eval_error(E_DIVIDE_BY_ZERO);
					error_token = np->right->tok_ptr;
					return(-1);
				}
				*value = (int64_t)np->left->value %
					(int64_t)np->right->value;
				break;

			case RIGHT_SHIFT :
				*value =
					(int64_t)np->left->value >>
						(int64_t)np->right->value;
				break;

			case LEFT_SHIFT :
				*value =
					(int64_t)np->left->value <<
						(int64_t)np->right->value;
				break;

			case LOGICAL_OR :
				if ((int64_t)np->left->value ||
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LOGICAL_AND :
				if ((int64_t)np->left->value &&
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case EQUAL :
				if ((int64_t)np->left->value ==
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case NOT_EQUAL :
				if ((int64_t)np->left->value !=
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LESS_THAN :
				if ((int64_t)np->left->value <
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case GREATER_THAN :
				if ((int64_t)np->left->value >
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LESS_THAN_OR_EQUAL :
				if ((int64_t)np->left->value <=
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case GREATER_THAN_OR_EQUAL :
				if ((int64_t)np->left->value >=
						(int64_t)np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			default :
				break;
		}
	} else {
		switch (np->operator) {
			case ADD :
				*value = np->left->value + np->right->value;
				break;

			case SUBTRACT :
				*value = np->left->value - np->right->value;
				break;

			case MULTIPLY :
				*value = np->left->value * np->right->value;
				break;

			case DIVIDE :
				*value = np->left->value / np->right->value;
				break;

			case BITWISE_OR :
				*value = np->left->value | np->right->value;
				break;

			case BITWISE_AND :
				*value = np->left->value & np->right->value;
				break;

			case MODULUS :
				*value = np->left->value % np->right->value;
				break;

			case RIGHT_SHIFT :
				*value = np->left->value >> np->right->value;
				break;

			case LEFT_SHIFT :
				*value = np->left->value << np->right->value;
				break;

			case LOGICAL_OR :
				if (np->left->value || np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LOGICAL_AND :
				if (np->left->value && np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case EQUAL :
				if (np->left->value == np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case NOT_EQUAL :
				if (np->left->value != np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LESS_THAN :
				if (np->left->value < np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case GREATER_THAN :
				if (np->left->value > np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case LESS_THAN_OR_EQUAL :
				if (np->left->value <= np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			case GREATER_THAN_OR_EQUAL :
				if (np->left->value >= np->right->value) {
					*value = 1;
				} else {
					*value = 0;
				}
				logical_flag++;
				break;

			default :
				break;
		}
	}
	return(0);
}

/*
 * member_to_type()
 */
static type_t *
member_to_type(kltype_t *kltp, int flags)
{
	kltype_t *rkltp;
	type_t *tp, *head = (type_t *)NULL, *last = (type_t *)NULL;

	/* Make sure this is a member
	 */
	if (kltp->kl_type != KLT_MEMBER) {
		return((type_t *)NULL);
	}

	rkltp = kltp->kl_realtype;
	while (rkltp && rkltp->kl_type == KLT_POINTER) {
		tp = (type_t *)kl_alloc_block(sizeof(type_t));
		tp->flag = POINTER_FLAG;
		if (last) {
			last->t_next = tp;
			last = tp;
		} else {
			head = last = tp;
		}
		rkltp = rkltp->kl_realtype;
	}

	/* If We step past all the pointer records and don't point
	 * at anything, this must be a void pointer. Setup a VOID
	 * type struct so that we can maintain a pointer to some
	 * type info.
	 */
	if (!rkltp) {
		tp = (type_t *)kl_alloc_block(sizeof(type_t));
		tp->flag = VOID_FLAG;
		tp->t_kltp = kltp;
		if (last) {
			last->t_next = tp;
			last = tp;
		} else {
			head = last = tp;
		}
		return(head);
	}

	tp = (type_t *)kl_alloc_block(sizeof(type_t));
	tp->flag = KLTYPE_FLAG;
	tp->t_kltp = kltp;
	if (last) {
		last->t_next = tp;
	} else {
		head = tp;
	}
	return(head);
}

/*
 * replace() --
 *
 * Replace the tree with a node containing the numerical result of
 * the equation. If pointer math is performed, the result will have
 * the same type as the pointer.
 */
static node_t *
replace(node_t *np, int flags)
{
	int offset;
	uint64_t value;
	node_t *q;

	if (!np) {
		return((node_t *)NULL);
	}

	if (np->node_type == OPERATOR) {
		if (!(q = np->left)) {
			return((node_t *)NULL);
		}
		while (q) {
			if (!replace(q, flags)) {
				return((node_t *)NULL);
			}
			q = q->right;
		}

		if ((np->operator == RIGHT_ARROW) || (np->operator == DOT)) {
			kaddr_t addr = 0;
			type_t *tp;

			if (!have_debug_file) {
				kdb_printf("no debuginfo file\n");
				return 0;
			}

			/* The left node must point to a TYPE_DEF
			 */
			if (np->left->node_type != TYPE_DEF) {
				if (np->left->flags & NOTYPE_FLAG) {
					set_eval_error(E_NOTYPE);
					error_token = np->left->tok_ptr;
				} else {
					set_eval_error(E_BAD_TYPE);
					error_token = np->left->tok_ptr;
				}
				return((node_t *)NULL);
			}

			/* Get the type information.  Check to see if we
			 * have a pointer to a type. If we do, we need
			 * to strip off the pointer and get the type info.
			 */
			if (np->left->type->flag == POINTER_FLAG) {
				tp = np->left->type->t_next;
				kl_free_block((void *)np->left->type);
			} else {
				tp = np->left->type;
			}

			/* We need to zero out the left child's type pointer
			 * to prevent the type structs from being prematurely
			 * freed (upon success). We have to remember, however,
			 * to the free the type information before we return.
			 */
			np->left->type = (type_t*)NULL;

			/* tp should now point at a type_t struct that
			 * references a kltype_t struct. If it points
			 * to anything else, return failure.
			 *
			 */
			if (tp->flag != KLTYPE_FLAG) {
				set_eval_error(E_BAD_TYPE);
				error_token = np->left->tok_ptr;
				free_type(tp);
				return((node_t *)NULL);
			}

			switch (tp->flag) {
				case KLTYPE_FLAG: {
					/* Make sure that the type referenced
					 * is a struct, union, or pointer to
					 * a struct or union. If it isn't one
					 * of these, then return failure.
					 */
					kltype_t *kltp, *kltmp;

					kltp = kl_realtype(tp->t_kltp, 0);
					if ((kltp->kl_type != KLT_STRUCT) &&
						(kltp->kl_type != KLT_UNION)) {
						error_token =
							np->left->tok_ptr;
						set_eval_error(E_BAD_TYPE);
						free_type(tp);
						return((node_t *)NULL);
					}

					/* Get type information for member.
					 * If member is a pointer to a type,
					 * get the pointer address and load
					 * it into value. In any event, load
					 * the struct/union address plus the
					 * offset of the member.
					 */
					kltmp = kl_get_member(kltp,
							np->right->name);
					if (!kltmp) {
						set_eval_error(E_BAD_MEMBER);
						error_token =
							np->right->tok_ptr;
						free_type(tp);
						return((node_t *)NULL);
					}

					/* We can't just use the offset value
					 * for the member. That's because it
					 * may be from an anonymous struct or
					 * union within another struct
					 * definition.
					 */
					offset = kl_get_member_offset(kltp,
						np->right->name);
					np->type = member_to_type(kltmp, flags);
					if (!np->type) {
						set_eval_error(E_BAD_MEMBER);
						error_token =
							np->right->tok_ptr;
						free_type(tp);
						return((node_t *)NULL);
					}

					/* Now free the struct type information
					 */
					free_type(tp);
					np->node_type = TYPE_DEF;
					np->flags |= KLTYPE_FLAG;
					np->operator = 0;
					addr = 0;
					if (np->left->flags & POINTER_FLAG) {
						addr =  np->left->value +
							offset;
					} else if (np->left->flags &
							ADDRESS_FLAG) {
						addr =  np->left->address +
							offset;
					}
					if (addr) {
						np->address = addr;
						np->flags |= ADDRESS_FLAG;
					}

					if (np->type->flag == POINTER_FLAG) {
						np->flags |= POINTER_FLAG;
						np->value = *((kaddr_t *)addr);
					} else {
						np->value = addr;
					}
					break;
				}
			}
			free_nodes(np->left);
			free_nodes(np->right);
			np->left = np->right = (node_t*)NULL;
			return(np);
		} else {
			if (!np->left || !np->right) {
				set_eval_error(E_MISSING_OPERAND);
				error_token = np->tok_ptr;
				return((node_t *)NULL);
			}
			if (np->left->byte_size && np->right->byte_size) {
				if (np->left->byte_size >
						np->right->byte_size) {

					/* Left byte_size is greater than right
					 */
					np->byte_size = np->left->byte_size;
					np->type = np->left->type;
					np->flags = np->left->flags;
					free_type(np->right->type);
				} else if (np->left->byte_size <
						np->right->byte_size) {

					/* Right byte_size is greater than left
					 */
					np->byte_size = np->right->byte_size;
					np->type = np->right->type;
					np->flags = np->right->flags;
					free_type(np->left->type);
				} else {

					/* Left and right byte_size is equal
					 */
					if (np->left->flags & UNSIGNED_FLAG) {
						np->byte_size =
							np->left->byte_size;
						np->type = np->left->type;
						np->flags = np->left->flags;
						free_type(np->right->type);
					} else if (np->right->flags &
							UNSIGNED_FLAG) {
						np->byte_size =
							np->right->byte_size;
						np->type = np->right->type;
						np->flags = np->right->flags;
						free_type(np->left->type);
					} else {
						np->byte_size =
							np->left->byte_size;
						np->type = np->left->type;
						np->flags = np->left->flags;
						free_type(np->right->type);
					}
				}
			} else if (np->left->byte_size) {
				np->byte_size = np->left->byte_size;
				np->type = np->left->type;
				np->flags = np->left->flags;
				free_type(np->right->type);
			} else if (np->right->byte_size) {
				np->byte_size = np->right->byte_size;
				np->type = np->right->type;
				np->flags = np->right->flags;
			} else {
				/* XXX - No byte sizes
				 */
			}

			if (apply(np, &value, flags)) {
				return((node_t *)NULL);
			}
		}
		np->right->type = np->left->type = (type_t*)NULL;

		/* Flesh out the rest of the node struct.
		 */
		if (np->type) {
			np->node_type = TYPE_DEF;
			np->flags |= KLTYPE_FLAG;
		} else {
			np->node_type = NUMBER;
			np->flags &= ~(KLTYPE_FLAG);
		}
		np->operator = 0;
		np->value = value;
		kl_free_block((void *)np->left);
		kl_free_block((void *)np->right);
		np->left = np->right = (node_t*)NULL;
	}
	return(np);
}

/*
 * replace_cast()
 */
static int
replace_cast(node_t *n, int flags)
{
	type_t *t;

	if (!n) {
		set_eval_error(E_SYNTAX_ERROR);
		return(-1);
	} else if (!n->right) {
		set_eval_error(E_SYNTAX_ERROR);
		error_token = n->tok_ptr;
		return(-1);
	}
	if (n->flags & POINTER_FLAG) {
		if (n->right->node_type == VADDR) {
			if (n->right->flags & ADDRESS_FLAG) {
				n->value = n->right->address;
			} else {
				set_eval_error(E_SYNTAX_ERROR);
				error_token = n->right->tok_ptr;
				return(-1);
			}

		} else {
			n->value = n->right->value;
			n->address = 0;
		}
	} else if (n->right->flags & ADDRESS_FLAG) {
		n->flags |= ADDRESS_FLAG;
		n->address = n->right->address;
		n->value = n->right->value;
	} else {
		kltype_t *kltp;

		if (!(t = eval_type(n))) {
			set_eval_error(E_BAD_TYPE);
			error_token = n->tok_ptr;
			return(-1);
		}
		if (t->t_kltp->kl_type != KLT_BASE) {

			kltp = kl_realtype(t->t_kltp, 0);
			if (kltp->kl_type != KLT_BASE) {
				set_eval_error(E_BAD_CAST);
				error_token = n->tok_ptr;
				return(-1);
			}
		}
		n->value = n->right->value;
		n->type = t;
	}
	n->node_type = TYPE_DEF;
	n->operator = 0;
	free_node(n->right);
	n->right = (node_t *)NULL;
	return(0);
}

/*
 * replace_indirection()
 */
static int
replace_indirection(node_t *n, int flags)
{
	kaddr_t addr;
	type_t *t, *tp, *rtp;

	/* Make sure there is a right child and that it is a TYPE_DEF.
	 */
	if (!n->right) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->tok_ptr;
		return(-1);
	} else if (n->right->node_type != TYPE_DEF) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	/* Make sure the right node contains a pointer or address value.
	 * Note that it's possible for the whatis command to generate
	 * this case without any actual pointer/address value.
	 */
	if (!(n->right->flags & (POINTER_FLAG|ADDRESS_FLAG))) {
		set_eval_error(E_BAD_POINTER);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	/* Get the pointer to the first type struct and make sure
	 * it's a pointer.
	 */
	if (!(tp = n->right->type) || (tp->flag != POINTER_FLAG)) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	/* Make sure we have a pointer to a type structure.
	 */
	if (!(n->right->flags & KLTYPE_FLAG)) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	n->node_type = TYPE_DEF;
	n->flags = KLTYPE_FLAG;
	n->operator = 0;

	if (!(t = tp->t_next)) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	if (!(rtp = eval_type(n->right))) {
		set_eval_error(E_BAD_TYPE);
		error_token = n->right->tok_ptr;
		return(-1);
	}

	/* Zero out the type field in the right child so
	 * it wont accidently be freed when the right child
	 * is freed (upon success).
	 */
	n->right->type = (type_t*)NULL;

	n->type = t;

	/* Free the pointer struct
	 */
	kl_free_block((void *)tp);

	/* Get the pointer address
	 */
	addr = n->address = n->right->value;
	n->flags |= ADDRESS_FLAG;

	if (rtp->t_kltp->kl_type == KLT_MEMBER) {
		/* If this is a member, we have to step over the KLT_MEMBER
		 * struct and then make sure we have a KLT_POINTER struct.
		 * If we do, we step over it too...otherwise return an
		 * error.
		 */
		if (rtp->t_kltp->kl_realtype->kl_type != KLT_POINTER) {
			set_eval_error(E_BAD_TYPE);
			error_token = n->right->tok_ptr;
			return(-1);
		}
		rtp->t_kltp = rtp->t_kltp->kl_realtype;
	}

	if (rtp->t_kltp->kl_type == KLT_POINTER) {
		/* Strip off the pointer type record so that
		 * we pick up the actual type definition with
		 * our indirection.
		 */
		rtp->t_kltp = rtp->t_kltp->kl_realtype;
		if (rtp->t_kltp->kl_name &&
				!strcmp(rtp->t_kltp->kl_name, "char")) {
			n->flags |= STRING_FLAG;
		}
	}


	/* If this is a pointer to a pointer, get the next
	 * pointer value.
	 */
	if (n->type->flag == POINTER_FLAG) {
		n->value = *((kaddr_t *)addr);

		/* Set the appropriate node flag values
		 */
		n->flags |= POINTER_FLAG;
		free_node(n->right);
		n->left = n->right = (node_t *)NULL;
		return(0);
	}
	/* Zero out the type field in the right child so it doesn't
	 * accidently get freed up when the right child is freed
	 * (upon success).
	 */
	n->right->type = (type_t*)NULL;
	free_node(n->right);
	n->left = n->right = (node_t *)NULL;
	return(0);
}

/*
 * replace_unary()
 *
 * Convert a unary operator node that contains a pointer to a value
 * with a node containing the numerical result. Free the node that
 * originally contained the value.
 */
static int
replace_unary(node_t *n, int flags)
{
	uint64_t value;

	if (!n->right) {
		set_eval_error(E_MISSING_OPERAND);
		error_token = n->tok_ptr;
		return(-1);
	}
	if (is_unary(n->right->operator)) {
		if (replace_unary(n->right, flags) == -1) {
			return(-1);
		}
	}
	if (n->operator == CAST) {
		return(replace_cast(n, flags));
	} else if (n->operator == INDIRECTION) {
		return(replace_indirection(n, flags));
	} else if (n->operator == ADDRESS) {
		type_t *t;

		if (n->right->node_type == TYPE_DEF) {
			if (!(n->right->flags & ADDRESS_FLAG)) {
				set_eval_error(E_NO_ADDRESS);
				error_token = n->right->tok_ptr;
				return(-1);
			}
			t = n->right->type;
		} else {
			set_eval_error(E_BAD_TYPE);
			error_token = n->right->tok_ptr;
			return(-1);
		}
		n->type = (type_t*)kl_alloc_block(sizeof(type_t));
		n->type->flag = POINTER_FLAG;
		n->type->t_next = t;
		n->node_type = TYPE_DEF;
		n->operator = 0;
		n->value = n->right->address;
		n->flags = POINTER_FLAG;
		if (!(t = eval_type(n))) {
			set_eval_error(E_BAD_TYPE);
			error_token = n->tok_ptr;
			return(-1);
		}
		n->flags |= t->flag;
		n->right->type = 0;
		free_nodes(n->right);
		n->left = n->right = (node_t *)NULL;
		return(0);
	} else if (apply_unary(n, &value) == -1) {
		return(-1);
	}
	free_nodes(n->right);
	n->node_type = NUMBER;
	n->operator = 0;
	n->left = n->right = (node_t *)NULL;
	memcpy(&n->value, &value, sizeof(uint64_t));
	return(0);
}

/*
 * pointer_to_element()
 */
static void
pointer_to_element(node_t *n0, node_t *n1)
{
	int size;
	kltype_t *kltp, *rkltp;
	type_t *tp;

	if (!(tp = n0->type)) {
		set_eval_error(E_BAD_INDEX);
		error_token = n0->tok_ptr;
		return;
	}
	if (tp->t_next->flag == POINTER_FLAG) {
		size = sizeof(void *);
	} else {
		kltp = tp->t_next->t_kltp;
		if (!(rkltp = kl_realtype(kltp, 0))) {
			set_eval_error(E_BAD_INDEX);
			error_token = n0->tok_ptr;
			return;
		}
		size = rkltp->kl_size;
	}

	/* Get the details on the array element
	 */
	n0->flags |= ADDRESS_FLAG;
	n0->address = n0->value + (n1->value * size);
	n0->type = tp->t_next;
	kl_free_block((char *)tp);
	if (tp->t_next->flag == POINTER_FLAG) {
		n0->flags |= POINTER_FLAG;
		n0->value = *((kaddr_t *)n0->address);
	} else {
		n0->flags &= (~POINTER_FLAG);
		n0->value = 0;
	}
}

/*
 * array_to_element()
 */
static void
array_to_element(node_t *n0, node_t *n1)
{
	kltype_t *kltp, *rkltp, *ip, *ep;
	type_t *tp, *troot = (type_t *)NULL;

	if (!(tp = n0->type)) {
		set_eval_error(E_BAD_INDEX);
		error_token = n0->tok_ptr;
		return;
	}

	/* If we are indexing a pointer, then make a call to the
	 * pointer_to_element() and return.
	 */
	if (tp->flag == POINTER_FLAG) {
		return(pointer_to_element(n0, n1));
	}

	if (!(kltp = n0->type->t_kltp)) {
		set_eval_error(E_BAD_INDEX);
		error_token = n0->tok_ptr;
		return;
	}
	if (!(rkltp = kl_realtype(kltp, KLT_ARRAY))) {
		set_eval_error(E_BAD_INDEX);
		error_token = n0->tok_ptr;
		return;
	}
	ip = rkltp->kl_indextype;
	ep = rkltp->kl_elementtype;
	if (!ip || !ep) {
		set_eval_error(E_BAD_INDEX);
		error_token = n1->tok_ptr;
		return;
	}
	/* Get the details on the array element
	 */
	n0->address = n0->address + (n1->value * ep->kl_size);
	if (ep->kl_type == KLT_POINTER) {
		n0->flags |= POINTER_FLAG;
		n0->value = *((kaddr_t *)n0->address);
	} else {
		n0->value = 0;
	}
	n0->flags |= ADDRESS_FLAG;
	kltp = ep;
	while (kltp->kl_type == KLT_POINTER) {
		if (troot) {
			tp->t_next = (type_t*)kl_alloc_block(sizeof(type_t));
			tp = tp->t_next;
		} else {
			tp = (type_t*)kl_alloc_block(sizeof(type_t));
			troot = tp;
		}
		tp->flag = POINTER_FLAG;
		kltp = kltp->kl_realtype;
	}
	if (troot) {
		tp->t_next = (type_t*)kl_alloc_block(sizeof(type_t));
		tp = tp->t_next;
		n0->type = troot;
	} else {
		tp = (type_t*)kl_alloc_block(sizeof(type_t));
		n0->type = tp;
	}
	tp->flag = KLTYPE_FLAG;
	tp->t_kltp = ep;
}

/*
 * number_to_size()
 */
int
number_to_size(node_t *np)
{
	int unsigned_flag = 0;

	if (np->node_type != NUMBER) {
		set_eval_error(E_BAD_TYPE);
		error_token = np->tok_ptr;
		return(0);
	}
	if (np->flags & UNSIGNED_FLAG) {
		unsigned_flag = 1;
	}
	if ((np->value >= 0) && (np->value <= 0xffffffff)) {
		return(4);
	} else if (((np->value >> 32) & 0xffffffff) == 0xffffffff) {
		if (unsigned_flag) {
			return(8);
		} else if (sizeof(void *) == 4) {
			return(4);
		} else {
			return(8);
		}
	}
	return(8);
}

/*
 * number_to_type()
 */
kltype_t *
number_to_type(node_t *np)
{
	int unsigned_flag = 0;
	kltype_t *kltp, *rkltp = (kltype_t *)NULL;

	if (np->node_type != NUMBER) {
		set_eval_error(E_BAD_TYPE);
		error_token = np->tok_ptr;
		return((kltype_t *)NULL);
	}
	if (np->flags & UNSIGNED_FLAG) {
		unsigned_flag = 1;
	}
	if ((np->value >= 0) && (np->value <= 0xffffffff)) {
		if (unsigned_flag) {
			kltp = kl_find_type("uint32_t", KLT_TYPEDEF);
		} else {
			kltp = kl_find_type("int32_t", KLT_TYPEDEF);
		}
	} else if (((np->value >> 32) & 0xffffffff) == 0xffffffff) {
		if (unsigned_flag) {
			kltp = kl_find_type("uint64_t", KLT_TYPEDEF);
		} else if (sizeof(void *) == 4) {
			kltp = kl_find_type("int32_t", KLT_TYPEDEF);
		} else {
			kltp = kl_find_type("int64_t", KLT_TYPEDEF);
		}
	} else {
		if (unsigned_flag) {
			kltp = kl_find_type("uint64_t", KLT_TYPEDEF);
		} else {
			kltp = kl_find_type("int64_t", KLT_TYPEDEF);
		}
	}
	if (kltp) {
		if (!(rkltp = kl_realtype(kltp, 0))) {
			rkltp = kltp;
		}
	} else {
		set_eval_error(E_BAD_TYPE);
		error_token = np->tok_ptr;
	}
	return(rkltp);
}

/*
 * type_to_number()
 *
 * Convert a base type to a numeric value. Return 1 on successful
 * conversion, 0 if nothing was done.
 */
static int
type_to_number(node_t *np)
{
	int byte_size, bit_offset, bit_size, encoding;
	uint64_t value, value1;
	kltype_t *kltp, *rkltp;

	/* Sanity check...
	 */
	if (np->node_type != TYPE_DEF) {
		set_eval_error(E_NOTYPE);
		error_token = np->tok_ptr;
		return(0);
	}
	if (!np->type) {
		set_eval_error(E_NOTYPE);
		error_token = np->tok_ptr;
		return(0);
	}
	if (np->type->flag == POINTER_FLAG) {
		return(0);
	}

	/* Get the real type record and make sure that it is
	 * for a base type.
	 */
	kltp = np->type->t_kltp;
	rkltp = kl_realtype(kltp, 0);
	if (rkltp->kl_type != KLT_BASE) {
		set_eval_error(E_NOTYPE);
		error_token = np->tok_ptr;
		return(0);
	}

	byte_size = rkltp->kl_size;
	bit_offset = rkltp->kl_bit_offset;
	if (!(bit_size = rkltp->kl_bit_size)) {
		bit_size = byte_size * 8;
	}
	encoding = rkltp->kl_encoding;
	if (np->flags & ADDRESS_FLAG) {
		/* FIXME: untested */
		if (invalid_address(np->address, byte_size)) {
			kdb_printf("ILLEGAL ADDRESS (%lx)",
						(uaddr_t)np->address);
			return (0);
		}
		kl_get_block(np->address, byte_size,(void *)&value1,(void *)0);
	} else {
		value1 = np->value;
	}
	value = kl_get_bit_value(&value1, byte_size, bit_size, bit_offset);
	switch (byte_size) {

		case 1 :
			if (encoding == ENC_UNSIGNED) {
				np->value = (unsigned char)value;
				np->flags |= UNSIGNED_FLAG;
			} else if (encoding == ENC_SIGNED) {
				np->value = (signed char)value;
			} else {
				np->value = (char)value;
			}
			break;

		case 2 :
			if (encoding == ENC_UNSIGNED) {
				np->value = (uint16_t)value;
				np->flags |= UNSIGNED_FLAG;
			} else {
				np->value = (int16_t)value;
			}
			break;

		case 4 :
			if (encoding == ENC_UNSIGNED) {
				np->value = (uint32_t)value;
				np->flags |= UNSIGNED_FLAG;
			} else {
				np->value = (int32_t)value;
			}
			break;

		case 8 :
			if (encoding == ENC_UNSIGNED) {
				np->value = (uint64_t)value;
				np->flags |= UNSIGNED_FLAG;
			} else {
				np->value = (int64_t)value;
			}
			break;

		default :
			set_eval_error(E_BAD_TYPE);
			error_token = np->tok_ptr;
			return(0);
	}
	np->byte_size = byte_size;
	np->node_type = NUMBER;
	return(1);
}

/*
 * eval_type()
 */
static type_t *
eval_type(node_t *n)
{
	type_t *t;

	if (!(t = n->type)) {
		return((type_t*)NULL);
	}
	while (t->flag == POINTER_FLAG) {
		t = t->t_next;

		/* If for some reason, there is no type pointer (this shouldn't
		 * happen but...), we have to make sure that we don't try to
		 * reference a NULL pointer and get a SEGV. Return an error if
		 * 't' is NULL.
		 */
		 if (!t) {
			return((type_t*)NULL);
		 }
	}
	if (t->flag == KLTYPE_FLAG) {
		return (t);
	}
	return((type_t*)NULL);
}

/*
 * expand_variables()
 */
static char *
expand_variables(char *exp, int flags)
{
	return((char *)NULL);
}

/*
 * eval()
 */
node_t *
eval(char **exp, int flags)
{
	token_t *tok;
	node_t *n, *root;
	char *e, *s;

	eval_error = 0;
	logical_flag = 0;

	/* Make sure there is an expression to evaluate
	 */
	if (!(*exp)) {
		return ((node_t*)NULL);
	}

	/* Expand any variables that are in the expression string. If
	 * a new string is allocated by the expand_variables() function,
	 * we need to make sure the original expression string gets
	 * freed. In any event, point s at the current expression string
	 * so that it gets freed up when we are done.
	 */
	if ((e = expand_variables(*exp, 0))) {
		kl_free_block((void *)*exp);
		*exp = e;
	} else if (eval_error) {
		eval_error |= E_BAD_EVAR;
		error_token = *exp;
	}
	s = *exp;
	tok = get_token_list(s);
	if (eval_error) {
		return((node_t*)NULL);
	}

	/* Get the node_list and evaluate the expression.
	 */
	node_list = get_node_list(tok, flags);
	if (eval_error) {
		free_nodelist(node_list);
		node_list = (node_t*)NULL;
		free_tokens(tok);
		return((node_t*)NULL);
	}
	if (!(n = do_eval(flags))) {
		if (!eval_error) {
			set_eval_error(E_SYNTAX_ERROR);
			error_token = s + strlen(s) - 1;
		}
		free_nodes(n);
		free_tokens(tok);
		return((node_t*)NULL);
	}

	if (!(root = replace(n, flags))) {
		if (eval_error) {
			free_nodes(n);
			free_tokens(tok);
			return((node_t*)NULL);
		}
		root = n;
	}

	/* Check to see if the the result should
	 * be interpreted as 'true' or 'false'
	 */
	if (logical_flag && ((root->value == 0) || (root->value == 1))) {
		root->flags |= BOOLIAN_FLAG;
	}
	free_tokens(tok);
	return(root);
}

/*
 * print_number()
 */
void
print_number(node_t *np, int flags)
{
	int size;
	unsigned long long value;

	if ((size = number_to_size(np)) && (size != sizeof(uint64_t))) {
		value = np->value & (((uint64_t)1 << (uint64_t)(size*8))-1);
	} else {
		value = np->value;
	}
	if (flags & C_HEX) {
		kdb_printf("0x%llx", value);
	} else if (flags & C_BINARY) {
		kdb_printf("0b");
		kl_binary_print(value);
	} else {
		if (np->flags & UNSIGNED_FLAG) {
			kdb_printf("%llu", value);
		} else {
			kdb_printf("%lld", np->value);
		}
	}
}

/*
 * print_string()
 */
void
print_string(kaddr_t addr, int size)
{
	int i;
	char *str;

	if (!size) {
		size = 255;
	}
	/* FIXME: untested */
	if (invalid_address(addr, size)) {
		klib_error = KLE_INVALID_PADDR;
		return;
	}
	str = (char*)kl_alloc_block(size);
	kl_get_block(addr, size, (void *)str, (void *)0);
	kdb_printf("\"%s", str);
	for (i = 0; i < size; i++) {
		if (!str[i]) {
			break;
		}
	}
	if (KL_ERROR || (i == size)) {
		kdb_printf("...");
	}
	kdb_printf("\"");
	kl_free_block(str);
}

/*
 * kl_print_error()
 */
void
kl_print_error(void)
{
	int ecode;

	ecode = klib_error & 0xffffffff;
	switch(ecode) {

		/** General klib error codes
		 **/
		case KLE_NO_MEMORY:
			kdb_printf("insufficient memory");
			break;
		case KLE_OPEN_ERROR:
			kdb_printf("unable to open file");
			break;
		case KLE_ZERO_BLOCK:
			kdb_printf("tried to allocate a zero-sized block");
			break;
		case KLE_INVALID_VALUE:
			kdb_printf("invalid input value");
			break;
		case KLE_NULL_BUFF:
			kdb_printf( "NULL buffer pointer");
			break;
		case KLE_ZERO_SIZE:
			kdb_printf("zero sized block requested");
			break;
		case KLE_ACTIVE:
			kdb_printf("operation not supported on a live system");
			break;
		case KLE_UNSUPPORTED_ARCH:
			kdb_printf("unsupported architecture");
			break;
		case KLE_MISC_ERROR:
			kdb_printf("KLIB error");
			break;
		case KLE_NOT_SUPPORTED:
			kdb_printf("operation not supported");
			break;
		case KLE_UNKNOWN_ERROR:
			kdb_printf("unknown error");
			break;

		/** memory error codes
		 **/
		case KLE_BAD_MAP_FILE:
			kdb_printf("bad map file");
			break;
		case KLE_BAD_DUMP:
			kdb_printf("bad dump file");
			break;
		case KLE_BAD_DUMPTYPE:
			kdb_printf("bad dumptype");
			break;
		case KLE_INVALID_LSEEK:
			kdb_printf("lseek error");
			break;
		case KLE_INVALID_READ:
			kdb_printf("not found in dump file");
			break;
		case KLE_BAD_KERNINFO:
			kdb_printf("bad kerninfo struct");
			break;
		case KLE_INVALID_PADDR:
			kdb_printf("invalid physical address");
			break;
		case KLE_INVALID_VADDR:
			kdb_printf("invalid virtual address");
			break;
		case KLE_INVALID_VADDR_ALIGN:
			kdb_printf("invalid vaddr alignment");
			break;
		case KLE_INVALID_MAPPING:
			kdb_printf("invalid address mapping");
			break;
		case KLE_PAGE_NOT_PRESENT:
			kdb_printf("page not present");
			break;
		case KLE_BAD_ELF_FILE:
			kdb_printf("bad elf file");
			break;
		case KLE_ARCHIVE_FILE:
			kdb_printf("archive file");
			break;
		case KLE_MAP_FILE_PRESENT:
			kdb_printf("map file present");
			break;
		case KLE_BAD_MAP_FILENAME:
			kdb_printf("bad map filename");
			break;
		case KLE_BAD_DUMP_FILENAME:
			kdb_printf("bad dump filename");
			break;
		case KLE_BAD_NAMELIST_FILE:
			kdb_printf("bad namelist file");
			break;
		case KLE_BAD_NAMELIST_FILENAME:
			kdb_printf("bad namelist filename");
			break;

		/** symbol error codes
		 **/
		case KLE_NO_SYMTAB:
			kdb_printf("no symtab");
			break;
		case KLE_NO_SYMBOLS:
			kdb_printf("no symbol information");
			break;
		case KLE_NO_MODULE_LIST:
			kdb_printf("kernel without module support");
			break;

		/** kernel data error codes
		 **/
		case KLE_INVALID_KERNELSTACK:
			kdb_printf("invalid kernel stack");
			break;
		case KLE_INVALID_STRUCT_SIZE:
			kdb_printf("invalid struct size");
			break;
		case KLE_BEFORE_RAM_OFFSET:
			kdb_printf("physical address proceeds start of RAM");
			break;
		case KLE_AFTER_MAXPFN:
			kdb_printf("PFN exceeds maximum PFN");
			break;
		case KLE_AFTER_PHYSMEM:
			kdb_printf("address exceeds physical memory");
			break;
		case KLE_AFTER_MAXMEM:
			kdb_printf("address exceeds maximum physical address");
			break;
		case KLE_PHYSMEM_NOT_INSTALLED:
			kdb_printf("physical memory not installed");
			break;
		case KLE_NO_DEFTASK:
			kdb_printf("default task not set");
			break;
		case KLE_PID_NOT_FOUND:
			kdb_printf("PID not found");
			break;
		case KLE_DEFTASK_NOT_ON_CPU:
			kdb_printf("default task not running on a cpu");
			break;
		case KLE_NO_CURCPU:
			kdb_printf("current cpu could not be determined");
			break;

		case KLE_KERNEL_MAGIC_MISMATCH:
			kdb_printf("kernel_magic mismatch "
				"of map and memory image");
			break;

		case KLE_INVALID_DUMP_HEADER:
			kdb_printf("invalid dump header in dump");
			break;

		case KLE_DUMP_INDEX_CREATION:
			kdb_printf("cannot create index file");
			break;

		case KLE_DUMP_HEADER_ONLY:
			kdb_printf("dump only has a dump header");
			break;

		case KLE_NO_END_SYMBOL:
			kdb_printf("no _end symbol in kernel");
			break;

		case KLE_NO_CPU:
			kdb_printf("CPU not installed");
			break;

		default:
			break;
	}
	kdb_printf("\n");
}

/*
 * kl_print_string()
 *
 *   print out a string, translating all embeded control characters
 *   (e.g., '\n' for newline, '\t' for tab, etc.)
 */
void
kl_print_string(char *s)
{
	char *sp, *cp;

	kl_reset_error();

	if (!(sp = s)) {
		klib_error = KLE_BAD_STRING;
		return;
	}
	/* FIXME: untested */
	if (invalid_address((kaddr_t)sp, 1)) {
		klib_error = KLE_INVALID_PADDR;
		return;
	}

	while (sp) {
		if ((cp = strchr(sp, '\\'))) {
			switch (*(cp + 1)) {

				case 'n' :
					*cp++ = '\n';
					*cp++ = 0;
					break;

				case 't' :
					*cp++ = '\t';
					*cp++ = 0;
					break;

				default :
					if (*(cp + 1) == 0) {
						klib_error = KLE_BAD_STRING;
						return;
					}
					/* Change the '\' character to a zero
					 * and then print the string (the rest
					 * of the string will be picked
					 * up on the next pass).
					 */
					*cp++ = 0;
					break;
			}
			kdb_printf("%s", sp);
			sp = cp;
		} else {
			kdb_printf("%s", sp);
			sp = 0;
		}
	}
}

/*
 * print_eval_results()
 */
int
print_eval_results(node_t *np, int flags)
{
	int size, i, count, ptr_cnt = 0;
	kaddr_t addr;
	char *typestr;
	kltype_t *kltp, *rkltp = NULL, *nkltp;
	type_t *tp;

	/* Print the results
	 */
	switch (np->node_type) {

		case NUMBER:
			print_number(np, flags);
			break;

		case TYPE_DEF: {

			/* First, determine the number of levels of indirection
			 * by determining the number of pointer type records.
			 */
			if ((tp = np->type)) {
				while (tp && (tp->flag == POINTER_FLAG)) {
					ptr_cnt++;
					tp = tp->t_next;
				}
				if (tp) {
					rkltp = tp->t_kltp;
				}
			}
			if (!rkltp) {
				kdb_printf("Type information not available\n");
				return(1);
			}

			if (ptr_cnt) {

				/* If this is a member, we need to get the
				 * first type record.
				 */
				if (rkltp->kl_type == KLT_MEMBER) {
					/* We need to get down to the first
					 * real type record...
					 */
					rkltp = rkltp->kl_realtype;
				}

				/* step over any KLT_POINTER type records.
				 */
				while (rkltp && rkltp->kl_type == KLT_POINTER) {
					rkltp = rkltp->kl_realtype;
				}
				if (!rkltp) {
					kdb_printf("Bad type information\n");
					return(1);
				}
				typestr = rkltp->kl_typestr;
				if (rkltp->kl_type == KLT_FUNCTION) {
					kdb_printf("%s(", typestr);
				} else if (rkltp->kl_type == KLT_ARRAY) {
					kdb_printf("(%s(", typestr);
				} else {
					kdb_printf("(%s", typestr);
				}
				for (i = 0; i < ptr_cnt; i++) {
					kdb_printf("*");
				}
				if (rkltp->kl_type == KLT_FUNCTION) {
					kdb_printf(")(");
				} else if (rkltp->kl_type == KLT_ARRAY) {
					kdb_printf(")");

					nkltp = rkltp;
					while (nkltp->kl_type == KLT_ARRAY) {
						count = nkltp->kl_high_bounds -
						  nkltp->kl_low_bounds + 1;
						kdb_printf("[%d]", count);
						nkltp = nkltp->kl_elementtype;
					}
				}
				kdb_printf(") ");
				kdb_printf("0x%llx", np->value);

				if (ptr_cnt > 1) {
					break;
				}

				if ((rkltp->kl_type == KLT_BASE) &&
					rkltp->kl_encoding == ENC_CHAR) {
					kdb_printf(" = ");
					print_string(np->value, 0);
				}
				break;
			}
			if (np->flags & KLTYPE_FLAG) {
				void * ptr;

				/* Get the type information. It's possible
				 * that the type is a member. In which case,
				 * the size may only be from this record
				 * (which would be the casse if this is an
				 * array). We must check the original type
				 * record first, and try the realtype record
				 * if the value is zero.
				 */
				kltp = np->type->t_kltp;

				if (kltp->kl_type == KLT_MEMBER) {
					rkltp = kltp->kl_realtype;
				} else {
					rkltp = kltp;
				}

				/* Check to see if this is a typedef. If
				 * it is, then it might be a typedef for
				 * a pointer type. Don't walk to the last
				 * type record.
				 */
				while (rkltp->kl_type == KLT_TYPEDEF) {
					rkltp = rkltp->kl_realtype;
				}

				if (rkltp->kl_type == KLT_POINTER) {
					kdb_printf("0x%llx", np->value);
					break;
				}
				if((rkltp->kl_name != 0) &&
					!(strcmp(rkltp->kl_name, "void"))) {
					/* we are about to dereference
					 * a void pointer.
					 */
					kdb_printf("Can't dereference a "
						"generic pointer.\n");
					return(1);
				}

				size = rkltp->kl_size;
				if (!size || (size < 0)) {
					size = kltp->kl_size;
				}

				if(rkltp->kl_type==KLT_ARRAY) {
					size = rkltp->kl_high_bounds -
						rkltp->kl_low_bounds + 1;
					if(rkltp->kl_elementtype == NULL){
						kdb_printf("Incomplete array"
							" type.\n");
							return(1);
					}
					if(rkltp->kl_elementtype->kl_type ==
							KLT_POINTER){
						size *= sizeof(void *);
					} else {
						size *= rkltp->kl_elementtype->kl_size;
					}
				}
				if(size){
					ptr = kl_alloc_block(size);
				} else {
					ptr = NULL;
				}
				if ((rkltp->kl_type == KLT_BASE) &&
						!(np->flags & ADDRESS_FLAG)) {
					switch (size) {
						case 1:
							*(unsigned char *)ptr =
								np->value;
							break;

						case 2:
							*(unsigned short *)ptr =
								np->value;
							break;

						case 4:
							*(unsigned int *)ptr =
								np->value;
							break;

						case 8:
							*(unsigned long long *)
								ptr = np->value;
							break;
					}
					kl_print_type(ptr, rkltp, 0,
						flags|SUPPRESS_NAME);
					kl_free_block(ptr);
					return(1);
				}

				if(size){
					addr = np->address;
					if (invalid_address(addr, size)) {
						kdb_printf (
						 "invalid address %#lx\n",
							 addr);
						return 1;
					}
					kl_get_block(addr, size, (void *)ptr,
							(void *)0);
					if (KL_ERROR) {
						kl_print_error();
						kl_free_block(ptr);
						return(1);
					}
				}
				/* Print out the actual type
				 */
				switch (rkltp->kl_type) {
					case KLT_STRUCT:
					case KLT_UNION:
						kl_print_type(ptr, rkltp, 0,
							flags);
						break;

					case KLT_ARRAY:
						kl_print_type(ptr, rkltp, 0,
							flags| SUPPRESS_NAME);
						break;

					default:
						kl_print_type(ptr, rkltp, 0,
							(flags|
							SUPPRESS_NAME|
							SUPPRESS_NL));
						break;
				}
				if(ptr){
					kl_free_block(ptr);
				}
			}
			break;
		}

		case VADDR:
			/* If we get here, there was no type info available.
			 * The ADDRESS_FLAG should be set (otherwise we
			 * would have returned an error). So, print out
			 * the address.
			 */
			kdb_printf("0x%lx", np->address);
			break;

		default:
			if (np->node_type == TEXT) {
				kl_print_string(np->name);
				if (KL_ERROR) {
					kl_print_error();
					return(1);
				}
			} else if (np->node_type == CHARACTER) {
				kdb_printf("\'%c\'", (char)np->value);
			}
			break;
	}
	return(0);
}

/*
 * print_eval_error()
 */
void
print_eval_error(
	char *cmdname,
	char *s,
	char *bad_ptr,
	uint64_t error,
	int flags)
{
	int i, cmd_len;

	kdb_printf("%s %s\n", cmdname, s);
	cmd_len = strlen(cmdname);

	if (!bad_ptr) {
		for (i = 0; i < (strlen(s) + cmd_len); i++) {
			kdb_printf(" ");
		}
	} else {
		for (i = 0; i < (bad_ptr - s + 1 + cmd_len); i++) {
			kdb_printf(" ");
		}
	}
	kdb_printf("^ ");
	switch (error) {
		case E_OPEN_PAREN :
			kdb_printf("Too many open parenthesis\n");
			break;

		case E_CLOSE_PAREN :
			kdb_printf("Too many close parenthesis\n");
			break;

		case E_BAD_STRUCTURE :
			kdb_printf("Invalid structure\n");
			break;

		case E_MISSING_STRUCTURE :
			kdb_printf("Missing structure\n");
			break;

		case E_BAD_MEMBER :
			kdb_printf("No such member\n");
			break;

		case E_BAD_OPERATOR :
			kdb_printf("Invalid operator\n");
			break;

		case E_MISSING_OPERAND :
			kdb_printf("Missing operand\n");
			break;

		case E_BAD_OPERAND :
			kdb_printf("Invalid operand\n");
			break;

		case E_BAD_TYPE :
			kdb_printf("Invalid type\n");
			if (!have_debug_file) {
				kdb_printf("no debuginfo file\n");
				return;
			}
			break;

		case E_NOTYPE :
			kdb_printf("Could not find type information\n");
			break;

		case E_BAD_POINTER :
			kdb_printf("Invalid pointer\n");
			break;

		case E_BAD_INDEX :
			kdb_printf("Invalid array index\n");
			break;

		case E_BAD_CHAR :
			kdb_printf("Invalid character value\n");
			break;

		case E_BAD_STRING :
			kdb_printf("Non-termining string\n");
			break;

		case E_END_EXPECTED :
			kdb_printf(
				"Expected end of print statement\n");
			break;

		case E_BAD_EVAR :
			kdb_printf("Invalid eval variable\n");
			break;

		case E_BAD_VALUE :
			kdb_printf("Invalid value\n");
			break;

		case E_NO_VALUE :
			kdb_printf("No value supplied\n");
			break;

		case E_DIVIDE_BY_ZERO :
			kdb_printf("Divide by zero\n");
			break;

		case E_BAD_CAST :
			kdb_printf("Invalid cast\n");
			break;

		case E_NO_ADDRESS :
			kdb_printf("Not an address\n");
			break;

		case E_SINGLE_QUOTE :
			kdb_printf("Missing single quote\n");
			break;

		case E_BAD_WHATIS :
			kdb_printf("Invalid whatis Operation\n");
			break;

		case E_NOT_IMPLEMENTED :
			kdb_printf("Not implemented\n");
			break;

		default :
			kdb_printf("Syntax error\n");
			break;
	}
}

/*
 * single_type()
 */
void
single_type(char *str)
{
	char		buffer[256], *type_name;
	kltype_t	*kltp;
	syment_t	*sp;

	type_name = buffer;
	strcpy(type_name, str);

	if (have_debug_file) {
		if ((kltp = kl_find_type(type_name, KLT_TYPE))) {
			kl_print_type((void *)NULL, kltp, 0, C_SHOWOFFSET);
			return;
		}
		if ((kltp = kl_find_type(type_name, KLT_TYPEDEF))) {
			kdb_printf ("typedef %s:\n", type_name);
			kl_print_type((void *)NULL, kltp, 0, C_SHOWOFFSET);
			return;
		}
	}
	if ((sp = kl_lkup_symname(type_name))) {
		kdb_printf ("symbol %s value: %#lx\n", str, sp->s_addr);
		kl_free_block((void *)sp);
		return;
	}
	kdb_printf("could not find type or symbol information for %s\n",
		type_name);
	return;
}

/*
 * sizeof_type()
 */
void
sizeof_type(char *str)
{
	char		buffer[256], *type_name;
	kltype_t	*kltp;

	type_name = buffer;
	strcpy(type_name, str);

	if ((kltp = kl_find_type(type_name, KLT_TYPE))) {
		kdb_printf ("%s %d %#x\n", kltp->kl_typestr,
				kltp->kl_size, kltp->kl_size);
		return;
	}
	if ((kltp = kl_find_type(type_name, KLT_TYPEDEF))) {
		kdb_printf ("%s %d %#x\n", kltp->kl_typestr,
				kltp->kl_size, kltp->kl_size);
		return;
	}
	kdb_printf("could not find type information for %s\n", type_name);
}

EXPORT_SYMBOL(have_debug_file);
EXPORT_SYMBOL(type_tree);
EXPORT_SYMBOL(typedef_tree);

#if defined(CONFIG_X86_32)
/* needed for i386: */
#include <linux/types.h>
#include <asm/div64.h>
/*
 * Generic C version of full 64 bit by 64 bit division
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Code generated for this function might be very inefficient
 * for some CPUs, can be overridden by linking arch-specific
 * assembly versions such as arch/sparc/lib/udivdi.S
 */
uint64_t
__udivdi3(uint64_t dividend, uint64_t divisor)
{
	uint32_t d = divisor;
	/* Scale divisor to 32 bits */
	if (divisor > 0xffffffffULL) {
		unsigned int shift = fls(divisor >> 32);
		d = divisor >> shift;
		dividend >>= shift;
	}
	/* avoid 64 bit division if possible */
	if (dividend >> 32)
		do_div(dividend, d);
	else
		dividend = (uint32_t) dividend / d;
	return dividend;
}

int64_t
__divdi3(int64_t dividend, int64_t divisor)
{
	int32_t d = divisor;
	/* Scale divisor to 32 bits */
	if (divisor > 0xffffffffLL) {
		unsigned int shift = fls(divisor >> 32);
		d = divisor >> shift;
		dividend >>= shift;
	}
	/* avoid 64 bit division if possible */
	if (dividend >> 32)
		do_div(dividend, d);
	else
		dividend = (int32_t) dividend / d;
	return dividend;
}

uint64_t
__umoddi3(uint64_t dividend, uint64_t divisor)
{
	return dividend - (__udivdi3(dividend, divisor) * divisor);
}

int64_t
__moddi3(int64_t dividend, int64_t divisor)
{
	return dividend - (__divdi3(dividend, divisor) * divisor);
}
#endif /* CONFIG_x86_32 */
