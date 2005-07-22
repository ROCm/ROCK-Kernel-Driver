/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
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
 ******************************************************************************/

/*
 * QLogic ISP4xxx Multi-path LUN Support Driver
 *
 */

#include "ql4_def.h"
#include "ql4_cfg.h"

#include "qlfo.h"
#include "qlfolimits.h"
/*
#include "ql4_foln.h"
*/
#include "ql4_glbl.h"

/*
 *  Local Function Prototypes.
 */

static uint32_t qla4xxx_add_iscsiname_to_mp_dev(mp_device_t *, uint8_t *, uint8_t *);

static mp_device_t * qla4xxx_allocate_mp_dev(uint8_t *, uint8_t *);
static mp_path_t * qla4xxx_allocate_path(mp_host_t *, uint16_t, fc_port_t *,
    uint16_t);
static mp_path_list_t * qla4xxx_allocate_path_list(void);

static mp_host_t * qla4xxx_find_host_by_iscsiname(uint8_t *);

static mp_path_t * qla4xxx_find_or_allocate_path(mp_host_t *, mp_device_t *,
    uint16_t, uint16_t, fc_port_t *);

static uint32_t qla4xxx_cfg_register_failover_lun(mp_device_t *,srb_t *,
    fc_lun_t *);
static uint32_t qla4xxx_send_failover_notify(mp_device_t *, uint8_t,
    mp_path_t *, mp_path_t *);
static mp_path_t * qla4xxx_select_next_path(mp_host_t *, mp_device_t *,
    uint8_t, srb_t *);

static uint8_t qla4xxx_update_mp_host(mp_host_t  *);
static uint32_t qla4xxx_update_mp_tree (void);

static fc_lun_t *qla4xxx_find_matching_lun(uint8_t , mp_device_t *, mp_path_t *);
static mp_path_t *qla4xxx_find_path_by_id(mp_device_t *, uint8_t);
static mp_device_t *qla4xxx_find_mp_dev_by_iscsiname(mp_host_t *, uint8_t *,
    uint16_t *);

static mp_path_t *qla4xxx_get_visible_path(mp_device_t *dp);
static void qla4xxx_map_os_targets(mp_host_t *);
static void qla4xxx_map_os_luns(mp_host_t *, mp_device_t *, uint16_t);
static uint8_t qla4xxx_map_a_oslun(mp_host_t *, mp_device_t *, uint16_t, uint16_t);

static uint8_t qla4xxx_is_name_zero(uint8_t *);
static void qla4xxx_add_path(mp_path_list_t *, mp_path_t *);
static void qla4xxx_failback_single_lun(mp_device_t *, uint8_t, uint8_t);
static void qla4xxx_failback_luns(mp_host_t *);
static void qla4xxx_setup_new_path(mp_device_t *, mp_path_t *, fc_port_t *);
int  qla4xxx_get_wwuln_from_device(mp_host_t *, fc_lun_t *, char	*, int);
static mp_lun_t  * qla4xxx_find_matching_lunid(char	*);
static fc_lun_t  * qla4xxx_find_matching_lun_by_num(uint16_t , mp_device_t *,
	mp_path_t *);
static int qla4xxx_configure_cfg_device(fc_port_t	*);
static mp_lun_t *
qla4xxx_find_or_allocate_lun(mp_host_t *, uint16_t ,
    fc_port_t *, fc_lun_t *);
static void qla4xxx_add_lun( mp_device_t *, mp_lun_t *);
static mp_port_t	*
qla4xxx_find_or_allocate_port(mp_host_t *, mp_lun_t *, 
	mp_path_t *);
static mp_port_t	*
qla4xxx_find_port_by_name(mp_lun_t *, mp_path_t *);
static struct _mp_path *
qla4xxx_find_first_active_path(mp_device_t *, mp_lun_t *);
#if 0
static int
qla4xxx_is_pathid_in_port(mp_port_t *, uint8_t );
#endif

static mp_device_t  *
qla4xxx_find_mp_dev_by_id(mp_host_t *host, uint16_t id );

#define qla4xxx_is_name_equal(N1,N2) \
	((memcmp((N1),(N2),ISCSI_NAME_SIZE)==0?1:0))
/*
 * Global data items
 */
mp_host_t  *mp_hosts_base = NULL;
DECLARE_MUTEX(mp_hosts_lock);
int   mp_config_required = 0;
static int mp_num_hosts;
static int mp_initialized;

/*
 * ENTRY ROUTINES
 */

 /*
 *  Borrowed from scsi_scan.c 
 */
int16_t
qla4xxx_cfg_lookup_device(unsigned char *response_data)
{
	int i = 0;
	unsigned char *pnt;
	DEBUG3(printk(KERN_INFO "Entering %s\n", __func__);)
	for (i = 0; 1; i++) {
		if (cfg_device_list[i].vendor == NULL)
			return -1;
		pnt = &response_data[8];
		while (*pnt && *pnt == ' ')
			pnt++;
		if (memcmp(cfg_device_list[i].vendor, pnt,
		    strlen(cfg_device_list[i].vendor)))
			continue;
		pnt = &response_data[16];
		while (*pnt && *pnt == ' ')
			pnt++;
		if (memcmp(cfg_device_list[i].model, pnt,
		    strlen(cfg_device_list[i].model)))
			continue;
		return i;
	}
	return -1;
}


void
qla4xxx_set_device_flags(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	if (fcport->cfg_id == -1)
		return;

	fcport->flags &= ~(FCF_XP_DEVICE|FCF_MSA_DEVICE|FCF_EVA_DEVICE);
	if ((cfg_device_list[fcport->cfg_id].flags & 1)) {
		printk(KERN_INFO
		    "scsi(%d) :Loop id 0x%04x is an XP device\n", ha->host_no,
		    fcport->loop_id);
		fcport->flags |= FCF_XP_DEVICE;
	} else if ((cfg_device_list[fcport->cfg_id].flags & 2)) {
		printk(KERN_INFO
		    "scsi(%d) :Loop id 0x%04x is a MSA1000 device\n",
		    ha->host_no, fcport->loop_id);
		fcport->flags |= FCF_MSA_DEVICE;
		fcport->flags |= FCF_FAILBACK_DISABLE;
	} else if ((cfg_device_list[fcport->cfg_id].flags & 4)) {
		printk(KERN_INFO
		    "scsi(%d) :Loop id 0x%04x is a EVA device\n", ha->host_no,
		    fcport->loop_id);
		fcport->flags |= FCF_EVA_DEVICE;
		fcport->flags |= FCF_FAILBACK_DISABLE;
	} 
	if ((cfg_device_list[fcport->cfg_id].flags & 8)) {
		printk(KERN_INFO
		    "scsi(%d) :Loop id 0x%04x has FAILOVERS disabled.\n",
		    ha->host_no, fcport->loop_id);
		fcport->flags |= FCF_FAILOVER_DISABLE;
	}
}


static int
qla4xxx_configure_cfg_device(fc_port_t *fcport)
{
	int id = fcport->cfg_id;

	DEBUG3(printk("Entering %s - id= %d\n", __func__, fcport->cfg_id));

	if (fcport->cfg_id == -1)
		return 0;

	/* Set any notify options */
	if (cfg_device_list[id].notify_type != FO_NOTIFY_TYPE_NONE) {
		fcport->notify_type = cfg_device_list[id].notify_type;
	}   

	DEBUG2(printk("%s - Configuring device \n", __func__)); 

	/* Disable failover capability if needed  and return */
	fcport->fo_combine = cfg_device_list[id].fo_combine;
	DEBUG2(printk("Exiting %s - id= %d\n", __func__, fcport->cfg_id));

	return 1;
}

/*
 * qla4xxx_cfg_init
 *      Initialize configuration structures to handle an instance of
 *      an HBA, QLA4xxx0 card.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Returns:
 *      qla4xxx local function return status code.
 *
 * Context:
 *      Kernel context.
 */
int
qla4xxx_cfg_init(scsi_qla_host_t *ha)
{
	int	rval;

	ENTER("qla4xxx_cfg_init");
	set_bit(CFG_ACTIVE, &ha->cfg_flags);
	mp_initialized = 1; 
	/* First HBA, initialize the failover global properties */
	qla4xxx_fo_init_params(ha);

        down(&mp_hosts_lock);
	/*
	 * If the user specified a device configuration then it is use as the
	 * configuration. Otherwise, we wait for path discovery.
	 */
	if (mp_config_required)
		qla4xxx_cfg_build_path_tree(ha);
	rval = qla4xxx_cfg_path_discovery(ha);
        up(&mp_hosts_lock);
	clear_bit(CFG_ACTIVE, &ha->cfg_flags);

	LEAVE("qla4xxx_cfg_init");
	return rval;
}

/*
 * qla4xxx_cfg_path_discovery
 *      Discover the path configuration from the device configuration
 *      for the specified host adapter and build the path search tree.
 *      This function is called after the lower level driver has
 *      completed its port and lun discovery.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Returns:
 *      qla4xxx local function return status code.
 *
 * Context:
 *      Kernel context.
 */
int
qla4xxx_cfg_path_discovery(scsi_qla_host_t *ha)
{
	int		rval = QLA_SUCCESS;
	mp_host_t	*host;
	uint8_t		*name;

	ENTER("qla4xxx_cfg_path_discovery");

	name = 	&ha->name_string[0];

	set_bit(CFG_ACTIVE, &ha->cfg_flags);
	/* Initialize the path tree for this adapter */
	host = qla4xxx_find_host_by_iscsiname(name);
	if (mp_config_required) {
		if (host == NULL ) {
			DEBUG4(printk("cfg_path_discovery: host not found, "
				"port name = "
				"%02x%02x%02x%02x%02x%02x%02x%02x\n",
				name[0], name[1], name[2], name[3],
				name[4], name[5], name[6], name[7]);)
			rval = QLA_ERROR;
		} else if (ha->instance != host->instance) {
			DEBUG4(printk("cfg_path_discovery: host instance "
				"don't match - instance=%ld.\n",
				ha->instance);)
			rval = QLA_ERROR;
		}
	} else if (host == NULL) {
		/* New host adapter so allocate it */
		DEBUG3(printk("%s: found new ha inst %ld. alloc host.\n",
		    __func__, ha->instance);)
		if ( (host = qla4xxx_alloc_host(ha)) == NULL ) {
			printk(KERN_INFO
				"qla4xxx(%d): Couldn't allocate "
				"host - ha = %p.\n",
				(int)ha->instance, ha);
			rval = QLA_ERROR;
		}
	}

	/* Fill in information about host */
	if (host != NULL ) {
		host->flags |= MP_HOST_FLAG_NEEDS_UPDATE;
		host->flags |= MP_HOST_FLAG_LUN_FO_ENABLED;
		host->fcports = &ha->fcports;

		/* Check if multipath is enabled */
		DEBUG3(printk("%s: updating mp host for ha inst %ld.\n",
		    __func__, ha->instance);)
		if (!qla4xxx_update_mp_host(host)) {
			rval = QLA_ERROR;
		}
		host->flags &= ~MP_HOST_FLAG_LUN_FO_ENABLED;
	}

	if (rval != QLA_SUCCESS) {
		/* EMPTY */
		DEBUG4(printk("qla4xxx_path_discovery: Exiting FAILED\n");)
	} else {
		LEAVE("qla4xxx_cfg_path_discovery");
	}
	clear_bit(CFG_ACTIVE, &ha->cfg_flags);

	return rval;
}

/*
 * qla4xxx_cfg_event_notifiy
 *      Callback for host driver to notify us of configuration changes.
 *
 * Input:
 *      ha = adapter state pointer.
 *      i_type = event type
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
int
qla4xxx_cfg_event_notify(scsi_qla_host_t *ha, uint32_t i_type)
{
	mp_host_t	*host;			/* host adapter pointer */

	ENTER("qla4xxx_cfg_event_notify");

	set_bit(CFG_ACTIVE, &ha->cfg_flags);
	switch (i_type) {
		case MP_NOTIFY_RESET_DETECTED:
			DEBUG(printk("scsi%ld: MP_NOTIFY_RESET_DETECTED "
					"- no action\n",
					ha->host_no);)
				break;
		case MP_NOTIFY_PWR_LOSS:
			DEBUG(printk("scsi%ld: MP_NOTIFY_PWR_LOSS - "
					"update tree\n",
					ha->host_no);)
			/*
			 * Update our path tree in case we are
			 * losing the adapter
			 */
			down(&mp_hosts_lock);
			qla4xxx_update_mp_tree();
			up(&mp_hosts_lock);
			/* Free our resources for adapter */
			break;
		case MP_NOTIFY_LOOP_UP:
			DEBUG(printk("scsi%ld: MP_NOTIFY_LOOP_UP - "
					"update host tree\n",
					ha->host_no);)
			/* Adapter is back up with new configuration */
			if ((host = qla4xxx_cfg_find_host(ha)) != NULL) {
				host->flags |= MP_HOST_FLAG_NEEDS_UPDATE;
				host->fcports = &ha->fcports;
				set_bit(CFG_FAILOVER, &ha->cfg_flags);
				down(&mp_hosts_lock);
				qla4xxx_update_mp_tree();
				up(&mp_hosts_lock);
				clear_bit(CFG_FAILOVER, &ha->cfg_flags);
			}
			break;
		case MP_NOTIFY_LOOP_DOWN:
		case MP_NOTIFY_BUS_RESET:
			DEBUG(printk("scsi%ld: MP_NOTIFY_OTHERS - "
					"no action\n",
					ha->host_no);)
			break;
		default:
			break;

	}
	clear_bit(CFG_ACTIVE, &ha->cfg_flags);

	LEAVE("qla4xxx_cfg_event_notify");

	return QLA_SUCCESS;
}

int
qla4xxx_cfg_remap(scsi_qla_host_t *halist)
{
	scsi_qla_host_t *ha;

	mp_initialized = 1; 
	read_lock(&qla4xxx_hostlist_lock);
	list_for_each_entry(ha, &qla4xxx_hostlist, list) {
		DEBUG2(printk("Entering %s ...\n",__func__);)
		/* Find the host that was specified */
		set_bit(CFG_FAILOVER, &ha->cfg_flags);
		qla4xxx_cfg_path_discovery(ha);
		clear_bit(CFG_FAILOVER, &ha->cfg_flags);
	}
	read_unlock(&qla4xxx_hostlist_lock);
	mp_initialized = 0; 
	DEBUG2(printk("Exiting %s ...\n",__func__);)

	return QLA_SUCCESS;
}

/*
 *  qla4xxx_allocate_mp_port
 *      Allocate an fc_mp_port, clear the memory, and log a system
 *      error if the allocation fails. After fc_mp_port is allocated
 *
 */
static mp_port_t *
qla4xxx_allocate_mp_port(uint8_t *iscsiname)
{
	mp_port_t   *port;
	int	i;

	DEBUG3(printk("%s: entered.\n", __func__);)

	port = kmalloc(sizeof(mp_port_t), GFP_KERNEL);
	if (!port)
		return NULL;
	memset(port, 0, sizeof(*port));

	DEBUG(printk("%s: mp_port_t allocated at %p\n",
		    __func__, port);)

	/*
	 * Since this is the first port, it goes at
	 * index zero.
	 */
	if (iscsiname)
	{
		DEBUG3(printk("%s: copying port name =%s\n",
		    __func__, iscsiname);)
		memcpy(&port->iscsiname[0], iscsiname, ISCSI_NAME_SIZE);
	}
	for ( i = 0 ;i <  MAX_HOSTS; i++ ) {
		port->path_list[i] = PATH_INDEX_INVALID;
	}
	port->fo_cnt = 0;
		

	DEBUG3(printk("%s: exiting.\n", __func__);)

	return port;
}

static mp_port_t	*
qla4xxx_find_port_by_name(mp_lun_t *mplun, 
	mp_path_t *path)
{
	mp_port_t	*port = NULL;
	mp_port_t	*temp_port;
	struct list_head *list, *temp;

	list_for_each_safe(list, temp, &mplun->ports_list) {
		temp_port = list_entry(list, mp_port_t, list);
		if ( memcmp(temp_port->iscsiname, path->iscsiname, ISCSI_NAME_SIZE) == 0 ) {
			port = temp_port;
			break;
		}
	}
	return port;
}


static mp_port_t	*
qla4xxx_find_or_allocate_port(mp_host_t *host, mp_lun_t *mplun, 
	mp_path_t *path)
{
	mp_port_t	*port = NULL;
	struct list_head *list, *temp;
	unsigned long	instance = host->instance;

	if( instance == MAX_HOSTS - 1) {
		printk(KERN_INFO "%s: Fail no room\n", __func__);
		return NULL;
	}

	if ( mplun == NULL ) {
		return NULL;
	}

	list_for_each_safe(list, temp, &mplun->ports_list) {
		port = list_entry(list, mp_port_t, list);
		if ( memcmp(port->iscsiname, path->iscsiname, ISCSI_NAME_SIZE) == 0 ) {
			if ( port->path_list[instance] == PATH_INDEX_INVALID ) {
			   DEBUG(printk("scsi%ld %s: Found matching mp port %02x%02x%02x"
			    "%02x%02x%02x%02x%02x.\n",
			    instance, __func__, port->iscsiname[0], port->iscsiname[1],
			    port->iscsiname[2], port->iscsiname[3], 
			    port->iscsiname[4], port->iscsiname[5], 
			    port->iscsiname[6], port->iscsiname[7]);)
				port->path_list[instance] = path->id;
				port->hba_list[instance] = host->ha;
				port->cnt++;
				DEBUG(printk("%s: adding iscsiname - port[%d] = "
			    "%p at index = %d with path id %d\n",
			    __func__, (int)instance ,port, 
				(int)instance, path->id);)
			}
			return port;
		}
	}
	port = qla4xxx_allocate_mp_port(path->iscsiname);
	if( port ) {
		port->cnt++;
		DEBUG(printk("%s: allocate and adding iscsiname - port[%d] = "
			    "%p at index = %d with path id %d\n",
			    __func__, (int)instance, port, 
				(int)instance, path->id);)
		port->path_list[instance] = path->id;
		port->hba_list[instance] = host->ha;
		/* add port to list */
		list_add_tail(&port->list,&mplun->ports_list );
	}
	return port;
}


/*
 * qla4xxx_cfg_failover_port
 *      Failover all the luns on the specified target to 
 *		the new path.
 *
 * Inputs:
 *      ha = pointer to host adapter
 *      fp - pointer to new fc_lun (failover lun)
 *      tgt - pointer to target
 *
 * Returns:
 *      
 */
static fc_lun_t *
qla4xxx_cfg_failover_port( mp_host_t *host, mp_device_t *dp,
	mp_path_t *new_path, fc_port_t *old_fcport, srb_t *sp)
{
#if 0
	uint8_t		l;
	fc_port_t	*fcport;
	fc_lun_t	*fclun;
	fc_lun_t	*new_fclun = NULL;
	os_lun_t 	 *up;
	mp_path_t	*vis_path;
	mp_host_t 	*vis_host;

	fcport = new_path->port;
#if MSA1000_SUPPORTED
	if( !qla4xxx_test_active_port(fcport) )  {
		DEBUG2(printk("%s(%ld): %s - port not ACTIVE "
		"to failover: port = %p, loop id= 0x%x\n",
		__func__,
		host->ha->host_no, __func__, fcport, fcport->loop_id);)
		return new_fclun;
	}
#endif

	/* Log the failover to console */
	printk(KERN_INFO
		"qla4xxx%d: FAILOVER all LUNS on device %d to WWPN "
		"%02x%02x%02x%02x%02x%02x%02x%02x -> "
		"%02x%02x%02x%02x%02x%02x%02x%02x, reason=0x%x\n",
		(int) host->instance,
		(int) dp->dev_id,
		old_fcport->iscsi_name[0], old_fcport->iscsi_name[1],
		old_fcport->iscsi_name[2], old_fcport->iscsi_name[3],
		old_fcport->iscsi_name[4], old_fcport->iscsi_name[5],
		old_fcport->iscsi_name[6], old_fcport->iscsi_name[7],
		fcport->iscsi_name[0], fcport->iscsi_name[1],
		fcport->iscsi_name[2], fcport->iscsi_name[3],
		fcport->iscsi_name[4], fcport->iscsi_name[5],
		fcport->iscsi_name[6], fcport->iscsi_name[7], sp->err_id );
		 printk(KERN_INFO
		"qla4xxx: FROM HBA %d to HBA %d\n",
		(int)old_fcport->ha->instance,
		(int)fcport->ha->instance);

	/* we failover all the luns on this port */
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		l = fclun->lun;
		if( (fclun->flags & FLF_VISIBLE_LUN) ) {  
			continue;
		}
		dp->path_list->current_path[l] = new_path->id;
		if ((vis_path =
		    qla4xxx_get_visible_path(dp)) == NULL ) {
			printk(KERN_INFO
		    "qla4xxx(%d): No visible "
			    "path for target %d, "
			    "dp = %p\n",
			    (int)host->instance,
		    dp->dev_id, dp);
		    continue;
		}

		vis_host = vis_path->host;
		up = (os_lun_t *) GET_LU_Q(vis_host->ha, 
		    dp->dev_id, l);
		if (up == NULL ) {
		DEBUG2(printk("%s: instance %d: No lun queue"
		    "for target %d, lun %d.. \n",
			__func__,(int)vis_host->instance,dp->dev_id,l);)
			continue;
		}

		up->fclun = fclun;
		fclun->fcport->cur_path = new_path->id;

		DEBUG2(printk("%s: instance %d: Mapping target %d:0x%x,"
		    "lun %d to path id %d\n",
			__func__,(int)vis_host->instance,dp->dev_id,
			fclun->fcport->loop_id, l,
		    fclun->fcport->cur_path);)

			/* issue reset to data luns only */
			if( fclun->device_type == TYPE_DISK) {
				new_fclun = fclun;
				/* send a reset lun command as well */
			printk(KERN_INFO 
			    "scsi(%ld:0x%x:%d) sending reset lun \n",
					fcport->ha->host_no,
					fcport->loop_id, l);
				qla4xxx_reset_lun(fcport->ha,
		  			fcport->ddbptr,
		  			fclun);
			}
		}
	return new_fclun;
#else
	return 0;
#endif
}

/*
 * qla4xxx_cfg_failover
 *      A problem has been detected with the current path for this
 *      lun.  Select the next available path as the current path
 *      for this device.
 *
 * Inputs:
 *      ha = pointer to host adapter
 *      fp - pointer to failed fc_lun (failback lun)
 *      tgt - pointer to target
 *
 * Returns:
 *      pointer to new fc_lun_t, or NULL if failover fails.
 */
fc_lun_t *
qla4xxx_cfg_failover(scsi_qla_host_t *ha, fc_lun_t *fp,
    os_tgt_t *tgt, srb_t *sp)
{
	mp_host_t	*host;			/* host adapter pointer */
	mp_device_t	*dp;			/* virtual device pointer */
	mp_path_t	*new_path;		/* new path pointer */
	fc_lun_t	*new_fp = NULL;
	fc_port_t	*fcport, *new_fcport;

	ENTER("qla4xxx_cfg_failover");
	DEBUG2(printk("%s entered\n",__func__);)

	set_bit(CFG_ACTIVE, &ha->cfg_flags);
	if ((host = qla4xxx_cfg_find_host(ha)) != NULL) {
		if ((dp = qla4xxx_find_mp_dev_by_id(
		    host, tgt->id)) != NULL ) {

			DEBUG3(printk("qla4xxx_cfg_failover: dp = %p\n", dp);)
			/*
			 * Point at the next path in the path list if there is
			 * one, and if it hasn't already been failed over by
			 * another I/O. If there is only one path continuer
			 * to point at it.
			 */
			new_path = qla4xxx_select_next_path(host, dp, 
				fp->lun, sp);
			if( new_path == NULL )
				goto cfg_failover_done;
			new_fp = qla4xxx_find_matching_lun(fp->lun, 
					dp, new_path);
			if( new_fp == NULL )
				goto cfg_failover_done;
			DEBUG2(printk("cfg_failover: new path=%p, new pathid=%d"
					" new fp lun= %p\n",
				new_path, new_path->id, new_fp);)

			fcport = fp->fcport;
			if( (fcport->flags & FCF_MSA_DEVICE) ) {
				/* 
				 * "select next path" has already 
				 * send out the switch path notify 
				 * command, so inactive old path 
				 */
       				fcport->flags &= ~(FCF_MSA_PORT_ACTIVE);
				if( qla4xxx_cfg_failover_port( host, dp, 
						new_path, fcport, sp) == NULL ) {
					printk(KERN_INFO
						"scsi(%d): Fail to failover device "
						" - fcport = %p\n",
						host->ha->host_no, fcport);
					goto cfg_failover_done;
				}
			} else if( (fcport->flags & FCF_EVA_DEVICE) ) { 
				new_fcport = new_path->port;
				if ( qla4xxx_test_active_lun( 
					new_fcport, new_fp ) ) {
					qla4xxx_cfg_register_failover_lun(dp, 
						sp, new_fp);
				 	 /* send a reset lun command as well */
				 	 printk(KERN_INFO 
			    	 	 "scsi(%d:0x%x:%d) sending"
					 "reset lun \n",
					 new_fcport->ha->host_no,
					 new_fcport->loop_id, new_fp->lun);
					 qla4xxx_reset_lun(new_fcport->ha,
		  				 new_fcport->ddbptr,
		  				 new_fp);
				} else {
					DEBUG2(printk(
						"scsi(%d): %s Fail to failover lun "
						"old fclun= %p, new fclun= %p\n",
						host->ha->host_no,
						 __func__,fp, new_fp);)
					goto cfg_failover_done;
				}
			} else { /*default */
				new_fp = qla4xxx_find_matching_lun(fp->lun, dp,
				    new_path);
				qla4xxx_cfg_register_failover_lun(dp, sp,
				    new_fp);
			}

		} else {
			printk(KERN_INFO
				"qla4xxx(%d): Couldn't find device "
				"to failover: dp = %p\n",
				host->instance, dp);
		}
	}

cfg_failover_done:
	clear_bit(CFG_ACTIVE, &ha->cfg_flags);

	LEAVE("qla4xxx_cfg_failover");

	return new_fp;
}

/*
 * IOCTL support -- moved to ql4_foioctl.c
 */

/*
 * MP SUPPORT ROUTINES
 */

/*
 * qla4xxx_add_mp_host
 *	Add the specified host the host list.
 *
 * Input:
 *	node_name = pointer to node name
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
mp_host_t *
qla4xxx_add_mp_host(uint8_t *iscsi_name)
{
	mp_host_t   *host, *temp;

	host = kmalloc(sizeof(mp_host_t), GFP_KERNEL);
	if (!host)
		return NULL;
	memset(host, 0, sizeof(*host));
	memcpy(host->iscsiname, iscsi_name, ISCSI_NAME_SIZE);
	host->next = NULL;

	/* add to list */
	if (mp_hosts_base == NULL) {
		mp_hosts_base = host;
	} else {
		temp = mp_hosts_base;
		while (temp->next != NULL)
			temp = temp->next;
		temp->next = host;
	}

	mp_num_hosts++;
	return host;
}

/*
 * qla4xxx_alloc_host
 *      Allocate and initialize an mp host structure.
 *
 * Input:
 *      ha = pointer to base driver's adapter structure.
 *
 * Returns:
 *      Pointer to host structure or null on error.
 *
 * Context:
 *      Kernel context.
 */
mp_host_t   *
qla4xxx_alloc_host(scsi_qla_host_t *ha)
{
	mp_host_t	*host, *temp;
	uint8_t		*name;

	name = &ha->name_string[0];

	ENTER("qla4xxx_alloc_host");

	host = kmalloc(sizeof(mp_host_t), GFP_KERNEL);
	if (!host)
		return NULL;

	memset(host, 0, sizeof(*host));
	host->ha = ha;
	memcpy(host->iscsiname, name, ISCSI_NAME_SIZE);
	host->next = NULL;
	host->flags = MP_HOST_FLAG_NEEDS_UPDATE;
	host->instance = ha->instance;

	if (qla4xxx_fo_enabled(host->ha, host->instance)) {
		host->flags |= MP_HOST_FLAG_FO_ENABLED;
		DEBUG4(printk("%s: Failover enabled.\n",
		    __func__);)
	} else {
		/* EMPTY */
		DEBUG4(printk("%s: Failover disabled.\n",
		    __func__);)
	}
	/* add to list */
	if (mp_hosts_base == NULL) {
		mp_hosts_base = host;
	} else {
		temp = mp_hosts_base;
		while (temp->next != NULL)
			temp = temp->next;
		temp->next = host;
	}
	mp_num_hosts++;

	DEBUG4(printk("%s: Alloc host @ %p\n", __func__, host);)
	return host;
}

/*
 * qla4xxx_add_iscsiname_to_mp_dev
 *      Add the specific port name to the list of port names for a
 *      multi-path device.
 *
 * Input:
 *      dp = pointer ti virtual device
 *      iscsiname = Port name to add to device
 *      nodename = Node name to add to device
 *
 * Returns:
 *      qla4xxx local function return status code.
 *
 * Context:
 *      Kernel context.
 */
static uint32_t
qla4xxx_add_iscsiname_to_mp_dev(mp_device_t *dp, uint8_t *iscsiname, uint8_t *nodename)
{
	uint16_t	index;
	uint32_t	rval = QLA_SUCCESS;

	ENTER("qla4xxx_add_iscsiname_to_mp_dev");

	/* Look for an empty slot and add the specified iscsiname.   */
	for (index = 0; index < MAX_NUMBER_PATHS; index++) {
		if (qla4xxx_is_name_zero(&dp->iscsinames[index][0])) {
			DEBUG4(printk("%s: adding iscsiname to dp = "
			    "%p at index = %d\n",
			    __func__, dp, index);)
			memcpy(&dp->iscsinames[index][0], iscsiname, ISCSI_NAME_SIZE);
			break;
		}
	}
	if (index == MAX_NUMBER_PATHS) {
		rval = QLA_ERROR;
		DEBUG4(printk("%s: Fail no room\n", __func__);)
	} else {
		/* EMPTY */
		DEBUG4(printk("%s: Exit OK\n", __func__);)
	}

	LEAVE("qla4xxx_add_iscsiname_to_mp_dev");

	return rval;
}


/*
 *  qla4xxx_allocate_mp_dev
 *      Allocate an fc_mp_dev, clear the memory, and log a system
 *      error if the allocation fails. After fc_mp_dev is allocated
 *
 *  Inputs:
 *      nodename  = pointer to nodename of new device
 *      iscsiname  = pointer to iscsiname of new device
 *
 *  Returns:
 *      Pointer to new mp_device_t, or NULL if the allocation fails.
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t *
qla4xxx_allocate_mp_dev(uint8_t  *devname, uint8_t *iscsiname)
{
	mp_device_t   *dp;            /* Virtual device pointer */

	ENTER("qla4xxx_allocate_mp_dev");
	DEBUG3(printk("%s: entered.\n", __func__);)

	dp = kmalloc(sizeof(mp_device_t), GFP_KERNEL);
	if (!dp) {
		DEBUG4(printk("%s: Allocate failed.\n", __func__);)
		return NULL;
	}
	memset(dp, 0, sizeof(*dp));

	DEBUG3(printk("%s: mp_device_t allocated at %p\n", __func__, dp);)

	/*
	 * Copy node name into the mp_device_t.
	 */
	if (devname) {
		DEBUG2(printk("%s: copying dev name={%s} \n",
		    __func__, devname);)
		memcpy(dp->devname, devname, ISCSI_NAME_SIZE);
	}

	/*
	 * Since this is the first port, it goes at
	 * index zero.
	 */
	if (iscsiname)
	{
		DEBUG3(printk("%s: copying port name (%s) "
		    ".\n",
		    __func__, iscsiname); )
		memcpy(&dp->iscsinames[0][0], iscsiname, ISCSI_NAME_SIZE);
	}

	/* Allocate an PATH_LIST for the fc_mp_dev. */
	if ((dp->path_list = qla4xxx_allocate_path_list()) == NULL) {
		DEBUG4(printk("%s: allocate path_list Failed.\n",
		    __func__);)
		kfree(dp);
		dp = NULL;
	} else {
		DEBUG4(printk("%s: mp_path_list_t allocated at %p\n",
		    __func__, dp->path_list);)
		/* EMPTY */
		DEBUG4(printk("qla4xxx_allocate_mp_dev: Exit Okay\n");)
	}

	DEBUG3(printk("%s: exiting.\n", __func__);)
	LEAVE("qla4xxx_allocate_mp_dev");

	return dp;
}

/*
 *  qla4xxx_allocate_path
 *      Allocate a PATH.
 *
 *  Inputs:
 *     host   Host adapter for the device.
 *     path_id  path number
 *     port   port for device.
 *      dev_id  device number
 *
 *  Returns:
 *      Pointer to new PATH, or NULL if the allocation failed.
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla4xxx_allocate_path(mp_host_t *host, uint16_t path_id,
    fc_port_t *port, uint16_t dev_id)
{
	mp_path_t	*path;
	uint16_t	lun;

	ENTER("qla4xxx_allocate_path");

	path = kmalloc(sizeof(mp_path_t), GFP_KERNEL);
	if (!path) {
		DEBUG4(printk("%s: Failed\n", __func__);)
		return 0;
	}
	memset(path, 0, sizeof(*path));

	DEBUG3(printk("%s(%ld): allocated path %p at path id %d.\n",
	    __func__, host->ha->host_no, path, path_id);)

	/* Copy the supplied information into the MP_PATH.  */
	path->host = host;

	DEBUG3(printk("%s(%ld): assigned port pointer %p "
		    "to path id %d.\n",
	    __func__, host->ha->host_no, port, path_id);)
	path->port = port;

	path->id   = path_id;
	port->cur_path = path->id;
	path->mp_byte  = port->mp_byte;
	path->next  = NULL;
	memcpy(path->iscsiname, port->iscsi_name, ISCSI_NAME_SIZE);

	for (lun = 0; lun < MAX_LUNS; lun++) {
		path->lun_data.data[lun] |= LUN_DATA_ENABLED;
	}

	return path;
}


/*
 *  qla4xxx_allocate_path_list
 *      Allocate a PATH_LIST
 *
 *  Input:
 * 		None
 *
 *  Returns:
 *      Pointer to new PATH_LIST, or NULL if the allocation fails.
 *
 * Context:
 *      Kernel context.
 */
static mp_path_list_t *
qla4xxx_allocate_path_list( void )
{
	mp_path_list_t	*path_list;
	uint16_t		i;
	uint8_t			l;

	path_list = kmalloc(sizeof(mp_path_list_t), GFP_KERNEL);
	if (!path_list) {
		DEBUG4(printk("%s: Alloc pool failed for MP_PATH_LIST.\n",
		    __func__);)
		return NULL;
	}
	memset(path_list, 0, sizeof(*path_list));

	DEBUG4(printk("%s: allocated at %p\n", __func__, path_list);)

	path_list->visible = PATH_INDEX_INVALID;
	/* Initialized current path */
	for (i = 0; i < MAX_LUNS_PER_DEVICE; i++) {
		l = (uint8_t)(i & 0xFF);
		path_list->current_path[l] = PATH_INDEX_INVALID;
	}
	path_list->last = NULL;

	return path_list;
}

/*
 *  qla4xxx_cfg_find_host
 *      Look through the existing multipath tree, and find
 *      a host adapter to match the specified ha.
 *
 *  Input:
 *      ha = pointer to host adapter
 *
 *  Return:
 *      Pointer to new host, or NULL if no match found.
 *
 * Context:
 *      Kernel context.
 */
mp_host_t *
qla4xxx_cfg_find_host(scsi_qla_host_t *ha)
{
	mp_host_t     *host = NULL;	/* Host found and null if not */
	mp_host_t     *tmp_host;

	ENTER("qla4xxx_cfg_find_host");

	for (tmp_host = mp_hosts_base; (tmp_host); tmp_host = tmp_host->next) {
		if (tmp_host->ha == ha) {
			host = tmp_host;
			DEBUG3(printk("%s: Found host =%p, instance %d\n",
			    __func__, host, host->instance);)
			break;
		}
	}

	LEAVE("qla4xxx_cfg_find_host");

	return host;
}

/*
 *  qla4xxx_find_host_by_iscsiname
 *      Look through the existing multipath tree, and find
 *      a host adapter to match the specified iscsiname.
 *
 *  Input:
 *      name = iscsiname to match.
 *
 *  Return:
 *      Pointer to new host, or NULL if no match found.
 *
 * Context:
 *      Kernel context.
 */
static mp_host_t *
qla4xxx_find_host_by_iscsiname(uint8_t *name)
{
	mp_host_t     *host;		/* Host found and null if not */

	for (host = mp_hosts_base; (host); host = host->next) {
		if (memcmp(host->iscsiname, name, ISCSI_NAME_SIZE) == 0)
			break;
	}
	return host;
}


/*
 * qla4xxx_find_matching_lunid
 *      Find the lun in the lun list that matches the
 *  specified wwu lun number.
 *
 * Input:
 *      buf  = buffer that contains the wwuln
 *      host = host to search for lun
 *
 * Returns:
 *      NULL or pointer to lun
 *
 * Context:
 *      Kernel context.
 * (dg)
 */
static mp_lun_t  *
qla4xxx_find_matching_lunid(char	*buf)
{
	int		devid = 0;
	mp_host_t	*temp_host;  /* temporary pointer */
	mp_device_t	*temp_dp;  /* temporary pointer */
	mp_lun_t *lun;

	ENTER(__func__);

	for (temp_host = mp_hosts_base; (temp_host);
	    temp_host = temp_host->next) {
		for (devid = 0; devid < MAX_MP_DEVICES; devid++) {
			temp_dp = temp_host->mp_devs[devid];

			if (temp_dp == NULL)
				continue;

			for( lun = temp_dp->luns; lun != NULL ; 
					lun = lun->next ) {

				if (lun->siz > WWLUN_SIZE )
					lun->siz = WWLUN_SIZE;

				if (memcmp(lun->wwuln, buf, lun->siz) == 0)
					return lun;
			}
		}
	}
	return NULL;

}

/*
 *  qla4xxx_combine_by_lunid
 *      Look through the existing multipath control tree, and find
 *      an mp_lun_t with the supplied world-wide lun number.  If
 *      one cannot be found, allocate one.
 *
 *  Input:
 *      host      Adapter to add device to.
 *      dev_id    Index of device on adapter.
 *      port      port database information.
 *
 *  Returns:
 *      Pointer to new mp_device_t, or NULL if the allocation fails.
 *
 *  Side Effects:
 *      If the MP HOST does not already point to the mp_device_t,
 *      a pointer is added at the proper port offset.
 *
 * Context:
 *      Kernel context.
 */
int
qla4xxx_combine_by_lunid( void *vhost, uint16_t dev_id, 
	fc_port_t *fcport, uint16_t pathid) 
{
	mp_host_t	*host = (mp_host_t *) vhost; 
	int fail = 0;
	mp_path_t 	*path;
	mp_device_t *dp = NULL;
	fc_lun_t	*fclun;
	mp_lun_t  *lun;
	mp_port_t	*port;
	int		l;

	ENTER("qla4xxx_combine_by_lunid");
	//printk("Entering %s\n", __func__); 

	/* 
	 * Currently mp_config_required is not process by this routine
	 * because we create common nodename for the gui, so we can use 
	 * the normal common namename processing.
	 */
#if MSA1000_SUPPORTED
	if (mp_initialized && fcport->flags & FCF_MSA_DEVICE) {
		 qla4xxx_test_active_port(fcport); 
	}
#endif
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		lun = qla4xxx_find_or_allocate_lun(host, dev_id,
		    fcport, fclun);

		if (lun == NULL) {
			fail++;
			continue;
		}
		/*
 		* Find the path in the current path list, or allocate
 		* a new one and put it in the list if it doesn't exist.
 		*/
		dp = lun->dp;
		if (fclun->mplun == NULL )
			fclun->mplun = lun; 
		path = qla4xxx_find_or_allocate_path(host, dp,
		    dp->dev_id, pathid, fcport);
		if (path == NULL || dp == NULL) {
			fail++;
			continue;
		}

		/* set the lun active flag */
		if (mp_initialized && fcport->flags & FCF_EVA_DEVICE) { 
		     qla4xxx_test_active_lun( 
			path->port, fclun );
		}

		/* Add fclun to path list */
		if (lun->paths[path->id] == NULL) {
			lun->paths[path->id] = fclun;
			DEBUG2(printk("Updated path[%d]= %p for lun %p\n",
				path->id, fclun, lun);)
			lun->path_cnt++;
		}
			
		/* 
		 * if we have a visible lun then make
		 * the target visible as well 
		 */
		l = lun->number;
		if( (fclun->flags & FLF_VISIBLE_LUN)  ) {  
			if (dp->path_list->visible ==
			    PATH_INDEX_INVALID) {
				dp->path_list->visible = path->id;
				DEBUG2(printk("%s: dp %p setting "
				    "visible id to %d\n",
				    __func__,dp,path->id );)
			}  
			dp->path_list->current_path[l] = path->id;
			path->lun_data.data[l] |=
			    LUN_DATA_PREFERRED_PATH;

			DEBUG2(printk("%s: Found a controller path 0x%x "
			    "- lun %d\n", __func__, path->id,l);)
		} else if (mp_initialized) {
   			/*
			 * Whenever a port or lun is "active" then
			 * force it to be a preferred path.
    			 */
   			if (qla4xxx_find_first_active_path(dp, lun) 
				== path ){
   				dp->path_list->current_path[l] =
				    path->id;
				path->lun_data.data[l] |=
				    LUN_DATA_PREFERRED_PATH;
				DEBUG2(printk(
				"%s: Found preferred lun at loopid=0x%02x, lun=%d, pathid=%d\n",
	    			__func__, fcport->loop_id, l, path->id);)
			}
		}

		/* if (port->flags & FCF_CONFIG)
		path->config = 1;  */

		port = qla4xxx_find_or_allocate_port(host, lun, path);
		if (port == NULL) {
			fail++;
			continue;
		}
	}

	if (fail)
		return 0;		
	return 1;		
}
	
/*
 *  qla4xxx_find_or_allocate_path
 *      Look through the path list for the supplied device, and either
 *      find the supplied adapter (path) for the adapter, or create
 *      a new one and add it to the path list.
 *
 *  Input:
 *      host      Adapter (path) for the device.
 *      dp       Device and path list for the device.
 *      dev_id    Index of device on adapter.
 *      port     Device data from port database.
 *
 *  Returns:
 *      Pointer to new PATH, or NULL if the allocation fails.
 *
 *  Side Effects:
 *      1. If the PATH_LIST does not already point to the PATH,
 *         a new PATH is added to the PATH_LIST.
 *      2. If the new path is found to be a second visible path, it is
 *         marked as hidden, and the device database is updated to be
 *         hidden as well, to keep the miniport synchronized.
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static mp_path_t *
qla4xxx_find_or_allocate_path(mp_host_t *host, mp_device_t *dp,
    uint16_t dev_id, uint16_t pathid, fc_port_t *port)
{
	mp_path_list_t	*path_list = dp->path_list;
	mp_path_t		*path;
	uint8_t			id;


	ENTER("qla4xxx_find_or_allocate_path");

	DEBUG4(printk("%s: host =%p, port =%p, dp=%p, dev id = %d\n",
	    __func__, host, port, dp, dev_id);)
	/*
	 * Loop through each known path in the path list.  Look for
	 * a PATH that matches both the adapter and the port name.
	 */
	path = qla4xxx_find_path_by_name(host, path_list, port->iscsi_name);


	if (path != NULL ) {
		DEBUG3(printk("%s: Found an existing "
		    "path %p-  host %p inst=%d, port =%p, path id = %d\n",
		    __func__, path, host, host->instance, path->port,
		    path->id);)
		DEBUG3(printk("%s: Luns for path_id %d, instance %d\n",
		    __func__, path->id, host->instance);)
		DEBUG3(qla4xxx_dump_buffer(
		    (char *)&path->lun_data.data[0], 64);)

		/* If we found an existing path, look for any changes to it. */
		if (path->port == NULL) {
			DEBUG3(printk("%s: update path %p w/ port %p, path id="
			    "%d, path mp_byte=0x%x port mp_byte=0x%x.\n",
			    __func__, path, port, path->id,
			    path->mp_byte, port->mp_byte);)
			path->port = port;
			port->mp_byte = path->mp_byte;
		} else {
			DEBUG3(printk("%s: update path %p port %p path id %d, "
			    "path mp_byte=0x%x port mp_byte=0x%x.\n",
			    __func__, path, path->port, path->id,
			    path->mp_byte, port->mp_byte);)

			if ((path->mp_byte & MP_MASK_HIDDEN) &&
			    !(port->mp_byte & MP_MASK_HIDDEN)) {

				DEBUG3(printk("%s: Adapter(%p) "
				    "Device (%p) Path (%d) "
				    "has become visible.\n",
				    __func__, host, dp, path->id);)

				path->mp_byte &= ~MP_MASK_HIDDEN;
			}

			if (!(path->mp_byte & MP_MASK_HIDDEN) &&
			    (port->mp_byte & MP_MASK_HIDDEN)) {

				DEBUG3(printk("%s(%ld): Adapter(%p) "
				    "Device (%p) Path (%d) "
				    "has become hidden.\n",
				    __func__, host->ha->host_no, host,
				    dp, path->id);)

				path->mp_byte |= MP_MASK_HIDDEN;
			}
		}

	} else {
		/*
		 * If we couldn't find an existing path, and there is still
		 * room to add one, allocate one and put it in the list.
		 */
		if (path_list->path_cnt < MAX_PATHS_PER_DEVICE &&
			path_list->path_cnt < qla_fo_params.MaxPathsPerDevice) {

			if (port->flags & FCF_CONFIG) {
				/* Use id specified in config file. */
				id = pathid;
				DEBUG3(printk("%s(%ld): using path id %d from "
				    "config file.\n",
				    __func__, host->ha->host_no, id);)
			} else {
				/* Assign one. */
				id = path_list->path_cnt;
				DEBUG3(printk(
				    "%s(%ld): assigning path id %d.\n",
				    __func__, host->ha->host_no, id);)
			}

			/* Update port with bitmask info */
			path = qla4xxx_allocate_path(host, id, port, dev_id);
			if (path) {
#if defined(QL_DEBUG_LEVEL_3)
				printk("%s: allocated new path %p, adding path "
				    "id %d, mp_byte=0x%x\n", __func__, path,
				    id, path->mp_byte);
				if (path->port)
					printk("port=%p-"
					   "%02x%02x%02x%02x%02x%02x%02x%02x\n",
					   path->port,
					   path->port->iscsi_name[0],
					   path->port->iscsi_name[1],
					   path->port->iscsi_name[2],
					   path->port->iscsi_name[3],
					   path->port->iscsi_name[4],
					   path->port->iscsi_name[5],
					   path->port->iscsi_name[6],
					   path->port->iscsi_name[7]);
#endif
				qla4xxx_add_path(path_list, path);

				/*
				 * Reconcile the new path against the existing
				 * ones.
				 */
				qla4xxx_setup_new_path(dp, path, port);
			}
		} else {
			/* EMPTY */
			DEBUG4(printk("%s: Err exit, no space to add path.\n",
			    __func__);)
		}

	}

	LEAVE("qla4xxx_find_or_allocate_path");

	return path;
}

/*
 *  qla4xxx_find_or_allocate_lun
 *      Look through the existing multipath control tree, and find
 *      an mp_lun_t with the supplied world-wide lun number.  If
 *      one cannot be found, allocate one.
 *
 *  Input:
 *      host      Adapter (lun) for the device.
 *      fclun     Lun data from port database.
 *
 *  Returns:
 *      Pointer to new LUN, or NULL if the allocation fails.
 *
 *  Side Effects:
 *      1. If the LUN_LIST does not already point to the LUN,
 *         a new LUN is added to the LUN_LIST.
 *      2. If the DEVICE_LIST does not already point to the DEVICE,
 *         a new DEVICE is added to the DEVICE_LIST.
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static mp_lun_t *
qla4xxx_find_or_allocate_lun(mp_host_t *host, uint16_t dev_id,
    fc_port_t *port, fc_lun_t *fclun)
{
	mp_lun_t		*lun = NULL;
	mp_device_t		*dp = NULL;
#if 0
	mp_device_t		*temp_dp = NULL;
#endif
	uint16_t		len;
	uint16_t		idx;
	uint16_t		new_id = dev_id;
	char			wwulnbuf[WWLUN_SIZE];
	int			new_dev = 0;
	int			i;


	ENTER("qla4xxx_find_or_allocate_lun");
	DEBUG(printk("Entering %s\n", __func__);)

	if( fclun == NULL )
		return NULL;

	DEBUG2(printk("%s: "
		    " lun num=%d fclun %p mplun %p hba inst=%d, port =%p, dev id = %d\n",
		    __func__, fclun->lun, fclun, fclun->mplun, host->instance, port,
		    dev_id);)
	/* 
	 * Perform inquiry page 83 to get the wwuln or 
	 * use what was specified by the user.
	 */
	if ( (port->flags & FCF_CONFIG) ) {
			if( (len = fclun->mplen) != 0 ) 
				memcpy(wwulnbuf, fclun->mpbuf, len); 
	} else {
		len = qla4xxx_get_wwuln_from_device(host, fclun, 
			&wwulnbuf[0], WWLUN_SIZE); 
		/* if fail to do the inq then exit */
		if( len == 0 ) {
			return lun;
		}
	}

	if( len != 0 )
		lun = qla4xxx_find_matching_lunid(wwulnbuf);

	/* 
	 * If this is a visible "controller" lun and
	 * it is already exists on somewhere world wide
 	 * then allocate a new device, so it can be 
	 * exported it to the OS.
	 */
	if( (fclun->flags & FLF_VISIBLE_LUN) &&
		lun != NULL ) {
		if( fclun->mplun ==  NULL ) {
			lun = NULL;
			new_dev++;
		DEBUG2(printk("%s: Creating visible lun "
		    "lun %p num %d fclun %p mplun %p inst=%d, port =%p, dev id = %d\n",
		    __func__, lun, fclun->lun, fclun, fclun->mplun, host->instance, port,
		    dev_id);)
		} else {
			lun = fclun->mplun;
			return lun;
		}
	} 

	if (lun != NULL ) {
		DEBUG2(printk("%s: Found an existing "
		    "lun %p num %d fclun %p host %p inst=%d, port =%p, dev id = %d\n",
		    __func__, lun, fclun->lun, fclun, host, host->instance, port,
		    dev_id);)
		if( (dp = lun->dp ) == NULL ) {
			printk("NO dp pointer in alloacted lun\n");
			return NULL;
		}
		if( qla4xxx_is_iscsiname_in_device(dp,
		    		 port->iscsi_name) ) {

				DEBUG2(printk("%s: Found iscsiname (%s)"
			    " match in mp_dev[%d] = %p\n",
			    __func__,
			    port->iscsi_name,
			    dp->dev_id, dp);)
			if(host->mp_devs[dp->dev_id] == NULL ) {
				host->mp_devs[dp->dev_id] = dp;
				dp->use_cnt++;
			}	
		} else {
			DEBUG(printk("%s(%ld): MP_DEV no-match on iscsiname. adding new port - "
		    	"dev_id %d. "
		    	"iscsi_name (%s)\n",
		    	__func__, host->ha->host_no, dev_id,
			port->iscsi_name);)

			qla4xxx_add_iscsiname_to_mp_dev(dp,
		    	port->iscsi_name, NULL);

			DEBUG2(printk("%s(%d): (1) Added iscsiname and mp_dev[%d] update"
		    	" with dp %p\n ",
		    	__func__, host->ha->host_no, dp->dev_id, dp);)
			if(host->mp_devs[dp->dev_id] == NULL ) {
				host->mp_devs[dp->dev_id] = dp;
				dp->use_cnt++; 
			}	
		} 
	} else {
		DEBUG2(printk("%s: MP_lun %d not found "
		    "for fclun %p inst=%d, port =%p, dev id = %d\n",
		    __func__, fclun->lun, fclun, host->instance, port,
		    dev_id);)
				
			if( (dp = qla4xxx_find_mp_dev_by_iscsiname(host,
			    	port->iscsi_name, &idx)) == NULL || new_dev ) {
				DEBUG2(printk("%s(%d): No match for WWPN. Creating new mpdev \n"
		    	"iscsi_name (%s)\n",
		    	__func__, host->ha->host_no, 
		    	port->iscsi_name );)
			dp = qla4xxx_allocate_mp_dev(port->iscsi_name, port->iscsi_name);
			/* find a good index */
			for( i = dev_id; i < MAX_MP_DEVICES; i++ )
				if(host->mp_devs[i] == NULL ) {
					new_id = i;
					break;
				}
			} else if( dp !=  NULL ) { /* found dp */
				new_id = dp->dev_id;
			}
			
			if( dp !=  NULL ) {
			DEBUG2(printk("%s(%d): (2) mp_dev[%d] update"
		    	" with dp %p\n ",
		    	__func__, host->ha->host_no, new_id, dp);)
				host->mp_devs[new_id] = dp;
				dp->dev_id = new_id;
				dp->use_cnt++;
				lun = kmalloc(sizeof(mp_lun_t), GFP_KERNEL);
				if (lun != NULL) {
					memset(lun, 0, sizeof(*lun));
					DEBUG(printk("Added lun %p to dp %p lun number %d\n",
					lun, dp, fclun->lun);)
					DEBUG(qla4xxx_dump_buffer(wwulnbuf, len);)
					memcpy(lun->wwuln, wwulnbuf, len);
					lun->siz = len;
					lun->number = fclun->lun;
					lun->dp = dp;
					qla4xxx_add_lun(dp, lun);
					INIT_LIST_HEAD(&lun->ports_list);
				}
			}
			else
				printk(KERN_WARNING
			    	"qla4xxx: Couldn't get memory for dp. \n");
	}

	DEBUG(printk("Exiting %s\n", __func__);)
	LEAVE("qla4xxx_find_or_allocate_lun");

	return lun;
}


static uint32_t
qla4xxx_cfg_register_failover_lun(mp_device_t *dp, srb_t *sp, fc_lun_t *new_lp)
{
	uint32_t	status = QLA_SUCCESS;
	os_tgt_t	*tq;
	os_lun_t	*lq;
	fc_lun_t 	*old_lp;

	DEBUG2(printk(KERN_INFO "%s: NEW fclun = %p, sp = %p\n",
	    __func__, new_lp, sp);)

	/*
	 * Fix lun descriptors to point to new fclun which is a new fcport.
	 */
	if (new_lp == NULL) {
		DEBUG2(printk(KERN_INFO "%s: Failed new lun %p\n",
		    __func__, new_lp);)
		return QLA_ERROR;
	}

	tq = sp->tgt_queue;
	lq = sp->lun_queue;
	if (tq == NULL) {
		DEBUG2(printk(KERN_INFO "%s: Failed to get old tq %p\n",
		    __func__, tq);)
		return QLA_ERROR;
	}
	if (lq == NULL) {
		DEBUG2(printk(KERN_INFO "%s: Failed to get old lq %p\n",
		    __func__, lq);)
		return QLA_ERROR;
	}
	old_lp = lq->fclun;
	lq->fclun = new_lp;

	/* Log the failover to console */
	printk(KERN_INFO
	    "qla4xxx: FAILOVER device %d from\n", dp->dev_id);
	printk(KERN_INFO
	    "  [%s] -> [%s]\n", old_lp->fcport->iscsi_name,
	    new_lp->fcport->iscsi_name);
	printk(KERN_INFO
	    "  TGT %02x LUN %02x, reason=0x%x\n",
	    tq->id, new_lp->lun, sp->err_id);
	printk(KERN_INFO
	    "  FROM HBA %d to HBA %d\n", (int)old_lp->fcport->ha->instance,
	    (int)new_lp->fcport->ha->instance);

	DEBUG3(printk("%s: NEW fclun = %p , port =%p, "
	    "loop_id =0x%x, instance %ld\n",
	    __func__,
	    new_lp, new_lp->fcport,
	    new_lp->fcport->loop_id,
	    new_lp->fcport->ha->instance);)

	return status;
}


/*
 * qla4xxx_send_failover_notify
 *      A failover operation has just been done from an old path
 *      index to a new index.  Call lower level driver
 *      to perform the failover notification.
 *
 * Inputs:
 *      device           Device being failed over.
 *      lun                LUN being failed over.
 *      newpath           path that was failed over too.
 *      oldpath           path that was failed over from.
 *
 * Return:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static uint32_t
qla4xxx_send_failover_notify(mp_device_t *dp,
    uint8_t lun, mp_path_t *newpath, mp_path_t *oldpath)
{
	fc_lun_t	*old_lp, *new_lp;
	uint32_t	status = QLA_SUCCESS;

	ENTER("qla4xxx_send_failover_notify");

	if ((old_lp = qla4xxx_find_matching_lun(lun, dp, oldpath)) == NULL) {
		DEBUG2(printk(KERN_INFO "%s: Failed to get old lun %p, %d\n",
		    __func__, old_lp,lun);)
		return QLA_ERROR;
	}
	if ((new_lp = qla4xxx_find_matching_lun(lun, dp, newpath)) == NULL) {
		DEBUG2(printk(KERN_INFO "%s: Failed to get new lun %p,%d\n",
		    __func__, new_lp,lun);)
		return QLA_ERROR;
	}

	/*
	 * If the target is the same target, but a new HBA has been selected,
	 * send a third party logout if required.
	 */
	if ((qla_fo_params.FailoverNotifyType &
			 FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET ||
			qla_fo_params.FailoverNotifyType &
			 FO_NOTIFY_TYPE_LOGOUT_OR_CDB) &&
			qla4xxx_is_name_equal(
				oldpath->iscsiname, newpath->iscsiname)) {

		status =  qla4xxx_send_fo_notification(old_lp, new_lp);
		if (status == QLA_SUCCESS) {
			/* EMPTY */
			DEBUG4(printk("%s: Logout succeded\n",
			    __func__);)
		} else {
			/* EMPTY */
			DEBUG4(printk("%s: Logout Failed\n",
			    __func__);)
		}
	} else if ((qla_fo_params.FailoverNotifyType &
			 FO_NOTIFY_TYPE_LUN_RESET) ||
			(qla_fo_params.FailoverNotifyType &
			 FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET)) {

		/*
		 * If desired, send a LUN reset as the
		 * failover notification type.
		 */
		if (newpath->lun_data.data[lun] & LUN_DATA_ENABLED) {
			status = qla4xxx_send_fo_notification(old_lp, new_lp);
			if (status == QLA_SUCCESS) {
				/* EMPTY */
				DEBUG4(printk("%s: LUN reset succeeded.\n",
				    __func__);)
			} else {
				/* EMPTY */
				DEBUG4(printk("%s: Failed reset LUN.\n",
				    __func__);)
			}
		}

	} else if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB ||
			qla_fo_params.FailoverNotifyType ==
			FO_NOTIFY_TYPE_LOGOUT_OR_CDB) {

		if (newpath->lun_data.data[lun] & LUN_DATA_ENABLED) {
			status = qla4xxx_send_fo_notification(old_lp, new_lp);
			if (status == QLA_SUCCESS) {
				/* EMPTY */
				DEBUG4(printk("%s: Send CDB succeeded.\n",
				    __func__);)
			} else {
				/* EMPTY */
				DEBUG4(printk("%s: Send CDB Error "
				    "lun=(%d).\n", __func__, lun);)
			}
		}
	} else if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_SPINUP ||
			old_lp->fcport->notify_type == FO_NOTIFY_TYPE_SPINUP ){

			status = qla4xxx_send_fo_notification(old_lp, new_lp);
			if (status == QLA_SUCCESS) {
				/* EMPTY */
				DEBUG(printk("%s: Send CDB succeeded.\n",
				    __func__);)
			} else {
				/* EMPTY */
				DEBUG(printk("%s: Send CDB Error "
				    "lun=(%d).\n", __func__, lun);)
			}
	} else {
		/* EMPTY */
		DEBUG4(printk("%s: failover disabled or no notify routine "
		    "defined.\n", __func__);)
	}

	return status;
}

static mp_path_t *
qla4xxx_find_host_from_port(mp_device_t *dp, 
		mp_host_t *host,
		mp_port_t *port )
{
	unsigned long	instance;
	uint8_t 	id;
	int		i;
	mp_path_t	*path = NULL;

	/* get next host instance */
	instance = host->instance;
	for(i = 0 ; i < port->cnt ; i++ ) {
		instance = instance + 1;
		DEBUG3(printk("%s: Finding new instance %d, max %d, cnt %d\n",
			__func__, (int)instance, port->cnt, i);)
		/* Handle wrap-around */
		if( instance == port->cnt )
			instance = 0;
		if( port->hba_list[instance] == NULL )
			continue;
		if( port->hba_list[instance] != host->ha )
			break;
	}
	/* Found a different hba then return the path to it */
	if ( i != port->cnt ) {
		id = port->path_list[instance];
		DEBUG2(printk("%s: Changing to new host - pathid=%d\n",
			__func__, id);)
		path = qla4xxx_find_path_by_id(dp, id);
	}
	return( path );
}

/*
 * Find_best_port
 * This routine tries to locate the best port to the target that 
 * doesn't require issuing a target notify command. 
 */
/* ARGSUSED */
static mp_path_t *
qla4xxx_find_best_port(mp_device_t *dp, 
		mp_path_t *orig_path,
		mp_port_t *port,
		fc_lun_t *fclun )
{
	mp_path_t	*path = NULL;
	mp_path_t	*new_path;
	mp_port_t	*temp_port;
	int		i, found;
	fc_lun_t 	*new_fp;
	struct list_head *list, *temp;
	mp_lun_t *mplun = (mp_lun_t *)fclun->mplun; 
	unsigned long	instance;
	uint16_t	id;

	found = 0;
	list_for_each_safe(list, temp, &mplun->ports_list) {
		temp_port = list_entry(list, mp_port_t, list);
		if ( port == temp_port ) {
			continue;
		}
		/* Search for an active matching lun on any HBA,
		   but starting with the orig HBA */
		instance = orig_path->host->instance;
		for(i = 0 ; i < temp_port->cnt ; instance++) {
			if( instance == MAX_HOSTS )
				instance = 0;
			id = temp_port->path_list[instance];
			DEBUG(printk(
			"qla%d %s: i=%d, Checking temp port=%p, pathid=%d\n",
				(int)instance,__func__, i, temp_port, id);)
			if (id == PATH_INDEX_INVALID)
				continue;
			i++; /* found a valid hba entry */
			new_fp = mplun->paths[id];
			DEBUG(printk(
			"qla%d %s: Checking fclun %p, for pathid=%d\n",
				(int)instance,__func__, new_fp, id);)
			if( new_fp == NULL ) 
				continue;
			new_path = qla4xxx_find_path_by_id(dp, id);
			if( new_path != NULL ) {
			DEBUG(printk(
			"qla%d %s: Found new path new_fp=%p, "
			"path=%p, flags=0x%x\n",
				(int)new_path->host->instance,__func__, new_fp, 
				new_path, new_path->port->flags);)


			if (atomic_read(&new_path->port->state) ==
			    FCS_DEVICE_DEAD) {
				DEBUG2(printk("qla(%d) %s - Port (0x%04x) "
				    "DEAD.\n", (int)new_path->host->instance,
				    __func__, new_path->port->loop_id));
				continue;
			}

			/* Is this path on an active controller? */
			if( (new_path->port->flags & FCF_EVA_DEVICE)  &&
	   			!(new_fp->flags & FLF_ACTIVE_LUN) ){
			 DEBUG2(printk("qla(%d) %s - EVA Port (0x%04x) INACTIVE.\n",
			(int)new_path->host->instance, __func__,
			new_path->port->loop_id);)
				continue;
			}

			if( (new_path->port->flags & FCF_MSA_DEVICE)  &&
       			   !(new_path->port->flags & FCF_MSA_PORT_ACTIVE) ) {
			 DEBUG2(printk("qla(%d) %s - MSA Port (0x%04x) INACTIVE.\n",
			(int)new_path->host->instance, __func__,
			new_path->port->loop_id);)
				continue;
			}

			/* found a good path */
			DEBUG2(printk(
			"qla%d %s: *** Changing from port %p to new port %p - pathid=%d\n",
				(int)instance,__func__, port, temp_port, new_path->id); )
			 return( new_path );
			}
		}
	}

	return( path );
}

void
qla4xxx_find_all_active_ports(srb_t *sp) 
{
	scsi_qla_host_t *ha;
	fc_port_t *fcport;
	fc_lun_t *fclun;
	uint16_t lun;

	DEBUG2(printk(KERN_INFO
	    "%s: Scanning for active ports...\n", __func__);)

	lun = sp->lun_queue->fclun->lun;

	read_lock(&qla4xxx_hostlist_lock);
	list_for_each_entry(ha, &qla4xxx_hostlist, list) {
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->port_type != FCT_TARGET)
				continue;

			if (fcport->flags & (FCF_EVA_DEVICE | FCF_MSA_DEVICE)) {
				list_for_each_entry(fclun, &fcport->fcluns,
				    list) {
					if (fclun->flags & FLF_VISIBLE_LUN)
						continue;
					if (lun != fclun->lun)
						continue;

					qla4xxx_test_active_lun(fcport, fclun);
				}
			}
#if MSA1000_SUPPORTED
			if ((fcport->flags & FCF_MSA_DEVICE))
				qla4xxx_test_active_port(fcport);
#endif
		}
	}
	read_unlock(&qla4xxx_hostlist_lock);

	DEBUG2(printk(KERN_INFO
	    "%s: Done Scanning ports...\n", __func__);)
}

/*
 * qla4xxx_smart_failover
 *      This routine tries to be smart about how it selects the 
 *	next path. It selects the next path base on whether the
 *	loop went down or the port went down. If the loop went
 *	down it will select the next HBA. Otherwise, it will select
 *	the next port. 
 *
 * Inputs:
 *      device           Device being failed over.
 *      sp               Request that initiated failover.
 *      orig_path           path that was failed over from.
 *
 * Return:
 *      next path	next path to use. 
 *	flag 		1 - Don't send notify command 
 *	 		0 - Send notify command 
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static mp_path_t *
qla4xxx_smart_path(mp_device_t *dp, 
	mp_path_t *orig_path, srb_t *sp, int *flag )
{
	mp_path_t	*path = NULL;
	fc_lun_t *fclun;
	mp_port_t *port;
	mp_host_t *host= orig_path->host;
		
	DEBUG2(printk("Entering %s - sp err = %d, instance =%d\n", 
		__func__, sp->err_id, (int)host->instance);)

	qla4xxx_find_all_active_ports(sp);
 
	if( sp != NULL ) {
		fclun = sp->lun_queue->fclun;
		if( fclun == NULL ) {
			printk( KERN_INFO
			"scsi%d %s: couldn't find fclun %p pathid=%d\n",
				(int)host->instance,__func__, fclun, orig_path->id);
			return( orig_path->next );
		}
		port = qla4xxx_find_port_by_name( 
			(mp_lun_t *)fclun->mplun, orig_path);
		if( port == NULL ) {
			printk( KERN_INFO
			"scsi%d %s: couldn't find MP port %p pathid=%d\n",
				(int)host->instance,__func__, port, orig_path->id);
			return( orig_path->next );
		} 

		/* Change to next HOST if loop went down */
		if( sp->err_id == SRB_ERR_LOOP )  {
			path = qla4xxx_find_host_from_port(dp, 
					host, port );
			if( path != NULL ) {
				port->fo_cnt++;
				*flag = 1;
		  		/* if we used all the hbas then 
			   	try and get another port */ 
		  		if( port->fo_cnt > port->cnt ) {
					port->fo_cnt = 0;
					*flag = 0;
					path = 
					  qla4xxx_find_best_port(dp, 
						orig_path, port, fclun );
					if( path )
						*flag = 1;
		   		}
			}
		} else {
			path = qla4xxx_find_best_port(dp, 
				orig_path, port, fclun );
			if( path )
				*flag = 1;
		}
	}
	/* Default path is next path*/
	if (path == NULL) 
		path = orig_path->next;

	DEBUG3(printk("Exiting %s\n", __func__);)
	return path;
}

/*
 *  qla4xxx_select_next_path
 *      A problem has been detected with the current path for this
 *      device.  Try to select the next available path as the current
 *      path for this device.  If there are no more paths, the same
 *      path will still be selected.
 *
 *  Inputs:
 *      dp           pointer of device structure.
 *      lun                LUN to failover.
 *
 *  Return Value:
 *      	new path or same path
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla4xxx_select_next_path(mp_host_t *host, mp_device_t *dp, uint8_t lun,
	srb_t *sp)
{
	mp_path_t	*path = NULL;
	mp_path_list_t	*path_list;
	mp_path_t	*orig_path;
	int		id;
	uint32_t	status;
	mp_host_t *new_host;
	int	skip_notify= 0;
#if 0
	fc_lun_t	*new_fp = NULL;
#endif
	

	ENTER("qla4xxx_select_next_path:");

	path_list = dp->path_list;
	if (path_list == NULL)
		return NULL;

	/* Get current path */
	id = path_list->current_path[lun];

	/* Get path for current path id  */
	if ((orig_path = qla4xxx_find_path_by_id(dp, id)) != NULL) {
		/* select next path */
       		if (orig_path->port && (orig_path->port->flags &
		    (FCF_MSA_DEVICE|FCF_EVA_DEVICE))) {
			path = qla4xxx_smart_path(dp, orig_path, sp,
			    &skip_notify); 
		} else
			path = orig_path->next;

		new_host = path->host;

		/* FIXME may need to check for HBA being reset */
		DEBUG2(printk("%s: orig path = %p new path = %p " 
		    "curr idx = %d, new idx = %d\n",
		    __func__, orig_path, path, orig_path->id, path->id);)
		DEBUG3(printk("  FAILOVER: device name: %s\n",
		    dp->devname);)
		DEBUG3(printk(" Original  - host name: %s\n",
		    orig_path->host->iscsi_name);)
		DEBUG3(printk("   path name: %s\n",
		    orig_path->port->iscsi_name);)
		DEBUG3(printk(" New  - host name: %s\n",
		    new_host->iscsi_name);)
		DEBUG3(printk("   path name: %s\n",
		    path->port->iscsi_name);)

		path_list->current_path[lun] = path->id;
		/* If we selected a new path, do failover notification. */
		if ( (path != orig_path) && !skip_notify ) {
			status = qla4xxx_send_failover_notify(
					dp, lun, path, orig_path);

			/*
			 * Currently we ignore the returned status from
			 * the notify. however, if failover notify fails
			 */
		}
	}

	LEAVE("qla4xxx_select_next_path:");

	return  path ;
}



/*
 *  qla4xxx_update_mp_host
 *      Update the multipath control information from the port
 *      database for that adapter.
 *
 *  Input:
 *      host      Adapter to update. Devices that are new are
 *                      known to be attached to this adapter.
 *
 *  Returns:
 *      1 if updated successfully; 0 if error.
 *
 */
static uint8_t
qla4xxx_update_mp_host(mp_host_t  *host)
{
	uint8_t		success = 1;
	uint16_t	dev_id;
	fc_port_t 	*fcport;
	scsi_qla_host_t *ha = host->ha;

	ENTER("qla4xxx_update_mp_host");

	/*
	 * We make sure each port is attached to some virtual device.
	 */
	dev_id = 0;
	fcport = NULL;
 	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		DEBUG2(printk("%s(%d): checking fcport list. update port "
		    "%p-%02x%02x%02x%02x%02x%02x%02x%02x dev_id %d "
		    "to ha inst %d.\n",
		    __func__, ha->host_no,
		    fcport,
		    fcport->iscsi_name[0], fcport->iscsi_name[1],
		    fcport->iscsi_name[2], fcport->iscsi_name[3],
		    fcport->iscsi_name[4], fcport->iscsi_name[5],
		    fcport->iscsi_name[6], fcport->iscsi_name[7],
		    dev_id, ha->instance);)

		qla4xxx_configure_cfg_device(fcport);
		success |= qla4xxx_update_mp_device(host, fcport, dev_id, 0);
		dev_id++;
	}
	if (success) {
		DEBUG2(printk(KERN_INFO "%s: Exit OK\n", __func__);)
		qla4xxx_map_os_targets(host);
	} else {
		/* EMPTY */
		DEBUG2(printk(KERN_INFO "%s: Exit FAILED\n", __func__);)
	}

	DEBUG2(printk("%s: inst %d exiting.\n", __func__, ha->instance);)
	LEAVE("qla4xxx_update_mp_host");

	return success;
}

/*
 *  qla4xxx_update_mp_device
 *      Update the multipath control information from the port
 *      database for that adapter.
 *
 *  Inputs:
 *		host   Host adapter structure
 *      port   Device to add to the path tree.
 *		dev_id  Device id
 *
 *  Synchronization:
 *      The Adapter Lock should have already been acquired
 *      before calling this routine.
 *
 *  Return
 *      1 if updated successfully; 0 if error.
 *
 */
uint8_t
qla4xxx_update_mp_device(mp_host_t *host,
    fc_port_t *port, uint16_t dev_id, uint16_t pathid)
{
	uint8_t		success = 1;

	ENTER("qla4xxx_update_mp_device");

	DEBUG3(printk("%s(%ld): entered. host %p inst=%d," 
	    "port iscsi_name=%s, dev id = %d\n",
	    __func__, host->ha->host_no, host, host->instance,
	    port->iscsi_name,
	    dev_id);)

	if (!qla4xxx_is_name_zero(port->iscsi_name)) {
		if( port->fo_combine ) {
			return( port->fo_combine(host, dev_id, port, pathid) );
		} else 
			success = qla4xxx_combine_by_lunid( host, dev_id, port, pathid ); 

	} else {
		/* EMPTY */
		DEBUG4(printk("%s: Failed iscsiname empty.\n",
		    __func__);)
	}

	DEBUG3(printk("%s(%ld): exiting.\n",
	    __func__, host->ha->host_no);)
	LEAVE("qla4xxx_update_mp_device");

	return success;
}

/*
 * qla4xxx_update_mp_tree
 *      Get port information from each adapter, and build or rebuild
 *      the multipath control tree from this data.  This is called
 *      from init and during port database notification.
 *
 * Input:
 *      None
 *
 * Return:
 *      Local function return code.
 *
 */
static uint32_t
qla4xxx_update_mp_tree(void)
{
	mp_host_t	*host;
	uint32_t	rval = QLA_SUCCESS;

	ENTER("qla4xxx_update_mp_tree:");

	/* Loop through each adapter and see what needs updating. */
	for (host = mp_hosts_base; (host) ; host = host->next) {

		DEBUG4(printk("%s: hba(%d) flags (%x)\n",
		    __func__, host->instance, host->flags);)
		/* Clear the countdown; it may be reset in the update. */
		host->relogin_countdown = 0;

		/* Override the NEEDS_UPDATE flag if disabled. */
		if (host->flags & MP_HOST_FLAG_DISABLE ||
		    list_empty(host->fcports))
			host->flags &= ~MP_HOST_FLAG_NEEDS_UPDATE;

		if (host->flags & MP_HOST_FLAG_NEEDS_UPDATE) {

			/*
			 * Perform the actual updates.  If this succeeds, clear
			 * the flag that an update is needed, and failback all
			 * devices that are visible on this path to use this
			 * path.  If the update fails, leave set the flag that
			 * an update is needed, and it will be picked back up
			 * during the next timer routine.
			 */
			if (qla4xxx_update_mp_host(host)) {
				host->flags &= ~MP_HOST_FLAG_NEEDS_UPDATE;

				qla4xxx_failback_luns(host);
			} else
				rval = QLA_ERROR;

		}

	}

	if (rval != QLA_SUCCESS) {
		/* EMPTY */
		DEBUG4(printk("%s: Exit FAILED.\n", __func__);)

	} else {
		/* EMPTY */
		DEBUG4(printk("%s: Exit OK.\n", __func__);)
	}
	return rval;
}



/*
 * qla4xxx_find_matching_lun_by_num
 *      Find the lun in the path that matches the
 *  specified lun number.
 *
 * Input:
 *      lun  = lun number
 *      newpath = path to search for lun
 *
 * Returns:
 *      NULL or pointer to lun
 *
 * Context:
 *      Kernel context.
 * (dg)
 */
static fc_lun_t  *
qla4xxx_find_matching_lun_by_num(uint16_t lun_no, mp_device_t *dp,
	mp_path_t *newpath)
{
	int found;
	fc_lun_t *lp = NULL;	/* lun ptr */
	fc_port_t *fcport;		/* port ptr */
	mp_lun_t  *lun;

	/* Use the lun list if we have one */	
	if( dp->luns ) {
		for (lun = dp->luns; lun != NULL ; lun = lun->next) {
			if( lun_no == lun->number ) {
				lp = lun->paths[newpath->id];
				break;
			}
		}
	} else {
	if ((fcport = newpath->port) != NULL) {
			found = 0;
			list_for_each_entry(lp, &fcport->fcluns, list) {
				if (lun_no == lp->lun) {
					found++;
				break;
			}
		}
			if (!found)
				lp = NULL;
		}
	}
	return lp;
}

static fc_lun_t  *
qla4xxx_find_matching_lun(uint8_t lun, mp_device_t *dp, 
	mp_path_t *newpath)
{
	fc_lun_t *lp;

	lp = qla4xxx_find_matching_lun_by_num(lun, dp, newpath);

	return lp;
}

/*
 * qla4xxx_find_path_by_name
 *      Find the path specified iscsiname from the pathlist
 *
 * Input:
 *      host = host adapter pointer.
 * 	pathlist =  multi-path path list
 *      iscsiname  	iscsiname to search for
 *
 * Returns:
 * pointer to the path or NULL
 *
 * Context:
 *      Kernel context.
 */
mp_path_t *
qla4xxx_find_path_by_name(mp_host_t *host, mp_path_list_t *plp,
    uint8_t *iscsiname)
{
	mp_path_t  *path = NULL;		/* match if not NULL */
	mp_path_t  *tmp_path;
	int cnt;

	if ((tmp_path = plp->last) != NULL) {
		for (cnt = 0; (tmp_path) && cnt < plp->path_cnt; cnt++) {
			if (tmp_path->host == host &&
				qla4xxx_is_name_equal(
					tmp_path->iscsiname, iscsiname)) {

				path = tmp_path;
				break;
			}
			tmp_path = tmp_path->next;
		}
	}
	return path ;
}

/*
 * qla4xxx_find_path_by_id
 *      Find the path for the specified path id.
 *
 * Input:
 * 	dp 		multi-path device
 * 	id 		path id
 *
 * Returns:
 *      pointer to the path or NULL
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla4xxx_find_path_by_id(mp_device_t *dp, uint8_t id)
{
	mp_path_t  *path = NULL;
	mp_path_t  *tmp_path;
	mp_path_list_t		*path_list;
	int cnt;

	path_list = dp->path_list;
	tmp_path = path_list->last;
	for (cnt = 0; (tmp_path) && cnt < path_list->path_cnt; cnt++) {
		if (tmp_path->id == id) {
			path = tmp_path;
			break;
		}
		tmp_path = tmp_path->next;
	}
	return path ;
}

/*
 * qla4xxx_find_mp_dev_by_id
 *      Find the mp_dev for the specified target id.
 *
 * Input:
 *      host = host adapter pointer.
 *      tgt  = Target id
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t  *
qla4xxx_find_mp_dev_by_id(mp_host_t *host, uint16_t id )
{
	if (id < MAX_MP_DEVICES)
		return host->mp_devs[id];
	else
		return NULL;
}

/*
 * qla4xxx_find_mp_dev_by_iscsiname
 *      Find the mp_dev for the specified target name.
 *
 * Input:
 *      host = host adapter pointer.
 *      name  = port name
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t  *
qla4xxx_find_mp_dev_by_iscsiname(mp_host_t *host, uint8_t *name, uint16_t *pidx)
{
	int		id;
	mp_device_t	*dp = NULL;

	DEBUG3(printk("%s: entered.\n", __func__);)

	for (id= 0; id < MAX_MP_DEVICES; id++) {
		if ((dp = host->mp_devs[id] ) == NULL)
			continue;

		if (qla4xxx_is_iscsiname_in_device(dp, name)) {
			DEBUG3(printk("%s: Found matching device @ index %d:\n",
			    __func__, id);)
			*pidx = id;
			return dp;
		}
	}

	DEBUG3(printk("%s: exiting.\n", __func__);)
 
 	return NULL;
 }

/*
 * qla4xxx_get_visible_path
 * Find the the visible path for the specified device.
 *
 * Input:
 *      dp = device pointer
 *
 * Returns:
 *      NULL or path
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla4xxx_get_visible_path(mp_device_t *dp)
{
	uint16_t	id;
	mp_path_list_t	*path_list;
	mp_path_t	*path;

	path_list = dp->path_list;
	/* if we don't have a visible path skip it */
	if ((id = path_list->visible) == PATH_INDEX_INVALID) {
		return NULL;
	}

	if ((path = qla4xxx_find_path_by_id(dp,id))== NULL)
		return NULL;

	return path ;
}

/*
 * qla4xxx_map_os_targets
 * Allocate the luns and setup the OS target.
 *
 * Input:
 *      host = host adapter pointer.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 */
static void
qla4xxx_map_os_targets(mp_host_t *host)
{
	scsi_qla_host_t *ha = host->ha;
	mp_path_t	*path;
	mp_device_t 	*dp;
	os_tgt_t	*tgt;
	int		t;

	ENTER("qla4xxx_map_os_targets ");

	for (t = 0; t < MAX_TARGETS; t++ ) {
		dp = host->mp_devs[t];
		if (dp != NULL) {
			DEBUG2(printk("%s: (%d) found a dp=%p, "
			    "host=%p, ha=%p\n",
			    __func__, t, dp, host,ha);)

			if ((path = qla4xxx_get_visible_path(dp)) == NULL) {
				DEBUG2( printk(KERN_INFO
				    "qla_cfg(%d): No visible path "
				    "for target %d, dp = %p\n",
				    host->instance, t, dp); )
				continue;
			}

			/* if not the visible path skip it */
			if (path->host == host) {
				if (TGT_Q(ha, t) == NULL) {
					/* XXX need to check for NULL */
					tgt = qla4xxx_tgt_alloc(ha, t);
					memcpy(tgt->iscsi_name,dp->devname,
					    ISCSI_NAME_SIZE);
					tgt->fcport = path->port;
				}
				if (path->port)
					path->port->os_target_id = t;

				DEBUG3(printk("%s(%ld): host instance =%d, "
				    "device= %p, tgt=%d has VISIBLE path,"
				    "path id=%d\n",
				    __func__, ha->host_no,
				    host->instance,
				    dp, t, path->id);)
			} else {
			/* EMPTY */
				DEBUG3(printk("%s(%ld): host instance =%d, "
				    "device= %p, tgt=%d has HIDDEN "
				    "path, path id=%d\n",
				    __func__, ha->host_no,
				    host->instance, dp, t, 
					path->id); )
				continue;
			}
			qla4xxx_map_os_luns(host, dp, t);
		} else {
			if ((tgt= TGT_Q(ha,t)) != NULL) {
				qla4xxx_tgt_free(ha,t);
			}
		}
	}

	LEAVE("qla4xxx_map_os_targets ");
}

static void
qla4xxx_map_or_failover_oslun(mp_host_t *host, mp_device_t *dp, 
	uint16_t t, uint16_t lun_no)
{
	int	i;

	/* 
	 * if this is initization time and we couldn't map the
	 * lun then try and find a usable path.
	 */
	if ( qla4xxx_map_a_oslun(host, dp, t, lun_no) &&
		(host->flags & MP_HOST_FLAG_LUN_FO_ENABLED) ){
		/* find a path for us to use */
		for ( i = 0; i < dp->path_list->path_cnt; i++ ){
			qla4xxx_select_next_path(host, dp, lun_no, NULL);
			if( !qla4xxx_map_a_oslun(host, dp, t, lun_no))
				break;
		}
	}
}

/*
 * qla4xxx_map_os_luns
 *      Allocate the luns for the OS target.
 *
 * Input:
 *      dp = pointer to device
 *      t  = OS target number.
 *
 * Returns:
 *      None
 *
 * Context:
 *	Kernel context.
 */
static void
qla4xxx_map_os_luns(mp_host_t *host, mp_device_t *dp, uint16_t t)
{
	uint16_t lun_no;
	mp_lun_t	*lun;
	os_lun_t *up;

	DEBUG3(printk("Entering %s..\n",__func__);)

	/* if we are using lun binding then scan for the discovered luns */
	if( dp->luns ) {
		for (lun = dp->luns; lun != NULL ; lun = lun->next) {
			lun_no = lun->number;
			DEBUG2(printk("%s: instance %d: Mapping target %d, lun %d..\n",
				__func__,host->instance,t,lun->number);)
			qla4xxx_map_or_failover_oslun(host, dp, 
				t, lun_no);
			up = (os_lun_t *) GET_LU_Q(host->ha, t, lun_no);
			if (up == NULL || up->fclun == NULL) {
			DEBUG2(printk("%s: instance %d: No FCLUN for target %d, lun %d.. \n",
				__func__,host->instance,t,lun->number);)
				continue;
			}
			if (up->fclun->fcport == NULL) {
			DEBUG2(printk("%s: instance %d: No FCPORT for target %d, lun %d.. \n",
				__func__,host->instance,t,lun->number);)
				continue;
			}
			DEBUG2(printk("%s: instance %d: Mapping target %d, lun %d.. to path id %d\n",
				__func__,host->instance,t,lun->number,
			    up->fclun->fcport->cur_path);)
		}
	} else {
		for (lun_no = 0; lun_no < MAX_LUNS; lun_no++ ) {
			qla4xxx_map_or_failover_oslun(host, dp, 
				t, lun_no);
		}
	}
	DEBUG3(printk("Exiting %s..\n",__func__);)
}

/*
 * qla4xxx_map_a_osluns
 *      Map the OS lun to the current path
 *
 * Input:
 *      host = pointer to host
 *      dp = pointer to device
 *      lun  = OS lun number.
 *
 * Returns:
 *      None
 *
 * Context:
 *	Kernel context.
 */

static uint8_t
qla4xxx_map_a_oslun(mp_host_t *host, mp_device_t *dp, uint16_t t, uint16_t lun)
{
	fc_port_t	*fcport;
	fc_lun_t	*fclun;
	os_lun_t	*lq;
	uint16_t	id;
	mp_path_t	*path, *vis_path;
	mp_host_t 	*vis_host;
	uint8_t		status = 0;

	if ((id = dp->path_list->current_path[lun]) != PATH_INDEX_INVALID) {
		DEBUG3(printk( "qla4xxx(%d): Current path for lun %d is path id %d\n",
		    host->instance,
		    lun, id);)
		path = qla4xxx_find_path_by_id(dp,id);
		if (path) {
			fcport = path->port;
			if (fcport) {

			 	fcport->cur_path = id;
				fclun = qla4xxx_find_matching_lun(lun,dp,path);
		DEBUG3(printk( "qla4xxx(%d): found fclun %p, path id = %d\n", host->instance,fclun,id);)

				/* Always map all luns if they are enabled */
				if (fclun &&
					(path->lun_data.data[lun] &
					 LUN_DATA_ENABLED) ) {
		DEBUG(printk( "qla4xxx(%d): Current path for lun %d/%p is path id %d\n",
		    host->instance,
		    lun, fclun, id);)
		DEBUG3(printk( "qla4xxx(%d): Lun is enable \n", host->instance);)

					/*
					 * Mapped lun on the visible path
					 */
					if ((vis_path =
					    qla4xxx_get_visible_path(dp)) ==
					    NULL ) {

						printk(KERN_INFO
						    "qla4xxx(%d): No visible "
						    "path for target %d, "
						    "dp = %p\n",
						    host->instance,
						    t, dp);

						return 0;
					}
					vis_host = vis_path->host;

					/* ra 11/30/01 */
					/*
					 * Always alloc LUN 0 so kernel
					 * will scan past LUN 0.
					 */
#if 0
					if (lun != 0 &&
					    (EXT_IS_LUN_BIT_SET(
						&(fcport->lun_mask), lun))) {

						/* mask this LUN */
						return 0;
					}
#endif

					if ((lq = qla4xxx_lun_alloc(
							vis_host->ha,
							t, lun)) != NULL) {

						lq->fclun = fclun;
					}
		DEBUG(printk( "qla4xxx(%d): lun allocated %p for lun %d\n",
			 host->instance,lq,lun);)
				}
			}
			else
				status = 1;
		}
	}
	return status;
}

/*
 * qla4xxx_is_name_zero
 *
 * Input:
 *      name = Pointer to WW name to check
 *
 * Returns:
 *      1 if name is 0 else 0
 *
 * Context:
 *      Kernel context.
 */
static uint8_t
qla4xxx_is_name_zero(uint8_t *nn)
{
	int cnt;

	/* Check for zero node name */
	for (cnt = 0; cnt < ISCSI_NAME_SIZE ; cnt++, nn++) {
		if (*nn != 0)
			break;
	}
	/* if zero return 1 */
	if (cnt == ISCSI_NAME_SIZE)
		return 1;
	else
		return 0;
}

/*
 * qla4xxx_add_path
 * Add a path to the pathlist
 *
 * Input:
 * pathlist -- path list of paths
 * path -- path to be added to list
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 */
static void
qla4xxx_add_path( mp_path_list_t *pathlist, mp_path_t *path )
{
	mp_path_t *last = pathlist->last;

	ENTER("qla4xxx_add_path");
	DEBUG3(printk("%s: entered for path id %d.\n",
	    __func__, path->id);)

	DEBUG3(printk("%s: pathlist =%p, path =%p, cnt = %d\n",
	    __func__, pathlist, path, pathlist->path_cnt);)
	if (last == NULL) {
		last = path;
	} else {
		path->next = last->next;
	}

	last->next = path;
	pathlist->last = path;
	pathlist->path_cnt++;

	DEBUG3(printk("%s: exiting. path cnt=%d.\n",
	    __func__, pathlist->path_cnt);)
	LEAVE("qla4xxx_add_path");
}

static void
qla4xxx_add_lun( mp_device_t *dp, mp_lun_t *lun)
{
	mp_lun_t 	*cur_lun;

	ENTER("qla4xxx_add_lun");

	/* Insert new entry into the list of luns */
	lun->next = NULL;

	cur_lun = dp->luns;
	if( cur_lun == NULL ) {
		dp->luns = lun;
	} else {
		/* add to tail of list */
		while( cur_lun->next != NULL )
			cur_lun = cur_lun->next;

		cur_lun->next = lun;
	}
	LEAVE("qla4xxx_add_lun");
}

/*
 * qla4xxx_is_iscsiname_in_device
 *	Search for the specified "iscsiname" in the device list.
 *
 * Input:
 *	dp = device pointer
 *	iscsiname = iscsiname to searched for in device
 *
 * Returns:
 *      qla4xxx local function return status code.
 *
 * Context:
 *      Kernel context.
 */
int
qla4xxx_is_iscsiname_in_device(mp_device_t *dp, uint8_t *iscsiname)
{
	int idx;

	for (idx = 0; idx < MAX_PATHS_PER_DEVICE; idx++) {
		if (memcmp(&dp->iscsinames[idx][0], iscsiname, ISCSI_NAME_SIZE) == 0)
			return 1;
	}
	return 0;
}


/*
 *  qla4xxx_set_lun_data_from_bitmask
 *      Set or clear the LUN_DATA_ENABLED bits in the LUN_DATA from
 *      a LUN bitmask provided from the miniport driver.
 *
 *  Inputs:
 *      lun_data = Extended LUN_DATA buffer to set.
 *      lun_mask = Pointer to lun bit mask union.
 *
 *  Return Value: none.
 */
void
qla4xxx_set_lun_data_from_bitmask(mp_lun_data_t *lun_data,
    lun_bit_mask_t *lun_mask)
{
	int16_t	lun;

	ENTER("qla4xxx_set_lun_data_from_bitmask");

	for (lun = 0; lun < MAX_LUNS; lun++) {
		/* our bit mask is inverted */
#if 0
		if (!(EXT_IS_LUN_BIT_SET(lun_mask,lun)))
			lun_data->data[lun] |= LUN_DATA_ENABLED;
		else
			lun_data->data[lun] &= ~LUN_DATA_ENABLED;
#else
		lun_data->data[lun] |= LUN_DATA_ENABLED;
#endif

		DEBUG5(printk("%s: lun data[%d] = 0x%x\n",
		    __func__, lun, lun_data->data[lun]);)
	}

	LEAVE("qla4xxx_set_lun_data_from_bitmask");

	return;
}

static void
qla4xxx_failback_single_lun(mp_device_t *dp, uint8_t lun, uint8_t new)
{
	mp_path_list_t   *pathlist;
	mp_path_t        *new_path, *old_path;
	uint8_t 	old;
	mp_host_t  *host;
	os_lun_t *lq;
	mp_path_t	*vis_path;
	mp_host_t 	*vis_host;

	/* Failback and update statistics. */
	if ((pathlist = dp->path_list) == NULL)
		return;

	old = pathlist->current_path[lun];
	pathlist->current_path[lun] = new;

	if ((new_path = qla4xxx_find_path_by_id(dp, new)) == NULL)
		return;
	if ((old_path = qla4xxx_find_path_by_id(dp, old)) == NULL)
		return;

	/* An fclun should exist for the failbacked lun */
	if (qla4xxx_find_matching_lun(lun, dp, new_path) == NULL)
		return;
	if (qla4xxx_find_matching_lun(lun, dp, old_path) == NULL)
		return;

	/* Log to console and to event log. */
	printk(KERN_INFO
		"qla4xxx: FAILBACK device %d -> "
		"[%s] LUN %02x\n",
		dp->dev_id, dp->devname, lun);

	printk(KERN_INFO
		"qla4xxx: FROM HBA %d to HBA %d \n",
		old_path->host->instance,
		new_path->host->instance);


	/* Send a failover notification. */
	qla4xxx_send_failover_notify(dp, lun, new_path, old_path);

	host = 	new_path->host;

	/* remap the lun */
	qla4xxx_map_a_oslun(host, dp, dp->dev_id, lun);

	/* 7/16
	 * Reset counts on the visible path
	 */
	if ((vis_path = qla4xxx_get_visible_path(dp)) == NULL) {
		printk(KERN_INFO
			"qla4xxx(%d): No visible path for "
			"target %d, dp = %p\n",
			host->instance,
			dp->dev_id, dp);
		return;
	}

	vis_host = vis_path->host;
	if ((lq = qla4xxx_lun_alloc(vis_host->ha, dp->dev_id, lun)) != NULL) {
		qla4xxx_delay_lun(vis_host->ha, lq, ql4xrecoveryTime);
		qla4xxx_flush_failover_q(vis_host->ha, lq);
		qla4xxx_reset_lun_fo_counts(vis_host->ha, lq);
	}
}

#if 0
static void
qla4xxx_failback_single_lun(mp_device_t *dp, uint8_t lun, uint8_t new)
{
	mp_path_list_t   *pathlist;
	mp_path_t        *new_path, *old_path;
	uint8_t 	old;
	mp_host_t  *new_host;
	os_lun_t *lq;
	mp_path_t	*vis_path;
	mp_host_t 	*vis_host;
	int		status;

	/* Failback and update statistics. */
	if ((pathlist = dp->path_list) == NULL)
		return;

	old = pathlist->current_path[lun];
	/* pathlist->current_path[lun] = new; */

	if ((new_path = qla4xxx_find_path_by_id(dp, new)) == NULL)
		return;
	if ((old_path = qla4xxx_find_path_by_id(dp, old)) == NULL)
		return;

	/* An fclun should exist for the failbacked lun */
	if (qla4xxx_find_matching_lun(lun, dp, new_path) == NULL)
		return;
	if (qla4xxx_find_matching_lun(lun, dp, old_path) == NULL)
		return;

	if ((vis_path = qla4xxx_get_visible_path(dp)) == NULL) {
		printk(KERN_INFO
			"No visible path for "
			"target %d, dp = %p\n",
			dp->dev_id, dp);
		return;
	}
	vis_host = vis_path->host;
	/* Schedule the recovery before we move the luns */
	if( (lq = (os_lun_t *) 
		LUN_Q(vis_host->ha, dp->dev_id, lun)) == NULL ) {
		printk(KERN_INFO
			"qla4xxx(%d): No visible lun for "
			"target %d, dp = %p, lun=%d\n",
			vis_host->instance,
			dp->dev_id, dp, lun);
		return;
  	}

	qla4xxx_delay_lun(vis_host->ha, lq, ql4xrecoveryTime);

	/* Log to console and to event log. */
	printk(KERN_INFO
		"qla4xxx: FAILBACK device %d -> "
		"%02x%02x%02x%02x%02x%02x%02x%02x LUN %02x\n",
		dp->dev_id,
		dp->devname[0], dp->devname[1],
		dp->devname[2], dp->devname[3],
		dp->devname[4], dp->devname[5],
		dp->devname[6], dp->devname[7],
		lun);

	printk(KERN_INFO
		"qla4xxx: FROM HBA %d to HBA %d \n",
		old_path->host->instance,
		new_path->host->instance);


	/* Send a failover notification. */
	status = qla4xxx_send_failover_notify(dp, lun, 
			new_path, old_path);

	new_host = 	new_path->host;

	/* remap the lun */
	if (status == QLA_SUCCESS ) {
		pathlist->current_path[lun] = new;
		qla4xxx_map_a_oslun(new_host, dp, dp->dev_id, lun);
		qla4xxx_flush_failover_q(vis_host->ha, lq);
		qla4xxx_reset_lun_fo_counts(vis_host->ha, lq);
	}
}
#endif

/*
*  qla4xxx_failback_luns
*      This routine looks through the devices on an adapter, and
*      for each device that has this adapter as the visible path,
*      it forces that path to be the current path.  This allows us
*      to keep some semblance of static load balancing even after
*      an adapter goes away and comes back.
*
*  Arguments:
*      host          Adapter that has just come back online.
*
*  Return:
*	None.
*/
static void
qla4xxx_failback_luns( mp_host_t  *host)
{
	uint16_t          dev_no;
	uint8_t           l;
	uint16_t          lun;
	int i;
	mp_device_t      *dp;
	mp_path_list_t   *path_list;
	mp_path_t        *path;
	fc_lun_t	*new_fp;

	ENTER("qla4xxx_failback_luns");

	for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		path_list = dp->path_list;
		for (path = path_list->last, i= 0;
			i < path_list->path_cnt;
			i++, path = path->next) {

			if (path->host != host )
				continue;

			if (path->port == NULL)
				continue;

			if (atomic_read(&path->port->state) == FCS_DEVICE_DEAD)
				continue;

		        if ((path->port->flags & FCF_FAILBACK_DISABLE))
				continue;

			/* 
			 * Failback all the paths for this host,
			 * the luns could be preferred across all paths 
			 */
			DEBUG4(printk("%s(%d): Lun Data for device %p, "
			    "dev id=%d, path id=%d\n",
			    __func__, host->instance, dp, dp->dev_id,
			    path->id);)
			DEBUG4(qla4xxx_dump_buffer(
			    (char *)&path->lun_data.data[0], 64);)
			DEBUG4(printk("%s(%d): Perferrred Path data:\n",
			    __func__, host->instance);)
			DEBUG4(qla4xxx_dump_buffer(
			    (char *)&path_list->current_path[0], 64);)

			for (lun = 0; lun < MAX_LUNS_PER_DEVICE; lun++) {
				l = (uint8_t)(lun & 0xFF);

				/*
				 * if this is the preferred lun and not
				 * the current path then failback lun.
				 */
				DEBUG4(printk("%s: target=%d, cur path id =%d, "
				    "lun data[%d] = %d)\n",
				    __func__, dp->dev_id, path->id,
				    lun, path->lun_data.data[lun]);)

				if ((path->lun_data.data[l] &
						LUN_DATA_PREFERRED_PATH) &&
					/* !path->relogin && */
					path_list->current_path[l] !=
						path->id) {
					/* No point in failing back a
					   disconnected lun */
					new_fp = qla4xxx_find_matching_lun(
							l, dp, path);

					if (new_fp == NULL)
						continue;
					/* Skip a disconect lun */
					if (new_fp->device_type & 0x20)
						continue;

					qla4xxx_failback_single_lun(
							dp, l, path->id);
				}
			}
		}

	}

	LEAVE("qla4xxx_failback_luns");

	return;
}

static struct _mp_path *
qla4xxx_find_first_active_path( mp_device_t *dp, mp_lun_t *lun)
{
	mp_path_t *path= NULL;
	mp_path_list_t  *plp = dp->path_list;
	mp_path_t  *tmp_path;
	fc_port_t 	*fcport;
	fc_lun_t 	*fclun;
	int cnt;

	if ((tmp_path = plp->last) != NULL) {
		tmp_path = tmp_path->next;
		for (cnt = 0; (tmp_path) && cnt < plp->path_cnt;
		    tmp_path = tmp_path->next, cnt++) {
			fcport = tmp_path->port;
			if (fcport != NULL) {
				if ((fcport->flags & FCF_EVA_DEVICE)) { 
					fclun = lun->paths[tmp_path->id];
					if (fclun == NULL)
						continue;
					if (fclun->flags & FLF_ACTIVE_LUN) {
						path = tmp_path;
						break;
					}
				} else {
					if ((fcport->flags &
					    FCF_MSA_PORT_ACTIVE)) {
						path = tmp_path;
						break;
					}
				}
			}
		}
	}
	return path;
}

/*
 *  qla4xxx_setup_new_path
 *      Checks the path against the existing paths to see if there
 *      are any incompatibilities.  It then checks and sets up the
 *      current path indices.
 *
 *  Inputs:
 *      dp   =  pointer to device
 *      path = new path
 *
 *  Returns:
 *      None
 */
static void
qla4xxx_setup_new_path( mp_device_t *dp, mp_path_t *path, fc_port_t *fcport)
{
	mp_path_list_t  *path_list = dp->path_list;
	mp_path_t       *tmp_path, *first_path;
	mp_host_t       *first_host;
	mp_host_t       *tmp_host;

	uint16_t	lun;
	uint8_t		l;
	int		i;

	ENTER("qla4xxx_setup_new_path");
	DEBUG(printk("qla4xxx_setup_new_path: path %p path id %d, fcport = %p\n", 
		path, path->id, path->port);)

	/* If this is a visible path, and there is not already a
	 * visible path, save it as the visible path.  If there
	 * is already a visible path, log an error and make this
	 * path invisible.
	 */
	if (!(path->mp_byte & (MP_MASK_HIDDEN | MP_MASK_UNCONFIGURED))) {

		/* No known visible path */
		if (path_list->visible == PATH_INDEX_INVALID) {
			DEBUG3(printk("%s: No know visible path - make this "
			    "path visible\n",
			    __func__);)
				
			path_list->visible = path->id;
			path->mp_byte &= ~MP_MASK_HIDDEN;
		} else {
			DEBUG3(printk("%s: Second visible path found- make "
			    "this one hidden\n",
			    __func__);)

			path->mp_byte |= MP_MASK_HIDDEN;
		}
		if (path->port)
			path->port->mp_byte = path->mp_byte;
	}

	/*
	 * If this is not the first path added, and the setting for
	 * MaxLunsPerTarget does not match that of the first path
	 * then disable qla_cfg for all adapters.
	 */
	first_path = qla4xxx_find_path_by_id(dp, 0);

	if (first_path != NULL) {
		first_host = first_path->host;
		if ((path->id != 0) &&
			(first_host->MaxLunsPerTarget !=
			 path->host->MaxLunsPerTarget)) {

			for (tmp_path = path_list->last, i = 0;
				(tmp_path) && i <= path->id; i++) {

				tmp_host = tmp_path->host;
				if (!(tmp_host->flags &
						MP_HOST_FLAG_DISABLE)) {

					DEBUG4(printk("%s: 2nd visible "
					    "path (%p)\n",
					    __func__, tmp_host);)

					tmp_host->flags |= MP_HOST_FLAG_DISABLE;
				}
			}
		}
	}

	if (!(fcport->flags & (FCF_MSA_DEVICE | FCF_EVA_DEVICE))) { 
		/*
		 * For each LUN, evaluate whether the new path that is added is
		 * better than the existing path.  If it is, make it the
		 * current path for the LUN.
		 */
		for (lun = 0; lun < MAX_LUNS_PER_DEVICE; lun++) {
			l = (uint8_t)(lun & 0xFF);

			/*
			 * If this is the first path added, it is the only
			 * available path, so make it the current path.
			 */
			DEBUG4(printk("%s: lun_data 0x%x, LUN %d\n",
			    __func__, path->lun_data.data[l], lun);)

			if (first_path == path) {
				path_list->current_path[l] = 0;
				path->lun_data.data[l] |=
				    LUN_DATA_PREFERRED_PATH;
			} else if (path->lun_data.data[l] &
			    LUN_DATA_PREFERRED_PATH) {
				/*
				 * If this is not the first path added, if this
				 * is the preferred path, so make it the
				 * current path.
				 */
				path_list->current_path[l] = path->id;
			}
		}
	}

	LEAVE("qla4xxx_setup_new_path");

	return;
}

/*
 * qla4xxx_cfg_mem_free
 *     Free all configuration structures.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Context:
 *      Kernel context.
 */
void
qla4xxx_cfg_mem_free(scsi_qla_host_t *ha)
{
	mp_lun_t        *cur_lun;
	mp_lun_t        *tmp_lun; 
	mp_device_t *dp;
	mp_path_list_t  *path_list;
	mp_path_t       *tmp_path, *path;
	mp_host_t       *host, *temp;
	mp_port_t	*temp_port;
	struct list_head *list, *temp_list;
	int	id, cnt;

	if ((host = qla4xxx_cfg_find_host(ha)) != NULL) {
		if( mp_num_hosts == 0 )
			return;

		for (id= 0; id < MAX_MP_DEVICES; id++) {
			if ((dp = host->mp_devs[id]) == NULL)
				continue;
			if ((path_list = dp->path_list) == NULL)
				continue;
			if ((tmp_path = path_list->last) == NULL)
				continue;
			for (cnt = 0; cnt < path_list->path_cnt; cnt++) {
				path = tmp_path;
				tmp_path = tmp_path->next;
				DEBUG(printk(KERN_INFO
						"host%d - Removing path[%d] "
						"= %p\n",
						host->instance,
						cnt, path);)
				kfree(path);
			}
			kfree(path_list);
			host->mp_devs[id] = NULL;
			/* remove dp from other hosts */
			for (temp = mp_hosts_base; (temp); temp = temp->next) {
				if (temp->mp_devs[id] == dp) {
					DEBUG(printk(KERN_INFO
						"host%d - Removing host[%d] = "
						"%p\n",
						host->instance,
						temp->instance,temp);)
					temp->mp_devs[id] = NULL;
				}
			}
			/* Free all the lun struc's attached 
			 * to this mp_device */
			for ( cur_lun = dp->luns; (cur_lun); 
					cur_lun = cur_lun->next) {
				DEBUG(printk(KERN_INFO
						"host%d - Removing lun:%p "
						"attached to device:%p\n",
						host->instance,
						cur_lun,dp);)
				list_for_each_safe(list, temp_list, 
					&cur_lun->ports_list) {
		
					temp_port = list_entry(list, mp_port_t, list);
					list_del_init(&temp_port->list);
				
					DEBUG(printk(KERN_INFO
						"host%d - Removing port:%p "
						"attached to lun:%p\n",
						host->instance, temp_port,
						cur_lun);)
	
				}
				tmp_lun = cur_lun;
				kfree(tmp_lun);
			}
			kfree(dp);
		}

		/* remove this host from host list */
		temp = mp_hosts_base;
		if (temp != NULL) {
			/* Remove from top of queue */
			if (temp == host) {
				mp_hosts_base = host->next;
			} else {
				/*
				 * Remove from middle of queue
				 * or bottom of queue
				 */
				for (temp = mp_hosts_base;
						temp != NULL;
						temp = temp->next) {

					if (temp->next == host) {
						temp->next = host->next;
						break;
					}
				}
			}
		}
		kfree(host);
		mp_num_hosts--;
	}
}

