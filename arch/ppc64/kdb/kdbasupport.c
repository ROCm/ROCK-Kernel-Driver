/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 *
 * See the file LIA-COPYRIGHT for additional information.
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *      Richard Bass                    1999/07/20
 *              Many bug fixes and enhancements.
 *      Scott Foehner
 *              Port to ia64
 *	Scott Lurndal			1999/12/12
 *		v1.0 restructuring.
 *	Keith Owens			2000/05/23
 *		KDB v1.2
 */

#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#include <asm/processor.h>
#include "privinst.h"
#include <asm/uaccess.h>
#include <asm/machdep.h>

extern const char *kdb_diemsg;
unsigned long cpus_in_kdb=0;
volatile unsigned long kdb_do_reboot=0;

/* prototypes */
int valid_ppc64_kernel_address(unsigned long addr, unsigned long size);
int kdba_excprint(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_super_regs(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_dissect_msr(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_halt(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_dump_tce_table(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_kernelversion(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_dmesg(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_dump_pci_info(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_rd(int argc, const char **argv, const char **envp, struct pt_regs *regs);
int kdba_bt(int argc, const char **argv, const char **envp, struct pt_regs *regs);
unsigned long kdba_getword(unsigned long addr, size_t width);


extern int kdb_dmesg(int argc, const char **argv, const char **envp, struct pt_regs *regs);
extern int kdb_ps(int argc, const char **argv, const char **envp, struct pt_regs *regs);

extern int kdb_parse(const char *cmdstr, struct pt_regs *regs);

/* 60secs * 1000*1000 usecs/sec.  HMC interface requires larger amount of time,.. */
#define KDB_RESET_TIMEOUT 60*1000*1000

/* kdb will use UDBG */
#define USE_UDBG

#ifdef USE_UDBG
#include <asm/udbg.h>
#endif

#include <linux/kbd_kern.h>
#include <linux/sysrq.h>
#include <linux/interrupt.h>

#ifdef CONFIG_MAGIC_SYSRQ
static void
sysrq_handle_kdb(int key, struct pt_regs *pt_regs, struct kbd_struct *kbd, struct tty_struct *tty) 
{
  kdb(KDB_REASON_KEYBOARD,0,pt_regs);
}

static struct sysrq_key_op sysrq_kdb_op = 
{
	handler:	(void*)sysrq_handle_kdb,
	help_msg:	"(x)kdb",
	action_msg:	"Entering kdb\n",
};

void
kdb_map_scc(void)
{
	/* register sysrq 'x' */
	__sysrq_put_key_op('x', &sysrq_kdb_op);
}
#endif


/*
 * kdba_prologue
 *
 *	This function analyzes a gcc-generated function prototype
 *	with or without frame pointers to determine the amount of
 *	automatic storage and register save storage is used on the
 *	stack of the target function.  It only counts instructions
 *	that have been executed up to but excluding the current nip.
 * Inputs:
 *	code	Start address of function code to analyze
 *	pc	Current program counter within function
 *	sp	Current stack pointer for function
 *	fp	Current frame pointer for function, may not be valid
 *	ss	Start of stack for current process.
 *	caller	1 if looking for data on the caller frame, 0 for callee.
 * Outputs:
 *	ar	Activation record, all fields may be set.  fp and oldfp
 *		are 0 if they cannot be extracted.  return is 0 if the
 *		code cannot find a valid return address.  args and arg0
 *		are 0 if the number of arguments cannot be safely
 *		calculated.
 * Returns:
 *	1 if prologue is valid, 0 otherwise.  If pc is 0 treat it as a
 *	valid prologue to allow bt on wild branches.
 * Locking:
 *	None.
 * Remarks:
 *
 */
int
kdba_prologue(const kdb_symtab_t *symtab, kdb_machreg_t pc, kdb_machreg_t sp,
	      kdb_machreg_t fp, kdb_machreg_t ss, int caller, kdb_ar_t *ar)
{
	/* We don't currently use kdb's generic activation record scanning
	 * code to handle backtrace.
	 */
	return 0;
}



/*
 * kdba_getregcontents
 *
 *	Return the contents of the register specified by the
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	The following pseudo register names are supported:
 *	   &regs	 - Prints address of exception frame
 *	   kesp		 - Prints kernel stack pointer at time of fault
 *	   cesp		 - Prints current kernel stack pointer, inside kdb
 *	   ceflags	 - Prints current flags, inside kdb
 *	   %<regname>	 - Uses the value of the registers at the
 *			   last time the user process entered kernel
 *			   mode, instead of the registers at the time
 *			   kdb was entered.
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 * Outputs:
 *	*contents	Pointer to unsigned long to recieve register contents
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 * 	If kdb was entered via an interrupt from the kernel itself then
 *	ss and esp are *not* on the stack.
 */

static struct kdbregs {
	char   *reg_name;
	size_t	reg_offset;
} kdbreglist[] = {
	{ "gpr0",	offsetof(struct pt_regs, gpr[0]) },
	{ "gpr1",	offsetof(struct pt_regs, gpr[1]) },
	{ "gpr2",	offsetof(struct pt_regs, gpr[2]) },
	{ "gpr3",	offsetof(struct pt_regs, gpr[3]) },
	{ "gpr4",	offsetof(struct pt_regs, gpr[4]) },
	{ "gpr5",	offsetof(struct pt_regs, gpr[5]) },
	{ "gpr6",	offsetof(struct pt_regs, gpr[6]) },
	{ "gpr7",	offsetof(struct pt_regs, gpr[7]) },
	{ "gpr8",	offsetof(struct pt_regs, gpr[8]) },
	{ "gpr9",	offsetof(struct pt_regs, gpr[9]) },
	{ "gpr10",	offsetof(struct pt_regs, gpr[10]) },
	{ "gpr11",	offsetof(struct pt_regs, gpr[11]) },
	{ "gpr12",	offsetof(struct pt_regs, gpr[12]) },
	{ "gpr13",	offsetof(struct pt_regs, gpr[13]) },
	{ "gpr14",	offsetof(struct pt_regs, gpr[14]) },
	{ "gpr15",	offsetof(struct pt_regs, gpr[15]) },
	{ "gpr16",	offsetof(struct pt_regs, gpr[16]) },
	{ "gpr17",	offsetof(struct pt_regs, gpr[17]) },
	{ "gpr18",	offsetof(struct pt_regs, gpr[18]) },
	{ "gpr19",	offsetof(struct pt_regs, gpr[19]) },
	{ "gpr20",	offsetof(struct pt_regs, gpr[20]) },
	{ "gpr21",	offsetof(struct pt_regs, gpr[21]) },
	{ "gpr22",	offsetof(struct pt_regs, gpr[22]) },
	{ "gpr23",	offsetof(struct pt_regs, gpr[23]) },
	{ "gpr24",	offsetof(struct pt_regs, gpr[24]) },
	{ "gpr25",	offsetof(struct pt_regs, gpr[25]) },
	{ "gpr26",	offsetof(struct pt_regs, gpr[26]) },
	{ "gpr27",	offsetof(struct pt_regs, gpr[27]) },
	{ "gpr28",	offsetof(struct pt_regs, gpr[28]) },
	{ "gpr29",	offsetof(struct pt_regs, gpr[29]) },
	{ "gpr30",	offsetof(struct pt_regs, gpr[30]) },
	{ "gpr31",	offsetof(struct pt_regs, gpr[31]) },
	{ "nip",	offsetof(struct pt_regs, nip) },
	{ "msr",	offsetof(struct pt_regs, msr) },
	{ "esp",	offsetof(struct pt_regs, gpr[1]) },
  	{ "orig_gpr3",  offsetof(struct pt_regs, orig_gpr3) },
	{ "ctr", 	offsetof(struct pt_regs, ctr) },
	{ "link",	offsetof(struct pt_regs, link) },
	{ "xer", 	offsetof(struct pt_regs, xer) },
	{ "ccr",	offsetof(struct pt_regs, ccr) },
	{ "mq",		offsetof(struct pt_regs, softe) /* mq */ },
	{ "trap",	offsetof(struct pt_regs, trap) },
	{ "dar",	offsetof(struct pt_regs, dar)  },
	{ "dsisr",	offsetof(struct pt_regs, dsisr) },
	{ "result",	offsetof(struct pt_regs, result) },
};

static const int nkdbreglist = sizeof(kdbreglist) / sizeof(struct kdbregs);

unsigned long
getsp(void)
{
	unsigned long x;
	asm("mr %0,1" : "=r" (x):);
	return x;
}

int
kdba_getregcontents(const char *regname,
		    struct pt_regs *regs,
		    kdb_machreg_t *contents)
{
	int i;

	if (strcmp(regname, "&regs") == 0) {
		*contents = (unsigned long)regs;
		return 0;
	}

	if (strcmp(regname, "kesp") == 0) {
		*contents = (unsigned long) current->thread.ksp;
		return 0;
	}

	if (strcmp(regname, "cesp") == 0) {
		*contents = getsp();
		return 0;
	}

	if (strcmp(regname, "ceflags") == 0) {
		long flags;
		local_save_flags(flags);
		*contents = flags;
		return 0;
	}

	if (regname[0] == '%') {
		/* User registers:  %%e[a-c]x, etc */
		regname++;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < nkdbreglist)
	 && (strlen(kdbreglist[i].reg_name) == strlen(regname))) {
		*contents = *(unsigned long *)((unsigned long)regs +
				kdbreglist[i].reg_offset);
		return(0);
	}

	return KDB_BADREG;
}

/*
 * kdba_setregcontents
 *
 *	Set the contents of the register specified by the
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	Supports modification of user-mode registers via
 *	%<register-name>
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 *	contents	Unsigned long containing new register contents
 * Outputs:
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 */

int
kdba_setregcontents(const char *regname,
		  struct pt_regs *regs,
		  unsigned long contents)
{
	int i;

	if (regname[0] == '%') {
		regname++;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < nkdbreglist)
	 && (strlen(kdbreglist[i].reg_name) == strlen(regname))) {
		*(unsigned long *)((unsigned long)regs
				   + kdbreglist[i].reg_offset) = contents;
		return 0;
	}

	return KDB_BADREG;
}

/*
 * kdba_dumpregs
 *
 *	Dump the specified register set to the display.
 *
 * Parameters:
 *	regs		Pointer to structure containing registers.
 *	type		Character string identifying register set to dump
 *	extra		string further identifying register (optional)
 * Outputs:
 * Returns:
 *	0		Success
 * Locking:
 * 	None.
 * Remarks:
 *	This function will dump the general register set if the type
 *	argument is NULL (struct pt_regs).   The alternate register
 *	set types supported by this function:
 *
 *	d 		Debug registers
 *	c		Control registers
 *	u		User registers at most recent entry to kernel
 * Following not yet implemented:
 *	m		Model Specific Registers (extra defines register #)
 *	r		Memory Type Range Registers (extra defines register)
 */

int
kdba_dumpregs(struct pt_regs *regs,
	    const char *type,
	    const char *extra)
{
	int i;
	int count = 0;

	if (type
	 && (type[0] == 'u')) {
		type = NULL;
		regs = (struct pt_regs *)
			(current->thread.ksp - sizeof(struct pt_regs));
	}

	if (type == NULL) {
		struct kdbregs *rlp;
		kdb_machreg_t contents;

		for (i=0, rlp=kdbreglist; i<nkdbreglist; i++,rlp++) {
			kdba_getregcontents(rlp->reg_name, regs, &contents);
			kdb_printf("%-5s = 0x%p%c", rlp->reg_name, (void *)contents, (++count % 2) ? ' ' : '\n');
		}

		kdb_printf("&regs = 0x%p\n", regs);
		return 0;
 	} else {  /* dump a specific register */
 	    kdb_machreg_t contents;
 	    if (KDB_BADREG==kdba_getregcontents(type, regs, &contents)) 
 		kdb_printf("register %-5s not found \n",type);
 	    else
 		kdb_printf("%-5s = 0x%p%c", type, (void *)contents, '\n');
 	    return 0;
	}

	switch (type[0]) {
	case 'm':
		break;
	case 'r':
		break;
	default:
		return KDB_BADREG;
	}

	/* NOTREACHED */
	return 0;
}

kdb_machreg_t
kdba_getpc(kdb_eframe_t ef)
{
    return ef ? ef->nip : 0;
}

int
kdba_setpc(kdb_eframe_t ef, kdb_machreg_t newpc)
{
/* for ppc64, newpc passed in is actually a function descriptor for kdb. */
    ef->nip =     kdba_getword(newpc+8, 8);
    KDB_STATE_SET(IP_ADJUSTED);
    return 0;
}

/*
 * kdba_main_loop
 *
 *	Do any architecture specific set up before entering the main kdb loop.
 *	The primary function of this routine is to make all processes look the
 *	same to kdb, kdb must be able to list a process without worrying if the
 *	process is running or blocked, so make all process look as though they
 *	are blocked.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	error2		kdb's current reason code.  Initially error but can change
 *			acording to kdb state.
 *	db_result	Result from break or debug point.
 *	ef		The exception frame at time of fault/breakpoint.  If reason
 *			is KDB_REASON_SILENT then ef is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Outputs:
 *	Sets nip and esp in current->thread.
 * Locking:
 *	None.
 * Remarks:
 *	none.
 */

int
kdba_main_loop(kdb_reason_t reason, kdb_reason_t reason2, int error,
	       kdb_dbtrap_t db_result, kdb_eframe_t ef)
{
	int rv;
	kdb_do_reboot=0;

	/* case where incoming registers are missing */
	if (ef == NULL)
	{
		struct pt_regs regs;
		asm volatile ("std	0,0(%0)\n\
                               std	1,8(%0)\n\
                               std	2,16(%0)\n\
                               std	3,24(%0)\n\
                               std	4,32(%0)\n\
                               std	5,40(%0)\n\
                               std	6,48(%0)\n\
                               std	7,56(%0)\n\
                               std	8,64(%0)\n\
                               std	9,72(%0)\n\
                               std	10,80(%0)\n\
                               std	11,88(%0)\n\
                               std	12,96(%0)\n\
                               std	13,104(%0)\n\
                               std	14,112(%0)\n\
                               std	15,120(%0)\n\
                               std	16,128(%0)\n\
                               std	17,136(%0)\n\
                               std	18,144(%0)\n\
                               std	19,152(%0)\n\
                               std	20,160(%0)\n\
                               std	21,168(%0)\n\
                               std	22,176(%0)\n\
                               std	23,184(%0)\n\
                               std	24,192(%0)\n\
                               std	25,200(%0)\n\
                               std	26,208(%0)\n\
                               std	27,216(%0)\n\
                               std	28,224(%0)\n\
                               std	29,232(%0)\n\
                               std	30,240(%0)\n\
                               std	31,248(%0)" : : "b" (&regs));
                /* one extra step back..  this frame disappears */
		regs.gpr[1] = kdba_getword(regs.gpr[1], 8);
		/* Fetch the link reg for this stack frame.
		 NOTE: the prev kdb_printf fills in the lr. */
		regs.nip = regs.link = ((unsigned long *)regs.gpr[1])[2];
		regs.msr = get_msr();
		regs.ctr = get_ctr();
		regs.xer = get_xer();
		regs.ccr = get_cr();
		regs.trap = 0;
		/*current->thread.regs = &regs; */
		ef = &regs;
	}
	cpus_in_kdb++;
	rv = kdb_main_loop(reason, reason2, error, db_result, ef);
	cpus_in_kdb--;
	return rv;
}

void
kdba_disableint(kdb_intstate_t *state)
{
	unsigned long *fp = (unsigned long *)state;
	unsigned long flags;
	local_irq_save(flags);
	*fp = flags;
}

void
kdba_restoreint(kdb_intstate_t *state)
{
	unsigned long flags = *(unsigned long *)state;
	local_irq_restore(flags);
}

void
kdba_setsinglestep(struct pt_regs *regs)
{
	regs->msr |= MSR_SE;
}

void
kdba_clearsinglestep(struct pt_regs *regs)
{
	
	regs->msr &= ~MSR_SE;
}

int
kdba_getcurrentframe(struct pt_regs *regs)
{
	regs->gpr[1] = getsp();
	/* this stack pointer becomes invalid after we return, so take another step back.  */
	regs->gpr[1] = kdba_getword(regs->gpr[1], 8);
	return 0;
}

#ifdef KDB_HAVE_LONGJMP
int
kdba_setjmp(kdb_jmp_buf *buf)
{
    asm volatile (
	"mflr 0; std 0,0(%0)\n\
	 std	1,8(%0)\n\
	 std	2,16(%0)\n\
	 mfcr 0; std 0,24(%0)\n\
	 std	13,32(%0)\n\
	 std	14,40(%0)\n\
	 std	15,48(%0)\n\
	 std	16,56(%0)\n\
	 std	17,64(%0)\n\
	 std	18,72(%0)\n\
	 std	19,80(%0)\n\
	 std	20,88(%0)\n\
	 std	21,96(%0)\n\
	 std	22,104(%0)\n\
	 std	23,112(%0)\n\
	 std	24,120(%0)\n\
	 std	25,128(%0)\n\
	 std	26,136(%0)\n\
	 std	27,144(%0)\n\
	 std	28,152(%0)\n\
	 std	29,160(%0)\n\
	 std	30,168(%0)\n\
	 std	31,176(%0)\n\
	 " : : "r" (buf));
    KDB_STATE_SET(LONGJMP);
    return 0;
}

void
kdba_longjmp(kdb_jmp_buf *buf, int val)
{
    if (val == 0)
	val = 1;
    asm volatile (
	"ld	13,32(%0)\n\
	 ld	14,40(%0)\n\
	 ld	15,48(%0)\n\
	 ld	16,56(%0)\n\
	 ld	17,64(%0)\n\
	 ld	18,72(%0)\n\
	 ld	19,80(%0)\n\
	 ld	20,88(%0)\n\
	 ld	21,96(%0)\n\
	 ld	22,104(%0)\n\
	 ld	23,112(%0)\n\
	 ld	24,120(%0)\n\
	 ld	25,128(%0)\n\
	 ld	26,136(%0)\n\
	 ld	27,144(%0)\n\
	 ld	28,152(%0)\n\
	 ld	29,160(%0)\n\
	 ld	30,168(%0)\n\
	 ld	31,176(%0)\n\
	 ld	0,24(%0)\n\
	 mtcrf	0x38,0\n\
	 ld	0,0(%0)\n\
	 ld	1,8(%0)\n\
	 ld	2,16(%0)\n\
	 mtlr	0\n\
	 mr	3,%1\n\
	 " : : "r" (buf), "r" (val));
}
#endif

/*
 * kdba_enable_mce
 *
 *	This function is called once on each CPU to enable machine
 *	check exception handling.
 *
 * Inputs:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *
 */

void
kdba_enable_mce(void)
{
}

/*
 * kdba_enable_lbr
 *
 *	Enable last branch recording.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_enable_lbr(void)
{
}

/*
 * kdba_disable_lbr
 *
 *	disable last branch recording.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_disable_lbr(void)
{
}

/*
 * kdba_print_lbr
 *
 *	Print last branch and last exception addresses
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_print_lbr(void)
{
}

/*
 * kdba_getword
 *
 * 	Architecture specific function to access kernel virtual
 *	address space.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */

/* 	if (access_ok(VERIFY_READ,__gu_addr,size))			\ */
 
extern inline void sync(void)
{
	asm volatile("sync; isync");
}

extern void (*debugger_fault_handler)(struct pt_regs *);
extern void longjmp(u_int *, int);

unsigned long
kdba_getword(unsigned long addr, size_t width)
{
	/*
	 * This function checks the address for validity.  Any address
	 * in the range PAGE_OFFSET to high_memory is legal, any address
	 * which maps to a vmalloc region is legal, and any address which
	 * is a user address, we use get_user() to verify validity.
	 */

    if (!valid_ppc64_kernel_address(addr, width)) {
		        /*
			 * Would appear to be an illegal kernel address;
			 * Print a message once, and don't print again until
			 * a legal address is used.
			 */
			if (!KDB_STATE(SUPPRESS)) {
				kdb_printf("    kdb: Not a kernel-space address 0x%lx \n",addr);
				KDB_STATE_SET(SUPPRESS);
			}
			return 0L;
	}


	/*
	 * A good address.  Reset error flag.
	 */
	KDB_STATE_CLEAR(SUPPRESS);

	switch (width) {
	case 8:
	{	unsigned long *lp;

		lp = (unsigned long *)(addr);
		return *lp;
	}
	case 4:
	{	unsigned int *ip;

		ip = (unsigned int *)(addr);
		return *ip;
	}
	case 2:
	{	unsigned short *sp;

		sp = (unsigned short *)(addr);
		return *sp;
	}
	case 1:
	{	unsigned char *cp;

		cp = (unsigned char *)(addr);
		return *cp;
	}
	}

	kdb_printf("kdbgetword: Bad width\n");
	return 0L;
}



/*
 * kdba_putword
 *
 * 	Architecture specific function to access kernel virtual
 *	address space.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */

unsigned long
kdba_putword(unsigned long addr, size_t size, unsigned long contents)
{
	/*
	 * This function checks the address for validity.  Any address
	 * in the range PAGE_OFFSET to high_memory is legal, any address
	 * which maps to a vmalloc region is legal, and any address which
	 * is a user address, we use get_user() to verify validity.
	 */

	if (addr < PAGE_OFFSET) {
		/*
		 * Usermode address.
		 */
		unsigned long diag;

		switch (size) {
		case 4:
		{	unsigned long *lp;

			lp = (unsigned long *) addr;
			diag = put_user(contents, lp);
			break;
		}
		case 2:
		{	unsigned short *sp;

			sp = (unsigned short *) addr;
			diag = put_user(contents, sp);
			break;
		}
		case 1:
		{	unsigned char *cp;

			cp = (unsigned char *) addr;
			diag = put_user(contents, cp);
			break;
		}
		default:
			kdb_printf("kdba_putword: Bad width\n");
			return 0;
		}

		if (diag) {
			if (!KDB_STATE(SUPPRESS)) {
				kdb_printf("kdb: Bad user address 0x%lx\n", addr);
				KDB_STATE_SET(SUPPRESS);
			}
			return 0;
		}
		KDB_STATE_CLEAR(SUPPRESS);
		return 0;
	}

#if 0
	if (addr > (unsigned long)high_memory) {
		if (!kdb_vmlist_check(addr, addr+size)) {
			/*
			 * Would appear to be an illegal kernel address;
			 * Print a message once, and don't print again until
			 * a legal address is used.
			 */
			if (!KDB_STATE(SUPPRESS)) {
				kdb_printf("kdb: xx Bad kernel address 0x%lx\n", addr);
				KDB_STATE_SET(SUPPRESS);
			}
			return 0L;
		}
	}
#endif

	/*
	 * A good address.  Reset error flag.
	 */
	KDB_STATE_CLEAR(SUPPRESS);

	switch (size) {
	case 4:
	{	unsigned long *lp;

		lp = (unsigned long *)(addr);
		*lp = contents;
		return 0;
	}
	case 2:
	{	unsigned short *sp;

		sp = (unsigned short *)(addr);
		*sp = (unsigned short) contents;
		return 0;
	}
	case 1:
	{	unsigned char *cp;

		cp = (unsigned char *)(addr);
		*cp = (unsigned char) contents;
		return 0;
	}
	}

	kdb_printf("kdba_putword: Bad width 0x%lx\n",size);
	return 0;
}

/*
 * kdba_callback_die
 *
 *	Callback function for kernel 'die' function.
 *
 * Parameters:
 *	regs	Register contents at time of trap
 *	error_code  Trap-specific error code value
 *	trapno	Trap number
 *	vp	Pointer to die message
 * Returns:
 *	Returns 1 if fault handled by kdb.
 * Locking:
 *	None.
 * Remarks:
 *
 */
int
kdba_callback_die(struct pt_regs *regs, int error_code, long trapno, void *vp)
{
	/*
	 * Save a pointer to the message provided to 'die()'.
	 */
	kdb_diemsg = (char *)vp;

	return kdb(KDB_REASON_OOPS, error_code, (kdb_eframe_t) regs);
}

/*
 * kdba_callback_bp
 *
 *	Callback function for kernel breakpoint trap.
 *
 * Parameters:
 *	regs	Register contents at time of trap
 *	error_code  Trap-specific error code value
 *	trapno	Trap number
 *	vp	Not Used.
 * Returns:
 *	Returns 1 if fault handled by kdb.
 * Locking:
 *	None.
 * Remarks:
 *
 */

int
kdba_callback_bp(struct pt_regs *regs, int error_code, long trapno, void *vp)
{
	int diag;

	if (KDB_DEBUG(BP))
		kdb_printf("cb_bp: e_c = %d  tn = %ld regs = 0x%p\n", error_code,
			   trapno, regs);

	diag = kdb(KDB_REASON_BREAK, error_code, (kdb_eframe_t) regs);

	if (KDB_DEBUG(BP))
		kdb_printf("cb_bp: e_c = %d  tn = %ld regs = 0x%p diag = %d\n", error_code,
			   trapno, regs, diag);
	return diag;
}

/*
 * kdba_callback_debug
 *
 *	Callback function for kernel debug register trap.
 *
 * Parameters:
 *	regs	Register contents at time of trap
 *	error_code  Trap-specific error code value
 *	trapno	Trap number
 *	vp	Not used.
 * Returns:
 *	Returns 1 if fault handled by kdb.
 * Locking:
 *	None.
 * Remarks:
 *
 */

int
kdba_callback_debug(struct pt_regs *regs, int error_code, long trapno, void *vp)
{
	return kdb(KDB_REASON_DEBUG, error_code, (kdb_eframe_t) regs);
}




/*
 * kdba_adjust_ip
 *
 * 	Architecture specific adjustment of instruction pointer before leaving
 *	kdb.
 *
 * Parameters:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	ef		The exception frame at time of fault/breakpoint.  If reason
 *			is KDB_REASON_SILENT then ef is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	noop on ix86.
 */

void
kdba_adjust_ip(kdb_reason_t reason, int error, kdb_eframe_t ef)
{
	return;
}



/*
 * kdba_find_tb_table
 *
 * 	Find the traceback table (defined by the ELF64 ABI) located at
 *	the end of the function containing pc.
 *
 * Parameters:
 *	nip	starting instruction addr.  does not need to be at the start of the func.
 *	tab	table to populate if successful
 * Returns:
 *	non-zero if successful.  unsuccessful means that a valid tb table was not found
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */
int kdba_find_tb_table(kdb_machreg_t nip, kdbtbtable_t *tab)
{
	kdb_machreg_t codeaddr = nip;
	kdb_machreg_t codeaddr_max;
	kdb_machreg_t tbtab_start;
	int instr;
	int num_parms;

	if (tab == NULL)
		return 0;
	memset(tab, 0, sizeof(tab));

	if (nip < PAGE_OFFSET) {  /* this is gonna fail for userspace, at least for now.. */
	    return 0;
	}

	/* Scan instructions starting at codeaddr for 128k max */
	for (codeaddr_max = codeaddr + 128*1024*4;
	     codeaddr < codeaddr_max;
	     codeaddr += 4) {
		instr = kdba_getword(codeaddr, 4);
		if (instr == 0) {
			/* table should follow. */
			int version;
			unsigned long flags;
			tbtab_start = codeaddr;	/* save it to compute func start addr */
			codeaddr += 4;
			flags = kdba_getword(codeaddr, 8);
			tab->flags = flags;
			version = (flags >> 56) & 0xff;
			if (version != 0)
				continue;	/* No tb table here. */
			/* Now, like the version, some of the flags are values
			 that are more conveniently extracted... */
			tab->fp_saved = (flags >> 24) & 0x3f;
			tab->gpr_saved = (flags >> 16) & 0x3f;
			tab->fixedparms = (flags >> 8) & 0xff;
			tab->floatparms = (flags >> 1) & 0x7f;
			codeaddr += 8;
			num_parms = tab->fixedparms + tab->floatparms;
			if (num_parms) {
				unsigned int parminfo;
				int parm;
				if (num_parms > 32)
					return 1;	/* incomplete */
				parminfo = kdba_getword(codeaddr, 4);
				/* decode parminfo...32 bits.
				 A zero means fixed.  A one means float and the
				 following bit determines single (0) or double (1).
				 */
				for (parm = 0; parm < num_parms; parm++) {
					if (parminfo & 0x80000000) {
						parminfo <<= 1;
						if (parminfo & 0x80000000)
							tab->parminfo[parm] = KDBTBTAB_PARMDFLOAT;
						else
							tab->parminfo[parm] = KDBTBTAB_PARMSFLOAT;
					} else {
						tab->parminfo[parm] = KDBTBTAB_PARMFIXED;
					}
					parminfo <<= 1;
				}
				codeaddr += 4;
			}
			if (flags & KDBTBTAB_FLAGSHASTBOFF) {
				tab->tb_offset = kdba_getword(codeaddr, 4);
				if (tab->tb_offset > 0) {
					tab->funcstart = tbtab_start - tab->tb_offset;
				}
				codeaddr += 4;
			}
			/* hand_mask appears to be always be omitted. */
			if (flags & KDBTBTAB_FLAGSHASCTL) {
				/* Assume this will never happen for C or asm */
				return 1;	/* incomplete */
			}
			if (flags & KDBTBTAB_FLAGSNAMEPRESENT) {
				int i;
				short namlen = kdba_getword(codeaddr, 2);
				if (namlen >= sizeof(tab->name))
					namlen = sizeof(tab->name)-1;
				codeaddr += 2;
				for (i = 0; i < namlen; i++) {
					tab->name[i] = kdba_getword(codeaddr++, 1);
				}
				tab->name[namlen] = '\0';
			}
			/* Fake up a symtab entry in case the caller finds it useful */
			tab->symtab.value = tab->symtab.sym_start = tab->funcstart;
			tab->symtab.sym_name = tab->name;
			tab->symtab.sym_end = tbtab_start;
			return 1;
		}
	}
	return 0;	/* hit max...sorry. */
}

int
kdba_putarea_size(unsigned long to_xxx, void *from, size_t size)
{
    char c;
    c = *((volatile char *)from);
    c = *((volatile char *)from+size-1);
    return __copy_to_user((void *)to_xxx,from,size);
}



/*
 * valid_ppc64_kernel_address() returns '1' if the address passed in is
 * within a valid range.  Function returns 0 if address is outside valid ranges.
 */

/*

    KERNELBASE    c000000000000000
        (good range)
    high_memory   c0000000 20000000

    VMALLOC_START d000000000000000
        (good range)
    VMALLOC_END   VMALLOC_START + VALID_EA_BITS  

    IMALLOC_START e000000000000000
        (good range)
    IMALLOC_END   IMALLOC_START + VALID_EA_BITS

*/

int
valid_ppc64_kernel_address(unsigned long addr, unsigned long size)
{
	unsigned long i;
	unsigned long end = (addr + size - 1);	

	int userspace_enabled=0;

/* set USERSPACE=1 to enable userspace memory lookups*/
	kdbgetintenv("USERSPACE", &userspace_enabled);	

	for (i = addr; i <= end; i = i ++ ) {
	    if (
		(!userspace_enabled &&
		 ((unsigned long)i < (unsigned long)KERNELBASE     ))  || 		
		(((unsigned long)i > (unsigned long)high_memory) &&
		 ((unsigned long)i < (unsigned long)VMALLOC_START) )  ||
		(((unsigned long)i > (unsigned long)VMALLOC_END) &&
		 ((unsigned long)i < (unsigned long)IMALLOC_START) )  ||
		( (unsigned long)i > (unsigned long)IMALLOC_END    )       ) {
		return 0;
	    }
	}
	return 1;
}


int
kdba_getarea_size(void *to, unsigned long from_xxx, size_t size)
{
	int is_valid_kern_addr = valid_ppc64_kernel_address(from_xxx, size);
	int diag = 0;

	*((volatile char *)to) = '\0';
	*((volatile char *)to + size - 1) = '\0';

	if (is_valid_kern_addr) {
		memcpy(to, (void *)from_xxx, size);
	} else {
            /*  user space address, just return.  */
	    diag = -1;
	}

	return diag;
}



/*
 *  kdba_readarea_size, reads size-lump of memory into to* passed in, returns size.
 * Making it feel a bit more like mread.. when i'm clearer on kdba end, probally will
 * remove one of these.
 */
int
kdba_readarea_size(unsigned long from_xxx,void *to, size_t size)
{
    int is_valid_kern_addr = valid_ppc64_kernel_address(from_xxx, size);

    *((volatile char *)to) = '\0';
    *((volatile char *)to + size - 1) = '\0';

    if (is_valid_kern_addr) {
	memcpy(to, (void *)from_xxx, size);
	return size;
    } else {
	/*  user-space, just return...    */
	return 0;
    }
    /* wont get here */
    return 0;
}


/* utilities migrated from Xmon or other kernel debug tools. */

/*
Notes for migrating functions from xmon...
Add functions to this file.  parmlist for functions must match
   (int argc, const char **argv, const char **envp, struct pt_regs *fp)
add function prototype to kdbasupport.c
add function hook to kdba_init() within kdbasupport.c

Common bits...
mread() function calls need to be changed to kdba_readarea_size calls.  straightforward change.
This:
	nr = mread(codeaddr, &namlen, 2); 
becomes this:
	nr = kdba_readarea_size(codeaddr,&namlen,2);
*/

#define EOF	(-1)

/* for traverse_all_pci_devices */
#include "../kernel/pci.h"
/* for NUM_TCE_LEVELS */
#include <asm/pci_dma.h>


/* prototypes */
int scanhex(unsigned long *);
int hexdigit(int c);
/* int kdba_readarea_size(unsigned long from_xxx,void *to,  size_t size); */
void machine_halt(void); 



/*
 A traceback table typically follows each function.
 The find_tb_table() func will fill in this struct.  Note that the struct
 is not an exact match with the encoded table defined by the ABI.  It is
 defined here more for programming convenience.
 */
struct tbtable {
	unsigned long	flags;		/* flags: */
#define TBTAB_FLAGSGLOBALLINK	(1L<<47)
#define TBTAB_FLAGSISEPROL	(1L<<46)
#define TBTAB_FLAGSHASTBOFF	(1L<<45)
#define TBTAB_FLAGSINTPROC	(1L<<44)
#define TBTAB_FLAGSHASCTL	(1L<<43)
#define TBTAB_FLAGSTOCLESS	(1L<<42)
#define TBTAB_FLAGSFPPRESENT	(1L<<41)
#define TBTAB_FLAGSNAMEPRESENT	(1L<<38)
#define TBTAB_FLAGSUSESALLOCA	(1L<<37)
#define TBTAB_FLAGSSAVESCR	(1L<<33)
#define TBTAB_FLAGSSAVESLR	(1L<<32)
#define TBTAB_FLAGSSTORESBC	(1L<<31)
#define TBTAB_FLAGSFIXUP	(1L<<30)
#define TBTAB_FLAGSPARMSONSTK	(1L<<0)
	unsigned char	fp_saved;	/* num fp regs saved f(32-n)..f31 */
	unsigned char	gpr_saved;	/* num gpr's saved */
	unsigned char	fixedparms;	/* num fixed point parms */
	unsigned char	floatparms;	/* num float parms */
	unsigned char	parminfo[32];	/* types of args.  null terminated */
#define TBTAB_PARMFIXED 1
#define TBTAB_PARMSFLOAT 2
#define TBTAB_PARMDFLOAT 3
	unsigned int	tb_offset;	/* offset from start of func */
	unsigned long	funcstart;	/* addr of start of function */
	char		name[64];	/* name of function (null terminated)*/
};


static int find_tb_table(unsigned long codeaddr, struct tbtable *tab);


/* Very cheap human name for vector lookup. */
static
const char *getvecname(unsigned long vec)
{
	char *ret;
	switch (vec) {
	case 0x100:	ret = "(System Reset)"; break; 
	case 0x200:	ret = "(Machine Check)"; break; 
	case 0x300:	ret = "(Data Access)"; break; 
	case 0x400:	ret = "(Instruction Access)"; break; 
	case 0x500:	ret = "(Hardware Interrupt)"; break; 
	case 0x600:	ret = "(Alignment)"; break; 
	case 0x700:	ret = "(Program Check)"; break; 
	case 0x800:	ret = "(FPU Unavailable)"; break; 
	case 0x900:	ret = "(Decrementer)"; break; 
	case 0xc00:	ret = "(System Call)"; break; 
	case 0xd00:	ret = "(Single Step)"; break; 
	case 0xf00:	ret = "(Performance Monitor)"; break; 
	default: ret = "";
	}
	return ret;
}

int
kdba_halt(int argc, const char **argv, const char **envp, struct pt_regs *fp)
{
    kdb_printf("halting machine. ");
    machine_halt();
return 0;
}


int
kdba_excprint(int argc, const char **argv, const char **envp, struct pt_regs *fp)
{
	struct task_struct *c;
	struct tbtable tab;

#ifdef CONFIG_SMP
	kdb_printf("cpu %d: ", smp_processor_id());
#endif /* CONFIG_SMP */

	kdb_printf("Vector: %lx %s at  [%p]\n", fp->trap, getvecname(fp->trap), fp);
	kdb_printf("    pc: %lx", fp->nip);
	if (find_tb_table(fp->nip, &tab) && tab.name[0]) {
		/* Got a nice name for it */
		int delta = fp->nip - tab.funcstart;
		kdb_printf(" (%s+0x%x)", tab.name, delta);
	}
	kdb_printf("\n");
	kdb_printf("    lr: %lx", fp->link);
	if (find_tb_table(fp->link, &tab) && tab.name[0]) {
		/* Got a nice name for it */
		int delta = fp->link - tab.funcstart;
		kdb_printf(" (%s+0x%x)", tab.name, delta);
	}
	kdb_printf("\n");
	kdb_printf("    sp: %lx\n", fp->gpr[1]);
	kdb_printf("   msr: %lx\n", fp->msr);

	if (fp->trap == 0x300 || fp->trap == 0x380 || fp->trap == 0x600) {
		kdb_printf("   dar: %lx\n", fp->dar);
		kdb_printf(" dsisr: %lx\n", fp->dsisr);
	}

	/* XXX: need to copy current or we die.  Why? */
	c = current;
	kdb_printf("  current = 0x%p\n", c);
	kdb_printf("  paca    = 0x%p\n", get_paca());
	if (c) {
		kdb_printf("  current = %p, pid = %ld, comm = %s\n",
		       c, (unsigned long)c->pid, (char *)c->comm);
	}
return 0;
}


/* Starting at codeaddr scan forward for a tbtable and fill in the
 given table.  Return non-zero if successful at doing something.
 */
static int
find_tb_table(unsigned long codeaddr, struct tbtable *tab)
{
	unsigned long codeaddr_max;
	unsigned long tbtab_start;
	int nr;
	int instr;
	int num_parms;

	if (tab == NULL)
		return 0;
	memset(tab, 0, sizeof(tab));

	/* Scan instructions starting at codeaddr for 128k max */
	for (codeaddr_max = codeaddr + 128*1024*4;
	     codeaddr < codeaddr_max;
	     codeaddr += 4) {
	    nr=kdba_readarea_size(codeaddr,&instr,4);
		if (nr != 4)
			return 0;	/* Bad read.  Give up promptly. */
		if (instr == 0) {
			/* table should follow. */
			int version;
			unsigned long flags;
			tbtab_start = codeaddr;	/* save it to compute func start addr */
			codeaddr += 4;
			nr = kdba_readarea_size(codeaddr,&flags,8);
			if (nr != 8)
				return 0;	/* Bad read or no tb table. */
			tab->flags = flags;
			version = (flags >> 56) & 0xff;
			if (version != 0)
				continue;	/* No tb table here. */
			/* Now, like the version, some of the flags are values
			 that are more conveniently extracted... */
			tab->fp_saved = (flags >> 24) & 0x3f;
			tab->gpr_saved = (flags >> 16) & 0x3f;
			tab->fixedparms = (flags >> 8) & 0xff;
			tab->floatparms = (flags >> 1) & 0x7f;
			codeaddr += 8;
			num_parms = tab->fixedparms + tab->floatparms;
			if (num_parms) {
				unsigned int parminfo;
				int parm;
				if (num_parms > 32)
					return 1;	/* incomplete */
				nr = kdba_readarea_size(codeaddr,&parminfo,4);
				if (nr != 4)
					return 1;	/* incomplete */
				/* decode parminfo...32 bits.
				 A zero means fixed.  A one means float and the
				 following bit determines single (0) or double (1).
				 */
				for (parm = 0; parm < num_parms; parm++) {
					if (parminfo & 0x80000000) {
						parminfo <<= 1;
						if (parminfo & 0x80000000)
							tab->parminfo[parm] = TBTAB_PARMDFLOAT;
						else
							tab->parminfo[parm] = TBTAB_PARMSFLOAT;
					} else {
						tab->parminfo[parm] = TBTAB_PARMFIXED;
					}
					parminfo <<= 1;
				}
				codeaddr += 4;
			}
			if (flags & TBTAB_FLAGSHASTBOFF) {
			    nr = kdba_readarea_size(codeaddr,&tab->tb_offset,4);
				if (nr != 4)
					return 1;	/* incomplete */
				if (tab->tb_offset > 0) {
					tab->funcstart = tbtab_start - tab->tb_offset;
				}
				codeaddr += 4;
			}
			/* hand_mask appears to be always be omitted. */
			if (flags & TBTAB_FLAGSHASCTL) {
				/* Assume this will never happen for C or asm */
				return 1;	/* incomplete */
			}
			if (flags & TBTAB_FLAGSNAMEPRESENT) {
				short namlen;
				nr = kdba_readarea_size(codeaddr,&namlen,2);
				if (nr != 2)
					return 1;	/* incomplete */
				if (namlen >= sizeof(tab->name))
					namlen = sizeof(tab->name)-1;
				codeaddr += 2;
				nr = kdba_readarea_size(codeaddr,tab->name,namlen);
				tab->name[namlen] = '\0';
				codeaddr += namlen;
			}
			return 1;
		}
	}
	return 0;	/* hit max...sorry. */
}


int
kdba_dissect_msr(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
   long int msr;

   if (argc==0)
       msr = regs->msr;
/*       msr = get_msr(); */
    else 
	kdbgetularg(argv[1], &msr);

   kdb_printf("msr: %lx (",msr);
   {
       if (msr & MSR_SF)   kdb_printf("SF ");
       if (msr & MSR_ISF)  kdb_printf("ISF ");
       if (msr & MSR_HV)   kdb_printf("HV ");
       if (msr & MSR_VEC)  kdb_printf("VEC ");
       if (msr & MSR_POW)  kdb_printf("POW/");  /* pow/we share */
       if (msr & MSR_WE)   kdb_printf("WE ");
       if (msr & MSR_TGPR) kdb_printf("TGPR/"); /* tgpr/ce share */
       if (msr & MSR_CE)   kdb_printf("CE ");
       if (msr & MSR_ILE)  kdb_printf("ILE ");
       if (msr & MSR_EE)   kdb_printf("EE ");
       if (msr & MSR_PR)   kdb_printf("PR ");
       if (msr & MSR_FP)   kdb_printf("FP ");
       if (msr & MSR_ME)   kdb_printf("ME ");
       if (msr & MSR_FE0)  kdb_printf("FE0 ");
       if (msr & MSR_SE)   kdb_printf("SE ");
       if (msr & MSR_BE)   kdb_printf("BE/");   /* be/de share */
       if (msr & MSR_DE)   kdb_printf("DE ");
       if (msr & MSR_FE1)  kdb_printf("FE1 ");
       if (msr & MSR_IP)   kdb_printf("IP ");
       if (msr & MSR_IR)   kdb_printf("IR ");
       if (msr & MSR_DR)   kdb_printf("DR ");
       if (msr & MSR_PE)   kdb_printf("PE ");
       if (msr & MSR_PX)   kdb_printf("PX ");
       if (msr & MSR_RI)   kdb_printf("RI ");
       if (msr & MSR_LE)   kdb_printf("LE ");
   }
   kdb_printf(")\n");

   if (msr & MSR_SF)   kdb_printf(" 64 bit mode enabled \n");
   if (msr & MSR_ISF)  kdb_printf(" Interrupt 64b mode valid on 630 \n");
   if (msr & MSR_HV)   kdb_printf(" Hypervisor State \n");
   if (msr & MSR_VEC)  kdb_printf(" Enable Altivec \n");
   if (msr & MSR_POW)  kdb_printf(" Enable Power Management  \n");
   if (msr & MSR_WE)   kdb_printf(" Wait State Enable   \n");
   if (msr & MSR_TGPR) kdb_printf(" TLB Update registers in use   \n");
   if (msr & MSR_CE)   kdb_printf(" Critical Interrupt Enable   \n");
   if (msr & MSR_ILE)  kdb_printf(" Interrupt Little Endian   \n");
   if (msr & MSR_EE)   kdb_printf(" External Interrupt Enable   \n");
   if (msr & MSR_PR)   kdb_printf(" Problem State / Privilege Level  \n"); 
   if (msr & MSR_FP)   kdb_printf(" Floating Point enable   \n");
   if (msr & MSR_ME)   kdb_printf(" Machine Check Enable   \n");
   if (msr & MSR_FE0)  kdb_printf(" Floating Exception mode 0  \n"); 
   if (msr & MSR_SE)   kdb_printf(" Single Step   \n");
   if (msr & MSR_BE)   kdb_printf(" Branch Trace   \n");
   if (msr & MSR_DE)   kdb_printf(" Debug Exception Enable   \n");
   if (msr & MSR_FE1)  kdb_printf(" Floating Exception mode 1   \n");
   if (msr & MSR_IP)   kdb_printf(" Exception prefix 0x000/0xFFF   \n");
   if (msr & MSR_IR)   kdb_printf(" Instruction Relocate   \n");
   if (msr & MSR_DR)   kdb_printf(" Data Relocate   \n");
   if (msr & MSR_PE)   kdb_printf(" Protection Enable   \n");
   if (msr & MSR_PX)   kdb_printf(" Protection Exclusive Mode   \n");
   if (msr & MSR_RI)   kdb_printf(" Recoverable Exception   \n");
   if (msr & MSR_LE)   kdb_printf(" Little Endian   \n");
   kdb_printf(".\n");

return 0;
}

int
kdba_super_regs(int argc, const char **argv, const char **envp, struct pt_regs *regs){
	int i;
	struct paca_struct*  ptrPaca = NULL;
	struct ItLpPaca*  ptrLpPaca = NULL;
	struct ItLpRegSave*  ptrLpRegSave = NULL;

	{
	        unsigned long sp, toc;
		kdb_printf("sr::");
		asm("mr %0,1" : "=r" (sp) :);
		asm("mr %0,2" : "=r" (toc) :);

		kdb_printf("msr  = %.16lx  sprg0= %.16lx\n", get_msr(), get_sprg0());
		kdb_printf("pvr  = %.16lx  sprg1= %.16lx\n", get_pvr(), get_sprg1()); 
		kdb_printf("dec  = %.16lx  sprg2= %.16lx\n", get_dec(), get_sprg2());
		kdb_printf("sp   = %.16lx  sprg3= %.16lx\n", sp, get_sprg3());
		kdb_printf("toc  = %.16lx  dar  = %.16lx\n", toc, get_dar());
		kdb_printf("srr0 = %.16lx  srr1 = %.16lx\n", get_srr0(), get_srr1());
		kdb_printf("asr  = %.16lx\n", mfasr());
		for (i = 0; i < 8; ++i)
			kdb_printf("sr%.2ld = %.16lx  sr%.2ld = %.16lx\n", (long int)i, (unsigned long)get_sr(i), (long int)(i+8), (long unsigned int) get_sr(i+8));

		// Dump out relevant Paca data areas.
		kdb_printf("Paca: \n");
		ptrPaca = (struct paca_struct*)get_sprg3();
    
		kdb_printf("  Local Processor Control Area (LpPaca): \n");
		ptrLpPaca = ptrPaca->xLpPacaPtr;
		kdb_printf("    Saved Srr0=%.16lx  Saved Srr1=%.16lx \n", ptrLpPaca->xSavedSrr0, ptrLpPaca->xSavedSrr1);
		kdb_printf("    Saved Gpr3=%.16lx  Saved Gpr4=%.16lx \n", ptrLpPaca->xSavedGpr3, ptrLpPaca->xSavedGpr4);
		kdb_printf("    Saved Gpr5=%.16lx \n", ptrLpPaca->xSavedGpr5);
    
		kdb_printf("  Local Processor Register Save Area (LpRegSave): \n");
		ptrLpRegSave = ptrPaca->xLpRegSavePtr;
		kdb_printf("    Saved Sprg0=%.16lx  Saved Sprg1=%.16lx \n", ptrLpRegSave->xSPRG0, ptrLpRegSave->xSPRG0);
		kdb_printf("    Saved Sprg2=%.16lx  Saved Sprg3=%.16lx \n", ptrLpRegSave->xSPRG2, ptrLpRegSave->xSPRG3);
		kdb_printf("    Saved Msr  =%.16lx  Saved Nia  =%.16lx \n", ptrLpRegSave->xMSR, ptrLpRegSave->xNIA);
    
		return 0;
	} 
}



	
int
kdba_dump_tce_table(int argc, const char **argv, const char **envp, struct pt_regs *regs){
    struct TceTable kt; 
    long tce_table_address;
    int nr;
    int i,j,k;
    int full,empty;
    int fulldump=0;
    u64 mapentry;
    int totalpages;
    int levelpages;

    if (argc == 0) {
	kdb_printf("need address\n");
	return 0;
    }
    else 
	kdbgetularg(argv[1], &tce_table_address);

    if (argc==2)
	if (strcmp(argv[2], "full") == 0) 
	    fulldump=1;

    /* with address, read contents of memory and dump tce table. */
    /* possibly making some assumptions on the depth and size of table..*/

    nr = kdba_readarea_size(tce_table_address+0 ,&kt.busNumber,8);
    nr = kdba_readarea_size(tce_table_address+8 ,&kt.size,8);
    nr = kdba_readarea_size(tce_table_address+16,&kt.startOffset,8);
    nr = kdba_readarea_size(tce_table_address+24,&kt.base,8);
    nr = kdba_readarea_size(tce_table_address+32,&kt.index,8);
    nr = kdba_readarea_size(tce_table_address+40,&kt.tceType,8);
    nr = kdba_readarea_size(tce_table_address+48,&kt.lock,8);

    kdb_printf("\n");
    kdb_printf("TceTable at address %s:\n",argv[1]);
    kdb_printf("BusNumber:   0x%x \n",(uint)kt.busNumber);
    kdb_printf("size:        0x%x \n",(uint)kt.size);
    kdb_printf("startOffset: 0x%x \n",(uint)kt.startOffset);
    kdb_printf("base:        0x%x \n",(uint)kt.base);
    kdb_printf("index:       0x%x \n",(uint)kt.index);
    kdb_printf("tceType:     0x%x \n",(uint)kt.tceType);
#ifdef CONFIG_SMP
    kdb_printf("lock:        0x%x \n",(uint)kt.lock.lock);
#endif

    nr = kdba_readarea_size(tce_table_address+56,&kt.mlbm.maxLevel,8);
    kdb_printf(" maxLevel:        0x%x \n",(uint)kt.mlbm.maxLevel);
    totalpages=0;
    for (i=0;i<NUM_TCE_LEVELS;i++) {
	nr = kdba_readarea_size(tce_table_address+64+i*24,&kt.mlbm.level[i].numBits,8);
	nr = kdba_readarea_size(tce_table_address+72+i*24,&kt.mlbm.level[i].numBytes,8);
	nr = kdba_readarea_size(tce_table_address+80+i*24,&kt.mlbm.level[i].map,8);
	kdb_printf("   level[%d]\n",i);
	kdb_printf("   numBits:   0x%x\n",(uint)kt.mlbm.level[i].numBits);
	kdb_printf("   numBytes:  0x%x\n",(uint)kt.mlbm.level[i].numBytes);
	kdb_printf("   map*:      %p\n",kt.mlbm.level[i].map);

	 /* if these dont match, this might not be a valid tce table, so
	    dont try to iterate the map entries. */
	if (kt.mlbm.level[i].numBits == 8*kt.mlbm.level[i].numBytes) {
	    full=0;empty=0;levelpages=0;
	    for (j=0;j<kt.mlbm.level[i].numBytes; j++) {
		mapentry=0;
		nr = kdba_readarea_size((long int)(kt.mlbm.level[i].map+j),&mapentry,1);
		if (mapentry)
		    full++;
		else
		    empty++;
		if (mapentry && fulldump) {
		    kdb_printf("0x%lx\n",mapentry);
		}
		for (k=0;(k<=64) && ((0x1UL<<k) <= mapentry);k++) {
		    if ((0x1UL<<k) & mapentry) levelpages++;
		}
	    }
	    kdb_printf("      full:0x%x empty:0x%x pages:0x%x\n",full,empty,levelpages);
	} else {
	    kdb_printf("      numBits/numBytes mismatch..? \n");
	}
	totalpages+=levelpages;
    }
    kdb_printf("      Total pages:0x%x\n",totalpages);
    kdb_printf("\n");
    return 0;
}

int
kdba_kernelversion(int argc, const char **argv, const char **envp, struct pt_regs *regs){
    extern char *linux_banner;

    kdb_printf("%s\n",linux_banner);

    return 0;
}


static void * 
kdba_dump_pci(struct device_node *dn, void *data)
{
    struct pci_controller *phb;
    char *device_type;
    char *status;

    phb = (struct pci_controller *)data;
    device_type = get_property(dn, "device_type", 0);
    status = get_property(dn, "status", 0);

    dn->phb = phb;
    kdb_printf("dn:   %p \n",dn);
    kdb_printf("    phb      : %p\n",dn->phb);
    kdb_printf("    name     : %s\n",dn->name);
    kdb_printf("    full_name: %s\n",dn->full_name);
    kdb_printf("    busno    : 0x%x\n",dn->busno);
    kdb_printf("    devfn    : 0x%x\n",dn->devfn);
    kdb_printf("    tce_table: %p\n",dn->tce_table);
    return NULL;
}

int
kdba_dump_pci_info(int argc, const char **argv, const char **envp, struct pt_regs *regs){

    kdb_printf("kdba_dump_pci_info\n");

/* call this traverse function with my function pointer.. it takes care of traversing, my func just needs to parse the device info.. */
    traverse_all_pci_devices(kdba_dump_pci);
    return 0;
}


char *kdb_dumpall_cmds[] = {
    "excp\n",
    "bt\n",
    "rd\n",
    "dmesg\n",
    "msr\n",
    "superreg\n",
    "pci_info\n",
    "ps\n",
    "cpu\n",
    "set BTAPROMPT=none\n",
    "bta\n",
    0
};

char *kdb_dumpbasic_cmds[] = {
    "excp\n",
    "bt\n",
    "rd\n",
    "dmesg 25\n",
    "msr\n",
    "superreg\n",
    "ps\n",
    "cpu\n",
    0
};


/* dump with "all" parm will dump all.  all other variations dump basic.  See the dump*_cmds defined above */
int
kdba_dump(int argc, const char **argv, const char **envp, struct pt_regs *fp)
{
    int i, diag;
    kdb_printf("dump-all\n");
    if ((argc==1)&& (strcmp(argv[1], "all")==0))	{
	for (i = 0; kdb_dumpall_cmds[i]; ++i) {
	    kdb_printf("kdb_cmd[%d]%s: %s",
		       i, " ", kdb_dumpall_cmds[i]);
	    diag = kdb_parse(kdb_dumpall_cmds[i], fp);
	    if (diag)
		kdb_printf("command failed, kdb diag %d\n", diag);
	}
    } else {
	kdb_printf("dump-basic\n");
	for (i = 0; kdb_dumpbasic_cmds[i]; ++i) {
	    kdb_printf("kdb_cmd[%d]%s: %s",
		       i, " ", kdb_dumpbasic_cmds[i]);
	    diag = kdb_parse(kdb_dumpbasic_cmds[i], fp);
	    if (diag)
		kdb_printf("command failed, kdb diag %d\n", diag);
	}
    }
    return 0;
}


/* Toggle the ppcdbg options.   kdb_parse tokenizes the parms, so need to account for that here.  */
int
kdba_ppcdbg(int argc, const char **argv, const char **envp, struct pt_regs *fp) {
    extern char *trace_names[PPCDBG_NUM_FLAGS];

    int i,j;
    unsigned long mask;
    int onoff;
    if (argc==0)
	goto ppcdbg_exit;

    for (i=1;i<=argc;i++) {
	onoff = 1;	/* default */
	if (argv[i][0] == '+' || argv[i][0] == '-') {
			/* explicit on or off */
	    onoff = (argv[i][0] == '+');
	    argv[i]++;
	}

	for (j=0;j<PPCDBG_NUM_FLAGS;j++) {
	    if (trace_names[j] && strcmp(trace_names[j],argv[i])==0) {
		/* have a match */
		mask = (1 << j);
		/* check special case */
		if (strcmp(argv[i],"all")==0) {
		    mask = PPCDBG_ALL;
		}
		if (mask) {
		    if (onoff)
			naca->debug_switch |= mask;
		    else
			naca->debug_switch &= ~mask;
		}
	    } 
	}
    }
    ppcdbg_exit:
      kdb_printf("naca->debug_switch 0x%lx\n",naca->debug_switch);
    return 0;
}

/* enable or disable surveillance.. based on rtasd.c function.
  no arguments - display current timeout value.
  one argument - 'off' or '0' turn off surveillance.
               - '1-255' set surveillance timeout to argument. */
int
kdba_surveillance(int argc, const char **argv, const char **envp, struct pt_regs *fp)
{
    unsigned long timeout;
    int ibm_indicator_token = 9000;
    int error;
    unsigned long ret;

    if (argc==0) {
	goto surveillance_status;
    } else if (((argc==1)&& (strcmp(argv[1], "off")==0))) {
	timeout=0;
    } else {
	kdbgetularg(argv[1], &timeout);
    }

    error = rtas_call(rtas_token("set-indicator"), 3, 1, &ret,
		      ibm_indicator_token, 0, timeout);
    /*    kdb_printf("Surveillance set-indicator returned value: 0x%x\n",ret); */

    if (error) 
	kdb_printf("surveillance rtas_call failure 0x%x \n",error);

    surveillance_status:
      rtas_call(rtas_token("get-sensor-state"), 2, 2, &ret, 
		ibm_indicator_token, 
		0/* instance */);
    kdb_printf("Current surveillance timeout is %ld minutes%s",ret,
	       ret==0?" (disabled).\n":".\n");
    return 0;
}

/* generic debugger() hooks into kdb.  These eliminate the need to add
  ifdef CONFIG_KDB goop to traps.c and fault.c */

void
kdb_reset_debugger(struct pt_regs *regs) {
    int cpu=smp_processor_id();
    static int reset_cpu = -1;
    static spinlock_t reset_lock = SPIN_LOCK_UNLOCKED;
    spin_lock(&reset_lock);
    if (reset_cpu == -1 || reset_cpu == cpu) {
	reset_cpu = cpu;
	spin_unlock(&reset_lock);
	if (kdb_on) {
	    ppc64_attention_msg(0x3200+cpu,"KDB Call        ");
	    kdb(KDB_REASON_ENTER, regs->trap, (kdb_eframe_t) regs);
	    ppc64_attention_msg(0x3300+cpu,"KDB Done        ");
	} else {
	    kdb_on=1;
	    kdb_do_reboot=1;
	    ppc64_attention_msg(0x3600+cpu,"KDB Enabled     ");
	    udelay(KDB_RESET_TIMEOUT);
	    kdb_on=0;
	    if (kdb_do_reboot) {
		ppc64_attention_msg(0x3900+cpu,"Rebooting       ");
		ppc_md.restart("rebooting...");
		return;	/* not reached */
	    } else {
		ppc64_attention_msg(0x3800+cpu,"KDB skip reboot ");
		return;
	    }
	}
    } else {
	spin_unlock(&reset_lock);
	return;
    }
}

void
kdb_debugger(struct pt_regs *regs) {
    if (regs)
	if (regs->trap==0x100) {
	    kdb_reset_debugger(regs);
	} else
	    kdb(KDB_REASON_ENTER,regs->trap,regs);   /* ok */
    else  /* regs invalid */
	kdb(KDB_REASON_SILENT,0,regs);
}

int
kdb_debugger_bpt(struct pt_regs *regs) {
    if (regs)
	return kdb(KDB_REASON_BREAK,regs->trap,regs);
    else  /* regs invalid */
	return kdb(KDB_REASON_SILENT,0,regs);
}

int
kdb_debugger_sstep(struct pt_regs *regs) {
    if (regs)
	return kdb(KDB_REASON_DEBUG,regs->trap,regs); /* ok */
    else  /* regs invalid */
	return kdb(KDB_REASON_SILENT,0,regs);
}

int
kdb_debugger_iabr_match(struct pt_regs *regs) {
    if (regs)
	return kdb(KDB_REASON_BREAK,regs->trap,regs);
    else  /* regs invalid */
	return kdb(KDB_REASON_SILENT,0,regs);
}

int
kdb_debugger_dabr_match(struct pt_regs *regs) {
    if (regs)
	return kdb(KDB_REASON_BREAK,regs->trap,regs);
    else  /* regs invalid */
	return kdb(KDB_REASON_SILENT,0,regs);
}

void
kdb_debugger_fault_handler(struct pt_regs *regs) {
    if (regs)
	kdb(KDB_REASON_FAULT,regs->trap,regs);
    else  /* regs invalid */
	kdb(KDB_REASON_SILENT,0,regs);
    return;
}



int
kdba_state(int argc, const char **argv, const char **envp, struct pt_regs *fp)
{
    int i;
    for (i=0;i<NR_CPUS;i++) {
	if ( kdb_state[i] != 0 ) {
	    kdb_printf("kdb_state[%d] = %x" ,i,kdb_state[i]);
	    kdb_printf(" [");
	    if KDB_STATE_CPU(KDB,i) kdb_printf("KDB,");
	    if KDB_STATE_CPU(LEAVING,i) kdb_printf("LEAVING,");
	    if KDB_STATE_CPU(CMD,i) kdb_printf("CMD,");
	    if KDB_STATE_CPU(KDB_CONTROL,i) kdb_printf("KDB_CONTROL,");
	    if KDB_STATE_CPU(HOLD_CPU,i) kdb_printf("HOLD_CPU,");
	    if KDB_STATE_CPU(DOING_SS,i) kdb_printf("DOING_SS,");
	    if KDB_STATE_CPU(DOING_SSB,i) kdb_printf("DOING_SSB,");
	    if KDB_STATE_CPU(SSBPT,i) kdb_printf("SSBPT,");
	    if KDB_STATE_CPU(REENTRY,i) kdb_printf("REENTRY,");
	    if KDB_STATE_CPU(SUPPRESS,i) kdb_printf("SUPPRESS,");
	    if KDB_STATE_CPU(LONGJMP,i) kdb_printf("LONGJMP,");
	    if KDB_STATE_CPU(PRINTF_LOCK,i) kdb_printf("PRINTF_LOCK,");
	    if KDB_STATE_CPU(WAIT_IPI,i) kdb_printf("WAIT_IPI,");
	    if KDB_STATE_CPU(RECURSE,i) kdb_printf("RECURSE,");
	    if KDB_STATE_CPU(IP_ADJUSTED,i) kdb_printf("IP_ADJUSTED,");
	    if KDB_STATE_CPU(NO_BP_DELAY,i) kdb_printf("NO_BP_DELAY");
	    kdb_printf("]\n");
	}
    }
return 0;
}


/*
 * kdba_init
 * 	Architecture specific initialization.
 */
/*
kdb_register("commandname",              # name of command user will use to invoke function  
             function_name,              # name of function within the code 
             "function example usage",   # sample usage 
             "function description",     # brief description. 
             0                           # if i hit enter again, will command repeat itself ?
Note: functions must take parameters as such:
functionname(int argc, const char **argv, const char **envp, struct pt_regs *regs)
*/

void __init
kdba_init(void)
{
#ifdef CONFIG_MAGIC_SYSRQ
	kdb_map_scc();		/* map sysrq key */
#endif

	debugger = kdb_debugger;
	debugger_bpt = kdb_debugger_bpt;
	debugger_sstep = kdb_debugger_sstep;
	debugger_iabr_match = kdb_debugger_iabr_match;
	debugger_dabr_match = kdb_debugger_dabr_match;
	debugger_fault_handler = NULL; /* this guy is normally off. */
				    /* = kdb_debugger_fault_handler; */

	kdba_enable_lbr();
	kdb_register("excp", kdba_excprint, "excp", "print exception info", 0);
	kdb_register("superreg", kdba_super_regs, "superreg", "display super_regs", 0);
	kdb_register("msr", kdba_dissect_msr, "msr", "dissect msr", 0);
	kdb_register("halt", kdba_halt, "halt", "halt machine", 0);
	kdb_register("tce_table", kdba_dump_tce_table, "tce_table <addr> [full]", "dump the tce table located at <addr>", 0);
	kdb_register("kernel", kdba_kernelversion, "version", "display running kernel version", 0);
	kdb_register("pci_info", kdba_dump_pci_info, "dump_pci_info", "dump pci device info", 0);
	kdb_register("dump", kdba_dump, "dump (all|basic)", "dump all info", 0); 
	kdb_register("state", kdba_state, "state ", "dump state of all processors", 0); 
	kdb_register("surv", kdba_surveillance, "surv [off|1-255] ", "disable/change surveillance timeout", 0); 
	kdb_register("ppcdbg", kdba_ppcdbg, "ppcdbg (a,+b,-c)","toggle PPCDBG options",0);
	if (!ppc_md.udbg_getc_poll)
		kdb_on = 0;
}
