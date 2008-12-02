/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#include "qla_def.h"
#include <net/sock.h>
#include <net/netlink.h>
#include "qla_nlnk.h"

static struct sock *ql_fc_nl_sock = NULL;
static int ql_fc_nl_event(struct notifier_block *this,
			unsigned long event, void *ptr);

static struct notifier_block ql_fc_nl_notifier = {
	.notifier_call = ql_fc_nl_event,
};

/*
 * local functions
 */
static int ql_fc_proc_nl_rcv_msg(struct sk_buff *skb,
		struct nlmsghdr *nlh, int rcvlen);
static int ql_fc_nl_rsp(uint32_t pid, uint32_t seq, uint32_t type,
		void *hdr, int hdr_len, void *payload, int size);

static int qla84xx_update_fw(struct scsi_qla_host *ha, int rlen,
		struct msg_update_fw *upd_fw)
{
	struct qlfc_fw *qlfw;
	struct verify_chip_entry_84xx *mn;
	dma_addr_t mn_dma;
	int ret = 0;
	uint32_t fw_ver;
	uint16_t options;

	if (rlen < (sizeof(struct msg_update_fw) + upd_fw->len +
		offsetof(struct qla_fc_msg, u))){
		DEBUG2_16(printk(KERN_ERR "%s(%lu): invalid len\n",
			__func__, ha->host_no));
		return -EINVAL;
	}

	qlfw = &ha->fw_buf;
	if (!upd_fw->offset) {
		if (qlfw->fw_buf || !upd_fw->fw_len ||
			upd_fw->len > upd_fw->fw_len) {
			DEBUG2_16(printk(KERN_ERR "%s(%lu): invalid offset or "
			    "fw_len\n", __func__, ha->host_no));
			return -EINVAL;
		} else {
			qlfw->fw_buf = dma_alloc_coherent(&ha->pdev->dev,
						upd_fw->fw_len, &qlfw->fw_dma,
						GFP_KERNEL);
			if (qlfw->fw_buf == NULL) {
				DEBUG2_16(printk(KERN_ERR "%s: dma alloc "
				    "failed%lu\n", __func__, ha->host_no));
				return (-ENOMEM);
			}
			qlfw->len = upd_fw->fw_len;
		}
		fw_ver = le32_to_cpu(*((uint32_t *)
				((uint32_t *)upd_fw->fw_bytes + 2)));
		if (!fw_ver) {
			DEBUG2_16(printk(KERN_ERR "%s(%lu): invalid fw "
			    "revision 0x%x\n", __func__, ha->host_no, fw_ver));
			return -EINVAL;
		}
	} else {
		/* make sure we have a buffer allocated */
		if (!qlfw->fw_buf || upd_fw->fw_len != qlfw->len ||
			((upd_fw->offset + upd_fw->len) > upd_fw->fw_len)){
			DEBUG2_16(printk(KERN_ERR "%s(%lu): invalid size of "
			    "offset=0 expected\n", __func__, ha->host_no));
			return -EINVAL;
		}
	}
	/* Copy the firmware into DMA Buffer */
	memcpy(((uint8_t *)qlfw->fw_buf + upd_fw->offset),
		upd_fw->fw_bytes, upd_fw->len);

	if ((upd_fw->offset+upd_fw->len) != qlfw->len)
		return 0;

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		DEBUG2_16(printk(KERN_ERR "%s: dma alloc for fw buffer "
		    "failed%lu\n", __func__, ha->host_no));
		return -ENOMEM;
	}

	fw_ver = le32_to_cpu(*((uint32_t *)((uint32_t *)qlfw->fw_buf + 2)));

	/* Create iocb and issue it */
	memset(mn, 0, sizeof(*mn));

	mn->entry_type = VERIFY_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	options = VCO_FORCE_UPDATE | VCO_END_OF_DATA;
	if (upd_fw->diag_fw)
		options |= VCO_DIAG_FW;
	mn->options = cpu_to_le16(options);

	mn->fw_ver = cpu_to_le32(fw_ver);
	mn->fw_size = cpu_to_le32(qlfw->len);
	mn->fw_seq_size = cpu_to_le32(qlfw->len);

	mn->dseg_address[0] = cpu_to_le32(LSD(qlfw->fw_dma));
	mn->dseg_address[1] = cpu_to_le32(MSD(qlfw->fw_dma));
	mn->dseg_length = cpu_to_le32(qlfw->len);
	mn->data_seg_cnt = cpu_to_le16(1);

	ret = qla2x00_issue_iocb_timeout(ha, mn, mn_dma, 0, 120);

	if (ret != QLA_SUCCESS) {
		DEBUG2_16(printk(KERN_ERR "%s(%lu): failed\n", __func__,
		    ha->host_no));
	}

	qla_free_nlnk_dmabuf(ha);
	return ret;
}

static int
qla84xx_mgmt_cmd(scsi_qla_host_t *ha, struct qla_fc_msg *cmd, int rlen,
	uint32_t pid, uint32_t seq, uint32_t type)
{
	struct access_chip_84xx *mn;
	dma_addr_t mn_dma, mgmt_dma;
	void *mgmt_b = NULL;
	int ret = 0;
	int rsp_hdr_len, len = 0;
	struct qla84_msg_mgmt *ql84_mgmt;

	ql84_mgmt = &cmd->u.utok.mgmt;
	rsp_hdr_len = offsetof(struct qla_fc_msg, u) +
			offsetof(struct qla84_msg_mgmt, payload);

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		DEBUG2_16(printk(KERN_ERR "%s: dma alloc for fw buffer "
		    "failed%lu\n", __FUNCTION__, ha->host_no));
		return (-ENOMEM);
	}

	memset(mn, 0, sizeof (struct access_chip_84xx));

	mn->entry_type = ACCESS_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	switch (ql84_mgmt->cmd) {
	case QLA84_MGMT_READ_MEM:
		mn->options = cpu_to_le16(ACO_DUMP_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_WRITE_MEM:
		if (rlen < (sizeof(struct qla84_msg_mgmt) + ql84_mgmt->len +
			offsetof(struct qla_fc_msg, u))){
			ret = -EINVAL;
			goto exit_mgmt0;
		}
		mn->options = cpu_to_le16(ACO_LOAD_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_CHNG_CONFIG:
		mn->options = cpu_to_le16(ACO_CHANGE_CONFIG_PARAM);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.id);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param0);
		mn->parameter3 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param1);
		break;
	case QLA84_MGMT_GET_INFO:
		mn->options = cpu_to_le16(ACO_REQUEST_INFO);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.type);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.context);
		break;
	default:
		ret = -EIO;
		goto exit_mgmt0;
	}

	if ((len = ql84_mgmt->len) && ql84_mgmt->cmd != QLA84_MGMT_CHNG_CONFIG) {
		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, len,
				&mgmt_dma, GFP_KERNEL);
		if (mgmt_b == NULL) {
			DEBUG2_16(printk(KERN_ERR "%s: dma alloc mgmt_b "
			    "failed%lu\n", __func__, ha->host_no));
			ret = -ENOMEM;
			goto exit_mgmt0;
		}
		mn->total_byte_cnt = cpu_to_le32(ql84_mgmt->len);
		mn->dseg_count = cpu_to_le16(1);
		mn->dseg_address[0] = cpu_to_le32(LSD(mgmt_dma));
		mn->dseg_address[1] = cpu_to_le32(MSD(mgmt_dma));
		mn->dseg_length = cpu_to_le32(len);

		if (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM) {
			memcpy(mgmt_b, ql84_mgmt->payload, len);
		}
	}

	ret = qla2x00_issue_iocb(ha, mn, mn_dma, 0);
	cmd->error = ret;

	if ((ret != QLA_SUCCESS) ||
	    (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM) ||
	    (ql84_mgmt->cmd == QLA84_MGMT_CHNG_CONFIG)) {
		if (ret != QLA_SUCCESS)
			DEBUG2_16(printk(KERN_ERR "%s(%lu): failed\n",
			    __func__, ha->host_no));
		ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len, NULL, 0);
	} else if ((ql84_mgmt->cmd == QLA84_MGMT_READ_MEM)||
			(ql84_mgmt->cmd == QLA84_MGMT_GET_INFO)) {
		ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len, mgmt_b, len);
	}

	if (mgmt_b)
		dma_free_coherent(&ha->pdev->dev, len, mgmt_b, mgmt_dma);

exit_mgmt0:
	dma_pool_free(ha->s_dma_pool, mn, mn_dma);
	return ret;
}

/*
 * Netlink Interface Related Functions
 */

static void
ql_fc_nl_rcv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct scsi_nl_hdr *snlh;
	uint32_t rlen;
	int err;

	while (skb->len >= NLMSG_SPACE(0)) {
		err = 0;

		nlh = (struct nlmsghdr *) skb->data;

		if ((nlh->nlmsg_len < (sizeof(*nlh) + sizeof(*snlh))) ||
		    (skb->len < nlh->nlmsg_len)) {
			DEBUG2_16(printk(KERN_WARNING "%s: discarding partial "
			    "skb\n", __FUNCTION__));
			break;
		}

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len) {
			DEBUG2_16(printk(KERN_WARNING "%s: rlen > skb->len\n",
				 __FUNCTION__));
			rlen = skb->len;
		}

		if (nlh->nlmsg_type != FC_TRANSPORT_MSG) {
			DEBUG2_16(printk(KERN_WARNING "%s: Not "
			    "FC_TRANSPORT_MSG\n", __FUNCTION__));
			err = -EBADMSG;
			goto next_msg;
		}

		snlh = NLMSG_DATA(nlh);
		if ((snlh->version != SCSI_NL_VERSION) ||
		    (snlh->magic != SCSI_NL_MAGIC)) {
			DEBUG2_16(printk(KERN_WARNING "%s: Bad Version or "
			    "Magic number\n", __FUNCTION__));
			err = -EPROTOTYPE;
			goto next_msg;
		}
		err = ql_fc_proc_nl_rcv_msg(skb, nlh, rlen);
next_msg:
		if (err)
			netlink_ack(skb, nlh, err);
		skb_pull(skb, rlen);
	}
}

static int
ql_fc_proc_nl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, int rcvlen)
{
	struct scsi_nl_hdr *snlh;
	struct qla_fc_msg  *ql_cmd;
	struct Scsi_Host *shost;
	struct scsi_qla_host *ha;
	int err = 0;
	int rsp_hdr_len;

	snlh = NLMSG_DATA(nlh);

	/* Only vendor specific commands are supported */
	if (!(snlh->msgtype & FC_NL_VNDR_SPECIFIC))
		return -EBADMSG;

	ql_cmd = (struct qla_fc_msg *)((char *)snlh + sizeof (struct scsi_nl_hdr));

	if (ql_cmd->magic != QL_FC_NL_MAGIC)
		return -EBADMSG;

	shost = scsi_host_lookup(ql_cmd->host_no);
	if (IS_ERR(shost)) {
		DEBUG2_16(printk(KERN_ERR "%s: could not find host no %u\n",
		    __FUNCTION__, ql_cmd->host_no));
		err = -ENODEV;
		goto exit_proc_nl_rcv_msg;
	}

	ha = (struct scsi_qla_host *)shost->hostdata;

	if (!ha || (!IS_QLA84XX(ha) && (ql_cmd->cmd != QLFC_GET_AEN))) {
		DEBUG2_16(printk(KERN_ERR "%s: invalid host ha = %p dtype = "
		    "0x%x\n", __FUNCTION__, ha, (ha ? DT_MASK(ha): ~0)));
		err = -ENODEV;
		goto exit_proc_nl_rcv_msg;
	}

	switch (ql_cmd->cmd) {

	case QLA84_RESET:

		rsp_hdr_len = offsetof(struct qla_fc_msg, u);
		err = qla84xx_reset(ha, ql_cmd->u.utok.qla84_reset.diag_fw);
		ql_cmd->error = err;

		err = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
		break;

	case QLA84_UPDATE_FW:
		rsp_hdr_len = offsetof(struct qla_fc_msg, u);
		err = qla84xx_update_fw(ha, (rcvlen - sizeof(struct scsi_nl_hdr)),
				&ql_cmd->u.utok.qla84_update_fw);
		ql_cmd->error = err;

		err = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
		break;

	case QLA84_MGMT_CMD:
		err = qla84xx_mgmt_cmd(ha, ql_cmd,
				(rcvlen - sizeof(struct scsi_nl_hdr)),
				NETLINK_CREDS(skb)->pid,
				nlh->nlmsg_seq, (uint32_t)nlh->nlmsg_type);
		break;
	default:
		err = -EBADMSG;
	}

exit_proc_nl_rcv_msg:
	return err;
}

static int
ql_fc_nl_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	DEBUG16(printk(KERN_WARNING "%s: event 0x%lx ptr = %p\n", __func__,
	    event, ptr));
	return NOTIFY_DONE;
}

static int
ql_fc_nl_rsp(uint32_t pid, uint32_t seq, uint32_t type, void *hdr, int hdr_len,
	void *payload, int size)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int rc;
	int len = NLMSG_SPACE(size + hdr_len);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		DEBUG2_16(printk(KERN_ERR "%s: Could not alloc skb\n",
		    __func__));
		return -ENOMEM;
	}
	nlh = __nlmsg_put(skb, pid, seq, type, (len - sizeof(*nlh)), 0);
	nlh->nlmsg_flags = 0;
	memcpy(NLMSG_DATA(nlh), hdr, hdr_len);

	if (payload)
		memcpy((void *)((char *)(NLMSG_DATA(nlh)) + hdr_len), payload, size);

	rc = netlink_unicast(ql_fc_nl_sock, skb, pid, MSG_DONTWAIT);
	if (rc < 0) {
		DEBUG2_16(printk(KERN_ERR "%s: netlink_unicast failed\n",
		    __func__));
		return rc;
	}
	return 0;
}

void qla_free_nlnk_dmabuf(scsi_qla_host_t *ha)
{
	struct qlfc_fw *qlfw;

	qlfw = &ha->fw_buf;

	if (qlfw->fw_buf) {
		dma_free_coherent(&ha->pdev->dev, qlfw->len, qlfw->fw_buf,
			qlfw->fw_dma);
		memset(qlfw, 0, sizeof(struct qlfc_fw));
	}
}

int
ql_nl_register(void)
{
	int error = 0;

	error = netlink_register_notifier(&ql_fc_nl_notifier);
	if (!error) {

		ql_fc_nl_sock = netlink_kernel_create(&init_net,
		    NETLINK_FCTRANSPORT, QL_FC_NL_GROUP_CNT, ql_fc_nl_rcv_msg,
		    NULL, THIS_MODULE);

		if (!ql_fc_nl_sock) {
			netlink_unregister_notifier(&ql_fc_nl_notifier);
			error = -ENODEV;
		}
	}
	return (error);
}

void
ql_nl_unregister()
{
	if (ql_fc_nl_sock) {
		sock_release(ql_fc_nl_sock->sk_socket);
		netlink_unregister_notifier(&ql_fc_nl_notifier);
	}
}
