/******************************************************************************
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
 ******************************************************************************/

/*
 * QLogic ISP2x00 Multi-path LUN Support Driver 
 * Solaris specific functions
 *
 */

#include "qla_os.h"
#include "qla_def.h"

#include "qlfo.h"

#define MAX_SEARCH_STR_SIZE	512

/*
 * qla2x00_set_lun_data_from_config
 * Set lun_data byte from the configuration parameters.
 *
 * Input:
 * host -- pointer to host adapter structure.
 * port -- pointer to port
 * tgt  -- target number
 * dev_no  -- device number
 */
void
qla2x00_set_lun_data_from_config(mp_host_t *host, fc_port_t *port,
    uint16_t tgt, uint16_t dev_no)
{
	char		*propbuf;  /* As big as largest search string */
	int		rval;
	int16_t		lun, l;
	scsi_qla_host_t *ha = host->ha;
	mp_device_t	*dp;
	lun_bit_mask_t	*plun_mask;
	lun_bit_mask_t  *mask_ptr;
	mp_path_list_t	*pathlist;
#if 0
	uint8_t		control_byte;
#endif

	mp_path_t *path;

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&propbuf,
	    MAX_SEARCH_STR_SIZE)) {
		/* not enough memory */
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "propbuf requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    MAX_SEARCH_STR_SIZE);)
		return;
	}

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&plun_mask,
	    sizeof(lun_bit_mask_t))) {
		/* not enough memory */
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "lun_mask requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(lun_bit_mask_t));)
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}
	mask_ptr = plun_mask;

	dp = host->mp_devs[tgt];
	if (dp == NULL) {
		printk("qla2x00_set_lun_data_from_config: Target %d "
		    "not found for hba %d\n",tgt, host->instance);
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}
	if ( (pathlist = dp->path_list) == NULL ) {
		printk("qla2x00_set_lun_data_from_config: path list "
		    "not found for target %d\n", tgt);
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}

	if ((path = qla2x00_find_path_by_name(host, pathlist,
	    port->port_name)) == NULL ) {
		printk("qla2x00_set_lun_data_from_config: No path found "
		    "for target %d\n", tgt);
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}

	/* Get "target-N-device-N-preferred" as a 256 bit lun_mask*/
	sprintf(propbuf, "scsi-qla%ld-tgt-%d-di-%d-preferred",
	    ha->instance, tgt, dev_no);
	DEBUG2(printk("build_tree: %s\n",propbuf);)

	rval = qla2x00_get_prop_xstr(ha, propbuf,
	    (uint8_t *)(plun_mask), sizeof(lun_bit_mask_t));

	if (rval == -1) {
		/* EMPTY */
		DEBUG2(printk("%s(%ld): no preferred mask entry found for "
		    "path id %d on port %02x%02x%02x%02x%02x%02x%02x%02x.\n",
		    __func__, ha->host_no, path->id,
		    path->portname[0], path->portname[1],
		    path->portname[2], path->portname[3],
		    path->portname[4], path->portname[5],
		    path->portname[6], path->portname[7]);)
	} else {
		if (rval != sizeof(lun_bit_mask_t)) {
			/* EMPTY */
			printk("qla2x00_set_lun_data_from_config: "
			    "Preferred mask len %d is incorrect.\n", rval);
		}

		DEBUG3(printk("%s(%ld): reading Preferred Mask for path id %d "
		    "on port %02x%02x%02x%02x%02x%02x%02x%02x:\n",
		    __func__, ha->host_no, path->id,
		    path->portname[0], path->portname[1],
		    path->portname[2], path->portname[3],
		    path->portname[4], path->portname[5],
		    path->portname[6], path->portname[7]);)
		DEBUG3(qla2x00_dump_buffer((char *)plun_mask,
		    sizeof(lun_bit_mask_t));)

		for (lun = MAX_LUNS-1, l =0; lun >= 0; lun--, l++ ) {
			if (EXT_IS_LUN_BIT_SET(mask_ptr, lun)) {
				path->lun_data.data[l] |=
				    LUN_DATA_PREFERRED_PATH;
				pathlist->current_path[l] = path->id;
			} else {
				path->lun_data.data[l] &=
				    ~LUN_DATA_PREFERRED_PATH;
			}
		}

	}

	/* Get "target-N-device-N-lun-disable" as a 256 bit lun_mask*/
	sprintf(propbuf, "scsi-qla%ld-tgt-%d-di-%d-lun-disabled",
	    ha->instance, tgt, dev_no);
	DEBUG3(printk("build_tree: %s\n",propbuf);)

	rval = qla2x00_get_prop_xstr(ha, propbuf,
	    (uint8_t *)plun_mask, sizeof(lun_bit_mask_t));
	if (rval == -1) {
		/* default: all luns enabled */
		DEBUG3(printk("%s(%ld): no entry found for path id %d. "
		    "Assume all LUNs enabled on port %02x%02x%02x%02x%02x%"
		    "02x%02x%02x.\n",
		    __func__, ha->host_no, path->id,
		    path->portname[0], path->portname[1],
		    path->portname[2], path->portname[3],
		    path->portname[4], path->portname[5],
		    path->portname[6], path->portname[7]);)

		for (lun = 0; lun < MAX_LUNS; lun++) {
			path->lun_data.data[lun] |= LUN_DATA_ENABLED;
		}
	} else {
		if (rval != sizeof(lun_bit_mask_t)) {
			printk("qla2x00_set_lun_data_from_config: Enable "
			    "mask has wrong size %d != %ld\n",
			    rval, (ulong)sizeof(lun_bit_mask_t));
		} else {
			for (lun = MAX_LUNS-1, l =0; lun >= 0; lun--, l++) {
				/* our bit mask is inverted */
				if (!EXT_IS_LUN_BIT_SET(mask_ptr,lun))
					path->lun_data.data[l] |=
					    LUN_DATA_ENABLED;
				else
					path->lun_data.data[l] &=
					    ~LUN_DATA_ENABLED;
			}
			DEBUG3(printk("%s(%ld): got lun mask for path id %d "
			    "port %02x%02x%02x%02x%02x%02x%02x%02x:\n",
			    __func__, ha->host_no, path->id,
			    path->portname[0], path->portname[1],
			    path->portname[2], path->portname[3],
			    path->portname[4], path->portname[5],
			    path->portname[6], path->portname[7]);)
			DEBUG3(qla2x00_dump_buffer(
			    (uint8_t *)&path->lun_data.data[0], 64);)
		}
	}

	DEBUG3(printk("qla2x00_set_lun_data_from_config: Luns data for "
	    "device %p, instance %d, path id=%d\n",
	    dp,host->instance,path->id);)
	DEBUG3(qla2x00_dump_buffer((char *)&path->lun_data.data[0], 64);)

	qla2x00_free_ioctl_scrap_mem(ha);
	LEAVE("qla2x00_set_lun_data_from_config");
}



/*
 * qla2x00_cfg_build_path_tree
 *	Find all path properties and build a path tree. The
 *  resulting tree has no actual port assigned to it
 *  until the port discovery is done by the lower level.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_cfg_build_path_tree(scsi_qla_host_t *ha)
{
	char		*propbuf;
	uint8_t		node_name[WWN_SIZE];
	uint8_t		port_name[WWN_SIZE];
	fc_port_t	*port;
	uint16_t	dev_no = 0, tgt;
	int		instance, rval;
	mp_host_t	*host = NULL;
	uint8_t		*name;
	int		done;
	uint8_t         control_byte;


	ENTER("qla2x00_cfg_build_path_tree");

	printk(KERN_INFO
	    "qla02%d: ConfigRequired is set. \n", (int)ha->instance);
	DEBUG(printk("qla2x00_cfg_build_path_tree: hba =%d",
	    (int)ha->instance);)

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&propbuf,
	    MAX_SEARCH_STR_SIZE)) {
		/* not enough memory */
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "propbuf requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    MAX_SEARCH_STR_SIZE);)
		return;
	}

	/* Look for adapter nodename in properties */
	sprintf(propbuf, "scsi-qla%ld-adapter-port", ha->instance);
	DEBUG(printk("build_tree: %s\n",propbuf);)

	rval = qla2x00_get_prop_xstr(ha, propbuf, port_name, WWN_SIZE);
	if (rval != WWN_SIZE) {
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}

	/* Does nodename match the host adapter nodename? */
	name = 	&ha->init_cb->port_name[0];
	if (!qla2x00_is_nodename_equal(name, port_name)) {
		printk(KERN_INFO
		    "scsi(%d): Adapter nodenames don't match - ha = %p.\n",
		    (int)ha->instance,ha);
		DEBUG(printk("qla(%d): Adapter nodenames don't match - "
		    "ha=%p. port name=%02x%02x%02x%02x%02x%02x%02x%02x\n",
		    (int)ha->instance,ha,
		    name[0], name[1], name[2], name[3],
		    name[4], name[5], name[6], name[7]);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return;
	}

	DEBUG(printk("%s: found entry for adapter port %02x%02x%02x%02x"
	    "%02x%02x%02x%02x.\n",
	    __func__,
	    port_name[0], port_name[1], port_name[2],
	    port_name[3], port_name[4], port_name[5],
	    port_name[6], port_name[7]);)

	instance = ha->instance;
	if ((host = qla2x00_alloc_host(ha)) == NULL) {
		printk(KERN_INFO
		    "scsi(%d): Couldn't allocate host - ha = %p.\n",
		    (int)instance,ha);
	} else {
		/* create a dummy port */
		port = (fc_port_t *)KMEM_ZALLOC(sizeof (fc_port_t),9);
		if (port == NULL) {
			printk(KERN_INFO
			    "scsi(%d): Couldn't allocate port.\n",
			    (int)instance);
			DEBUG(printk("qla(%d): Couldn't allocate port.\n",
			    (int)host->instance);)
			/* remove host */
			qla2x00_free_ioctl_scrap_mem(ha);
			return;
		}

		done = 0;

		/* For each target on the host bus adapter */
		for (tgt= 0; tgt< MAX_MP_DEVICES && !done; tgt++) {

			/* get all paths for this target */
			for (dev_no = 0; dev_no < MAX_PATHS_PER_DEVICE &&
			    !done ; dev_no++) {

				/*
				 * O(N*M) scan, should ideally check if there
				 * are any tgt entries present, if not, then
				 * continue.
				 *
				 *   sprintf(propbuf,
				 * 		"scsi-qla%d-tgt-%d-",
				 *		instance, tgt_no);
				 *   if (strstr(ha->cmdline, propbuf) == NULL)
				 *	continue;
				 *
				 */
				memset(port, 0, sizeof (fc_port_t));

				/*
				 * Get "target-N-device-N-node" is a 16-chars
				 * number
				 */
				sprintf(propbuf, "scsi-qla%d-tgt-%d-di-%d-node",
				    instance, tgt, dev_no);
				DEBUG(printk("build_tree: %s\n",propbuf);)

				rval = qla2x00_get_prop_xstr(ha, propbuf,
				    node_name, WWN_SIZE);
				if (rval != WWN_SIZE)
					/*
					 * di values may not be contiguous for
					 * override case.
					 */
					continue;

				memcpy(port->node_name, node_name, WWN_SIZE);

				/*
				 * Get "target-N-device-N-port" is a 16-chars
				 * number
				 */
				sprintf(propbuf, "scsi-qla%d-tgt-%d-di-%d-port",
				    instance, tgt, dev_no);
				DEBUG(printk("build_tree: %s\n",propbuf);)

				rval = qla2x00_get_prop_xstr(ha, propbuf,
				    port_name, WWN_SIZE);
				if (rval != WWN_SIZE)
					continue;

				memcpy(port->node_name, node_name, WWN_SIZE);
				memcpy(port->port_name, port_name, WWN_SIZE);
				port->flags |= FCF_CONFIG;

				/*
				 * Get "target-N-device-N-control" if property 
				 * is present then all luns are visible.
				 */
				sprintf(propbuf,
				    "scsi-qla%d-tgt-%d-di-%d-control",
				    instance, tgt, dev_no);
				DEBUG3(printk("build_tree: %s\n",propbuf);)

				rval = qla2x00_get_prop_xstr(ha, propbuf,
				    (uint8_t *)(&control_byte),
				    sizeof(control_byte));
				if (rval == -1) {
					/* error getting string. go to next. */
					DEBUG2(printk(
					    "%s: string parsing failed.\n",
					    __func__);)
					continue;
				}

				DEBUG(printk("build_tree: control byte 0x%x\n",
				    control_byte);)

				port->mp_byte = control_byte;
				DEBUG(printk("%s(%ld): calling update_mp_device"
				    " for host %p port %p-%02x%02x%02x%02x%02x"
				    "%02x%02x%02x tgt=%d mpbyte=%02x.\n",
				    __func__, ha->host_no, host, port,
				    port->port_name[0], port->port_name[1],
				    port->port_name[2], port->port_name[3],
				    port->port_name[4], port->port_name[5],
				    port->port_name[6], port->port_name[7],
				    tgt, port->mp_byte);)

				qla2x00_update_mp_device(host,
				    port, tgt, dev_no);
				qla2x00_set_lun_data_from_config(host,
				    port, tgt, dev_no);
			}
		}
		KMEM_FREE(port, sizeof (fc_port_t));
	}

	qla2x00_free_ioctl_scrap_mem(ha);

	LEAVE("qla2x00_cfg_build_path_tree");
	DEBUG(printk("Leaving: qla2x00_cfg_build_path_tree\n");)
}

/*
 * qla2x00_cfg_display_devices
 *      This routine will the node names of the different devices found
 *      after port inquiry.
 *
 * Input:
 *
 * Returns:
 *      None.
 */
void qla2x00_cfg_display_devices(void)
{
	mp_host_t     *host;
	int     id;
	mp_device_t	*dp;
	mp_path_t  *path;
	mp_path_list_t	*path_list;
	int cnt, i, dev_no;
	int instance;
	lun_bit_mask_t	lun_mask;
	int	mask_set;
	uint8_t	l;

	printk("qla2x00_cfg_display_devices\n");
	for (host = mp_hosts_base; (host); host = host->next) {

		instance = (int) host->instance;
		/* Display the node name for adapter */
		printk(KERN_INFO
			"scsi-qla%d-adapter-port="
			"%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
			instance,
			host->portname[0],
			host->portname[1],
			host->portname[2],
			host->portname[3],
			host->portname[4],
			host->portname[5],
			host->portname[6],
			host->portname[7]);

		for (id = 0; id < MAX_MP_DEVICES; id++) {
			if( (dp = host->mp_devs[id] ) == NULL )
				continue;

			path_list = dp->path_list;


			if( (path = path_list->last) != NULL ) {
				/* Print out device port names */
				path = path->next; /* first path */
				for (dev_no = 0,  cnt = 0;
					cnt < path_list->path_cnt;
					path = path->next, cnt++) {

					/* skip others if not our host */
					if (host != path->host)
						continue;
					printk(KERN_INFO
						"scsi-qla%d-tgt-%d-di-%d-node="
						"%02x%02x%02x%02x"
						"%02x%02x%02x%02x\\;\n",
						instance, id, path->id,
						dp->nodename[0],
						dp->nodename[1],
						dp->nodename[2],
						dp->nodename[3],
						dp->nodename[4],
						dp->nodename[5],
						dp->nodename[6],
						dp->nodename[7]);

					/* port_name */
					printk(KERN_INFO
						"scsi-qla%d-tgt-%d-di-%d-port="
						"%02x%02x%02x%02x"
						"%02x%02x%02x%02x\\;\n",
						instance, id, path->id,
						path->portname[0],
						path->portname[1],
						path->portname[2],
						path->portname[3],
						path->portname[4],
						path->portname[5],
						path->portname[6],
						path->portname[7]);

					/* control byte */
					printk(KERN_INFO
						"scsi-qla%d-tgt-%d-di-%d-"
						"control=%02x\\;\n",
						instance, id, path->id,
						path->mp_byte);

					/*
					 * Build preferred bit mask for this
					 * path */
					memset(&lun_mask, 0, sizeof(lun_mask));
					mask_set = 0;
					for (i = 0; i < MAX_LUNS; i++) {
						l = (uint8_t)(i & 0xFF);
						if (path_list->current_path[l] == path->id ) {
							EXT_SET_LUN_BIT((&lun_mask),l);
							mask_set++;
						}
					}
					if (mask_set) {
						printk(KERN_INFO
							"scsi-qla%d-tgt-%d-di-%d-preferred=%08x%08x%08x%08x%08x%08x%08x%08x\\;\n",
							instance,  id, path->id,
							*((uint32_t *) &lun_mask.mask[28]),
							*((uint32_t *) &lun_mask.mask[24]),
							*((uint32_t *) &lun_mask.mask[20]),
							*((uint32_t *) &lun_mask.mask[16]),
							*((uint32_t *) &lun_mask.mask[12]),
							*((uint32_t *) &lun_mask.mask[8]),
							*((uint32_t *) &lun_mask.mask[4]),
							*((uint32_t *) &lun_mask.mask[0]) );
					}
					/*
					 * Build disable bit mask for this path
					 */
					mask_set = 0;
					for (i = 0; i < MAX_LUNS; i++) {
						l = (uint8_t)(i & 0xFF);
						if (!(path->lun_data.data[l] &
							LUN_DATA_ENABLED) ) {

							mask_set++;
						}
					}
					if (mask_set) {
						printk(KERN_INFO
							"scsi-qla%d-tgt-%d-di-%d-lun-disable=%08x%08x%08x%08x%08x%08x%08x%08x\\;\n",
							instance,  id, path->id,
							*((uint32_t *) &lun_mask.mask[28]),
							*((uint32_t *) &lun_mask.mask[24]),
							*((uint32_t *) &lun_mask.mask[20]),
							*((uint32_t *) &lun_mask.mask[16]),
							*((uint32_t *) &lun_mask.mask[12]),
							*((uint32_t *) &lun_mask.mask[8]),
							*((uint32_t *) &lun_mask.mask[4]),
							*((uint32_t *) &lun_mask.mask[0]) );
					}
					dev_no++;
				}

			}
		}
	}
}

#if 0
int qla2x00_cfg_build_range( mp_path_t *path, uint8_t *buf, int siz, uint8_t mask )
{
        int 	i;
        int	max, min;
        int	colonflg = FALSE;
        int	len = 0;

        max = -1;
        min = 0;
        for (i = 0; i < MAX_LUNS; i++) {
                if( (path->lun_data.data[i] & mask) ) {
                        max = i;
                } else {
                        if( colonflg && max >= min ) {
                                len += sprintf(&buf[len],":");
                                if( len > siz)
                                        return len;
                                colonflg = FALSE;
                        }
                        if (max > min ) {
                                len += sprintf(&buf[len],"%02x-%02x",min,max);
                                if( len > siz)
                                        return len;
                                colonflg = TRUE;
                        } else if ( max == min ) {
                                len += sprintf(&buf[len],"%02x",max);
                                if( len > siz)
                                        return len;
                                colonflg = TRUE;
                        }
                        min = i + 1;
                        max = i;
                }
        }
        DEBUG4(printk("build_range: return len =%d\n",len);)
        return(len);
}
#endif

#if 0
/*
 * qla2x00_cfg_proc_display_devices
 *      This routine will the node names of the different devices found
 *      after port inquiry.
 *
 * Input:
 *
 * Returns:
 *      None.
 */
int qla2x00_cfg_proc_display_devices(scsi_qla_host_t *ha)
{
        mp_host_t     *host;
        int     id;
        mp_device_t	*dp;
        mp_path_t  *path;
        mp_path_list_t	*path_list;
        int cnt, i;
        int instance;
        lun_bit_mask_t	lun_mask;
        int	mask_set;
        uint8_t	l;
        fc_port_t 	*port;
        int len = 0;

        for (host = mp_hosts_base; (host); host = host->next) {

                if( host->ha != ha )
                        continue;

                instance = (int) host->instance;

                /* Display the node name for adapter */
                len += sprintf(PROC_BUF,
                                "scsi-qla%d-adapter-node="
				"%02x%02x%02x%02x%02x%02x%02x%02x;\n",
                                instance,
                                host->nodename[0],
                                host->nodename[1],
                                host->nodename[2],
                                host->nodename[3],
                                host->nodename[4],
                                host->nodename[5],
                                host->nodename[6],
                                host->nodename[7]);


                for (id = 0; id < MAX_MP_DEVICES; id++) {
                        if( (dp = host->mp_devs[id] ) == NULL )
                                continue;

                        path_list = dp->path_list;

                        if( (path = path_list->last) != NULL ) {
                                /* Print out device port names */
                                path = path->next; /* first path */
                                for (cnt = 0; cnt < path_list->path_cnt; path = path->next, cnt++) {
                                        /* skip others if not our host */
                                        if (host != path->host)
                                                continue;
                                        len += sprintf(PROC_BUF,
                                                       "scsi-qla%d-target-%d-path-%d-node=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
                                                       instance,  id, path->id,
                                                       dp->nodename[0],
                                                       dp->nodename[1],
                                                       dp->nodename[2],
                                                       dp->nodename[3],
                                                       dp->nodename[4],
                                                       dp->nodename[5],
                                                       dp->nodename[6],
                                                       dp->nodename[7]);

                                        /* port_name */
                                        len += sprintf(PROC_BUF,
                                                       "scsi-qla%d-target-%d-path-%d-port=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
                                                       instance,  id, path->id,
                                                       path->portname[0],
                                                       path->portname[1],
                                                       path->portname[2],
                                                       path->portname[3],
                                                       path->portname[4],
                                                       path->portname[5],
                                                       path->portname[6],
                                                       path->portname[7]);

                                        if( path_list->visible == path->id ) {
                                                len += sprintf(PROC_BUF, "scsi-qla%d-target-%d-path-%d-visible=%02x;\n",
                                                               instance,  id, path->id, path->id);
                                        }

                                        len +=sprintf(PROC_BUF, "scsi-qla%d-target-%d-path-%d-control=%02x;\n",
                                                      instance,  id, path->id, path->mp_byte);

                                        /* Build preferred bit mask for this path */
                                        memset(&lun_mask, 0, sizeof(lun_mask));
                                        mask_set = 0;
                                        for (i = 0; i < MAX_LUNS_PER_DEVICE; i++) {
                                                l = (uint8_t)(i & 0xFF);
                                                if( path_list->current_path[l] == path->id ) {
                                                        EXT_SET_LUN_BIT((&lun_mask),l);
                                                        mask_set++;
                                                }
                                        }
                                        if( mask_set && EXT_DEF_MAX_LUNS <= 256 ) {
                                                len += sprintf(PROC_BUF,
                                                               "scsi-qla%d-target-%d-path-%d-preferred=%08x%08x%08x%08x%08x%08x%08x%08x;\n",
                                                               instance,  id, path->id,
                                                               *((uint32_t *) &lun_mask.mask[0]),
                                                               *((uint32_t *) &lun_mask.mask[4]),
                                                               *((uint32_t *) &lun_mask.mask[8]),
                                                               *((uint32_t *) &lun_mask.mask[12]),
                                                               *((uint32_t *) &lun_mask.mask[16]),
                                                               *((uint32_t *) &lun_mask.mask[20]),
                                                               *((uint32_t *) &lun_mask.mask[24]),
                                                               *((uint32_t *) &lun_mask.mask[28]) );
                                        }

                                        len += sprintf(PROC_BUF,
                                                       "scsi-qla%d-target-%d-path-%d-lun-enable=%08x%08x%08x%08x%08x%08x%08x%08x;\n",
                                                       instance,  id, path->id,
                                                       *((uint32_t *) &path->lun_data.data[0]),
                                                       *((uint32_t *) &path->lun_data.data[4]),
                                                       *((uint32_t *) &path->lun_data.data[8]),
                                                       *((uint32_t *) &path->lun_data.data[12]),
                                                       *((uint32_t *) &path->lun_data.data[16]),
                                                       *((uint32_t *) &path->lun_data.data[20]),
                                                       *((uint32_t *) &path->lun_data.data[24]),
                                                       *((uint32_t *) &path->lun_data.data[28]) );

                                } /* for */
                        }
                }
        }
        return( len );
}
#endif
