/*
 *  linux/arch/arm/kernel/compat.c
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * We keep the old params compatibility cruft in one place (here)
 * so we don't end up with lots of mess around other places.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/page.h>

#include <asm/mach/arch.h>

static struct tag * __init memtag(struct tag *tag, unsigned long start, unsigned long size)
{
	tag = tag_next(tag);
	tag->hdr.tag = ATAG_MEM;
	tag->hdr.size = tag_size(tag_mem32);
	tag->u.mem.size = size;
	tag->u.mem.start = start;

	return tag;
}

static void __init build_tag_list(struct param_struct *params, void *taglist, int mem_init)
{
	struct tag *tag = taglist;

	printk(KERN_DEBUG "Converting old-style param struct to taglist\n");

	if (params->u1.s.page_size != PAGE_SIZE) {
		printk(KERN_WARNING "Warning: bad configuration page, "
		       "trying to continue\n");
		return;
	}

	tag->hdr.tag  = ATAG_CORE;
	tag->hdr.size = tag_size(tag_core);
	tag->u.core.flags = params->u1.s.flags & FLAG_READONLY;
	tag->u.core.pagesize = params->u1.s.page_size;
	tag->u.core.rootdev = params->u1.s.rootdev;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_RAMDISK;
	tag->hdr.size = tag_size(tag_ramdisk);
	tag->u.ramdisk.flags = (params->u1.s.flags & FLAG_RDLOAD ? 1 : 0) |
			       (params->u1.s.flags & FLAG_RDPROMPT ? 2 : 0);
	tag->u.ramdisk.size  = params->u1.s.ramdisk_size;
	tag->u.ramdisk.start = params->u1.s.rd_start;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_INITRD;
	tag->hdr.size = tag_size(tag_initrd);
	tag->u.initrd.start = params->u1.s.initrd_start;
	tag->u.initrd.size  = params->u1.s.initrd_size;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_SERIAL;
	tag->hdr.size = tag_size(tag_serialnr);
	tag->u.serialnr.low = params->u1.s.system_serial_low;
	tag->u.serialnr.high = params->u1.s.system_serial_high;

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_REVISION;
	tag->hdr.size = tag_size(tag_revision);
	tag->u.revision.rev = params->u1.s.system_rev;

	if (mem_init) {
#ifdef CONFIG_ARCH_ACORN
		if (machine_is_riscpc()) {
			int i;
			for (i = 0; i < 4; i++)
				tag = memtag(tag, PHYS_OFFSET + (i << 26),
					 params->u1.s.pages_in_bank[i] * PAGE_SIZE);
		} else
#endif
		tag = memtag(tag, PHYS_OFFSET, params->u1.s.nr_pages * PAGE_SIZE);
	}

#ifdef CONFIG_FOOTBRIDGE
	if (params->u1.s.mem_fclk_21285) {
		tag = tag_next(tag);
		tag->hdr.tag = ATAG_MEMCLK;
		tag->hdr.size = tag_size(tag_memclk);
		tag->u.memclk.fmemclk = params->u1.s.mem_fclk_21285;
	}
#endif

#ifdef CONFIG_ARCH_ACORN
	tag = tag_next(tag);
	tag->hdr.tag = ATAG_ACORN;
	tag->hdr.size = tag_size(tag_acorn);
	tag->u.acorn.memc_control_reg = params->u1.s.memc_control_reg;
	tag->u.acorn.vram_pages       = params->u1.s.pages_in_vram;
	tag->u.acorn.sounddefault     = params->u1.s.sounddefault;
	tag->u.acorn.adfsdrives       = params->u1.s.adfsdrives;
#endif

	tag = tag_next(tag);
	tag->hdr.tag = ATAG_CMDLINE;
	tag->hdr.size = (strlen(params->commandline) + 3 +
			 sizeof(struct tag_header)) >> 2;
	strcpy(tag->u.cmdline.cmdline, params->commandline);

	tag = tag_next(tag);
	tag->hdr.tag = 0;
	tag->hdr.size = 0;

	memmove(params, taglist, ((int)tag) - ((int)taglist) +
				 sizeof(struct tag_header));
}

void __init convert_to_tag_list(struct param_struct *params, int mem_init)
{
	build_tag_list(params, &params->u2, mem_init);
}
