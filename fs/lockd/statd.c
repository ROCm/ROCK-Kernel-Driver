/*
 * linux/fs/lockd/nsmproc.c
 *
 * Kernel-based status monitor. This is an alternative to
 * the stuff in mon.c.
 *
 * When asked to monitor a host, we add it to /var/lib/nsm/sm
 * ourselves, and that's it. In order to catch SM_NOTIFY calls
 * we implement a minimal statd.
 *
 * Minimal user space requirements for this implementation:
 *  /var/lib/nfs/state
 *	must exist, and must contain the NSM state as a 32bit
 *	binary counter.
 * /var/lib/nfs/sm
 *	must exist
 *
 * Copyright (C) 2004, Olaf Kirch <okir@suse.de>
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsd.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/share.h>
#include <linux/lockd/sm_inter.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>


/* XXX make this a module parameter? */
#define NSM_BASE_PATH		"/var/lib/nfs"
#define NSM_SM_PATH		NSM_BASE_PATH "/sm"
#define NSM_STATE_PATH		NSM_BASE_PATH "/state"

#define NLMDBG_FACILITY		NLMDBG_CLIENT

/*
 * Local NSM state.
 */
u32				nsm_local_state;

/*
 * Initialize local NSM state variable
 */
int
nsm_init(void)
{
	struct file	*filp;
	char		buffer[32];
	mm_segment_t	fs;
	int		res;

	dprintk("lockd: nsm_init()\n");
	filp = filp_open(NSM_STATE_PATH, O_RDONLY, 0444);
	if (IS_ERR(filp)) {
		res = PTR_ERR(filp);
		printk(KERN_NOTICE "lockd: failed to open %s: err=%d\n",
				NSM_STATE_PATH, res);
		return res;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	res = vfs_read(filp, buffer, sizeof(buffer), &filp->f_pos);
	set_fs(fs);
	filp_close(filp, NULL);

	if (res < 0)
		return res;
	if (res == 4)
		nsm_local_state = *(u32 *) buffer;
	else
		nsm_local_state = simple_strtol(buffer, NULL, 10);
	return 0;
}

/*
 * Build the path name for this lockd peer.
 *
 * We keep it extremely simple. Since we can have more
 * than one nlm_host object peer (depending on whether
 * it's server or client, and what proto/version of NLM
 * we use to communicate), we cannot create a file named
 * $IPADDR and remove it when the nlm_host is unmonitored.
 * Besides, unlink() is tricky (there's no kernel_syscall
 * for it), so we just create the file and leave it.
 *
 * When we reboot, the notifier should sort the IPs by
 * descending mtime so that the most recent hosts get
 * notified first.
 */
static char *
nsm_filename(struct in_addr addr)
{
	char		*name;

	name = (char *) __get_free_page(GFP_KERNEL);
	if (name == NULL)
		return NULL;

	/* FIXME IPV6 */
	snprintf(name, PAGE_SIZE, "%s/%u.%u.%u.%u",
			NSM_SM_PATH, NIPQUAD(addr));
	return name;
}

/*
 * Create the NSM monitor file
 */
static int
nsm_create(struct in_addr addr)
{
	struct file	*filp;
	char		*name;
	int		res = 0;

	if (!(name = nsm_filename(addr)))
		return -ENOMEM;

	dprintk("lockd: creating statd monitor file %s\n", name);
	filp = filp_open(name, O_CREAT|O_SYNC|O_RDWR, 0644);
	if (IS_ERR(filp)) {
		res = PTR_ERR(filp);
		printk(KERN_NOTICE
			"lockd/statd: failed to create %s: err=%d\n",
			name, res);
	} else {
		fsync_super(filp->f_dentry->d_inode->i_sb);
		filp_close(filp, NULL);
	}

	free_page((long) name);
	return res;
}

static int
nsm_unlink(struct in_addr addr)
{
	struct nameidata nd;
	struct inode	*inode = NULL;
	struct dentry	*dentry;
	char		*name;
	int		res = 0;

	if (!(name = nsm_filename(addr)))
		return -ENOMEM;

	if ((res = path_lookup(name, LOOKUP_PARENT, &nd)) != 0)
		goto exit;

	if (nd.last_type == LAST_NORM && !nd.last.name[nd.last.len]) {
		down(&nd.dentry->d_inode->i_sem);

		dentry = lookup_hash(&nd.last, nd.dentry);
		if (!IS_ERR(dentry)) {
			if ((inode = dentry->d_inode) != NULL)
				atomic_inc(&inode->i_count);
			res = vfs_unlink(nd.dentry->d_inode, dentry);
			dput(dentry);
		} else {
			res = PTR_ERR(dentry);
		}
		up(&nd.dentry->d_inode->i_sem);
	} else {
		res = -EISDIR;
	}
	path_release(&nd);

exit:
	if (res < 0) {
		printk(KERN_NOTICE
			"lockd/statd: failed to unlink %s: err=%d\n",
			name, res);
	}

	free_page((long) name);
	if (inode)
		iput(inode);
	return res;
}

/*
 * Allocate an NSM handle
 */
struct nsm_handle *
nsm_alloc(struct sockaddr_in *sin)
{
	struct nsm_handle *nsm;

	nsm = (struct nsm_handle *) kmalloc(sizeof(*nsm), GFP_KERNEL);
	if (nsm == NULL)
		return NULL;

	memset(nsm, 0, sizeof(*nsm));
	memcpy(&nsm->sm_addr, sin, sizeof(nsm->sm_addr));
	atomic_set(&nsm->sm_count, 1);

	return nsm;
}

/*
 * Set up monitoring of a remote host
 * Note we hold the semaphore for the host table while
 * we're here.
 */
int
nsm_monitor(struct nlm_host *host)
{
	kernel_cap_t	cap = current->cap_effective;
	struct nsm_handle *nsm;
	int		res = 0;

	dprintk("lockd: nsm_monitor(%s)\n", host->h_name);
	if ((nsm = host->h_nsmhandle) == NULL)
		BUG();

	/* Raise capability to that we're able to create the file */
	cap_raise(current->cap_effective, CAP_DAC_OVERRIDE);
	res = nsm_create(nsm->sm_addr.sin_addr);
	current->cap_effective = cap;

	if (res >= 0)
		nsm->sm_monitored = 1;
	return res;
}

/*
 * Cease to monitor remote host
 * Code stolen from sys_unlink.
 */
int
nsm_unmonitor(struct nlm_host *host)
{
	kernel_cap_t	cap = current->cap_effective;
	struct nsm_handle *nsm;
	int		res = 0;

	nsm = host->h_nsmhandle;
	host->h_nsmhandle = NULL;

	if (!nsm || !atomic_dec_and_test(&nsm->sm_count))
	 	return 0;

	/* If the host was invalidated due to lockd restart/shutdown,
	 * don't unmonitor it.
	 * (Strictly speaking, we would have to keep the SM file
	 * until the next reboot. The only way to achieve that
	 * would be to link the monitor file to sm.bak now.)
	 */
	if (nsm->sm_monitored && !nsm->sm_sticky) {
		dprintk("lockd: nsm_unmonitor(%s)\n", host->h_name);

		/* Raise capability to that we're able to delete the file */
		cap_raise(current->cap_effective, CAP_DAC_OVERRIDE);
		res = nsm_unlink(host->h_addr.sin_addr);
		current->cap_effective = cap;
	}

	kfree(nsm);
	return res;
}

/*
 * NSM server implementation starts here
 */

/*
 * NULL: Test for presence of service
 */
static int
nsmsvc_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	dprintk("statd: NULL          called\n");
	return rpc_success;
}

/*
 * NOTIFY: receive notification that remote host rebooted
 */
static int
nsmsvc_proc_notify(struct svc_rqst *rqstp, struct nsm_args *argp,
				           struct nsm_res  *resp)
{
	struct sockaddr_in	saddr = rqstp->rq_addr;

	dprintk("statd: NOTIFY        called\n");
	if (ntohs(saddr.sin_port) >= 1024) {
		printk(KERN_WARNING
			"statd: rejected NSM_NOTIFY from %08x:%d\n",
			ntohl(rqstp->rq_addr.sin_addr.s_addr),
			ntohs(rqstp->rq_addr.sin_port));
		return rpc_system_err;
	}

	nlm_host_rebooted(&saddr, argp->state);
	return rpc_success;
}

/*
 * All other operations: return failure
 */
static int
nsmsvc_proc_fail(struct svc_rqst *rqstp, struct nsm_args *argp,
				         struct nsm_res  *resp)
{
	dprintk("statd: proc %u        called\n", rqstp->rq_proc);
	resp->status = 0;
	resp->state = -1;
	return rpc_success;
}

/*
 * NSM XDR routines
 */
int
nsmsvc_decode_void(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

int
nsmsvc_encode_void(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

int
nsmsvc_decode_stat_chge(struct svc_rqst *rqstp, u32 *p, struct nsm_args *argp)
{
	char	*mon_name;
	__u32	mon_name_len;

	/* Skip over the client's mon_name */
	p = xdr_decode_string_inplace(p, &mon_name, &mon_name_len, SM_MAXSTRLEN);
	if (p == NULL)
		return 0;

	argp->state = ntohl(*p++);
	return xdr_argsize_check(rqstp, p);
}

int
nsmsvc_encode_res(struct svc_rqst *rqstp, u32 *p, struct nsm_res *resp)
{
	*p++ = resp->status;
	return xdr_ressize_check(rqstp, p);
}

int
nsmsvc_encode_stat_res(struct svc_rqst *rqstp, u32 *p, struct nsm_res *resp)
{
	*p++ = resp->status;
	*p++ = resp->state;
	return xdr_ressize_check(rqstp, p);
}

struct nsm_void			{ int dummy; };

#define PROC(name, xargt, xrest, argt, rest, respsize)	\
 { .pc_func	= (svc_procfunc) nsmsvc_proc_##name,	\
   .pc_decode	= (kxdrproc_t) nsmsvc_decode_##xargt,	\
   .pc_encode	= (kxdrproc_t) nsmsvc_encode_##xrest,	\
   .pc_release	= NULL,					\
   .pc_argsize	= sizeof(struct nsm_##argt),		\
   .pc_ressize	= sizeof(struct nsm_##rest),		\
   .pc_xdrressize = respsize,				\
 }

struct svc_procedure		nsmsvc_procedures[] = {
  PROC(null,		void,		void,		void,	void, 1),
  PROC(fail,		void,		stat_res,	void,	res, 2),
  PROC(fail,		void,		stat_res,	void,	res, 2),
  PROC(fail,		void,		res,		void,	res, 1),
  PROC(fail,		void,		res,		void,	res, 1),
  PROC(fail,		void,		res,		void,	res, 1),
  PROC(notify,		stat_chge,	void,		args,	void, 1)
};
