/*
  File: fs/devpts/xattr.c
 
  Derived from fs/ext3/xattr.c, changed in the following ways:
      drop everything related to persistent storage of EAs
      pass dentry rather than inode to internal methods
      only presently define a handler for security modules
*/

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/semaphore.h>
#include "xattr.h"

static struct devpts_xattr_handler *devpts_xattr_handlers[DEVPTS_XATTR_INDEX_MAX];
static rwlock_t devpts_handler_lock = RW_LOCK_UNLOCKED;

int
devpts_xattr_register(int name_index, struct devpts_xattr_handler *handler)
{
	int error = -EINVAL;

	if (name_index > 0 && name_index <= DEVPTS_XATTR_INDEX_MAX) {
		write_lock(&devpts_handler_lock);
		if (!devpts_xattr_handlers[name_index-1]) {
			devpts_xattr_handlers[name_index-1] = handler;
			error = 0;
		}
		write_unlock(&devpts_handler_lock);
	}
	return error;
}

void
devpts_xattr_unregister(int name_index, struct devpts_xattr_handler *handler)
{
	if (name_index > 0 || name_index <= DEVPTS_XATTR_INDEX_MAX) {
		write_lock(&devpts_handler_lock);
		devpts_xattr_handlers[name_index-1] = NULL;
		write_unlock(&devpts_handler_lock);
	}
}

static inline const char *
strcmp_prefix(const char *a, const char *a_prefix)
{
	while (*a_prefix && *a == *a_prefix) {
		a++;
		a_prefix++;
	}
	return *a_prefix ? NULL : a;
}

/*
 * Decode the extended attribute name, and translate it into
 * the name_index and name suffix.
 */
static inline struct devpts_xattr_handler *
devpts_xattr_resolve_name(const char **name)
{
	struct devpts_xattr_handler *handler = NULL;
	int i;

	if (!*name)
		return NULL;
	read_lock(&devpts_handler_lock);
	for (i=0; i<DEVPTS_XATTR_INDEX_MAX; i++) {
		if (devpts_xattr_handlers[i]) {
			const char *n = strcmp_prefix(*name,
				devpts_xattr_handlers[i]->prefix);
			if (n) {
				handler = devpts_xattr_handlers[i];
				*name = n;
				break;
			}
		}
	}
	read_unlock(&devpts_handler_lock);
	return handler;
}

static inline struct devpts_xattr_handler *
devpts_xattr_handler(int name_index)
{
	struct devpts_xattr_handler *handler = NULL;
	if (name_index > 0 && name_index <= DEVPTS_XATTR_INDEX_MAX) {
		read_lock(&devpts_handler_lock);
		handler = devpts_xattr_handlers[name_index-1];
		read_unlock(&devpts_handler_lock);
	}
	return handler;
}

/*
 * Inode operation getxattr()
 *
 * dentry->d_inode->i_sem down
 */
ssize_t
devpts_getxattr(struct dentry *dentry, const char *name,
	      void *buffer, size_t size)
{
	struct devpts_xattr_handler *handler;

	handler = devpts_xattr_resolve_name(&name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->get(dentry, name, buffer, size);
}

/*
 * Inode operation listxattr()
 *
 * dentry->d_inode->i_sem down
 */
ssize_t
devpts_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct devpts_xattr_handler *handler = NULL;
	int i, error = 0;
	unsigned int size = 0;
	char *buf;

	read_lock(&devpts_handler_lock);

	for (i=0; i<DEVPTS_XATTR_INDEX_MAX; i++) {
		handler = devpts_xattr_handlers[i];
		if (handler)
			size += handler->list(dentry, NULL);
	}

	if (!buffer) {
		error = size;
		goto out;
	} else {
		error = -ERANGE;
		if (size > buffer_size)
			goto out;
	}

	buf = buffer;
	for (i=0; i<DEVPTS_XATTR_INDEX_MAX; i++) {
		handler = devpts_xattr_handlers[i];
		if (handler)
			buf += handler->list(dentry, buf);
	}
	error = size;

out:
	read_unlock(&devpts_handler_lock);
	return size;
}

/*
 * Inode operation setxattr()
 *
 * dentry->d_inode->i_sem down
 */
int
devpts_setxattr(struct dentry *dentry, const char *name,
	      const void *value, size_t size, int flags)
{
	struct devpts_xattr_handler *handler;

	if (size == 0)
		value = "";  /* empty EA, do not remove */
	handler = devpts_xattr_resolve_name(&name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->set(dentry, name, value, size, flags);
}

/*
 * Inode operation removexattr()
 *
 * dentry->d_inode->i_sem down
 */
int
devpts_removexattr(struct dentry *dentry, const char *name)
{
	struct devpts_xattr_handler *handler;

	handler = devpts_xattr_resolve_name(&name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->set(dentry, name, NULL, 0, XATTR_REPLACE);
}

int __init
init_devpts_xattr(void)
{
#ifdef CONFIG_DEVPTS_FS_SECURITY	
	int	err;

	err = devpts_xattr_register(DEVPTS_XATTR_INDEX_SECURITY,
				    &devpts_xattr_security_handler);
	if (err)
		return err;
#endif

	return 0;
}

void
exit_devpts_xattr(void)
{
#ifdef CONFIG_DEVPTS_FS_SECURITY	
	devpts_xattr_unregister(DEVPTS_XATTR_INDEX_SECURITY,
				&devpts_xattr_security_handler);
#endif

}
