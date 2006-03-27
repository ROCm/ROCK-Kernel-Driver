/*
 * taskstats.c - Export per-task statistics to userland
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/taskstats.h>
#include <linux/delayacct.h>
#include <net/genetlink.h>
#include <asm/atomic.h>

const int taskstats_version = TASKSTATS_VERSION;
static DEFINE_PER_CPU(__u32, taskstats_seqnum) = { 0 };
static int family_registered = 0;

static struct genl_family family = {
	.id             = GENL_ID_GENERATE,
	.name           = TASKSTATS_GENL_NAME,
	.version        = TASKSTATS_GENL_VERSION,
	.maxattr        = TASKSTATS_CMD_ATTR_MAX,
};

static struct nla_policy taskstats_cmd_get_policy[TASKSTATS_CMD_ATTR_MAX+1] __read_mostly = {
	[TASKSTATS_CMD_ATTR_PID]  = { .type = NLA_U32 },
	[TASKSTATS_CMD_ATTR_TGID] = { .type = NLA_U32 },
};


static int prepare_reply(struct genl_info *info, u8 cmd, struct sk_buff **skbp,
			 void **replyp, size_t size)
{
	struct sk_buff *skb;
	void *reply;

	/*
	 * If new attributes are added, please revisit this allocation
	 */
	skb = nlmsg_new(size);
	if (!skb)
		return -ENOMEM;

	if (!info) {
		int seq = get_cpu_var(taskstats_seqnum)++;
		put_cpu_var(taskstats_seqnum);

		reply = genlmsg_put(skb, 0, seq,
				    family.id, 0, 0,
				    cmd, family.version);
	} else
		reply = genlmsg_put(skb, info->snd_pid, info->snd_seq,
				    family.id, 0, 0,
				    cmd, family.version);
	if (reply == NULL) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	*skbp = skb;
	*replyp = reply;
	return 0;
}

static int send_reply(struct sk_buff *skb, pid_t pid, int event)
{
	struct genlmsghdr *genlhdr = nlmsg_data((struct nlmsghdr *)skb->data);
	void *reply;
	int rc;

	reply = genlmsg_data(genlhdr);

	rc = genlmsg_end(skb, reply);
	if (rc < 0) {
		nlmsg_free(skb);
		return rc;
	}

	if (event == TASKSTATS_MSG_MULTICAST)
		return genlmsg_multicast(skb, pid, TASKSTATS_LISTEN_GROUP);
	return genlmsg_unicast(skb, pid);
}

static inline int fill_pid(pid_t pid, struct task_struct *pidtsk,
			   struct taskstats *stats)
{
	int rc;
	struct task_struct *tsk = pidtsk;

	if (!pidtsk) {
		read_lock(&tasklist_lock);
		tsk = find_task_by_pid(pid);
		if (!tsk) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
		get_task_struct(tsk);
		read_unlock(&tasklist_lock);
	} else
		get_task_struct(tsk);

	rc = delayacct_add_tsk(stats, tsk);
	put_task_struct(tsk);

	return rc;

}

static inline int fill_tgid(pid_t tgid, struct task_struct *tgidtsk,
			    struct taskstats *stats)
{
	int rc;
	struct task_struct *tsk, *first;

	first = tgidtsk;
	read_lock(&tasklist_lock);
	if (!first) {
		first = find_task_by_pid(tgid);
		if (!first) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
	}
	tsk = first;
	do {
		rc = delayacct_add_tsk(stats, tsk);
		if (rc)
			break;
	} while_each_thread(first, tsk);
	read_unlock(&tasklist_lock);

	return rc;
}

static int taskstats_send_stats(struct sk_buff *skb, struct genl_info *info)
{
	int rc = 0;
	struct sk_buff *rep_skb;
	struct taskstats stats;
	void *reply;
	size_t size;
	struct nlattr *na;

	/*
	 * Size includes space for nested attribute as well
	 * The returned data is of the format
	 * TASKSTATS_TYPE_AGGR_PID/TGID
	 * --> TASKSTATS_TYPE_PID/TGID
	 * --> TASKSTATS_TYPE_STATS
	 */
	size = nla_total_size(sizeof(u32)) +
		nla_total_size(sizeof(struct taskstats)) + nla_total_size(0);

	memset(&stats, 0, sizeof(stats));
	rc = prepare_reply(info, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		return rc;

	if (info->attrs[TASKSTATS_CMD_ATTR_PID]) {
		u32 pid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_PID]);
		rc = fill_pid((pid_t)pid, NULL, &stats);
		if (rc < 0)
			goto err;

		na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_PID);
		NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_PID, pid);
	} else if (info->attrs[TASKSTATS_CMD_ATTR_TGID]) {
		u32 tgid = nla_get_u32(info->attrs[TASKSTATS_CMD_ATTR_TGID]);
		rc = fill_tgid((pid_t)tgid, NULL, &stats);
		if (rc < 0)
			goto err;

		na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_TGID);
		NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_TGID, tgid);
	} else {
		rc = -EINVAL;
		goto err;
	}

	NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS, stats);
	nla_nest_end(rep_skb, na);

	return send_reply(rep_skb, info->snd_pid, TASKSTATS_MSG_UNICAST);

nla_put_failure:
	return  genlmsg_cancel(rep_skb, reply);
err:
	nlmsg_free(rep_skb);
	return rc;
}


/* Send pid data out on exit */
void taskstats_exit_pid(struct task_struct *tsk)
{
	int rc = 0;
	struct sk_buff *rep_skb;
	void *reply;
	struct taskstats stats;
	size_t size;
	int is_thread_group = !thread_group_empty(tsk);
	struct nlattr *na;

	/*
	 * tasks can start to exit very early. Ensure that the family
	 * is registered before notifications are sent out
	 */
	if (!family_registered)
		return;

	/*
	 * Size includes space for nested attributes
	 */
	size = nla_total_size(sizeof(u32)) +
		nla_total_size(sizeof(struct taskstats)) + nla_total_size(0);

	if (is_thread_group)
		size = 2 * size;	// PID + STATS + TGID + STATS

	memset(&stats, 0, sizeof(stats));
	rc = prepare_reply(NULL, TASKSTATS_CMD_NEW, &rep_skb, &reply, size);
	if (rc < 0)
		return;

	rc = fill_pid(tsk->pid, tsk, &stats);
	if (rc < 0)
		goto err;

	na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_PID);
	NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_PID, (u32)tsk->pid);
	NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS, stats);
	nla_nest_end(rep_skb, na);

	if (!is_thread_group) {
		send_reply(rep_skb, 0, TASKSTATS_MSG_MULTICAST);
		return;
	}

	memset(&stats, 0, sizeof(stats));
	rc = fill_tgid(tsk->tgid, tsk, &stats);
	if (rc < 0)
		goto err;

	na = nla_nest_start(rep_skb, TASKSTATS_TYPE_AGGR_TGID);
	NLA_PUT_U32(rep_skb, TASKSTATS_TYPE_TGID, (u32)tsk->tgid);
	NLA_PUT_TYPE(rep_skb, struct taskstats, TASKSTATS_TYPE_STATS, stats);
	nla_nest_end(rep_skb, na);

	send_reply(rep_skb, 0, TASKSTATS_MSG_MULTICAST);
	return;

nla_put_failure:
	genlmsg_cancel(rep_skb, reply);
	return;
err:
	nlmsg_free(rep_skb);
}

static struct genl_ops taskstats_ops = {
	.cmd            = TASKSTATS_CMD_GET,
	.doit           = taskstats_send_stats,
	.policy		= taskstats_cmd_get_policy,
};

static int __init taskstats_init(void)
{
	if (genl_register_family(&family))
		return -EFAULT;
        family_registered = 1;

	if (genl_register_ops(&family, &taskstats_ops))
		goto err;

	return 0;
err:
	genl_unregister_family(&family);
	family_registered = 0;
	return -EFAULT;
}

late_initcall(taskstats_init);
