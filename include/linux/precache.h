#ifndef _LINUX_PRECACHE_H

#include <linux/fs.h>
#include <linux/mm.h>

#ifdef CONFIG_PRECACHE
extern void precache_init(struct super_block *sb);
extern void shared_precache_init(struct super_block *sb, char *uuid);
extern int precache_get(struct address_space *mapping, unsigned long index,
	       struct page *empty_page);
extern int precache_put(struct address_space *mapping, unsigned long index,
		struct page *page);
extern int precache_flush(struct address_space *mapping, unsigned long index);
extern int precache_flush_inode(struct address_space *mapping);
extern int precache_flush_filesystem(struct super_block *s);
#else
static inline void precache_init(struct super_block *sb)
{
}

static inline void shared_precache_init(struct super_block *sb, char *uuid)
{
}

static inline int precache_get(struct address_space *mapping,
		unsigned long index, struct page *empty_page)
{
	return 0;
}

static inline int precache_put(struct address_space *mapping,
		unsigned long index, struct page *page)
{
	return 0;
}

static inline int precache_flush(struct address_space *mapping,
		unsigned long index)
{
	return 0;
}

static inline int precache_flush_inode(struct address_space *mapping)
{
	return 0;
}

static inline int precache_flush_filesystem(struct super_block *s)
{
	return 0;
}
#endif

#define _LINUX_PRECACHE_H
#endif /* _LINUX_PRECACHE_H */
