/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <linux/file.h>
#include <linux/ip.h>
#include <net/tcp.h>

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "digest.h"

static void print_conn_state(char *p, size_t size, unsigned long state)
{
	if (test_bit(CONN_ACTIVE, &state))
		snprintf(p, size, "%s", "active");
	else if (test_bit(CONN_CLOSING, &state))
		snprintf(p, size, "%s", "closing");
	else
		snprintf(p, size, "%s", "unknown");
}

static void print_digest_state(char *p, size_t size, unsigned long flags)
{
	if (DIGEST_NONE & flags)
		snprintf(p, size, "%s", "none");
	else if (DIGEST_CRC32C & flags)
		snprintf(p, size, "%s", "crc32c");
	else
		snprintf(p, size, "%s", "unknown");
}

void conn_info_show(struct seq_file *seq, struct iscsi_session *session)
{
	struct iscsi_conn *conn;
	struct sock *sk;
	char buf[64];

	list_for_each_entry(conn, &session->conn_list, list) {
		sk = conn->sock->sk;
		switch (sk->sk_family) {
		case AF_INET:
			snprintf(buf, sizeof(buf),
				 "%u.%u.%u.%u", NIPQUAD(inet_sk(sk)->daddr));
			break;
		case AF_INET6:
			snprintf(buf, sizeof(buf),
				 "[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]",
				 NIP6(inet6_sk(sk)->daddr));
			break;
		default:
			break;
		}
		seq_printf(seq, "\t\tcid:%u ip:%s ", conn->cid, buf);
		print_conn_state(buf, sizeof(buf), conn->state);
		seq_printf(seq, "state:%s ", buf);
		print_digest_state(buf, sizeof(buf), conn->hdigest_type);
		seq_printf(seq, "hd:%s ", buf);
		print_digest_state(buf, sizeof(buf), conn->ddigest_type);
		seq_printf(seq, "dd:%s\n", buf);
	}
}

struct iscsi_conn *conn_lookup(struct iscsi_session *session, u16 cid)
{
	struct iscsi_conn *conn;

	list_for_each_entry(conn, &session->conn_list, list) {
		if (conn->cid == cid)
			return conn;
	}
	return NULL;
}

static void iet_state_change(struct sock *sk)
{
	struct iscsi_conn *conn = sk->sk_user_data;
	struct iscsi_target *target = conn->session->target;

	if (sk->sk_state != TCP_ESTABLISHED)
		conn_close(conn);
	else
		nthread_wakeup(target);

	target->nthread_info.old_state_change(sk);
}

static void iet_data_ready(struct sock *sk, int len)
{
	struct iscsi_conn *conn = sk->sk_user_data;
	struct iscsi_target *target = conn->session->target;

	nthread_wakeup(target);
	target->nthread_info.old_data_ready(sk, len);
}

static void iet_socket_bind(struct iscsi_conn *conn)
{
	int opt = 1;
	mm_segment_t oldfs;
	struct iscsi_session *session = conn->session;
	struct iscsi_target *target = session->target;

	dprintk(D_GENERIC, "%llu\n", (unsigned long long) session->sid);

	conn->sock = SOCKET_I(conn->file->f_dentry->d_inode);
	conn->sock->sk->sk_user_data = conn;

	write_lock(&conn->sock->sk->sk_callback_lock);
	target->nthread_info.old_state_change = conn->sock->sk->sk_state_change;
	conn->sock->sk->sk_state_change = iet_state_change;

	target->nthread_info.old_data_ready = conn->sock->sk->sk_data_ready;
	conn->sock->sk->sk_data_ready = iet_data_ready;
	write_unlock(&conn->sock->sk->sk_callback_lock);

	oldfs = get_fs();
	set_fs(get_ds());
	conn->sock->ops->setsockopt(conn->sock, SOL_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt));
	set_fs(oldfs);
}

int conn_free(struct iscsi_conn *conn)
{
	dprintk(D_GENERIC, "%p %#Lx %u\n", conn->session,
		(unsigned long long) conn->session->sid, conn->cid);

	assert(atomic_read(&conn->nr_cmnds) == 0);
	assert(list_empty(&conn->pdu_list));
	assert(list_empty(&conn->write_list));

	list_del(&conn->list);
	list_del(&conn->poll_list);

	digest_cleanup(conn);
	kfree(conn);

	return 0;
}

static int iet_conn_alloc(struct iscsi_session *session, struct conn_info *info)
{
	struct iscsi_conn *conn;

	dprintk(D_SETUP, "%#Lx:%u\n", (unsigned long long) session->sid, info->cid);

	conn = kmalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn)
		return -ENOMEM;
	memset(conn, 0, sizeof(*conn));

	conn->session = session;
	conn->cid = info->cid;
	conn->stat_sn = info->stat_sn;
	conn->exp_stat_sn = info->exp_stat_sn;

	conn->hdigest_type = info->header_digest;
	conn->ddigest_type = info->data_digest;
	if (digest_init(conn) < 0) {
		kfree(conn);
		return -ENOMEM;
	}

	spin_lock_init(&conn->list_lock);
	atomic_set(&conn->nr_cmnds, 0);
	atomic_set(&conn->nr_busy_cmnds, 0);
	INIT_LIST_HEAD(&conn->pdu_list);
	INIT_LIST_HEAD(&conn->write_list);
	INIT_LIST_HEAD(&conn->poll_list);

	list_add(&conn->list, &session->conn_list);

	set_bit(CONN_ACTIVE, &conn->state);

	conn->file = fget(info->fd);
	iet_socket_bind(conn);

	list_add(&conn->poll_list, &session->target->nthread_info.active_conns);

	nthread_wakeup(conn->session->target);

	return 0;
}

void conn_close(struct iscsi_conn *conn)
{
	if (test_and_clear_bit(CONN_ACTIVE, &conn->state))
		set_bit(CONN_CLOSING, &conn->state);

	nthread_wakeup(conn->session->target);
}

int conn_add(struct iscsi_session *session, struct conn_info *info)
{
	struct iscsi_conn *conn;
	int err = -EEXIST;

	if ((conn = conn_lookup(session, info->cid)))
		return err;

	return iet_conn_alloc(session, info);
}

int conn_del(struct iscsi_session *session, struct conn_info *info)
{
	struct iscsi_conn *conn;
	int err = -EEXIST;

	if (!(conn = conn_lookup(session, info->cid)))
		return err;

	conn_close(conn);

	return 0;
}
