#ifndef _LINUX_FSHOOKS_H
#define _LINUX_FSHOOKS_H

/*
 * This file has definitions for file system hooks.
 */

#include <linux/config.h>

#ifdef CONFIG_FSHOOKS

#include <linux/rwsem.h>

enum FShook {
#define FSHOOK_DEFINE(type, fields...) FSHOOK_##type,
#include <linux/fshooks.h>
	fshook_COUNT /* keep this last */
};

/* for the lack of a kernel-wide definition */
typedef enum {
	false,
	true
} __attribute__((__mode__(__QI__))) boolean_t;

typedef union fshook_info {
	struct fshook_generic_info {
		enum FShook type;
		int result;
	} const *gen;
#define FSHOOK_DEFINE(type, fields...) \
	struct fshook_##type##_info { \
		struct fshook_generic_info gen; \
		fields \
	} const *type;
#include <linux/fshooks.h>
} fshook_info_t __attribute__((__transparent_union__));

typedef int fshook_pre_t(fshook_info_t info, void *ctx);
typedef void fshook_post_t(fshook_info_t info, void *ctx);

int fshook_register(enum FShook, fshook_pre_t *, fshook_post_t *, void *);
int fshook_deregister(enum FShook, fshook_pre_t *, fshook_post_t *, void *);

#ifndef MODULE

struct fshook {
	struct fshook *next;
	void *ctx;
	fshook_pre_t *pre;
	fshook_post_t *post;
};

struct fshook_list {
	struct fshook *first;
	struct fshook *last;
	struct rw_semaphore lock;
};

extern struct fshook_list fshooks[fshook_COUNT];

member_type(struct fshook_generic_info, result) fshook_run_pre(enum FShook type, fshook_info_t info);
void fshook_run_post(fshook_info_t info, member_type(struct fshook_generic_info, result) result);

/* there must not be semicolons after the invocations of FSHOOK_BEGN/FSHOOK_END */
#define FSHOOK_BEGIN(type, result, args...) { \
		struct fshook_##type##_info info = { args }; \
		\
		if (!fshooks[FSHOOK_##type].first \
		   || !(result = (__typeof__(result))fshook_run_pre(FSHOOK_##type, &info))) {

#define FSHOOK_END(type, result, errcode...) \
			(void)(&info != (struct fshook_##type##_info *)-1L); \
		} \
		else { \
			errcode; \
		} \
		if (fshooks[FSHOOK_##type].first) \
			fshook_run_post(&info, result); \
	}

#define FSHOOK_BEGIN_USER_WALK_COMMON(type, err, walk, args...) { \
		struct fshook_##type##_info info = { args }; \
		if (!(err = walk) \
		    && (!fshooks[FSHOOK_##type].first \
		        || !(err = (__typeof__(err))fshook_run_pre(FSHOOK_##type, &info)))) {

#define FSHOOK_BEGIN_USER_WALK(type, err, path, flags, nd, field, args...) \
		FSHOOK_BEGIN_USER_WALK_COMMON(type, err, __user_walk(path, flags, &nd, &info.field), args)

#define FSHOOK_BEGIN_USER_PATH_WALK(type, err, path, nd, field, args...) \
		FSHOOK_BEGIN_USER_WALK_COMMON(type, err, __user_walk(path, LOOKUP_FOLLOW, &nd, &info.field), args)

#define FSHOOK_BEGIN_USER_PATH_WALK_LINK(type, err, path, nd, field, args...) \
		FSHOOK_BEGIN_USER_WALK_COMMON(type, err, __user_walk(path, 0, &nd, &info.field), args)

#define FSHOOK_END_USER_WALK(type, err, field) \
			(void)(&info != (struct fshook_##type##_info *)-1L); \
		} \
		else if (!IS_ERR(info.field) && fshooks[FSHOOK_##type].first) { \
			__typeof__(err) fshook_err = (__typeof__(err))fshook_run_pre(FSHOOK_##type, &info); \
			\
			/* simulate normal (hooks precede path resolution) sequence of operation */ \
			if (fshook_err) err = fshook_err; \
		} \
		if (!IS_ERR(info.field)) { \
			if (fshooks[FSHOOK_##type].first) \
				fshook_run_post(&info, err); \
			putname(info.field); \
		} \
	}

#endif /* MODULE */

#else /* ndef CONFIG_FSHOOKS */

#ifndef MODULE

#define FSHOOK_BEGIN(type, result, args...) {

#define FSHOOK_END(type, result) ((void)0);}

#define FSHOOK_BEGIN_USER_WALK(type, err, path, flags, nd, field, args...) \
	if (!(err = __user_walk(path, flags, &nd, 0))) {

#define FSHOOK_BEGIN_USER_PATH_WALK(type, err, path, nd, field, args...) \
	if (!(err = user_path_walk(path, &nd))) {

#define FSHOOK_BEGIN_USER_PATH_WALK_LINK(type, err, path, nd, field, args...) \
	if (!(err = user_path_walk_link(path, &nd))) {

#define FSHOOK_END_USER_WALK(type, err, field) ((void)0);}

#endif /* MODULE */

#endif /* CONFIG_FSHOOKS */

#elif defined(FSHOOK_DEFINE)

FSHOOK_DEFINE(access,
	const char *path;
	int mode;
)

FSHOOK_DEFINE(chdir,
	const char *dirname;
)

FSHOOK_DEFINE(chmod,
	const char *path;
	mode_t mode;
	boolean_t link;
)

FSHOOK_DEFINE(chown,
	const char *path;
	uid_t uid;
	gid_t gid;
	boolean_t link;
)

FSHOOK_DEFINE(chroot,
	const char *path;
)

FSHOOK_DEFINE(close,
	int fd;
)

FSHOOK_DEFINE(fchdir,
	int fd;
)

FSHOOK_DEFINE(fchmod,
	int fd;
	mode_t mode;
)

FSHOOK_DEFINE(fchown,
	int fd;
	uid_t uid;
	gid_t gid;
)

FSHOOK_DEFINE(fgetxattr,
	int fd;
	const char __user *name;
)

FSHOOK_DEFINE(flistxattr,
	int fd;
)

FSHOOK_DEFINE(frmxattr,
	int fd;
	const char __user *name;
)

FSHOOK_DEFINE(fsetxattr,
	int fd;
	const char __user *name;
	const void __user *value;
	size_t size;
	unsigned flags;
)

FSHOOK_DEFINE(fstat,
	int fd;
)

FSHOOK_DEFINE(fstatfs,
	int fd;
)

FSHOOK_DEFINE(ftruncate,
	int fd;
	loff_t length;
)

FSHOOK_DEFINE(getxattr,
	const char *path;
	const char __user *name;
	boolean_t link;
)

FSHOOK_DEFINE(ioctl,
	int fd;
	unsigned cmd;
	union {
		unsigned long value;
		void __user *ptr;
	} arg;
)

FSHOOK_DEFINE(link,
	const char *oldpath;
	const char *newpath;
)

FSHOOK_DEFINE(listxattr,
	const char *path;
	boolean_t link;
)

FSHOOK_DEFINE(mkdir,
	const char *dirname;
	mode_t mode;
)

FSHOOK_DEFINE(mknod,
	const char *path;
	mode_t mode;
	dev_t dev;
)

FSHOOK_DEFINE(mount,
	const char *devname;
	const char *dirname;
	const char *type;
	unsigned long flags;
	const void *data;
)

FSHOOK_DEFINE(open,
	const char *filename;
	int flags;
	int mode;
)

FSHOOK_DEFINE(readlink,
	const char *path;
)

FSHOOK_DEFINE(rmxattr,
	const char *path;
	const char __user *name;
	boolean_t link;
)

FSHOOK_DEFINE(rename,
	const char *oldpath;
	const char *newpath;
)

FSHOOK_DEFINE(rmdir,
	const char *dirname;
)

FSHOOK_DEFINE(setxattr,
	const char *path;
	const char __user *name;
	const void __user *value;
	size_t size;
	unsigned flags;
	boolean_t link;
)

FSHOOK_DEFINE(stat,
	const char *path;
	boolean_t link;
)

FSHOOK_DEFINE(statfs,
	const char *path;
)

FSHOOK_DEFINE(symlink,
	const char *oldpath;
	const char *newpath;
)

FSHOOK_DEFINE(truncate,
	const char *filename;
	loff_t length;
)

FSHOOK_DEFINE(umount,
	const char *dirname;
	int flags;
)

FSHOOK_DEFINE(unlink,
	const char *filename;
)

FSHOOK_DEFINE(utimes,
	const char *path;
	const struct timeval *atime;
	const struct timeval *mtime;
)

#undef FSHOOK_DEFINE

#endif /* _LINUX_FSHOOKS_H / FSHOOK_DEFINE */
