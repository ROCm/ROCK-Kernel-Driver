#ifndef __PRESTO_PSDEV_H
#define __PRESTO_PSDEV_H

#ifdef PRESTO_DEVEL
# define PRESTO_FS_NAME "izofs"
# define PRESTO_PSDEV_NAME "/dev/izo"
# define PRESTO_PSDEV_MAJOR 186
#else
# define PRESTO_FS_NAME "InterMezzo"
# define PRESTO_PSDEV_NAME "/dev/intermezzo"
# define PRESTO_PSDEV_MAJOR 185
#endif

#define MAX_PRESTODEV 16

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
#define wait_queue_head_t  struct wait_queue *
#define DECLARE_WAITQUEUE(name,task) \
        struct wait_queue name = { task, NULL }
#define init_waitqueue_head(arg) 
#else
#ifndef __initfunc
#define __initfunc(arg) arg
#endif
#endif


/* represents state of a /dev/presto */
/* communication pending & processing queues */
struct upc_comm {
        unsigned int         uc_seq;
        wait_queue_head_t    uc_waitq;     /* Lento wait queue */
        struct list_head    uc_pending;
        struct list_head    uc_processing;
        int                  uc_pid;       /* Lento's pid */
        int                  uc_hard; /* allows signals during upcalls */
        int                  uc_no_filter;
        int                  uc_no_journal;
        int                  uc_no_upcall;
        int                  uc_timeout; /* . sec: signals will dequeue upc */
        long                 uc_errorval; /* for testing I/O failures */
        struct list_head     uc_cache_list;
        int                   uc_minor;
        char *                uc_devname;
};

#define ISLENTO(minor) (current->pid == upc_comms[minor].uc_pid \
                || current->p_pptr->pid == upc_comms[minor].uc_pid)

extern struct upc_comm upc_comms[MAX_PRESTODEV];

/* messages between presto filesystem in kernel and Venus */
#define REQ_READ   1
#define REQ_WRITE  2
#define REQ_ASYNC  4
#define REQ_DEAD   8

struct upc_req {
        struct list_head   rq_chain;
        caddr_t            rq_data;
        u_short            rq_flags;
        u_short            rq_bufsize;
        u_short            rq_rep_size;
        u_short            rq_opcode;  /* copied from data to save lookup */
        int                rq_unique;
        wait_queue_head_t  rq_sleep;   /* process' wait queue */
        unsigned long      rq_posttime;
};

#endif
