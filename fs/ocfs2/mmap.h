#ifndef OCFS2_MMAP_H
#define OCFS2_MMAP_H

int ocfs2_mmap(struct file *file,
	       struct vm_area_struct *vma);

/* used by file_read/file_write and nopage to coordinate file
 * locking. I keep this out of the dlmglue code, because quite frankly
 * I don't like that we have to do this stuff. */
struct ocfs2_io_marker {
	struct list_head io_list;
	struct task_struct *io_task;
};

#define __IOMARKER_INITIALIZER(name) {					\
	.io_list      = { &(name).io_list, &(name).io_list },		\
	.io_task      = NULL }

#define DECLARE_IO_MARKER(name)						\
	struct ocfs2_io_marker name = __IOMARKER_INITIALIZER(name)

static inline void ocfs2_init_io_marker(struct ocfs2_io_marker *task)
{
	INIT_LIST_HEAD(&task->io_list);
	task->io_task = NULL;
}

static inline void ocfs2_add_io_marker(struct inode *inode,
				       struct ocfs2_io_marker *task)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	task->io_task = current;
	spin_lock(&oi->ip_lock);
	list_add(&task->io_list, &oi->ip_io_markers);
	spin_unlock(&oi->ip_lock);
}

static inline void ocfs2_del_io_marker(struct inode *inode,
				       struct ocfs2_io_marker *task)
{
	spin_lock(&OCFS2_I(inode)->ip_lock);
	if (!list_empty(&task->io_list))
		list_del_init(&task->io_list);
	spin_unlock(&OCFS2_I(inode)->ip_lock);
}

static inline int ocfs2_is_in_io_marker_list(struct inode *inode,
					   struct task_struct *task)
{
	int ret = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct list_head *p;
	struct ocfs2_io_marker *tmp;

	spin_lock(&oi->ip_lock);
	list_for_each(p, &oi->ip_io_markers) {
		tmp = list_entry(p, struct ocfs2_io_marker, io_list);
		if (tmp->io_task == task) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&oi->ip_lock);

	return ret;
}

struct ocfs2_backing_inode {
	struct rb_node           ba_node;
	struct inode            *ba_inode;
	unsigned		 ba_meta_locked:1, 	/* meta is locked */
				 ba_locked:1,		/* both are locked */
				 ba_lock_data:1,	/* should lock data */
				 ba_lock_meta_level:1,
				 ba_lock_data_level:1;
	struct ocfs2_io_marker   ba_task;
};

/* Used to manage the locks taken during I/O. */
struct ocfs2_buffer_lock_ctxt {
	struct rb_root			b_inodes;
	struct ocfs2_backing_inode	*b_next_unlocked;
	ocfs2_lock_callback		b_cb;
	unsigned long			b_cb_data;
};

#define __BUFFERLOCK_INITIALIZER {					\
	.b_inodes               = RB_ROOT,				\
	.b_next_unlocked	= NULL,					\
	.b_cb			= NULL,					\
	.b_cb_data		= 0 }

#define DECLARE_BUFFER_LOCK_CTXT(name)					\
	struct ocfs2_buffer_lock_ctxt name = __BUFFERLOCK_INITIALIZER

#define INIT_BUFFER_LOCK_CTXT(ctxt)	\
	*(ctxt) = (struct ocfs2_buffer_lock_ctxt) __BUFFERLOCK_INITIALIZER

int ocfs2_setup_io_locks(struct super_block *sb,
			 struct inode *target_inode,
			 char __user *buf,
			 size_t size,
			 struct ocfs2_buffer_lock_ctxt *ctxt,
			 struct ocfs2_backing_inode **target_binode);

int ocfs2_lock_buffer_inodes(struct ocfs2_buffer_lock_ctxt *ctxt,
			     struct inode *last_inode);

void ocfs2_unlock_buffer_inodes(struct ocfs2_buffer_lock_ctxt *ctxt);

struct ocfs2_write_lock_info {
	u64				wl_newsize;
	unsigned			wl_extended:1,
					wl_do_direct_io:1,
					wl_have_i_sem:1,
					wl_unlock_ctxt:1,
					wl_have_before:1,
					wl_have_target_meta:1,
					wl_have_data_lock:1;
	struct ocfs2_backing_inode	*wl_target_binode;
};

ssize_t ocfs2_write_lock_maybe_extend(struct file *filp,
				      const char __user *buf,
				     size_t count,
				      loff_t *ppos,
				     struct ocfs2_write_lock_info *info,
				     struct ocfs2_buffer_lock_ctxt *ctxt);

#endif  /* OCFS2_MMAP_H */
