#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

typedef int (elevator_merge_fn) (request_queue_t *, struct list_head **,
				 struct bio *);

typedef void (elevator_merge_req_fn) (request_queue_t *, struct request *, struct request *);

typedef void (elevator_merged_fn) (request_queue_t *, struct request *);

typedef struct request *(elevator_next_req_fn) (request_queue_t *);

typedef void (elevator_add_req_fn) (request_queue_t *, struct request *, struct list_head *);
typedef int (elevator_queue_empty_fn) (request_queue_t *);
typedef void (elevator_remove_req_fn) (request_queue_t *, struct request *);
typedef struct list_head *(elevator_get_sort_head_fn) (request_queue_t *, struct request *);

typedef int (elevator_init_fn) (request_queue_t *, elevator_t *);
typedef void (elevator_exit_fn) (request_queue_t *, elevator_t *);

struct elevator_s
{
	elevator_merge_fn *elevator_merge_fn;
	elevator_merged_fn *elevator_merged_fn;
	elevator_merge_req_fn *elevator_merge_req_fn;

	elevator_next_req_fn *elevator_next_req_fn;
	elevator_add_req_fn *elevator_add_req_fn;
	elevator_remove_req_fn *elevator_remove_req_fn;

	elevator_queue_empty_fn *elevator_queue_empty_fn;
	elevator_get_sort_head_fn *elevator_get_sort_head_fn;

	elevator_init_fn *elevator_init_fn;
	elevator_exit_fn *elevator_exit_fn;

	void *elevator_data;
};

/*
 * block elevator interface
 */
extern void __elv_add_request(request_queue_t *, struct request *,
			      struct list_head *);
extern int elv_merge(request_queue_t *, struct list_head **, struct bio *);
extern void elv_merge_requests(request_queue_t *, struct request *,
			       struct request *);
extern void elv_merged_request(request_queue_t *, struct request *);
extern void elv_remove_request(request_queue_t *, struct request *);
extern int elv_queue_empty(request_queue_t *);
extern inline struct list_head *elv_get_sort_head(request_queue_t *, struct request *);

/*
 * noop I/O scheduler. always merges, always inserts new request at tail
 */
extern elevator_t elevator_noop;

/*
 * deadline i/o scheduler. uses request time outs to prevent indefinite
 * starvation
 */
extern elevator_t iosched_deadline;

/*
 * use the /proc/iosched interface, all the below is history ->
 */
typedef struct blkelv_ioctl_arg_s {
	int queue_ID;
	int read_latency;
	int write_latency;
	int max_bomb_segments;
} blkelv_ioctl_arg_t;
#define BLKELVGET   _IOR(0x12,106,sizeof(blkelv_ioctl_arg_t))
#define BLKELVSET   _IOW(0x12,107,sizeof(blkelv_ioctl_arg_t))

extern int elevator_init(request_queue_t *, elevator_t *);
extern void elevator_exit(request_queue_t *);
extern inline int bio_rq_in_between(struct bio *, struct request *, struct list_head *);
extern inline int elv_rq_merge_ok(struct request *, struct bio *);
extern inline int elv_try_merge(struct request *, struct bio *);
extern inline int elv_try_last_merge(request_queue_t *, struct bio *);

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

#endif
