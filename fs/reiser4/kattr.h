/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Interface to sysfs' attributes. See kattr.c for comments */

#if !defined( __REISER4_KATTR_H__ )
#define __REISER4_KATTR_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/fs.h>

/* fixme: access to sysfs files may cause deadlock. Do not turn for now */
#define REISER4_USE_SYSFS (1)

#if REISER4_USE_SYSFS

/* helper macros used by kattr code to output information into buffer without
 * caring about overflow checking. */
#define KATTR_LEFT(p, buf) (PAGE_SIZE - (p - buf) - 1)
#define KATTR_PRINT(p, buf, ...)				\
({ 								\
	p += snprintf(p, KATTR_LEFT(p, buf) , ## __VA_ARGS__); 	\
})

struct super_block;
struct reiser4_kattr;
typedef struct reiser4_kattr reiser4_kattr;

/*
 * reiser4_kattr represents a sysfs-exported attribute of reiser4 file system.
 */
struct reiser4_kattr {
	struct fs_kattr attr; /* file-system attribute used to interact with
			       * sysfs */
	void  *cookie;        /* parameter used to avoid code duplication. See
			       * kattr.c for explanation. */
};

extern struct kobj_type ktype_reiser4;

#else

struct reiser4_kattr {
};

typedef struct reiser4_kattr reiser4_kattr;
#endif /* REISER4_USE_SYSFS */

extern int reiser4_sysfs_init_once(void);
extern void reiser4_sysfs_done_once(void);

extern int  reiser4_sysfs_init(struct super_block *super);
extern void reiser4_sysfs_done(struct super_block *super);

/* __REISER4_KATTR_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
