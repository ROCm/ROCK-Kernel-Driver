/*
 *  $Id: init.c,v 1.195 1999/10/15 16:39:39 cort Exp $
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>
#endif
#ifdef CONFIG_8260
#include <asm/immap_8260.h>
#include <asm/mpc8260.h>
#endif
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/gemini.h>

#include "mem_pieces.h"

#if defined(CONFIG_4xx)
#include "4xx_tlb.h"
#endif

#define MAX_LOW_MEM	(512 << 20)

#define	PGTOKB(pages)	(((pages) * PAGE_SIZE) >> 10)

int prom_trashed;
atomic_t next_mmu_context;
rwlock_t context_overflow_lock __cacheline_aligned = RW_LOCK_UNLOCKED;
unsigned long *end_of_DRAM;
unsigned long total_memory;
unsigned long total_lowmem;
unsigned long ram_phys_base;
int mem_init_done;
int init_bootmem_done;
int boot_mapsize;
unsigned long totalram_pages;
unsigned long totalhigh_pages;
extern pgd_t swapper_pg_dir[];
extern char _start[], _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern char __prep_begin, __prep_end;
extern char __chrp_begin, __chrp_end;
extern char __pmac_begin, __pmac_end;
extern char __apus_begin, __apus_end;
extern char __openfirmware_begin, __openfirmware_end;
unsigned long ioremap_base;
unsigned long ioremap_bot;
unsigned long avail_start;
extern int num_memory;
extern struct mem_info memory[];
extern boot_infos_t *boot_infos;
extern unsigned int rtas_data, rtas_size;
#ifndef CONFIG_SMP
struct pgtable_cache_struct quicklists;
#endif
#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;
#endif

void MMU_init(void);
void *early_get_page(void);
static void mapin_ram(void);
int map_page(unsigned long va, unsigned long pa, int flags);
void set_phys_avail(unsigned long total_ram);
extern void die_if_kernel(char *,struct pt_regs *,long);

extern char _start[], _end[];
extern char _stext[], etext[];
extern struct task_struct *current_set[NR_CPUS];

char *klimit = _end;
struct mem_pieces phys_avail;

PTE *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
unsigned long _SDR1;
static void hash_init(void);

union ubat {			/* BAT register values to be loaded */
	BAT	bat;
#ifdef CONFIG_PPC64BRIDGE
	u64	word[2];
#else
	u32	word[2];
#endif	
} BATS[4][2];			/* 4 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	unsigned long phys;
} bat_addrs[4];

/*
 * Return PA for this VA if it is mapped by a BAT, or 0
 */
static inline unsigned long v_mapped_by_bats(unsigned long va)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (va >= bat_addrs[b].start && va < bat_addrs[b].limit)
			return bat_addrs[b].phys + (va - bat_addrs[b].start);
	return 0;
}

/*
 * Return VA for a given PA or 0 if not mapped
 */
static inline unsigned long p_mapped_by_bats(unsigned long pa)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (pa >= bat_addrs[b].phys
	    	    && pa < (bat_addrs[b].limit-bat_addrs[b].start)
		              +bat_addrs[b].phys)
			return bat_addrs[b].start+(pa-bat_addrs[b].phys);
	return 0;
}

#else /* CONFIG_4xx || CONFIG_8xx */
#define v_mapped_by_bats(x)	(0UL)
#define p_mapped_by_bats(x)	(0UL)
#endif /* !CONFIG_4xx && !CONFIG_8xx */

/*
 * this tells the system to map all of ram with the segregs
 * (i.e. page tables) instead of the bats.
 * -- Cort
 */
int __map_without_bats;

/* max amount of RAM to use */
unsigned long __max_memory;
/* max amount of low RAM to map in */
unsigned long __max_low_memory = MAX_LOW_MEM;

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if (pgtable_cache_size > high) {
		do {
                        if (pgd_quicklist) {
				free_pgd_slow(get_pgd_fast());
				freed++;
			}
			if (pte_quicklist) {
				pte_free_slow(pte_alloc_one_fast(NULL, 0));
				freed++;
			}
		} while (pgtable_cache_size > low);
	}
	return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t *empty_bad_page_table;

pte_t * __bad_pagetable(void)
{
	clear_page(empty_bad_page_table);
	return empty_bad_page_table;
}

void *empty_bad_page;

pte_t __bad_page(void)
{
	clear_page(empty_bad_page);
	return pte_mkdirty(mk_pte_phys(__pa(empty_bad_page), PAGE_SHARED));
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	struct task_struct *p;
	int highmem = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageHighMem(mem_map+i))
			highmem++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d pages of HIGHMEM\n", highmem);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%d pages in page table cache\n",(int)pgtable_cache_size);
	show_buffers();
	printk("%-8s %3s %8s %8s %8s %9s %8s", "Process", "Pid",
	       "Ctx", "Ctx<<4", "Last Sys", "pc", "task");
#ifdef CONFIG_SMP
	printk(" %3s", "CPU");
#endif /* CONFIG_SMP */
	printk("\n");
	for_each_task(p)
	{
		printk("%-8.8s %3d %8ld %8ld %8ld %c%08lx %08lx ",
		       p->comm,p->pid,
		       (p->mm)?p->mm->context:0,
		       (p->mm)?(p->mm->context<<4):0,
		       p->thread.last_syscall,
		       (p->thread.regs)?user_mode(p->thread.regs) ? 'u' : 'k' : '?',
		       (p->thread.regs)?p->thread.regs->nip:0,
		       (ulong)p);
		{
			int iscur = 0;
#ifdef CONFIG_SMP
			printk("%3d ", p->processor);
			if ( (p->processor != NO_PROC_ID) &&
			     (p == current_set[p->processor]) )
			{
				iscur = 1;
				printk("current");
			}
#else
			if ( p == current )
			{
				iscur = 1;
				printk("current");
			}
			
			if ( p == last_task_used_math )
			{
				if ( iscur )
					printk(",");
				printk("last math");
			}			
#endif /* CONFIG_SMP */
			printk("\n");
		}
	}
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
}

void *
ioremap(unsigned long addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE);
}

void *
__ioremap(unsigned long addr, unsigned long size, unsigned long flags)
{
	unsigned long p, v, i;
	int err;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we map addresses >= ioremap_base
	 * virt == phys; for addresses below this we use
	 * space going down from ioremap_base (ioremap_bot
	 * records where we're up to).
	 */
	p = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;

	/*
	 * If the address lies within the first 16 MB, assume it's in ISA
	 * memory space
	 */
	if ( p < 16*1024*1024 )
		p += _ISA_MEM_BASE;

	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 */
	if ( mem_init_done && (p < virt_to_phys(high_memory)) )
	{
		printk("__ioremap(): phys addr %0lx is RAM lr %p\n", p,
		       __builtin_return_address(0));
		return NULL;
	}

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped?  Perhaps overlapped by a previous
	 * BAT mapping.  If the whole area is mapped then we're done,
	 * otherwise remap it since we want to keep the virt addrs for
	 * each request contiguous.
	 *
	 * We make the assumption here that if the bottom and top
	 * of the range we want are mapped then it's mapped to the
	 * same virt address (and this is contiguous).
	 *  -- Cort
	 */
	if ((v = p_mapped_by_bats(p)) /*&& p_mapped_by_bats(p+size-1)*/ )
		goto out;
	
	if (mem_init_done) {
		struct vm_struct *area;
		area = get_vm_area(size, VM_IOREMAP);
		if (area == 0)
			return NULL;
		v = VMALLOC_VMADDR(area->addr);
	} else {
#ifndef CONFIG_HIGHMEM
		if (p >= ioremap_base)
#else
		if (p >= ioremap_base && p < PKMAP_BASE)
#endif /* CONFIG_HIGHMEM */
			v = p;
		else
			v = (ioremap_bot -= size);
	}

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);
	if (flags & (_PAGE_NO_CACHE | _PAGE_WRITETHRU))
		flags |= _PAGE_GUARDED;

	/*
	 * Should check if it is a candidate for a BAT mapping
	 */

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_page(v+i, p+i, flags);
	if (err) {
		if (mem_init_done)
			vfree((void *)v);
		return NULL;
	}

out:
	return (void *) (v + (addr & ~PAGE_MASK));
}

void iounmap(void *addr)
{
	if (addr > high_memory && (unsigned long) addr < ioremap_bot)
		vfree((void *) (PAGE_MASK & (unsigned long) addr));
}

unsigned long iopa(unsigned long addr)
{
	unsigned long pa;
	pmd_t *pd;
	pte_t *pg;

	/* Check the BATs */
	pa = v_mapped_by_bats(addr);
	if (pa)
		return pa;

	/* Do we have a page table? */
	if (init_mm.pgd == NULL)
		return 0;

	/* Use upper 10 bits of addr to index the first level map */
	pd = (pmd_t *) (init_mm.pgd + (addr >> PGDIR_SHIFT));
	if (pmd_none(*pd))
		return 0;

	/* Use middle 10 bits of addr to index the second-level map */
	pg = pte_offset(pd, addr);
	return (pte_val(*pg) & PAGE_MASK) | (addr & ~PAGE_MASK);
}

int
map_page(unsigned long va, unsigned long pa, int flags)
{
	pmd_t *pd;
	pte_t *pg;
	int err = -ENOMEM;

	spin_lock(&init_mm.page_table_lock);
	/* Use upper 10 bits of VA to index the first level map */
	pd = pmd_offset(pgd_offset_k(va), va);
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_alloc(&init_mm, pd, va);
	if (pg != 0) {
		err = 0;
		set_pte(pg, mk_pte_phys(pa & PAGE_MASK, __pgprot(flags)));
		if (mem_init_done)
			flush_hash_page(0, va);
	}
	spin_unlock(&init_mm.page_table_lock);
	return err;
}

#ifndef CONFIG_8xx
/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * since the hardware hash table functions as an extension of the
 * tlb as far as the linux tables are concerned, flush it too.
 *    -- Cort
 */

/*
 * Flush all tlb/hash table entries (except perhaps for those
 * mapping RAM starting at PAGE_OFFSET, since they never change).
 */
void
local_flush_tlb_all(void)
{
#ifdef CONFIG_PPC64BRIDGE
	/* XXX this assumes that the vmalloc arena starts no lower than
	 * 0xd0000000 on 64-bit machines. */
	flush_hash_segments(0xd, 0xffffff);
#else
	/* this could cause problems on SMP with nobats  -- paulus */
	/* XXX no hash_table_lock?  interesting  -- paulus */
	__clear_user(Hash, Hash_size);
	_tlbia();
#ifdef CONFIG_SMP
	smp_send_tlb_invalidate(0);
#endif /* CONFIG_SMP */
#endif /* CONFIG_PPC64BRIDGE */
}

/*
 * Flush all the (user) entries for the address space described
 * by mm.  We can't rely on mm->mmap describing all the entries
 * that might be in the hash table.
 */
void
local_flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->context == 0) {
		/* don't try to reassign a new context to the kernel */
		/*
		 * This could cause problems on SMP if we aren't using
		 * the BATs (e.g. on POWER4 or if the nobats option is used).
		 * The problem scenario is that one cpu is doing
		 * flush_hash_page or similar when another cpu clears
		 * out the HPTEs which map the flush_hash_page text
		 * and the hash table.  hash_page will then deadlock.
		 * We need some way to have "protected" HPTEs or else
		 * do all hash-table manipulation with the MMU off.
		 *  -- paulus.
		 */
#ifdef CONFIG_PPC64BRIDGE
		flush_hash_segments(0xd, 0xf);
#else
		flush_hash_segments(0xc, 0xf);
#endif CONFIG_PPC64BRIDGE
		_tlbia();
		return;
	}
	mm->context = NO_CONTEXT;
	if (mm == current->mm)
		activate_mm(mm, mm);
#ifdef CONFIG_SMP
	smp_send_tlb_invalidate(0);
#endif	
}

void
local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	if (vmaddr < TASK_SIZE)
		flush_hash_page(vma->vm_mm->context, vmaddr);
	else
		flush_hash_page(0, vmaddr);
#ifdef CONFIG_SMP
	smp_send_tlb_invalidate(0);
#endif	
}


/*
 * for each page addr in the range, call MMU_invalidate_page()
 * if the range is very large and the hash table is small it might be
 * faster to do a search of the hash table and just invalidate pages
 * that are in the range but that's for study later.
 * -- Cort
 */
void
local_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;

	if (mm->context != 0) {
		if (end > TASK_SIZE)
			end = TASK_SIZE;
		if (end - start > 20 * PAGE_SIZE) {
			flush_tlb_mm(mm);
			return;
		}
	}

	for (; start < end; start += PAGE_SIZE)
		flush_hash_page(mm->context, start);

#ifdef CONFIG_SMP
	smp_send_tlb_invalidate(0);
#endif	
}

/*
 * The context counter has overflowed.
 * We set mm->context to NO_CONTEXT for all mm's in the system.
 * We assume we can get to all mm's by looking as tsk->mm for
 * all tasks in the system.
 */
void
mmu_context_overflow(void)
{
	struct task_struct *tsk;

	printk(KERN_DEBUG "mmu_context_overflow\n");
	/* acquire the write lock for context overflow */
	write_lock (&context_overflow_lock);
	/* recheck if overflow still exists */
	if (atomic_read(&next_mmu_context) == LAST_CONTEXT) {
		read_lock(&tasklist_lock);
		for_each_task(tsk) {
			if (tsk->mm)
				tsk->mm->context = NO_CONTEXT;
		}
		read_unlock(&tasklist_lock);
		flush_hash_segments(0x10, 0xffffff);
#ifdef CONFIG_SMP
		smp_send_tlb_invalidate(0);
#endif	
		atomic_set(&next_mmu_context, 0);
	}
	write_unlock (&context_overflow_lock);
	/* make sure current always has a context */
	/* need to check to assure current task has an mm */
	/*   - idle thread does not have an MM */
	if (current->mm) {
		current->mm->context = MUNGE_CONTEXT(atomic_inc_return(&next_mmu_context));
		set_context(current->mm->context, current->mm->pgd);
	}
}
#else /* CONFIG_8xx */
void
mmu_context_overflow(void)
{
	atomic_set(&next_mmu_context, -1);
}
#endif /* CONFIG_8xx */

void flush_page_to_ram(struct page *page)
{
	unsigned long vaddr = (unsigned long) kmap(page);
	__flush_page_to_ram(vaddr);
	kunmap(page);
}

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 */
void __init setbat(int index, unsigned long virt, unsigned long phys,
       unsigned int size, int flags)
{
	unsigned int bl;
	int wimgxpp;
	union ubat *bat = BATS[index];

	bl = (size >> 17) - 1;
	if ((_get_PVR() >> 16) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].word[0] = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].word[1] = phys | wimgxpp;
#ifndef CONFIG_KGDB /* want user access for breakpoints */
		if (flags & _PAGE_USER)
#endif
			bat[1].bat.batu.vp = 1;
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			bat[0].word[0] = bat[0].word[1] = 0;
		} else {
			/* make IBAT same as DBAT */
			bat[0] = bat[1];
		}
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->word[0] = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->word[1] = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

#define IO_PAGE	(_PAGE_NO_CACHE | _PAGE_GUARDED | _PAGE_RW)
#ifdef CONFIG_SMP
#define RAM_PAGE (_PAGE_RW|_PAGE_COHERENT)
#else
#define RAM_PAGE (_PAGE_RW)
#endif
#endif /* CONFIG_8xx */

/*
 * Map in all of physical memory starting at KERNELBASE.
 */
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

static void __init mapin_ram(void)
{
	unsigned long v, p, s, f;

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx) && !defined(CONFIG_POWER4)
	if (!__map_without_bats) {
		unsigned long tot, bl, done;
		unsigned long max_size = (256<<20);
		unsigned long align;

		/* Set up BAT2 and if necessary BAT3 to cover RAM. */

		/* Make sure we don't map a block larger than the
		   smallest alignment of the physical address. */
		/* alignment of ram_phys_base */
		align = ~(ram_phys_base-1) & ram_phys_base;
		/* set BAT block size to MIN(max_size, align) */
		if (align && align < max_size)
			max_size = align;

		tot = total_lowmem;
		for (bl = 128<<10; bl < max_size; bl <<= 1) {
			if (bl * 2 > tot)
				break;
		}

		setbat(2, KERNELBASE, ram_phys_base, bl, RAM_PAGE);
		done = (unsigned long)bat_addrs[2].limit - KERNELBASE + 1;
		if ((done < tot) && !bat_addrs[3].limit) {
			/* use BAT3 to cover a bit more */
			tot -= done;
			for (bl = 128<<10; bl < max_size; bl <<= 1)
				if (bl * 2 > tot)
					break;
			setbat(3, KERNELBASE+done, ram_phys_base+done, bl, 
			       RAM_PAGE);
		}
	}
#endif /* !CONFIG_4xx && !CONFIG_8xx && !CONFIG_POWER4 */

	v = KERNELBASE;
	p = ram_phys_base;
	for (s = 0; s < total_lowmem; s += PAGE_SIZE) {
		/* On the MPC8xx, we want the page shared so we
		 * don't get ASID compares on kernel space.
		 */
		f = _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_SHARED;
#if defined(CONFIG_KGDB) || defined(CONFIG_XMON)
		/* Allows stub to set breakpoints everywhere */
		f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
#else
		if ((char *) v < _stext || (char *) v >= etext)
			f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
#ifndef CONFIG_8xx
		else
			/* On the powerpc (not 8xx), no user access
			   forces R/W kernel access */
			f |= _PAGE_USER;
#endif /* CONFIG_8xx */
#endif /* CONFIG_KGDB */
		map_page(v, p, f);
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	void *p;

	if (init_bootmem_done) {
		p = alloc_bootmem_pages(PAGE_SIZE);
	} else {
		p = mem_pieces_find(PAGE_SIZE, PAGE_SIZE);
	}
	return p;
}

static void free_sec(unsigned long start, unsigned long end, const char *name)
{
	unsigned long cnt = 0;

	while (start < end) {
	  	clear_bit(PG_reserved, &virt_to_page(start)->flags);
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		cnt++;
		start += PAGE_SIZE;
 	}
	if (cnt)
		printk(" %ldk %s", PGTOKB(cnt), name);
}

void free_initmem(void)
{
#define FREESEC(TYPE) \
	free_sec((unsigned long)(&__ ## TYPE ## _begin), \
		 (unsigned long)(&__ ## TYPE ## _end), \
		 #TYPE);

	printk ("Freeing unused kernel memory:");
	FREESEC(init);
	if (_machine != _MACH_Pmac)
		FREESEC(pmac);
	if (_machine != _MACH_chrp)
		FREESEC(chrp);
	if (_machine != _MACH_prep)
		FREESEC(prep);
	if (_machine != _MACH_apus)
		FREESEC(apus);
	if (!have_of)
		FREESEC(openfirmware);
 	printk("\n");
#undef FREESEC
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

extern boot_infos_t *disp_bi;

/*
 * Do very early mm setup such as finding the size of memory
 * and setting up the hash table.
 * A lot of this is prep/pmac specific but a lot of it could
 * still be merged.
 * -- Cort
 */
#if defined(CONFIG_4xx)
void __init
MMU_init(void)
{
	/*
	 * The Zone Protection Register (ZPR) defines how protection will
	 * be applied to every page which is a member of a given zone. At
	 * present, we utilize only two of the 4xx's zones.
	 * The zone index bits (of ZSEL) in the PTE are used for software
	 * indicators, except the LSB.  For user access, zone 15 is used,
	 * for kernel access, zone 14 is used.  We set all but zone 15
	 * to zero, allowing only kernel access as indicated in the PTE.
	 * For zone 15, we set a 10 binary (I guess a 01 would work too)
	 * to allow user access as indicated in the PTE.  This also allows
	 * kernel access as indicated in the PTE.
	 */

        mtspr(SPRN_ZPR, 0x00000002);

	flush_instruction_cache();

	/*
	 * Find the top of physical memory and map all of it in starting
	 * at KERNELBASE.
	 */

        total_memory = total_lowmem = ppc_md.find_end_of_memory();
	end_of_DRAM = __va(total_lowmem);
	set_phys_avail(total_lowmem);
        mapin_ram();

	/*
	 * Set up the real-mode cache parameters for the exception vector
	 * handlers (which are run in real-mode).
	 */

        mtspr(SPRN_DCWR, 0x00000000);	/* All caching is write-back */

        /*
	 * Cache instruction and data space where the exception
	 * vectors and the kernel live in real-mode.
	 */

        mtspr(SPRN_DCCR, 0x80000000);	/* 128 MB of data space at 0x0. */
        mtspr(SPRN_ICCR, 0x80000000);	/* 128 MB of instr. space at 0x0. */
}

#else /* !CONFIG_4xx */
void __init MMU_init(void)
{
	if ( ppc_md.progress ) ppc_md.progress("MMU:enter", 0x111);

	total_memory = ppc_md.find_end_of_memory();

	if (__max_memory && total_memory > __max_memory)
		total_memory = __max_memory;
	total_lowmem = total_memory;
	if (total_lowmem > __max_low_memory) {
		total_lowmem = __max_low_memory;
#ifndef CONFIG_HIGHMEM
		total_memory = total_lowmem;
#endif /* CONFIG_HIGHMEM */
	}
	end_of_DRAM = __va(total_lowmem);
	set_phys_avail(total_lowmem);

#if !defined(CONFIG_8xx)
	if ( ppc_md.progress ) ppc_md.progress("MMU:hash init", 0x300);
        hash_init();
#ifndef CONFIG_PPC64BRIDGE
        _SDR1 = __pa(Hash) | (Hash_mask >> 10);
#endif
	
	ioremap_base = 0xf8000000;
#endif /* CONFIG_8xx */

	if ( ppc_md.progress ) ppc_md.progress("MMU:mapin", 0x301);
	/* Map in all of RAM starting at KERNELBASE */
	mapin_ram();

#if defined(CONFIG_POWER4)
	ioremap_base = ioremap_bot = 0xfffff000;
	isa_io_base = (unsigned long) ioremap(0xffd00000, 0x200000) + 0x100000;

#elif defined(CONFIG_8xx)
        /* Now map in some of the I/O space that is generically needed
         * or shared with multiple devices.
         * All of this fits into the same 4Mbyte region, so it only
         * requires one page table page.
         */
        ioremap(IMAP_ADDR, IMAP_SIZE);
#ifdef CONFIG_MBX
        ioremap(NVRAM_ADDR, NVRAM_SIZE);
        ioremap(MBX_CSR_ADDR, MBX_CSR_SIZE);
        ioremap(PCI_CSR_ADDR, PCI_CSR_SIZE);

	/* Map some of the PCI/ISA I/O space to get the IDE interface.
	*/
        ioremap(PCI_ISA_IO_ADDR, 0x4000);
        ioremap(PCI_IDE_ADDR, 0x4000);
#endif
#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)
	ioremap(RPX_CSR_ADDR, RPX_CSR_SIZE);
#if !defined(CONFIG_PCI)
	ioremap(_IO_BASE,_IO_BASE_SIZE);
#endif
#endif
#ifdef CONFIG_HTDMSOUND
	ioremap(HIOX_CSR_ADDR, HIOX_CSR_SIZE);
#endif
#ifdef CONFIG_FADS
	ioremap(BCSR_ADDR, BCSR_SIZE);
#endif
#ifdef CONFIG_PCI
        ioremap(PCI_CSR_ADDR, PCI_CSR_SIZE);
#endif
#else /* !CONFIG_POWER4 && !CONFIG_8xx */
	/*
	 * Setup the bat mappings we're going to load that cover
	 * the io areas.  RAM was mapped by mapin_ram().
	 * -- Cort
	 */
	if ( ppc_md.progress ) ppc_md.progress("MMU:setbat", 0x302);
	switch (_machine) {
	case _MACH_prep:
		setbat(0, 0x80000000, 0x80000000, 0x10000000, IO_PAGE);
		setbat(1, 0xf0000000, 0xc0000000, 0x08000000, IO_PAGE);
		ioremap_base = 0xf0000000;
		break;
	case _MACH_chrp:
		/*
		 * The code below tends to get removed, please don't take it out.
		 * The F50 needs this mapping and it you take it out I'll track you
		 * down and slap your hands.  If it causes problems please email me.
		 *  -- Cort <cort@fsmlabs.com>
		 */
#ifndef CONFIG_POWER3
		setbat(0, 0x80000000, 0x80000000, 0x10000000, IO_PAGE);
		setbat(1, 0x90000000, 0x90000000, 0x10000000, IO_PAGE);
#endif
		break;
	case _MACH_Pmac:
		ioremap_base = 0xfe000000;
		break;
	case _MACH_apus:
		/* Map PPC exception vectors. */
		setbat(0, 0xfff00000, 0xfff00000, 0x00020000, RAM_PAGE);
		/* Map chip and ZorroII memory */
		setbat(1, zTwoBase,   0x00000000, 0x01000000, IO_PAGE);
		break;
	case _MACH_gemini:
		setbat(0, 0xf0000000, 0xf0000000, 0x10000000, IO_PAGE);
		setbat(1, 0x80000000, 0x80000000, 0x10000000, IO_PAGE);
		break;
	case _MACH_8260:
		/* Map the IMMR, plus anything else we can cover
		 * in that upper space according to the memory controller
		 * chip select mapping.  Grab another bunch of space
		 * below that for stuff we can't cover in the upper.
		 */
		setbat(0, 0xf0000000, 0xf0000000, 0x10000000, IO_PAGE);
		setbat(1, 0xe0000000, 0xe0000000, 0x10000000, IO_PAGE);
		ioremap_base = 0xe0000000;
		break;
	}
	ioremap_bot = ioremap_base;
#endif /* CONFIG_POWER4 || CONFIG_8xx */

	if ( ppc_md.progress ) ppc_md.progress("MMU:exit", 0x211);
#ifdef CONFIG_BOOTX_TEXT
	/* Must be done last, or ppc_md.progress will die */
	if (_machine == _MACH_Pmac || _machine == _MACH_chrp)
		map_bootx_text();
#endif
}
#endif /* CONFIG_4xx */

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
void __init do_init_bootmem(void)
{
	unsigned long start, size;
	int i;

	/*
	 * Find an area to use for the bootmem bitmap.
	 * We look for the first area which is at least
	 * 128kB in length (128kB is enough for a bitmap
	 * for 4GB of memory, using 4kB pages), plus 1 page
	 * (in case the address isn't page-aligned).
	 */
	start = 0;
	size = 0;
	for (i = 0; i < phys_avail.n_regions; ++i) {
		unsigned long a = phys_avail.regions[i].address;
		unsigned long s = phys_avail.regions[i].size;
		if (s <= size)
			continue;
		start = a;
		size = s;
		if (s >= 33 * PAGE_SIZE)
			break;
	}
	start = PAGE_ALIGN(start);

	boot_mapsize = init_bootmem(start >> PAGE_SHIFT,
				    total_lowmem >> PAGE_SHIFT);

	/* remove the bootmem bitmap from the available memory */
	mem_pieces_remove(&phys_avail, start, boot_mapsize, 1);

	/* add everything in phys_avail into the bootmem map */
	for (i = 0; i < phys_avail.n_regions; ++i)
		free_bootmem(phys_avail.regions[i].address,
			     phys_avail.regions[i].size);

	init_bootmem_done = 1;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], i;

#ifdef CONFIG_HIGHMEM
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = pte_offset(pmd_offset(pgd_offset_k(PKMAP_BASE), PKMAP_BASE), PKMAP_BASE);
	map_page(KMAP_FIX_BEGIN, 0, 0);	/* XXX gross */
	kmap_pte = pte_offset(pmd_offset(pgd_offset_k(KMAP_FIX_BEGIN), KMAP_FIX_BEGIN), KMAP_FIX_BEGIN);
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	/*
	 * Grab some memory for bad_page and bad_pagetable to use.
	 */
	empty_bad_page = alloc_bootmem_pages(PAGE_SIZE);
	empty_bad_page_table = alloc_bootmem_pages(PAGE_SIZE);

	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	zones_size[ZONE_DMA] = total_lowmem >> PAGE_SHIFT;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = (total_memory - total_lowmem) >> PAGE_SHIFT;
#endif /* CONFIG_HIGHMEM */

	free_area_init(zones_size);
}

void __init mem_init(void)
{
	extern char *sysmap; 
	extern unsigned long sysmap_size;
	unsigned long addr;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
#ifdef CONFIG_HIGHMEM
	unsigned long highmem_mapnr;

	highmem_mapnr = total_lowmem >> PAGE_SHIFT;
	highmem_start_page = mem_map + highmem_mapnr;
	max_mapnr = total_memory >> PAGE_SHIFT;
	totalram_pages += max_mapnr - highmem_mapnr;
#else
	max_mapnr = max_low_pfn;
#endif /* CONFIG_HIGHMEM */

	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */

	totalram_pages += free_all_bootmem();

#ifdef CONFIG_BLK_DEV_INITRD
	/* if we are booted from BootX with an initial ramdisk,
	   make sure the ramdisk pages aren't reserved. */
	if (initrd_start) {
		for (addr = initrd_start; addr < initrd_end; addr += PAGE_SIZE)
			clear_bit(PG_reserved, &virt_to_page(addr)->flags);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

#if defined(CONFIG_ALL_PPC)	
	/* mark the RTAS pages as reserved */
	if ( rtas_data )
		for (addr = (ulong)__va(rtas_data);
		     addr < PAGE_ALIGN((ulong)__va(rtas_data)+rtas_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(virt_to_page(addr));
#endif /* defined(CONFIG_ALL_PPC) */
	if ( sysmap_size )
		for (addr = (unsigned long)sysmap;
		     addr < PAGE_ALIGN((unsigned long)sysmap+sysmap_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(virt_to_page(addr));
	
	for (addr = PAGE_OFFSET; addr < (unsigned long)end_of_DRAM;
	     addr += PAGE_SIZE) {
		if (!PageReserved(virt_to_page(addr)))
			continue;
		if (addr < (ulong) etext)
			codepages++;
		else if (addr >= (unsigned long)&__init_begin
			 && addr < (unsigned long)&__init_end)
			initpages++;
		else if (addr < (ulong) klimit)
			datapages++;
	}

#ifdef CONFIG_HIGHMEM
	{
		unsigned long pfn;

		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			struct page *page = mem_map + pfn;

			ClearPageReserved(page);
			set_bit(PG_highmem, &page->flags);
			atomic_set(&page->count, 1);
			__free_page(page);
			totalhigh_pages++;
		}
		totalram_pages += totalhigh_pages;
	}
#endif /* CONFIG_HIGHMEM */

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init, %ldk highmem)\n",
	       (unsigned long)nr_free_pages()<< (PAGE_SHIFT-10),
	       codepages<< (PAGE_SHIFT-10), datapages<< (PAGE_SHIFT-10),
	       initpages<< (PAGE_SHIFT-10),
	       (unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));
	mem_init_done = 1;
}

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
/*
 * Initialize the hash table and patch the instructions in head.S.
 */
static void __init hash_init(void)
{
	int Hash_bits, mb, mb2;
	unsigned int hmask, h;

	extern unsigned int hash_page_patch_A[], hash_page_patch_B[],
		hash_page_patch_C[], hash_page[];

#ifdef CONFIG_PPC64BRIDGE
	/* The hash table has already been allocated and initialized
	   in prom.c */
	Hash_mask = (Hash_size >> 7) - 1;
	hmask = Hash_mask >> 9;
	Hash_bits = __ilog2(Hash_size) - 7;
	mb = 25 - Hash_bits;
	if (Hash_bits > 16)
		Hash_bits = 16;
	mb2 = 25 - Hash_bits;

#else /* CONFIG_PPC64BRIDGE */

	if ( ppc_md.progress ) ppc_md.progress("hash:enter", 0x105);
	/*
	 * Allow 64k of hash table for every 16MB of memory,
	 * up to a maximum of 2MB.
	 */
	for (h = 64<<10; h < total_memory / 256 && h < (2<<20); h *= 2)
		;
	Hash_size = h;
	Hash_mask = (h >> 6) - 1;
	hmask = Hash_mask >> 10;
	Hash_bits = __ilog2(h) - 6;
	mb = 26 - Hash_bits;
	if (Hash_bits > 16)
		Hash_bits = 16;
	mb2 = 26 - Hash_bits;

	/* shrink the htab since we don't use it on 603's -- Cort */
	switch (_get_PVR()>>16) {
	case 3: /* 603 */
	case 6: /* 603e */
	case 7: /* 603ev */
	case 0x0081: /* 82xx */
		Hash_size = 0;
		Hash_mask = 0;
		break;
	default:
	        /* on 601/4 let things be */
		break;
 	}
	
	if ( ppc_md.progress ) ppc_md.progress("hash:find piece", 0x322);
	/* Find some memory for the hash table. */
	if ( Hash_size ) {
		Hash = mem_pieces_find(Hash_size, Hash_size);
		cacheable_memzero(Hash, Hash_size);
	} else
		Hash = 0;
#endif /* CONFIG_PPC64BRIDGE */

	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       total_memory >> 20, Hash_size >> 10, Hash);
	if ( Hash_size )
	{
		if ( ppc_md.progress ) ppc_md.progress("hash:patch", 0x345);
		Hash_end = (PTE *) ((unsigned long)Hash + Hash_size);

		/*
		 * Patch up the instructions in head.S:hash_page
		 */
		hash_page_patch_A[0] = (hash_page_patch_A[0] & ~0xffff)
			| (__pa(Hash) >> 16);
		hash_page_patch_A[1] = (hash_page_patch_A[1] & ~0x7c0)
			| (mb << 6);
		hash_page_patch_A[2] = (hash_page_patch_A[2] & ~0x7c0)
			| (mb2 << 6);
		hash_page_patch_B[0] = (hash_page_patch_B[0] & ~0xffff)
			| hmask;
		hash_page_patch_C[0] = (hash_page_patch_C[0] & ~0xffff)
			| hmask;
#if 0	/* see hash_page in head.S, note also patch_C ref below */
		hash_page_patch_D[0] = (hash_page_patch_D[0] & ~0xffff)
			| hmask;
#endif
		/*
		 * Ensure that the locations we've patched have been written
		 * out from the data cache and invalidated in the instruction
		 * cache, on those machines with split caches.
		 */
		flush_icache_range((unsigned long) &hash_page_patch_A[0],
				   (unsigned long) &hash_page_patch_C[1]);
	}
	else {
		Hash_end = 0;
		/*
		 * Put a blr (procedure return) instruction at the
		 * start of hash_page, since we can still get DSI
		 * exceptions on a 603.
		 */
		hash_page[0] = 0x4e800020;
		flush_icache_range((unsigned long) &hash_page[0],
				   (unsigned long) &hash_page[1]);
	}
	if ( ppc_md.progress ) ppc_md.progress("hash:done", 0x205);
}
#endif /* !CONFIG_4xx && !CONFIG_8xx */

/*
 * Set phys_avail to the amount of physical memory,
 * less the kernel text/data/bss.
 */
void __init
set_phys_avail(unsigned long total_memory)
{
	unsigned long kstart, ksize;

	/*
	 * Initially, available phyiscal memory is equivalent to all
	 * physical memory.
	 */

	phys_avail.regions[0].address = 0;
	phys_avail.regions[0].size = total_memory;
	phys_avail.n_regions = 1;

	/*
	 * Map out the kernel text/data/bss from the available physical
	 * memory.
	 */

	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);

	mem_pieces_remove(&phys_avail, kstart, ksize, 0);
	mem_pieces_remove(&phys_avail, 0, 0x4000, 0);

#if defined(CONFIG_BLK_DEV_INITRD)
	/* Remove the init RAM disk from the available memory. */
	if (initrd_start) {
		mem_pieces_remove(&phys_avail, __pa(initrd_start),
				  initrd_end - initrd_start, 1);
	}
#endif /* CONFIG_BLK_DEV_INITRD */
#ifdef CONFIG_ALL_PPC
	/* remove the RTAS pages from the available memory */
	if (rtas_data)
		mem_pieces_remove(&phys_avail, rtas_data, rtas_size, 1);
#endif /* CONFIG_ALL_PPC */
#ifdef CONFIG_PPC64BRIDGE
	/* Remove the hash table from the available memory */
	if (Hash)
		mem_pieces_remove(&phys_avail, __pa(Hash), Hash_size, 1);
#endif /* CONFIG_PPC64BRIDGE */
}
