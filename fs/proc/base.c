/*
 *  linux/fs/proc/base.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc base directory handling functions
 *
 *  1999, Al Viro. Rewritten. Now it covers the whole per-process part.
 *  Instead of using magical inumbers to determine the kind of object
 *  we allocate and fill in-core inodes upon lookup. They don't even
 *  go into icache. We cache the reference to task_struct upon lookup too.
 *  Eventually it should become a filesystem in its own. We don't use the
 *  rest of procfs anymore.
 */

#include <asm/uaccess.h>

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/string.h>

/*
 * For hysterical raisins we keep the same inumbers as in the old procfs.
 * Feel free to change the macro below - just keep the range distinct from
 * inumbers of the rest of procfs (currently those are in 0x0000--0xffff).
 * As soon as we'll get a separate superblock we will be able to forget
 * about magical ranges too.
 */

#define fake_ino(pid,ino) (((pid)<<16)|(ino))

ssize_t proc_pid_read_maps(struct task_struct*,struct file*,char*,size_t,loff_t*);
int proc_pid_stat(struct task_struct*,char*);
int proc_pid_status(struct task_struct*,char*);
int proc_pid_statm(struct task_struct*,char*);
int proc_pid_cpu(struct task_struct*,char*);

static int proc_fd_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	if (inode->u.proc_i.file) {
		*mnt = mntget(inode->u.proc_i.file->f_vfsmnt);
		*dentry = dget(inode->u.proc_i.file->f_dentry);
		return 0;
	}
	return -ENOENT;
}

static int proc_exe_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct mm_struct * mm;
	struct vm_area_struct * vma;
	int result = -ENOENT;
	struct task_struct *task = inode->u.proc_i.task;

	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (!mm)
		goto out;
	down(&mm->mmap_sem);
	vma = mm->mmap;
	while (vma) {
		if ((vma->vm_flags & VM_EXECUTABLE) && 
		    vma->vm_file) {
			*mnt = mntget(vma->vm_file->f_vfsmnt);
			*dentry = dget(vma->vm_file->f_dentry);
			result = 0;
			break;
		}
		vma = vma->vm_next;
	}
	up(&mm->mmap_sem);
	mmput(mm);
out:
	return result;
}

static int proc_cwd_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct fs_struct *fs;
	int result = -ENOENT;
	task_lock(inode->u.proc_i.task);
	fs = inode->u.proc_i.task->fs;
	if(fs)
		atomic_inc(&fs->count);
	task_unlock(inode->u.proc_i.task);
	if (fs) {
		read_lock(&fs->lock);
		*mnt = mntget(fs->pwdmnt);
		*dentry = dget(fs->pwd);
		read_unlock(&fs->lock);
		result = 0;
		put_fs_struct(fs);
	}
	return result;
}

static int proc_root_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct fs_struct *fs;
	int result = -ENOENT;
	task_lock(inode->u.proc_i.task);
	fs = inode->u.proc_i.task->fs;
	if(fs)
		atomic_inc(&fs->count);
	task_unlock(inode->u.proc_i.task);
	if (fs) {
		read_lock(&fs->lock);
		*mnt = mntget(fs->rootmnt);
		*dentry = dget(fs->root);
		read_unlock(&fs->lock);
		result = 0;
		put_fs_struct(fs);
	}
	return result;
}

static int proc_pid_environ(struct task_struct *task, char * buffer)
{
	struct mm_struct *mm;
	int res = 0;
	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (mm) {
		int len = mm->env_end - mm->env_start;
		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		res = access_process_vm(task, mm->env_start, buffer, len, 0);
		mmput(mm);
	}
	return res;
}

static int proc_pid_cmdline(struct task_struct *task, char * buffer)
{
	struct mm_struct *mm;
	int res = 0;
	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (mm) {
		int len = mm->arg_end - mm->arg_start;
		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		res = access_process_vm(task, mm->arg_start, buffer, len, 0);
		// If the nul at the end of args has been overwritten, then
		// assume application is using setproctitle(3).
		if ( res > 0 && buffer[res-1] != '\0' )
		{
			len = strnlen( buffer, res );
			if ( len < res )
			{
			    res = len;
			}
			else
			{
				len = mm->env_end - mm->env_start;
				if (len > PAGE_SIZE - res)
					len = PAGE_SIZE - res;
				res += access_process_vm(task, mm->env_start, buffer+res, len, 0);
				res = strnlen( buffer, res );
			}
		}
		mmput(mm);
	}
	return res;
}

/************************************************************************/
/*                       Here the fs part begins                        */
/************************************************************************/

/* permission checks */

static int standard_permission(struct inode *inode, int mask)
{
	int mode = inode->i_mode;

	if ((mask & S_IWOTH) && IS_RDONLY(inode) &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS; /* Nobody gets write access to a read-only fs */
	else if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES; /* Nobody gets write access to an immutable file */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & S_IRWXO) == mask) || capable(CAP_DAC_OVERRIDE))
		return 0;
	/* read and search access */
	if ((mask == S_IROTH) ||
	    (S_ISDIR(mode)  && !(mask & ~(S_IROTH | S_IXOTH))))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;
	return -EACCES;
}

static int proc_check_root(struct inode *inode)
{
	struct dentry *de, *base, *root;
	struct vfsmount *our_vfsmnt, *vfsmnt, *mnt;
	int res = 0;

	if (proc_root_link(inode, &root, &vfsmnt)) /* Ewww... */
		return -ENOENT;
	read_lock(&current->fs->lock);
	our_vfsmnt = mntget(current->fs->rootmnt);
	base = dget(current->fs->root);
	read_unlock(&current->fs->lock);

	spin_lock(&dcache_lock);
	de = root;
	mnt = vfsmnt;

	while (vfsmnt != our_vfsmnt) {
		if (vfsmnt == vfsmnt->mnt_parent)
			goto out;
		de = vfsmnt->mnt_mountpoint;
		vfsmnt = vfsmnt->mnt_parent;
	}

	if (!is_subdir(de, base))
		goto out;
	spin_unlock(&dcache_lock);

exit:
	dput(base);
	mntput(our_vfsmnt);
	dput(root);
	mntput(mnt);
	return res;
out:
	spin_unlock(&dcache_lock);
	res = -EACCES;
	goto exit;
}

static int proc_permission(struct inode *inode, int mask)
{
	if (standard_permission(inode, mask) != 0)
		return -EACCES;
	return proc_check_root(inode);
}

static ssize_t pid_maps_read(struct file * file, char * buf,
			      size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	struct task_struct *task = inode->u.proc_i.task;
	ssize_t res;

	res = proc_pid_read_maps(task, file, buf, count, ppos);
	return res;
}

static struct file_operations proc_maps_operations = {
	read:		pid_maps_read,
};

#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static ssize_t proc_info_read(struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	unsigned long page;
	ssize_t length;
	ssize_t end;
	struct task_struct *task = inode->u.proc_i.task;

	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;
	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	length = inode->u.proc_i.op.proc_read(task, (char*)page);

	if (length < 0) {
		free_page(page);
		return length;
	}
	/* Static 4kB (or whatever) block capacity */
	if (*ppos >= length) {
		free_page(page);
		return 0;
	}
	if (count + *ppos > length)
		count = length - *ppos;
	end = count + *ppos;
	copy_to_user(buf, (char *) page + *ppos, count);
	*ppos = end;
	free_page(page);
	return count;
}

static struct file_operations proc_info_file_operations = {
	read:		proc_info_read,
};

#define MAY_PTRACE(p) \
(p==current||(p->p_pptr==current&&(p->ptrace & PT_PTRACED)&&p->state==TASK_STOPPED))

static ssize_t mem_read(struct file * file, char * buf,
			size_t count, loff_t *ppos)
{
	struct task_struct *task = file->f_dentry->d_inode->u.proc_i.task;
	char *page;
	unsigned long src = *ppos;
	int copied = 0;

	if (!MAY_PTRACE(task))
		return -ESRCH;

	page = (char *)__get_free_page(GFP_USER);
	if (!page)
		return -ENOMEM;

	while (count > 0) {
		int this_len, retval;

		this_len = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		retval = access_process_vm(task, src, page, this_len, 0);
		if (!retval) {
			if (!copied)
				copied = -EIO;
			break;
		}
		if (copy_to_user(buf, page, retval)) {
			copied = -EFAULT;
			break;
		}
		copied += retval;
		src += retval;
		buf += retval;
		count -= retval;
	}
	*ppos = src;
	free_page((unsigned long) page);
	return copied;
}

#define mem_write NULL

#ifndef mem_write
/* This is a security hazard */
static ssize_t mem_write(struct file * file, const char * buf,
			 size_t count, loff_t *ppos)
{
	int copied = 0;
	char *page;
	struct task_struct *task = file->f_dentry->d_inode->u.proc_i.task;
	unsigned long dst = *ppos;

	if (!MAY_PTRACE(task))
		return -ESRCH;

	page = (char *)__get_free_page(GFP_USER);
	if (!page)
		return -ENOMEM;

	while (count > 0) {
		int this_len, retval;

		this_len = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		if (copy_from_user(page, buf, this_len)) {
			copied = -EFAULT;
			break;
		}
		retval = access_process_vm(task, dst, page, this_len, 1);
		if (!retval) {
			if (!copied)
				copied = -EIO;
			break;
		}
		copied += retval;
		buf += retval;
		dst += retval;
		count -= retval;			
	}
	*ppos = dst;
	free_page((unsigned long) page);
	return copied;
}
#endif

static struct file_operations proc_mem_operations = {
	read:		mem_read,
	write:		mem_write,
};

static struct inode_operations proc_mem_inode_operations = {
	permission:	proc_permission,
};

static int proc_pid_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	int error = -EACCES;

	/* We don't need a base pointer in the /proc filesystem */
	path_release(nd);

	if (current->fsuid != inode->i_uid && !capable(CAP_DAC_OVERRIDE))
		goto out;
	error = proc_check_root(inode);
	if (error)
		goto out;

	error = inode->u.proc_i.op.proc_get_link(inode, &nd->dentry, &nd->mnt);
	nd->last_type = LAST_BIND;
out:
	return error;
}

static int do_proc_readlink(struct dentry *dentry, struct vfsmount *mnt,
			    char * buffer, int buflen)
{
	struct inode * inode;
	char * tmp = (char*)__get_free_page(GFP_KERNEL), *path;
	int len;

	if (!tmp)
		return -ENOMEM;
		
	inode = dentry->d_inode;
	path = d_path(dentry, mnt, tmp, PAGE_SIZE);
	len = tmp + PAGE_SIZE - 1 - path;

	if (len < buflen)
		buflen = len;
	copy_to_user(buffer, path, buflen);
	free_page((unsigned long)tmp);
	return buflen;
}

static int proc_pid_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	int error = -EACCES;
	struct inode *inode = dentry->d_inode;
	struct dentry *de;
	struct vfsmount *mnt = NULL;

	if (current->fsuid != inode->i_uid && !capable(CAP_DAC_OVERRIDE))
		goto out;
	error = proc_check_root(inode);
	if (error)
		goto out;

	error = inode->u.proc_i.op.proc_get_link(inode, &de, &mnt);
	if (error)
		goto out;

	error = do_proc_readlink(de, mnt, buffer, buflen);
	dput(de);
	mntput(mnt);
out:
	return error;
}

static struct inode_operations proc_pid_link_inode_operations = {
	readlink:	proc_pid_readlink,
	follow_link:	proc_pid_follow_link
};

struct pid_entry {
	int type;
	int len;
	char *name;
	mode_t mode;
};

enum pid_directory_inos {
	PROC_PID_INO = 2,
	PROC_PID_STATUS,
	PROC_PID_MEM,
	PROC_PID_CWD,
	PROC_PID_ROOT,
	PROC_PID_EXE,
	PROC_PID_FD,
	PROC_PID_ENVIRON,
	PROC_PID_CMDLINE,
	PROC_PID_STAT,
	PROC_PID_STATM,
	PROC_PID_MAPS,
	PROC_PID_CPU,
	PROC_PID_FD_DIR = 0x8000,	/* 0x8000-0xffff */
};

#define E(type,name,mode) {(type),sizeof(name)-1,(name),(mode)}
static struct pid_entry base_stuff[] = {
  E(PROC_PID_FD,	"fd",		S_IFDIR|S_IRUSR|S_IXUSR),
  E(PROC_PID_ENVIRON,	"environ",	S_IFREG|S_IRUSR),
  E(PROC_PID_STATUS,	"status",	S_IFREG|S_IRUGO),
  E(PROC_PID_CMDLINE,	"cmdline",	S_IFREG|S_IRUGO),
  E(PROC_PID_STAT,	"stat",		S_IFREG|S_IRUGO),
  E(PROC_PID_STATM,	"statm",	S_IFREG|S_IRUGO),
#ifdef CONFIG_SMP
  E(PROC_PID_CPU,	"cpu",		S_IFREG|S_IRUGO),
#endif
  E(PROC_PID_MAPS,	"maps",		S_IFREG|S_IRUGO),
  E(PROC_PID_MEM,	"mem",		S_IFREG|S_IRUSR|S_IWUSR),
  E(PROC_PID_CWD,	"cwd",		S_IFLNK|S_IRWXUGO),
  E(PROC_PID_ROOT,	"root",		S_IFLNK|S_IRWXUGO),
  E(PROC_PID_EXE,	"exe",		S_IFLNK|S_IRWXUGO),
  {0,0,NULL,0}
};
#undef E

#define NUMBUF 10

static int proc_readfd(struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct task_struct *p = inode->u.proc_i.task;
	unsigned int fd, pid, ino;
	int retval;
	char buf[NUMBUF];
	struct files_struct * files;

	retval = 0;
	pid = p->pid;

	fd = filp->f_pos;
	switch (fd) {
		case 0:
			if (filldir(dirent, ".", 1, 0, inode->i_ino, DT_DIR) < 0)
				goto out;
			filp->f_pos++;
		case 1:
			ino = fake_ino(pid, PROC_PID_INO);
			if (filldir(dirent, "..", 2, 1, ino, DT_DIR) < 0)
				goto out;
			filp->f_pos++;
		default:
			task_lock(p);
			files = p->files;
			if (files)
				atomic_inc(&files->count);
			task_unlock(p);
			if (!files)
				goto out;
			for (fd = filp->f_pos-2;
			     fd < files->max_fds;
			     fd++, filp->f_pos++) {
				unsigned int i,j;

				if (!fcheck_files(files, fd))
					continue;

				j = NUMBUF;
				i = fd;
				do {
					j--;
					buf[j] = '0' + (i % 10);
					i /= 10;
				} while (i);

				ino = fake_ino(pid, PROC_PID_FD_DIR + fd);
				if (filldir(dirent, buf+j, NUMBUF-j, fd+2, ino, DT_LNK) < 0)
					break;
			}
			put_files_struct(files);
	}
out:
	return retval;
}

static int proc_base_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	int i;
	int pid;
	struct inode *inode = filp->f_dentry->d_inode;
	struct pid_entry *p;

	pid = inode->u.proc_i.task->pid;
	if (!inode->u.proc_i.task->p_pptr)
		return -ENOENT;
	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, inode->i_ino, DT_DIR) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		case 1:
			if (filldir(dirent, "..", 2, i, PROC_ROOT_INO, DT_DIR) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		default:
			i -= 2;
			if (i>=sizeof(base_stuff)/sizeof(base_stuff[0]))
				return 1;
			p = base_stuff + i;
			while (p->name) {
				if (filldir(dirent, p->name, p->len, filp->f_pos,
					    fake_ino(pid, p->type), p->mode >> 12) < 0)
					return 0;
				filp->f_pos++;
				p++;
			}
	}
	return 1;
}

/* building an inode */

static struct inode *proc_pid_make_inode(struct super_block * sb, struct task_struct *task, int ino)
{
	struct inode * inode;

	/* We need a new inode */
	
	inode = new_inode(sb);
	if (!inode)
		goto out;

	/* Common stuff */

	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_ino = fake_ino(task->pid, ino);

	inode->u.proc_i.file = NULL;
	/*
	 * grab the reference to task.
	 */
	inode->u.proc_i.task = task;
	get_task_struct(task);
	if (!task->p_pptr)
		goto out_unlock;

	inode->i_uid = 0;
	inode->i_gid = 0;
	if (ino == PROC_PID_INO || task->dumpable) {
		inode->i_uid = task->euid;
		inode->i_gid = task->egid;
	}

out:
	return inode;

out_unlock:
	iput(inode);
	return NULL;
}

/* dentry stuff */

static int pid_fd_revalidate(struct dentry * dentry, int flags)
{
	return 0;
}

/*
 *	Exceptional case: normally we are not allowed to unhash a busy
 * directory. In this case, however, we can do it - no aliasing problems
 * due to the way we treat inodes.
 */
static int pid_base_revalidate(struct dentry * dentry, int flags)
{
	if (dentry->d_inode->u.proc_i.task->p_pptr)
		return 1;
	d_drop(dentry);
	return 0;
}

static int pid_delete_dentry(struct dentry * dentry)
{
	return 1;
}

static struct dentry_operations pid_fd_dentry_operations =
{
	d_revalidate:	pid_fd_revalidate,
	d_delete:	pid_delete_dentry,
};

static struct dentry_operations pid_dentry_operations =
{
	d_delete:	pid_delete_dentry,
};

static struct dentry_operations pid_base_dentry_operations =
{
	d_revalidate:	pid_base_revalidate,
	d_delete:	pid_delete_dentry,
};

/* Lookups */
#define MAX_MULBY10	((~0U-9)/10)

static struct dentry *proc_lookupfd(struct inode * dir, struct dentry * dentry)
{
	unsigned int fd, c;
	struct task_struct *task = dir->u.proc_i.task;
	struct file * file;
	struct files_struct * files;
	struct inode *inode;
	const char *name;
	int len;

	fd = 0;
	len = dentry->d_name.len;
	name = dentry->d_name.name;
	if (len > 1 && *name == '0') goto out;
	while (len-- > 0) {
		c = *name - '0';
		name++;
		if (c > 9)
			goto out;
		if (fd >= MAX_MULBY10)
			goto out;
		fd *= 10;
		fd += c;
	}

	inode = proc_pid_make_inode(dir->i_sb, task, PROC_PID_FD_DIR+fd);
	if (!inode)
		goto out;
	task_lock(task);
	files = task->files;
	if (files)
		atomic_inc(&files->count);
	task_unlock(task);
	if (!files)
		goto out_unlock;
	read_lock(&files->file_lock);
	file = inode->u.proc_i.file = fcheck_files(files, fd);
	if (!file)
		goto out_unlock2;
	get_file(file);
	read_unlock(&files->file_lock);
	put_files_struct(files);
	inode->i_op = &proc_pid_link_inode_operations;
	inode->i_size = 64;
	inode->i_mode = S_IFLNK;
	inode->u.proc_i.op.proc_get_link = proc_fd_link;
	if (file->f_mode & 1)
		inode->i_mode |= S_IRUSR | S_IXUSR;
	if (file->f_mode & 2)
		inode->i_mode |= S_IWUSR | S_IXUSR;
	dentry->d_op = &pid_fd_dentry_operations;
	d_add(dentry, inode);
	return NULL;

out_unlock2:
	put_files_struct(files);
	read_unlock(&files->file_lock);
out_unlock:
	iput(inode);
out:
	return ERR_PTR(-ENOENT);
}

static struct file_operations proc_fd_operations = {
	read:		generic_read_dir,
	readdir:	proc_readfd,
};

/*
 * proc directories can do almost nothing..
 */
static struct inode_operations proc_fd_inode_operations = {
	lookup:		proc_lookupfd,
	permission:	proc_permission,
};

static struct dentry *proc_base_lookup(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode;
	int error;
	struct task_struct *task = dir->u.proc_i.task;
	struct pid_entry *p;

	error = -ENOENT;
	inode = NULL;

	for (p = base_stuff; p->name; p++) {
		if (p->len != dentry->d_name.len)
			continue;
		if (!memcmp(dentry->d_name.name, p->name, p->len))
			break;
	}
	if (!p->name)
		goto out;

	error = -EINVAL;
	inode = proc_pid_make_inode(dir->i_sb, task, p->type);
	if (!inode)
		goto out;

	inode->i_mode = p->mode;
	/*
	 * Yes, it does not scale. And it should not. Don't add
	 * new entries into /proc/<pid>/ without very good reasons.
	 */
	switch(p->type) {
		case PROC_PID_FD:
			inode->i_nlink = 2;
			inode->i_op = &proc_fd_inode_operations;
			inode->i_fop = &proc_fd_operations;
			break;
		case PROC_PID_EXE:
			inode->i_op = &proc_pid_link_inode_operations;
			inode->u.proc_i.op.proc_get_link = proc_exe_link;
			break;
		case PROC_PID_CWD:
			inode->i_op = &proc_pid_link_inode_operations;
			inode->u.proc_i.op.proc_get_link = proc_cwd_link;
			break;
		case PROC_PID_ROOT:
			inode->i_op = &proc_pid_link_inode_operations;
			inode->u.proc_i.op.proc_get_link = proc_root_link;
			break;
		case PROC_PID_ENVIRON:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_environ;
			break;
		case PROC_PID_STATUS:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_status;
			break;
		case PROC_PID_STAT:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_stat;
			break;
		case PROC_PID_CMDLINE:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_cmdline;
			break;
		case PROC_PID_STATM:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_statm;
			break;
		case PROC_PID_MAPS:
			inode->i_fop = &proc_maps_operations;
			break;
#ifdef CONFIG_SMP
		case PROC_PID_CPU:
			inode->i_fop = &proc_info_file_operations;
			inode->u.proc_i.op.proc_read = proc_pid_cpu;
			break;
#endif
		case PROC_PID_MEM:
			inode->i_op = &proc_mem_inode_operations;
			inode->i_fop = &proc_mem_operations;
			break;
		default:
			printk("procfs: impossible type (%d)",p->type);
			iput(inode);
			return ERR_PTR(-EINVAL);
	}
	dentry->d_op = &pid_dentry_operations;
	d_add(dentry, inode);
	return NULL;

out:
	return ERR_PTR(error);
}

static struct file_operations proc_base_operations = {
	read:		generic_read_dir,
	readdir:	proc_base_readdir,
};

static struct inode_operations proc_base_inode_operations = {
	lookup:		proc_base_lookup,
};

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char tmp[30];
	sprintf(tmp, "%d", current->pid);
	return vfs_readlink(dentry,buffer,buflen,tmp);
}

static int proc_self_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char tmp[30];
	sprintf(tmp, "%d", current->pid);
	return vfs_follow_link(nd,tmp);
}	

static struct inode_operations proc_self_inode_operations = {
	readlink:	proc_self_readlink,
	follow_link:	proc_self_follow_link,
};

struct dentry *proc_pid_lookup(struct inode *dir, struct dentry * dentry)
{
	unsigned int pid, c;
	struct task_struct *task;
	const char *name;
	struct inode *inode;
	int len;

	pid = 0;
	name = dentry->d_name.name;
	len = dentry->d_name.len;
	if (len == 4 && !memcmp(name, "self", 4)) {
		inode = new_inode(dir->i_sb);
		if (!inode)
			return ERR_PTR(-ENOMEM);
		inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
		inode->i_ino = fake_ino(0, PROC_PID_INO);
		inode->u.proc_i.file = NULL;
		inode->u.proc_i.task = NULL;
		inode->i_mode = S_IFLNK|S_IRWXUGO;
		inode->i_uid = inode->i_gid = 0;
		inode->i_size = 64;
		inode->i_op = &proc_self_inode_operations;
		d_add(dentry, inode);
		return NULL;
	}
	while (len-- > 0) {
		c = *name - '0';
		name++;
		if (c > 9)
			goto out;
		if (pid >= MAX_MULBY10)
			goto out;
		pid *= 10;
		pid += c;
		if (!pid)
			goto out;
	}

	read_lock(&tasklist_lock);
	task = find_task_by_pid(pid);
	if (task)
		get_task_struct(task);
	read_unlock(&tasklist_lock);
	if (!task)
		goto out;

	inode = proc_pid_make_inode(dir->i_sb, task, PROC_PID_INO);

	free_task_struct(task);

	if (!inode)
		goto out;
	inode->i_mode = S_IFDIR|S_IRUGO|S_IXUGO;
	inode->i_op = &proc_base_inode_operations;
	inode->i_fop = &proc_base_operations;
	inode->i_nlink = 3;
	inode->i_flags|=S_IMMUTABLE;

	dentry->d_op = &pid_base_dentry_operations;
	d_add(dentry, inode);
	return NULL;
out:
	return ERR_PTR(-ENOENT);
}

void proc_pid_delete_inode(struct inode *inode)
{
	if (inode->u.proc_i.file)
		fput(inode->u.proc_i.file);
	if (inode->u.proc_i.task)
		free_task_struct(inode->u.proc_i.task);
}

#define PROC_NUMBUF 10
#define PROC_MAXPIDS 20

/*
 * Get a few pid's to return for filldir - we need to hold the
 * tasklist lock while doing this, and we must release it before
 * we actually do the filldir itself, so we use a temp buffer..
 */
static int get_pid_list(int index, unsigned int *pids)
{
	struct task_struct *p;
	int nr_pids = 0;

	index--;
	read_lock(&tasklist_lock);
	for_each_task(p) {
		int pid = p->pid;
		if (!pid)
			continue;
		if (--index >= 0)
			continue;
		pids[nr_pids] = pid;
		nr_pids++;
		if (nr_pids >= PROC_MAXPIDS)
			break;
	}
	read_unlock(&tasklist_lock);
	return nr_pids;
}

int proc_pid_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	unsigned int pid_array[PROC_MAXPIDS];
	char buf[PROC_NUMBUF];
	unsigned int nr = filp->f_pos - FIRST_PROCESS_ENTRY;
	unsigned int nr_pids, i;

	if (!nr) {
		ino_t ino = fake_ino(0,PROC_PID_INO);
		if (filldir(dirent, "self", 4, filp->f_pos, ino, DT_LNK) < 0)
			return 0;
		filp->f_pos++;
		nr++;
	}

	nr_pids = get_pid_list(nr, pid_array);

	for (i = 0; i < nr_pids; i++) {
		int pid = pid_array[i];
		ino_t ino = fake_ino(pid,PROC_PID_INO);
		unsigned long j = PROC_NUMBUF;

		do buf[--j] = '0' + (pid % 10); while (pid/=10);

		if (filldir(dirent, buf+j, PROC_NUMBUF-j, filp->f_pos, ino, DT_DIR) < 0)
			break;
		filp->f_pos++;
	}
	return 0;
}
