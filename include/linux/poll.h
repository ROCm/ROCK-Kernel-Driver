#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H

#include <asm/poll.h>

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#define POLL_INLINE_BYTES 256
#define FAST_SELECT_MAX  128
#define FAST_POLL_MAX    128
#define POLL_INLINE_ENTRIES (1+(POLL_INLINE_BYTES / sizeof(struct poll_table_entry)))

struct poll_table_entry {
	struct file * filp;
	wait_queue_t wait;
	wait_queue_head_t * wait_address;
};

struct poll_table_page {
	struct poll_table_page * next;
	struct poll_table_entry * entry;
	struct poll_table_entry entries[0];
};

typedef struct poll_table_struct {
	int error;
	struct poll_table_page * table;
	struct poll_table_page inline_page; 
	struct poll_table_entry inline_table[POLL_INLINE_ENTRIES]; 
} poll_table;

#define POLL_INLINE_TABLE_LEN (sizeof(poll_table) - offsetof(poll_table, inline_page))

extern void __pollwait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p);

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && wait_address)
		__pollwait(filp, wait_address, p);
}

static inline void poll_initwait(poll_table* pt)
{
	pt->error = 0;
	pt->table = NULL;
}

extern void poll_freewait(poll_table* pt);


/*
 * Scaleable version of the fd_set.
 */

typedef struct {
	unsigned long *in, *out, *ex;
	unsigned long *res_in, *res_out, *res_ex;
} fd_set_bits;

/*
 * How many longwords for "nr" bits?
 */
#define FDS_BITPERLONG	(8*sizeof(long))
#define FDS_LONGS(nr)	(((nr)+FDS_BITPERLONG-1)/FDS_BITPERLONG)
#define FDS_BYTES(nr)	(FDS_LONGS(nr)*sizeof(long))

static inline
void set_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	if (ufdset)
		__copy_to_user(ufdset, fdset, FDS_BYTES(nr));
}

extern int do_select(int n, fd_set_bits *fds, long *timeout);

#endif /* KERNEL */

#endif /* _LINUX_POLL_H */
