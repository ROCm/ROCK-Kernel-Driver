/*
 * linux/fs/fat/fatfs_syms.c
 *
 * Exported kernel symbols for the low-level FAT-based fs support.
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

EXPORT_SYMBOL(fat_new_dir);
EXPORT_SYMBOL(fat_get_block);
EXPORT_SYMBOL(fat_clear_inode);
EXPORT_SYMBOL(fat_date_unix2dos);
EXPORT_SYMBOL(fat_delete_inode);
EXPORT_SYMBOL(fat__get_entry);
EXPORT_SYMBOL(fat_notify_change);
EXPORT_SYMBOL(fat_put_super);
EXPORT_SYMBOL(fat_attach);
EXPORT_SYMBOL(fat_detach);
EXPORT_SYMBOL(fat_build_inode);
EXPORT_SYMBOL(fat_fill_super);
EXPORT_SYMBOL(fat_search_long);
EXPORT_SYMBOL(fat_readdir);
EXPORT_SYMBOL(fat_scan);
EXPORT_SYMBOL(fat_statfs);
EXPORT_SYMBOL(fat_write_inode);
EXPORT_SYMBOL(fat_dir_ioctl);
EXPORT_SYMBOL(fat_add_entries);
EXPORT_SYMBOL(fat_dir_empty);
EXPORT_SYMBOL(fat_truncate);

int __init fat_init_inodecache(void);
void __exit fat_destroy_inodecache(void);
static int __init init_fat_fs(void)
{
	fat_hash_init();
	return fat_init_inodecache();
}

static void __exit exit_fat_fs(void)
{
	fat_destroy_inodecache();
}

module_init(init_fat_fs)
module_exit(exit_fat_fs)
