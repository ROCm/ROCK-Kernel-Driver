/* cxgb3i_iscsi.c: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include <linux/inet.h>
#include <linux/crypto.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/iscsi_proto.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "cxgb3i.h"

static struct scsi_transport_template *cxgb3i_scsi_transport;
static struct scsi_host_template cxgb3i_host_template;
static struct iscsi_transport cxgb3i_iscsi_transport;

static LIST_HEAD(cxgb3i_snic_list);
static DEFINE_RWLOCK(cxgb3i_snic_rwlock);

/**
 * cxgb3i_adapter_add - init a s3 adapter structure and any h/w settings
 * @snic:	pointer to adapter instance
 */
struct cxgb3i_adapter *cxgb3i_adapter_add(struct t3cdev *t3dev)
{
	struct cxgb3i_adapter *snic;
	struct adapter *adapter = tdev2adap(t3dev);
	int i;

	snic = kzalloc(sizeof(*snic), GFP_KERNEL);
	if (!snic) {
		cxgb3i_log_debug("cxgb3 %s, OOM.\n", t3dev->name);
		return NULL;
	}

	spin_lock_init(&snic->lock);
	snic->tdev = t3dev;
	snic->pdev = adapter->pdev;

	if (cxgb3i_adapter_ulp_init(snic))
		goto free_snic;

	for_each_port(adapter, i) {
		snic->hba[i] = cxgb3i_hba_host_add(snic, adapter->port[i]);
		if (!snic->hba[i])
			goto ulp_cleanup;
	}
	snic->hba_cnt = adapter->params.nports;

	/* add to the list */
	write_lock(&cxgb3i_snic_rwlock);
	list_add_tail(&snic->list_head, &cxgb3i_snic_list);
	write_unlock(&cxgb3i_snic_rwlock);

	return snic;

ulp_cleanup:
	cxgb3i_adapter_ulp_cleanup(snic);
free_snic:
	kfree(snic);
	return NULL;
}

/**
 * cxgb3i_snic_cleanup - release all the resources held and cleanup h/w settings
 * @snic:	pointer to adapter instance
 */
void cxgb3i_adapter_remove(struct t3cdev *t3dev)
{
	int i;
	struct cxgb3i_adapter *snic;

	/* remove from the list */
	read_lock(&cxgb3i_snic_rwlock);
	list_for_each_entry(snic, &cxgb3i_snic_list, list_head) {
		if (snic->tdev == t3dev) {
			list_del(&snic->list_head);
			break;
		}
	}
	write_unlock(&cxgb3i_snic_rwlock);

	if (snic) {
		for (i = 0; i < snic->hba_cnt; i++) {
			if (snic->hba[i]) {
				cxgb3i_hba_host_remove(snic->hba[i]);
				snic->hba[i] = NULL;
			}
		}

		/* release ddp resources */
		cxgb3i_adapter_ulp_cleanup(snic);
		kfree(snic);
	}
}

struct cxgb3i_hba *cxgb3i_hba_find_by_netdev(struct net_device *ndev)
{
	struct cxgb3i_adapter *snic;
	int i;

	read_lock(&cxgb3i_snic_rwlock);
	list_for_each_entry(snic, &cxgb3i_snic_list, list_head) {
		for (i = 0; i < snic->hba_cnt; i++) {
			if (snic->hba[i]->ndev == ndev) {
				read_unlock(&cxgb3i_snic_rwlock);
				return snic->hba[i];
			}
		}
	}
	read_unlock(&cxgb3i_snic_rwlock);
	return NULL;
}

struct cxgb3i_hba *cxgb3i_hba_host_add(struct cxgb3i_adapter *snic,
				       struct net_device *ndev)
{
	struct cxgb3i_hba *hba;
	struct Scsi_Host *shost;
	int err;

	shost = iscsi_host_alloc(&cxgb3i_host_template,
				 sizeof(struct cxgb3i_hba),
				 CXGB3I_SCSI_QDEPTH_DFLT);
	if (!shost) {
		cxgb3i_log_info("iscsi_host_alloc failed.\n");
		return NULL;
	}

	shost->transportt = cxgb3i_scsi_transport;
	shost->max_lun = CXGB3I_MAX_LUN;
	shost->max_id = CXGB3I_MAX_TARGET;
	shost->max_channel = 0;
	shost->max_cmd_len = 16;

	hba = iscsi_host_priv(shost);
	hba->snic = snic;
	hba->ndev = ndev;
	hba->shost = shost;

	pci_dev_get(snic->pdev);
	err = iscsi_host_add(shost, &snic->pdev->dev);
	if (err) {
		cxgb3i_log_info("iscsi_host_add failed.\n");
		goto pci_dev_put;
	}

	cxgb3i_log_debug("shost 0x%p, hba 0x%p, no %u.\n",
			 shost, hba, shost->host_no);

	return hba;

pci_dev_put:
	pci_dev_put(snic->pdev);
	scsi_host_put(shost);
	return NULL;
}

void cxgb3i_hba_host_remove(struct cxgb3i_hba *hba)
{
	cxgb3i_log_debug("shost 0x%p, hba 0x%p, no %u.\n",
			 hba->shost, hba, hba->shost->host_no);
	iscsi_host_remove(hba->shost);
	pci_dev_put(hba->snic->pdev);
	iscsi_host_free(hba->shost);
}

/**
 * cxgb3i_ep_connect - establish TCP connection to target portal
 * @dst_addr:		target IP address
 * @non_blocking:	blocking or non-blocking call
 *
 * Initiates a TCP/IP connection to the dst_addr
 */
static struct iscsi_endpoint *cxgb3i_ep_connect(struct sockaddr *dst_addr,
						int non_blocking)
{
	struct iscsi_endpoint *ep;
	struct cxgb3i_endpoint *cep;
	struct cxgb3i_hba *hba;
	struct s3_conn *c3cn = NULL;
	int err = 0;

	c3cn = cxgb3i_c3cn_create();
	if (!c3cn) {
		cxgb3i_log_info("ep connect OOM.\n");
		err = -ENOMEM;
		goto release_conn;
	}

	err = cxgb3i_c3cn_connect(c3cn, (struct sockaddr_in *)dst_addr);
	if (err < 0) {
		cxgb3i_log_info("ep connect failed.\n");
		goto release_conn;
	}
	hba = cxgb3i_hba_find_by_netdev(c3cn->dst_cache->dev);
	if (!hba) {
		err = -ENOSPC;
		cxgb3i_log_info("NOT going through cxgbi device.\n");
		goto release_conn;
	}
	if (c3cn_in_state(c3cn, C3CN_STATE_CLOSE)) {
		err = -ENOSPC;
		cxgb3i_log_info("ep connect unable to connect.\n");
		goto release_conn;
	}

	ep = iscsi_create_endpoint(sizeof(*cep));
	if (!ep) {
		err = -ENOMEM;
		cxgb3i_log_info("iscsi alloc ep, OOM.\n");
		goto release_conn;
	}
	cep = ep->dd_data;
	cep->c3cn = c3cn;
	cep->hba = hba;

	cxgb3i_log_debug("ep 0x%p, 0x%p, c3cn 0x%p, hba 0x%p.\n",
			  ep, cep, c3cn, hba);
	return ep;

release_conn:
	cxgb3i_log_debug("conn 0x%p failed, release.\n", c3cn);
	if (c3cn)
		cxgb3i_c3cn_release(c3cn);
	return ERR_PTR(err);
}

/**
 * cxgb3i_ep_poll - polls for TCP connection establishement
 * @ep:		TCP connection (endpoint) handle
 * @timeout_ms:	timeout value in milli secs
 *
 * polls for TCP connect request to complete
 */
static int cxgb3i_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct cxgb3i_endpoint *cep = ep->dd_data;
	struct s3_conn *c3cn = cep->c3cn;

	if (!c3cn_in_state(c3cn, C3CN_STATE_ESTABLISHED))
		return 0;
	cxgb3i_log_debug("ep 0x%p, c3cn 0x%p established.\n", ep, c3cn);
	return 1;
}

/**
 * cxgb3i_ep_disconnect - teardown TCP connection
 * @ep:		TCP connection (endpoint) handle
 *
 * teardown TCP connection
 */
static void cxgb3i_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct cxgb3i_endpoint *cep = ep->dd_data;
	struct cxgb3i_conn *cconn = cep->cconn;

	cxgb3i_log_debug("ep 0x%p, cep 0x%p.\n", ep, cep);

	if (cconn && cconn->conn) {
		struct iscsi_tcp_conn *tcp_conn = &cconn->tcp_conn;

		/*
		 * stop the xmit path so the xmit_segment function is
		 * not being called
		 */
		write_lock_bh(&cep->c3cn->callback_lock);
		set_bit(ISCSI_SUSPEND_BIT, &cconn->conn->suspend_rx);
		cep->c3cn->user_data = NULL;
		cconn->cep = NULL;
		tcp_conn->sock = NULL;
		write_unlock_bh(&cep->c3cn->callback_lock);
	}

	cxgb3i_log_debug("ep 0x%p, cep 0x%p, release c3cn 0x%p.\n",
			 ep, cep, cep->c3cn);
	cxgb3i_c3cn_release(cep->c3cn);
	iscsi_destroy_endpoint(ep);
}

/**
 * cxgb3i_session_create - create a new iscsi session
 * @cmds_max:		max # of commands
 * @qdepth:		scsi queue depth
 * @initial_cmdsn:	initial iscsi CMDSN for this session
 * @host_no:		pointer to return host no
 *
 * Creates a new iSCSI session
 */
static struct iscsi_cls_session *
cxgb3i_session_create(struct iscsi_endpoint *ep, u16 cmds_max, u16 qdepth,
		      u32 initial_cmdsn, u32 *host_no)
{
	struct cxgb3i_endpoint *cep;
	struct cxgb3i_hba *hba;
	struct Scsi_Host *shost;
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	int i;

	if (!ep) {
		cxgb3i_log_error("%s, missing endpoint.\n", __func__);
		return NULL;
	}

	cep = ep->dd_data;
	hba = cep->hba;
	shost = hba->shost;
	cxgb3i_log_debug("ep 0x%p, cep 0x%p, hba 0x%p.\n", ep, cep, hba);
	BUG_ON(hba != iscsi_host_priv(shost));

	*host_no = shost->host_no;

	cls_session = iscsi_session_setup(&cxgb3i_iscsi_transport, shost,
					  cmds_max,
					  sizeof(struct iscsi_tcp_task),
					  initial_cmdsn, ISCSI_MAX_TARGET);
	if (!cls_session)
		return NULL;
	session = cls_session->dd_data;

	for (i = 0; i < session->cmds_max; i++) {
		struct iscsi_task *task = session->cmds[i];
		struct iscsi_tcp_task *tcp_task = task->dd_data;

		task->hdr = &tcp_task->hdr.cmd_hdr;
		task->hdr_max = sizeof(tcp_task->hdr) - ISCSI_DIGEST_SIZE;
	}

	if (iscsi_r2tpool_alloc(session))
		goto remove_session;

	return cls_session;

remove_session:
	iscsi_session_teardown(cls_session);
	return NULL;
}

/**
 * cxgb3i_session_destroy - destroys iscsi session
 * @cls_session:	pointer to iscsi cls session
 *
 * Destroys an iSCSI session instance and releases its all resources held
 */
static void cxgb3i_session_destroy(struct iscsi_cls_session *cls_session)
{
	cxgb3i_log_debug("sess 0x%p.\n", cls_session);
	iscsi_r2tpool_free(cls_session->dd_data);
	iscsi_session_teardown(cls_session);
}

/**
 * cxgb3i_conn_create - create iscsi connection instance
 * @cls_session:	pointer to iscsi cls session
 * @cid:		iscsi cid
 *
 * Creates a new iSCSI connection instance for a given session
 */
static inline void cxgb3i_conn_max_xmit_dlength(struct iscsi_conn *conn)
{
	struct cxgb3i_conn *cconn = conn->dd_data;

	if (conn->max_xmit_dlength)
		conn->max_xmit_dlength = min_t(unsigned int,
						conn->max_xmit_dlength,
						cconn->hba->snic->tx_max_size -
						ISCSI_PDU_HEADER_MAX);
	else
		conn->max_xmit_dlength = cconn->hba->snic->tx_max_size -
						ISCSI_PDU_HEADER_MAX;
	cxgb3i_log_debug("conn 0x%p, max xmit %u.\n",
			 conn, conn->max_xmit_dlength);
}

static inline void cxgb3i_conn_max_recv_dlength(struct iscsi_conn *conn)
{
	struct cxgb3i_conn *cconn = conn->dd_data;

	if (conn->max_recv_dlength)
		conn->max_recv_dlength = min_t(unsigned int,
						conn->max_recv_dlength,
						cconn->hba->snic->rx_max_size -
						ISCSI_PDU_HEADER_MAX);
	else
		conn->max_recv_dlength = cconn->hba->snic->rx_max_size -
						ISCSI_PDU_HEADER_MAX;
	cxgb3i_log_debug("conn 0x%p, max recv %u.\n",
			 conn, conn->max_recv_dlength);
}

static struct iscsi_cls_conn *cxgb3i_conn_create(struct iscsi_cls_session
						 *cls_session, u32 cid)
{
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_conn *conn;
	struct cxgb3i_conn *cconn;

	cxgb3i_log_debug("sess 0x%p, cid %u.\n", cls_session, cid);

	cls_conn = iscsi_conn_setup(cls_session, sizeof(*cconn), cid);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;

	cconn = conn->dd_data;
	cconn->tcp_conn.iscsi_conn = conn;
	cconn->conn = conn;

	return cls_conn;
}

/**
 * cxgb3i_conn_xmit_segment - transmit segment
 * @conn:	pointer to iscsi conn
 */
static int cxgb3i_conn_xmit_segment(struct iscsi_conn *conn)
{
	struct cxgb3i_conn *cconn = conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = &cconn->tcp_conn;
	struct iscsi_segment *segment = &tcp_conn->out.segment;

	if (segment->total_copied < segment->total_size)
		return cxgb3i_conn_ulp2_xmit(conn);
	return 0;
}

/**
 * cxgb3i_conn_bind - binds iscsi sess, conn and endpoint together
 * @cls_session:	pointer to iscsi cls session
 * @cls_conn:		pointer to iscsi cls conn
 * @transport_eph:	64-bit EP handle
 * @is_leading:		leading connection on this session?
 *
 * Binds together an iSCSI session, an iSCSI connection and a
 *	TCP connection. This routine returns error code if the TCP
 *	connection does not belong on the device iSCSI sess/conn is bound
 */

static int cxgb3i_conn_bind(struct iscsi_cls_session *cls_session,
			    struct iscsi_cls_conn *cls_conn,
			    u64 transport_eph, int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct cxgb3i_conn *cconn = conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = &cconn->tcp_conn;
	struct iscsi_endpoint *ep;
	struct cxgb3i_endpoint *cep;
	struct s3_conn *c3cn;
	int err;

	ep = iscsi_lookup_endpoint(transport_eph);
	if (!ep)
		return -EINVAL;

	cxgb3i_log_debug("ep 0x%p, cls sess 0x%p, cls conn 0x%p.\n",
			 ep, cls_session, cls_conn);

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		return -EINVAL;

	cep = ep->dd_data;
	c3cn = cep->c3cn;

	read_lock(&c3cn->callback_lock);
	/* mnc: TODO don't abuse iscsi_tcp fields */
	tcp_conn->sock = (struct socket *)c3cn;
	c3cn->user_data = conn;
	read_unlock(&c3cn->callback_lock);

	cconn->hba = cep->hba;
	cconn->cep = cep;
	cep->cconn = cconn;

	cxgb3i_conn_max_xmit_dlength(conn);
	cxgb3i_conn_max_recv_dlength(conn);

	spin_lock_bh(&conn->session->lock);
	sprintf(conn->portal_address, NIPQUAD_FMT,
		NIPQUAD(c3cn->daddr.sin_addr.s_addr));
	conn->portal_port = ntohs(c3cn->daddr.sin_port);
	spin_unlock_bh(&conn->session->lock);

	tcp_conn->xmit_segment = cxgb3i_conn_xmit_segment;
	iscsi_tcp_hdr_recv_prep(tcp_conn);

	return 0;
}

/**
 * cxgb3i_conn_get_param - return iscsi connection parameter to caller
 * @cls_conn:	pointer to iscsi cls conn
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 *
 * returns iSCSI connection parameters
 */
static int cxgb3i_conn_get_param(struct iscsi_cls_conn *cls_conn,
				 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int len;

	cxgb3i_log_debug("cls_conn 0x%p, param %d.\n", cls_conn, param);

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%hu\n", conn->portal_port);
		spin_unlock_bh(&conn->session->lock);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%s\n", conn->portal_address);
		spin_unlock_bh(&conn->session->lock);
		break;
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}

	return len;
}

static int cxgb3i_conn_set_param(struct iscsi_cls_conn *cls_conn,
				 enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct cxgb3i_conn *cconn = conn->dd_data;
	int value, err = 0;

	switch (param) {
	case ISCSI_PARAM_HDRDGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->hdrdgst_en)
			cxgb3i_conn_ulp_setup(cconn, conn->hdrdgst_en,
					      conn->datadgst_en);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->datadgst_en)
			cxgb3i_conn_ulp_setup(cconn, conn->hdrdgst_en,
					      conn->datadgst_en);
		break;
	case ISCSI_PARAM_MAX_R2T:
		sscanf(buf, "%d", &value);
		if (value <= 0 || !is_power_of_2(value))
			return -EINVAL;
		if (session->max_r2t == value)
			break;
		iscsi_r2tpool_free(session);
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && iscsi_r2tpool_alloc(session))
			return -ENOMEM;
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		cxgb3i_conn_max_recv_dlength(conn);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		cxgb3i_conn_max_xmit_dlength(conn);
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}
	return err;
}

/**
 * cxgb3i_host_set_param - configure host (adapter) related parameters
 * @shost:	scsi host pointer
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 */
static int cxgb3i_host_set_param(struct Scsi_Host *shost,
				 enum iscsi_host_param param,
				 char *buf, int buflen)
{
	struct cxgb3i_hba *hba = iscsi_host_priv(shost);

	cxgb3i_log_debug("param %d, buf %s.\n", param, buf);

	if (hba && param == ISCSI_HOST_PARAM_IPADDRESS) {
		__be32 addr = in_aton(buf);
		cxgb3i_set_private_ipv4addr(hba->ndev, addr);
		return 0;
	}

	return iscsi_host_get_param(shost, param, buf);
}

/**
 * cxgb3i_host_get_param - returns host (adapter) related parameters
 * @shost:	scsi host pointer
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 */
static int cxgb3i_host_get_param(struct Scsi_Host *shost,
				 enum iscsi_host_param param, char *buf)
{
	struct cxgb3i_hba *hba = iscsi_host_priv(shost);
	int i;
	int len = 0;

	cxgb3i_log_debug("hba %s, param %d.\n", hba->ndev->name, param);

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		for (i = 0; i < 6; i++)
			len +=
			    sprintf(buf + len, "%02x.",
				    hba->ndev->dev_addr[i]);
		len--;
		buf[len] = '\0';
		break;
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		len = sprintf(buf, "%s\n", hba->ndev->name);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS:
	{
		__be32 addr;

		addr = cxgb3i_get_private_ipv4addr(hba->ndev);
		len = sprintf(buf, "%u.%u.%u.%u", NIPQUAD(addr));
		break;
	}
	default:
		return iscsi_host_get_param(shost, param, buf);
	}
	return len;
}

/**
 * cxgb3i_conn_get_stats - returns iSCSI stats
 * @cls_conn:	pointer to iscsi cls conn
 * @stats:	pointer to iscsi statistic struct
 */
static void cxgb3i_conn_get_stats(struct iscsi_cls_conn *cls_conn,
				  struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->digest_err = 0;
	stats->timeout_err = 0;
	stats->custom_length = 1;
	strcpy(stats->custom[0].desc, "eh_abort_cnt");
	stats->custom[0].value = conn->eh_abort_cnt;
}

static inline u32 tag_base(struct cxgb3i_tag_format *format,
			   unsigned int idx, unsigned int age)
{
	u32 sw_bits = idx | (age << format->idx_bits);
	u32 tag = sw_bits >> format->rsvd_shift;

	tag <<= format->rsvd_bits + format->rsvd_shift;
	tag |= sw_bits & ((1 << format->rsvd_shift) - 1);
	return tag;
}

static inline void cxgb3i_parse_tag(struct cxgb3i_tag_format *format,
				    u32 tag, u32 *rsvd_bits, u32 *sw_bits)
{
	if (rsvd_bits)
		*rsvd_bits = (tag >> format->rsvd_shift) & format->rsvd_mask;
	if (sw_bits) {
		*sw_bits = (tag >> (format->rsvd_shift + format->rsvd_bits))
			    << format->rsvd_shift;
		*sw_bits |= tag & ((1 << format->rsvd_shift) - 1);
	}
}


static void cxgb3i_parse_itt(struct iscsi_conn *conn, itt_t itt,
			     int *idx, int *age)
{
	struct cxgb3i_conn *cconn = conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	u32 sw_bits;

	cxgb3i_parse_tag(&snic->tag_format, itt, NULL, &sw_bits);
	if (idx)
		*idx = sw_bits & ISCSI_ITT_MASK;
	if (age)
		*age = (sw_bits >> snic->tag_format.idx_bits) & ISCSI_AGE_MASK;
}

static int cxgb3i_reserve_itt(struct iscsi_task *task, itt_t *hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *sess = conn->session;
	struct cxgb3i_conn *cconn = conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = &cconn->tcp_conn;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	u32 sw_tag = tag_base(&snic->tag_format, task->itt, sess->age);
	u32 tag = RESERVED_ITT;

	if (sc && (sc->sc_data_direction == DMA_FROM_DEVICE)) {
		struct s3_conn *c3cn = (struct s3_conn *)(tcp_conn->sock);
		tag =
		    cxgb3i_ddp_tag_reserve(snic, c3cn->tid, sw_tag,
					   scsi_out(sc)->length,
					   scsi_out(sc)->table.sgl,
					   scsi_out(sc)->table.nents);
	}
	if (tag == RESERVED_ITT)
		tag = sw_tag | (snic->tag_format.rsvd_mask <<
				snic->tag_format.rsvd_shift);
	*hdr_itt = htonl(tag);
	return 0;
}

static void cxgb3i_release_itt(struct iscsi_task *task, itt_t hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_conn *conn = task->conn;
	struct cxgb3i_conn *cconn = conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;

	hdr_itt = ntohl(hdr_itt);
	if (sc && (sc->sc_data_direction == DMA_FROM_DEVICE))
		cxgb3i_ddp_tag_release(snic, hdr_itt,
				       scsi_out(sc)->table.sgl,
				       scsi_out(sc)->table.nents);
}

/**
 * cxgb3i_host_template -- Scsi_Host_Template structure
 *	used when registering with the scsi mid layer
 */
static struct scsi_host_template cxgb3i_host_template = {
	.module = THIS_MODULE,
	.name = "Chelsio S3xx iSCSI Initiator",
	.proc_name = "cxgb3i",
	.queuecommand = iscsi_queuecommand,
	.change_queue_depth = iscsi_change_queue_depth,
	.can_queue = 128 * (ISCSI_DEF_XMIT_CMDS_MAX - 1),
	.sg_tablesize = SG_ALL,
	.max_sectors = 0xFFFF,
	.cmd_per_lun = ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler = iscsi_eh_abort,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_host_reset,
	.use_clustering = DISABLE_CLUSTERING,
	.this_id = -1,
};

static struct iscsi_transport cxgb3i_iscsi_transport = {
	.owner = THIS_MODULE,
	.name = "cxgb3i",
	.caps = CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST
	    | CAP_DATADGST | CAP_DIGEST_OFFLOAD,
	.param_mask = ISCSI_MAX_RECV_DLENGTH |
	    ISCSI_MAX_XMIT_DLENGTH |
	    ISCSI_HDRDGST_EN |
	    ISCSI_DATADGST_EN |
	    ISCSI_INITIAL_R2T_EN |
	    ISCSI_MAX_R2T |
	    ISCSI_IMM_DATA_EN |
	    ISCSI_FIRST_BURST |
	    ISCSI_MAX_BURST |
	    ISCSI_PDU_INORDER_EN |
	    ISCSI_DATASEQ_INORDER_EN |
	    ISCSI_ERL |
	    ISCSI_CONN_PORT |
	    ISCSI_CONN_ADDRESS |
	    ISCSI_EXP_STATSN |
	    ISCSI_PERSISTENT_PORT |
	    ISCSI_PERSISTENT_ADDRESS |
	    ISCSI_TARGET_NAME | ISCSI_TPGT |
	    ISCSI_USERNAME | ISCSI_PASSWORD |
	    ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
	    ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
	    ISCSI_LU_RESET_TMO |
	    ISCSI_PING_TMO | ISCSI_RECV_TMO |
	    ISCSI_IFACE_NAME | ISCSI_INITIATOR_NAME,
	.host_param_mask = ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
	    ISCSI_HOST_INITIATOR_NAME | ISCSI_HOST_NETDEV_NAME,
	.get_host_param = cxgb3i_host_get_param,
	.set_host_param = cxgb3i_host_set_param,
	/* session management */
	.create_session = cxgb3i_session_create,
	.destroy_session = cxgb3i_session_destroy,
	.get_session_param = iscsi_session_get_param,
	/* connection management */
	.create_conn = cxgb3i_conn_create,
	.bind_conn = cxgb3i_conn_bind,
	.destroy_conn = iscsi_conn_teardown,
	.start_conn = iscsi_conn_start,
	.stop_conn = iscsi_conn_stop,
	.get_conn_param = cxgb3i_conn_get_param,
	.set_param = cxgb3i_conn_set_param,
	.get_stats = cxgb3i_conn_get_stats,
	/* pdu xmit req. from user space */
	.send_pdu = iscsi_conn_send_pdu,
	/* task */
	.init_task = iscsi_tcp_task_init,
	.xmit_task = iscsi_tcp_task_xmit,
	.cleanup_task = iscsi_tcp_cleanup_task,
	.parse_itt = cxgb3i_parse_itt,
	.reserve_itt = cxgb3i_reserve_itt,
	.release_itt = cxgb3i_release_itt,
	/* TCP connect/disconnect */
	.ep_connect = cxgb3i_ep_connect,
	.ep_poll = cxgb3i_ep_poll,
	.ep_disconnect = cxgb3i_ep_disconnect,
	/* Error recovery timeout call */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

int cxgb3i_iscsi_init(void)
{
	cxgb3i_scsi_transport =
	    iscsi_register_transport(&cxgb3i_iscsi_transport);
	if (!cxgb3i_scsi_transport) {
		cxgb3i_log_error("Could not register cxgb3i transport.\n");
		return -ENODEV;
	}
	cxgb3i_log_debug("cxgb3i transport 0x%p.\n", cxgb3i_scsi_transport);
	return 0;
}

void cxgb3i_iscsi_cleanup(void)
{
	if (cxgb3i_scsi_transport) {
		cxgb3i_log_debug("cxgb3i transport 0x%p.\n",
				 cxgb3i_scsi_transport);
		iscsi_unregister_transport(&cxgb3i_iscsi_transport);
	}
}
