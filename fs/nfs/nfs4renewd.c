/*
 *  fs/nfs/nfs4renewd.c
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
 * Implementation of the NFSv4 "renew daemon", which wakes up periodically to
 * send a RENEW, to keep state alive on the server.  The daemon is implemented
 * as an rpc_task, not a real kernel thread, so it always runs in rpciod's
 * context.  There is one renewd per nfs_server.
 *
 * TODO: If the send queue gets backlogged (e.g., if the server goes down),
 * we will keep filling the queue with periodic RENEW requests.  We need a
 * mechanism for ensuring that if renewd successfully sends off a request,
 * then it only wakes up when the request is finished.  Maybe use the
 * child task framework of the RPC layer?
 */

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>

#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>

static RPC_WAITQ(nfs4_renewd_queue, "nfs4_renewd_queue");

static void
renewd(struct rpc_task *task)
{
	struct nfs_server *server = (struct nfs_server *)task->tk_calldata;
	unsigned long lease = server->lease_time;
	unsigned long last = server->last_renewal;
	unsigned long timeout;

	if (!server->nfs4_state)
		timeout = (2 * lease) / 3;
	else if (jiffies < last + lease/3)
		timeout = (2 * lease) / 3 + last - jiffies;
	else {
		/* Queue an asynchronous RENEW. */
		nfs4_proc_renew(server);
		timeout = (2 * lease) / 3;
	}

	if (timeout < 5 * HZ)    /* safeguard */
		timeout = 5 * HZ;
	task->tk_timeout = timeout;
	task->tk_action = renewd;
	task->tk_exit = NULL;
	rpc_sleep_on(&nfs4_renewd_queue, task, NULL, NULL);
	return;
}

int
nfs4_init_renewd(struct nfs_server *server)
{
	struct rpc_task *task;
	int status;

	lock_kernel();
	status = -ENOMEM;
	task = rpc_new_task(server->client, NULL, RPC_TASK_ASYNC);
	if (!task)
		goto out;
	task->tk_calldata = server;
	task->tk_action = renewd;
	status = rpc_execute(task);

out:
	unlock_kernel();
	return status;
}

/*
 * Local variables:
 *   c-basic-offset: 8
 * End:
 */
