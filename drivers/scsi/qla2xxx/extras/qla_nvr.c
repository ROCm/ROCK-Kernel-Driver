/********************************************************************************
* QLogic ISP2x00 NVRAM parser
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

static char *qla_version = "1.00";

#define	MAX_STRINGS	16
#define	MAX_PARAMS_SIZE	256

/*
 * ISP2X00 NVRAM structure definition.
 */
typedef struct nvram {
	uint8_t id[4];
	uint8_t nvram_version;
	uint8_t reserved_0;
	uint8_t parameter_block_version;
	uint8_t reserved_1;
	uint8_t firmware_options[2];
	uint8_t max_frame_length[2];
	uint8_t max_iocb_allocation[2];
	uint8_t execution_throttle[2];
	uint8_t login_retry_count;
	uint8_t retry_delay;
	uint8_t port_name[8];
	uint8_t hard_address[2];
	uint8_t inquiry;
	uint8_t login_timeout;
	uint8_t node_name[8];
	uint8_t add_fw_opt[2];
	uint8_t response_accumulation_timer;
	uint8_t interrupt_delay_timer;
	uint8_t special_options[2];
	uint8_t reserved_4[26];
	uint8_t host_p[2];
	uint8_t boot_node_name[8];
	uint8_t boot_lun_number;
	uint8_t reset_delay;
	uint8_t port_down_retry_count;
	uint8_t boot_id_number;
	uint8_t maximum_luns_per_target[2];
	uint8_t boot_port_name[8];
	uint8_t reserved_6[6];
	uint8_t reserved_7[50];
	uint8_t reserved_8[50];
	uint8_t reserved_9[32];
	uint8_t adapter_features[2];
	uint8_t reserved_10[6];
	uint8_t reserved_11[4];
	uint8_t subsystem_vendor_id[2];
	uint8_t reserved_12[2];
	uint8_t subsystem_device_id[2];
	uint8_t subsystem_vendor_id_2200[2];
	uint8_t subsystem_device_id_2200[2];
	uint8_t reserved_13;
	uint8_t checksum;
} nvram_t;

/*
 * Parameter types.
 */
typedef enum {
	BITS,
	NUMBER,
	STRING
} prm_t;

typedef struct {
	prm_t type;
	uint8_t bits[MAX_STRINGS];
	uint8_t bytes;
	char *strings[MAX_STRINGS];
} nv_param_t;

typedef struct {
	uint8_t *addr;
	nv_param_t *nvp;
} input_t;

/*
 * Global data.
 */
nvram_t nv;

nv_param_t nv_id = {
	STRING,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	4,
	{
	"\n; NVRAM header\n\n"
	"id                              [\"4 characters\"] = "
	}
};
nv_param_t nv_version = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"version                         [0-255] = "
	}
};
nv_param_t rsv_byte = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"reserved                        [0-255] = "
	}
};
nv_param_t p_version = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\n; RISC Parameter Block\n\n"
	"version                         [0-255] = "
	}
};
nv_param_t firmware_options = {
	BITS,
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
	2,
	{
	"\n; Firmware options\n\nenable hard loop ID             [0-1] = ",
	"enable fairness                 [0-1] = ",
	"enable full duplex              [0-1] = ",
	"enable fast posting             [0-1] = ",
	"enable target mode              [0-1] = ",
	"disable initiator mode          [0-1] = ",
	"Enable ADISC                    [0-1] = ",
	"Target Inquiry Data             [0-1] = ",
	"Enable PDBC Notify              [0-1] = ",
	"Non participating LIP           [0-1] = ",
	"Descending Search LoopID        [0-1] = ",
	"Acquire LoopID in LIPA          [0-1] = ",
	"Stop PortQ on FullStatus        [0-1] = ",
	"Full Login After LIP            [0-1] = ",
	"Node Name option                [0-1] = ",
	"Ext IFWCB enable bit            [0-1] = "
	}
};
nv_param_t frame_payload_size = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nframe payload size              [0-65535] = "
	}
};
nv_param_t max_iocb_allocation = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"max iocb allocation             [0-65535] = "
	}
};
nv_param_t execution_throttle = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"execution throttle              [0-65535] = "
	}
};
nv_param_t login_retry_count = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"login retry count               [0-255] = "
	}
};
nv_param_t retry_delay = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"retry delay                     [0-255] = "
	}
};
nv_param_t port_name_0 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nport name 0                     [0-255] = "
	}
};
nv_param_t port_name_1 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 1 (must be zero)      [0-255] = "
	}
};
nv_param_t port_name_2 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 2 (company ID)        [0-255] = "
	}
};
nv_param_t port_name_3 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 3 (company ID)        [0-255] = "
	}
};
nv_param_t port_name_4 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 4 (company ID)        [0-255] = "
	}
};
nv_param_t port_name_5 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 5 (serial number)     [0-255] = "
	}
};
nv_param_t port_name_6 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 6 (serial number)     [0-255] = "
	}
};
nv_param_t port_name_7 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port name 7 (serial number)     [0-255] = "
	}
};
nv_param_t hard_address = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nadapter hard loop ID            [0-65535] = "
	}
};
nv_param_t inquiry = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"inquiry data                    [0-255] = "
	}
};
nv_param_t login_timeout = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"login timeout                   [0-255] = "
	}
};
nv_param_t node_name_0 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nnode name 0                     [0-255] = "
	}
};
nv_param_t node_name_1 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 1                     [0-255] = "
	}
};
nv_param_t node_name_2 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 2                     [0-255] = "
	}
};
nv_param_t node_name_3 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 3                     [0-255] = "
	}
};
nv_param_t node_name_4 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 4                     [0-255] = "
	}
};
nv_param_t node_name_5 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 5                     [0-255] = "
	}
};
nv_param_t node_name_6 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 6                     [0-255] = "
	}
};
nv_param_t node_name_7 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"node name 7                     [0-255] = "
	}
};
nv_param_t add_fw_opt = {
	BITS,
	{ 4, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, },
	2,
	{
	"\n; Extended Parameter\n\nOperation Mode                  [0-15] = ",
	"Init Config Mode                [0-7] = ",
	"Enable Non part on LIHA failure [0-1] = ",
	"Enable Class 2                  [0-1] = ",
	"Enable Ack0                     [0-1] = ",
	"Reserved                        [0-1] = ",
	"Reserved                        [0-1] = ",
	"Enable FC Tape                  [0-1] = ",
	"Enable FC Confirm               [0-1] = ",
	"Enable Queueing                 [0-1] = ",
	"No Logo On Link Down            [0-1] = "
	}
};
nv_param_t response_accumulation_timer = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nResponse Accumulation Timer     [0-255] = "
	}
};
nv_param_t interrupt_delay_timer = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"Interrupt Delay Timer           [0-255] = "
	}
};
nv_param_t special_options = {
	BITS,
	{ 1, 1, 2, 2, 2, 5, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\n; special options\n\nenable read xfr rdy                  [0-1]  = ",
	"not re-acquire ALPA during LIFA/LIPA [0-1]  = ",
	"Reserved                             [0-3]  = ",
	"FCP_RSP Payload Size                 [0-3]  = ",
	"Reserved                             [0-3]  = ",
	"Reserved                             [0-31] = ",
	"enable 50 ohm termination            [0-1]  = ",
	"Data Rate                            [0-3]  = "
	}
};
nv_param_t rsv_word = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"reserved                        [0-65535] = "
	}
};
nv_param_t host_p = {
	BITS,
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
	2,
	{
	"\n; HOST Parameter block\n\nunused                          [0-1] = ",
	"disable BIOS                    [0-1] = ",
	"disable LUNs                    [0-1] = ",
	"enable selectable boot          [0-1] = ",
	"disable RISC code load          [0-1] = ",
	"set cache line size 1           [0-1] = ",
	"PCI Parity Disable              [0-1] = ",
	"enable extended logging         [0-1] = ",
	"enable 64bit addressing         [0-1] = ",
	"enable LIP reset                [0-1] = ",
	"enable LIP full login           [0-1] = ",
	"enable target reset             [0-1] = ",
	"enable database storage         [0-1] = ",
	"enable cache flush read         [0-1] = ",
	"enable database load            [0-1] = ",
	"unused                          [0-1] = "
	}
};
nv_param_t boot_node_name_0 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nboot node name 0                [0-255] = "
	}
};
nv_param_t boot_node_name_1 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 1                [0-255] = "
	}
};
nv_param_t boot_node_name_2 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 2                [0-255] = "
	}
};
nv_param_t boot_node_name_3 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 3                [0-255] = "
	}
};
nv_param_t boot_node_name_4 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 4                [0-255] = "
	}
};
nv_param_t boot_node_name_5 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 5                [0-255] = "
	}
};
nv_param_t boot_node_name_6 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 6                [0-255] = "
	}
};
nv_param_t boot_node_name_7 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot node name 7                [0-255] = "
	}
};
nv_param_t boot_lun_number = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot LUN number                 [0-255] = "
	}
};
nv_param_t reset_delay = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nreset delay                     [0-255] = "
	}
};
nv_param_t port_down_retry_count = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"port down retry count           [0-255] = "
	}
};
nv_param_t boot_id_number = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot ID number                  [0-255] = "
	}
};
nv_param_t maximum_luns_per_target = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nmaximum LUNs per target         [0-65535] = "
	}
};
nv_param_t boot_port_name_0 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nboot port name 0                [0-255] = "
	}
};
nv_param_t boot_port_name_1 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 1                [0-255] = "
	}
};
nv_param_t boot_port_name_2 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 2                [0-255] = "
	}
};
nv_param_t boot_port_name_3 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 3                [0-255] = "
	}
};
nv_param_t boot_port_name_4 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 4                [0-255] = "
	}
};
nv_param_t boot_port_name_5 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 5                [0-255] = "
	}
};
nv_param_t boot_port_name_6 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 6                [0-255] = "
	}
};
nv_param_t boot_port_name_7 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"boot port name 7                [0-255] = "
	}
};
nv_param_t adapter_features = {
	BITS,
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
	2,
	{
	"\n; Adapter Features\n\nExternal GBIC                   [0-1] = ",
	"Risc RAM Parity                 [0-1] = ",
	"Buffer Plus Module              [0-1] = ",
	"Multi Chip Adapter              [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = ",
	"unused                          [0-1] = "
	}
};
nv_param_t subsystem_vendor_id = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nsubsystem vendor ID             [0-65535] = "
	}
};
nv_param_t subsystem_device_id = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nsubsystem device ID             [0-65535] = "
	}
};
nv_param_t subsystem_vendor_id_2200 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nsubsystem vendor ID 2           [0-65535] = "
	}
};
nv_param_t subsystem_device_id_2200 = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	2,
	{
	"\nsubsystem device ID 2           [0-65535] = "
	}
};
nv_param_t checksum = {
	NUMBER,
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	1,
	{
	"\nchecksum                        [0-255] = "
	}
};

input_t nvram_params[] = {
	{ &nv.id[0], &nv_id },
	{ &nv.nvram_version, &nv_version },
	{ &nv.reserved_0, &rsv_byte },
	{ &nv.parameter_block_version, &p_version },
	{ &nv.reserved_1, &rsv_byte },
	{ &nv.firmware_options[0], &firmware_options },
	{ &nv.max_frame_length[0], &frame_payload_size },
	{ &nv.max_iocb_allocation[0], &max_iocb_allocation },
	{ &nv.execution_throttle[0], &execution_throttle },
	{ &nv.login_retry_count, &login_retry_count },
	{ &nv.retry_delay, &retry_delay },
	{ &nv.port_name[0], &port_name_0 },
	{ &nv.port_name[1], &port_name_1 },
	{ &nv.port_name[2], &port_name_2 },
	{ &nv.port_name[3], &port_name_3 },
	{ &nv.port_name[4], &port_name_4 },
	{ &nv.port_name[5], &port_name_5 },
	{ &nv.port_name[6], &port_name_6 },
	{ &nv.port_name[7], &port_name_7 },
	{ &nv.hard_address[0], &hard_address },
	{ &nv.inquiry, &inquiry },
	{ &nv.login_timeout, &login_timeout },
	{ &nv.node_name[0], &node_name_0 },
	{ &nv.node_name[1], &node_name_1 },
	{ &nv.node_name[2], &node_name_2 },
	{ &nv.node_name[3], &node_name_3 },
	{ &nv.node_name[4], &node_name_4 },
	{ &nv.node_name[5], &node_name_5 },
	{ &nv.node_name[6], &node_name_6 },
	{ &nv.node_name[7], &node_name_7 },
	{ &nv.add_fw_opt[0], &add_fw_opt },
	{ &nv.response_accumulation_timer, &response_accumulation_timer },
	{ &nv.interrupt_delay_timer, &interrupt_delay_timer },
	{ &nv.special_options[0], &special_options },
	{ &nv.reserved_4[0], &rsv_word },
	{ &nv.reserved_4[2], &rsv_word },
	{ &nv.reserved_4[4], &rsv_word },
	{ &nv.reserved_4[6], &rsv_word },
	{ &nv.reserved_4[8], &rsv_word },
	{ &nv.reserved_4[10], &rsv_word },
	{ &nv.reserved_4[12], &rsv_word },
	{ &nv.reserved_4[14], &rsv_word },
	{ &nv.reserved_4[16], &rsv_word },
	{ &nv.reserved_4[18], &rsv_word },
	{ &nv.reserved_4[20], &rsv_word },
	{ &nv.reserved_4[22], &rsv_word },
	{ &nv.reserved_4[24], &rsv_word },
	{ &nv.host_p[0], &host_p },
	{ &nv.boot_node_name[0], &boot_node_name_0 },
	{ &nv.boot_node_name[1], &boot_node_name_1 },
	{ &nv.boot_node_name[2], &boot_node_name_2 },
	{ &nv.boot_node_name[3], &boot_node_name_3 },
	{ &nv.boot_node_name[4], &boot_node_name_4 },
	{ &nv.boot_node_name[5], &boot_node_name_5 },
	{ &nv.boot_node_name[6], &boot_node_name_6 },
	{ &nv.boot_node_name[7], &boot_node_name_7 },
	{ &nv.boot_lun_number, &boot_lun_number },
	{ &nv.reset_delay, &reset_delay },
	{ &nv.port_down_retry_count, &port_down_retry_count },
	{ &nv.boot_id_number, &boot_id_number },
	{ &nv.maximum_luns_per_target[0], &maximum_luns_per_target },
	{ &nv.boot_port_name[0], &boot_port_name_0 },
	{ &nv.boot_port_name[1], &boot_port_name_1 },
	{ &nv.boot_port_name[2], &boot_port_name_2 },
	{ &nv.boot_port_name[3], &boot_port_name_3 },
	{ &nv.boot_port_name[4], &boot_port_name_4 },
	{ &nv.boot_port_name[5], &boot_port_name_5 },
	{ &nv.boot_port_name[6], &boot_port_name_6 },
	{ &nv.boot_port_name[7], &boot_port_name_7 },
	{ &nv.reserved_6[0], &rsv_word },
	{ &nv.reserved_6[2], &rsv_word },
	{ &nv.reserved_6[4], &rsv_word },
	{ &nv.reserved_7[0], &rsv_word },	/* 100 */
	{ &nv.reserved_7[2], &rsv_word },
	{ &nv.reserved_7[4], &rsv_word },
	{ &nv.reserved_7[6], &rsv_word },
	{ &nv.reserved_7[8], &rsv_word },
	{ &nv.reserved_7[10], &rsv_word },
	{ &nv.reserved_7[12], &rsv_word },
	{ &nv.reserved_7[14], &rsv_word },
	{ &nv.reserved_7[16], &rsv_word },
	{ &nv.reserved_7[18], &rsv_word },
	{ &nv.reserved_7[20], &rsv_word },
	{ &nv.reserved_7[22], &rsv_word },
	{ &nv.reserved_7[24], &rsv_word },
	{ &nv.reserved_7[26], &rsv_word },
	{ &nv.reserved_7[28], &rsv_word },
	{ &nv.reserved_7[30], &rsv_word },
	{ &nv.reserved_7[32], &rsv_word },
	{ &nv.reserved_7[34], &rsv_word },
	{ &nv.reserved_7[36], &rsv_word },
	{ &nv.reserved_7[38], &rsv_word },
	{ &nv.reserved_7[40], &rsv_word },
	{ &nv.reserved_7[42], &rsv_word },
	{ &nv.reserved_7[44], &rsv_word },
	{ &nv.reserved_7[46], &rsv_word },
	{ &nv.reserved_7[48], &rsv_word },
	{ &nv.reserved_8[0], &rsv_word },	/* 150 */
	{ &nv.reserved_8[2], &rsv_word },
	{ &nv.reserved_8[4], &rsv_word },
	{ &nv.reserved_8[6], &rsv_word },
	{ &nv.reserved_8[8], &rsv_word },
	{ &nv.reserved_8[10], &rsv_word },
	{ &nv.reserved_8[12], &rsv_word },
	{ &nv.reserved_8[14], &rsv_word },
	{ &nv.reserved_8[16], &rsv_word },
	{ &nv.reserved_8[18], &rsv_word },
	{ &nv.reserved_8[20], &rsv_word },
	{ &nv.reserved_8[22], &rsv_word },
	{ &nv.reserved_8[24], &rsv_word },
	{ &nv.reserved_8[26], &rsv_word },
	{ &nv.reserved_8[28], &rsv_word },
	{ &nv.reserved_8[30], &rsv_word },
	{ &nv.reserved_8[32], &rsv_word },
	{ &nv.reserved_8[34], &rsv_word },
	{ &nv.reserved_8[36], &rsv_word },
	{ &nv.reserved_8[38], &rsv_word },
	{ &nv.reserved_8[40], &rsv_word },
	{ &nv.reserved_8[42], &rsv_word },
	{ &nv.reserved_8[44], &rsv_word },
	{ &nv.reserved_8[46], &rsv_word },
	{ &nv.reserved_8[48], &rsv_word },
	{ &nv.reserved_9[0], &rsv_word },	/* 200 */
	{ &nv.reserved_9[2], &rsv_word },
	{ &nv.reserved_9[4], &rsv_word },
	{ &nv.reserved_9[6], &rsv_word },
	{ &nv.reserved_9[8], &rsv_word },
	{ &nv.reserved_9[10], &rsv_word },
	{ &nv.reserved_9[12], &rsv_word },
	{ &nv.reserved_9[14], &rsv_word },
	{ &nv.reserved_9[16], &rsv_word },
	{ &nv.reserved_9[18], &rsv_word },
	{ &nv.reserved_9[20], &rsv_word },
	{ &nv.reserved_9[22], &rsv_word },
	{ &nv.reserved_9[24], &rsv_word },
	{ &nv.reserved_9[26], &rsv_word },
	{ &nv.reserved_9[28], &rsv_word },
	{ &nv.reserved_9[30], &rsv_word },
	{ &nv.adapter_features[0], &adapter_features },
	{ &nv.reserved_10[0], &rsv_word },
	{ &nv.reserved_10[2], &rsv_word },
	{ &nv.reserved_10[4], &rsv_word },
	{ &nv.reserved_11[0], &rsv_word },
	{ &nv.reserved_11[2], &rsv_word },
	{ &nv.subsystem_vendor_id[0], &subsystem_vendor_id },
	{ &nv.reserved_12[0], &rsv_word },
	{ &nv.subsystem_device_id[0], &subsystem_device_id },
	{ &nv.subsystem_vendor_id_2200[0], &subsystem_vendor_id_2200 },
	{ &nv.subsystem_device_id_2200[0], &subsystem_device_id_2200 },
	{ &nv.reserved_13, &rsv_byte },
	{ &nv.checksum, &checksum }
};

/*
 * Local function prototypes.
 */
static void usage(void);
static void bin2asc(void);
static void asc2bin(void);
static void get_param(input_t *);
static uint8_t calc_checksum(void);
static void dmp_param(input_t *);

/*
 * Local functions.
 */
int
main(int argc, char *argv[])
{
	if (argc != 2) {
		usage();
		exit(1);
	}

	if (strcmp(argv[1], "-a") == 0) {
		bin2asc();
	} else if (strcmp(argv[1], "-b") == 0) {
		asc2bin();
	} else {
		usage();
		exit(1);
	}

	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "QLogic NVRAM Parser: %s\n", qla_version);
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "    qla_nvr [-a] [-b] -\n");
	fprintf(stderr, "      -a  -- convert raw nvram to ASCII\n");
	fprintf(stderr, "      -b  -- convert ASCII to raw nvram\n\n");
}

static void
bin2asc(void)
{
	int stat = 0;
	uint32_t i;

	/* Get NVRAM data. */
	fread(&nv, sizeof(char), sizeof(nvram_t), stdin);

	if (calc_checksum() != 0) {
		fprintf(stderr, "qla_nvr: NVRAM checksum error!!!\n");
		return;
	}

	for (i = 0; nvram_params[i].nvp != &checksum; i++) {
		if (nvram_params[i].nvp == &rsv_word ||
		    nvram_params[i].nvp == &rsv_byte) {
			if (stat == 0) {
				printf("\n");
				stat++;
			}
		} else {
			stat = 0;
		}
		dmp_param(&nvram_params[i]);
	}
}

static void
asc2bin(void)
{
	uint32_t i;

	memset(&nv, 0, sizeof(nvram_t));

	for (i = 0; nvram_params[i].nvp != &checksum; i++)
		get_param(&nvram_params[i]);

	/* Check for proper port name. */
	if (nv.port_name[0] != 33 || nv.port_name[2] != 0 ||
	    nv.port_name[3] != 224 || nv.port_name[4] != 139) {
		fprintf(stderr, "qla_nvr: Invalid port name!!!\n");
		return;
	}

	/* calculate checksum */
	nv.checksum = ~calc_checksum() + 1;

	/* Write NVRAM data. */
	fwrite(&nv, sizeof(char), sizeof(nvram_t), stdout);
}

static void
get_param(input_t *inp)
{
	char		s[MAX_PARAMS_SIZE];
	uint32_t	i;
	uint32_t	n = 0;
	uint8_t		bc = 0;
	uint8_t		bp = 0;
	uint32_t	b = 0;

	 while (fgets(s, MAX_PARAMS_SIZE, stdin) != NULL) {
		i = 0;
		while (isspace(s[i]))
			i++;
		if(s[i] == ';' || s[i] == '\0')
			continue;

		while (s[i++] != '=')
			;
		while (isspace(s[i]))
			i++;

		/* Get decimal number. */
		n = 0;
		while (s[i] != '\0') {
			if (inp->nvp->type == STRING) {
				if (s[i] == '"') {
					i++;
				} else {
					n |= s[i++] << (bp++ * 8);
					if (bp == inp->nvp->bytes)
						break;
				}
			} else {
				n = atol(&s[i]);
				break;
			}
		}

		/* If number or string, done. */
		if (inp->nvp->type == NUMBER || inp->nvp->type == STRING) {
			break;
		}
		
		/* Bits. */
		b |= n << bc;
		bc += inp->nvp->bits[bp++];
		if (bp == inp->nvp->bytes * 8 || inp->nvp->bits[bp] == 0) {
			n = b;
			break;
		}
	}

	/* Store input data. */
	for (i = 0; i < inp->nvp->bytes; i++) {
		inp->addr[i] = (uint8_t)n;
		n >>= 8;
	}
}

static uint8_t
calc_checksum(void)
{
	uint32_t	i;
	uint8_t		rval = 0;
	uint8_t		*nvp = (uint8_t *)&nv;

	for (i = 0; i < sizeof (nvram_t); i++)
		rval += *nvp++;

	return (rval);
}


static void
dmp_param(input_t *inp)
{
	uint32_t	i;
	uint32_t	n;
	uint32_t	x;
	uint32_t	b;
	uint8_t		bc = 0;
	uint8_t		bp = 0;

	/* Get number. */
	n = 0;
	x = inp->nvp->bytes;
	while (x != 0) {
		n <<= 8;
		n |= inp->addr[--x];
	}

	for (i = 0; i < MAX_STRINGS && inp->nvp->strings[i] != NULL; i++) {
		/* If number, done. */
		if (inp->nvp->type == NUMBER) {
			printf("%s%d\n", inp->nvp->strings[i], n);
			return;
		}

		/* If string, done. */
		if (inp->nvp->type == STRING) {
			printf("%s\"", inp->nvp->strings[i]);
			for (x = 0; x < inp->nvp->bytes; x++) {
				bc = n & 0xff;
				n >>= 8;
				printf("%c", bc);
			}
			printf("\"\n");
			return;
		}

		/* Make bit mask. */
		for (b = 0, x = 0; x < inp->nvp->bits[bp]; x++)
			b = b << 1 | 1;

		/* Get masked data. */
		b = n >> bc & b;
		bc += inp->nvp->bits[bp++];

		/* Output string and number. */
		printf("%s%d\n", inp->nvp->strings[i], b);

		/* Test for last bit. */
		if (bp == inp->nvp->bytes * 8 || inp->nvp->bits[bp] == 0)
			break;
	}
}
