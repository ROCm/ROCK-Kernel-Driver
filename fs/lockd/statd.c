/*
 * linux/fs/lockd/statd.c
 *
 * Kernel-based status monitor. This is an alternative to
 * the code in mon.c.
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

static int	__nsm_monitor(struct nlm_host *host);
static int	__nsm_unmonitor(struct nlm_host *host);

/*
 * Initialize local NSM state variable
 */
int
nsm_kernel_statd_init(void)
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

	nsm_monitor = __nsm_monitor;
	nsm_unmonitor = __nsm_unmonitor;
	return 0;
}

/*
 * Build the NSM file name path
 */
static char *
nsm_get_name(const char *hostname)
{
	char	*name;

	if (strchr(hostname, '/') != NULL) {
		printk(KERN_NOTICE "lockd: invalid characters in hostname \"%s\"\n", hostname);
		return ERR_PTR(-EINVAL);
	}

	name = (char *) __get_free_page(GFP_KERNEL);
	if (name == NULL)
		return ERR_PTR(-ENOMEM);

	snprintf(name, PAGE_SIZE, "%s/%s", NSM_SM_PATH, hostname);
	return name;
}

static void
nsm_put_name(char *name)
{
	free_page((unsigned long) name);
}

/*
 * Create the NSM monitor file
 */
static int
nsm_create(const char *hostname)
{
	struct file	*filp;
	char		*filename;
	int		res = 0;

	dprintk("lockd: creating statd monitor file %s\n", hostname);

	filename = nsm_get_name(hostname);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	filp = filp_open(filename, O_CREAT|O_SYNC|O_RDWR, 0644);
	if (IS_ERR(filp)) {
		res = PTR_ERR(filp);
		printk(KERN_NOTICE
			"lockd/statd: failed to create %s: err=%d\n",
			filename, res);
	} else {
		fsync_super(filp->f_dentry->d_inode->i_sb);
		filp_close(filp, NULL);
	}

	nsm_put_name(filename);
	return res;
}

static int
nsm_unlink(const char *hostname)
{
	struct nameidata nd;
	struct inode	*inode = NULL;
	struct dentry	*dentry;
	char		*filename;
	int		res = 0;

	filename = nsm_get_name(hostname);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	if ((res = path_lookup(filename, LOOKUP_PARENT, &nd)) != 0)
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
			filename, res);
	}

	if (inode)
		iput(inode);
	nsm_put_name(filename);
	return res;
}

/*
 * Call nsm_create/nsm_unlink with CAP_DAC_OVERRIDE
 */
#define swap_ugid(type, var) { \
	type tmp = current->var; current->var = var; var = tmp; \
}

static int
with_privilege(int (*func)(const char *), const char *hostname)
{
	kernel_cap_t	cap = current->cap_effective;
	int		res = 0, mask;
	uid_t		fsuid = 0;
	gid_t		fsgid = 0;

	/* If we're unprivileged, a call to capable() will set the
	 * SUPERPRIV flag */
	mask = current->flags | ~PF_SUPERPRIV;

	/* Raise capability to that we're able to create/unlink the file.
	 * Set fsuid/fsgid to 0 so the file will be owned by root. */
	cap_raise(current->cap_effective, CAP_DAC_OVERRIDE);
	swap_ugid(uid_t, fsuid);
	swap_ugid(gid_t, fsgid);

	res = func(hostname);

	/* drop privileges */
	current->cap_effective = cap;
	swap_ugid(uid_t, fsuid);
	swap_ugid(gid_t, fsgid);

	/* Clear PF_SUPERPRIV unless it was set to begin with */
	current->flags &= mask;

	return res;
}

/*
 * Set up monitoring of a remote host
 * Note we hold the semaphore for the host table while
 * we're here.
 */
static int
__nsm_monitor(struct nlm_host *host)
{
	struct nsm_handle *nsm;
	int		res = 0;

	dprintk("lockd: nsm_monitor(%s)\n", host->h_name);
	if ((nsm = host->h_nsmhandle) == NULL)
		BUG();
	if (!nsm->sm_monitored) {
		res = with_privilege(nsm_create, nsm->sm_name);
		if (res >= 0) {
			nsm->sm_monitored = 1;
		} else {
			dprintk(KERN_NOTICE "nsm_monitor(%s) failed: errno=%d\n",
					nsm->sm_name, -res);
		}
	}

	return res;
}

/*
 * Cease to monitor remote host
 * Code stolen from sys_unlink.
 */
static int
__nsm_unmonitor(struct nlm_host *host)
{
	struct nsm_handle *nsm;
	int res = 0;

	nsm = host->h_nsmhandle;
	host->h_nsmhandle = NULL;

	/* If the host was invalidated due to lockd restart/shutdown,
	 * don't unmonitor it.
	 * (Strictly speaking, we would have to keep the SM file
	 * until the next reboot. The only way to achieve that
	 * would be to link the monitor file to sm.bak now.)
	 */
	if (nsm && atomic_read(&nsm->sm_count) == 1
	 && nsm->sm_monitored && !nsm->sm_sticky) {
		dprintk("lockd: nsm_unmonitor(%s)\n", host->h_name);

		nsm->sm_monitored = 0;
		res = with_privilege(nsm_unlink, nsm->sm_name);
		if (res < 0) {
			dprintk(KERN_NOTICE "nsm_unmonitor(%s) failed: errno=%d\n",
					nsm->sm_name, -res);
		}
	}

	nsm_release(nsm);
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
	dprintk("statd: NOTIFY        called\n");
	nlm_host_rebooted(argp->mon_name, argp->state);
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
static int
nsmsvc_decode_void(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

static int
nsmsvc_encode_void(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

static int
nsmsvc_decode_stat_chge(struct svc_rqst *rqstp, u32 *p, struct nsm_args *argp)
{
	__u32	mon_name_len;

	p = xdr_decode_string(p, &argp->mon_name, &mon_name_len, SM_MAXSTRLEN);
	if (p == NULL)
		return 0;

	argp->state = ntohl(*p++);
	return xdr_argsize_check(rqstp, p);
}

static int
nsmsvc_encode_res(struct svc_rqst *rqstp, u32 *p, struct nsm_res *resp)
{
	*p++ = resp->status;
	return xdr_ressize_check(rqstp, p);
}

static int
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
