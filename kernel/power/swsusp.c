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
#include <linux/bio.h>

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
int pagedir_order_check;
int nr_copy_pages_check;

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
suspend_pagedir_t *pagedir_save;
int pagedir_order __nosavedata = 0;

struct swsusp_info swsusp_info;

struct link {
	char dummy[PAGE_SIZE - sizeof(swp_entry_t)];
	swp_entry_t next;
};

union diskpage {
	union swap_header swh;
	struct link link;
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

/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

unsigned short swapfile_used[MAX_SWAPFILES];
unsigned short root_swap;
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


/*
 * Check whether the swap device is the specified resume
 * device, irrespective of whether they are specified by
 * identical names.
 *
 * (Thus, device inode aliasing is allowed.  You can say /dev/hda4
 * instead of /dev/ide/host0/bus0/target0/lun0/part4 [if using devfs]
 * and they'll be considered the same device.  This is *necessary* for
 * devfs, since the resume code can only recognize the form /dev/hda4,
 * but the suspend code would see the long name.)
 */
static int is_resume_device(const struct swap_info_struct *swap_info)
{
	struct file *file = swap_info->swap_file;
	struct inode *inode = file->f_dentry->d_inode;

	return S_ISBLK(inode->i_mode) &&
		resume_device == MKDEV(imajor(inode), iminor(inode));
}

int swsusp_swap_check(void) /* This is called before saving image */
{
	int i, len;
	
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
				if (is_resume_device(&swap_info[i])) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else {
				  	swapfile_used[i] = SWAPFILE_IGNORED;
				}
			}
		}
	}
	swap_list_unlock();
	return (root_swap != 0xffff) ? 0 : -ENODEV;
}

/**
 * This is called after saving image so modification
 * will be lost after resume... and that's what we want.
 * we make the device unusable. A new call to
 * lock_swapdevices can unlock the devices. 
 */
void swsusp_swap_lock(void)
{
	int i;

	swap_list_lock();
	for(i = 0; i< MAX_SWAPFILES; i++)
		if(swapfile_used[i] == SWAPFILE_IGNORED) {
			swap_info[i].flags ^= 0xFF;
		}
	swap_list_unlock();
}



/**
 *	write_swap_page - Write one page to a fresh swap location.
 *	@addr:	Address we're writing.
 *	@loc:	Place to store the entry we used.
 *
 *	Allocate a new swap entry and 'sync' it. Note we discard -EIO
 *	errors. That is an artifact left over from swsusp. It did not 
 *	check the return of rw_swap_page_sync() at all, since most pages
 *	written back to swap would return -EIO.
 *	This is a partial improvement, since we will at least return other
 *	errors, though we need to eventually fix the damn code.
 */

int swsusp_write_page(unsigned long addr, swp_entry_t * loc)
{
	swp_entry_t entry;
	int error = 0;

	entry = get_swap_page();
	if (swp_offset(entry) && 
	    swapfile_used[swp_type(entry)] == SWAPFILE_SUSPEND) {
		error = rw_swap_page_sync(WRITE, entry,
					  virt_to_page(addr));
		if (error == -EIO)
			error = 0;
		if (!error)
			*loc = entry;
	} else
		error = -ENOSPC;
	return error;
}


/**
 *	free_data - Free the swap entries used by the saved image.
 *
 *	Walk the list of used swap entries and free each one. 
 */

void swsusp_data_free(void)
{
	swp_entry_t entry;
	int i;

	for (i = 0; i < nr_copy_pages; i++) {
		entry = (pagedir_nosave + i)->swap_address;
		if (entry.val)
			swap_free(entry);
		else
			break;
		(pagedir_nosave + i)->swap_address = (swp_entry_t){0};
	}
}


/**
 *	write_data - Write saved image to swap.
 *
 *	Walk the list of pages in the image and sync each one to swap.
 */

int swsusp_data_write(void)
{
	int error = 0;
	int i;

	printk( "Writing data to swap (%d pages): ", nr_copy_pages );
	for (i = 0; i < nr_copy_pages && !error; i++) {
		if (!(i%100))
			printk( "." );
		error = swsusp_write_page((pagedir_nosave+i)->address,
					  &((pagedir_nosave+i)->swap_address));
	}
	printk(" %d Pages done.\n",i);
	return error;
}

#ifdef DEBUG
static void dump_info(void)
{
	printk(" swsusp: Version: %u\n",swsusp_info.version_code);
	printk(" swsusp: Num Pages: %ld\n",swsusp_info.num_physpages);
	printk(" swsusp: UTS Sys: %s\n",swsusp_info.uts.sysname);
	printk(" swsusp: UTS Node: %s\n",swsusp_info.uts.nodename);
	printk(" swsusp: UTS Release: %s\n",swsusp_info.uts.release);
	printk(" swsusp: UTS Version: %s\n",swsusp_info.uts.version);
	printk(" swsusp: UTS Machine: %s\n",swsusp_info.uts.machine);
	printk(" swsusp: UTS Domain: %s\n",swsusp_info.uts.domainname);
	printk(" swsusp: CPUs: %d\n",swsusp_info.cpus);
	printk(" swsusp: Image: %ld Pages\n",swsusp_info.image_pages);
	printk(" swsusp: Pagedir: %ld Pages\n",swsusp_info.pagedir_pages);
}
#else
static void dump_info(void)
{

}
#endif

void swsusp_init_header(void)
{
	memset(&swsusp_info,0,sizeof(swsusp_info));
	swsusp_info.version_code = LINUX_VERSION_CODE;
	swsusp_info.num_physpages = num_physpages;
	memcpy(&swsusp_info.uts,&system_utsname,sizeof(system_utsname));

	swsusp_info.suspend_pagedir = pagedir_nosave;
	swsusp_info.cpus = num_online_cpus();
	swsusp_info.image_pages = nr_copy_pages;
	dump_info();
}

/**
 *	write_header - Fill and write the suspend header.
 *	@entry:	Location of the last swap entry used.
 *
 *	Allocate a page, fill header, write header. 
 *
 *	@entry is the location of the last pagedir entry written on 
 *	entrance. On exit, it contains the location of the header. 
 */

int swsusp_write_header(swp_entry_t * entry)
{
	return swsusp_write_page((unsigned long)&swsusp_info,entry);
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
	int error = 0;
	swp_entry_t entry = { 0 };
	int nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	union diskpage *cur,  *buffer = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	unsigned long addr;

	if (!buffer)
		return -ENOMEM;

	swsusp_data_write();

	printk( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	addr = (unsigned long)pagedir_nosave;
	for (i=0; i<nr_pgdir_pages && !error; i++, addr += PAGE_SIZE) {
		cur = (union diskpage *)addr;
		cur->link.next = entry;
		printk( "." );
		error = swsusp_write_page(addr,&entry);
	}
	printk("H");
	BUG_ON (sizeof(union diskpage) != PAGE_SIZE);
	BUG_ON (sizeof(struct link) != PAGE_SIZE);

	swsusp_init_header();
	swsusp_info.pagedir[0] = entry;
	error = swsusp_write_header(&entry);
	printk( "S" );
	mark_swapfiles(entry, MARK_SWAP_SUSPEND);
	printk( "|\n" );

	MDELAY(1000);
	return 0;
}

#ifdef CONFIG_HIGHMEM
struct highmem_page {
	char *data;
	struct page *page;
	struct highmem_page *next;
};

struct highmem_page *highmem_copy = NULL;

static int save_highmem_zone(struct zone *zone)
{
	unsigned long zone_pfn;
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		struct highmem_page *save;
		void *kaddr;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;
		int chunk_size;

		if (!(pfn%1000))
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
			continue;
		}
		if ((chunk_size = is_head_of_free_region(page))) {
			pfn += chunk_size - 1;
			zone_pfn += chunk_size - 1;
			continue;
		}
		save = kmalloc(sizeof(struct highmem_page), GFP_ATOMIC);
		if (!save)
			return -ENOMEM;
		save->next = highmem_copy;
		save->page = page;
		save->data = (void *) get_zeroed_page(GFP_ATOMIC);
		if (!save->data) {
			kfree(save);
			return -ENOMEM;
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(save->data, kaddr, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		highmem_copy = save;
	}
	return 0;
}

static int save_highmem(void)
{
	struct zone *zone;
	int res = 0;
	for_each_zone(zone) {
		if (is_highmem(zone))
			res = save_highmem_zone(zone);
		if (res)
			return res;
	}
	return 0;
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
#endif

static int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

/**
 *	saveable - Determine whether a page should be cloned or not.
 *	@pfn:	The page
 *
 *	We save a page if it's Reserved, and not in the range of pages
 *	statically defined as 'unsaveable', or if it isn't reserved, and
 *	isn't part of a free chunk of pages.
 *	If it is part of a free chunk, we update @pfn to point to the last 
 *	page of the chunk.
 */

static int saveable(struct zone * zone, unsigned long * zone_pfn)
{
	unsigned long pfn = *zone_pfn + zone->zone_start_pfn;
	unsigned long chunk_size;
	struct page * page;

	if (!pfn_valid(pfn))
		return 0;

	if (!(pfn%1000))
		printk(".");
	page = pfn_to_page(pfn);
	BUG_ON(PageReserved(page) && PageNosave(page));
	if (PageNosave(page))
		return 0;
	if (PageReserved(page) && pfn_is_nosave(pfn)) {
		PRINTK("[nosave pfn 0x%lx]", pfn);
		return 0;
	}
	if ((chunk_size = is_head_of_free_region(page))) {
		zone_pfn += chunk_size - 1;
		return 0;
	}

	return 1;
}

static void count_data_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;

	nr_copy_pages = 0;

	for_each_zone(zone) {
		if (!is_highmem(zone)) {
			for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
				nr_copy_pages += saveable(zone, &zone_pfn);
		}
	}
}


static void copy_data_pages(struct pbe * pbe)
{
	struct zone *zone;
	unsigned long zone_pfn;

	
	for_each_zone(zone) {
		if (!is_highmem(zone))
			for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
				if (saveable(zone, &zone_pfn)) {
					struct page * page;
					page = pfn_to_page(zone_pfn + zone->zone_start_pfn);
					pbe->orig_address = (long) page_address(page);
					/* Copy page is dangerous: it likes to mess with
					   preempt count on specific cpus. Wrong preempt 
					   count is then copied, oops. 
					*/
					copy_page((void *)pbe->address, 
						  (void *)pbe->orig_address);
					pbe++;
				}
			}
	}
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

void free_suspend_pagedir(unsigned long this_pagedir)
{
	struct zone *zone;
	for_each_zone(zone) {
		if (!is_highmem(zone))
			free_suspend_pagedir_zone(zone, this_pagedir);
	}
	free_pages(this_pagedir, pagedir_order);
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


static void calc_order(void)
{
	int diff;
	int order;

	order = get_bitmask_order(SUSPEND_PD_PAGES(nr_copy_pages));
	nr_copy_pages += 1 << order;
	do {
		diff = get_bitmask_order(SUSPEND_PD_PAGES(nr_copy_pages)) - order;
		if (diff) {
			order += diff;
			nr_copy_pages += 1 << diff;
		}
	} while(diff);
	pagedir_order = order;
}


/**
 *	alloc_pagedir - Allocate the page directory.
 *
 *	First, determine exactly how many contiguous pages we need, 
 *	allocate them, then mark each 'unsavable'.
 */

static int alloc_pagedir(void)
{
	calc_order();
	pagedir_save = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC | __GFP_COLD, 
							     pagedir_order);
	if(!pagedir_save)
		return -ENOMEM;
	memset(pagedir_save,0,(1 << pagedir_order) * PAGE_SIZE);
	pagedir_nosave = pagedir_save;
	return 0;
}


/**
 *	alloc_image_pages - Allocate pages for the snapshot.
 *
 */

static int alloc_image_pages(void)
{
	struct pbe * p;
	int i;

	for (i = 0, p = pagedir_save; i < nr_copy_pages; i++, p++) {
		p->address = get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
		if(!p->address)
			goto Error;
		SetPageNosave(virt_to_page(p->address));
	}
	return 0;
 Error:
	do { 
		if (p->address)
			free_page(p->address);
		p->address = 0;
	} while (p-- > pagedir_save);
	return -ENOMEM;
}


/**
 *	enough_free_mem - Make sure we enough free memory to snapshot.
 *
 *	Returns TRUE or FALSE after checking the number of available 
 *	free pages.
 */

static int enough_free_mem(void)
{
	if(nr_free_pages() < (nr_copy_pages + PAGES_FOR_IO)) {
		pr_debug("swsusp: Not enough free pages: Have %d\n",
			 nr_free_pages());
		return 0;
	}
	return 1;
}


/**
 *	enough_swap - Make sure we have enough swap to save the image.
 *
 *	Returns TRUE or FALSE after checking the total amount of swap 
 *	space avaiable.
 *
 *	FIXME: si_swapinfo(&i) returns all swap devices information.
 *	We should only consider resume_device. 
 */

static int enough_swap(void)
{
	struct sysinfo i;

	si_swapinfo(&i);
	if (i.freeswap < (nr_copy_pages + PAGES_FOR_IO))  {
		pr_debug("swsusp: Not enough swap. Need %ld\n",i.freeswap);
		return 0;
	}
	return 1;
}

static int swsusp_alloc(void)
{
	int error;

	pr_debug("suspend: (pages needed: %d + %d free: %d)\n",
		 nr_copy_pages,PAGES_FOR_IO,nr_free_pages());

	pagedir_nosave = NULL;
	if (!enough_free_mem())
		return -ENOMEM;

	if (!enough_swap())
		return -ENOSPC;

	if ((error = alloc_pagedir())) {
		pr_debug("suspend: Allocating pagedir failed.\n");
		return error;
	}
	if ((error = alloc_image_pages())) {
		pr_debug("suspend: Allocating image pages failed.\n");
		free_suspend_pagedir((unsigned long)pagedir_save);
		return error;
	}

	nr_copy_pages_check = nr_copy_pages;
	pagedir_order_check = pagedir_order;
	return 0;
}

int suspend_prepare_image(void)
{
	unsigned int nr_needed_pages = 0;

	printk( "/critical section: ");
#ifdef CONFIG_HIGHMEM
	printk( "handling highmem" );
	if (save_highmem()) {
		printk(KERN_CRIT "%sNot enough free pages for highmem\n", name_suspend);
		return -ENOMEM;
	}
	printk(", ");
#endif

	printk("counting pages to copy" );
	drain_local_pages();
	count_data_pages();
	nr_needed_pages = nr_copy_pages + PAGES_FOR_IO;

	swsusp_alloc();
	
	/* During allocating of suspend pagedir, new cold pages may appear. 
	 * Kill them.
	 */
	drain_local_pages();
	copy_data_pages(pagedir_nosave);

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

	swsusp_swap_lock();
	write_suspend_image();
	/* This will unlock ignored swap devices since writing is finished */
	swsusp_swap_lock();

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
		device_suspend(3);
		device_shutdown();
		machine_power_off();
	}

	printk(KERN_EMERG "%sProbably not capable for powerdown. System halted.\n", name_suspend);
	machine_halt();
	while (1);
	/* NOTREACHED */
}


static void suspend_finish(void)
{
	spin_lock_irq(&suspend_pagedir_lock);	/* Done to disable interrupts */ 

	free_pages((unsigned long) pagedir_nosave, pagedir_order);
	spin_unlock_irq(&suspend_pagedir_lock);

#ifdef CONFIG_HIGHMEM
	printk( "Restoring highmem\n" );
	restore_highmem();
#endif
	device_resume();
	PRINTK( "Fixing swap signatures... " );
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	PRINTK( "ok\n" );

#ifdef SUSPEND_CONSOLE
	acquire_console_sem();
	update_screen(fg_console);
	release_console_sem();
#endif
}


extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);


asmlinkage int swsusp_save(void)
{
	int error = 0;

	if ((error = swsusp_swap_check()))
		return error;
	return suspend_prepare_image();
}

int swsusp_suspend(void)
{
	int error;
	if ((error = arch_prepare_suspend()))
		return error;
	local_irq_disable();
	save_processor_state();
	error = swsusp_arch_suspend();
	restore_processor_state();
	local_irq_enable();
	return error;
}


asmlinkage int swsusp_restore(void)
{
	BUG_ON (nr_copy_pages_check != nr_copy_pages);
	BUG_ON (pagedir_order_check != pagedir_order);
	
	/* Even mappings of "global" things (vmalloc) need to be fixed */
	__flush_tlb_global();
	return 0;
}

int swsusp_resume(void)
{
	int error;
	local_irq_disable();
	save_processor_state();
	error = swsusp_arch_resume();
	restore_processor_state();
	local_irq_enable();
	return error;
}



static int in_suspend __nosavedata = 0;

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
		disable_nonboot_cpus();
		/* Save state of all device drivers, and stop them. */
		printk("Suspending devices... ");
		if ((res = device_suspend(3))==0) {
			in_suspend = 1;

			res = swsusp_save();

			if (!res && in_suspend) {
				suspend_save_image();
				suspend_power_down();
			}
			in_suspend = 0;
			suspend_finish();
		}
		thaw_processes();
		enable_nonboot_cpus();
	} else
		res = -EBUSY;
	software_suspend_enabled = 1;
	MDELAY(1000);
	pm_restore_console();
	return res;
}

/* More restore stuff */

#define does_collide(addr) does_collide_order(pagedir_nosave, addr, 0)

/*
 * Returns true if given address/order collides with any orig_address 
 */
static int __init does_collide_order(suspend_pagedir_t *pagedir, unsigned long addr,
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
static int __init check_pagedir(void)
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

static int __init swsusp_pagedir_relocate(void)
{
	/*
	 * We have to avoid recursion (not to overflow kernel stack),
	 * and that's why code looks pretty cryptic 
	 */
	suspend_pagedir_t *old_pagedir = pagedir_nosave;
	void **eaten_memory = NULL;
	void **c = eaten_memory, *m, *f;
	int ret = 0;

	printk("Relocating pagedir ");

	if(!does_collide_order(old_pagedir, (unsigned long)old_pagedir, pagedir_order)) {
		printk("not necessary\n");
		return 0;
	}

	while ((m = (void *) __get_free_pages(GFP_ATOMIC, pagedir_order)) != NULL) {
		if (!does_collide_order(old_pagedir, (unsigned long)m, pagedir_order))
			break;
		eaten_memory = m;
		printk( "." ); 
		*eaten_memory = c;
		c = eaten_memory;
	}

	if (!m) {
		printk("out of memory\n");
		ret = -ENOMEM;
	} else {
		pagedir_nosave =
			memcpy(m, old_pagedir, PAGE_SIZE << pagedir_order);
	}

	c = eaten_memory;
	while (c) {
		printk(":");
		f = c;
		c = *c;
		free_pages((unsigned long)f, pagedir_order);
	}
	printk("|\n");
	return check_pagedir();
}

/**
 *	Using bio to read from swap.
 *	This code requires a bit more work than just using buffer heads
 *	but, it is the recommended way for 2.5/2.6.
 *	The following are to signal the beginning and end of I/O. Bios
 *	finish asynchronously, while we want them to happen synchronously.
 *	A simple atomic_t, and a wait loop take care of this problem.
 */

static atomic_t io_done = ATOMIC_INIT(0);

static void start_io(void)
{
	atomic_set(&io_done,1);
}

static int end_io(struct bio * bio, unsigned int num, int err)
{
	atomic_set(&io_done,0);
	return 0;
}

static void wait_io(void)
{
	while(atomic_read(&io_done))
		io_schedule();
}


struct block_device * resume_bdev;

/**
 *	submit - submit BIO request.
 *	@rw:	READ or WRITE.
 *	@off	physical offset of page.
 *	@page:	page we're reading or writing.
 *
 *	Straight from the textbook - allocate and initialize the bio.
 *	If we're writing, make sure the page is marked as dirty.
 *	Then submit it and wait.
 */

static int submit(int rw, pgoff_t page_off, void * page)
{
	int error = 0;
	struct bio * bio;

	bio = bio_alloc(GFP_ATOMIC,1);
	if (!bio)
		return -ENOMEM;
	bio->bi_sector = page_off * (PAGE_SIZE >> 9);
	bio_get(bio);
	bio->bi_bdev = resume_bdev;
	bio->bi_end_io = end_io;

	if (bio_add_page(bio, virt_to_page(page), PAGE_SIZE, 0) < PAGE_SIZE) {
		printk("swsusp: ERROR: adding page to bio at %ld\n",page_off);
		error = -EFAULT;
		goto Done;
	}

	if (rw == WRITE)
		bio_set_pages_dirty(bio);
	start_io();
	submit_bio(rw | (1 << BIO_RW_SYNC), bio);
	wait_io();
 Done:
	bio_put(bio);
	return error;
}

int bio_read_page(pgoff_t page_off, void * page)
{
	return submit(READ,page_off,page);
}

int bio_write_page(pgoff_t page_off, void * page)
{
	return submit(WRITE,page_off,page);
}

/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static const char * __init sanity_check(void)
{
	dump_info();
	if(swsusp_info.version_code != LINUX_VERSION_CODE)
		return "kernel version";
	if(swsusp_info.num_physpages != num_physpages)
		return "memory size";
	if (strcmp(swsusp_info.uts.sysname,system_utsname.sysname))
		return "system type";
	if (strcmp(swsusp_info.uts.release,system_utsname.release))
		return "kernel release";
	if (strcmp(swsusp_info.uts.version,system_utsname.version))
		return "version";
	if (strcmp(swsusp_info.uts.machine,system_utsname.machine))
		return "machine";
	if(swsusp_info.cpus != num_online_cpus())
		return "number of cpus";
	return NULL;
}


int __init swsusp_check_header(swp_entry_t loc)
{
	const char * reason = NULL;
	int error;

	if ((error = bio_read_page(swp_offset(loc), &swsusp_info)))
		return error;

 	/* Is this same machine? */
	if ((reason = sanity_check())) {
		printk(KERN_ERR "swsusp: Resume mismatch: %s\n",reason);
		return -EPERM;
	}
	nr_copy_pages = swsusp_info.image_pages;
	return error;
}



/**
 *	swsusp_read_data - Read image pages from swap.
 *
 *	You do not need to check for overlaps, check_pagedir()
 *	already did that.
 */

int __init swsusp_data_read(void)
{
	struct pbe * p;
	int error;
	int i;

	if ((error = swsusp_pagedir_relocate()))
		return error;

	printk( "Reading image data (%d pages): ", nr_copy_pages );
	for(i = 0, p = pagedir_nosave; i < nr_copy_pages && !error; i++, p++) {
		if (!(i%100))
			printk( "." );
		error = bio_read_page(swp_offset(p->swap_address),
				  (void *)p->address);
	}
	printk(" %d done.\n",i);
	return error;

}

extern dev_t __init name_to_dev_t(const char *line);

static int __init __read_suspend_image(int noresume)
{
	union diskpage *cur;
	swp_entry_t next;
	int i, nr_pgdir_pages;
	int error;

	cur = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	if (!cur)
		return -ENOMEM;

#define PREPARENEXT \
	{	next = cur->link.next; \
		next.val = swp_offset(next); \
        }

	if (bio_read_page(0, cur)) return -EIO;

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
		bio_write_page(0, cur);
	}

	printk( "%sSignature found, resuming\n", name_resume );
	MDELAY(1000);

	if ((error = swsusp_check_header(next)))
		return error;

	next = swsusp_info.pagedir[0];
	next.val = swp_offset(next);

	pagedir_save = swsusp_info.suspend_pagedir;
	nr_copy_pages = swsusp_info.image_pages;
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
		if (bio_read_page(next.val, cur)) return -EIO;
		PREPARENEXT;
	}
	BUG_ON (next.val);

	error = swsusp_data_read();
	if (!error)
		printk( "|\n" );
	return 0;
}

static int __init read_suspend_image(const char * specialfile, int noresume)
{
	int error;
	char b[BDEVNAME_SIZE];

	resume_device = name_to_dev_t(specialfile);
	printk("Resuming from device %s\n", __bdevname(resume_device, b));
	resume_bdev = open_by_devnum(resume_device, FMODE_READ);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		error = __read_suspend_image(noresume);
		blkdev_put(resume_bdev);
	} else
		error = PTR_ERR(resume_bdev);
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
	/* FIXME: Should we stop processes here, just to be safer? */
	disable_nonboot_cpus();
	device_suspend(3);
	swsusp_resume();
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
