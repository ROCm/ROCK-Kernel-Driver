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

/* This protects most of the client-side state. */
static spinlock_t               state_spinlock = SPIN_LOCK_UNLOCKED;

nfs4_stateid zero_stateid =
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

nfs4_stateid one_stateid =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };


/*
 * nfs4_get_client(): returns an empty client structure
 * nfs4_put_client(): drops reference to client structure
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
struct nfs4_client *
nfs4_get_client(void)
{
	struct nfs4_client *clp;

	if ((clp = kmalloc(sizeof(*clp), GFP_KERNEL)))
		memset(clp, 0, sizeof(nfs4_verifier));
	return clp;
}

void
nfs4_put_client(struct nfs4_client *clp)
{
	BUG_ON(!clp);
	kfree(clp);
}

static inline u32
nfs4_alloc_lockowner_id(struct nfs4_client *clp)
{
	u32 res;

	spin_lock(&state_spinlock);
	res = clp->cl_lockowner_id ++;
	spin_unlock(&state_spinlock);
	return res;
}

/*
 * nfs4_get_shareowner(): this is called on the OPEN or CREATE path to
 * obtain a new shareowner.
 *
 * There are three shareowners (open_owner4 in rfc3010) per inode,
 * one for each possible combination of share lock access. Since
 * Linux does not support the deny access type, there are
 * three (not 9) referenced by the nfs_inode:
 *
 * O_WRONLY: inode->wo_owner
 * O_RDONLY: inode->ro_owner
 * O_RDWR:   inode->rw_owner
 *
 * We create a new shareowner the first time a file is OPENed with
 * one of the above shares. All other OPENs with a similar
 * share use the single stateid associated with the inode.
 *
 */
struct nfs4_shareowner *
nfs4_get_shareowner(struct inode *dir)
{
	struct nfs4_client *clp;
	struct nfs4_shareowner *sp;

	sp = kmalloc(sizeof(*sp),GFP_KERNEL);
	if (!sp)
		return NULL;
	clp = (NFS_SB(dir->i_sb))->nfs4_state;
	BUG_ON(!clp);
	init_MUTEX(&sp->so_sema);
	sp->so_seqid = 0;                 /* arbitrary */
	memset(sp->so_stateid, 0, sizeof(nfs4_stateid));
	sp->so_id = nfs4_alloc_lockowner_id(clp);
	return sp;
}

/*
 * Called for each non-null inode shareowner in nfs_clear_inode, 
 * or if nfs4_do_open fails.
 */
void
nfs4_put_shareowner(struct inode *inode, struct nfs4_shareowner *sp)
{
	if (!sp)
		return;
	if (sp->so_flags & O_ACCMODE)
		nfs4_do_close(inode, sp);
        kfree(sp);
}

/*
* Called with sp->so_sema held.
*
* Increment the seqid if the OPEN/OPEN_DOWNGRADE/CLOSE succeeded, or
* failed with a seqid incrementing error -
* see comments nfs_fs.h:seqid_mutating_error()
*/
void
nfs4_increment_seqid(u32 status, struct nfs4_shareowner *sp)
{
	if (status == NFS_OK || seqid_mutating_err(status))
		sp->so_seqid++;
}

/*
* Called by nfs4_proc_open to set the appropriate stateid
*/
int
nfs4_set_inode_share(struct inode * inode, struct nfs4_shareowner *sp, unsigned int open_flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	switch (open_flags & O_ACCMODE) {
		case O_RDONLY:
			if (!nfsi->ro_owner) {
				nfsi->ro_owner = sp;
				return 0;
			}
			break;
		case O_WRONLY:
			if (!nfsi->wo_owner) {
				nfsi->wo_owner = sp;
				return 0;
			}
			break;
		case O_RDWR:
			if (!nfsi->rw_owner) {
				nfsi->rw_owner = sp;
				return 0;
			}
	}
	return -EBUSY;
}

/*
* Boolean test to determine if an OPEN call goes on the wire.
*
* Called by nfs4_proc_open.
*/
int
nfs4_test_shareowner(struct inode *inode, unsigned int open_flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	switch (open_flags & O_ACCMODE) {
		case O_RDONLY:
			if(nfsi->ro_owner)
				return 0;
			break;
		case O_WRONLY:
			if(nfsi->wo_owner)
				return 0;
			break;
		case O_RDWR:
			if(nfsi->rw_owner)
				return 0;
        }
        return 1;
}

struct nfs4_shareowner *
nfs4_get_inode_share(struct inode * inode, unsigned int open_flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	switch (open_flags & O_ACCMODE) {
		case O_RDONLY:
			return nfsi->ro_owner;
		case O_WRONLY:
			return nfsi->wo_owner;
		case O_RDWR:
			return nfsi->rw_owner;
	}
	/* Duh gcc warning if we don't... */
	return NULL;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
