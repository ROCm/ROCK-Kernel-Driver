/*
  The ts_kcompat module contains backported code from the Linux kernel
  licensed under the GNU General Public License (GPL) Version 2.  All
  code is copyrighted by its authors.

  $Id: ts_kcompat.h 68 2004-04-21 00:03:57Z roland $
*/

#ifndef _TS_KCOMPAT_H
#define _TS_KCOMPAT_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,kcompat_export.ver)
#endif

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/module.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,20)
static inline void cond_resched(void)
{
	if (current->need_resched) {
		set_current_state(TASK_RUNNING);
		schedule();
	}

}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,23)
static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
  return inode->u.generic_ip;
}
#endif

/*
  Linux kernel 2.4.15 introduced the seq_file library, which
  simplifies /proc file handling.
*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,15)
#  define TS_KCOMPAT_PROVIDE_SEQ_FILE 1
#  include <linux/fs.h>

struct seq_operations;

struct seq_file {
	char *buf;
	size_t size;
	size_t from;
	size_t count;
	loff_t index;
	struct semaphore sem;
	struct seq_operations *op;
	void *private;
};

struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};

int seq_open(struct file *, struct seq_operations *);
ssize_t seq_read(struct file *, char *, size_t, loff_t *);
loff_t seq_lseek(struct file *, loff_t, int);
int seq_release(struct inode *, struct file *);
int seq_escape(struct seq_file *, const char *, const char *);

static inline int seq_putc(struct seq_file *m, char c)
{
	if (m->count < m->size) {
		m->buf[m->count++] = c;
		return 0;
	}
	return -1;
}

static inline int seq_puts(struct seq_file *m, const char *s)
{
	int len = strlen(s);
	if (m->count + len < m->size) {
		memcpy(m->buf + m->count, s, len);
		m->count += len;
		return 0;
	}
	m->count = m->size;
	return -1;
}

int seq_printf(struct seq_file *, const char *, ...)
	__attribute__ ((format (printf,2,3)));

#else /* 2.4.15 seq_file */
#  include <linux/fs.h>
#  include <linux/seq_file.h>
#endif /* 2.4.15 seq_file */

/*
  Linux kernel 2.4.10 introduced min/max macros that do strict type
  checking.  Make sure we use that version.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#undef min
#undef max

#define min(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })
/*
  And another def to override the type check for those who "know better."
  This is only here, because the compiler can barf on enum values.
 */
#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif /* 2.4.10 min/max */

/*
  snprintf()/vsnprintf() and sscanf()/vsscanf() first appeared in
  Linux kernel 2.4.10, but the implementation of vsscanf is still
  buggy as of 2.4.17.
*/
#define TS_KCOMPAT_PROVIDE_VSSCANF 1

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#define TS_KCOMPAT_PROVIDE_VSNPRINTF 1
#endif /* 2.4.10 */

#if defined(TS_KCOMPAT_PROVIDE_VSNPRINTF)
#include <linux/types.h>

extern int ts_snprintf(char * buf, size_t size, const char *fmt, ...)
     __attribute__ ((format (printf, 3, 4)));
extern int ts_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

#undef snprintf
#undef vsnprintf
#define snprintf ts_snprintf
#define vsnprintf ts_vsnprintf
#endif /* TS_KCOMPAT_PROVIDE_VSNPRINTF */

#if defined(TS_KCOMPAT_PROVIDE_VSSCANF)
extern int ts_sscanf(const char *, const char *, ...)
     __attribute__ ((format (scanf,2,3)));
extern int ts_vsscanf(const char *, const char *, va_list);

#undef sscanf
#undef vsscanf
#define sscanf ts_sscanf
#define vsscanf ts_vsscanf
#endif /* TS_KCOMPAT_PROVIDE_VSSCANF */

/* Linux kernel 2.4.7 introduced complete_and_exit() et al. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)
#  include <asm/semaphore.h>
#  undef DECLARE_COMPLETION
#  undef init_completion
#  undef complete_and_exit
#  undef wait_for_completion
#  undef completion
#  define DECLARE_COMPLETION(x)   DECLARE_MUTEX_LOCKED(x)
#  define init_completion(x) init_MUTEX_LOCKED(x)
#  define complete_and_exit(x, y) up_and_exit((x), (y))
#  define wait_for_completion(x) down(x)
#  define completion semaphore
#else /* 2.4.7 */
#  include <linux/completion.h>
#endif /* 2.4.7 */

/* Linux kernel 2.4.10 introduced reparent_to_init() */
/* RHAS 2.4.9 kernel has reparent_to_init. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,9)
#  undef reparent_to_init
#  define reparent_to_init() do { } while (0)
#endif /* 2.4.10 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)
#endif /* 2.4.10 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,17)
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head); \
        	pos = pos->prev, prefetch(pos->prev))
#endif /* 2.4.17 */

/* No 2.4 kernel has try_module_get or module_put */
static inline int try_module_get(struct module *module)
{
	if (module && !try_inc_mod_count(module))
		return 0;
	return 1;
}

static inline void module_put(struct module *module)
{
	if (module)
		__MOD_DEC_USE_COUNT(module);
}

/* Linux 2.4.10 introduced a red-black tree implementation */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#  define TS_KCOMPAT_PROVIDE_RBTREE 1

/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...

  Some example of insert and search follows here. The search is a plain
  normal search over an ordered tree. The insert instead must be implemented
  int two steps: as first thing the code must insert the element in
  order as a red leaf in the tree, then the support library function
  rb_insert_color() must be called. Such function will do the
  not trivial work to rebalance the rbtree if necessary.

-----------------------------------------------------------------------
static inline struct page * rb_search_page_cache(struct inode * inode,
						 unsigned long offset)
{
	rb_node_t * n = inode->i_rb_page_cache.rb_node;
	struct page * page;

	while (n)
	{
		page = rb_entry(n, struct page, rb_page_cache);

		if (offset < page->offset)
			n = n->rb_left;
		else if (offset > page->offset)
			n = n->rb_right;
		else
			return page;
	}
	return NULL;
}

static inline struct page * __rb_insert_page_cache(struct inode * inode,
						   unsigned long offset,
						   rb_node_t * node)
{
	rb_node_t ** p = &inode->i_rb_page_cache.rb_node;
	rb_node_t * parent = NULL;
	struct page * page;

	while (*p)
	{
		parent = *p;
		page = rb_entry(parent, struct page, rb_page_cache);

		if (offset < page->offset)
			p = &(*p)->rb_left;
		else if (offset > page->offset)
			p = &(*p)->rb_right;
		else
			return page;
	}

	rb_link_node(node, parent, p);

	return NULL;
}

static inline struct page * rb_insert_page_cache(struct inode * inode,
						 unsigned long offset,
						 rb_node_t * node)
{
	struct page * ret;
	if ((ret = __rb_insert_page_cache(inode, offset, node)))
		goto out;
	rb_insert_color(node, &inode->i_rb_page_cache);
 out:
	return ret;
}
-----------------------------------------------------------------------
*/

#include <linux/stddef.h>

typedef struct rb_node_s
{
	struct rb_node_s * rb_parent;
	int rb_color;
#define	RB_RED		0
#define	RB_BLACK	1
	struct rb_node_s * rb_right;
	struct rb_node_s * rb_left;
}
rb_node_t;

typedef struct rb_root_s
{
	struct rb_node_s * rb_node;
}
rb_root_t;

#define RB_ROOT	(rb_root_t) { NULL, }
#define	rb_entry(ptr, type, member)					\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

extern void rb_insert_color(rb_node_t *, rb_root_t *);
extern void rb_erase(rb_node_t *, rb_root_t *);

static inline void rb_link_node(rb_node_t * node, rb_node_t * parent, rb_node_t ** rb_link)
{
	node->rb_parent = parent;
	node->rb_color = RB_RED;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}

#else
#include <linux/rbtree.h>

# if defined(TS_host_i386_2_4_18_3_redhat) || \
     defined(TS_host_i386_smp_2_4_18_3_redhat) || \
     defined(TS_host_i386_2_4_18_10_redhat) || \
     defined(TS_host_i386_smp_2_4_18_10_redhat) || \
     defined(TS_host_ia64_2_4_18_e31_rhas) || \
     defined(TS_host_ia64_smp_2_4_18_e31_rhas) || \
     defined(TS_host_ia64_2_4_18_e37_rhas) || \
     defined(TS_host_ia64_smp_2_4_18_e37_rhas) || \
     defined(TS_host_ia64_2_4_18_e41_rhas) || \
     defined(TS_host_ia64_smp_2_4_18_e41_rhas)
#  define TS_KCOMPAT_PROVIDE_RBTREE 1
/*
 * Bug #2935. Workaround problem with some RHL 7.x/RHAS 2.1 kernels. They
 * have rbtree code support, but the symbols aren't exported so modules can
 * use them. We could avoid this by not including rbtree.h, but it is
 * indirectly included with header files we do need. So as a result, we use
 * a macro to rename the usage here for our copy of the functions
 */
#  define rb_insert_color __ts_rb_insert_color
#  define rb_erase __ts_rb_erase
extern void rb_insert_color(rb_node_t *, rb_root_t *);
extern void rb_erase(rb_node_t *, rb_root_t *);
# endif
#endif

/* Proposed patch, not in any 2.4 Linux kernel */
extern void rb_replace_node(rb_node_t *victim, rb_node_t *new, rb_root_t *root);

#endif /* _TS_KCOMPAT_H */
