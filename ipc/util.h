/*
 * linux/ipc/util.h
 * Copyright (C) 1999 Christoph Rohland
 *
 * ipc helper functions (c) 1999 Manfred Spraul <manfreds@colorfullife.com>
 */
#include <linux/rcupdate.h>

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

/*
 * ipc_get() requires ipc_ids.sem down, otherwise we need a rmb() here
 * to sync with grow_ary();
 *
 * So far only shm_get_stat() uses ipc_get() via shm_get().  So ipc_get()
 * is called with shm_ids.sem locked.  Thus a rmb() is not needed here,
 * as grow_ary() also requires shm_ids.sem down(for shm).
 *
 * But if ipc_get() is used in the future without ipc_ids.sem down,
 * we need to add a rmb() before accessing the entries array
 */
extern inline struct kern_ipc_perm* ipc_get(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;
	if(lid >= ids->size)
		return NULL;
	rmb();
	out = ids->entries[lid].p;
	return out;
}

extern inline struct kern_ipc_perm* ipc_lock(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;

	rcu_read_lock();
	if(lid >= ids->size) {
		rcu_read_unlock();
		return NULL;
	}

	/* we need a barrier here to sync with grow_ary() */
	rmb();
	out = ids->entries[lid].p;
	if(out == NULL) {
		rcu_read_unlock();
		return NULL;
	}
	spin_lock(&out->lock);
	
	/* ipc_rmid() may have already freed the ID while ipc_lock
	 * was spinning: here verify that the structure is still valid
	 */
	if (out->deleted) {
		spin_unlock(&out->lock);
		rcu_read_unlock();
		return NULL;
	}
	return out;
}

extern inline void ipc_unlock(struct kern_ipc_perm* perm)
{
	spin_unlock(&perm->lock);
	rcu_read_unlock();
}

extern inline int ipc_buildid(struct ipc_ids* ids, int id, int seq)
{
	return SEQ_MULTIPLIER*seq + id;
}

extern inline int ipc_checkid(struct ipc_ids* ids, struct kern_ipc_perm* ipcp, int uid)
{
	if(uid/SEQ_MULTIPLIER != ipcp->seq)
		return 1;
	return 0;
}

void kernel_to_ipc64_perm(struct kern_ipc_perm *in, struct ipc64_perm *out);
void ipc64_perm_to_ipc_perm(struct ipc64_perm *in, struct ipc_perm *out);

#ifdef __ia64__
  /* On IA-64, we always use the "64-bit version" of the IPC structures.  */ 
# define ipc_parse_version(cmd)	IPC_64
#else
int ipc_parse_version (int *cmd);
#endif
