/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug page information");
MODULE_LICENSE("GPL");

/* Standard Linux page stuff */

static char	*pg_flag_vals[] = {
	"PG_locked", "PG_error", "PG_referenced", "PG_uptodate",
	"PG_dirty_dontuse", "PG_lru", "PG_active", "PG_slab",
	"PG_highmem", "PG_checked", "PG_arch_1", "PG_reserved",
	"PG_private", "PG_writeback",
	NULL };

static char	*bh_state_vals[] = {
	"Uptodate", "Dirty", "Lock", "Req",
	"Mapped", "New", "AsyncRead", "AsyncWrite",
	"JBD", "Delay", "Private",
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

static char	*page_flags(unsigned long flags)
{
	return(map_flags(flags, pg_flag_vals));
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
	kdb_printf("  bno %ld size %d dev 0x%x\n",
		bh.b_blocknr, bh.b_size, bh.b_bdev->bd_dev);
	kdb_printf("  count %d state 0x%lx [%s]\n",
		bh.b_count.counter, bh.b_state,
		map_flags(bh.b_state, bh_state_vals));
	kdb_printf("  b_data 0x%p b_page 0x%p b_this_page 0x%p b_private 0x%p\n",
		bh.b_data, bh.b_page, bh.b_this_page, bh.b_private);

	return 0;
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

unsigned long
print_request(unsigned long addr)
{
	struct request	rq;

	if (kdb_getarea(rq, addr))
		return(0);

	kdb_printf("struct request at 0x%lx\n", addr);
	kdb_printf("  rq_dev 0x%x errors %d sector %ld nr_sectors %ld\n",
			kdev_val(rq.rq_dev), rq.errors, rq.sector,
			rq.nr_sectors);

	kdb_printf("  hsect %ld hnrsect %ld nrhwseg %d currnrsect %d\n",
			rq.hard_sector, rq.hard_nr_sectors,
			rq.nr_hw_segments, rq.current_nr_sectors);
	kdb_printf("  ");

	return 1;
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

static void
do_buffer(unsigned long addr)
{
	struct buffer_head	bh;
	
	if (kdb_getarea(bh, addr))
		return;

	kdb_printf("bh 0x%lx bno %8ld [%s]\n", addr, bh.b_blocknr,
		 map_flags(bh.b_state, bh_state_vals));
}

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
	  head = &ap->clean_pages;
	  kdb_printf("CLEAN  page_struct   index  cnt  flags\n");
	} else if (which == 1) {
	  which=2;
	  head = &ap->dirty_pages;
	  kdb_printf("DIRTY  page_struct   index  cnt  flags\n");
	} else if (which == 2) {
	  which=3;
	  head = &ap->locked_pages;
	  kdb_printf("LOCKED page_struct   index  cnt  flags\n");
	} else {
	  goto out;
	}
	
	if(!head) goto again;
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

	kdb_printf(" i_ino = %lu i_count = %u i_dev = 0x%x i_size %Ld\n",
					inode->i_ino, atomic_read(&inode->i_count),
					inode->i_dev, inode->i_size);

	kdb_printf(" i_mode = 0%o  i_nlink = %d  i_rdev = 0x%x\n",
					inode->i_mode, inode->i_nlink,
					kdev_val(inode->i_rdev));

	kdb_printf(" i_hash.nxt = 0x%p i_hash.prv = 0x%p\n",
					inode->i_hash.next, inode->i_hash.prev);

	kdb_printf(" i_list.nxt = 0x%p i_list.prv = 0x%p\n",
					inode->i_list.next, inode->i_list.prev);

	kdb_printf(" i_dentry.nxt = 0x%p i_dentry.prv = 0x%p\n",
					inode->i_dentry.next,
					inode->i_dentry.prev);

	kdb_printf(" i_sb = 0x%p i_op = 0x%p i_data = 0x%lx nrpages = %lu\n",
					inode->i_sb, inode->i_op,
					addr + offsetof(struct inode, i_data),
					inode->i_data.nrpages);
	kdb_printf(" i_mapping = 0x%p\n i_flags 0x%x i_state 0x%lx [%s]",
			   inode->i_mapping, inode->i_flags,
			   inode->i_state,
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
		if (page_has_buffers(&page))
			buffered_count++;

	}

	kdb_printf("  Total pages:      %6d\n", page_count);
	kdb_printf("  Slab pages:       %6d\n", slab_count);
	kdb_printf("  Dirty pages:      %6d\n", dirty_count);
	kdb_printf("  Locked pages:     %6d\n", locked_count);
	kdb_printf("  Buffer pages:     %6d\n", buffered_count);
	for (i = 0; i < 8; i++) {
		kdb_printf("  %d page count:     %6d\n",
			i, page_counts[i]);
	}
	kdb_printf("  high page count:  %6d\n", page_counts[8]);
	return 0;
}

static int
kdbm_bio(int argc, const char **argv, const char **envp,
	struct pt_regs *regs)
{
	struct bio	bio;
	struct bio_vec	vec;
	unsigned long addr;
	long    offset=0;
	int nextarg;
	int diag;
	int i;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	if ((diag = kdb_getarea(bio, addr)))
		return(diag);

	kdb_printf("struct bio at 0x%lx\n", addr);
	kdb_printf("  sector 0x%lx dev 0x%x size 0x%x flags 0x%lx rw 0x%lx\n",
		bio.bi_sector, bio.bi_bdev->bd_dev, bio.bi_size, bio.bi_flags,
		bio.bi_rw);
	kdb_printf("  vcnt %d vec 0x%p idx %d max %d private 0x%p\n",
		   bio.bi_vcnt, bio.bi_io_vec, bio.bi_idx, bio.bi_max_vecs,
		   bio.bi_private);
	addr = (unsigned long) bio.bi_io_vec;
	for (i = 0; i < bio.bi_vcnt; i++) {
		diag = kdb_getarea(vec, addr);
		if (diag)
			return diag;
		addr += sizeof(struct bio_vec);
		kdb_printf("    page 0x%p offset 0x%x len 0x%x\n",
			vec.bv_page, vec.bv_offset, vec.bv_len);
	}

	return 0;
}

static int __init kdbm_pg_init(void)
{
	kdb_register("page", kdbm_page, "<vaddr>", "Display page", 0);
	kdb_register("inode", kdbm_inode, "<vaddr>", "Display inode", 0);
	kdb_register("bh", kdbm_buffers, "<buffer head address>", "Display buffer", 0);
	kdb_register("bio", kdbm_bio, "<bio address>", "Display bio struct", 0);
	kdb_register("inode_pages", kdbm_inode_pages, "<inode *>", "Display pages in an inode", 0);
	kdb_register("req", kdbm_request, "<vaddr>", "dump request struct", 0);
	kdb_register("memmap", kdbm_memmap, "", "page table summary", 0);

	return 0;
}


static void __exit kdbm_pg_exit(void)
{
	kdb_unregister("page");
	kdb_unregister("inode");
	kdb_unregister("bh");
	kdb_unregister("bio");
	kdb_unregister("inode_pages");
	kdb_unregister("memmap");
}

module_init(kdbm_pg_init)
module_exit(kdbm_pg_exit)
