/*
 * $Id: idle.c,v 1.68 1999/10/15 18:16:03 cort Exp $
 *
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>

void zero_paged(void);
void power_save(void);
void inline htab_reclaim(void);

unsigned long htab_reclaim_on = 0;
unsigned long zero_paged_on = 0;
unsigned long powersave_nap = 0;

unsigned long *zero_cache;    /* head linked list of pre-zero'd pages */
atomic_t zerototal;      /* # pages zero'd over time */
atomic_t zeropage_hits;  /* # zero'd pages request that we've done */
atomic_t zero_sz;	      /* # currently pre-zero'd pages */
atomic_t zeropage_calls; /* # zero'd pages request that've been made */

int idled(void)
{
	/* endless loop with no priority at all */
	current->nice = 20;
	current->counter = -100;
	init_idle();	
	for (;;)
	{
		__sti();
		
		check_pgt_cache();

		/*if ( !current->need_resched && zero_paged_on ) zero_paged();*/
		if ( !current->need_resched && htab_reclaim_on ) htab_reclaim();
		if ( !current->need_resched ) power_save();

#ifdef CONFIG_SMP
		if (current->need_resched)
#endif
			schedule();
	}
	return 0;
}

/*
 * SMP entry into the idle task - calls the same thing as the
 * non-smp versions. -- Cort
 */
int cpu_idle(void)
{
	idled();
	return 0; 
}

/*
 * Mark 'zombie' pte's in the hash table as invalid.
 * This improves performance for the hash table reload code
 * a bit since we don't consider unused pages as valid.
 *  -- Cort
 */
PTE *reclaim_ptr = 0;
void inline htab_reclaim(void)
{
#ifndef CONFIG_8xx		
#if 0	
	PTE *ptr, *start;
	static int dir = 1;
#endif	
	struct task_struct *p;
	unsigned long valid = 0;
	extern PTE *Hash, *Hash_end;
	extern unsigned long Hash_size;

	/* if we don't have a htab */
	if ( Hash_size == 0 )
		return;
#if 0	
	/* find a random place in the htab to start each time */
	start = &Hash[jiffies%(Hash_size/sizeof(PTE))];
	/* go a different direction each time */
	dir *= -1;
        for ( ptr = start;
	      !current->need_resched && (ptr != Hash_end) && (ptr != Hash);
	      ptr += dir)
	{
#else
	if ( !reclaim_ptr ) reclaim_ptr = Hash;
	while ( !current->need_resched )
	{
		reclaim_ptr++;
		if ( reclaim_ptr == Hash_end ) reclaim_ptr = Hash;
#endif	  
		if (!reclaim_ptr->v)
			continue;
		valid = 0;
		for_each_task(p)
		{
			if ( current->need_resched )
				goto out;
			/* if this vsid/context is in use */
			if ( (reclaim_ptr->vsid >> 4) == p->mm->context )
			{
				valid = 1;
				break;
			}
		}
		if ( valid )
			continue;
		/* this pte isn't used */
		reclaim_ptr->v = 0;
	}
out:
	if ( current->need_resched ) printk("need_resched: %lx\n", current->need_resched);
#endif /* CONFIG_8xx */
}

#if 0
/*
 * Returns a pre-zero'd page from the list otherwise returns
 * NULL.
 */
unsigned long get_zero_page_fast(void)
{
	unsigned long page = 0;

	atomic_inc(&zero_cache_calls);
	if ( zero_quicklist )
	{
		/* atomically remove this page from the list */
		register unsigned long tmp;
		asm (	"101:lwarx  %1,0,%3\n"  /* reserve zero_cache */
			"    lwz    %0,0(%1)\n" /* get next -- new zero_cache */
			"    stwcx. %0,0,%3\n"  /* update zero_cache */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (tmp), "=&r" (page), "+m" (zero_cache)
			: "r" (&zero_quicklist)
			: "cc" );
#ifdef CONFIG_SMP
		/* if another cpu beat us above this can happen -- Cort */
		if ( page == 0 ) 
			return 0;
#endif /* CONFIG_SMP */		
		/* we can update zerocount after the fact since it is not
		 * used for anything but control of a loop which doesn't
		 * matter since it won't affect anything if it zeros one
		 * less page -- Cort
		 */
		atomic_inc((atomic_t *)&zero_cache_hits);
		atomic_dec((atomic_t *)&zero_cache_sz);
		
		/* zero out the pointer to next in the page */
		*(unsigned long *)page = 0;
		return page;
	}
	return 0;
}

/*
 * Experimental stuff to zero out pages in the idle task
 * to speed up get_free_pages(). Zero's out pages until
 * we've reached the limit of zero'd pages.  We handle
 * reschedule()'s in here so when we return we know we've
 * zero'd all we need to for now.
 */
int zero_cache_water[2] = { 25, 96 }; /* high and low water marks for zero cache */
void zero_paged(void)
{
	unsigned long pageptr = 0;	/* current page being zero'd */
	unsigned long bytecount = 0;  
        register unsigned long tmp;
	pte_t *pte;

	if ( atomic_read(&zero_cache_sz) >= zero_cache_water[0] )
		return;
	while ( (atomic_read(&zero_cache_sz) < zero_cache_water[1]) && (!current->need_resched) )
	{
		/*
		 * Mark a page as reserved so we can mess with it
		 * If we're interrupted we keep this page and our place in it
		 * since we validly hold it and it's reserved for us.
		 */
		pageptr = __get_free_pages(GFP_ATOMIC, 0);
		if ( !pageptr )
			return;
		
		if ( current->need_resched )
			schedule();
		
		/*
		 * Make the page no cache so we don't blow our cache with 0's
		 */
		pte = find_pte(&init_mm, pageptr);
		if ( !pte )
		{
			printk("pte NULL in zero_paged()\n");
			return;
		}
		
		pte_uncache(*pte);
		flush_tlb_page(find_vma(&init_mm,pageptr),pageptr);
		/*
		 * Important here to not take time away from real processes.
		 */
		for ( bytecount = 0; bytecount < PAGE_SIZE ; bytecount += 4 )
		{
			if ( current->need_resched )
				schedule();
			*(unsigned long *)(bytecount + pageptr) = 0;
		}
		
		/*
		 * If we finished zero-ing out a page add this page to
		 * the zero_cache atomically -- we can't use
		 * down/up since we can't sleep in idle.
		 * Disabling interrupts is also a bad idea since we would
		 * steal time away from real processes.
		 * We can also have several zero_paged's running
		 * on different processors so we can't interfere with them.
		 * So we update the list atomically without locking it.
		 * -- Cort
		 */
		
		/* turn cache on for this page */
		pte_cache(*pte);
		flush_tlb_page(find_vma(&init_mm,pageptr),pageptr);
		/* atomically add this page to the list */
		asm (	"101:lwarx  %0,0,%2\n"  /* reserve zero_cache */
			"    stw    %0,0(%3)\n" /* update *pageptr */
#ifdef CONFIG_SMP
			"    sync\n"            /* let store settle */
#endif			
			"    stwcx. %3,0,%2\n"  /* update zero_cache in mem */
			"    bne-   101b\n"     /* if lost reservation try again */
			: "=&r" (tmp), "+m" (zero_quicklist)
			: "r" (&zero_quicklist), "r" (pageptr)
			: "cc" );
		/*
		 * This variable is used in the above loop and nowhere
		 * else so the worst that could happen is we would
		 * zero out one more or one less page than we want
		 * per processor on the machine.  This is because
		 * we could add our page to the list but not have
		 * zerocount updated yet when another processor
		 * reads it.  -- Cort
		 */
		atomic_inc((atomic_t *)&zero_cache_sz);
		atomic_inc((atomic_t *)&zero_cache_total);
	}
}
#endif

void power_save(void)
{
	unsigned long msr, hid0;

	/* only sleep on the 603-family/750 processors */
	switch (_get_PVR() >> 16) {
	case 3:			/* 603 */
	case 6:			/* 603e */
	case 7:			/* 603ev */
	case 8:			/* 750 */
	case 12:		/* 7400 */
		save_flags(msr);
		__cli();
		if (!current->need_resched) {
			asm("mfspr %0,1008" : "=r" (hid0) :);
			hid0 &= ~(HID0_NAP | HID0_SLEEP | HID0_DOZE);
			hid0 |= (powersave_nap? HID0_NAP: HID0_DOZE) | HID0_DPM;
			asm("mtspr 1008,%0" : : "r" (hid0));
		
			/* set the POW bit in the MSR, and enable interrupts
			 * so we wake up sometime! */
			__sti(); /* this keeps rtl from getting confused -- Cort */
			_nmask_and_or_msr(0, MSR_POW | MSR_EE);
		}
		restore_flags(msr);
	default:
		return;
	}
}

