/*
 * linux/kernel/power/swsusp.c
 *
 * This file is to realize architecture-independent
 * machine suspend feature using pretty near only high-level routines
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001-2004 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2.
 *
 * I'd like to thank the following people for their work:
 * 
 * Pavel Machek <pavel@ucw.cz>:
 * Modifications, defectiveness pointing, being with me at the very beginning,
 * suspend to swap space, stop all tasks. Port to 2.4.18-ac and 2.5.17.
 *
 * Steve Doddi <dirk@loth.demon.co.uk>: 
 * Support the possibility of hardware state restoring.
 *
 * Raph <grey.havens@earthling.net>:
 * Support for preserving states of network devices and virtual console
 * (including X and svgatextmode)
 *
 * Kurt Garloff <garloff@suse.de>:
 * Straightened the critical function in order to prevent compilers from
 * playing tricks with local variables.
 *
 * Andreas Mohr <a.mohr@mailto.de>
 *
 * Alex Badea <vampire@go.ro>:
 * Fixed runaway init
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/power/swsusp.txt
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/bitops.h>
#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/keyboard.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/swap.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/buffer_head.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/console.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include "power.h"

unsigned char software_suspend_enabled = 0;

#define NORESUME		1
#define RESUME_SPECIFIED	2

/* References to section boundaries */
extern char __nosave_begin, __nosave_end;

extern int is_head_of_free_region(struct page *);

/* Locks */
spinlock_t suspend_pagedir_lock __nosavedata = SPIN_LOCK_UNLOCKED;

/* Variables to be preserved over suspend */
static int pagedir_order_check;
static int nr_copy_pages_check;

static int resume_status;
static char resume_file[256] = "";			/* For resume= kernel option */
static dev_t resume_device;
/* Local variables that should not be affected by save */
unsigned int nr_copy_pages __nosavedata = 0;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume 

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave", 
   allocated at time of resume, that travels through memory not to
   collide with anything.

   Warning: this is even more evil than it seems. Pagedirs this file
   talks about are completely different from page directories used by
   MMU hardware.
 */
suspend_pagedir_t *pagedir_nosave __nosavedata = NULL;
static suspend_pagedir_t *pagedir_save;
static int pagedir_order __nosavedata = 0;

struct link {
	char dummy[PAGE_SIZE - sizeof(swp_entry_t)];
	swp_entry_t next;
};

union diskpage {
	union swap_header swh;
	struct link link;
	struct suspend_header sh;
};

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	512

static const char name_suspend[] = "Suspend Machine: ";
static const char name_resume[] = "Resume Machine: ";

/*
 * Debug
 */
#define	DEBUG_DEFAULT
#undef	DEBUG_PROCESS
#undef	DEBUG_SLOW
#define TEST_SWSUSP 0		/* Set to 1 to reboot instead of halt machine after suspension */

#ifdef DEBUG_DEFAULT
# define PRINTK(f, a...)	printk(f, ## a)
#else
# define PRINTK(f, a...)       	do { } while(0)
#endif

#ifdef DEBUG_SLOW
#define MDELAY(a) mdelay(a)
#else
#define MDELAY(a) do { } while(0)
#endif

/*
 * Saving part...
 */

static __inline__ int fill_suspend_header(struct suspend_header *sh)
{
	memset((char *)sh, 0, sizeof(*sh));

	sh->version_code = LINUX_VERSION_CODE;
	sh->num_physpages = num_physpages;
	strncpy(sh->machine, system_utsname.machine, 8);
	strncpy(sh->version, system_utsname.version, 20);
	/* FIXME: Is this bogus? --RR */
	sh->num_cpus = num_online_cpus();
	sh->page_size = PAGE_SIZE;
	sh->suspend_pagedir = pagedir_nosave;
	BUG_ON (pagedir_save != pagedir_nosave);
	sh->num_pbes = nr_copy_pages;
	/* TODO: needed? mounted fs' last mounted date comparison
	 * [so they haven't been mounted since last suspend.
	 * Maybe it isn't.] [we'd need to do this for _all_ fs-es]
	 */
	return 0;
}

/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

static unsigned short swapfile_used[MAX_SWAPFILES];
static unsigned short root_swap;
#define MARK_SWAP_SUSPEND 0
#define MARK_SWAP_RESUME 2

static void mark_swapfiles(swp_entry_t prev, int mode)
{
	swp_entry_t entry;
	union diskpage *cur;
	struct page *page;

	if (root_swap == 0xFFFF)  /* ignored */
		return;

	page = alloc_page(GFP_ATOMIC);
	if (!page)
		panic("Out of memory in mark_swapfiles");
	cur = page_address(page);
	/* XXX: this is dirty hack to get first page of swap file */
	entry = swp_entry(root_swap, 0);
	rw_swap_page_sync(READ, entry, page);

	if (mode == MARK_SWAP_RESUME) {
	  	if (!memcmp("S1",cur->swh.magic.magic,2))
		  	memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
		else if (!memcmp("S2",cur->swh.magic.magic,2))
			memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
		else printk("%sUnable to find suspended-data signature (%.10s - misspelled?\n", 
		      	name_resume, cur->swh.magic.magic);
	} else {
	  	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)))
		  	memcpy(cur->swh.magic.magic,"S1SUSP....",10);
		else if ((!memcmp("SWAPSPACE2",cur->swh.magic.magic,10)))
			memcpy(cur->swh.magic.magic,"S2SUSP....",10);
		else panic("\nSwapspace is not swapspace (%.10s)\n", cur->swh.magic.magic);
		cur->link.next = prev; /* prev is the first/last swap page of the resume area */
		/* link.next lies *no more* in last 4/8 bytes of magic */
	}
	rw_swap_page_sync(WRITE, entry, page);
	__free_page(page);
}

static void read_swapfiles(void) /* This is called before saving image */
{
	int i, len;
	char buff[sizeof(resume_file)], *sname;
	
	len=strlen(resume_file);
	root_swap = 0xFFFF;
	
	swap_list_lock();
	for(i=0; i<MAX_SWAPFILES; i++) {
		if (swap_info[i].flags == 0) {
			swapfile_used[i]=SWAPFILE_UNUSED;
		} else {
			if(!len) {
	    			printk(KERN_WARNING "resume= option should be used to set suspend device" );
				if(root_swap == 0xFFFF) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else
					swapfile_used[i] = SWAPFILE_IGNORED;				  
			} else {
	  			/* we ignore all swap devices that are not the resume_file */
				sname = d_path(swap_info[i].swap_file->f_dentry,
					       swap_info[i].swap_file->f_vfsmnt,
					       buff,
					       sizeof(buff));
				if (!strcmp(sname, resume_file)) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else {
#if 0
					printk( "Resume: device %s (%x != %x) ignored\n", swap_info[i].swap_file->d_name.name, swap_info[i].swap_device, resume_device );				  
#endif
				  	swapfile_used[i] = SWAPFILE_IGNORED;
				}
			}
		}
	}
	swap_list_unlock();
}

static void lock_swapdevices(void) /* This is called after saving image so modification
				      will be lost after resume... and that's what we want. */
{
	int i;

	swap_list_lock();
	for(i = 0; i< MAX_SWAPFILES; i++)
		if(swapfile_used[i] == SWAPFILE_IGNORED) {
			swap_info[i].flags ^= 0xFF; /* we make the device unusable. A new call to
						       lock_swapdevices can unlock the devices. */
		}
	swap_list_unlock();
}

/**
 *    write_suspend_image - Write entire image to disk.
 *
 *    After writing suspend signature to the disk, suspend may no
 *    longer fail: we have ready-to-run image in swap, and rollback
 *    would happen on next reboot -- corrupting data.
 *
 *    Note: The buffer we allocate to use to write the suspend header is
 *    not freed; its not needed since the system is going down anyway
 *    (plus it causes an oops and I'm lazy^H^H^H^Htoo busy).
 */
static int write_suspend_image(void)
{
	int i;
	swp_entry_t entry, prev = { 0 };
	int nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	union diskpage *cur,  *buffer = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	unsigned long address;
	struct page *page;

	if (!buffer)
		return -ENOMEM;

	printk( "Writing data to swap (%d pages): ", nr_copy_pages );
	for (i=0; i<nr_copy_pages; i++) {
		if (!(i%100))
			printk( "." );
		if (!(entry = get_swap_page()).val)
			panic("\nNot enough swapspace when writing data" );
		
		if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
			panic("\nPage %d: not enough swapspace on suspend device", i );
	    
		address = (pagedir_nosave+i)->address;
		page = virt_to_page(address);
		rw_swap_page_sync(WRITE, entry, page);
		(pagedir_nosave+i)->swap_address = entry;
	}
	printk( "|\n" );
	printk( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	for (i=0; i<nr_pgdir_pages; i++) {
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		BUG_ON ((char *) cur != (((char *) pagedir_nosave) + i*PAGE_SIZE));
		printk( "." );
		if (!(entry = get_swap_page()).val) {
			printk(KERN_CRIT "Not enough swapspace when writing pgdir\n" );
			panic("Don't know how to recover");
			free_page((unsigned long) buffer);
			return -ENOSPC;
		}

		if(swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
			panic("\nNot enough swapspace for pagedir on suspend device" );

		BUG_ON (sizeof(swp_entry_t) != sizeof(long));
		BUG_ON (PAGE_SIZE % sizeof(struct pbe));

		cur->link.next = prev;				
		page = virt_to_page((unsigned long)cur);
		rw_swap_page_sync(WRITE, entry, page);
		prev = entry;
	}
	printk("H");
	BUG_ON (sizeof(struct suspend_header) > PAGE_SIZE-sizeof(swp_entry_t));
	BUG_ON (sizeof(union diskpage) != PAGE_SIZE);
	BUG_ON (sizeof(struct link) != PAGE_SIZE);
	if (!(entry = get_swap_page()).val)
		panic( "\nNot enough swapspace when writing header" );
	if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
		panic("\nNot enough swapspace for header on suspend device" );

	cur = (void *) buffer;
	if (fill_suspend_header(&cur->sh))
		BUG();		/* Not a BUG_ON(): we want fill_suspend_header to be called, always */
		
	cur->link.next = prev;

	page = virt_to_page((unsigned long)cur);
	rw_swap_page_sync(WRITE, entry, page);
	prev = entry;

	printk( "S" );
	mark_swapfiles(prev, MARK_SWAP_SUSPEND);
	printk( "|\n" );

	MDELAY(1000);
	return 0;
}

struct highmem_page {
	char *data;
	struct page *page;
	struct highmem_page *next;
};

struct highmem_page *highmem_copy = NULL;

static void save_highmem_zone(struct zone *zone)
{
	unsigned long zone_pfn;
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		struct highmem_page *save;
		void *kaddr;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;
		int chunk_size;

		if (!(pfn%200))
			printk(".");
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		/*
		 * This condition results from rvmalloc() sans vmalloc_32()
		 * and architectural memory reservations. This should be
		 * corrected eventually when the cases giving rise to this
		 * are better understood.
		 */
		if (PageReserved(page)) {
			printk("highmem reserved page?!\n");
			BUG();
		}
		if ((chunk_size = is_head_of_free_region(page))) {
			pfn += chunk_size - 1;
			zone_pfn += chunk_size - 1;
			continue;
		}
		save = kmalloc(sizeof(struct highmem_page), GFP_ATOMIC);
		if (!save)
			panic("Not enough memory");
		save->next = highmem_copy;
		save->page = page;
		save->data = (void *) get_zeroed_page(GFP_ATOMIC);
		if (!save->data)
			panic("Not enough memory");
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(save->data, kaddr, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		highmem_copy = save;
	}
}

static void save_highmem(void)
{
	struct zone *zone;
	for_each_zone(zone) {
		if (is_highmem(zone))
			save_highmem_zone(zone);
	}
}

static int restore_highmem(void)
{
	while (highmem_copy) {
		struct highmem_page *save = highmem_copy;
		void *kaddr;
		highmem_copy = save->next;

		kaddr = kmap_atomic(save->page, KM_USER0);
		memcpy(kaddr, save->data, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		free_page((long) save->data);
		kfree(save);
	}
	return 0;
}

static int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

/* if *pagedir_p != NULL it also copies the counted pages */
static int count_and_copy_zone(struct zone *zone, struct pbe **pagedir_p)
{
	unsigned long zone_pfn, chunk_size, nr_copy_pages = 0;
	struct pbe *pbe = *pagedir_p;
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;

		if (!(pfn%200))
			printk(".");
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		BUG_ON(PageReserved(page) && PageNosave(page));
		if (PageNosave(page))
			continue;
		if (PageReserved(page) && pfn_is_nosave(pfn)) {
			PRINTK("[nosave pfn 0x%lx]", pfn);
			continue;
		}
		if ((chunk_size = is_head_of_free_region(page))) {
			pfn += chunk_size - 1;
			zone_pfn += chunk_size - 1;
			continue;
		}
		nr_copy_pages++;
		if (!pbe)
			continue;
		pbe->orig_address = (long) page_address(page);
		copy_page((void *)pbe->address, (void *)pbe->orig_address);
		pbe++;
	}
	*pagedir_p = pbe;
	return nr_copy_pages;
}

static int count_and_copy_data_pages(struct pbe *pagedir_p)
{
	int nr_copy_pages = 0;
	struct zone *zone;
	for_each_zone(zone) {
		if (!is_highmem(zone))
			nr_copy_pages += count_and_copy_zone(zone, &pagedir_p);
	}
	return nr_copy_pages;
}

static void free_suspend_pagedir_zone(struct zone *zone, unsigned long pagedir)
{
	unsigned long zone_pfn, pagedir_end, pagedir_pfn, pagedir_end_pfn;
	pagedir_end = pagedir + (PAGE_SIZE << pagedir_order);
	pagedir_pfn = __pa(pagedir) >> PAGE_SHIFT;
	pagedir_end_pfn = __pa(pagedir_end) >> PAGE_SHIFT;
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		if (!TestClearPageNosave(page))
			continue;
		else if (pfn >= pagedir_pfn && pfn < pagedir_end_pfn)
			continue;
		__free_page(page);
	}
}

static void free_suspend_pagedir(unsigned long this_pagedir)
{
	struct zone *zone;
	for_each_zone(zone) {
		if (!is_highmem(zone))
			free_suspend_pagedir_zone(zone, this_pagedir);
	}
	free_pages(this_pagedir, pagedir_order);
}

static suspend_pagedir_t *create_suspend_pagedir(int nr_copy_pages)
{
	int i;
	suspend_pagedir_t *pagedir;
	struct pbe *p;
	struct page *page;

	pagedir_order = get_bitmask_order(SUSPEND_PD_PAGES(nr_copy_pages));

	p = pagedir = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC | __GFP_COLD, pagedir_order);
	if (!pagedir)
		return NULL;

	page = virt_to_page(pagedir);
	for(i=0; i < 1<<pagedir_order; i++)
		SetPageNosave(page++);
		
	while(nr_copy_pages--) {
		p->address = get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
		if(!p->address) {
			free_suspend_pagedir((unsigned long) pagedir);
			return NULL;
		}
		SetPageNosave(virt_to_page(p->address));
		p->orig_address = 0;
		p++;
	}
	return pagedir;
}

static int prepare_suspend_processes(void)
{
	sys_sync();	/* Syncing needs pdflushd, so do it before stopping processes */
	if (freeze_processes()) {
		printk( KERN_ERR "Suspend failed: Not all processes stopped!\n" );
		thaw_processes();
		return 1;
	}
	return 0;
}

/*
 * Try to free as much memory as possible, but do not OOM-kill anyone
 *
 * Notice: all userland should be stopped at this point, or livelock is possible.
 */
static void free_some_memory(void)
{
	printk("Freeing memory: ");
	while (shrink_all_memory(10000))
		printk(".");
	printk("|\n");
}

static int suspend_prepare_image(void)
{
	struct sysinfo i;
	unsigned int nr_needed_pages = 0;

	pagedir_nosave = NULL;
	printk( "/critical section: Handling highmem" );
	save_highmem();

	printk(", counting pages to copy" );
	drain_local_pages();
	nr_copy_pages = count_and_copy_data_pages(NULL);
	nr_needed_pages = nr_copy_pages + PAGES_FOR_IO;
	
	printk(" (pages needed: %d+%d=%d free: %d)\n",nr_copy_pages,PAGES_FOR_IO,nr_needed_pages,nr_free_pages());
	if(nr_free_pages() < nr_needed_pages) {
		printk(KERN_CRIT "%sCouldn't get enough free pages, on %d pages short\n",
		       name_suspend, nr_needed_pages-nr_free_pages());
		root_swap = 0xFFFF;
		return 1;
	}
	si_swapinfo(&i);	/* FIXME: si_swapinfo(&i) returns all swap devices information.
				   We should only consider resume_device. */
	if (i.freeswap < nr_needed_pages)  {
		printk(KERN_CRIT "%sThere's not enough swap space available, on %ld pages short\n",
		       name_suspend, nr_needed_pages-i.freeswap);
		return 1;
	}

	PRINTK( "Alloc pagedir\n" ); 
	pagedir_save = pagedir_nosave = create_suspend_pagedir(nr_copy_pages);
	if(!pagedir_nosave) {
		/* Shouldn't happen */
		printk(KERN_CRIT "%sCouldn't allocate enough pages\n",name_suspend);
		panic("Really should not happen");
		return 1;
	}
	nr_copy_pages_check = nr_copy_pages;
	pagedir_order_check = pagedir_order;

	drain_local_pages();	/* During allocating of suspend pagedir, new cold pages may appear. Kill them */
	if (nr_copy_pages != count_and_copy_data_pages(pagedir_nosave))	/* copy */
		BUG();

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	printk( "critical section/: done (%d pages copied)\n", nr_copy_pages );
	return 0;
}

static void suspend_save_image(void)
{
	device_resume();

	lock_swapdevices();
	write_suspend_image();
	lock_swapdevices();	/* This will unlock ignored swap devices since writing is finished */

	/* It is important _NOT_ to umount filesystems at this point. We want
	 * them synced (in case something goes wrong) but we DO not want to mark
	 * filesystem clean: it is not. (And it does not matter, if we resume
	 * correctly, we'll mark system clean, anyway.)
	 */
}

static void suspend_power_down(void)
{
	extern int C_A_D;
	C_A_D = 0;
	printk(KERN_EMERG "%s%s Trying to power down.\n", name_suspend, TEST_SWSUSP ? "Disable TEST_SWSUSP. NOT ": "");
#ifdef CONFIG_VT
	PRINTK(KERN_EMERG "shift_state: %04x\n", shift_state);
	mdelay(1000);
	if (TEST_SWSUSP ^ (!!(shift_state & (1 << KG_CTRL))))
		machine_restart(NULL);
	else
#endif
	{
		device_shutdown();
		machine_power_off();
	}

	printk(KERN_EMERG "%sProbably not capable for powerdown. System halted.\n", name_suspend);
	machine_halt();
	while (1);
	/* NOTREACHED */
}

/*
 * Magic happens here
 */

asmlinkage void do_magic_resume_1(void)
{
	barrier();
	mb();
	spin_lock_irq(&suspend_pagedir_lock);	/* Done to disable interrupts */ 

	device_power_down(4);
	PRINTK( "Waiting for DMAs to settle down...\n");
	mdelay(1000);	/* We do not want some readahead with DMA to corrupt our memory, right?
			   Do it with disabled interrupts for best effect. That way, if some
			   driver scheduled DMA, we have good chance for DMA to finish ;-). */
}

asmlinkage void do_magic_resume_2(void)
{
	BUG_ON (nr_copy_pages_check != nr_copy_pages);
	BUG_ON (pagedir_order_check != pagedir_order);

	__flush_tlb_global();		/* Even mappings of "global" things (vmalloc) need to be fixed */

	PRINTK( "Freeing prev allocated pagedir\n" );
	free_suspend_pagedir((unsigned long) pagedir_save);

	printk( "Restoring highmem\n" );
	restore_highmem();
	printk("done, devices\n");

	device_power_up();
	spin_unlock_irq(&suspend_pagedir_lock);
	device_resume();

	/* Fixme: this is too late; we should do this ASAP to avoid "infinite reboots" problem */
	PRINTK( "Fixing swap signatures... " );
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	PRINTK( "ok\n" );

#ifdef SUSPEND_CONSOLE
	acquire_console_sem();
	update_screen(fg_console);
	release_console_sem();
#endif
}

/* do_magic() is implemented in arch/?/kernel/suspend_asm.S, and basically does:

	if (!resume) {
		do_magic_suspend_1();
		save_processor_state();
		SAVE_REGISTERS
		do_magic_suspend_2();
		return;
	}
	GO_TO_SWAPPER_PAGE_TABLES
	do_magic_resume_1();
	COPY_PAGES_BACK
	RESTORE_REGISTERS
	restore_processor_state();
	do_magic_resume_2();

 */

asmlinkage void do_magic_suspend_1(void)
{
	mb();
	barrier();
	BUG_ON(in_atomic());
	spin_lock_irq(&suspend_pagedir_lock);
}

asmlinkage void do_magic_suspend_2(void)
{
	int is_problem;
	read_swapfiles();
	device_power_down(4);
	is_problem = suspend_prepare_image();
	device_power_up();
	spin_unlock_irq(&suspend_pagedir_lock);
	if (!is_problem) {
		kernel_fpu_end();	/* save_processor_state() does kernel_fpu_begin, and we need to revert it in order to pass in_atomic() checks */
		BUG_ON(in_atomic());
		suspend_save_image();
		suspend_power_down();	/* FIXME: if suspend_power_down is commented out, console is lost after few suspends ?! */
	}

	printk(KERN_EMERG "%sSuspend failed, trying to recover...\n", name_suspend);
	MDELAY(1000); /* So user can wait and report us messages if armageddon comes :-) */

	barrier();
	mb();
	spin_lock_irq(&suspend_pagedir_lock);	/* Done to disable interrupts */ 
	mdelay(1000);

	free_pages((unsigned long) pagedir_nosave, pagedir_order);
	spin_unlock_irq(&suspend_pagedir_lock);

	device_resume();
	PRINTK( "Fixing swap signatures... " );
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	PRINTK( "ok\n" );
}

/*
 * This is main interface to the outside world. It needs to be
 * called from process context.
 */
int software_suspend(void)
{
	int res;
	if (!software_suspend_enabled)
		return -EAGAIN;

	software_suspend_enabled = 0;
	might_sleep();

	if (arch_prepare_suspend()) {
		printk("%sArchitecture failed to prepare\n", name_suspend);
		return -EPERM;
	}		
	if (pm_prepare_console())
		printk( "%sCan't allocate a console... proceeding\n", name_suspend);
	if (!prepare_suspend_processes()) {

		/* At this point, all user processes and "dangerous"
                   kernel threads are stopped. Free some memory, as we
                   need half of memory free. */

		free_some_memory();
		
		/* Save state of all device drivers, and stop them. */		   
		if ((res = device_suspend(4))==0)
			/* If stopping device drivers worked, we proceed basically into
			 * suspend_save_image.
			 *
			 * do_magic(0) returns after system is resumed.
			 *
			 * do_magic() copies all "used" memory to "free" memory, then
			 * unsuspends all device drivers, and writes memory to disk
			 * using normal kernel mechanism.
			 */
			do_magic(0);
		thaw_processes();
	} else
		res = -EBUSY;
	software_suspend_enabled = 1;
	MDELAY(1000);
	pm_restore_console();
	return res;
}

/* More restore stuff */

/* FIXME: Why not memcpy(to, from, 1<<pagedir_order*PAGE_SIZE)? */
static void copy_pagedir(suspend_pagedir_t *to, suspend_pagedir_t *from)
{
	int i;
	char *topointer=(char *)to, *frompointer=(char *)from;

	for(i=0; i < 1 << pagedir_order; i++) {
		copy_page(topointer, frompointer);
		topointer += PAGE_SIZE;
		frompointer += PAGE_SIZE;
	}
}

#define does_collide(addr) does_collide_order(pagedir_nosave, addr, 0)

/*
 * Returns true if given address/order collides with any orig_address 
 */
static int does_collide_order(suspend_pagedir_t *pagedir, unsigned long addr,
		int order)
{
	int i;
	unsigned long addre = addr + (PAGE_SIZE<<order);
	
	for(i=0; i < nr_copy_pages; i++)
		if((pagedir+i)->orig_address >= addr &&
			(pagedir+i)->orig_address < addre)
			return 1;

	return 0;
}

/*
 * We check here that pagedir & pages it points to won't collide with pages
 * where we're going to restore from the loaded pages later
 */
static int check_pagedir(void)
{
	int i;

	for(i=0; i < nr_copy_pages; i++) {
		unsigned long addr;

		do {
			addr = get_zeroed_page(GFP_ATOMIC);
			if(!addr)
				return -ENOMEM;
		} while (does_collide(addr));

		(pagedir_nosave+i)->address = addr;
	}
	return 0;
}

static int relocate_pagedir(void)
{
	/*
	 * We have to avoid recursion (not to overflow kernel stack),
	 * and that's why code looks pretty cryptic 
	 */
	suspend_pagedir_t *new_pagedir, *old_pagedir = pagedir_nosave;
	void **eaten_memory = NULL;
	void **c = eaten_memory, *m, *f;

	printk("Relocating pagedir");

	if(!does_collide_order(old_pagedir, (unsigned long)old_pagedir, pagedir_order)) {
		printk("not necessary\n");
		return 0;
	}

	while ((m = (void *) __get_free_pages(GFP_ATOMIC, pagedir_order))) {
		memset(m, 0, PAGE_SIZE);
		if (!does_collide_order(old_pagedir, (unsigned long)m, pagedir_order))
			break;
		eaten_memory = m;
		printk( "." ); 
		*eaten_memory = c;
		c = eaten_memory;
	}

	if (!m)
		return -ENOMEM;

	pagedir_nosave = new_pagedir = m;
	copy_pagedir(new_pagedir, old_pagedir);

	c = eaten_memory;
	while(c) {
		printk(":");
		f = *c;
		c = *c;
		if (f)
			free_pages((unsigned long)f, pagedir_order);
	}
	printk("|\n");
	return 0;
}

/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static int sanity_check_failed(char *reason)
{
	printk(KERN_ERR "%s%s\n", name_resume, reason);
	return -EPERM;
}

static int sanity_check(struct suspend_header *sh)
{
	if (sh->version_code != LINUX_VERSION_CODE)
		return sanity_check_failed("Incorrect kernel version");
	if (sh->num_physpages != num_physpages)
		return sanity_check_failed("Incorrect memory size");
	if (strncmp(sh->machine, system_utsname.machine, 8))
		return sanity_check_failed("Incorrect machine type");
	if (strncmp(sh->version, system_utsname.version, 20))
		return sanity_check_failed("Incorrect version");
	if (sh->num_cpus != num_online_cpus())
		return sanity_check_failed("Incorrect number of cpus");
	if (sh->page_size != PAGE_SIZE)
		return sanity_check_failed("Incorrect PAGE_SIZE");
	return 0;
}

static int bdev_read_page(struct block_device *bdev, long pos, void *buf)
{
	struct buffer_head *bh;
	BUG_ON (pos%PAGE_SIZE);
	bh = __bread(bdev, pos/PAGE_SIZE, PAGE_SIZE);
	if (!bh || (!bh->b_data)) {
		return -1;
	}
	memcpy(buf, bh->b_data, PAGE_SIZE);	/* FIXME: may need kmap() */
	BUG_ON(!buffer_uptodate(bh));
	brelse(bh);
	return 0;
} 

static int bdev_write_page(struct block_device *bdev, long pos, void *buf)
{
#if 0
	struct buffer_head *bh;
	BUG_ON (pos%PAGE_SIZE);
	bh = __bread(bdev, pos/PAGE_SIZE, PAGE_SIZE);
	if (!bh || (!bh->b_data)) {
		return -1;
	}
	memcpy(bh->b_data, buf, PAGE_SIZE);	/* FIXME: may need kmap() */
	BUG_ON(!buffer_uptodate(bh));
	generic_make_request(WRITE, bh);
	if (!buffer_uptodate(bh))
		printk(KERN_CRIT "%sWarning %s: Fixing swap signatures unsuccessful...\n", name_resume, resume_file);
	wait_on_buffer(bh);
	brelse(bh);
	return 0;
#endif
	printk(KERN_CRIT "%sWarning %s: Fixing swap signatures unimplemented...\n", name_resume, resume_file);
	return 0;
}

extern dev_t __init name_to_dev_t(const char *line);

static int __init __read_suspend_image(struct block_device *bdev, union diskpage *cur, int noresume)
{
	swp_entry_t next;
	int i, nr_pgdir_pages;

#define PREPARENEXT \
	{	next = cur->link.next; \
		next.val = swp_offset(next) * PAGE_SIZE; \
        }

	if (bdev_read_page(bdev, 0, cur)) return -EIO;

	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)) ||
	    (!memcmp("SWAPSPACE2",cur->swh.magic.magic,10))) {
		printk(KERN_ERR "%sThis is normal swap space\n", name_resume );
		return -EINVAL;
	}

	PREPARENEXT; /* We have to read next position before we overwrite it */

	if (!memcmp("S1",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
	else if (!memcmp("S2",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
	else {
		if (noresume)
			return -EINVAL;
		panic("%sUnable to find suspended-data signature (%.10s - misspelled?\n", 
			name_resume, cur->swh.magic.magic);
	}
	if (noresume) {
		/* We don't do a sanity check here: we want to restore the swap
		   whatever version of kernel made the suspend image;
		   We need to write swap, but swap is *not* enabled so
		   we must write the device directly */
		printk("%s: Fixing swap signatures %s...\n", name_resume, resume_file);
		bdev_write_page(bdev, 0, cur);
	}

	printk( "%sSignature found, resuming\n", name_resume );
	MDELAY(1000);

	if (bdev_read_page(bdev, next.val, cur)) return -EIO;
	if (sanity_check(&cur->sh)) 	/* Is this same machine? */	
		return -EPERM;
	PREPARENEXT;

	pagedir_save = cur->sh.suspend_pagedir;
	nr_copy_pages = cur->sh.num_pbes;
	nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	pagedir_order = get_bitmask_order(nr_pgdir_pages);

	pagedir_nosave = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if (!pagedir_nosave)
		return -ENOMEM;

	PRINTK( "%sReading pagedir, ", name_resume );

	/* We get pages in reverse order of saving! */
	for (i=nr_pgdir_pages-1; i>=0; i--) {
		BUG_ON (!next.val);
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		if (bdev_read_page(bdev, next.val, cur)) return -EIO;
		PREPARENEXT;
	}
	BUG_ON (next.val);

	if (relocate_pagedir())
		return -ENOMEM;
	if (check_pagedir())
		return -ENOMEM;

	printk( "Reading image data (%d pages): ", nr_copy_pages );
	for(i=0; i < nr_copy_pages; i++) {
		swp_entry_t swap_address = (pagedir_nosave+i)->swap_address;
		if (!(i%100))
			printk( "." );
		/* You do not need to check for overlaps...
		   ... check_pagedir already did this work */
		if (bdev_read_page(bdev, swp_offset(swap_address) * PAGE_SIZE, (char *)((pagedir_nosave+i)->address)))
			return -EIO;
	}
	printk( "|\n" );
	return 0;
}

static int read_suspend_image(const char * specialfile, int noresume)
{
	union diskpage *cur;
	unsigned long scratch_page = 0;
	int error;
	char b[BDEVNAME_SIZE];

	resume_device = name_to_dev_t(specialfile);
	scratch_page = get_zeroed_page(GFP_ATOMIC);
	cur = (void *) scratch_page;
	if (cur) {
		struct block_device *bdev;
		printk("Resuming from device %s\n",
				__bdevname(resume_device, b));
		bdev = open_by_devnum(resume_device, FMODE_READ);
		if (IS_ERR(bdev)) {
			error = PTR_ERR(bdev);
		} else {
			set_blocksize(bdev, PAGE_SIZE);
			error = __read_suspend_image(bdev, cur, noresume);
			blkdev_put(bdev);
		}
	} else error = -ENOMEM;

	if (scratch_page)
		free_page(scratch_page);
	switch (error) {
		case 0:
			PRINTK("Reading resume file was successful\n");
			break;
		case -EINVAL:
			break;
		case -EIO:
			printk( "%sI/O error\n", name_resume);
			break;
		case -ENOENT:
			printk( "%s%s: No such file or directory\n", name_resume, specialfile);
			break;
		case -ENOMEM:
			printk( "%sNot enough memory\n", name_resume);
			break;
		default:
			printk( "%sError %d resuming\n", name_resume, error );
	}
	MDELAY(1000);
	return error;
}

/**
 *	software_resume - Resume from a saved image.
 *
 *	Called as a late_initcall (so all devices are discovered and 
 *	initialized), we call swsusp to see if we have a saved image or not.
 *	If so, we quiesce devices, then restore the saved image. We will 
 *	return above (in pm_suspend_disk() ) if everything goes well. 
 *	Otherwise, we fail gracefully and return to the normally 
 *	scheduled program.
 *
 */
static int __init software_resume(void)
{
	if (num_online_cpus() > 1) {
		printk(KERN_WARNING "Software Suspend has malfunctioning SMP support. Disabled :(\n");	
		return -EINVAL;
	}
	/* We enable the possibility of machine suspend */
	software_suspend_enabled = 1;
	if (!resume_status)
		return 0;

	printk( "%s", name_resume );
	if (resume_status == NORESUME) {
		if(resume_file[0])
			read_suspend_image(resume_file, 1);
		printk( "disabled\n" );
		return 0;
	}
	MDELAY(1000);

	if (pm_prepare_console())
		printk("swsusp: Can't allocate a console... proceeding\n");

	if (!resume_file[0] && resume_status == RESUME_SPECIFIED) {
		printk( "suspension device unspecified\n" );
		return -EINVAL;
	}

	printk( "resuming from %s\n", resume_file);
	if (read_suspend_image(resume_file, 0))
		goto read_failure;
	device_suspend(4);
	do_magic(1);
	panic("This never returns");

read_failure:
	pm_restore_console();
	return 0;
}

late_initcall(software_resume);

static int __init resume_setup(char *str)
{
	if (resume_status == NORESUME)
		return 1;

	strncpy( resume_file, str, 255 );
	resume_status = RESUME_SPECIFIED;

	return 1;
}

static int __init noresume_setup(char *str)
{
	resume_status = NORESUME;
	return 1;
}

__setup("noresume", noresume_setup);
__setup("resume=", resume_setup);

EXPORT_SYMBOL(software_suspend);
EXPORT_SYMBOL(software_suspend_enabled);
