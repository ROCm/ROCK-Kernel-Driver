#ifndef IEEE1394_RAW1394_H
#define IEEE1394_RAW1394_H

#define RAW1394_DEVICE_MAJOR      171
#define RAW1394_DEVICE_NAME       "raw1394"

#define RAW1394_KERNELAPI_VERSION 4

/* state: opened */
#define RAW1394_REQ_INITIALIZE    1

/* state: initialized */
#define RAW1394_REQ_LIST_CARDS    2
#define RAW1394_REQ_SET_CARD      3

/* state: connected */
#define RAW1394_REQ_ASYNC_READ    100
#define RAW1394_REQ_ASYNC_WRITE   101
#define RAW1394_REQ_LOCK          102
#define RAW1394_REQ_LOCK64        103
#define RAW1394_REQ_ISO_SEND      104

#define RAW1394_REQ_ISO_LISTEN    200
#define RAW1394_REQ_FCP_LISTEN    201
#define RAW1394_REQ_RESET_BUS     202

/* kernel to user */
#define RAW1394_REQ_BUS_RESET     10000
#define RAW1394_REQ_ISO_RECEIVE   10001
#define RAW1394_REQ_FCP_REQUEST   10002

/* error codes */
#define RAW1394_ERROR_NONE        0
#define RAW1394_ERROR_COMPAT      (-1001)
#define RAW1394_ERROR_STATE_ORDER (-1002)
#define RAW1394_ERROR_GENERATION  (-1003)
#define RAW1394_ERROR_INVALID_ARG (-1004)
#define RAW1394_ERROR_MEMFAULT    (-1005)
#define RAW1394_ERROR_ALREADY     (-1006)

#define RAW1394_ERROR_EXCESSIVE   (-1020)
#define RAW1394_ERROR_UNTIDY_LEN  (-1021)

#define RAW1394_ERROR_SEND_ERROR  (-1100)
#define RAW1394_ERROR_ABORTED     (-1101)
#define RAW1394_ERROR_TIMEOUT     (-1102)


#include <asm/types.h>

struct raw1394_request {
        __u32 type;
        __s32 error;
        __u32 misc;

        __u32 generation;
        __u32 length;

        __u64 address;

        __u64 tag;

        __u64 sendb;
        __u64 recvb;
};

struct raw1394_khost_list {
        __u32 nodes;
        __u8 name[32];
};

#ifdef __KERNEL__

struct iso_block_store {
        atomic_t refcount;
        size_t data_size;
        quadlet_t data[0];
};

struct file_info {
        struct list_head list;

        enum { opened, initialized, connected } state;
        unsigned int protocol_version;

        struct hpsb_host *host;

        struct list_head req_pending;
        struct list_head req_complete;
        struct semaphore complete_sem;
        spinlock_t reqlists_lock;
        wait_queue_head_t poll_wait_complete;

        u8 *fcp_buffer;

        u64 listen_channels;
        quadlet_t *iso_buffer;
        size_t iso_buffer_length;
};

struct pending_request {
        struct list_head list;
        struct file_info *file_info;
        struct hpsb_packet *packet;
        struct tq_struct tq;
        struct iso_block_store *ibs;
        quadlet_t *data;
        int free_data;
        struct raw1394_request req;
};

struct host_info {
        struct list_head list;
        struct hpsb_host *host;
        struct list_head file_info_list;
};

#endif /* __KERNEL__ */

#endif /* IEEE1394_RAW1394_H */
