/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

#if !defined( __REISER4_FILE_H__ )
#define __REISER4_FILE_H__

/* declarations of functions implementing file plugin for unix file plugin */
int truncate_unix_file(struct inode *, loff_t size);
int readpage_unix_file(void *, struct page *);
int capturepage_unix_file(struct page *);
int capture_unix_file(struct inode *, const struct writeback_control *);
ssize_t read_unix_file(struct file *, char *buf, size_t size, loff_t *off);
ssize_t write_unix_file(struct file *, const char *buf, size_t size, loff_t *off);
int release_unix_file(struct inode *inode, struct file *);
int ioctl_unix_file(struct inode *, struct file *, unsigned int cmd, unsigned long arg);
int mmap_unix_file(struct file *, struct vm_area_struct *vma);
int get_block_unix_file(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_unix_file(struct inode *, char *buf, int user, loff_t, loff_t, rw_op, flow_t *);
int key_by_inode_unix_file(struct inode *, loff_t off, reiser4_key *);
int owns_item_unix_file(const struct inode *, const coord_t *);
int setattr_unix_file(struct inode *, struct iattr *);
void readpages_unix_file(struct file *, struct address_space *, struct list_head *pages);
void init_inode_data_unix_file(struct inode *, reiser4_object_create_data *, int create);
int pre_delete_unix_file(struct inode *);

extern ssize_t sendfile_common (
	struct file *file, loff_t *ppos, size_t count, read_actor_t actor, void __user *target);
extern ssize_t sendfile_unix_file (
	struct file *file, loff_t *ppos, size_t count, read_actor_t actor, void __user *target);
extern int prepare_write_unix_file (struct file *, struct page *, unsigned, unsigned);

int sync_unix_file(struct inode *, int datasync);


/* all the write into unix file is performed by item write method. Write method of unix file plugin only decides which
   item plugin (extent or tail) and in which mode (one from the enum below) to call */
typedef enum {
	FIRST_ITEM = 1,
	APPEND_ITEM = 2,
	OVERWRITE_ITEM = 3
} write_mode_t;


/* unix file may be in one the following states */
typedef enum {
	UF_CONTAINER_UNKNOWN = 0,
	UF_CONTAINER_TAILS = 1,
	UF_CONTAINER_EXTENTS = 2,
	UF_CONTAINER_EMPTY = 3
} file_container_t;

struct formatting_plugin;
struct inode;

/* unix file plugin specific part of reiser4 inode */
typedef struct unix_file_info {
	/* this read-write lock protects file containerization change. Accesses
	   which do not change file containerization (see file_container_t)
	   (read, readpage, writepage, write (until tail conversion is
	   involved)) take read-lock. Accesses which modify file
	   containerization (truncate, conversion from tail to extent and back)
	   take write-lock. */
	struct rw_semaphore latch;
	/* this semaphore is used to serialize writes instead of inode->i_sem,
	   because write_unix_file uses get_user_pages which is to be used
	   under mm->mmap_sem and because it is required to take mm->mmap_sem
	   before inode->i_sem, so inode->i_sem would have to be up()-ed before
	   calling to get_user_pages which is unacceptable */
	struct semaphore write;
	/* this enum specifies which items are used to build the file */
	file_container_t container;
	/* plugin which controls when file is to be converted to extents and
	   back to tail */
	struct formatting_plugin *tplug;
	/* if this is set, file is in exclusive use */
	int exclusive_use;
#if REISER4_DEBUG
	/* pointer to task struct of thread owning exclusive access to file */
	void *ea_owner;
	atomic_t nr_neas;
	void *last_reader;
#ifdef CONFIG_FRAME_POINTER
	void *where[5];
#endif
#endif
} unix_file_info_t;

struct unix_file_info *unix_file_inode_data(const struct inode * inode);

#include "../item/extent.h"
#include "../item/tail.h"
#include "../item/ctail.h"

struct uf_coord {
	coord_t base_coord;
	lock_handle *lh;
	int valid;
	union {
		extent_coord_extension_t extent;
		tail_coord_extension_t tail;
		ctail_coord_extension_t ctail;
	} extension;
};

#include "../../seal.h"

/* structure used to speed up file operations (reads and writes). It contains
 * a seal over last file item accessed. */
struct hint {
	seal_t seal;
	uf_coord_t coord;
	loff_t offset;
	tree_level level;
	znode_lock_mode mode;
};

void set_hint(hint_t *, const reiser4_key *, znode_lock_mode);
void unset_hint(hint_t *);
int hint_validate(hint_t *, const reiser4_key *, int check_key, znode_lock_mode);


#if REISER4_DEBUG

/* return 1 is exclusive access is obtained, 0 - otherwise */
static inline int ea_obtained(unix_file_info_t *uf_info)
{
	int ret;

	ret = down_read_trylock(&uf_info->latch);
	if (ret)
		up_read(&uf_info->latch);
	return !ret;
}

#endif


/* __REISER4_FILE_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
