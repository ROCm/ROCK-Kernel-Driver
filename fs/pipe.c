/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992, 1999  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pipe_fs_i.h>
#include <linux/uio.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>

/*
 * We use a start+len construction, which provides full use of the 
 * allocated memory.
 * -- Florian Coosmann (FGC)
 * 
 * Reads with count = 0 should always return 0.
 * -- Julian Bradfield 1999-06-07.
 *
 * FIFOs and Pipes now generate SIGIO for both readers and writers.
 * -- Jeremy Elson <jelson@circlemud.org> 2001-08-16
 *
 * pipe_read & write cleanup
 * -- Manfred Spraul <manfred@colorfullife.com> 2002-05-09
 */

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct inode * inode)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(PIPE_WAIT(*inode), &wait, TASK_INTERRUPTIBLE);
	up(PIPE_SEM(*inode));
	schedule();
	finish_wait(PIPE_WAIT(*inode), &wait);
	down(PIPE_SEM(*inode));
}

static inline int
pipe_iov_copy_from_user(void *to, struct iovec *iov, unsigned long len)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (copy_from_user(to, iov->iov_base, copy))
			return -EFAULT;
		to += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

static inline int
pipe_iov_copy_to_user(struct iovec *iov, const void *from, unsigned long len)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (copy_to_user(iov->iov_base, from, copy))
			return -EFAULT;
		from += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

static ssize_t
pipe_readv(struct file *filp, const struct iovec *_iov,
	   unsigned long nr_segs, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int do_wakeup;
	ssize_t ret;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;

	total_len = iov_length(iov, nr_segs);
	/* Null read succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	down(PIPE_SEM(*inode));
	for (;;) {
		int size = PIPE_LEN(*inode);
		if (size) {
			char *pipebuf = PIPE_BASE(*inode) + PIPE_START(*inode);
			ssize_t chars = PIPE_MAX_RCHUNK(*inode);

			if (chars > total_len)
				chars = total_len;
			if (chars > size)
				chars = size;

			if (pipe_iov_copy_to_user(iov, pipebuf, chars)) {
				if (!ret) ret = -EFAULT;
				break;
			}
			ret += chars;

			PIPE_START(*inode) += chars;
			PIPE_START(*inode) &= (PIPE_SIZE - 1);
			PIPE_LEN(*inode) -= chars;
			total_len -= chars;
			do_wakeup = 1;
			if (!total_len)
				break;	/* common path: read succeeded */
		}
		if (PIPE_LEN(*inode)) /* test for cyclic buffers */
			continue;
		if (!PIPE_WRITERS(*inode))
			break;
		if (!PIPE_WAITING_WRITERS(*inode)) {
			/* syscall merging: Usually we must not sleep
			 * if O_NONBLOCK is set, or if we got some data.
			 * But if a writer sleeps in kernel space, then
			 * we can wait for that data without violating POSIX.
			 */
			if (ret)
				break;
			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
		}
		if (signal_pending(current)) {
			if (!ret) ret = -ERESTARTSYS;
			break;
		}
		if (do_wakeup) {
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
 			kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
		}
		pipe_wait(inode);
	}
	up(PIPE_SEM(*inode));
	/* Signal writers asynchronously that there is more room.  */
	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
	}
	if (ret > 0)
		file_accessed(filp);
	return ret;
}

static ssize_t
pipe_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = count };
	return pipe_readv(filp, &iov, 1, ppos);
}

static ssize_t
pipe_writev(struct file *filp, const struct iovec *_iov,
	    unsigned long nr_segs, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	ssize_t ret;
	size_t min;
	int do_wakeup;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;

	total_len = iov_length(iov, nr_segs);
	/* Null write succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	min = total_len;
	if (min > PIPE_BUF)
		min = 1;
	down(PIPE_SEM(*inode));
	for (;;) {
		int free;
		if (!PIPE_READERS(*inode)) {
			send_sig(SIGPIPE, current, 0);
			if (!ret) ret = -EPIPE;
			break;
		}
		free = PIPE_FREE(*inode);
		if (free >= min) {
			/* transfer data */
			ssize_t chars = PIPE_MAX_WCHUNK(*inode);
			char *pipebuf = PIPE_BASE(*inode) + PIPE_END(*inode);
			/* Always wakeup, even if the copy fails. Otherwise
			 * we lock up (O_NONBLOCK-)readers that sleep due to
			 * syscall merging.
			 */
			do_wakeup = 1;
			if (chars > total_len)
				chars = total_len;
			if (chars > free)
				chars = free;

			if (pipe_iov_copy_from_user(pipebuf, iov, chars)) {
				if (!ret) ret = -EFAULT;
				break;
			}
			ret += chars;

			PIPE_LEN(*inode) += chars;
			total_len -= chars;
			if (!total_len)
				break;
		}
		if (PIPE_FREE(*inode) && ret) {
			/* handle cyclic data buffers */
			min = 1;
			continue;
		}
		if (filp->f_flags & O_NONBLOCK) {
			if (!ret) ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			if (!ret) ret = -ERESTARTSYS;
			break;
		}
		if (do_wakeup) {
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
			kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
			do_wakeup = 0;
		}
		PIPE_WAITING_WRITERS(*inode)++;
		pipe_wait(inode);
		PIPE_WAITING_WRITERS(*inode)--;
	}
	up(PIPE_SEM(*inode));
	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
	}
	if (ret > 0)
		inode_update_time(inode, 1);	/* mtime and ctime */
	return ret;
}

static ssize_t
pipe_write(struct file *filp, const char __user *buf,
	   size_t count, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = count };
	return pipe_writev(filp, &iov, 1, ppos);
}

static ssize_t
bad_pipe_r(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static ssize_t
bad_pipe_w(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static int
pipe_ioctl(struct inode *pino, struct file *filp,
	   unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case FIONREAD:
			return put_user(PIPE_LEN(*pino), (int __user *)arg);
		default:
			return -EINVAL;
	}
}

/* No kernel lock held - fine */
static unsigned int
pipe_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct inode *inode = filp->f_dentry->d_inode;

	poll_wait(filp, PIPE_WAIT(*inode), wait);

	/* Reading only -- no need for acquiring the semaphore.  */
	mask = POLLIN | POLLRDNORM;
	if (PIPE_EMPTY(*inode))
		mask = POLLOUT | POLLWRNORM;
	if (!PIPE_WRITERS(*inode) && filp->f_version != PIPE_WCOUNTER(*inode))
		mask |= POLLHUP;
	if (!PIPE_READERS(*inode))
		mask |= POLLERR;

	return mask;
}

/* FIXME: most Unices do not set POLLERR for fifos */
#define fifo_poll pipe_poll

static int
pipe_release(struct inode *inode, int decr, int decw)
{
	down(PIPE_SEM(*inode));
	PIPE_READERS(*inode) -= decr;
	PIPE_WRITERS(*inode) -= decw;
	if (!PIPE_READERS(*inode) && !PIPE_WRITERS(*inode)) {
		struct pipe_inode_info *info = inode->i_pipe;
		inode->i_pipe = NULL;
		free_page((unsigned long) info->base);
		kfree(info);
	} else {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
		kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
	}
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_read_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	down(PIPE_SEM(*inode));
	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_READERS(*inode));
	up(PIPE_SEM(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_write_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	down(PIPE_SEM(*inode));
	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_WRITERS(*inode));
	up(PIPE_SEM(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_rdwr_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	down(PIPE_SEM(*inode));

	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_READERS(*inode));

	if (retval >= 0)
		retval = fasync_helper(fd, filp, on, PIPE_FASYNC_WRITERS(*inode));

	up(PIPE_SEM(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_read_release(struct inode *inode, struct file *filp)
{
	pipe_read_fasync(-1, filp, 0);
	return pipe_release(inode, 1, 0);
}

static int
pipe_write_release(struct inode *inode, struct file *filp)
{
	pipe_write_fasync(-1, filp, 0);
	return pipe_release(inode, 0, 1);
}

static int
pipe_rdwr_release(struct inode *inode, struct file *filp)
{
	int decr, decw;

	pipe_rdwr_fasync(-1, filp, 0);
	decr = (filp->f_mode & FMODE_READ) != 0;
	decw = (filp->f_mode & FMODE_WRITE) != 0;
	return pipe_release(inode, decr, decw);
}

static int
pipe_read_open(struct inode *inode, struct file *filp)
{
	/* We could have perhaps used atomic_t, but this and friends
	   below are the only places.  So it doesn't seem worthwhile.  */
	down(PIPE_SEM(*inode));
	PIPE_READERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_write_open(struct inode *inode, struct file *filp)
{
	down(PIPE_SEM(*inode));
	PIPE_WRITERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_rdwr_open(struct inode *inode, struct file *filp)
{
	down(PIPE_SEM(*inode));
	if (filp->f_mode & FMODE_READ)
		PIPE_READERS(*inode)++;
	if (filp->f_mode & FMODE_WRITE)
		PIPE_WRITERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on FIFOs.
 */
struct file_operations read_fifo_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= bad_pipe_w,
	.poll		= fifo_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_read_open,
	.release	= pipe_read_release,
	.fasync		= pipe_read_fasync,
};

struct file_operations write_fifo_fops = {
	.llseek		= no_llseek,
	.read		= bad_pipe_r,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= fifo_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_write_open,
	.release	= pipe_write_release,
	.fasync		= pipe_write_fasync,
};

struct file_operations rdwr_fifo_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= fifo_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_rdwr_open,
	.release	= pipe_rdwr_release,
	.fasync		= pipe_rdwr_fasync,
};

struct file_operations read_pipe_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= bad_pipe_w,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_read_open,
	.release	= pipe_read_release,
	.fasync		= pipe_read_fasync,
};

struct file_operations write_pipe_fops = {
	.llseek		= no_llseek,
	.read		= bad_pipe_r,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_write_open,
	.release	= pipe_write_release,
	.fasync		= pipe_write_fasync,
};

struct file_operations rdwr_pipe_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_rdwr_open,
	.release	= pipe_rdwr_release,
	.fasync		= pipe_rdwr_fasync,
};

struct inode* pipe_new(struct inode* inode)
{
	unsigned long page;

	page = __get_free_page(GFP_USER);
	if (!page)
		return NULL;

	inode->i_pipe = kmalloc(sizeof(struct pipe_inode_info), GFP_KERNEL);
	if (!inode->i_pipe)
		goto fail_page;

	init_waitqueue_head(PIPE_WAIT(*inode));
	PIPE_BASE(*inode) = (char*) page;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
	PIPE_WAITING_WRITERS(*inode) = 0;
	PIPE_RCOUNTER(*inode) = PIPE_WCOUNTER(*inode) = 1;
	*PIPE_FASYNC_READERS(*inode) = *PIPE_FASYNC_WRITERS(*inode) = NULL;

	return inode;
fail_page:
	free_page(page);
	return NULL;
}

static struct vfsmount *pipe_mnt;
static int pipefs_delete_dentry(struct dentry *dentry)
{
	return 1;
}
static struct dentry_operations pipefs_dentry_operations = {
	.d_delete	= pipefs_delete_dentry,
};

static struct inode * get_pipe_inode(void)
{
	struct inode *inode = new_inode(pipe_mnt->mnt_sb);

	if (!inode)
		goto fail_inode;

	if(!pipe_new(inode))
		goto fail_iput;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	inode->i_fop = &rdwr_pipe_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because "mark_inode_dirty()" will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_blksize = PAGE_SIZE;
	return inode;

fail_iput:
	iput(inode);
fail_inode:
	return NULL;
}

int do_pipe(int *fd)
{
	struct qstr this;
	char name[32];
	struct dentry *dentry;
	struct inode * inode;
	struct file *f1, *f2;
	int error;
	int i,j;

	error = -ENFILE;
	f1 = get_empty_filp();
	if (!f1)
		goto no_files;

	f2 = get_empty_filp();
	if (!f2)
		goto close_f1;

	inode = get_pipe_inode();
	if (!inode)
		goto close_f12;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode;
	i = error;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode_i;
	j = error;

	error = -ENOMEM;
	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.len = strlen(name);
	this.hash = inode->i_ino; /* will go */
	dentry = d_alloc(pipe_mnt->mnt_sb->s_root, &this);
	if (!dentry)
		goto close_f12_inode_i_j;
	dentry->d_op = &pipefs_dentry_operations;
	d_add(dentry, inode);
	f1->f_vfsmnt = f2->f_vfsmnt = mntget(mntget(pipe_mnt));
	f1->f_dentry = f2->f_dentry = dget(dentry);
	f1->f_mapping = f2->f_mapping = inode->i_mapping;

	/* read file */
	f1->f_pos = f2->f_pos = 0;
	f1->f_flags = O_RDONLY;
	f1->f_op = &read_pipe_fops;
	f1->f_mode = FMODE_READ;
	f1->f_version = 0;

	/* write file */
	f2->f_flags = O_WRONLY;
	f2->f_op = &write_pipe_fops;
	f2->f_mode = FMODE_WRITE;
	f2->f_version = 0;

	fd_install(i, f1);
	fd_install(j, f2);
	fd[0] = i;
	fd[1] = j;
	return 0;

close_f12_inode_i_j:
	put_unused_fd(j);
close_f12_inode_i:
	put_unused_fd(i);
close_f12_inode:
	free_page((unsigned long) PIPE_BASE(*inode));
	kfree(inode->i_pipe);
	inode->i_pipe = NULL;
	iput(inode);
close_f12:
	put_filp(f2);
close_f1:
	put_filp(f1);
no_files:
	return error;	
}

/*
 * pipefs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pipe: will go nicely and kill the special-casing in procfs.
 */

static struct super_block *pipefs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "pipe:", NULL, PIPEFS_MAGIC);
}

static struct file_system_type pipe_fs_type = {
	.name		= "pipefs",
	.get_sb		= pipefs_get_sb,
	.kill_sb	= kill_anon_super,
};

static int __init init_pipe_fs(void)
{
	int err = register_filesystem(&pipe_fs_type);
	if (!err) {
		pipe_mnt = kern_mount(&pipe_fs_type);
		if (IS_ERR(pipe_mnt)) {
			err = PTR_ERR(pipe_mnt);
			unregister_filesystem(&pipe_fs_type);
		}
	}
	return err;
}

static void __exit exit_pipe_fs(void)
{
	unregister_filesystem(&pipe_fs_type);
	mntput(pipe_mnt);
}

module_init(init_pipe_fs)
module_exit(exit_pipe_fs)
