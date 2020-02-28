#ifndef _LIBRBD_H
#define _LIBRBD_H

#include <linux/blk-mq.h>
#include <linux/device.h>
#include <linux/ceph/striper.h>
#include <linux/ceph/osdmap.h>

#define RBD_DRV_NAME "rbd"

/*
 * An RBD device name will be "rbd#", where the "rbd" comes from
 * RBD_DRV_NAME above, and # is a unique integer identifier.
 */
#define DEV_NAME_LEN		32

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	/* These six fields never change for a given rbd image */
	char *object_prefix;
	__u8 obj_order;
	u64 stripe_unit;
	u64 stripe_count;
	s64 data_pool_id;
	u64 features;		/* Might be changeable someday? */

	/* The remaining fields need to be updated occasionally */
	u64 image_size;
	struct ceph_snap_context *snapc;
	char *snap_names;	/* format 1 only */
	u64 *snap_sizes;	/* format 1 only */
};

/*
 * An rbd image specification.
 *
 * The tuple (pool_id, image_id, snap_id) is sufficient to uniquely
 * identify an image.  Each rbd_dev structure includes a pointer to
 * an rbd_spec structure that encapsulates this identity.
 *
 * Each of the id's in an rbd_spec has an associated name.  For a
 * user-mapped image, the names are supplied and the id's associated
 * with them are looked up.  For a layered image, a parent image is
 * defined by the tuple, and the names are looked up.
 *
 * An rbd_dev structure contains a parent_spec pointer which is
 * non-null if the image it represents is a child in a layered
 * image.  This pointer will refer to the rbd_spec structure used
 * by the parent rbd_dev for its own identity (i.e., the structure
 * is shared between the parent and child).
 *
 * Since these structures are populated once, during the discovery
 * phase of image construction, they are effectively immutable so
 * we make no effort to synchronize access to them.
 *
 * Note that code herein does not assume the image name is known (it
 * could be a null pointer).
 */
struct rbd_spec {
	u64		pool_id;
	const char	*pool_name;
	const char	*pool_ns;	/* NULL if default, never "" */

	const char	*image_id;
	const char	*image_name;

	u64		snap_id;
	const char	*snap_name;

	struct kref	kref;
};

struct pending_result {
	int			result;		/* first nonzero result */
	int			num_pending;
};

struct rbd_img_request;

enum obj_request_type {
	OBJ_REQUEST_NODATA = 1,
	OBJ_REQUEST_BIO,	/* pointer into provided bio (list) */
	OBJ_REQUEST_BVECS,	/* pointer into provided bio_vec array */
	OBJ_REQUEST_OWN_BVECS,	/* private bio_vec array, doesn't own pages */
};

enum obj_operation_type {
	OBJ_OP_READ = 1,
	OBJ_OP_WRITE,
	OBJ_OP_DISCARD,
	OBJ_OP_ZEROOUT,
};

enum rbd_img_state {
	RBD_IMG_START = 1,
	RBD_IMG_EXCLUSIVE_LOCK,
	__RBD_IMG_OBJECT_REQUESTS,
	RBD_IMG_OBJECT_REQUESTS,
};

struct rbd_obj_request;
typedef void (*rbd_img_request_end_cb_t)(struct rbd_img_request *img_request,
					 int result);

struct rbd_img_request {
	struct rbd_device	*rbd_dev;
	enum obj_operation_type	op_type;
	enum obj_request_type	data_type;
	unsigned long		flags;
	enum rbd_img_state	state;
	union {
		u64			snap_id;	/* for reads */
		struct ceph_snap_context *snapc;	/* for writes */
	};
	union {
		struct request		*rq;		/* block request */
		struct rbd_obj_request	*obj_request;	/* obj req initiator */
		void			*lio_cmd_data;	/* lio specific data */
	};
	rbd_img_request_end_cb_t callback;

	struct list_head	lock_item;
	struct list_head	object_extents;	/* obj_req.ex structs */

	struct mutex		state_mutex;
	struct pending_result	pending;
	struct work_struct	work;
	int			work_result;
	struct kref		kref;
};

enum rbd_watch_state {
	RBD_WATCH_STATE_UNREGISTERED,
	RBD_WATCH_STATE_REGISTERED,
	RBD_WATCH_STATE_ERROR,
};

enum rbd_lock_state {
	RBD_LOCK_STATE_UNLOCKED,
	RBD_LOCK_STATE_LOCKED,
	RBD_LOCK_STATE_RELEASING,
};

/* WatchNotify::ClientId */
struct rbd_client_id {
	u64 gid;
	u64 handle;
};

struct rbd_mapping {
	u64                     size;
};

/*
 * a single device
 */
struct rbd_device {
	int			dev_id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	int			minor;
	struct gendisk		*disk;		/* blkdev's gendisk and rq */

	u32			image_format;	/* Either 1 or 2 */
	struct rbd_client	*rbd_client;

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. rbd3 */

	spinlock_t		lock;		/* queue, flags, open_count */

	struct rbd_image_header	header;
	unsigned long		flags;		/* possibly lock protected */
	struct rbd_spec		*spec;
	struct rbd_options	*opts;
	char			*config_info;	/* add{,_single_major} string */

	struct ceph_object_id	header_oid;
	struct ceph_object_locator header_oloc;

	struct ceph_file_layout	layout;		/* used for all rbd requests */

	struct mutex		watch_mutex;
	enum rbd_watch_state	watch_state;
	struct ceph_osd_linger_request *watch_handle;
	u64			watch_cookie;
	struct delayed_work	watch_dwork;

	struct rw_semaphore	lock_rwsem;
	enum rbd_lock_state	lock_state;
	char			lock_cookie[32];
	struct rbd_client_id	owner_cid;
	struct work_struct	acquired_lock_work;
	struct work_struct	released_lock_work;
	struct delayed_work	lock_dwork;
	struct work_struct	unlock_work;
	spinlock_t		lock_lists_lock;
	struct list_head	acquiring_list;
	struct list_head	running_list;
	struct completion	acquire_wait;
	int			acquire_err;
	struct completion	releasing_wait;

	spinlock_t		object_map_lock;
	u8			*object_map;
	u64			object_map_size;	/* in objects */
	u64			object_map_flags;

	struct workqueue_struct	*task_wq;

	struct rbd_spec		*parent_spec;
	u64			parent_overlap;
	atomic_t		parent_ref;
	struct rbd_device	*parent;

	/* Block layer tags. */
	struct blk_mq_tag_set	tag_set;

	/* protects updating the header */
	struct rw_semaphore     header_rwsem;

	struct rbd_mapping	mapping;

	struct list_head	node;

	/* sysfs related */
	struct device		dev;
	unsigned long		open_count;	/* protected by lock */
};

/*
 * Flag bits for rbd_dev->flags:
 * - REMOVING (which is coupled with rbd_dev->open_count) is protected
 *   by rbd_dev->lock
 */
enum rbd_dev_flags {
	RBD_DEV_FLAG_EXISTS,	/* rbd_dev_device_setup() ran */
	RBD_DEV_FLAG_REMOVING,	/* this mapping is being removed */
	RBD_DEV_FLAG_READONLY,	/* -o ro or snapshot */
};

extern struct rbd_img_request *rbd_img_request_create(
					struct rbd_device *rbd_dev,
					enum obj_operation_type op_type,
					struct ceph_snap_context *snapc,
					rbd_img_request_end_cb_t end_cb);
extern int rbd_img_fill_nodata(struct rbd_img_request *img_req,
			       u64 off, u64 len);
extern int rbd_img_fill_from_bvecs(struct rbd_img_request *img_req,
				   struct ceph_file_extent *img_extents,
				   u32 num_img_extents,
				   struct bio_vec *bvecs);
extern void rbd_img_handle_request(struct rbd_img_request *img_req, int result);
extern void rbd_img_request_put(struct rbd_img_request *img_request);

#endif
