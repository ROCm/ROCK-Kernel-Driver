#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/linkage.h>
#include <linux/string.h>

struct vfsmount;
struct nameidata;

/* intent opcodes */
#define IT_OPEN     (1)
#define IT_CREAT    (1<<1)
#define IT_READDIR  (1<<2)
#define IT_GETATTR  (1<<3)
#define IT_LOOKUP   (1<<4)
#define IT_UNLINK   (1<<5)
#define IT_TRUNC    (1<<6)
#define IT_GETXATTR (1<<7)

struct lustre_intent_data {
	int       it_disposition;
	int       it_status;
	__u64     it_lock_handle;
	void     *it_data;
	int       it_lock_mode;
};

#define INTENT_MAGIC 0x19620323
struct lookup_intent {
	int     it_magic;
	void    (*it_op_release)(struct lookup_intent *);
	int     it_op;
	int	it_flags;
	int	it_create_mode;
	union {
		struct lustre_intent_data lustre;
	} d;
};

static inline void intent_init(struct lookup_intent *it, int op)
{
	memset(it, 0, sizeof(*it));
	it->it_magic = INTENT_MAGIC;
	it->it_op = op;
}

struct nameidata {
	struct dentry	*dentry;
	struct vfsmount *mnt;
	struct qstr	last;
	unsigned int	flags;
	int		last_type;
	struct lookup_intent intent;
};

/*
 * Type of the last component on LOOKUP_PARENT
 */
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path compnents" flag
 *  - locked when lookup done with dcache_lock held
 */
#define LOOKUP_FOLLOW		 1
#define LOOKUP_DIRECTORY	 2
#define LOOKUP_CONTINUE		 4
#define LOOKUP_PARENT		16
#define LOOKUP_NOALT		32
#define LOOKUP_LAST		 (1<<6)
#define LOOKUP_LINK_NOTLAST	 (1<<7)

/*
 * Intent data
 */
#define LOOKUP_OPEN		(0x0100)
#define LOOKUP_CREATE		(0x0200)
#define LOOKUP_ACCESS		(0x0400)

extern int FASTCALL(__user_walk(const char __user *, unsigned, struct nameidata *, const char **));
extern int FASTCALL(__user_walk_it(const char __user *, unsigned, struct nameidata *, const char **));
#define user_path_walk_it(name,nd) \
	__user_walk_it(name, LOOKUP_FOLLOW, nd, 0)
#define user_path_walk_link_it(name,nd) \
	__user_walk_it(name, 0, nd, 0)
extern void intent_release(struct lookup_intent *);
#define user_path_walk(name,nd) \
	__user_walk(name, LOOKUP_FOLLOW, nd, 0)
#define user_path_walk_link(name,nd) \
	__user_walk(name, 0, nd, 0)
extern int FASTCALL(path_lookup(const char *, unsigned, struct nameidata *));
extern int FASTCALL(path_walk(const char *, struct nameidata *));
extern int FASTCALL(link_path_walk(const char *, struct nameidata *));
extern void path_release(struct nameidata *);

extern struct dentry * lookup_one_len(const char *, struct dentry *, int);
extern struct dentry * lookup_hash(struct qstr *, struct dentry *);
extern int follow_down(struct vfsmount **, struct dentry **);
extern int follow_up(struct vfsmount **, struct dentry **);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

#endif /* _LINUX_NAMEI_H */
