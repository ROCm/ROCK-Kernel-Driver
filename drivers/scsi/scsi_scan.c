/*
 * scsi_scan.c
 *
 * Copyright (C) 2000 Eric Youngdale,
 * Copyright (C) 2002 Patrick Mansfield
 *
 * The general scanning/probing algorithm is as follows, exceptions are
 * made to it depending on device specific flags, compilation options, and
 * global variable (boot or module load time) settings.
 *
 * A specific LUN is scanned via an INQUIRY command; if the LUN has a
 * device attached, a Scsi_Device is allocated and setup for it.
 *
 * For every id of every channel on the given host:
 *
 * 	Scan LUN 0; if the target responds to LUN 0 (even if there is no
 * 	device or storage attached to LUN 0):
 *
 * 		If LUN 0 has a device attached, allocate and setup a
 * 		Scsi_Device for it.
 *
 * 		If target is SCSI-3 or up, issue a REPORT LUN, and scan
 * 		all of the LUNs returned by the REPORT LUN; else,
 * 		sequentially scan LUNs up until some maximum is reached,
 * 		or a LUN is seen that cannot have a device attached to it.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/blk.h>

#include "scsi.h"
#include "hosts.h"

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

/*
 * Flags for SCSI devices that need special treatment
 */
#define BLIST_NOLUN     	0x001	/* Only scan LUN 0 */
#define BLIST_FORCELUN  	0x002	/* Known to have LUNs, force scanning */
#define BLIST_BORKEN    	0x004	/* Flag for broken handshaking */
#define BLIST_KEY       	0x008	/* unlock by special command */
#define BLIST_SINGLELUN 	0x010	/* Do not use LUNs in parallel */
#define BLIST_NOTQ		0x020	/* Buggy Tagged Command Queuing */
#define BLIST_SPARSELUN 	0x040	/* Non consecutive LUN numbering */
#define BLIST_MAX5LUN		0x080	/* Avoid LUNS >= 5 */
#define BLIST_ISROM     	0x100	/* Treat as (removable) CD-ROM */
#define BLIST_LARGELUN		0x200	/* LUNs past 7 on a SCSI-2 device */
#define BLIST_INQUIRY_36	0x400	/* override additional length field */
#define BLIST_INQUIRY_58	0x800	/* ... for broken inquiry responses */

struct dev_info {
	const char *vendor;
	const char *model;
	const char *revision;	/* revision known to be bad, unused */
	unsigned flags;
};

/*
 * device_list: devices that require settings that differ from the
 * default, includes black-listed (broken) devices.
 */
static struct dev_info device_list[] = {
	/*
	 * The following devices are known not to tolerate a lun != 0 scan
	 * for one reason or another. Some will respond to all luns,
	 * others will lock up.
	 */
	{"Aashima", "IMAGERY 2400SP", "1.03", BLIST_NOLUN},	/* locks up */
	{"CHINON", "CD-ROM CDS-431", "H42", BLIST_NOLUN},	/* locks up */
	{"CHINON", "CD-ROM CDS-535", "Q14", BLIST_NOLUN},	/* locks up */
	{"DENON", "DRD-25X", "V", BLIST_NOLUN},			/* locks up */
	{"HITACHI", "DK312C", "CM81", BLIST_NOLUN},	/* responds to all lun */
	{"HITACHI", "DK314C", "CR21", BLIST_NOLUN},	/* responds to all lun */
	{"IMS", "CDD521/10", "2.06", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-3280", "PR02", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-4380S", "B3C", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "MXT-1240S", "I1.2", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-4170S", "B5A", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-8760S", "B7B", BLIST_NOLUN},	/* locks up */
	{"MEDIAVIS", "RENO CD-ROMX2A", "2.03", BLIST_NOLUN},	/* responds to all lun */
	{"NEC", "CD-ROM DRIVE:841", "1.0", BLIST_NOLUN},/* locks up */
	{"PHILIPS", "PCA80SC", "V4-2", BLIST_NOLUN},	/* responds to all lun */
	{"RODIME", "RO3000S", "2.33", BLIST_NOLUN},	/* locks up */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * aha152x controller, which causes SCSI code to reset bus.
	 */
	{"SANYO", "CRD-250S", "1.20", BLIST_NOLUN},
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * aha152x controller, which causes SCSI code to reset bus.
	 */
	{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},
	{"SEAGATE", "ST296", "921", BLIST_NOLUN},	/* responds to all lun */
	{"SEAGATE", "ST1581", "6538", BLIST_NOLUN},	/* responds to all lun */
	{"SONY", "CD-ROM CDU-541", "4.3d", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-55S", "1.0i", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-561", "1.7x", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-8012", NULL, BLIST_NOLUN},
	{"TANDBERG", "TDC 3600", "U07", BLIST_NOLUN},	/* locks up */
	{"TEAC", "CD-R55S", "1.0H", BLIST_NOLUN},	/* locks up */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * seagate controller, which causes SCSI code to reset bus.
	 */
	{"TEAC", "CD-ROM", "1.06", BLIST_NOLUN},
	{"TEAC", "MT-2ST/45S2-27", "RV M", BLIST_NOLUN},	/* responds to all lun */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * seagate controller, which causes SCSI code to reset bus.
	 */
	{"TEXEL", "CD-ROM", "1.06", BLIST_NOLUN},
	{"QUANTUM", "LPS525S", "3110", BLIST_NOLUN},	/* locks up */
	{"QUANTUM", "PD1225S", "3110", BLIST_NOLUN},	/* locks up */
	{"QUANTUM", "FIREBALL ST4.3S", "0F0C", BLIST_NOLUN},	/* locks up */
	{"MEDIAVIS", "CDR-H93MV", "1.31", BLIST_NOLUN},	/* locks up */
	{"SANKYO", "CP525", "6.64", BLIST_NOLUN},	/* causes failed REQ SENSE, extra reset */
	{"HP", "C1750A", "3226", BLIST_NOLUN},		/* scanjet iic */
	{"HP", "C1790A", "", BLIST_NOLUN},		/* scanjet iip */
	{"HP", "C2500A", "", BLIST_NOLUN},		/* scanjet iicx */
	{"YAMAHA", "CDR100", "1.00", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CDR102", "1.00", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CRW8424S", "1.0", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CRW6416S", "1.0c", BLIST_NOLUN},	/* locks up */
	{"MITSUMI", "CD-R CR-2201CS", "6119", BLIST_NOLUN},	/* locks up */
	{"RELISYS", "Scorpio", NULL, BLIST_NOLUN},	/* responds to all lun */
	{"MICROTEK", "ScanMaker II", "5.61", BLIST_NOLUN},	/* responds to all lun */

	/*
	 * Other types of devices that have special flags.
	 */
	{"SONY", "CD-ROM CDU-8001", NULL, BLIST_BORKEN},
	{"TEXEL", "CD-ROM", "1.06", BLIST_BORKEN},
	{"IOMEGA", "Io20S         *F", NULL, BLIST_KEY},
	{"INSITE", "Floptical   F*8I", NULL, BLIST_KEY},
	{"INSITE", "I325VM", NULL, BLIST_KEY},
	{"LASOUND", "CDX7405", "3.10", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"MICROP", "4110", NULL, BLIST_NOTQ},
	{"NRC", "MBR-7", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7.4", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"REGAL", "CDC-4X", NULL, BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-4.8S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-5.16S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-600", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-602X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-604X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"EMULEX", "MD21/S2     ESDI", NULL, BLIST_SINGLELUN},
	{"CANON", "IPUBJD", NULL, BLIST_SPARSELUN},
	{"nCipher", "Fastness Crypto", NULL, BLIST_FORCELUN},
	{"DEC", "HSG80", NULL, BLIST_FORCELUN},
	{"COMPAQ", "LOGICAL VOLUME", NULL, BLIST_FORCELUN},
	{"COMPAQ", "CR3500", NULL, BLIST_FORCELUN},
	{"NEC", "PD-1 ODX654P", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"MATSHITA", "PD-1", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"iomega", "jaz 1GB", "J.86", BLIST_NOTQ | BLIST_NOLUN},
	{"TOSHIBA", "CDROM", NULL, BLIST_ISROM},
	{"TOSHIBA", "CD-ROM", NULL, BLIST_ISROM},
	{"MegaRAID", "LD", NULL, BLIST_FORCELUN},
	{"DGC", "RAID", NULL, BLIST_SPARSELUN},	/* Dell PV 650F, storage on LUN 0 */
	{"DGC", "DISK", NULL, BLIST_SPARSELUN},	/* Dell PV 650F, no storage on LUN 0 */
	{"DELL", "PV660F", NULL, BLIST_SPARSELUN},
	{"DELL", "PV660F   PSEUDO", NULL, BLIST_SPARSELUN},
	{"DELL", "PSEUDO DEVICE .", NULL, BLIST_SPARSELUN},	/* Dell PV 530F */
	{"DELL", "PV530F", NULL, BLIST_SPARSELUN},
	{"EMC", "SYMMETRIX", NULL, BLIST_SPARSELUN | BLIST_LARGELUN | BLIST_FORCELUN},
	{"HP", "A6189A", NULL, BLIST_SPARSELUN | BLIST_LARGELUN},	/* HP VA7400 */
	{"CMD", "CRA-7280", NULL, BLIST_SPARSELUN},	/* CMD RAID Controller */
	{"CNSI", "G7324", NULL, BLIST_SPARSELUN},	/* Chaparral G7324 RAID */
	{"CNSi", "G8324", NULL, BLIST_SPARSELUN},	/* Chaparral G8324 RAID */
	{"Zzyzx", "RocketStor 500S", NULL, BLIST_SPARSELUN},
	{"Zzyzx", "RocketStor 2000", NULL, BLIST_SPARSELUN},
	{"SONY", "TSL", NULL, BLIST_FORCELUN},		/* DDS3 & DDS4 autoloaders */
	{"DELL", "PERCRAID", NULL, BLIST_FORCELUN},
	{"HP", "NetRAID-4M", NULL, BLIST_FORCELUN},
	{"ADAPTEC", "AACRAID", NULL, BLIST_FORCELUN},
	{"ADAPTEC", "Adaptec 5400S", NULL, BLIST_FORCELUN},
	{"COMPAQ", "MSA1000", NULL, BLIST_FORCELUN},
	{"HP", "C1557A", NULL, BLIST_FORCELUN},
	{"IBM", "AuSaV1S2", NULL, BLIST_FORCELUN},
};

#define ALLOC_FAILURE_MSG	KERN_ERR "%s: Allocation failure during" \
	" SCSI scanning, some SCSI devices might not be configured\n"

/*
 * Prefix values for the SCSI id's (stored in driverfs name field)
 */
#define SCSI_UID_SER_NUM 'S'
#define SCSI_UID_UNKNOWN 'Z'

/*
 * Return values of some of the scanning functions.
 *
 * SCSI_SCAN_NO_RESPONSE: no valid response received from the target, this
 * includes allocation or general failures preventing IO from being sent.
 *
 * SCSI_SCAN_TARGET_PRESENT: target responded, but no device is available
 * on the given LUN.
 *
 * SCSI_SCAN_LUN_PRESENT: target responded, and a device is available on a
 * given LUN.
 */
#define SCSI_SCAN_NO_RESPONSE		0
#define SCSI_SCAN_TARGET_PRESENT	1
#define SCSI_SCAN_LUN_PRESENT		2

static char *scsi_null_device_strs = "nullnullnullnull";

#define MAX_SCSI_LUNS	512

#ifdef CONFIG_SCSI_MULTI_LUN
static unsigned int max_scsi_luns = MAX_SCSI_LUNS;
#else
static unsigned int max_scsi_luns = 1;
#endif

#ifdef MODULE
MODULE_PARM(max_scsi_luns, "i");
MODULE_PARM_DESC(max_scsi_luns,
		 "last scsi LUN (should be between 1 and 2^32-1)");
#else

static int __init scsi_luns_setup(char *str)
{
	unsigned int tmp;

	if (get_option(&str, &tmp) == 1) {
		max_scsi_luns = tmp;
		return 1;
	} else {
		printk(KERN_WARNING "scsi_luns_setup: usage max_scsi_luns=n "
		       "(n should be between 1 and 2^32-1)\n");
		return 0;
	}
}

__setup("max_scsi_luns=", scsi_luns_setup);

#endif

#ifdef CONFIG_SCSI_REPORT_LUNS
/*
 * max_scsi_report_luns: the maximum number of LUNS that will be
 * returned from the REPORT LUNS command. 8 times this value must
 * be allocated. In theory this could be up to an 8 byte value, but
 * in practice, the maximum number of LUNs suppored by any device
 * is about 16k.
 */
static unsigned int max_scsi_report_luns = 128;

#ifdef MODULE
MODULE_PARM(max_scsi_report_luns, "i");
MODULE_PARM_DESC(max_scsi_report_luns,
		 "REPORT LUNS maximum number of LUNS received (should be"
		 " between 1 and 16384)");
#else
static int __init scsi_report_luns_setup(char *str)
{
	unsigned int tmp;

	if (get_option(&str, &tmp) == 1) {
		max_scsi_report_luns = tmp;
		return 1;
	} else {
		printk(KERN_WARNING "scsi_report_luns_setup: usage"
		       " max_scsi_report_luns=n (n should be between 1"
		       " and 16384)\n");
		return 0;
	}
}

__setup("max_scsi_report_luns=", scsi_report_luns_setup);
#endif
#endif

/**
 * scsi_unlock_floptical - unlock device via a special MODE SENSE command
 * @sreq:	used to send the command
 * @result:	area to store the result of the MODE SENSE
 *
 * Description:
 *     Send a vendor specific MODE SENSE (not a MODE SELECT) command using
 *     @sreq to unlock a device, storing the (unused) results into result.
 *     Called for BLIST_KEY devices.
 **/
static void scsi_unlock_floptical(Scsi_Request *sreq, unsigned char *result)
{
	Scsi_Device *sdscan = sreq->sr_device;
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];

	printk(KERN_NOTICE "scsi: unlocking floptical drive\n");
	scsi_cmd[0] = MODE_SENSE;
	if (sdscan->scsi_level <= SCSI_2)
		scsi_cmd[1] = (sdscan->lun << 5) & 0xe0;
	else
		scsi_cmd[1] = 0;
	scsi_cmd[2] = 0x2e;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 0x2a;	/* size */
	scsi_cmd[5] = 0;
	sreq->sr_cmd_len = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(sreq, (void *) scsi_cmd, (void *) result, 0x2a /* size */,
		      SCSI_TIMEOUT, 3);
}

/**
 * scsi_device_type_read - copy out the SCSI type
 * @driverfs_dev:	driverfs device to check
 * @page:		copy data into this area
 * @count:		number of bytes to copy
 * @off:		start at this offset in page
 *
 * Description:
 *     Called via driverfs when the "type" (in scsi_device_type_file)
 *     field is read. Copy the appropriate SCSI type string into @page,
 *     followed by a newline and a '\0'. Go through gyrations so we don't
 *     write more than @count, and we don't write past @off.
 *
 * Notes:
 *     This is for the top-most scsi entry in driverfs, the upper-level
 *     drivers have their own type file. XXX This is not part of scanning,
 *     other than we reference the attr struct in this file, move to
 *     scsi.c or scsi_lib.c.
 *
 * Return:
 *     number of bytes written into page.
 **/
static ssize_t scsi_device_type_read(struct device *driverfs_dev, char *page,
	size_t count, loff_t off)
{
	struct scsi_device *sdev = to_scsi_device(driverfs_dev);
	const char *type;
	size_t size, len;

	if ((sdev->type > MAX_SCSI_DEVICE_CODE) ||
	    (scsi_device_types[(int)sdev->type] == NULL))
		type = "Unknown";
	else
		type = scsi_device_types[(int)sdev->type];
	size = strlen(type);
	/*
	 * Check if off is past size + 1 for newline + 1 for a '\0'.
	 */
	if (off >= (size + 2))
		return 0;
	if (size > off) {
		len = min((size_t) (size - off), count);
		memcpy(page + off, type + off, len);
	} else
		len = 0;
	if (((len + off) == size) && (len < count))
		/*
		 * We are at the end of the string and have space, add a
		 * new line.
		 */
		*(page + off + len++) = '\n';
	if (((len + off) == (size + 1)) && (len < count))
		/*
		 * We are past the newline and have space, add a
		 * terminating '\0'.
		 */
		*(page + off + len++) = '\0';
	return len;
}

/*
 * Create dev_attr_type. This is different from the dev_attr_type in scsi
 * upper level drivers.
 */
static DEVICE_ATTR(type,S_IRUGO,scsi_device_type_read,NULL);


/**
 * print_inquiry - printk the inquiry information
 * @inq_result:	printk this SCSI INQUIRY
 *
 * Description:
 *     printk the vendor, model, and other information found in the
 *     INQUIRY data in @inq_result.
 *
 * Notes:
 *     Remove this, and replace with a hotplug event that logs any
 *     relevant information.
 **/
static void print_inquiry(unsigned char *inq_result)
{
	int i;

	printk(KERN_NOTICE "  Vendor: ");
	for (i = 8; i < 16; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("  Model: ");
	for (i = 16; i < 32; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("  Rev: ");
	for (i = 32; i < 36; i++)
		if (inq_result[i] >= 0x20 && i < inq_result[4] + 5)
			printk("%c", inq_result[i]);
		else
			printk(" ");

	printk("\n");

	i = inq_result[0] & 0x1f;

	printk(KERN_NOTICE "  Type:   %s ",
	       i <
	       MAX_SCSI_DEVICE_CODE ? scsi_device_types[i] :
	       "Unknown          ");
	printk("                 ANSI SCSI revision: %02x",
	       inq_result[2] & 0x07);
	if ((inq_result[2] & 0x07) == 1 && (inq_result[3] & 0x0f) == 1)
		printk(" CCS\n");
	else
		printk("\n");
}

/**
 * get_device_flags - get device specific flags from the device_list
 * @vendor:	vendor name
 * @model:	model name
 *
 * Description:
 *     Search device_list for an entry matching @vendor and @model, if
 *     found, return the matching flags value, else return 0.
 *     Partial matches count as success - good for @model, but maybe not
 *     @vendor.
 **/
static int get_device_flags(unsigned char *vendor, unsigned char *model)
{
	int i;
	size_t max;

	for (i = 0; i < ARRAY_SIZE(device_list); i++) {
		/*
		 * XXX why skip leading spaces? If an odd INQUIRY value,
		 * that should have been part of the device_list[] entry,
		 * such as "  FOO" rather than "FOO". Since this code is
		 * already here, and we don't know what device it is
		 * trying to work with, leave it as-is.
		 */
		max = 8;	/* max length of vendor */
		while ((max > 0) && *vendor == ' ') {
			max--;
			vendor++;
		}
		/*
		 * XXX removing the following strlen() would be good,
		 * using it means that for a an entry not in the list, we
		 * scan every byte of every vendor listed in
		 * device_list[], and never match a single one (and still
		 * have to compare at least the first byte of each
		 * vendor).
		 */
		if (memcmp(device_list[i].vendor, vendor,
			    min(max, strlen(device_list[i].vendor))))
			continue;
		/*
		 * Skip spaces again.
		 */
		max = 16;	/* max length of model */
		while ((max > 0) && *model == ' ') {
			max--;
			model++;
		}
		if (memcmp(device_list[i].model, model,
			   min(max, strlen(device_list[i].model))))
			continue;
		return device_list[i].flags;
	}
	return 0;
}

/**
 * scsi_alloc_sdev - allocate and setup a Scsi_Device
 *
 * Description:
 *     Allocate, initialize for io, and return a pointer to a Scsi_Device.
 *     Stores the @shost, @channel, @id, and @lun in the Scsi_Device, and
 *     adds Scsi_Device to the appropriate list.
 *
 * Return value:
 *     Scsi_Device pointer, or NULL on failure.
 **/
static Scsi_Device *scsi_alloc_sdev(struct Scsi_Host *shost, uint channel,
				    uint id, uint lun)
{
	Scsi_Device *sdev;

	sdev = (Scsi_Device *) kmalloc(sizeof(Scsi_Device), GFP_ATOMIC);
	if (sdev == NULL)
		printk(ALLOC_FAILURE_MSG, __FUNCTION__);
	else {
		memset(sdev, 0, sizeof(Scsi_Device));
		sdev->vendor = scsi_null_device_strs;
		sdev->model = scsi_null_device_strs;
		sdev->rev = scsi_null_device_strs;
		sdev->host = shost;
		sdev->id = id;
		sdev->lun = lun;
		sdev->channel = channel;
		sdev->online = TRUE;
		/*
		 * Some low level driver could use device->type
		 */
		sdev->type = -1;
		/*
		 * Assume that the device will have handshaking problems,
		 * and then fix this field later if it turns out it
		 * doesn't
		 */
		sdev->borken = 1;
		scsi_initialize_queue(sdev, shost);
		sdev->request_queue.queuedata = (void *) sdev;

		scsi_initialize_merge_fn(sdev);
		init_waitqueue_head(&sdev->scpnt_wait);

		/*
		 * Add it to the end of the shost->host_queue list.
		 */
		if (shost->host_queue != NULL) {
			sdev->prev = shost->host_queue;
			while (sdev->prev->next != NULL)
				sdev->prev = sdev->prev->next;
			sdev->prev->next = sdev;
		} else
			shost->host_queue = sdev;

	}
	return (sdev);
}

/**
 * scsi_free_sdev - cleanup and free a Scsi_Device
 * @sdev:	cleanup and free this Scsi_Device
 *
 * Description:
 *     Undo the actions in scsi_alloc_sdev, including removing @sdev from
 *     the list, and freeing @sdev.
 **/
static void scsi_free_sdev(Scsi_Device *sdev)
{
	if (sdev->prev != NULL)
		sdev->prev->next = sdev->next;
	else
		sdev->host->host_queue = sdev->next;
	if (sdev->next != NULL)
		sdev->next->prev = sdev->prev;

	blk_cleanup_queue(&sdev->request_queue);
	if (sdev->inquiry != NULL)
		kfree(sdev->inquiry);
	kfree(sdev);
}

/**
 * scsi_check_id_size - check if size fits in the driverfs name
 * @sdev:	Scsi_Device to use for error message
 * @size:	the length of the id we want to store
 *
 * Description:
 *     Use a function for this since the same code is used in various
 *     places, and we only create one string and call to printk.
 *
 * Return:
 *     0 - fits
 *     1 - size too large
 **/
static int scsi_check_id_size(Scsi_Device *sdev, int size)
{
	if (size > DEVICE_NAME_SIZE) {
		printk(KERN_WARNING "scsi scan: host %d channel %d id %d lun %d"
		       " identifier too long, length %d, max %d. Device might"
		       " be improperly identified.\n", sdev->host->host_no,
		       sdev->channel, sdev->id, sdev->lun, size,
		       DEVICE_NAME_SIZE);
		return 1;
	} else
		return 0;
}

/**
 * scsi_get_evpd_page - get a list of supported vpd pages
 * @sdev:	Scsi_Device to send an INQUIRY VPD
 * @sreq:	Scsi_Request associated with @sdev
 *
 * Description:
 *     Get SCSI INQUIRY Vital Product Data page 0 - a list of supported
 *     VPD pages.
 *
 * Return:
 *     A pointer to data containing the results on success, else NULL.
 **/
unsigned char *scsi_get_evpd_page(Scsi_Device *sdev, Scsi_Request *sreq)
{
	unsigned char *evpd_page;
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	int lun = sdev->lun;
	int scsi_level = sdev->scsi_level;
	int max_lgth = 255;

retry:
	evpd_page = kmalloc(max_lgth, GFP_ATOMIC |
			      (sdev->host->unchecked_isa_dma) ?
			      GFP_DMA : 0);
	if (!evpd_page) {
		printk(KERN_WARNING "scsi scan: Allocation failure identifying"
		       " host %d channel %d id %d lun %d, device might be"
		       " improperly identified.\n", sdev->host->host_no,
		       sdev->channel, sdev->id, sdev->lun);
		return NULL;
	}

	memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[4] = max_lgth;
	sreq->sr_cmd_len = 0;
	sreq->sr_sense_buffer[0] = 0;
	sreq->sr_sense_buffer[2] = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(sreq, (void *) scsi_cmd, (void *) evpd_page,
			max_lgth, SCSI_TIMEOUT+4*HZ, 3);

	if (sreq->sr_result) {
		kfree(evpd_page);
		return NULL;
	}

	/*
	 * check to see if response was truncated
	 */
	if (evpd_page[3] > max_lgth) {
		max_lgth = evpd_page[3] + 4;
		kfree(evpd_page);
		goto retry;
	}

	/*
	 * Some ill behaved devices return the standard inquiry here
	 * rather than the evpd data, snoop the data to verify.
	 */
	if (evpd_page[3] > 16) {
		/*
		 * If the vendor id appears in the evpd page assume the
		 * page is invalid.
		 */
		if (!strncmp(&evpd_page[8], sdev->vendor, 8)) {
			kfree(evpd_page);
			return NULL;
		}
	}
	return evpd_page;
}


/*
 * INQUIRY VPD page 0x83 identifier descriptor related values. Reference the
 * SCSI Primary Commands specification for details.
 *
 * XXX The following defines should be in scsi.h
 */

/*
 * id type values of id descriptors. These are assumed to fit in 4 bits,
 * else the code using hex_str[id_type] needs modification.
 */
#define SCSI_ID_VENDOR_SPECIFIC	0
#define SCSI_ID_T10_VENDOR	1
#define SCSI_ID_EUI_64		2
#define SCSI_ID_NAA		3

/*
 * Supported NAA values. These fit in 4 bits, so the don't care value
 * cannot conflict with real values.
 *
 */
#define	SCSI_ID_NAA_DONT_CARE		0xff
#define	SCSI_ID_NAA_IEEE_REG		5
#define	SCSI_ID_NAA_IEEE_REG_EXTENDED	6

/*
 * Supported Code Set values.
 */
#define	SCSI_ID_BINARY	1
#define	SCSI_ID_ASCII	2

/*
 * Use a priority based list of id, naa, and binary/ascii for the
 * identifier descriptor in VPD page 0x83.
 *
 * Brute force search for a match starting with the first value in
 * id_search_list. This is not a performance issue, since there
 * is normally one or some small number of descriptors.
 */
struct scsi_id_search_values {
	int	id_type;
	int	naa_type;
	int	code_set;
};

static const struct scsi_id_search_values id_search_list[] = {
	{ SCSI_ID_NAA,	SCSI_ID_NAA_IEEE_REG_EXTENDED,	SCSI_ID_BINARY },
	{ SCSI_ID_NAA,	SCSI_ID_NAA_IEEE_REG_EXTENDED,	SCSI_ID_ASCII },
	{ SCSI_ID_NAA,	SCSI_ID_NAA_IEEE_REG,	SCSI_ID_BINARY },
	{ SCSI_ID_NAA,	SCSI_ID_NAA_IEEE_REG,	SCSI_ID_ASCII },
	/*
	 * Devices already exist using NAA values that are now marked
	 * reserved. These should not conflict with other values, or it is
	 * a bug in the device. As long as we find the IEEE extended one
	 * first, we really don't care what other ones are used. Using
	 * don't care here means that a device that returns multiple
	 * non-IEEE descriptors in a random order will get different
	 * names.
	 */
	{ SCSI_ID_NAA,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_BINARY },
	{ SCSI_ID_NAA,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_ASCII },
	{ SCSI_ID_EUI_64,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_BINARY },
	{ SCSI_ID_EUI_64,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_ASCII },
	{ SCSI_ID_T10_VENDOR,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_BINARY },
	{ SCSI_ID_T10_VENDOR,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_ASCII },
	{ SCSI_ID_VENDOR_SPECIFIC,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_BINARY },
	{ SCSI_ID_VENDOR_SPECIFIC,	SCSI_ID_NAA_DONT_CARE,	SCSI_ID_ASCII },
};

/**
 * scsi_check_fill_deviceid - check the id and if OK fill it
 * @sdev:	device to use for error messages
 * @id_page:	id descriptor for INQUIRY VPD DEVICE ID, page 0x83
 * @name:	store the id in name
 * @id_search:	store if the id_page matches these values
 *
 * Description:
 *     Check if @id_page matches the @id_search, if so store an id (uid)
 *     into name.
 *
 * Return:
 *     0: Success
 *     1: No match
 *     2: Failure due to size constraints
 **/
static int scsi_check_fill_deviceid(Scsi_Device *sdev, char *id_page,
	char *name, const struct scsi_id_search_values *id_search)
{
	static const char hex_str[]="0123456789abcdef";
	int i, j;

	/*
	 * ASSOCIATION must be with the device (value 0)
	 */
	if ((id_page[1] & 0x30) != 0)
		return 1;

	if ((id_page[1] & 0x0f) != id_search->id_type)
		return 1;
	/*
	 * Possibly check NAA sub-type.
	 */
	if ((id_search->naa_type != SCSI_ID_NAA_DONT_CARE) &&
	    (id_search->naa_type != (id_page[4] & 0xf0) >> 4)) {
		return 1;
	}

	/*
	 * Check for matching code set - ASCII or BINARY.
	 */
	if ((id_page[0] & 0x0f) != id_search->code_set)
		return 1;

	name[0]  = hex_str[id_search->id_type];
	if ((id_page[0] & 0x0f) == SCSI_ID_ASCII) {
		/*
		 * ASCII descriptor.
		 */
		if (id_search->id_type == SCSI_ID_VENDOR_SPECIFIC) {
			/*
			 * Prepend the vendor and model before the id,
			 * since the id might not be unique across all
			 * vendors and models. The same code is used
			 * below, with a differnt size.
			 *
			 * Need 1 byte for the idtype, 1 for trailing
			 * '\0', 8 for vendor, 16 for model total 26, plus
			 * the name descriptor length.
			 */
			if (scsi_check_id_size(sdev, 26 + id_page[3]))
				return 2;
			else {
				strncat(name, sdev->vendor, 8);
				strncat(name, sdev->model, 16);
			}
		} else if (scsi_check_id_size (sdev, (2 + id_page[3])))
			/*
			 * Need 1 byte for the idtype, 1 byte for
			 * the trailing '\0', plus the descriptor length.
			 */
			return 2;
		memcpy(&name[strlen(name)], &id_page[4], id_page[3]);
		return 0;
	} else if ((id_page[0] & 0x0f) == SCSI_ID_BINARY) {
		if (id_search->id_type == SCSI_ID_VENDOR_SPECIFIC) {
			/*
			 * Prepend the vendor and model before the id.
			 */
			if (scsi_check_id_size(sdev, 26 + (id_page[3] * 2)))
				return 2;
			else {
				strncat(name, sdev->vendor, 8);
				strncat(name, sdev->model, 16);
			}
		} else if (scsi_check_id_size(sdev, 2 + (id_page[3] * 2)))
			/*
			 * Need 1 byte for the idtype, 1 for trailing
			 * '\0', 8 for vendor, 16 for model total 26, plus
			 * the name descriptor length.
			 */
			return 2;
		/*
		 * Binary descriptor, convert to ASCII, using two bytes of
		 * ASCII for each byte in the id_page. Store starting at
		 * the end of name.
		 */
		for(i = 4, j = strlen(name); i < 4 + id_page[3]; i++) {
			name[j++] = hex_str[(id_page[i] & 0xf0) >> 4];
			name[j++] = hex_str[id_page[i] & 0x0f];
		}
		return 0;
	}
	/*
	 * Code set must have already matched.
	 */
	printk(KERN_ERR "scsi scan: scsi_check_fill_deviceid unexpected state.\n");
	return 1;
}

/**
 * scsi_get_deviceid - get a device id using INQUIRY VPD page 0x83
 * @sdev:	get the identifer of this device
 * @sreq:	Scsi_Requeset associated with @sdev
 *
 * Description:
 *     Try to get an id (serial number) for device @sdev using a SCSI
 *     Vital Product Data page 0x83 (device id).
 *
 * Return:
 *     0: Failure
 *     1: Success
 **/
int scsi_get_deviceid(Scsi_Device *sdev, Scsi_Request *sreq)
{
	unsigned char *id_page;
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	int id_idx, scnt, ret;
	int lun = sdev->lun;
	int scsi_level = sdev->scsi_level;
	int max_lgth = 255;

retry:
	id_page = kmalloc(max_lgth, GFP_ATOMIC |
			      (sdev->host->unchecked_isa_dma) ?
			      GFP_DMA : 0);
	if (!id_page) {
		printk(KERN_WARNING "scsi scan: Allocation failure identifying"
		       " host %d channel %d id %d lun %d, device might be"
		       " improperly identified.\n", sdev->host->host_no,
		       sdev->channel, sdev->id, sdev->lun);
		return 0;
	}

	memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[2] = 0x83;
	scsi_cmd[4] = max_lgth;
	sreq->sr_cmd_len = 0;
	sreq->sr_sense_buffer[0] = 0;
	sreq->sr_sense_buffer[2] = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(sreq, (void *) scsi_cmd, (void *) id_page,
			max_lgth, SCSI_TIMEOUT+4*HZ, 3);
	if (sreq->sr_result) {
		ret = 0;
		goto leave;
	}

	/*
	 * check to see if response was truncated
	 */
	if (id_page[3] > max_lgth) {
		max_lgth = id_page[3] + 4;
		kfree(id_page);
		goto retry;
	}

	/*
	 * Search for a match in the proiritized id_search_list.
	 */
	for (id_idx = 0; id_idx < ARRAY_SIZE(id_search_list); id_idx++) {
		/*
		 * Examine each descriptor returned. There is normally only
		 * one or a small number of descriptors.
		 */
		for(scnt = 4; scnt <= id_page[3] + 3;
			scnt += id_page[scnt + 3] + 4) {
			if ((scsi_check_fill_deviceid(sdev, &id_page[scnt],
			     sdev->sdev_driverfs_dev.name,
			     &id_search_list[id_idx])) == 0) {
				SCSI_LOG_SCAN_BUS(4, printk(KERN_INFO
				  "scsi scan: host %d channel %d id %d lun %d"
				  " used id desc %d/%d/%d\n",
				  sdev->host->host_no, sdev->channel,
				  sdev->id, sdev->lun,
				  id_search_list[id_idx].id_type,
				  id_search_list[id_idx].naa_type,
				  id_search_list[id_idx].code_set));
				ret = 1;
				goto leave;
			} else {
				SCSI_LOG_SCAN_BUS(4, printk(KERN_INFO
				  "scsi scan: host %d channel %d id %d lun %d"
				  " no match/error id desc %d/%d/%d\n",
				  sdev->host->host_no, sdev->channel,
				  sdev->id, sdev->lun,
				  id_search_list[id_idx].id_type,
				  id_search_list[id_idx].naa_type,
				  id_search_list[id_idx].code_set));
			}
			/*
			 * scsi_check_fill_deviceid can fill the first
			 * byte of name with a non-zero value, reset it.
			 */
			sdev->sdev_driverfs_dev.name[0] = '\0';
		}
	}
	ret = 0;

  leave:
	kfree(id_page);
	return ret;
}

/**
 * scsi_get_serialnumber - get a serial number using INQUIRY page 0x80
 * @sdev:	get the serial number of this device
 * @sreq:	Scsi_Requeset associated with @sdev
 *
 * Description:
 *     Send a SCSI INQUIRY page 0x80 to @sdev to get a serial number.
 *
 * Return:
 *     0: Failure
 *     1: Success
 **/
int scsi_get_serialnumber(Scsi_Device *sdev, Scsi_Request *sreq)
{
	unsigned char *serialnumber_page;
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	int lun = sdev->lun;
	int scsi_level = sdev->scsi_level;
	int max_lgth = 255;

 retry:
	serialnumber_page = kmalloc(max_lgth, GFP_ATOMIC |
			      (sdev->host->unchecked_isa_dma) ?
			      GFP_DMA : 0);
	if (!serialnumber_page) {
		printk(KERN_WARNING "scsi scan: Allocation failure identifying"
		       " host %d channel %d id %d lun %d, device might be"
		       " improperly identified.\n", sdev->host->host_no,
		       sdev->channel, sdev->id, sdev->lun);
		return 0;
	}

	memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[2] = 0x80;
	scsi_cmd[4] = max_lgth;
	sreq->sr_cmd_len = 0;
	sreq->sr_sense_buffer[0] = 0;
	sreq->sr_sense_buffer[2] = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(sreq, (void *) scsi_cmd, (void *) serialnumber_page,
			max_lgth, SCSI_TIMEOUT+4*HZ, 3);

	if (sreq->sr_result)
		goto leave;
	/*
	 * check to see if response was truncated
	 */
	if (serialnumber_page[3] > max_lgth) {
		max_lgth = serialnumber_page[3] + 4;
		kfree(serialnumber_page);
		goto retry;
	}

	/*
	 * Need 1 byte for SCSI_UID_SER_NUM, 1 for trailing '\0', 8 for
	 * vendor, 16 for model = 26, plus serial number size.
	 */
	if (scsi_check_id_size (sdev, (26 + serialnumber_page[3])))
		goto leave;
	sdev->sdev_driverfs_dev.name[0] = SCSI_UID_SER_NUM;
	strncat(sdev->sdev_driverfs_dev.name, sdev->vendor, 8);
	strncat(sdev->sdev_driverfs_dev.name, sdev->model, 16);
	strncat(sdev->sdev_driverfs_dev.name, &serialnumber_page[4],
		serialnumber_page[3]);
	kfree(serialnumber_page);
	return 1;
 leave:
	memset(sdev->sdev_driverfs_dev.name, 0, DEVICE_NAME_SIZE);
	kfree(serialnumber_page);
	return 0;
}

/**
 * scsi_get_default_name - get a default name
 * @sdev:	get a default name for this device
 *
 * Description:
 *     Set the name of @sdev to the concatenation of the vendor, model,
 *     and revision found in @sdev.
 *
 * Return:
 *     1: Success
 **/
int scsi_get_default_name(Scsi_Device *sdev)
{
	if (scsi_check_id_size(sdev, 29))
		return 0;
	else {
		sdev->sdev_driverfs_dev.name[0] = SCSI_UID_UNKNOWN;
		strncpy(&sdev->sdev_driverfs_dev.name[1], sdev->vendor, 8);
		strncat(sdev->sdev_driverfs_dev.name, sdev->model, 16);
		strncat(sdev->sdev_driverfs_dev.name, sdev->rev, 4);
		return 1;
	}
}

/**
 * scsi_load_identifier:
 * @sdev:	get an identifier (name) of this device
 * @sreq:	Scsi_Requeset associated with @sdev
 *
 * Description:
 *     Determine what INQUIRY pages are supported by @sdev, and try the
 *     different pages until we get an identifier, or no other pages are
 *     left. Start with page 0x83 (device id) and then try page 0x80
 *     (serial number). If neither of these pages gets an id, use the
 *     default naming convention.
 *
 *     The first character of sdev_driverfs_dev.name is SCSI_UID_SER_NUM
 *     (S) if we used page 0x80, SCSI_UID_UNKNOWN (Z) if we used the
 *     default name, otherwise it starts with the page 0x83 id type
 *     (see the SCSI Primary Commands specification for details).
 *
 * Notes:
 *     If a device returns the same serial number for different LUNs or
 *     even for different LUNs on different devices, special handling must
 *     be added to get an id, or a new black list flag must to added and
 *     used in device_list[] (so we use the default name, or add a way to
 *     prefix the id/name with SCSI_UID_UNKNOWN - and change the define to
 *     something meaningful like SCSI_UID_NOT_UNIQUE). Complete user level
 *     scanning would be nice for such devices, so we do not need device
 *     specific code in the kernel.
 **/
static void scsi_load_identifier(Scsi_Device *sdev, Scsi_Request *sreq)
{
	unsigned char *evpd_page = NULL;
	int cnt;

	memset(sdev->sdev_driverfs_dev.name, 0, DEVICE_NAME_SIZE);
	evpd_page = scsi_get_evpd_page(sdev, sreq);
	if (evpd_page == NULL) {
		/*
		 * try to obtain serial number anyway
		 */
		(void)scsi_get_serialnumber(sdev, sreq);
	} else {
		/*
		 * XXX search high to low, since the pages are lowest to
		 * highest - page 0x83 will be after page 0x80.
		 */
		for(cnt = 4; cnt <= evpd_page[3] + 3; cnt++)
			if (evpd_page[cnt] == 0x83)
				if (scsi_get_deviceid(sdev, sreq))
					goto leave;

		for(cnt = 4; cnt <= evpd_page[3] + 3; cnt++)
			if (evpd_page[cnt] == 0x80)
				if (scsi_get_serialnumber(sdev, sreq))
					goto leave;

		if (sdev->sdev_driverfs_dev.name[0] == 0)
			scsi_get_default_name(sdev);

	}
leave:
	if (evpd_page) kfree(evpd_page);
	SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: host %d channel %d"
	    " id %d lun %d name/id: '%s'\n", sdev->host->host_no,
	    sdev->channel, sdev->id, sdev->lun, sdev->sdev_driverfs_dev.name));
	return;
}

/**
 * scsi_find_scsi_level - return the scsi_level of a matching target
 *
 * Description:
 *     Return the scsi_level of any Scsi_Device matching @channel, @id,
 *     and @shost.
 * Notes:
 *     Needs to issue an INQUIRY to LUN 0 if no Scsi_Device matches, and
 *     if the INQUIRY can't be sent return a failure.
 **/
static int scsi_find_scsi_level(unsigned int channel, unsigned int id,
				struct Scsi_Host *shost)
{
	int res = SCSI_2;
	Scsi_Device *sdev;

	for (sdev = shost->host_queue; sdev; sdev = sdev->next)
		if ((id == sdev->id) && (channel == sdev->channel))
			return (int) sdev->scsi_level;
	/*
	 * FIXME No matching target id is configured, this needs to get
	 * the INQUIRY for LUN 0, and use it to determine the scsi_level.
	 */
	return res;
}

/**
 * scsi_probe_lun - probe a single LUN using a SCSI INQUIRY
 * @sreq:	used to send the INQUIRY
 * @inq_result:	area to store the INQUIRY result
 * @bflags:	store any bflags found here
 *
 * Description:
 *     Probe the lun associated with @sreq using a standard SCSI INQUIRY;
 *
 *     If the INQUIRY is successful, sreq->sr_result is zero and: the
 *     INQUIRY data is in @inq_result; the scsi_level and INQUIRY length
 *     are copied to the Scsi_Device at @sreq->sr_device (sdev);
 *     any device_list flags value is stored in *@bflags.
 **/
static void scsi_probe_lun(Scsi_Request *sreq, char *inq_result,
			   int *bflags)
{
	Scsi_Device *sdev = sreq->sr_device;	/* a bit ugly */
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	int possible_inq_resp_len;

	SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: INQUIRY to host %d"
			" channel %d id %d lun %d\n", sdev->host->host_no,
			sdev->channel, sdev->id, sdev->lun));

	memset(scsi_cmd, 0, 6);
	scsi_cmd[0] = INQUIRY;
	if ((sdev->lun > 0) && (sdev->scsi_level <= SCSI_2))
		scsi_cmd[1] = (sdev->lun << 5) & 0xe0;
	scsi_cmd[4] = 36;	/* issue conservative alloc_length */
	sreq->sr_cmd_len = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;

	memset(inq_result, 0, 36);
	scsi_wait_req(sreq, (void *) scsi_cmd, (void *) inq_result, 36,
		      SCSI_TIMEOUT + 4 * HZ, 3);

	SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: 1st INQUIRY %s with"
			" code 0x%x\n", sreq->sr_result ?
			"failed" : "successful", sreq->sr_result));

	if (sreq->sr_result) {
		if ((driver_byte(sreq->sr_result) & DRIVER_SENSE) != 0 &&
		    (sreq->sr_sense_buffer[2] & 0xf) == UNIT_ATTENTION &&
		    sreq->sr_sense_buffer[12] == 0x28 &&
		    sreq->sr_sense_buffer[13] == 0) {
			/* not-ready to ready transition - good */
			/* dpg: bogus? INQUIRY never returns UNIT_ATTENTION */
		} else
			/*
			 * assume no peripheral if any other sort of error
			 */
			return;
	}

	/*
	 * Get any flags for this device.
	 *
	 * XXX add a bflags to Scsi_Device, and replace the corresponding
	 * bit fields in Scsi_Device, so bflags need not be passed as an
	 * argument.
	 */
	BUG_ON(bflags == NULL);
	*bflags = get_device_flags(&inq_result[8], &inq_result[16]);

	possible_inq_resp_len = (unsigned char) inq_result[4] + 5;
	if (BLIST_INQUIRY_36 & *bflags)
		possible_inq_resp_len = 36;
	else if (BLIST_INQUIRY_58 & *bflags)
		possible_inq_resp_len = 58;
	else if (possible_inq_resp_len > 255)
		possible_inq_resp_len = 36;	/* sanity */

	if (possible_inq_resp_len > 36) {	/* do additional INQUIRY */
		memset(scsi_cmd, 0, 6);
		scsi_cmd[0] = INQUIRY;
		if ((sdev->lun > 0) && (sdev->scsi_level <= SCSI_2))
			scsi_cmd[1] = (sdev->lun << 5) & 0xe0;
		scsi_cmd[4] = (unsigned char) possible_inq_resp_len;
		sreq->sr_cmd_len = 0;
		sreq->sr_data_direction = SCSI_DATA_READ;
		/*
		 * re-zero inq_result just to be safe.
		 */
		memset(inq_result, 0, possible_inq_resp_len);
		scsi_wait_req(sreq, (void *) scsi_cmd,
			      (void *) inq_result,
			      possible_inq_resp_len, SCSI_TIMEOUT + 4 * HZ, 3);
		SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: 2nd INQUIRY"
				" %s with code 0x%x\n", sreq->sr_result ?
				"failed" : "successful", sreq->sr_result));
		if (sreq->sr_result)
			return;

		/*
		 * The INQUIRY can change, this means the length can change.
		 */
		possible_inq_resp_len = (unsigned char) inq_result[4] + 5;
		if (BLIST_INQUIRY_58 & *bflags)
			possible_inq_resp_len = 58;
		else if (possible_inq_resp_len > 255)
			possible_inq_resp_len = 36;	/* sanity */
	}

	sdev->inquiry_len = possible_inq_resp_len;

	/*
	 * XXX Abort if the response length is less than 36? If less than
	 * 32, the lookup of the device flags (above) could be invalid,
	 * and it would be possible to take an incorrect action - we do
	 * not want to hang because of a short INQUIRY. On the flip side,
	 * if the device is spun down or becoming ready (and so it gives a
	 * short INQUIRY), an abort here prevents any further use of the
	 * device, including spin up.
	 *
	 * Related to the above issue:
	 *
	 * XXX Devices (disk or all?) should be sent a TEST UNIT READY,
	 * and if not ready, sent a START_STOP to start (maybe spin up) and
	 * then send the INQUIRY again, since the INQUIRY can change after
	 * a device is initialized.
	 *
	 * Ideally, start a device if explicitly asked to do so.  This
	 * assumes that a device is spun up on power on, spun down on
	 * request, and then spun up on request.
	 */

	/*
	 * The scanning code needs to know the scsi_level, even if no
	 * device is attached at LUN 0 (SCSI_SCAN_TARGET_PRESENT) so
	 * non-zero LUNs can be scanned.
	 */
	sdev->scsi_level = inq_result[2] & 0x07;
	if (sdev->scsi_level >= 2 ||
	    (sdev->scsi_level == 1 && (inq_result[3] & 0x0f) == 1))
		sdev->scsi_level++;

	return;
}

/**
 * scsi_add_lun - allocate and fully initialze a Scsi_Device
 * @sdevscan:	holds information to be stored in the new Scsi_Device
 * @sdevnew:	store the address of the newly allocated Scsi_Device
 * @sreq:	scsi request used when getting an identifier
 * @inq_result:	holds the result of a previous INQUIRY to the LUN
 * @bflags:	flags value from device_list
 *
 * Description:
 *     Allocate and initialize a Scsi_Device matching sdevscan. Optionally
 *     set fields based on values in *@bflags. If @sdevnew is not
 *     NULL, store the address of the new Scsi_Device in *@sdevnew (needed
 *     when scanning a particular LUN).
 *
 * Return:
 *     SCSI_SCAN_NO_RESPONSE: could not allocate or setup a Scsi_Device
 *     SCSI_SCAN_LUN_PRESENT: a new Scsi_Device was allocated and initialized
 **/
static int scsi_add_lun(Scsi_Device *sdevscan, Scsi_Device **sdevnew,
			Scsi_Request *sreq, char *inq_result, int *bflags)
{
	Scsi_Device *sdev;
	struct Scsi_Device_Template *sdt;
	char devname[64];
	extern devfs_handle_t scsi_devfs_handle;

	sdev = scsi_alloc_sdev(sdevscan->host, sdevscan->channel,
				     sdevscan->id, sdevscan->lun);
	if (sdev == NULL)
		return SCSI_SCAN_NO_RESPONSE;

	sdev->scsi_level = sdevscan->scsi_level;
	/*
	 * XXX do not save the inquiry, since it can change underneath us,
	 * save just vendor/model/rev.
	 *
	 * Rather than save it and have an ioctl that retrieves the saved
	 * value, have an ioctl that executes the same INQUIRY code used
	 * in scsi_probe_lun, let user level programs doing INQUIRY
	 * scanning run at their own risk, or supply a user level program
	 * that can correctly scan.
	 */
	sdev->inquiry_len = sdevscan->inquiry_len;
	sdev->inquiry = kmalloc(sdev->inquiry_len, GFP_ATOMIC);
	if (sdev->inquiry == NULL) {
		scsi_free_sdev(sdev);
		return SCSI_SCAN_NO_RESPONSE;
	}

	memcpy(sdev->inquiry, inq_result, sdev->inquiry_len);
	sdev->vendor = (char *) (sdev->inquiry + 8);
	sdev->model = (char *) (sdev->inquiry + 16);
	sdev->rev = (char *) (sdev->inquiry + 32);

	if (*bflags & BLIST_ISROM) {
		/*
		 * It would be better to modify sdev->type, and set
		 * sdev->removable, but then the print_inquiry() output
		 * would not show TYPE_ROM; if print_inquiry() is removed
		 * the issue goes away.
		 */
		inq_result[0] = TYPE_ROM;
		inq_result[1] |= 0x80;	/* removable */
	}

	switch (sdev->type = (inq_result[0] & 0x1f)) {
	case TYPE_TAPE:
	case TYPE_DISK:
	case TYPE_PRINTER:
	case TYPE_MOD:
	case TYPE_PROCESSOR:
	case TYPE_SCANNER:
	case TYPE_MEDIUM_CHANGER:
	case TYPE_ENCLOSURE:
	case TYPE_COMM:
		sdev->writeable = 1;
		break;
	case TYPE_WORM:
	case TYPE_ROM:
		sdev->writeable = 0;
		break;
	default:
		printk(KERN_INFO "scsi: unknown device type %d\n", sdev->type);
	}

	sdev->random = (sdev->type == TYPE_TAPE) ? 0 : 1;

	print_inquiry(inq_result);

	/*
	 * For a peripheral qualifier (PQ) value of 1 (001b), the SCSI
	 * spec says: The device server is capable of supporting the
	 * specified peripheral device type on this logical unit. However,
	 * the physical device is not currently connected to this logical
	 * unit.
	 *
	 * The above is vague, as it implies that we could treat 001 and
	 * 011 the same. Stay compatible with previous code, and create a
	 * Scsi_Device for a PQ of 1
	 *
	 * XXX Save the PQ field let the upper layers figure out if they
	 * want to attach or not to this device, do not set online FALSE;
	 * otherwise, offline devices still get an sd allocated, and they
	 * use up an sd slot.
	 */
	if (((inq_result[0] >> 5) & 7) == 1) {
		SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: peripheral"
				" qualifier of 1, device offlined\n"));
		sdev->online = FALSE;
	}

	sdev->removable = (0x80 & inq_result[1]) >> 7;
	sdev->lockable = sdev->removable;
	sdev->soft_reset = (inq_result[7] & 1) && ((inq_result[3] & 7) == 2);

	/*
	 * XXX maybe move the identifier and driverfs/devfs setup to a new
	 * function, and call them after this function is called.
	 *
	 * scsi_load_identifier is the only reason sreq is needed in this
	 * function.
	 */
	scsi_load_identifier(sdev, sreq);

	/*
	 * create driverfs files
	 */
	sprintf(sdev->sdev_driverfs_dev.bus_id,"%d:%d:%d:%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	sdev->sdev_driverfs_dev.parent = &sdev->host->host_driverfs_dev;
	sdev->sdev_driverfs_dev.bus = &scsi_driverfs_bus_type;
	device_register(&sdev->sdev_driverfs_dev);

	/*
	 * Create driverfs file entries
	 */
	device_create_file(&sdev->sdev_driverfs_dev, &dev_attr_type);

	sprintf(devname, "host%d/bus%d/target%d/lun%d",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	if (sdev->de)
		printk(KERN_WARNING "scsi devfs dir: \"%s\" already exists\n",
		       devname);
	else
		sdev->de = devfs_mk_dir(scsi_devfs_handle, devname, NULL);
	/*
	 * End driverfs/devfs code.
	 */

	if ((sdev->scsi_level >= SCSI_2) && (inq_result[7] & 2) &&
	    !(*bflags & BLIST_NOTQ))
		sdev->tagged_supported = 1;
	/*
	 * Some devices (Texel CD ROM drives) have handshaking problems
	 * when used with the Seagate controllers. borken is initialized
	 * to 1, and then set it to 0 here.
	 */
	if ((*bflags & BLIST_BORKEN) == 0)
		sdev->borken = 0;

	/*
	 * If we need to allow I/O to only one of the luns attached to
	 * this target id at a time, then we set this flag.
	 */
	if (*bflags & BLIST_SINGLELUN)
		sdev->single_lun = 1;

	/* if the device needs this changing, it may do so in the detect
	 * function */
	sdev->max_device_blocked = SCSI_DEFAULT_DEVICE_BLOCKED;

	for (sdt = scsi_devicelist; sdt; sdt = sdt->next)
		if (sdt->detect)
			sdev->attached += (*sdt->detect) (sdev);

	if (sdevnew != NULL)
		*sdevnew = sdev;

	return SCSI_SCAN_LUN_PRESENT;
}

/**
 * scsi_probe_and_add_lun - probe a LUN, if a LUN is found add it
 * @sdevscan:	probe the LUN corresponding to this Scsi_Device
 * @sdevnew:	store the value of any new Scsi_Device allocated
 * @bflagsp:	store bflags here if not NULL
 *
 * Description:
 *     Call scsi_probe_lun, if a LUN with an attached device is found,
 *     allocate and set it up by calling scsi_add_lun.
 *
 * Return:
 *     SCSI_SCAN_NO_RESPONSE: could not allocate or setup a Scsi_Device
 *     SCSI_SCAN_TARGET_PRESENT: target responded, but no device is
 *         attached at the LUN
 *     SCSI_SCAN_LUN_PRESENT: a new Scsi_Device was allocated and initialized
 **/
static int scsi_probe_and_add_lun(Scsi_Device *sdevscan, Scsi_Device **sdevnew,
				  int *bflagsp)
{
	Scsi_Device *sdev = NULL;
	Scsi_Request *sreq = NULL;
	unsigned char *scsi_result = NULL;
	int bflags;
	int res;

	/*
	 * Any command blocks allocated are fixed to use sdevscan->lun,
	 * so they must be allocated and released if sdevscan->lun
	 * changes.
	 *
	 * XXX optimize and don't call build/release commandblocks, instead
	 * modify the LUN value of the existing command block - this means
	 * the build/release calls would be moved to the alloc/free of
	 * sdevscan, and the modifying function would be called here.
	 *
	 * XXX maybe change scsi_release_commandblocks to not reset
	 * queue_depth to 0.
	 */
	sdevscan->queue_depth = 1;
	scsi_build_commandblocks(sdevscan);
	if (sdevscan->has_cmdblocks == 0)
		goto alloc_failed;

	sreq = scsi_allocate_request(sdevscan);
	if (sreq == NULL)
		goto alloc_failed;
	/*
	 * The sreq is for use only with sdevscan.
	 */

	scsi_result = kmalloc(256, GFP_ATOMIC |
			      (sdevscan->host->unchecked_isa_dma) ?
			      GFP_DMA : 0);
	if (scsi_result == NULL)
		goto alloc_failed;

	scsi_probe_lun(sreq, scsi_result, &bflags);
	if (sreq->sr_result)
		res = SCSI_SCAN_NO_RESPONSE;
	else {
		/*
		 * scsi_result contains valid SCSI INQUIRY data.
		 */
		if ((scsi_result[0] >> 5) == 3) {
			/*
			 * For a Peripheral qualifier 3 (011b), the SCSI
			 * spec says: The device server is not capable of
			 * supporting a physical device on this logical
			 * unit.
			 *
			 * For disks, this implies that there is no
			 * logical disk configured at sdev->lun, but there
			 * is a target id responding.
			 */
			SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO
					"scsi scan: peripheral qualifier of 3,"
					" no device added\n"));
			res = SCSI_SCAN_TARGET_PRESENT;
		} else {
			res = scsi_add_lun(sdevscan, &sdev, sreq, scsi_result,
					   &bflags);
			if (res == SCSI_SCAN_LUN_PRESENT) {
				BUG_ON(sdev == NULL);
				if ((bflags & BLIST_KEY) != 0) {
					sdev->lockable = 0;
					scsi_unlock_floptical(sreq,
							      scsi_result);
					/*
					 * scsi_result no longer contains
					 * the INQUIRY data.
					 */
				}
				/*
				 * "hardcoded" scans of a single LUN need
				 * to know the sdev just allocated.
				 */
				if (sdevnew != NULL)
					*sdevnew = sdev;
				if (bflagsp != NULL)
					*bflagsp = bflags;
			}
		}
	}
	kfree(scsi_result);
	scsi_release_request(sreq);
	scsi_release_commandblocks(sdevscan);
	return res;

alloc_failed:
	printk(ALLOC_FAILURE_MSG, __FUNCTION__);
	if (scsi_result != NULL)
		kfree(scsi_result);
	if (sreq != NULL)
		scsi_release_request(sreq);
	if (sdevscan->has_cmdblocks != 0)
		scsi_release_commandblocks(sdevscan);
	return SCSI_SCAN_NO_RESPONSE;
}

/**
 * scsi_sequential_lun_scan - sequentially scan a SCSI target
 * @sdevscan:	scan the host, channel, and id of this Scsi_Device
 * @bflags:	flags from device_list for LUN 0
 * @lun0_res:	result of scanning LUN 0
 *
 * Description:
 *     Generally, scan from LUN 1 (LUN 0 is assumed to already have been
 *     scanned) to some maximum lun until a LUN is found with no device
 *     attached. Use the bflags to figure out any oddities.
 *
 *     Modifies sdevscan->lun.
 **/
static void scsi_sequential_lun_scan(Scsi_Device *sdevscan, int bflags,
				     int lun0_res)
{
	struct Scsi_Host *shost = sdevscan->host;
	unsigned int sparse_lun;
	unsigned int max_dev_lun;

	SCSI_LOG_SCAN_BUS(3, printk(KERN_INFO "scsi scan: Sequential scan of"
			" host %d channel %d id %d\n", sdevscan->host->host_no,
			sdevscan->channel, sdevscan->id));

	max_dev_lun = min(max_scsi_luns, shost->max_lun);
	/*
	 * If this device is known to support sparse multiple units,
	 * override the other settings, and scan all of them. Normally,
	 * SCSI-3 devices should be scanned via the REPORT LUNS.
	 */
	if (bflags & BLIST_SPARSELUN) {
		max_dev_lun = shost->max_lun;
		sparse_lun = 1;
	} else
		sparse_lun = 0;

	/*
	 * If not sparse lun and no device attached at LUN 0 do not scan
	 * any further.
	 */
	if (!sparse_lun && (lun0_res != SCSI_SCAN_LUN_PRESENT))
		return;

	/*
	 * If less than SCSI_1_CSS, and no special lun scaning, stop
	 * scanning; this matches 2.4 behaviour, but could just be a bug
	 * (to continue scanning a SCSI_1_CSS device).
	 */
	if ((sdevscan->scsi_level < SCSI_1_CCS) &&
	    ((bflags & (BLIST_FORCELUN | BLIST_SPARSELUN | BLIST_MAX5LUN))
	     == 0))
		return;
	/*
	 * If this device is known to support multiple units, override
	 * the other settings, and scan all of them.
	 */
	if (bflags & BLIST_FORCELUN)
		max_dev_lun = shost->max_lun;
	/*
	 * REGAL CDC-4X: avoid hang after LUN 4
	 */
	if (bflags & BLIST_MAX5LUN)
		max_dev_lun = min(5U, max_dev_lun);
	/*
	 * Do not scan SCSI-2 or lower device past LUN 7, unless
	 * BLIST_LARGELUN.
	 */
	if ((sdevscan->scsi_level < SCSI_3) && !(bflags & BLIST_LARGELUN))
		max_dev_lun = min(8U, max_dev_lun);

	/*
	 * We have already scanned LUN 0, so start at LUN 1. Keep scanning
	 * until we reach the max, or no LUN is found and we are not
	 * sparse_lun.
	 */
	for (sdevscan->lun = 1; sdevscan->lun < max_dev_lun; ++sdevscan->lun)
		if ((scsi_probe_and_add_lun(sdevscan, NULL, NULL)
		     != SCSI_SCAN_LUN_PRESENT) && !sparse_lun)
			return;
}

#ifdef CONFIG_SCSI_REPORT_LUNS
/**
 * scsilun_to_int: convert a ScsiLun to an int
 * @scsilun:	ScsiLun to be converted.
 *
 * Description:
 *     Convert @scsilun from a ScsiLun to a four byte host byte-ordered
 *     integer, and return the result. The caller must check for
 *     truncation before using this function.
 *
 * Notes:
 *     The ScsiLun is assumed to be four levels, with each level
 *     effectively containing a SCSI byte-ordered (big endian) short; the
 *     addressing bits of each level are ignored (the highest two bits).
 *     For a description of the LUN format, post SCSI-3 see the SCSI
 *     Architecture Model, for SCSI-3 see the SCSI Controller Commands.
 *
 *     Given a ScsiLun of: 0a 04 0b 03 00 00 00 00, this function returns
 *     the integer: 0x0b030a04
 **/
static int scsilun_to_int(ScsiLun *scsilun)
{
	int i;
	unsigned int lun;

	lun = 0;
	for (i = 0; i < sizeof(lun); i += 2)
		lun = lun | (((scsilun->scsi_lun[i] << 8) |
			      scsilun->scsi_lun[i + 1]) << (i * 8));
	return lun;
}
#endif

/**
 * scsi_report_lun_scan - Scan using SCSI REPORT LUN results
 * @sdevscan:	scan the host, channel, and id of this Scsi_Device
 *
 * Description:
 *     If @sdevscan is for a SCSI-3 or up device, send a REPORT LUN
 *     command, and scan the resulting list of LUNs by calling
 *     scsi_probe_and_add_lun.
 *
 *     Modifies sdevscan->lun.
 *
 * Return:
 *     0: scan completed (or no memory, so further scanning is futile)
 *     1: no report lun scan, or not configured
 **/
static int scsi_report_lun_scan(Scsi_Device *sdevscan)
{
#ifdef CONFIG_SCSI_REPORT_LUNS

	char devname[64];
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	unsigned int length;
	unsigned int lun;
	unsigned int num_luns;
	unsigned int retries;
	ScsiLun *fcp_cur_lun, *lun_data;
	Scsi_Request *sreq;
	char *data;

	/*
	 * Only support SCSI-3 and up devices.
	 */
	if (sdevscan->scsi_level < SCSI_3)
		return 1;

	sdevscan->queue_depth = 1;
	scsi_build_commandblocks(sdevscan);
	if (sdevscan->has_cmdblocks == 0) {
		printk(ALLOC_FAILURE_MSG, __FUNCTION__);
		/*
		 * We are out of memory, don't try scanning any further.
		 */
		return 0;
	}
	sreq = scsi_allocate_request(sdevscan);

	sprintf(devname, "host %d channel %d id %d", sdevscan->host->host_no,
		sdevscan->channel, sdevscan->id);
	/*
	 * Allocate enough to hold the header (the same size as one ScsiLun)
	 * plus the max number of luns we are requesting.
	 *
	 * Reallocating and trying again (with the exact amount we need)
	 * would be nice, but then we need to somehow limit the size
	 * allocated based on the available memory and the limits of
	 * kmalloc - we don't want a kmalloc() failure of a huge value to
	 * prevent us from finding any LUNs on this target.
	 */
	length = (max_scsi_report_luns + 1) * sizeof(ScsiLun);
	lun_data = (ScsiLun *) kmalloc(length, GFP_ATOMIC |
					   (sdevscan->host->unchecked_isa_dma ?
					    GFP_DMA : 0));
	if (lun_data == NULL) {
		printk(ALLOC_FAILURE_MSG, __FUNCTION__);
		scsi_release_commandblocks(sdevscan);
		/*
		 * We are out of memory, don't try scanning any further.
		 */
		return 0;
	}

	scsi_cmd[0] = REPORT_LUNS;
	/*
	 * bytes 1 - 5: reserved, set to zero.
	 */
	memset(&scsi_cmd[1], 0, 5);
	/*
	 * bytes 6 - 9: length of the command.
	 */
	scsi_cmd[6] = (unsigned char) (length >> 24) & 0xff;
	scsi_cmd[7] = (unsigned char) (length >> 16) & 0xff;
	scsi_cmd[8] = (unsigned char) (length >> 8) & 0xff;
	scsi_cmd[9] = (unsigned char) length & 0xff;

	scsi_cmd[10] = 0;	/* reserved */
	scsi_cmd[11] = 0;	/* control */
	sreq->sr_cmd_len = 0;
	sreq->sr_data_direction = SCSI_DATA_READ;

	/*
	 * We can get a UNIT ATTENTION, for example a power on/reset, so
	 * retry a few times (like sd.c does for TEST UNIT READY).
	 * Experience shows some combinations of adapter/devices get at
	 * least two power on/resets.
	 *
	 * Illegal requests (for devices that do not support REPORT LUNS)
	 * should come through as a check condition, and will not generate
	 * a retry.
	 */
	retries = 0;
	while (retries++ < 3) {
		SCSI_LOG_SCAN_BUS(3, printk (KERN_INFO "scsi scan: Sending"
				" REPORT LUNS to %s (try %d)\n", devname,
				retries));
		scsi_wait_req(sreq, (void *) scsi_cmd, (void *) lun_data,
			      length, SCSI_TIMEOUT + 4 * HZ, 3);
		SCSI_LOG_SCAN_BUS(3, printk (KERN_INFO "scsi scan: REPORT LUNS"
				" %s (try %d) result 0x%x\n", sreq->sr_result
				?  "failed" : "successful", retries,
				sreq->sr_result));
		if (sreq->sr_result == 0
		    || sreq->sr_sense_buffer[2] != UNIT_ATTENTION)
			break;
	}
	scsi_release_commandblocks(sdevscan);

	if (sreq->sr_result) {
		/*
		 * The device probably does not support a REPORT LUN command
		 */
		kfree((char *) lun_data);
		scsi_release_request(sreq);
		return 1;
	}
	scsi_release_request(sreq);

	/*
	 * Get the length from the first four bytes of lun_data.
	 */
	data = (char *) lun_data->scsi_lun;
	length = ((data[0] << 24) | (data[1] << 16) |
		  (data[2] << 8) | (data[3] << 0));
	if ((length / sizeof(ScsiLun)) > max_scsi_report_luns) {
		printk(KERN_WARNING "scsi: On %s only %d (max_scsi_report_luns)"
		       " of %d luns reported, try increasing"
		       " max_scsi_report_luns.\n", devname,
		       max_scsi_report_luns, length / sizeof(ScsiLun));
		num_luns = max_scsi_report_luns;
	} else
		num_luns = (length / sizeof(ScsiLun));

	SCSI_LOG_SCAN_BUS(3, printk (KERN_INFO "scsi scan: REPORT LUN scan of"
			" host %d channel %d id %d\n", sdevscan->host->host_no,
			sdevscan->channel, sdevscan->id));
	/*
	 * Scan the luns in lun_data. The entry at offset 0 is really
	 * the header, so start at 1 and go up to and including num_luns.
	 */
	for (fcp_cur_lun = &lun_data[1];
	     fcp_cur_lun <= &lun_data[num_luns]; fcp_cur_lun++) {
		lun = scsilun_to_int(fcp_cur_lun);
		/*
		 * Check if the unused part of fcp_cur_lun is non-zero,
		 * and so does not fit in lun.
		 */
		if (memcmp(&fcp_cur_lun->scsi_lun[sizeof(lun)],
			   "\0\0\0\0", 4) != 0) {
			int i;

			/*
			 * Output an error displaying the LUN in byte order,
			 * this differs from what linux would print for the
			 * integer LUN value.
			 */
			printk(KERN_WARNING "scsi: %s lun 0x", devname);
			data = (char *) fcp_cur_lun->scsi_lun;
			for (i = 0; i < sizeof(ScsiLun); i++)
				printk("%02x", data[i]);
			printk(" has a LUN larger than currently supported.\n");
		} else if (lun == 0) {
			/*
			 * LUN 0 has already been scanned.
			 */
		} else if (lun > sdevscan->host->max_lun) {
			printk(KERN_WARNING "scsi: %s lun%d has a LUN larger"
			       " than allowed by the host adapter\n",
			       devname, lun);
		} else {
			int res;

			sdevscan->lun = lun;
			res = scsi_probe_and_add_lun(sdevscan, NULL, NULL);
			if (res == SCSI_SCAN_NO_RESPONSE) {
				/*
				 * Got some results, but now none, abort.
				 */
				printk(KERN_ERR "scsi: Unexpected response"
				       " from %s lun %d while scanning, scan"
				       " aborted\n", devname, sdevscan->lun);
				break;
			}
		}
	}

	kfree((char *) lun_data);
	return 0;

#else
	return 1;
#endif	/* CONFIG_SCSI_REPORT_LUNS */

}

/**
 * scsi_scan_target - scan a target id, possibly including all LUNs on the
 *     target.
 * @sdevsca:	Scsi_Device handle for scanning
 * @shost:	host to scan
 * @channel:	channel to scan
 * @id:		target id to scan
 *
 * Description:
 *     Scan the target id on @shost, @channel, and @id. Scan at least LUN
 *     0, and possibly all LUNs on the target id.
 *
 *     Use the pre-allocated @sdevscan as a handle for the scanning. This
 *     function sets sdevscan->host, sdevscan->id and sdevscan->lun; the
 *     scanning functions modify sdevscan->lun.
 *
 *     First try a REPORT LUN scan, if that does not scan the target, do a
 *     sequential scan of LUNs on the target id.
 **/
static void scsi_scan_target(Scsi_Device *sdevscan, struct Scsi_Host *shost,
			     unsigned int channel, unsigned int id)
{
	int bflags;
	int res;

	if (shost->this_id == id)
		/*
		 * Don't scan the host adapter
		 */
		return;

	sdevscan->host = shost;
	sdevscan->id = id;
	sdevscan->channel = channel;
	/*
	 * Scan LUN 0, if there is some response, scan further. Ideally, we
	 * would not configure LUN 0 until all LUNs are scanned.
	 *
	 * The scsi_level is set (in scsi_probe_lun) if a target responds.
	 */
	sdevscan->lun = 0;
	res = scsi_probe_and_add_lun(sdevscan, NULL, &bflags);
	if (res != SCSI_SCAN_NO_RESPONSE) {
		/*
		 * Some scsi devices cannot properly handle a lun != 0.
		 * BLIST_NOLUN also prevents a REPORT LUN from being sent.
		 * Any multi-lun SCSI-3 device that hangs because of a
		 * REPORT LUN command is seriously broken.
		 */
		if (!(bflags & BLIST_NOLUN))
			/*
			 * Ending the scan here if max_scsi_luns == 1
			 * breaks scanning of SPARSE, FORCE, MAX5 LUN
			 * devices, and the report lun scan.
			 */
			if (scsi_report_lun_scan(sdevscan) != 0)
				/*
				 * The REPORT LUN did not scan the target,
				 * do a sequential scan.
				 */
				scsi_sequential_lun_scan(sdevscan, bflags, res);
	}
}

/**
 * scsi_scan_selected_lun - probe and add one LUN
 *
 * Description:
 *     Probe a single LUN on @shost, @channel, @id and @lun. If the LUN is
 *     found, set the queue depth, allocate command blocks, and call
 *     init/attach/finish of the upper level (sd, sg, etc.) drivers.
 **/
static void scsi_scan_selected_lun(struct Scsi_Host *shost, uint channel,
				   uint id, uint lun)
{
	Scsi_Device *sdevscan, *sdev = NULL;
	struct Scsi_Device_Template *sdt;
	int res;

	if ((channel > shost->max_channel) || (id >= shost->max_id) ||
	    (lun >= shost->max_lun))
		return;

	sdevscan = scsi_alloc_sdev(shost, channel, id, lun);
	if (sdevscan == NULL)
		return;

	sdevscan->scsi_level = scsi_find_scsi_level(channel, id, shost);
	res = scsi_probe_and_add_lun(sdevscan, &sdev, NULL);
	scsi_free_sdev(sdevscan);
	if (res == SCSI_SCAN_LUN_PRESENT) {
		BUG_ON(sdev == NULL);
		/*
		 * FIXME calling select_queue_depths is wrong for adapters
		 * that modify queue depths of all scsi devices - the
		 * adapter might change a queue depth (not for this sdev),
		 * but the mid-layer will not change the queue depth. This
		 * does not cause an oops, but queue_depth will not match
		 * the actual queue depth used.
		 *
		 * Perhaps use a default queue depth, and allow them to be
		 * modified at boot/insmod time, and/or via sysctl/ioctl/proc;
		 * plus have dynamic queue depth adjustment like the
		 * aic7xxx driver.
		 */
		if (shost->select_queue_depths != NULL)
			(shost->select_queue_depths) (shost, shost->host_queue);

		for (sdt = scsi_devicelist; sdt; sdt = sdt->next)
			if (sdt->init && sdt->dev_noticed)
				(*sdt->init) ();

		for (sdt = scsi_devicelist; sdt; sdt = sdt->next)
			if (sdt->attach) {
				(*sdt->attach) (sdev);
				if (sdev->attached) {
					scsi_build_commandblocks(sdev);
					if (sdev->has_cmdblocks == 0)
						printk(ALLOC_FAILURE_MSG,
						       __FUNCTION__);
				}
			}

		for (sdt = scsi_devicelist; sdt; sdt = sdt->next)
			if (sdt->finish && sdt->nr_dev)
				(*sdt->finish) ();

	}
}

/**
 * scan_scsis - scan the given adapter, or scan a single LUN
 * @shost:	adapter to scan
 * @hardcoded:	1 if a single channel/id/lun should be scanned, else 0
 * @hchannel:	channel to scan for hardcoded case
 * @hid:	target id to scan for hardcoded case
 * @hlun:	lun to scan for hardcoded case
 *
 * Description:
 *     If @hardcoded is 1, call scsi_scan_selected_lun to scan a single
 *     LUN; else, iterate and call scsi_scan_target to scan all possible
 *     target id's on all possible channels.
 **/
void scan_scsis(struct Scsi_Host *shost, uint hardcoded, uint hchannel,
		uint hid, uint hlun)
{
	if (hardcoded == 1) {
		/*
		 * XXX Overload hchannel/hid/hlun to figure out what to
		 * scan, and use the standard scanning code rather than
		 * this function - that way, an entire bus (or fabric), or
		 * target id can be scanned. There are problems with queue
		 * depth and the init/attach/finish that must be resolved
		 * before (re-)scanning can handle finding more than one new
		 * LUN.
		 *
		 * For example, set hchannel 0 and hid to 5, and hlun to -1
		 * in order to scan all LUNs on channel 0, target id 5.
		 */
		scsi_scan_selected_lun(shost, hchannel, hid, hlun);
	} else {
		Scsi_Device *sdevscan;
		uint channel;
		unsigned int id, order_id;

		/*
		 * The blk layer queue allocation is a bit expensive to
		 * repeat for each channel and id - for FCP max_id is near
		 * 255: each call to scsi_alloc_sdev() implies a call to
		 * blk_init_queue, and then blk_init_free_list, where 2 *
		 * queue_nr_requests requests are allocated. Don't do so
		 * here for scsi_scan_selected_lun, since we end up
		 * calling select_queue_depths with an extra Scsi_Device
		 * on the host_queue list.
		 */
		sdevscan = scsi_alloc_sdev(shost, 0, 0, 0);
		if (sdevscan == NULL)
			return;
		/*
		 * The sdevscan host, channel, id and lun are filled in as
		 * needed to scan.
		 */
		for (channel = 0; channel <= shost->max_channel; channel++) {
			/*
			 * XXX adapter drivers when possible (FCP, iSCSI)
			 * could modify max_id to match the current max,
			 * not the absolute max.
			 *
			 * XXX add a shost id iterator, so for example,
			 * the FC ID can be the same as a target id
			 * without a huge overhead of sparse id's.
			 */
			for (id = 0; id < shost->max_id; ++id) {
				if (shost->reverse_ordering)
					/*
					 * Scan from high to low id.
					 */
					order_id = shost->max_id - id - 1;
				else
					order_id = id;
				scsi_scan_target(sdevscan, shost, channel,
						 order_id);
			}
		}
		scsi_free_sdev(sdevscan);
	}
}
