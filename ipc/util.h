/*
 * linux/ipc/util.h
 * Copyright (C) 1999 Christoph Rohland
 *
 * ipc helper functions (c) 1999 Manfred Spraul <manfreds@colorfullife.com>
 */

#ifndef _IPC_UTIL_H
#define _IPC_UTIL_H

#define USHRT_MAX 0xffff
#define SEQ_MULTIPLIER	(IPCMNI)

void sem_init (void);
void msg_init (void);
void shm_init (void);

struct ipc_ids {
	int size;
	int in_use;
	int max_id;
	unsigned short seq;
	unsigned short seq_max;
	struct semaphore sem;	
	struct ipc_id* entries;
};

struct ipc_id {
	struct kern_ipc_perm* p;
};

void __init ipc_init_ids(struct ipc_ids* ids, int size);

/* must be called with ids->sem acquired.*/
int ipc_findkey(struct ipc_ids* ids, key_t key);
int ipc_addid(struct ipc_ids* ids, struct kern_ipc_perm* new, int size);

/* must be called with both locks acquired. */
struct kern_ipc_perm* ipc_rmid(struct ipc_ids* ids, int id);

int ipcperms (struct kern_ipc_perm *ipcp, short flg);

/* for rare, potentially huge allocations.
 * both function can sleep
 */
void* ipc_alloc(int size);
void ipc_free(void* ptr, int size);
/* for allocation that need to be freed by RCU
 * both function can sleep
 */
void* ipc_rcu_alloc(int size);
void ipc_rcu_free(void* arg, int size);

struct kern_ipc_perm* ipc_get(struct ipc_ids* ids, int id);
struct kern_ipc_perm* ipc_lock(struct ipc_ids* ids, int id);
void ipc_unlock(struct kern_ipc_perm* perm);
int ipc_buildid(struct ipc_ids* ids, int id, int seq);
int ipc_checkid(struct ipc_ids* ids, struct kern_ipc_perm* ipcp, int uid);

void kernel_to_ipc64_perm(struct kern_ipc_perm *in, struct ipc64_perm *out);
void ipc64_perm_to_ipc_perm(struct ipc64_perm *in, struct ipc_perm *out);

#if defined(__ia64__) || defined(__x86_64__) || defined(__hppa__)
  /* On IA-64, we always use the "64-bit version" of the IPC structures.  */ 
# define ipc_parse_version(cmd)	IPC_64
#else
int ipc_parse_version (int *cmd);
#endif

extern void free_msg(struct msg_msg *msg);
extern struct msg_msg *load_msg(void __user *src, int len);
extern int store_msg(void __user *dest, struct msg_msg *msg, int len);

#endif
