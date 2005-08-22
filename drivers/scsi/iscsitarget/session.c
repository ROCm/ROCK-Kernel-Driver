/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"

struct iscsi_session *session_lookup(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		if (session->sid == sid)
			return session;
	}
	return NULL;
}

static int iet_session_alloc(struct iscsi_target *target, struct session_info *info)
{
	int err, i;
	struct iscsi_session *session;

	dprintk(D_SETUP, "%p %u %#Lx\n", target, target->tid,
		(unsigned long long) info->sid);

	if (!(session = kmalloc(sizeof(*session), GFP_KERNEL)))
		return -ENOMEM;
	memset(session, 0, sizeof(*session));

	session->target = target;
	session->sid = info->sid;
	memcpy(&session->param, &target->sess_param, sizeof(session->param));
	session->max_queued_cmnds = target->trgt_param.queued_cmnds;

	session->exp_cmd_sn = info->exp_cmd_sn;
	session->max_cmd_sn = info->max_cmd_sn;

	session->initiator = kmalloc(sizeof(info->initiator_name), GFP_KERNEL);
	if (!session->initiator) {
		err = -ENOMEM;
		goto err;
	}
	memcpy(session->initiator, info->initiator_name, sizeof(info->initiator_name));

	INIT_LIST_HEAD(&session->conn_list);
	INIT_LIST_HEAD(&session->pending_list);

	spin_lock_init(&session->cmnd_hash_lock);
	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++)
		INIT_LIST_HEAD(&session->cmnd_hash[i]);

	session->next_ttt = 1;

	list_add(&session->list, &target->session_list);

	return 0;
err:
	if (session) {
		kfree(session->initiator);
		kfree(session);
	}
	return err;
}

int session_add(struct iscsi_target *target, struct session_info *info)
{
	struct iscsi_session *session;
	int err = -EEXIST;

	if ((session = session_lookup(target, info->sid)))
		return err;

	err = iet_session_alloc(target, info);

	return err;
}

static int session_free(struct iscsi_session *session)
{
	int i;

	dprintk(D_SETUP, "%#Lx\n", (unsigned long long) session->sid);

	assert(list_empty(&session->conn_list));

	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++) {
		if (!list_empty(&session->cmnd_hash[i]))
			BUG();
	}

	list_del(&session->list);

	kfree(session->initiator);
	kfree(session);

	return 0;
}

int session_del(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	if (!(session = session_lookup(target, sid)))
		return -ENOENT;

	if (!list_empty(&session->conn_list)) {
		eprintk("%llu still have connections\n", (unsigned long long) session->sid);
		return -EBUSY;
	}

	return session_free(session);
}

static void iet_session_info_show(struct seq_file *seq, struct iscsi_target *target)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		seq_printf(seq, "\tsid:%llu initiator:%s\n",
			   (unsigned long long) session->sid, session->initiator);
		conn_info_show(seq, session);
	}
}

static int iet_sessions_info_show(struct seq_file *seq, void *v)
{
	return iet_info_show(seq, iet_session_info_show);
}

static int iet_session_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, iet_sessions_info_show, NULL);
}

struct file_operations session_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= iet_session_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
