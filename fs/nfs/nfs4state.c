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

        if ((clp = kmalloc(sizeof(*clp), GFP_KERNEL))) {
                atomic_set(&clp->cl_count, 1);
                clp->cl_clientid = 0;
                INIT_LIST_HEAD(&clp->cl_lockowners);
        }
        return clp;
}

void
nfs4_put_client(struct nfs4_client *clp)
{
        BUG_ON(!clp);
        BUG_ON(!atomic_read(&clp->cl_count));
        
        if (atomic_dec_and_test(&clp->cl_count)) {
                BUG_ON(!list_empty(&clp->cl_lockowners));
                kfree(clp);
        }
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
