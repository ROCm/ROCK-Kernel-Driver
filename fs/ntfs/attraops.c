#include "ntfs.h"

/*
 * We need to define the attribute object structure. FIXME: Move these to
 * ntfs.h.
 */
typedef struct {
	ntfs_inode *a_ni;
	ntfs_volume *a_vol;
	atomic_t a_count;
	s64 a_size;
	struct rw_semaphore a_sem;
	struct address_space a_mapping;
	unsigned long a_flags;
} attr_obj;

/**
 * ntfs_attr_readpage - fill a page @page of an attribute object @aobj with data
 * @aobj:	attribute object to which the page @page belongs
 * @page:	page cache page to fill with data
 *
 */
//static int ntfs_attr_readpage(attr_obj *aobj, struct page *page)
static int ntfs_attr_readpage(struct file *aobj, struct page *page)
{
	return -EOPNOTSUPP;
}

/*
 * Address space operations for accessing attributes. Note that these functions
 * do not accept an inode as the first parameter but an attribute object. We
 * use this to implement a generic interface that is not bound to inodes in
 * order to support multiple named streams per file, multiple bitmaps per file
 * and directory, etc. Basically, this gives access to any attribute within an
 * mft record.
 *
 * We make use of a slab cache for attribute object allocations.
 */
struct address_space_operations ntfs_attr_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	ntfs_attr_readpage,	/* Fill page with data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
	//truncatepage:	NULL,			/* . */
};

