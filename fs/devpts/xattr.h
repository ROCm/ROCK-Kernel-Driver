/*
  File: fs/devpts/xattr.h
 
  Derived from fs/ext3/xattr.h, changed in the following ways:
      drop everything related to persistent storage of EAs
      pass dentry rather than inode to internal methods
      only presently define a handler for security modules
*/

#include <linux/config.h>
#include <linux/xattr.h>

/* Name indexes */
#define DEVPTS_XATTR_INDEX_MAX			10
#define DEVPTS_XATTR_INDEX_SECURITY	        1

# ifdef CONFIG_DEVPTS_FS_XATTR

struct devpts_xattr_handler {
	char *prefix;
	size_t (*list)(struct dentry *dentry, char *buffer);
	int (*get)(struct dentry *dentry, const char *name, void *buffer,
		   size_t size);
	int (*set)(struct dentry *dentry, const char *name, const void *buffer,
		   size_t size, int flags);
};

extern int devpts_xattr_register(int, struct devpts_xattr_handler *);
extern void devpts_xattr_unregister(int, struct devpts_xattr_handler *);

extern int devpts_setxattr(struct dentry *, const char *, const void *, size_t, int);
extern ssize_t devpts_getxattr(struct dentry *, const char *, void *, size_t);
extern ssize_t devpts_listxattr(struct dentry *, char *, size_t);
extern int devpts_removexattr(struct dentry *, const char *);

extern int init_devpts_xattr(void);
extern void exit_devpts_xattr(void);

# else  /* CONFIG_DEVPTS_FS_XATTR */
#  define devpts_setxattr		NULL
#  define devpts_getxattr		NULL
#  define devpts_listxattr	NULL
#  define devpts_removexattr	NULL

static inline int
init_devpts_xattr(void)
{
	return 0;
}

static inline void
exit_devpts_xattr(void)
{
}

# endif  /* CONFIG_DEVPTS_FS_XATTR */

extern struct devpts_xattr_handler devpts_xattr_security_handler;

