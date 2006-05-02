/*
 * Default single stage dump scheme methods
 *
 * Previously a part of dump_base.c
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 * 	Split and rewrote LKCD dump scheme to generic dump method
 * 	interfaces
 * Derived from original code created by
 * 	Matt Robinson <yakker@sourceforge.net>)
 *
 * Contributions from SGI, IBM, HP, MCL, and others.
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/*
 * Implements the default dump scheme, i.e. single-stage gathering and
 * saving of dump data directly to the target device, which operates in
 * a push mode, where the dumping system decides what data it saves
 * taking into account pre-specified dump config options.
 *
 * Aside: The 2-stage dump scheme, where there is a soft-reset between
 * the gathering and saving phases, also reuses some of these
 * default routines (see dump_overlay.c)
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/nmi.h>
#include <linux/dump.h>
#include "dump_methods.h"

extern int panic_timeout;  /* time before reboot */

extern void dump_speedo(int);

/* Default sequencer used during single stage dumping */
/* Also invoked during stage 2 of soft-boot based dumping */
int dump_generic_sequencer(void)
{
	struct dump_data_filter *filter = dump_config.dumper->filter;
	int pass = 0, err = 0, save = 0;
	int (*action)(unsigned long, unsigned long);

	/*
	 * We want to save the more critical data areas first in
	 * case we run out of space, encounter i/o failures, or get
	 * interrupted otherwise and have to give up midway
	 * So, run through the passes in increasing order
	 */
	for (;filter->selector; filter++, pass++)
	{
		/* Assumes passes are exclusive (even across dumpers) */
		/* Requires care when coding the selection functions */
		if ((save = filter->level_mask & dump_config.level))
			action = dump_save_data;
		else
			action = dump_skip_data;

		if ((err = dump_iterator(pass, action, filter)) < 0)
			break;

		printk("LKCD: \n %d dump pages %s of %d each in pass %d\n",
		err, save ? "saved" : "skipped", (int)DUMP_PAGE_SIZE, pass);

	}

	return (err < 0) ? err : 0;
}

static inline struct page *dump_get_page(loff_t loc)
{

	unsigned long page_index = loc >> PAGE_SHIFT;

	/* todo: complete this  to account for ia64/discontig mem */
	/* todo: and to check for validity, ram page, no i/o mem etc */
	/* need to use pfn/physaddr equiv of kern_addr_valid */

	/* Important:
	 *   On ARM/XScale system, the physical address starts from
	 *   PHYS_OFFSET, and it maybe the situation that PHYS_OFFSET != 0.
	 *   For example on Intel's PXA250, PHYS_OFFSET = 0xa0000000. And the
	 *   page index starts from PHYS_PFN_OFFSET. When configuring
 	 *   filter, filter->start is assigned to 0 in dump_generic_configure.
	 *   Here we want to adjust it by adding PHYS_PFN_OFFSET to it!
	 */

	if (__dump_page_valid(page_index))
		return pfn_to_page(page_index);
	else
		return NULL;

}

/* Default iterator: for singlestage and stage 1 of soft-boot dumping */
/* Iterates over range of physical memory pages in DUMP_PAGE_SIZE increments */
int dump_page_iterator(int pass, int (*action)(unsigned long, unsigned long),
	struct dump_data_filter *filter)
{
	/* Todo : fix unit, type */
	loff_t loc, start, end;
	int i, count = 0, err = 0;
	struct page *page;

	/* Todo: Add membanks code */
	/* TBD: Check if we need to address DUMP_PAGE_SIZE < PAGE_SIZE */

	for (i = 0; i < filter->num_mbanks; i++) {
		start = filter->start[i];
		end = filter->end[i];
		for (loc = start; loc < end; loc += DUMP_PAGE_SIZE) {
			dump_config.dumper->curr_loc = loc;
			page = dump_get_page(loc);
			if (page && filter->selector(pass,
				(unsigned long) page, DUMP_PAGE_SIZE)) {
				if ((err = action((unsigned long)page,
					DUMP_PAGE_SIZE))) {
					printk("LKCD: dump_page_iterator: err %d"
						"for loc 0x%llx, in pass %d\n",
						err, loc, pass);
					return err ? err : count;
				} else
					count++;
			}
		}
	}

	return err ? err : count;
}

/*
 * Base function that saves the selected block of data in the dump
 * Action taken when iterator decides that data needs to be saved
 */
int dump_generic_save_data(unsigned long loc, unsigned long sz)
{
	void *buf;
	void *dump_buf = dump_config.dumper->dump_buf;
	int left, bytes, ret;

	if ((ret = dump_add_data(loc, sz))) {
		return ret;
	}
	buf = dump_config.dumper->curr_buf;

	/* If we've filled up the buffer write it out */
	if ((left = buf - dump_buf) >= DUMP_BUFFER_SIZE) {
		bytes = dump_write_buffer(dump_buf, DUMP_BUFFER_SIZE);
		if (bytes < DUMP_BUFFER_SIZE) {
			printk("LKCD: dump_write_buffer failed %d\n", bytes);
			return bytes ? -ENOSPC : bytes;
		}

		left -= bytes;

		/* -- A few chores to do from time to time -- */
		dump_config.dumper->count++;

		if (!(dump_config.dumper->count & 0x3f)) {
			/* Update the header every one in a while */
			memset((void *)dump_buf, 'b', DUMP_BUFFER_SIZE);
			if ((ret = dump_update_header()) < 0) {
				/* issue warning */
				return ret;
			}
			printk(".");

			touch_nmi_watchdog();
		} else if (!(dump_config.dumper->count & 0x7)) {
			/* Show progress so the user knows we aren't hung */
			dump_speedo(dump_config.dumper->count >> 3);
		}
		/* Todo: Touch/Refresh watchdog */

		/* --- Done with periodic chores -- */

		/*
		 * extra bit of copying to simplify verification
		 * in the second kernel boot based scheme
		 */
		memcpy(dump_buf - DUMP_PAGE_SIZE, dump_buf +
			DUMP_BUFFER_SIZE - DUMP_PAGE_SIZE, DUMP_PAGE_SIZE);

		/* now adjust the leftover bits back to the top of the page */
		/* this case would not arise during stage 2 (passthru) */
		memset(dump_buf, 'z', DUMP_BUFFER_SIZE);
		if (left) {
			memcpy(dump_buf, dump_buf + DUMP_BUFFER_SIZE, left);
		}
		buf -= DUMP_BUFFER_SIZE;
		dump_config.dumper->curr_buf = buf;
	}

	return 0;
}

int dump_generic_skip_data(unsigned long loc, unsigned long sz)
{
	/* dummy by default */
	return 0;
}

/*
 * Common low level routine to write a buffer to current dump device
 * Expects checks for space etc to have been taken care of by the caller
 * Operates serially at the moment for simplicity.
 * TBD/Todo: Consider batching for improved throughput
 */
int dump_ll_write(void *buf, unsigned long len)
{
	long transferred = 0, last_transfer = 0;
	int ret = 0;

	/* make sure device is ready */
	while ((ret = dump_dev_ready(NULL)) == -EAGAIN);
	if  (ret < 0) {
		printk("LKCD: dump_dev_ready failed !err %d\n", ret);
		return ret;
	}

	while (len) {
		if ((last_transfer = dump_dev_write(buf, len)) <= 0)  {
			ret = last_transfer;
			printk("LKCD: dump_dev_write failed !err %d\n",
			ret);
			break;
		}
		/* wait till complete */
		while ((ret = dump_dev_ready(buf)) == -EAGAIN)
			cpu_relax();

		if  (ret < 0) {
			printk("LKCD: i/o failed !err %d\n", ret);
			break;
		}

		len -= last_transfer;
		buf += last_transfer;
		transferred += last_transfer;
	}
	return (ret < 0) ? ret : transferred;
}

/* default writeout routine for single dump device */
/* writes out the dump data ensuring enough space is left for the end marker */
int dump_generic_write_buffer(void *buf, unsigned long len)
{
	long written = 0;
	int err = 0;

	/* check for space */
	if ((err = dump_dev_seek(dump_config.dumper->curr_offset + len +
			2*DUMP_BUFFER_SIZE)) < 0) {
		printk("LKCD: dump_write_buffer: insuff space after offset 0x%llx\n",
			dump_config.dumper->curr_offset);
		return err;
	}
	/* alignment check would happen as a side effect of this */
	if ((err = dump_dev_seek(dump_config.dumper->curr_offset)) < 0)
		return err;

	written = dump_ll_write(buf, len);

	/* all or none */

	if (written < len)
		written = written ? -ENOSPC : written;
	else
		dump_config.dumper->curr_offset += len;

	return written;
}

int dump_generic_configure(const char *devid)
{
	struct dump_dev *dev = dump_config.dumper->dev;
	struct dump_data_filter *filter;
	void *buf;
	int ret = 0;

	/* Allocate the dump buffer and initialize dumper state */
	/* Assume that we get aligned addresses */
	if (!(buf = dump_alloc_mem(DUMP_BUFFER_SIZE + 3 * DUMP_PAGE_SIZE)))
		return -ENOMEM;

	if ((unsigned long)buf & (PAGE_SIZE - 1)) {
		/* sanity check for page aligned address */
		dump_free_mem(buf);
		return -ENOMEM; /* fixme: better error code */
	}

	/* Initialize the rest of the fields */
	dump_config.dumper->dump_buf = buf + DUMP_PAGE_SIZE;
	dumper_reset();

	/* Open the dump device */
	if (!dev)
		return -ENODEV;

	if ((ret = dev->ops->open(dev, devid))) {
	       return ret;
	}

	/* Initialise the memory ranges in the dump filter */
	for (filter = dump_config.dumper->filter ;filter->selector; filter++) {
		if (!filter->start[0] && !filter->end[0]) {
			pg_data_t *pgdat;
			int i = 0;
			for_each_pgdat(pgdat) {
				filter->start[i] =
					(loff_t)pgdat->node_start_pfn << PAGE_SHIFT;
				filter->end[i] =
					(loff_t)(pgdat->node_start_pfn + pgdat->node_spanned_pages) << PAGE_SHIFT;
				i++;
			}
			filter->num_mbanks = i;
		}
	}

	return 0;
}

int dump_generic_unconfigure(void)
{
	struct dump_dev *dev = dump_config.dumper->dev;
	void *buf = dump_config.dumper->dump_buf;
	int ret = 0;

	printk("LKCD: Generic unconfigure\n");
	/* Close the dump device */
	if (dev && (ret = dev->ops->release(dev)))
		return ret;

	printk("LKCD: Closed dump device\n");

	if (buf)
		dump_free_mem((buf - DUMP_PAGE_SIZE));

	dump_config.dumper->curr_buf = dump_config.dumper->dump_buf = NULL;
	printk("LKCD: Released dump buffer\n");

	return 0;
}

#ifdef CONFIG_DISCONTIGMEM

void dump_reconfigure_mbanks(void)
{
        pg_data_t *pgdat;
        loff_t start, end, loc, loc_end;
        int i=0;
        struct dump_data_filter *filter = dump_config.dumper->filter;

        for_each_pgdat(pgdat) {

                start = (loff_t)(pgdat->node_start_pfn << PAGE_SHIFT);
                end = ((loff_t)(pgdat->node_start_pfn + pgdat->node_spanned_pages) << PAGE_SHIFT);
		for(loc = start; loc < end; loc += (DUMP_PAGE_SIZE)) {

                        if(!(__dump_page_valid(loc >> PAGE_SHIFT)))
                                continue;

                        if (i == MAX_MBANKS) {
				printk("Too many mbanks.  Dump will be incomplete.\n");
				break;
			}

                        /* We found a valid page. This is the start */
                        filter->start[i] = loc;

                        /* Now loop here till you find the end */
                        for(loc_end = loc; loc_end < end; loc_end += (DUMP_PAGE_SIZE)) {

				if(__dump_page_valid(loc_end >> PAGE_SHIFT)) {
                                /* This page could very well be the last page */
                                        filter->end[i] = loc_end;
                                        continue;
                                }
                                break;
                        }
                        i++;
                        loc = loc_end;
                }
        }
        filter->num_mbanks = i;

        /* Propagate memory bank information to other filters */
        for (filter = dump_config.dumper->filter, filter++ ;filter->selector; filter++) {
                for(i = 0; i < dump_config.dumper->filter->num_mbanks; i++) {
                        filter->start[i] = dump_config.dumper->filter->start[i];
                        filter->end[i] = dump_config.dumper->filter->end[i];
                }
                filter->num_mbanks = dump_config.dumper->filter->num_mbanks;
        }
}
#endif

/* Set up the default dump scheme */

struct dump_scheme_ops dump_scheme_singlestage_ops = {
	.configure	= dump_generic_configure,
	.unconfigure	= dump_generic_unconfigure,
	.sequencer	= dump_generic_sequencer,
	.iterator	= dump_page_iterator,
	.save_data	= dump_generic_save_data,
	.skip_data	= dump_generic_skip_data,
	.write_buffer	= dump_generic_write_buffer,
};

struct dump_scheme dump_scheme_singlestage = {
	.name		= "single-stage",
	.ops		= &dump_scheme_singlestage_ops
};

/* The single stage dumper comprising all these */
struct dumper dumper_singlestage = {
	.name		= "single-stage",
	.scheme		= &dump_scheme_singlestage,
	.fmt		= &dump_fmt_lcrash,
	.compress 	= &dump_none_compression,
	.filter		= dump_filter_table,
	.dev		= NULL,
};

