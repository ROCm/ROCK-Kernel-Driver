#ifndef AMDKCL_FS_H
#define AMDKCL_FS_H

#include <linux/fs.h>
#include <linux/version.h>

static inline void *kcl_file_private(const struct file *f)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
	return f->private_data;
#else
	return f->f_inode->i_private;
#endif
}

#endif /* AMDKCL_FS_H */
