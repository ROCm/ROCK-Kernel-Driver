/*
 * Copyright (C) 1999-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug page information");
MODULE_LICENSE("GPL");

/* Standard Linux page stuff */

#ifndef CONFIG_DISCONTIGMEM
static char	*pg_flag_vals[] = {
	"PG_locked", "PG_error", "PG_referenced", "PG_uptodate",
	"PG_dirty", "PG_lru", "PG_active", "PG_slab",
	"PG_highmem", "PG_checked", "PG_arch_1", "PG_reserved",
	"PG_private", "PG_writeback", "PG_nosave", "PG_chainlock",
	"PG_direct", "PG_mappedtodisk", "PG_reclaim", "PG_compound",
	NULL };
#endif

static char	*bh_state_vals[] = {
	"Uptodate", "Dirty", "Lock", "Req",
	"Mapped", "New", "Async_read", "Async_write",
	"Delay", "Boundary", "Write_EIO",
	"Private",
	NULL };

static char *inode_flag_vals[] = {
	"I_DIRTY_SYNC", "I_DIRTY_DATASYNC", "I_DIRTY_PAGES", "I_LOCK",
	"I_FREEING", "I_CLEAR", "I_NEW",
	NULL };

static char	*map_flags(unsigned long flags, char *mapping[])
{
	static	char	buffer[256];
	int	index;
	int	offset = 12;

	buffer[0] = '\0';

	for (index = 0; flags && mapping[index]; flags >>= 1, index++) {
		if (flags & 1) {
			if ((offset + strlen(mapping[index]) + 1) >= 80) {
				strcat(buffer, "\n            ");
				offset = 12;
			} else if (offset > 12) {
				strcat(buffer, " ");
				offset++;
			}
			strcat(buffer, mapping[index]);
			offset += strlen(mapping[index]);
		}
	}

	return (buffer);
}

static int
kdbm_buffers(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct buffer_head	bh;
	unsigned long addr;
	long	offset=0;
	int nextarg;
	int diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
	    (diag = kdb_getarea(bh, addr)))
		return(diag);

	kdb_printf("buffer_head at 0x%lx\n", addr);
	kdb_printf("  bno %llu size %d dev 0x%x\n",
		(unsigned long long)bh.b_blocknr,
		bh.b_size,
		bh.b_bdev->bd_dev);
	kdb_printf("  count %d state 0x%lx [%s]\n",
		bh.b_count.counter, bh.b_state,
		map_flags(bh.b_state, bh_state_vals));
	kdb_printf("  b_data 0x%p\n",
		bh.b_data);
	kdb_printf("  b_page 0x%p b_this_page 0x%p b_private 0x%p\n",
		bh.b_page, bh.b_this_page, bh.b_private);
	kdb_printf("  b_end_io ");
	if (bh.b_end_io)
		kdb_symbol_print(kdba_funcptr_value(bh.b_end_io), NULL, KDB_SP_VALUE);
	else
		kdb_printf("(NULL)");
	kdb_printf("\n");

	return 0;
}

#ifndef CONFIG_DISCONTIGMEM
static char	*page_flags(unsigned long flags)
{
	return(map_flags(flags, pg_flag_vals));
}

static int
kdbm_page(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct page	page;
	unsigned long addr;
	long	offset=0;
	int nextarg;
	int diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	if (addr < PAGE_OFFSET)
		addr = (unsigned long) &mem_map[addr];

	if ((diag = kdb_getarea(page, addr)))
		return(diag);

	kdb_printf("struct page at 0x%lx\n", addr);
	kdb_printf("  next 0x%p prev 0x%p addr space 0x%p index %lu (offset 0x%x)\n",
		   page.list.next, page.list.prev, page.mapping, page.index,
		   (int)(page.index << PAGE_CACHE_SHIFT));
	kdb_printf("  count %d flags %s\n",
		   page.count.counter, page_flags(page.flags));
	kdb_printf("  virtual 0x%p\n", page_address((struct page *)addr));
	if (page_has_buffers(&page))
		kdb_printf("  buffers 0x%p\n", page_buffers(&page));

	return 0;
}
#endif /* CONFIG_DISCONTIGMEM */

unsigned long
print_request(unsigned long addr)
{
	struct request	rq;

	if (kdb_getarea(rq, addr))
		return(0);

	kdb_printf("struct request at 0x%lx\n", addr);
	kdb_printf("  errors %d sector %llu nr_sectors %lu waiting 0x%p\n",
			rq.errors,
			(unsigned long long)rq.sector, rq.nr_sectors,
			rq.waiting);

	kdb_printf("  hsect %llu hnrsect %lu nrseg %u nrhwseg %u currnrsect %u\n",
			(unsigned long long)rq.hard_sector, rq.hard_nr_sectors,
			rq.nr_phys_segments, rq.nr_hw_segments,
			rq.current_nr_sectors);

	return (unsigned long) rq.queuelist.next;
}

static int
kdbm_request(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	long	offset=0;
	unsigned long addr;
	int nextarg;
	int diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	print_request(addr);
	return 0;
}


static int
kdbm_rqueue(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct request_queue	rq;
	unsigned long addr, head_addr, next;
	long	offset=0;
	int nextarg;
	int i, diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
	    (diag = kdb_getarea(rq, addr)))
		return(diag);

	kdb_printf("struct request_queue at 0x%lx\n", addr);
	i = 0;
	next = (unsigned long)rq.queue_head.next;
	head_addr = addr + offsetof(struct request_queue, queue_head);
	kdb_printf(" request queue: %s\n", next == head_addr ?
		"empty" : "");
	while (next != head_addr) {
		i++;
		next = print_request(next);
	}

	if (i)
		kdb_printf("%d requests found\n", i);

	return 0;
}


static void
do_buffer(unsigned long addr)
{
	struct buffer_head	bh;
	
	if (kdb_getarea(bh, addr))
		return;

	kdb_printf("bh 0x%lx bno %8llu [%s]\n", addr,
		 (unsigned long long)bh.b_blocknr,
		 map_flags(bh.b_state, bh_state_vals));
}

#if 0
static int
kdbm_inode_pages(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct inode *inode = NULL;
	struct address_space *ap = NULL;
	unsigned long addr, addr1 = 0;
	long offset=0;
	int nextarg;
	int diag;
	int which=0;

	struct list_head *head, *curr;
	
	if (argc < 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		goto out;

	if (argc == 2) {
		nextarg = 2;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr1,
					&offset, NULL, regs);
		if (diag)
			goto out;
		kdb_printf("Looking for page index 0x%lx ... \n", addr1);
	}

	if (!(inode = kmalloc(sizeof(*inode), GFP_ATOMIC))) {
		kdb_printf("kdbm_inode_pages: cannot kmalloc inode\n");
		goto out;
	}
	if (!(ap = kmalloc(sizeof(*ap), GFP_ATOMIC))) {
		kdb_printf("kdbm_inode_pages: cannot kmalloc ap\n");
		goto out;
	}
	if ((diag = kdb_getarea(*inode, addr)))
		goto out;
	if (!inode->i_mapping) {
		kdb_printf("inode has no mapping\n");
		goto out;
	}
	if ((diag = kdb_getarea(*ap, (unsigned long) inode->i_mapping)))
		goto out;
	
 again:
	if (which == 0){
	  which=1;
	  head = &inode->i_mapping->clean_pages;
	  kdb_printf("CLEAN  page_struct   index  cnt  flags\n");
	} else if (which == 1) {
	  which=2;
	  head = &inode->i_mapping->dirty_pages;
	  kdb_printf("DIRTY  page_struct   index  cnt  flags\n");
	} else if (which == 2) {
	  which=3;
	  head = &inode->i_mapping->locked_pages;
	  kdb_printf("LOCKED page_struct   index  cnt  flags\n");
	} else {
	  goto out;
	}
	
	curr = head->next;
	while (curr != head) {
		struct page 	 page;
		struct list_head curr_struct;

		addr = (unsigned long) list_entry(curr, struct page, list);
		if ((diag = kdb_getarea(page, addr)))
			goto out;

		if (!addr1 || page.index == addr1 ||
			(addr1 == -1 && (page.flags & ( 1 << PG_locked))))
		{
			kdb_printf("    0x%lx    %6lu    %5d    0x%lx ",
				addr, page.index, page.count.counter,
				page.flags);
			if (page_has_buffers(&page))
				do_buffer((unsigned long) page_buffers(&page));
			else
				kdb_printf("bh [NULL]\n");
		}

		if ((diag = kdb_getarea(curr_struct, (unsigned long) curr)))
			goto out;

		curr = curr_struct.next;
	}
	goto again;
 out:
	if (inode)
		kfree(inode);
	if (ap)
		kfree(ap);
	return diag;
}
#endif

static int
kdbm_inode(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct inode *inode = NULL;
	unsigned long addr;
	unsigned char *iaddr;
	long	offset=0;
	int nextarg;
	int diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)))
		goto out;
	if (!(inode = kmalloc(sizeof(*inode), GFP_ATOMIC))) {
		kdb_printf("kdbm_inode: cannot kmalloc inode\n");
		goto out;
	}
	if ((diag = kdb_getarea(*inode, addr)))
		goto out;

	kdb_printf("struct inode at  0x%lx\n", addr);

	kdb_printf(" i_ino = %lu i_count = %u i_size %Ld\n",
					inode->i_ino, atomic_read(&inode->i_count),
					inode->i_size);

	kdb_printf(" i_mode = 0%o  i_nlink = %d  i_rdev = 0x%x\n",
					inode->i_mode, inode->i_nlink,
					inode->i_rdev);

	kdb_printf(" i_hash.nxt = 0x%p i_hash.pprev = 0x%p\n",
		inode->i_hash.next,
		inode->i_hash.pprev);

	kdb_printf(" i_list.nxt = 0x%p i_list.prv = 0x%p\n",
		list_entry(inode->i_list.next, struct inode, i_list),
		list_entry(inode->i_list.prev, struct inode, i_list));

	kdb_printf(" i_dentry.nxt = 0x%p i_dentry.prv = 0x%p\n",
		list_entry(inode->i_dentry.next, struct dentry, d_alias),
		list_entry(inode->i_dentry.prev, struct dentry, d_alias));

	kdb_printf(" i_sb = 0x%p i_op = 0x%p i_data = 0x%lx nrpages = %lu\n",
					inode->i_sb, inode->i_op,
					addr + offsetof(struct inode, i_data),
					inode->i_data.nrpages);
	kdb_printf(" i_fop= 0x%p i_flock = 0x%p i_mapping = 0x%p\n",
			   inode->i_fop, inode->i_flock, inode->i_mapping);
	
	kdb_printf(" i_flags 0x%x i_state 0x%lx [%s]",
			   inode->i_flags, inode->i_state,
			   map_flags(inode->i_state, inode_flag_vals));

	iaddr  = (char *)addr;
	iaddr += offsetof(struct inode, u);

	kdb_printf("  fs specific info @ 0x%p\n", iaddr);
out:
	if (inode)
		kfree(inode);
	return diag;
}

static int
kdbm_sb(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct super_block *sb = NULL;
	unsigned long addr;
	long	offset=0;
	int nextarg;
	int diag;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)))
		goto out;
	if (!(sb = kmalloc(sizeof(*sb), GFP_ATOMIC))) {
		kdb_printf("kdbm_sb: cannot kmalloc sb\n");
		goto out;
	}
	if ((diag = kdb_getarea(*sb, addr)))
		goto out;

	kdb_printf("struct super_block at  0x%lx\n", addr);
	kdb_printf(" s_dev 0x%x blocksize 0x%lx\n", sb->s_dev, sb->s_blocksize);
	kdb_printf(" s_flags 0x%lx s_root 0x%p\n", sb->s_flags, sb->s_root);
	kdb_printf(" s_dirt %d s_dirty.next 0x%p s_dirty.prev 0x%p\n",
		sb->s_dirt, sb->s_dirty.next, sb->s_dirty.prev);
out:
	if (sb)
		kfree(sb);
	return diag;
}



#ifdef	CONFIG_X86
/* According to Steve Lord, this code is ix86 specific.  Patches to extend it to
 * other architectures will be greatefully accepted.
 */
static int
kdbm_memmap(int argc, const char **argv, const char **envp,
        struct pt_regs *regs)
{
	struct page	page;
	int		i, page_count;
	int		slab_count = 0;
	int		dirty_count = 0;
	int		locked_count = 0;
	int		page_counts[9];
	int		buffered_count = 0;
#ifdef buffer_delay
	int		delay_count = 0;
#endif
	int		diag;
	unsigned long addr;

	addr = (unsigned long)mem_map;
	page_count = max_mapnr;
	memset(page_counts, 0, sizeof(page_counts));

	for (i = 0; i < page_count; i++) {
		if ((diag = kdb_getarea(page, addr)))
			return(diag);
		addr += sizeof(page);

		if (PageSlab(&page))
			slab_count++;
		if (PageDirty(&page))
			dirty_count++;
		if (PageLocked(&page))
			locked_count++;
		if (page.count.counter < 8)
			page_counts[page.count.counter]++;
		else
			page_counts[8]++;
		if (page_has_buffers(&page)) {
			buffered_count++;
#ifdef buffer_delay
			if (buffer_delay(page.buffers))
				delay_count++;
#endif
		}

	}

	kdb_printf("  Total pages:      %6d\n", page_count);
	kdb_printf("  Slab pages:       %6d\n", slab_count);
	kdb_printf("  Dirty pages:      %6d\n", dirty_count);
	kdb_printf("  Locked pages:     %6d\n", locked_count);
	kdb_printf("  Buffer pages:     %6d\n", buffered_count);
#ifdef buffer_delay
	kdb_printf("  Delalloc pages:   %6d\n", delay_count);
#endif
	for (i = 0; i < 8; i++) {
		kdb_printf("  %d page count:     %6d\n",
			i, page_counts[i]);
	}
	kdb_printf("  high page count:  %6d\n", page_counts[8]);
	return 0;
}
#endif	/* CONFIG_X86 */

static int __init kdbm_pg_init(void)
{
#ifndef CONFIG_DISCONTIGMEM
	kdb_register("page", kdbm_page, "<vaddr>", "Display page", 0);
#endif
	kdb_register("inode", kdbm_inode, "<vaddr>", "Display inode", 0);
	kdb_register("sb", kdbm_sb, "<vaddr>", "Display super_block", 0);
	kdb_register("bh", kdbm_buffers, "<buffer head address>", "Display buffer", 0);
#if 0
	kdb_register("inode_pages", kdbm_inode_pages, "<inode *>", "Display pages in an inode", 0);
#endif
	kdb_register("req", kdbm_request, "<vaddr>", "dump request struct", 0);
	kdb_register("rqueue", kdbm_rqueue, "<vaddr>", "dump request queue", 0);
#ifdef	CONFIG_X86
	kdb_register("memmap", kdbm_memmap, "", "page table summary", 0);
#endif

	return 0;
}


static void __exit kdbm_pg_exit(void)
{
#ifndef CONFIG_DISCONTIGMEM
	kdb_unregister("page");
#endif
	kdb_unregister("inode");
	kdb_unregister("sb");
	kdb_unregister("bh");
	kdb_unregister("inode_pages");
	kdb_unregister("req");
	kdb_unregister("rqueue");
#ifdef	CONFIG_X86
	kdb_unregister("memmap");
#endif
}

module_init(kdbm_pg_init)
module_exit(kdbm_pg_exit)
