/* 
 *   Creation Date: <2002/06/08 21:01:54 samuel>
 *   Time-stamp: <2003/08/27 13:18:33 samuel>
 *   
 *	<fault.c>
 *	
 *	Linux part
 *   
 *   Copyright (C) 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"
#include "alloc.h"
#include "kernel_vars.h"
#include "mmu.h"
#include "mmu_contexts.h"
#include "asmfuncs.h"
#include "emu.h"
#include "misc.h"
#include "rvec.h"
#include "performance.h"
#include "mol-ioctl.h"
#include "mtable.h"

#ifdef CONFIG_HIGHPTE
#error	"MOL is currently incompatible with CONFIG_HIGHPTE"
#endif

static inline ulong
fix_pte( ulong *p, ulong set, ulong flags )
{
	unsigned long ret, tmp;
	
	__asm__ __volatile__("\n"
		"1:	lwarx	%0,0,%3		\n"
		"	andc.	%1,%5,%0	\n"
		"	addi	%1,0,0		\n"
		"	bne-	2f		\n"
		"	or	%1,%0,%4	\n"
		"	stwcx.	%1,0,%3		\n"
		"	bne-	1b		\n"
		"2:				\n"
		: "=&r" (tmp), "=&r" (ret), "=m" (*p)
		: "r" (p), "r" (set), "r" (flags), "m" (*p)
		: "cc" );
	return ret;
}

/*
 * Get physical page corresponding to linux virtual address. Invokes linux page
 * fault handler if the page is missing. This function never fails since we
 * know there is a valid mapping...
 */
#define PAGE_BITS_WRITE		(_PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_HASHPTE )
#define PAGE_BITS_READ		(_PAGE_ACCESSED | _PAGE_HASHPTE )

ulong 
get_phys_page( kernel_vars_t *kv, char *lvptr, int request_rw )
{
	ulong va = (ulong)lvptr;
	ulong lpte, uptr, *ptr;
	ulong flags;
	struct mm_struct *mm;
	struct vm_area_struct *vma;

	/* pte bits that must be set */
	flags = request_rw ? (_PAGE_USER | _PAGE_RW | _PAGE_PRESENT)
		: (_PAGE_USER | _PAGE_PRESENT);
	
	uptr = ((ulong*)current->thread.pgdir)[va>>22];	/* top 10 bits */
	ptr = (ulong*)(uptr & ~0xfff);
	if( !ptr )
		goto no_page;
#ifdef LINUX_26
	ptr = phys_to_virt( (int)ptr );
#endif
	ptr = ptr + ((va>>12) & 0x3ff);	      	/* next 10 bits */

	/* this allows us to keep track of this page until we have
	 * added a full mtable entry for it. The reservation is lost if
	 * a TLB invalidation occurs.
	 */
	make_lvptr_reservation( kv, lvptr );

	/* we atomically set _PAGE_HASHPTE after checking PAGE_PRESENT and PAGE_RW.
	 * We are then guaranteed to be notified about a TLB invalidation through the
	 * flush_hash_page hook.
	 */
	lpte = fix_pte( ptr, (request_rw? PAGE_BITS_WRITE : PAGE_BITS_READ), flags );

	/* permissions violation */
	if( !lpte )
		goto no_page;

	return lpte & ~0xfff;

no_page:
	BUMP( page_missing );

	/* no mac page found... */
	mm = current->mm;
	down_read( &mm->mmap_sem );

	if( !(vma=find_vma(mm,va)) || vma->vm_start > va )
		goto bad_area;
	if( !(vma->vm_flags & (request_rw ? VM_WRITE : (VM_READ | VM_EXEC))) )
		goto bad_area;

	handle_mm_fault( mm, vma, va, request_rw );

	up_read( &mm->mmap_sem );
	return get_phys_page(kv, lvptr, request_rw);

bad_area:
	up_read( &mm->mmap_sem );
	printk("get_phys_page: BAD AREA, lvptr = %08lx\n", va );
	force_sig(SIGSEGV, current);
	return 0;
}


/************************************************************************/
/*	Debugger functions						*/
/************************************************************************/

int 
dbg_get_linux_page( ulong va, dbg_page_info_t *r )
{
	ulong val, uptr, *ptr;
	
	uptr = ((ulong*)current->thread.pgdir)[va>>22];	/* top 10 bits */
	ptr = (ulong*)(uptr & ~0xfff);
	if( !ptr )
		return 1;
#ifdef LINUX_26
	ptr = phys_to_virt( (int)ptr );
#endif
	val = ptr[ (va>>12)&0x3ff ];		/* next 10 bits */

	r->phys = val & ~0xfff;
	r->mflags = 
		  DBG_TRANSL_PAGE_FLAG( val, _PAGE_PRESENT )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_USER )
		| DBG_TRANSL_PAGE_FLAG(	val, _PAGE_GUARDED )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_COHERENT )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_NO_CACHE )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_WRITETHRU )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_DIRTY )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_ACCESSED )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_RW )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_HASHPTE )
		| DBG_TRANSL_PAGE_FLAG( val, _PAGE_EXEC );
	return 0;
}
