/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.6.x
* Copyright (C) 2003 QLogic Corporation
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

#include "qla_os.h"
#include "qla_def.h"

#include "qlfo.h"
#include "qlfolimits.h"

//TODO Why??
#include "qla_fo.cfg"

/* This type is used to create a temporary list of port names */
typedef struct _portname_list {
	struct _portname_list *pnext;
	uint8_t 	portname[8];
} portname_list;

/*
 * Global variables
 */
SysFoParams_t qla_fo_params;

/*
 * Local routines
 */
#if !defined(linux)
static int qla2x00_sdm_setup(EXT_IOCTL *cmd_stp, void *arg, int mode);
#endif
static uint32_t qla2x00_fo_get_params(PFO_PARAMS pp);
static uint32_t qla2x00_fo_set_params(PFO_PARAMS pp);
static uint8_t qla2x00_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp);
static int qla2x00_fo_get_lun_data(EXT_IOCTL *pext,
    FO_LUN_DATA_INPUT *bp, int mode);
static int qla2x00_fo_set_lun_data(EXT_IOCTL *pext,
    FO_LUN_DATA_INPUT *bp, int mode);
static uint32_t qla2x00_fo_stats(FO_HBA_STAT *stat_p, uint8_t reset);
static int qla2x00_fo_get_target_data(EXT_IOCTL *pext,
    FO_TARGET_DATA_INPUT *bp, int mode);

static int qla2x00_std_get_tgt(scsi_qla_host_t *, EXT_IOCTL *,
    FO_DEVICE_DATA *);
static int qla2x00_fo_get_tgt(mp_host_t *, scsi_qla_host_t *, EXT_IOCTL *,
    FO_DEVICE_DATA *);
static int qla2x00_fo_set_target_data(EXT_IOCTL *pext,
    FO_TARGET_DATA_INPUT *bp, int mode);

static int qla2x00_port_name_in_list(uint8_t *, portname_list *);
static int qla2x00_add_to_portname_list(uint8_t *, portname_list **);
static void qla2x00_free_portname_list(portname_list **);

/*
 * qla2x00_get_hba
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
qla2x00_get_hba(unsigned long instance)
{
	int	found;
	struct list_head *hal;
	scsi_qla_host_t *ha;

	ha = NULL;
	found = 0;
	read_lock(&qla_hostlist_lock);
	list_for_each(hal, &qla_hostlist) {
		ha = list_entry(hal, scsi_qla_host_t, list);

		if (ha->instance == instance) {
			found++;
			break;
		}
	}
	read_unlock(&qla_hostlist_lock);

	return (found ? ha : NULL);
}

/*
 * qla2x00_fo_stats
 *	Searches the hba structure chan for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	stat_p = Pointer to FO_HBA_STAT union.
 *      reset  = Flag, TRUE = reset statistics.
 *                     FALSE = return statistics values.
 *
 * Returns:
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_stats(FO_HBA_STAT *stat_p, uint8_t reset)
{
	int32_t	inst, idx;
	uint32_t rval = 0;
	struct list_head *hal;
	scsi_qla_host_t *ha;

	DEBUG9(printk("%s: entered.\n", __func__);)

	inst = stat_p->input.HbaInstance;
	stat_p->info.HbaCount = 0;

	ha = NULL;

	read_lock(&qla_hostlist_lock);
	list_for_each(hal, &qla_hostlist) {
		ha = list_entry(hal, scsi_qla_host_t, list);

		if (inst == FO_ADAPTER_ALL) {
			stat_p->info.HbaCount++;
			idx = ha->instance;
		} else if (ha->instance == inst) {
			stat_p->info.HbaCount = 1;
			idx = inst;
		}
		if (reset == TRUE) {
			DEBUG9(printk("%s: reset stats.\n", __func__);)
			ha->IosRequested = 0;
			ha->BytesRequested = 0;
			ha->IosExecuted = 0;
			ha->BytesExecuted = 0;
		} else {
 			DEBUG9(printk("%s: get stats for inst %d.\n",
 			    __func__, inst);)
 
#if 0
			stat_p->info.StatEntry[idx].IosRequested =
				ha->IosRequested;
			stat_p->info.StatEntry[idx].BytesRequested =
				ha->BytesRequested;
			stat_p->info.StatEntry[idx].IosExecuted =
				ha->IosExecuted;
			stat_p->info.StatEntry[idx].BytesExecuted =
				ha->BytesExecuted;
#endif
		}
		if (inst != FO_ADAPTER_ALL)
			break;
	}
	read_unlock(&qla_hostlist_lock);
 
 	DEBUG9(printk("%s: exiting.\n", __func__);)
 
	return rval;
}

/*
 * qla2x00_fo_get_lun_data
 *      Get lun data from all devices attached to a HBA (FO_GET_LUN_DATA).
 *      Gets lun mask if failover not enabled.
 *
 * Input:
 *      ha = pointer to adapter
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_lun_data(EXT_IOCTL *pext, FO_LUN_DATA_INPUT *bp, int mode)
{
	scsi_qla_host_t  *ha;
	struct list_head	*fcports;
	fc_port_t        *fcport;
	int              ret = 0;
	mp_host_t        *host = NULL;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	os_tgt_t         *ostgt;
	uint8_t          path_id;
	uint16_t         dev_no;
	uint16_t         cnt;
	uint16_t         lun;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;
	FO_LUN_DATA_LIST *u_list, *list;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((unsigned long)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)
	DEBUG4(printk("%s: hba %p, buff %p bp->HbaInstance(%x).\n",
	    __func__, ha, bp, (int)bp->HbaInstance));

	if (qla2x00_failover_enabled(ha)) {
		if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
			if (list_empty(&ha->fcports)) {
				DEBUG2_9_10(printk(
				    "%s: no HOST for ha inst %ld.\n",
				    __func__, ha->instance);)
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				return (ret);
			}

			/* Since all ports are unconfigured, return a dummy
			 * entry for each of them.
			 */
			list = (FO_LUN_DATA_LIST *)qla2x00_kmem_zalloc(
			    sizeof(FO_LUN_DATA_LIST), GFP_ATOMIC, 12);
			if (list == NULL) {
				DEBUG2_9_10(printk("%s: failed to alloc "
				    "memory of size (%d)\n", __func__,
				    (int)sizeof(FO_LUN_DATA_LIST));)
				pext->Status = EXT_STATUS_NO_MEMORY;
				return (-ENOMEM);
			}

			entry = &list->DataEntry[0];

			u_list = (FO_LUN_DATA_LIST *)pext->ResponseAdr;
			u_entry = &u_list->DataEntry[0];

			fcport = NULL;
			list_for_each_entry(fcport, &ha->fcports, list) {
				memcpy(entry->NodeName, fcport->node_name,
				    EXT_DEF_WWN_NAME_SIZE);
				memcpy(entry->PortName, fcport->port_name,
				    EXT_DEF_WWN_NAME_SIZE);

				DEBUG9(printk("%s(%ld): entry %d for "
				    "unconfigured portname=%02x%02x"
				    "%02x%02x%02x%02x%02x%02x, "
				    "tgt_id=%d.\n",
				    __func__, ha->host_no,
				    list->EntryCount,
				    entry->PortName[0],
				    entry->PortName[1],
				    entry->PortName[2],
				    entry->PortName[3],
				    entry->PortName[4],
				    entry->PortName[5],
				    entry->PortName[6],
				    entry->PortName[7],
				    entry->TargetId);)

				list->EntryCount++;

				ret = verify_area(VERIFY_WRITE,
				    (void *)u_entry,
				    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk(
					    "%s: u_entry %p verify "
					    "wrt err. EntryCount=%d.\n",                                                    __func__, u_entry,
					    list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}
				
				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
				if (ret) { /* error */
					DEBUG2_9_10(printk(
					    "%s: u_entry %p copy out "
					    "err. EntryCount=%d.\n",
					    __func__, u_entry,
					    list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;
			}

			if (ret == 0) {
				ret = verify_area(VERIFY_WRITE,
				    (void *)&u_list->EntryCount,
				    sizeof(list->EntryCount));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk(
					    "%s: u_list->EntryCount %p verify "
					    " write error. "
					    "list->EntryCount=%d.\n",
					    __func__, u_entry,
					    list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
				} else {
					/* copy number of entries */
					ret = copy_to_user(&u_list->EntryCount,
					    &list->EntryCount,
					    sizeof(list->EntryCount));
				}
			}

			KMEM_FREE(list, sizeof(FO_LUN_DATA_LIST));

			return (ret);
		}
	}

	list = (FO_LUN_DATA_LIST *)qla2x00_kmem_zalloc(
	    sizeof(FO_LUN_DATA_LIST), GFP_ATOMIC, 12);
	if (list == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	entry = &list->DataEntry[0];

	u_list = (FO_LUN_DATA_LIST *)pext->ResponseAdr;
	u_entry = &u_list->DataEntry[0];

	/* find the correct fcport list */
	if (!qla2x00_failover_enabled(ha))
		fcports = &ha->fcports;
	else
		fcports = host->fcports;

	/* Check thru this adapter's fcport list */
	fcport = NULL;
	list_for_each_entry(fcport, fcports, list) {
                if ((atomic_read(&fcport->state) != FCS_ONLINE) &&
		    !qla2x00_is_fcport_in_config(ha, fcport)) {
			/* no need to report */
			DEBUG2_9_10(printk("%s(%ld): not reporting fcport "
			    "%02x%02x%02x%02x%02x%02x%02x%02x. state=%i,"
			    " flags=%02x.\n",
			    __func__, ha->host_no, fcport->port_name[0],
			    fcport->port_name[1], fcport->port_name[2],
			    fcport->port_name[3], fcport->port_name[4],
			    fcport->port_name[5], fcport->port_name[6],
			    fcport->port_name[7], atomic_read(&fcport->state),
			    fcport->flags);)
			continue;
		}

		memcpy(entry->NodeName,
		    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		/* Return dummy entry for unconfigured ports */
		if (fcport->mp_byte & MP_MASK_UNCONFIGURED) {
			for (lun = 0; lun < MAX_LUNS; lun++) {
				entry->Data[lun] = 0;
			}
			entry->TargetId = 0;

			DEBUG9(printk("%s(%ld): entry %d for unconfigured "
			    "portname=%02x%02x%02x%02x%02x%02x%02x%02x, "
			    "tgt_id=%d.\n",
			    __func__, ha->host_no,
			    list->EntryCount,
			    entry->PortName[0], entry->PortName[1],
			    entry->PortName[2], entry->PortName[3],
			    entry->PortName[4], entry->PortName[5],
			    entry->PortName[6], entry->PortName[7],
			    entry->TargetId);)

			list->EntryCount++;

			ret = verify_area(VERIFY_WRITE, (void *)u_entry,
			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s: u_entry %p "
				    "verify wrt err. EntryCount=%d.\n",
				    __func__, u_entry, list->EntryCount);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s: u_entry %p "
				    "copy out err. EntryCount=%d.\n",
				    __func__, u_entry, list->EntryCount);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;

			continue;
		}

		if (!qla2x00_failover_enabled(ha)) {
			/*
			 * Failover disabled. Just return LUN mask info
			 * in lun data entry of this port.
			 */
			entry->TargetId = 0;
			for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
				if (!(ostgt = ha->otgt[cnt])) {
					continue;
				}

				if (ostgt->fcport == fcport) {
					entry->TargetId = cnt;
					break;
				}
			}
			if (cnt == MAX_FIBRE_DEVICES) {
				/* Not found?  For now just go to next port. */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_10)
				uint8_t          *tmp_name;

				tmp_name = fcport->port_name;

 				printk("%s(%ld): ERROR - port "
 				    "%02x%02x%02x%02x%02x%02x%02x%02x "
 				    "not configured.\n",
 				    __func__, ha->host_no,
 				    tmp_name[0], tmp_name[1], tmp_name[2],
 				    tmp_name[3], tmp_name[4], tmp_name[5],
 				    tmp_name[6], tmp_name[7]);
#endif /* DEBUG */

				continue;
			}

			/* Got a valid port */
			list->EntryCount++;

			for (lun = 0; lun < MAX_LUNS; lun++) {
				/* set MSB if masked */
				entry->Data[lun] = LUN_DATA_PREFERRED_PATH;
				if (!EXT_IS_LUN_BIT_SET(&(fcport->lun_mask),
				    lun)) {
					entry->Data[lun] |= LUN_DATA_ENABLED;
				}
			}

 			DEBUG9(printk("%s: got lun_mask for tgt %d\n",
 			    __func__, cnt);)
 			DEBUG9(qla2x00_dump_buffer((char *)&(fcport->lun_mask),
 			    sizeof(lun_bit_mask_t));)
 
 			ret = verify_area(VERIFY_WRITE, (void *)u_entry,
 			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
 			if (ret) {
 				/* error */
 				DEBUG9_10(printk("%s: u_entry %p verify write"
 				    " error. list->EntryCount=%d.\n",
 				    __func__, u_entry, list->EntryCount);)
 				pext->Status = EXT_STATUS_COPY_ERR;
 				break;
 			}
 
 			ret = copy_to_user(u_entry, entry,
 			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
 
 			if (ret) {
 				/* error */
 				DEBUG9_10(printk("%s: u_entry %p copy "
 				    "error. list->EntryCount=%d.\n",
 				    __func__, u_entry, list->EntryCount);)
 				pext->Status = EXT_STATUS_COPY_ERR;
 				break;
 			}

			copy_to_user(u_entry, entry,
					sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));

			/* Go to next port */
			u_entry++;
			continue;
		}

		/*
		 * Failover is enabled. Go through the mp_devs list and return
		 * lun data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path = path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				/* Got an entry */
				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;
				list->EntryCount++;

				DEBUG9_10(printk(
				    "%s(%ld): got lun_mask for tgt %d\n",
				    __func__, ha->host_no, entry->TargetId);)
				DEBUG9(qla2x00_dump_buffer(
				    (char *)&(fcport->lun_mask),
				    sizeof(lun_bit_mask_t));)

				for (lun = 0; lun < MAX_LUNS; lun++) {
					entry->Data[lun] =
					    path->lun_data.data[lun];
				}

				ret = verify_area(VERIFY_WRITE, (void *)u_entry,
				    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "verify wrt err. EntryCount=%d.\n",
					    __func__, u_entry, list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "copy out err. EntryCount=%d.\n",
					    __func__, u_entry, list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;
				
				DEBUG9_10(printk("%s: get_lun_data for tgt "
				    "%d- u_entry(%p) - lun entry[%d] :\n",
				    __func__, entry->TargetId,
				    u_entry,list->EntryCount - 1);)
									 
				DEBUG9(qla2x00_dump_buffer((void *)entry, 64);)

				/*
				 * We found the right path for this port.
				 * Continue with next port.
				 */
				break;
			}

			/* Continue with next port. */
			break;
		}
	}

	DEBUG9(printk("%s: get_lun_data - entry count = [%d]\n",
	    __func__, list->EntryCount);)
	DEBUG4(printk("%s: get_lun_data - entry count = [%d]\n",
	    __func__, list->EntryCount);)

	if (ret == 0) {
		ret = verify_area(VERIFY_WRITE, (void *)&u_list->EntryCount,
		    sizeof(list->EntryCount));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_list->EntryCount %p verify "
			    " write error. list->EntryCount=%d.\n",
			    __func__, u_entry, list->EntryCount);)
			pext->Status = EXT_STATUS_COPY_ERR;
		} else {
			/* copy number of entries */
			ret = copy_to_user(&u_list->EntryCount, &list->EntryCount,
			    sizeof(list->EntryCount));
			pext->ResponseLen = FO_LUN_DATA_LIST_MAX_SIZE;
		}
	}

	KMEM_FREE(list, sizeof(FO_LUN_DATA_LIST));

	DEBUG9(printk("%s: exiting. ret=%d.\n", __func__, ret);)

	return ret;
}

/*
 * qla2x00_fo_set_lun_data
 *      Set lun data for the specified device on the attached hba
 *      (FO_SET_LUN_DATA).
 *      Sets lun mask if failover not enabled.
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_lun_data(EXT_IOCTL *pext, FO_LUN_DATA_INPUT  *bp, int mode)
{
	scsi_qla_host_t  *ha;
	fc_port_t        *fcport;
	int              i;
	int              ret = 0;
	mp_host_t        *host = NULL;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	os_tgt_t         *ostgt;
	uint8_t	         path_id;
	uint16_t         dev_no;
	uint16_t         lun;
	FO_LUN_DATA_LIST *u_list, *list;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;

	typedef struct _tagStruct {
		FO_LUN_DATA_INPUT   foLunDataInput;
		FO_LUN_DATA_LIST    foLunDataList;
	}
	com_struc;
	com_struc *com_iter;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((unsigned long)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (qla2x00_failover_enabled(ha)) {
		if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
			DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			return (ret);
		}
	}

	list = (FO_LUN_DATA_LIST *)qla2x00_kmem_zalloc(
	    sizeof(FO_LUN_DATA_LIST), GFP_ATOMIC, 13);
	if (list == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	entry = &list->DataEntry[0];

	/* get lun data list from user */
	com_iter = (com_struc *)pext->RequestAdr;
	u_list = &(com_iter->foLunDataList);
	u_entry = &u_list->DataEntry[0];

	ret = verify_area(VERIFY_READ, (void *)u_list,
	    sizeof(FO_LUN_DATA_LIST));
	if (ret) {
		/* error */
		DEBUG2_9_10(printk("%s: u_list %p verify read error.\n",
		    __func__, u_list);)
		pext->Status = EXT_STATUS_COPY_ERR;
		KMEM_FREE(list, FO_LUN_DATA_LIST);
		return (ret);
	}

	ret = copy_from_user(list, u_list, sizeof(FO_LUN_DATA_LIST));
	if (ret) {
		/* error */
		DEBUG2_9_10(printk("%s: u_list %p copy error.\n",
		    __func__, u_list);)
		pext->Status = EXT_STATUS_COPY_ERR;
		KMEM_FREE(list, FO_LUN_DATA_LIST);
		return (ret);
	}

	DEBUG2(printk("qla_fo_set_lun_data: pext->RequestAdr(%p) u_list (%p) "
			"sizeof(FO_LUN_DATA_INPUT) =(%d) and 64 bytes...\n",
			pext->RequestAdr, u_list,
			(int)sizeof(FO_LUN_DATA_INPUT));)
	DEBUG2(qla2x00_dump_buffer((void *)u_list, 64);)

	for (i = 0; i < list->EntryCount; i++, u_entry++) {

		ret = verify_area(VERIFY_READ, (void *)u_entry,
		    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p verify "
			    " read error.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}
		ret = copy_from_user(entry, u_entry,
		    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p copy error.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		if (!qla2x00_failover_enabled(ha)) {
			/*
			 * Failover disabled. Just find the port and set
			 * LUN mask values in lun_mask field of this port.
			 */

			if (entry->TargetId >= MAX_FIBRE_DEVICES)
				/* ERROR */
				continue;

			if (!(ostgt = ha->otgt[entry->TargetId]))
				/* ERROR */
				continue;

			if (!(fcport = ostgt->fcport))
				/* ERROR */
				continue;

			for (lun = 0; lun < MAX_LUNS; lun++) {
				/* set MSB if masked */
				if (entry->Data[lun] | LUN_DATA_ENABLED) {
					EXT_CLR_LUN_BIT(&(fcport->lun_mask),
								lun);
				} else {
					EXT_SET_LUN_BIT(&(fcport->lun_mask),
								lun);
				}
			}

			/* Go to next entry */
			continue;
		}

		/*
		 * Failover is enabled. Go through the mp_devs list and set lun
		 * data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
					continue;

			if ((pathlist = dp->path_list) == NULL)
					continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path = path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				for (lun = 0; lun < MAX_LUNS; lun++) {
					path->lun_data.data[lun] =
					    entry->Data[lun];
					DEBUG4(printk("cfg_set_lun_data: lun "
					    "data[%d] = 0x%x \n", lun,
					    path->lun_data.data[lun]);)
				}

				break;
			}
			break;
		}
	}

	KMEM_FREE(list, FO_LUN_DATA_LIST);

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return ret;
}

/*
 * qla2x00_fo_get_target_data
 *      Get the target control byte for all devices attached to a HBA.
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_target_data(EXT_IOCTL *pext, FO_TARGET_DATA_INPUT *bp, int mode)
{
	scsi_qla_host_t  *ha;
	int              ret = 0;
	mp_host_t        *host = NULL;
	FO_DEVICE_DATA   *entry;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((unsigned long)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (qla2x00_failover_enabled(ha)) {
		if ((host = qla2x00_cfg_find_host(ha)) == NULL &&
		    list_empty(&ha->fcports)) {
			DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			return (ret);
		}
	}

	if ((entry = (FO_DEVICE_DATA *)kmalloc(sizeof(FO_DEVICE_DATA),
	    GFP_ATOMIC)) == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_DEVICE_DATA));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	/* Return data accordingly. */
	if (!qla2x00_failover_enabled(ha))
		ret = qla2x00_std_get_tgt(ha, pext, entry);
	else
		ret = qla2x00_fo_get_tgt(host, ha, pext, entry);


	if (ret == 0) {
		pext->ResponseLen = sizeof(FO_DEVICE_DATABASE);
	}

	KMEM_FREE(entry, sizeof(FO_DEVICE_DATA));

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return (ret);
}

static int
qla2x00_std_get_tgt(scsi_qla_host_t *ha, EXT_IOCTL *pext, FO_DEVICE_DATA *entry)
{
	int              ret = 0;
	uint8_t          i, cnt;
	uint32_t	b;
	fc_port_t        *fcport;
	os_tgt_t         *ostgt;
	FO_DEVICE_DATA   *u_entry;

	DEBUG9(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	u_entry = (FO_DEVICE_DATA *) pext->ResponseAdr;

	if (pext->ResponseLen < sizeof(FO_DEVICE_DATA)) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		DEBUG9_10(printk("%s: ERROR ResponseLen %d too small.\n",
		    __func__, pext->ResponseLen);)

		return (ret);
	}

	DEBUG9(printk("%s(%ld): user buffer size=%d. Copying fcport list\n",
	    __func__, ha->host_no, pext->ResponseLen);)

	/* Loop through and return ports found. */
	/* Check thru this adapter's fcport list */
	i = 0;
	fcport = NULL;
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (i >= MAX_TARGETS)
			break;

		/* clear for a new entry */
		memset(entry, 0, sizeof(FO_DEVICE_DATA));

		memcpy(entry->WorldWideName,
		    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		for (b = 0; b < 3 ; b++)
			entry->PortId[b] = fcport->d_id.r.d_id[2-b];

		DEBUG9(printk("%s(%ld): found fcport %p:%02x%02x%02x%02x"
		    "%02x%02x%02x%02x.\n",
		    __func__, ha->host_no,
		    fcport,
		    fcport->port_name[0],
		    fcport->port_name[1],
		    fcport->port_name[2],
		    fcport->port_name[3],
		    fcport->port_name[4],
		    fcport->port_name[5],
		    fcport->port_name[6],
		    fcport->port_name[7]);)

		/*
		 * Just find the port and return target info.
		 */
		for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
			if (!(ostgt = ha->otgt[cnt])) {
				continue;
			}

			if (ostgt->fcport == fcport) {
				DEBUG9(printk("%s(%ld): Found target %d.\n",
				    __func__, ha->host_no, cnt);)

				entry->TargetId = cnt;
				break;
			}
		}

		if (cnt == MAX_FIBRE_DEVICES) {
			/* Not bound, this target is unconfigured. */
			entry->MultipathControl = MP_MASK_UNCONFIGURED;
		} else {
			entry->MultipathControl = 0; /* always configured */
		}

		ret = verify_area(VERIFY_WRITE, (void *)u_entry,
		    sizeof(FO_DEVICE_DATA));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s(%ld): u_entry %p verify "
			    " wrt err. tgt id=%d.\n",
			    __func__, ha->host_no, u_entry, cnt);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		ret = copy_to_user(u_entry, entry,
		    sizeof(FO_DEVICE_DATA));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s(%ld): u_entry %p copy "
			    "out err. tgt id=%d.\n",
			    __func__, ha->host_no, u_entry, cnt);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		u_entry++;
	}

	DEBUG9(printk("%s(%ld): done copying fcport list entries.\n",
	    __func__, ha->host_no);)

	DEBUG9(printk("%s(%ld): exiting. ret = %d.\n",
	    __func__, ha->host_no, ret);)

	return (ret);
}

static int
qla2x00_fo_get_tgt(mp_host_t *host, scsi_qla_host_t *ha, EXT_IOCTL *pext,
    FO_DEVICE_DATA *entry)
{
	int		ret = 0;
	uint8_t 	path_id;
	uint16_t	dev_no;
	uint32_t	b;
	uint16_t	cnt;

	fc_port_t        *fcport;
	mp_device_t	*dp;
	mp_path_list_t	*pathlist;
	mp_path_t	*path;

	FO_DEVICE_DATA	*u_entry;

	DEBUG9(printk("%s(%ld): entered.\n", __func__, host->ha->host_no);)

	u_entry = (FO_DEVICE_DATA *) pext->ResponseAdr;

	/* If host is NULL then report all online fcports of the corresponding
	 * ha as unconfigured devices.  ha should never be NULL.
	 */
	if (host == NULL) {
		/* Loop through and return ports found. */
		/* Check thru this adapter's fcport list */
		cnt = 0;
		fcport = NULL;
		list_for_each_entry(fcport, &ha->fcports, list) {
			
			if (atomic_read(&fcport->state) != FCS_ONLINE) {
				/* no need to report */
				DEBUG2_9_10(printk("%s(%ld): not reporting "
				    "fcport %02x%02x%02x%02x%02x%02x%02x%02x. "
				    "state=%i, flags=%02x.\n",
				    __func__, ha->host_no,
				    fcport->port_name[0], fcport->port_name[1],
				    fcport->port_name[2], fcport->port_name[3],
				    fcport->port_name[4], fcport->port_name[5],
				    fcport->port_name[6], fcport->port_name[7],
				    atomic_read(&fcport->state),
				    fcport->flags);)
				continue;
			}

			cnt++;
			if (cnt >= MAX_TARGETS)
				break;

			/* clear for a new entry */
			memset(entry, 0, sizeof(FO_DEVICE_DATA));

			memcpy(entry->WorldWideName,
			    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
			memcpy(entry->PortName,
			    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

			DEBUG10(printk("%s(%ld): found fcport %p:%02x%02x%02x"
			    "%02x%02x%02x%02x%02x.\n",
			    __func__, host->ha->host_no,
			    fcport,
			    fcport->port_name[0],
			    fcport->port_name[1],
			    fcport->port_name[2],
			    fcport->port_name[3],
			    fcport->port_name[4],
			    fcport->port_name[5],
			    fcport->port_name[6],
			    fcport->port_name[7]);)

			for (b = 0; b < 3 ; b++)
				entry->PortId[b] = fcport->d_id.r.d_id[2-b];

			DEBUG9_10(printk("%s(%ld): fcport mpbyte=%02x. "
			    "return unconfigured. ",
			    __func__, host->ha->host_no, fcport->mp_byte);)

			entry->TargetId = 0;
			entry->Dev_No = 0;
			entry->MultipathControl = MP_MASK_UNCONFIGURED;

			DEBUG9_10(printk("tgtid=%d dev_no=%d, mpdata=0x%x.\n",
			    entry->TargetId, entry->Dev_No,
			    entry->MultipathControl);)

			ret = verify_area(VERIFY_WRITE, (void *)u_entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "verify wrt err. no tgt id.\n",
				    __func__, host->ha->host_no, u_entry);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "copy out err. no tgt id.\n",
				    __func__, host->ha->host_no, u_entry);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;
		}

		DEBUG9(printk("%s(%ld): after returning unconfigured fcport "
		    "list. got %d entries.\n",
		    __func__, host->ha->host_no, cnt);)

		return (ret);
	}

	/* Check thru fcport list on host */
	/* Loop through and return online ports found. */
	/* Check thru this adapter's fcport list */
	cnt = 0;
	fcport = NULL;
	list_for_each_entry(fcport, host->fcports, list) {

		if ((atomic_read(&fcport->state) != FCS_ONLINE) &&
		    !qla2x00_is_fcport_in_config(ha, fcport)) {
			/* no need to report */
			DEBUG2_9_10(printk("%s(%ld): not reporting "
			    "fcport %02x%02x%02x%02x%02x%02x%02x%02x. "
			    "state=%i, flags=%02x.\n",
			    __func__, ha->host_no, fcport->port_name[0],
			    fcport->port_name[1], fcport->port_name[2],
			    fcport->port_name[3], fcport->port_name[4],
			    fcport->port_name[5], fcport->port_name[6],
			    fcport->port_name[7],
			    atomic_read(&fcport->state),
			    fcport->flags);)
			continue;
		}

		cnt++;
		if (cnt >= MAX_TARGETS)
			break;

		/* clear for a new entry */
		memset(entry, 0, sizeof(FO_DEVICE_DATA));

		memcpy(entry->WorldWideName,
		    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		DEBUG10(printk("%s(%ld): found fcport %p:%02x%02x%02x%02x"
		    "%02x%02x%02x%02x.\n",
		    __func__, host->ha->host_no,
		    fcport,
		    fcport->port_name[0],
		    fcport->port_name[1],
		    fcport->port_name[2],
		    fcport->port_name[3],
		    fcport->port_name[4],
		    fcport->port_name[5],
		    fcport->port_name[6],
		    fcport->port_name[7]);)

		for (b = 0; b < 3 ; b++)
			entry->PortId[b] = fcport->d_id.r.d_id[2-b];

		if (fcport->mp_byte & MP_MASK_UNCONFIGURED) {
			DEBUG9_10(printk("%s(%ld): fcport mpbyte=%02x. "
			    "return unconfigured. ",
			    __func__, host->ha->host_no, fcport->mp_byte);)

			entry->TargetId = fcport->os_target_id;
			entry->Dev_No = 0;
			entry->MultipathControl = MP_MASK_UNCONFIGURED;

			DEBUG9_10(printk("tgtid=%d dev_no=%d, mpdata=0x%x.\n",
			    entry->TargetId, entry->Dev_No,
			    entry->MultipathControl);)

			ret = verify_area(VERIFY_WRITE, (void *)u_entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "verify wrt err. tgt id=%d.\n",
				    __func__, host->ha->host_no, u_entry,
				    fcport->os_target_id);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "copy out err. tgt id=%d.\n",
				    __func__, host->ha->host_no, u_entry,
				    fcport->os_target_id);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;
			continue;
		}

		/*
		 * Port was configured. Go through the mp_devs list and
		 * get target data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path= path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;
				entry->MultipathControl = path->mp_byte;

				if (path->config == TRUE ||
				    !mp_config_required) {
					entry->MultipathControl = path->mp_byte;
				} else {
					entry->MultipathControl =
					    MP_MASK_UNCONFIGURED;
				}

				DEBUG9_10(printk("%s(%ld): fcport path->id "
				    "= %d, target/mpbyte data = 0x%02x.\n",
				    __func__, host->ha->host_no,
				    path->id, entry->MultipathControl);)

				ret = verify_area(VERIFY_WRITE, (void *)u_entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s(%ld): u_entry %p"
					    " verify wrt err. tgt id=%d.\n",
					    __func__, host->ha->host_no,
					    u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s(%ld): u_entry %p "
					    "copy out err. tgt id=%d.\n",
					    __func__, host->ha->host_no,
					    u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;

				/* Path found. Continue with next fcport */
				break;
			}
			break;
		}
	}

	DEBUG9(printk("%s(%ld): after checking fcport list. got %d entries.\n",
	    __func__, host->ha->host_no, cnt);)

	/* For ports not found but were in config file, return unconfigured
	 * status so agent will try to issue commands to it and GUI will display
	 * them as missing.
	 */
	for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		/* Sanity check */
		if (qla2x00_is_wwn_zero(dp->nodename))
			continue;

		if ((pathlist = dp->path_list) == NULL)
			continue;

		path = pathlist->last;
		for (path_id = 0; path_id < pathlist->path_cnt;
		    path_id++, path = path->next) {

			/* Sanity check */
			if (qla2x00_is_wwn_zero(path->portname))
				continue;

			if (path->port == NULL) {
				if (path->host != host) {
					/* path on other host. no need to
					 * report
					 */
					DEBUG10(printk("%s(%ld): path host %p "
					    "not for current host %p.\n",
					    __func__, host->ha->host_no,
					    path->host, host);)

					continue;
				}

				/* clear for a new entry */
				memset(entry, 0, sizeof(FO_DEVICE_DATA));

				/* This device was not found. Return
				 * unconfigured.
				 */
				memcpy(entry->WorldWideName,
				    dp->nodename, EXT_DEF_WWN_NAME_SIZE);
				memcpy(entry->PortName,
				    path->portname, EXT_DEF_WWN_NAME_SIZE);

				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;
				/*
				entry->MultipathControl = path->mp_byte
				    | MP_MASK_UNCONFIGURED;
				    */
				entry->MultipathControl = MP_MASK_UNCONFIGURED;
				cnt++;

				DEBUG9_10(printk("%s: found missing device. "
				    "return tgtid=%d dev_no=%d, mpdata=0x%x for"
				    " port %02x%02x%02x%02x%02x%02x%02x%02x\n",
				    __func__, entry->TargetId, entry->Dev_No,
				    entry->MultipathControl,
				    path->portname[0], path->portname[1],
				    path->portname[2], path->portname[3],
				    path->portname[4], path->portname[5],
				    path->portname[6], path->portname[7]);)

				ret = verify_area(VERIFY_WRITE, (void *)u_entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "verify wrt err. tgt id=%d.\n",
					    __func__, u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "copy out err. tgt id=%d.\n",
					    __func__, u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;
			}
		}
	}

	DEBUG9(printk("%s(%ld): after checking missing devs. got %d entries.\n",
	    __func__, host->ha->host_no, cnt);)

	DEBUG9(printk("%s(%ld): exiting. ret = %d.\n",
	    __func__, host->ha->host_no, ret);)

	return (ret);

} /* qla2x00_get_fo_tgt */

/*
 * qla2x00_fo_set_target_data
 *      Set multipath control byte for all devices on the attached hba
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_target_data(EXT_IOCTL *pext, FO_TARGET_DATA_INPUT  *bp, int mode)
{
	scsi_qla_host_t  *ha;
	int              i;
	int              ret = 0;
	mp_host_t        *host;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	uint16_t         dev_no;
	uint8_t	         path_id;
	FO_DEVICE_DATA *entry, *u_entry;

	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((unsigned long)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (!qla2x00_failover_enabled(ha))
		/* non-failover mode. nothing to be done. */
		return 0;

	if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
		DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
		    __func__, ha->instance);)
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	entry = (FO_DEVICE_DATA *)qla2x00_kmem_zalloc(
	    sizeof(FO_DEVICE_DATA), GFP_ATOMIC, 15);
	if (entry == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_DEVICE_DATA));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	u_entry = (FO_DEVICE_DATA *)(pext->RequestAdr +
	    sizeof(FO_TARGET_DATA_INPUT));

	for (i = 0; i < MAX_TARGETS; i++, u_entry++) {
		ret = verify_area(VERIFY_READ, (void *)u_entry,
		    sizeof(FO_DEVICE_DATA));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p verify read err.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		ret = copy_from_user(entry, u_entry, sizeof(FO_DEVICE_DATA));

		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p copy error.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path= path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				path->mp_byte = entry->MultipathControl;

				DEBUG9(printk("cfg_set_target_data: %d target "
				    "data = 0x%x \n",
				    path->id,path->mp_byte);)

				/*
				 * If this is the visible path, then make it
				 * available on next reboot.
				 */
				if (!((path->mp_byte & MP_MASK_HIDDEN) ||
				    (path->mp_byte & MP_MASK_UNCONFIGURED))) {
					pathlist->visible = path->id;
				}

				/* Found path. Go to next entry. */
				break;
			}
			break;
		}
	}

	KMEM_FREE(entry, sizeof(FO_DEVICE_DATA));

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return (ret);

}

/*
 * qla2x00_fo_ioctl
 *	Provides functions for failover ioctl() calls.
 *
 * Input:
 *	ha = adapter state pointer.
 *	ioctl_code = ioctl function to perform
 *	arg = Address of application EXT_IOCTL cmd data
 *	mode = flags
 *
 * Returns:
 *	Return value is the ioctl rval_p return value.
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
/* ARGSUSED */
int
qla2x00_fo_ioctl(scsi_qla_host_t *ha, int ioctl_code, EXT_IOCTL *pext, int mode)
{
	int	rval = 0;
	size_t	in_size, out_size;
	static	union {
		FO_PARAMS params;
		FO_GET_PATHS path;
		FO_SET_CURRENT_PATH set_path;
		/* FO_HBA_STAT_INPUT stat; */
		FO_HBA_STAT stat;
		FO_LUN_DATA_INPUT lun_data;
		FO_TARGET_DATA_INPUT target_data;
	} buff;

	ENTER("qla2x00_fo_ioctl");
	DEBUG9(printk("%s: entered. arg (%p):\n", __func__, pext);)

	/*
	 * default case for this switch not needed,
	 * ioctl_code validated by caller.
	 */
	in_size = out_size = 0;
	switch (ioctl_code) {
		case FO_CC_GET_PARAMS:
			out_size = sizeof(FO_PARAMS);
			break;
		case FO_CC_SET_PARAMS:
			in_size = sizeof(FO_PARAMS);
			break;
		case FO_CC_GET_PATHS:
			in_size = sizeof(FO_GET_PATHS);
			break;
		case FO_CC_SET_CURRENT_PATH:
			in_size = sizeof(FO_SET_CURRENT_PATH);
			break;
		case FO_CC_GET_HBA_STAT:
		case FO_CC_RESET_HBA_STAT:
			in_size = sizeof(FO_HBA_STAT_INPUT);
			break;
		case FO_CC_GET_LUN_DATA:
			in_size = sizeof(FO_LUN_DATA_INPUT);
			break;
		case FO_CC_SET_LUN_DATA:
			in_size = sizeof(FO_LUN_DATA_INPUT);
			break;
		case FO_CC_GET_TARGET_DATA:
			in_size = sizeof(FO_TARGET_DATA_INPUT);
			break;
		case FO_CC_SET_TARGET_DATA:
			in_size = sizeof(FO_TARGET_DATA_INPUT);
			break;

	}
	if (in_size != 0) {
		if ((int)pext->RequestLen < in_size) {
			pext->Status = EXT_STATUS_INVALID_PARAM;
			pext->DetailStatus = EXT_DSTATUS_REQUEST_LEN;
			DEBUG10(printk("%s: got invalie req len (%d).\n",
			    __func__, pext->RequestLen);)

		} else {

			rval = verify_area(VERIFY_READ,
			    (void *)pext->RequestAdr, in_size);
			if (rval) {
				/* error */
				DEBUG2_9_10(printk("%s: req buf verify read "
				    "error. size=%ld.\n",
				    __func__, (ulong)in_size);)
				pext->Status = EXT_STATUS_COPY_ERR;
			}
			rval = copy_from_user(&buff,
			    (void *)pext->RequestAdr, in_size);

			if (rval) {
				DEBUG2_9_10(printk("%s: req buf copy error. "
				    "size=%ld.\n",
				    __func__, (ulong)in_size);)

				pext->Status = EXT_STATUS_COPY_ERR;
			} else {
				DEBUG9(printk("qla2x00_fo_ioctl: req buf "
				    "copied ok.\n"));
			}
		}
	} else if (out_size != 0 && (ulong)pext->ResponseLen < out_size) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		pext->DetailStatus = out_size;
		DEBUG10(printk("%s: got invalie resp len (%d).\n",
		    __func__, pext->ResponseLen);)
	}

	if (rval != 0 || pext->Status != 0)
		goto done_fo_ioctl;

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	switch (ioctl_code) {
		case FO_CC_GET_PARAMS:
			rval = qla2x00_fo_get_params(&buff.params);
			break;
		case FO_CC_SET_PARAMS:
			rval = qla2x00_fo_set_params(&buff.params);
			break;
		case FO_CC_GET_PATHS:
			rval = qla2x00_cfg_get_paths(pext,
			    &buff.path,mode);
			if (rval != 0)
				out_size = 0;
			break;
		case FO_CC_SET_CURRENT_PATH:
			rval = qla2x00_cfg_set_current_path(pext,
			    &buff.set_path,mode);
			break;
		case FO_CC_RESET_HBA_STAT:
			rval = qla2x00_fo_stats(&buff.stat, TRUE);
			break;
		case FO_CC_GET_HBA_STAT:
			rval = qla2x00_fo_stats(&buff.stat, FALSE);
			break;
		case FO_CC_GET_LUN_DATA:

			DEBUG4(printk("calling qla2x00_fo_get_lun_data\n");)
			DEBUG4(printk("pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_get_lun_data(pext,
			    &buff.lun_data, mode);

			if (rval != 0)
				out_size = 0;
			break;
		case FO_CC_SET_LUN_DATA:

			DEBUG4(printk("calling qla2x00_fo_set_lun_data\n");)
			DEBUG4(printk("	pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_set_lun_data(pext,
			    &buff.lun_data, mode);
			break;
		case FO_CC_GET_TARGET_DATA:
			DEBUG4(printk("calling qla2x00_fo_get_target_data\n");)
			DEBUG4(printk("pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_get_target_data(pext,
			    &buff.target_data, mode);

			if (rval != 0) {
				out_size = 0;
			}
			break;
		case FO_CC_SET_TARGET_DATA:
			DEBUG4(printk("calling qla2x00_fo_set_target_data\n");)
			DEBUG4(printk("	pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)
			rval = qla2x00_fo_set_target_data(pext,
			    &buff.target_data, mode);
			break;

	}

	if (rval == 0 && (pext->ResponseLen = out_size) != 0) {
		rval = verify_area(VERIFY_WRITE, (void *)pext->ResponseAdr,
		    out_size);
		if (rval != 0) {
			DEBUG10(printk("%s: resp buf very write error.\n",
			    __func__);)
			pext->Status = EXT_STATUS_COPY_ERR;
		}
	}

	if (rval == 0) {
		rval = copy_to_user((void *)pext->ResponseAdr,
		    &buff, out_size);

		if (rval != 0) {
			DEBUG10(printk("%s: resp buf copy error. size=%ld.\n",
			    __func__, (ulong)out_size);)
			pext->Status = EXT_STATUS_COPY_ERR;
		}
	}

done_fo_ioctl:

	if (rval != 0) {
		/*EMPTY*/
		DEBUG10(printk("%s: **** FAILED ****\n", __func__);)
	} else {
		/*EMPTY*/
		DEBUG9(printk("%s: exiting normally\n", __func__);)
	}

	return rval;
}


/*
 * qla2x00_fo_count_retries
 *	Increment the retry counter for the command.
 *      Set or reset the SRB_RETRY flag.
 *
 * Input:
 *	sp = Pointer to command.
 *
 * Returns:
 *	TRUE -- retry
 * 	FALSE -- don't retry
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp)
{
	uint8_t		retry = TRUE;
	os_lun_t	*lq;
	os_tgt_t	*tq;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (++sp->fo_retry_cnt >  qla_fo_params.MaxRetriesPerIo) {
		/* no more failovers for this request */
		retry = FALSE;
		sp->fo_retry_cnt = 0;
		printk(KERN_INFO
		    "qla2x00: no more failovers for request - pid= %ld\n",
		    sp->cmd->serial_number);
	} else {
		/*
		 * We haven't exceeded the max retries for this request, check
		 * max retries this path
		 */
		if ((sp->fo_retry_cnt % qla_fo_params.MaxRetriesPerPath) == 0) {
			DEBUG(printk("qla2x00_fo_count_retries: FAILOVER - "
			    "queuing ha=%ld, sp=%p, pid =%ld, fo retry= %d\n",
			    ha->host_no, sp, sp->cmd->serial_number,
			    sp->fo_retry_cnt);)

			/*
			 * Note: we don't want it to timeout, so it is
			 * recycling on the retry queue and the fialover queue.
			 */
			lq = sp->lun_queue;
			tq = sp->tgt_queue;
			set_bit(LUN_MPIO_BUSY, &lq->q_flag);

			/*
			 * ??? We can get a path error on any ha, but always
			 * queue failover on originating ha. This will allow us
			 * to syncronized the requests for a given lun.
			 */
			sp->f_start=jiffies;	/*ra 10/29/01*/
			/* Now queue it on to be failover */
			sp->ha = ha;
			add_to_failover_queue(ha, sp);
		}
	}

	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry ;
}


/*
 * qla2x00_fo_check
 *	This function is called from the done routine to see if
 *  the SRB requires a failover.
 *
 *	This function examines the available os returned status and
 *  if meets condition, the command(srb) is placed ont the failover
 *  queue for processing.
 *
 * Input:
 *	sp  = Pointer to the SCSI Request Block
 *
 * Output:
 *      sp->flags SRB_RETRY bit id command is to
 *      be retried otherwise bit is reset.
 *
 * Returns:
 *      None.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
uint8_t
qla2x00_fo_check(scsi_qla_host_t *ha, srb_t *sp)
{
	uint8_t		retry = FALSE;
	int host_status;
#if DEBUG_QLA2100
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* we failover on selction timeouts only */
	host_status = host_byte(sp->cmd->result);
	if (host_status == DID_NO_CONNECT) {
		if (qla2x00_fo_count_retries(ha, sp)) {
			/* Force a retry  on this request, it will
			 * cause the LINUX timer to get reset, while we
			 * we are processing the failover.
			 */
			sp->cmd->result = DID_BUS_BUSY << 16;
			retry = TRUE;
		}
		DEBUG(printk("qla2x00_fo_check: pid= %ld sp %p retry count=%d, "
		    "retry flag = %d, host status (%s)\n",
		    sp->cmd->serial_number, sp, sp->fo_retry_cnt, retry,
		    reason[host_status]);)
	}

	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry;
}

/*
 * qla2x00_fo_path_change
 *	This function is called from configuration mgr to notify
 *	of a path change.
 *
 * Input:
 *      type    = Failover notify type, FO_NOTIFY_LUN_RESET or FO_NOTIFY_LOGOUT
 *      newlunp = Pointer to the fc_lun struct for current path.
 *      oldlunp = Pointer to fc_lun struct for previous path.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
uint32_t
qla2x00_fo_path_change(uint32_t type, fc_lun_t *newlunp, fc_lun_t *oldlunp)
{
	uint32_t	ret = QLA_SUCCESS;

	newlunp->max_path_retries = 0;
	return ret;
}

/*
 * qla2x00_fo_get_params
 *	Process an ioctl request to get system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_get_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	pp->MaxPathsPerDevice = qla_fo_params.MaxPathsPerDevice;
	pp->MaxRetriesPerPath = qla_fo_params.MaxRetriesPerPath;
	pp->MaxRetriesPerIo = qla_fo_params.MaxRetriesPerIo;
	pp->Flags = qla_fo_params.Flags;
	pp->FailoverNotifyType = qla_fo_params.FailoverNotifyType;
	pp->FailoverNotifyCdbLength = qla_fo_params.FailoverNotifyCdbLength;
	memset(pp->FailoverNotifyCdb, 0, sizeof(pp->FailoverNotifyCdb));
	memcpy(pp->FailoverNotifyCdb,
	    &qla_fo_params.FailoverNotifyCdb[0], sizeof(pp->FailoverNotifyCdb));

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}

/*
 * qla2x00_fo_set_params
 *	Process an ioctl request to set system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_set_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	/* Check values for defined MIN and MAX */
	if ((pp->MaxPathsPerDevice > SDM_DEF_MAX_PATHS_PER_DEVICE) ||
	    (pp->MaxRetriesPerPath < FO_MAX_RETRIES_PER_PATH_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_PATH_MAX) ||
	    (pp->MaxRetriesPerIo < FO_MAX_RETRIES_PER_IO_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_IO_MAX)) {
		DEBUG2_9_10(printk("%s: got invalid params.\n", __func__);)
		return EXT_STATUS_INVALID_PARAM;
	}

	/* Update the global structure. */
	qla_fo_params.MaxPathsPerDevice = pp->MaxPathsPerDevice;
	qla_fo_params.MaxRetriesPerPath = pp->MaxRetriesPerPath;
	qla_fo_params.MaxRetriesPerIo = pp->MaxRetriesPerIo;
	qla_fo_params.Flags = pp->Flags;
	qla_fo_params.FailoverNotifyType = pp->FailoverNotifyType;
	qla_fo_params.FailoverNotifyCdbLength = pp->FailoverNotifyCdbLength;
	if (pp->FailoverNotifyType & FO_NOTIFY_TYPE_CDB) {
		if (pp->FailoverNotifyCdbLength >
		    sizeof(qla_fo_params.FailoverNotifyCdb)) {
			DEBUG2_9_10(printk("%s: got invalid cdb length.\n",
			    __func__);)
			return EXT_STATUS_INVALID_PARAM;
		}

		memcpy(qla_fo_params.FailoverNotifyCdb,
		    pp->FailoverNotifyCdb,
		    sizeof(qla_fo_params.FailoverNotifyCdb));
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}


/*
 * qla2x00_fo_init_params
 *	Gets driver configuration file failover properties to initalize
 *	the global failover parameters structure.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_fo_init_params(scsi_qla_host_t *ha)
{
	DEBUG3(printk("%s: entered.\n", __func__);)

	/* For parameters that are not completely implemented yet, */

	memset(&qla_fo_params, 0, sizeof(qla_fo_params));

	if(MaxPathsPerDevice) {
		qla_fo_params.MaxPathsPerDevice = MaxPathsPerDevice;
	} else
		qla_fo_params.MaxPathsPerDevice =FO_MAX_PATHS_PER_DEVICE_DEF ;
	if(MaxRetriesPerPath) {
		qla_fo_params.MaxRetriesPerPath = MaxRetriesPerPath;
	} else
		qla_fo_params.MaxRetriesPerPath =FO_MAX_RETRIES_PER_PATH_DEF;
	if(MaxRetriesPerIo) {
		qla_fo_params.MaxRetriesPerIo =MaxRetriesPerIo;
	} else
		qla_fo_params.MaxRetriesPerIo =FO_MAX_RETRIES_PER_IO_DEF;

	qla_fo_params.Flags =  0;
	qla_fo_params.FailoverNotifyType = FO_NOTIFY_TYPE_NONE;
	
	/* Set it to whatever user specified on the cmdline */
	if (qlFailoverNotifyType != FO_NOTIFY_TYPE_NONE)
		qla_fo_params.FailoverNotifyType = qlFailoverNotifyType;
	

	DEBUG3(printk("%s: exiting.\n", __func__);)
}

static int
qla2x00_spinup(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun) 
{
	inq_cmd_rsp_t	*pkt;
	int		rval, count, retry;
	dma_addr_t	phys_address = 0;
	uint16_t	comp_status;
	uint16_t	scsi_status;

	ENTER(__func__);

	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - INQ\n",
		    ha->host_no);
	}

	count = 100; 
	retry = 10;
	do {
		/* issue spinup */
		memset(pkt, 0, sizeof(inq_cmd_rsp_t));
		pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
		pkt->p.cmd.entry_count = 1;
		pkt->p.cmd.lun = cpu_to_le16(lun);
		SET_TARGET_ID(ha, pkt->p.cmd.target, fcport->loop_id);
		/* no direction for this command */
		pkt->p.cmd.control_flags =
		    __constant_cpu_to_le16(CF_SIMPLE_TAG);
		pkt->p.cmd.scsi_cdb[0] = START_STOP;
		pkt->p.cmd.scsi_cdb[4] = 1;	/* start spin cycle */
		pkt->p.cmd.dseg_count = __constant_cpu_to_le16(0);
		pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
		pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);

		rval = qla2x00_issue_iocb(ha, pkt,
		    phys_address, sizeof(inq_cmd_rsp_t));

		comp_status = le16_to_cpu(pkt->p.rsp.comp_status);
		scsi_status = le16_to_cpu(pkt->p.rsp.scsi_status);

		if ( (scsi_status & SS_CHECK_CONDITION) ) {
			DEBUG2(printk("%s(%ld): SS_CHECK_CONDITION "
			    "Sense Data "
			    "%02x %02x %02x %02x "
			    "%02x %02x %02x %02x\n",
			    __func__,
			    ha->host_no,
			    pkt->p.rsp.req_sense_data[0],
			    pkt->p.rsp.req_sense_data[1],
			    pkt->p.rsp.req_sense_data[2],
			    pkt->p.rsp.req_sense_data[3],
			    pkt->p.rsp.req_sense_data[4],
			    pkt->p.rsp.req_sense_data[5],
			    pkt->p.rsp.req_sense_data[6],
			    pkt->p.rsp.req_sense_data[7]);)
				if (pkt->p.rsp.req_sense_data[2] ==
				    NOT_READY  &&
				    (pkt->p.rsp.req_sense_data[12] == 4 ) &&
				    (pkt->p.rsp.req_sense_data[13] == 3 ) ) {

					current->state = TASK_UNINTERRUPTIBLE;
					schedule_timeout(HZ);
					printk(".");
					count--;
				} else
					retry--;
		}

		printk("qla_fo: Sending Start - count %d, retry=%d"
		    "comp status 0x%x, "
		    "scsi status 0x%x, rval=%d\n",
		    count,
		    retry,
		    comp_status,
		    scsi_status, 
		    rval);

		if (rval != QLA_SUCCESS || comp_status != CS_COMPLETE)
			retry--;

	} while ( count && retry  &&
	    (rval != QLA_SUCCESS || comp_status != CS_COMPLETE ||
		(scsi_status & SS_CHECK_CONDITION)));

	if (rval != QLA_SUCCESS ||
	    comp_status != CS_COMPLETE ||
	    (scsi_status & SS_CHECK_CONDITION)) {

		DEBUG(printk("qla_fo: Failed spinup - "
		    "comp status 0x%x, "
		    "scsi status 0x%x. loop_id=%d\n",
		    comp_status,
		    scsi_status, 
		    fcport->loop_id);)
	}

	pci_free_consistent(ha->pdev, sizeof(rpt_lun_cmd_rsp_t),
	    pkt, phys_address);


	LEAVE(__func__);

	return( rval );

}


/*
 * qla2x00_send_fo_notification
 *      Sends failover notification if needed.  Change the fc_lun pointer
 *      in the old path lun queue.
 *
 * Input:
 *      old_lp = Pointer to old fc_lun.
 *      new_lp = Pointer to new fc_lun.
 *
 * Returns:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
uint32_t
qla2x00_send_fo_notification(fc_lun_t *old_lp, fc_lun_t *new_lp)
{
	scsi_qla_host_t	*old_ha = old_lp->fcport->ha;
	int		rval = QLA_SUCCESS;
	inq_cmd_rsp_t	*pkt;
	uint16_t	loop_id, lun;
	dma_addr_t	phys_address;


	ENTER("qla2x00_send_fo_notification");
	DEBUG3(printk("%s: entered.\n", __func__);)

	loop_id = new_lp->fcport->loop_id;
	lun = new_lp->lun;

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LUN_RESET) {
		rval = qla2x00_lun_reset(old_ha, loop_id, lun);
		if (rval == QLA_SUCCESS) {
			DEBUG4(printk("qla2x00_send_fo_notification: LUN "
			    "reset succeded\n");)
		} else {
			DEBUG4(printk("qla2x00_send_fo_notification: LUN "
			    "reset failed\n");)
		}

	}
	if ( (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET) ||
	    (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_CDB) )  {

		rval = qla2x00_fabric_logout(old_ha, loop_id);
		if (rval == QLA_SUCCESS) {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "logout succeded\n");)
		} else {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "logout failed\n");)
		}

	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_SPINUP) {
		qla2x00_spinup(new_lp->fcport->ha, new_lp->fcport, new_lp->lun);
	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB) {
		pkt = pci_alloc_consistent(old_ha->pdev,
		    sizeof(inq_cmd_rsp_t), &phys_address);
		if (pkt == NULL) {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "memory allocation failed\n");)

			return(QLA_FUNCTION_FAILED);
		}

		memset(pkt,0, sizeof(inq_cmd_rsp_t));
		pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
		pkt->p.cmd.entry_count = 1;
		pkt->p.cmd.lun = cpu_to_le16(lun);
		SET_TARGET_ID(old_ha, pkt->p.cmd.target, loop_id);

		/* FIXME: How do you know the direction ???? */
		/* This has same issues as passthur commands - you 
		 * need more than just the CDB.
		 */
		pkt->p.cmd.control_flags =
		    __constant_cpu_to_le16(CF_SIMPLE_TAG);
		memcpy(pkt->p.cmd.scsi_cdb,
		    qla_fo_params.FailoverNotifyCdb,
		    qla_fo_params.FailoverNotifyCdbLength);
		pkt->p.cmd.dseg_count = __constant_cpu_to_le16(1);
		pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);
		pkt->p.cmd.dseg_0_address[0] = cpu_to_le32(
		    LSD(phys_address + sizeof (sts_entry_t)));
		pkt->p.cmd.dseg_0_address[1] = cpu_to_le32(
		    MSD(phys_address + sizeof (sts_entry_t)));
		pkt->p.cmd.dseg_0_length = __constant_cpu_to_le32(0);

		rval = qla2x00_issue_iocb(old_ha,
		    pkt, phys_address, sizeof (inq_cmd_rsp_t));

		if (rval != QLA_SUCCESS ||
		    pkt->p.rsp.comp_status != CS_COMPLETE ||
		    pkt->p.rsp.scsi_status & SS_CHECK_CONDITION ||
		    pkt->inq[0] == 0x7f) {

			DEBUG4(printk("qla2x00_fo_notification: send CDB "
			    "failed: comp_status = %x"
			    "scsi_status = %x inq[0] = %x\n",
			    pkt->p.rsp.comp_status,
			    pkt->p.rsp.scsi_status,
			    pkt->inq[0]);)
		}

		pci_free_consistent(old_ha->pdev,
		    sizeof(inq_cmd_rsp_t), pkt, phys_address);
	}

	DEBUG3(printk("%s: exiting. rval = %d.\n", __func__, rval);)

	return rval;
}


/*
 * qla2100_fo_enabled
 *      Reads and validates the failover enabled property.
 *
 * Input:
 *      ha = adapter state pointer.
 *      instance = HBA number.
 *
 * Returns:
 *      TRUE when failover is authorized else FALSE
 *
 * Context:
 *      Kernel context.
 */
uint8_t
qla2x00_fo_enabled(scsi_qla_host_t *ha, int instance)
{
	return qla2x00_failover_enabled(ha);
}

/*
 * qla2x00_fo_missing_port_summary
 *	Returns values of devices not connected but found in configuration
 *	file in user's dd_entry list.
 *
 * Input:
 *	ha = adapter state pointer.
 *	pdd_entry = pointer to a temporary EXT_DEVICEDATAENTRY struct
 *	pstart_of_entry_list = start of user addr of buffer for dd_entry entries
 *	max_entries = max number of entries allowed by user buffer
 *	pentry_cnt = pointer to total number of entries so far
 *	ret_status = pointer to ioctl status field
 *
 * Returns:
 *	0 = success
 *	others = errno value
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_fo_missing_port_summary(scsi_qla_host_t *ha,
    EXT_DEVICEDATAENTRY *pdd_entry, void *pstart_of_entry_list,
    uint32_t max_entries, uint32_t *pentry_cnt, uint32_t *ret_status)
{
	int		ret = 0;
	uint8_t 	path_id;
	uint8_t		*usr_temp, *kernel_tmp;
	uint16_t	dev_no;
	uint32_t	b;
	uint32_t	current_offset;
	uint32_t	transfer_size;
	mp_device_t	*dp;
	mp_host_t	*host;
	mp_path_list_t	*pathlist;
	mp_path_t	*path;
	portname_list 	*portname_used = NULL;

	DEBUG9(printk("%s(%ld): inst=%ld entered.\n",
	    __func__, ha->host_no, ha->instance);)

	if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
		DEBUG2_9_10(printk("%s(%ld): no HOST for ha inst %ld.\n",
		    __func__, ha->host_no, ha->instance);)
		*ret_status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	/* Assumption: each port name cannot appear in more than one mpdev
	 * structure.
	 */
	for (dev_no = 0; dev_no < MAX_MP_DEVICES && *pentry_cnt < max_entries;
	    dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		/* Sanity check */
		if (qla2x00_is_wwn_zero(dp->nodename))
			continue;

		if ((pathlist = dp->path_list) == NULL)
			continue;

		path = pathlist->last;
		for (path_id = 0; path_id < pathlist->path_cnt &&
		    *pentry_cnt < max_entries; path_id++, path = path->next) {

			/* Sanity check */
			if (qla2x00_is_wwn_zero(path->portname))
				continue;

			if (path->config == TRUE && path->port == NULL) {
				/* This path was created from config file
				 * but has not been configured.
				 */
				if (path->host != host) {
					/* path on other host. don't report */
					DEBUG10(printk("%s(%ld): path host %p "
					    "not for current host %p.\n",
					    __func__, ha->host_no, path->host,
					    host);)

					continue;
				}

				/* Check whether we've copied info on this
				 * port name before.  If this is a new port
				 * name, save the port name so we won't copy
				 * it again if it's also found on other hosts.
				 */
				if (qla2x00_port_name_in_list(path->portname,
				    portname_used)) {
					DEBUG10(printk("%s(%ld): found previously "
					    "reported portname=%02x%02x%02x"
					    "%02x%02x%02x%02x%02x.\n",
					    __func__, ha->host_no,
					    path->portname[0],
					    path->portname[1],
					    path->portname[2],
					    path->portname[3],
					    path->portname[4],
					    path->portname[5],
					    path->portname[6],
					    path->portname[7]);)
					continue;
				}

				if ((ret = qla2x00_add_to_portname_list(
				    path->portname, &portname_used))) {
					/* mem alloc error? */
					*ret_status = EXT_STATUS_NO_MEMORY;
					break;
				}

				DEBUG10(printk("%s(%ld): returning missing device "
				    "%02x%02x%02x%02x%02x%02x%02x%02x.\n",
				    __func__, ha->host_no,
				    path->portname[0], path->portname[1],
				    path->portname[2], path->portname[3],
				    path->portname[4], path->portname[5],
				    path->portname[6], path->portname[7]);)

				/* This device was not found. Return
				 * as unconfigured.
				 */
				memcpy(pdd_entry->NodeWWN, dp->nodename,
				    WWN_SIZE);
				memcpy(pdd_entry->PortWWN, path->portname,
				    WWN_SIZE);

				for (b = 0; b < 3 ; b++)
					pdd_entry->PortID[b] = 0;

				/* assume fabric dev so api won't translate the portid from loopid */
				pdd_entry->ControlFlags = EXT_DEF_GET_FABRIC_DEVICE;

				pdd_entry->TargetAddress.Bus    = 0;
				pdd_entry->TargetAddress.Target = dp->dev_id;
				pdd_entry->TargetAddress.Lun    = 0;
				pdd_entry->DeviceFlags          = 0;
				pdd_entry->LoopID               = 0;
				pdd_entry->BaseLunNumber        = 0;

				current_offset = *pentry_cnt *
				    sizeof(EXT_DEVICEDATAENTRY);

				transfer_size = sizeof(EXT_DEVICEDATAENTRY);
				ret = verify_area(VERIFY_WRITE,
				    (void *)(pstart_of_entry_list +
				    current_offset), transfer_size);

				if (ret) {
					*ret_status = EXT_STATUS_COPY_ERR;
					DEBUG10(printk("%s(%ld): inst=%ld "
					    "ERROR verify wrt rsp bufaddr=%p\n",
					    __func__, ha->host_no, ha->instance,
					    (void *)(pstart_of_entry_list +
					    current_offset));)
					break;
				}

				/* now copy up this dd_entry to user */
				usr_temp = (uint8_t *)pstart_of_entry_list +
				    current_offset;
				kernel_tmp = (uint8_t *)pdd_entry;
			 	ret = copy_to_user(usr_temp, kernel_tmp,
				    transfer_size);
				if (ret) {
					*ret_status = EXT_STATUS_COPY_ERR;
					DEBUG9_10(printk("%s(%ld): inst=%ld "
					    "ERROR copy rsp list buffer.\n",
					    __func__, ha->host_no,
					    ha->instance);)
					break;
				}
				*pentry_cnt+=1;
			}

		}

		if (ret || *ret_status) {
			break;
		}
	}

	DEBUG9(printk("%s(%ld): ending entry cnt=%d.\n",
	    __func__, ha->host_no, *pentry_cnt);)

	qla2x00_free_portname_list(&portname_used);

	DEBUG9(printk("%s(%ld): inst=%ld exiting. ret=%d.\n",
	    __func__, ha->host_no, ha->instance, ret);)

	return (ret);
}

/*
 * qla2x00_port_name_in_list
 *	Returns whether we found the specified port name in the list given.
 *
 * Input:
 *	wwpn = pointer to ww port name.
 *	list = pointer to a portname_list list.
 *
 * Returns:
 *	1 = found portname in list
 *	0 = portname not in list
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_port_name_in_list(uint8_t *wwpn, portname_list *list)
{
	int	found_name = 0;
	portname_list	*ptmp;

	for (ptmp = list; ptmp; ptmp = ptmp->pnext) {
		if (qla2x00_is_nodename_equal(ptmp->portname, wwpn)) {
		    found_name = 1;
		    break;
		}
	}

	return (found_name);
}

/*
 * qla2x00_add_to_portname_list
 *	Allocates a portname_list member and adds it to the list given
 *	with the specified port name.
 *
 * Input:
 *	wwpn = pointer to ww port name.
 *	plist = pointer to a pointer of portname_list list.
 *
 * Returns:
 *	0 = success
 *	others = errno indicating error
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_add_to_portname_list(uint8_t *wwpn, portname_list **plist)
{
	int		ret = 0;
	portname_list	*ptmp;
	portname_list	*plast;

	if ((ptmp = (portname_list *)KMEM_ZALLOC(sizeof(portname_list), 50))) {

		memcpy(ptmp->portname, wwpn, EXT_DEF_WWN_NAME_SIZE);

		if (*plist) {
			/* Add to tail of list */
			for (plast = *plist; plast->pnext; plast=plast->pnext) {
				/* empty */
			}
			plast->pnext = ptmp;
		} else {
			*plist = ptmp;
		}

	} else {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		ret = -ENOMEM;
	}

	return (ret);
}

/*
 * qla2x00_free_portname_list
 *	Free the list given.
 *
 * Input:
 *	plist = pointer to a pointer of portname_list list to free.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_free_portname_list(portname_list **plist)
{
	portname_list	*ptmp;
	portname_list	*ptmpnext;

	for (ptmp = *plist; ptmp; ptmp = ptmpnext) {
		ptmpnext = ptmp->pnext;
		KMEM_FREE(ptmp, sizeof(portname_list));
	}
	*plist = NULL;
}
