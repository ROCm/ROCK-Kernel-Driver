/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP4xxx device driver for Linux 2.6.x
* Copyright (C) 2004 QLogic Corporation
* (www.qlogic.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************
* Failover include file
******************************************************************************/

#include "ql4_def.h"

#include <linux/blkdev.h>
#include <asm/uaccess.h>

#include "qlnfo.h"
#include "ql4_ioctl.h"

/*
 * Global variables
 */

/*
 * Support routines
 */

/*
 * qla4xxx_get_hba
 *	Searches the hba structure chain for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	inst = adapter instance number.
 *
 * Returns:
 *	Return value is a pointer to the adapter structure or
 *      NULL if instance not found.
 *
 * Context:
 *	Kernel context.
 */
scsi_qla_host_t *
qla4xxx_get_hba(unsigned long instance)
{
	int	found;
	scsi_qla_host_t *ha;

	ha = NULL;
	found = 0;
	read_lock(&qla4xxx_hostlist_lock);
	list_for_each_entry(ha, &qla4xxx_hostlist, list) {
		if (ha->instance == instance) {
			found++;
			break;
		}
	}
	read_unlock(&qla4xxx_hostlist_lock);

	return (found ? ha : NULL);
}

/*
 * qla4xxx_nfo_ioctl
 *	Provides functions for failover ioctl() calls.
 *
 * Input:
 *	ha = adapter state pointer.
 *	ioctl_code = ioctl function to perform
 *	arg = Address of application EXT_IOCTL_NFO cmd data
 *	mode = flags
 *
 * Returns:
 *	Return value is the ioctl rval_p return value.
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_nfo_ioctl(struct scsi_device *dev, int cmd, void *arg)
{
	char		*ptemp;
	int		status = 0;
	int		tmp_stat = 0;
	EXT_IOCTL_NFO	*pioctl = NULL;
	scsi_qla_host_t *ha = NULL;


	ENTER(__func__);

	/*
	 * Check to see if we can access the ioctl command structure
	 */
	if (!access_ok(VERIFY_WRITE, arg, sizeof(EXT_IOCTL_NFO))) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s: NULL EXT_IOCTL_NFO buffer\n",
		    __func__));

		status = (-EFAULT);
		goto exit_qla4nfo_ioctl;
	}

	/* Allocate ioctl structure buffer to support multiple concurrent
	 * entries. NO static structures allowed.
	 */
	pioctl = QL_KMEM_ZALLOC(sizeof(EXT_IOCTL_NFO));
	if (pioctl == NULL) {
		/* error */
		printk(KERN_WARNING
		    "qla4xxx: ERROR in main nfo ioctl buffer allocation.\n");
		status = (-ENOMEM);
		goto exit_qla4nfo_ioctl;
	}

	/*
	 * Copy the ioctl command structure from user space to local structure
	 */
	status = copy_from_user((uint8_t *)pioctl, arg, sizeof(EXT_IOCTL_NFO));
	if (status) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi: %s: EXT_IOCTL_NFO copy error.\n",
		    __func__));

		goto exit_qla4nfo_ioctl;
	}
	QL4PRINT(QLP4|QLP10, printk("EXT_IOCTL_NFO structure: \n"));
	qla4xxx_dump_dwords(QLP4|QLP10, pioctl, sizeof(*pioctl));

        /* check signature of this ioctl */
        ptemp = (uint8_t *)pioctl->Signature;

	if (memcmp(ptemp, NFO_DEF_SIGNATURE, NFO_DEF_SIGNATURE_SIZE) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("%s: signature did not match. "
		    "cmd=%x arg=%p.\n", __func__, cmd, arg));
		pioctl->Status = EXT_STATUS_INVALID_PARAM;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_NFO));

		goto exit_qla4nfo_ioctl;
	}

	/* check version of this ioctl */
	if (pioctl->Version > NFO_VERSION) {
		printk(KERN_WARNING
		    "ql4xxx: ioctl interface version not supported = %d.\n",
		    pioctl->Version);

		pioctl->Status = EXT_STATUS_UNSUPPORTED_VERSION;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_NFO));
		goto exit_qla4nfo_ioctl;
	}

	if (!((ulong)pioctl->VendorSpecificData & EXT_DEF_USE_HBASELECT)) {
		/* we don't support api that are too old */
		QL4PRINT(QLP2|QLP4,
		    printk(
		    "%s: got setinstance cmd w/o HbaSelect. Return error.\n",
		    __func__));
		pioctl->Status = EXT_STATUS_INVALID_PARAM;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_NFO));
		goto exit_qla4nfo_ioctl;
	}

	/*
	 * Get the adapter handle for the corresponding adapter instance
	 */
	ha = qla4xxx_get_adapter_handle(pioctl->HbaSelect);
	if (ha == NULL) {
		QL4PRINT(QLP2,
		    printk("%s: NULL EXT_IOCTL_NFO buffer\n",
		    __func__));

		pioctl->Status = EXT_STATUS_DEV_NOT_FOUND;
		status = copy_to_user(arg, (void *)pioctl,
		    sizeof(EXT_IOCTL_NFO));
		goto exit_qla4nfo_ioctl;
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
#if 0
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
		status = xx();
		break;
#endif
	default:
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unsupported command code (%X)\n",
		    ha->host_no, __func__, cmd));

		pioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
	}

	/*
	 * Copy the updated ioctl structure back to the user
	 */
	tmp_stat = copy_to_user(arg, (void *)pioctl, sizeof(EXT_IOCTL_NFO));
	if (status == 0)
		status = tmp_stat;

	ha->i_end = jiffies;

	up(&ha->ioctl->ioctl_sem);

	QL4PRINT(QLP4, printk("scsi%d: ioctl- (%s) "
	    "i_start=%lx, f_start=%lx, f_end=%lx, i_end=%lx\n",
	    ha->host_no, IOCTL_TBL_STR(cmd, pioctl->SubCode),
	    ha->i_start, ha->f_start, ha->f_end, ha->i_end));

exit_qla4nfo_ioctl:

	if (pioctl)
		QL_KMEM_FREE(pioctl);

	LEAVE(__func__);

	return (status);
}


