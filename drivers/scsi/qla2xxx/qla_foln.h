/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
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

#ifndef __QLA_FOLN_H
#define	__QLA_FOLN_H

#if defined(CONFIG_SCSI_QLA2XXX_FAILOVER)

#include "exioct.h"
#include "qla_fo.h"
#include "qla_cfg.h"

// Inbound or Outbound tranfer of data
#define QLA2X00_UNKNOWN  0
#define QLA2X00_READ	1
#define QLA2X00_WRITE	2

/* 
 * Device configuration table
 *
 * This table provides a library of information about the device
 */
struct cfg_device_info {
	const char *vendor;
	const char *model;
	const int  flags;	/* bit 0 (0x1) -- translate the real 
				   WWNN to the common WWNN for the target AND
				   XP_DEVICE */
				/* bit 1 (0x2) -- MSA 1000  */
				/* bit 2 (0x4) -- EVA  */
				/* bit 3 (0x8) -- DISABLE FAILOVER  */
	const int  notify_type;	/* support the different types: 1 - 4 */
	int	( *fo_combine)(void *,
		 uint16_t, fc_port_t *, uint16_t );
	int	( *fo_detect)(void);
	int	( *fo_notify)(void);
	int	( *fo_select)(void);
};

#define VITAL_PRODUCT_DATA_SIZE 32
#define INQ_EVPD_SET	1
#define INQ_DEV_IDEN_PAGE  0x83  	
#define WWLUN_SIZE	32	

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
	} p;
	uint8_t inq[VITAL_PRODUCT_DATA_SIZE];
} evpd_inq_cmd_rsp_t;

typedef struct {
	union {
		cmd_a64_entry_t cmd;
		sts_entry_t rsp;
	} p;
} tur_cmd_rsp_t;

/*
 * Global Data in qla_fo.c source file.
 */
extern SysFoParams_t qla_fo_params;

/*
 * Global Function Prototypes in qla_fo.c source file.
 */
extern scsi_qla_host_t *qla2x00_get_hba(unsigned long);
extern uint32_t qla2x00_send_fo_notification(fc_lun_t *fclun_p, fc_lun_t *olun_p);
extern void qla2x00_fo_init_params(scsi_qla_host_t *ha);
extern uint8_t qla2x00_fo_enabled(scsi_qla_host_t *ha, int instance);
extern int qla2x00_fo_ioctl(scsi_qla_host_t *, int, EXT_IOCTL *, int);

extern int qla2x00_fo_missing_port_summary(scsi_qla_host_t *,
    EXT_DEVICEDATAENTRY *, void *, uint32_t, uint32_t *, uint32_t *);

/*
 * Global Data in qla_cfg.c source file.
 */
extern mp_host_t *mp_hosts_base;
extern int mp_config_required;

/*
 * Global Function Prototypes in qla_cfg.c source file.
 */

extern mp_device_t *qla2x00_find_mp_dev_by_portname(mp_host_t *, uint8_t *,
    uint16_t *);
extern mp_host_t *qla2x00_cfg_find_host(scsi_qla_host_t *);
extern int qla2x00_is_portname_in_device(mp_device_t *, uint8_t *);
extern int qla2x00_cfg_path_discovery(scsi_qla_host_t *);
extern int qla2x00_cfg_event_notify(scsi_qla_host_t *, uint32_t);
extern fc_lun_t *qla2x00_cfg_failover(scsi_qla_host_t *, fc_lun_t *,
    os_tgt_t *, srb_t *);
extern int qla2x00_cfg_get_paths(EXT_IOCTL *, FO_GET_PATHS *, int);
extern int qla2x00_cfg_set_current_path(EXT_IOCTL *, FO_SET_CURRENT_PATH *,
    int);
extern void qla2x00_fo_properties(scsi_qla_host_t *);
extern mp_host_t *qla2x00_add_mp_host(uint8_t *);
extern mp_host_t *qla2x00_alloc_host(scsi_qla_host_t *);
extern uint8_t qla2x00_fo_check(scsi_qla_host_t *ha, srb_t *);
extern mp_path_t *qla2x00_find_path_by_name(mp_host_t *, mp_path_list_t *,
    uint8_t *);

extern int __qla2x00_is_fcport_in_config(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_cfg_init(scsi_qla_host_t *);
extern void qla2x00_cfg_mem_free(scsi_qla_host_t *);

extern int qla2x00_cfg_remap(scsi_qla_host_t *);
extern void qla2x00_set_device_flags(scsi_qla_host_t *, fc_port_t *);

extern int16_t qla2x00_cfg_lookup_device(unsigned char *);
extern int qla2x00_combine_by_lunid(void *, uint16_t, fc_port_t *, uint16_t); 
extern int qla2x00_export_target(void *, uint16_t, fc_port_t *, uint16_t); 

extern int qla2x00_test_active_lun(fc_port_t *, fc_lun_t *);
extern int qla2x00_test_active_port(fc_port_t *);

extern int qla2x00_is_fcport_in_foconfig(scsi_qla_host_t *, fc_port_t *);

/*
 * Global Function Prototypes in qla_cfgln.c source file.
 */
extern void qla2x00_cfg_build_path_tree( scsi_qla_host_t *ha);
extern uint8_t qla2x00_update_mp_device(mp_host_t *,
    fc_port_t  *, uint16_t, uint16_t);
extern void qla2x00_cfg_display_devices(int);


/*
 * Global Function Prototypes in qla_foln.c source file.
 */
extern int qla2x00_search_failover_queue(scsi_qla_host_t *, struct scsi_cmnd *);
extern void qla2x00_process_failover_event(scsi_qla_host_t *);
extern int qla2x00_do_fo_check(scsi_qla_host_t *, srb_t *, scsi_qla_host_t *);
extern void qla2xxx_start_all_adapters(scsi_qla_host_t *);

extern int ql2xfailover;
extern int ql2xrecoveryTime;
extern int ql2xfailbackTime;

extern int MaxPathsPerDevice;
extern int MaxRetriesPerPath;
extern int MaxRetriesPerIo;
extern int qlFailoverNotifyType;

extern struct cfg_device_info cfg_device_list[];

#define qla2x00_failover_enabled(ha)				(ql2xfailover)

#else

#define qla2x00_is_fcport_in_foconfig(ha, fcport)		(0)
#define qla2x00_fo_missing_port_summary(ha, e, s, m, c, r)	(0)
/* qla2x00_cfg_init() is declared int but the retval isn't checked.. */
#define qla2x00_cfg_init(ha)					do { } while (0)
#define qla2x00_cfg_mem_free(ha)				do { } while (0)
#define qla2x00_cfg_display_devices()				do { } while (0)
#define qla2x00_process_failover_event(ha)			do { } while (0)
#define qla2xxx_start_all_adapters(ha)				do { } while (0)
#define qla2x00_search_failover_queue(ha, cmd)			(0)
#define qla2x00_do_fo_check(ha, sp, vis_ha)			(0)
#define qla2x00_failover_enabled(ha)				(0)
#endif /* CONFIG_SCSI_QLA2XXX_FAILOVER */

static __inline int
qla2x00_is_fcport_in_config(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	if (qla2x00_failover_enabled(ha))
		return qla2x00_is_fcport_in_foconfig(ha, fcport);
	else if (fcport->flags & FCF_PERSISTENT_BOUND)
		return 1;
	return 0;
}


#endif /* __QLA_FOLN_H */
