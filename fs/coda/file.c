/*
 * File operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>
#include <linux/coda_proc.h>

/* if CODA_STORE fails with EOPNOTSUPP, venus clearly doesn't support
 * CODA_STORE/CODA_RELEASE and we fall back on using the CODA_CLOSE upcall */
int use_coda_close;

static ssize_t
coda_file_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct coda_inode_info *cii = ITOC(inode);
	struct file *cfile;

	cfile = cii->c_container;
	if (!cfile) BUG();

	if (!cfile->f_op || !cfile->f_op->read)
		return -EINVAL;

	return cfile->f_op->read(cfile, buf, count, ppos);
}

static ssize_t
coda_file_write(struct file *file,const char *buf,size_t count,loff_t *ppos)
{
	struct inode *cinode, *inode = file->f_dentry->d_inode;
	struct coda_inode_info *cii = ITOC(inode);
	struct file *cfile;
	ssize_t ret;
	int flags;

	cfile = cii->c_container;
	if (!cfile) BUG();

	if (!cfile->f_op || !cfile->f_op->write)
		return -EINVAL;

	cinode = cfile->f_dentry->d_inode;
	down(&inode->i_sem);
	flags = cfile->f_flags;
        cfile->f_flags |= file->f_flags & (O_APPEND | O_SYNC);

	ret = cfile->f_op->write(cfile, buf, count, ppos);

	cfile->f_flags = flags;
	inode->i_size = cinode->i_size;
	up(&inode->i_sem);

	return ret;
}

static int
coda_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct coda_inode_info *cii = ITOC(inode);
	struct file *cfile;

	cfile = cii->c_container;

	if (!cfile) BUG();

	if (!cfile->f_op || !cfile->f_op->mmap)
		return -ENODEV;

	return cfile->f_op->mmap(cfile, vma);
}

int coda_open(struct inode *i, struct file *f)
{
	struct file *fh = NULL;
	int error = 0;
	unsigned short flags = f->f_flags & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_cred *cred;
	struct coda_inode_info *cii;

	lock_kernel();
	coda_vfs_stat.open++;

	CDEBUG(D_SPECIAL, "OPEN inode number: %ld, count %d, flags %o.\n", 
	       f->f_dentry->d_inode->i_ino, atomic_read(&f->f_dentry->d_count), flags);

	error = venus_open(i->i_sb, coda_i2f(i), coda_flags, &fh); 
	if (error || !fh) {
	        CDEBUG(D_FILE, "coda_open: venus_open result %d\n", error);
		unlock_kernel();
		return error;
	}

	/* coda_upcall returns filehandle of container file object */
	cii = ITOC(i);
	if (cii->c_container)
		fput(cii->c_container);

	cii->c_contcount++;
	cii->c_container = fh;
	i->i_mapping = &cii->c_container->f_dentry->d_inode->i_data;

	cred = kmalloc(sizeof(struct coda_cred), GFP_KERNEL);

	/* If the allocation failed, we'll just muddle on. This actually works
	 * fine for normal cases. (i.e. when open credentials are the same as
	 * close credentials) */
	if (cred) {
		coda_load_creds(cred);
		f->private_data = cred;
	}

	CDEBUG(D_FILE, "result %d, coda i->i_count is %d, cii->contcount is %d for ino %ld\n", 
	       error, atomic_read(&i->i_count), cii->c_contcount, i->i_ino);
	CDEBUG(D_FILE, "cache ino: %ld, count %d, ops %p\n", 
	       fh->f_dentry->d_inode->i_ino,
	       atomic_read(&fh->f_dentry->d_inode->i_count),
               fh->f_dentry->d_inode->i_op);
	unlock_kernel();
	return 0;
}


int coda_flush(struct file *file)
{
	unsigned short flags = (file->f_flags) & (~O_EXCL);
	unsigned short cflags;
	struct coda_inode_info *cii;
	struct file *cfile;
	struct inode *cinode, *inode;
	int err = 0, fcnt;

	coda_vfs_stat.flush++;

	/* No need to make an upcall when we have not made any modifications
	 * to the file */
	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		return 0;

	if (use_coda_close)
		return 0;

	fcnt = file_count(file);
	if (fcnt > 1) return 0;

	cflags = coda_flags_to_cflags(flags);

	inode = file->f_dentry->d_inode;
	cii = ITOC(inode);
	cfile = cii->c_container;
	if (!cfile) BUG();

	cinode = cfile->f_dentry->d_inode;

	CDEBUG(D_FILE, "FLUSH coda (file %p ct %d)\n", file, fcnt);

	err = venus_store(inode->i_sb, coda_i2f(inode), cflags,
                          (struct coda_cred *)file->private_data);
	if (err == -EOPNOTSUPP) {
		use_coda_close = 1;
		err = 0;
	}

	CDEBUG(D_FILE, "coda_flush: result: %d\n", err);
	return err;
}

int coda_release(struct inode *i, struct file *f)
{
	unsigned short flags = (f->f_flags) & (~O_EXCL);
	unsigned short cflags = coda_flags_to_cflags(flags);
	struct coda_inode_info *cii;
	struct file *cfile;
	int err = 0;

	lock_kernel();
	coda_vfs_stat.release++;
 
	if (!use_coda_close) {
		err = venus_release(i->i_sb, coda_i2f(i), cflags);
		if (err == -EOPNOTSUPP) {
			use_coda_close = 1;
			err = 0;
		}
	}

	if (use_coda_close)
		err = venus_close(i->i_sb, coda_i2f(i), cflags,
                                  (struct coda_cred *)f->private_data);

	cii = ITOC(i);
	cfile = cii->c_container;
	if (!cfile) BUG();

	if (--cii->c_contcount) {
		unlock_kernel();
		return err;
	}

	i->i_mapping = &i->i_data;
	fput(cfile);
	cii->c_container = NULL;

	if (f->private_data) {
		kfree(f->private_data);
		f->private_data = NULL;
	}

	unlock_kernel();
	return err;
}

int coda_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct file *cfile;
	struct dentry *cdentry;
	struct inode *cinode, *inode = dentry->d_inode;
	struct coda_inode_info *cii = ITOC(inode);
	int err = 0;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;

	cfile = cii->c_container;
	if (!cfile) BUG();

	coda_vfs_stat.fsync++;

	if (cfile->f_op && cfile->f_op->fsync) {
		cdentry = cfile->f_dentry;
		cinode = cdentry->d_inode;
		down(&cinode->i_sem);
		err = cfile->f_op->fsync(cfile, cdentry, datasync);
		up(&cinode->i_sem);
	}

	if ( !err && !datasync ) {
		lock_kernel();
		err = venus_fsync(inode->i_sb, coda_i2f(inode));
		unlock_kernel();
	}

	return err;
}

struct file_operations coda_file_operations = {
	llseek:		generic_file_llseek,
	read:		coda_file_read,
	write:		coda_file_write,
	mmap:		coda_file_mmap,
	open:		coda_open,
	flush:  	coda_flush,
	release:	coda_release,
	fsync:		coda_fsync,
};

