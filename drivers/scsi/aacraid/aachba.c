/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
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
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>

#include "aacraid.h"


/* values for inqd_pdt: Peripheral device type in plain English */
#define	INQD_PDT_DA	0x00	/* Direct-access (DISK) device */
#define	INQD_PDT_PROC	0x03	/* Processor device */
#define	INQD_PDT_CHNGR	0x08	/* Changer (jukebox, scsi2) */
#define	INQD_PDT_COMM	0x09	/* Communication device (scsi2) */
#define	INQD_PDT_NOLUN2 0x1f	/* Unknown Device (scsi2) */
#define	INQD_PDT_NOLUN	0x7f	/* Logical Unit Not Present */

#define	INQD_PDT_DMASK	0x1F	/* Peripheral Device Type Mask */
#define	INQD_PDT_QMASK	0xE0	/* Peripheral Device Qualifer Mask */

/*
 *	Sense codes
 */
 
#define SENCODE_NO_SENSE                        0x00
#define SENCODE_END_OF_DATA                     0x00
#define SENCODE_BECOMING_READY                  0x04
#define SENCODE_INIT_CMD_REQUIRED               0x04
#define SENCODE_PARAM_LIST_LENGTH_ERROR         0x1A
#define SENCODE_INVALID_COMMAND                 0x20
#define SENCODE_LBA_OUT_OF_RANGE                0x21
#define SENCODE_INVALID_CDB_FIELD               0x24
#define SENCODE_LUN_NOT_SUPPORTED               0x25
#define SENCODE_INVALID_PARAM_FIELD             0x26
#define SENCODE_PARAM_NOT_SUPPORTED             0x26
#define SENCODE_PARAM_VALUE_INVALID             0x26
#define SENCODE_RESET_OCCURRED                  0x29
#define SENCODE_LUN_NOT_SELF_CONFIGURED_YET     0x3E
#define SENCODE_INQUIRY_DATA_CHANGED            0x3F
#define SENCODE_SAVING_PARAMS_NOT_SUPPORTED     0x39
#define SENCODE_DIAGNOSTIC_FAILURE              0x40
#define SENCODE_INTERNAL_TARGET_FAILURE         0x44
#define SENCODE_INVALID_MESSAGE_ERROR           0x49
#define SENCODE_LUN_FAILED_SELF_CONFIG          0x4c
#define SENCODE_OVERLAPPED_COMMAND              0x4E

/*
 *	Additional sense codes
 */
 
#define ASENCODE_NO_SENSE                       0x00
#define ASENCODE_END_OF_DATA                    0x05
#define ASENCODE_BECOMING_READY                 0x01
#define ASENCODE_INIT_CMD_REQUIRED              0x02
#define ASENCODE_PARAM_LIST_LENGTH_ERROR        0x00
#define ASENCODE_INVALID_COMMAND                0x00
#define ASENCODE_LBA_OUT_OF_RANGE               0x00
#define ASENCODE_INVALID_CDB_FIELD              0x00
#define ASENCODE_LUN_NOT_SUPPORTED              0x00
#define ASENCODE_INVALID_PARAM_FIELD            0x00
#define ASENCODE_PARAM_NOT_SUPPORTED            0x01
#define ASENCODE_PARAM_VALUE_INVALID            0x02
#define ASENCODE_RESET_OCCURRED                 0x00
#define ASENCODE_LUN_NOT_SELF_CONFIGURED_YET    0x00
#define ASENCODE_INQUIRY_DATA_CHANGED           0x03
#define ASENCODE_SAVING_PARAMS_NOT_SUPPORTED    0x00
#define ASENCODE_DIAGNOSTIC_FAILURE             0x80
#define ASENCODE_INTERNAL_TARGET_FAILURE        0x00
#define ASENCODE_INVALID_MESSAGE_ERROR          0x00
#define ASENCODE_LUN_FAILED_SELF_CONFIG         0x00
#define ASENCODE_OVERLAPPED_COMMAND             0x00

#define BYTE0(x) (unsigned char)(x)
#define BYTE1(x) (unsigned char)((x) >> 8)
#define BYTE2(x) (unsigned char)((x) >> 16)
#define BYTE3(x) (unsigned char)((x) >> 24)

/*------------------------------------------------------------------------------
 *              S T R U C T S / T Y P E D E F S
 *----------------------------------------------------------------------------*/
/* SCSI inquiry data */
struct inquiry_data {
	u8 inqd_pdt;	/* Peripheral qualifier | Peripheral Device Type  */
	u8 inqd_dtq;	/* RMB | Device Type Qualifier  */
	u8 inqd_ver;	/* ISO version | ECMA version | ANSI-approved version */
	u8 inqd_rdf;	/* AENC | TrmIOP | Response data format */
	u8 inqd_len;	/* Additional length (n-4) */
	u8 inqd_pad1[2];/* Reserved - must be zero */
	u8 inqd_pad2;	/* RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
	u8 inqd_vid[8];	/* Vendor ID */
	u8 inqd_pid[16];/* Product ID */
	u8 inqd_prl[4];	/* Product Revision Level */
};

/*
 *              M O D U L E   G L O B A L S
 */
 
static unsigned long aac_build_sg(struct scsi_cmnd* scsicmd, struct sgmap* sgmap);
static unsigned long aac_build_sg64(struct scsi_cmnd* scsicmd, struct sgmap64* psg);
static unsigned long aac_build_sgraw(struct scsi_cmnd* scsicmd, struct sgmapraw* psg);
static int aac_send_srb_fib(struct scsi_cmnd* scsicmd);
#ifdef AAC_DETAILED_STATUS_INFO
static char *aac_get_status_string(u32 status);
#endif

/*
 *	Non dasd selection is handled entirely in aachba now
 */	
 
MODULE_PARM(nondasd, "i");
MODULE_PARM_DESC(nondasd, "Control scanning of hba for nondasd devices. 0=off, 1=on");
MODULE_PARM(dacmode, "i");
MODULE_PARM_DESC(dacmode, "Control whether dma addressing is using 64 bit DAC. 0=off, 1=on");
MODULE_PARM(commit, "i");
MODULE_PARM_DESC(commit, "Control whether a COMMIT_CONFIG is issued to the adapter for foreign arrays.\nThis is typically needed in systems that do not have a BIOS. 0=off, 1=on");
MODULE_PARM(coalescethreshold, "i");
MODULE_PARM_DESC(coalescethreshold, "Control the maximum block size of sequential requests that are fed back to the\nscsi_merge layer for coalescing. 0=off, 16 block (8KB) default.");
MODULE_PARM(acbsize, "i");
MODULE_PARM_DESC(acbsize, "Request a specific adapter control block (FIB) size. Valid values are 512,\n2048, 4096 and 8192. Default is to use suggestion from Firmware.");
MODULE_PARM(overridetimeout, "i");
MODULE_PARM_DESC(overridetimeout, "Request a specific timeout to override I/O requests issed to the adapter.");

static int nondasd = -1;
static int dacmode = -1;
#ifdef __arm__
static int commit = 1;
#else
static int commit = -1;
#endif
static int coalescethreshold = 16; /* 8KB coalesce knee */
int acbsize = -1;
static int overridetimeout = -1;

/**
 *	aac_get_config_status	-	check the adapter configuration
 *	@common: adapter to query
 *
 *	Query config status, and commit the configuration if needed.
 */
int aac_get_config_status(struct aac_dev *dev)
{
	int status = 0;
	struct fib * fibptr;

	if (!(fibptr = fib_alloc(dev)))
		return -ENOMEM;

	fib_init(fibptr);
	{
		struct aac_get_config_status *dinfo;
		dinfo = (struct aac_get_config_status *) fib_data(fibptr);

		dinfo->command = cpu_to_le32(VM_ContainerConfig);
		dinfo->type = cpu_to_le32(CT_GET_CONFIG_STATUS);
		dinfo->count = cpu_to_le32(sizeof(((struct aac_get_config_status_resp *)NULL)->data));
	}

	status = fib_send(ContainerCommand,
			    fibptr,
			    sizeof (struct aac_get_config_status),
			    FsaNormal,
			    1, 1,
			    NULL, NULL);
	if (status < 0 ) {
		printk(KERN_WARNING "aac_get_config_status: SendFIB failed.\n");
	} else {
		struct aac_get_config_status_resp *reply
		  = (struct aac_get_config_status_resp *) fib_data(fibptr);
		dprintk((KERN_WARNING
		  "aac_get_config_status: response=%d status=%d action=%d\n",
		  reply->response, reply->status, reply->data.action));
		if ((reply->response != ST_OK)
		 || (reply->status != CT_OK)
		 || (reply->data.action > CFACT_PAUSE)) {
			printk(KERN_WARNING "aac_get_config_status: Will not issue the Commit Configuration\n");
			status = -EINVAL;
		}
	}
	fib_complete(fibptr);
	/* Send a CT_COMMIT_CONFIG to enable discovery of devices */
	if (status >= 0) {
		if (commit == 1) {
			struct aac_commit_config * dinfo;
			fib_init(fibptr);
			dinfo = (struct aac_commit_config *) fib_data(fibptr);
	
			dinfo->command = cpu_to_le32(VM_ContainerConfig);
			dinfo->type = cpu_to_le32(CT_COMMIT_CONFIG);
	
			status = fib_send(ContainerCommand,
				    fibptr,
				    sizeof (struct aac_commit_config),
				    FsaNormal,
				    1, 1,
				    NULL, NULL);
			fib_complete(fibptr);
		} else if (commit == 0) {
			printk(KERN_WARNING
			  "aac_get_config_status: Foreign device configurations are being ignored\n");
		}
	}
	fib_free(fibptr);
	return status;
}

/**
 *	aac_get_containers	-	list containers
 *	@common: adapter to probe
 *
 *	Make a list of all containers on this controller
 */
int aac_get_containers(struct aac_dev *dev)
{
	struct fsa_dev_info *fsa_dev_ptr;
	u32 index; 
	int status = 0;
	struct fib * fibptr;
	unsigned instance;

	instance = dev->scsi_host_ptr->unique_id;

	if (!(fibptr = fib_alloc(dev)))
		return -ENOMEM;

	{
		struct aac_get_container_count *dinfo;
		struct aac_get_container_count_resp *dresp;
		int maximum_num_containers = MAXIMUM_NUM_CONTAINERS;

		fib_init(fibptr);
		dinfo = (struct aac_get_container_count *) fib_data(fibptr);
	
		dinfo->command = cpu_to_le32(VM_ContainerConfig);
		dinfo->type = cpu_to_le32(CT_GET_CONTAINER_COUNT);
	
		status = fib_send(ContainerCommand,
			    fibptr,
			    sizeof (struct aac_get_container_count),
			    FsaNormal,
			    1, 1,
			    NULL, NULL);
		if (status >= 0) {
			dresp = (struct aac_get_container_count_resp *) fib_data(fibptr);
			maximum_num_containers = dresp->ContainerSwitchEntries;
//DEBUG
//printk(KERN_INFO "maximum_num_containers=%d\n", maximum_num_containers);
			fib_complete(fibptr);
		}

		if (maximum_num_containers < MAXIMUM_NUM_CONTAINERS)
			maximum_num_containers = MAXIMUM_NUM_CONTAINERS;
		fsa_dev_ptr = (struct fsa_dev_info *) kmalloc(
		  sizeof(*fsa_dev_ptr) * maximum_num_containers, GFP_KERNEL);
		if (!fsa_dev_ptr) {
			fib_free(fibptr);
			return -ENOMEM;
		}
		memset(fsa_dev_ptr, 0, sizeof(*fsa_dev_ptr) * maximum_num_containers);

		dev->fsa_dev = fsa_dev_ptr;
		dev->maximum_num_containers = maximum_num_containers;
	}

	for (index = 0; index < dev->maximum_num_containers; index++) {
		struct aac_query_mount *dinfo;
		struct aac_mount *dresp;

		fsa_dev_ptr[index].devname[0] = '\0';

		fib_init(fibptr);
		dinfo = (struct aac_query_mount *) fib_data(fibptr);

		dinfo->command = cpu_to_le32(VM_NameServe);
		dinfo->count = cpu_to_le32(index);
		dinfo->type = cpu_to_le32(FT_FILESYS);

		status = fib_send(ContainerCommand,
				    fibptr,
				    sizeof (struct aac_query_mount),
				    FsaNormal,
				    1, 1,
				    NULL, NULL);
		if (status < 0 ) {
			printk(KERN_WARNING "aac_get_containers: SendFIB failed.\n");
			break;
		}
		dresp = (struct aac_mount *)fib_data(fibptr);

		dprintk ((KERN_DEBUG
		  "VM_NameServe cid=%d status=%d vol=%d state=%d cap=%u\n",
		  (int)index, (int)le32_to_cpu(dresp->status),
		  (int)le32_to_cpu(dresp->mnt[0].vol),
		  (int)le32_to_cpu(dresp->mnt[0].state),
		  (unsigned)le32_to_cpu(dresp->mnt[0].capacity)));
		if ((le32_to_cpu(dresp->status) == ST_OK) &&
		    (le32_to_cpu(dresp->mnt[0].vol) != CT_NONE) &&
		    (le32_to_cpu(dresp->mnt[0].state) != FSCS_HIDDEN)) {
			fsa_dev_ptr[index].valid = 1;
			fsa_dev_ptr[index].type = le32_to_cpu(dresp->mnt[0].vol);
			fsa_dev_ptr[index].size = le32_to_cpu(dresp->mnt[0].capacity);
			if (le32_to_cpu(dresp->mnt[0].state) & FSCS_READONLY)
				    fsa_dev_ptr[index].ro = 1;
		}
		fib_complete(fibptr);
		/*
		 *	If there are no more containers, then stop asking.
		 */
		if ((index + 1) >= le32_to_cpu(dresp->count)){
			break;
		}
	}
	fib_free(fibptr);
	return status;
}

//DEBUG
//static void aacraid_timeout_dummy_fn(struct scsi_cmnd * scsicmd)
//{
//	return;
//}

static void aac_io_done(struct scsi_cmnd * scsicmd)
{
	unsigned long cpu_flags;
	struct Scsi_Host *host = scsicmd->device->host;

	if (scsicmd->scsi_done == (void (*)(struct scsi_cmnd*))NULL) {
		printk(KERN_WARNING "aac_io_done: scsi_done NULL\n");
		return;
	}
	spin_lock_irqsave(host->host_lock, cpu_flags);
	/*
	 * Tell system to perform scsi_done functionality even despite
	 * being in the error recovery code. Leave a `timer' to delete
	 * so that completion can progress.
	 */
//DEBUG
//	scsi_add_timer(scsicmd, 60*HZ, aacraid_timeout_dummy_fn);
	scsicmd->scsi_done(scsicmd);
//DEBUG
//	if (scsicmd->serial_number) {
//		scsi_add_timer(scsicmd, 60*HZ, aacraid_timeout_dummy_fn);
//		scsicmd->scsi_done(scsicmd);
//	}
	spin_unlock_irqrestore(host->host_lock, cpu_flags);
}

static inline void __aac_io_done(struct scsi_cmnd * scsicmd)
{
	scsicmd->scsi_done(scsicmd);
}

static void get_container_name_callback(void *context, struct fib * fibptr)
{
	struct aac_get_name_resp * get_name_reply;
	struct scsi_cmnd * scsicmd;

	scsicmd = (struct scsi_cmnd *) context;

	dprintk((KERN_DEBUG "get_container_name_callback[cpu %d]: t = %ld.\n", smp_processor_id(), jiffies));
	if (fibptr == NULL)
		BUG();

	get_name_reply = (struct aac_get_name_resp *) fib_data(fibptr);
	/* Failure is irrelevant, using default value instead */
	if ((le32_to_cpu(get_name_reply->status) == CT_OK)
	 && (get_name_reply->data[0] != '\0')) {
		char * sp = get_name_reply->data;
		char * dp = ((struct inquiry_data *)scsicmd->request_buffer)->inqd_pid;
		int    count = sizeof(((struct aac_get_name_resp *)NULL)->data);
		do {
			if ((*sp == '\0') || (count <= 0)) {
				*dp++ = ' ';
			} else {
				*dp++ = *sp++;
			}
		} while (--count
		  > (sizeof(((struct aac_get_name_resp *)NULL)->data)
		   - sizeof(((struct inquiry_data *)NULL)->inqd_pid)));
	}
	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	fib_complete(fibptr);
	fib_free(fibptr);
	aac_io_done(scsicmd);
}

/**
 *	aac_get_container_name	-	get container name, none blocking.
 */
static int aac_get_container_name(struct scsi_cmnd * scsicmd, int cid)
{
	int status;
	struct aac_get_name *dinfo;
	struct fib * cmd_fibcontext;
	struct aac_dev * dev;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;

	if (!(cmd_fibcontext = fib_alloc(dev)))
		return -ENOMEM;

	fib_init(cmd_fibcontext);
	dinfo = (struct aac_get_name *) fib_data(cmd_fibcontext);

	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_READ_NAME);
	dinfo->cid = cpu_to_le32(cid);
	dinfo->count = cpu_to_le32(sizeof(((struct aac_get_name_resp *)NULL)->data));

	status = fib_send(ContainerCommand, 
		  cmd_fibcontext, 
		  sizeof (struct aac_get_name),
		  FsaNormal, 
		  0, 1, 
		  (fib_callback) get_container_name_callback, 
		  (void *) scsicmd);
	
	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) 
		return 0;
		
	printk(KERN_WARNING "aac_get_container_name: fib_send failed with status: %d.\n", status);
	fib_complete(cmd_fibcontext);
	fib_free(cmd_fibcontext);
	return -1;
}

/**
 *	probe_container		-	query a logical volume
 *	@dev: device to query
 *	@cid: container identifier
 *
 *	Queries the controller about the given volume. The volume information
 *	is updated in the struct fsa_dev_info structure rather than returned.
 */
 
static int probe_container(struct aac_dev *dev, int cid)
{
	struct fsa_dev_info *fsa_dev_ptr;
	int status;
	struct aac_query_mount *dinfo;
	struct aac_mount *dresp;
	struct fib * fibptr;
	unsigned instance;

	fsa_dev_ptr = dev->fsa_dev;
	instance = dev->scsi_host_ptr->unique_id;

	if (!(fibptr = fib_alloc(dev)))
		return -ENOMEM;

	fib_init(fibptr);

	dinfo = (struct aac_query_mount *)fib_data(fibptr);

	dinfo->command = cpu_to_le32(VM_NameServe);
	dinfo->count = cpu_to_le32(cid);
	dinfo->type = cpu_to_le32(FT_FILESYS);

	status = fib_send(ContainerCommand,
			    fibptr,
			    sizeof(struct aac_query_mount),
			    FsaNormal,
			    1, 1,
			    NULL, NULL);
	if (status < 0) {
		printk(KERN_WARNING "aacraid: probe_container query failed.\n");
		goto error;
	}

	dresp = (struct aac_mount *) fib_data(fibptr);

	if ((le32_to_cpu(dresp->status) == ST_OK) &&
	    (le32_to_cpu(dresp->mnt[0].vol) != CT_NONE) &&
	    (le32_to_cpu(dresp->mnt[0].state) != FSCS_HIDDEN)) {
		fsa_dev_ptr[cid].valid = 1;
		fsa_dev_ptr[cid].type = le32_to_cpu(dresp->mnt[0].vol);
		fsa_dev_ptr[cid].size = le32_to_cpu(dresp->mnt[0].capacity);
		if (le32_to_cpu(dresp->mnt[0].state) & FSCS_READONLY)
			fsa_dev_ptr[cid].ro = 1;
	}

error:
	fib_complete(fibptr);
	fib_free(fibptr);

	return status;
}

/* Local Structure to set SCSI inquiry data strings */
struct scsi_inq {
	char vid[8];         /* Vendor ID */
	char pid[16];        /* Product ID */
	char prl[4];         /* Product Revision Level */
};

/**
 *	InqStrCopy	-	string merge
 *	@a:	string to copy from
 *	@b:	string to copy to
 *
 * 	Copy a String from one location to another
 *	without copying \0
 */

static void inqstrcpy(char *a, char *b)
{

	while(*a != (char)0) 
		*b++ = *a++;
}

static char *container_types[] = {
        "None",
        "Volume",
        "Mirror",
        "Stripe",
        "RAID5",
        "SSRW",
        "SSRO",
        "Morph",
        "Legacy",
        "RAID4",
        "RAID10",             
        "RAID00",             
        "V-MIRRORS",          
        "PSEUDO R4",          
	"RAID50",
        "Unknown"
};



/* Function: setinqstr
 *
 * Arguments: [1] pointer to void [1] int
 *
 * Purpose: Sets SCSI inquiry data strings for vendor, product
 * and revision level. Allows strings to be set in platform dependant
 * files instead of in OS dependant driver source.
 */

static void setinqstr(int devtype, void *data, int tindex)
{
	struct scsi_inq *str;
	struct aac_driver_ident *mp;

	mp = aac_get_driver_ident(devtype);
   
	str = (struct scsi_inq *)(data); /* cast data to scsi inq block */

	inqstrcpy (mp->vname, str->vid); 
	inqstrcpy (mp->model, str->pid); /* last six chars reserved for vol type */

	if (tindex < (sizeof(container_types)/sizeof(char *))){
		char *findit = str->pid;

		for ( ; *findit != ' '; findit++); /* walk till we find a space */
		/* RAID is superfluous in the context of a RAID device */
		if (memcmp(findit-4, "RAID", 4) == 0)
			*(findit -= 4) = ' ';
		inqstrcpy (container_types[tindex], findit + 1);
	}
	inqstrcpy ("V1.0", str->prl);
}

void set_sense(u8 *sense_buf, u8 sense_key, u8 sense_code,
		    u8 a_sense_code, u8 incorrect_length,
		    u8 bit_pointer, u16 field_pointer,
		    u32 residue)
{
	sense_buf[0] = 0xF0;	/* Sense data valid, err code 70h (current error) */
	sense_buf[1] = 0;	/* Segment number, always zero */

	if (incorrect_length) {
		sense_buf[2] = sense_key | 0x20;/* Set ILI bit | sense key */
		sense_buf[3] = BYTE3(residue);
		sense_buf[4] = BYTE2(residue);
		sense_buf[5] = BYTE1(residue);
		sense_buf[6] = BYTE0(residue);
	} else
		sense_buf[2] = sense_key;	/* Sense key */

	if (sense_key == ILLEGAL_REQUEST)
		sense_buf[7] = 10;	/* Additional sense length */
	else
		sense_buf[7] = 6;	/* Additional sense length */

	sense_buf[12] = sense_code;	/* Additional sense code */
	sense_buf[13] = a_sense_code;	/* Additional sense code qualifier */
	if (sense_key == ILLEGAL_REQUEST) {
		sense_buf[15] = 0;

		if (sense_code == SENCODE_INVALID_PARAM_FIELD)
			sense_buf[15] = 0x80;/* Std sense key specific field */
		/* Illegal parameter is in the parameter block */

		if (sense_code == SENCODE_INVALID_CDB_FIELD)
			sense_buf[15] = 0xc0;/* Std sense key specific field */
		/* Illegal parameter is in the CDB block */
		sense_buf[15] |= bit_pointer;
		sense_buf[16] = field_pointer >> 8;	/* MSB */
		sense_buf[17] = field_pointer;		/* LSB */
	}
}

int aac_get_adapter_info(struct aac_dev* dev)
{
	struct fib* fibptr;
	struct aac_adapter_info* info;
	int rcode;
	u32 tmp;
	if (!(fibptr = fib_alloc(dev)))
		return -ENOMEM;

	fib_init(fibptr);
	info = (struct aac_adapter_info*) fib_data(fibptr);

	memset(info,0,sizeof(struct aac_adapter_info));

	rcode = fib_send(RequestAdapterInfo,
			fibptr, 
			sizeof(struct aac_adapter_info),
			FsaNormal, 
			1, 1, 
			NULL, 
			NULL);

	memcpy(&dev->adapter_info, info, sizeof(struct aac_adapter_info));

	tmp = dev->adapter_info.kernelrev;
	printk(KERN_INFO "%s%d: kernel %d.%d-%d build %d\n", 
			dev->name, dev->id,
			tmp>>24,(tmp>>16)&0xff,tmp&0xff,
			dev->adapter_info.kernelbuild);
	tmp = dev->adapter_info.monitorrev;
	printk(KERN_INFO "%s%d: monitor %d.%d-%d build %d\n", 
			dev->name, dev->id,
			tmp>>24,(tmp>>16)&0xff,tmp&0xff,
			dev->adapter_info.monitorbuild);
	tmp = dev->adapter_info.biosrev;
	printk(KERN_INFO "%s%d: bios %d.%d-%d build %d\n", 
			dev->name, dev->id,
			tmp>>24,(tmp>>16)&0xff,tmp&0xff,
			dev->adapter_info.biosbuild);
	if (dev->adapter_info.serial[0] != 0xBAD0)
		printk(KERN_INFO "%s%d: serial %x\n",
			dev->name, dev->id,
			dev->adapter_info.serial[0]);

	dev->nondasd_support = 0;
	dev->raid_scsi_mode = 0;
	if(dev->adapter_info.options & AAC_OPT_NONDASD){
		dev->nondasd_support = 1;
	}

	/*
	 * If the firmware supports ROMB RAID/SCSI mode and we are currently
	 * in RAID/SCSI mode, set the flag. For now if in this mode we will
	 * force nondasd support on. If we decide to allow the non-dasd flag
	 * additional changes changes will have to be made to support
	 * RAID/SCSI.  the function aac_scsi_cmd in this module will have to be
	 * changed to support the new dev->raid_scsi_mode flag instead of
	 * leaching off of the dev->nondasd_support flag. Also in linit.c the
	 * function aac_detect will have to be modified where it sets up the
	 * max number of channels based on the aac->nondasd_support flag only.
	 */
	if ((dev->adapter_info.options & AAC_OPT_SCSI_MANAGED)
		&& (dev->adapter_info.options & AAC_OPT_RAID_SCSI_MODE))
	{
		dev->nondasd_support = 1;
		dev->raid_scsi_mode = 1;
	}
	if (dev->raid_scsi_mode != 0)
		printk(KERN_INFO "%s%d: ROMB RAID/SCSI mode enabled\n",dev->name, dev->id);
		
	if(nondasd != -1) {  
		dev->nondasd_support = (nondasd!=0);
	}
	if(dev->nondasd_support != 0)
		printk(KERN_INFO"%s%d: Non-DASD support enabled\n",dev->name, dev->id);

	dev->dac_support = 0;
	/*
	 *	Only enable DAC mode if the dma_addr_t is larger than 32
	 * bit addressing, and we have more than 32 bit addressing worth of
	 * memory and if the controller supports 64 bit scatter gather elements.
	 */
	if( (sizeof(dma_addr_t) > 4) && (num_physpages > (0xFFFFFFFFULL >> PAGE_SHIFT)) && (dev->adapter_info.options & AAC_OPT_SGMAP_HOST64)){
		dev->dac_support = 1;
	}

	if(dacmode != -1){
		dev->dac_support = (dacmode!=0);
	}
	if(dev->dac_support != 0) {
		if (!pci_set_dma_mask(dev->pdev, (dma_addr_t)0xFFFFFFFFFFFFFFFFULL) &&
			!pci_set_consistent_dma_mask(dev->pdev, (dma_addr_t)0xFFFFFFFFFFFFFFFFULL)) {
			printk(KERN_INFO"%s%d: 64 Bit DAC enabled\n",
				dev->name, dev->id);
		} else if (!pci_set_dma_mask(dev->pdev, (dma_addr_t)0xFFFFFFFFULL) &&
			!pci_set_consistent_dma_mask(dev->pdev, (dma_addr_t)0xFFFFFFFFULL)) {
			printk(KERN_INFO"%s%d: DMA mask set failed, 64 Bit DAC disabled\n",
				dev->name, dev->id);
			dev->dac_support = 0;
		} else {
			printk(KERN_WARNING"%s%d: No suitable DMA available.\n",
				dev->name, dev->id);
			rcode = -ENOMEM;
		}
	}
	/* 57 scatter gather elements */
	if (!(dev->raw_io_interface)) {
		dev->scsi_host_ptr->sg_tablesize = (dev->max_fib_size
			- sizeof(struct aac_fibhdr)
			- sizeof(struct aac_write) + sizeof(struct sgmap))
				/ sizeof(struct sgmap);
		if( (sizeof(dma_addr_t) <= 4) || (num_physpages < (0xFFFFFFFFULL >> PAGE_SHIFT)) || (dev->adapter_info.options & AAC_OPT_SGMAP_HOST64) || (dev->dac_support != 0) ){
			/* 38 scatter gather elements */
			dev->scsi_host_ptr->sg_tablesize
				= (dev->max_fib_size
				- sizeof(struct aac_fibhdr)
				- sizeof(struct aac_write64)
				+ sizeof(struct sgmap64))
					/ sizeof(struct sgmap64);
		}
		dev->scsi_host_ptr->max_sectors = AAC_MAX_32BIT_SGBCOUNT;
		if(!(dev->adapter_info.options & AAC_OPT_NEW_COMM)) {
			/*
			 * Worst case size that could cause sg overflow when
			 * we break up SG elements that are larger than 64KB.
			 * Would be nice if we could tell the SCSI layer what
			 * the maximum SG element size can be. Worst case is
			 * (sg_tablesize-1) 4KB elements with one 64KB
			 * element.
			 *	32bit -> 468 or 238KB	64bit -> 424 or 212KB
			 */
			dev->scsi_host_ptr->max_sectors
			  = (dev->scsi_host_ptr->sg_tablesize * 8) + 120;
		}
	}

	fib_complete(fibptr);
	fib_free(fibptr);

	return rcode;
}

static void io_callback(void *context, struct fib * fibptr)
{
	struct aac_dev *dev;
	struct aac_read_reply *readreply;
	struct scsi_cmnd *scsicmd;
	u32 cid;

	scsicmd = (struct scsi_cmnd *) context;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	cid = ID_LUN_TO_CONTAINER(scsicmd->device->id, scsicmd->device->lun);

	if (nblank(dprintk(x))) {
		u64 lba;
		if ((scsicmd->cmnd[0] == WRITE_6)	/* 6 byte command */
		 || (scsicmd->cmnd[0] == READ_6))
			lba = ((scsicmd->cmnd[1] & 0x1F) << 16)
			    | (scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
#ifdef WRITE_16
		else if ((scsicmd->cmnd[0] == WRITE_16)	/* 16 byte command */
		 || (scsicmd->cmnd[0] == READ_16))
			lba = ((u64)scsicmd->cmnd[2] << 56)
			    | ((u64)scsicmd->cmnd[3] << 48)
			    | ((u64)scsicmd->cmnd[4] << 40)
			    | ((u64)scsicmd->cmnd[9] << 32)
			    | ((u64)scsicmd->cmnd[6] << 24)
			    | (scsicmd->cmnd[7] << 16)
			    | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
#endif
		else if ((scsicmd->cmnd[0] == WRITE_12)	/* 12 byte command */
		 || (scsicmd->cmnd[0] == READ_12))
			lba = ((u64)scsicmd->cmnd[2] << 24)
			    | (scsicmd->cmnd[3] << 16)
			    | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		else
			lba = ((u64)scsicmd->cmnd[2] << 24)
			    | (scsicmd->cmnd[3] << 16)
			    | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		printk(KERN_DEBUG
		  "io_callback[cpu %d]: lba = %llu, t = %ld.\n",
		  smp_processor_id(), (unsigned long long)lba, jiffies);
	}

	if (fibptr == NULL)
		BUG();
		
	if(scsicmd->use_sg)
		pci_unmap_sg(dev->pdev, 
			(struct scatterlist *)scsicmd->buffer,
			scsicmd->use_sg,
			scsicmd->sc_data_direction);
	else if(scsicmd->request_bufflen)
		pci_unmap_single(dev->pdev, scsicmd->SCp.dma_handle,
				 scsicmd->request_bufflen,
				 scsicmd->sc_data_direction);
	readreply = (struct aac_read_reply *)fib_data(fibptr);
	if (le32_to_cpu(readreply->status) == ST_OK)
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
	else {
		printk(KERN_WARNING "io_callback: io failed, status = %d\n", le32_to_cpu(readreply->status));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				    HARDWARE_ERROR,
				    SENCODE_INTERNAL_TARGET_FAILURE,
				    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
				    0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
	}
	fib_complete(fibptr);
	fib_free(fibptr);

	aac_io_done(scsicmd);
}

static inline void aac_select_queue_depth(
	struct scsi_cmnd * scsicmd,
	int cid,
	u64 lba,
	u32 count)
{
	struct scsi_device *device = scsicmd->device;
	struct aac_dev *dev;
	unsigned depth;

	if (!device->tagged_supported)
		return;
	dev = (struct aac_dev *)device->host->hostdata;
	if (dev->fsa_dev[cid].queue_depth <= 2)
		dev->fsa_dev[cid].queue_depth = device->queue_depth;
	if (lba == dev->fsa_dev[cid].last) {
		/*
		 * If larger than coalescethreshold in size, coalescing has
		 * less effect on overall performance.  Also, if we are
		 * coalescing right now, leave it alone if above the threshold.
		 */
		if (count > coalescethreshold)
			return;
		depth = 2;
	} else {
		depth = dev->fsa_dev[cid].queue_depth;
	}
	scsi_adjust_queue_depth(device, MSG_ORDERED_TAG, depth);
	dprintk((KERN_DEBUG "l=%llu %llu[%u] q=%u %lu\n",
	  dev->fsa_dev[cid].last, lba, count, device->queue_depth,
	  dev->queues->queue[AdapNormCmdQueue].numpending));
	dev->fsa_dev[cid].last = lba + count;
}

int aac_read(struct scsi_cmnd * scsicmd, int cid)
{
	u64 lba;
	u32 count;
	int status;

	u16 fibsize;
	struct aac_dev *dev;
	struct fib * cmd_fibcontext;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	/*
	 *	Get block address and transfer length
	 */
	dprintk((KERN_DEBUG "aac_read: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	  scsicmd->cmnd[0],  scsicmd->cmnd[1],  scsicmd->cmnd[2],
	  scsicmd->cmnd[3],  scsicmd->cmnd[4],  scsicmd->cmnd[5],
	  scsicmd->cmnd[6],  scsicmd->cmnd[7],  scsicmd->cmnd[8],
	  scsicmd->cmnd[9],  scsicmd->cmnd[10], scsicmd->cmnd[11],
	  scsicmd->cmnd[12], scsicmd->cmnd[13], scsicmd->cmnd[14],
	  scsicmd->cmnd[15]));
	if (scsicmd->cmnd[0] == READ_6)	/* 6 byte command */
	{
		dprintk((KERN_DEBUG "aachba: received a read(6) command on id %d.\n", cid));

		lba = ((scsicmd->cmnd[1] & 0x1F) << 16) | (scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
		count = scsicmd->cmnd[4];

		if (count == 0)
			count = 256;
#ifdef READ_16
	} else if (scsicmd->cmnd[0] == READ_16) { /* 16 byte command */
		dprintk((KERN_DEBUG "aachba: received a read(16) command on id %d.\n", cid));

		lba = ((u64)scsicmd->cmnd[2] << 56)
		    | ((u64)scsicmd->cmnd[3] << 48)
		    | ((u64)scsicmd->cmnd[4] << 40)
		    | ((u64)scsicmd->cmnd[9] << 32)
		    | ((u64)scsicmd->cmnd[6] << 24) | (scsicmd->cmnd[7] << 16)
		    | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		count = (scsicmd->cmnd[10] << 24) | (scsicmd->cmnd[11] << 16)
		      | (scsicmd->cmnd[12] << 8) | scsicmd->cmnd[13];
#endif
	} else if (scsicmd->cmnd[0] == READ_12) { /* 12 byte command */
		dprintk((KERN_DEBUG "aachba: received a read(12) command on id %d.\n", cid));

		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16)
		    | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[6] << 24) | (scsicmd->cmnd[7] << 16)
		      | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
	} else {
		dprintk((KERN_DEBUG "aachba: received a read(10) command on id %d.\n", cid));

		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16) | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[7] << 8) | scsicmd->cmnd[8];
	}
	dprintk((KERN_DEBUG "aac_read[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if ((!(dev->raw_io_interface) || !(dev->raw_io_64))
	 && (lba & 0xffffffff00000000LL)) {
		dprintk((KERN_DEBUG "aac_read: Illegal lba\n"));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
			    HARDWARE_ERROR,
			    SENCODE_INTERNAL_TARGET_FAILURE,
			    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
			    0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
		__aac_io_done(scsicmd);
		return 0;
	}
	/*
	 *	Are we in a sequential mode?
	 */
	aac_select_queue_depth(scsicmd, cid, lba, count);
	/*
	 *	Alocate and initialize a Fib
	 */
	if (!(cmd_fibcontext = fib_alloc(dev))) {
		return -1;
	}

	fib_init(cmd_fibcontext);

	if (dev->raw_io_interface) {
		struct aac_raw_io *readcmd;
		readcmd = (struct aac_raw_io *) fib_data(cmd_fibcontext);
		readcmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
		readcmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		readcmd->count = cpu_to_le32(count<<9);
		readcmd->cid = cpu_to_le16(cid);
		readcmd->flags = 1; 
		readcmd->bpTotal = 0;
		readcmd->bpComplete = 0;
		
		aac_build_sgraw(scsicmd, &readcmd->sg);
		fibsize = sizeof(struct aac_raw_io) + ((readcmd->sg.count - 1) * sizeof (struct sgentryraw));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerRawIo,
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	} else if (dev->dac_support == 1) {
		struct aac_read64 *readcmd;
		readcmd = (struct aac_read64 *) fib_data(cmd_fibcontext);
		readcmd->command = cpu_to_le32(VM_CtHostRead64);
		readcmd->cid = cpu_to_le16(cid);
		readcmd->sector_count = cpu_to_le16(count);
		readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
		readcmd->pad   = 0;
		readcmd->flags = 0; 
		
		aac_build_sg64(scsicmd, &readcmd->sg);
		fibsize = sizeof(struct aac_read64) + ((readcmd->sg.count - 1) * sizeof (struct sgentry64));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerCommand64, 
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	} else {
		struct aac_read *readcmd;
		readcmd = (struct aac_read *) fib_data(cmd_fibcontext);
		readcmd->command = cpu_to_le32(VM_CtBlockRead);
		readcmd->cid = cpu_to_le32(cid);
		readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
		readcmd->count = cpu_to_le32(count * 512);

		aac_build_sg(scsicmd, &readcmd->sg);
		fibsize = sizeof(struct aac_read) + ((readcmd->sg.count - 1) * sizeof (struct sgentry));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerCommand, 
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	}

	

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) 
		return 0;
		
	printk(KERN_WARNING "aac_read: fib_send failed with status: %d.\n", status);
	fib_complete(cmd_fibcontext);
	fib_free(cmd_fibcontext);
	return -1;
}

static int aac_write(struct scsi_cmnd * scsicmd, int cid)
{
	u64 lba;
	u32 count;
	int status;
	u16 fibsize;
	struct aac_dev *dev;
	struct fib * cmd_fibcontext;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	/*
	 *	Get block address and transfer length
	 */
	dprintk((KERN_DEBUG "aac_write: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	  scsicmd->cmnd[0],  scsicmd->cmnd[1],  scsicmd->cmnd[2],
	  scsicmd->cmnd[3],  scsicmd->cmnd[4],  scsicmd->cmnd[5],
	  scsicmd->cmnd[6],  scsicmd->cmnd[7],  scsicmd->cmnd[8],
	  scsicmd->cmnd[9],  scsicmd->cmnd[10], scsicmd->cmnd[11],
	  scsicmd->cmnd[12], scsicmd->cmnd[13], scsicmd->cmnd[14],
	  scsicmd->cmnd[15]));
	if (scsicmd->cmnd[0] == WRITE_6)	/* 6 byte command */
	{
		lba = ((scsicmd->cmnd[1] & 0x1F) << 16) | (scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
		count = scsicmd->cmnd[4];
		if (count == 0)
			count = 256;
#ifdef WRITE_16
	} else if (scsicmd->cmnd[0] == WRITE_16) { /* 16 byte command */
		dprintk((KERN_DEBUG "aachba: received a write(16) command on id %d.\n", cid));

		lba = ((u64)scsicmd->cmnd[2] << 56)
		    | ((u64)scsicmd->cmnd[3] << 48)
		    | ((u64)scsicmd->cmnd[4] << 40)
		    | ((u64)scsicmd->cmnd[9] << 32)
		    | ((u64)scsicmd->cmnd[6] << 24) | (scsicmd->cmnd[7] << 16)
		    | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		count = (scsicmd->cmnd[10] << 24) | (scsicmd->cmnd[11] << 16)
		      | (scsicmd->cmnd[12] << 8) | scsicmd->cmnd[13];
#endif
	} else if (scsicmd->cmnd[0] == WRITE_12) { /* 12 byte command */
		dprintk((KERN_DEBUG "aachba: received a write(12) command on id %d.\n", cid));

		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16)
		    | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[6] << 24) | (scsicmd->cmnd[7] << 16)
		      | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
	} else {
		dprintk((KERN_DEBUG "aachba: received a write(10) command on id %d.\n", cid));
		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16) | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[7] << 8) | scsicmd->cmnd[8];
	}
	dprintk((KERN_DEBUG "aac_write[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if ((!(dev->raw_io_interface) || !(dev->raw_io_64))
	 && (lba & 0xffffffff00000000LL)) {
		dprintk((KERN_DEBUG "aac_write: Illegal lba\n"));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
			    HARDWARE_ERROR,
			    SENCODE_INTERNAL_TARGET_FAILURE,
			    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
			    0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
		__aac_io_done(scsicmd);
		return 0;
	}
	/*
	 *	Are we in a sequential mode?
	 */
	aac_select_queue_depth(scsicmd, cid, lba, count);
	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	if (!(cmd_fibcontext = fib_alloc(dev))) {
		return -1;
	}
	fib_init(cmd_fibcontext);

	if (dev->raw_io_interface) {
		struct aac_raw_io *writecmd;
		writecmd = (struct aac_raw_io *) fib_data(cmd_fibcontext);
		writecmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
		writecmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		writecmd->count = cpu_to_le32(count<<9);
		writecmd->cid = cpu_to_le16(cid);
		writecmd->flags = 0; 
		writecmd->bpTotal = 0;
		writecmd->bpComplete = 0;
		
		aac_build_sgraw(scsicmd, &writecmd->sg);
		fibsize = sizeof(struct aac_raw_io) + ((writecmd->sg.count - 1) * sizeof (struct sgentryraw));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerRawIo,
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	} else if (dev->dac_support == 1) {
		struct aac_write64 *writecmd;
		writecmd = (struct aac_write64 *) fib_data(cmd_fibcontext);
		writecmd->command = cpu_to_le32(VM_CtHostWrite64);
		writecmd->cid = cpu_to_le16(cid);
		writecmd->sector_count = cpu_to_le16(count); 
		writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
		writecmd->pad	= 0;
		writecmd->flags	= 0;

		aac_build_sg64(scsicmd, &writecmd->sg);
		fibsize = sizeof(struct aac_write64) + ((writecmd->sg.count - 1) * sizeof (struct sgentry64));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerCommand64, 
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	} else {
		struct aac_write *writecmd;
		writecmd = (struct aac_write *) fib_data(cmd_fibcontext);
		writecmd->command = cpu_to_le32(VM_CtBlockWrite);
		writecmd->cid = cpu_to_le32(cid);
		writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
		writecmd->count = cpu_to_le32(count * 512);
		writecmd->sg.count = cpu_to_le32(1);
		/* ->stable is not used - it did mean which type of write */

		aac_build_sg(scsicmd, &writecmd->sg);
		fibsize = sizeof(struct aac_write) + ((writecmd->sg.count - 1) * sizeof (struct sgentry));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();
		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ContainerCommand, 
			  cmd_fibcontext, 
			  fibsize, 
			  FsaNormal, 
			  0, 1, 
			  (fib_callback) io_callback, 
			  (void *) scsicmd);
	}

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

	printk(KERN_WARNING "aac_write: fib_send failed with status: %d\n", status);

	fib_complete(cmd_fibcontext);
	fib_free(cmd_fibcontext);
	return -1;
}

static void synchronize_callback(void *context, struct fib * fibptr)
{
	struct aac_synchronize_reply * synchronizereply;
	struct scsi_cmnd * scsicmd;

	scsicmd = (struct scsi_cmnd *) context;

	dprintk((KERN_DEBUG "synchronize_callback[cpu %d]: t = %ld.\n", smp_processor_id(), jiffies));
	if (fibptr == NULL)
		BUG();


	synchronizereply = (struct aac_synchronize_reply *) fib_data(fibptr);
	if (le32_to_cpu(synchronizereply->status) == CT_OK) {
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
	} else {
		struct scsi_device * device = scsicmd->device;
		struct aac_dev *dev = (struct aac_dev *)device->host->hostdata;
		u32 cid = ID_LUN_TO_CONTAINER(device->id, device->lun);
		printk(KERN_WARNING "synchronize_callback: synchronize failed, status = %d\n", synchronizereply->status);
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				    HARDWARE_ERROR,
				    SENCODE_INTERNAL_TARGET_FAILURE,
				    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
				    0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
	}

	fib_complete(fibptr);
	fib_free(fibptr);
	aac_io_done(scsicmd);
}

static int aac_synchronize(struct scsi_cmnd * scsicmd, int cid)
{
	int status;
	struct fib * cmd_fibcontext;
	struct aac_synchronize * synchronizecmd;

	/*
	 * Wait for all commands to complete to this specific
	 * target (block).
	 */
	struct scsi_cmnd * cmd;
	struct scsi_device * device = scsicmd->device;
	int active = 0;
	unsigned long flags;
	spin_lock_irqsave(&device->list_lock, flags);
	list_for_each_entry(cmd, &device->cmd_list, list) {
		if ((cmd != scsicmd) && (cmd->serial_number != 0)) {
			++active;
			break;
		}
	}
	spin_unlock_irqrestore(&device->list_lock, flags);
	if (active) {
		/*
		 *	Yield the processor (requeue for later)
		 */
		return -1;
	}

	dprintk((KERN_DEBUG "aac_synchronize[cpu %d]: t = %ld.\n", smp_processor_id(), jiffies));
	/*
	 *	Alocate and initialize a Fib
	 */
	if (!(cmd_fibcontext = fib_alloc((struct aac_dev *)scsicmd->device->host->hostdata))) {
		return -1;
	}

	fib_init(cmd_fibcontext);

	synchronizecmd = (struct aac_synchronize *) fib_data(cmd_fibcontext);
	synchronizecmd->command = cpu_to_le32(VM_ContainerConfig);
	synchronizecmd->type = cpu_to_le32(CT_FLUSH_CACHE);
	synchronizecmd->cid = cpu_to_le32(cid);
	synchronizecmd->count = cpu_to_le32(sizeof(((struct aac_synchronize_reply *)NULL)->data));

	/*
	 *	Now send the Fib to the adapter
	 */
	status = fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof(struct aac_synchronize),
		  FsaNormal,
		  0, 1,
		  (fib_callback) synchronize_callback,
		  (void *) scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		return 0;
	}

	printk(KERN_WARNING "aac_synchronize: fib_send failed with status: %d.\n", status);
	fib_complete(cmd_fibcontext);
	fib_free(cmd_fibcontext);
	return -1;
}


/**
 *	aac_scsi_cmd()		-	Process SCSI command
 *	@scsicmd:		SCSI command block
 *
 *	Emulate a SCSI command and queue the required request for the
 *	aacraid firmware.
 */
 
int aac_scsi_cmd(struct scsi_cmnd * scsicmd)
{
	u32 cid = 0;
	struct Scsi_Host *host = scsicmd->device->host;
	struct aac_dev *dev = (struct aac_dev *)host->hostdata;
	struct fsa_dev_info *fsa_dev_ptr = dev->fsa_dev;
	int cardtype = dev->cardtype;

	/*
	 *	If the bus, id or lun is out of range, return fail
	 *	Test does not apply to ID 16, the pseudo id for the controller
	 *	itself.
	 */
	if (scsicmd->device->id != host->this_id) {
		if ((scsicmd->device->channel == 0) ){
			if( (scsicmd->device->id >= dev->maximum_num_containers) || (scsicmd->device->lun != 0)){ 
				scsicmd->result = DID_NO_CONNECT << 16;
				__aac_io_done(scsicmd);
				return 0;
			}
			cid = ID_LUN_TO_CONTAINER(scsicmd->device->id, scsicmd->device->lun);

			/*
			 *	If the target container doesn't exist, it may have
			 *	been newly created
			 */
			if ((fsa_dev_ptr[cid].valid & 1) == 0) {
				switch (scsicmd->cmnd[0]) {
#ifdef SERVICE_ACTION_IN
				case SERVICE_ACTION_IN:
					if ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16)
						break;
#endif
				case INQUIRY:
				case READ_CAPACITY:
				case TEST_UNIT_READY:
					spin_unlock_irq(host->host_lock);
					probe_container(dev, cid);
					spin_lock_irq(host->host_lock);
					if (fsa_dev_ptr[cid].valid == 0) {
						scsicmd->result = DID_NO_CONNECT << 16;
						__aac_io_done(scsicmd);
						return 0;
					}
				default:
					break;
				}
			}
			/*
			 *	If the target container still doesn't exist, 
			 *	return failure
			 */
			if (fsa_dev_ptr[cid].valid == 0) {
				scsicmd->result = DID_BAD_TARGET << 16;
				__aac_io_done(scsicmd);
				return 0;
			}
		} else {  /* check for physical non-dasd devices */
			if(dev->nondasd_support == 1){
				return aac_send_srb_fib(scsicmd);
			} else {
				scsicmd->result = DID_NO_CONNECT << 16;
				__aac_io_done(scsicmd);
				return 0;
			}
		}
	}
	/*
	 * else Command for the controller itself
	 */
	else if ((scsicmd->cmnd[0] != INQUIRY) &&	/* only INQUIRY & TUR cmnd supported for controller */
		(scsicmd->cmnd[0] != TEST_UNIT_READY)) 
	{
		dprintk((KERN_WARNING "Only INQUIRY & TUR command supported for controller, rcvd = 0x%x.\n", scsicmd->cmnd[0]));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
			    ILLEGAL_REQUEST,
			    SENCODE_INVALID_COMMAND,
			    ASENCODE_INVALID_COMMAND, 0, 0, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
		__aac_io_done(scsicmd);
		return 0;
	}


	/* Handle commands here that don't really require going out to the adapter */
	switch (scsicmd->cmnd[0]) {
	case INQUIRY:
	{
		struct inquiry_data *inq_data_ptr;

		dprintk((KERN_DEBUG "INQUIRY command, ID: %d.\n", scsicmd->device->id));
		inq_data_ptr = (struct inquiry_data *)scsicmd->request_buffer;
		memset(inq_data_ptr, 0, sizeof (struct inquiry_data));

		inq_data_ptr->inqd_ver = 2;	/* claim compliance to SCSI-2 */
		inq_data_ptr->inqd_rdf = 2;	/* A response data format value of two indicates that the data shall be in the format specified in SCSI-2 */
		inq_data_ptr->inqd_len = 31;
		/*Format for "pad2" is  RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
		inq_data_ptr->inqd_pad2= 0x32 ;	 /*WBus16|Sync|CmdQue */
		/*
		 *	Set the Vendor, Product, and Revision Level
		 *	see: <vendor>.c i.e. aac.c
		 */
		if (scsicmd->device->id == host->this_id) {
			setinqstr(cardtype, (void *) (inq_data_ptr->inqd_vid), (sizeof(container_types)/sizeof(char *)));
			inq_data_ptr->inqd_pdt = INQD_PDT_PROC;	/* Processor device */
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
			__aac_io_done(scsicmd);
			return 0;
		}
		setinqstr(cardtype, (void *) (inq_data_ptr->inqd_vid), fsa_dev_ptr[cid].type);
		inq_data_ptr->inqd_pdt = INQD_PDT_DA;	/* Direct/random access device */
		return aac_get_container_name(scsicmd, cid);
	}
#ifdef SERVICE_ACTION_IN
	case SERVICE_ACTION_IN:
		if ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16)
			break;
	{
		u64 capacity;
		char *cp;

		dprintk((KERN_DEBUG "READ CAPACITY_16 command.\n"));
		capacity = fsa_dev_ptr[cid].size - 1;
		cp = scsicmd->request_buffer;
		cp[0] = 0;
		cp[1] = 0;
		cp[2] = (capacity >> 56) & 0xff;
		cp[3] = (capacity >> 48) & 0xff;
		cp[4] = (capacity >> 40) & 0xff;
		cp[5] = (capacity >> 32) & 0xff;
		cp[6] = (capacity >> 24) & 0xff;
		cp[7] = (capacity >> 16) & 0xff;
		cp[8] = (capacity >> 8) & 0xff;
		cp[9] = (capacity >> 0) & 0xff;
		cp[10] = 0;
		cp[11] = 0;
		cp[12] = 2;
		cp[13] = 0;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);

		return 0;
	}
#endif
	case READ_CAPACITY:
	{
		u32 capacity;
		char *cp;

		dprintk((KERN_DEBUG "READ CAPACITY command.\n"));
		if (fsa_dev_ptr[cid].size <= 0x100000000)
			capacity = fsa_dev_ptr[cid].size - 1;
		else
			capacity = (u32)-1;
		cp = scsicmd->request_buffer;
		cp[0] = (capacity >> 24) & 0xff;
		cp[1] = (capacity >> 16) & 0xff;
		cp[2] = (capacity >> 8) & 0xff;
		cp[3] = (capacity >> 0) & 0xff;
		cp[4] = 0;
		cp[5] = 0;
		cp[6] = 2;
		cp[7] = 0;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);

		return 0;
	}

	case MODE_SENSE:
	{
		char *mode_buf;

		dprintk((KERN_DEBUG "MODE SENSE command.\n"));
		mode_buf = scsicmd->request_buffer;
		mode_buf[0] = 3;	/* Mode data length */
		mode_buf[1] = 0;	/* Medium type - default */
		mode_buf[2] = 0;	/* Device-specific param, bit 8: 0/1 = write enabled/protected */
		mode_buf[3] = 0;	/* Block descriptor length */

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);

		return 0;
	}
	case MODE_SENSE_10:
	{
		char *mode_buf;

		dprintk((KERN_DEBUG "MODE SENSE 10 byte command.\n"));
		mode_buf = scsicmd->request_buffer;
		mode_buf[0] = 0;	/* Mode data length (MSB) */
		mode_buf[1] = 6;	/* Mode data length (LSB) */
		mode_buf[2] = 0;	/* Medium type - default */
		mode_buf[3] = 0;	/* Device-specific param, bit 8: 0/1 = write enabled/protected */
		mode_buf[4] = 0;	/* reserved */
		mode_buf[5] = 0;	/* reserved */
		mode_buf[6] = 0;	/* Block descriptor length (MSB) */
		mode_buf[7] = 0;	/* Block descriptor length (LSB) */

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);

		return 0;
	}
	case REQUEST_SENSE:
		dprintk((KERN_DEBUG "REQUEST SENSE command.\n"));
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data, sizeof (struct sense_data));
		memset(&dev->fsa_dev[cid].sense_data, 0, sizeof (struct sense_data));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);
		return 0;

	case ALLOW_MEDIUM_REMOVAL:
		dprintk((KERN_DEBUG "LOCK command.\n"));
		if (scsicmd->cmnd[4])
			fsa_dev_ptr[cid].locked = 1;
		else
			fsa_dev_ptr[cid].locked = 0;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);
		return 0;
	/*
	 *	These commands are all No-Ops
	 */
	case TEST_UNIT_READY:
	case RESERVE:
	case RELEASE:
	case REZERO_UNIT:
	case REASSIGN_BLOCKS:
	case SEEK_10:
	case START_STOP:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		__aac_io_done(scsicmd);
		return 0;
	}

	switch (scsicmd->cmnd[0]) 
	{
		case READ_6:
		case READ_10:
		case READ_12:
#ifdef READ_16
		case READ_16:
#endif
			/*
			 *	Hack to keep track of ordinal number of the device that
			 *	corresponds to a container. Needed to convert
			 *	containers to /dev/sd device names
			 */
			if ((scsicmd->eh_state != SCSI_STATE_QUEUED)
			 && (overridetimeout > 0)) {
				mod_timer(&scsicmd->eh_timeout, jiffies + (overridetimeout * HZ));
			}
			if (scsicmd->request->rq_disk)
				strlcpy(fsa_dev_ptr[cid].devname,
				  scsicmd->request->rq_disk->disk_name,
				  min(
				    sizeof(fsa_dev_ptr[cid].devname),
				    sizeof(scsicmd->request->rq_disk->disk_name) + 1));

			return aac_read(scsicmd, cid);

		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
#ifdef WRITE_16
		case WRITE_16:
#endif
			if ((scsicmd->eh_state != SCSI_STATE_QUEUED)
			 && (overridetimeout > 0)) {
				mod_timer(&scsicmd->eh_timeout, jiffies + (overridetimeout * HZ));
			}
			return aac_write(scsicmd, cid);

		case SYNCHRONIZE_CACHE:
			/* Issue FIB to tell Firmware to flush it's cache */
			return aac_synchronize(scsicmd, cid);
			
		default:
			/*
			 *	Unhandled commands
			 */
			dprintk((KERN_WARNING "Unhandled SCSI Command: 0x%x.\n", scsicmd->cmnd[0]));
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
			set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				ILLEGAL_REQUEST, SENCODE_INVALID_COMMAND,
				ASENCODE_INVALID_COMMAND, 0, 0, 0, 0);
			memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
			  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
			    ? sizeof(scsicmd->sense_buffer)
			    : sizeof(dev->fsa_dev[cid].sense_data));
			__aac_io_done(scsicmd);
			return 0;
	}
}

static int busy_disk(struct aac_dev * dev, int cid)
{
	if ((dev != (struct aac_dev *)NULL)
	 && (dev->scsi_host_ptr != (struct Scsi_Host *)NULL)) {
		struct scsi_device *device;
		shost_for_each_device(device, dev->scsi_host_ptr)
		{
			if ((device->channel == CONTAINER_TO_CHANNEL(cid))
			 && (device->id == CONTAINER_TO_ID(cid))
			 && (device->lun == CONTAINER_TO_LUN(cid))
			&& (device->device_busy
			  || test_bit(SHOST_RECOVERY, &dev->scsi_host_ptr->shost_state))) {
				scsi_device_put(device);
				return 1;
			}
		}
	}
	return 0;
}

static int query_disk(struct aac_dev *dev, void *arg)
{
	struct aac_query_disk qd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;
	if (copy_from_user(&qd, arg, sizeof (struct aac_query_disk)))
		return -EFAULT;
	if (qd.cnum == -1)
		qd.cnum = ID_LUN_TO_CONTAINER(qd.id, qd.lun);
	else if ((qd.bus == -1) && (qd.id == -1) && (qd.lun == -1)) 
	{
		if (qd.cnum < 0 || qd.cnum >= dev->maximum_num_containers)
			return -EINVAL;
		qd.instance = dev->scsi_host_ptr->host_no;
		qd.bus = 0;
		qd.id = CONTAINER_TO_ID(qd.cnum);
		qd.lun = CONTAINER_TO_LUN(qd.cnum);
	}
	else return -EINVAL;

	qd.valid = fsa_dev_ptr[qd.cnum].valid;
	qd.locked = fsa_dev_ptr[qd.cnum].locked || busy_disk(dev, qd.cnum);
	qd.deleted = fsa_dev_ptr[qd.cnum].deleted;

	if (fsa_dev_ptr[qd.cnum].devname[0] == '\0')
		qd.unmapped = 1;
	else
		qd.unmapped = 0;

	strlcpy(qd.name, fsa_dev_ptr[qd.cnum].devname,
	  min(sizeof(qd.name), sizeof(fsa_dev_ptr[qd.cnum].devname) + 1));

	if (copy_to_user(arg, &qd, sizeof (struct aac_query_disk)))
		return -EFAULT;
	return 0;
}

static int force_delete_disk(struct aac_dev *dev, void *arg)
{
	struct aac_delete_disk dd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;

	if (copy_from_user(&dd, arg, sizeof (struct aac_delete_disk)))
		return -EFAULT;

	if (dd.cnum >= dev->maximum_num_containers)
		return -EINVAL;
	/*
	 *	Mark this container as being deleted.
	 */
	fsa_dev_ptr[dd.cnum].deleted = 1;
	/*
	 *	Mark the container as no longer valid
	 */
	fsa_dev_ptr[dd.cnum].valid = 0;
	return 0;
}

static int delete_disk(struct aac_dev *dev, void *arg)
{
	struct aac_delete_disk dd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;

	if (copy_from_user(&dd, arg, sizeof (struct aac_delete_disk)))
		return -EFAULT;

	if (dd.cnum >= dev->maximum_num_containers)
		return -EINVAL;
	/*
	 *	If the container is locked, it can not be deleted by the API.
	 */
	if (fsa_dev_ptr[dd.cnum].locked || busy_disk(dev, dd.cnum))
		return -EBUSY;
	else {
		/*
		 *	Mark the container as no longer being valid.
		 */
		fsa_dev_ptr[dd.cnum].valid = 0;
		fsa_dev_ptr[dd.cnum].devname[0] = '\0';
		return 0;
	}
}

int aac_dev_ioctl(struct aac_dev *dev, int cmd, void *arg)
{
	switch (cmd) {
	case FSACTL_QUERY_DISK:
		return query_disk(dev, arg);
	case FSACTL_DELETE_DISK:
		return delete_disk(dev, arg);
	case FSACTL_FORCE_DELETE_DISK:
		return force_delete_disk(dev, arg);
	case FSACTL_GET_CONTAINERS:
		return aac_get_containers(dev);
	default:
		return -ENOTTY;
	}
}

/**
 *
 * aac_srb_callback
 * @context: the context set in the fib - here it is scsi cmd
 * @fibptr: pointer to the fib
 *
 * Handles the completion of a scsi command to a non dasd device
 *
 */

static void aac_srb_callback(void *context, struct fib * fibptr)
{
	struct aac_dev *dev;
	struct aac_srb_reply *srbreply;
	struct scsi_cmnd *scsicmd;

	scsicmd = (struct scsi_cmnd *) context;
	dev = (struct aac_dev *)scsicmd->device->host->hostdata;

	if (fibptr == NULL)
		BUG();

	srbreply = (struct aac_srb_reply *) fib_data(fibptr);

	scsicmd->sense_buffer[0] = '\0';  // initialize sense valid flag to false
	// calculate resid for sg 
	scsicmd->resid = scsicmd->request_bufflen - srbreply->data_xfer_length;

	if(scsicmd->use_sg)
		pci_unmap_sg(dev->pdev, 
			(struct scatterlist *)scsicmd->buffer,
			scsicmd->use_sg,
			scsicmd->sc_data_direction);
	else if(scsicmd->request_bufflen)
		pci_unmap_single(dev->pdev, scsicmd->SCp.dma_handle,
			scsicmd->request_bufflen,
			scsicmd->sc_data_direction);

	/*
	 * First check the fib status
	 */

	if (le32_to_cpu(srbreply->status) != ST_OK){
		int len;
		printk(KERN_WARNING "aac_srb_callback: srb failed, status = %d\n", le32_to_cpu(srbreply->status));
		len = (srbreply->sense_data_size > sizeof(scsicmd->sense_buffer))?
				sizeof(scsicmd->sense_buffer):srbreply->sense_data_size;
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		memcpy(scsicmd->sense_buffer, srbreply->sense_data, len);
	}

	/*
	 * Next check the srb status
	 */
	switch( (le32_to_cpu(srbreply->srb_status))&0x3f){
	case SRB_STATUS_ERROR_RECOVERY:
	case SRB_STATUS_PENDING:
	case SRB_STATUS_SUCCESS:
		if(scsicmd->cmnd[0] == INQUIRY ){
			u8 b;
			u8 b1;
			/* We can't expose disk devices because we can't tell whether they
			 * are the raw container drives or stand alone drives.  If they have
			 * the removable bit set then we should expose them though.
			 */
			b = (*(u8*)scsicmd->buffer)&0x1f;
			b1 = ((u8*)scsicmd->buffer)[1];
			if( b==TYPE_TAPE || b==TYPE_WORM || b==TYPE_ROM || b==TYPE_MOD|| b==TYPE_MEDIUM_CHANGER 
					|| (b==TYPE_DISK && (b1&0x80)) ){
				scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			/*
			 * We will allow disk devices if in RAID/SCSI mode and
			 * the channel is 2
			 */
			} else if((dev->raid_scsi_mode)&&(scsicmd->device->channel == 2)){
				scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			} else {
				scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
			}
		} else {
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		}
		break;
	case SRB_STATUS_DATA_OVERRUN:
		switch(scsicmd->cmnd[0]){
		case  READ_6:
		case  WRITE_6:
		case  READ_10:
		case  WRITE_10:
		case  READ_12:
		case  WRITE_12:
#ifdef READ_16
		case  READ_16:
#endif
#ifdef WRITE_16
		case  WRITE_16:
#endif
			if(le32_to_cpu(srbreply->data_xfer_length) < scsicmd->underflow ) {
				printk(KERN_WARNING"aacraid: SCSI CMD underflow\n");
			} else {
				printk(KERN_WARNING"aacraid: SCSI CMD Data Overrun\n");
			}
			scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
			break;
		case INQUIRY: {
			u8 b;
			u8 b1;
			/* We can't expose disk devices because we can't tell whether they
			* are the raw container drives or stand alone drives
			*/
			b = (*(u8*)scsicmd->buffer)&0x0f;
			b1 = ((u8*)scsicmd->buffer)[1];
			if( b==TYPE_TAPE || b==TYPE_WORM || b==TYPE_ROM || b==TYPE_MOD|| b==TYPE_MEDIUM_CHANGER
					|| (b==TYPE_DISK && (b1&0x80)) ){
				scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			/*
			 * We will allow disk devices if in RAID/SCSI mode and
			 * the channel is 2
			 */
			} else if((dev->raid_scsi_mode)&&(scsicmd->device->channel == 2)){
				scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			} else {
				scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
			}
			break;
		}
		default:
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			break;
		}
		break;
	case SRB_STATUS_ABORTED:
		scsicmd->result = DID_ABORT << 16 | ABORT << 8;
		break;
	case SRB_STATUS_ABORT_FAILED:
		// Not sure about this one - but assuming the hba was trying to abort for some reason
		scsicmd->result = DID_ERROR << 16 | ABORT << 8;
		break;
	case SRB_STATUS_PARITY_ERROR:
		scsicmd->result = DID_PARITY << 16 | MSG_PARITY_ERROR << 8;
		break;
	case SRB_STATUS_NO_DEVICE:
	case SRB_STATUS_INVALID_PATH_ID:
	case SRB_STATUS_INVALID_TARGET_ID:
	case SRB_STATUS_INVALID_LUN:
	case SRB_STATUS_SELECTION_TIMEOUT:
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_COMMAND_TIMEOUT:
	case SRB_STATUS_TIMEOUT:
		scsicmd->result = DID_TIME_OUT << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUSY:
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUS_RESET:
		scsicmd->result = DID_RESET << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_MESSAGE_REJECTED:
		scsicmd->result = DID_ERROR << 16 | MESSAGE_REJECT << 8;
		break;
	case SRB_STATUS_REQUEST_FLUSHED:
	case SRB_STATUS_ERROR:
	case SRB_STATUS_INVALID_REQUEST:
	case SRB_STATUS_REQUEST_SENSE_FAILED:
	case SRB_STATUS_NO_HBA:
	case SRB_STATUS_UNEXPECTED_BUS_FREE:
	case SRB_STATUS_PHASE_SEQUENCE_FAILURE:
	case SRB_STATUS_BAD_SRB_BLOCK_LENGTH:
	case SRB_STATUS_DELAYED_RETRY:
	case SRB_STATUS_BAD_FUNCTION:
	case SRB_STATUS_NOT_STARTED:
	case SRB_STATUS_NOT_IN_USE:
	case SRB_STATUS_FORCE_ABORT:
	case SRB_STATUS_DOMAIN_VALIDATION_FAIL:
	default:
#ifdef AAC_DETAILED_STATUS_INFO
		printk("aacraid: SRB ERROR(%u) %s scsi cmd 0x%x - scsi status 0x%x\n",le32_to_cpu(srbreply->srb_status)&0x3f,aac_get_status_string(le32_to_cpu(srbreply->srb_status)&0x3F), scsicmd->cmnd[0], le32_to_cpu(srbreply->scsi_status) );
#endif
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
		break;
	}
	if (le32_to_cpu(srbreply->scsi_status) == 0x02 ){  // Check Condition
		int len;
		scsicmd->result |= SAM_STAT_CHECK_CONDITION;
		len = (srbreply->sense_data_size > sizeof(scsicmd->sense_buffer))?
				sizeof(scsicmd->sense_buffer):srbreply->sense_data_size;
#ifdef AAC_DETAILED_STATUS_INFO
		printk(KERN_WARNING "aac_srb_callback: check condition, status = %d len=%d\n", le32_to_cpu(srbreply->status), len);
#endif
		memcpy(scsicmd->sense_buffer, srbreply->sense_data, len);
	}
	/*
	 * OR in the scsi status (already shifted up a bit)
	 */
	scsicmd->result |= le32_to_cpu(srbreply->scsi_status);

	fib_complete(fibptr);
	fib_free(fibptr);
	aac_io_done(scsicmd);
}

/**
 *
 * aac_send_scb_fib
 * @scsicmd: the scsi command block
 *
 * This routine will form a FIB and fill in the aac_srb from the 
 * scsicmd passed in.
 */

static int aac_send_srb_fib(struct scsi_cmnd* scsicmd)
{
	struct fib* cmd_fibcontext;
	struct aac_dev* dev;
	int status;
	struct aac_srb *srbcmd;
	u16 fibsize;
	u32 flag;
	u32 timeout;

	if( scsicmd->device->id > 15 || scsicmd->device->lun > 7) {
		scsicmd->result = DID_NO_CONNECT << 16;
		__aac_io_done(scsicmd);
		return 0;
	}

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	switch(scsicmd->sc_data_direction){
	case DMA_TO_DEVICE:
		flag = SRB_DataOut;
		break;
	case DMA_BIDIRECTIONAL:
		flag = SRB_DataIn | SRB_DataOut;
		break;
	case DMA_FROM_DEVICE:
		flag = SRB_DataIn;
		break;
	case DMA_NONE:
	default:	/* shuts up some versions of gcc */
		flag = SRB_NoDataXfer;
		break;
	}


	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	if (!(cmd_fibcontext = fib_alloc(dev))) {
		return -1;
	}
	fib_init(cmd_fibcontext);

	srbcmd = (struct aac_srb*) fib_data(cmd_fibcontext);
	srbcmd->function = cpu_to_le32(SRBF_ExecuteScsi);
	srbcmd->channel  = cpu_to_le32(aac_logical_to_phys(scsicmd->device->channel));
	srbcmd->id       = cpu_to_le32(scsicmd->device->id);
	srbcmd->lun      = cpu_to_le32(scsicmd->device->lun);
	srbcmd->flags    = cpu_to_le32(flag);
	timeout = (scsicmd->timeout-jiffies)/HZ;
	if(timeout == 0){
		timeout = 1;
	}
	srbcmd->timeout  = cpu_to_le32(timeout);  // timeout in seconds
	srbcmd->retry_limit = 0; // Obsolete parameter
	srbcmd->cdb_size = cpu_to_le32(scsicmd->cmd_len);
	
	if (dev->dac_support == 1) {
		aac_build_sg64(scsicmd, (struct sgmap64*) &srbcmd->sg);
		srbcmd->count = cpu_to_le32(scsicmd->request_bufflen);

		memset(srbcmd->cdb, 0, sizeof(srbcmd->cdb));
		memcpy(srbcmd->cdb, scsicmd->cmnd, scsicmd->cmd_len);
		/*
		 *	Build Scatter/Gather list
		 */
		fibsize = sizeof (struct aac_srb) - sizeof (struct sgentry) +
			((srbcmd->sg.count & 0xff) * sizeof (struct sgentry64));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();

		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ScsiPortCommand64, cmd_fibcontext, fibsize, FsaNormal, 0, 1,
				  (fib_callback) aac_srb_callback, (void *) scsicmd);
	} else {
		aac_build_sg(scsicmd, (struct sgmap*)&srbcmd->sg);
		srbcmd->count = cpu_to_le32(scsicmd->request_bufflen);

		memset(srbcmd->cdb, 0, sizeof(srbcmd->cdb));
		memcpy(srbcmd->cdb, scsicmd->cmnd, scsicmd->cmd_len);
		/*
		 *	Build Scatter/Gather list
		 */
		fibsize = sizeof (struct aac_srb) + (((srbcmd->sg.count & 0xff) - 1) * sizeof (struct sgentry));
		if (fibsize > (dev->max_fib_size - sizeof(struct aac_fibhdr)))
			BUG();

		/*
		 *	Now send the Fib to the adapter
		 */
		status = fib_send(ScsiPortCommand, cmd_fibcontext, fibsize, FsaNormal, 0, 1,
				  (fib_callback) aac_srb_callback, (void *) scsicmd);
	}
	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS){
		return 0;
	}

	printk(KERN_WARNING "aac_srb: fib_send failed with status: %d\n", status);

	fib_complete(cmd_fibcontext);
	fib_free(cmd_fibcontext);

	return -1;
}

static unsigned long aac_build_sg(struct scsi_cmnd* scsicmd, struct sgmap* psg)
{
	struct Scsi_Host *host = scsicmd->device->host;
	struct aac_dev *dev = (struct aac_dev *)host->hostdata;
	unsigned long byte_count = 0;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr = 0;
	psg->sg[0].count = 0;  
	if (scsicmd->use_sg) {
		struct scatterlist *sg;
		int i;
		int sg_count;
		sg = (struct scatterlist *) scsicmd->request_buffer;

		sg_count = pci_map_sg(dev->pdev, sg, scsicmd->use_sg,
			scsicmd->sc_data_direction);

		for (i = 0; i < sg_count; i++) {
			int count = sg_dma_len(sg);
			u32 addr = sg_dma_address(sg);
			if (host->max_sectors < AAC_MAX_32BIT_SGBCOUNT)
			while (count > 65536) {
				psg->sg[i].addr = cpu_to_le32(addr);
				psg->sg[i].count = cpu_to_le32(65536);
				++i;
				if (++sg_count > host->sg_tablesize)
					BUG();
				byte_count += 65536;
				addr += 65536;
				count -= 65536;
			}

			psg->sg[i].addr = cpu_to_le32(addr);
			psg->sg[i].count = cpu_to_le32(count);
			byte_count += count;
			sg++;
		}
		psg->count = cpu_to_le32(sg_count);
		/* hba wants the size to be exact */
		if(byte_count > scsicmd->request_bufflen){
			psg->sg[i-1].count -= (byte_count - scsicmd->request_bufflen);
			byte_count = scsicmd->request_bufflen;
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	else if(scsicmd->request_bufflen) {
		int i, count;
		u32 addr;
		scsicmd->SCp.dma_handle = pci_map_single(dev->pdev,
				scsicmd->request_buffer,
				scsicmd->request_bufflen,
				scsicmd->sc_data_direction);
		addr = scsicmd->SCp.dma_handle;
		count = scsicmd->request_bufflen;  
		i = 0;
		if (host->max_sectors < AAC_MAX_32BIT_SGBCOUNT)
		while (count > 65536) {
			psg->sg[i].addr = cpu_to_le32(addr);
			psg->sg[i].count = cpu_to_le32(65536);
			if (++i >= host->sg_tablesize)
				BUG();
			addr += 65536;
			count -= 65536;
		}
		psg->count = cpu_to_le32(1+i);
		psg->sg[i].addr = cpu_to_le32(addr);
		psg->sg[i].count = cpu_to_le32(count);
		byte_count = scsicmd->request_bufflen;
	}
	return byte_count;
}


static unsigned long aac_build_sg64(struct scsi_cmnd* scsicmd, struct sgmap64* psg)
{
	struct Scsi_Host *host = scsicmd->device->host;
	struct aac_dev *dev = (struct aac_dev *)host->hostdata;
	unsigned long byte_count = 0;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;  
	if (scsicmd->use_sg) {
		struct scatterlist *sg;
		int i;
		int sg_count;
		sg = (struct scatterlist *) scsicmd->request_buffer;

		sg_count = pci_map_sg(dev->pdev, sg, scsicmd->use_sg,
			scsicmd->sc_data_direction);

		for (i = 0; i < sg_count; i++) {
			int count = sg_dma_len(sg);
			u64 addr = sg_dma_address(sg);
			if (host->max_sectors < AAC_MAX_32BIT_SGBCOUNT)
			while (count > 65536) {
				psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
				psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
				psg->sg[i].count = cpu_to_le32(65536);
				++i;
				if (++sg_count > host->sg_tablesize)
					BUG();
				byte_count += 65536;
				addr += 65536;
				count -= 65536;
			}
			psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
			psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
			psg->sg[i].count = cpu_to_le32(count);
			byte_count += count;
			sg++;
		}
		psg->count = cpu_to_le32(sg_count);
		/* hba wants the size to be exact */
		if(byte_count > scsicmd->request_bufflen){
			psg->sg[i-1].count -= (byte_count - scsicmd->request_bufflen);
			byte_count = scsicmd->request_bufflen;
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	else if(scsicmd->request_bufflen) {
		int i, count;
		u64 addr;
		scsicmd->SCp.dma_handle = pci_map_single(dev->pdev,
				scsicmd->request_buffer,
				scsicmd->request_bufflen,
				scsicmd->sc_data_direction);
		addr = scsicmd->SCp.dma_handle;
		count = scsicmd->request_bufflen;  
		i = 0;
		if (host->max_sectors < AAC_MAX_32BIT_SGBCOUNT)
		while (count > 65536) {
			psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
			psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
			psg->sg[i].count = cpu_to_le32(65536);
			if (++i >= host->sg_tablesize)
				BUG();
			addr += 65536;
			count -= 65536;
		}
		psg->count = cpu_to_le32(1+i);
		psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
		psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
		psg->sg[i].count = cpu_to_le32(count);
		byte_count = scsicmd->request_bufflen;
	}
	return byte_count;
}

static unsigned long aac_build_sgraw(struct scsi_cmnd* scsicmd, struct sgmapraw* psg)
{
	struct Scsi_Host *host = scsicmd->device->host;
	struct aac_dev *dev = (struct aac_dev *)host->hostdata;
	unsigned long byte_count = 0;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].next = 0;
	psg->sg[0].prev = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;
	psg->sg[0].flags = 0;
	if (scsicmd->use_sg) {
		struct scatterlist *sg;
		int i;
		int sg_count;
		sg = (struct scatterlist *) scsicmd->request_buffer;

		sg_count = pci_map_sg(dev->pdev, sg, scsicmd->use_sg,
			scsicmd->sc_data_direction);

		for (i = 0; i < sg_count; i++) {
			int count = sg_dma_len(sg);
			u64 addr = sg_dma_address(sg);
			psg->sg[i].next = 0;
			psg->sg[i].prev = 0;
			psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
			psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
			psg->sg[i].count = cpu_to_le32(count);
			psg->sg[i].flags = 0;
			byte_count += count;
			sg++;
		}
		psg->count = cpu_to_le32(sg_count);
		/* hba wants the size to be exact */
		if(byte_count > scsicmd->request_bufflen){
			psg->sg[i-1].count -= (byte_count - scsicmd->request_bufflen);
			byte_count = scsicmd->request_bufflen;
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	else if(scsicmd->request_bufflen) {
		int count;
		u64 addr;
		scsicmd->SCp.dma_handle = pci_map_single(dev->pdev,
				scsicmd->request_buffer,
				scsicmd->request_bufflen,
				scsicmd->sc_data_direction);
		addr = scsicmd->SCp.dma_handle;
		count = scsicmd->request_bufflen;
		psg->count = cpu_to_le32(1);
		psg->sg[0].next = 0;
		psg->sg[0].prev = 0;
		psg->sg[0].addr[1] = cpu_to_le32((u32)(addr>>32));
		psg->sg[0].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
		psg->sg[0].count = cpu_to_le32(count);
		psg->sg[0].flags = 0;
		byte_count = scsicmd->request_bufflen;
	}
	return byte_count;
}

#ifdef AAC_DETAILED_STATUS_INFO

struct aac_srb_status_info {
	u32	status;
	char	*str;
};


static struct aac_srb_status_info srb_status_info[] = {
	{ SRB_STATUS_PENDING,		"Pending Status"},
	{ SRB_STATUS_SUCCESS,		"Success"},
	{ SRB_STATUS_ABORTED,		"Aborted Command"},
	{ SRB_STATUS_ABORT_FAILED,	"Abort Failed"},
	{ SRB_STATUS_ERROR,		"Error Event"}, 
	{ SRB_STATUS_BUSY,		"Device Busy"},
	{ SRB_STATUS_INVALID_REQUEST,	"Invalid Request"},
	{ SRB_STATUS_INVALID_PATH_ID,	"Invalid Path ID"},
	{ SRB_STATUS_NO_DEVICE,		"No Device"},
	{ SRB_STATUS_TIMEOUT,		"Timeout"},
	{ SRB_STATUS_SELECTION_TIMEOUT,	"Selection Timeout"},
	{ SRB_STATUS_COMMAND_TIMEOUT,	"Command Timeout"},
	{ SRB_STATUS_MESSAGE_REJECTED,	"Message Rejected"},
	{ SRB_STATUS_BUS_RESET,		"Bus Reset"},
	{ SRB_STATUS_PARITY_ERROR,	"Parity Error"},
	{ SRB_STATUS_REQUEST_SENSE_FAILED,"Request Sense Failed"},
	{ SRB_STATUS_NO_HBA,		"No HBA"},
	{ SRB_STATUS_DATA_OVERRUN,	"Data Overrun/Data Underrun"},
	{ SRB_STATUS_UNEXPECTED_BUS_FREE,"Unexpected Bus Free"},
	{ SRB_STATUS_PHASE_SEQUENCE_FAILURE,"Phase Error"},
	{ SRB_STATUS_BAD_SRB_BLOCK_LENGTH,"Bad Srb Block Length"},
	{ SRB_STATUS_REQUEST_FLUSHED,	"Request Flushed"},
	{ SRB_STATUS_DELAYED_RETRY,	"Delayed Retry"},
	{ SRB_STATUS_INVALID_LUN,	"Invalid LUN"}, 
	{ SRB_STATUS_INVALID_TARGET_ID,	"Invalid TARGET ID"},
	{ SRB_STATUS_BAD_FUNCTION,	"Bad Function"},
	{ SRB_STATUS_ERROR_RECOVERY,	"Error Recovery"},
	{ SRB_STATUS_NOT_STARTED,	"Not Started"},
	{ SRB_STATUS_NOT_IN_USE,	"Not In Use"},
    	{ SRB_STATUS_FORCE_ABORT,	"Force Abort"},
	{ SRB_STATUS_DOMAIN_VALIDATION_FAIL,"Domain Validation Failure"},
	{ 0xff,				"Unknown Error"}
};

char *aac_get_status_string(u32 status)
{
	int i;

	for(i=0; i < (sizeof(srb_status_info)/sizeof(struct aac_srb_status_info)); i++ ){
		if(srb_status_info[i].status == status){
			return srb_status_info[i].str;
		}
	}

	return "Bad Status Code";
}

#endif
