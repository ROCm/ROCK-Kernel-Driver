#ifndef _LINUX_DEVFS_FS_KERNEL_H
#define _LINUX_DEVFS_FS_KERNEL_H

#include <linux/fs.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/kdev_t.h>
#include <linux/types.h>

#include <asm/semaphore.h>

#define DEVFS_SUPER_MAGIC                0x1373

#define DEVFS_FL_NONE           0x000 /* This helps to make code more readable
				         no, it doesn't  --hch */
#define DEVFS_FL_DEFAULT        DEVFS_FL_NONE


typedef struct devfs_entry * devfs_handle_t;

struct gendisk;

#ifdef CONFIG_DEVFS_FS
extern devfs_handle_t devfs_register (devfs_handle_t dir, const char *name,
				      unsigned int flags,
				      unsigned int major, unsigned int minor,
				      umode_t mode, void *ops, void *info);
extern void devfs_unregister (devfs_handle_t de);
extern int devfs_mk_symlink (const char *name, const char *link);
extern devfs_handle_t devfs_mk_dir(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern void devfs_remove(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern int devfs_register_tape(const char *name);
extern void devfs_unregister_tape(int num);
extern void devfs_create_partitions(struct gendisk *dev);
extern void devfs_create_cdrom(struct gendisk *dev);
extern void devfs_remove_partitions(struct gendisk *dev);
extern void devfs_remove_cdrom(struct gendisk *dev);
extern void devfs_register_partition(struct gendisk *dev, int part);
extern void mount_devfs_fs(void);
#else  /*  CONFIG_DEVFS_FS  */
static inline devfs_handle_t devfs_register (devfs_handle_t dir,
					     const char *name,
					     unsigned int flags,
					     unsigned int major,
					     unsigned int minor,
					     umode_t mode,
					     void *ops, void *info)
{
    return NULL;
}
static inline void devfs_unregister (devfs_handle_t de)
{
    return;
}
static inline int devfs_mk_symlink (const char *name, const char *link)
{
    return 0;
}
static inline devfs_handle_t devfs_mk_dir(const char *fmt, ...)
{
    return NULL;
}
static inline void devfs_remove(const char *fmt, ...)
{
}
static inline int devfs_register_tape (devfs_handle_t de)
{
    return -1;
}
static inline void devfs_unregister_tape(int num)
{
}
static inline void devfs_create_partitions(struct gendisk *dev)
{
}
static inline void devfs_create_cdrom(struct gendisk *dev)
{
}
static inline void devfs_remove_partitions(struct gendisk *dev)
{
}
static inline void devfs_remove_cdrom(struct gendisk *dev)
{
}
static inline void devfs_register_partition(struct gendisk *dev, int part)
{
}
static inline void mount_devfs_fs (void)
{
    return;
}
#endif  /*  CONFIG_DEVFS_FS  */
#endif  /*  _LINUX_DEVFS_FS_KERNEL_H  */
