/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *
 * Sep 1997 - Call suser() last after "normal" permission checks so we
 *            get BSD style process accounting right.
 *            Occurs in several places in the IPC code.
 *            Chris Evans, <chris@ferret.lmh.ox.ac.uk>
 * Nov 1999 - ipc helper functions, unified SMP locking
 *	      Manfred Spraul <manfreds@colorfullife.com>
 * Oct 2002 - One lock per IPC id. RCU ipc_free for lock-free grow_ary().
 *            Mingming Cao <cmm@us.ibm.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/init.h>
#include <linux/msg.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highuid.h>
#include <linux/security.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

#include "util.h"

/**
 *	ipc_init	-	initialise IPC subsystem
 *
 *	The various system5 IPC resources (semaphores, messages and shared
 *	memory are initialised
 */
 
static int __init ipc_init(void)
{
	sem_init();
	msg_init();
	shm_init();
	return 0;
}
__initcall(ipc_init);

/**
 *	ipc_init_ids		-	initialise IPC identifiers
 *	@ids: Identifier set
 *	@size: Number of identifiers
 *
 *	Given a size for the ipc identifier range (limited below IPCMNI)
 *	set up the sequence range to use then allocate and initialise the
 *	array itself. 
 */
 
void __init ipc_init_ids(struct ipc_ids* ids, int size)
{
	int i;
	sema_init(&ids->sem,1);

	if(size > IPCMNI)
		size = IPCMNI;
	ids->size = size;
	ids->in_use = 0;
	ids->max_id = -1;
	ids->seq = 0;
	{
		int seq_limit = INT_MAX/SEQ_MULTIPLIER;
		if(seq_limit > USHRT_MAX)
			ids->seq_max = USHRT_MAX;
		 else
		 	ids->seq_max = seq_limit;
	}

	ids->entries = ipc_rcu_alloc(sizeof(struct ipc_id)*size);

	if(ids->entries == NULL) {
		printk(KERN_ERR "ipc_init_ids() failed, ipc service disabled.\n");
		ids->size = 0;
	}
	for(i=0;i<ids->size;i++)
		ids->entries[i].p = NULL;
}

/**
 *	ipc_findkey	-	find a key in an ipc identifier set	
 *	@ids: Identifier set
 *	@key: The key to find
 *	
 *	Requires ipc_ids.sem locked.
 *	Returns the identifier if found or -1 if not.
 */
 
int ipc_findkey(struct ipc_ids* ids, key_t key)
{
	int id;
	struct kern_ipc_perm* p;
	int max_id = ids->max_id;

	/*
	 * read_barrier_depends is not needed here
	 * since ipc_ids.sem is held
	 */
	for (id = 0; id <= max_id; id++) {
		p = ids->entries[id].p;
		if(p==NULL)
			continue;
		if (key == p->key)
			return id;
	}
	return -1;
}

/*
 * Requires ipc_ids.sem locked
 */
static int grow_ary(struct ipc_ids* ids, int newsize)
{
	struct ipc_id* new;
	struct ipc_id* old;
	int i;

	if(newsize > IPCMNI)
		newsize = IPCMNI;
	if(newsize <= ids->size)
		return newsize;

	new = ipc_rcu_alloc(sizeof(struct ipc_id)*newsize);
	if(new == NULL)
		return ids->size;
	memcpy(new, ids->entries, sizeof(struct ipc_id)*ids->size);
	for(i=ids->size;i<newsize;i++) {
		new[i].p = NULL;
	}
	old = ids->entries;
	i = ids->size;

	/*
	 * before setting the ids->entries to the new array, there must be a
	 * smp_wmb() to make sure the memcpyed contents of the new array are
	 * visible before the new array becomes visible.
	 */
	smp_wmb();	/* prevent seeing new array uninitialized. */
	ids->entries = new;
	smp_wmb();	/* prevent indexing into old array based on new size. */
	ids->size = newsize;

	ipc_rcu_free(old, sizeof(struct ipc_id)*i);
	return ids->size;
}

/**
 *	ipc_addid 	-	add an IPC identifier
 *	@ids: IPC identifier set
 *	@new: new IPC permission set
 *	@size: new size limit for the id array
 *
 *	Add an entry 'new' to the IPC arrays. The permissions object is
 *	initialised and the first free entry is set up and the id assigned
 *	is returned. The list is returned in a locked state on success.
 *	On failure the list is not locked and -1 is returned.
 *
 *	Called with ipc_ids.sem held.
 */
 
int ipc_addid(struct ipc_ids* ids, struct kern_ipc_perm* new, int size)
{
	int id;

	size = grow_ary(ids,size);

	/*
	 * read_barrier_depends() is not needed here since
	 * ipc_ids.sem is held
	 */
	for (id = 0; id < size; id++) {
		if(ids->entries[id].p == NULL)
			goto found;
	}
	return -1;
found:
	ids->in_use++;
	if (id > ids->max_id)
		ids->max_id = id;

	new->cuid = new->uid = current->euid;
	new->gid = new->cgid = current->egid;

	new->seq = ids->seq++;
	if(ids->seq > ids->seq_max)
		ids->seq = 0;

	new->lock = SPIN_LOCK_UNLOCKED;
	new->deleted = 0;
	rcu_read_lock();
	spin_lock(&new->lock);
	ids->entries[id].p = new;
	return id;
}

/**
 *	ipc_rmid	-	remove an IPC identifier
 *	@ids: identifier set
 *	@id: Identifier to remove
 *
 *	The identifier must be valid, and in use. The kernel will panic if
 *	fed an invalid identifier. The entry is removed and internal
 *	variables recomputed. The object associated with the identifier
 *	is returned.
 *	ipc_ids.sem and the spinlock for this ID is hold before this function
 *	is called, and remain locked on the exit.
 */
 
struct kern_ipc_perm* ipc_rmid(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* p;
	int lid = id % SEQ_MULTIPLIER;
	if(lid >= ids->size)
		BUG();

	/* 
	 * do not need a read_barrier_depends() here to force ordering
	 * on Alpha, since the ipc_ids.sem is held.
	 */	
	p = ids->entries[lid].p;
	ids->entries[lid].p = NULL;
	if(p==NULL)
		BUG();
	ids->in_use--;

	if (lid == ids->max_id) {
		do {
			lid--;
			if(lid == -1)
				break;
		} while (ids->entries[lid].p == NULL);
		ids->max_id = lid;
	}
	p->deleted = 1;
	return p;
}

/**
 *	ipc_alloc	-	allocate ipc space
 *	@size: size desired
 *
 *	Allocate memory from the appropriate pools and return a pointer to it.
 *	NULL is returned if the allocation fails
 */
 
void* ipc_alloc(int size)
{
	void* out;
	if(size > PAGE_SIZE)
		out = vmalloc(size);
	else
		out = kmalloc(size, GFP_KERNEL);
	return out;
}

/**
 *	ipc_free        -       free ipc space
 *	@ptr: pointer returned by ipc_alloc
 *	@size: size of block
 *
 *	Free a block created with ipc_alloc. The caller must know the size
 *	used in the allocation call.
 */

void ipc_free(void* ptr, int size)
{
	if(size > PAGE_SIZE)
		vfree(ptr);
	else
		kfree(ptr);
}

struct ipc_rcu_kmalloc
{
	struct rcu_head rcu;
	/* "void *" makes sure alignment of following data is sane. */
	void *data[0];
};

struct ipc_rcu_vmalloc
{
	struct rcu_head rcu;
	struct work_struct work;
	/* "void *" makes sure alignment of following data is sane. */
	void *data[0];
};

static inline int rcu_use_vmalloc(int size)
{
	/* Too big for a single page? */
	if (sizeof(struct ipc_rcu_kmalloc) + size > PAGE_SIZE)
		return 1;
	return 0;
}

/**
 *	ipc_rcu_alloc	-	allocate ipc and rcu space 
 *	@size: size desired
 *
 *	Allocate memory for the rcu header structure +  the object.
 *	Returns the pointer to the object.
 *	NULL is returned if the allocation fails. 
 */
 
void* ipc_rcu_alloc(int size)
{
	void* out;
	/* 
	 * We prepend the allocation with the rcu struct, and
	 * workqueue if necessary (for vmalloc). 
	 */
	if (rcu_use_vmalloc(size)) {
		out = vmalloc(sizeof(struct ipc_rcu_vmalloc) + size);
		if (out) out += sizeof(struct ipc_rcu_vmalloc);
	} else {
		out = kmalloc(sizeof(struct ipc_rcu_kmalloc)+size, GFP_KERNEL);
		if (out) out += sizeof(struct ipc_rcu_kmalloc);
	}

	return out;
}

/**
 *	ipc_schedule_free	- free ipc + rcu space
 * 
 * Since RCU callback function is called in bh,
 * we need to defer the vfree to schedule_work
 */
static void ipc_schedule_free(void* arg)
{
	struct ipc_rcu_vmalloc *free = arg;

	INIT_WORK(&free->work, vfree, free);
	schedule_work(&free->work);
}

void ipc_rcu_free(void* ptr, int size)
{
	if (rcu_use_vmalloc(size)) {
		struct ipc_rcu_vmalloc *free;
		free = ptr - sizeof(*free);
		call_rcu(&free->rcu, ipc_schedule_free, free);
	} else {
		struct ipc_rcu_kmalloc *free;
		free = ptr - sizeof(*free);
		/* kfree takes a "const void *" so gcc warns.  So we cast. */
		call_rcu(&free->rcu, (void (*)(void *))kfree, free);
	}

}

/**
 *	ipcperms	-	check IPC permissions
 *	@ipcp: IPC permission set
 *	@flag: desired permission set.
 *
 *	Check user, group, other permissions for access
 *	to ipc resources. return 0 if allowed
 */
 
int ipcperms (struct kern_ipc_perm *ipcp, short flag)
{	/* flag will most probably be 0 or S_...UGO from <linux/stat.h> */
	int requested_mode, granted_mode;

	requested_mode = (flag >> 6) | (flag >> 3) | flag;
	granted_mode = ipcp->mode;
	if (current->euid == ipcp->cuid || current->euid == ipcp->uid)
		granted_mode >>= 6;
	else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
		granted_mode >>= 3;
	/* is there some bit set in requested_mode but not in granted_mode? */
	if ((requested_mode & ~granted_mode & 0007) && 
	    !capable(CAP_IPC_OWNER))
		return -1;

	return security_ipc_permission(ipcp, flag);
}

/*
 * Functions to convert between the kern_ipc_perm structure and the
 * old/new ipc_perm structures
 */

/**
 *	kernel_to_ipc64_perm	-	convert kernel ipc permissions to user
 *	@in: kernel permissions
 *	@out: new style IPC permissions
 *
 *	Turn the kernel object 'in' into a set of permissions descriptions
 *	for returning to userspace (out).
 */
 

void kernel_to_ipc64_perm (struct kern_ipc_perm *in, struct ipc64_perm *out)
{
	out->key	= in->key;
	out->uid	= in->uid;
	out->gid	= in->gid;
	out->cuid	= in->cuid;
	out->cgid	= in->cgid;
	out->mode	= in->mode;
	out->seq	= in->seq;
}

/**
 *	ipc64_perm_to_ipc_perm	-	convert old ipc permissions to new
 *	@in: new style IPC permissions
 *	@out: old style IPC permissions
 *
 *	Turn the new style permissions object in into a compatibility
 *	object and store it into the 'out' pointer.
 */
 
void ipc64_perm_to_ipc_perm (struct ipc64_perm *in, struct ipc_perm *out)
{
	out->key	= in->key;
	SET_UID(out->uid, in->uid);
	SET_GID(out->gid, in->gid);
	SET_UID(out->cuid, in->cuid);
	SET_GID(out->cgid, in->cgid);
	out->mode	= in->mode;
	out->seq	= in->seq;
}

/*
 * So far only shm_get_stat() calls ipc_get() via shm_get(), so ipc_get()
 * is called with shm_ids.sem locked.  Since grow_ary() is also called with
 * shm_ids.sem down(for Shared Memory), there is no need to add read 
 * barriers here to gurantee the writes in grow_ary() are seen in order 
 * here (for Alpha).
 *
 * However ipc_get() itself does not necessary require ipc_ids.sem down. So
 * if in the future ipc_get() is used by other places without ipc_ids.sem
 * down, then ipc_get() needs read memery barriers as ipc_lock() does.
 */
struct kern_ipc_perm* ipc_get(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;
	if(lid >= ids->size)
		return NULL;
	out = ids->entries[lid].p;
	return out;
}

struct kern_ipc_perm* ipc_lock(struct ipc_ids* ids, int id)
{
	struct kern_ipc_perm* out;
	int lid = id % SEQ_MULTIPLIER;
	struct ipc_id* entries;

	rcu_read_lock();
	if(lid >= ids->size) {
		rcu_read_unlock();
		return NULL;
	}

	/* 
	 * Note: The following two read barriers are corresponding
	 * to the two write barriers in grow_ary(). They guarantee 
	 * the writes are seen in the same order on the read side. 
	 * smp_rmb() has effect on all CPUs.  read_barrier_depends() 
	 * is used if there are data dependency between two reads, and 
	 * has effect only on Alpha.
	 */
	smp_rmb(); /* prevent indexing old array with new size */
	entries = ids->entries;
	read_barrier_depends(); /*prevent seeing new array unitialized */
	out = entries[lid].p;
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

void ipc_unlock(struct kern_ipc_perm* perm)
{
	spin_unlock(&perm->lock);
	rcu_read_unlock();
}

int ipc_buildid(struct ipc_ids* ids, int id, int seq)
{
	return SEQ_MULTIPLIER*seq + id;
}

int ipc_checkid(struct ipc_ids* ids, struct kern_ipc_perm* ipcp, int uid)
{
	if(uid/SEQ_MULTIPLIER != ipcp->seq)
		return 1;
	return 0;
}

#if !defined(__ia64__) && !defined(__x86_64__) && !defined(__hppa__)

/**
 *	ipc_parse_version	-	IPC call version
 *	@cmd: pointer to command
 *
 *	Return IPC_64 for new style IPC and IPC_OLD for old style IPC. 
 *	The cmd value is turned from an encoding command and version into
 *	just the command code.
 */
 
int ipc_parse_version (int *cmd)
{
	if (*cmd & IPC_64) {
		*cmd ^= IPC_64;
		return IPC_64;
	} else {
		return IPC_OLD;
	}
}

#endif /* __ia64__ */
