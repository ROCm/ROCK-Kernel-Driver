/*
 * linux/arch/parisc/kernel/sys_hpux.c
 *
 * implements HPUX syscalls.
 */

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/utsname.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

unsigned long sys_brk(unsigned long addr);
 
unsigned long hpux_brk(unsigned long addr)
{
	/* Sigh.  Looks like HP/UX libc relies on kernel bugs. */
	return sys_brk(addr + PAGE_SIZE);
}

int hpux_sbrk(void)
{
	return -ENOSYS;
}

/* Random other syscalls */

int hpux_nice(int priority_change)
{
	return -ENOSYS;
}

int hpux_ptrace(void)
{
	return -ENOSYS;
}

int hpux_wait(int *stat_loc)
{
	extern int sys_waitpid(int, int *, int);
	return sys_waitpid(-1, stat_loc, 0);
}

#define _SC_CPU_VERSION	10001
#define _SC_OPEN_MAX	4
#define CPU_PA_RISC1_1	0x210

int hpux_sysconf(int which)
{
	switch (which) {
	case _SC_CPU_VERSION:
		return CPU_PA_RISC1_1;
	case _SC_OPEN_MAX:
		return INT_MAX;
	default:
		return -EINVAL;
	}
}

/*****************************************************************************/

#define HPUX_UTSLEN 9
#define HPUX_SNLEN 15

struct hpux_utsname {
	char sysname[HPUX_UTSLEN];
	char nodename[HPUX_UTSLEN];
	char release[HPUX_UTSLEN];
	char version[HPUX_UTSLEN];
	char machine[HPUX_UTSLEN];
	char idnumber[HPUX_SNLEN];
} ;

struct hpux_ustat {
	int32_t		f_tfree;	/* total free (daddr_t)  */
	u_int32_t	f_tinode;	/* total inodes free (ino_t)  */
	char		f_fname[6];	/* filsys name */
	char		f_fpack[6];	/* filsys pack name */
	u_int32_t	f_blksize;	/* filsys block size (int) */
};

/*
 * HPUX's utssys() call.  It's a collection of miscellaneous functions,
 * alas, so there's no nice way of splitting them up.
 */

/*  This function is called from hpux_utssys(); HP-UX implements
 *  ustat() as an option to utssys().
 *
 *  Now, struct ustat on HP-UX is exactly the same as on Linux, except
 *  that it contains one addition field on the end, int32_t f_blksize.
 *  So, we could have written this function to just call the Linux
 *  sys_ustat(), (defined in linux/fs/super.c), and then just
 *  added this additional field to the user's structure.  But I figure
 *  if we're gonna be digging through filesystem structures to get
 *  this, we might as well just do the whole enchilada all in one go.
 *
 *  So, most of this function is almost identical to sys_ustat().
 *  I have placed comments at the few lines changed or added, to
 *  aid in porting forward if and when sys_ustat() is changed from
 *  its form in kernel 2.2.5.
 */
static int hpux_ustat(dev_t dev, struct hpux_ustat *ubuf)
{
	struct super_block *s;
	struct hpux_ustat tmp;  /* Changed to hpux_ustat */
	struct statfs sbuf;
	int err = -EINVAL;

	lock_kernel();
	s = get_super(to_kdev_t(dev));
	if (s == NULL)
		goto out;
	err = vfs_statfs(s, &sbuf);
	if (err)
		goto out;

	memset(&tmp,0,sizeof(struct hpux_ustat));  /* Changed to hpux_ustat */

	tmp.f_tfree = (int32_t)sbuf.f_bfree;
	tmp.f_tinode = (u_int32_t)sbuf.f_ffree;
	tmp.f_blksize = (u_int32_t)sbuf.f_bsize;  /*  Added this line  */

	/* Changed to hpux_ustat:  */
	err = copy_to_user(ubuf,&tmp,sizeof(struct hpux_ustat)) ? -EFAULT : 0;
out:
	unlock_kernel();
	return err;
}


/*  This function is called from hpux_utssys(); HP-UX implements
 *  uname() as an option to utssys().
 *
 *  The form of this function is pretty much copied from sys_olduname(),
 *  defined in linux/arch/i386/kernel/sys_i386.c.
 */
/*  TODO: Are these put_user calls OK?  Should they pass an int?
 *        (I copied it from sys_i386.c like this.)
 */
static int hpux_uname(struct hpux_utsname *name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct hpux_utsname)))
		return -EFAULT;

	down_read(&uts_sem);

	error = __copy_to_user(&name->sysname,&system_utsname.sysname,HPUX_UTSLEN-1);
	error |= __put_user(0,name->sysname+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->nodename,&system_utsname.nodename,HPUX_UTSLEN-1);
	error |= __put_user(0,name->nodename+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->release,&system_utsname.release,HPUX_UTSLEN-1);
	error |= __put_user(0,name->release+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->version,&system_utsname.version,HPUX_UTSLEN-1);
	error |= __put_user(0,name->version+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->machine,&system_utsname.machine,HPUX_UTSLEN-1);
	error |= __put_user(0,name->machine+HPUX_UTSLEN-1);

	up_read(&uts_sem);

	/*  HP-UX  utsname has no domainname field.  */

	/*  TODO:  Implement idnumber!!!  */
#if 0
	error |= __put_user(0,name->idnumber);
	error |= __put_user(0,name->idnumber+HPUX_SNLEN-1);
#endif

	error = error ? -EFAULT : 0;

	return error;
}

int sys_sethostname(char *, int);
int sys_gethostname(char *, int);

/*  Note: HP-UX just uses the old suser() function to check perms
 *  in this system call.  We'll use capable(CAP_SYS_ADMIN).
 */
int hpux_utssys(char *ubuf, int n, int type)
{
	int len;
	int error;
	switch( type ) {
	case 0:
		/*  uname():  */
		return( hpux_uname( (struct hpux_utsname *)ubuf ) );
		break ;
	case 1:
		/*  Obsolete (used to be umask().)  */
		return -EFAULT ;
		break ;
	case 2:
		/*  ustat():  */
		return( hpux_ustat((dev_t)n, (struct hpux_ustat *)ubuf) );
		break ;
	case 3:
		/*  setuname():
		 *
		 *  On linux (unlike HP-UX), utsname.nodename
		 *  is the same as the hostname.
		 *
		 *  sys_sethostname() is defined in linux/kernel/sys.c.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		return( sys_sethostname(ubuf, len) );
		break ;
	case 4:
		/*  sethostname():
		 *
		 *  sys_sethostname() is defined in linux/kernel/sys.c.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		return( sys_sethostname(ubuf, len) );
		break ;
	case 5:
		/*  gethostname():
		 *
		 *  sys_gethostname() is defined in linux/kernel/sys.c.
		 */
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		return( sys_gethostname(ubuf, n) );
		break ;
	case 6:
		/*  Supposedly called from setuname() in libc.
		 *  TODO: When and why is this called?
		 *        Is it ever even called?
		 *
		 *  This code should look a lot like sys_sethostname(),
		 *  defined in linux/kernel/sys.c.  If that gets updated,
		 *  update this code similarly.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		/**/
		/*  TODO:  print a warning about using this?  */
		down_write(&uts_sem);
		error = -EFAULT;
		if (!copy_from_user(system_utsname.sysname, ubuf, len)) {
			system_utsname.sysname[len] = 0;
			error = 0;
		}
		up_write(&uts_sem);
		return error;
		break ;
	case 7:
		/*  Sets utsname.release, if you're allowed.
		 *  Undocumented.  Used by swinstall to change the
		 *  OS version, during OS updates.  Yuck!!!
		 *
		 *  This code should look a lot like sys_sethostname()
		 *  in linux/kernel/sys.c.  If that gets updated, update
		 *  this code similarly.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		/**/
		/*  TODO:  print a warning about this?  */
		down_write(&uts_sem);
		error = -EFAULT;
		if (!copy_from_user(system_utsname.release, ubuf, len)) {
			system_utsname.release[len] = 0;
			error = 0;
		}
		up_write(&uts_sem);
		return error;
		break ;
	default:
		/*  This system call returns -EFAULT if given an unknown type.
	 	 *  Why not -EINVAL?  I don't know, it's just not what they did.
	 	 */
		return -EFAULT ;
	}
}

int hpux_getdomainname(char *name, int len)
{
 	int nlen;
 	int err = -EFAULT;
 	
 	down_read(&uts_sem);
 	
	nlen = strlen(system_utsname.domainname) + 1;

	if (nlen < len)
		len = nlen;
	if(len > __NEW_UTS_LEN)
		goto done;
	if(copy_to_user(name, system_utsname.domainname, len))
		goto done;
	err = 0;
done:
	up_read(&uts_sem);
	return err;
	
}

int hpux_pipe(int *kstack_fildes)
{
	int error;

	lock_kernel();
	error = do_pipe(kstack_fildes);
	unlock_kernel();
	return error;
}
