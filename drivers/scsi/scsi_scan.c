/*
 *  scsi_scan.c Copyright (C) 2000 Eric Youngdale
 *
 *  Bus scan logic.
 *
 *  This used to live in scsi.c, but that file was just a laundry basket
 *  full of misc stuff.  This got separated out in order to make things
 *  clearer.
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
 * Flags for irregular SCSI devices that need special treatment
 */
#define BLIST_NOLUN		0x001	/* Don't scan for LUNs */
#define BLIST_FORCELUN		0x002	/* Known to have LUNs, force sanning */
#define BLIST_BORKEN		0x004	/* Flag for broken handshaking */
#define BLIST_KEY		0x008	/* Needs to be unlocked by special command */
#define BLIST_SINGLELUN		0x010	/* LUNs should better not be used in parallel */
#define BLIST_NOTQ		0x020	/* Buggy Tagged Command Queuing */
#define BLIST_SPARSELUN		0x040	/* Non consecutive LUN numbering */
#define BLIST_MAX5LUN		0x080	/* Avoid LUNS >= 5 */
#define BLIST_ISDISK		0x100	/* Treat as (removable) disk */
#define BLIST_ISROM		0x200	/* Treat as (removable) CD-ROM */
#define BLIST_LARGELUN		0x400	/* LUNs larger than 7 despite reporting as SCSI 2 */
#define BLIST_INQUIRY_36	0x800	/* override additional length field */
#define BLIST_INQUIRY_58	0x1000	/* ... for broken inquiry responses */

/*
 * scan_scsis_single() return values.
 */
#define SCSI_SCAN_NO_RESPONSE      0
#define SCSI_SCAN_DEVICE_PRESENT   1
#define SCSI_SCAN_DEVICE_ADDED     2

static void print_inquiry(unsigned char *data);
static int scan_scsis_single(unsigned int channel, unsigned int dev,
		unsigned int lun, int scsi_level, Scsi_Device ** SDpnt2,
		struct Scsi_Host *shpnt, char *scsi_result);
static void scan_scsis_target(unsigned int channel, unsigned int dev,
		Scsi_Device ** SDpnt2, struct Scsi_Host *shpnt,
		char *scsi_result);
static int find_lun0_scsi_level(unsigned int channel, unsigned int dev,
				struct Scsi_Host *shpnt);
static void scsi_load_identifier(Scsi_Device *SDpnt, Scsi_Request * SRpnt);

struct dev_info {
	const char *vendor;
	const char *model;
	const char *revision;	/* Latest revision known to be bad.  Not used yet */
	unsigned flags;
};

/*
 * This is what was previously known as the blacklist.  The concept
 * has been expanded so that we can specify other types of things we
 * need to be aware of.
 */
static struct dev_info device_list[] =
{
/* The following devices are known not to tolerate a lun != 0 scan for
 * one reason or another.  Some will respond to all luns, others will
 * lock up.
 */
	{"Aashima", "IMAGERY 2400SP", "1.03", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"CHINON", "CD-ROM CDS-431", "H42", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"CHINON", "CD-ROM CDS-535", "Q14", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"DENON", "DRD-25X", "V", BLIST_NOLUN},			/* Locks up if probed for lun != 0 */
	{"HITACHI", "DK312C", "CM81", BLIST_NOLUN},		/* Responds to all lun - dtg */
	{"HITACHI", "DK314C", "CR21", BLIST_NOLUN},		/* responds to all lun */
	{"IMS", "CDD521/10", "2.06", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "XT-3280", "PR02", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "XT-4380S", "B3C", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "MXT-1240S", "I1.2", BLIST_NOLUN},		/* Locks up when LUN>0 polled */
	{"MAXTOR", "XT-4170S", "B5A", BLIST_NOLUN},		/* Locks-up sometimes when LUN>0 polled. */
	{"MAXTOR", "XT-8760S", "B7B", BLIST_NOLUN},		/* guess what? */
	{"MEDIAVIS", "RENO CD-ROMX2A", "2.03", BLIST_NOLUN},	/* Responds to all lun */
	{"NEC", "CD-ROM DRIVE:841", "1.0", BLIST_NOLUN},	/* Locks-up when LUN>0 polled. */
	{"PHILIPS", "PCA80SC", "V4-2", BLIST_NOLUN},		/* Responds to all lun */
	{"RODIME", "RO3000S", "2.33", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"SANYO", "CRD-250S", "1.20", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for aha152x controller, which causes
								 * SCSI code to reset bus.*/
	{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for aha152x controller, which causes
								 * SCSI code to reset bus.*/
	{"SEAGATE", "ST296", "921", BLIST_NOLUN},		/* Responds to all lun */
	{"SEAGATE", "ST1581", "6538", BLIST_NOLUN},		/* Responds to all lun */
	{"SONY", "CD-ROM CDU-541", "4.3d", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-55S", "1.0i", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-561", "1.7x", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-8012", "*", BLIST_NOLUN},
	{"TANDBERG", "TDC 3600", "U07", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"TEAC", "CD-R55S", "1.0H", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"TEAC", "CD-ROM", "1.06", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for seagate controller, which causes
								 * SCSI code to reset bus.*/
	{"TEAC", "MT-2ST/45S2-27", "RV M", BLIST_NOLUN},	/* Responds to all lun */
	{"TEXEL", "CD-ROM", "1.06", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for seagate controller, which causes
								 * SCSI code to reset bus.*/
	{"QUANTUM", "LPS525S", "3110", BLIST_NOLUN},		/* Locks sometimes if polled for lun != 0 */
	{"QUANTUM", "PD1225S", "3110", BLIST_NOLUN},		/* Locks sometimes if polled for lun != 0 */
	{"QUANTUM", "FIREBALL ST4.3S", "0F0C", BLIST_NOLUN},	/* Locks up when polled for lun != 0 */
	{"MEDIAVIS", "CDR-H93MV", "1.31", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"SANKYO", "CP525", "6.64", BLIST_NOLUN},		/* causes failed REQ SENSE, extra reset */
	{"HP", "C1750A", "3226", BLIST_NOLUN},			/* scanjet iic */
	{"HP", "C1790A", "", BLIST_NOLUN},			/* scanjet iip */
	{"HP", "C2500A", "", BLIST_NOLUN},			/* scanjet iicx */
	{"YAMAHA", "CDR100", "1.00", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"YAMAHA", "CDR102", "1.00", BLIST_NOLUN},		/* Locks up if polled for lun != 0 extra reset */
	{"YAMAHA", "CRW8424S", "1.0", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"YAMAHA", "CRW6416S", "1.0c", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"MITSUMI", "CD-R CR-2201CS", "6119", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"RELISYS", "Scorpio", "*", BLIST_NOLUN},		/* responds to all LUN */
	{"MICROTEK", "ScanMaker II", "5.61", BLIST_NOLUN},	/* responds to all LUN */

/*
 * Other types of devices that have special flags.
 */
	{"SONY", "CD-ROM CDU-8001", "*", BLIST_BORKEN},
	{"TEXEL", "CD-ROM", "1.06", BLIST_BORKEN},
	{"IOMEGA", "Io20S         *F", "*", BLIST_KEY},
	{"INSITE", "Floptical   F*8I", "*", BLIST_KEY},
	{"INSITE", "I325VM", "*", BLIST_KEY},
	{"LASOUND","CDX7405","3.10", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"MICROP", "4110", "*", BLIST_NOTQ},			/* Buggy Tagged Queuing */
	{"NRC", "MBR-7", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7.4", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"REGAL", "CDC-4X", "*", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-4.8S", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-5.16S", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-600", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-602X", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-604X", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"EMULEX", "MD21/S2     ESDI", "*", BLIST_SINGLELUN},
	{"CANON", "IPUBJD", "*", BLIST_SPARSELUN},
	{"nCipher", "Fastness Crypto", "*", BLIST_FORCELUN},
	{"DEC","HSG80","*", BLIST_FORCELUN},
	{"COMPAQ","LOGICAL VOLUME","*", BLIST_FORCELUN},
	{"COMPAQ","CR3500","*", BLIST_FORCELUN},
	{"NEC", "PD-1 ODX654P", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"MATSHITA", "PD-1", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"iomega", "jaz 1GB", "J.86", BLIST_NOTQ | BLIST_NOLUN},
 	{"TOSHIBA","CDROM","*", BLIST_ISROM},
 	{"TOSHIBA","CD-ROM","*", BLIST_ISROM},
	{"MegaRAID", "LD", "*", BLIST_FORCELUN},
	{"DGC",  "RAID",      "*", BLIST_SPARSELUN}, // Dell PV 650F (tgt @ LUN 0)
	{"DGC",  "DISK",      "*", BLIST_SPARSELUN}, // Dell PV 650F (no tgt @ LUN 0) 
	{"DELL", "PV660F",   "*", BLIST_SPARSELUN},
	{"DELL", "PV660F   PSEUDO",   "*", BLIST_SPARSELUN},
	{"DELL", "PSEUDO DEVICE .",   "*", BLIST_SPARSELUN}, // Dell PV 530F
	{"DELL", "PV530F",    "*", BLIST_SPARSELUN}, // Dell PV 530F
	{"EMC", "SYMMETRIX", "*", BLIST_SPARSELUN | BLIST_LARGELUN | BLIST_FORCELUN},
	{"HP", "A6189A", "*", BLIST_SPARSELUN |  BLIST_LARGELUN}, // HP VA7400, by Alar Aun
	{"CMD", "CRA-7280", "*", BLIST_SPARSELUN},   // CMD RAID Controller
	{"CNSI", "G7324", "*", BLIST_SPARSELUN},     // Chaparral G7324 RAID
	{"CNSi", "G8324", "*", BLIST_SPARSELUN},     // Chaparral G8324 RAID
	{"Zzyzx", "RocketStor 500S", "*", BLIST_SPARSELUN},
	{"Zzyzx", "RocketStor 2000", "*", BLIST_SPARSELUN},
	{"SONY", "TSL",       "*", BLIST_FORCELUN},  // DDS3 & DDS4 autoloaders
	{"DELL", "PERCRAID", "*", BLIST_FORCELUN},
	{"HP", "NetRAID-4M", "*", BLIST_FORCELUN},
	{"ADAPTEC", "AACRAID", "*", BLIST_FORCELUN},
	{"ADAPTEC", "Adaptec 5400S", "*", BLIST_FORCELUN},
	{"COMPAQ", "MSA1000", "*", BLIST_FORCELUN},
	{"HP", "C1557A", "*", BLIST_FORCELUN},
	{"IBM", "AuSaV1S2", "*", BLIST_FORCELUN},

	/*
	 * Must be at end of list...
	 */
	{NULL, NULL, NULL}
};

static char * scsi_null_device_strs = "nullnullnullnull";

#define MAX_SCSI_LUNS 0xFFFFFFFF

#ifdef CONFIG_SCSI_MULTI_LUN
static unsigned int max_scsi_luns = MAX_SCSI_LUNS;
#else
static unsigned int max_scsi_luns = 1;
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
#endif

#ifdef MODULE

MODULE_PARM(max_scsi_luns, "i");
MODULE_PARM_DESC(max_scsi_luns, "last scsi LUN (should be between 1 and 2^32-1)");

#ifdef CONFIG_SCSI_REPORT_LUNS
MODULE_PARM(max_scsi_report_luns, "i");
MODULE_PARM_DESC(max_scsi_report_luns, "REPORT LUNS maximum number of LUNS received (should be between 1 and 16384)");
#endif

#else

static int __init scsi_luns_setup(char *str)
{
	unsigned int tmp;

	if (get_option(&str, &tmp) == 1) {
		max_scsi_luns = tmp;
		return 1;
	} else {
		printk("scsi_luns_setup : usage max_scsi_luns=n "
		       "(n should be between 1 and 2^32-1)\n");
		return 0;
	}
}

__setup("max_scsi_luns=", scsi_luns_setup);

#ifdef CONFIG_SCSI_REPORT_LUNS
static int __init max_scsi_report_luns_setup(char *str)
{
	unsigned int tmp;

	if (get_option(&str, &tmp) == 1) {
		max_scsi_report_luns = tmp;
		return 1;
	} else {
		printk("scsi_report_luns_setup : usage max_scsi_report_luns=n "
		       "(n should be between 1 and 16384)\n");
		return 0;
	}
}

__setup("max_scsi_report_luns=", max_scsi_report_luns_setup);
#endif /* CONFIG_SCSI_REPORT_LUNS */

#endif

#ifdef CONFIG_SCSI_REPORT_LUNS
/*
 * Function:    scsilun_to_int
 *
 * Purpose:     Convert ScsiLun (8 byte LUN) to an int.
 *
 * Arguments:   scsilun_pnt - pointer to a ScsiLun to be converted
 *
 * Lock status: None
 *
 * Returns:     cpu ordered integer containing the truncated LUN value
 *
 * Notes:       The ScsiLun is assumed to be four levels, with each level
 * 		effectively containing a SCSI byte-ordered (big endidan)
 * 		short; the addressing bits of each level are ignored (the
 * 		highest two bits). For a description of the LUN format, post
 * 		SCSI-3 see the SCSI Architecture Model, for SCSI-3 see the
 * 		SCSI Controller Commands.
 *
 * 		Given a ScsiLun of: 0a 04 0b 03 00 00 00 00, this function
 * 		returns the integer: 0x0b030a04
 */
static int scsilun_to_int(ScsiLun *scsilun_pnt) 
{
	int i;
	unsigned int lun;

	lun = 0;
	for (i = 0; i < sizeof(lun); i += 2)
		lun = lun | (((scsilun_pnt->scsi_lun[i] << 8) |
			scsilun_pnt->scsi_lun[i + 1]) << (i * 8));
	return lun;
}
#endif

/* Driverfs file content handlers */
static ssize_t scsi_device_type_read(struct device *driverfs_dev, char *page, 
	size_t count, loff_t off)
{
	struct scsi_device *SDpnt = to_scsi_device(driverfs_dev);

	if ((SDpnt->type <= MAX_SCSI_DEVICE_CODE) && 
		(scsi_device_types[(int)SDpnt->type] != NULL))
		return off ? 0 : 
			sprintf(page, "%s\n", 
				scsi_device_types[(int)SDpnt->type]);
	else
		return off ? 0 : sprintf(page, "UNKNOWN\n");

	return 0;
}

static struct driver_file_entry scsi_device_type_file = {
	name: "type",
	mode: S_IRUGO,
	show: scsi_device_type_read,
};
/* end content handlers */

static void print_inquiry(unsigned char *data)
{
	int i;

	printk("  Vendor: ");
	for (i = 8; i < 16; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("  Model: ");
	for (i = 16; i < 32; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("  Rev: ");
	for (i = 32; i < 36; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("\n");

	i = data[0] & 0x1f;

	printk("  Type:   %s ",
	       i < MAX_SCSI_DEVICE_CODE ? scsi_device_types[i] : "Unknown          ");
	printk("                 ANSI SCSI revision: %02x", data[2] & 0x07);
	if ((data[2] & 0x07) == 1 && (data[3] & 0x0f) == 1)
		printk(" CCS\n");
	else
		printk("\n");
}

static int get_device_flags(unsigned char *vendor_pnt, unsigned char *model_pnt)
{
	int i = 0;
	for (i = 0; 1; i++) {
		if (device_list[i].vendor == NULL)
			return 0;
		while (*vendor_pnt && *vendor_pnt == ' ')
			vendor_pnt++;
		if (memcmp(device_list[i].vendor, vendor_pnt,
			   strlen(device_list[i].vendor)))
			continue;
		while (*model_pnt && *model_pnt == ' ')
			model_pnt++;
		if (memcmp(device_list[i].model, model_pnt,
			   strlen(device_list[i].model)))
			continue;
		return device_list[i].flags;
	}
	return 0;
}

/*
 *  Detecting SCSI devices :
 *  We scan all present host adapter's busses,  from ID 0 to ID (max_id).
 *  We use the INQUIRY command, determine device type, and pass the ID /
 *  lun address of all sequential devices to the tape driver, all random
 *  devices to the disk driver.
 */
void scan_scsis(struct Scsi_Host *shpnt,
		       uint hardcoded,
		       uint hchannel,
		       uint hid,
		       uint hlun)
{
	uint channel;
	unsigned int dev;
	unsigned int lun;
	unsigned char *scsi_result;
	unsigned char scsi_result0[256];
	Scsi_Device *SDpnt;
	Scsi_Device *SDtail;

	scsi_result = NULL;

	SDpnt = (Scsi_Device *) kmalloc(sizeof(Scsi_Device),
					GFP_ATOMIC);
	if (SDpnt) {
		memset(SDpnt, 0, sizeof(Scsi_Device));
		SDpnt->vendor = scsi_null_device_strs;
		SDpnt->model = scsi_null_device_strs;
		SDpnt->rev = scsi_null_device_strs;
		/*
		 * Register the queue for the device.  All I/O requests will
		 * come in through here.  We also need to register a pointer to
		 * ourselves, since the queue handler won't know what device
		 * the queue actually represents.   We could look it up, but it
		 * is pointless work.
		 */
		scsi_initialize_queue(SDpnt, shpnt);
		SDpnt->request_queue.queuedata = (void *) SDpnt;
		/* Make sure we have something that is valid for DMA purposes */
		scsi_result = ((!shpnt->unchecked_isa_dma)
			       ? &scsi_result0[0] : kmalloc(512, GFP_DMA));
	}

	if (scsi_result == NULL) {
		printk("Unable to obtain scsi_result buffer\n");
		goto leave;
	}
	/*
	 * We must chain ourself in the host_queue, so commands can time out 
	 */
	SDpnt->queue_depth = 1;
	SDpnt->host = shpnt;
	SDpnt->online = TRUE;

	scsi_initialize_merge_fn(SDpnt);

        /*
         * Initialize the object that we will use to wait for command blocks.
         */
	init_waitqueue_head(&SDpnt->scpnt_wait);

	/*
	 * Next, hook the device to the host in question.
	 */
	SDpnt->prev = NULL;
	SDpnt->next = NULL;
	if (shpnt->host_queue != NULL) {
		SDtail = shpnt->host_queue;
		while (SDtail->next != NULL)
			SDtail = SDtail->next;

		SDtail->next = SDpnt;
		SDpnt->prev = SDtail;
	} else {
		shpnt->host_queue = SDpnt;
	}

	/*
	 * We need to increment the counter for this one device so we can track
	 * when things are quiet.
	 */
	if (hardcoded == 1) {
		Scsi_Device *oldSDpnt = SDpnt;
		struct Scsi_Device_Template *sdtpnt;
		unsigned int lun0_sl;

		channel = hchannel;
		if (channel > shpnt->max_channel)
			goto leave;
		dev = hid;
		if (dev >= shpnt->max_id)
			goto leave;
		lun = hlun;
		if (lun >= shpnt->max_lun)
			goto leave;
		if ((0 == lun) || (lun > 7))
			lun0_sl = SCSI_3; /* actually don't care for 0 == lun */
		else
			lun0_sl = find_lun0_scsi_level(channel, dev, shpnt);
		scan_scsis_single(channel, dev, lun, lun0_sl, &SDpnt, shpnt,
				  scsi_result);
		if (SDpnt != oldSDpnt) {

			/* it could happen the blockdevice hasn't yet been inited */
			/* queue_depth() moved from scsi_proc_info() so that
			   it is called before scsi_build_commandblocks() */
			if (shpnt->select_queue_depths != NULL)
				(shpnt->select_queue_depths)(shpnt,
							     shpnt->host_queue);

			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
				if (sdtpnt->init && sdtpnt->dev_noticed)
					(*sdtpnt->init) ();

			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next) {
				if (sdtpnt->attach) {
					(*sdtpnt->attach) (oldSDpnt);
					if (oldSDpnt->attached) {
						scsi_build_commandblocks(oldSDpnt);
						if (0 == oldSDpnt->has_cmdblocks) {
							printk("scan_scsis: DANGER, no command blocks\n");
							/* What to do now ?? */
						}
					}
				}
			}
			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next) {
				if (sdtpnt->finish && sdtpnt->nr_dev) {
					(*sdtpnt->finish) ();
				}
			}
		}
	} else {
		/* Actual LUN. PC ordering is 0->n IBM/spec ordering is n->0 */
		int order_dev;

		for (channel = 0; channel <= shpnt->max_channel; channel++) {
			for (dev = 0; dev < shpnt->max_id; ++dev) {
				if (shpnt->reverse_ordering)
					/* Shift to scanning 15,14,13... or 7,6,5,4, */
					order_dev = shpnt->max_id - dev - 1;
				else
					order_dev = dev;

				if (shpnt->this_id != order_dev) {
					scan_scsis_target(channel, order_dev,
						&SDpnt, shpnt, scsi_result);
				}
			}
		}
	}			/* if/else hardcoded */

      leave:

	{			/* Unchain SRpnt from host_queue */
		Scsi_Device *prev, *next;
		Scsi_Device *dqptr;

		for (dqptr = shpnt->host_queue; dqptr != SDpnt; dqptr = dqptr->next)
			continue;
		if (dqptr) {
			prev = dqptr->prev;
			next = dqptr->next;
			if (prev)
				prev->next = next;
			else
				shpnt->host_queue = next;
			if (next)
				next->prev = prev;
		}
	}

	/* Last device block does not exist.  Free memory. */
	if (SDpnt != NULL) {
		blk_cleanup_queue(&SDpnt->request_queue);
		if (SDpnt->inquiry)
			kfree(SDpnt->inquiry);
		kfree((char *) SDpnt);
	}

	/* If we allocated a buffer so we could do DMA, free it now */
	if (scsi_result != &scsi_result0[0] && scsi_result != NULL) {
		kfree(scsi_result);
	} {
		Scsi_Device *sdev;
		Scsi_Cmnd *scmd;

		SCSI_LOG_SCAN_BUS(4, printk("Host status for host %p:\n", shpnt));
		for (sdev = shpnt->host_queue; sdev; sdev = sdev->next) {
			SCSI_LOG_SCAN_BUS(4, printk("Device %d %p: ", sdev->id, sdev));
			for (scmd = sdev->device_queue; scmd; scmd = scmd->next) {
				SCSI_LOG_SCAN_BUS(4, printk("%p ", scmd));
			}
			SCSI_LOG_SCAN_BUS(4, printk("\n"));
		}
	}
}

/*
 * Function:    scan_scsis_single
 *
 * Purpose:     Determine if a SCSI device (a single LUN) exists, and
 * 		configure it into the system.
 *
 * Arguments:   channel    - the host's channel
 * 		dev        - target dev (target id)
 * 		lun        - LUN
 * 		scsi_level - SCSI 1, 2 or 3
 * 		SDpnt2     - pointer to pointer of a preallocated Scsi_Device
 * 		shpnt      - host device to use
 * 		scsi_result - preallocated buffer for the SCSI command result
 *
 * Lock status: None
 *
 * Returns:     SCSI_SCAN_NO_RESPONSE - no valid response received from the
 * 		device, this includes allocation failures preventing IO from
 * 		being sent, or any general failures.
 *
 *		SCSI_SCAN_DEVICE_PRESENT - device responded, SDpnt2 has all
 *		values needed to send IO set, plus scsi_level is set, but no
 *		new Scsi_Device was added/allocated.
 *
 *   		SCSI_SCAN_DEVICE_ADDED - device responded, and added to list;
 *   		SDpnt2 filled, and pointed to new allocated Scsi_Device.
 *
 * Notes:       This could be cleaned up more by moving SDpnt2 and Scsi_Device
 * 		allocation into scan_scsis_target.
 */
static int scan_scsis_single(unsigned int channel, unsigned int dev,
		unsigned int lun, int scsi_level, Scsi_Device ** SDpnt2,
		struct Scsi_Host *shpnt, char *scsi_result)
{
	char devname[64];
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	struct Scsi_Device_Template *sdtpnt;
	Scsi_Device *SDtail, *SDpnt = *SDpnt2;
	Scsi_Request * SRpnt;
	int bflags, type = -1;
	int possible_inq_resp_len;
	extern devfs_handle_t scsi_devfs_handle;

	SDpnt->host = shpnt;
	SDpnt->id = dev;
	SDpnt->lun = lun;
	SDpnt->channel = channel;
	SDpnt->online = TRUE;

	scsi_build_commandblocks(SDpnt);
 
	/* Some low level driver could use device->type (DB) */
	SDpnt->type = -1;

	/*
	 * Assume that the device will have handshaking problems, and then fix
	 * this field later if it turns out it doesn't
	 */
	SDpnt->borken = 1;
	SDpnt->was_reset = 0;
	SDpnt->expecting_cc_ua = 0;
	SDpnt->starved = 0;

	if (NULL == (SRpnt = scsi_allocate_request(SDpnt))) {
		printk("scan_scsis_single: no memory\n");
		scsi_release_commandblocks(SDpnt);
		return SCSI_SCAN_NO_RESPONSE;
	}

	/*
	 * We used to do a TEST_UNIT_READY before the INQUIRY but that was 
	 * not really necessary.  Spec recommends using INQUIRY to scan for
	 * devices (and TEST_UNIT_READY to poll for media change). - Paul G.
	 */

	SCSI_LOG_SCAN_BUS(3,
		printk("scsi scan: INQUIRY to host %d channel %d id %d lun %d\n",
		       shpnt->host_no, channel, dev, lun)
	);

	/*
	 * Build an INQUIRY command block.
	 */
	memset(scsi_cmd, 0, 6);
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = (lun << 5) & 0xe0;
	scsi_cmd[4] = 36;	/* issue conservative alloc_length */
	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;

	memset(scsi_result, 0, 36);
	scsi_wait_req (SRpnt, (void *) scsi_cmd,
	          (void *) scsi_result,
	          36, SCSI_TIMEOUT+4*HZ, 3);

	SCSI_LOG_SCAN_BUS(3, printk("scsi: INQUIRY %s with code 0x%x\n",
		SRpnt->sr_result ? "failed" : "successful", SRpnt->sr_result));

	/*
	 * Now that we don't do TEST_UNIT_READY anymore, we must be prepared
	 * for media change conditions here, so cannot require zero result.
	 */
	if (SRpnt->sr_result) {
		if ((driver_byte(SRpnt->sr_result) & DRIVER_SENSE) != 0 &&
		    (SRpnt->sr_sense_buffer[2] & 0xf) == UNIT_ATTENTION &&
		    SRpnt->sr_sense_buffer[12] == 0x28 &&
		    SRpnt->sr_sense_buffer[13] == 0) {
			/* not-ready to ready transition - good */
	 		/* dpg: bogus? INQUIRY never returns UNIT_ATTENTION */
		} else {
			/* assume no peripheral if any other sort of error */
			scsi_release_request(SRpnt);
			return 0;
		}
	}

	/*
	 * Get any flags for this device.  
	 */
	bflags = get_device_flags (&scsi_result[8], &scsi_result[16]);

	possible_inq_resp_len = (unsigned char)scsi_result[4] + 5;
	if (BLIST_INQUIRY_36 & bflags)
		possible_inq_resp_len = 36;
	else if (BLIST_INQUIRY_58 & bflags)
		possible_inq_resp_len = 58;
	else if (possible_inq_resp_len > 255)
		possible_inq_resp_len = 36;	/* sanity */

	if (possible_inq_resp_len > 36) { /* do additional INQUIRY */
		memset(scsi_cmd, 0, 6);
		scsi_cmd[0] = INQUIRY;
		if ((lun > 0) && (scsi_level <= SCSI_2))
			scsi_cmd[1] = (lun << 5) & 0xe0;
		scsi_cmd[4] = (unsigned char)possible_inq_resp_len;
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;

		scsi_wait_req (SRpnt, (void *) scsi_cmd,
			  (void *) scsi_result,
			  scsi_cmd[4], SCSI_TIMEOUT+4*HZ, 3);
		/* assume successful */
	}
	SDpnt->inquiry_len = possible_inq_resp_len;
	SDpnt->inquiry = kmalloc(possible_inq_resp_len, GFP_ATOMIC);
	if (NULL == SDpnt->inquiry) {
		scsi_release_commandblocks(SDpnt);
		scsi_release_request(SRpnt);
		return SCSI_SCAN_NO_RESPONSE;
	}
	memcpy(SDpnt->inquiry, scsi_result, possible_inq_resp_len);
	SDpnt->vendor = (char *)(SDpnt->inquiry + 8);
	SDpnt->model = (char *)(SDpnt->inquiry + 16);
	SDpnt->rev = (char *)(SDpnt->inquiry + 32);

	SDpnt->scsi_level = scsi_result[2] & 0x07;
	if (SDpnt->scsi_level >= 2 ||
	    (SDpnt->scsi_level == 1 &&
	     (scsi_result[3] & 0x0f) == 1))
		SDpnt->scsi_level++;

	/*
	 * Check the peripheral qualifier field - this tells us whether LUNS
	 * are supported here or not.
	 */
	if ((scsi_result[0] >> 5) == 3) {
		/*
		 * Peripheral qualifier 3 (011b): The device server is not
		 * capable of supporting a physical device on this logical
		 * unit.
		 */
		scsi_release_commandblocks(SDpnt);
  		scsi_release_request(SRpnt);
		return SCSI_SCAN_DEVICE_PRESENT;
	}
	 /*   The Toshiba ROM was "gender-changed" here as an inline hack.
	      This is now much more generic.
	      This is a mess: What we really want is to leave the scsi_result
	      alone, and just change the SDpnt structure. And the SDpnt is what
	      we want print_inquiry to print.  -- REW
	 */
	if (bflags & BLIST_ISDISK) {
		scsi_result[0] = TYPE_DISK;                                                
		scsi_result[1] |= 0x80;     /* removable */
	}

	if (bflags & BLIST_ISROM) {
		scsi_result[0] = TYPE_ROM;
		scsi_result[1] |= 0x80;     /* removable */
	}
    

	SDpnt->removable = (0x80 & scsi_result[1]) >> 7;
	/* Use the peripheral qualifier field to determine online/offline */
	if (((scsi_result[0] >> 5) & 7) == 1) 	SDpnt->online = FALSE;
	else SDpnt->online = TRUE;
	SDpnt->lockable = SDpnt->removable;
	SDpnt->changed = 0;
	SDpnt->access_count = 0;
	SDpnt->busy = 0;
	SDpnt->has_cmdblocks = 0;
	/*
	 * Currently, all sequential devices are assumed to be tapes, all random
	 * devices disk, with the appropriate read only flags set for ROM / WORM
	 * treated as RO.
	 */
	switch (type = (scsi_result[0] & 0x1f)) {
	case TYPE_TAPE:
	case TYPE_DISK:
	case TYPE_PRINTER:
	case TYPE_MOD:
	case TYPE_PROCESSOR:
	case TYPE_SCANNER:
	case TYPE_MEDIUM_CHANGER:
	case TYPE_ENCLOSURE:
	case TYPE_COMM:
		SDpnt->writeable = 1;
		break;
	case TYPE_WORM:
	case TYPE_ROM:
		SDpnt->writeable = 0;
		break;
	default:
		printk("scsi: unknown type %d\n", type);
	}

	SDpnt->device_blocked = FALSE;
	SDpnt->device_busy = 0;
	SDpnt->single_lun = 0;
	SDpnt->soft_reset =
	    (scsi_result[7] & 1) && ((scsi_result[3] & 7) == 2);
	SDpnt->random = (type == TYPE_TAPE) ? 0 : 1;
	SDpnt->type = (type & 0x1f);

	print_inquiry(scsi_result);

	/* interrogate scsi target to provide device identifier */
	scsi_load_identifier(SDpnt, SRpnt);

	/* create driverfs files */
	sprintf(SDpnt->sdev_driverfs_dev.bus_id,"%d:%d:%d:%d",
		SDpnt->host->host_no, SDpnt->channel, SDpnt->id, SDpnt->lun);
	
	SDpnt->sdev_driverfs_dev.parent = &SDpnt->host->host_driverfs_dev;
	SDpnt->sdev_driverfs_dev.bus = &scsi_driverfs_bus_type;

	device_register(&SDpnt->sdev_driverfs_dev); 

	/* Create driverfs file entries */
	device_create_file(&SDpnt->sdev_driverfs_dev, 
			&scsi_device_type_file);

        sprintf (devname, "host%d/bus%d/target%d/lun%d",
                 SDpnt->host->host_no, SDpnt->channel, SDpnt->id, SDpnt->lun);
        if (SDpnt->de) printk ("DEBUG: dir: \"%s\" already exists\n", devname);
        else SDpnt->de = devfs_mk_dir (scsi_devfs_handle, devname, NULL);

	for (sdtpnt = scsi_devicelist; sdtpnt;
	     sdtpnt = sdtpnt->next)
		if (sdtpnt->detect)
			SDpnt->attached +=
			    (*sdtpnt->detect) (SDpnt);

	/*
	 * Accommodate drivers that want to sleep when they should be in a polling
	 * loop.
	 */
	SDpnt->disconnect = 0;


	/*
	 * Set the tagged_queue flag for SCSI-II devices that purport to support
	 * tagged queuing in the INQUIRY data.
	 */
	SDpnt->tagged_queue = 0;
	if ((SDpnt->scsi_level >= SCSI_2) &&
	    (scsi_result[7] & 2) &&
	    !(bflags & BLIST_NOTQ)) {
		SDpnt->tagged_supported = 1;
		SDpnt->current_tag = 0;
	}
	/*
	 * Some revisions of the Texel CD ROM drives have handshaking problems when
	 * used with the Seagate controllers.  Before we know what type of device
	 * we're talking to, we assume it's borken and then change it here if it
	 * turns out that it isn't a TEXEL drive.
	 */
	if ((bflags & BLIST_BORKEN) == 0)
		SDpnt->borken = 0;

	/*
	 * If we want to only allow I/O to one of the luns attached to this device
	 * at a time, then we set this flag.
	 */
	if (bflags & BLIST_SINGLELUN)
		SDpnt->single_lun = 1;

	/*
	 * These devices need this "key" to unlock the devices so we can use it
	 */
	if ((bflags & BLIST_KEY) != 0) {
		printk("Unlocked floptical drive.\n");
		SDpnt->lockable = 0;
		scsi_cmd[0] = MODE_SENSE;
		if (shpnt->max_lun <= 8)
			scsi_cmd[1] = (lun << 5) & 0xe0;
		else	scsi_cmd[1] = 0;	/* any other idea? */
		scsi_cmd[2] = 0x2e;
		scsi_cmd[3] = 0;
		scsi_cmd[4] = 0x2a;
		scsi_cmd[5] = 0;
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req (SRpnt, (void *) scsi_cmd,
	        	(void *) scsi_result, 0x2a,
	        	SCSI_TIMEOUT, 3);
		/*
		 * scsi_result no longer holds inquiry data.
		 */
	}

	scsi_release_request(SRpnt);
	SRpnt = NULL;

	scsi_release_commandblocks(SDpnt);

	/*
	 * This device was already hooked up to the host in question,
	 * so at this point we just let go of it and it should be fine.  We do need to
	 * allocate a new one and attach it to the host so that we can further scan the bus.
	 */
	SDpnt = (Scsi_Device *) kmalloc(sizeof(Scsi_Device), GFP_ATOMIC);
	if (!SDpnt) {
		printk("scsi: scan_scsis_single: Cannot malloc\n");
		return SCSI_SCAN_NO_RESPONSE;
	}
        memset(SDpnt, 0, sizeof(Scsi_Device));
	SDpnt->vendor = scsi_null_device_strs;
	SDpnt->model = scsi_null_device_strs;
	SDpnt->rev = scsi_null_device_strs;

	*SDpnt2 = SDpnt;
	SDpnt->queue_depth = 1;
	SDpnt->host = shpnt;
	SDpnt->online = TRUE;
	SDpnt->scsi_level = scsi_level;

	/*
	 * Register the queue for the device.  All I/O requests will come
	 * in through here.  We also need to register a pointer to
	 * ourselves, since the queue handler won't know what device
	 * the queue actually represents.   We could look it up, but it
	 * is pointless work.
	 */
	scsi_initialize_queue(SDpnt, shpnt);
	SDpnt->host = shpnt;
	scsi_initialize_merge_fn(SDpnt);

	/*
	 * Mark this device as online, or otherwise we won't be able to do much with it.
	 */
	SDpnt->online = TRUE;

        /*
         * Initialize the object that we will use to wait for command blocks.
         */
	init_waitqueue_head(&SDpnt->scpnt_wait);

	/*
	 * Since we just found one device, there had damn well better be one in the list
	 * already.
	 */
	if (shpnt->host_queue == NULL)
		panic("scan_scsis_single: Host queue == NULL\n");

	SDtail = shpnt->host_queue;
	while (SDtail->next) {
		SDtail = SDtail->next;
	}

	/* Add this device to the linked list at the end */
	SDtail->next = SDpnt;
	SDpnt->prev = SDtail;
	SDpnt->next = NULL;

	return SCSI_SCAN_DEVICE_ADDED;
}

/*
 * Function:    scsi_report_lun_scan
 *
 * Purpose:     Use a SCSI REPORT LUN to determine what LUNs to scan.
 *
 * Arguments:   SDlun0_pnt - pointer to a Scsi_Device for LUN 0
 * 		channel    - the host's channel
 * 		dev        - target dev (target id)
 * 		SDpnt2     - pointer to pointer of a preallocated Scsi_Device
 * 		shpnt      - host device to use
 * 		scsi_result - preallocated buffer for the SCSI command result
 *
 * Lock status: None
 *
 * Returns:     If the LUNs for device at shpnt/channel/dev are scanned,
 * 		return 0, else return 1.
 *
 * Notes:       This code copies and truncates the 8 byte LUN into the
 * 		current 4 byte (int) lun.
 */
static int scsi_report_lun_scan(Scsi_Device *SDlun0_pnt, unsigned
		int channel, unsigned int dev, Scsi_Device **SDpnt2,
		struct Scsi_Host *shpnt, char *scsi_result)
{
#ifdef CONFIG_SCSI_REPORT_LUNS

	char devname[64];
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	unsigned int length;
	unsigned int lun;
	unsigned int num_luns;
	unsigned int retries;
	ScsiLun *fcp_cur_lun_pnt, *lun_data_pnt;
	Scsi_Request *SRpnt;
	int scsi_level;
	char *byte_pnt;
	int got_command_blocks = 0;

	/*
	 * Only support SCSI-3 and up devices.
	 */
	if (SDlun0_pnt->scsi_level < SCSI_3)
		return 1;

	/*
	 * Note SDlun0_pnt might be invalid after scan_scsis_single is called.
	 */

	/*
	 * Command blocks might be built depending on whether LUN 0 was
	 * configured or not. Checking has_cmdblocks here is ugly.
	 */
	if (SDlun0_pnt->has_cmdblocks == 0) {
		got_command_blocks = 1;
		scsi_build_commandblocks(SDlun0_pnt);
	}
	SRpnt = scsi_allocate_request(SDlun0_pnt);

	sprintf (devname, "host %d channel %d id %d",
		 SDlun0_pnt->host->host_no, SDlun0_pnt->channel,
		 SDlun0_pnt->id);
	/*
	 * Allocate enough to hold the header (the same size as one ScsiLun)
	 * plus the max number of luns we are requesting.
	 *
	 * XXX: Maybe allocate this once, like scsi_result, and pass it down.
	 * scsi_result can't be used, as it is needed for the scan INQUIRY
	 * data. In addition, reallocating and trying again (with the exact
	 * amount we need) would be nice, but then we need to somehow limit the
	 * size allocated based on the available memory (and limits of kmalloc).
	 */
	length = (max_scsi_report_luns + 1) * sizeof(ScsiLun);
	lun_data_pnt = (ScsiLun *) kmalloc(length,
			(shpnt->unchecked_isa_dma ?  GFP_DMA : GFP_ATOMIC));
	if (lun_data_pnt == NULL) {
		printk("scsi: scsi_report_lun_scan: Cannot malloc\n");
		if (got_command_blocks)
			scsi_release_commandblocks(SDlun0_pnt);
		return 1;
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

	scsi_cmd[10] = 0; /* reserved */
	scsi_cmd[11] = 0; /* control */
	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;

	/*
	 * We can get a UNIT ATTENTION, for example a power on/reset, so retry
	 * a few times (like sd.c does for TEST UNIT READY). Experience shows
	 * some combinations of adapter/devices get at least two power
	 * on/resets.
	 *
	 * Illegal requests (for devices that do not support REPORT LUNS)
	 * should come through as a check condition, and will not generate a
	 * retry.
	 */
	retries = 0;
	while (retries++ < 3) {
		SCSI_LOG_SCAN_BUS(3,
			printk("scsi: Sending REPORT LUNS to %s (try %d)\n",
				devname, retries));

		scsi_wait_req (SRpnt, (void *) scsi_cmd, (void *) lun_data_pnt,
			  length, SCSI_TIMEOUT+4*HZ, 3);

		SCSI_LOG_SCAN_BUS(3,
			printk("scsi: REPORT LUNS %s (try %d) result 0x%x\n",
			SRpnt->sr_result ? "failed" : "successful", retries,
			SRpnt->sr_result));

		if (SRpnt->sr_result == 0
		    || SRpnt->sr_sense_buffer[2] != UNIT_ATTENTION)
			break;
	}

	scsi_release_request(SRpnt);
	if (got_command_blocks)
		scsi_release_commandblocks(SDlun0_pnt);

	if (SRpnt->sr_result) {
		kfree((char *) lun_data_pnt);
		return 1;
	}

	/*
	 * Get the length from the first four bytes of lun_data_pnt.
	 */
	byte_pnt = (char*) lun_data_pnt->scsi_lun;
	length = ((byte_pnt[0] << 24) | (byte_pnt[1] << 16) |
			 (byte_pnt[2] << 8) | (byte_pnt[3] << 0));
	if ((length / sizeof(ScsiLun)) > max_scsi_report_luns) {
		printk("scsi: On %s only %d (max_scsi_report_luns) of %d luns"
			" reported, try increasing max_scsi_report_luns.\n",
			devname, max_scsi_report_luns,
			length / sizeof(ScsiLun));
		num_luns = max_scsi_report_luns;
	} else
		num_luns = (length / sizeof(ScsiLun));

	scsi_level = SDlun0_pnt->scsi_level;

	SCSI_LOG_SCAN_BUS(3,
		printk("scsi: REPORT LUN scan of host %d channel %d id %d\n",
		 SDlun0_pnt->host->host_no, SDlun0_pnt->channel,
		 SDlun0_pnt->id));
	/*
	 * Scan the luns in lun_data_pnt. The entry at offset 0 is really
	 * the header, so start at 1 and go up to and including num_luns.
	 */
	for (fcp_cur_lun_pnt = &lun_data_pnt[1];
	     fcp_cur_lun_pnt <= &lun_data_pnt[num_luns];
	     fcp_cur_lun_pnt++) {
		lun = scsilun_to_int(fcp_cur_lun_pnt);
		/*
		 * Check if the unused part of fcp_cur_lun_pnt is non-zero,
		 * and so does not fit in lun.
		 */
		if (memcmp(&fcp_cur_lun_pnt->scsi_lun[sizeof(lun)],
			   "\0\0\0\0", 4) != 0) {
			int i;

			/*
			 * Output an error displaying the LUN in byte order,
			 * this differs from what linux would print for the
			 * integer LUN value.
			 */
			printk("scsi: %s lun 0x", devname);
			byte_pnt = (char*) fcp_cur_lun_pnt->scsi_lun;
			for (i = 0; i < sizeof(ScsiLun); i++)
				printk("%02x", byte_pnt[i]);
			printk(" has a LUN larger than that supported by"
				" the kernel\n");
		} else if (lun == 0) {
			/*
			 * LUN 0 has already been scanned.
			 */
		} else if (lun > shpnt->max_lun) {
			printk("scsi: %s lun %d has a LUN larger than allowed"
				" by the host adapter\n", devname, lun);
		} else {
			/*
			 * Don't use SDlun0_pnt after this call - it can be
			 * overwritten via SDpnt2 if there was no real device
			 * at LUN 0.
			 */
			if (scan_scsis_single(channel, dev, lun,
			    scsi_level, SDpnt2, shpnt, scsi_result)
				== SCSI_SCAN_NO_RESPONSE) {
				/*
				 * Got some results, but now none, abort.
				 */
				printk("scsi: no response from %s lun %d while"
				       " scanning, scan aborted\n", devname, 
				       lun);
				break;
			}
		}
	}

	kfree((char *) lun_data_pnt);
	return 0;

#else
	return 1;
#endif	/* CONFIG_SCSI_REPORT_LUNS */

}

/*
 * Function:    scan_scsis_target
 *
 * Purpose:     Scan the given scsi target dev, and as needed all LUNs
 * 		on the target dev.
 *
 * Arguments:   channel    - the host's channel
 * 		dev        - target dev (target id)
 * 		SDpnt2     - pointer to pointer of a preallocated Scsi_Device
 * 		shpnt      - host device to use
 * 		scsi_result - preallocated buffer for the SCSI command result
 *
 * Lock status: None
 *
 * Returns:     void
 *
 * Notes:       This tries to be compatible with linux 2.4.x. This function
 * 		relies on scan_scsis_single to setup SDlun0_pnt. 
 *
 * 		It would be better if the Scsi_Device allocation and freeing
 * 		was done here, rather than oddly embedded in scan_scsis_single
 * 		and scan_scsis.
 */
static void scan_scsis_target(unsigned int channel, unsigned int dev,
		Scsi_Device **SDpnt2, struct Scsi_Host *shpnt,
		char *scsi_result)
{
	int bflags, scsi_level;
	Scsi_Device *SDlun0_pnt;
	unsigned int sparse_lun = 0;
	unsigned int max_dev_lun, lun;
	unsigned int sdlun0_res;

	/*
	 * Scan lun 0, use the results to determine whether to scan further.
	 * Ideally, we would not configure LUN 0 until we scan.
	 */
	SDlun0_pnt = *SDpnt2;
	sdlun0_res = scan_scsis_single(channel, dev, 0 /* LUN 0 */, SCSI_2,
		SDpnt2, shpnt, scsi_result);
	if (sdlun0_res == SCSI_SCAN_NO_RESPONSE)
		return;

	/*
	 * If no new SDpnt was allocated (SCSI_SCAN_DEVICE_PRESENT), SDlun0_pnt
	 * can later be modified. It is unlikely the lun level would change,
	 * but save it just in case.
	 */
	scsi_level = SDlun0_pnt->scsi_level;

	/*
	 * We could probably use and save the bflags from lun 0 for all luns
	 * on a target, but be safe and match current behaviour. (LUN 0
	 * bflags controls the target settings checked within this function.)
	 */
	bflags = get_device_flags (SDlun0_pnt->vendor,  SDlun0_pnt->model);

	/*
	 * Some scsi devices cannot be polled for lun != 0 due to firmware bugs
	 */
	if (bflags & BLIST_NOLUN)
		return;

	/*
	 * Ending the scan here if max_scsi_luns == 1 breaks scanning of
	 * SPARSE, FORCE, MAX5 LUN devices, and the report lun scans.
	 */

	if (scsi_report_lun_scan(SDlun0_pnt, channel, dev, SDpnt2, shpnt,
	    scsi_result) == 0)
		return;

	SCSI_LOG_SCAN_BUS(3,
		printk("scsi: Sequential scan of host %d channel %d id %d\n",
		 SDlun0_pnt->host->host_no, SDlun0_pnt->channel,
		 SDlun0_pnt->id));

	max_dev_lun = (max_scsi_luns < shpnt->max_lun ?
			max_scsi_luns : shpnt->max_lun);
	/*
	 * If this device is known to support sparse multiple units,
	 * override the other settings, and scan all of them.
	 */
	if (bflags & BLIST_SPARSELUN) {
		max_dev_lun = shpnt->max_lun;
		sparse_lun = 1;
	} else if (sdlun0_res == SCSI_SCAN_DEVICE_PRESENT) {
		/*
		 * LUN 0 responded, but no LUN 0 was added, don't scan any
		 * further. This matches linux 2.4.x behaviour.
		 */
		return;
	}
	/*
	 * If less than SCSI_1_CSS, and not a forced lun scan, stop
	 * scanning, this matches 2.4 behaviour, but it could be a bug
	 * to scan SCSI_1_CSS devices past LUN 0.
	 */
	if ((scsi_level < SCSI_1_CCS) && ((bflags &
	     (BLIST_FORCELUN | BLIST_SPARSELUN | BLIST_MAX5LUN)) == 0))
		return;
	/*
	 * If this device is known to support multiple units, override
	 * the other settings, and scan all of them.
	 */
	if (bflags & BLIST_FORCELUN)
		max_dev_lun = shpnt->max_lun;
	/*
	 * REGAL CDC-4X: avoid hang after LUN 4
	 */
	if (bflags & BLIST_MAX5LUN)
		max_dev_lun = min(5U, max_dev_lun);
	/*
	 * Do not scan past LUN 7.
	 */
	if (scsi_level < SCSI_3)
		max_dev_lun = min(8U, max_dev_lun);

	/*
	 * We have already scanned lun 0.
	 */
	for (lun = 1; lun < max_dev_lun; ++lun) {
		int res;
		/*
		 * Scan until scan_scsis_single says stop,
		 * unless sparse_lun is set.
		 */
		res = scan_scsis_single(channel, dev, lun,
		     scsi_level, SDpnt2, shpnt, scsi_result);
		if (res == SCSI_SCAN_NO_RESPONSE) {
			/*
			 * Got a response on LUN 0, but now no response.
			 */
			printk("scsi: no response from device"
				" host%d/bus%d/target%d/lun%d"
				" while scanning, scan aborted\n",
				shpnt->host_no, channel, dev, lun);
			return;
		} else if ((res == SCSI_SCAN_DEVICE_PRESENT)
			    && !sparse_lun)
			return;
	}
}

/*
 * Returns the scsi_level of lun0 on this host, channel and dev (if already
 * known), otherwise returns SCSI_2.
 */
static int find_lun0_scsi_level(unsigned int channel, unsigned int dev,
				struct Scsi_Host *shpnt)
{
	int res = SCSI_2;
	Scsi_Device *SDpnt;

	for (SDpnt = shpnt->host_queue; SDpnt; SDpnt = SDpnt->next)
	{
		if ((0 == SDpnt->lun) && (dev == SDpnt->id) &&
		    (channel == SDpnt->channel))
			return (int)SDpnt->scsi_level;
	}
	/* haven't found lun0, should send INQUIRY but take easy route */
	return res;
}

#define SCSI_UID_DEV_ID  'U'
#define SCSI_UID_SER_NUM 'S'
#define SCSI_UID_UNKNOWN 'Z'

unsigned char *scsi_get_evpd_page(Scsi_Device *SDpnt, Scsi_Request * SRpnt)
{
	unsigned char *evpd_page = NULL;
	unsigned char *scsi_cmd = NULL;
	int lun = SDpnt->lun;
	int scsi_level = SDpnt->scsi_level;

	evpd_page = kmalloc(255, 
		(SDpnt->host->unchecked_isa_dma ? GFP_DMA : GFP_ATOMIC));
	if (!evpd_page)
		return NULL;

	scsi_cmd = kmalloc(MAX_COMMAND_SIZE, GFP_ATOMIC);
	if (!scsi_cmd) {
		kfree(evpd_page);
		return NULL;
	}

	/* Use vital product pages to determine serial number */
	/* Try Supported vital product data pages 0x00 first */
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[2] = 0x00;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 255;
	scsi_cmd[5] = 0;
	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_sense_buffer[0] = 0;
	SRpnt->sr_sense_buffer[2] = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(SRpnt, (void *) scsi_cmd, (void *) evpd_page,
			255, SCSI_TIMEOUT+4*HZ, 3);

	if (SRpnt->sr_result) {
		kfree(scsi_cmd);
		kfree(evpd_page);
		return NULL;
	}

	/* check to see if response was truncated */
	if (evpd_page[3] > 255) {
		int max_lgth = evpd_page[3] + 4;

		kfree(evpd_page);
		evpd_page = kmalloc(max_lgth, (SDpnt->host->unchecked_isa_dma ? 
			GFP_DMA : GFP_ATOMIC));
		if (!evpd_page) {
			kfree(scsi_cmd);
			return NULL;
		}
		memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
		scsi_cmd[0] = INQUIRY;
		if ((lun > 0) && (scsi_level <= SCSI_2))
			scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
		else
			scsi_cmd[1] = 0x01; /* SCSI_3 and higher, don't touch */
		scsi_cmd[2] = 0x00;
		scsi_cmd[3] = 0;
		scsi_cmd[4] = max_lgth;
		scsi_cmd[5] = 0;
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) scsi_cmd, (void *) evpd_page,
			max_lgth, SCSI_TIMEOUT+4*HZ, 3);
		if (SRpnt->sr_result) {
			kfree(scsi_cmd);
			kfree(evpd_page);
			return NULL;
		}
	}
	kfree(scsi_cmd);
	/* some ill behaved devices return the std inquiry here rather than
		the evpd data. snoop the data to verify */
	if (evpd_page[3] > 16) {
		/* if vend id appears in the evpd page assume evpd is invalid */
		if (!strncmp(&evpd_page[8], SDpnt->vendor, 8)) {
			kfree(evpd_page);
			return NULL;
		}
	}
	return evpd_page;
}

int scsi_get_deviceid(Scsi_Device *SDpnt,Scsi_Request * SRpnt)
{
	unsigned char *id_page = NULL;
	unsigned char *scsi_cmd = NULL;
	int scnt, i, j, idtype;
	char * id = SDpnt->sdev_driverfs_dev.name;
	int lun = SDpnt->lun;
	int scsi_level = SDpnt->scsi_level;

	id_page = kmalloc(255, 
		(SDpnt->host->unchecked_isa_dma ? GFP_DMA : GFP_ATOMIC)); 
	if (!id_page)
		return 0;

	scsi_cmd = kmalloc(MAX_COMMAND_SIZE, GFP_ATOMIC);
	if (!scsi_cmd)
		goto leave;

	/* Use vital product pages to determine serial number */
	/* Try Supported vital product data pages 0x00 first */
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[2] = 0x83;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 255;
	scsi_cmd[5] = 0;
	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_sense_buffer[0] = 0;
	SRpnt->sr_sense_buffer[2] = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(SRpnt, (void *) scsi_cmd, (void *) id_page,
			255, SCSI_TIMEOUT+4*HZ, 3);
	if (SRpnt->sr_result) {
		kfree(scsi_cmd);
		goto leave;
	}

	/* check to see if response was truncated */
	if (id_page[3] > 255) {
		int max_lgth = id_page[3] + 4;

		kfree(id_page);
		id_page = kmalloc(max_lgth,
			(SDpnt->host->unchecked_isa_dma ? 
			GFP_DMA : GFP_ATOMIC)); 
		if (!id_page) {
			kfree(scsi_cmd);
			return 0;
		}
		memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
		scsi_cmd[0] = INQUIRY;
		if ((lun > 0) && (scsi_level <= SCSI_2))
			scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
		else
			scsi_cmd[1] = 0x01; /* SCSI_3 and higher, don't touch */
		scsi_cmd[2] = 0x83;
		scsi_cmd[3] = 0;
		scsi_cmd[4] = max_lgth;
		scsi_cmd[5] = 0;
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) scsi_cmd, (void *) id_page,
				max_lgth, SCSI_TIMEOUT+4*HZ, 3);
		if (SRpnt->sr_result) {
			kfree(scsi_cmd);
			goto leave;
		}
	}
	kfree(scsi_cmd);

	idtype = 3;
	while (idtype > 0) {
		for(scnt = 4; scnt <= id_page[3] + 3; 
		    scnt += id_page[scnt + 3] + 4) {
			if ((id_page[scnt + 1] & 0x0f) != idtype) {
				continue;
			}
			if ((id_page[scnt] & 0x0f) == 2) {  
				for(i = scnt + 4, j = 1;
				    i < scnt + 4 + id_page[scnt + 3]; 
				    i++) {
					if (id_page[i] > 0x20) {
						if (j == DEVICE_NAME_SIZE) {
							memset(id, 0, 
							    DEVICE_NAME_SIZE);
							break;
						}
						id[j++] = id_page[i];
					}
				}
			} else if ((id_page[scnt] & 0x0f) == 1) {
				static const char hex_str[]="0123456789abcdef";
				for(i = scnt + 4, j = 1;
				    i < scnt + 4 + id_page[scnt + 3]; 
				    i++) {
					if ((j + 1) == DEVICE_NAME_SIZE) {
						memset(id, 0, DEVICE_NAME_SIZE);
						break;
					}
					id[j++] = hex_str[(id_page[i] & 0xf0) >>
								4];
					id[j++] = hex_str[id_page[i] & 0x0f];
				}
			}
			if (id[1] != 0) goto leave;
		}
		idtype--;
	}
 leave:
	kfree(id_page);
	if (id[1] != 0) {
		id[0] = SCSI_UID_DEV_ID;
		return 1;
	}
	return 0;
}

int scsi_get_serialnumber(Scsi_Device *SDpnt, Scsi_Request * SRpnt)
{
	unsigned char *serialnumber_page = NULL;
	unsigned char *scsi_cmd = NULL;
	int lun = SDpnt->lun;
	int scsi_level = SDpnt->scsi_level;
	int i, j;

	serialnumber_page = kmalloc(255, (SDpnt->host->unchecked_isa_dma ? 
			GFP_DMA : GFP_ATOMIC));
	if (!serialnumber_page)
		return 0;

	scsi_cmd = kmalloc(MAX_COMMAND_SIZE, GFP_ATOMIC);
	if (!scsi_cmd)
		goto leave;

	/* Use vital product pages to determine serial number */
	/* Try Supported vital product data pages 0x00 first */
	scsi_cmd[0] = INQUIRY;
	if ((lun > 0) && (scsi_level <= SCSI_2))
		scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
	else	
		scsi_cmd[1] = 0x01;	/* SCSI_3 and higher, don't touch */
	scsi_cmd[2] = 0x80;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 255;
	scsi_cmd[5] = 0;
	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_sense_buffer[0] = 0;
	SRpnt->sr_sense_buffer[2] = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;
	scsi_wait_req(SRpnt, (void *) scsi_cmd, (void *) serialnumber_page,
			255, SCSI_TIMEOUT+4*HZ, 3);

	if (SRpnt->sr_result) {
		kfree(scsi_cmd);
		goto leave;
	}
	/* check to see if response was truncated */
	if (serialnumber_page[3] > 255) {
		int max_lgth = serialnumber_page[3] + 4;

		kfree(serialnumber_page);
		serialnumber_page = kmalloc(max_lgth, 
			(SDpnt->host->unchecked_isa_dma ? 
			GFP_DMA : GFP_ATOMIC));
		if (!serialnumber_page) {
			kfree(scsi_cmd);
			return 0;
		}
		memset(scsi_cmd, 0, MAX_COMMAND_SIZE);
		scsi_cmd[0] = INQUIRY;
		if ((lun > 0) && (scsi_level <= SCSI_2))
			scsi_cmd[1] = ((lun << 5) & 0xe0) | 0x01;
		else
			scsi_cmd[1] = 0x01; /* SCSI_3 and higher, don't touch */
		scsi_cmd[2] = 0x80;
		scsi_cmd[3] = 0;
		scsi_cmd[4] = max_lgth;
		scsi_cmd[5] = 0;
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) scsi_cmd, 
				(void *) serialnumber_page,
				max_lgth, SCSI_TIMEOUT+4*HZ, 3);
		if (SRpnt->sr_result) {
			kfree(scsi_cmd);
			goto leave;
		}
	}
	kfree(scsi_cmd);

	SDpnt->sdev_driverfs_dev.name[0] = SCSI_UID_SER_NUM;
	for(i = 0, j = 1; i < serialnumber_page[3]; i++) {
		if (serialnumber_page[4 + i] > 0x20)
			SDpnt->sdev_driverfs_dev.name[j++] = 
				serialnumber_page[4 + i];
	}
	for(i = 0, j = strlen(SDpnt->sdev_driverfs_dev.name); i < 8; i++) {
		if (SDpnt->vendor[i] > 0x20) {
			SDpnt->sdev_driverfs_dev.name[j++] = SDpnt->vendor[i];
		}
	}
	kfree(serialnumber_page);
	return 1;
 leave:
	memset(SDpnt->sdev_driverfs_dev.name, 0, DEVICE_NAME_SIZE);
	kfree(serialnumber_page);
	return 0;
}

int scsi_get_default_name(Scsi_Device *SDpnt)
{
	int i, j;

	SDpnt->sdev_driverfs_dev.name[0] = SCSI_UID_UNKNOWN;
	for(i = 0, j = 1; i < 8; i++) {
		if (SDpnt->vendor[i] > 0x20) {
			SDpnt->sdev_driverfs_dev.name[j++] = 
				SDpnt->vendor[i];
		}
	}
	for(i = 0, j = strlen(SDpnt->sdev_driverfs_dev.name); 
	    i < 16; i++) {
		if (SDpnt->model[i] > 0x20) {
			SDpnt->sdev_driverfs_dev.name[j++] = 
				SDpnt->model[i];
		}
	}
	for(i = 0, j = strlen(SDpnt->sdev_driverfs_dev.name); i < 4; i++) {
		if (SDpnt->rev[i] > 0x20) {
			SDpnt->sdev_driverfs_dev.name[j++] = SDpnt->rev[i];
		}
	}	
	return 1;
}


static void scsi_load_identifier(Scsi_Device *SDpnt, Scsi_Request * SRpnt)
{
	unsigned char *evpd_page = NULL;
	int cnt;

	memset(SDpnt->sdev_driverfs_dev.name, 0, DEVICE_NAME_SIZE);
	evpd_page = scsi_get_evpd_page(SDpnt, SRpnt);
	if (!evpd_page) {
		/* try to obtain serial number anyway */
		if (!scsi_get_serialnumber(SDpnt, SRpnt))
			goto leave;
		goto leave;
	}

	for(cnt = 4; cnt <= evpd_page[3] + 3; cnt++) {
		if (evpd_page[cnt] == 0x83) {
			if (scsi_get_deviceid(SDpnt, SRpnt))
				goto leave;
		}
	}

	for(cnt = 4; cnt <= evpd_page[3] + 3; cnt++) {
		if (evpd_page[cnt] == 0x80) {
			if (scsi_get_serialnumber(SDpnt, SRpnt))
				goto leave;
		}
	}

leave:
	if (SDpnt->sdev_driverfs_dev.name[0] == 0)
		scsi_get_default_name(SDpnt);

	if (evpd_page) kfree(evpd_page);
	return;
}
