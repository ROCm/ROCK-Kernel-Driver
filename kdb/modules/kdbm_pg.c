/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

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
/* From include/linux/page-flags.h */
static char *pg_flag_vals[] = {
	"PG_locked", "PG_error", "PG_referenced", "PG_uptodate",
	"PG_dirty", "PG_lru", "PG_active", "PG_slab",
	"PG_owner_priv_1", "PG_arch_1", "PG_reserved", "PG_private",
	"PG_writeback", "?? 13 ??", "PG_compound", "PG_swapcache",
	"PG_mappedtodisk", "PG_reclaim", "?? 18 ??", "PG_buddy",
	NULL };
#endif

/* From include/linux/buffer_head.h */
static char *bh_state_vals[] = {
	"Uptodate", "Dirty", "Lock", "Req",
	"Uptodate_Lock", "Mapped", "New", "Async_read",
	"Async_write", "Delay", "Boundary", "Write_EIO",
	"Ordered", "Eopnotsupp", "Unwritten", "Private",
	NULL };

/* From include/linux/bio.h */
static char *bio_flag_vals[] = {
	"Uptodate", "RW_block", "EOF", "Seg_valid",
	"Cloned", "Bounced", "User_mapped", "Eopnotsupp",
	NULL };

/* From include/linux/fs.h */
static char *inode_flag_vals[] = {
	"I_DIRTY_SYNC", "I_DIRTY_DATASYNC", "I_DIRTY_PAGES", "I_NEW",
	"I_WILL_FREE", "I_FREEING", "I_CLEAR", "I_LOCK",
	"I_SYNC", NULL };

static char *map_flags(unsigned long flags, char *mapping[])
{
	static char buffer[256];
	int index;
	int offset = 12;

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
kdbm_buffers(int argc, const char **argv)
{
	struct buffer_head bh;
	unsigned long addr;
	long offset = 0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(bh, addr)))
		return(diag);

	kdb_printf("buffer_head at 0x%lx\n", addr);
	kdb_printf("  bno %llu size %llu dev 0x%x\n",
		(unsigned long long)bh.b_blocknr,
		(unsigned long long)bh.b_size,
		bh.b_bdev ? bh.b_bdev->bd_dev : 0);
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

static int
print_biovec(struct bio_vec *vec, int vcount)
{
	struct bio_vec bvec;
	unsigned long addr;
	int diag;
	int i;

	if (vcount < 1 || vcount > BIO_MAX_PAGES) {
		kdb_printf("  [skipped iovecs, vcnt is %d]\n", vcount);
		return 0;
	}

	addr = (unsigned long)vec;
	for (i = 0; i < vcount; i++) {
		if ((diag = kdb_getarea(bvec, addr)))
			return(diag);
		addr += sizeof(bvec);
		kdb_printf("  [%d] page 0x%p length=%u offset=%u\n",
			i, bvec.bv_page, bvec.bv_len, bvec.bv_offset);
	}
	return 0;
}

static int
kdbm_bio(int argc, const char **argv)
{
	struct bio bio;
	unsigned long addr;
	long offset = 0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(bio, addr)))
		return(diag);

	kdb_printf("bio at 0x%lx\n", addr);
	kdb_printf("  bno %llu  next 0x%p  dev 0x%x\n",
		(unsigned long long)bio.bi_sector,
		bio.bi_next, bio.bi_bdev ? bio.bi_bdev->bd_dev : 0);
	kdb_printf("  vcnt %u vec 0x%p  rw 0x%lx flags 0x%lx [%s]\n",
		bio.bi_vcnt, bio.bi_io_vec, bio.bi_rw, bio.bi_flags,
		map_flags(bio.bi_flags, bio_flag_vals));
	print_biovec(bio.bi_io_vec, bio.bi_vcnt);
	kdb_printf("  count %d  private 0x%p\n",
		atomic_read(&bio.bi_cnt), bio.bi_private);
	kdb_printf("  bi_end_io ");
	if (bio.bi_end_io)
		kdb_symbol_print(kdba_funcptr_value(bio.bi_end_io), NULL, KDB_SP_VALUE);
	else
		kdb_printf("(NULL)");
	kdb_printf("\n");

	return 0;
}

#ifndef CONFIG_DISCONTIGMEM
static char *page_flags(unsigned long flags)
{
	return(map_flags(flags, pg_flag_vals));
}

static int
kdbm_page(int argc, const char **argv)
{
	struct page page;
	unsigned long addr;
	long offset = 0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

#ifdef	__ia64__
	if (rgn_index(addr) == 0)
		addr = (unsigned long) &mem_map[addr];	/* assume region 0 is a page index, not an address */
#else
	if (addr < PAGE_OFFSET)
		addr = (unsigned long) &mem_map[addr];
#endif

	if ((diag = kdb_getarea(page, addr)))
		return(diag);

	kdb_printf("struct page at 0x%lx\n", addr);
	kdb_printf("  addr space 0x%p index %lu (offset 0x%llx)\n",
		   page.mapping, page.index,
		   (unsigned long long)page.index << PAGE_CACHE_SHIFT);
	kdb_printf("  count %d flags %s\n",
		   page._count.counter, page_flags(page.flags));
	kdb_printf("  virtual 0x%p\n", page_address((struct page *)addr));
	if (page_has_buffers(&page))
		kdb_printf("  buffers 0x%p\n", page_buffers(&page));
	else
		kdb_printf("  private 0x%lx\n", page_private(&page));

	return 0;
}
#endif /* CONFIG_DISCONTIGMEM */

static unsigned long
print_request(unsigned long addr)
{
	struct request rq;

	if (kdb_getarea(rq, addr))
		return(0);

	kdb_printf("struct request at 0x%lx\n", addr);
	kdb_printf("  errors %d sector %llu nr_sectors %lu\n",
			rq.errors,
			(unsigned long long)rq.sector, rq.nr_sectors);

	kdb_printf("  hsect %llu hnrsect %lu nrseg %u nrhwseg %u currnrsect %u\n",
			(unsigned long long)rq.hard_sector, rq.hard_nr_sectors,
			rq.nr_phys_segments, rq.nr_hw_segments,
			rq.current_nr_sectors);

	return (unsigned long) rq.queuelist.next;
}

static int
kdbm_request(int argc, const char **argv)
{
	long offset = 0;
	unsigned long addr;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	print_request(addr);
	return 0;
}


static int
kdbm_rqueue(int argc, const char **argv)
{
	struct request_queue rq;
	unsigned long addr, head_addr, next;
	long offset = 0;
	int nextarg;
	int i, diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
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
	struct buffer_head bh;

	if (kdb_getarea(bh, addr))
		return;

	kdb_printf("\tbh 0x%lx bno %8llu [%s]\n", addr,
		 (unsigned long long)bh.b_blocknr,
		 map_flags(bh.b_state, bh_state_vals));
}

static void
kdbm_show_page(struct page *page, int first)
{
	if (first)
		kdb_printf("page_struct       index   cnt zone nid flags\n");
	kdb_printf("%p%s %6lu %5d %3d %3d 0x%lx",
		page_address(page), sizeof(void *) == 4 ? "        " : "",
		page->index, atomic_read(&(page->_count)),
		page_zonenum(page), page_to_nid(page),
		page->flags & (~0UL >> ZONES_SHIFT));
#define kdb_page_flags(page, type) if (Page ## type(page)) kdb_printf(" " #type);
	kdb_page_flags(page, Locked);
	kdb_page_flags(page, Error);
	kdb_page_flags(page, Referenced);
	kdb_page_flags(page, Uptodate);
	kdb_page_flags(page, Dirty);
	kdb_page_flags(page, LRU);
	kdb_page_flags(page, Active);
	kdb_page_flags(page, Slab);
	kdb_page_flags(page, Checked);
	if (page->flags & (1UL << PG_arch_1))
		kdb_printf(" arch_1");
	kdb_page_flags(page, Reserved);
	kdb_page_flags(page, Private);
	kdb_page_flags(page, Writeback);
	kdb_page_flags(page, Compound);
	kdb_page_flags(page, SwapCache);
	kdb_page_flags(page, MappedToDisk);
	kdb_page_flags(page, Reclaim);
	kdb_page_flags(page, Buddy);

	/* PageHighMem is not a flag any more, but treat it as one */
	kdb_page_flags(page, HighMem);

	if (page_has_buffers(page)) {
		struct buffer_head *head, *bh;
		kdb_printf("\n");
		head = bh = page_buffers(page);
		do {
			do_buffer((unsigned long) bh);
		} while ((bh = bh->b_this_page) != head);
	} else if (page_private(page)) {
		kdb_printf(" private= 0x%lx", page_private(page));
	}
	/* Cannot use page_mapping(page) here, it needs swapper_space which is
	 * not exported.
	 */
	if (page->mapping)
		kdb_printf(" mapping= %p", page->mapping);
	kdb_printf("\n");
#undef kdb_page_flags
}

static int
kdbm_inode_pages(int argc, const char **argv)
{
	struct inode *inode = NULL;
	struct address_space *ap = NULL;
	unsigned long addr, addr1 = 0;
	long offset = 0;
	int nextarg;
	int diag;
	pgoff_t next = 0;
	struct page *page;
	int first;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		goto out;

	if (argc == 2) {
		nextarg = 2;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr1,
					&offset, NULL);
		if (diag)
			goto out;
		kdb_printf("Looking for page index 0x%lx ... \n", addr1);
		next = addr1;
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

	/* Run the pages in the radix tree, printing the state of each page */
	first = 1;
	while (radix_tree_gang_lookup(&ap->page_tree, (void **)&page, next, 1)) {
		kdbm_show_page(page, first);
		if (addr1)
			break;
		first = 0;
		next = page->index + 1;
	}

out:
	if (inode)
		kfree(inode);
	if (ap)
		kfree(ap);
	return diag;
}

static int
kdbm_inode(int argc, const char **argv)
{
	struct inode *inode = NULL;
	unsigned long addr;
	unsigned char *iaddr;
	long offset = 0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
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
	iaddr += offsetof(struct inode, i_private);

	kdb_printf("  fs specific info @ 0x%p\n", iaddr);
out:
	if (inode)
		kfree(inode);
	return diag;
}

static int
kdbm_sb(int argc, const char **argv)
{
	struct super_block *sb = NULL;
	unsigned long addr;
	long offset = 0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
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
	kdb_printf(" s_frozen %d s_id [%s]\n", sb->s_frozen, sb->s_id);
out:
	if (sb)
		kfree(sb);
	return diag;
}



#if defined(CONFIG_X86) && !defined(CONFIG_X86_64)
/* According to Steve Lord, this code is ix86 specific.  Patches to extend it to
 * other architectures will be greatefully accepted.
 */
static int
kdbm_memmap(int argc, const char **argv)
{
	struct page page;
	int i, page_count;
	int slab_count = 0;
	int dirty_count = 0;
	int locked_count = 0;
	int page_counts[9];
	int buffered_count = 0;
#ifdef buffer_delay
	int delay_count = 0;
#endif
	int diag;
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
		if (page._count.counter < 8)
			page_counts[page._count.counter]++;
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
#endif /* CONFIG_X86 && !CONFIG_X86_64 */

static int __init kdbm_pg_init(void)
{
#ifndef CONFIG_DISCONTIGMEM
	kdb_register("page", kdbm_page, "<vaddr>", "Display page", 0);
#endif
	kdb_register("inode", kdbm_inode, "<vaddr>", "Display inode", 0);
	kdb_register("sb", kdbm_sb, "<vaddr>", "Display super_block", 0);
	kdb_register("bh", kdbm_buffers, "<buffer head address>", "Display buffer", 0);
	kdb_register("bio", kdbm_bio, "<bio address>", "Display bio", 0);
	kdb_register("inode_pages", kdbm_inode_pages, "<inode *>", "Display pages in an inode", 0);
	kdb_register("req", kdbm_request, "<vaddr>", "dump request struct", 0);
	kdb_register("rqueue", kdbm_rqueue, "<vaddr>", "dump request queue", 0);
#if defined(CONFIG_X86) && !defined(CONFIG_X86_64)
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
	kdb_unregister("bio");
	kdb_unregister("inode_pages");
	kdb_unregister("req");
	kdb_unregister("rqueue");
#if defined(CONFIG_X86) && !defined(CONFIG_X86_64)
	kdb_unregister("memmap");
#endif
}

module_init(kdbm_pg_init)
module_exit(kdbm_pg_exit)
