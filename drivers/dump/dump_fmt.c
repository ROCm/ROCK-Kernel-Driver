/*
 * Implements the routines which handle the format specific
 * aspects of dump for the default dump format.
 *
 * Used in single stage dumping and stage 1 of soft-boot based dumping 
 * Saves data in LKCD (lcrash) format 
 *
 * Previously a part of dump_base.c
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 *	Split off and reshuffled LKCD dump format code around generic
 *	dump method interfaces.
 *
 * Derived from original code created by 
 * 	Matt Robinson <yakker@sourceforge.net>)
 *
 * Contributions from SGI, IBM, HP, MCL, and others.
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2000 - 2002 TurboLinux, Inc.  All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * This code is released under version 2 of the GNU GPL.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/utsname.h>
#include <asm/dump.h>
#include <linux/dump.h>
#include "dump_methods.h"

/*
 * SYSTEM DUMP LAYOUT
 * 
 * System dumps are currently the combination of a dump header and a set
 * of data pages which contain the system memory.  The layout of the dump
 * (for full dumps) is as follows:
 *
 *             +-----------------------------+
 *             |     generic dump header     |
 *             +-----------------------------+
 *             |   architecture dump header  |
 *             +-----------------------------+
 *             |         page header         |
 *             +-----------------------------+
 *             |          page data          |
 *             +-----------------------------+
 *             |         page header         |
 *             +-----------------------------+
 *             |          page data          |
 *             +-----------------------------+
 *             |              |              |
 *             |              |              |
 *             |              |              |
 *             |              |              |
 *             |              V              |
 *             +-----------------------------+
 *             |        PAGE_END header      |
 *             +-----------------------------+
 *
 * There are two dump headers, the first which is architecture
 * independent, and the other which is architecture dependent.  This
 * allows different architectures to dump different data structures
 * which are specific to their chipset, CPU, etc.
 *
 * After the dump headers come a succession of dump page headers along
 * with dump pages.  The page header contains information about the page
 * size, any flags associated with the page (whether it's compressed or
 * not), and the address of the page.  After the page header is the page
 * data, which is either compressed (or not).  Each page of data is
 * dumped in succession, until the final dump header (PAGE_END) is
 * placed at the end of the dump, assuming the dump device isn't out
 * of space.
 *
 * This mechanism allows for multiple compression types, different
 * types of data structures, different page ordering, etc., etc., etc.
 * It's a very straightforward mechanism for dumping system memory.
 */

struct __dump_header dump_header;  /* the primary dump header              */
struct __dump_header_asm dump_header_asm; /* the arch-specific dump header */

/*
 *  Set up common header fields (mainly the arch indep section) 
 *  Per-cpu state is handled by lcrash_save_context
 *  Returns the size of the header in bytes.
 */
static int lcrash_init_dump_header(const char *panic_str)
{
	struct timeval dh_time;
	unsigned long temp_dha_stack[DUMP_MAX_NUM_CPUS];

	/* make sure the dump header isn't TOO big */
	if ((sizeof(struct __dump_header) +
		sizeof(struct __dump_header_asm)) > DUMP_BUFFER_SIZE) {
			printk("lcrash_init_header(): combined "
				"headers larger than DUMP_BUFFER_SIZE!\n");
			return -E2BIG;
	}

	/* initialize the dump headers to zero */
	/* save dha_stack pointer because it may contains pointer for stack! */
	memcpy(&(temp_dha_stack[0]), &(dump_header_asm.dha_stack[0]),
		DUMP_MAX_NUM_CPUS * sizeof(unsigned long));
	memset(&dump_header, 0, sizeof(dump_header));
	memset(&dump_header_asm, 0, sizeof(dump_header_asm));
	memcpy(&(dump_header_asm.dha_stack[0]), &(temp_dha_stack[0]),
		DUMP_MAX_NUM_CPUS * sizeof(unsigned long));

	/* configure dump header values */
	dump_header.dh_magic_number = DUMP_MAGIC_NUMBER;
	dump_header.dh_version = DUMP_VERSION_NUMBER;
	dump_header.dh_memory_start = PAGE_OFFSET;
	dump_header.dh_memory_end = DUMP_MAGIC_NUMBER;
	dump_header.dh_header_size = sizeof(struct __dump_header);
	dump_header.dh_page_size = PAGE_SIZE;
	dump_header.dh_dump_level = dump_config.level;
	dump_header.dh_current_task = (unsigned long) current;
	dump_header.dh_dump_compress = dump_config.dumper->compress->
		compress_type;
	dump_header.dh_dump_flags = dump_config.flags;
	dump_header.dh_dump_device = dump_config.dumper->dev->device_id; 

#if DUMP_DEBUG >= 6
	dump_header.dh_num_bytes = 0;
#endif
	dump_header.dh_num_dump_pages = 0;
	do_gettimeofday(&dh_time);
	dump_header.dh_time.tv_sec = dh_time.tv_sec;
	dump_header.dh_time.tv_usec = dh_time.tv_usec;

	memcpy((void *)&(dump_header.dh_utsname_sysname), 
		(const void *)&(system_utsname.sysname), __NEW_UTS_LEN + 1);
	memcpy((void *)&(dump_header.dh_utsname_nodename), 
		(const void *)&(system_utsname.nodename), __NEW_UTS_LEN + 1);
	memcpy((void *)&(dump_header.dh_utsname_release), 
		(const void *)&(system_utsname.release), __NEW_UTS_LEN + 1);
	memcpy((void *)&(dump_header.dh_utsname_version), 
		(const void *)&(system_utsname.version), __NEW_UTS_LEN + 1);
	memcpy((void *)&(dump_header.dh_utsname_machine), 
		(const void *)&(system_utsname.machine), __NEW_UTS_LEN + 1);
	memcpy((void *)&(dump_header.dh_utsname_domainname), 
		(const void *)&(system_utsname.domainname), __NEW_UTS_LEN + 1);

	if (panic_str) {
		memcpy((void *)&(dump_header.dh_panic_string),
			(const void *)panic_str, DUMP_PANIC_LEN);
	}

        dump_header_asm.dha_magic_number = DUMP_ASM_MAGIC_NUMBER;
        dump_header_asm.dha_version = DUMP_ASM_VERSION_NUMBER;
        dump_header_asm.dha_header_size = sizeof(dump_header_asm);
#ifdef CONFIG_ARM
	dump_header_asm.dha_physaddr_start = PHYS_OFFSET;
#endif

	dump_header_asm.dha_smp_num_cpus = num_online_cpus();
	pr_debug("smp_num_cpus in header %d\n", 
		dump_header_asm.dha_smp_num_cpus);

	dump_header_asm.dha_dumping_cpu = smp_processor_id();
	
	return sizeof(dump_header) + sizeof(dump_header_asm);
}


int dump_lcrash_configure_header(const char *panic_str, 
	const struct pt_regs *regs)
{
	int retval = 0;

	dump_config.dumper->header_len = lcrash_init_dump_header(panic_str);

	/* capture register states for all processors */
	dump_save_this_cpu(regs);
	__dump_save_other_cpus(); /* side effect:silence cpus */

	/* configure architecture-specific dump header values */
	if ((retval = __dump_configure_header(regs))) 
		return retval;

	dump_config.dumper->header_dirty++;
	return 0;
}

/* save register and task context */
void dump_lcrash_save_context(int cpu, const struct pt_regs *regs, 
	struct task_struct *tsk)
{
	dump_header_asm.dha_smp_current_task[cpu] = (unsigned long)tsk;

	__dump_save_regs(&dump_header_asm.dha_smp_regs[cpu], regs);

	/* take a snapshot of the stack */
	/* doing this enables us to tolerate slight drifts on this cpu */
	if (dump_header_asm.dha_stack[cpu]) {
		memcpy((void *)dump_header_asm.dha_stack[cpu],
				tsk->thread_info, THREAD_SIZE);
	}
	dump_header_asm.dha_stack_ptr[cpu] = (unsigned long)(tsk->thread_info);
}

/* write out the header */
int dump_write_header(void)
{
	int retval = 0, size;
	void *buf = dump_config.dumper->dump_buf;

	/* accounts for DUMP_HEADER_OFFSET if applicable */
	if ((retval = dump_dev_seek(0))) {
		printk("Unable to seek to dump header offset: %d\n", 
			retval);
		return retval;
	}

	memcpy(buf, (void *)&dump_header, sizeof(dump_header));
	size = sizeof(dump_header);
	memcpy(buf + size, (void *)&dump_header_asm, sizeof(dump_header_asm));
	size += sizeof(dump_header_asm);
	size = PAGE_ALIGN(size);
	retval = dump_ll_write(buf , size);

	if (retval < size) 
		return (retval >= 0) ? ENOSPC : retval;
	return 0;
}

int dump_generic_update_header(void)
{
	int err = 0;

	if (dump_config.dumper->header_dirty) {
		if ((err = dump_write_header())) {
			printk("dump write header failed !err %d\n", err);
		} else {
			dump_config.dumper->header_dirty = 0;
		}
	}

	return err;
}

static inline int is_curr_stack_page(struct page *page, unsigned long size)
{
	unsigned long thread_addr = (unsigned long)current_thread_info();
	unsigned long addr = (unsigned long)page_address(page);

	return !PageHighMem(page) && (addr < thread_addr + THREAD_SIZE)
		&& (addr + size > thread_addr);
}

static inline int is_dump_page(struct page *page, unsigned long size)
{
	unsigned long addr = (unsigned long)page_address(page);
	unsigned long dump_buf = (unsigned long)dump_config.dumper->dump_buf;

	return !PageHighMem(page) && (addr < dump_buf + DUMP_BUFFER_SIZE)
		&& (addr + size > dump_buf);
}

int dump_allow_compress(struct page *page, unsigned long size)
{
	/*
	 * Don't compress the page if any part of it overlaps
	 * with the current stack or dump buffer (since the contents
	 * in these could be changing while compression is going on)
	 */
	return !is_curr_stack_page(page, size) && !is_dump_page(page, size);
}

void lcrash_init_pageheader(struct __dump_page *dp, struct page *page, 
	unsigned long sz)
{
	memset(dp, sizeof(struct __dump_page), 0);
	dp->dp_flags = 0; 
	dp->dp_size = 0;
	if (sz > 0)
		dp->dp_address = (loff_t)page_to_pfn(page) << PAGE_SHIFT;

#if DUMP_DEBUG > 6
	dp->dp_page_index = dump_header.dh_num_dump_pages;
	dp->dp_byte_offset = dump_header.dh_num_bytes + DUMP_BUFFER_SIZE
		+ DUMP_HEADER_OFFSET; /* ?? */
#endif /* DUMP_DEBUG */
}

int dump_lcrash_add_data(unsigned long loc, unsigned long len)
{
	struct page *page = (struct page *)loc;
	void *addr, *buf = dump_config.dumper->curr_buf;
	struct __dump_page *dp = (struct __dump_page *)buf; 
	int bytes, size;

	if (buf > dump_config.dumper->dump_buf + DUMP_BUFFER_SIZE)
		return -ENOMEM;

	lcrash_init_pageheader(dp, page, len);
	buf += sizeof(struct __dump_page);

	while (len) {
		addr = kmap_atomic(page, KM_DUMP);
		size = bytes = (len > PAGE_SIZE) ? PAGE_SIZE : len;	
		/* check for compression */
		if (dump_allow_compress(page, bytes)) {
			size = dump_compress_data((char *)addr, bytes, (char *)buf);
		}
		/* set the compressed flag if the page did compress */
		if (size && (size < bytes)) {
			dp->dp_flags |= DUMP_DH_COMPRESSED;
		} else {
			/* compression failed -- default to raw mode */
			dp->dp_flags |= DUMP_DH_RAW;
			memcpy(buf, addr, bytes);
			size = bytes;
		}
		/* memset(buf, 'A', size); temporary: testing only !! */
		kunmap_atomic(addr, KM_DUMP);
		dp->dp_size += size;
		buf += size;
		len -= bytes;
		page++;
	}

	/* now update the header */
#if DUMP_DEBUG > 6
	dump_header.dh_num_bytes += dp->dp_size + sizeof(*dp);
#endif
	dump_header.dh_num_dump_pages++;
	dump_config.dumper->header_dirty++;

	dump_config.dumper->curr_buf = buf;	

	return len;
}

int dump_lcrash_update_end_marker(void)
{
	struct __dump_page *dp = 
		(struct __dump_page *)dump_config.dumper->curr_buf;
	unsigned long left;
	int ret = 0;
		
	lcrash_init_pageheader(dp, NULL, 0);
	dp->dp_flags |= DUMP_DH_END; /* tbd: truncation test ? */
	
	/* now update the header */
#if DUMP_DEBUG > 6
	dump_header.dh_num_bytes += sizeof(*dp);
#endif
	dump_config.dumper->curr_buf += sizeof(*dp);
	left = dump_config.dumper->curr_buf - dump_config.dumper->dump_buf;

	printk("\n");

	while (left) {
		if ((ret = dump_dev_seek(dump_config.dumper->curr_offset))) {
			printk("Seek failed at offset 0x%llx\n", 
			dump_config.dumper->curr_offset);
			return ret;
		}

		if (DUMP_BUFFER_SIZE > left) 
			memset(dump_config.dumper->curr_buf, 'm', 
				DUMP_BUFFER_SIZE - left);

		if ((ret = dump_ll_write(dump_config.dumper->dump_buf, 
			DUMP_BUFFER_SIZE)) < DUMP_BUFFER_SIZE) {
			return (ret < 0) ? ret : -ENOSPC;
		}

		dump_config.dumper->curr_offset += DUMP_BUFFER_SIZE;
	
		if (left > DUMP_BUFFER_SIZE) {
			left -= DUMP_BUFFER_SIZE;
			memcpy(dump_config.dumper->dump_buf, 
			dump_config.dumper->dump_buf + DUMP_BUFFER_SIZE, left);
			dump_config.dumper->curr_buf -= DUMP_BUFFER_SIZE;
		} else {
			left = 0;
		}
	}
	return 0;
}


/* Default Formatter (lcrash) */
struct dump_fmt_ops dump_fmt_lcrash_ops = {
	.configure_header	= dump_lcrash_configure_header,
	.update_header		= dump_generic_update_header,
	.save_context		= dump_lcrash_save_context,
	.add_data		= dump_lcrash_add_data,
	.update_end_marker	= dump_lcrash_update_end_marker
};

struct dump_fmt dump_fmt_lcrash = {
	.name	= "lcrash",
	.ops	= &dump_fmt_lcrash_ops
};

