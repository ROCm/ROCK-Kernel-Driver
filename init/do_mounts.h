#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/mount.h>

extern asmlinkage long sys_unlink(const char *name);
extern asmlinkage long sys_mknod(const char *name, int mode, dev_t dev);
extern asmlinkage long sys_newstat(char * filename, struct stat * statbuf);
extern asmlinkage long sys_mount(char *dev_name, char *dir_name, char *type,
				 unsigned long flags, void *data);
extern asmlinkage long sys_umount(char *name, int flags);

#ifdef CONFIG_DEVFS_FS

void mount_devfs(void);
void umount_devfs(char *path);
int  create_dev(char *name, dev_t dev, char *devfs_name);

#else

static inline void mount_devfs(void) {}

static inline void umount_devfs(const char *path) {}

static inline int create_dev(char *name, dev_t dev, char *devfs_name)
{
	sys_unlink(name);
	return sys_mknod(name, S_IFBLK|0600, dev);
}

#endif
