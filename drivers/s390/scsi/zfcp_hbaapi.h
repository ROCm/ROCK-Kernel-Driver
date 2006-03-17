/*
 * $Id: linux-2.6.12-zfcp_hbaapi.patch,v 1.4 2005/08/23 20:04:56 aherrman Exp $
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * interface for HBA API (FC-HBA)
 *
 * (C) Copyright IBM Corp. 2003, 2004
 *
 * Authors:
 *       Stefan Voelkel <Stefan.Voelkel@millenux.com>
 *       Andreas Herrmann <aherrman@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ZFCP_HBAAPI_H
#define ZFCP_HBAAPI_H


#include <linux/ioctl.h>
#include <asm/string.h>
#include "zfcp_def.h"

/* Maximum number of events in the queues (shared and polled) */
#define ZH_EVENTS_MAX 20
#define ZH_GET_EVENT_BUFFER_COUNT 10

/*
 * Besides a punch of standard error codes we use some newly defined error
 * codes.
 */
#define ENOADA 200 /* no such adapter */
#define ENOPOR 201 /* no such port */
#define ENOUNI 202 /* no such unit */

typedef u64 devid_t;

#define DEVID_TO_BUSID(devid, busid) \
	memcpy(busid, devid, 8);
#define BUSID_TO_DEVID(busid, devid) \
	memcpy(devid, busid, 8);

/**
 * struct zfcp_adapter_attributes
 */
struct zfcp_adapter_attributes {
	char manufacturer[64];
	char serial_number[64];
	char model[256];
	char model_description[256];
	wwn_t node_wwn;
	char node_symbolic_name[256];
	char hardware_version[256];
	char driver_version[256];
	char option_rom_version[256];
	char firmware_version[256];
	u32 vendor_specific_id;
	u32 number_of_ports;
	char driver_name[256];
};

/**
 * struct zfcp_port_attributes
 */
struct zfcp_port_attributes {
	wwn_t wwnn;
	wwn_t wwpn;
	wwn_t fabric_name;
	u32 fcid;
	u32 type;
	u32 state;
	u32 supported_class_of_service;
	u32 supported_speed;
	u32 speed;
	u32 max_frame_size;
	u32 discovered_ports;
	u8 supported_fc4_types[32];
	u8 active_fc4_types[32];
	char symbolic_name[256];
};

/**
 * struct zfcp_port_statistics
 */
struct zfcp_port_statistics {
	u64 last_reset;
	u64 tx_frames;
	u64 tx_words;
	u64 rx_frames;
	u64 rx_words;
	u64 lip;
	u64 nos;
	u64 error_frames;
	u64 dumped_frames;
	u64 link_failure;
	u64 loss_of_sync;
	u64 loss_of_signal;
	u64 prim_seq_prot_error;
	u64 invalid_tx_words;
	u64 invalid_crc;
	u64 input_requests;
	u64 output_requests;
	u64 control_requests;
	u64 input_megabytes;
	u64 output_megabytes;
};

/* SPC-2 defines the additional_length field in the sense data format as a byte,
 * thus only 255 bytes of additional data may be returned. Size of header in
 * sense data format is 8 bytes.
 */
#define ZH_SCSI_SENSE_BUFFERSIZE   263

/* SCSI INQUIRY command (see SPC-2 for details) */
struct scsi_inquiry_cmd {
	u8 op;
	u8 reserved1:6;
	u8 cmdt:1;
	u8 evpd:1;
	u8 page_code;
	u8 reserved2;
	u8 alloc_length;
	u8 control;
} __attribute__((packed));

/* SCSI READ CAPACITY (10) command (see SBC-2) for details) */
struct scsi_read_capacity_cmd {
	u8 op;
	u8 reserved1:7;
	u8 reladdr:1;
	u32 lba;
	u16 reserved2;
	u8 reserved3:7;
	u8 pmi:1;
	u8 control;
} __attribute__((packed));

/* SCSI REPORT LUNSx command (see SPC-2 for details) */
struct scsi_report_luns_cmd {
	u8 op;
	u8 reserved1[5];
	u32 alloc_length;
	u8 reserved2;
	u8 control;
} __attribute__((packed));

/* minimum size of the response data for REPORT LUNS */
#define SCSI_REPORT_LUNS_SIZE_MIN 8

#ifndef REPORT_LUNS
#define REPORT_LUNS 0xA0
#endif

/**
 * struct zh_get_config - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @flags: specifies kind of configuration events
 *
 * Used with ioctl ZH_IOC_GET_CONFIG.
 *
 * No flags (or ZH_GET_CONFIG_ADAPTERS) causes generation of
 * adapter_add events.  Flag ZH_GET_CONFIG_PORT is used to generate
 * port_add events for the adapter specified with @devid.  Flag
 * ZH_GET_CONFIG_UNITS is used to generate unit_add events for the
 * port with @wwpn at adapter specified by @devid.
 */
struct zh_get_config {
	devid_t devid;
	wwn_t wwpn;
	unsigned int flags:2;
#define ZH_GET_CONFIG_ADAPTERS 0
#define ZH_GET_CONFIG_PORTS 1
#define ZH_GET_CONFIG_UNITS 2
};

/**
 * struct zh_get_adapterattributes - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @attributes: adapter attributes
 *
 * Used with ioctl ZH_IOC_GET_ADAPTERATTRIBUTES.
 */
struct zh_get_adapterattributes {
	devid_t devid;
	struct zfcp_adapter_attributes attributes;
};

/**
 * struct zh_get_portattributes - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote or local port (optional)
 * @attributes: port attributes
 *
 * Used with ioctl ZH_IOC_GET_PORTATTRIBUTES and ZH_IOC_GET_DPORTATTRIBUTES.
 */
struct zh_get_portattributes {
	devid_t devid;
	wwn_t wwpn;
	struct zfcp_port_attributes attributes;
};

/**
 * struct zh_get_portstatistics - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @portwwn: WWPN of local port (optional)
 * @stat: port statistics
 *
 * Used with ioctl ZH_IOC_GET_PORTSTATISTICS.
 */
struct zh_get_portstatistics {
	devid_t devid;
	wwn_t portwwn;
	struct zfcp_port_statistics statistics;
};

/**
 * struct zh_event_polled_link
 * @event: subtype of link event, see @zh_event_polled_link_e
 * @port_fc_id: port FC id, where this event occurred
 *
 * The standard defines an array of 3 u32 as reserved
 * in its structure. We do not need this here since no data
 * is passed with this member from kernel, to user-space
 */
struct zh_event_polled_link {
	u32 port_fc_id;
};

/**
 * struct zh_event_polled_rscn
 * @port_fc_id: port FC id, where this event occurred
 * @port_page: affected port_id pages
 *
 * The standard defines an array of 2 u32 as reserved
 * in its structure. We do not need this here since no data
 * is passed with this member from kernel, to user-space
 */
struct zh_event_polled_rscn {
	u32 port_fc_id;
	u32 port_page;
};

/**
 * struct zh_event_polled_pty
 * @pty_data: proprietary data
 */
struct zh_event_polled_pty {
	u32 pty_data[4];
};

/**
 * struct zh_event_polled
 * @event: type of occurred event
 * @data: union of different events
 * @link: link event, @see zh_event_polled_link
 * @rscn: rscn event, @see zh_event_polled_rscn
 * @pty: pty event, @see zh_event_polled_pty
 */
struct zh_event_polled {
	u32 event;
	devid_t devid;
	union {
		struct zh_event_polled_link link;
		struct zh_event_polled_rscn rscn;
		struct zh_event_polled_pty pty;
	} data;
};

/**
 * struct zh_get_event_buffer - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @count: maximum number of events that can be copied to user space
 * @polled: array of events
 *
 * Used with ioctl ZH_IOC_GET_EVENT_BUFFER.
 */
struct zh_get_event_buffer {
	devid_t devid;
	unsigned int count;
	struct zh_event_polled polled[ZH_GET_EVENT_BUFFER_COUNT];
};

/**
 * struct zh_event_adapter_add
 * @devid: unique id of adapter device
 * @wwnn: WWN of adapter node
 * @wwpn: WWN of adapter port
 *
 * structure used for adapter add events
 */
struct zh_event_adapter_add {
	devid_t	devid;
	wwn_t	wwnn;
	wwn_t	wwpn;
};

/**
 * struct zh_event_port_edd
 * @devid: unique id for adapter device
 * @wwpn: WWPN of new remote port
 * @wwnn: WWNN of new remote port
 * @did: DID of new remote port
 */
struct zh_event_port_add {
	devid_t	devid;
	wwn_t wwpn;
	wwn_t wwnn;
	u32 did;
};

/**
 * struct zh_event_unit_add
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port for new unit
 * @fclun: FCP LUN of new unit
 * @host: SCSI host id for new unit
 * @channel: SCSI channel for new unit
 * @id: SCSI id for new unit
 * @lun: SCSI lun for new unit
 */
struct zh_event_unit_add {
	devid_t	devid;
	wwn_t wwpn;
	fcp_lun_t fclun;
	unsigned int host;
	unsigned int channel;
	unsigned int id;
	unsigned int lun;
};

/**
 * struct zh_event - structure passed to ioctl()
 * @event: type of event
 * @data: event specific structure
 *
 * Used with ioctl ZH_IOC_EVENT.
 */
struct zh_event {
	u8 type;
	union {
		struct zh_event_polled polled;
		struct zh_event_adapter_add adapter_add;
		struct zh_event_port_add port_add;
		struct zh_event_unit_add unit_add;
	} data;
};

/* SPC-2 defines the additional_length field of the INQUIRY reply as a byte,
 * thus only 255 bytes of additional data may be returned. Size of header for
 * standard INQUIRY data is 5 bytes.
 */

#define ZH_SCSI_INQUIRY_SIZE 260

/**
 * struct zh_scsi_inquiry - data needed for an INQUIRY
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @fclun: FCP LUN of unit where the command is sent to
 * @evpd: flag to request EVPD
 * @page_code: page code specifying the EVPD format
 * @inquiry: buffer for response payload
 * @sense: buffer for sense data
 */
struct zh_scsi_inquiry {
	devid_t devid;
	wwn_t wwpn;
	u64 fclun;
	u8 evpd;
	u32 page_code;
	u8 inquiry[ZH_SCSI_INQUIRY_SIZE];
	u8 sense[ZH_SCSI_SENSE_BUFFERSIZE];
};

/* SBC-2 defines the READ CAPACITY data */
#define ZH_SCSI_READ_CAPACITY_SIZE 8

/**
 * struct zh_scsi_read_capacity - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @fclun: FCP LUN of unit where command is sent to
 * @read_capacity: buffer for response payload
 * @sense: buffer for sense data
 *
 * Used with ZH_IOC_SCSI_READ_CAPACITY.
 */
struct zh_scsi_read_capacity {
	devid_t devid;
	wwn_t wwpn;
	u64 fclun;
	u8 read_capacity[ZH_SCSI_READ_CAPACITY_SIZE];
	u8 sense[ZH_SCSI_SENSE_BUFFERSIZE];
};

/**
 * struct zh_scsi_report_luns - structure passed to ioctl()
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @rsp_buffer: buffer for response payload
 * @rsp_buffer_size: size of response buffer
 * @sense: buffer for sense data
 *
 * Used with ioctl ZH_IOC_SCSI_REPORT_LUNS.
 */
struct zh_scsi_report_luns {
	devid_t devid;
	wwn_t wwpn;
	void __user *rsp_buffer;
	u32 rsp_buffer_size;
	u8 sense[ZH_SCSI_SENSE_BUFFERSIZE];
} __attribute__((packed));

/* RNID request payload (see FC-FS for details) */
struct zfcp_ls_rnid {
	u8 code;
	u8 field[3];
	u8 node_id_format;
	u8 reserved[3];
} __attribute__((packed));

/* RLS request payload (see FC-FS for details) */
struct zfcp_ls_rls {
	u8 code;
	u8 field[3];
	u32 n_port_id;
} __attribute__((packed));

/* RPS request payload (see FC-FS for details) */
struct zfcp_ls_rps {
	u8 code;
	u8 reserved[2];
	u8 flag;
	u64 port_selection;
} __attribute__((packed));

/* common identification data in RNID accept payload (see FC-FS) */
struct zfcp_ls_rnid_common_id {
	u64 n_port_name;
	u64 node_name;
} __attribute__((packed));

/* general topology specific identification data in RNID accept payload */
struct zfcp_ls_rnid_general_topology_id {
	u8 vendor_unique[16];
	u32 associated_type;
	u32 physical_port_number;
	u32 nr_attached_nodes;
	u8 node_management;
	u8 ip_version;
	u16 port_number;
	u8 ip_address[16];
	u8 reserved[2];
	u16 vendor_specific;
} __attribute__((packed));

/* RNID accept payload (see FC-FS for details) */
struct zfcp_ls_rnid_acc {
	u8 code;
	u8 field[3];
	u8 node_id_format;
	u8 common_id_length;
	u8 reserved;
	u8 specific_id_length;
	struct zfcp_ls_rnid_common_id common_id;
	struct zfcp_ls_rnid_general_topology_id specific_id;
} __attribute__((packed));

/* link error status block in RLS accept payload */
struct zfcp_ls_rls_lesb {
	u32 link_failure_count;
	u32 loss_of_sync_count;
	u32 loss_of_signal_count;
	u32 prim_seq_prot_error;
	u32 invalid_trans_word;
	u32 invalid_crc_count;
} __attribute__((packed));

/* RLS accept payload (see FC-FS for details) */
struct zfcp_ls_rls_acc {
	u8 code;
	u8 field[3];
	struct zfcp_ls_rls_lesb lesb;
} __attribute__((packed));

/* RPS accept payload (see FC-FS for details) */
struct zfcp_ls_rps_acc {
	u8 code;
	u16 reserved1;
	u8 flag;
	u16 reserved2;
	u16 port_status;
	struct zfcp_ls_rls_lesb lesb;
	u32 lport_extension[8];
} __attribute__((packed));

/**
 * struct zh_get_rnid - retrieve RNID from adapter
 * @devid: unique id for adapter device
 * @payload: payload for RNID ELS
 */
struct zh_get_rnid {
	devid_t devid;
	struct zfcp_ls_rnid_acc payload;
};

/**
 * struct zh_send_rnid - send out an RNID ELS
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @size: to return size of accept payload
 * @payload: payload buffer
 */
struct zh_send_rnid {
	devid_t devid;
	wwn_t wwpn;
	u32 size;
	struct zfcp_ls_rnid_acc payload;
};

/**
 * struct zh_send_rls - send out an RLS ELS
 * @devid: unique id for adapter device
 * @wwpn: WWPN of remote port
 * @size: to return size of accept payload
 * @payload: payload buffer
 */
struct zh_send_rls {
	devid_t devid;
	wwn_t wwpn;
	u32 size;
	struct zfcp_ls_rls_acc payload;
};

/**
 * struct zh_send_rps - send out an RPS ELS
 * @devid: unique id for adapter device
 * @agent_wwpn: wwpn of agent
 * @domain: domain to identify domain controller
 * @objet_wwpn: wwpn of object
 * @port_number: physical port number
 * @size: to return size of accept payload
 * @payload: payload buffer
 */
struct zh_send_rps {
	devid_t devid;
	wwn_t agent_wwpn;
	u32 domain;
	wwn_t object_wwpn;
	u32 port_number;
	u32 size;
	struct zfcp_ls_rps_acc payload;
};

/**
 * struct zh_send_ct - send out a Generic Service command
 * @devid: unique id for adapter device
 * @req_length: size the request buffer
 * @req: request buffer
 * @resp_length: size of response buffer
 * @resp: response buffer
 *
 * Used with ioctl ZH_IOC_SEND_CT.
 */
struct zh_send_ct {
	devid_t devid;
	u32 req_length;
	void __user *req;
	u32 resp_length;
	void __user *resp;
} __attribute__((packed));

/* IOCTL's */
#define ZH_IOC_MAGIC 0xDD

#define ZH_IOC_GET_ADAPTERATTRIBUTES \
 _IOWR(ZH_IOC_MAGIC, 1, struct zh_get_adapterattributes)
#define ZH_IOC_GET_PORTATTRIBUTES \
 _IOWR(ZH_IOC_MAGIC, 2, struct zh_get_portattributes)
#define ZH_IOC_GET_PORTSTATISTICS \
 _IOWR(ZH_IOC_MAGIC, 3, struct zh_get_portstatistics)
#define ZH_IOC_GET_DPORTATTRIBUTES \
 _IOWR(ZH_IOC_MAGIC, 4, struct zh_get_portattributes)
#define ZH_IOC_GET_RNID _IOWR(ZH_IOC_MAGIC, 5, struct zh_get_rnid)
#define ZH_IOC_SEND_RNID _IOWR(ZH_IOC_MAGIC, 6, struct zh_send_rnid)
#define ZH_IOC_SEND_CT _IOWR(ZH_IOC_MAGIC, 7, struct zh_send_ct)
#define ZH_IOC_SCSI_INQUIRY _IOW(ZH_IOC_MAGIC, 8, struct zh_scsi_inquiry)
#define ZH_IOC_SCSI_READ_CAPACITY \
 _IOW(ZH_IOC_MAGIC, 9, struct zh_scsi_read_capacity)
#define ZH_IOC_SCSI_REPORT_LUNS \
 _IOW(ZH_IOC_MAGIC, 10, struct zh_scsi_report_luns)
#define ZH_IOC_GET_EVENT_BUFFER \
 _IOWR(ZH_IOC_MAGIC, 11, struct zh_get_event_buffer)
#define ZH_IOC_GET_CONFIG _IOW(ZH_IOC_MAGIC, 12, struct zh_get_config)
#define ZH_IOC_CLEAR_CONFIG _IO(ZH_IOC_MAGIC, 13)
#define ZH_IOC_EVENT_START _IO(ZH_IOC_MAGIC, 14)
#define ZH_IOC_EVENT_STOP _IO(ZH_IOC_MAGIC, 15)
#define ZH_IOC_EVENT _IOR(ZH_IOC_MAGIC, 16, struct zh_event)
#define ZH_IOC_EVENT_INSERT _IO(ZH_IOC_MAGIC, 17)
#define ZH_IOC_SEND_RLS _IOWR(ZH_IOC_MAGIC, 18, struct zh_send_rls)
#define ZH_IOC_SEND_RPS _IOWR(ZH_IOC_MAGIC, 19, struct zh_send_rps)

enum zh_event_e {
	ZH_EVENT_DUMMY,
	ZH_EVENT_ADAPTER_ADD,
	ZH_EVENT_ADAPTER_DEL,
	ZH_EVENT_PORT_ADD,
	ZH_EVENT_UNIT_ADD,
	ZH_EVENT_POLLED
};

enum zh_event_polled_e {
	ZH_EVENT_POLLED_LINK_UP,
	ZH_EVENT_POLLED_LINK_DOWN,
	ZH_EVENT_POLLED_RSCN,
	ZH_EVENT_POLLED_PTY
};

#define ZFCP_CT_GA_NXT			0x0100

/* nameserver request CT_IU -- for requests where
 * a port identifier is required */
struct ct_iu_ga_nxt_req {
	struct ct_hdr header;
	u32 d_id;
} __attribute__ ((packed));

/* FS_ACC IU and data unit for GA_NXT nameserver request */
struct ct_iu_ga_nxt_resp {
	struct ct_hdr header;
        u8 port_type;
        u8 port_id[3];
	u64 port_wwn;
        u8 port_symbolic_name_length;
        u8 port_symbolic_name[255];
	u64 node_wwn;
        u8 node_symbolic_name_length;
        u8 node_symbolic_name[255];
        u64 initial_process_associator;
        u8 node_ip[16];
        u32 cos;
        u8 fc4_types[32];
        u8 port_ip[16];
	u64 fabric_wwn;
        u8 reserved;
        u8 hard_address[3];
} __attribute__ ((packed));

/* other stuff */
#define ZH_PORT_OPERATING_SPEED 1
#define ZH_PORT_SUPPORTED_SPEED 0


#endif /* ZFCP_HBAAPI_H */
