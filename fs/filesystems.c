/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/uaccess.h>

/*
 * Handling of filesystem drivers list.
 * Rules:
 *	Inclusion to/removals from/scanning of list are protected by spinlock.
 *	During the unload module must call unregister_filesystem().
 *	We can access the fields of list element if:
 *		1) spinlock is held or
 *		2) we hold the reference to the module.
 *	The latter can be guaranteed by call of try_module_get(); if it
 *	returned 0 we must skip the element, otherwise we got the reference.
 *	Once the reference is obtained we can drop the spinlock.
 */

static struct file_system_type *file_systems;
static rwlock_t file_systems_lock = RW_LOCK_UNLOCKED;

/* WARNING: This can be used only if we _already_ own a reference */
void get_filesystem(struct file_system_type *fs)
{
	if (!try_module_get(fs->owner)) {
#ifdef CONFIG_MODULE_UNLOAD
		unsigned int cpu = get_cpu();
		local_inc(&fs->owner->ref[cpu].count);
		put_cpu();
#else
		/* Getting filesystem while it's starting up?  We're
                   already supposed to have a reference. */
		BUG();
#endif
	}
}

void put_filesystem(struct file_system_type *fs)
{
	module_put(fs->owner);
}

static struct file_system_type **find_filesystem(const char *name)
{
	struct file_system_type **p;
	for (p=&file_systems; *p; p=&(*p)->next)
		if (strcmp((*p)->name,name) == 0)
			break;
	return p;
}


/* define fs_subsys */
static decl_subsys(fs, NULL, NULL);

static int register_fs_subsys(struct file_system_type * fs)
{
	struct subsystem *sub = &fs->subsys;

	snprintf(sub->kset.kobj.name, KOBJ_NAME_LEN, "%s", fs->name);
	subsys_set_kset(fs, fs_subsys);
	return subsystem_register(sub);
}

static int unlink_fs(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	write_lock(&file_systems_lock);
	tmp = &file_systems;
	while (*tmp) {
		if (fs == *tmp) {
			*tmp = fs->next;
			fs->next = NULL;
			write_unlock(&file_systems_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&file_systems_lock);
	return -EINVAL;
}

/**
 *	register_filesystem - register a new filesystem
 *	@fs: the file system structure
 *
 *	Adds the file system passed to the list of file systems the kernel
 *	is aware of for mount and other syscalls. Returns 0 on success,
 *	or a negative errno code on an error.
 *
 *	The &struct file_system_type that is passed is linked into the kernel 
 *	structures and must not be freed until the file system has been
 *	unregistered.
 */
 
int register_filesystem(struct file_system_type * fs)
{
	int res = 0;
	struct file_system_type ** p;

	if (!fs)
		return -EINVAL;
	if (fs->next)
		return -EBUSY;
	INIT_LIST_HEAD(&fs->fs_supers);
	write_lock(&file_systems_lock);
	p = find_filesystem(fs->name);
	if (*p)
		res = -EBUSY;
	else
		*p = fs;
	write_unlock(&file_systems_lock);

	if (!res) {
		/* we implicitly possess reference to @fs during registration,
		 * so it cannot be unregister from under us. */
		register_fs_subsys(fs);
	}
	return res;
}

/**
 *	unregister_filesystem - unregister a file system
 *	@fs: filesystem to unregister
 *
 *	Remove a file system that was previously successfully registered
 *	with the kernel. An error is returned if the file system is not found.
 *	Zero is returned on a success.
 *	
 *	Once this function has returned the &struct file_system_type structure
 *	may be freed or reused.
 */
 
int unregister_filesystem(struct file_system_type * fs)
{
	int res;

	res = unlink_fs(fs);
	if (!res)
		subsystem_unregister(&fs->subsys);
	return res;
}

extern int sysfs_init(void);

/**
 *	fs_subsys_init - initialize sysfs and fs subsystem.
 *
 *	In order to register filesystems in sysfs, it has to be 
 *	initialized. Also, we need the base fs filesystem, so the 
 *	registered filesystems have a home.
 *	
 *	During sysfs_init(), the registration of sysfs into itself
 *	will fail, since it's not mounted yet. To make sure that 
 *	sysfs does show up, we re-register sysfs's embedded subsystem,
 *	which will get added, since sysfs is now mounted.
 */

void __init fs_subsys_init(void)
{
	struct file_system_type ** p;

	/* make sure sysfs is up and running */
	sysfs_init();

	/* register fs_subsys */
	subsystem_register(&fs_subsys);

	p = find_filesystem("sysfs");

	if (p)
		/* make sure it's registered */
		register_fs_subsys(*p);
}

static int fs_index(const char * __name)
{
	struct file_system_type * tmp;
	char * name;
	int err, index;

	name = getname(__name);
	err = PTR_ERR(name);
	if (IS_ERR(name))
		return err;

	err = -EINVAL;
	read_lock(&file_systems_lock);
	for (tmp=file_systems, index=0 ; tmp ; tmp=tmp->next, index++) {
		if (strcmp(tmp->name,name) == 0) {
			err = index;
			break;
		}
	}
	read_unlock(&file_systems_lock);
	putname(name);
	return err;
}

static int fs_name(unsigned int index, char * buf)
{
	struct file_system_type * tmp;
	int len, res;

	read_lock(&file_systems_lock);
	for (tmp = file_systems; tmp; tmp = tmp->next, index--)
		if (index <= 0 && try_module_get(tmp->owner))
			break;
	read_unlock(&file_systems_lock);
	if (!tmp)
		return -EINVAL;

	/* OK, we got the reference, so we can safely block */
	len = strlen(tmp->name) + 1;
	res = copy_to_user(buf, tmp->name, len) ? -EFAULT : 0;
	put_filesystem(tmp);
	return res;
}

static int fs_maxindex(void)
{
	struct file_system_type * tmp;
	int index;

	read_lock(&file_systems_lock);
	for (tmp = file_systems, index = 0 ; tmp ; tmp = tmp->next, index++)
		;
	read_unlock(&file_systems_lock);
	return index;
}

/*
 * Whee.. Weird sysv syscall. 
 */
asmlinkage long sys_sysfs(int option, unsigned long arg1, unsigned long arg2)
{
	int retval = -EINVAL;

	switch (option) {
		case 1:
			retval = fs_index((const char *) arg1);
			break;

		case 2:
			retval = fs_name(arg1, (char *) arg2);
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	return retval;
}

int get_filesystem_list(char * buf)
{
	int len = 0;
	struct file_system_type * tmp;

	read_lock(&file_systems_lock);
	tmp = file_systems;
	while (tmp && len < PAGE_SIZE - 80) {
		len += sprintf(buf+len, "%s\t%s\n",
			(tmp->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
			tmp->name);
		tmp = tmp->next;
	}
	read_unlock(&file_systems_lock);
	return len;
}

struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;

	read_lock(&file_systems_lock);
	fs = *(find_filesystem(name));
	if (fs && !try_module_get(fs->owner))
		fs = NULL;
	read_unlock(&file_systems_lock);
	if (!fs && (request_module(name) == 0)) {
		read_lock(&file_systems_lock);
		fs = *(find_filesystem(name));
		if (fs && !try_module_get(fs->owner))
			fs = NULL;
		read_unlock(&file_systems_lock);
	}
	return fs;
}
