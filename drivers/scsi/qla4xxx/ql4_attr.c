/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include <scsi/scsi_transport_iscsi.h>

/* Host attributes. */

static void qla4xxx_get_initiator_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_host(shost);

	/* &ha->iscsi_name[0]; */
	/*  ha->ip_address[0], ha->ip_address[1], ha->ip_address[2], ha->ip_address[3]) */
	/* ha->subnet_mask[0], ha->subnet_mask[1], ha->subnet_mask[2], ha->subnet_mask[3]) */
	/* init_fw_cb->GatewayIPAddr[0],
			init_fw_cb->GatewayIPAddr[1],
			init_fw_cb->GatewayIPAddr[2],
			init_fw_cb->GatewayIPAddr[3]) */

}

static void qla4xxx_get_initiator_alias(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_host(shost);
}

/* Target attributes. */
static void qla4xxx_get_isid(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb;
}

static void qla4xxx_get_tsih(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb = qla4xxx_lookup_ddb_by_fw_index(ha, starget->id);
}

static void qla4xxx_get_ip_address(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb = qla4xxx_lookup_ddb_by_fw_index(ha, starget->id);

	if (ddb)
		memcpy(&iscsi_sin_addr(starget), &ddb->ipaddress,
		    ISCSI_IPADDR_SIZE);
}

static void qla4xxx_get_port(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb = qla4xxx_lookup_ddb_by_fw_index(ha, starget->id);

	struct sockaddr_in *addr = (struct sockaddr_in *)&ddb->addr;
	iscsi_port(starget) = addr->sin_port;
}

static void qla4xxx_get_tpgt(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb = qla4xxx_lookup_ddb_by_fw_index(ha, starget->id);

	iscsi_tpgt(starget) = ddb->portal_group_tag;
}

static void qla4xxx_get_isid(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(shost);
	struct ddb_entry *ddb = qla4xxx_lookup_ddb_by_fw_index(ha, starget->id);

	memcpy(iscsi_isid(starget), ddb->isid, sizeof(ddb->isid));
}

static void qla4xxx_get_initial_r2t(struct scsi_target *starget)
{
}

static void qla4xxx_get_immediate_data(struct scsi_target *starget)
{
}

static void qla4xxx_get_header_digest(struct scsi_target *starget)
{
}

static void qla4xxx_get_data_digest(struct scsi_target *starget)
{
}

static void qla4xxx_get_max_burst_len(struct scsi_target *starget)
{
}

static void qla4xxx_get_first_burst_len(struct scsi_target *starget)
{
}

static void qla4xxx_get_max_recv_data_segment_len(struct scsi_target *starget)
{
}

static void qla4xxx_get_starget_loss_tmo(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(host);

	iscsi_starget_dev_loss_tmo(starget) = ha->port_down_retry_count + 5;
}

static void
qla4xxx_set_starget_loss_tmo(struct scsi_target *starget, uint32_t timeout)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(host);

	if (timeout)
		ha->port_down_retry_count = timeout;
	else
		ha->port_down_retry_count = 1;
}

static struct fc_function_template qla4xxx_transport_functions = {
	.get_isid = qla4xxx_get_isid,
	.show_isid = 1,
	.get_tsih = qla4xxx_get_tsih,
	.show_tsih = 1,
	.get_port = qla4xxx_get_target_port,
	.show_port = 1,
	.get_tpgt = qla4xxx_get_tpgt,
	.show_tpgt = 1,
	.get_ip_address = qla4xxx_get_target_ip_address,
	.show_ip_address = 1,
	.get_initial_r2t = qla4xxx_get_initial_r2t,
	.show_initial_r2t = 1,
	.get_immediate_data = qla4xxx_get_immediate_data,
	.show_immediate_data = 1,
	.get_header_digest = qla4xxx_get_header_digest,
	.show_header_digest = 1,
	.get_data_digest = qla4xxx_get_data_digest,
	.show_data_digest = 1,
	.get_max_burst_len = qla4xxx_get_max_burst_len,
	.show_max_burst_len = 1,
	.get_first_burst_len = qla4xxx_get_first_burst_len,
	.show_first_burst_len = 1,
	.get_max_recv_data_segment_len = qla4xxx_get_max_recv_data_segment_len,
	.show_max_recv_data_segment_len = 1,
	.get_target_name = qla4xxx_get_target_name,
	.show_target_name = 1,
	.get_target_alias = qla4xxx_get_target_alias,
	.show_target_alias = 1,
	.get_initiator_alias = qla4xxx_get_initiator_alias,
	.show_initiator_alias = 1,
	.get_initiator_name = qla4xxx_get_initiator_name,
	.show_initiator_name = 1,

	.get_starget_dev_loss_tmo = qla4xxx_get_starget_loss_tmo,
	.set_starget_dev_loss_tmo = qla4xxx_set_starget_loss_tmo,
	.show_starget_dev_loss_tmo = 1,

};

struct scsi_transport_template *qla4xxx_alloc_transport_tmpl(void)
{
	return (iscsi_attach_transport(&qla4xxx_transport_functions));
}

/* SYSFS attributes --------------------------------------------------------- */

static ssize_t
qla4xxx_sysfs_read_nvram(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint16_t *witer;
	unsigned long flags;
	uint16_t cnt;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != sizeof(nvram_t))
		return 0;

	/* Read NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla4xxx_lock_nvram_access(ha);
	witer = (uint16_t *) buf;
	for (cnt = 0; cnt < count / 2; cnt++) {
		*witer = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    cnt + ha->nvram_base));
		witer++;
	}
	qla4xxx_unlock_nvram_access(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (count);
}

static ssize_t
qla4xxx_sysfs_write_nvram(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint8_t *iter;
	uint16_t *witer;
	unsigned long flags;
	uint16_t cnt;
	uint8_t chksum;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != sizeof(nvram_t))
		return 0;

	/* Checksum NVRAM. */
	iter = (uint8_t *) buf;
	chksum = 0;
	for (cnt = 0; cnt < count - 1; cnt++)
		chksum += *iter++;
	chksum = ~chksum + 1;
	*iter = chksum;

	/* Write NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla4xxx_lock_nvram_access(ha);
	qla4xxx_release_nvram_protection(ha);
	witer = (uint16_t *) buf;
	for (cnt = 0; cnt < count / 2; cnt++) {
		qla4xxx_write_nvram_word(ha, cnt + ha->nvram_base,
		    cpu_to_le16(*witer));
		witer++;
	}
	qla4xxx_unlock_nvram_access(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (count);
}

static struct bin_attribute sysfs_nvram_attr = {
	.attr = {
		 .name = "nvram",
		 .mode = S_IRUSR | S_IWUSR,
		 .owner = THIS_MODULE,
		 },
	.size = sizeof(nvram_t),
	.read = qla4xxx_sysfs_read_nvram,
	.write = qla4xxx_sysfs_write_nvram,
};

void qla4xxx_alloc_sysfs_attr(scsi_qla_host_t * ha)
{
	struct Scsi_Host *host = ha->host;

	sysfs_create_bin_file(&host->shost_gendev.kobj, &sysfs_fw_dump_attr);
	sysfs_create_bin_file(&host->shost_gendev.kobj, &sysfs_nvram_attr);
}

void qla4xxx_free_sysfs_attr(scsi_qla_host_t * ha)
{
	struct Scsi_Host *host = ha->host;

	sysfs_remove_bin_file(&host->shost_gendev.kobj, &sysfs_fw_dump_attr);
	sysfs_remove_bin_file(&host->shost_gendev.kobj, &sysfs_nvram_attr);
}
