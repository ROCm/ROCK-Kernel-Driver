#ifndef _BLKTAP_H_
#define _BLKTAP_H_

#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/scatterlist.h>
#include <xen/blkif.h>
#include <xen/gnttab.h>

//#define ENABLE_PASSTHROUGH

extern int blktap_debug_level;

#define BTPRINTK(level, tag, force, _f, _a...)				\
	do {								\
		if (blktap_debug_level > level &&			\
		    (force || printk_ratelimit()))			\
			printk(tag "%s: " _f, __func__, ##_a);		\
	} while (0)

#define BTDBG(_f, _a...)             BTPRINTK(8, KERN_DEBUG, 1, _f, ##_a)
#define BTINFO(_f, _a...)            BTPRINTK(0, KERN_INFO, 0, _f, ##_a)
#define BTWARN(_f, _a...)            BTPRINTK(0, KERN_WARNING, 0, _f, ##_a)
#define BTERR(_f, _a...)             BTPRINTK(0, KERN_ERR, 0, _f, ##_a)

#define MAX_BLKTAP_DEVICE            256

#define BLKTAP_CONTROL               1
#define BLKTAP_RING_FD               2
#define BLKTAP_RING_VMA              3
#define BLKTAP_DEVICE                4
#define BLKTAP_SYSFS                 5
#define BLKTAP_PAUSE_REQUESTED       6
#define BLKTAP_PAUSED                7
#define BLKTAP_SHUTDOWN_REQUESTED    8
#define BLKTAP_PASSTHROUGH           9
#define BLKTAP_DEFERRED              10

/* blktap IOCTLs: */
#define BLKTAP2_IOCTL_KICK_FE        1
#define BLKTAP2_IOCTL_ALLOC_TAP	     200
#define BLKTAP2_IOCTL_FREE_TAP       201
#define BLKTAP2_IOCTL_CREATE_DEVICE  202
#define BLKTAP2_IOCTL_SET_PARAMS     203
#define BLKTAP2_IOCTL_PAUSE          204
#define BLKTAP2_IOCTL_REOPEN         205
#define BLKTAP2_IOCTL_RESUME         206

#define BLKTAP2_MAX_MESSAGE_LEN      256

#define BLKTAP2_RING_MESSAGE_PAUSE   1
#define BLKTAP2_RING_MESSAGE_RESUME  2
#define BLKTAP2_RING_MESSAGE_CLOSE   3

#define BLKTAP_REQUEST_FREE          0
#define BLKTAP_REQUEST_PENDING       1

/*
 * The maximum number of requests that can be outstanding at any time
 * is determined by
 *
 *   [mmap_alloc * MAX_PENDING_REQS * BLKIF_MAX_SEGMENTS_PER_REQUEST] 
 *
 * where mmap_alloc < MAX_DYNAMIC_MEM.
 *
 * TODO:
 * mmap_alloc is initialised to 2 and should be adjustable on the fly via
 * sysfs.
 */
#define BLK_RING_SIZE		__RING_SIZE((blkif_sring_t *)0, PAGE_SIZE)
#define MAX_DYNAMIC_MEM		BLK_RING_SIZE
#define MAX_PENDING_REQS	BLK_RING_SIZE
#define MMAP_PAGES (MAX_PENDING_REQS * BLKIF_MAX_SEGMENTS_PER_REQUEST)
#define MMAP_VADDR(_start, _req, _seg)					\
        (_start +                                                       \
         ((_req) * BLKIF_MAX_SEGMENTS_PER_REQUEST * PAGE_SIZE) +        \
         ((_seg) * PAGE_SIZE))

#define blktap_get(_b) (atomic_inc(&(_b)->refcnt))
#define blktap_put(_b)					\
	do {						\
		if (atomic_dec_and_test(&(_b)->refcnt))	\
			wake_up(&(_b)->wq);		\
	} while (0)

struct blktap;

struct grant_handle_pair {
	grant_handle_t                 kernel;
	grant_handle_t                 user;
};
#define INVALID_GRANT_HANDLE           0xFFFF

struct blktap_handle {
	unsigned int                   ring;
	unsigned int                   device;
	unsigned int                   minor;
};

struct blktap_params {
	char                           name[BLKTAP2_MAX_MESSAGE_LEN];
	unsigned long long             capacity;
	unsigned long                  sector_size;
};

struct blktap_device {
	int                            users;
	spinlock_t                     lock;
	struct gendisk                *gd;

#ifdef ENABLE_PASSTHROUGH
	struct block_device           *bdev;
#endif
};

struct blktap_ring {
	struct vm_area_struct         *vma;
	blkif_front_ring_t             ring;
	struct vm_foreign_map          foreign_map;
	unsigned long                  ring_vstart;
	unsigned long                  user_vstart;

	int                            response;

	wait_queue_head_t              poll_wait;

	dev_t                          devno;
	struct device                 *dev;
	atomic_t                       sysfs_refcnt;
	struct mutex                   sysfs_mutex;
};

struct blktap_statistics {
	unsigned long                  st_print;
	int                            st_rd_req;
	int                            st_wr_req;
	int                            st_oo_req;
	int                            st_pk_req;
	int                            st_rd_sect;
	int                            st_wr_sect;
	s64                            st_rd_cnt;
	s64                            st_rd_sum_usecs;
	s64                            st_rd_max_usecs;
	s64                            st_wr_cnt;
	s64                            st_wr_sum_usecs;
	s64                            st_wr_max_usecs;	
};

struct blktap_request {
	uint64_t                       id;
	uint16_t                       usr_idx;

	uint8_t                        status;
	atomic_t                       pendcnt;
	uint8_t                        nr_pages;
	unsigned short                 operation;

	struct timeval                 time;
	struct grant_handle_pair       handles[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct list_head               free_list;
};

struct blktap {
	int                            minor;
	pid_t                          pid;
	atomic_t                       refcnt;
	unsigned long                  dev_inuse;

	struct blktap_params           params;

	struct rw_semaphore            tap_sem;

	struct blktap_ring             ring;
	struct blktap_device           device;

	int                            pending_cnt;
	struct blktap_request         *pending_requests[MAX_PENDING_REQS];
	struct scatterlist             sg[BLKIF_MAX_SEGMENTS_PER_REQUEST];

	wait_queue_head_t              wq;
	struct list_head               deferred_queue;

	struct blktap_statistics       stats;
};

extern struct blktap *blktaps[MAX_BLKTAP_DEVICE];

static inline int
blktap_active(struct blktap *tap)
{
	return test_bit(BLKTAP_RING_VMA, &tap->dev_inuse);
}

static inline int
blktap_validate_params(struct blktap *tap, struct blktap_params *params)
{
	/* TODO: sanity check */
	params->name[sizeof(params->name) - 1] = '\0';
	BTINFO("%s: capacity: %llu, sector-size: %lu\n",
	       params->name, params->capacity, params->sector_size);
	return 0;
}

int blktap_control_destroy_device(struct blktap *);

int blktap_ring_init(int *);
int blktap_ring_free(void);
int blktap_ring_create(struct blktap *);
int blktap_ring_destroy(struct blktap *);
int blktap_ring_pause(struct blktap *);
int blktap_ring_resume(struct blktap *);
void blktap_ring_kick_user(struct blktap *);

int blktap_sysfs_init(void);
void blktap_sysfs_free(void);
int blktap_sysfs_create(struct blktap *);
int blktap_sysfs_destroy(struct blktap *);

int blktap_device_init(int *);
void blktap_device_free(void);
int blktap_device_create(struct blktap *);
int blktap_device_destroy(struct blktap *);
int blktap_device_pause(struct blktap *);
int blktap_device_resume(struct blktap *);
void blktap_device_restart(struct blktap *);
void blktap_device_finish_request(struct blktap *,
				  blkif_response_t *,
				  struct blktap_request *);
void blktap_device_fail_pending_requests(struct blktap *);
#ifdef ENABLE_PASSTHROUGH
int blktap_device_enable_passthrough(struct blktap *,
				     unsigned, unsigned);
#endif

void blktap_defer(struct blktap *);
void blktap_run_deferred(void);

int blktap_request_pool_init(void);
void blktap_request_pool_free(void);
int blktap_request_pool_grow(void);
int blktap_request_pool_shrink(void);
struct blktap_request *blktap_request_allocate(struct blktap *);
void blktap_request_free(struct blktap *, struct blktap_request *);
unsigned long request_to_kaddr(struct blktap_request *, int);

#endif
