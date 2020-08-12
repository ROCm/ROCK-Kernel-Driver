// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/umh.h>
#include <linux/bpfilter.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include "msgfmt.h"

extern char bpfilter_umh_start;
extern char bpfilter_umh_end;

static void shutdown_umh(void)
{
	struct task_struct *tsk;

	if (bpfilter_ops.stop)
		return;

	tsk = get_pid_task(find_vpid(bpfilter_ops.info.pid), PIDTYPE_PID);
	if (tsk) {
		send_sig(SIGKILL, tsk, 1);
		put_task_struct(tsk);
	}
}

static void __stop_umh(void)
{
	if (IS_ENABLED(CONFIG_INET))
		shutdown_umh();
}

static int bpfilter_send_req(struct mbox_request *req)
{
	struct mbox_reply reply;
	loff_t pos = 0;
	ssize_t n;

	if (!bpfilter_ops.info.pid)
		return -EFAULT;
	pos = 0;
	n = kernel_write(bpfilter_ops.info.pipe_to_umh, req, sizeof(*req),
			   &pos);
	if (n != sizeof(*req)) {
		pr_err("write fail %zd\n", n);
		goto stop;
	}
	pos = 0;
	n = kernel_read(bpfilter_ops.info.pipe_from_umh, &reply, sizeof(reply),
			&pos);
	if (n != sizeof(reply)) {
		pr_err("read fail %zd\n", n);
		goto stop;
	}
	return reply.status;
stop:
	__stop_umh();
	return -EFAULT;
}

static int bpfilter_process_sockopt(struct sock *sk, int optname,
				    char __user *optval, unsigned int optlen,
				    bool is_set)
{
	struct mbox_request req = {
		.is_set		= is_set,
		.pid		= current->pid,
		.cmd		= optname,
		.addr		= (uintptr_t)optval,
		.len		= optlen,
	};
	if (uaccess_kernel()) {
		pr_err("kernel access not supported\n");
		return -EFAULT;
	}
	return bpfilter_send_req(&req);
}

static int start_umh(void)
{
	struct mbox_request req = { .pid = current->pid };
	int err;

	/* fork usermode process */
	err = fork_usermode_blob(&bpfilter_umh_start,
				 &bpfilter_umh_end - &bpfilter_umh_start,
				 &bpfilter_ops.info);
	if (err)
		return err;
	bpfilter_ops.stop = false;
	pr_info("Loaded bpfilter_umh pid %d\n", bpfilter_ops.info.pid);

	/* health check that usermode process started correctly */
	if (bpfilter_send_req(&req) != 0) {
		shutdown_umh();
		return -EFAULT;
	}

	return 0;
}

static int __init load_umh(void)
{
	int err;

	mutex_lock(&bpfilter_ops.lock);
	if (!bpfilter_ops.stop) {
		err = -EFAULT;
		goto out;
	}
	err = start_umh();
	if (!err && IS_ENABLED(CONFIG_INET)) {
		bpfilter_ops.sockopt = &bpfilter_process_sockopt;
		bpfilter_ops.start = &start_umh;
	}
out:
	mutex_unlock(&bpfilter_ops.lock);
	return err;
}

static void __exit fini_umh(void)
{
	mutex_lock(&bpfilter_ops.lock);
	if (IS_ENABLED(CONFIG_INET)) {
		shutdown_umh();
		bpfilter_ops.start = NULL;
		bpfilter_ops.sockopt = NULL;
	}
	mutex_unlock(&bpfilter_ops.lock);
}
module_init(load_umh);
module_exit(fini_umh);
MODULE_LICENSE("GPL");
