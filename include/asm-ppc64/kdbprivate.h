/*
 * Minimalist Kernel Debugger
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
#if !defined(_ASM_KDBPRIVATE_H)
#define _ASM_KDBPRIVATE_H

typedef unsigned long kdb_machinst_t;

	/*
	 * KDB_MAXBPT describes the total number of breakpoints
	 * supported by this architecure.  
	 */
#define KDB_MAXBPT	4
	/*
	 * KDB_MAXHARDBPT describes the total number of hardware
	 * breakpoint registers that exist.
	 */
#define KDB_MAXHARDBPT	 1
        /*
         * Provide space for KDB_MAX_COMMANDS commands.
         */
#define KDB_MAX_COMMANDS        125

	/*
	 * Platform specific environment entries
	 */
#define KDB_PLATFORM_ENV	"IDMODE=PPC64", "BYTESPERWORD=8", "IDCOUNT=16"

	/*
	 * Define the direction that the stack grows
	 */
#define KDB_STACK_DIRECTION	-1	/* Stack grows down */

	/*
	 * Support for ia32 debug registers 
	 */
typedef struct _kdbhard_bp {
	kdb_machreg_t	bph_reg;	/* Register this breakpoint uses */

	unsigned int	bph_free:1;	/* Register available for use */
	unsigned int	bph_data:1;	/* Data Access breakpoint */

	unsigned int	bph_write:1;	/* Write Data breakpoint */
	unsigned int	bph_mode:2;	/* 0=inst, 1=write, 2=io, 3=read */
	unsigned int	bph_length:2;	/* 0=1, 1=2, 2=BAD, 3=4 (bytes) */
} kdbhard_bp_t;

extern kdbhard_bp_t	kdb_hardbreaks[/* KDB_MAXHARDBPT */];

#define PPC64_BREAKPOINT_INSTRUCTION 0x7fe00008    
#define PPC64_ADJUST_OFFSET 0x00   

#define KDB_HAVE_LONGJMP 
#ifdef KDB_HAVE_LONGJMP
typedef struct __kdb_jmp_buf {
	unsigned int regs[100];
} kdb_jmp_buf;
extern int kdb_setjmp(kdb_jmp_buf *);
extern void kdba_longjmp(kdb_jmp_buf *, int);
extern kdb_jmp_buf  kdbjmpbuf[];
#endif	/* KDB_HAVE_LONGJMP */


/*
 A traceback table typically follows each function.
 The find_tb_table() func will fill in this struct.  Note that the struct
 is not an exact match with the encoded table defined by the ABI.  It is
 defined here more for programming convenience.
 */
typedef struct {
    unsigned long	flags;		/* flags: */
#define KDBTBTAB_FLAGSGLOBALLINK	(1L<<47)
#define KDBTBTAB_FLAGSISEPROL		(1L<<46)
#define KDBTBTAB_FLAGSHASTBOFF		(1L<<45)
#define KDBTBTAB_FLAGSINTPROC		(1L<<44)
#define KDBTBTAB_FLAGSHASCTL		(1L<<43)
#define KDBTBTAB_FLAGSTOCLESS		(1L<<42)
#define KDBTBTAB_FLAGSFPPRESENT		(1L<<41)
#define KDBTBTAB_FLAGSNAMEPRESENT	(1L<<38)
#define KDBTBTAB_FLAGSUSESALLOCA	(1L<<37)
#define KDBTBTAB_FLAGSSAVESCR		(1L<<33)
#define KDBTBTAB_FLAGSSAVESLR		(1L<<32)
#define KDBTBTAB_FLAGSSTORESBC		(1L<<31)
#define KDBTBTAB_FLAGSFIXUP		(1L<<30)
#define KDBTBTAB_FLAGSPARMSONSTK	(1L<<0)
    unsigned char	fp_saved;	/* num fp regs saved f(32-n)..f31 */
    unsigned char	gpr_saved;	/* num gpr's saved */
    unsigned char	fixedparms;	/* num fixed point parms */
    unsigned char	floatparms;	/* num float parms */
    unsigned char	parminfo[32];	/* types of args.  null terminated */
#define KDBTBTAB_PARMFIXED 1
#define KDBTBTAB_PARMSFLOAT 2
#define KDBTBTAB_PARMDFLOAT 3
    unsigned int	tb_offset;	/* offset from start of func */
    unsigned long	funcstart;	/* addr of start of function */
    char		name[64];	/* name of function (null terminated)*/
    kdb_symtab_t	symtab;		/* fake symtab entry */
} kdbtbtable_t;
int kdba_find_tb_table(kdb_machreg_t eip, kdbtbtable_t *tab);

#endif	/* !_ASM_KDBPRIVATE_H */
