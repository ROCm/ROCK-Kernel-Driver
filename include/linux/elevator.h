#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

typedef int (elevator_merge_fn) (request_queue_t *, struct request **,
				 struct bio *);

typedef void (elevator_merge_cleanup_fn) (request_queue_t *, struct request *, int);

typedef void (elevator_merge_req_fn) (struct request *, struct request *);

typedef struct request *(elevator_next_req_fn) (request_queue_t *);

typedef void (elevator_add_req_fn) (request_queue_t *, struct request *, struct list_head *);

typedef int (elevator_init_fn) (request_queue_t *, elevator_t *);
typedef void (elevator_exit_fn) (request_queue_t *, elevator_t *);

struct elevator_s
{
	int latency[2];

	elevator_merge_fn *elevator_merge_fn;
	elevator_merge_cleanup_fn *elevator_merge_cleanup_fn;
	elevator_merge_req_fn *elevator_merge_req_fn;

	elevator_next_req_fn *elevator_next_req_fn;
	elevator_add_req_fn *elevator_add_req_fn;

	elevator_init_fn *elevator_init_fn;
	elevator_exit_fn *elevator_exit_fn;
};

int elevator_noop_merge(request_queue_t *, struct request **, struct bio *);
void elevator_noop_merge_cleanup(request_queue_t *, struct request *, int);
void elevator_noop_merge_req(struct request *, struct request *);

int elevator_linus_merge(request_queue_t *, struct request **, struct bio *);
void elevator_linus_merge_cleanup(request_queue_t *, struct request *, int);
void elevator_linus_merge_req(struct request *, struct request *);
int elv_linus_init(request_queue_t *, elevator_t *);
void elv_linus_exit(request_queue_t *, elevator_t *);
struct request *elv_next_request_fn(request_queue_t *);
void elv_add_request_fn(request_queue_t *, struct request *,struct list_head *);

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

extern int elevator_init(request_queue_t *, elevator_t *, elevator_t);
extern void elevator_exit(request_queue_t *, elevator_t *);

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

#define elevator_request_latency(e, rw)	((e)->latency[(rw) & 1])

/*
 * will change once we move to a more complex data structure than a simple
 * list for pending requests
 */
#define elv_queue_empty(q)	list_empty(&(q)->queue_head)

/*
 * elevator private data
 */
struct elv_linus_data {
	unsigned long flags;
};

#define ELV_DAT(e) ((struct elv_linus_data *)(e)->elevator_data)

#define ELV_LINUS_BACK_MERGE	1
#define ELV_LINUS_FRONT_MERGE	2

#define ELEVATOR_NOOP							\
((elevator_t) {								\
	{ 0, 0},							\
	elevator_noop_merge,		/* elevator_merge_fn */		\
	elevator_noop_merge_cleanup,	/* elevator_merge_cleanup_fn */	\
	elevator_noop_merge_req,	/* elevator_merge_req_fn */	\
	elv_next_request_fn,						\
	elv_add_request_fn,						\
	elv_linus_init,							\
	elv_linus_exit,							\
	})

#define ELEVATOR_LINUS							\
((elevator_t) {								\
	{ 8192, 16384 },						\
	elevator_linus_merge,		/* elevator_merge_fn */		\
	elevator_linus_merge_cleanup,	/* elevator_merge_cleanup_fn */	\
	elevator_linus_merge_req,	/* elevator_merge_req_fn */	\
	elv_next_request_fn,						\
	elv_add_request_fn,						\
	elv_linus_init,							\
	elv_linus_exit,							\
	})

#endif
