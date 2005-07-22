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

#ifndef __QLA_FOLN_H
#define	__QLA_FOLN_H

#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER

// #include "exioct.h"
#include "ql4_fo.h"
#include "ql4_cfg.h"
#include "ql4_fw.h"

/*
 * Inquiry command structure.
 */
#define	INQ_DATA_SIZE	36

typedef struct {
	union {
		COMMAND_T3_ENTRY cmd;
		STATUS_ENTRY rsp;
	} p;
	uint8_t inq[INQ_DATA_SIZE];
} inq_cmd_rsp_t;

/*
 * Report LUN command structure.
 */
#define RPT_LUN_SCSI_OPCODE	0xA0
#define CHAR_TO_SHORT(a, b)	(uint16_t)((uint8_t)b << 8 | (uint8_t)a)

typedef struct {
	uint32_t	len;
	uint32_t	rsrv;
} rpt_hdr_t;

typedef struct {
	struct {
		uint8_t		b : 6;
		uint8_t		address_method : 2;
	} msb;
	uint8_t		lsb;
	uint8_t		unused[6];
} rpt_lun_t;

typedef struct {
	rpt_hdr_t	hdr;
	rpt_lun_t	lst[MAX_LUNS];
} rpt_lun_lst_t;

typedef struct {
	union {
		COMMAND_T3_ENTRY cmd;
		STATUS_ENTRY rsp;
	} p;
	rpt_lun_lst_t list;
} rpt_lun_cmd_rsp_t;


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


typedef struct {
	union {
		COMMAND_T3_ENTRY cmd;
		STATUS_ENTRY rsp;
	} p;
	uint8_t inq[VITAL_PRODUCT_DATA_SIZE];
} evpd_inq_cmd_rsp_t;

typedef struct {
	union {
		COMMAND_T3_ENTRY cmd;
		STATUS_ENTRY rsp;
	} p;
} tur_cmd_rsp_t;


#define SDM_DEF_MAX_DEVICES		16
#define SDM_DEF_MAX_PATHS_PER_TARGET	4
#define SDM_DEF_MAX_TARGETS_PER_DEVICE	4
#define SDM_DEF_MAX_PATHS_PER_DEVICE (SDM_DEF_MAX_PATHS_PER_TARGET * SDM_DEF_MAX_TARGETS_PER_DEVICE)

#define FO_MAX_LUNS_PER_DEVICE	MAX_LUNS_OS
#define FO_MAX_PATHS (SDM_DEF_MAX_PATHS_PER_DEVICE * SDM_DEF_MAX_DEVICES)
#define FO_MAX_ADAPTERS		32
#define FO_ADAPTER_ALL		0xFF
#define FO_DEF_WWN_SIZE             8
#define FO_MAX_GEN_INFO_STRING_LEN  32

/*
 * Global Data in qla_fo.c source file.
 */

/*
 * Global Function Prototypes in qla_fo.c source file.
 */
extern scsi_qla_host_t *qla4xxx_get_hba(unsigned long);
extern uint32_t qla4xxx_send_fo_notification(fc_lun_t *fclun_p, fc_lun_t *olun_p);
extern void qla4xxx_fo_init_params(scsi_qla_host_t *ha);
extern uint8_t qla4xxx_fo_enabled(scsi_qla_host_t *ha, int instance);
//extern int qla4xxx_fo_ioctl(scsi_qla_host_t *, int, EXT_IOCTL *, int);

/*
 * Global Data in qla_cfg.c source file.
 */
extern mp_host_t *mp_hosts_base;
extern int mp_config_required;

/*
 * Global Function Prototypes in qla_cfg.c source file.
 */

extern mp_host_t *qla4xxx_cfg_find_host(scsi_qla_host_t *);
extern int qla4xxx_is_iscsiname_in_device(mp_device_t *, uint8_t *);
extern int qla4xxx_cfg_path_discovery(scsi_qla_host_t *);
extern int qla4xxx_cfg_event_notify(scsi_qla_host_t *, uint32_t);
extern fc_lun_t *qla4xxx_cfg_failover(scsi_qla_host_t *, fc_lun_t *,
    os_tgt_t *, srb_t *);
extern void qla4xxx_fo_properties(scsi_qla_host_t *);
extern mp_host_t *qla4xxx_add_mp_host(uint8_t *);
extern mp_host_t *qla4xxx_alloc_host(scsi_qla_host_t *);
extern uint8_t qla4xxx_fo_check(scsi_qla_host_t *ha, srb_t *);
extern mp_path_t *qla4xxx_find_path_by_name(mp_host_t *, mp_path_list_t *,
    uint8_t *);

extern int __qla4xxx_is_fcport_in_config(scsi_qla_host_t *, fc_port_t *);
extern int qla4xxx_cfg_init(scsi_qla_host_t *);
extern void qla4xxx_cfg_mem_free(scsi_qla_host_t *);

extern int qla4xxx_cfg_remap(scsi_qla_host_t *);
extern void qla4xxx_set_device_flags(scsi_qla_host_t *, fc_port_t *);

extern int16_t qla4xxx_cfg_lookup_device(unsigned char *);
extern int qla4xxx_combine_by_lunid(void *, uint16_t, fc_port_t *, uint16_t); 
extern int qla4xxx_export_target(void *, uint16_t, fc_port_t *, uint16_t); 

extern int qla4xxx_test_active_lun(fc_port_t *, fc_lun_t *);
extern int qla4xxx_test_active_port(fc_port_t *);

extern int qla4xxx_is_fcport_in_foconfig(scsi_qla_host_t *, fc_port_t *);

/*
 * Global Function Prototypes in qla_cfgln.c source file.
 */
extern void qla4xxx_cfg_build_path_tree( scsi_qla_host_t *ha);
extern uint8_t qla4xxx_update_mp_device(mp_host_t *,
    fc_port_t  *, uint16_t, uint16_t);
extern void qla4xxx_cfg_display_devices(int);


/*
 * Global Function Prototypes in qla_foln.c source file.
 */
extern int qla4xxx_search_failover_queue(scsi_qla_host_t *, struct scsi_cmnd *);
extern void qla4xxx_process_failover_event(scsi_qla_host_t *);
extern int qla4xxx_do_fo_check(scsi_qla_host_t *, srb_t *, scsi_qla_host_t *);
extern void qla4xxx_start_all_adapters(scsi_qla_host_t *);

extern int ql4xfailover;
extern int ql4xrecoveryTime;
extern int ql4xfailbackTime;

extern int MaxPathsPerDevice;
extern int MaxRetriesPerPath;
extern int MaxRetriesPerIo;
extern int qlFailoverNotifyType;

extern struct cfg_device_info cfg_device_list[];

#define qla4xxx_failover_enabled(ha)				(ql4xfailover)

#else

#define qla4xxx_is_fcport_in_foconfig(ha, fcport)		(0)
#define qla4xxx_fo_missing_port_summary(ha, e, s, m, c, r)	(0)
/* qla4xxx_cfg_init() is declared int but the retval isn't checked.. */
#define qla4xxx_cfg_init(ha)					do { } while (0)
#define qla4xxx_cfg_mem_free(ha)				do { } while (0)
#define qla4xxx_cfg_display_devices()				do { } while (0)
#define qla4xxx_process_failover_event(ha)			do { } while (0)
#define qla4xxx_start_all_adapters(ha)				do { } while (0)
#define qla4xxx_search_failover_queue(ha, cmd)			(0)
#define qla4xxx_do_fo_check(ha, sp, vis_ha)			(0)
#define qla4xxx_failover_enabled(ha)				(0)
#endif /* CONFIG_SCSI_QLA2XXX_FAILOVER */

static __inline int
qla4xxx_is_fcport_in_config(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	if (qla4xxx_failover_enabled(ha))
		return qla4xxx_is_fcport_in_foconfig(ha, fcport);
	else if (fcport->flags & FCF_PERSISTENT_BOUND)
		return 1;
	return 0;
}


#endif /* __QLA_FOLN_H */
