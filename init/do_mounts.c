#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/blk.h>
#include <linux/tty.h>
#include <linux/fd.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

#include <asm/uaccess.h>

/* syscalls missing from unistd.h */
 
static inline _syscall2(int,mkdir,char *,name,int,mode);
static inline _syscall1(int,chdir,char *,name);
static inline _syscall1(int,chroot,char *,name);
static inline _syscall1(int,unlink,char *,name);
static inline _syscall3(int,mknod,char *,name,int,mode,dev_t,dev);
static inline _syscall5(int,mount,char *,dev,char *,dir,char *,type,
			unsigned long,flags,void *,data);
static inline _syscall2(int,umount,char *,name,int,flags);

extern void rd_load(void);
extern void initrd_load(void);
extern int get_filesystem_list(char * buf);
extern void wait_for_keypress(void);

asmlinkage long sys_mount(char * dev_name, char * dir_name, char * type,
	 unsigned long flags, void * data);

#ifdef CONFIG_BLK_DEV_INITRD
unsigned int real_root_dev;	/* do_proc_dointvec cannot handle kdev_t */
#endif
int root_mountflags = MS_RDONLY;
char root_device_name[64];

/* this is initialized in init/main.c */
kdev_t ROOT_DEV;

static int __init readonly(char *str)
{
	if (*str)
		return 0;
	root_mountflags |= MS_RDONLY;
	return 1;
}

static int __init readwrite(char *str)
{
	if (*str)
		return 0;
	root_mountflags &= ~MS_RDONLY;
	return 1;
}

__setup("ro", readonly);
__setup("rw", readwrite);

static struct dev_name_struct {
	const char *name;
	const int num;
} root_dev_names[] __initdata = {
	{ "nfs",     0x00ff },
	{ "hda",     0x0300 },
	{ "hdb",     0x0340 },
	{ "loop",    0x0700 },
	{ "hdc",     0x1600 },
	{ "hdd",     0x1640 },
	{ "hde",     0x2100 },
	{ "hdf",     0x2140 },
	{ "hdg",     0x2200 },
	{ "hdh",     0x2240 },
	{ "hdi",     0x3800 },
	{ "hdj",     0x3840 },
	{ "hdk",     0x3900 },
	{ "hdl",     0x3940 },
	{ "hdm",     0x5800 },
	{ "hdn",     0x5840 },
	{ "hdo",     0x5900 },
	{ "hdp",     0x5940 },
	{ "hdq",     0x5A00 },
	{ "hdr",     0x5A40 },
	{ "hds",     0x5B00 },
	{ "hdt",     0x5B40 },
	{ "sda",     0x0800 },
	{ "sdb",     0x0810 },
	{ "sdc",     0x0820 },
	{ "sdd",     0x0830 },
	{ "sde",     0x0840 },
	{ "sdf",     0x0850 },
	{ "sdg",     0x0860 },
	{ "sdh",     0x0870 },
	{ "sdi",     0x0880 },
	{ "sdj",     0x0890 },
	{ "sdk",     0x08a0 },
	{ "sdl",     0x08b0 },
	{ "sdm",     0x08c0 },
	{ "sdn",     0x08d0 },
	{ "sdo",     0x08e0 },
	{ "sdp",     0x08f0 },
	{ "ada",     0x1c00 },
	{ "adb",     0x1c10 },
	{ "adc",     0x1c20 },
	{ "add",     0x1c30 },
	{ "ade",     0x1c40 },
	{ "fd",      0x0200 },
	{ "md",      0x0900 },	     
	{ "xda",     0x0d00 },
	{ "xdb",     0x0d40 },
	{ "ram",     0x0100 },
	{ "scd",     0x0b00 },
	{ "mcd",     0x1700 },
	{ "cdu535",  0x1800 },
	{ "sonycd",  0x1800 },
	{ "aztcd",   0x1d00 },
	{ "cm206cd", 0x2000 },
	{ "gscd",    0x1000 },
	{ "sbpcd",   0x1900 },
	{ "eda",     0x2400 },
	{ "edb",     0x2440 },
	{ "pda",	0x2d00 },
	{ "pdb",	0x2d10 },
	{ "pdc",	0x2d20 },
	{ "pdd",	0x2d30 },
	{ "pcd",	0x2e00 },
	{ "pf",		0x2f00 },
	{ "apblock", APBLOCK_MAJOR << 8},
	{ "ddv", DDV_MAJOR << 8},
	{ "jsfd",    JSFD_MAJOR << 8},
#if defined(CONFIG_ARCH_S390)
	{ "dasda", (DASD_MAJOR << MINORBITS) },
	{ "dasdb", (DASD_MAJOR << MINORBITS) + (1 << 2) },
	{ "dasdc", (DASD_MAJOR << MINORBITS) + (2 << 2) },
	{ "dasdd", (DASD_MAJOR << MINORBITS) + (3 << 2) },
	{ "dasde", (DASD_MAJOR << MINORBITS) + (4 << 2) },
	{ "dasdf", (DASD_MAJOR << MINORBITS) + (5 << 2) },
	{ "dasdg", (DASD_MAJOR << MINORBITS) + (6 << 2) },
	{ "dasdh", (DASD_MAJOR << MINORBITS) + (7 << 2) },
#endif
#if defined(CONFIG_BLK_CPQ_DA) || defined(CONFIG_BLK_CPQ_DA_MODULE)
	{ "ida/c0d0p",0x4800 },
	{ "ida/c0d1p",0x4810 },
	{ "ida/c0d2p",0x4820 },
	{ "ida/c0d3p",0x4830 },
	{ "ida/c0d4p",0x4840 },
	{ "ida/c0d5p",0x4850 },
	{ "ida/c0d6p",0x4860 },
	{ "ida/c0d7p",0x4870 },
	{ "ida/c0d8p",0x4880 },
	{ "ida/c0d9p",0x4890 },
	{ "ida/c0d10p",0x48A0 },
	{ "ida/c0d11p",0x48B0 },
	{ "ida/c0d12p",0x48C0 },
	{ "ida/c0d13p",0x48D0 },
	{ "ida/c0d14p",0x48E0 },
	{ "ida/c0d15p",0x48F0 },
#endif
#if defined(CONFIG_BLK_CPQ_CISS_DA) || defined(CONFIG_BLK_CPQ_CISS_DA_MODULE)
	{ "cciss/c0d0p",0x6800 },
	{ "cciss/c0d1p",0x6810 },
	{ "cciss/c0d2p",0x6820 },
	{ "cciss/c0d3p",0x6830 },
	{ "cciss/c0d4p",0x6840 },
	{ "cciss/c0d5p",0x6850 },
	{ "cciss/c0d6p",0x6860 },
	{ "cciss/c0d7p",0x6870 },
	{ "cciss/c0d8p",0x6880 },
	{ "cciss/c0d9p",0x6890 },
	{ "cciss/c0d10p",0x68A0 },
	{ "cciss/c0d11p",0x68B0 },
	{ "cciss/c0d12p",0x68C0 },
	{ "cciss/c0d13p",0x68D0 },
	{ "cciss/c0d14p",0x68E0 },
	{ "cciss/c0d15p",0x68F0 },
#endif
	{ "nftla", 0x5d00 },
	{ "nftlb", 0x5d10 },
	{ "nftlc", 0x5d20 },
	{ "nftld", 0x5d30 },
	{ "ftla", 0x2c00 },
	{ "ftlb", 0x2c08 },
	{ "ftlc", 0x2c10 },
	{ "ftld", 0x2c18 },
	{ "mtdblock", 0x1f00 },
	{ NULL, 0 }
};

kdev_t __init name_to_kdev_t(char *line)
{
	int base = 0;

	if (strncmp(line,"/dev/",5) == 0) {
		struct dev_name_struct *dev = root_dev_names;
		line += 5;
		do {
			int len = strlen(dev->name);
			if (strncmp(line,dev->name,len) == 0) {
				line += len;
				base = dev->num;
				break;
			}
			dev++;
		} while (dev->name);
	}
	return to_kdev_t(base + simple_strtoul(line,NULL,base?10:16));
}

static int __init root_dev_setup(char *line)
{
	int i;
	char ch;

	ROOT_DEV = name_to_kdev_t(line);
	memset (root_device_name, 0, sizeof root_device_name);
	if (strncmp (line, "/dev/", 5) == 0) line += 5;
	for (i = 0; i < sizeof root_device_name - 1; ++i)
	{
	    ch = line[i];
	    if ( isspace (ch) || (ch == ',') || (ch == '\0') ) break;
	    root_device_name[i] = ch;
	}
	return 1;
}

__setup("root=", root_dev_setup);

static char * __initdata root_mount_data;
static int __init root_data_setup(char *str)
{
	root_mount_data = str;
	return 1;
}

static char * __initdata root_fs_names;
static int __init fs_names_setup(char *str)
{
	root_fs_names = str;
	return 1;
}

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);

static void __init get_fs_names(char *page)
{
	char *s = page;

	if (root_fs_names) {
		strcpy(page, root_fs_names);
		while (*s++) {
			if (s[-1] == ',')
				s[-1] = '\0';
		}
	} else {
		int len = get_filesystem_list(page);
		char *p, *next;

		page[len] = '\0';
		for (p = page-1; p; p = next) {
			next = strchr(++p, '\n');
			if (*p++ != '\t')
				continue;
			while ((*s++ = *p++) != '\n')
				;
			s[-1] = '\0';
		}
	}
	*s = '\0';
}

static void __init mount_root(void)
{
	void *handle;
	char path[64];
	char *name = "/dev/root";
	char *fs_names, *p;
	int do_devfs = 0;

	root_mountflags |= MS_VERBOSE;

	fs_names = __getname();
	get_fs_names(fs_names);

#ifdef CONFIG_ROOT_NFS
	if (MAJOR(ROOT_DEV) == UNNAMED_MAJOR) {
		void *data;
		data = nfs_root_data();
		if (data) {
			int err = mount("/dev/root", "/root", "nfs", root_mountflags, data);
			if (!err)
				goto done;
		}
		printk(KERN_ERR "VFS: Unable to mount root fs via NFS, trying floppy.\n");
		ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
	}
#endif

#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
#ifdef CONFIG_BLK_DEV_RAM
		extern int rd_doload;
		extern void rd_load_secondary(void);
#endif
		floppy_eject();
#ifndef CONFIG_BLK_DEV_RAM
		printk(KERN_NOTICE "(Warning, this kernel has no ramdisk support)\n");
#else
		/* rd_doload is 2 for a dual initrd/ramload setup */
		if(rd_doload==2)
			rd_load_secondary();
		else
#endif
		{
			printk(KERN_NOTICE "VFS: Insert root floppy and press ENTER\n");
			wait_for_keypress();
		}
	}
#endif

	devfs_make_root (root_device_name);
	handle = devfs_find_handle (NULL, ROOT_DEVICE_NAME,
	                            MAJOR (ROOT_DEV), MINOR (ROOT_DEV),
				    DEVFS_SPECIAL_BLK, 1);
	if (handle) {
		int n;
		unsigned major, minor;

		devfs_get_maj_min (handle, &major, &minor);
		ROOT_DEV = MKDEV (major, minor);
		if (!ROOT_DEV)
			panic("I have no root and I want to scream");
		n = devfs_generate_path (handle, path + 5, sizeof (path) - 5);
		if (n >= 0) {
			name = path + n;
			devfs_mk_symlink (NULL, "root", DEVFS_FL_DEFAULT,
					  name + 5, NULL, NULL);
			memcpy (name, "/dev/", 5);
			do_devfs = 1;
		}
	}
	chdir("/dev");
	unlink("root");
	mknod("root", S_IFBLK|0600, kdev_t_to_nr(ROOT_DEV));
	if (do_devfs)
		mount("devfs", ".", "devfs", 0, NULL);
retry:
	for (p = fs_names; *p; p += strlen(p)+1) {
		int err;
		err = sys_mount(name,"/root",p,root_mountflags,root_mount_data);
		switch (err) {
			case 0:
				goto done;
			case -EACCES:
				root_mountflags |= MS_RDONLY;
				goto retry;
			case -EINVAL:
				continue;
		}
	        /*
		 * Allow the user to distinguish between failed open
		 * and bad superblock on root device.
		 */
		printk ("VFS: Cannot open root device \"%s\" or %s\n",
			root_device_name, kdevname (ROOT_DEV));
		printk ("Please append a correct \"root=\" boot option\n");
		panic("VFS: Unable to mount root fs on %s",
			kdevname(ROOT_DEV));
	}
	panic("VFS: Unable to mount root fs on %s", kdevname(ROOT_DEV));

done:
	putname(fs_names);
	if (do_devfs)
		umount(".", 0);
}

#ifdef CONFIG_BLK_DEV_INITRD

static int __init change_root(kdev_t new_root_dev,const char *put_old)
{
	struct vfsmount *old_rootmnt;
	struct nameidata devfs_nd;
	char *new_devname = kmalloc(strlen("/dev/root.old")+1, GFP_KERNEL);
	int error = 0;

	if (new_devname)
		strcpy(new_devname, "/dev/root.old");

	/* .. here is directory mounted over root */
	mount("..", ".", NULL, MS_MOVE, NULL);
	chdir("/old");

	read_lock(&current->fs->lock);
	old_rootmnt = mntget(current->fs->pwdmnt);
	read_unlock(&current->fs->lock);

	/*  First unmount devfs if mounted  */
	if (path_init("/old/dev", LOOKUP_FOLLOW|LOOKUP_POSITIVE, &devfs_nd))
		error = path_walk("/old/dev", &devfs_nd);
	if (!error) {
		if (devfs_nd.mnt->mnt_sb->s_magic == DEVFS_SUPER_MAGIC &&
		    devfs_nd.dentry == devfs_nd.mnt->mnt_root)
			umount("/old/dev", 0);
		path_release(&devfs_nd);
	}

	ROOT_DEV = new_root_dev;
	mount_root();

	chdir("/root");
	ROOT_DEV = current->fs->pwdmnt->mnt_sb->s_dev;
	printk("VFS: Mounted root (%s filesystem)%s.\n",
		current->fs->pwdmnt->mnt_sb->s_type->name,
		(current->fs->pwdmnt->mnt_sb->s_flags & MS_RDONLY) ? " readonly" : "");

#if 1
	shrink_dcache();
	printk("change_root: old root has d_count=%d\n", 
	       atomic_read(&old_rootmnt->mnt_root->d_count));
#endif

	error = mount("/old", "/root/initrd", NULL, MS_MOVE, NULL);
	if (error) {
		int blivet;
		struct block_device *ramdisk = old_rootmnt->mnt_sb->s_bdev;

		atomic_inc(&ramdisk->bd_count);
		blivet = blkdev_get(ramdisk, FMODE_READ, 0, BDEV_FS);
		printk(KERN_NOTICE "Trying to unmount old root ... ");
		umount("/old", MNT_DETACH);
		if (!blivet) {
			blivet = ioctl_by_bdev(ramdisk, BLKFLSBUF, 0);
			blkdev_put(ramdisk, BDEV_FS);
		}
		if (blivet) {
			printk(KERN_ERR "error %d\n", blivet);
		} else {
			printk("okay\n");
			error = 0;
		}
	} else {
		spin_lock(&dcache_lock);
		if (new_devname) {
			void *p = old_rootmnt->mnt_devname;
			old_rootmnt->mnt_devname = new_devname;
			new_devname = p;
		}
		spin_unlock(&dcache_lock);
	}

	/* put the old stuff */
	mntput(old_rootmnt);
	kfree(new_devname);
	return error;
}

#endif

#ifdef CONFIG_BLK_DEV_INITRD
static int do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };
	extern char * envp_init[];

	chdir("/root");
	mount(".", "/", NULL, MS_MOVE, NULL);
	chroot(".");

	mount_devfs_fs ();

	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/console",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp_init);
}

#endif

/*
 * Prepare the namespace - decide what/where to mount, load ramdisks, etc.
 */
void prepare_namespace(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	int real_root_mountflags = root_mountflags;
	if (!initrd_start)
		mount_initrd = 0;
	if (mount_initrd)
		root_mountflags &= ~MS_RDONLY;
	real_root_dev = ROOT_DEV;
#endif
	mkdir("/dev", 0700);
	mkdir("/root", 0700);

#ifdef CONFIG_BLK_DEV_RAM
#ifdef CONFIG_BLK_DEV_INITRD
	if (mount_initrd)
		initrd_load();
	else
#endif
	rd_load();
#endif

	/* Mount the root filesystem.. */
	mount_root();
	chdir("/root");
	ROOT_DEV = current->fs->pwdmnt->mnt_sb->s_dev;
	printk("VFS: Mounted root (%s filesystem)%s.\n",
		current->fs->pwdmnt->mnt_sb->s_type->name,
		(current->fs->pwdmnt->mnt_sb->s_flags & MS_RDONLY) ? " readonly" : "");

#ifdef CONFIG_BLK_DEV_INITRD
	root_mountflags = real_root_mountflags;
	if (mount_initrd && ROOT_DEV != real_root_dev
	    && MAJOR(ROOT_DEV) == RAMDISK_MAJOR && MINOR(ROOT_DEV) == 0) {
		int error;
		int i, pid;
		mkdir("/old", 0700);
		chdir("/old");

		pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
		if (pid > 0) {
			while (pid != wait(&i)) {
				current->policy |= SCHED_YIELD;
				schedule();
			}
		}
		if (MAJOR(real_root_dev) != RAMDISK_MAJOR
		     || MINOR(real_root_dev) != 0) {
			error = change_root(real_root_dev,"/initrd");
			if (error)
				printk(KERN_ERR "Change root to /initrd: "
				    "error %d\n",error);

			chdir("/root");
			mount(".", "/", NULL, MS_MOVE, NULL);
			chroot(".");

			mount_devfs_fs ();
			return;
		}
		chroot("..");
		chdir("/");
		return;
	}
#endif
	mount(".", "/", NULL, MS_MOVE, NULL);
	chroot(".");

	mount_devfs_fs ();
}
