/*
 * include/linux/backing-dev.h
 *
 * low-level device information and state which is propagated up through
 * to high-level code.
 */

#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <asm/atomic.h>

/*
 * Bits in backing_dev_info.state
 */
enum bdi_state {
	BDI_pdflush,		/* A pdflush thread is working this device */
	BDI_write_congested,	/* The write queue is getting full */
	BDI_read_congested,	/* The read queue is getting full */
	BDI_unused,		/* Available bits start here */
};

struct backing_dev_info {
	unsigned long ra_pages;	/* max readahead in PAGE_CACHE_SIZE units */
	unsigned long state;	/* Always use atomic bitops on this */
	int memory_backed;	/* Cannot clean pages with writepage */
};

extern struct backing_dev_info default_backing_dev_info;

int writeback_acquire(struct backing_dev_info *bdi);
int writeback_in_progress(struct backing_dev_info *bdi);
void writeback_release(struct backing_dev_info *bdi);

static inline int bdi_read_congested(struct backing_dev_info *bdi)
{
	return test_bit(BDI_read_congested, &bdi->state);
}

static inline int bdi_write_congested(struct backing_dev_info *bdi)
{
	return test_bit(BDI_write_congested, &bdi->state);
}

#endif		/* _LINUX_BACKING_DEV_H */
