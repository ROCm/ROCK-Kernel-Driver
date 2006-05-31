/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 *
 * This code is licenced under the GPL.
 */

#include <linux/proc_fs.h>

#include "iscsi.h"
#include "iscsi_dbg.h"

struct proc_entries {
	const char *name;
	struct file_operations *fops;
};

static struct proc_entries iet_proc_entries[] =
{
	{"volume", &volume_seq_fops},
	{"session", &session_seq_fops},
};

static struct proc_dir_entry *proc_iet_dir;

void iet_procfs_exit(void)
{
	int i;

	if (!proc_iet_dir)
		return;

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++)
		remove_proc_entry(iet_proc_entries[i].name, proc_iet_dir);

	remove_proc_entry(proc_iet_dir->name, proc_iet_dir->parent);
}

int iet_procfs_init(void)
{
	int i;
	struct proc_dir_entry *ent;

	if (!(proc_iet_dir = proc_mkdir("net/iet", 0)))
		goto err;

	proc_iet_dir->owner = THIS_MODULE;

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++) {
		ent = create_proc_entry(iet_proc_entries[i].name, 0, proc_iet_dir);
		if (ent)
			ent->proc_fops = iet_proc_entries[i].fops;
		else
			goto err;
	}

	return 0;

err:
	if (proc_iet_dir)
		iet_procfs_exit();

	return -ENOMEM;
}

static int get_conn_info(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;
	struct iscsi_conn *conn;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;
	conn = conn_lookup(session, info.cid);

	info.cid = conn->cid;
	info.stat_sn = conn->stat_sn;
	info.exp_stat_sn = conn->exp_stat_sn;

	if (copy_to_user((void *) ptr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int add_conn(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	if (!(session = session_lookup(target, info.sid)))
		return -ENOENT;

	return conn_add(session, &info);
}

static int del_conn(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	if (!(session = session_lookup(target, info.sid)))
		return -ENOENT;

	return conn_del(session, &info);
}

static int get_session_info(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct session_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);

	if (!session)
		return -ENOENT;

	info.exp_cmd_sn = session->exp_cmd_sn;
	info.max_cmd_sn = session->max_cmd_sn;

	if (copy_to_user((void *) ptr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int add_session(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct session_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return session_add(target, &info);
}

static int del_session(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct session_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return session_del(target, info.sid);
}

static int add_volume(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct volume_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return volume_add(target, &info);
}

static int del_volume(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct volume_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return iscsi_volume_del(target, &info);
}

static int iscsi_param_config(struct iscsi_target *target, unsigned long ptr, int set)
{
	int err;
	struct iscsi_param_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		goto out;

	if ((err = iscsi_param_set(target, &info, set)) < 0)
		goto out;

	if (!set)
		err = copy_to_user((void *) ptr, &info, sizeof(info));

out:
	return err;
}

static int add_target(unsigned long ptr)
{
	int err;
	struct target_info info;

	if ((err = copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	if (!(err = target_add(&info)))
		err = copy_to_user((void *) ptr, &info, sizeof(info));

	return err;
}

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iscsi_target *target = NULL;
	long err;
	u32 id;

	if ((err = get_user(id, (u32 *) arg)) != 0)
		goto done;

	if (cmd == DEL_TARGET) {
		err = target_del(id);
		goto done;
	}

	target = target_lookup_by_id(id);

	if (cmd == ADD_TARGET)
		if (target) {
			err = -EEXIST;
			eprintk("Target %u already exist!\n", id);
			goto done;
		}

	switch (cmd) {
	case ADD_TARGET:
		assert(!target);
		err = add_target(arg);
		goto done;
	}

	if (!target) {
		eprintk("can't find the target %u\n", id);
		err = -EINVAL;
		goto done;
	}

	if ((err = target_lock(target, 1)) < 0) {
		eprintk("interrupted %ld %d\n", err, cmd);
		goto done;
	}

	switch (cmd) {
	case ADD_VOLUME:
		err = add_volume(target, arg);
		break;

	case DEL_VOLUME:
		err = del_volume(target, arg);
		break;

	case ADD_SESSION:
		err = add_session(target, arg);
		break;

	case DEL_SESSION:
		err = del_session(target, arg);
		break;

	case GET_SESSION_INFO:
		err = get_session_info(target, arg);
		break;

	case ISCSI_PARAM_SET:
		err = iscsi_param_config(target, arg, 1);
		break;

	case ISCSI_PARAM_GET:
		err = iscsi_param_config(target, arg, 0);
		break;

	case ADD_CONN:
		err = add_conn(target, arg);
		break;

	case DEL_CONN:
		err = del_conn(target, arg);
		break;

	case GET_CONN_INFO:
		err = get_conn_info(target, arg);
		break;

	}

	if (target)
		target_unlock(target);

done:
	return err;
}

struct file_operations ctr_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= ioctl,
	.compat_ioctl	= ioctl,
};
