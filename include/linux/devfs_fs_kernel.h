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
				       */
#define DEVFS_FL_AUTO_DEVNUM    0x002 /* Automatically generate device number
				       */
#define DEVFS_FL_REMOVABLE      0x008 /* This is a removable media device    */
#define DEVFS_FL_WAIT           0x010 /* Wait for devfsd to finish           */
#define DEVFS_FL_CURRENT_OWNER  0x020 /* Set initial ownership to current    */
#define DEVFS_FL_DEFAULT        DEVFS_FL_NONE


#define DEVFS_SPECIAL_CHR     0
#define DEVFS_SPECIAL_BLK     1

typedef struct devfs_entry * devfs_handle_t;

#ifdef CONFIG_DEVFS_FS

extern void devfs_remove(const char *fmt, ...) __attribute__((format (printf, 1, 2)));

struct unique_numspace
{
    spinlock_t init_lock;
    unsigned char sem_initialised;
    unsigned int num_free;          /*  Num free in bits       */
    unsigned int length;            /*  Array length in bytes  */
    unsigned long *bits;
    struct semaphore semaphore;
};

#define UNIQUE_NUMBERSPACE_INITIALISER {SPIN_LOCK_UNLOCKED, 0, 0, 0, NULL}

extern void devfs_put (devfs_handle_t de);
extern devfs_handle_t devfs_register (devfs_handle_t dir, const char *name,
				      unsigned int flags,
				      unsigned int major, unsigned int minor,
				      umode_t mode, void *ops, void *info);
extern void devfs_unregister (devfs_handle_t de);
extern int devfs_mk_symlink (devfs_handle_t dir, const char *name,
			     unsigned int flags, const char *link,
			     devfs_handle_t *handle, void *info);
extern devfs_handle_t devfs_mk_dir (devfs_handle_t dir, const char *name,
				    void *info);
extern devfs_handle_t devfs_get_handle (devfs_handle_t dir, const char *name,
					int traverse_symlinks);
extern devfs_handle_t devfs_get_handle_from_inode (struct inode *inode);
extern int devfs_generate_path (devfs_handle_t de, char *path, int buflen);
extern void *devfs_get_ops (devfs_handle_t de);
extern void devfs_put_ops (devfs_handle_t de);
extern int devfs_set_file_size (devfs_handle_t de, unsigned long size);
extern void *devfs_get_info (devfs_handle_t de);
extern int devfs_set_info (devfs_handle_t de, void *info);
extern devfs_handle_t devfs_get_parent (devfs_handle_t de);
extern devfs_handle_t devfs_get_first_child (devfs_handle_t de);
extern devfs_handle_t devfs_get_next_sibling (devfs_handle_t de);
extern const char *devfs_get_name (devfs_handle_t de, unsigned int *namelen);
extern int devfs_only (void);
extern int devfs_register_tape (devfs_handle_t de);
extern void devfs_unregister_tape(int num);
extern void devfs_register_series (devfs_handle_t dir, const char *format,
				   unsigned int num_entries,
				   unsigned int flags, unsigned int major,
				   unsigned int minor_start,
				   umode_t mode, void *ops, void *info);
extern int devfs_alloc_major (char type);
extern void devfs_dealloc_major (char type, int major);
extern kdev_t devfs_alloc_devnum (char type);
extern void devfs_dealloc_devnum (char type, kdev_t devnum);
extern int devfs_alloc_unique_number (struct unique_numspace *space);
extern void devfs_dealloc_unique_number (struct unique_numspace *space,
					 int number);

extern void mount_devfs_fs (void);

#else  /*  CONFIG_DEVFS_FS  */

struct unique_numspace
{
    char dummy;
};

#define UNIQUE_NUMBERSPACE_INITIALISER {0}

static inline void devfs_put (devfs_handle_t de)
{
    return;
}
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
static inline int devfs_mk_symlink (devfs_handle_t dir, const char *name,
				    unsigned int flags, const char *link,
				    devfs_handle_t *handle, void *info)
{
    return 0;
}
static inline devfs_handle_t devfs_mk_dir (devfs_handle_t dir,
					   const char *name, void *info)
{
    return NULL;
}
static inline devfs_handle_t devfs_get_handle (devfs_handle_t dir,
					       const char *name,
					       int traverse_symlinks)
{
    return NULL;
}
static inline void devfs_remove(const char *fmt, ...)
{
}
static inline devfs_handle_t devfs_get_handle_from_inode (struct inode *inode)
{
    return NULL;
}
static inline int devfs_generate_path (devfs_handle_t de, char *path,
				       int buflen)
{
    return -ENOSYS;
}
static inline void *devfs_get_ops (devfs_handle_t de)
{
    return NULL;
}
static inline void devfs_put_ops (devfs_handle_t de)
{
    return;
}
static inline int devfs_set_file_size (devfs_handle_t de, unsigned long size)
{
    return -ENOSYS;
}
static inline void *devfs_get_info (devfs_handle_t de)
{
    return NULL;
}
static inline int devfs_set_info (devfs_handle_t de, void *info)
{
    return 0;
}
static inline devfs_handle_t devfs_get_parent (devfs_handle_t de)
{
    return NULL;
}
static inline devfs_handle_t devfs_get_first_child (devfs_handle_t de)
{
    return NULL;
}
static inline devfs_handle_t devfs_get_next_sibling (devfs_handle_t de)
{
    return NULL;
}
static inline const char *devfs_get_name (devfs_handle_t de,
					  unsigned int *namelen)
{
    return NULL;
}
static inline int devfs_only (void)
{
    return 0;
}
static inline int devfs_register_tape (devfs_handle_t de)
{
    return -1;
}
static inline void devfs_unregister_tape(int num)
{
}
static inline void devfs_register_series (devfs_handle_t dir,
					  const char *format,
					  unsigned int num_entries,
					  unsigned int flags,
					  unsigned int major,
					  unsigned int minor_start,
					  umode_t mode, void *ops, void *info)
{
    return;
}

static inline int devfs_alloc_major (char type)
{
    return -1;
}

static inline void devfs_dealloc_major (char type, int major)
{
    return;
}

static inline kdev_t devfs_alloc_devnum (char type)
{
    return NODEV;
}

static inline void devfs_dealloc_devnum (char type, kdev_t devnum)
{
    return;
}

static inline int devfs_alloc_unique_number (struct unique_numspace *space)
{
    return -1;
}

static inline void devfs_dealloc_unique_number (struct unique_numspace *space,
						int number)
{
    return;
}

static inline void mount_devfs_fs (void)
{
    return;
}
#endif  /*  CONFIG_DEVFS_FS  */

#endif  /*  _LINUX_DEVFS_FS_KERNEL_H  */
