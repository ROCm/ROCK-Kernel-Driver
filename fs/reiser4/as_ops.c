/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to VFS. Reiser4 address_space_operations are defined here. */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/plugin.h"
#include "plugin/plugin_set.h"
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "log.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"

#include <linux/profile.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/quotaops.h>
#include <linux/security.h>

/* address space operations */

static int reiser4_readpage(struct file *, struct page *);

static int reiser4_prepare_write(struct file *,
				 struct page *, unsigned, unsigned);

static int reiser4_commit_write(struct file *,
				struct page *, unsigned, unsigned);

static int reiser4_set_page_dirty (struct page *);
static sector_t reiser4_bmap(struct address_space *, sector_t);
/* static int reiser4_direct_IO(int, struct inode *,
			     struct kiobuf *, unsigned long, int); */

/* address space operations */

/* clear PAGECACHE_TAG_DIRTY tag of a page. This is used in uncapture_page.  This resembles test_clear_page_dirty. The
   only difference is that page's mapping exists and REISER4_MOVED tag is checked */
reiser4_internal void
reiser4_clear_page_dirty(struct page *page)
{
	struct address_space *mapping;
	unsigned long flags;

	mapping = page->mapping;
	BUG_ON(mapping == NULL);

	read_lock_irqsave(&mapping->tree_lock, flags);
	if (TestClearPageDirty(page)) {
		/* clear dirty tag of page in address space radix tree */
		radix_tree_tag_clear(&mapping->page_tree, page->index,
				     PAGECACHE_TAG_DIRTY);
		/* FIXME: remove this when reiser4_set_page_dirty will skip setting this tag for captured pages */
		radix_tree_tag_clear(&mapping->page_tree, page->index,
				     PAGECACHE_TAG_REISER4_MOVED);

		read_unlock_irqrestore(&mapping->tree_lock, flags);
		if (!mapping->backing_dev_info->memory_backed)
			dec_page_state(nr_dirty);
		return;
	}
	read_unlock_irqrestore(&mapping->tree_lock, flags);
}

/* as_ops->set_page_dirty() VFS method in reiser4_address_space_operations.

   It is used by others (except reiser4) to set reiser4 pages dirty. Reiser4
   itself uses set_page_dirty_internal().

   The difference is that reiser4_set_page_dirty sets MOVED tag on the page and clears DIRTY tag. Pages tagged as MOVED
   get processed by reiser4_writepages() to do reiser4 specific work over dirty pages (allocation jnode, capturing, atom
   creation) which cannot be done in the contexts where reiser4_set_page_dirty is called.
   set_page_dirty_internal sets DIRTY tag and clear MOVED
*/
static int reiser4_set_page_dirty(struct page *page /* page to mark dirty */)
{
	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			read_lock_irq(&mapping->tree_lock);
			/* check for race with truncate */
			if (page->mapping) {
				assert("vs-1652", page->mapping == mapping);
				if (!mapping->backing_dev_info->memory_backed)
					inc_page_state(nr_dirty);
				radix_tree_tag_clear(&mapping->page_tree,
						   page->index, PAGECACHE_TAG_DIRTY);
				/* FIXME: if would be nice to not set this tag on pages which are captured already */
				radix_tree_tag_set(&mapping->page_tree,
						   page->index, PAGECACHE_TAG_REISER4_MOVED);
			}
			read_unlock_irq(&mapping->tree_lock);
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
	}
	return 0;
}

/* ->readpage() VFS method in reiser4 address_space_operations
   method serving file mmapping
*/
static int
reiser4_readpage(struct file *f /* file to read from */ ,
		 struct page *page	/* page where to read data
					 * into */ )
{
	struct inode *inode;
	file_plugin *fplug;
	int result;
	reiser4_context ctx;

	/*
	 * basically calls ->readpage method of object plugin and handles
	 * errors.
	 */

	assert("umka-078", f != NULL);
	assert("umka-079", page != NULL);
	assert("nikita-2280", PageLocked(page));
	assert("vs-976", !PageUptodate(page));

	assert("vs-318", page->mapping && page->mapping->host);
	assert("nikita-1352", (f == NULL) || (f->f_dentry->d_inode == page->mapping->host));

	/* ->readpage can be called from page fault service routine */
	assert("nikita-3174", schedulable());

	inode = page->mapping->host;
	init_context(&ctx, inode->i_sb);
	fplug = inode_file_plugin(inode);
	if (fplug->readpage != NULL)
		result = fplug->readpage(f, page);
	else
		result = RETERR(-EINVAL);
	if (result != 0) {
		SetPageError(page);
		unlock_page(page);
	}

	reiser4_exit_context(&ctx);
	return 0;
}

/* ->readpages() VFS method in reiser4 address_space_operations
   method serving page cache readahead

   reiser4_readpages works in the following way: on input it has coord which is set on extent that addresses first of
   pages for which read requests are to be issued. So, reiser4_readpages just walks forward through extent unit, finds
   which blocks are to be read and start read for them.

reiser4_readpages can be called from two places: from
sys_read->reiser4_read->read_unix_file->read_extent->page_cache_readahead and
from
handling page fault:
handle_mm_fault->do_no_page->filemap_nopage->page_cache_readaround

In first case coord is set by reiser4 read code. This case is detected by  if
(is_in_reiser4_context()).

In second case, coord is not set and currently, reiser4_readpages does
nothing.
*/
static int
reiser4_readpages(struct file *file, struct address_space *mapping,
		  struct list_head *pages, unsigned nr_pages)
{
	file_plugin *fplug;

	if (is_in_reiser4_context()) {
		/* we are called from reiser4 context, typically from method
		   which implements read into page cache. From read_extent,
		   for example */
		fplug = inode_file_plugin(mapping->host);
		if (fplug->readpages)
			fplug->readpages(file, mapping, pages);
	} else {
		/* we are called from page fault. Currently, we do not
		 * readahead in this case. */;
	}

	/* __do_page_cache_readahead expects filesystem's readpages method to
	 * process every page on this list */
	while (!list_empty(pages)) {
		struct page *page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);
		page_cache_release(page);
	}
	return 0;
}

/* prepares @page to be written. This means, that if we want to modify only some
   part of page, page should be read first and than modified. Actually this function
   almost the same as reiser4_readpage(). The differentce is only that, it does not
   unlock the page in the case of error. This is needed because loop back device
   driver expects it locked. */
static int reiser4_prepare_write(struct file *file, struct page *page,
				 unsigned from, unsigned to)
{
	int result;
	file_plugin * fplug;
	struct inode * inode;
	reiser4_context ctx;

	inode = page->mapping->host;
	init_context(&ctx, inode->i_sb);
	fplug = inode_file_plugin(inode);

	if (fplug->prepare_write != NULL)
		result = fplug->prepare_write(file, page, from, to);
	else
		result = RETERR(-EINVAL);

	/* don't commit transaction under inode semaphore */
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);

	return result;
}

/* captures jnode of @page to current atom. */
static int reiser4_commit_write(struct file *file, struct page *page,
				unsigned from, unsigned to)
{
	int result;
	file_plugin *fplug;
	struct inode *inode;
	reiser4_context ctx;

	assert("umka-3101", file != NULL);
	assert("umka-3102", page != NULL);
	assert("umka-3093", PageLocked(page));

	SetPageUptodate(page);

	inode = page->mapping->host;
	init_context(&ctx, inode->i_sb);
	fplug = inode_file_plugin(inode);

	if (fplug->capturepage)
		result = fplug->capturepage(page);
	else
		result = RETERR(-EINVAL);

	/* here page is return locked. */
	assert("umka-3103", PageLocked(page));

	/* don't commit transaction under inode semaphore */
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->writepages()
   ->vm_writeback()
   ->set_page_dirty()
   ->prepare_write()
   ->commit_write()
*/

/* ->bmap() VFS method in reiser4 address_space_operations */
reiser4_internal int
reiser4_lblock_to_blocknr(struct address_space *mapping,
			  sector_t lblock, reiser4_block_nr *blocknr)
{
	file_plugin *fplug;
	int result;
	reiser4_context ctx;

	init_context(&ctx, mapping->host->i_sb);
	reiser4_stat_inc(vfs_calls.bmap);

	fplug = inode_file_plugin(mapping->host);
	if (fplug && fplug->get_block) {
		*blocknr = generic_block_bmap(mapping, lblock, fplug->get_block);
		result = 0;
	} else
		result = RETERR(-EINVAL);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->bmap() VFS method in reiser4 address_space_operations */
static sector_t
reiser4_bmap(struct address_space *mapping, sector_t lblock)
{
	reiser4_block_nr blocknr;
	int result;

	result = reiser4_lblock_to_blocknr(mapping, lblock, &blocknr);
	if (result == 0)
		if (sizeof blocknr == sizeof(sector_t) ||
		    !blocknr_is_fake(&blocknr))
			return blocknr;
		else
			return 0;
	else
		return result;
}

/* ->invalidatepage method for reiser4 */

/*
 * this is called for each truncated page from
 * truncate_inode_pages()->truncate_{complete,partial}_page().
 *
 * At the moment of call, page is under lock, and outstanding io (if any) has
 * completed.
 */

reiser4_internal int
reiser4_invalidatepage(struct page *page /* page to invalidate */,
		       unsigned long offset /* starting offset for partial
					     * invalidation */)
{
	int ret = 0;
	reiser4_context ctx;
	struct inode *inode;

	/*
	 * This is called to truncate file's page.
	 *
	 * Originally, reiser4 implemented truncate in a standard way
	 * (vmtruncate() calls ->invalidatepage() on all truncated pages
	 * first, then file system ->truncate() call-back is invoked).
	 *
	 * This lead to the problem when ->invalidatepage() was called on a
	 * page with jnode that was captured into atom in ASTAGE_PRE_COMMIT
	 * process. That is, truncate was bypassing transactions. To avoid
	 * this, try_capture_page_to_invalidate() call was added here.
	 *
	 * After many troubles with vmtruncate() based truncate (including
	 * races with flush, tail conversion, etc.) it was re-written in the
	 * top-to-bottom style: items are killed in cut_tree_object() and
	 * pages belonging to extent are invalidated in kill_hook_extent(). So
	 * probably now additional call to capture is not needed here.
	 *
	 */

	assert("nikita-3137", PageLocked(page));
	assert("nikita-3138", !PageWriteback(page));
	inode = page->mapping->host;

	/*
	 * ->invalidatepage() should only be called for the unformatted
	 * jnodes. Destruction of all other types of jnodes is performed
	 * separately. But, during some corner cases (like handling errors
	 * during mount) it is simpler to let ->invalidatepage to be called on
	 * them. Check for this, and do nothing.
	 */
	if (get_super_fake(inode->i_sb) == inode)
		return 0;
	if (get_cc_fake(inode->i_sb) == inode)
		return 0;
	if (get_super_private(inode->i_sb)->bitmap == inode)
		return 0;

	assert("vs-1426", PagePrivate(page));
	assert("vs-1427", page->mapping == jnode_get_mapping(jnode_by_page(page)));

	init_context(&ctx, inode->i_sb);
	/* capture page being truncated. */
	ret = try_capture_page_to_invalidate(page);
	if (ret != 0) {
		warning("nikita-3141", "Cannot capture: %i", ret);
		print_page("page", page);
	}

	if (offset == 0) {
		jnode *node;

		/* remove jnode from transaction and detach it from page. */
		node = jnode_by_page(page);
		if (node != NULL) {
			assert("vs-1435", !JF_ISSET(node, JNODE_CC));
			jref(node);
 			JF_SET(node, JNODE_HEARD_BANSHEE);
			/* page cannot be detached from jnode concurrently,
			 * because it is locked */
			uncapture_page(page);

			/* this detaches page from jnode, so that jdelete will not try to lock page which is already locked */
			UNDER_SPIN_VOID(jnode,
					node,
					page_clear_jnode(page, node));
			unhash_unformatted_jnode(node);

			jput(node);
		}
	}
	reiser4_exit_context(&ctx);
	return ret;
}

#define INC_STAT(page, node, counter)						\
	reiser4_stat_inc_at(page->mapping->host->i_sb, 				\
			    level[jnode_get_level(node)].counter);

#define INC_NSTAT(node, counter) INC_STAT(jnode_page(node), node, counter)

int is_cced(const jnode *node);

/* help function called from reiser4_releasepage(). It returns true if jnode
 * can be detached from its page and page released. */
static int
releasable(const jnode *node /* node to check */)
{
	assert("nikita-2781", node != NULL);
	assert("nikita-2783", spin_jnode_is_locked(node));

	/* is some thread is currently using jnode page, later cannot be
	 * detached */
	if (atomic_read(&node->d_count) != 0) {
		INC_NSTAT(node, vm.release.loaded);
		return 0;
	}

	assert("vs-1214", !jnode_is_loaded(node));

	/* this jnode is just a copy. Its page cannot be released, because
	 * otherwise next jload() would load obsolete data from disk
	 * (up-to-date version may still be in memory). */
	if (is_cced(node)) {
		INC_NSTAT(node, vm.release.copy);
		return 0;
	}

	/* emergency flushed page can be released. This is what emergency
	 * flush is all about after all. */
	if (JF_ISSET(node, JNODE_EFLUSH)) {
		INC_NSTAT(node, vm.release.eflushed);
		return 1; /* yeah! */
	}

	/* can only release page if real block number is assigned to
	   it. Simple check for ->atom wouldn't do, because it is possible for
	   node to be clean, not it atom yet, and still having fake block
	   number. For example, node just created in jinit_new(). */
	if (blocknr_is_fake(jnode_get_block(node))) {
		INC_NSTAT(node, vm.release.fake);
		return 0;
	}
	/* dirty jnode cannot be released. It can however be submitted to disk
	 * as part of early flushing, but only after getting flush-prepped. */
	if (jnode_is_dirty(node)) {
		INC_NSTAT(node, vm.release.dirty);
		return 0;
	}
	/* overwrite set is only written by log writer. */
	if (JF_ISSET(node, JNODE_OVRWR)) {
		INC_NSTAT(node, vm.release.ovrwr);
		return 0;
	}
	/* jnode is already under writeback */
	if (JF_ISSET(node, JNODE_WRITEBACK)) {
		INC_NSTAT(node, vm.release.writeback);
		return 0;
	}
	/* page was modified through mmap, but its jnode is not yet
	 * captured. Don't discard modified data. */
	if (jnode_is_unformatted(node) && JF_ISSET(node, JNODE_KEEPME)) {
		INC_NSTAT(node, vm.release.keepme);
		return 0;
	}
	/* don't flush bitmaps or journal records */
	if (!jnode_is_znode(node) && !jnode_is_unformatted(node)) {
		INC_NSTAT(node, vm.release.bitmap);
		return 0;
	}
	return 1;
}

#if REISER4_DEBUG
int jnode_is_releasable(jnode *node)
{
	return UNDER_SPIN(jload, node, releasable(node));
}
#endif

/*
 * ->releasepage method for reiser4
 *
 * This is called by VM scanner when it comes across clean page.  What we have
 * to do here is to check whether page can really be released (freed that is)
 * and if so, detach jnode from it and remove page from the page cache.
 *
 * Check for releasability is done by releasable() function.
 */
reiser4_internal int
reiser4_releasepage(struct page *page, int gfp UNUSED_ARG)
{
	jnode *node;
	void *oid;

	assert("nikita-2257", PagePrivate(page));
	assert("nikita-2259", PageLocked(page));
	assert("nikita-2892", !PageWriteback(page));
	assert("nikita-3019", schedulable());

	/* NOTE-NIKITA: this can be called in the context of reiser4 call. It
	   is not clear what to do in this case. A lot of deadlocks seems be
	   possible. */

	node = jnode_by_page(page);
	assert("nikita-2258", node != NULL);
	assert("reiser4-4", page->mapping != NULL);
	assert("reiser4-5", page->mapping->host != NULL);

	INC_STAT(page, node, vm.release.try);

	oid = (void *)(unsigned long)get_inode_oid(page->mapping->host);

	/* is_page_cache_freeable() check
	   (mapping + private + page_cache_get() by shrink_cache()) */
	if (page_count(page) > 3)
		return 0;

	if (PageDirty(page))
		return 0;

	/* releasable() needs jnode lock, because it looks at the jnode fields
	 * and we need jload_lock here to avoid races with jload(). */
	LOCK_JNODE(node);
	LOCK_JLOAD(node);
	if (releasable(node)) {
		struct address_space *mapping;

		mapping = page->mapping;
		INC_STAT(page, node, vm.release.ok);
		jref(node);
		if (jnode_is_znode(node))
			ON_STATS(znode_at_read(JZNODE(node)));
		/* there is no need to synchronize against
		 * jnode_extent_write() here, because pages seen by
		 * jnode_extent_write() are !releasable(). */
		page_clear_jnode(page, node);
		UNLOCK_JLOAD(node);
		UNLOCK_JNODE(node);

		/* we are under memory pressure so release jnode also. */
		jput(node);

		write_lock_irq(&mapping->tree_lock);
		/* shrink_list() + radix-tree */
		if (page_count(page) == 2) {
			__remove_from_page_cache(page);
			__put_page(page);
		}
		write_unlock_irq(&mapping->tree_lock);

		return 1;
	} else {
		UNLOCK_JLOAD(node);
		UNLOCK_JNODE(node);
		assert("nikita-3020", schedulable());
		return 0;
	}
}

#undef INC_NSTAT
#undef INC_STAT

reiser4_internal void
move_inode_out_from_sync_inodes_loop(struct address_space * mapping)
{
	/* work around infinite loop in pdflush->sync_sb_inodes. */
	/* Problem: ->writepages() is supposed to submit io for the pages from
	 * ->io_pages list and to clean this list. */
	mapping->host->dirtied_when = jiffies;
	spin_lock(&inode_lock);
	list_move(&mapping->host->i_list, &mapping->host->i_sb->s_dirty);
	spin_unlock(&inode_lock);

}

/* reiser4 writepages() address space operation this captures anonymous pages
   and anonymous jnodes. Anonymous pages are pages which are dirtied via
   mmapping. Anonymous jnodes are ones which were created by reiser4_writepage
 */
reiser4_internal int
reiser4_writepages(struct address_space *mapping,
		   struct writeback_control *wbc)
{
	int ret = 0;
	struct inode *inode;
	file_plugin *fplug;

	inode = mapping->host;
	fplug = inode_file_plugin(inode);
	if (fplug != NULL && fplug->capture != NULL) {
		long captured = 0;

		/* call file plugin method to capture anonymous pages and
		 * anonymous jnodes */
		ret = fplug->capture(inode, wbc, &captured);
	}

	move_inode_out_from_sync_inodes_loop(mapping);
	return ret;
}

/* start actual IO on @page */
reiser4_internal int reiser4_start_up_io(struct page *page)
{
	block_sync_page(page);
	return 0;
}

/*
 * reiser4 methods for VM
 */
struct address_space_operations reiser4_as_operations = {
	/* called during memory pressure by kswapd */
	.writepage = reiser4_writepage,
	/* called to read page from the storage when page is added into page
	   cache. This is done by page-fault handler. */
	.readpage = reiser4_readpage,
	/* Start IO on page. This is called from wait_on_page_bit() and
	   lock_page() and its purpose is to actually start io by jabbing
	   device drivers. */
	.sync_page = reiser4_start_up_io,
	/* called from
	 * reiser4_sync_inodes()->generic_sync_sb_inodes()->...->do_writepages()
	 *
	 * captures anonymous pages for given inode
	 */
	.writepages = reiser4_writepages,
	/* marks page dirty. Note that this is never called by reiser4
	 * directly. Reiser4 uses set_page_dirty_internal(). Reiser4 set page
	 * dirty is called for pages dirtied though mmap and moves dirty page
	 * to the special ->moved_list in its mapping. */
	.set_page_dirty = reiser4_set_page_dirty,
	/* called during read-ahead */
	.readpages = reiser4_readpages,
	.prepare_write = reiser4_prepare_write, /* loop back device driver and generic_file_write() call-back */
	.commit_write = reiser4_commit_write,  /* loop back device driver and generic_file_write() call-back */
	/* map logical block number to disk block number. Used by FIBMAP ioctl
	 * and ..bmap pseudo file. */
	.bmap = reiser4_bmap,
	/* called just before page is taken out from address space (on
	   truncate, umount, or similar).  */
	.invalidatepage = reiser4_invalidatepage,
	/* called when VM is about to take page from address space (due to
	   memory pressure). */
	.releasepage = reiser4_releasepage,
	/* not yet implemented */
	.direct_IO = NULL
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
