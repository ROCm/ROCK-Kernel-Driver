/*
 * Two-stage soft-boot based dump scheme methods (memory overlay
 * with post soft-boot writeout)
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 *
 * This approach of saving the dump in memory and writing it 
 * out after a softboot without clearing memory is derived from the 
 * Mission Critical Linux dump implementation. Credits and a big
 * thanks for letting the lkcd project make use of the excellent 
 * piece of work and also for helping with clarifications and 
 * tips along the way are due to:
 * 	Dave Winchell <winchell@mclx.com> (primary author of mcore)
 * 	and also to
 * 	Jeff Moyer <moyer@mclx.com>
 * 	Josh Huber <huber@mclx.com>
 * 
 * For those familiar with the mcore implementation, the key 
 * differences/extensions here are in allowing entire memory to be 
 * saved (in compressed form) through a careful ordering scheme 
 * on both the way down as well on the way up after boot, the latter
 * for supporting the LKCD notion of passes in which most critical 
 * data is the first to be saved to the dump device. Also the post 
 * boot writeout happens from within the kernel rather than driven 
 * from userspace.
 *
 * The sequence is orchestrated through the abstraction of "dumpers",
 * one for the first stage which then sets up the dumper for the next 
 * stage, providing for a smooth and flexible reuse of the singlestage 
 * dump scheme methods and a handle to pass dump device configuration 
 * information across the soft boot. 
 *
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * This code is released under version 2 of the GNU GPL.
 */

/*
 * Disruptive dumping using the second kernel soft-boot option
 * for issuing dump i/o operates in 2 stages:
 * 
 * (1) - Saves the (compressed & formatted) dump in memory using a 
 *       carefully ordered overlay scheme designed to capture the 
 *       entire physical memory or selective portions depending on 
 *       dump config settings, 
 *     - Registers the stage 2 dumper and 
 *     - Issues a soft reboot w/o clearing memory. 
 *
 *     The overlay scheme starts with a small bootstrap free area
 *     and follows a reverse ordering of passes wherein it 
 *     compresses and saves data starting with the least critical 
 *     areas first, thus freeing up the corresponding pages to 
 *     serve as destination for subsequent data to be saved, and
 *     so on. With a good compression ratio, this makes it feasible
 *     to capture an entire physical memory dump without significantly
 *     reducing memory available during regular operation.
 *
 * (2) Post soft-reboot, runs through the saved memory dump and
 *     writes it out to disk, this time around, taking care to
 *     save the more critical data first (i.e. pages which figure 
 *     in early passes for a regular dump). Finally issues a 
 *     clean reboot.
 *     
 *     Since the data was saved in memory after selection/filtering
 *     and formatted as per the chosen output dump format, at this 
 *     stage the filter and format actions are just dummy (or
 *     passthrough) actions, except for influence on ordering of
 *     passes.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/dump.h>
#ifdef CONFIG_KEXEC
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#endif
#include "dump_methods.h"

extern struct list_head dumper_list_head;
extern struct dump_memdev *dump_memdev;
extern struct dumper dumper_stage2;
struct dump_config_block *dump_saved_config = NULL;
extern struct dump_blockdev *dump_blockdev;
static struct dump_memdev *saved_dump_memdev = NULL;
static struct dumper *saved_dumper = NULL;

#ifdef CONFIG_KEXEC
extern int panic_timeout;
#endif

/* For testing 
extern void dump_display_map(struct dump_memdev *);
*/

struct dumper *dumper_by_name(char *name)
{
#ifdef LATER
	struct dumper *dumper;
	list_for_each_entry(dumper, &dumper_list_head, dumper_list)
		if (!strncmp(dumper->name, name, 32))
			return dumper;

	/* not found */
	return NULL; 
#endif
	/* Temporary proof of concept */
	if (!strncmp(dumper_stage2.name, name, 32))
		return &dumper_stage2;
	else
		return NULL;
}

#ifdef CONFIG_CRASH_DUMP_SOFTBOOT
extern void dump_early_reserve_map(struct dump_memdev *);

void crashdump_reserve(void)
{
	extern unsigned long crashdump_addr;

	if (crashdump_addr == 0xdeadbeef) 
		return;

	/* reserve dump config and saved dump pages */
	dump_saved_config = (struct dump_config_block *)crashdump_addr;
	/* magic verification */
	if (dump_saved_config->magic != DUMP_MAGIC_LIVE) {
		printk("Invalid dump magic. Ignoring dump\n");
		dump_saved_config = NULL;
		return;
	}
			
	printk("Dump may be available from previous boot\n");

	reserve_bootmem(virt_to_phys((void *)crashdump_addr), 
		PAGE_ALIGN(sizeof(struct dump_config_block)));
	dump_early_reserve_map(&dump_saved_config->memdev);

}
#endif

/* 
 * Loads the dump configuration from a memory block saved across soft-boot
 * The ops vectors need fixing up as the corresp. routines may have 
 * relocated in the new soft-booted kernel.
 */
int dump_load_config(struct dump_config_block *config)
{
	struct dumper *dumper;
	struct dump_data_filter *filter_table, *filter;
	struct dump_dev *dev;
	int i;

	if (config->magic != DUMP_MAGIC_LIVE)
		return -ENOENT; /* not a valid config */

	/* initialize generic config data */
	memcpy(&dump_config, &config->config, sizeof(dump_config));

	/* initialize dumper state */
	if (!(dumper = dumper_by_name(config->dumper.name)))  {
		printk("dumper name mismatch\n");
		return -ENOENT; /* dumper mismatch */
	}
	
	/* verify and fixup schema */
	if (strncmp(dumper->scheme->name, config->scheme.name, 32)) {
		printk("dumper scheme mismatch\n");
		return -ENOENT; /* mismatch */
	}
	config->scheme.ops = dumper->scheme->ops;
	config->dumper.scheme = &config->scheme;
	
	/* verify and fixup filter operations */
	filter_table = dumper->filter;
	for (i = 0, filter = config->filter_table; 
		((i < MAX_PASSES) && filter_table[i].selector); 
		i++, filter++) {
		if (strncmp(filter_table[i].name, filter->name, 32)) {
			printk("dump filter mismatch\n");
			return -ENOENT; /* filter name mismatch */
		}
		filter->selector = filter_table[i].selector;
	}
	config->dumper.filter = config->filter_table;

	/* fixup format */
	if (strncmp(dumper->fmt->name, config->fmt.name, 32)) {
		printk("dump format mismatch\n");
		return -ENOENT; /* mismatch */
	}
	config->fmt.ops = dumper->fmt->ops;
	config->dumper.fmt = &config->fmt;

	/* fixup target device */
	dev = (struct dump_dev *)(&config->dev[0]);
	if (dumper->dev == NULL) {
		pr_debug("Vanilla dumper - assume default\n");
		if (dump_dev == NULL)
			return -ENODEV;
		dumper->dev = dump_dev;
	}

	if (strncmp(dumper->dev->type_name, dev->type_name, 32)) { 
		printk("dump dev type mismatch %s instead of %s\n",
				dev->type_name, dumper->dev->type_name);
		return -ENOENT; /* mismatch */
	}
	dev->ops = dumper->dev->ops; 
	config->dumper.dev = dev;
	
	/* fixup memory device containing saved dump pages */
	/* assume statically init'ed dump_memdev */
	config->memdev.ddev.ops = dump_memdev->ddev.ops; 
	/* switch to memdev from prev boot */
	saved_dump_memdev = dump_memdev; /* remember current */
	dump_memdev = &config->memdev;

	/* Make this the current primary dumper */
	dump_config.dumper = &config->dumper;

	return 0;
}

/* Saves the dump configuration in a memory block for use across a soft-boot */
int dump_save_config(struct dump_config_block *config)
{
	printk("saving dump config settings\n");

	/* dump config settings */
	memcpy(&config->config, &dump_config, sizeof(dump_config));

	/* dumper state */
	memcpy(&config->dumper, dump_config.dumper, sizeof(struct dumper));
	memcpy(&config->scheme, dump_config.dumper->scheme, 
		sizeof(struct dump_scheme));
	memcpy(&config->fmt, dump_config.dumper->fmt, sizeof(struct dump_fmt));
	memcpy(&config->dev[0], dump_config.dumper->dev, 
		sizeof(struct dump_anydev));
	memcpy(&config->filter_table, dump_config.dumper->filter, 
		sizeof(struct dump_data_filter)*MAX_PASSES);

	/* handle to saved mem pages */
	memcpy(&config->memdev, dump_memdev, sizeof(struct dump_memdev));

	config->magic = DUMP_MAGIC_LIVE;
	
	return 0;
}

int dump_init_stage2(struct dump_config_block *saved_config)
{
	int err = 0;

	pr_debug("dump_init_stage2\n");
	/* Check if dump from previous boot exists */
	if (saved_config) {
		printk("loading dumper from previous boot \n");
		/* load and configure dumper from previous boot */
		if ((err = dump_load_config(saved_config)))
			return err;

		if (!dump_oncpu) {
			if ((err = dump_configure(dump_config.dump_device))) {
				printk("Stage 2 dump configure failed\n");
				return err;
			}
		}

		dumper_reset();
		dump_dev = dump_config.dumper->dev;
		/* write out the dump */
		err = dump_generic_execute(NULL, NULL);
		
		dump_saved_config = NULL;

		if (!dump_oncpu) {
			dump_unconfigure(); 
		}
		
		return err;

	} else {
		/* no dump to write out */
		printk("no dumper from previous boot \n");
		return 0;
	}
}

extern void dump_mem_markpages(struct dump_memdev *);

int dump_switchover_stage(void)
{
	int ret = 0;

	/* trigger stage 2 rightaway - in real life would be after soft-boot */
	/* dump_saved_config would be a boot param */
	saved_dump_memdev = dump_memdev;
	saved_dumper = dump_config.dumper;
	ret = dump_init_stage2(dump_saved_config);
	dump_memdev = saved_dump_memdev;
	dump_config.dumper = saved_dumper;
	return ret;
}

int dump_activate_softboot(void) 
{
        int err = 0;
#ifdef CONFIG_KEXEC
        int num_cpus_online = 0;
        struct kimage *image;
#endif

        /* temporary - switchover to writeout previously saved dump */
#ifndef CONFIG_KEXEC
        err = dump_switchover_stage(); /* non-disruptive case */
        if (dump_oncpu)
	                dump_config.dumper = &dumper_stage1; /* set things back */

        return err;
#else

        dump_silence_level = DUMP_HALT_CPUS;
        /* wait till we become the only cpu */
        /* maybe by checking for online cpus ? */

        while((num_cpus_online = num_online_cpus()) > 1);

        /* now call into kexec */

        image = xchg(&kexec_image, 0);
        if (image) {
	                mdelay(panic_timeout*1000);
		                machine_kexec(image);
			        }


        /* TBD/Fixme:
	 *          * should we call reboot notifiers ? inappropriate for panic ?
	 *                   * what about device_shutdown() ?
	 *                            * is explicit bus master disabling needed or can we do that
	 *                                     * through driverfs ?
	 *                                              */
        return 0;
#endif
}

/* --- DUMP SCHEME ROUTINES  --- */

static inline int dump_buf_pending(struct dumper *dumper)
{
	return (dumper->curr_buf - dumper->dump_buf);
}

/* Invoked during stage 1 of soft-reboot based dumping */
int dump_overlay_sequencer(void)
{
	struct dump_data_filter *filter = dump_config.dumper->filter;
	struct dump_data_filter *filter2 = dumper_stage2.filter;
	int pass = 0, err = 0, save = 0;
	int (*action)(unsigned long, unsigned long);

	/* Make sure gzip compression is being used */
	if (dump_config.dumper->compress->compress_type != DUMP_COMPRESS_GZIP) {
		printk(" Please set GZIP compression \n");
		return -EINVAL;
	}

	/* start filling in dump data right after the header */
	dump_config.dumper->curr_offset = 
		PAGE_ALIGN(dump_config.dumper->header_len);

	/* Locate the last pass */
	for (;filter->selector; filter++, pass++);
	
	/* 
	 * Start from the end backwards: overlay involves a reverse 
	 * ordering of passes, since less critical pages are more
	 * likely to be reusable as scratch space once we are through
	 * with them. 
	 */
	for (--pass, --filter; pass >= 0; pass--, filter--)
	{
		/* Assumes passes are exclusive (even across dumpers) */
		/* Requires care when coding the selection functions */
		if ((save = filter->level_mask & dump_config.level))
			action = dump_save_data;
		else
			action = dump_skip_data;

		/* Remember the offset where this pass started */
		/* The second stage dumper would use this */
		if (dump_buf_pending(dump_config.dumper) & (PAGE_SIZE - 1)) {
			pr_debug("Starting pass %d with pending data\n", pass);
			pr_debug("filling dummy data to page-align it\n");
			dump_config.dumper->curr_buf = (void *)PAGE_ALIGN(
				(unsigned long)dump_config.dumper->curr_buf);
		}
		
		filter2[pass].start[0] = dump_config.dumper->curr_offset
			+ dump_buf_pending(dump_config.dumper);

		err = dump_iterator(pass, action, filter);

		filter2[pass].end[0] = dump_config.dumper->curr_offset
			+ dump_buf_pending(dump_config.dumper);
		filter2[pass].num_mbanks = 1;

		if (err < 0) {
			printk("dump_overlay_seq: failure %d in pass %d\n", 
				err, pass);
			break;
		}	
		printk("\n %d overlay pages %s of %d each in pass %d\n", 
		err, save ? "saved" : "skipped", DUMP_PAGE_SIZE, pass);
	}

	return err;
}

/* from dump_memdev.c */
extern struct page *dump_mem_lookup(struct dump_memdev *dev, unsigned long loc);
extern struct page *dump_mem_next_page(struct dump_memdev *dev);

static inline struct page *dump_get_saved_page(loff_t loc)
{
	return (dump_mem_lookup(dump_memdev, loc >> PAGE_SHIFT));
}

static inline struct page *dump_next_saved_page(void)
{
	return (dump_mem_next_page(dump_memdev));
}

/* 
 * Iterates over list of saved dump pages. Invoked during second stage of 
 * soft boot dumping
 *
 * Observation: If additional selection is desired at this stage then
 * a different iterator could be written which would advance 
 * to the next page header everytime instead of blindly picking up
 * the data. In such a case loc would be interpreted differently. 
 * At this moment however a blind pass seems sufficient, cleaner and
 * faster.
 */
int dump_saved_data_iterator(int pass, int (*action)(unsigned long, 
	unsigned long), struct dump_data_filter *filter)
{
	loff_t loc, end;
	struct page *page;
	unsigned long count = 0;
	int i, err = 0;
	unsigned long sz;

	for (i = 0; i < filter->num_mbanks; i++) {
		loc  = filter->start[i];
		end = filter->end[i];
		printk("pass %d, start off 0x%llx end offset 0x%llx\n", pass,
			loc, end);

		/* loc will get treated as logical offset into stage 1 */
		page = dump_get_saved_page(loc);
			
		for (; loc < end; loc += PAGE_SIZE) {
			dump_config.dumper->curr_loc = loc;
			if (!page) {
				printk("no more saved data for pass %d\n", 
					pass);
				break;
			}
			sz = (loc + PAGE_SIZE > end) ? end - loc : PAGE_SIZE;

			if (page && filter->selector(pass, (unsigned long)page, 
				PAGE_SIZE))  {
				pr_debug("mem offset 0x%llx\n", loc);
				if ((err = action((unsigned long)page, sz))) 
					break;
				else
					count++;
				/* clear the contents of page */
				/* fixme: consider using KM_DUMP instead */
				clear_highpage(page);
			
			}
			page = dump_next_saved_page();
		}
	}

	return err ? err : count;
}

static inline int dump_overlay_pages_done(struct page *page, int nr)
{
	int ret=0;

	for (; nr ; page++, nr--) {
		if (dump_check_and_free_page(dump_memdev, page))
			ret++;
	}
	return ret;
}

int dump_overlay_save_data(unsigned long loc, unsigned long len)
{
	int err = 0;
	struct page *page = (struct page *)loc;
	static unsigned long cnt = 0;

	if ((err = dump_generic_save_data(loc, len)))
		return err;

	if (dump_overlay_pages_done(page, len >> PAGE_SHIFT)) {
		cnt++;
		if (!(cnt & 0x7f))
			pr_debug("released page 0x%lx\n", page_to_pfn(page));
	}
	
	return err;
}


int dump_overlay_skip_data(unsigned long loc, unsigned long len)
{
	struct page *page = (struct page *)loc;

	dump_overlay_pages_done(page, len >> PAGE_SHIFT);
	return 0;
}

int dump_overlay_resume(void)
{
	int err = 0;

	/* 
	 * switch to stage 2 dumper, save dump_config_block
	 * and then trigger a soft-boot
	 */
	dumper_stage2.header_len = dump_config.dumper->header_len;
	dump_config.dumper = &dumper_stage2;
	if ((err = dump_save_config(dump_saved_config)))
		return err;

	dump_dev = dump_config.dumper->dev;

#ifdef CONFIG_KEXEC
        /* If we are doing a disruptive dump, activate softboot now */
        if((panic_timeout > 0) && (!(dump_config.flags & DUMP_FLAGS_NONDISRUPT)))
        err = dump_activate_softboot();
#endif
		
	return err;
	err = dump_switchover_stage();  /* plugs into soft boot mechanism */
	dump_config.dumper = &dumper_stage1; /* set things back */
	return err;
}

int dump_overlay_configure(unsigned long devid)
{
	struct dump_dev *dev;
	struct dump_config_block *saved_config = dump_saved_config;
	int err = 0;

	/* If there is a previously saved dump, write it out first */
	if (saved_config) {
		printk("Processing old dump pending writeout\n");
		err = dump_switchover_stage();
		if (err) {
			printk("failed to writeout saved dump\n");
			return err;
		}
		dump_free_mem(saved_config); /* testing only: not after boot */
	}

	dev = dumper_stage2.dev = dump_config.dumper->dev;
	/* From here on the intermediate dump target is memory-only */
	dump_dev = dump_config.dumper->dev = &dump_memdev->ddev;
	if ((err = dump_generic_configure(0))) {
		printk("dump generic configure failed: err %d\n", err);
		return err;
	}
	/* temporary */
	dumper_stage2.dump_buf = dump_config.dumper->dump_buf;

	/* Sanity check on the actual target dump device */
	if (!dev || (err = dev->ops->open(dev, devid))) {
		return err;
	}
	/* TBD: should we release the target if this is soft-boot only ? */

	/* alloc a dump config block area to save across reboot */
	if (!(dump_saved_config = dump_alloc_mem(sizeof(struct 
		dump_config_block)))) {
		printk("dump config block alloc failed\n");
		/* undo configure */
		dump_generic_unconfigure();
		return -ENOMEM;
	}
	dump_config.dump_addr = (unsigned long)dump_saved_config;
	printk("Dump config block of size %d set up at 0x%lx\n", 
		sizeof(*dump_saved_config), (unsigned long)dump_saved_config);
	return 0;
}

int dump_overlay_unconfigure(void)
{
	struct dump_dev *dev = dumper_stage2.dev;
	int err = 0;

	pr_debug("dump_overlay_unconfigure\n");
	/* Close the secondary device */
	dev->ops->release(dev); 
	pr_debug("released secondary device\n");

	err = dump_generic_unconfigure();
	pr_debug("Unconfigured generic portions\n");
	dump_free_mem(dump_saved_config);
	dump_saved_config = NULL;
	pr_debug("Freed saved config block\n");
	dump_dev = dump_config.dumper->dev = dumper_stage2.dev;

	printk("Unconfigured overlay dumper\n");
	return err;
}

int dump_staged_unconfigure(void)
{
	int err = 0;
	struct dump_config_block *saved_config = dump_saved_config;
	struct dump_dev *dev;

	pr_debug("dump_staged_unconfigure\n");
	err = dump_generic_unconfigure();

	/* now check if there is a saved dump waiting to be written out */
	if (saved_config) {
		printk("Processing saved dump pending writeout\n");
		if ((err = dump_switchover_stage())) {
			printk("Error in commiting saved dump at 0x%lx\n", 
				(unsigned long)saved_config);
			printk("Old dump may hog memory\n");
		} else {
			dump_free_mem(saved_config);
			pr_debug("Freed saved config block\n");
		}
		dump_saved_config = NULL;
	} else {
		dev = &dump_memdev->ddev;
		dev->ops->release(dev);
	}
	printk("Unconfigured second stage dumper\n");

	return 0;
}

/* ----- PASSTHRU FILTER ROUTINE --------- */

/* transparent - passes everything through */
int dump_passthru_filter(int pass, unsigned long loc, unsigned long sz)
{
	return 1;
}

/* ----- PASSTRU FORMAT ROUTINES ---- */


int dump_passthru_configure_header(const char *panic_str, const struct pt_regs *regs)
{
	dump_config.dumper->header_dirty++;
	return 0;
}

/* Copies bytes of data from page(s) to the specified buffer */
int dump_copy_pages(void *buf, struct page *page, unsigned long sz)
{
	unsigned long len = 0, bytes;
	void *addr;

	while (len < sz) {
		addr = kmap_atomic(page, KM_DUMP);
		bytes = (sz > len + PAGE_SIZE) ? PAGE_SIZE : sz - len;	
		memcpy(buf, addr, bytes); 
		kunmap_atomic(addr, KM_DUMP);
		buf += bytes;
		len += bytes;
		page++;
	}
	/* memset(dump_config.dumper->curr_buf, 0x57, len); temporary */

	return sz - len;
}

int dump_passthru_update_header(void)
{
	long len = dump_config.dumper->header_len;
	struct page *page;
	void *buf = dump_config.dumper->dump_buf;
	int err = 0;

	if (!dump_config.dumper->header_dirty)
		return 0;

	pr_debug("Copying header of size %ld bytes from memory\n", len);
	if (len > DUMP_BUFFER_SIZE) 
		return -E2BIG;

	page = dump_mem_lookup(dump_memdev, 0);
	for (; (len > 0) && page; buf += PAGE_SIZE, len -= PAGE_SIZE) {
		if ((err = dump_copy_pages(buf, page, PAGE_SIZE)))
			return err;
		page = dump_mem_next_page(dump_memdev);
	}
	if (len > 0) {
		printk("Incomplete header saved in mem\n");
		return -ENOENT;
	}

	if ((err = dump_dev_seek(0))) {
		printk("Unable to seek to dump header offset\n");
		return err;
	}
	err = dump_ll_write(dump_config.dumper->dump_buf, 
		buf - dump_config.dumper->dump_buf);
	if (err < dump_config.dumper->header_len)
		return (err < 0) ? err : -ENOSPC;

	dump_config.dumper->header_dirty = 0;
	return 0;
}

static loff_t next_dph_offset = 0;

static int dph_valid(struct __dump_page *dph)
{
	if ((dph->dp_address & (PAGE_SIZE - 1)) || (dph->dp_flags 
	      > DUMP_DH_COMPRESSED) || (!dph->dp_flags) ||
		(dph->dp_size > PAGE_SIZE)) {
	printk("dp->address = 0x%llx, dp->size = 0x%x, dp->flag = 0x%x\n",
		dph->dp_address, dph->dp_size, dph->dp_flags);
		return 0;
	}
	return 1;
}

int dump_verify_lcrash_data(void *buf, unsigned long sz)
{
	struct __dump_page *dph;

	/* sanity check for page headers */
	while (next_dph_offset + sizeof(*dph) < sz) {
		dph = (struct __dump_page *)(buf + next_dph_offset);
		if (!dph_valid(dph)) {
			printk("Invalid page hdr at offset 0x%llx\n",
				next_dph_offset);
			return -EINVAL;
		}
		next_dph_offset += dph->dp_size + sizeof(*dph);
	}

	next_dph_offset -= sz;	
	return 0;
}

/* 
 * TBD/Later: Consider avoiding the copy by using a scatter/gather 
 * vector representation for the dump buffer
 */
int dump_passthru_add_data(unsigned long loc, unsigned long sz)
{
	struct page *page = (struct page *)loc;
	void *buf = dump_config.dumper->curr_buf;
	int err = 0;

	if ((err = dump_copy_pages(buf, page, sz))) {
		printk("dump_copy_pages failed");
		return err;
	}

	if ((err = dump_verify_lcrash_data(buf, sz))) {
		printk("dump_verify_lcrash_data failed\n");
		printk("Invalid data for pfn 0x%lx\n", page_to_pfn(page));
		printk("Page flags 0x%lx\n", page->flags);
		printk("Page count 0x%x\n", atomic_read(&page->count));
		return err;
	}

	dump_config.dumper->curr_buf = buf + sz;

	return 0;
}


/* Stage 1 dumper: Saves compressed dump in memory and soft-boots system */

/* Scheme to overlay saved data in memory for writeout after a soft-boot */
struct dump_scheme_ops dump_scheme_overlay_ops = {
	.configure	= dump_overlay_configure,
	.unconfigure	= dump_overlay_unconfigure,
	.sequencer	= dump_overlay_sequencer,
	.iterator	= dump_page_iterator,
	.save_data	= dump_overlay_save_data,
	.skip_data	= dump_overlay_skip_data,
	.write_buffer	= dump_generic_write_buffer
};

struct dump_scheme dump_scheme_overlay = {
	.name		= "overlay",
	.ops		= &dump_scheme_overlay_ops
};


/* Stage 1 must use a good compression scheme - default to gzip */
extern struct __dump_compress dump_gzip_compression;

struct dumper dumper_stage1 = {
	.name		= "stage1",
	.scheme		= &dump_scheme_overlay,
	.fmt		= &dump_fmt_lcrash,
	.compress 	= &dump_none_compression, /* needs to be gzip */
	.filter		= dump_filter_table,
	.dev		= NULL,
};		

/* Stage 2 dumper: Activated after softboot to write out saved dump to device */

/* Formatter that transfers data as is (transparent) w/o further conversion */
struct dump_fmt_ops dump_fmt_passthru_ops = {
	.configure_header	= dump_passthru_configure_header,
	.update_header		= dump_passthru_update_header,
	.save_context		= NULL, /* unused */
	.add_data		= dump_passthru_add_data,
	.update_end_marker	= dump_lcrash_update_end_marker
};

struct dump_fmt dump_fmt_passthru = {
	.name	= "passthru",
	.ops	= &dump_fmt_passthru_ops
};

/* Filter that simply passes along any data within the range (transparent)*/
/* Note: The start and end ranges in the table are filled in at run-time */

extern int dump_filter_none(int pass, unsigned long loc, unsigned long sz);

struct dump_data_filter dump_passthru_filtertable[MAX_PASSES] = {
{.name = "passkern", .selector = dump_passthru_filter, 
	.level_mask = DUMP_MASK_KERN },
{.name = "passuser", .selector = dump_passthru_filter, 
	.level_mask = DUMP_MASK_USED },
{.name = "passunused", .selector = dump_passthru_filter, 
	.level_mask = DUMP_MASK_UNUSED },
{.name = "none", .selector = dump_filter_none, 
	.level_mask = DUMP_MASK_REST }
};


/* Scheme to handle data staged / preserved across a soft-boot */
struct dump_scheme_ops dump_scheme_staged_ops = {
	.configure	= dump_generic_configure,
	.unconfigure	= dump_staged_unconfigure,
	.sequencer	= dump_generic_sequencer,
	.iterator	= dump_saved_data_iterator,
	.save_data	= dump_generic_save_data,
	.skip_data	= dump_generic_skip_data,
	.write_buffer	= dump_generic_write_buffer
};

struct dump_scheme dump_scheme_staged = {
	.name		= "staged",
	.ops		= &dump_scheme_staged_ops
};

/* The stage 2 dumper comprising all these */
struct dumper dumper_stage2 = {
	.name		= "stage2",
	.scheme		= &dump_scheme_staged,
	.fmt		= &dump_fmt_passthru,
	.compress 	= &dump_none_compression,
	.filter		= dump_passthru_filtertable,
	.dev		= NULL,
};		

