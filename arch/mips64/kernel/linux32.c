/* 
 * Conversion between 32-bit and 64-bit native system calls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * sys32_execve from ia64/ia32 code, Feb 2000, Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/highuid.h>
#include <linux/dirent.h>
#include <linux/resource.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/filter.h>
#include <linux/shm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/sysctl.h>
#include <linux/utime.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/timex.h>
#include <linux/dnotify.h>
#include <net/sock.h>

#include <asm/uaccess.h>
#include <asm/mman.h>
#include <asm/ipc.h>


#define A(__x) ((unsigned long)(__x))

/*
 * Revalidate the inode. This is required for proper NFS attribute caching.
 */
static __inline__ int
do_revalidate(struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;

	if (inode->i_op && inode->i_op->revalidate)
		return inode->i_op->revalidate(dentry);

	return 0;
}

static int cp_new_stat32(struct inode * inode, struct stat32 * statbuf)
{
	struct stat32 tmp;
	unsigned int blocks, indirect;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = kdev_t_to_nr(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	SET_STAT_UID(tmp, inode->i_uid);
	SET_STAT_GID(tmp, inode->i_gid);
	tmp.st_rdev = kdev_t_to_nr(inode->i_rdev);
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;

	/*
	 * st_blocks and st_blksize are approximated with a simple algorithm if
	 * they aren't supported directly by the filesystem. The minix and msdos
	 * filesystems don't keep track of blocks, so they would either have to
	 * be counted explicitly (by delving into the file itself), or by using
	 * this simple algorithm to get a reasonable (although not 100%
	 * accurate) value.
	 */

	/*
	 * Use minix fs values for the number of direct and indirect blocks.
	 * The count is now exact for the minix fs except that it counts zero
	 * blocks.  Everything is in units of BLOCK_SIZE until the assignment
	 * to tmp.st_blksize.
	 */
#define D_B   7
#define I_B   (BLOCK_SIZE / sizeof(unsigned short))

	if (!inode->i_blksize) {
		blocks = (tmp.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
		if (blocks > D_B) {
			indirect = (blocks - D_B + I_B - 1) / I_B;
			blocks += indirect;
			if (indirect > 1) {
				indirect = (indirect - 1 + I_B - 1) / I_B;
				blocks += indirect;
				if (indirect > 1)
					blocks++;
			}
		}
		tmp.st_blocks = (BLOCK_SIZE / 512) * blocks;
		tmp.st_blksize = BLOCK_SIZE;
	} else {
		tmp.st_blocks = inode->i_blocks;
		tmp.st_blksize = inode->i_blksize;
	}

	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage int sys32_newstat(char * filename, struct stat32 *statbuf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(filename, &nd);
	if (!error) {
		error = do_revalidate(nd.dentry);
		if (!error)
			error = cp_new_stat32(nd.dentry->d_inode, statbuf);

		path_release(&nd);
	}

	return error;
}

asmlinkage int sys32_newlstat(char * filename, struct stat32 *statbuf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(filename, &nd);
	if (!error) {
		error = do_revalidate(nd.dentry);
		if (!error)
			error = cp_new_stat32(nd.dentry->d_inode, statbuf);

		path_release(&nd);
	}

	return error;
}

asmlinkage long sys32_newfstat(unsigned int fd, struct stat32 * statbuf)
{
	struct file * f;
	int err = -EBADF;

	f = fget(fd);
	if (f) {
		struct dentry * dentry = f->f_dentry;

		err = do_revalidate(dentry);
		if (!err)
			err = cp_new_stat32(dentry->d_inode, statbuf);
		fput(f);
	}

	return err;
}

asmlinkage int sys_mmap2(void) {return 0;}

asmlinkage long sys_truncate(const char * path, unsigned long length);

asmlinkage int sys_truncate64(const char *path, unsigned int high,
			      unsigned int low)
{
	if ((int)high < 0)
		return -EINVAL;
	return sys_truncate(path, ((long) high << 32) | low);
}

asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);

asmlinkage int sys_ftruncate64(unsigned int fd, unsigned int high,
			       unsigned int low)
{
	if ((int)high < 0)
		return -EINVAL;
	return sys_ftruncate(fd, ((long) high << 32) | low);
}

extern asmlinkage int sys_utime(char * filename, struct utimbuf * times);

struct utimbuf32 {
	__kernel_time_t32 actime, modtime;
};

asmlinkage int sys32_utime(char * filename, struct utimbuf32 *times)
{
	struct utimbuf t;
	mm_segment_t old_fs;
	int ret;
	char *filenam;
	
	if (!times)
		return sys_utime(filename, NULL);
	if (get_user (t.actime, &times->actime) ||
	    __get_user (t.modtime, &times->modtime))
		return -EFAULT;
	filenam = getname (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		old_fs = get_fs();
		set_fs (KERNEL_DS); 
		ret = sys_utime(filenam, &t);
		set_fs (old_fs);
		putname (filenam);
	}
	return ret;
}

#if 0
/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p;
			/* egcs is stupid */
			if (!access_ok(VERIFY_READ, argv, sizeof (u32)))
				return -EFAULT;
			__get_user(p,argv);
			if (!p)
				break;
			argv++;
			if(++i > max)
				return -E2BIG;
		}
	}
	return i;
}


/*
 * 'copy_strings32()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
int copy_strings32(int argc, u32 * argv, struct linux_binprm *bprm) 
{
	while (argc-- > 0) {
		u32 str;
		int len;
		unsigned long pos;

		if (get_user(str, argv+argc) || !str ||
		     !(len = strnlen_user((char *)A(str), bprm->p))) 
			return -EFAULT;
		if (bprm->p < len) 
			return -E2BIG; 

		bprm->p -= len;
		/* XXX: add architecture specific overflow check here. */ 

		pos = bprm->p;
		while (len > 0) {
			char *kaddr;
			int i, new, err;
			struct page *page;
			int offset, bytes_to_copy;

			offset = pos % PAGE_SIZE;
			i = pos/PAGE_SIZE;
			page = bprm->page[i];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				bprm->page[i] = page;
				if (!page)
					return -ENOMEM;
				new = 1;
			}
			kaddr = kmap(page);

			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
					       PAGE_SIZE-offset-len);
			}
			err = copy_from_user(kaddr + offset, (char *)A(str),
			                     bytes_to_copy);
			flush_page_to_ram(page);
			kunmap(page);

			if (err)
				return -EFAULT; 

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	return 0;
}


/*
 * sys_execve32() executes a new program.
 */
int do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct dentry * dentry;
	int retval;
	int i;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES*sizeof(bprm.page[0])); 

	dentry = open_namei(filename, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	bprm.dentry = dentry;
	bprm.filename = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	if ((bprm.argc = count32(argv, bprm.p / sizeof(u32))) < 0) {
		dput(dentry);
		return bprm.argc;
	}

	if ((bprm.envc = count32(envp, bprm.p / sizeof(u32))) < 0) {
		dput(dentry);
		return bprm.envc;
	}

	retval = prepare_binprm(&bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings_kernel(1, &bprm.filename, &bprm);
	if (retval < 0) 
		goto out; 

	bprm.exec = bprm.p;
	retval = copy_strings32(bprm.envc, envp, &bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings32(bprm.argc, argv, &bprm);
	if (retval < 0) 
		goto out; 

	retval = search_binary_handler(&bprm,regs);
	if (retval >= 0)
		/* execve success */
		return retval;

out:
	/* Something went wrong, return the inode and free the argument pages*/
	if (bprm.dentry)
		dput(bprm.dentry);

	/* Assumes that free_page() can take a NULL argument. */ 
	/* I hope this is ok for all architectures */ 
	for (i = 0 ; i < MAX_ARG_PAGES ; i++)
		if (bprm.page[i])
			__free_page(bprm.page[i]);

	return retval;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys32_execve(abi64_no_regargs, struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) (long)regs.regs[4]);
	printk("Executing: %s\n", filename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve32(filename, (u32 *) (long)regs.regs[5],
	                  (u32 *) (long)regs.regs[6], &regs);
	putname(filename);

out:
	return error;
}
#else
static int
nargs(unsigned int arg, char **ap)
{
	char *ptr;
	int n, ret;

	n = 0;
	do {
		/* egcs is stupid */
		if (!access_ok(VERIFY_READ, arg, sizeof (unsigned int)))
			return -EFAULT;
		if (IS_ERR(ret = __get_user((long)ptr,(int *)A(arg))))
			return ret;
		if (ap)		/* no access_ok needed, we allocated */
			if (IS_ERR(ret = __put_user(ptr, ap++)))
				return ret;
		arg += sizeof(unsigned int);
		n++;
	} while (ptr);
	return(n - 1);
}

asmlinkage int 
sys32_execve(abi64_no_regargs, struct pt_regs regs)
{
	extern asmlinkage int sys_execve(abi64_no_regargs, struct pt_regs regs);
	extern asmlinkage long sys_munmap(unsigned long addr, size_t len);
	unsigned int argv = (unsigned int)regs.regs[5];
	unsigned int envp = (unsigned int)regs.regs[6];
	char **av, **ae;
	int na, ne, r, len;
	char * filename;

	na = nargs(argv, NULL);
	if (IS_ERR(na))
		return(na);
	ne = nargs(envp, NULL);
	if (IS_ERR(ne))
		return(ne);
	len = (na + ne + 2) * sizeof(*av);
	/*
	 *  kmalloc won't work because the `sys_exec' code will attempt
	 *  to do a `get_user' on the arg list and `get_user' will fail
	 *  on a kernel address (simplifies `get_user').  Instead we
	 *  do an mmap to get a user address.  Note that since a successful
	 *  `execve' frees all current memory we only have to do an
	 *  `munmap' if the `execve' failes.
	 */
	down(&current->mm->mmap_sem);
	av = (char **) do_mmap_pgoff(0, 0, len, PROT_READ | PROT_WRITE,
				     MAP_PRIVATE | MAP_ANONYMOUS, 0);
	up(&current->mm->mmap_sem);

	if (IS_ERR(av))
		return (long) av;
	ae = av + na + 1;
	if (IS_ERR(r = __put_user(0, (av + na))))
		goto out;
	if (IS_ERR(r = __put_user(0, (ae + ne))))
		goto out;
	if (IS_ERR(r = nargs(argv, av)))
		goto out;
	if (IS_ERR(r = nargs(envp, ae)))
		goto out;
	filename = getname((char *) (long)regs.regs[4]);
	r = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	r = do_execve(filename, av, ae, &regs);
	putname(filename);
	if (IS_ERR(r))
out:
		sys_munmap((unsigned long)av, len);
	return(r);
}
#endif

struct dirent32 {
	unsigned int	d_ino;
	unsigned int	d_off;
	unsigned short	d_reclen;
	char		d_name[NAME_MAX + 1];
};

static void
xlate_dirent(void *dirent64, void *dirent32, long n)
{
	long off;
	struct dirent *dirp;
	struct dirent32 *dirp32;

	off = 0;
	while (off < n) {
		dirp = (struct dirent *)(dirent64 + off);
		dirp32 = (struct dirent32 *)(dirent32 + off);
		off += dirp->d_reclen;
		dirp32->d_ino = dirp->d_ino;
		dirp32->d_off = (unsigned int)dirp->d_off;
		dirp32->d_reclen = dirp->d_reclen;
		strncpy(dirp32->d_name, dirp->d_name, dirp->d_reclen - ((3 * 4) + 2));
	}
	return;
}

asmlinkage long sys_getdents(unsigned int fd, void * dirent, unsigned int count);

asmlinkage long
sys32_getdents(unsigned int fd, void * dirent32, unsigned int count)
{
	long n;
	void *dirent64;

	dirent64 = (void *)((unsigned long)(dirent32 + (sizeof(long) - 1)) & ~(sizeof(long) - 1));
	if ((n = sys_getdents(fd, dirent64, count - (dirent64 - dirent32))) < 0)
		return(n);
	xlate_dirent(dirent64, dirent32, n);
	return(n);
}

asmlinkage int old_readdir(unsigned int fd, void * dirent, unsigned int count);

asmlinkage int
sys32_readdir(unsigned int fd, void * dirent32, unsigned int count)
{
	int n;
	struct dirent dirent64;

	if ((n = old_readdir(fd, &dirent64, count)) < 0)
		return(n);
	xlate_dirent(&dirent64, dirent32, dirent64.d_reclen);
	return(n);
}

struct timeval32
{
    int tv_sec, tv_usec;
};

struct itimerval32
{
    struct timeval32 it_interval;
    struct timeval32 it_value;
};

struct rusage32 {
        struct timeval32 ru_utime;
        struct timeval32 ru_stime;
        int    ru_maxrss;
        int    ru_ixrss;
        int    ru_idrss;
        int    ru_isrss;
        int    ru_minflt;
        int    ru_majflt;
        int    ru_nswap;
        int    ru_inblock;
        int    ru_oublock;
        int    ru_msgsnd; 
        int    ru_msgrcv; 
        int    ru_nsignals;
        int    ru_nvcsw;
        int    ru_nivcsw;
};

static int
put_rusage (struct rusage32 *ru, struct rusage *r)
{
	int err;
	
	err = put_user (r->ru_utime.tv_sec, &ru->ru_utime.tv_sec);
	err |= __put_user (r->ru_utime.tv_usec, &ru->ru_utime.tv_usec);
	err |= __put_user (r->ru_stime.tv_sec, &ru->ru_stime.tv_sec);
	err |= __put_user (r->ru_stime.tv_usec, &ru->ru_stime.tv_usec);
	err |= __put_user (r->ru_maxrss, &ru->ru_maxrss);
	err |= __put_user (r->ru_ixrss, &ru->ru_ixrss);
	err |= __put_user (r->ru_idrss, &ru->ru_idrss);
	err |= __put_user (r->ru_isrss, &ru->ru_isrss);
	err |= __put_user (r->ru_minflt, &ru->ru_minflt);
	err |= __put_user (r->ru_majflt, &ru->ru_majflt);
	err |= __put_user (r->ru_nswap, &ru->ru_nswap);
	err |= __put_user (r->ru_inblock, &ru->ru_inblock);
	err |= __put_user (r->ru_oublock, &ru->ru_oublock);
	err |= __put_user (r->ru_msgsnd, &ru->ru_msgsnd);
	err |= __put_user (r->ru_msgrcv, &ru->ru_msgrcv);
	err |= __put_user (r->ru_nsignals, &ru->ru_nsignals);
	err |= __put_user (r->ru_nvcsw, &ru->ru_nvcsw);
	err |= __put_user (r->ru_nivcsw, &ru->ru_nivcsw);
	return err;
}

extern asmlinkage int sys_wait4(pid_t pid, unsigned int * stat_addr,
				int options, struct rusage * ru);

asmlinkage int
sys32_wait4(__kernel_pid_t32 pid, unsigned int * stat_addr, int options,
	    struct rusage32 * ru)
{
	if (!ru)
		return sys_wait4(pid, stat_addr, options, NULL);
	else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();
	
		set_fs(KERNEL_DS);	
		ret = sys_wait4(pid, stat_addr ? &status : NULL, options, &r);
		set_fs(old_fs);
		if (put_rusage (ru, &r)) return -EFAULT;
		if (stat_addr && put_user (status, stat_addr))
			return -EFAULT;
		return ret;
	}
}

asmlinkage int
sys32_waitpid(__kernel_pid_t32 pid, unsigned int *stat_addr, int options)
{
	return sys32_wait4(pid, stat_addr, options, NULL);
}

#define RLIM_INFINITY32	0x7fffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	int	rlim_cur;
	int	rlim_max;
};

extern asmlinkage int sys_old_getrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage int
sys32_getrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_old_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret) {
		ret = put_user (RESOURCE32(r.rlim_cur), &rlim->rlim_cur);
		ret |= __put_user (RESOURCE32(r.rlim_max), &rlim->rlim_max);
	}
	return ret;
}

extern asmlinkage int sys_setrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage int
sys32_setrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) return -EINVAL;	
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	if (r.rlim_cur == RLIM_INFINITY32)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max == RLIM_INFINITY32)
		r.rlim_max = RLIM_INFINITY;
	set_fs (KERNEL_DS);
	ret = sys_setrlimit(resource, &r);
	set_fs (old_fs);
	return ret;
}

struct statfs32 {
	int	f_type;
	int	f_bsize;
	int	f_frsize;
	int	f_blocks;
	int	f_bfree;
	int	f_files;
	int	f_ffree;
	int	f_bavail;
	__kernel_fsid_t32	f_fsid;
	int	f_namelen;
	int	f_spare[6];
};

static inline int
put_statfs (struct statfs32 *ubuf, struct statfs *kbuf)
{
	int err;
	
	err = put_user (kbuf->f_type, &ubuf->f_type);
	err |= __put_user (kbuf->f_bsize, &ubuf->f_bsize);
	err |= __put_user (kbuf->f_blocks, &ubuf->f_blocks);
	err |= __put_user (kbuf->f_bfree, &ubuf->f_bfree);
	err |= __put_user (kbuf->f_bavail, &ubuf->f_bavail);
	err |= __put_user (kbuf->f_files, &ubuf->f_files);
	err |= __put_user (kbuf->f_ffree, &ubuf->f_ffree);
	err |= __put_user (kbuf->f_namelen, &ubuf->f_namelen);
	err |= __put_user (kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]);
	err |= __put_user (kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]);
	return err;
}

extern asmlinkage int sys_statfs(const char * path, struct statfs * buf);

asmlinkage int
sys32_statfs(const char * path, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_statfs((const char *)path, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_fstatfs(unsigned int fd, struct statfs * buf);

asmlinkage int
sys32_fstatfs(unsigned int fd, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_fstatfs(fd, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage int
sys_getrusage(int who, struct rusage *ru);

asmlinkage int
sys32_getrusage(int who, struct rusage32 *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getrusage(who, &r);
	set_fs (old_fs);
	if (put_rusage (ru, &r)) return -EFAULT;
	return ret;
}

static inline long
get_tv32(struct timeval *o, struct timeval32 *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
	return ENOSYS;
}

static inline long
get_it32(struct itimerval *o, struct itimerval32 *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
	return ENOSYS;
}

static inline long
put_tv32(struct timeval32 *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

static inline long
put_it32(struct itimerval32 *o, struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, i, sizeof(*i)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
	return ENOSYS;
}

extern int do_getitimer(int which, struct itimerval *value);

asmlinkage int
sys32_getitimer(int which, struct itimerval32 *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_it32(it, &kit))
		error = -EFAULT;

	return error;
}

extern int do_setitimer(int which, struct itimerval *, struct itimerval *);


asmlinkage int
sys32_setitimer(int which, struct itimerval32 *in, struct itimerval32 *out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_it32(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_it32(out, &kout))
		return -EFAULT;

	return 0;

}
asmlinkage unsigned long 
sys32_alarm(unsigned int seconds)
{
	struct itimerval it_new, it_old;
	unsigned int oldalarm;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	do_setitimer(ITIMER_REAL, &it_new, &it_old);
	oldalarm = it_old.it_value.tv_sec;
	/* ehhh.. We can't return 0 if we have an alarm pending.. */
	/* And we'd better return too much than too little anyway */
	if (it_old.it_value.tv_usec)
		oldalarm++;
	return oldalarm;
}

/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */


extern struct timezone sys_tz;
extern int do_sys_settimeofday(struct timeval *tv, struct timezone *tz);

asmlinkage int
sys32_gettimeofday(struct timeval32 *tv, struct timezone *tz)
{
	if (tv) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (put_tv32(tv, &ktv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage int
sys32_settimeofday(struct timeval32 *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timezone ktz;

 	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &ktv : NULL, tz ? &ktz : NULL);
}

extern asmlinkage long sys_llseek(unsigned int fd, unsigned long offset_high,
			          unsigned long offset_low, loff_t * result,
			          unsigned int origin);

extern asmlinkage int sys32_llseek(unsigned int fd, unsigned int offset_high,
			           unsigned int offset_low, loff_t * result,
			           unsigned int origin)
{
	return sys_llseek(fd, offset_high, offset_low, result, origin);
}

struct iovec32 { unsigned int iov_base; int iov_len; };

typedef ssize_t (*IO_fn_t)(struct file *, char *, size_t, loff_t *);

static long
do_readv_writev32(int type, struct file *file, const struct iovec32 *vector,
		  u32 count)
{
	unsigned long tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	IO_fn_t fn;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (!count)
		return 0;
	if(verify_area(VERIFY_READ, vector, sizeof(struct iovec32)*count))
		return -EFAULT;
	if (count > UIO_MAXIOV)
		return -EINVAL;
	if (count > UIO_FASTIOV) {
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return -ENOMEM;
	}

	tot_len = 0;
	i = count;
	ivp = iov;
	while (i > 0) {
		u32 len;
		u32 buf;

		__get_user(len, &vector->iov_len);
		__get_user(buf, &vector->iov_base);
		tot_len += len;
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t) len;
		vector++;
		ivp++;
		i--;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	retval = locks_verify_area((type == VERIFY_WRITE
				    ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				   inode, file, file->f_pos, tot_len);
	if (retval) {
		if (iov != iovstack)
			kfree(iov);
		return retval;
	}

	/* Then do the actual IO.  Note that sockets need to be handled
	 * specially as they have atomicity guarantees and can handle
	 * iovec's natively
	 */
	if (inode->i_sock) {
		int err;
		err = sock_readv_writev(type, inode, file, iov, count, tot_len);
		if (iov != iovstack)
			kfree(iov);
		return err;
	}

	if (!file->f_op) {
		if (iov != iovstack)
			kfree(iov);
		return -EINVAL;
	}
	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = file->f_op->read;
	if (type == VERIFY_READ)
		fn = (IO_fn_t) file->f_op->write;		
	ivp = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = ivp->iov_base;
		len = ivp->iov_len;
		ivp++;
		count--;
		nr = fn(file, base, len, &file->f_pos);
		if (nr < 0) {
			if (retval)
				break;
			retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
	if (iov != iovstack)
		kfree(iov);

	return retval;
}

asmlinkage long
sys32_readv(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	ssize_t ret;

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_READ) &&
	    (file->f_op->readv || file->f_op->read))
		ret = do_readv_writev32(VERIFY_WRITE, file, vector, count);

	fput(file);

bad_file:
	return ret;
}

asmlinkage long
sys32_writev(int fd, struct iovec32 *vector, u32 count)
{
	struct file *file;
	ssize_t ret;

	ret = -EBADF;
	file = fget(fd);
	if(!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_WRITE) &&
	    (file->f_op->writev || file->f_op->write))
	        ret = do_readv_writev32(VERIFY_READ, file, vector, count);
	fput(file);

bad_file:
	return ret;
}

/* From the Single Unix Spec: pread & pwrite act like lseek to pos + op +
   lseek back to original location.  They fail just like lseek does on
   non-seekable files.  */

asmlinkage ssize_t sys32_pread(unsigned int fd, char * buf,
			       size_t count, u32 unused, loff_t pos)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_READ))
		goto out;
	ret = locks_verify_area(FLOCK_VERIFY_READ, file->f_dentry->d_inode,
				file, pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(read = file->f_op->read))
		goto out;
	if (pos < 0)
		goto out;
	ret = read(file, buf, count, &pos);
	if (ret > 0)
		inode_dir_notify(file->f_dentry->d_parent->d_inode, DN_ACCESS);
out:
	fput(file);
bad_file:
	return ret;
}

asmlinkage ssize_t sys32_pwrite(unsigned int fd, const char * buf,
			        size_t count, u32 unused, loff_t pos)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_WRITE))
		goto out;
	ret = locks_verify_area(FLOCK_VERIFY_WRITE, file->f_dentry->d_inode,
				file, pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(write = file->f_op->write))
		goto out;
	if (pos < 0)
		goto out;

	ret = write(file, buf, count, &pos);
	if (ret > 0)
		inode_dir_notify(file->f_dentry->d_parent->d_inode, DN_MODIFY);
out:
	fput(file);
bad_file:
	return ret;
}
/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */

static inline int
get_fd_set32(unsigned long n, unsigned long *fdset, u32 *ufdset)
{
#ifdef __MIPSEB__
	if (ufdset) {
		unsigned long odd;

		if (verify_area(VERIFY_WRITE, ufdset, n*sizeof(u32)))
			return -EFAULT;

		odd = n & 1UL;
		n &= ~1UL;
		while (n) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			n -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((n + 1) & ~1)*sizeof(u32));
	}
	return 0;
#else
	<<Bomb - little endian support must define this>>
#endif
}

static inline void
set_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	unsigned long odd;

	if (!ufdset)
		return;

	odd = n & 1UL;
	n &= ~1UL;
	while (n) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		n -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage int sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, struct timeval32 *tvp)
{
	fd_set_bits fds;
	char *bits;
	unsigned long nn;
	long timeout;
	int ret, size;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;

		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		ret = -EINVAL;
		if(sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = (usec + 1000000/HZ - 1) / (1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;
	if (n > current->files->max_fdset)
		n = current->files->max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = kmalloc(6 * size, GFP_KERNEL);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	nn = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if ((ret = get_fd_set32(nn, fds.in, inp)) ||
	    (ret = get_fd_set32(nn, fds.out, outp)) ||
	    (ret = get_fd_set32(nn, fds.ex, exp)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(nn, inp, fds.res_in);
	set_fd_set32(nn, outp, fds.res_out);
	set_fd_set32(nn, exp, fds.res_ex);

out:
	kfree(bits);
out_nofds:
	return ret;
}



struct timespec32 {
	int 	tv_sec;
	int	tv_nsec;
};

extern asmlinkage int sys_sched_rr_get_interval(pid_t pid,
						struct timespec *interval);

asmlinkage int
sys32_sched_rr_get_interval(__kernel_pid_t32 pid, struct timespec32 *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user (t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}


extern asmlinkage int sys_nanosleep(struct timespec *rqtp,
				    struct timespec *rmtp); 

asmlinkage int
sys32_nanosleep(struct timespec32 *rqtp, struct timespec32 *rmtp)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (get_user (t.tv_sec, &rqtp->tv_sec) ||
	    __get_user (t.tv_nsec, &rqtp->tv_nsec))
		return -EFAULT;
	
	set_fs (KERNEL_DS);
	ret = sys_nanosleep(&t, rmtp ? &t : NULL);
	set_fs (old_fs);
	if (rmtp && ret == -EINTR) {
		if (__put_user (t.tv_sec, &rmtp->tv_sec) ||
	    	    __put_user (t.tv_nsec, &rmtp->tv_nsec))
			return -EFAULT;
	}
	return ret;
}

struct tms32 {
	int tms_utime;
	int tms_stime;
	int tms_cutime;
	int tms_cstime;
};

extern asmlinkage long sys_times(struct tms * tbuf);
asmlinkage long sys32_times(struct tms32 *tbuf)
{
	struct tms t;
	long ret;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	ret = sys_times(tbuf ? &t : NULL);
	set_fs(old_fs);
	if (tbuf) {
		err = put_user (t.tms_utime, &tbuf->tms_utime);
		err |= __put_user (t.tms_stime, &tbuf->tms_stime);
		err |= __put_user (t.tms_cutime, &tbuf->tms_cutime);
		err |= __put_user (t.tms_cstime, &tbuf->tms_cstime);
		if (err)
			ret = -EFAULT;
	}
	return ret;
}

extern asmlinkage int sys_setsockopt(int fd, int level, int optname,
				     char *optval, int optlen);

asmlinkage int sys32_setsockopt(int fd, int level, int optname,
				char *optval, int optlen)
{
	if (optname == SO_ATTACH_FILTER) {
		struct sock_fprog32 {
			__u16 len;
			__u32 filter;
		} *fprog32 = (struct sock_fprog32 *)optval;
		struct sock_fprog kfprog;
		struct sock_filter *kfilter;
		unsigned int fsize;
		mm_segment_t old_fs;
		__u32 uptr;
		int ret;

		if (get_user(kfprog.len, &fprog32->len) ||
		    __get_user(uptr, &fprog32->filter))
			return -EFAULT;
		kfprog.filter = (struct sock_filter *)A(uptr);
		fsize = kfprog.len * sizeof(struct sock_filter);
		kfilter = (struct sock_filter *)kmalloc(fsize, GFP_KERNEL);
		if (kfilter == NULL)
			return -ENOMEM;
		if (copy_from_user(kfilter, kfprog.filter, fsize)) {
			kfree(kfilter);
			return -EFAULT;
		}
		kfprog.filter = kfilter;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_setsockopt(fd, level, optname,
				     (char *)&kfprog, sizeof(kfprog));
		set_fs(old_fs);
		kfree(kfilter);
		return ret;
	}
	return sys_setsockopt(fd, level, optname, optval, optlen);
}

struct flock32 {
	short l_type;
	short l_whence;
	__kernel_off_t32 l_start;
	__kernel_off_t32 l_len;
	__kernel_pid_t32 l_pid;
	short __unused;
};

static inline int get_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = get_user(kfl->l_type, &ufl->l_type);
	err |= __get_user(kfl->l_whence, &ufl->l_whence);
	err |= __get_user(kfl->l_start, &ufl->l_start);
	err |= __get_user(kfl->l_len, &ufl->l_len);
	err |= __get_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

static inline int put_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = __put_user(kfl->l_type, &ufl->l_type);
	err |= __put_user(kfl->l_whence, &ufl->l_whence);
	err |= __put_user(kfl->l_start, &ufl->l_start);
	err |= __put_user(kfl->l_len, &ufl->l_len);
	err |= __put_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

extern asmlinkage long
sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);

asmlinkage long
sys32_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		{
			struct flock f;
			mm_segment_t old_fs;
			long ret;
			
			if (get_flock(&f, (struct flock32 *)arg))
				return -EFAULT;
			old_fs = get_fs(); set_fs (KERNEL_DS);
			ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs (old_fs);
			if (put_flock(&f, (struct flock32 *)arg))
				return -EFAULT;
			return ret;
		}
	default:
		return sys_fcntl(fd, cmd, (unsigned long)arg);
	}
}

asmlinkage long
sys32_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F_GETLK64:
		return sys_fcntl(fd, F_GETLK, arg);
	case F_SETLK64:
		return sys_fcntl(fd, F_SETLK, arg);
	case F_SETLKW64:
		return sys_fcntl(fd, F_SETLKW, arg);
	}

	return sys32_fcntl(fd, cmd, arg);
}

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc_perm32
{
	key_t    	  key;
        __kernel_uid_t32  uid;
        __kernel_gid_t32  gid;
        __kernel_uid_t32  cuid;
        __kernel_gid_t32  cgid;
        __kernel_mode_t32 mode;
        unsigned short  seq;
};

struct semid_ds32 {
        struct ipc_perm32 sem_perm;               /* permissions .. see ipc.h */
        __kernel_time_t32 sem_otime;              /* last semop time */
        __kernel_time_t32 sem_ctime;              /* last change time */
        u32 sem_base;              /* ptr to first semaphore in array */
        u32 sem_pending;          /* pending operations to be processed */
        u32 sem_pending_last;    /* last pending operation */
        u32 undo;                  /* undo requests on this array */
        unsigned short  sem_nsems;              /* no. of semaphores in array */
};

struct msqid_ds32
{
        struct ipc_perm32 msg_perm;
        u32 msg_first;
        u32 msg_last;
        __kernel_time_t32 msg_stime;
        __kernel_time_t32 msg_rtime;
        __kernel_time_t32 msg_ctime;
        u32 wwait;
        u32 rwait;
        unsigned short msg_cbytes;
        unsigned short msg_qnum;  
        unsigned short msg_qbytes;
        __kernel_ipc_pid_t32 msg_lspid;
        __kernel_ipc_pid_t32 msg_lrpid;
};

struct shmid_ds32 {
        struct ipc_perm32       shm_perm;
        int                     shm_segsz;
        __kernel_time_t32       shm_atime;
        __kernel_time_t32       shm_dtime;
        __kernel_time_t32       shm_ctime;
        __kernel_ipc_pid_t32    shm_cpid; 
        __kernel_ipc_pid_t32    shm_lpid; 
        unsigned short          shm_nattch;
};

#define IPCOP_MASK(__x)	(1UL << (__x))

static int
do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	struct semid64_ds s;
	struct semid_ds32 *usp;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user (pad, (u32 *)uptr))
		return err;
	if(third == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third) {

	case IPC_INFO:
	case IPC_RMID:
	case IPC_SET:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETVAL:
	case SETALL:
		err = sys_semctl (first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		usp = (struct semid_ds32 *)A(pad);
		fourth.__pad = &s;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_semctl (first, second, third, fourth);
		set_fs (old_fs);
		err2 = put_user(s.sem_perm.key, &usp->sem_perm.key);
		err2 |= __put_user(s.sem_perm.uid, &usp->sem_perm.uid);
		err2 |= __put_user(s.sem_perm.gid, &usp->sem_perm.gid);
		err2 |= __put_user(s.sem_perm.cuid,
				   &usp->sem_perm.cuid);
		err2 |= __put_user (s.sem_perm.cgid,
				    &usp->sem_perm.cgid);
		err2 |= __put_user (s.sem_perm.mode,
				    &usp->sem_perm.mode);
		err2 |= __put_user (s.sem_perm.seq, &usp->sem_perm.seq);
		err2 |= __put_user (s.sem_otime, &usp->sem_otime);
		err2 |= __put_user (s.sem_ctime, &usp->sem_ctime);
		err2 |= __put_user (s.sem_nsems, &usp->sem_nsems);
		if (err2)
			err = -EFAULT;
		break;

	}

	return err;
}

static int
do_sys32_msgsnd (int first, int second, int third, void *uptr)
{
	struct msgbuf *p = kmalloc (second + sizeof (struct msgbuf)
				    + 4, GFP_USER);
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (!p)
		return -ENOMEM;
	err = get_user (p->mtype, &up->mtype);
	err |= __copy_from_user (p->mtext, &up->mtext, second);
	if (err)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgsnd (first, p, second, third);
	set_fs (old_fs);
out:
	kfree (p);
	return err;
}

static int
do_sys32_msgrcv (int first, int second, int msgtyp, int third,
		 int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (!version) {
		struct ipc_kludge *uipck = (struct ipc_kludge *)uptr;
		struct ipc_kludge ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user (&ipck, uipck, sizeof (struct ipc_kludge)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc (second + sizeof (struct msgbuf) + 4, GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgrcv (first, p, second + 4, msgtyp, third);
	set_fs (old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user (p->mtype, &up->mtype) ||
	    __copy_to_user (&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree (p);
out:
	return err;
}

static int
do_sys32_msgctl (int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	struct msqid_ds m;
	struct msqid64_ds m64;
	struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;
	mm_segment_t old_fs;

	switch (second) {

	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl (first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		err = get_user (m.msg_perm.uid, &up->msg_perm.uid);
		err |= __get_user (m.msg_perm.gid, &up->msg_perm.gid);
		err |= __get_user (m.msg_perm.mode, &up->msg_perm.mode);
		err |= __get_user (m.msg_qbytes, &up->msg_qbytes);
		if (err)
			break;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, &m);
		set_fs (old_fs);
		break;

	case IPC_STAT:
	case MSG_STAT:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, (void *) &m64);
		set_fs (old_fs);
		err2 = put_user (m64.msg_perm.key, &up->msg_perm.key);
		err2 |= __put_user(m64.msg_perm.uid, &up->msg_perm.uid);
		err2 |= __put_user(m64.msg_perm.gid, &up->msg_perm.gid);
		err2 |= __put_user(m64.msg_perm.cuid, &up->msg_perm.cuid);
		err2 |= __put_user(m64.msg_perm.cgid, &up->msg_perm.cgid);
		err2 |= __put_user(m64.msg_perm.mode, &up->msg_perm.mode);
		err2 |= __put_user(m64.msg_perm.seq, &up->msg_perm.seq);
		err2 |= __put_user(m64.msg_stime, &up->msg_stime);
		err2 |= __put_user(m64.msg_rtime, &up->msg_rtime);
		err2 |= __put_user(m64.msg_ctime, &up->msg_ctime);
		err2 |= __put_user(m64.msg_cbytes, &up->msg_cbytes);
		err2 |= __put_user(m64.msg_qnum, &up->msg_qnum);
		err2 |= __put_user(m64.msg_qbytes, &up->msg_qbytes);
		err2 |= __put_user(m64.msg_lspid, &up->msg_lspid);
		err2 |= __put_user(m64.msg_lrpid, &up->msg_lrpid);
		if (err2)
			err = -EFAULT;
		break;

	}

	return err;
}

static int
do_sys32_shmat (int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	if (version == 1)
		return err;
	err = sys_shmat (first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user (raddr, uaddr);
	return err;
}

static int
do_sys32_shmctl (int first, int second, void *uptr)
{
	int err = -EFAULT, err2;
	struct shmid_ds s;
	struct shmid64_ds s64;
	struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
	mm_segment_t old_fs;
	struct shm_info32 {
		int used_ids;
		u32 shm_tot, shm_rss, shm_swp;
		u32 swap_attempts, swap_successes;
	} *uip = (struct shm_info32 *)uptr;
	struct shm_info si;

	switch (second) {

	case IPC_INFO:
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl (first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		err = get_user (s.shm_perm.uid, &up->shm_perm.uid);
		err |= __get_user (s.shm_perm.gid, &up->shm_perm.gid);
		err |= __get_user (s.shm_perm.mode, &up->shm_perm.mode);
		if (err)
			break;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, &s);
		set_fs (old_fs);
		break;

	case IPC_STAT:
	case SHM_STAT:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, (void *) &s64);
		set_fs (old_fs);
		if (err < 0)
			break;
		err2 = put_user (s64.shm_perm.key, &up->shm_perm.key);
		err2 |= __put_user (s64.shm_perm.uid, &up->shm_perm.uid);
		err2 |= __put_user (s64.shm_perm.gid, &up->shm_perm.gid);
		err2 |= __put_user (s64.shm_perm.cuid,
				    &up->shm_perm.cuid);
		err2 |= __put_user (s64.shm_perm.cgid,
				    &up->shm_perm.cgid);
		err2 |= __put_user (s64.shm_perm.mode,
				    &up->shm_perm.mode);
		err2 |= __put_user (s64.shm_perm.seq, &up->shm_perm.seq);
		err2 |= __put_user (s64.shm_atime, &up->shm_atime);
		err2 |= __put_user (s64.shm_dtime, &up->shm_dtime);
		err2 |= __put_user (s64.shm_ctime, &up->shm_ctime);
		err2 |= __put_user (s64.shm_segsz, &up->shm_segsz);
		err2 |= __put_user (s64.shm_nattch, &up->shm_nattch);
		err2 |= __put_user (s64.shm_cpid, &up->shm_cpid);
		err2 |= __put_user (s64.shm_lpid, &up->shm_lpid);
		if (err2)
			err = -EFAULT;
		break;

	case SHM_INFO:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, (void *)&si);
		set_fs (old_fs);
		if (err < 0)
			break;
		err2 = put_user (si.used_ids, &uip->used_ids);
		err2 |= __put_user (si.shm_tot, &uip->shm_tot);
		err2 |= __put_user (si.shm_rss, &uip->shm_rss);
		err2 |= __put_user (si.shm_swp, &uip->shm_swp);
		err2 |= __put_user (si.swap_attempts,
				    &uip->swap_attempts);
		err2 |= __put_user (si.swap_successes,
				    &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;

	}
	return err;
}

asmlinkage long
sys32_ipc (u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semop (first, (struct sembuf *)A(ptr),
				 second);
		break;
	case SEMGET:
		err = sys_semget (first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl (first, second, third,
				       (void *)A(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd (first, second, third,
				       (void *)A(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv (first, second, fifth, third,
				       version, (void *)A(ptr));
		break;
	case MSGGET:
		err = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl (first, second, (void *)A(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat (first, second, third,
				      version, (void *)A(ptr));
		break;
	case SHMDT: 
		err = sys_shmdt ((char *)A(ptr));
		break;
	case SHMGET:
		err = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl (first, second, (void *)A(ptr));
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

struct sysctl_args32
{
	__kernel_caddr_t32 name;
	int nlen;
	__kernel_caddr_t32 oldval;
	__kernel_caddr_t32 oldlenp;
	__kernel_caddr_t32 newval;
	__kernel_size_t32 newlen;
	unsigned int __unused[4];
};

asmlinkage long sys32_sysctl(struct sysctl_args32 *uargs32)
{
	struct __sysctl_args kargs;
	struct sysctl_args32 kargs32;
	mm_segment_t old_fs;
	int name[CTL_MAXNAME];
	size_t oldlen[1];
	int err, ret;

	ret = -EFAULT;

	memset(&kargs, 0, sizeof (kargs));
	
	err = get_user(kargs32.name, &uargs32->name);
	err |= __get_user(kargs32.nlen, &uargs32->nlen);
	err |= __get_user(kargs32.oldval, &uargs32->oldval);
	err |= __get_user(kargs32.oldlenp, &uargs32->oldlenp);
	err |= __get_user(kargs32.newval, &uargs32->newval);
	err |= __get_user(kargs32.newlen, &uargs32->newlen);
	if (err)
		goto out;

	if (kargs32.nlen == 0 || kargs32.nlen >= CTL_MAXNAME) {
		ret = -ENOTDIR;
		goto out;
	}

	kargs.name = name;
	kargs.nlen = kargs32.nlen;
	if (copy_from_user(kargs.name, (int *)A(kargs32.name),
			   kargs32.nlen * sizeof(name) / sizeof(name[0])))
		goto out;

	if (kargs32.oldval) {
		if (!kargs32.oldlenp || get_user(oldlen[0],
						 (int *)A(kargs32.oldlenp)))
			return -EFAULT;
		kargs.oldlenp = oldlen;
		kargs.oldval = kmalloc(oldlen[0], GFP_KERNEL);
		if (!kargs.oldval) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (kargs32.newval && kargs32.newlen) {
		kargs.newval = kmalloc(kargs32.newlen, GFP_KERNEL);
		if (!kargs.newval) {
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(kargs.newval, (int *)A(kargs32.newval),
				   kargs32.newlen))
			goto out;
	}

	old_fs = get_fs(); set_fs (KERNEL_DS);
	ret = sys_sysctl(&kargs);
	set_fs (old_fs);

	if (ret)
		goto out;

	if (kargs.oldval) {
		if (put_user(oldlen[0], (int *)A(kargs32.oldlenp)) ||
		    copy_to_user((int *)A(kargs32.oldval), kargs.oldval,
				 oldlen[0]))
			ret = -EFAULT;
	}
out:
	if (kargs.oldval)
		kfree(kargs.oldval);
	if (kargs.newval)
		kfree(kargs.newval);
	return ret; 
}

asmlinkage long sys32_newuname(struct new_utsname * name)
{
	int ret = 0;

	down_read(&uts_sem);
	if (copy_to_user(name,&system_utsname,sizeof *name))
		ret = -EFAULT;
	up_read(&uts_sem);

	if (current->personality == PER_LINUX32 && !ret)
		if (copy_to_user(name->machine, "mips\0\0\0", 8))
			ret = -EFAULT;

	return ret;
}

extern asmlinkage long sys_personality(unsigned long);

asmlinkage int sys32_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

/* Handle adjtimex compatability. */

struct timex32 {
	u32 modes;
	s32 offset, freq, maxerror, esterror;
	s32 status, constant, precision, tolerance;
	struct timeval32 time;
	s32 tick;
	s32 ppsfreq, jitter, shift, stabil;
	s32 jitcnt, calcnt, errcnt, stbcnt;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
};

extern int do_adjtimex(struct timex *);

asmlinkage int sys32_adjtimex(struct timex32 *utp)
{
	struct timex txc;
	int ret;

	memset(&txc, 0, sizeof(struct timex));

	if(get_user(txc.modes, &utp->modes) ||
	   __get_user(txc.offset, &utp->offset) ||
	   __get_user(txc.freq, &utp->freq) ||
	   __get_user(txc.maxerror, &utp->maxerror) ||
	   __get_user(txc.esterror, &utp->esterror) ||
	   __get_user(txc.status, &utp->status) ||
	   __get_user(txc.constant, &utp->constant) ||
	   __get_user(txc.precision, &utp->precision) ||
	   __get_user(txc.tolerance, &utp->tolerance) ||
	   __get_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __get_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __get_user(txc.tick, &utp->tick) ||
	   __get_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __get_user(txc.jitter, &utp->jitter) ||
	   __get_user(txc.shift, &utp->shift) ||
	   __get_user(txc.stabil, &utp->stabil) ||
	   __get_user(txc.jitcnt, &utp->jitcnt) ||
	   __get_user(txc.calcnt, &utp->calcnt) ||
	   __get_user(txc.errcnt, &utp->errcnt) ||
	   __get_user(txc.stbcnt, &utp->stbcnt))
		return -EFAULT;

	ret = do_adjtimex(&txc);

	if(put_user(txc.modes, &utp->modes) ||
	   __put_user(txc.offset, &utp->offset) ||
	   __put_user(txc.freq, &utp->freq) ||
	   __put_user(txc.maxerror, &utp->maxerror) ||
	   __put_user(txc.esterror, &utp->esterror) ||
	   __put_user(txc.status, &utp->status) ||
	   __put_user(txc.constant, &utp->constant) ||
	   __put_user(txc.precision, &utp->precision) ||
	   __put_user(txc.tolerance, &utp->tolerance) ||
	   __put_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __put_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __put_user(txc.tick, &utp->tick) ||
	   __put_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __put_user(txc.jitter, &utp->jitter) ||
	   __put_user(txc.shift, &utp->shift) ||
	   __put_user(txc.stabil, &utp->stabil) ||
	   __put_user(txc.jitcnt, &utp->jitcnt) ||
	   __put_user(txc.calcnt, &utp->calcnt) ||
	   __put_user(txc.errcnt, &utp->errcnt) ||
	   __put_user(txc.stbcnt, &utp->stbcnt))
		ret = -EFAULT;

	return ret;
}

/*
 *  Declare the 32-bit version of the msghdr
 */
 
struct msghdr32 {
	unsigned int    msg_name;	/* Socket name			*/
	int		msg_namelen;	/* Length of name		*/
	unsigned int    msg_iov;	/* Data blocks			*/
	unsigned int	msg_iovlen;	/* Number of blocks		*/
	unsigned int    msg_control;	/* Per protocol magic (eg BSD file descriptor passing) */
	unsigned int	msg_controllen;	/* Length of cmsg list */
	unsigned	msg_flags;
};

static inline int
shape_msg(struct msghdr *mp, struct msghdr32 *mp32)
{
	int ret;
	unsigned int i;

	if (!access_ok(VERIFY_READ, mp32, sizeof(*mp32)))
		return(-EFAULT);
	ret = __get_user(i, &mp32->msg_name);
	mp->msg_name = (void *)A(i);
	ret |= __get_user(mp->msg_namelen, &mp32->msg_namelen);
	ret |= __get_user(i, &mp32->msg_iov);
	mp->msg_iov = (struct iovec *)A(i);
	ret |= __get_user(mp->msg_iovlen, &mp32->msg_iovlen);
	ret |= __get_user(i, &mp32->msg_control);
	mp->msg_control = (void *)A(i);
	ret |= __get_user(mp->msg_controllen, &mp32->msg_controllen);
	ret |= __get_user(mp->msg_flags, &mp32->msg_flags);
	return(ret ? -EFAULT : 0);
}

/*
 *	Verify & re-shape IA32 iovec. The caller must ensure that the
 *      iovec is big enough to hold the re-shaped message iovec.
 *
 *	Save time not doing verify_area. copy_*_user will make this work
 *	in any case.
 *
 *	Don't need to check the total size for overflow (cf net/core/iovec.c),
 *	32-bit sizes can't overflow a 64-bit count.
 */

static inline int
verify_iovec32(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int size, err, ct;
	struct iovec32 *iov32;
	
	if(m->msg_namelen)
	{
		if(mode==VERIFY_READ)
		{
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
			if(err<0)
				goto out;
		}
		
		m->msg_name = address;
	} else
		m->msg_name = NULL;

	err = -EFAULT;
	size = m->msg_iovlen * sizeof(struct iovec32);
	if (copy_from_user(iov, m->msg_iov, size))
		goto out;
	m->msg_iov=iov;

	err = 0;
	iov32 = (struct iovec32 *)iov;
	for (ct = m->msg_iovlen; ct-- > 0; ) {
		iov[ct].iov_len = (__kernel_size_t)iov32[ct].iov_len;
		iov[ct].iov_base = (void *) A(iov32[ct].iov_base);
		err += iov[ct].iov_len;
	}
out:
	return err;
}

extern __inline__ void
sockfd_put(struct socket *sock)
{
	fput(sock->file);
}

/* XXX This really belongs in some header file... -DaveM */
#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */

extern struct socket *sockfd_lookup(int fd, int *err);

/*
 *	BSD sendmsg interface
 */

int sys32_sendmsg(int fd, struct msghdr32 *msg, unsigned flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	unsigned char ctl[sizeof(struct cmsghdr) + 20];	/* 20 is size of ipv6_pktinfo */
	unsigned char *ctl_buf = ctl;
	struct msghdr msg_sys;
	int err, ctl_len, iov_size, total_len;
	
	err = -EFAULT;
	if (shape_msg(&msg_sys, msg))
		goto out; 

	sock = sockfd_lookup(fd, &err);
	if (!sock) 
		goto out;

	/* do not move before msg_sys is valid */
	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;

	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec32);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/* This will also move the address data into kernel space */
	err = verify_iovec32(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0) 
		goto out_freeiov;
	total_len = err;

	err = -ENOBUFS;

	if (msg_sys.msg_controllen > INT_MAX)
		goto out_freeiov;
	ctl_len = msg_sys.msg_controllen; 
	if (ctl_len) 
	{
		if (ctl_len > sizeof(ctl))
		{
			err = -ENOBUFS;
			ctl_buf = sock_kmalloc(sock->sk, ctl_len, GFP_KERNEL);
			if (ctl_buf == NULL) 
				goto out_freeiov;
		}
		err = -EFAULT;
		if (copy_from_user(ctl_buf, msg_sys.msg_control, ctl_len))
			goto out_freectl;
		msg_sys.msg_control = ctl_buf;
	}
	msg_sys.msg_flags = flags;

	if (sock->file->f_flags & O_NONBLOCK)
		msg_sys.msg_flags |= MSG_DONTWAIT;
	err = sock_sendmsg(sock, &msg_sys, total_len);

out_freectl:
	if (ctl_buf != ctl)    
		sock_kfree_s(sock->sk, ctl_buf, ctl_len);
out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:       
	return err;
}

/*
 *	BSD recvmsg interface
 */

int
sys32_recvmsg (int fd, struct msghdr32 *msg, unsigned int flags)
{
	struct socket *sock;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	struct msghdr msg_sys;
	unsigned long cmsg_ptr;
	int err, iov_size, total_len, len;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];

	/* user mode address pointers */
	struct sockaddr *uaddr;
	int *uaddr_len;
	
	err=-EFAULT;
	if (shape_msg(&msg_sys, msg))
		goto out;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;
	
	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/*
	 *	Save the user-mode address (verify_iovec will change the
	 *	kernel msghdr to use the kernel address space)
	 */
	 
	uaddr = msg_sys.msg_name;
	uaddr_len = &msg->msg_namelen;
	err = verify_iovec32(&msg_sys, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out_freeiov;
	total_len=err;

	cmsg_ptr = (unsigned long)msg_sys.msg_control;
	msg_sys.msg_flags = 0;
	
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg(sock, &msg_sys, total_len, flags);
	if (err < 0)
		goto out_freeiov;
	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user(addr, msg_sys.msg_namelen, uaddr, uaddr_len);
		if (err < 0)
			goto out_freeiov;
	}
	err = __put_user(msg_sys.msg_flags, &msg->msg_flags);
	if (err)
		goto out_freeiov;
	err = __put_user((unsigned long)msg_sys.msg_control-cmsg_ptr, 
							 &msg->msg_controllen);
	if (err)
		goto out_freeiov;
	err = len;

out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:
	return err;
}
