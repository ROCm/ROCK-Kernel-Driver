/*
 * sbp2.h - Defines and prototypes for sbp2.c
 *
 * Copyright (C) 2000 James Goodwin, Filanet Corporation (www.filanet.com)
 * jamesg@filanet.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef SBP2_H
#define SBP2_H

#define SBP2_DEVICE_NAME		"sbp2"
#define SBP2_DEVICE_NAME_SIZE		4

/*
 * SBP2 specific structures and defines
 */

#define ORB_FMT_CMD			0x0
#define ORB_FMT_DUMMY			0x3

#define ORB_DIRECTION_WRITE_TO_MEDIA    0x0
#define ORB_DIRECTION_READ_FROM_MEDIA   0x1
#define ORB_DIRECTION_NO_DATA_TRANSFER  0x2

#define SPEED_S100			0x0
#define SPEED_S200			0x1
#define SPEED_S400			0x2
#define SPEED_S800			0x3
#define SPEED_S1600			0x4
#define SPEED_S3200			0x5

/* 2^(MAX_PAYLOAD+1) = Maximum data transfer length */
#define MAX_PAYLOAD_S100		0x7
#define MAX_PAYLOAD_S200		0x8
#define MAX_PAYLOAD_S400		0x9

/* Max rec matches node_entry values */
#define MAX_REC_S100			512
#define MAX_REC_S200			1024
#define MAX_REC_S400			2048

#define ORB_SET_NOTIFY(value)                   ((value & 0x1) << 31)
#define ORB_SET_RQ_FMT(value)                   ((value & 0x3) << 29)
#define ORB_SET_NODE_ID(value)			((value & 0xffff) << 16)

struct sbp2_dummy_orb {
	volatile u32 next_ORB_hi;
	volatile u32 next_ORB_lo;
	u32 reserved1;
        u32 reserved2;
	u32 notify_rq_fmt;
	u8 command_block[12];
};

#define ORB_SET_DATA_SIZE(value)                (value & 0xffff)
#define ORB_SET_PAGE_SIZE(value)                ((value & 0x7) << 16)
#define ORB_SET_PAGE_TABLE_PRESENT(value)       ((value & 0x1) << 19)
#define ORB_SET_MAX_PAYLOAD(value)              ((value & 0xf) << 20)
#define ORB_SET_SPEED(value)                    ((value & 0x7) << 24)
#define ORB_SET_DIRECTION(value)                ((value & 0x1) << 27)

struct sbp2_command_orb {
	volatile u32 next_ORB_hi;
	volatile u32 next_ORB_lo;
	u32 data_descriptor_hi;
	u32 data_descriptor_lo;
	u32 misc;
	u8 cdb[12];
};

#define LOGIN_REQUEST			0x0
#define QUERY_LOGINS_REQUEST		0x1
#define RECONNECT_REQUEST		0x3
#define SET_PASSWORD_REQUEST		0x4
#define LOGOUT_REQUEST			0x7
#define ABORT_TASK_REQUEST		0xb
#define ABORT_TASK_SET			0xc
#define LOGICAL_UNIT_RESET		0xe
#define TARGET_RESET_REQUEST		0xf

#define ORB_SET_LUN(value)                      (value & 0xffff)
#define ORB_SET_FUNCTION(value)                 ((value & 0xf) << 16)
#define ORB_SET_RECONNECT(value)                ((value & 0xf) << 20)
#define ORB_SET_EXCLUSIVE(value)                ((value & 0x1) << 28)
#define ORB_SET_LOGIN_RESP_LENGTH(value)        (value & 0xffff)
#define ORB_SET_PASSWD_LENGTH(value)            ((value & 0xffff) << 16)

struct sbp2_login_orb {
	u32 password_hi;
	u32 password_lo;
	u32 login_response_hi;
	u32 login_response_lo;
	u32 lun_misc;
	u32 passwd_resp_lengths;
	u32 status_FIFO_hi;
	u32 status_FIFO_lo;
};

#define RESPONSE_GET_LOGIN_ID(value)            (value & 0xffff)
#define RESPONSE_GET_LENGTH(value)              ((value >> 16) & 0xffff)
#define RESPONSE_GET_RECONNECT_HOLD(value)      (value & 0xffff)

struct sbp2_login_response {
	u32 length_login_ID;
	u32 command_block_agent_hi;
	u32 command_block_agent_lo;
	u32 reconnect_hold;
};

#define ORB_SET_LOGIN_ID(value)                 (value & 0xffff)

struct sbp2_reconnect_orb {
	u32 reserved1;
	u32 reserved2;
        u32 reserved3;
        u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_FIFO_hi;
	u32 status_FIFO_lo;
};

struct sbp2_logout_orb {
	u32 reserved1;
	u32 reserved2;
        u32 reserved3;
        u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_FIFO_hi;
	u32 status_FIFO_lo;
};

#define PAGE_TABLE_SET_SEGMENT_BASE_HI(value)   (value & 0xffff)
#define PAGE_TABLE_SET_SEGMENT_LENGTH(value)    ((value & 0xffff) << 16)

struct sbp2_unrestricted_page_table {
	u32 length_segment_base_hi;
	u32 segment_base_lo;
};

#define RESP_STATUS_REQUEST_COMPLETE		0x0
#define RESP_STATUS_TRANSPORT_FAILURE		0x1
#define RESP_STATUS_ILLEGAL_REQUEST		0x2
#define RESP_STATUS_VENDOR_DEPENDENT		0x3

#define SBP2_STATUS_NO_ADDITIONAL_INFO		0x0
#define SBP2_STATUS_REQ_TYPE_NOT_SUPPORTED	0x1
#define SBP2_STATUS_SPEED_NOT_SUPPORTED		0x2
#define SBP2_STATUS_PAGE_SIZE_NOT_SUPPORTED	0x3
#define SBP2_STATUS_ACCESS_DENIED		0x4
#define SBP2_STATUS_LU_NOT_SUPPORTED		0x5
#define SBP2_STATUS_MAX_PAYLOAD_TOO_SMALL	0x6
#define SBP2_STATUS_RESOURCES_UNAVAILABLE	0x8
#define SBP2_STATUS_FUNCTION_REJECTED		0x9
#define SBP2_STATUS_LOGIN_ID_NOT_RECOGNIZED	0xa
#define SBP2_STATUS_DUMMY_ORB_COMPLETED		0xb
#define SBP2_STATUS_REQUEST_ABORTED		0xc
#define SBP2_STATUS_UNSPECIFIED_ERROR		0xff

#define SFMT_CURRENT_ERROR			0x0
#define SFMT_DEFERRED_ERROR			0x1
#define SFMT_VENDOR_DEPENDENT_STATUS		0x3

#define SBP2_SCSI_STATUS_GOOD			0x0
#define SBP2_SCSI_STATUS_CHECK_CONDITION	0x2
#define SBP2_SCSI_STATUS_CONDITION_MET		0x4
#define SBP2_SCSI_STATUS_BUSY			0x8
#define SBP2_SCSI_STATUS_RESERVATION_CONFLICT	0x18
#define SBP2_SCSI_STATUS_COMMAND_TERMINATED	0x22

#define SBP2_SCSI_STATUS_SELECTION_TIMEOUT	0xff

#define STATUS_GET_ORB_OFFSET_HI(value)         (value & 0xffff)
#define STATUS_GET_SBP_STATUS(value)            ((value >> 16) & 0xff)
#define STATUS_GET_LENGTH(value)                ((value >> 24) & 0x7)
#define STATUS_GET_DEAD_BIT(value)              ((value >> 27) & 0x1)
#define STATUS_GET_RESP(value)                  ((value >> 28) & 0x3)
#define STATUS_GET_SRC(value)                   ((value >> 30) & 0x3)

struct sbp2_status_block {
	u32 ORB_offset_hi_misc;
	u32 ORB_offset_lo;
        u8 command_set_dependent[24];
};

/*
 * Miscellaneous SBP2 related config rom defines
 */

#define SBP2_STATUS_FIFO_ADDRESS				0xfffe00000000ULL     /* for write posting! */
#define SBP2_STATUS_FIFO_ADDRESS_HI                             0xfffe
#define SBP2_STATUS_FIFO_ADDRESS_LO                             0x0

#define SBP2_UNIT_DIRECTORY_OFFSET_KEY				0xd1
#define SBP2_CSR_OFFSET_KEY					0x54
#define SBP2_UNIT_SPEC_ID_KEY					0x12
#define SBP2_UNIT_SW_VERSION_KEY				0x13
#define SBP2_COMMAND_SET_SPEC_ID_KEY				0x38
#define SBP2_COMMAND_SET_KEY					0x39
#define SBP2_UNIT_CHARACTERISTICS_KEY				0x3a
#define SBP2_DEVICE_TYPE_AND_LUN_KEY				0x14
#define SBP2_FIRMWARE_REVISION_KEY				0x3c

#define SBP2_AGENT_STATE_OFFSET					0x00ULL
#define SBP2_AGENT_RESET_OFFSET					0x04ULL
#define SBP2_ORB_POINTER_OFFSET					0x08ULL
#define SBP2_DOORBELL_OFFSET					0x10ULL
#define SBP2_UNSOLICITED_STATUS_ENABLE_OFFSET			0x14ULL
#define SBP2_UNSOLICITED_STATUS_VALUE				0xf

#define SBP2_BUSY_TIMEOUT_ADDRESS				0xfffff0000210ULL
#define SBP2_BUSY_TIMEOUT_VALUE					0xf

#define SBP2_AGENT_RESET_DATA					0xf

/*
 * Unit spec id and sw version entry for SBP-2 devices
 */

#define SBP2_UNIT_SPEC_ID_ENTRY					0x1200609e
#define SBP2_SW_VERSION_ENTRY					0x13010483

/*
 * Miscellaneous general config rom related defines
 */

#define CONFIG_ROM_INITIAL_MEMORY_SPACE 			0xfffff0000000ULL

#define CONFIG_ROM_BASE_ADDRESS					0xfffff0000400ULL
#define CONFIG_ROM_ROOT_DIR_BASE				0xfffff0000414ULL
#define CONFIG_ROM_SIGNATURE_ADDRESS				0xfffff0000404ULL
#define CONFIG_ROM_UNIT_DIRECTORY_OFFSET			0xfffff0000424ULL

#define IEEE1394_CONFIG_ROM_SIGNATURE				0x31333934

#define SBP2_128KB_BROKEN_FIRMWARE				0xa0b800
#define SBP2_BROKEN_FIRMWARE_MAX_TRANSFER			0x20000

/*
 * Flags for SBP-2 functions
 */
#define SBP2_SEND_NO_WAIT					0x00000001

/*
 * SCSI specific stuff
 */

#define SBP2_MAX_SG_ELEMENTS		SG_ALL
#define SBP2_CLUSTERING			ENABLE_CLUSTERING
#define SBP2_MAX_SG_ELEMENT_LENGTH	0xf000
#define SBP2SCSI_MAX_SCSI_IDS		8
#define SBP2SCSI_MAX_OUTSTANDING_CMDS	8	/* Max total outstanding sbp2 commands allowed at a time! */
#define SBP2SCSI_MAX_CMDS_PER_LUN	4 	/* Max outstanding sbp2 commands per device - tune as needed */

#ifndef TYPE_SDAD
#define TYPE_SDAD			0x0e	/* simplified direct access device */
#endif

/*
 * SCSI direction table... since the scsi stack doesn't specify direction...   =(
 *
 * DIN = IN data direction
 * DOU = OUT data direction
 * DNO = No data transfer
 * DUN = Unknown data direction
 */
#define DIN				ORB_DIRECTION_READ_FROM_MEDIA
#define DOU				ORB_DIRECTION_WRITE_TO_MEDIA
#define DNO				ORB_DIRECTION_NO_DATA_TRANSFER
#define DUN				DIN 

static unchar sbp2scsi_direction_table[0x100] = {
	DNO,DNO,DIN,DIN,DOU,DIN,DIN,DOU,DIN,DUN,DOU,DOU,DUN,DUN,DUN,DIN,
	DNO,DIN,DIN,DOU,DIN,DOU,DNO,DNO,DOU,DNO,DIN,DNO,DIN,DOU,DNO,DUN,
	DIN,DUN,DIN,DIN,DOU,DIN,DUN,DUN,DIN,DIN,DOU,DNO,DUN,DIN,DOU,DOU,
	DOU,DOU,DOU,DNO,DIN,DNO,DNO,DIN,DOU,DOU,DOU,DOU,DIN,DOU,DIN,DOU,
	DOU,DOU,DIN,DIN,DIN,DNO,DIN,DNO,DNO,DNO,DUN,DNO,DOU,DIN,DNO,DUN,
	DUN,DIN,DIN,DNO,DOU,DOU,DUN,DUN,DNO,DIN,DIN,DNO,DIN,DOU,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DNO,DOU,DOU,DIN,DNO,DNO,DNO,DIN,DNO,DOU,DUN,DNO,DIN,DOU,DOU,
	DOU,DOU,DOU,DNO,DUN,DIN,DOU,DIN,DIN,DIN,DNO,DNO,DNO,DIN,DIN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DOU,DUN,DUN,DUN,DUN,DUN,
	DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN
};


/*
 * Number of request packets available for actual sbp2 I/O requests (these are used
 * for sending command and agent reset packets).
 */
#define SBP2_MAX_REQUEST_PACKETS	SBP2SCSI_MAX_OUTSTANDING_CMDS	/* Per host adapter instance */
#define SBP2_MAX_COMMAND_ORBS		SBP2SCSI_MAX_CMDS_PER_LUN * 2	/* Per sbp2 device instance */

/*
 * Request packets structure (used for sending command and agent reset packets)
 */
struct sbp2_request_packet {

	struct list_head list;
	struct hpsb_packet *packet;
	struct tq_struct tq;
	void *hi_context;

};

/* 
 * Encapsulates all the info necessary for an outstanding command. 
 */
struct sbp2_command_info {

	struct list_head list;
	struct sbp2_command_orb command_orb;
	dma_addr_t command_orb_dma;
	Scsi_Cmnd *Current_SCpnt;
	void (*Current_done)(Scsi_Cmnd *);
	unsigned int linked;

	/* Also need s/g structure for each sbp2 command */
	struct sbp2_unrestricted_page_table scatter_gather_element[SBP2_MAX_SG_ELEMENTS];
	dma_addr_t sge_dma;
	void *sge_buffer;
	dma_addr_t cmd_dma;
	int dma_type;
	unsigned long dma_size;
	int dma_dir;

};

/*
 * Information needed on a per scsi id basis (one for each sbp2 device)
 */
struct scsi_id_instance_data {

	/*
	 * Various sbp2 specific structures
	 */
	struct sbp2_command_orb *last_orb;
	struct sbp2_login_orb *login_orb;
	dma_addr_t login_orb_dma;
	struct sbp2_login_response *login_response;
	dma_addr_t login_response_dma;
	struct sbp2_reconnect_orb *reconnect_orb;
	dma_addr_t reconnect_orb_dma;
	struct sbp2_logout_orb *logout_orb;
	dma_addr_t logout_orb_dma;
	struct sbp2_status_block status_block;

	/*
	 * Stuff we need to know about the sbp2 device itself
	 */
	u64 node_unique_id;
	u64 sbp2_management_agent_addr;
	u64 sbp2_command_block_agent_addr;
	u32 node_id;
	u32 speed_code;
	u32 max_payload_size;

	/*
	 * Values pulled from the device's unit directory
	 */
	u32 sbp2_unit_spec_id;
	u32 sbp2_unit_sw_version;
	u32 sbp2_command_set_spec_id;
	u32 sbp2_command_set;
	u32 sbp2_unit_characteristics;
	u32 sbp2_device_type_and_lun;
	u32 sbp2_firmware_revision;

	/* 
	 * Wait queue used for logins, reconnects, logouts 
	 */
	wait_queue_head_t sbp2_login_wait;

	/*
	 * Flag noting whether the sbp2 device is currently validated (for use during
	 * bus resets).
	 */
	u32 validated;

	/* 
	 * Pool of command orbs, so we can have more than overlapped command per id
	 */
	spinlock_t sbp2_command_orb_lock;
	struct list_head sbp2_command_orb_inuse;
	struct list_head sbp2_command_orb_completed;
	u32 sbp2_total_command_orbs;

};

/*
 * Sbp2 host data structure (one per sbp2 host)
 */
struct sbp2scsi_host_info {

	/*
	 * For use in keeping track of hosts
	 */
	struct list_head list;
	struct hpsb_host *host;

	/*
	 * Spin locks for command processing and packet pool management
	 */
	spinlock_t sbp2_command_lock;
	spinlock_t sbp2_request_packet_lock;

	/*
	 * Flag indicating if a bus reset (or device detection) is in progress
	 */
	u32 bus_reset_in_progress;

	/*
	 * We currently use a kernel thread for dealing with bus resets and sbp2
	 * device detection. We use this to wake up the thread when needed.
	 */
	wait_queue_head_t sbp2_detection_wait;

	/* 
	 * PID of sbp2 detection kernel thread 
	 */
	int sbp2_detection_pid;

	/*
	 * Lists keeping track of inuse/free sbp2_request_packets. These structures are
	 * used for sending out sbp2 command and agent reset packets. We initially create
	 * a pool of request packets so that we don't have to do any kmallocs while in critical
	 * I/O paths.
	 */
	struct list_head sbp2_req_inuse;
	struct list_head sbp2_req_free;

	/*
	 * Stuff to keep track of the initial scsi bus scan (so that we don't miss it)
	 */
	u32 initial_scsi_bus_scan_complete;
	Scsi_Cmnd *bus_scan_SCpnt;
	void (*bus_scan_done)(Scsi_Cmnd *);

	/*
	 * Here is the pool of request packets. All the hpsb packets (for 1394 bus transactions)
	 * are allocated at init and simply re-initialized when needed.
	 */
	struct sbp2_request_packet request_packet[SBP2_MAX_REQUEST_PACKETS];

	/*
	 * SCSI ID instance data (one for each sbp2 device instance possible)
	 */
	struct scsi_id_instance_data *scsi_id[SBP2SCSI_MAX_SCSI_IDS];

};

/*
 * Function prototypes
 */

/*
 * Various utility prototypes
 */
static int sbp2util_read_quadlet(struct sbp2scsi_host_info *hi, nodeid_t node, u64 addr,
				 quadlet_t *buffer);
static int sbp2util_unit_directory(struct sbp2scsi_host_info *hi, nodeid_t node, u64 *addr);
static int sbp2util_create_request_packet_pool(struct sbp2scsi_host_info *hi);
static void sbp2util_remove_request_packet_pool(struct sbp2scsi_host_info *hi);
static struct sbp2_request_packet *sbp2util_allocate_write_request_packet(struct sbp2scsi_host_info *hi,
									  nodeid_t node, u64 addr,
									  size_t data_size,
									  quadlet_t data);
static void sbp2util_free_request_packet(struct sbp2_request_packet *request_packet);
static int sbp2util_create_command_orb_pool(struct scsi_id_instance_data *scsi_id, struct sbp2scsi_host_info *hi);
static void sbp2util_remove_command_orb_pool(struct scsi_id_instance_data *scsi_id, struct sbp2scsi_host_info *hi);
static struct sbp2_command_info *sbp2util_find_command_for_orb(struct scsi_id_instance_data *scsi_id, dma_addr_t orb);
static struct sbp2_command_info *sbp2util_find_command_for_SCpnt(struct scsi_id_instance_data *scsi_id, void *SCpnt);
static struct sbp2_command_info *sbp2util_allocate_command_orb(struct scsi_id_instance_data *scsi_id, 
							  Scsi_Cmnd *Current_SCpnt, 
							  void (*Current_done)(Scsi_Cmnd *),
							  struct sbp2scsi_host_info *hi);
static void sbp2util_mark_command_completed(struct scsi_id_instance_data *scsi_id,
		struct sbp2_command_info *command);

/*
 * IEEE-1394 core driver related prototypes
 */
static void sbp2_remove_unvalidated_devices(struct sbp2scsi_host_info *hi);
static int sbp2_start_device(struct sbp2scsi_host_info *hi, int node_id);
static int sbp2_check_device(struct sbp2scsi_host_info *hi, int node_id);
static void sbp2_bus_reset_handler(void *context);
static void sbp2_add_host(struct hpsb_host *host);
static struct sbp2scsi_host_info *sbp2_find_host_info(struct hpsb_host *host);
static void sbp2_remove_host(struct hpsb_host *host);
static void sbp2_host_reset(struct hpsb_host *host);
static int sbp2_detection_thread(void *__sbp2);
int sbp2_init(void);
void sbp2_cleanup(void);
#if 0
static int sbp2_handle_physdma_write(struct hpsb_host *host, int nodeid, quadlet_t *data,
				     u64 addr, unsigned int length);
static int sbp2_handle_physdma_read(struct hpsb_host *host, int nodeid, quadlet_t *data,
				    u64 addr, unsigned int length);
#endif
/*
 * SBP-2 protocol related prototypes
 */
static int sbp2_login_device(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id);
static int sbp2_reconnect_device(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id); 
static int sbp2_logout_device(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id); 
static int sbp2_handle_status_write(struct hpsb_host *host, int nodeid, int destid,
				    quadlet_t *data, u64 addr, unsigned int length);
static int sbp2_agent_reset(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id, u32 flags);
static int sbp2_create_command_orb(struct sbp2scsi_host_info *hi, 
				   struct scsi_id_instance_data *scsi_id,
				   struct sbp2_command_info *command,
				   unchar *scsi_cmd,
				   unsigned int scsi_use_sg,
				   unsigned int scsi_request_bufflen,
				   void *scsi_request_buffer, int dma_dir);
static int sbp2_link_orb_command(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id,
				 struct sbp2_command_info *command);
static int sbp2_send_command(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id,
			     Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
static int sbp2_send_split_command(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id,
				   Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
static unsigned int sbp2_status_to_sense_data(unchar *sbp2_status, unchar *sense_data);
static void sbp2_check_sbp2_command(unchar *cmd);
static void sbp2_check_sbp2_response(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id, 
				     Scsi_Cmnd *SCpnt);
static int sbp2_parse_unit_directory(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id);
static int sbp2_set_busy_timeout(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id);
static int sbp2_max_speed_and_size(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id);

/*
 * Scsi interface related prototypes
 */
static const char *sbp2scsi_info (struct Scsi_Host *host);
static int sbp2scsi_detect (Scsi_Host_Template *tpnt);
void sbp2scsi_setup(char *str, int *ints);
static int sbp2scsi_biosparam (Scsi_Disk *disk, kdev_t dev, int geom[]);
static int sbp2scsi_abort (Scsi_Cmnd *SCpnt); 
static int sbp2scsi_reset (Scsi_Cmnd *SCpnt, unsigned int reset_flags); 
static int sbp2scsi_queuecommand (Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
static void sbp2scsi_complete_all_commands(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id, 
					   u32 status);
static void sbp2scsi_complete_command(struct sbp2scsi_host_info *hi, struct scsi_id_instance_data *scsi_id, 
				      u32 scsi_status, Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
static void sbp2scsi_register_scsi_host(struct sbp2scsi_host_info *hi);

#endif /* SBP2_H */
