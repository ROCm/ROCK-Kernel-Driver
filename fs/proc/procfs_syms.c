#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

extern struct proc_dir_entry *proc_sys_root;

#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(proc_sys_root);
#endif
EXPORT_SYMBOL(proc_symlink);
EXPORT_SYMBOL(proc_mknod);
EXPORT_SYMBOL(proc_mkdir);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_root_fs);
EXPORT_SYMBOL(proc_net);
EXPORT_SYMBOL(proc_bus);
EXPORT_SYMBOL(proc_root_driver);

static DECLARE_FSTYPE(proc_fs_type, "proc", proc_read_super, FS_SINGLE);

static int __init init_proc_fs(void)
{
	int err = register_filesystem(&proc_fs_type);
	if (!err) {
		proc_mnt = kern_mount(&proc_fs_type);
		err = PTR_ERR(proc_mnt);
		if (IS_ERR(proc_mnt))
			unregister_filesystem(&proc_fs_type);
		else
			err = 0;
	}
	return err;
}

static void __exit exit_proc_fs(void)
{
	unregister_filesystem(&proc_fs_type);
	kern_umount(proc_mnt);
}

module_init(init_proc_fs)
module_exit(exit_proc_fs)
