/*
 * linux/fs/msdos/vfatfs_syms.c
 *
 * Exported kernel symbols for the VFAT filesystem.
 * These symbols are used by dmsdos.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

static struct super_block *vfat_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, vfat_fill_super);
}

static struct file_system_type vfat_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "vfat",
	.get_sb		= vfat_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

EXPORT_SYMBOL(vfat_create);
EXPORT_SYMBOL(vfat_unlink);
EXPORT_SYMBOL(vfat_mkdir);
EXPORT_SYMBOL(vfat_rmdir);
EXPORT_SYMBOL(vfat_rename);
EXPORT_SYMBOL(vfat_lookup);

static int __init init_vfat_fs(void)
{
	return register_filesystem(&vfat_fs_type);
}

static void __exit exit_vfat_fs(void)
{
	unregister_filesystem(&vfat_fs_type);
}

module_init(init_vfat_fs)
module_exit(exit_vfat_fs)
MODULE_LICENSE("GPL");
