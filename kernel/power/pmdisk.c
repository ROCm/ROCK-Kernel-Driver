/*
 * kernel/power/pmdisk.c - Suspend-to-disk implmentation
 *
 * This STD implementation is initially derived from swsusp (suspend-to-swap).
 * The original copyright on that was: 
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001,2002 Pavel Machek <pavel@suse.cz>
 *
 * The additional parts are: 
 * 
 * Copyright (C) 2003 Patrick Mochel
 * Copyright (C) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2. 
 *
 * For more information, please see the text files in Documentation/power/
 *
 */

#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/suspend.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/device.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>

#include <asm/mmu_context.h>

#include "power.h"


extern int pmdisk_arch_suspend(int resume);

#define __ADDRESS(x)  ((unsigned long) phys_to_virt(x))
#define ADDRESS(x) __ADDRESS((x) << PAGE_SHIFT)
#define ADDRESS2(x) __ADDRESS(__pa(x))		/* Needed for x86-64 where some pages are in memory twice */

/* References to section boundaries */
extern char __nosave_begin, __nosave_end;

extern int is_head_of_free_region(struct page *);

/* Variables to be preserved over suspend */
static int pagedir_order_check;
static int nr_copy_pages_check;

/* For resume= kernel option */
static char resume_file[256] = CONFIG_PM_DISK_PARTITION;

static dev_t resume_device;
/* Local variables that should not be affected by save */
unsigned int pmdisk_pages __nosavedata = 0;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume 

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave", 
   allocated at time of resume, that travels through memory not to
   collide with anything.
 */
suspend_pagedir_t *pm_pagedir_nosave __nosavedata = NULL;
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
# define PRINTK(f, a...)       printk(f, ## a)
#else
# define PRINTK(f, a...)
#endif

#ifdef DEBUG_SLOW
#define MDELAY(a) mdelay(a)
#else
#define MDELAY(a)
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
	sh->suspend_pagedir = pm_pagedir_nosave;
	BUG_ON (pagedir_save != pm_pagedir_nosave);
	sh->num_pbes = pmdisk_pages;
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
				if (1) {
// FIXME				if(resume_device == swap_info[i].swap_device) {
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

static int write_suspend_image(void)
{
	int i;
	swp_entry_t entry, prev = { 0 };
	int nr_pgdir_pages = SUSPEND_PD_PAGES(pmdisk_pages);
	union diskpage *cur,  *buffer = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	unsigned long address;
	struct page *page;

	printk( "Writing data to swap (%d pages): ", pmdisk_pages );
	for (i=0; i<pmdisk_pages; i++) {
		if (!(i%100))
			printk( "." );
		if (!(entry = get_swap_page()).val)
			panic("\nNot enough swapspace when writing data" );
		
		if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
			panic("\nPage %d: not enough swapspace on suspend device", i );
	    
		address = (pm_pagedir_nosave+i)->address;
		page = virt_to_page(address);
		rw_swap_page_sync(WRITE, entry, page);
		(pm_pagedir_nosave+i)->swap_address = entry;
	}
	printk( "|\n" );
	printk( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	for (i=0; i<nr_pgdir_pages; i++) {
		cur = (union diskpage *)((char *) pm_pagedir_nosave)+i;
		BUG_ON ((char *) cur != (((char *) pm_pagedir_nosave) + i*PAGE_SIZE));
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
	if (!(entry = get_swap_page()).val)
		panic( "\nNot enough swapspace when writing header" );
	if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
		panic("\nNot enough swapspace for header on suspend device" );

	cur = (void *) buffer;
	if (fill_suspend_header(&cur->sh))
		panic("\nOut of memory while writing header");
		
	cur->link.next = prev;

	page = virt_to_page((unsigned long)cur);
	rw_swap_page_sync(WRITE, entry, page);
	prev = entry;

	printk( "S" );
	mark_swapfiles(prev, MARK_SWAP_SUSPEND);
	printk( "|\n" );

	MDELAY(1000);
	free_page((unsigned long) buffer);
	return 0;
}

/* if pagedir_p != NULL it also copies the counted pages */
static int count_and_copy_data_pages(struct pbe *pagedir_p)
{
	int chunk_size;
	int nr_copy_pages = 0;
	int pfn;
	struct page *page;
	
	BUG_ON (max_pfn != num_physpages);

	for (pfn = 0; pfn < max_pfn; pfn++) {
		page = pfn_to_page(pfn);

		if (!PageReserved(page)) {
			if (PageNosave(page))
				continue;

			if ((chunk_size=is_head_of_free_region(page))!=0) {
				pfn += chunk_size - 1;
				continue;
			}
		} else if (PageReserved(page)) {
			BUG_ON (PageNosave(page));

			/*
			 * Just copy whole code segment. Hopefully it is not that big.
			 */
			if ((ADDRESS(pfn) >= (unsigned long) ADDRESS2(&__nosave_begin)) && 
			    (ADDRESS(pfn) <  (unsigned long) ADDRESS2(&__nosave_end))) {
				PRINTK("[nosave %lx]", ADDRESS(pfn));
				continue;
			}
			/* Hmm, perhaps copying all reserved pages is not too healthy as they may contain 
			   critical bios data? */
		} else	BUG();

		nr_copy_pages++;
		if (pagedir_p) {
			pagedir_p->orig_address = ADDRESS(pfn);
			copy_page((void *) pagedir_p->address, (void *) pagedir_p->orig_address);
			pagedir_p++;
		}
	}
	return nr_copy_pages;
}

static void free_suspend_pagedir(unsigned long this_pagedir)
{
	struct page *page;
	int pfn;
	unsigned long this_pagedir_end = this_pagedir +
		(PAGE_SIZE << pagedir_order);

	for(pfn = 0; pfn < num_physpages; pfn++) {
		page = pfn_to_page(pfn);
		if (!TestClearPageNosave(page))
			continue;

		if (ADDRESS(pfn) >= this_pagedir && ADDRESS(pfn) < this_pagedir_end)
			continue; /* old pagedir gets freed in one */
		
		free_page(ADDRESS(pfn));
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
	if(!pagedir)
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


int pmdisk_suspend(void)
{
	struct sysinfo i;
	unsigned int nr_needed_pages = 0;

	read_swapfiles();
	drain_local_pages();

	pm_pagedir_nosave = NULL;
	printk( "/critical section: Counting pages to copy" );
	pmdisk_pages = count_and_copy_data_pages(NULL);
	nr_needed_pages = pmdisk_pages + PAGES_FOR_IO;
	
	printk(" (pages needed: %d+%d=%d free: %d)\n",pmdisk_pages,PAGES_FOR_IO,nr_needed_pages,nr_free_pages());
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
	pagedir_save = pm_pagedir_nosave = create_suspend_pagedir(pmdisk_pages);
	if(!pm_pagedir_nosave) {
		/* Shouldn't happen */
		printk(KERN_CRIT "%sCouldn't allocate enough pages\n",name_suspend);
		panic("Really should not happen");
		return 1;
	}
	nr_copy_pages_check = pmdisk_pages;
	pagedir_order_check = pagedir_order;

	drain_local_pages();	/* During allocating of suspend pagedir, new cold pages may appear. Kill them */
	if (pmdisk_pages != count_and_copy_data_pages(pm_pagedir_nosave))	/* copy */
		BUG();

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	printk( "critical section/: done (%d pages copied)\n", pmdisk_pages );
	return 0;
}


/**
 *	suspend_save_image - Prepare and write saved image to swap.
 *
 *	IRQs are re-enabled here so we can resume devices and safely write
 *	to the swap devices. We disable them again before we leave.
 *
 *	The second lock_swapdevices() will unlock ignored swap devices since
 *	writing is finished.
 *	It is important _NOT_ to umount filesystems at this point. We want
 *	them synced (in case something goes wrong) but we DO not want to mark
 *	filesystem clean: it is not. (And it does not matter, if we resume
 *	correctly, we'll mark system clean, anyway.)
 */

static int suspend_save_image(void)
{
	int error;
	device_resume();
	lock_swapdevices();
	error = write_suspend_image();
	lock_swapdevices();
	return error;
}

/*
 * Magic happens here
 */

int pmdisk_resume(void)
{
	BUG_ON (nr_copy_pages_check != pmdisk_pages);
	BUG_ON (pagedir_order_check != pagedir_order);
	
	/* Even mappings of "global" things (vmalloc) need to be fixed */
	__flush_tlb_global();
	return 0;
}

/* pmdisk_arch_suspend() is implemented in arch/?/power/pmdisk.S,
   and basically does:

	if (!resume) {
		save_processor_state();
		SAVE_REGISTERS
		return pmdisk_suspend();
	}
	GO_TO_SWAPPER_PAGE_TABLES
	COPY_PAGES_BACK
	RESTORE_REGISTERS
	restore_processor_state();
	return pmdisk_resume();

 */


/* More restore stuff */

/* FIXME: Why not memcpy(to, from, 1<<pagedir_order*PAGE_SIZE)? */
static void __init copy_pagedir(suspend_pagedir_t *to, suspend_pagedir_t *from)
{
	int i;
	char *topointer=(char *)to, *frompointer=(char *)from;

	for(i=0; i < 1 << pagedir_order; i++) {
		copy_page(topointer, frompointer);
		topointer += PAGE_SIZE;
		frompointer += PAGE_SIZE;
	}
}

#define does_collide(addr) does_collide_order(pm_pagedir_nosave, addr, 0)

/*
 * Returns true if given address/order collides with any orig_address 
 */
static int __init does_collide_order(suspend_pagedir_t *pagedir, 
				     unsigned long addr, int order)
{
	int i;
	unsigned long addre = addr + (PAGE_SIZE<<order);
	
	for(i=0; i < pmdisk_pages; i++)
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

	for(i=0; i < pmdisk_pages; i++) {
		unsigned long addr;

		do {
			addr = get_zeroed_page(GFP_ATOMIC);
			if(!addr)
				return -ENOMEM;
		} while (does_collide(addr));

		(pm_pagedir_nosave+i)->address = addr;
	}
	return 0;
}

static int __init relocate_pagedir(void)
{
	/*
	 * We have to avoid recursion (not to overflow kernel stack),
	 * and that's why code looks pretty cryptic 
	 */
	suspend_pagedir_t *new_pagedir, *old_pagedir = pm_pagedir_nosave;
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

	pm_pagedir_nosave = new_pagedir = m;
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

static int __init sanity_check_failed(char *reason)
{
	printk(KERN_ERR "%s%s\n",name_resume,reason);
	return -EPERM;
}

static int __init sanity_check(struct suspend_header *sh)
{
	if(sh->version_code != LINUX_VERSION_CODE)
		return sanity_check_failed("Incorrect kernel version");
	if(sh->num_physpages != num_physpages)
		return sanity_check_failed("Incorrect memory size");
	if(strncmp(sh->machine, system_utsname.machine, 8))
		return sanity_check_failed("Incorrect machine type");
	if(strncmp(sh->version, system_utsname.version, 20))
		return sanity_check_failed("Incorrect version");
	if(sh->num_cpus != num_online_cpus())
		return sanity_check_failed("Incorrect number of cpus");
	if(sh->page_size != PAGE_SIZE)
		return sanity_check_failed("Incorrect PAGE_SIZE");
	return 0;
}

static struct block_device * resume_bdev;


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
	blk_run_queues();
	while(atomic_read(&io_done))
		io_schedule();
}


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
		printk("ERROR: adding page to bio at %ld\n",page_off);
		error = -EFAULT;
		goto Done;
	}

	if (rw == WRITE)
		bio_set_pages_dirty(bio);
	start_io();
	submit_bio(rw,bio);
	wait_io();
 Done:
	bio_put(bio);
	return error;
}

static int
read_page(pgoff_t page_off, void * page)
{
	return submit(READ,page_off,page);
}

static int
write_page(pgoff_t page_off, void * page)
{
	return submit(WRITE,page_off,page);
}


extern dev_t __init name_to_dev_t(const char *line);


#define next_entry(diskpage)	diskpage->link.next

static int __init read_suspend_image(void)
{
	swp_entry_t next;
	int i, nr_pgdir_pages;
	union diskpage *cur;
	int error = 0;

	cur = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	if (!cur)
		return -ENOMEM;

	if ((error = read_page(0, cur)))
		goto Done;

	/*
	 * We have to read next position before we overwrite it
	 */
	next = next_entry(cur);

	if (!memcmp("S1",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
	else if (!memcmp("S2",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
	else if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)) ||
		 (!memcmp("SWAPSPACE2",cur->swh.magic.magic,10))) {
		printk(KERN_ERR "pmdisk: Partition is normal swap space\n");
		error = -EINVAL;
		goto Done;
	} else {
		printk(KERN_ERR "pmdisk: Invalid partition type.\n");
		error = -EINVAL;
		goto Done;
	}

	/*
	 * Reset swap signature now.
	 */
	if ((error = write_page(0,cur)))
		goto Done;

	printk( "%sSignature found, resuming\n", name_resume );
	MDELAY(1000);

	if ((error = read_page(swp_offset(next), cur)))
		goto Done;
 	/* Is this same machine? */
	if ((error = sanity_check(&cur->sh)))
		goto Done;
	next = next_entry(cur);

	pagedir_save = cur->sh.suspend_pagedir;
	pmdisk_pages = cur->sh.num_pbes;
	nr_pgdir_pages = SUSPEND_PD_PAGES(pmdisk_pages);
	pagedir_order = get_bitmask_order(nr_pgdir_pages);

	pm_pagedir_nosave = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if (!pm_pagedir_nosave) {
		error = -ENOMEM;
		goto Done;
	}

	PRINTK( "%sReading pagedir, ", name_resume );

	/* We get pages in reverse order of saving! */
	for (i=nr_pgdir_pages-1; i>=0; i--) {
		BUG_ON (!next.val);
		cur = (union diskpage *)((char *) pm_pagedir_nosave)+i;
		error = read_page(swp_offset(next), cur);
		if (error)
			goto FreePagedir;
		next = next_entry(cur);
	}
	BUG_ON (next.val);

	if ((error = relocate_pagedir()))
		goto FreePagedir;
	if ((error = check_pagedir()))
		goto FreePagedir;

	printk( "Reading image data (%d pages): ", pmdisk_pages );
	for(i=0; i < pmdisk_pages; i++) {
		swp_entry_t swap_address = (pm_pagedir_nosave+i)->swap_address;
		if (!(i%100))
			printk( "." );
		/* You do not need to check for overlaps...
		   ... check_pagedir already did this work */
		error = read_page(swp_offset(swap_address),
				  (char *)((pm_pagedir_nosave+i)->address));
		if (error)
			goto FreePagedir;
	}
	printk( "|\n" );
 Done:
	free_page((unsigned long)cur);
	return error;
 FreePagedir:
	free_pages((unsigned long)pm_pagedir_nosave,pagedir_order);
	goto Done;
}

/**
 *	pmdisk_save - Snapshot memory
 */

int pmdisk_save(void) 
{
	int error;

#if defined (CONFIG_HIGHMEM) || defined (COFNIG_DISCONTIGMEM)
	printk("pmdisk is not supported with high- or discontig-mem.\n");
	return -EPERM;
#endif
	if ((error = arch_prepare_suspend()))
		return error;
	local_irq_disable();
	error = pmdisk_arch_suspend(0);
	local_irq_enable();
	return error;
}


/**
 *	pmdisk_write - Write saved memory image to swap.
 *
 *	pmdisk_arch_suspend(0) returns after system is resumed.
 *
 *	pmdisk_arch_suspend() copies all "used" memory to "free" memory,
 *	then unsuspends all device drivers, and writes memory to disk
 *	using normal kernel mechanism.
 */

int pmdisk_write(void)
{
	return suspend_save_image();
}


/**
 *	pmdisk_read - Read saved image from swap.
 */

int __init pmdisk_read(void)
{
	int error;
	char b[BDEVNAME_SIZE];

	if (!strlen(resume_file))
		return -ENOENT;

	resume_device = name_to_dev_t(resume_file);
	printk("pmdisk: Resume From Partition: %s, Device: %s\n", 
	       resume_file, __bdevname(resume_device, b));

	resume_bdev = open_by_devnum(resume_device, FMODE_READ, BDEV_RAW);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		error = read_suspend_image();
		blkdev_put(resume_bdev, BDEV_RAW);
	} else
		error = PTR_ERR(resume_bdev);

	if (!error)
		PRINTK("Reading resume file was successful\n");
	else
		printk( "%sError %d resuming\n", name_resume, error );
	MDELAY(1000);
	return error;
}


/**
 *	pmdisk_restore - Replace running kernel with saved image.
 */

int __init pmdisk_restore(void)
{
	int error;
	local_irq_disable();
	error = pmdisk_arch_suspend(1);
	local_irq_enable();
	return error;
}


/**
 *	pmdisk_free - Free memory allocated to hold snapshot.
 */

int pmdisk_free(void)
{
	PRINTK( "Freeing prev allocated pagedir\n" );
	free_suspend_pagedir((unsigned long) pagedir_save);
	return 0;
}

static int __init pmdisk_setup(char *str)
{
	if (strlen(str)) {
		if (!strcmp(str,"off"))
			resume_file[0] = '\0';
		else
			strncpy(resume_file, str, 255);
	} else
		resume_file[0] = '\0';
	return 1;
}

__setup("pmdisk=", pmdisk_setup);

