/*
 *  fs/nfs/nfs4state.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Implementation of the NFSv4 state model.  For the time being,
 * this is minimal, but will be made much more complex in a
 * subsequent patch.
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/nfs_fs.h>

#define OPENOWNER_POOL_SIZE	8

static spinlock_t		state_spinlock = SPIN_LOCK_UNLOCKED;

nfs4_stateid zero_stateid;

#if 0
nfs4_stateid one_stateid =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif

static LIST_HEAD(nfs4_clientid_list);

/*
 * nfs4_get_client(): returns an empty client structure
 * nfs4_put_client(): drops reference to client structure
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
static struct nfs4_client *
nfs4_alloc_client(struct in_addr *addr)
{
	struct nfs4_client *clp;

	if ((clp = kmalloc(sizeof(*clp), GFP_KERNEL))) {
		memset(clp, 0, sizeof(*clp));
		memcpy(&clp->cl_addr, addr, sizeof(clp->cl_addr));
		init_rwsem(&clp->cl_sem);
		INIT_LIST_HEAD(&clp->cl_state_owners);
		INIT_LIST_HEAD(&clp->cl_unused);
		spin_lock_init(&clp->cl_lock);
		atomic_set(&clp->cl_count, 1);
		clp->cl_state = NFS4CLNT_NEW;
	}
	return clp;
}

static void
nfs4_free_client(struct nfs4_client *clp)
{
	struct nfs4_state_owner *sp;

	while (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next,
				struct nfs4_state_owner,
				so_list);
		list_del(&sp->so_list);
		kfree(sp);
	}
	BUG_ON(!list_empty(&clp->cl_state_owners));
	kfree(clp);
}

struct nfs4_client *
nfs4_get_client(struct in_addr *addr)
{
	struct nfs4_client *new, *clp = NULL;

	new = nfs4_alloc_client(addr);
	spin_lock(&state_spinlock);
	list_for_each_entry(clp, &nfs4_clientid_list, cl_servers) {
		if (memcmp(&clp->cl_addr, addr, sizeof(clp->cl_addr)) == 0)
			goto found;
	}
	if (new)
		list_add(&new->cl_servers, &nfs4_clientid_list);
	spin_unlock(&state_spinlock);
	return new;
found:
	atomic_inc(&clp->cl_count);
	spin_unlock(&state_spinlock);
	if (new)
		nfs4_free_client(new);
	return clp;
}

void
nfs4_put_client(struct nfs4_client *clp)
{
	if (!atomic_dec_and_lock(&clp->cl_count, &state_spinlock))
		return;
	list_del(&clp->cl_servers);
	spin_unlock(&state_spinlock);
	nfs4_free_client(clp);
}

static inline u32
nfs4_alloc_lockowner_id(struct nfs4_client *clp)
{
	return clp->cl_lockowner_id ++;
}

static struct nfs4_state_owner *
nfs4_client_grab_unused(struct nfs4_client *clp, struct rpc_cred *cred)
{
	struct nfs4_state_owner *sp = NULL;

	if (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next, struct nfs4_state_owner, so_list);
		atomic_inc(&sp->so_count);
		sp->so_cred = cred;
		list_move(&sp->so_list, &clp->cl_state_owners);
		clp->cl_nunused--;
	}
	return sp;
}

/*
 * nfs4_alloc_state_owner(): this is called on the OPEN or CREATE path to
 * create a new state_owner.
 *
 */
static struct nfs4_state_owner *
nfs4_alloc_state_owner(void)
{
	struct nfs4_state_owner *sp;

	sp = kmalloc(sizeof(*sp),GFP_KERNEL);
	if (!sp)
		return NULL;
	init_MUTEX(&sp->so_sema);
	sp->so_seqid = 0;                 /* arbitrary */
	INIT_LIST_HEAD(&sp->so_states);
	atomic_set(&sp->so_count, 1);
	return sp;
}

struct nfs4_state_owner *
nfs4_get_state_owner(struct nfs_server *server, struct rpc_cred *cred)
{
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs4_state_owner *sp, *new;

	get_rpccred(cred);
	new = nfs4_alloc_state_owner();
	spin_lock(&clp->cl_lock);
	sp = nfs4_client_grab_unused(clp, cred);
	if (sp == NULL && new != NULL) {
		list_add(&new->so_list, &clp->cl_state_owners);
		new->so_client = clp;
		new->so_id = nfs4_alloc_lockowner_id(clp);
		new->so_cred = cred;
		sp = new;
		new = NULL;
	}
	spin_unlock(&clp->cl_lock);
	if (new)
		kfree(new);
	if (!sp)
		put_rpccred(cred);
	return sp;
}

void
nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs4_client *clp = sp->so_client;
	struct rpc_cred *cred = sp->so_cred;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;
	if (clp->cl_nunused >= OPENOWNER_POOL_SIZE)
		goto out_free;
	list_move(&sp->so_list, &clp->cl_unused);
	clp->cl_nunused++;
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	cred = NULL;
	return;
out_free:
	list_del(&sp->so_list);
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	kfree(sp);
}

static struct nfs4_state *
nfs4_alloc_open_state(void)
{
	struct nfs4_state *state;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->pid = current->pid;
	state->state = 0;
	memset(state->stateid.data, 0, sizeof(state->stateid.data));
	atomic_set(&state->count, 1);
	return state;
}

static struct nfs4_state *
__nfs4_find_state_bypid(struct inode *inode, pid_t pid)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		if (state->pid == pid) {
			atomic_inc(&state->count);
			return state;
		}
	}
	return NULL;
}

static struct nfs4_state *
__nfs4_find_state_byowner(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		if (state->owner == owner) {
			atomic_inc(&state->count);
			return state;
		}
	}
	return NULL;
}

struct nfs4_state *
nfs4_find_state_bypid(struct inode *inode, pid_t pid)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_bypid(inode, pid);
	/* Add the state to the tail of the inode's list */
	if (state)
		list_move_tail(&state->inode_states, &nfsi->open_states);
	spin_unlock(&inode->i_lock);
	return state;
}

static void
nfs4_free_open_state(struct nfs4_state *state)
{
	kfree(state);
}

struct nfs4_state *
nfs4_get_open_state(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs4_state *state, *new;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	spin_unlock(&inode->i_lock);
	if (state)
		goto out;
	new = nfs4_alloc_open_state();
	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	if (state == NULL && new != NULL) {
		state = new;
		/* Caller *must* be holding owner->so_sem */
		list_add(&state->open_states, &owner->so_states);
		state->owner = owner;
		atomic_inc(&owner->so_count);
		list_add(&state->inode_states, &nfsi->open_states);
		state->inode = inode;
		atomic_inc(&inode->i_count);
		spin_unlock(&inode->i_lock);
	} else {
		spin_unlock(&inode->i_lock);
		if (new)
			nfs4_free_open_state(new);
	}
out:
	return state;
}

void
nfs4_put_open_state(struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *owner = state->owner;

	if (!atomic_dec_and_lock(&state->count, &inode->i_lock))
		return;
	list_del(&state->inode_states);
	spin_unlock(&inode->i_lock);
	down(&owner->so_sema);
	list_del(&state->open_states);
	if (state->state != 0)
		nfs4_do_close(inode, state);
	up(&owner->so_sema);
	iput(inode);
	nfs4_free_open_state(state);
	nfs4_put_state_owner(owner);
}

/*
* Called with sp->so_sema held.
*
* Increment the seqid if the OPEN/OPEN_DOWNGRADE/CLOSE succeeded, or
* failed with a seqid incrementing error -
* see comments nfs_fs.h:seqid_mutating_error()
*/
void
nfs4_increment_seqid(u32 status, struct nfs4_state_owner *sp)
{
	if (status == NFS_OK || seqid_mutating_err(status))
		sp->so_seqid++;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
