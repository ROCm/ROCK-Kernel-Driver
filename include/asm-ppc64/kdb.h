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
 */
#if !defined(_ASM_KDB_H)
#define _ASM_KDB_H
	/*
	 * KDB_ENTER() is a macro which causes entry into the kernel
	 * debugger from any point in the kernel code stream.  If it 
	 * is intended to be used from interrupt level, it must  use
	 * a non-maskable entry method.
	 */
#define KDB_ENTER()	kdb(KDB_REASON_CALL,0,0);

#ifndef ElfW
# if ELFCLASSM == ELFCLASS32
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
# else
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
# endif
#endif

	/*
	 * Define the exception frame for this architecture
	 */
struct pt_regs;
typedef struct pt_regs	*kdb_eframe_t;

	/*
	 * Needed for exported symbols.
	 */
typedef unsigned long kdb_machreg_t;

#define kdb_machreg_fmt		"0x%016lx"
#define kdb_machreg_fmt0	"0x%016lx"
#define kdb_bfd_vma_fmt		"0x%016lx"
#define kdb_bfd_vma_fmt0	"0x%016lx"
#define kdb_elfw_addr_fmt	"0x%016lx"
#define kdb_elfw_addr_fmt0	"0x%016lx"

	/*
	 * Per cpu arch specific kdb state.  Must be in range 0xff000000.
	 */
#define KDB_STATE_A_IF		0x01000000	/* Saved IF flag */

	 /*
	  * Interface from kernel trap handling code to kernel debugger.
	  */
extern int	kdba_callback_die(struct pt_regs *, int, long, void*);
extern int	kdba_callback_bp(struct pt_regs *, int, long, void*);
extern int	kdba_callback_debug(struct pt_regs *, int, long, void *);

#include <linux/types.h>
extern int kdba_putarea_size(unsigned long to_xxx, void *from, size_t size);
extern int kdba_getarea_size(void *to, unsigned long from_xxx, size_t size);

static inline int
kdba_verify_rw(unsigned long addr, size_t size)
{
	unsigned char data[size];
	return(kdba_getarea_size(data, addr, size) || kdba_putarea_size(addr, data, size));
}

#endif	/* ASM_KDB_H */
