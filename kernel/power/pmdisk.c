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

#undef DEBUG

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
 * Saving part...
 */

static void fill_suspend_header(struct suspend_header *sh)
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
}


/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

static unsigned short swapfile_used[MAX_SWAPFILES];
static unsigned short root_swap;

static int mark_swapfiles(swp_entry_t prev)
{
	swp_entry_t entry;
	union diskpage *cur;
	int error;

	printk( "S" );

	if (root_swap == 0xFFFF)  /* ignored */
		return -EINVAL;

	cur = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	if (!cur)
		return -ENOMEM;

	/* XXX: this is dirty hack to get first page of swap file */
	entry = swp_entry(root_swap, 0);
	rw_swap_page_sync(READ, entry, virt_to_page((unsigned long)cur));

	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)))
		memcpy(cur->swh.magic.magic,"P1DISK....",10);
	else if ((!memcmp("SWAPSPACE2",cur->swh.magic.magic,10)))
		memcpy(cur->swh.magic.magic,"P2DISK....",10);
	else {
		pr_debug("pmdisk: Partition is not swap space.\n");
		return -ENODEV;
	}

	/* prev is the first/last swap page of the resume area */
	cur->link.next = prev; 

	/* link.next lies *no more* in last 4/8 bytes of magic */

	error = rw_swap_page_sync(WRITE, entry, 
				  virt_to_page((unsigned long)cur));
	free_page((unsigned long)cur);
	return error;
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
				pr_debug("pmdisk: Default resume partition not set.\n");
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
				} else
				  	swapfile_used[i] = SWAPFILE_IGNORED;
			}
		}
	}
	swap_list_unlock();
}


/* This is called after saving image so modification
   will be lost after resume... and that's what we want. */
static void lock_swapdevices(void)
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

static int write_swap_page(unsigned long addr, swp_entry_t * loc)
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

static void free_data(void)
{
	swp_entry_t entry;
	int i;

	for (i = 0; i < pmdisk_pages; i++) {
		entry = (pm_pagedir_nosave + i)->swap_address;
		if (entry.val)
			swap_free(entry);
		else
			break;
		(pm_pagedir_nosave + i)->swap_address = (swp_entry_t){0};
	}
}


/**
 *	write_data - Write saved image to swap.
 *
 *	Walk the list of pages in the image and sync each one to swap.
 */

static int write_data(void)
{
	int error = 0;
	int i;

	printk( "Writing data to swap (%d pages): ", pmdisk_pages );
	for (i = 0; i < pmdisk_pages && !error; i++) {
		if (!(i%100))
			printk( "." );
		error = write_swap_page((pm_pagedir_nosave+i)->address,
					&((pm_pagedir_nosave+i)->swap_address));
	}
	printk( "|\n" );
	return error;
}


/**
 *	free_pagedir - Free pages used by the page directory.
 */

static void free_pagedir_entries(void)
{
	int num = SUSPEND_PD_PAGES(pmdisk_pages);
	union diskpage * cur;
	swp_entry_t entry;
	int i;

	for (i = 0; i < num; i++) {
		cur = (union diskpage *)((char *) pm_pagedir_nosave)+i;
		entry = cur->link.next;
		if (entry.val)
			swap_free(entry);
	}
}


/**
 *	write_pagedir - Write the array of pages holding the page directory.
 *	@last:	Last swap entry we write (needed for header).
 */

static int write_pagedir(swp_entry_t * last)
{
	int nr_pgdir_pages = SUSPEND_PD_PAGES(pmdisk_pages);
	union diskpage *cur;
	swp_entry_t prev = {0};
	int error = 0;
	int i;

	printk( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	for (i = 0; i < nr_pgdir_pages && !error; i++) {
		cur = (union diskpage *)((char *) pm_pagedir_nosave)+i;
		BUG_ON ((char *) cur != (((char *) pm_pagedir_nosave) + i*PAGE_SIZE));
		printk( "." );

		cur->link.next = prev;
		error = write_swap_page((unsigned long)cur,&prev);
	}
	*last = prev;
	return error;
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

static int write_header(swp_entry_t * entry)
{
	union diskpage * buffer;
	int error;

	buffer = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	printk("H");
	fill_suspend_header(&buffer->sh);
	buffer->link.next = *entry;
	error = write_swap_page((unsigned long)buffer,entry);
	free_page((unsigned long) buffer);
	return error;
}


/**
 *	write_suspend_image - Write entire image and metadata.
 *
 */

static int write_suspend_image(void)
{
	int error;
	swp_entry_t prev = { 0 };

	if ((error = write_data()))
		goto FreeData;

	if ((error = write_pagedir(&prev)))
		goto FreePagedir;

	if ((error = write_header(&prev)))
		goto FreePagedir;

	error = mark_swapfiles(prev);
	printk( "|\n" );
 Done:
	return error;
 FreePagedir:
	free_pagedir_entries();
 FreeData:
	free_data();
	goto Done;
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

static int saveable(unsigned long * pfn)
{
	struct page * page = pfn_to_page(*pfn);

	if (PageNosave(page))
		return 0;

	if (!PageReserved(page)) {
		int chunk_size;

		if ((chunk_size = is_head_of_free_region(page))) {
			*pfn += chunk_size - 1;
			return 0;
		}
	} else if (PageReserved(page)) {
		/* Just copy whole code segment. 
		 * Hopefully it is not that big.
		 */
		if ((ADDRESS(*pfn) >= (unsigned long) ADDRESS2(&__nosave_begin)) && 
		    (ADDRESS(*pfn) <  (unsigned long) ADDRESS2(&__nosave_end))) {
			pr_debug("[nosave %lx]\n", ADDRESS(*pfn));
			return 0;
		}
		/* Hmm, perhaps copying all reserved pages is not 
		 * too healthy as they may contain 
		 * critical bios data? 
		 */
	}
	return 1;
}



/**
 *	count_pages - Determine size of page directory.
 *	
 *	Iterate over all the pages in the system and tally the number
 *	we need to clone.
 */

static void count_pages(void)
{
	unsigned long pfn;
	int n = 0;
	
	for (pfn = 0; pfn < max_pfn; pfn++) {
		if (saveable(&pfn))
			n++;
	}
	pmdisk_pages = n;
}


/**
 *	copy_pages - Atmoically snapshot memory.
 *
 *	Iterate over all the pages in the system and copy each one 
 *	into its corresponding location in the pagedir.
 *	We rely on the fact that the number of pages that we're snap-
 *	shotting hasn't changed since we counted them. 
 */

static void copy_pages(void)
{
	struct pbe * p = pagedir_save;
	unsigned long pfn;
	int n = 0;

	for (pfn = 0; pfn < max_pfn; pfn++) {
		if (saveable(&pfn)) {
			n++;
			p->orig_address = ADDRESS(pfn);
			copy_page((void *) p->address, 
				  (void *) p->orig_address);
			p++;
		}
	}
	BUG_ON(n != pmdisk_pages);
}


/**
 *	free_image_pages - Free each page allocated for snapshot.
 */

static void free_image_pages(void)
{
	struct pbe * p;
	int i;

	for (i = 0, p = pagedir_save; i < pmdisk_pages; i++, p++) {
		ClearPageNosave(virt_to_page(p->address));
		free_page(p->address);
	}
}


/**
 *	free_pagedir - Free the page directory.
 */

static void free_pagedir(void)
{
	free_image_pages();
	free_pages((unsigned long)pagedir_save, pagedir_order);
}


static void calc_order(void)
{
	int diff;
	int order;

	order = get_bitmask_order(SUSPEND_PD_PAGES(pmdisk_pages));
	pmdisk_pages += 1 << order;
	do {
		diff = get_bitmask_order(SUSPEND_PD_PAGES(pmdisk_pages)) - order;
		if (diff) {
			order += diff;
			pmdisk_pages += 1 << diff;
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
	pm_pagedir_nosave = pagedir_save;
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

	for (i = 0, p = pagedir_save; i < pmdisk_pages; i++, p++) {
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
	if(nr_free_pages() < (pmdisk_pages + PAGES_FOR_IO)) {
		pr_debug("pmdisk: Not enough free pages: Have %d\n",
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
	if (i.freeswap < (pmdisk_pages + PAGES_FOR_IO))  {
		pr_debug("pmdisk: Not enough swap. Need %ld\n",i.freeswap);
		return 0;
	}
	return 1;
}


/**
 *	pmdisk_suspend - Atomically snapshot the system.
 *
 *	This must be called with interrupts disabled, to prevent the 
 *	system changing at all from underneath us. 
 *
 *	To do this, we count the number of pages in the system that we 
 *	need to save; make sure	we have enough memory and swap to clone
 *	the pages and save them in swap, allocate the space to hold them,
 *	and then snapshot them all.
 */

int pmdisk_suspend(void)
{
	int error = 0;

	read_swapfiles();
	drain_local_pages();

	pm_pagedir_nosave = NULL;
	pr_debug("pmdisk: Counting pages to copy.\n" );
	count_pages();
	
	pr_debug("pmdisk: (pages needed: %d + %d free: %d)\n",
		 pmdisk_pages,PAGES_FOR_IO,nr_free_pages());

	if (!enough_free_mem())
		return -ENOMEM;

	if (!enough_swap())
		return -ENOSPC;

	if ((error = alloc_pagedir())) {
		pr_debug("pmdisk: Allocating pagedir failed.\n");
		return error;
	}
	if ((error = alloc_image_pages())) {
		pr_debug("pmdisk: Allocating image pages failed.\n");
		free_pagedir();
		return error;
	}

	nr_copy_pages_check = pmdisk_pages;
	pagedir_order_check = pagedir_order;

	/* During allocating of suspend pagedir, new cold pages may appear. 
	 * Kill them 
	 */
	drain_local_pages();

	/* copy */
	copy_pages();

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	pr_debug("pmdisk: %d pages copied\n", pmdisk_pages );
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

	pr_debug("pmdisk: Relocating pagedir\n");

	if(!does_collide_order(old_pagedir, (unsigned long)old_pagedir, pagedir_order)) {
		pr_debug("pmdisk: Relocation not necessary\n");
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
		printk("pmdisk: ERROR: adding page to bio at %ld\n",page_off);
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


#define next_entry(diskpage)	(diskpage)->link.next


static int __init check_sig(swp_entry_t * next)
{
	union swap_header * hdr;
	int error;

	hdr = (union swap_header *)get_zeroed_page(GFP_ATOMIC);
	if (!hdr)
		return -ENOMEM;

	if ((error = read_page(0,hdr)))
		return error;
	/*
	 * We have to read next position before we overwrite it
	 */
	*next = next_entry((union diskpage *)hdr);

	if (!memcmp("P1",hdr->magic.magic,2))
		memcpy(hdr->magic.magic,"SWAP-SPACE",10);
	else if (!memcmp("P2",hdr->magic.magic,2))
		memcpy(hdr->magic.magic,"SWAPSPACE2",10);
	else if ((!memcmp("SWAP-SPACE",hdr->magic.magic,10)) ||
		 (!memcmp("SWAPSPACE2",hdr->magic.magic,10))) {
		pr_debug(KERN_ERR "pmdisk: Partition is normal swap space\n");
		error = -EINVAL;
		goto Done;
	} else {
		pr_debug(KERN_ERR "pmdisk: Invalid partition type.\n");
		error = -EINVAL;
		goto Done;
	}

	/*
	 * Reset swap signature now.
	 */
	if ((error = write_page(0,hdr)))
		goto Done;

	pr_debug( "%sSignature found, resuming\n", name_resume );
 Done:
	free_page((unsigned long)hdr);
	return error;
}


/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static const char * __init sanity_check(struct suspend_header *sh)
{
	if(sh->version_code != LINUX_VERSION_CODE)
		return "Incorrect kernel version";
	if(sh->num_physpages != num_physpages)
		return "Incorrect memory size";
	if(strncmp(sh->machine, system_utsname.machine, 8))
		return "Incorrect machine type";
	if(strncmp(sh->version, system_utsname.version, 20))
		return "Incorrect version";
	if(sh->num_cpus != num_online_cpus())
		return "Incorrect number of cpus";
	if(sh->page_size != PAGE_SIZE)
		return "Incorrect PAGE_SIZE";
	return 0;
}


static int __init check_header(swp_entry_t * next)
{
	struct suspend_header * sh;
	const char * reason = NULL;
	int error;

	sh = (struct suspend_header *)get_zeroed_page(GFP_ATOMIC);
	if (!sh)
		return -ENOMEM;

	if ((error = read_page(swp_offset(*next), sh)))
		goto Done;

 	/* Is this same machine? */
	if ((reason = sanity_check(sh))) {
		printk(KERN_ERR "pmdisk: Resume mismatch: %s\n",reason);
		error = -EPERM;
		goto Done;
	}

	*next = next_entry((union diskpage *)sh);
	pagedir_save = sh->suspend_pagedir;
	pmdisk_pages = sh->num_pbes;
 Done:
	free_page((unsigned long)sh);
	return error;
}


static int __init read_pagedir(swp_entry_t start)
{
	int i, nr_pgdir_pages;
	swp_entry_t entry = start;
	int error = 0;

	nr_pgdir_pages = SUSPEND_PD_PAGES(pmdisk_pages);
	pagedir_order = get_bitmask_order(nr_pgdir_pages);

	pm_pagedir_nosave = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if (!pm_pagedir_nosave)
		return -ENOMEM;

	pr_debug("pmdisk: sReading pagedir\n");

	/* We get pages in reverse order of saving! */
	for (i = nr_pgdir_pages-1; i >= 0 && !error; i--) {
		void * data = ((char *)pm_pagedir_nosave) + i * PAGE_SIZE;
		unsigned long offset = swp_offset(entry);

		if (offset) {
			if (!(error = read_page(offset, data)))
				entry = next_entry((union diskpage *)data);
		} else
			error = -EFAULT;
	}
	if (swp_offset(entry))
		error = -E2BIG;
	if (error)
		free_pages((unsigned long)pm_pagedir_nosave,pagedir_order);
	return error;
}


/**
 *	read_image_data - Read image pages from swap.
 *
 *	You do not need to check for overlaps, check_pagedir()
 *	already did that.
 */

static int __init read_image_data(void)
{
	struct pbe * p;
	int error = 0;
	int i;

	printk( "Reading image data (%d pages): ", pmdisk_pages );
	for(i = 0, p = pm_pagedir_nosave; i < pmdisk_pages && !error; i++, p++) {
		if (!(i%100))
			printk( "." );
		error = read_page(swp_offset(p->swap_address),
				  (void *)p->address);
	}
	printk(" %d done|\n",i);
	return error;
}


static int __init read_suspend_image(void)
{
	swp_entry_t next;
	int error = 0;

	if ((error = check_sig(&next)))
		return error;
	if ((error = check_header(&next)))
		return error;
	if ((error = read_pagedir(next)))
		return error;
	if ((error = relocate_pagedir()))
		goto FreePagedir;
	if ((error = check_pagedir()))
		goto FreePagedir;
	if ((error = read_image_data()))
		goto FreePagedir;
 Done:
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

#if defined (CONFIG_HIGHMEM) || defined (CONFIG_DISCONTIGMEM)
	pr_debug("pmdisk: not supported with high- or discontig-mem.\n");
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

	if (!strlen(resume_file))
		return -ENOENT;

	resume_device = name_to_dev_t(resume_file);
	pr_debug("pmdisk: Resume From Partition: %s\n", resume_file);

	resume_bdev = open_by_devnum(resume_device, FMODE_READ, BDEV_RAW);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		error = read_suspend_image();
		blkdev_put(resume_bdev, BDEV_RAW);
	} else
		error = PTR_ERR(resume_bdev);

	if (!error)
		pr_debug("Reading resume file was successful\n");
	else
		pr_debug( "%sError %d resuming\n", name_resume, error );
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
	pr_debug( "Freeing prev allocated pagedir\n" );
	free_pagedir();
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

