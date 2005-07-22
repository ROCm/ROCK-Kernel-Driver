/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE                                     *
 *                                                                            *
 * QLogic ISP4xxx device driver for Linux 2.4.x                               *
 * Copyright (C) 2004 Qlogic Corporation                                      *
 * (www.qlogic.com)                                                           *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the      *
 * Free Software Foundation; either version 2, or (at your option) any        *
 * later version.                                                             *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          *
 * General Public License for more details.                                   *
 *                                                                            *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *	qla4extioctl_query_hba_iscsi_node
 *	qla4extioctl_query_hba_iscsi_portal
 *	qla4extioctl_query_disc_iscsi_node
 *	qla4extioctl_query_disc_iscsi_portal
 *	qla4extioctl_query_driver
 *	qla4extioctl_query_fw
 *	qla4extioctl_query_chip
 *	qla4extioctl_query
 *	qla4extioctl_reg_aen
 *	qla4extioctl_get_aen
 *	qla4extioctl_get_statistics_gen
 *	qla4extioctl_get_statistics_iscsi
 *	qla4extioctl_get_device_entry_iscsi
 *	qla4extioctl_get_init_fw_iscsi
 *	qla4extioctl_get_isns_server
 *	qla4extioctl_get_isns_disc_targets
 *	qla4extioctl_get_data
 *	qla4extioctl_rst_statistics_gen
 *	qla4extioctl_rst_statistics_iscsi
 *	qla4extioctl_set_device_entry_iscsi
 *	qla4extioctl_set_init_fw_iscsi
 *	qla4extioctl_set_isns_server
 *	qla4extioctl_set_data
 *	qla4xxx_ioctl_sleep_done
 *	qla4xxx_ioctl_sem_init
 *	qla4xxx_scsi_pass_done
 *	qla4extioctl_scsi_passthru
 *	qla4extioctl_iscsi_passthru
 *	qla4extioctl_get_hbacnt
 *	qla4xxx_ioctl
 ****************************************************************************/

#include "ql4_def.h"

#include "ql4_ioctl.h"
#include "qlinioct.h"
#if defined(QLA_CONFIG_COMPAT)
#include "ql4_32ioctl.h"
#endif

#define QLA_IOCTL_SCRAP_SIZE		17000 /* scrap memory for local use. */
#define STATIC

/*
 * Externs from ql4_inioct.c
 */
extern int qla4intioctl_logout_iscsi(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);
extern int qla4intioctl_ping(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);
extern int qla4intioctl_get_data(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);
extern int qla4intioctl_set_data(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);
extern int qla4intioctl_hba_reset(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);
extern int qla4intioctl_copy_fw_flash(scsi_qla_host_t *, EXT_IOCTL_ISCSI *);

/*
 * extern from ql4_nfoioctl.c
 */
extern int qla4xxx_nfo_ioctl(struct scsi_device *, int, void *);

/* local function prototypes */
int
qla4xxx_ioctl(struct scsi_device *, int, void *);

/*
 * ioctl initialization
 */
static struct class_simple *apidev_class;
static int apidev_major;

static int 
apidev_ioctl(struct inode *inode, struct file *fp, unsigned int cmd,
    unsigned long arg) 
{
	return (qla4xxx_ioctl(NULL, (int)cmd, (void*)arg));
}

static struct file_operations apidev_fops = {
	.owner = THIS_MODULE,
	.ioctl = apidev_ioctl,
};

void *
ql4_kzmalloc(int siz, int code)
{
	void *		bp;

	if ((bp = kmalloc(siz, code)) != NULL) {
		memset(bp, 0, siz);
	}

	return (bp);
}


/*
 * qla4xxx_alloc_ioctl_mem
 *	Allocates memory needed by IOCTL code.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Returns:
 *	ql4xxx local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_alloc_ioctl_mem(scsi_qla_host_t *ha)
{
	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/* Allocate IOCTL DMA Buffer
	 * ------------------------- */
	ha->ioctl_dma_buf_len = DMA_BUFFER_SIZE;
	ha->ioctl_dma_bufv = pci_alloc_consistent(ha->pdev,
	    ha->ioctl_dma_buf_len, &ha->ioctl_dma_bufp);
	if (ha->ioctl_dma_bufv == NULL) {
		printk(KERN_WARNING
		    "qla4xxx(%d): Memory Allocation failed - "
		    "IOCTL DMA buffer.\n", ha->host_no);

		return QLA_ERROR;
	}

	memset(ha->ioctl_dma_bufv, 0, ha->ioctl_dma_buf_len);
	QL4PRINT(QLP4|QLP7,
	    printk("scsi%d: %s: IOCTL DMAv = 0x%p\n",
	    ha->host_no, __func__, ha->ioctl_dma_bufv));
	QL4PRINT(QLP4|QLP7,
	    printk("scsi%d: %s: IOCTL DMAp = 0x%lx\n",
	    ha->host_no, __func__, (unsigned long)ha->ioctl_dma_bufp));

	/* Allocate context memory buffer */
	ha->ioctl = QL_KMEM_ZALLOC(sizeof(hba_ioctl_context));
	if (ha->ioctl == NULL) {
		/* error */
		printk(KERN_WARNING
		    "ql4xxx(%d): ERROR in ioctl context allocation.\n",
		    ha->host_no);
		return QLA_ERROR;
	}

	/* Allocate AEN tracking buffer */
	ha->ioctl->aen_tracking_queue =
	    QL_KMEM_ZALLOC(EXT_DEF_MAX_AEN_QUEUE * sizeof(EXT_ASYNC_EVENT));
	if (ha->ioctl->aen_tracking_queue == NULL) {
		printk(KERN_WARNING
		    "ql4xxx(%d): ERROR in ioctl aen_queue allocation.\n",
		    ha->host_no);
		return QLA_ERROR;
	}

	/* Pick the largest size we'll need per ha of all ioctl cmds.
	 * Use this size when freeing.
	 */
	ha->ioctl->scrap_mem = QL_KMEM_ZALLOC(QLA_IOCTL_SCRAP_SIZE);
	if (ha->ioctl->scrap_mem == NULL) {
		printk(KERN_WARNING
		    "ql4xxx(%d): ERROR in ioctl scrap_mem allocation.\n",
		    ha->host_no);
		return QLA_ERROR;
	}
	ha->ioctl->scrap_mem_size = QLA_IOCTL_SCRAP_SIZE;
	ha->ioctl->scrap_mem_used = 0;

	QL4PRINT(QLP4|QLP7,
	    printk("scsi(%d): %s: scrap_mem_size=%d.\n",
	    ha->host_no, __func__, ha->ioctl->scrap_mem_size));

	QL4PRINT(QLP4,
	    printk("scsi(%d): %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));

	LEAVE(__func__);
	return QLA_SUCCESS;
}

/*
 * qla4xxx_free_ioctl_mem
 *	Frees memory used by IOCTL code for the specified ha.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla4xxx_free_ioctl_mem(scsi_qla_host_t *ha)
{
	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (ha->ioctl != NULL) {

		if (ha->ioctl->scrap_mem != NULL) {
			/* The size here must match up to what we
			 * allocated before.
			 */
			QL_KMEM_FREE(ha->ioctl->scrap_mem);
			ha->ioctl->scrap_mem = NULL;
			ha->ioctl->scrap_mem_size = 0;
		}

		if (ha->ioctl->aen_tracking_queue != NULL) {
			QL_KMEM_FREE(ha->ioctl->aen_tracking_queue);
			ha->ioctl->aen_tracking_queue = NULL;
		}

		QL_KMEM_FREE(ha->ioctl);
		ha->ioctl = NULL;
	}

	if (ha->ioctl_dma_bufv) {
		QL4PRINT(QLP4|QLP7,
		    printk("scsi%d: %s: freeing IOCTL DMA Buffers\n",
		    ha->host_no, __func__));
		pci_free_consistent(ha->pdev, ha->ioctl_dma_buf_len,
		    ha->ioctl_dma_bufv, ha->ioctl_dma_bufp);
	}
	ha->ioctl_dma_buf_len = 0;
	ha->ioctl_dma_bufv = 0;
	ha->ioctl_dma_bufp = 0;

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);
}

/*
 * qla4xxx_get_ioctl_scrap_mem
 *	Returns pointer to memory of the specified size from the scrap buffer.
 *	This can be called multiple times before the free call as long
 *	as the memory is to be used by the same ioctl command and
 *	there's still memory left in the scrap buffer.
 *
 * Input:
 *	ha = adapter state pointer.
 *	ppmem = pointer to return a buffer pointer.
 *	size = size of buffer to return.
 *
 * Returns:
 *	ql4xxx local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_get_ioctl_scrap_mem(scsi_qla_host_t *ha, void **ppmem, uint32_t size)
{
	int		ret = QLA_SUCCESS;
	uint32_t	free_mem;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	free_mem = ha->ioctl->scrap_mem_size - ha->ioctl->scrap_mem_used;

	if (free_mem >= size) {
		*ppmem = ha->ioctl->scrap_mem + ha->ioctl->scrap_mem_used;
		ha->ioctl->scrap_mem_used += size;
	} else {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi(%d): %s: no more scrap memory.\n",
		    ha->host_no, __func__));

		ret = QLA_ERROR;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return (ret);
}

/*
 * qla4xxx_free_ioctl_scrap_mem
 *	Makes the entire scrap buffer free for use.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Returns:
 *	ql4xxx local function return status code.
 *
 */
void
qla4xxx_free_ioctl_scrap_mem(scsi_qla_host_t *ha)
{
	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	memset(ha->ioctl->scrap_mem, 0, ha->ioctl->scrap_mem_size);
	ha->ioctl->scrap_mem_used = 0;

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);
}

int
qla4xxx_ioctl_init(void)
{
	void * tmp;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi: %s: entered.\n",
	    __func__));

	apidev_class = class_simple_create(THIS_MODULE, "qla4xxx");
	if (IS_ERR(apidev_class)) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s(): Unable to sysfs class for qla4xxx.\n",
		    __func__));

		apidev_class = NULL;
		return 1;
	}
	QL4PRINT(QLP4,
	    printk("scsi: %s: apidev_class=%p.\n",
	    __func__, apidev_class));

	apidev_major = register_chrdev(0, "qla4xxx", &apidev_fops);
	if (apidev_major < 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s(): Unable to register CHAR device (%d)\n",
		    __func__, apidev_major));

		class_simple_destroy(apidev_class);
		apidev_class = NULL;

		return apidev_major;
	}
	QL4PRINT(QLP4,
	    printk("scsi: %s: apidev_major=%d.\n",
	    __func__, apidev_major));

	tmp = class_simple_device_add(apidev_class, MKDEV(apidev_major, 0),
	    NULL, "qla4xxx");
	QL4PRINT(QLP4,
	    printk("scsi: %s: tmp=%p.\n",
	    __func__, tmp));

#if defined(QLA_CONFIG_COMPAT)
	ql4_apidev_init_32ioctl();
#endif

	QL4PRINT(QLP4,
	    printk("scsi: %s: exiting.\n",
	    __func__));
	LEAVE(__func__);

	return 0;
}

int
qla4xxx_ioctl_exit(void)
{
	ENTER(__func__);

	if (!apidev_class)
		return 1;

#if defined(QLA_CONFIG_COMPAT)
	ql4_apidev_cleanup_32ioctl();
#endif

	class_simple_device_remove(MKDEV(apidev_major, 0));

	unregister_chrdev(apidev_major, "qla4xxx");

	class_simple_destroy(apidev_class);

	apidev_class = NULL;

	LEAVE(__func__);

	return 0;
}

/*
 * ioctl support functions
 */

void *
Q64BIT_TO_PTR(uint64_t buf_addr)
{
#if defined(QLA_CONFIG_COMPAT) || !defined(CONFIG_64BIT)
	union ql_doublelong {
		struct {
			uint32_t        lsl;
			uint32_t        msl;
		} longs;
		uint64_t        dl;
	};

	union ql_doublelong tmpval;

	tmpval.dl = buf_addr;
#if defined(QLA_CONFIG_COMPAT)
	return((void *)(uint64_t)(tmpval.longs.lsl));
#else
	return((void *)(tmpval.longs.lsl));
#endif
#else
	return((void *)buf_addr);
#endif
}

/**************************************************************************
 * qla4extioctl_query_hba_iscsi_node
 *	This routine retrieves the HBA node properties
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_hba_iscsi_node(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	EXT_HBA_ISCSI_NODE	*phba_node = NULL;
	INIT_FW_CTRL_BLK	*init_fw_cb;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&phba_node,
	    sizeof(EXT_HBA_ISCSI_NODE))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_HBA_ISCSI_NODE)));
		goto exit_query_hba_node;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp || !ioctl->ResponseAdr) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory allocation problem\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_query_hba_node;
	}

	if (ioctl->ResponseLen < sizeof(EXT_HBA_ISCSI_NODE) ||
	    ha->ioctl_dma_buf_len < sizeof(*init_fw_cb)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_query_hba_node;
	}

	/*
	 * Send mailbox command
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];

		goto exit_query_hba_node;
	}

	/*
	 * Transfer data from Fw's DEV_DB_ENTRY buffer to IOCTL's
	 * EXT_HBA_ISCSI_NODE buffer
	 */
	init_fw_cb = (INIT_FW_CTRL_BLK *) ha->ioctl_dma_bufv;

	memset(phba_node, 0, sizeof(EXT_HBA_ISCSI_NODE));
	phba_node->PortNumber = le16_to_cpu(init_fw_cb->PortNumber);
	phba_node->NodeInfo.PortalCount = 1;

	memcpy(phba_node->NodeInfo.IPAddr.IPAddress, init_fw_cb->IPAddr,
	    sizeof(phba_node->NodeInfo.IPAddr.IPAddress));
	memcpy(phba_node->NodeInfo.iSCSIName, init_fw_cb->iSCSINameString,
	    sizeof(phba_node->NodeInfo.iSCSIName));
	memcpy(phba_node->NodeInfo.Alias, init_fw_cb->Alias,
	    sizeof(phba_node->NodeInfo.Alias));

	sprintf(phba_node->DeviceName, "/proc/scsi/qla4xxx/%d",
	    ha->host_no);

	/*
	 * Copy the IOCTL EXT_HBA_ISCSI_NODE buffer to the user's data space
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), phba_node,
	    ioctl->ResponseLen)) != 0) {
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: copy failed\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_query_hba_node;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_query_hba_node:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_hba_iscsi_portal
 *	This routine retrieves the HBA iSCSI portal properties
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_hba_iscsi_portal(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	EXT_HBA_ISCSI_PORTAL *phba_portal;
	FLASH_SYS_INFO *sys_info;
	uint32_t num_valid_ddb_entries;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (!ioctl->ResponseAdr) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: no response buffer found.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_query_hba_portal;
	}

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&phba_portal,
	    sizeof(EXT_HBA_ISCSI_PORTAL))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_HBA_ISCSI_PORTAL)));
		goto exit_query_hba_portal;
	}

	if (ioctl->ResponseLen < sizeof(*phba_portal)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_query_hba_portal;
	}

	/*
	 * Fill in EXT_HBA_ISCSI_PORTAL buffer
	 */
	memset(phba_portal, 0, sizeof(EXT_HBA_ISCSI_PORTAL));

	strcpy(phba_portal->DriverVersion, QLA4XXX_DRIVER_VERSION);
	sprintf(phba_portal->FWVersion, "%02d.%02d Patch %02d Build %02d",
		ha->firmware_version[0], ha->firmware_version[1],
		ha->patch_number, ha->build_number);

	/* ----- Get firmware state information ---- */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_FW_STATE;
	if (qla4xxx_mailbox_command(ha, 1, 4, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: MBOX_CMD_GET_FW_STATE "
		    "failed w/ status %04X\n",
		    ha->host_no, __func__, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
/* RLU: mailbox values should be stored in VendorSpecificStatus */
		goto exit_query_hba_portal;
	}

	switch (mbox_sts[1]) {
	case FW_STATE_READY:
		phba_portal->State = EXT_DEF_CARD_STATE_READY;
		break;
	case FW_STATE_CONFIG_WAIT:
		phba_portal->State = EXT_DEF_CARD_STATE_CONFIG_WAIT;
		break;
	case FW_STATE_WAIT_LOGIN:
		phba_portal->State = EXT_DEF_CARD_STATE_LOGIN;
		break;
	case FW_STATE_ERROR:
		phba_portal->State = EXT_DEF_CARD_STATE_ERROR;
		break;
	}

	switch (mbox_sts[3] & 0x0001) {
	case FW_ADDSTATE_COPPER_MEDIA:
		phba_portal->Type = EXT_DEF_TYPE_COPPER;
		break;
	case FW_ADDSTATE_OPTICAL_MEDIA:
		phba_portal->Type = EXT_DEF_TYPE_OPTICAL;
		break;
	}

	/* ----- Get ddb entry information ---- */
	if (qla4xxx_get_fwddb_entry(ha, 0, NULL, 0, &num_valid_ddb_entries,
	    NULL, NULL, NULL, NULL, NULL) == QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: qla4xxx_get_ddb_entry failed!\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->RequestLen = 0;
		ioctl->DetailStatus = ioctl->Instance;

		goto exit_query_hba_portal;
	}

	phba_portal->DiscTargetCount = (uint16_t) num_valid_ddb_entries;

	/* ----- Get flash sys info information ---- */
	sys_info = (FLASH_SYS_INFO *) ha->ioctl_dma_bufv;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = INT_ISCSI_SYSINFO_FLASH_OFFSET;
	mbox_cmd[4] = sizeof(*sys_info);

	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: MBOX_CMD_READ_FLASH failed w/ "
		    "status %04X\n",
		    ha->host_no, __func__, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
/* RLU: mailbox values should be stored in VendorSpecificStatus */

		goto exit_query_hba_portal;
	}

	phba_portal->SerialNum = le32_to_cpu(sys_info->serialNumber);
	memcpy(phba_portal->IPAddr.IPAddress, ha->ip_address,
	    MIN(sizeof(phba_portal->IPAddr.IPAddress), sizeof(ha->ip_address)));
	memcpy(phba_portal->MacAddr, sys_info->physAddr[0].address,
	    sizeof(phba_portal->MacAddr));
	memcpy(phba_portal->Manufacturer, sys_info->vendorId,
	    sizeof(phba_portal->Manufacturer));
	memcpy(phba_portal->Model, sys_info->productId,
	    sizeof(phba_portal->Model));

	/*memcpy(phba_portal->OptRomVersion, ?,
		sizeof(phba_portal->OptRomVersion)); */

	/*
	 * Copy the IOCTL EXT_HBA_ISCSI_PORTAL buffer to the user's data space
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    phba_portal, ioctl->ResponseLen)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_query_hba_portal;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_query_hba_portal:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_disc_iscsi_node
 *	This routine retrieves the properties of the attached devices
 *	registered as iSCSI nodes discovered by the HBA driver.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_disc_iscsi_node(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	DEV_DB_ENTRY *fw_ddb_entry = (DEV_DB_ENTRY *) ha->ioctl_dma_bufv;
	EXT_DISC_ISCSI_NODE *pdisc_node;
	ddb_entry_t *ddb_entry;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (ioctl->ResponseLen < sizeof(EXT_DISC_ISCSI_NODE)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_disc_node;
	}

	if (ha->ioctl_dma_buf_len < sizeof(DEV_DB_ENTRY)) {
		if (qla4xxx_resize_ioctl_dma_buf(ha, sizeof(DEV_DB_ENTRY)) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_NO_MEMORY;
			goto exit_disc_node;
		}
	}

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pdisc_node,
	    sizeof(EXT_DISC_ISCSI_NODE))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_DISC_ISCSI_NODE)));
		goto exit_disc_node;
	}

	/* ----- get device database entry info from firmware ---- */
	if (qla4xxx_get_fwddb_entry(ha, ioctl->Instance, fw_ddb_entry,
	    ha->ioctl_dma_bufp, NULL, NULL, NULL, NULL, NULL, NULL) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to get DEV_DB_ENTRY "
		    "info.\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->RequestLen = 0;
		ioctl->DetailStatus = ioctl->Instance;

		goto exit_disc_node;
	}

	/* --- Transfer data from Fw's DEV_DB_ENTRY buffer to
	*      IOCTL's EXT_DISC_ISCSI_PORTAL buffer --- */
	memset(pdisc_node, 0, sizeof(EXT_DISC_ISCSI_NODE));
	pdisc_node->NodeInfo.PortalCount = 1;
	pdisc_node->NodeInfo.IPAddr.Type = EXT_DEF_TYPE_ISCSI_IP;
	memcpy(pdisc_node->NodeInfo.IPAddr.IPAddress, fw_ddb_entry->ipAddr,
	    MIN(sizeof(pdisc_node->NodeInfo.IPAddr.IPAddress),
	    sizeof(fw_ddb_entry->ipAddr)));
	strncpy(pdisc_node->NodeInfo.Alias, fw_ddb_entry->iSCSIAlias,
	    MIN(sizeof(pdisc_node->NodeInfo.Alias),
	    sizeof(fw_ddb_entry->iSCSIAlias)));
	strncpy(pdisc_node->NodeInfo.iSCSIName, fw_ddb_entry->iscsiName,
	    MIN(sizeof(pdisc_node->NodeInfo.iSCSIName),
	    sizeof(fw_ddb_entry->iscsiName)));

	if ((ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, ioctl->Instance))==
	    NULL) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: device index [%d] not logged in. "
		    "Dummy target info returned.\n",
		    ha->host_no, __func__, ioctl->Instance));

		pdisc_node->SessionID       = 0xDEAD;
		pdisc_node->ConnectionID    = 0xDEAD;
		pdisc_node->PortalGroupID   = 0xDEAD;
		pdisc_node->ScsiAddr.Bus    = 0xFF;
		pdisc_node->ScsiAddr.Target = 0xFF;
		pdisc_node->ScsiAddr.Lun    = 0xFF;
	}
	else {
		pdisc_node->SessionID       = ddb_entry->target_session_id;
		pdisc_node->ConnectionID    = ddb_entry->connection_id;
		pdisc_node->PortalGroupID   = 0;
		pdisc_node->ScsiAddr.Bus    = ddb_entry->bus;
		pdisc_node->ScsiAddr.Target = ddb_entry->target;
		pdisc_node->ScsiAddr.Lun    = 0;
	}

	/* --- Copy Results to user space --- */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pdisc_node, sizeof(EXT_DISC_ISCSI_NODE))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: copy error to user space.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_disc_node;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_disc_node:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_disc_iscsi_portal
 *	This routine retrieves the properties of the iSCSI portal
 *	discovered by the HBA driver.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_disc_iscsi_portal(scsi_qla_host_t *ha,
    EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	DEV_DB_ENTRY *fw_ddb_entry = (DEV_DB_ENTRY *) ha->ioctl_dma_bufv;
	EXT_DISC_ISCSI_PORTAL *pdisc_portal;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pdisc_portal,
	    sizeof(EXT_DISC_ISCSI_PORTAL))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_DISC_ISCSI_PORTAL)));
		goto exit_disc_portal;
	}

	if (ioctl->ResponseLen < sizeof(EXT_DISC_ISCSI_PORTAL)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_disc_portal;
	}

	if (ha->ioctl_dma_buf_len < sizeof(DEV_DB_ENTRY)) {
		if (qla4xxx_resize_ioctl_dma_buf(ha, sizeof(DEV_DB_ENTRY)) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_NO_MEMORY;
			goto exit_disc_portal;
		}
	}

	/* ----- get device database entry info from firmware ---- */
	if (qla4xxx_get_fwddb_entry(ha, ioctl->Instance, fw_ddb_entry,
	    ha->ioctl_dma_bufp, NULL, NULL, NULL, NULL, NULL, NULL) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to get DEV_DB_ENTRY info.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->RequestLen = 0;
		ioctl->DetailStatus = ioctl->Instance;
		goto exit_disc_portal;
	}

	/* --- Transfer data from Fw's DEV_DB_ENTRY buffer to IOCTL's
	*      EXT_DISC_ISCSI_PORTAL buffer --- */
	memset(pdisc_portal, 0, sizeof(EXT_DISC_ISCSI_PORTAL));
	memcpy(pdisc_portal->IPAddr.IPAddress, fw_ddb_entry->ipAddr,
	    MIN(sizeof(pdisc_portal->IPAddr.IPAddress),
	    sizeof(fw_ddb_entry->ipAddr)));

	pdisc_portal->PortNumber = le16_to_cpu(fw_ddb_entry->portNumber);
	pdisc_portal->IPAddr.Type = EXT_DEF_TYPE_ISCSI_IP;
	pdisc_portal->NodeCount = 0;

	strncpy(pdisc_portal->HostName, fw_ddb_entry->iscsiName,
	    MIN(sizeof(pdisc_portal->HostName),
	    sizeof(fw_ddb_entry->iscsiName)));

	/* --- Copy Results to user space --- */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	     pdisc_portal, sizeof(EXT_DISC_ISCSI_PORTAL))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: copy error to user space.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_disc_portal;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_disc_portal:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_driver
 *	This routine retrieves the driver properties.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_driver(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	EXT_DRIVER_INFO *pdinfo;
	int status = 0;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pdinfo,
	    sizeof(EXT_DRIVER_INFO))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_DRIVER_INFO)));
		goto exit_query_driver;
	}

	if (ioctl->ResponseLen < sizeof(EXT_DRIVER_INFO)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_query_driver;
	}

	memset(pdinfo, 0, sizeof(EXT_DRIVER_INFO));
	memcpy(pdinfo->Version, QLA4XXX_DRIVER_VERSION,
	       sizeof(QLA4XXX_DRIVER_VERSION));

	pdinfo->NumOfBus        = EXT_DEF_MAX_HBA;
	pdinfo->TargetsPerBus   = EXT_DEF_MAX_TARGET;
	pdinfo->LunPerTarget    = EXT_DEF_MAX_LUN;
	pdinfo->LunPerTargetOS  = EXT_DEF_MAX_BUS;

	if (sizeof(dma_addr_t) > 4)
		pdinfo->DmaBitAddresses = 1;  /* 64-bit */
	else
		pdinfo->DmaBitAddresses = 0;  /* 32-bit */

	if (ha->mem_addr)
		pdinfo->IoMapType       = 1;
	else
		pdinfo->IoMapType       = 0;

	//FIXME: Incomplete
	//pdinfo->MaxTransferLen  = ?;
	//pdinfo->MaxDataSegments = ?;
	//pdinfo->Attrib          = ?;
	//pdinfo->InternalFlags   = ?;

	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), pdinfo,
	    sizeof(EXT_DRIVER_INFO))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi(%d): %s: error copy to response buffer.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_query_driver;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_query_driver:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_fw
 *	This routine retrieves the firmware properties.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_fw(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	EXT_FW_INFO *pfw_info;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = 0;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pfw_info,
	    sizeof(EXT_FW_INFO))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_FW_INFO)));
		goto exit_query_fw;
	}

	if (ioctl->ResponseLen < sizeof(EXT_FW_INFO)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_query_fw;
	}

	/* Fill in structure */
	memset(pfw_info, 0, sizeof(EXT_FW_INFO));

	/* ----- Get firmware version information ---- */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_ABOUT_FW;

	/*
	 * NOTE: In QLA4010, mailboxes 2 & 3 may hold an address for data.
	 * Make sure that we write 0 to those mailboxes, if unused.
	 */
	if (qla4xxx_mailbox_command(ha, 4, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: MBOX_CMD_ABOUT_FW failed w/ "
		    "status %04X\n",
		    ha->host_no, __func__, mbox_sts[0]));
		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
/* RLU: mailbox values should be stored in VendorSpecificStatus */
		goto exit_query_fw;
	}

	sprintf(pfw_info->Version, "FW Version %d.%d Patch %d Build %d",
	    mbox_sts[1], mbox_sts[2], mbox_sts[3], mbox_sts[4]);

	/* Copy info to caller */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), pfw_info,
	    sizeof(EXT_FW_INFO))) != 0) {
		ioctl->Status = EXT_STATUS_COPY_ERR;
		QL4PRINT(QLP2|QLP4,
		    printk("scsi(%d): %s: response copy error.\n",
		    ha->host_no, __func__));

		goto exit_query_fw;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_query_fw:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query_chip
 *	This routine retrieves the chip properties.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query_chip(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	EXT_CHIP_INFO	*pchip_info;
	FLASH_SYS_INFO	*sys_info;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pchip_info,
	    sizeof(EXT_CHIP_INFO))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_CHIP_INFO)));
		goto exit_query_chip;
	}

	if (!ioctl->ResponseAdr || ioctl->ResponseLen < sizeof(EXT_CHIP_INFO)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_query_chip;
	}

	/* Fill in structure */
	memset(pchip_info, 0, sizeof(EXT_CHIP_INFO));

	/* ----- Get flash sys info information ---- */
	sys_info = (FLASH_SYS_INFO *) ha->ioctl_dma_bufv;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = INT_ISCSI_SYSINFO_FLASH_OFFSET;
	mbox_cmd[4] = sizeof(*sys_info);

	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: MBOX_CMD_READ_FLASH failed "
		    "w/ status %04X\n",
		    ha->host_no, __func__, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
/* RLU: mailbox values should be stored in VendorSpecificStatus */
		goto exit_query_chip;
	}

	pchip_info->VendorId    = le32_to_cpu(sys_info->pciDeviceVendor);
	pchip_info->DeviceId    = le32_to_cpu(sys_info->pciDeviceId);
	pchip_info->SubVendorId = le32_to_cpu(sys_info->pciSubsysVendor);
	pchip_info->SubSystemId = le32_to_cpu(sys_info->pciSubsysId);

	/* ----- Get firmware state information ---- */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_FW_STATE;
	if (qla4xxx_mailbox_command(ha, 1, 4, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: MBOX_CMD_GET_FW_STATE failed "
		    "w/ status %04X\n",
		    ha->host_no, __func__, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
/* RLU: mailbox values should be stored in VendorSpecificStatus */
		goto exit_query_chip;
	}

	pchip_info->BoardID     = mbox_sts[2];

	/* Copy info to caller */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pchip_info, sizeof(EXT_CHIP_INFO))) != 0) {
		ioctl->Status = EXT_STATUS_COPY_ERR;
		QL4PRINT(QLP2|QLP4,
		    printk("scsi(%d): %s: response copy error.\n",
		    ha->host_no, __func__));

		goto exit_query_chip;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_query_chip:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_query
 *	This routine calls query IOCTLs based on the IOCTL Sub Code.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *    	-EINVAL     = if the command is invalid
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_query(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	switch (ioctl->SubCode) {
	case EXT_SC_QUERY_HBA_ISCSI_NODE:
		return(qla4extioctl_query_hba_iscsi_node(ha, ioctl));

	case EXT_SC_QUERY_HBA_ISCSI_PORTAL:
		return(qla4extioctl_query_hba_iscsi_portal(ha, ioctl));

	case EXT_SC_QUERY_DISC_ISCSI_NODE:
		return(qla4extioctl_query_disc_iscsi_node(ha, ioctl));

	case EXT_SC_QUERY_DISC_ISCSI_PORTAL:
		return(qla4extioctl_query_disc_iscsi_portal(ha, ioctl));

	case EXT_SC_QUERY_DRIVER:
		return(qla4extioctl_query_driver(ha, ioctl));

	case EXT_SC_QUERY_FW:
		return(qla4extioctl_query_fw(ha, ioctl));

	case EXT_SC_QUERY_CHIP:
		return(qla4extioctl_query_chip(ha, ioctl));

	default:
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: unsupported query sub-command "
		    "code (%x)\n",
		    ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		return(0);
	}
}

/**************************************************************************
 * qla4extioctl_reg_aen
 *	This routine enables/disables storing of asynchronous events
 *	from the ISP into the driver's internal buffer.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_reg_aen(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;

	ENTER(__func__);

	ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: UNSUPPORTED\n", ha->host_no, __func__));

	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_aen
 *	This routine retrieves the contents of the driver's internal
 *	asynchronous event tracking queue.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_aen(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;

	ENTER("qla4extioctl_get_aen");

	ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: UNSUPPORTED\n", ha->host_no, __func__));

	LEAVE("qla4extioctl_get_aen");

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_statistics_gen
 *	This routine retrieves the HBA general statistical information.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_statistics_gen(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int			status = 0;
	EXT_HBA_PORT_STAT_GEN	*pstat_gen;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pstat_gen,
	    sizeof(EXT_HBA_PORT_STAT_GEN))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_HBA_PORT_STAT_GEN)));
		goto exit_get_stat_gen;
	}

	if (ioctl->ResponseLen < sizeof(EXT_HBA_PORT_STAT_GEN)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_get_stat_gen;
	}

	/*
	 * Fill in the data
	 */
	memset(pstat_gen, 0, sizeof(EXT_HBA_PORT_STAT_GEN));
	pstat_gen->HBAPortErrorCount     = ha->adapter_error_count;
	pstat_gen->DevicePortErrorCount  = ha->device_error_count;
	pstat_gen->IoCount               = ha->total_io_count;
	pstat_gen->MBytesCount           = ha->total_mbytes_xferred;
	pstat_gen->InterruptCount        = ha->isr_count;
	pstat_gen->LinkFailureCount      = ha->link_failure_count;
	pstat_gen->InvalidCrcCount       = ha->invalid_crc_count;

	/*
	 * Copy the IOCTL EXT_HBA_PORT_STAT_GEN buffer to the user's data space
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), pstat_gen,
	    ioctl->ResponseLen)) != 0) {
		QL4PRINT(QLP2, printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_stat_gen;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_get_stat_gen:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_statistics_iscsi
 *	This routine retrieves the HBA iSCSI statistical information.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_statistics_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	EXT_HBA_PORT_STAT_ISCSI* pstat_local;
	EXT_HBA_PORT_STAT_ISCSI* pstat_user;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	pstat_user = kmalloc(sizeof(EXT_HBA_PORT_STAT_ISCSI), GFP_ATOMIC);

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pstat_user,
	    sizeof(EXT_HBA_PORT_STAT_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_HBA_PORT_STAT_ISCSI)));
		goto exit_get_stats_iscsi;
	}

	if (!ioctl->ResponseAdr || !ioctl->ResponseLen) {
		QL4PRINT(QLP2, printk("scsi%d: %s: invalid parameter\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_INVALID_PARAM;
		goto exit_get_stats_iscsi;
	}

	if (ioctl->ResponseLen < sizeof(EXT_HBA_PORT_STAT_ISCSI)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: RespLen too small (0x%x),  "
		    "need (0x%x).\n",
		    ha->host_no, __func__, ioctl->ResponseLen,
		    (unsigned int) sizeof(EXT_HBA_PORT_STAT_ISCSI)));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_get_stats_iscsi;
	}

	if ((ha->ioctl_dma_buf_len < sizeof(EXT_HBA_PORT_STAT_ISCSI)) &&
	    (qla4xxx_resize_ioctl_dma_buf(ha, sizeof(EXT_HBA_PORT_STAT_ISCSI))
	     != QLA_SUCCESS)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: unable to allocate memory "
		    "for dma buffer.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_get_stats_iscsi;
	}

	/*
	 * Make the mailbox call
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_MANAGEMENT_DATA;
	mbox_cmd[1] = ioctl->Instance;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: get mngmt data for index [%d] failed "
		    "w/ mailbox ststus 0x%x\n",
		    ha->host_no, __func__, ioctl->Instance, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_get_stats_iscsi;
	}

	pstat_local = (EXT_HBA_PORT_STAT_ISCSI *) ha->ioctl_dma_bufv;
	memset(pstat_user, 0, sizeof(EXT_HBA_PORT_STAT_ISCSI));
	pstat_user->MACTxFramesCount          =
	    le64_to_cpu(pstat_local->MACTxFramesCount);
	pstat_user->MACTxBytesCount           =
	    le64_to_cpu(pstat_local->MACTxBytesCount);
	pstat_user->MACRxFramesCount          =
	    le64_to_cpu(pstat_local->MACRxFramesCount);
	pstat_user->MACRxBytesCount           =
	    le64_to_cpu(pstat_local->MACRxBytesCount);
	pstat_user->MACCRCErrorCount          =
	    le64_to_cpu(pstat_local->MACCRCErrorCount);
	pstat_user->MACEncodingErrorCount     =
	    le64_to_cpu(pstat_local->MACEncodingErrorCount);
	pstat_user->IPTxPacketsCount          =
	    le64_to_cpu(pstat_local->IPTxPacketsCount);
	pstat_user->IPTxBytesCount            =
	    le64_to_cpu(pstat_local->IPTxBytesCount);
	pstat_user->IPTxFragmentsCount        =
	    le64_to_cpu(pstat_local->IPTxFragmentsCount);
	pstat_user->IPRxPacketsCount          =
	    le64_to_cpu(pstat_local->IPRxPacketsCount);
	pstat_user->IPRxBytesCount            =
	    le64_to_cpu(pstat_local->IPRxBytesCount);
	pstat_user->IPRxFragmentsCount        =
	    le64_to_cpu(pstat_local->IPRxFragmentsCount);
	pstat_user->IPDatagramReassemblyCount =
	    le64_to_cpu(pstat_local->IPDatagramReassemblyCount);
	pstat_user->IPv6RxPacketsCount        =
	    le64_to_cpu(pstat_local->IPv6RxPacketsCount);
	pstat_user->IPRxPacketErrorCount      =
	    le64_to_cpu(pstat_local->IPRxPacketErrorCount);
	pstat_user->IPReassemblyErrorCount    =
	    le64_to_cpu(pstat_local->IPReassemblyErrorCount);
	pstat_user->TCPTxSegmentsCount        =
	    le64_to_cpu(pstat_local->TCPTxSegmentsCount);
	pstat_user->TCPTxBytesCount           =
	    le64_to_cpu(pstat_local->TCPTxBytesCount);
	pstat_user->TCPRxSegmentsCount        =
	    le64_to_cpu(pstat_local->TCPRxSegmentsCount);
	pstat_user->TCPRxBytesCount           =
	    le64_to_cpu(pstat_local->TCPRxBytesCount);
	pstat_user->TCPTimerExpiredCount      =
	    le64_to_cpu(pstat_local->TCPTimerExpiredCount);
	pstat_user->TCPRxACKCount             =
	    le64_to_cpu(pstat_local->TCPRxACKCount);
	pstat_user->TCPTxACKCount             =
	    le64_to_cpu(pstat_local->TCPTxACKCount);
	pstat_user->TCPRxErrorSegmentCount    =
	    le64_to_cpu(pstat_local->TCPRxErrorSegmentCount);
	pstat_user->TCPWindowProbeUpdateCount =
	    le64_to_cpu(pstat_local->TCPWindowProbeUpdateCount);
	pstat_user->iSCSITxPDUCount           =
	    le64_to_cpu(pstat_local->iSCSITxPDUCount);
	pstat_user->iSCSITxBytesCount         =
	    le64_to_cpu(pstat_local->iSCSITxBytesCount);
	pstat_user->iSCSIRxPDUCount           =
	    le64_to_cpu(pstat_local->iSCSIRxPDUCount);
	pstat_user->iSCSIRxBytesCount         =
	    le64_to_cpu(pstat_local->iSCSIRxBytesCount);
	pstat_user->iSCSICompleteIOsCount     =
	    le64_to_cpu(pstat_local->iSCSICompleteIOsCount);
	pstat_user->iSCSIUnexpectedIORxCount  =
	    le64_to_cpu(pstat_local->iSCSIUnexpectedIORxCount);
	pstat_user->iSCSIFormatErrorCount     =
	    le64_to_cpu(pstat_local->iSCSIFormatErrorCount);
	pstat_user->iSCSIHeaderDigestCount    =
	    le64_to_cpu(pstat_local->iSCSIHeaderDigestCount);
	pstat_user->iSCSIDataDigestErrorCount =
	    le64_to_cpu(pstat_local->iSCSIDataDigestErrorCount);
	pstat_user->iSCSISeqErrorCount        =
	    le64_to_cpu(pstat_local->iSCSISeqErrorCount);

	/*
	 * Copy the data from the dma buffer to the user's data space
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pstat_user, ioctl->ResponseLen)) != 0) {
		QL4PRINT(QLP2, printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_stats_iscsi;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_get_stats_iscsi:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_device_entry_iscsi
 *	This routine retrieves the database entry for the specified device.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_device_entry_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	DEV_DB_ENTRY	*pfw_ddb_entry;
	EXT_DEVICE_ENTRY_ISCSI	*pdev_entry;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pdev_entry,
	    sizeof(EXT_DEVICE_ENTRY_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_DEVICE_ENTRY_ISCSI)));
		goto exit_get_dev_entry;
	}

	if (ha->ioctl_dma_buf_len < sizeof(DEV_DB_ENTRY)) {
		if (qla4xxx_resize_ioctl_dma_buf(ha, sizeof(DEV_DB_ENTRY)) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_NO_MEMORY;
			goto exit_get_dev_entry;
		}
	}

	if (ioctl->ResponseLen < sizeof(EXT_DEVICE_ENTRY_ISCSI)) { 
		QL4PRINT(QLP2, printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_get_dev_entry;
	}

	/*
	 * Make the mailbox call
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	memset(pdev_entry, 0, sizeof(EXT_DEVICE_ENTRY_ISCSI));

	if (ioctl->SubCode == EXT_SC_GET_DEVICE_ENTRY_ISCSI)
		mbox_cmd[0] = MBOX_CMD_GET_DATABASE_ENTRY;
	else
		mbox_cmd[0] = MBOX_CMD_GET_DATABASE_ENTRY_DEFAULTS;

	mbox_cmd[1] = ioctl->Instance;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if (qla4xxx_mailbox_command(ha, 4, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: get ddb entry for index [%d] failed "
		    "w/ mailbox ststus 0x%x\n",
		    ha->host_no, __func__, ioctl->Instance, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_get_dev_entry;
	}

	/*
	 * Transfer data from Fw's DEV_DB_ENTRY buffer to IOCTL's
	 * EXT_DEVICE_ENTRY_ISCSI buffer
	 */
	pfw_ddb_entry = ha->ioctl_dma_bufv;

	pdev_entry->NumValid     = mbox_sts[2];
	pdev_entry->NextValid    = mbox_sts[3];
	pdev_entry->DeviceState  = mbox_sts[4];
	pdev_entry->Options      = pfw_ddb_entry->options;
	pdev_entry->Control      = pfw_ddb_entry->control;
	pdev_entry->TargetSessID = le16_to_cpu(pfw_ddb_entry->TSID);
	memcpy(pdev_entry->InitiatorSessID, pfw_ddb_entry->ISID,
	    sizeof(pfw_ddb_entry->ISID));

	pdev_entry->DeviceInfo.DeviceType = le16_to_cpu(EXT_DEF_ISCSI_REMOTE);
	pdev_entry->DeviceInfo.ExeThrottle =
	    le16_to_cpu(pfw_ddb_entry->exeThrottle);
	pdev_entry->DeviceInfo.InitMarkerlessInt =
	    le16_to_cpu(pfw_ddb_entry->iSCSIMaxSndDataSegLen);
	pdev_entry->DeviceInfo.RetryCount = pfw_ddb_entry->retryCount;
	pdev_entry->DeviceInfo.RetryDelay = pfw_ddb_entry->retryDelay;
	pdev_entry->DeviceInfo.iSCSIOptions =
	    le16_to_cpu(pfw_ddb_entry->iSCSIOptions);
	pdev_entry->DeviceInfo.TCPOptions =
	    le16_to_cpu(pfw_ddb_entry->TCPOptions);
	pdev_entry->DeviceInfo.IPOptions =
	    le16_to_cpu(pfw_ddb_entry->IPOptions);
	pdev_entry->DeviceInfo.MaxPDUSize =
	    le16_to_cpu(pfw_ddb_entry->maxPDUSize);
	pdev_entry->DeviceInfo.FirstBurstSize =
	    le16_to_cpu(pfw_ddb_entry->firstBurstSize);
	pdev_entry->DeviceInfo.LogoutMinTime =
	    le16_to_cpu(pfw_ddb_entry->minTime2Wait);
	pdev_entry->DeviceInfo.LogoutMaxTime =
	    le16_to_cpu(pfw_ddb_entry->maxTime2Retain);
	pdev_entry->DeviceInfo.MaxOutstandingR2T =
	    le16_to_cpu(pfw_ddb_entry->maxOutstndngR2T);
	pdev_entry->DeviceInfo.KeepAliveTimeout =
	    le16_to_cpu(pfw_ddb_entry->keepAliveTimeout);
	pdev_entry->DeviceInfo.PortNumber =
	    le16_to_cpu(pfw_ddb_entry->portNumber);
	pdev_entry->DeviceInfo.MaxBurstSize =
	    le16_to_cpu(pfw_ddb_entry->maxBurstSize);
	pdev_entry->DeviceInfo.TaskMgmtTimeout =
	    le16_to_cpu(pfw_ddb_entry->taskMngmntTimeout);
	pdev_entry->EntryInfo.PortalCount = mbox_sts[2];
	pdev_entry->ExeCount = le16_to_cpu(pfw_ddb_entry->exeCount);
	pdev_entry->DDBLink = le16_to_cpu(pfw_ddb_entry->ddbLink);

	memcpy(pdev_entry->UserID, pfw_ddb_entry->userID,
	    sizeof(pdev_entry->UserID));
	memcpy(pdev_entry->Password, pfw_ddb_entry->password,
	    sizeof(pdev_entry->Password));

	memcpy(pdev_entry->DeviceInfo.TargetAddr, pfw_ddb_entry->targetAddr,
	    sizeof(pdev_entry->DeviceInfo.TargetAddr));
	memcpy(pdev_entry->EntryInfo.IPAddr.IPAddress, pfw_ddb_entry->ipAddr,
	    sizeof(pdev_entry->EntryInfo.IPAddr.IPAddress));
	memcpy(pdev_entry->EntryInfo.iSCSIName, pfw_ddb_entry->iscsiName,
	    sizeof(pdev_entry->EntryInfo.iSCSIName));
	memcpy(pdev_entry->EntryInfo.Alias, pfw_ddb_entry->iSCSIAlias,
	    sizeof(pdev_entry->EntryInfo.Alias));

	QL4PRINT(QLP10|QLP4,
	    printk("scsi%d: DEV_DB_ENTRY structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP10|QLP4,
	    pfw_ddb_entry, sizeof(DEV_DB_ENTRY));
	QL4PRINT(QLP10|QLP4,
	    printk("scsi%d: EXT_DEVICE_ENTRY_ISCSI structure:\n",
	    ha->host_no));
	qla4xxx_dump_bytes(QLP10|QLP4,
	    pdev_entry, sizeof(EXT_DEVICE_ENTRY_ISCSI));

	/*
	 * Copy the IOCTL EXT_DEVICE_ENTRY_ISCSI buffer to the user's data space
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pdev_entry, ioctl->ResponseLen)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_dev_entry;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_get_dev_entry:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}


/**************************************************************************
 * qla4extioctl_get_init_fw_iscsi
 *	This routine retrieves the initialize firmware control block for
 *	the specified HBA.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_init_fw_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	EXT_INIT_FW_ISCSI *pinit_fw;
	INIT_FW_CTRL_BLK  *pinit_fw_cb;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pinit_fw,
	    sizeof(EXT_INIT_FW_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_INIT_FW_ISCSI)));
		goto exit_get_init_fw;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp ||
	    (ha->ioctl_dma_buf_len < sizeof(INIT_FW_CTRL_BLK)) ||
	    (ioctl->ResponseLen < sizeof(EXT_INIT_FW_ISCSI))) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_get_init_fw;
	}

	/*
	 * Send mailbox command
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	switch (ioctl->SubCode) {
	case EXT_SC_GET_INIT_FW_ISCSI:
		mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
		break;
	case EXT_SC_GET_INIT_FW_DEFAULTS_ISCSI:
		mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK_DEFAULTS;
		break;
	default:
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: invalid subcode (0x%04X) speficied\n",
		    ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_INVALID_PARAM;
		goto exit_get_init_fw;
	}

	mbox_cmd[1] = 0;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2, printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_get_init_fw;
	}

	/*
	 * Transfer Data from DMA buffer to Local buffer
	 */
	pinit_fw_cb = (INIT_FW_CTRL_BLK *)ha->ioctl_dma_bufv;
	memset(pinit_fw, 0, sizeof(EXT_INIT_FW_ISCSI));

	pinit_fw->Version         = pinit_fw_cb->Version;
	pinit_fw->FWOptions       = le16_to_cpu(pinit_fw_cb->FwOptions);
	pinit_fw->AddFWOptions    = le16_to_cpu(pinit_fw_cb->AddFwOptions);
	//FIXME: pinit_fw->WakeupThreshold = le16_to_cpu(pinit_fw_cb->WakeupThreshold);
	memcpy(&pinit_fw->IPAddr.IPAddress, &pinit_fw_cb->IPAddr,
	    MIN(sizeof(pinit_fw->IPAddr.IPAddress),
	    sizeof(pinit_fw_cb->IPAddr)));
	memcpy(&pinit_fw->SubnetMask.IPAddress, &pinit_fw_cb->SubnetMask,
	    MIN(sizeof(pinit_fw->SubnetMask.IPAddress),
	    sizeof(pinit_fw_cb->SubnetMask)));
	memcpy(&pinit_fw->Gateway.IPAddress, &pinit_fw_cb->GatewayIPAddr,
	    MIN(sizeof(pinit_fw->Gateway.IPAddress),
	    sizeof(pinit_fw_cb->GatewayIPAddr)));
	memcpy(&pinit_fw->DNSConfig.IPAddr.IPAddress,
	    &pinit_fw_cb->PriDNSIPAddr,
	    MIN(sizeof(pinit_fw->DNSConfig.IPAddr.IPAddress),
	    sizeof(pinit_fw_cb->PriDNSIPAddr)));
	memcpy(&pinit_fw->Alias, &pinit_fw_cb->Alias,
	    MIN(sizeof(pinit_fw->Alias), sizeof(pinit_fw_cb->Alias)));
	memcpy(&pinit_fw->iSCSIName, &pinit_fw_cb->iSCSINameString,
	    MIN(sizeof(pinit_fw->iSCSIName),
	    sizeof(pinit_fw_cb->iSCSINameString)));

	pinit_fw->DeviceInfo.DeviceType = le16_to_cpu(EXT_DEF_ISCSI_LOCAL);
	pinit_fw->DeviceInfo.ExeThrottle =
	    le16_to_cpu(pinit_fw_cb->ExecThrottle);
	pinit_fw->DeviceInfo.InitMarkerlessInt =
	    le16_to_cpu(pinit_fw_cb->InitMarkerlessInt);
	pinit_fw->DeviceInfo.RetryCount = pinit_fw_cb->RetryCount;
	pinit_fw->DeviceInfo.RetryDelay = pinit_fw_cb->RetryDelay;
	pinit_fw->DeviceInfo.iSCSIOptions =
	    le16_to_cpu(pinit_fw_cb->iSCSIOptions);
	pinit_fw->DeviceInfo.TCPOptions = le16_to_cpu(pinit_fw_cb->TCPOptions);
	pinit_fw->DeviceInfo.IPOptions = le16_to_cpu(pinit_fw_cb->IPOptions);
	pinit_fw->DeviceInfo.MaxPDUSize = le16_to_cpu(pinit_fw_cb->MaxPDUSize);
	pinit_fw->DeviceInfo.FirstBurstSize =
	    le16_to_cpu(pinit_fw_cb->FirstBurstSize);
	pinit_fw->DeviceInfo.LogoutMinTime =
	    le16_to_cpu(pinit_fw_cb->DefaultTime2Wait);
	pinit_fw->DeviceInfo.LogoutMaxTime =
	    le16_to_cpu(pinit_fw_cb->DefaultTime2Retain);
	pinit_fw->DeviceInfo.LogoutMaxTime =
	    le16_to_cpu(pinit_fw_cb->DefaultTime2Retain);
	pinit_fw->DeviceInfo.MaxOutstandingR2T =
	    le16_to_cpu(pinit_fw_cb->MaxOutStndngR2T);
	pinit_fw->DeviceInfo.KeepAliveTimeout =
	    le16_to_cpu(pinit_fw_cb->KeepAliveTimeout);
	pinit_fw->DeviceInfo.PortNumber = le16_to_cpu(pinit_fw_cb->PortNumber);
	pinit_fw->DeviceInfo.MaxBurstSize =
	    le16_to_cpu(pinit_fw_cb->MaxBurstSize);
	//pinit_fw->DeviceInfo.TaskMgmtTimeout   = pinit_fw_cb->T;
	memcpy(&pinit_fw->DeviceInfo.TargetAddr, &pinit_fw_cb->TargAddr,
	    EXT_DEF_ISCSI_TADDR_SIZE);

	/*
	 * Copy the local data to the user's buffer
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), pinit_fw,
	    sizeof(EXT_INIT_FW_ISCSI))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_init_fw;
	}

	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP10|QLP4,
	    printk("scsi%d: EXT_INIT_FW_ISCSI structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP10|QLP4, pinit_fw, sizeof(EXT_INIT_FW_ISCSI));

exit_get_init_fw:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_isns_server
 *	This routine retrieves the iSNS server information.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_isns_server(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	EXT_ISNS_SERVER *pisns_server;
	FLASH_INIT_FW_CTRL_BLK *pflash_init_fw_cb = NULL;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pisns_server,
	    sizeof(EXT_ISNS_SERVER))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_ISNS_SERVER)));
		goto exit_get_isns_server;
	}

	if (ioctl->ResponseLen < sizeof(EXT_ISNS_SERVER)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		ioctl->ResponseLen = 0;
		goto exit_get_isns_server;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp ||
	    (ha->ioctl_dma_buf_len < sizeof(FLASH_INIT_FW_CTRL_BLK))) {
		if (qla4xxx_resize_ioctl_dma_buf(ha,
		    sizeof(FLASH_INIT_FW_CTRL_BLK)) != QLA_SUCCESS) {
			QL4PRINT(QLP2,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_NO_MEMORY;
			ioctl->ResponseLen = 0;
			goto exit_get_isns_server;
		}
	}

	/*
	 * First get Flash Initialize Firmware Control Block, so as not to
	 * destroy unaffected data
	 *----------------------------------------------------------------*/
	pflash_init_fw_cb = (FLASH_INIT_FW_CTRL_BLK *)ha->ioctl_dma_bufv;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = INT_ISCSI_INITFW_FLASH_OFFSET;
	mbox_cmd[4] = sizeof(FLASH_INIT_FW_CTRL_BLK);

	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: READ_FLASH command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		ioctl->ResponseLen = 0;
		goto exit_get_isns_server;
	}

	QL4PRINT(QLP4, printk("scsi%d: %s: READ_FLASH command successful \n",
	    ha->host_no, __func__));

	/*
	 * Copy iSNS Server info to the isns_server structure
	 *---------------------------------------------------*/
	memset(pisns_server, 0, sizeof(EXT_ISNS_SERVER));
	pisns_server->PerformiSNSDiscovery =
	    (pflash_init_fw_cb->init_fw_cb.TCPOptions & TOPT_ISNS_ENABLE) ? 1:0;
	pisns_server->AutomaticiSNSDiscovery =
	    (pflash_init_fw_cb->init_fw_cb.TCPOptions &
	    TOPT_LEARN_ISNS_IP_ADDR_ENABLE) ? 1 : 0;
	pisns_server->PortNumber =
	    pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber;
	pisns_server->IPAddr.Type = EXT_DEF_TYPE_ISCSI_IP;
	memcpy(pisns_server->IPAddr.IPAddress,
	       pflash_init_fw_cb->init_fw_cb.iSNSIPAddr,
	       MIN(sizeof(pisns_server->IPAddr.IPAddress),
		   sizeof(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr)));
	memcpy(pisns_server->InitiatorName,
	       pflash_init_fw_cb->init_fw_cb.iSCSINameString,
	       MIN(sizeof(pisns_server->InitiatorName),
		   sizeof(pflash_init_fw_cb->init_fw_cb.iSCSINameString)));

#if 1
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_SET_ISNS_SERVICE;
	mbox_cmd[1] = ISNS_STATUS;
	if (qla4xxx_mailbox_command(ha, 2, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: GET ISNS SERVICE STATUS cmnd failed \n",
		    ha->host_no, __func__));
		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		ioctl->ResponseLen = 0;
		goto exit_get_isns_server;
	}

	QL4PRINT(QLP4|QLP20,
	    printk("scsi%d: %s: GET ISNS SERVICE STATUS = 0x%04x \"%s\"\n",
	    ha->host_no, __func__, mbox_sts[1],
	    ((mbox_sts[1] & 1) == 0) ? "DISABLED" : "ENABLED"));
#endif

	/*
	 * Copy the local data to the user's buffer
	 *-----------------------------------------*/
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pisns_server, sizeof(EXT_ISNS_SERVER))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_isns_server;
	}

	ioctl->Status = EXT_STATUS_OK;
	ioctl->ResponseLen = sizeof(EXT_ISNS_SERVER);
	ioctl->DetailStatus = 0;

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: EXT_ISNS_SERVER structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP4|QLP10,
	    pisns_server, sizeof(EXT_ISNS_SERVER));

exit_get_isns_server:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_isns_disc_targets
 *	This routine retrieves the targets discovered via iSNS.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_isns_disc_targets(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status   = 0;
	uint32_t	isns_disc_tgt_index_start;
	uint32_t	i, j;
	EXT_ISNS_DISCOVERED_TARGETS *pisns_disc_tgts = NULL;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (ioctl->ResponseLen < sizeof(EXT_ISNS_DISCOVERED_TARGETS)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: response buffer "
		    "too small.  RspLen=0x%x, need 0x%x\n",
		    ha->host_no, __func__, ioctl->ResponseLen,
		    (unsigned int) sizeof(EXT_ISNS_DISCOVERED_TARGETS)));
		ioctl->ResponseLen = 0;
		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		ioctl->DetailStatus = sizeof(EXT_ISNS_DISCOVERED_TARGETS);
		goto exit_get_isns_disc_tgts;
	}

	if (!ha->ioctl_dma_bufv ||
	    ((ioctl->ResponseLen > ha->ioctl_dma_buf_len) &&
	    qla4xxx_resize_ioctl_dma_buf(ha,
	    sizeof(EXT_ISNS_DISCOVERED_TARGETS)) != QLA_SUCCESS)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: unable to allocate memory "
		    "for dma buffer.\n",
		    ha->host_no, __func__));
		ioctl->ResponseLen = 0;
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_get_isns_disc_tgts;
	}

	/*
	 * Copy the IOCTL EXT_ISNS_DISCOVERED_TARGETS buffer from the user's
	 * data space
	 */
	pisns_disc_tgts = (EXT_ISNS_DISCOVERED_TARGETS *) ha->ioctl_dma_bufv;
	if (copy_from_user((uint8_t *)pisns_disc_tgts,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), ioctl->RequestLen) != 0) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->ResponseLen = 0;
		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_isns_disc_tgts;
	}

	isns_disc_tgt_index_start =
	    pisns_disc_tgts->iSNSDiscoveredTargetIndexStart;
	memset(pisns_disc_tgts, 0, sizeof(EXT_ISNS_DISCOVERED_TARGETS));
	pisns_disc_tgts->iSNSDiscoveredTargetIndexStart =
	    isns_disc_tgt_index_start;

	/*
	 * Transfer Data from Local buffer to DMA buffer
	 */
	if (isns_disc_tgt_index_start < ha->isns_num_discovered_targets) {
		EXT_ISNS_DISCOVERED_TARGET *isns_disc_tgt;
		ISNS_DISCOVERED_TARGET *isns_local_disc_target;

		for (i = isns_disc_tgt_index_start;
		    i < ha->isns_num_discovered_targets &&
		    pisns_disc_tgts->NumiSNSDiscoveredTargets <
		    EXT_DEF_NUM_ISNS_DISCOVERED_TARGETS;
		    i++) {
			isns_disc_tgt = (EXT_ISNS_DISCOVERED_TARGET *)
			    &pisns_disc_tgts->iSNSDiscoveredTargets[
			    pisns_disc_tgts->NumiSNSDiscoveredTargets];
			isns_local_disc_target = (ISNS_DISCOVERED_TARGET *)
			    &ha->isns_disc_tgt_databasev[i];

			isns_disc_tgt->NumPortals =
			    isns_local_disc_target->NumPortals;

			for (j = 0; j < isns_disc_tgt->NumPortals; j++) {
				memcpy(isns_disc_tgt->Portal[j].IPAddr.
				    IPAddress,
				    isns_local_disc_target->Portal[j].IPAddr,
				    MIN(sizeof(isns_disc_tgt->Portal[j].IPAddr.
				    IPAddress),
				    sizeof(isns_local_disc_target->Portal[j].
				    IPAddr)));
				isns_disc_tgt->Portal[j].IPAddr.Type =
				    EXT_DEF_TYPE_ISCSI_IP;
				isns_disc_tgt->Portal[j].PortNumber =
				    isns_local_disc_target->Portal[j].
				    PortNumber;
			}

			isns_disc_tgt->DDID = isns_local_disc_target->DDID;

			memcpy(isns_disc_tgt->NameString,
			    isns_local_disc_target->NameString,
			    MIN(sizeof(isns_disc_tgt->NameString),
			    sizeof(isns_local_disc_target->NameString)));
			memcpy(isns_disc_tgt->Alias,
			    isns_local_disc_target->Alias,
			    MIN(sizeof(isns_disc_tgt->Alias),
			    sizeof(isns_local_disc_target->Alias)));

			pisns_disc_tgts->NumiSNSDiscoveredTargets++;
		}
	}

	/*
	 * Copy the data to the user's buffer
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    pisns_disc_tgts, sizeof(EXT_ISNS_DISCOVERED_TARGETS))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_isns_disc_tgts;
	}

	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: EXT_INIT_FW_ISCSI structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP4|QLP10,
	    pisns_disc_tgts, sizeof(EXT_ISNS_DISCOVERED_TARGETS));

exit_get_isns_disc_tgts:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_data
 *	This routine calls get data IOCTLs based on the IOCTL Sub Code.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *    	-EINVAL     = if the command is invalid
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_data(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	switch (ioctl->SubCode) {
	case EXT_SC_GET_STATISTICS_GEN:
		return(qla4extioctl_get_statistics_gen(ha, ioctl));

	case EXT_SC_GET_STATISTICS_ISCSI:
		return(qla4extioctl_get_statistics_iscsi(ha, ioctl));

	case EXT_SC_GET_DEVICE_ENTRY_ISCSI:
	case EXT_SC_GET_DEVICE_ENTRY_DEFAULTS_ISCSI:
		return(qla4extioctl_get_device_entry_iscsi(ha, ioctl));

	case EXT_SC_GET_INIT_FW_ISCSI:
	case EXT_SC_GET_INIT_FW_DEFAULTS_ISCSI:
		return(qla4extioctl_get_init_fw_iscsi(ha, ioctl));

	case EXT_SC_GET_ISNS_SERVER:
		return(qla4extioctl_get_isns_server(ha, ioctl));

	case EXT_SC_GET_ISNS_DISCOVERED_TARGETS:
		return(qla4extioctl_get_isns_disc_targets(ha, ioctl));

	default:
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unsupported external get "
		    "data sub-command code (%X)\n",
		    ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		return(0);
	}
}

/**************************************************************************
 * qla4extioctl_rst_statistics_gen
 *	This routine clears the HBA general statistical information.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_rst_statistics_gen(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/*
	 * Reset the general statistics fields
	 */
	ha->adapter_error_count = 0;
	ha->device_error_count = 0;
	ha->total_io_count = 0;
	ha->total_mbytes_xferred = 0;
	ha->isr_count = 0;
	ha->link_failure_count = 0;
	ha->invalid_crc_count = 0;

	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(QLA_SUCCESS);
}

/**************************************************************************
 * qla4extioctl_rst_statistics_iscsi
 *	This routine clears the HBA iSCSI statistical information.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_rst_statistics_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/*
	 * Make the mailbox call
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_MANAGEMENT_DATA;
	mbox_cmd[1] = ioctl->Instance;
	mbox_cmd[2] = 0;
	mbox_cmd[3] = 0;

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: get mngmt data for index [%d] failed! "
		    "w/ mailbox ststus 0x%x\n",
		    ha->host_no, __func__, ioctl->Instance, mbox_sts[0]));

		ioctl->Status = EXT_STATUS_MAILBOX;
		ioctl->DetailStatus = mbox_sts[0];

		return(0);
	}

	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(QLA_SUCCESS);
}

/**************************************************************************
 * qla4extioctl_set_device_entry_iscsi
 *	This routine configures a device with specific database entry data.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_set_device_entry_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	DEV_DB_ENTRY	*pfw_ddb_entry;
	EXT_DEVICE_ENTRY_ISCSI *pdev_entry;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pdev_entry,
	    sizeof(EXT_DEVICE_ENTRY_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_DEVICE_ENTRY_ISCSI)));
		goto exit_set_dev_entry;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp || !ioctl->RequestAdr) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory allocation problem\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_set_dev_entry;
	}

	if (ha->ioctl_dma_buf_len < sizeof(DEV_DB_ENTRY)) {
		if (qla4xxx_resize_ioctl_dma_buf(ha, sizeof(DEV_DB_ENTRY)) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_NO_MEMORY;
			goto exit_set_dev_entry;
		}
	}

	if (ioctl->RequestLen < sizeof(EXT_DEVICE_ENTRY_ISCSI)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_set_dev_entry;
	}

	/*
	 * Copy the IOCTL EXT_DEVICE_ENTRY_ISCSI buffer from the user's
	 * data space
	 */
	if ((status = copy_from_user((uint8_t *)pdev_entry,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), ioctl->RequestLen)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_set_dev_entry;
	}

	/*
	 * Transfer data from IOCTL's EXT_DEVICE_ENTRY_ISCSI buffer to
	 * Fw's DEV_DB_ENTRY buffer
	 */
	pfw_ddb_entry = ha->ioctl_dma_bufv;
	memset(pfw_ddb_entry, 0, sizeof(DEV_DB_ENTRY));

	pfw_ddb_entry->options          = pdev_entry->Options;
	pfw_ddb_entry->control          = pdev_entry->Control;
	pfw_ddb_entry->TSID             = cpu_to_le16(pdev_entry->TargetSessID);
	pfw_ddb_entry->exeCount         = cpu_to_le16(pdev_entry->ExeCount);
	pfw_ddb_entry->ddbLink          = cpu_to_le16(pdev_entry->DDBLink);
	memcpy(pfw_ddb_entry->ISID, pdev_entry->InitiatorSessID,
	    sizeof(pdev_entry->InitiatorSessID));
	memcpy(pfw_ddb_entry->userID, pdev_entry->UserID,
	    sizeof(pdev_entry->UserID));
	memcpy(pfw_ddb_entry->password, pdev_entry->Password,
	    sizeof(pdev_entry->Password));

	pfw_ddb_entry->exeThrottle =
	    cpu_to_le16(pdev_entry->DeviceInfo.ExeThrottle);
	pfw_ddb_entry->iSCSIMaxSndDataSegLen =
	    cpu_to_le16(pdev_entry->DeviceInfo.InitMarkerlessInt);
	pfw_ddb_entry->retryCount =
	    pdev_entry->DeviceInfo.RetryCount;
	pfw_ddb_entry->retryDelay = pdev_entry->DeviceInfo.RetryDelay;
	pfw_ddb_entry->iSCSIOptions =
	    cpu_to_le16(pdev_entry->DeviceInfo.iSCSIOptions);
	pfw_ddb_entry->TCPOptions =
	    cpu_to_le16(pdev_entry->DeviceInfo.TCPOptions);
	pfw_ddb_entry->IPOptions =
	    cpu_to_le16(pdev_entry->DeviceInfo.IPOptions);
	pfw_ddb_entry->maxPDUSize =
	    cpu_to_le16(pdev_entry->DeviceInfo.MaxPDUSize);
	pfw_ddb_entry->firstBurstSize =
	    cpu_to_le16(pdev_entry->DeviceInfo.FirstBurstSize);
	pfw_ddb_entry->minTime2Wait =
	    cpu_to_le16(pdev_entry->DeviceInfo.LogoutMinTime);
	pfw_ddb_entry->maxTime2Retain =
	    cpu_to_le16(pdev_entry->DeviceInfo.LogoutMaxTime);
	pfw_ddb_entry->maxOutstndngR2T =
	    cpu_to_le16(pdev_entry->DeviceInfo.MaxOutstandingR2T);
	pfw_ddb_entry->keepAliveTimeout =
	    cpu_to_le16(pdev_entry->DeviceInfo.KeepAliveTimeout);
	pfw_ddb_entry->portNumber =
	    cpu_to_le16(pdev_entry->DeviceInfo.PortNumber);
	pfw_ddb_entry->maxBurstSize =
	    cpu_to_le16(pdev_entry->DeviceInfo.MaxBurstSize);
	pfw_ddb_entry->taskMngmntTimeout =
	    cpu_to_le16(pdev_entry->DeviceInfo.TaskMgmtTimeout);
	memcpy(pfw_ddb_entry->targetAddr, pdev_entry->DeviceInfo.TargetAddr,
	    sizeof(pdev_entry->DeviceInfo.TargetAddr));

	memcpy(pfw_ddb_entry->ipAddr, pdev_entry->EntryInfo.IPAddr.IPAddress,
	    sizeof(pdev_entry->EntryInfo.IPAddr.IPAddress));
	memcpy(pfw_ddb_entry->iscsiName, pdev_entry->EntryInfo.iSCSIName,
	    sizeof(pdev_entry->EntryInfo.iSCSIName));
	memcpy(pfw_ddb_entry->iSCSIAlias, pdev_entry->EntryInfo.Alias,
	    sizeof(pdev_entry->EntryInfo.Alias));

	/*
	 * Make the IOCTL call
	 */
	if (qla4xxx_set_ddb_entry(ha, ioctl->Instance, pfw_ddb_entry,
	    ha->ioctl_dma_bufp) != QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: SET DDB Entry failed\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		goto exit_set_dev_entry;
	}

	ioctl->Status = EXT_STATUS_OK;

exit_set_dev_entry:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_set_init_fw_iscsi
 *	This routine configures a device with specific data entry data.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_set_init_fw_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	EXT_INIT_FW_ISCSI *pinit_fw;
	INIT_FW_CTRL_BLK  *pinit_fw_cb;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pinit_fw,
	    sizeof(EXT_INIT_FW_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_INIT_FW_ISCSI)));
		goto exit_set_init_fw;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp ||
	    (ha->ioctl_dma_buf_len < sizeof(INIT_FW_CTRL_BLK)) ||
	    (ioctl->RequestLen < sizeof(EXT_INIT_FW_ISCSI))) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: requst buffer too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_set_init_fw;
	}

	/*
	 * Copy the data from the user's buffer
	 */
	if ((status = copy_from_user((uint8_t *)pinit_fw,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(EXT_INIT_FW_ISCSI))) != 
	    0) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_set_init_fw;
	}

	/*
	 * First get Initialize Firmware Control Block, so as not to
	 * destroy unaffected data
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_set_init_fw;
	}

	/*
	 * Transfer Data from Local buffer to DMA buffer
	 */
	pinit_fw_cb = (INIT_FW_CTRL_BLK *)ha->ioctl_dma_bufv;

	pinit_fw_cb->Version         = pinit_fw->Version;
	pinit_fw_cb->FwOptions       = cpu_to_le16(pinit_fw->FWOptions);
	pinit_fw_cb->AddFwOptions    = cpu_to_le16(pinit_fw->AddFWOptions);
	//FIXME: pinit_fw_cb->WakeupThreshold = cpu_to_le16(pinit_fw->WakeupThreshold);
	memcpy(pinit_fw_cb->IPAddr, pinit_fw->IPAddr.IPAddress,
	    MIN(sizeof(pinit_fw_cb->IPAddr),
	    sizeof(pinit_fw->IPAddr.IPAddress)));
	memcpy(pinit_fw_cb->SubnetMask, pinit_fw->SubnetMask.IPAddress,
	    MIN(sizeof(pinit_fw_cb->SubnetMask),
	    sizeof(pinit_fw->SubnetMask.IPAddress)));
	memcpy(pinit_fw_cb->GatewayIPAddr, pinit_fw->Gateway.IPAddress,
	    MIN(sizeof(pinit_fw_cb->GatewayIPAddr),
	    sizeof(pinit_fw->Gateway.IPAddress)));
	memcpy(pinit_fw_cb->PriDNSIPAddr, pinit_fw->DNSConfig.IPAddr.IPAddress,
	    MIN(sizeof(pinit_fw_cb->PriDNSIPAddr),
	    sizeof(pinit_fw->DNSConfig.IPAddr.IPAddress)));
	memcpy(pinit_fw_cb->Alias, pinit_fw->Alias,
	    MIN(sizeof(pinit_fw_cb->Alias), sizeof(pinit_fw->Alias)));
	memcpy(pinit_fw_cb->iSCSINameString, pinit_fw->iSCSIName,
	    MIN(sizeof(pinit_fw_cb->iSCSINameString),
	    sizeof(pinit_fw->iSCSIName)));

	pinit_fw_cb->ExecThrottle =
	    cpu_to_le16(pinit_fw->DeviceInfo.ExeThrottle);
	pinit_fw_cb->InitMarkerlessInt =
	    cpu_to_le16(pinit_fw->DeviceInfo.InitMarkerlessInt);
	pinit_fw_cb->RetryCount = pinit_fw->DeviceInfo.RetryCount;
	pinit_fw_cb->RetryDelay = pinit_fw->DeviceInfo.RetryDelay;
	pinit_fw_cb->iSCSIOptions =
	    cpu_to_le16(pinit_fw->DeviceInfo.iSCSIOptions);
	pinit_fw_cb->TCPOptions = cpu_to_le16(pinit_fw->DeviceInfo.TCPOptions);
	pinit_fw_cb->IPOptions = cpu_to_le16(pinit_fw->DeviceInfo.IPOptions);
	pinit_fw_cb->MaxPDUSize = cpu_to_le16(pinit_fw->DeviceInfo.MaxPDUSize);
	pinit_fw_cb->FirstBurstSize =
	    cpu_to_le16(pinit_fw->DeviceInfo.FirstBurstSize);
	pinit_fw_cb->DefaultTime2Wait =
	    cpu_to_le16(pinit_fw->DeviceInfo.LogoutMinTime);
	pinit_fw_cb->DefaultTime2Retain =
	    cpu_to_le16(pinit_fw->DeviceInfo.LogoutMaxTime);
	pinit_fw_cb->MaxOutStndngR2T =
	    cpu_to_le16(pinit_fw->DeviceInfo.MaxOutstandingR2T);
	pinit_fw_cb->KeepAliveTimeout =
	    cpu_to_le16(pinit_fw->DeviceInfo.KeepAliveTimeout);
	pinit_fw_cb->PortNumber = cpu_to_le16(pinit_fw->DeviceInfo.PortNumber);
	pinit_fw_cb->MaxBurstSize =
	    cpu_to_le16(pinit_fw->DeviceInfo.MaxBurstSize);
	//pinit_fw_cb->?                = pinit_fw->DeviceInfo.TaskMgmtTimeout;
	memcpy(pinit_fw_cb->TargAddr, pinit_fw->DeviceInfo.TargetAddr,
	    EXT_DEF_ISCSI_TADDR_SIZE);

	/*
	 * Send mailbox command
	 */

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_INITIALIZE_FIRMWARE;
	mbox_cmd[2] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = MSDW(ha->ioctl_dma_bufp);

	if ((status = qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0],
	    &mbox_sts[0])) == QLA_ERROR) {
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_set_init_fw;
	}

	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: EXT_INIT_FW_ISCSI structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP4|QLP10, pinit_fw, sizeof(EXT_INIT_FW_ISCSI));

exit_set_init_fw:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_set_isns_server
 *	This routine retrieves the targets discovered via iSNS.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_set_isns_server(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];
	EXT_ISNS_SERVER *pisns_server;
	FLASH_INIT_FW_CTRL_BLK *pflash_init_fw_cb = NULL;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pisns_server,
	    sizeof(EXT_ISNS_SERVER))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_ISNS_SERVER)));
		goto exit_set_isns_svr;
	}

	if (ioctl->RequestLen < sizeof(*pisns_server)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: requst buffer too small (%d/%xh)\n",
		    ha->host_no, __func__, ioctl->RequestLen,
		    ioctl->RequestLen));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		ioctl->ResponseLen = 0;
		goto exit_set_isns_svr;
	}

	if (!ha->ioctl_dma_bufv || !ha->ioctl_dma_bufp ||
	    (ha->ioctl_dma_buf_len < sizeof(FLASH_INIT_FW_CTRL_BLK))) {
		if (qla4xxx_resize_ioctl_dma_buf(ha,
		    sizeof(DEV_DB_ENTRY)) != QLA_SUCCESS) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_NO_MEMORY;
			ioctl->ResponseLen = 0;
			goto exit_set_isns_svr;
		}
	}

	/*
	 * Copy iSNS Server info from the user's buffer
	 *---------------------------------------------*/
	if ((status = copy_from_user((uint8_t *)pisns_server,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(EXT_ISNS_SERVER))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		ioctl->ResponseLen = 0;
		goto exit_set_isns_svr;
	}

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: EXT_ISNS_SERVER structure:\n", ha->host_no));
	qla4xxx_dump_bytes(QLP4|QLP10, pisns_server, sizeof(EXT_ISNS_SERVER));

	/*
	 * First get Flash Initialize Firmware Control Block, so as not to
	 * destroy unaffected data
	 *----------------------------------------------------------------*/
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = INT_ISCSI_INITFW_FLASH_OFFSET;
	mbox_cmd[4] = sizeof(FLASH_INIT_FW_CTRL_BLK);

	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: READ_FLASH command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		ioctl->ResponseLen = 0;
		goto exit_set_isns_svr;
	}

	QL4PRINT(QLP4, printk("scsi%d: %s: READ_FLASH command successful \n",
	    ha->host_no, __func__));

	/*
	 * Copy iSNS Server info to the flash_init_fw_cb
	 *----------------------------------------------*/
	pflash_init_fw_cb = (FLASH_INIT_FW_CTRL_BLK *)ha->ioctl_dma_bufv;

	if (pisns_server->PerformiSNSDiscovery) {
		if (pisns_server->AutomaticiSNSDiscovery) {
			pflash_init_fw_cb->init_fw_cb.TCPOptions |=
			    TOPT_LEARN_ISNS_IP_ADDR_ENABLE;
			memset(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr, 0,
			       sizeof(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr));
		} else {
			pflash_init_fw_cb->init_fw_cb.TCPOptions &=
			    ~TOPT_LEARN_ISNS_IP_ADDR_ENABLE;
			memcpy(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr,
			    pisns_server->IPAddr.IPAddress,
			    MIN(sizeof(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr),
			    sizeof(pisns_server->IPAddr.IPAddress)));
		}

		pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber =
		    (pisns_server->PortNumber) ?  pisns_server->PortNumber :
		    EXT_DEF_ISNS_WELL_KNOWN_PORT;
		pflash_init_fw_cb->init_fw_cb.TCPOptions |= TOPT_ISNS_ENABLE;

	} else {
		pflash_init_fw_cb->init_fw_cb.TCPOptions &= ~TOPT_ISNS_ENABLE;
		memset(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr, 0,
		       sizeof(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr));
		pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber = 0;
	}

	QL4PRINT(QLP4, printk("scsi%d: %s: IPAddr %d.%d.%d.%d Port# %04d\n",
	    ha->host_no, __func__,
	    pflash_init_fw_cb->init_fw_cb.iSNSIPAddr[0],
	    pflash_init_fw_cb->init_fw_cb.iSNSIPAddr[1],
	    pflash_init_fw_cb->init_fw_cb.iSNSIPAddr[2],
	    pflash_init_fw_cb->init_fw_cb.iSNSIPAddr[3],
	    pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber));

	/*
	 * If the internal iSNS info is different from the flash_init_fw_cb,
	 * flash it now.
	 *------------------------------------------------------------------*/
	if (((ha->tcp_options & TOPT_LEARN_ISNS_IP_ADDR_ENABLE) !=
	    (pflash_init_fw_cb->init_fw_cb.TCPOptions &
	    TOPT_LEARN_ISNS_IP_ADDR_ENABLE)) ||
	    (!IPAddrIsEqual(ha->isns_ip_address,
	    pflash_init_fw_cb->init_fw_cb.iSNSIPAddr)) ||
	    (ha->isns_server_port_number !=
	    pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber)) {
		memset(&mbox_cmd, 0, sizeof(mbox_cmd));
		memset(&mbox_sts, 0, sizeof(mbox_sts));
		mbox_cmd[0] = MBOX_CMD_WRITE_FLASH;
		mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
		mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
		mbox_cmd[3] = INT_ISCSI_INITFW_FLASH_OFFSET;
		mbox_cmd[4] = sizeof(*pflash_init_fw_cb);
		mbox_cmd[5] = WRITE_FLASH_OPTION_COMMIT_DATA;

		if (qla4xxx_mailbox_command(ha, 6, 2, &mbox_cmd[0],
		    &mbox_sts[0]) == QLA_ERROR) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: WRITE_FLASH command failed \n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_ERR;
			ioctl->DetailStatus = mbox_sts[0];
			ioctl->ResponseLen = 0;
			goto exit_set_isns_svr;
		}

		QL4PRINT(QLP4,
		    printk("scsi%d: %s: WRITE_FLASH command successful \n",
				ha->host_no, __func__));
		QL4PRINT(QLP4,
		    printk("scsi%d: Init Fw Ctrl Blk\n", ha->host_no));
		qla4xxx_dump_bytes(QLP4, pflash_init_fw_cb,
		    sizeof(FLASH_INIT_FW_CTRL_BLK));

		/*
		 * Update internal iSNS info
		 */
		if (pisns_server->AutomaticiSNSDiscovery)
			ha->tcp_options |= TOPT_LEARN_ISNS_IP_ADDR_ENABLE;
		else
			ha->tcp_options	&= ~TOPT_LEARN_ISNS_IP_ADDR_ENABLE;

		memcpy(ha->isns_ip_address,
		       pflash_init_fw_cb->init_fw_cb.iSNSIPAddr,
		       MIN(sizeof(ha->isns_ip_address),
		sizeof(pflash_init_fw_cb->init_fw_cb.iSNSIPAddr)));

		ha->isns_server_port_number =
		pflash_init_fw_cb->init_fw_cb.iSNSServerPortNumber;
	}

	/*
	 * Start or Stop iSNS Service accordingly, if needed.
	 *---------------------------------------------------*/
	//FIXME:
	if (test_bit(ISNS_FLAG_ISNS_ENABLED_IN_ISP, &ha->isns_flags)) {
		if (!IPAddrIsZero(ha->isns_ip_address) &&
		    ha->isns_server_port_number &&
		    (ha->tcp_options & TOPT_LEARN_ISNS_IP_ADDR_ENABLE) == 0) {
			uint32_t ip_addr;
			IPAddr2Uint32(ha->isns_ip_address, &ip_addr);

			status = qla4xxx_isns_reenable(ha, ip_addr,
			    ha->isns_server_port_number);

			if (status == QLA_ERROR) {
				QL4PRINT(QLP4, printk(
				    "scsi%d: qla4xxx_isns_reenable failed!\n",
				    ha->host_no));
				ioctl->Status = EXT_STATUS_ERR;
				ioctl->DetailStatus = 0;
				ioctl->ResponseLen = 0;
				goto exit_set_isns_svr;
			}
		} else if (test_bit(ISNS_FLAG_ISNS_SRV_ENABLED, 
		    &ha->isns_flags) && IPAddrIsZero(ha->isns_ip_address)) {
			qla4xxx_isns_disable(ha);
		}
	}

	/*
	 * Complete IOCTL successfully
	 *----------------------------*/
	ioctl->Status = EXT_STATUS_OK;
	ioctl->DetailStatus = 0;
	ioctl->ResponseLen = 0;

exit_set_isns_svr:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_set_data
 *	This routine calls set data IOCTLs based on the IOCTL Sub Code.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *    	-EINVAL     = if the command is invalid
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int qla4extioctl_set_data(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	switch (ioctl->SubCode) {
	case EXT_SC_RST_STATISTICS_GEN:
		return(qla4extioctl_rst_statistics_gen(ha, ioctl));

	case EXT_SC_RST_STATISTICS_ISCSI:
		return(qla4extioctl_rst_statistics_iscsi(ha, ioctl));

	case EXT_SC_SET_DEVICE_ENTRY_ISCSI:
		return(qla4extioctl_set_device_entry_iscsi(ha, ioctl));

	case EXT_SC_SET_INIT_FW_ISCSI:
		return(qla4extioctl_set_init_fw_iscsi(ha, ioctl));

	case EXT_SC_SET_ISNS_SERVER:
		return(qla4extioctl_set_isns_server(ha, ioctl));

	default:
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unsupported set data sub-command "
		    "code (%X)\n",
		    ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		return(0);
	}
	return(0);
}

/**************************************************************************
 * qla4xxx_ioctl_sleep_done
 *	This routine is the callback function to wakeup ioctl completion
 *	semaphore for the ioctl request that is waiting.
 *
 * Input:
 *   	sem - pointer to the ioctl completion semaphore.
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static void
qla4xxx_ioctl_sleep_done (struct semaphore * sem)
{
	ENTER(__func__);

	if (sem != NULL) {
		QL4PRINT(QLP4, printk("%s: wake up sem.\n", __func__));
		QL4PRINT(QLP10, printk("%s: UP count=%d\n", __func__,
		    atomic_read(&sem->count)));
		up(sem);
	}

	LEAVE(__func__);
}

/**************************************************************************
 * qla4xxx_ioctl_sem_init
 *	This routine initializes the ioctl timer and semaphore used to wait
 *	for passthru completion.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
void
qla4xxx_ioctl_sem_init (scsi_qla_host_t *ha)
{
	init_timer(&(ha->ioctl->ioctl_cmpl_timer));
	ha->ioctl->ioctl_cmpl_timer.data = (ulong)&ha->ioctl->ioctl_cmpl_sem;
	ha->ioctl->ioctl_cmpl_timer.function =
	    (void (*)(ulong))qla4xxx_ioctl_sleep_done;
}

/**************************************************************************
 * qla4xxx_scsi_pass_done
 *	This routine resets the ioctl progress flag and wakes up the ioctl
 * 	completion semaphore.
 *
 * Input:
 *   	cmd - pointer to the passthru Scsi cmd structure which has completed.
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 *
 * Context:
 *	Interrupt context.
 **************************************************************************/
void
qla4xxx_scsi_pass_done(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	ENTER(__func__);

	/* First check to see if the command has previously timed-out
	 * because we don't want to get the up/down semaphore counters off.
	 */
	if (ha->ioctl->ioctl_scsi_pass_in_progress == 1) {
		ha->ioctl->ioctl_scsi_pass_in_progress = 0;
		ha->ioctl->ioctl_tov = 0;
		ha->ioctl->ioctl_err_cmd = NULL;

		up(&ha->ioctl->ioctl_cmpl_sem);
	}

	LEAVE(__func__);

	return;
}

/**************************************************************************
 * qla4extioctl_scsi_passthru
 *	This routine
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Map of DMA Buffer:
 *    +-------------------------+
 *    | EXT_SCSI_PASSTHRU_ISCSI |
 *    +-------------------------+
 *    | [SCSI READ|WRITE data]  |
 *    +-------------------------+
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_scsi_passthru(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int			status = 0;
	ddb_entry_t		*ddb_entry;
	int			i;
	EXT_SCSI_PASSTHRU_ISCSI *pscsi_pass;
	struct scsi_device	*pscsi_device;
	struct scsi_cmnd	*pscsi_cmd;
	srb_t			*srb;
	uint32_t		dma_buf_len;
	os_tgt_t		*tgt_entry;
	os_lun_t		*lun_entry;
	fc_port_t		*fcport;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (!ADAPTER_UP(ha)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: command not pocessed, "
		    "adapter link down.\n",
		    ha->host_no, __func__));
		ioctl->Status = EXT_STATUS_HBA_NOT_READY;
		return(QLA_ERROR);
	}

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pscsi_cmd,
	    sizeof(struct scsi_cmnd))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(struct scsi_cmnd)));
		goto error_exit_scsi_pass;
	}

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pscsi_device,
	    sizeof(struct scsi_device))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(struct scsi_device)));
		goto error_exit_scsi_pass;
	}

	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&pscsi_pass,
	    sizeof(EXT_SCSI_PASSTHRU_ISCSI))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(EXT_SCSI_PASSTHRU_ISCSI)));
		goto error_exit_scsi_pass;
	}

	memset(pscsi_device, 0, sizeof(struct scsi_device));
	memset(pscsi_pass, 0, sizeof(EXT_SCSI_PASSTHRU_ISCSI));
	memset(pscsi_cmd, 0, sizeof(struct scsi_cmnd));
	pscsi_cmd->device = pscsi_device;

	/* ---- Get passthru structure from user space ---- */
	if ((status = copy_from_user((uint8_t *)pscsi_pass,
	    Q64BIT_TO_PTR(ioctl->RequestAdr),
	    sizeof(EXT_SCSI_PASSTHRU_ISCSI))) != 0) {
		QL4PRINT(QLP2,
		    printk("scsi%d: %s: unable to copy passthru struct "
		    "from user's memory area.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto error_exit_scsi_pass;
	}

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: %s: incoming  EXT_SCSI_PASSTHRU_ISCSI structure:\n",
	    ha->host_no, __func__));
	qla4xxx_dump_bytes(QLP4|QLP10,
	    pscsi_pass, sizeof(EXT_SCSI_PASSTHRU_ISCSI));

	/* ---- Make sure device exists ---- */
	tgt_entry = qla4xxx_lookup_target_by_SCSIID(ha, pscsi_pass->Addr.Bus,
	    pscsi_pass->Addr.Target);
	if (tgt_entry ==  NULL) {
		goto error_exit_scsi_pass;
	}

	lun_entry = qla4xxx_lookup_lun_handle(ha, tgt_entry,
	    pscsi_pass->Addr.Lun);
	if (lun_entry ==  NULL) {
		goto error_exit_scsi_pass;
	}

	fcport = lun_entry->fclun->fcport;
	if (fcport ==  NULL) {
		goto error_exit_scsi_pass;
	}

	ddb_entry = fcport->ddbptr;

	if (ddb_entry == NULL) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: invalid device (b%d,t%d) specified.\n",
		    ha->host_no, __func__,
		    pscsi_pass->Addr.Bus, pscsi_pass->Addr.Target));

		ioctl->Status = EXT_STATUS_DEV_NOT_FOUND;
		goto error_exit_scsi_pass;
	}

	/* ---- Make sure device is in an active state ---- */
	if (ddb_entry->fw_ddb_device_state != DDB_DS_SESSION_ACTIVE) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: device (b%d,t%d) not in active state\n",
		    ha->host_no, __func__,
		    pscsi_pass->Addr.Bus, pscsi_pass->Addr.Target));

		ioctl->Status = EXT_STATUS_DEVICE_NOT_READY;
		goto error_exit_scsi_pass;
	}

	/* ---- Retrieve srb from pool ---- */
	srb = del_from_free_srb_q_head(ha);
	if (srb == NULL) {
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: srb not available\n",
		    ha->host_no, __func__));
		goto error_exit_scsi_pass;
	}

	/* ---- Allocate larger DMA buffer, if neccessary ---- */
	dma_buf_len = MAX(ioctl->ResponseLen - sizeof(EXT_SCSI_PASSTHRU_ISCSI),
	    ioctl->RequestLen - sizeof(EXT_SCSI_PASSTHRU_ISCSI));

	if (ha->ioctl_dma_buf_len < dma_buf_len &&
	    qla4xxx_resize_ioctl_dma_buf(ha, dma_buf_len) != QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: ERROR cannot allocate requested "
		    "DMA buffer size 0x%x.\n",
		    ha->host_no, __func__, dma_buf_len));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto error_exit_scsi_pass;
	}

	memset(ha->ioctl_dma_bufv, 0, ha->ioctl_dma_buf_len);

	/* ---- Fill in the SCSI command structure ---- */
	pscsi_cmd->device->channel = pscsi_pass->Addr.Bus;
	pscsi_cmd->device->id = pscsi_pass->Addr.Target;
	pscsi_cmd->device->lun = pscsi_pass->Addr.Lun;
	pscsi_cmd->device = pscsi_device;
	pscsi_cmd->device->host = ha->host;
	pscsi_cmd->request_buffer = ha->ioctl_dma_bufv;
	pscsi_cmd->scsi_done = qla4xxx_scsi_pass_done;
	pscsi_cmd->timeout_per_command = IOCTL_PASSTHRU_TOV * HZ;

	CMD_SP(pscsi_cmd) = (char *) srb;
	srb->cmd = pscsi_cmd;
	srb->fw_ddb_index = ddb_entry->fw_ddb_index;
	srb->lun = pscsi_cmd->device->lun;
	srb->flags |= SRB_IOCTL_CMD;

	if (pscsi_pass->CdbLength == 6 || pscsi_pass->CdbLength == 10 ||
	    pscsi_pass->CdbLength == 12 || pscsi_pass->CdbLength == 16) {
		pscsi_cmd->cmd_len = pscsi_pass->CdbLength;
	} else {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: Unsupported CDB length 0x%x \n",
		    ha->host_no, __func__, pscsi_cmd->cmd_len));

		ioctl->Status = EXT_STATUS_INVALID_PARAM;
		goto error_exit_scsi_pass;
	}

	if (pscsi_pass->Direction == EXT_DEF_SCSI_PASSTHRU_DATA_IN) {
		pscsi_cmd->sc_data_direction = DMA_FROM_DEVICE;
		pscsi_cmd->request_bufflen = ioctl->ResponseLen -
		    sizeof(EXT_SCSI_PASSTHRU_ISCSI);

	} else if (pscsi_pass->Direction ==  EXT_DEF_SCSI_PASSTHRU_DATA_OUT) {
		pscsi_cmd->sc_data_direction = DMA_TO_DEVICE;
		pscsi_cmd->request_bufflen = ioctl->RequestLen -
		    sizeof(EXT_SCSI_PASSTHRU_ISCSI);

		/* Sending user data from ioctl->ResponseAddr to SCSI
		 * command buffer
		 */
		if ((status = copy_from_user((uint8_t *)pscsi_cmd->
		    request_buffer, Q64BIT_TO_PTR(ioctl->RequestAdr) +
		    sizeof(EXT_SCSI_PASSTHRU_ISCSI),
		    pscsi_cmd->request_bufflen)) != 0) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: unable to copy write buffer "
			    "from user's memory area.\n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_COPY_ERR;
			goto error_exit_scsi_pass;
		}
	} else {
		pscsi_cmd->sc_data_direction = DMA_NONE;
		pscsi_cmd->request_buffer  = 0;
		pscsi_cmd->request_bufflen = 0;
	}

	memcpy(pscsi_cmd->cmnd, pscsi_pass->Cdb, pscsi_cmd->cmd_len);
	memcpy(pscsi_cmd->data_cmnd, pscsi_pass->Cdb, pscsi_cmd->cmd_len);

	QL4PRINT(QLP4,
	    printk("scsi%d:%d:%d:%d: %s: CDB = ",
	    ha->host_no, pscsi_cmd->device->channel, pscsi_cmd->device->id,
	    pscsi_cmd->device->lun, __func__));

	for (i = 0; i < pscsi_cmd->cmd_len; i++)
		QL4PRINT(QLP4, printk("%02X ", pscsi_cmd->cmnd[i]));

	QL4PRINT(QLP4, printk("\n"));


	/* ---- prepare for receiving completion ---- */
	ha->ioctl->ioctl_scsi_pass_in_progress = 1;
	ha->ioctl->ioctl_tov = pscsi_cmd->timeout_per_command;

	qla4xxx_ioctl_sem_init(ha);
	CMD_COMPL_STATUS(pscsi_cmd)  = IOCTL_INVALID_STATUS;
	CMD_PASSTHRU_TYPE(pscsi_cmd) = (void *)1;

	/* ---- send command to adapter ---- */
	QL4PRINT(QLP4, printk("scsi%d:%d:%d:%d: %s: sending command.\n",
	    ha->host_no, pscsi_cmd->device->channel, pscsi_cmd->device->id,
	    pscsi_cmd->device->lun, __func__));

	ha->ioctl->ioctl_cmpl_timer.expires = jiffies + ha->ioctl->ioctl_tov;
	add_timer(&ha->ioctl->ioctl_cmpl_timer);

	if (qla4xxx_send_command_to_isp(ha, srb) != QLA_SUCCESS) {
		add_to_free_srb_q(ha, srb);
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: error sending cmd to isp\n",
		    ha->host_no, __func__));
		del_timer(&ha->ioctl->ioctl_cmpl_timer);
		ioctl->Status = EXT_STATUS_DEV_NOT_FOUND;
		goto error_exit_scsi_pass;
	}

	down(&ha->ioctl->ioctl_cmpl_sem);

	/*******************************************************
	 *						       *
	 *             Passthru Completion                     *
	 *						       *
	 *******************************************************/
	del_timer(&ha->ioctl->ioctl_cmpl_timer);

	/* ---- check for timeout --- */
	if (ha->ioctl->ioctl_scsi_pass_in_progress == 1) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: ERROR = command timeout.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;

		if ((srb != NULL) && (srb->active_array_index < MAX_SRBS)) {
			u_long wait_cnt = WAIT_CMD_TOV;

			if ((srb->flags & SRB_FREE_STATE) == 0)
				qla4xxx_delete_timer_from_cmd(srb);

			/* Wait for command to get out of active state */
			wait_cnt = jiffies + WAIT_CMD_TOV * HZ;
			while (wait_cnt > jiffies){
				if (srb->flags != SRB_ACTIVE_STATE)
					break;

				QL4PRINT(QLP7, printk("."));
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(1 * HZ);
			}
		}

		ha->ioctl->ioctl_scsi_pass_in_progress = 0;
		goto error_exit_scsi_pass;
	}

	/* --- Return info from status entry --- */
	ioctl->DetailStatus = CMD_SCSI_STATUS(pscsi_cmd);
	pscsi_pass->Reserved[0] = (uint8_t) CMD_SCSI_STATUS(pscsi_cmd);
	pscsi_pass->Reserved[1] = (uint8_t) CMD_COMPL_STATUS(pscsi_cmd);
	pscsi_pass->Reserved[2] = (uint8_t) CMD_ACTUAL_SNSLEN(pscsi_cmd);
	pscsi_pass->Reserved[3] = (uint8_t) CMD_HOST_STATUS(pscsi_cmd);
	pscsi_pass->Reserved[6] = (uint8_t) CMD_ISCSI_RESPONSE(pscsi_cmd);
	pscsi_pass->Reserved[7] = (uint8_t) CMD_STATE_FLAGS(pscsi_cmd);

	if (CMD_ACTUAL_SNSLEN(pscsi_cmd)) {
		memcpy(pscsi_pass->SenseData, pscsi_cmd->sense_buffer,
		    MIN(CMD_ACTUAL_SNSLEN(pscsi_cmd),
		    sizeof(pscsi_pass->SenseData)));
	}

	/* ---- check for command completion --- */
	if (CMD_COMPL_STATUS(pscsi_cmd) == IOCTL_INVALID_STATUS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d:%d:%d:%d: %s: ERROR = "
		    "command not completed.\n",
		    ha->host_no, pscsi_cmd->device->channel,
		    pscsi_cmd->device->id,
		    pscsi_cmd->device->lun, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		goto error_exit_scsi_pass;

	} else if (CMD_HOST_STATUS(pscsi_cmd) == DID_OK) {

		ioctl->Status = EXT_STATUS_OK;

	} else if (CMD_COMPL_STATUS(pscsi_cmd) == SCS_DATA_UNDERRUN) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: Data underrun.  Resid = 0x%x\n",
		    ha->host_no, __func__, CMD_RESID_LEN(pscsi_cmd)));

		ioctl->Status = EXT_STATUS_DATA_UNDERRUN;
		pscsi_pass->Reserved[4] = MSB(CMD_RESID_LEN(pscsi_cmd));
		pscsi_pass->Reserved[5] = LSB(CMD_RESID_LEN(pscsi_cmd));

	} else if (CMD_COMPL_STATUS(pscsi_cmd) == SCS_DATA_OVERRUN) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: Data overrun.  Resid = 0x%x\n",
		    ha->host_no, __func__, CMD_RESID_LEN(pscsi_cmd)));

		ioctl->Status = EXT_STATUS_DATA_OVERRUN;
		pscsi_pass->Reserved[4] = MSB(CMD_RESID_LEN(pscsi_cmd));
		pscsi_pass->Reserved[5] = LSB(CMD_RESID_LEN(pscsi_cmd));

	} else {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: Command completed in ERROR. "
		    "cs=%04x, ss=%-4x\n", ha->host_no, __func__,
		    CMD_COMPL_STATUS(pscsi_cmd), CMD_SCSI_STATUS(pscsi_cmd)));

		if (CMD_SCSI_STATUS(pscsi_cmd) != SCSI_GOOD) {
			ioctl->Status = EXT_STATUS_SCSI_STATUS;
		} else {
			ioctl->Status = EXT_STATUS_ERR;
		}
	}

	/* ---- Copy SCSI Passthru structure with updated sense buffer
	 *      to user space ----
	 */
	if (copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), pscsi_pass,
	    sizeof(EXT_SCSI_PASSTHRU_ISCSI)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy passthru struct "
		    "to user's memory area.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto error_exit_scsi_pass;
	}

	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d: %s: outgoing EXT_SCSI_PASSTHRU_ISCSI structure:\n",
	    ha->host_no, __func__));
	qla4xxx_dump_bytes(QLP4|QLP10,
	    Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    sizeof(EXT_SCSI_PASSTHRU_ISCSI));

	/* ---- Copy SCSI READ data from SCSI command buffer
	*       to user space ---- */
	if (pscsi_pass->Direction == EXT_DEF_SCSI_PASSTHRU_DATA_IN) {
		void    *xfer_ptr = Q64BIT_TO_PTR(ioctl->ResponseAdr) +
				    sizeof(EXT_SCSI_PASSTHRU_ISCSI);
		uint32_t xfer_len = ioctl->ResponseLen -
				    sizeof(EXT_SCSI_PASSTHRU_ISCSI);


		/* Update ResponseLen if a data underrun occurred */
		if (CMD_COMPL_STATUS(pscsi_cmd) == SCS_DATA_UNDERRUN &&
		    CMD_RESID_LEN(pscsi_cmd)) {
			xfer_len -= CMD_RESID_LEN(pscsi_cmd);
		}

		if ((status = copy_to_user(xfer_ptr, pscsi_cmd->request_buffer,
		    xfer_len)) != 0) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: unable to copy READ data "
			    "to user's memory area.\n",
			    ha->host_no, __func__));

			ioctl->Status = EXT_STATUS_COPY_ERR;
			goto error_exit_scsi_pass;
		}

		QL4PRINT(QLP4|QLP10,
		    printk("scsi%d: %s: outgoing READ data:  (0x%p)\n",
		    ha->host_no, __func__, xfer_ptr));

		qla4xxx_dump_bytes(QLP4|QLP10, xfer_ptr, xfer_len);
	}

	goto exit_scsi_pass;

error_exit_scsi_pass:
	ioctl->ResponseLen = 0;

exit_scsi_pass:
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_iscsi_passthru
 *	This routine sends iSCSI pass-through to destination.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_iscsi_passthru(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
	QL4PRINT(QLP4, printk("scsi%d: %s: UNSUPPORTED\n",
	    ha->host_no, __func__));

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4extioctl_get_hbacnt
 *	This routine retrieves the number of supported HBAs found.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4extioctl_get_hbacnt(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	EXT_HBA_COUNT	hba_cnt;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	hba_cnt.HbaCnt = qla4xxx_get_hba_count();
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    &hba_cnt, sizeof(hba_cnt))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to copy data\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_hbacnt;
	}

	QL4PRINT(QLP4, printk("scsi%d: %s: hbacnt is %d\n",
	    ha->host_no, __func__, hba_cnt.HbaCnt));
	ioctl->Status = EXT_STATUS_OK;

exit_get_hbacnt:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

STATIC int
qla4extioctl_get_hostno(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	ioctl->HbaSelect = ha->host_no;
	ioctl->Status = EXT_STATUS_OK;

	QL4PRINT(QLP4, printk("scsi%d: %s: instance is %d\n",
	    ha->host_no, __func__, ha->instance));

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

STATIC int
qla4extioctl_driver_specific(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int			status = 0;
	EXT_LN_DRIVER_DATA      data;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));

	if (ioctl->ResponseLen < sizeof(EXT_LN_DRIVER_DATA)) {
		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;

		QL4PRINT(QLP2|QLP4,
		    printk("%s: ERROR ResponseLen too small.\n",
		    __func__));

		goto exit_driver_specific;
	}

	data.DrvVer.Major = QL4_DRIVER_MAJOR_VER;
	data.DrvVer.Minor = QL4_DRIVER_MINOR_VER;
	data.DrvVer.Patch = QL4_DRIVER_PATCH_VER;
	data.DrvVer.Beta  = QL4_DRIVER_BETA_VER;
	/* RLU: set this flag when code is added.
	data.Flags = EXT_DEF_NGFO_CAPABLE;
	 */
	if (IS_QLA4010(ha))
		data.AdapterModel = EXT_DEF_QLA4010_DRIVER;
	else if (IS_QLA4022(ha))
		data.AdapterModel = EXT_DEF_QLA4022_DRIVER;

	status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr), &data,
	    sizeof(EXT_LN_DRIVER_DATA));

	if (status) {
		ioctl->Status = EXT_STATUS_COPY_ERR;

		QL4PRINT(QLP2|QLP4,
		    printk("%s: ERROR copy resp buf\n", __func__));
	}

exit_driver_specific:

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

ioctl_tbl_row_t IOCTL_CMD_TBL[] =
{
	{EXT_CC_QUERY, "EXT_CC_QUERY"},
	{EXT_CC_REG_AEN, "EXT_CC_REG_AEN"},
	{EXT_CC_GET_AEN, "EXT_CC_GET_AEN"},
	{EXT_CC_GET_DATA, "EXT_CC_GET_DATA"},
	{EXT_CC_SET_DATA, "EXT_CC_SET_DATA"},
	{EXT_CC_SEND_SCSI_PASSTHRU, "EXT_CC_SEND_SCSI_PASSTHRU"},
	{EXT_CC_SEND_ISCSI_PASSTHRU, "EXT_CC_SEND_ISCSI_PASSTHRU"},
	{INT_CC_LOGOUT_ISCSI, "INT_CC_LOGOUT_ISCSI"},
	{EXT_CC_GET_HBACNT, "EXT_CC_GET_HBACNT"},
	{INT_CC_DIAG_PING, "INT_CC_DIAG_PING"},
	{INT_CC_GET_DATA, "INT_CC_GET_DATA"},
	{INT_CC_SET_DATA, "INT_CC_SET_DATA"},
	{INT_CC_HBA_RESET, "INT_CC_HBA_RESET"},
	{INT_CC_COPY_FW_FLASH, "INT_CC_COPY_FW_FLASH"},
	{INT_CC_IOCB_PASSTHRU, "INT_CC_IOCB_PASSTHRU"},
	{0, "UNKNOWN"}
};

ioctl_tbl_row_t IOCTL_SCMD_QUERY_TBL[] =
{
	{EXT_SC_QUERY_HBA_ISCSI_NODE, "EXT_SC_QUERY_HBA_ISCSI_NODE"},
	{EXT_SC_QUERY_HBA_ISCSI_PORTAL, "EXT_SC_QUERY_HBA_ISCSI_PORTAL"},
	{EXT_SC_QUERY_DISC_ISCSI_NODE, "EXT_SC_QUERY_DISC_ISCSI_NODE"},
	{EXT_SC_QUERY_DISC_ISCSI_PORTAL, "EXT_SC_QUERY_DISC_ISCSI_PORTAL"},
	{EXT_SC_QUERY_DRIVER, "EXT_SC_QUERY_DRIVER"},
	{EXT_SC_QUERY_FW, "EXT_SC_QUERY_FW"},
	{EXT_SC_QUERY_CHIP, "EXT_SC_QUERY_CHIP"},
	{0, "UNKNOWN"}
};

ioctl_tbl_row_t IOCTL_SCMD_EGET_DATA_TBL[] =
{
	{EXT_SC_GET_STATISTICS_ISCSI, "EXT_SC_GET_STATISTICS_ISCSI"},
	{EXT_SC_GET_DEVICE_ENTRY_ISCSI, "EXT_SC_GET_DEVICE_ENTRY_ISCSI"},
	{EXT_SC_GET_DEVICE_ENTRY_DEFAULTS_ISCSI, "EXT_SC_GET_DEVICE_ENTRY_DEFAULTS_ISCSI"},
	{EXT_SC_GET_INIT_FW_ISCSI, "EXT_SC_GET_INIT_FW_ISCSI"},
	{EXT_SC_GET_INIT_FW_DEFAULTS_ISCSI, "EXT_SC_GET_INIT_FW_DEFAULTS_ISCSI"},
	{EXT_SC_GET_ISNS_SERVER, "EXT_SC_GET_ISNS_SERVER"},
	{EXT_SC_GET_ISNS_DISCOVERED_TARGETS, "EXT_SC_GET_ISNS_DISCOVERED_TARGETS"},
	{0, "UNKNOWN"}
};

ioctl_tbl_row_t IOCTL_SCMD_ESET_DATA_TBL[] =
{
	{EXT_SC_RST_STATISTICS_GEN, "EXT_SC_RST_STATISTICS_GEN"},
	{EXT_SC_RST_STATISTICS_ISCSI, "EXT_SC_RST_STATISTICS_ISCSI"},
	{EXT_SC_SET_DEVICE_ENTRY_ISCSI, "EXT_SC_SET_DEVICE_ENTRY_ISCSI"},
	{EXT_SC_SET_INIT_FW_ISCSI, "EXT_SC_SET_INIT_FW_ISCSI"},
	{EXT_SC_SET_ISNS_SERVER, "EXT_SC_SET_ISNS_SERVER"},
	{0, "UNKNOWN"}
};

char *IOCTL_TBL_STR(int cc, int sc)
{
	ioctl_tbl_row_t *r;
	int cmd;

	switch (cc) {
	case EXT_CC_QUERY:
		r = IOCTL_SCMD_QUERY_TBL;
		cmd = sc;
		break;
	case EXT_CC_GET_DATA:
		r = IOCTL_SCMD_EGET_DATA_TBL;
		cmd = sc;
		break;
	case EXT_CC_SET_DATA:
		r = IOCTL_SCMD_ESET_DATA_TBL;
		cmd = sc;
		break;
	case INT_CC_GET_DATA:
		r = IOCTL_SCMD_IGET_DATA_TBL;
		cmd = sc;
		break;
	case INT_CC_SET_DATA:
		r = IOCTL_SCMD_ISET_DATA_TBL;
		cmd = sc;
		break;

	default:
		r = IOCTL_CMD_TBL;
		cmd = cc;
		break;
	}

	while (r->cmd != 0) {
		if (r->cmd == cmd) break;
		r++;
	}
	return(r->s);

}

/**************************************************************************
 * qla4xxx_ioctl
 * 	This the main entry point for all ioctl requests
 *
 * Input:
 *	dev - pointer to SCSI device structure
 *	cmd - internal or external ioctl command code
 *	arg - pointer to the main ioctl structure
 *
 *	Instance field in ioctl structure - to determine which device to
 *	perform ioctl
 *	HbaSelect field in ioctl structure - to determine which adapter to
 *	perform ioctl
 *
 * Output:
 *	The resulting data/status is returned via the main ioctl structure.
 *
 *	When Status field in ioctl structure is valid for normal command errors
 * 	this function returns 0 (QLA_SUCCESS).
 *
 *      All other return values indicate ioctl/system specific error which
 *	prevented the actual ioctl command from completing.
 *
 * Returns:
 *	 QLA_SUCCESS - command completed successfully, either with or without
 *			errors in the Status field of the main ioctl structure
 *    	-EFAULT      - arg pointer is NULL or memory access error
 *    	-EINVAL      - command is invalid
 *    	-ENOMEM      - memory allocation failed
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_ioctl(struct scsi_device *dev, int cmd, void *arg)
{
	EXT_IOCTL_ISCSI *pioctl = NULL;
	scsi_qla_host_t *ha = NULL;
	int status = 0;	/* ioctl status; errno value when function returns */
	int tmp_stat;

	ENTER(__func__);

	/* Catch any non-exioct ioctls */
	if (_IOC_TYPE(cmd) != QLMULTIPATH_MAGIC) {
		printk(KERN_WARNING
		    "qla4xxx: invalid ioctl magic number received.\n");
		QL4PRINT(QLP2|QLP4,
		    printk("scsi(): %s: invalid magic number received.\n",
		    __func__));

		status = (-EINVAL);
		goto exit_qla4xxx_ioctl;
	}

	QL4PRINT(QLP4,
	    printk("scsi(): %s: received cmd %x.\n",
	    __func__, cmd));

	switch (cmd) {
		/* All NFO functions go here */
	case EXT_CC_TRANSPORT_INFO:
	case EXT_CC_GET_FOM_PROP:
	case EXT_CC_GET_HBA_INFO:
	case EXT_CC_GET_DPG_PROP:
	case EXT_CC_GET_DPG_PATH_INFO:
	case EXT_CC_SET_DPG_PATH_INFO:
	case EXT_CC_GET_LB_INFO:
	case EXT_CC_GET_LB_POLICY:
	case EXT_CC_SET_LB_POLICY:
	case EXT_CC_GET_DPG_STATS:
	case EXT_CC_CLEAR_DPG_ERR_STATS:
	case EXT_CC_CLEAR_DPG_IO_STATS:
	case EXT_CC_CLEAR_DPG_FO_STATS:
	case EXT_CC_GET_PATHS_FOR_ALL:
	case EXT_CC_MOVE_PATH:
	case EXT_CC_VERIFY_PATH:
	case EXT_CC_GET_EVENT_LIST:
	case EXT_CC_ENABLE_FOM:
	case EXT_CC_DISABLE_FOM:
	case EXT_CC_GET_STORAGE_LIST:
		status = qla4xxx_nfo_ioctl(dev, cmd, arg);
		goto exit_qla4xxx_ioctl;
	}

	/* Allocate ioctl structure buffer to support multiple concurrent
	 * entries. NO static structures allowed.
	 */
	pioctl = QL_KMEM_ZALLOC(sizeof(EXT_IOCTL_ISCSI));
	if (pioctl == NULL) {
		/* error */
		printk(KERN_WARNING
		    "qla4xxx: ERROR in main ioctl buffer allocation.\n");
		status = (-ENOMEM);
		goto exit_qla4xxx_ioctl;
	}

	/*
	 * Check to see if we can access the ioctl command structure
	 */
	if (!access_ok(VERIFY_WRITE, arg, sizeof(EXT_IOCTL_ISCSI))) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s: EXT_IOCTL_ISCSI access error.\n",
		    __func__));

		status = (-EFAULT);
		goto exit_qla4xxx_ioctl;
	}

	/*
	 * Copy the ioctl command structure from user space to local structure
	 */
	if ((status = copy_from_user((uint8_t *)pioctl, arg,
	    sizeof(EXT_IOCTL_ISCSI)))) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s: EXT_IOCTL_ISCSI copy error.\n",
		    __func__));

		goto exit_qla4xxx_ioctl;
	}

	QL4PRINT(QLP4|QLP10, printk("EXT_IOCTL_ISCSI structure dump: \n"));
	qla4xxx_dump_dwords(QLP4|QLP10, pioctl, sizeof(*pioctl));

        /* check signature of this ioctl */
	if (memcmp(pioctl->Signature, EXT_DEF_REGULAR_SIGNATURE,
	    sizeof(EXT_DEF_REGULAR_SIGNATURE)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s: signature did not match. "
		    "received cmd=%x arg=%p signature=%s.\n",
		    __func__, cmd, arg, pioctl->Signature));
		pioctl->Status = EXT_STATUS_INVALID_PARAM;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_ISCSI));

		goto exit_qla4xxx_ioctl;
	}

        /* check version of this ioctl */
        if (pioctl->Version > EXT_VERSION) {
                printk(KERN_WARNING
                    "ql4xxx: ioctl interface version not supported = %d.\n",
                    pioctl->Version);

		pioctl->Status = EXT_STATUS_UNSUPPORTED_VERSION;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_ISCSI));
		goto exit_qla4xxx_ioctl;
        }

	/*
	 * Get the adapter handle for the corresponding adapter instance
	 */
	ha = qla4xxx_get_adapter_handle(pioctl->HbaSelect);
	if (ha == NULL) {
		QL4PRINT(QLP2,
		    printk("%s: NULL EXT_IOCTL_ISCSI buffer\n",
		    __func__));

		pioctl->Status = EXT_STATUS_DEV_NOT_FOUND;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_ISCSI));
		goto exit_qla4xxx_ioctl;
	}

	QL4PRINT(QLP4, printk("scsi%d: ioctl+ (%s)\n", ha->host_no,
	    IOCTL_TBL_STR(cmd, pioctl->SubCode)));

	down(&ha->ioctl->ioctl_sem);

	/*
	 * If the DPC is active, wait for it to complete before proceeding
	 */
	while (ha->dpc_active) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1*HZ);
	}

	ha->i_start = jiffies;
	ha->i_end = 0;
	ha->f_start = 0;
	ha->f_end = 0;

	/*
	 * Issue the ioctl command
	 */
	switch (cmd) {
	case EXT_CC_QUERY:
		status = qla4extioctl_query(ha, pioctl);
		break;

	case EXT_CC_REG_AEN:
		status = qla4extioctl_reg_aen(ha, pioctl);
		break;

	case EXT_CC_GET_AEN:
		status = qla4extioctl_get_aen(ha, pioctl);
		break;

	case EXT_CC_GET_DATA:
		status = qla4extioctl_get_data(ha, pioctl);
		break;

	case EXT_CC_SET_DATA:
		status = qla4extioctl_set_data(ha, pioctl);
		break;

	case EXT_CC_SEND_SCSI_PASSTHRU:
		status = qla4extioctl_scsi_passthru(ha, pioctl);
		break;

	case EXT_CC_SEND_ISCSI_PASSTHRU:
		status = qla4extioctl_iscsi_passthru(ha, pioctl);
		break;

	case INT_CC_LOGOUT_ISCSI:
		status = qla4intioctl_logout_iscsi(ha, pioctl);
		break;

	case EXT_CC_GET_HBACNT:
		status = qla4extioctl_get_hbacnt(ha, pioctl);
		break;

	case EXT_CC_GET_HOST_NO:
		status = qla4extioctl_get_hostno(ha, pioctl);
		break;

	case EXT_CC_DRIVER_SPECIFIC:
		status = qla4extioctl_driver_specific(ha, pioctl);
		break;

	case INT_CC_DIAG_PING:
		status = qla4intioctl_ping(ha, pioctl);
		break;

	case INT_CC_GET_DATA:
		status = qla4intioctl_get_data(ha, pioctl);
		break;

	case INT_CC_SET_DATA:
		status = qla4intioctl_set_data(ha, pioctl);
		break;

	case INT_CC_HBA_RESET:
		status = qla4intioctl_hba_reset(ha, pioctl);
		break;

	case INT_CC_COPY_FW_FLASH:
		status = qla4intioctl_copy_fw_flash(ha, pioctl);
		break;

	case INT_CC_IOCB_PASSTHRU:
		status = qla4intioctl_iocb_passthru(ha, pioctl);
		break;

	default:
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unsupported command code (%x)\n",
		    ha->host_no, __func__, cmd));

		pioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
	}

	/*
	 * Copy the updated ioctl structure back to the user
	 */
	tmp_stat = copy_to_user(arg, (void *)pioctl, sizeof(EXT_IOCTL_ISCSI));
	if (status == 0) {
		status = tmp_stat;
	}

	ha->i_end = jiffies;

	up(&ha->ioctl->ioctl_sem);

	QL4PRINT(QLP15, printk("scsi%d: ioctl- (%s) "
	    "i_start=%lx, f_start=%lx, f_end=%lx, i_end=%lx\n",
	    ha->host_no, IOCTL_TBL_STR(cmd, pioctl->SubCode),
	    ha->i_start, ha->f_start, ha->f_end, ha->i_end));

exit_qla4xxx_ioctl:

	if (pioctl)
		QL_KMEM_FREE(pioctl);

	LEAVE(__func__);

	return(status);
}

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

