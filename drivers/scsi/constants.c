/* 
 * ASCII values for a number of symbolic constants, printing functions,
 * etc.
 * Additions for SCSI 2 and Linux 2.2.x by D. Gilbert (990422)
 * Additions for SCSI 3+ (SPC-3 T10/1416-D Rev 07 3 May 2002)
 *   by D. Gilbert and aeb (20020609)
 */

#include <linux/module.h>

#include <linux/config.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include "scsi.h"
#include "hosts.h"

#define CONST_COMMAND   0x01
#define CONST_STATUS    0x02
#define CONST_SENSE     0x04
#define CONST_XSENSE    0x08
#define CONST_CMND      0x10
#define CONST_MSG       0x20
#define CONST_HOST	0x40
#define CONST_DRIVER	0x80

static const char unknown[] = "UNKNOWN";

#ifdef CONFIG_SCSI_CONSTANTS
#ifdef CONSTANTS
#undef CONSTANTS
#endif
#define CONSTANTS (CONST_COMMAND | CONST_STATUS | CONST_SENSE | CONST_XSENSE \
		   | CONST_CMND | CONST_MSG | CONST_HOST | CONST_DRIVER)
#endif

#if (CONSTANTS & CONST_COMMAND)
static const char * group_0_commands[] = {
/* 00-03 */ "Test Unit Ready", "Rezero Unit", unknown, "Request Sense",
/* 04-07 */ "Format Unit", "Read Block Limits", unknown, "Reasssign Blocks",
/* 08-0d */ "Read (6)", unknown, "Write (6)", "Seek (6)", unknown, unknown,
/* 0e-12 */ unknown, "Read Reverse", "Write Filemarks", "Space", "Inquiry",  
/* 13-16 */ "Verify", "Recover Buffered Data", "Mode Select", "Reserve",
/* 17-1b */ "Release", "Copy", "Erase", "Mode Sense", "Start/Stop Unit",
/* 1c-1d */ "Receive Diagnostic", "Send Diagnostic", 
/* 1e-1f */ "Prevent/Allow Medium Removal", unknown,
};


static const char *group_1_commands[] = {
/* 20-22 */  unknown, unknown, unknown,
/* 23-28 */ unknown, "Define window parameters", "Read Capacity", 
            unknown, unknown, "Read (10)", 
/* 29-2d */ "Read Generation", "Write (10)", "Seek (10)", "Erase", 
            "Read updated block", 
/* 2e-31 */ "Write Verify","Verify", "Search High", "Search Equal", 
/* 32-34 */ "Search Low", "Set Limits", "Prefetch or Read Position", 
/* 35-37 */ "Synchronize Cache","Lock/Unlock Cache", "Read Defect Data", 
/* 38-3c */ "Medium Scan", "Compare", "Copy Verify", "Write Buffer", 
            "Read Buffer", 
/* 3d-3f */ "Update Block", "Read Long",  "Write Long",
};


static const char *group_2_commands[] = {
/* 40-41 */ "Change Definition", "Write Same",
/* 42-48 */ "Read sub-channel", "Read TOC", "Read header",
            "Play audio (10)", "Get configuration", "Play audio msf",
            "Play audio track/index",
/* 49-4f */ "Play track relative (10)", "Get event status notification",
            "Pause/resume", "Log Select", "Log Sense", "Stop play/scan",
            unknown,
/* 50-55 */ "Xdwrite", "Xpwrite, Read disk info", "Xdread, Read track info",
            "Reserve track", "Send OPC onfo", "Mode Select (10)",
/* 56-5b */ "Reserve (10)", "Release (10)", "Repair track", "Read master cue",
            "Mode Sense (10)", "Close track/session",
/* 5c-5f */ "Read buffer capacity", "Send cue sheet", "Persistent reserve in",
            "Persistent reserve out",
};


/* The following are 16 byte commands in group 4 */
static const char *group_4_commands[] = {
/* 80-84 */ "Xdwrite (16)", "Rebuild (16)", "Regenerate (16)", "Extended copy",
            "Receive copy results",
/* 85-89 */ "Memory Export In (16)", "Access control in", "Access control out",
            "Read (16)", "Memory Export Out (16)",
/* 8a-8f */ "Write (16)", unknown, "Read attributes", "Write attributes",
            "Write and verify (16)", "Verify (16)",
/* 90-94 */ "Pre-fetch (16)", "Synchronize cache (16)",
            "Lock/unlock cache (16)", "Write same (16)", unknown,
/* 95-99 */ unknown, unknown, unknown, unknown, unknown,
/* 9a-9f */ unknown, unknown, unknown, unknown, "Service action in",
            "Service action out",
};

/* The following are 12 byte commands in group 5 */
static const char *group_5_commands[] = {
/* a0-a5 */ "Report luns", "Blank", "Send event", "Maintenance (in)",
            "Maintenance (out)", "Move medium/play audio(12)",
/* a6-a9 */ "Exchange medium", "Move medium attached", "Read(12)",
            "Play track relative(12)",
/* aa-ae */ "Write(12)", unknown, "Erase(12), Get Performance",
            "Read DVD structure", "Write and verify(12)",
/* af-b1 */ "Verify(12)", "Search data high(12)", "Search data equal(12)",
/* b2-b4 */ "Search data low(12)", "Set limits(12)",
            "Read element status attached",
/* b5-b6 */ "Request volume element address", "Send volume tag, set streaming",
/* b7-b9 */ "Read defect data(12)", "Read element status", "Read CD msf",
/* ba-bc */ "Redundancy group (in), Scan",
            "Redundancy group (out), Set cd-rom speed", "Spare (in), Play cd",
/* bd-bf */ "Spare (out), Mechanism status", "Volume set (in), Read cd",
            "Volume set (out), Send DVD structure",
};



#define group(opcode) (((opcode) >> 5) & 7)

#define RESERVED_GROUP  0
#define VENDOR_GROUP    1

static const char **commands[] = {
    group_0_commands, group_1_commands, group_2_commands, 
    (const char **) RESERVED_GROUP, group_4_commands, 
    group_5_commands, (const char **) VENDOR_GROUP, 
    (const char **) VENDOR_GROUP
};

static const char reserved[] = "RESERVED";
static const char vendor[] = "VENDOR SPECIFIC";

static void print_opcode(int opcode) {
    const char **table = commands[ group(opcode) ];
    switch ((unsigned long) table) {
    case RESERVED_GROUP:
	printk("%s(0x%02x) ", reserved, opcode); 
	break;
    case VENDOR_GROUP:
	printk("%s(0x%02x) ", vendor, opcode); 
	break;
    default:
	if (table[opcode & 0x1f] != unknown)
	    printk("%s ",table[opcode & 0x1f]);
	else
	    printk("%s(0x%02x) ", unknown, opcode);
	break;
    }
}
#else /* CONST & CONST_COMMAND */
static void print_opcode(int opcode) {
    printk("0x%02x ", opcode);
}
#endif  

void print_command (unsigned char *command) {
    int i,s;
    print_opcode(command[0]);
    for ( i = 1, s = COMMAND_SIZE(command[0]); i < s; ++i) 
	printk("%02x ", command[i]);
    printk("\n");
}

/**
 *
 *	print_status - print scsi status description
 *	@scsi_status: scsi status value
 *
 *	If the status is recognized, the description is printed.
 *	Otherwise "Unknown status" is output. No trailing space.
 *	If CONFIG_SCSI_CONSTANTS is not set, then print status in hex
 *	(e.g. "0x2" for Check Condition).
 **/
void
print_status(unsigned char scsi_status) {
#if (CONSTANTS & CONST_STATUS)
	const char * ccp;

	switch (scsi_status) {
	case 0:    ccp = "Good"; break;
	case 0x2:  ccp = "Check Condition"; break;
	case 0x4:  ccp = "Condition Met"; break;
	case 0x8:  ccp = "Busy"; break;
	case 0x10: ccp = "Intermediate"; break;
	case 0x14: ccp = "Intermediate-Condition Met"; break;
	case 0x18: ccp = "Reservation Conflict"; break;
	case 0x22: ccp = "Command Terminated"; break;	/* obsolete */
	case 0x28: ccp = "Task set Full"; break;	/* was: Queue Full */
	case 0x30: ccp = "ACA Active"; break;
	case 0x40: ccp = "Task Aborted"; break;
	default:   ccp = "Unknown status";
	}
	printk("%s", ccp);
#else
	printk("0x%0x", scsi_status);
#endif
}

#if (CONSTANTS & CONST_XSENSE)

struct error_info {
	unsigned short code12;	/* 0x0302 looks better than 0x03,0x02 */
	const char * text;
};

static struct error_info additional[] =
{
	{0x0000, "No additional sense information"},
	{0x0001, "Filemark detected"},
	{0x0002, "End-of-partition/medium detected"},
	{0x0003, "Setmark detected"},
	{0x0004, "Beginning-of-partition/medium detected"},
	{0x0005, "End-of-data detected"},
	{0x0006, "I/O process terminated"},
	{0x0011, "Audio play operation in progress"},
	{0x0012, "Audio play operation paused"},
	{0x0013, "Audio play operation successfully completed"},
	{0x0014, "Audio play operation stopped due to error"},
	{0x0015, "No current audio status to return"},
	{0x0016, "Operation in progress"},
	{0x0017, "Cleaning requested"},
	{0x0018, "Erase operation in progress"},
	{0x0019, "Locate operation in progress"},
	{0x001A, "Rewind operation in progress"},
	{0x001B, "Set capacity operation in progress"},
	{0x001C, "Verify operation in progress"},

	{0x0100, "No index/sector signal"},

	{0x0200, "No seek complete"},

	{0x0300, "Peripheral device write fault"},
	{0x0301, "No write current"},
	{0x0302, "Excessive write errors"},

	{0x0400, "Logical unit not ready, cause not reportable"},
	{0x0401, "Logical unit is in process of becoming ready"},
	{0x0402, "Logical unit not ready, initializing cmd. required"},
	{0x0403, "Logical unit not ready, manual intervention required"},
	{0x0404, "Logical unit not ready, format in progress"},
	{0x0405, "Logical unit not ready, rebuild in progress"},
	{0x0406, "Logical unit not ready, recalculation in progress"},
	{0x0407, "Logical unit not ready, operation in progress"},
	{0x0408, "Logical unit not ready, long write in progress"},
	{0x0409, "Logical unit not ready, self-test in progress"},
	{0x040A, "Logical unit not accessible, asymmetric access state "
	 "transition"},
	{0x040B, "Logical unit not accessible, target port in standby state"},
	{0x040C, "Logical unit not accessible, target port in unavailable "
	 "state"},
	{0x0410, "Logical unit not ready, auxiliary memory not accessible"},

	{0x0500, "Logical unit does not respond to selection"},

	{0x0600, "No reference position found"},

	{0x0700, "Multiple peripheral devices selected"},

	{0x0800, "Logical unit communication failure"},
	{0x0801, "Logical unit communication time-out"},
	{0x0802, "Logical unit communication parity error"},
	{0x0803, "Logical unit communication CRC error (Ultra-DMA/32)"},
	{0x0804, "Unreachable copy target"},

	{0x0900, "Track following error"},
	{0x0901, "Tracking servo failure"},
	{0x0902, "Focus servo failure"},
	{0x0903, "Spindle servo failure"},
	{0x0904, "Head select fault"},

	{0x0A00, "Error log overflow"},

	{0x0B00, "Warning"},
	{0x0B01, "Warning - specified temperature exceeded"},
	{0x0B02, "Warning - enclosure degraded"},

	{0x0C00, "Write error"},
	{0x0C01, "Write error - recovered with auto reallocation"},
	{0x0C02, "Write error - auto reallocation failed"},
	{0x0C03, "Write error - recommend reassignment"},
	{0x0C04, "Compression check miscompare error"},
	{0x0C05, "Data expansion occurred during compression"},
	{0x0C06, "Block not compressible"},
	{0x0C07, "Write error - recovery needed"},
	{0x0C08, "Write error - recovery failed"},
	{0x0C09, "Write error - loss of streaming"},
	{0x0C0A, "Write error - padding blocks added"},
	{0x0C0B, "Auxiliary memory write error"},
	{0x0C0C, "Write error - unexpected unsolicited data"},
	{0x0C0D, "Write error - not enough unsolicited data"},

	{0x0D00, "Error detected by third party temporary initiator"},
	{0x0D01, "Third party device failure"},
	{0x0D02, "Copy target device not reachable"},
	{0x0D03, "Incorrect copy target device type"},
	{0x0D04, "Copy target device data underrun"},
	{0x0D05, "Copy target device data overrun"},

	{0x1000, "Id CRC or ECC error"},

	{0x1100, "Unrecovered read error"},
	{0x1101, "Read retries exhausted"},
	{0x1102, "Error too long to correct"},
	{0x1103, "Multiple read errors"},
	{0x1104, "Unrecovered read error - auto reallocate failed"},
	{0x1105, "L-EC uncorrectable error"},
	{0x1106, "CIRC unrecovered error"},
	{0x1107, "Data re-synchronization error"},
	{0x1108, "Incomplete block read"},
	{0x1109, "No gap found"},
	{0x110A, "Miscorrected error"},
	{0x110B, "Unrecovered read error - recommend reassignment"},
	{0x110C, "Unrecovered read error - recommend rewrite the data"},
	{0x110D, "De-compression CRC error"},
	{0x110E, "Cannot decompress using declared algorithm"},
	{0x110F, "Error reading UPC/EAN number"},
	{0x1110, "Error reading ISRC number"},
	{0x1111, "Read error - loss of streaming"},
	{0x1112, "Auxiliary memory read error"},
	{0x1113, "Read error - failed retransmission request"},

	{0x1200, "Address mark not found for id field"},

	{0x1300, "Address mark not found for data field"},

	{0x1400, "Recorded entity not found"},
	{0x1401, "Record not found"},
	{0x1402, "Filemark or setmark not found"},
	{0x1403, "End-of-data not found"},
	{0x1404, "Block sequence error"},
	{0x1405, "Record not found - recommend reassignment"},
	{0x1406, "Record not found - data auto-reallocated"},
	{0x1407, "Locate operation failure"},

	{0x1500, "Random positioning error"},
	{0x1501, "Mechanical positioning error"},
	{0x1502, "Positioning error detected by read of medium"},

	{0x1600, "Data synchronization mark error"},
	{0x1601, "Data sync error - data rewritten"},
	{0x1602, "Data sync error - recommend rewrite"},
	{0x1603, "Data sync error - data auto-reallocated"},
	{0x1604, "Data sync error - recommend reassignment"},

	{0x1700, "Recovered data with no error correction applied"},
	{0x1701, "Recovered data with retries"},
	{0x1702, "Recovered data with positive head offset"},
	{0x1703, "Recovered data with negative head offset"},
	{0x1704, "Recovered data with retries and/or circ applied"},
	{0x1705, "Recovered data using previous sector id"},
	{0x1706, "Recovered data without ECC - data auto-reallocated"},
	{0x1707, "Recovered data without ECC - recommend reassignment"},
	{0x1708, "Recovered data without ECC - recommend rewrite"},
	{0x1709, "Recovered data without ECC - data rewritten"},

	{0x1800, "Recovered data with error correction applied"},
	{0x1801, "Recovered data with error corr. & retries applied"},
	{0x1802, "Recovered data - data auto-reallocated"},
	{0x1803, "Recovered data with CIRC"},
	{0x1804, "Recovered data with L-EC"},
	{0x1805, "Recovered data - recommend reassignment"},
	{0x1806, "Recovered data - recommend rewrite"},
	{0x1807, "Recovered data with ECC - data rewritten"},
	{0x1808, "Recovered data with linking"},

	{0x1900, "Defect list error"},
	{0x1901, "Defect list not available"},
	{0x1902, "Defect list error in primary list"},
	{0x1903, "Defect list error in grown list"},

	{0x1A00, "Parameter list length error"},

	{0x1B00, "Synchronous data transfer error"},

	{0x1C00, "Defect list not found"},
	{0x1C01, "Primary defect list not found"},
	{0x1C02, "Grown defect list not found"},

	{0x1D00, "Miscompare during verify operation"},

	{0x1E00, "Recovered id with ECC correction"},

	{0x1F00, "Partial defect list transfer"},

	{0x2000, "Invalid command operation code"},
	{0x2001, "Access denied - initiator pending-enrolled"},
	{0x2002, "Access denied - no access rights"},
	{0x2003, "Access denied - invalid mgmt id key"},
	{0x2004, "Illegal command while in write capable state"},
	{0x2005, "Obsolete"},
	{0x2006, "Illegal command while in explicit address mode"},
	{0x2007, "Illegal command while in implicit address mode"},
	{0x2008, "Access denied - enrollment conflict"},
	{0x2009, "Access denied - invalid LU identifier"},
	{0x200A, "Access denied - invalid proxy token"},
	{0x200B, "Access denied - ACL LUN conflict"},

	{0x2100, "Logical block address out of range"},
	{0x2101, "Invalid element address"},
	{0x2102, "Invalid address for write"},

	{0x2200, "Illegal function (use 20 00, 24 00, or 26 00)"},

	{0x2400, "Invalid field in cdb"},
	{0x2401, "CDB decryption error"},
	{0x2402, "Obsolete"},
	{0x2403, "Obsolete"},

	{0x2500, "Logical unit not supported"},

	{0x2600, "Invalid field in parameter list"},
	{0x2601, "Parameter not supported"},
	{0x2602, "Parameter value invalid"},
	{0x2603, "Threshold parameters not supported"},
	{0x2604, "Invalid release of persistent reservation"},
	{0x2605, "Data decryption error"},
	{0x2606, "Too many target descriptors"},
	{0x2607, "Unsupported target descriptor type code"},
	{0x2608, "Too many segment descriptors"},
	{0x2609, "Unsupported segment descriptor type code"},
	{0x260A, "Unexpected inexact segment"},
	{0x260B, "Inline data length exceeded"},
	{0x260C, "Invalid operation for copy source or destination"},
	{0x260D, "Copy segment granularity violation"},

	{0x2700, "Write protected"},
	{0x2701, "Hardware write protected"},
	{0x2702, "Logical unit software write protected"},
	{0x2703, "Associated write protect"},
	{0x2704, "Persistent write protect"},
	{0x2705, "Permanent write protect"},
	{0x2706, "Conditional write protect"},

	{0x2800, "Not ready to ready change, medium may have changed"},
	{0x2801, "Import or export element accessed"},

	{0x2900, "Power on, reset, or bus device reset occurred"},
	{0x2901, "Power on occurred"},
	{0x2902, "Scsi bus reset occurred"},
	{0x2903, "Bus device reset function occurred"},
	{0x2904, "Device internal reset"},
	{0x2905, "Transceiver mode changed to single-ended"},
	{0x2906, "Transceiver mode changed to lvd"},
	{0x2907, "I_T nexus loss occurred"},

	{0x2A00, "Parameters changed"},
	{0x2A01, "Mode parameters changed"},
	{0x2A02, "Log parameters changed"},
	{0x2A03, "Reservations preempted"},
	{0x2A04, "Reservations released"},
	{0x2A05, "Registrations preempted"},
	{0x2A06, "Asymmetric access state changed"},
	{0x2A07, "Implicit asymmetric access state transition failed"},

	{0x2B00, "Copy cannot execute since host cannot disconnect"},

	{0x2C00, "Command sequence error"},
	{0x2C01, "Too many windows specified"},
	{0x2C02, "Invalid combination of windows specified"},
	{0x2C03, "Current program area is not empty"},
	{0x2C04, "Current program area is empty"},
	{0x2C05, "Illegal power condition request"},
	{0x2C06, "Persistent prevent conflict"},
	{0x2C07, "Previous busy status"},
	{0x2C08, "Previous task set full status"},
	{0x2C09, "Previous reservation conflict status"},

	{0x2D00, "Overwrite error on update in place"},

	{0x2E00, "Insufficient time for operation"},

	{0x2F00, "Commands cleared by another initiator"},

	{0x3000, "Incompatible medium installed"},
	{0x3001, "Cannot read medium - unknown format"},
	{0x3002, "Cannot read medium - incompatible format"},
	{0x3003, "Cleaning cartridge installed"},
	{0x3004, "Cannot write medium - unknown format"},
	{0x3005, "Cannot write medium - incompatible format"},
	{0x3006, "Cannot format medium - incompatible medium"},
	{0x3007, "Cleaning failure"},
	{0x3008, "Cannot write - application code mismatch"},
	{0x3009, "Current session not fixated for append"},
	{0x3010, "Medium not formatted"},

	{0x3100, "Medium format corrupted"},
	{0x3101, "Format command failed"},
	{0x3102, "Zoned formatting failed due to spare linking"},

	{0x3200, "No defect spare location available"},
	{0x3201, "Defect list update failure"},

	{0x3300, "Tape length error"},

	{0x3400, "Enclosure failure"},

	{0x3500, "Enclosure services failure"},
	{0x3501, "Unsupported enclosure function"},
	{0x3502, "Enclosure services unavailable"},
	{0x3503, "Enclosure services transfer failure"},
	{0x3504, "Enclosure services transfer refused"},

	{0x3600, "Ribbon, ink, or toner failure"},

	{0x3700, "Rounded parameter"},

	{0x3800, "Event status notification"},
	{0x3802, "Esn - power management class event"},
	{0x3804, "Esn - media class event"},
	{0x3806, "Esn - device busy class event"},

	{0x3900, "Saving parameters not supported"},

	{0x3A00, "Medium not present"},
	{0x3A01, "Medium not present - tray closed"},
	{0x3A02, "Medium not present - tray open"},
	{0x3A03, "Medium not present - loadable"},
	{0x3A04, "Medium not present - medium auxiliary memory accessible"},

	{0x3B00, "Sequential positioning error"},
	{0x3B01, "Tape position error at beginning-of-medium"},
	{0x3B02, "Tape position error at end-of-medium"},
	{0x3B03, "Tape or electronic vertical forms unit not ready"},
	{0x3B04, "Slew failure"},
	{0x3B05, "Paper jam"},
	{0x3B06, "Failed to sense top-of-form"},
	{0x3B07, "Failed to sense bottom-of-form"},
	{0x3B08, "Reposition error"},
	{0x3B09, "Read past end of medium"},
	{0x3B0A, "Read past beginning of medium"},
	{0x3B0B, "Position past end of medium"},
	{0x3B0C, "Position past beginning of medium"},
	{0x3B0D, "Medium destination element full"},
	{0x3B0E, "Medium source element empty"},
	{0x3B0F, "End of medium reached"},
	{0x3B11, "Medium magazine not accessible"},
	{0x3B12, "Medium magazine removed"},
	{0x3B13, "Medium magazine inserted"},
	{0x3B14, "Medium magazine locked"},
	{0x3B15, "Medium magazine unlocked"},
	{0x3B16, "Mechanical positioning or changer error"},

	{0x3D00, "Invalid bits in identify message"},

	{0x3E00, "Logical unit has not self-configured yet"},
	{0x3E01, "Logical unit failure"},
	{0x3E02, "Timeout on logical unit"},
	{0x3E03, "Logical unit failed self-test"},
	{0x3E04, "Logical unit unable to update self-test log"},

	{0x3F00, "Target operating conditions have changed"},
	{0x3F01, "Microcode has been changed"},
	{0x3F02, "Changed operating definition"},
	{0x3F03, "Inquiry data has changed"},
	{0x3F04, "Component device attached"},
	{0x3F05, "Device identifier changed"},
	{0x3F06, "Redundancy group created or modified"},
	{0x3F07, "Redundancy group deleted"},
	{0x3F08, "Spare created or modified"},
	{0x3F09, "Spare deleted"},
	{0x3F0A, "Volume set created or modified"},
	{0x3F0B, "Volume set deleted"},
	{0x3F0C, "Volume set deassigned"},
	{0x3F0D, "Volume set reassigned"},
	{0x3F0E, "Reported luns data has changed"},
	{0x3F0F, "Echo buffer overwritten"},
	{0x3F10, "Medium loadable"},
	{0x3F11, "Medium auxiliary memory accessible"},

#if 0
	{0x40NN, "Ram failure"},
	{0x40NN, "Diagnostic failure on component nn"},
	{0x41NN, "Data path failure"},
	{0x42NN, "Power-on or self-test failure"},
#endif

	{0x4300, "Message error"},

	{0x4400, "Internal target failure"},

	{0x4500, "Select or reselect failure"},

	{0x4600, "Unsuccessful soft reset"},

	{0x4700, "Scsi parity error"},
	{0x4701, "Data phase CRC error detected"},
	{0x4702, "Scsi parity error detected during st data phase"},
	{0x4703, "Information unit CRC error detected"},
	{0x4704, "Asynchronous information protection error detected"},
	{0x4705, "Protocol service CRC error"},

	{0x4800, "Initiator detected error message received"},

	{0x4900, "Invalid message error"},

	{0x4A00, "Command phase error"},

	{0x4B00, "Data phase error"},

	{0x4C00, "Logical unit failed self-configuration"},

#if 0
	{0x4DNN, "Tagged overlapped commands (nn = queue tag)"},
#endif

	{0x4E00, "Overlapped commands attempted"},

	{0x5000, "Write append error"},
	{0x5001, "Write append position error"},
	{0x5002, "Position error related to timing"},

	{0x5100, "Erase failure"},
	{0x5101, "Erase failure - incomplete erase operation detected"},

	{0x5200, "Cartridge fault"},

	{0x5300, "Media load or eject failed"},
	{0x5301, "Unload tape failure"},
	{0x5302, "Medium removal prevented"},

	{0x5400, "Scsi to host system interface failure"},

	{0x5500, "System resource failure"},
	{0x5501, "System buffer full"},
	{0x5502, "Insufficient reservation resources"},
	{0x5503, "Insufficient resources"},
	{0x5504, "Insufficient registration resources"},
	{0x5505, "Insufficient access control resources"},
	{0x5506, "Auxiliary memory out of space"},

	{0x5700, "Unable to recover table-of-contents"},

	{0x5800, "Generation does not exist"},

	{0x5900, "Updated block read"},

	{0x5A00, "Operator request or state change input"},
	{0x5A01, "Operator medium removal request"},
	{0x5A02, "Operator selected write protect"},
	{0x5A03, "Operator selected write permit"},

	{0x5B00, "Log exception"},
	{0x5B01, "Threshold condition met"},
	{0x5B02, "Log counter at maximum"},
	{0x5B03, "Log list codes exhausted"},

	{0x5C00, "Rpl status change"},
	{0x5C01, "Spindles synchronized"},
	{0x5C02, "Spindles not synchronized"},

	{0x5D00, "Failure prediction threshold exceeded"},
	{0x5D01, "Media failure prediction threshold exceeded"},
	{0x5D02, "Logical unit failure prediction threshold exceeded"},
	{0x5D03, "Spare area exhaustion prediction threshold exceeded"},
	{0x5D10, "Hardware impending failure general hard drive failure"},
	{0x5D11, "Hardware impending failure drive error rate too high"},
	{0x5D12, "Hardware impending failure data error rate too high"},
	{0x5D13, "Hardware impending failure seek error rate too high"},
	{0x5D14, "Hardware impending failure too many block reassigns"},
	{0x5D15, "Hardware impending failure access times too high"},
	{0x5D16, "Hardware impending failure start unit times too high"},
	{0x5D17, "Hardware impending failure channel parametrics"},
	{0x5D18, "Hardware impending failure controller detected"},
	{0x5D19, "Hardware impending failure throughput performance"},
	{0x5D1A, "Hardware impending failure seek time performance"},
	{0x5D1B, "Hardware impending failure spin-up retry count"},
	{0x5D1C, "Hardware impending failure drive calibration retry count"},
	{0x5D20, "Controller impending failure general hard drive failure"},
	{0x5D21, "Controller impending failure drive error rate too high"},
	{0x5D22, "Controller impending failure data error rate too high"},
	{0x5D23, "Controller impending failure seek error rate too high"},
	{0x5D24, "Controller impending failure too many block reassigns"},
	{0x5D25, "Controller impending failure access times too high"},
	{0x5D26, "Controller impending failure start unit times too high"},
	{0x5D27, "Controller impending failure channel parametrics"},
	{0x5D28, "Controller impending failure controller detected"},
	{0x5D29, "Controller impending failure throughput performance"},
	{0x5D2A, "Controller impending failure seek time performance"},
	{0x5D2B, "Controller impending failure spin-up retry count"},
	{0x5D2C, "Controller impending failure drive calibration retry count"},
	{0x5D30, "Data channel impending failure general hard drive failure"},
	{0x5D31, "Data channel impending failure drive error rate too high"},
	{0x5D32, "Data channel impending failure data error rate too high"},
	{0x5D33, "Data channel impending failure seek error rate too high"},
	{0x5D34, "Data channel impending failure too many block reassigns"},
	{0x5D35, "Data channel impending failure access times too high"},
	{0x5D36, "Data channel impending failure start unit times too high"},
	{0x5D37, "Data channel impending failure channel parametrics"},
	{0x5D38, "Data channel impending failure controller detected"},
	{0x5D39, "Data channel impending failure throughput performance"},
	{0x5D3A, "Data channel impending failure seek time performance"},
	{0x5D3B, "Data channel impending failure spin-up retry count"},
	{0x5D3C, "Data channel impending failure drive calibration retry "
	 "count"},
	{0x5D40, "Servo impending failure general hard drive failure"},
	{0x5D41, "Servo impending failure drive error rate too high"},
	{0x5D42, "Servo impending failure data error rate too high"},
	{0x5D43, "Servo impending failure seek error rate too high"},
	{0x5D44, "Servo impending failure too many block reassigns"},
	{0x5D45, "Servo impending failure access times too high"},
	{0x5D46, "Servo impending failure start unit times too high"},
	{0x5D47, "Servo impending failure channel parametrics"},
	{0x5D48, "Servo impending failure controller detected"},
	{0x5D49, "Servo impending failure throughput performance"},
	{0x5D4A, "Servo impending failure seek time performance"},
	{0x5D4B, "Servo impending failure spin-up retry count"},
	{0x5D4C, "Servo impending failure drive calibration retry count"},
	{0x5D50, "Spindle impending failure general hard drive failure"},
	{0x5D51, "Spindle impending failure drive error rate too high"},
	{0x5D52, "Spindle impending failure data error rate too high"},
	{0x5D53, "Spindle impending failure seek error rate too high"},
	{0x5D54, "Spindle impending failure too many block reassigns"},
	{0x5D55, "Spindle impending failure access times too high"},
	{0x5D56, "Spindle impending failure start unit times too high"},
	{0x5D57, "Spindle impending failure channel parametrics"},
	{0x5D58, "Spindle impending failure controller detected"},
	{0x5D59, "Spindle impending failure throughput performance"},
	{0x5D5A, "Spindle impending failure seek time performance"},
	{0x5D5B, "Spindle impending failure spin-up retry count"},
	{0x5D5C, "Spindle impending failure drive calibration retry count"},
	{0x5D60, "Firmware impending failure general hard drive failure"},
	{0x5D61, "Firmware impending failure drive error rate too high"},
	{0x5D62, "Firmware impending failure data error rate too high"},
	{0x5D63, "Firmware impending failure seek error rate too high"},
	{0x5D64, "Firmware impending failure too many block reassigns"},
	{0x5D65, "Firmware impending failure access times too high"},
	{0x5D66, "Firmware impending failure start unit times too high"},
	{0x5D67, "Firmware impending failure channel parametrics"},
	{0x5D68, "Firmware impending failure controller detected"},
	{0x5D69, "Firmware impending failure throughput performance"},
	{0x5D6A, "Firmware impending failure seek time performance"},
	{0x5D6B, "Firmware impending failure spin-up retry count"},
	{0x5D6C, "Firmware impending failure drive calibration retry count"},
	{0x5DFF, "Failure prediction threshold exceeded (false)"},

	{0x5E00, "Low power condition on"},
	{0x5E01, "Idle condition activated by timer"},
	{0x5E02, "Standby condition activated by timer"},
	{0x5E03, "Idle condition activated by command"},
	{0x5E04, "Standby condition activated by command"},
	{0x5E41, "Power state change to active"},
	{0x5E42, "Power state change to idle"},
	{0x5E43, "Power state change to standby"},
	{0x5E45, "Power state change to sleep"},
	{0x5E47, "Power state change to device control"},

	{0x6000, "Lamp failure"},

	{0x6100, "Video acquisition error"},
	{0x6101, "Unable to acquire video"},
	{0x6102, "Out of focus"},

	{0x6200, "Scan head positioning error"},

	{0x6300, "End of user area encountered on this track"},
	{0x6301, "Packet does not fit in available space"},

	{0x6400, "Illegal mode for this track"},
	{0x6401, "Invalid packet size"},

	{0x6500, "Voltage fault"},

	{0x6600, "Automatic document feeder cover up"},
	{0x6601, "Automatic document feeder lift up"},
	{0x6602, "Document jam in automatic document feeder"},
	{0x6603, "Document miss feed automatic in document feeder"},

	{0x6700, "Configuration failure"},
	{0x6701, "Configuration of incapable logical units failed"},
	{0x6702, "Add logical unit failed"},
	{0x6703, "Modification of logical unit failed"},
	{0x6704, "Exchange of logical unit failed"},
	{0x6705, "Remove of logical unit failed"},
	{0x6706, "Attachment of logical unit failed"},
	{0x6707, "Creation of logical unit failed"},
	{0x6708, "Assign failure occurred"},
	{0x6709, "Multiply assigned logical unit"},
	{0x670A, "Set target port groups command failed"},

	{0x6800, "Logical unit not configured"},

	{0x6900, "Data loss on logical unit"},
	{0x6901, "Multiple logical unit failures"},
	{0x6902, "Parity/data mismatch"},

	{0x6A00, "Informational, refer to log"},

	{0x6B00, "State change has occurred"},
	{0x6B01, "Redundancy level got better"},
	{0x6B02, "Redundancy level got worse"},

	{0x6C00, "Rebuild failure occurred"},

	{0x6D00, "Recalculate failure occurred"},

	{0x6E00, "Command to logical unit failed"},

	{0x6F00, "Copy protection key exchange failure - authentication "
	 "failure"},
	{0x6F01, "Copy protection key exchange failure - key not present"},
	{0x6F02, "Copy protection key exchange failure - key not established"},
	{0x6F03, "Read of scrambled sector without authentication"},
	{0x6F04, "Media region code is mismatched to logical unit region"},
	{0x6F05, "Drive region must be permanent/region reset count error"},

#if 0
	{0x70NN, "Decompression exception short algorithm id of nn"},
#endif

	{0x7100, "Decompression exception long algorithm id"},

	{0x7200, "Session fixation error"},
	{0x7201, "Session fixation error writing lead-in"},
	{0x7202, "Session fixation error writing lead-out"},
	{0x7203, "Session fixation error - incomplete track in session"},
	{0x7204, "Empty or partially written reserved track"},
	{0x7205, "No more track reservations allowed"},

	{0x7300, "Cd control error"},
	{0x7301, "Power calibration area almost full"},
	{0x7302, "Power calibration area is full"},
	{0x7303, "Power calibration area error"},
	{0x7304, "Program memory area update failure"},
	{0x7305, "Program memory area is full"},
	{0x7306, "RMA/PMA is almost full"},
	{0, NULL}
};

struct error_info2 {
	unsigned char code1, code2_min, code2_max;
	const char * fmt;
};

static struct error_info2 additional2[] =
{
	{0x40,0x00,0x7f,"Ram failure (%x)"},
	{0x40,0x80,0xff,"Diagnostic failure on component (%x)"},
	{0x41,0x00,0xff,"Data path failure (%x)"},
	{0x42,0x00,0xff,"Power-on or self-test failure (%x)"},
	{0x4D,0x00,0xff,"Tagged overlapped commands (queue tag %x)"},
	{0x70,0x00,0xff,"Decompression exception short algorithm id of %x"},
	{0, 0, 0, NULL}
};
#endif

#if (CONSTANTS & CONST_SENSE)
/* description of the sense key values */
static const char *snstext[] = {
	"No Sense",	    /* 0: There is no sense information */
	"Recovered Error",  /* 1: The last command completed successfully
				  but used error correction */
	"Not Ready",	    /* 2: The addressed target is not ready */
	"Medium Error",	    /* 3: Data error detected on the medium */
	"Hardware Error",   /* 4: Controller or device failure */
	"Illegal Request",  /* 5: Error in request */
	"Unit Attention",   /* 6: Removable medium was changed, or
				  the target has been reset */
	"Data Protect",	    /* 7: Access to the data is blocked */
	"Blank Check",	    /* 8: Reached unexpected written or unwritten
				  region of the medium */
	"Vendor Specific",  /* 9: Vendor specific */
	"Copy Aborted",	    /* A: COPY or COMPARE was aborted */
	"Aborted Command",  /* B: The target aborted the command */
	"Equal",	    /* C: A SEARCH DATA command found data equal */
	"Volume Overflow",  /* D: Medium full with still data to be written */
	"Miscompare",	    /* E: Source data and data on the medium
				  do not agree */
};
#endif

/* Get sense key string or NULL if not available */
const char *
scsi_sense_key_string(unsigned char key) {
#if (CONSTANTS & CONST_SENSE)
	if (key <= 0xE)
		return snstext[key];
#endif
	return NULL;
}

/*
 * Get extended sense key string or NULL if not available.
 * This string may contain a %x and must be printed with ascq as arg.
 */
const char *
scsi_extd_sense_format(unsigned char asc, unsigned char ascq) {
#if (CONSTANTS & CONST_XSENSE)
	int i;
	unsigned short code = ((asc << 8) | ascq);

	for (i=0; additional[i].text; i++)
		if (additional[i].code12 == code)
			return additional[i].text;
	for (i=0; additional2[i].fmt; i++)
		if (additional2[i].code1 == asc &&
		    additional2[i].code2_min >= ascq &&
		    additional2[i].code2_max <= ascq)
			return additional2[i].fmt;
#endif
	return NULL;
}

/* Print extended sense information */
static void
scsi_show_extd_sense(unsigned char asc, unsigned char ascq) {
	const char *extd_sense_fmt = scsi_extd_sense_format(asc, ascq);

	if (extd_sense_fmt) {
		printk("Additional sense: ");
		printk(extd_sense_fmt, ascq);
		printk("\n");
	} else {
		printk("ASC=%2x ASCQ=%2x\n", asc, ascq);
	}
}

/* Print sense information */
static void
print_sense_internal(const char *devclass, 
		     const unsigned char *sense_buffer,
		     struct request *req)
{
	int s, sense_class, valid, code, info;
	const char *error = NULL;
	unsigned char asc, ascq;
	const char *sense_txt;
	const char *name = req->rq_disk ? req->rq_disk->disk_name : devclass;
    
	sense_class = (sense_buffer[0] >> 4) & 0x07;
	code = sense_buffer[0] & 0xf;
	valid = sense_buffer[0] & 0x80;
    
	if (sense_class == 7) {	/* extended sense data */
		s = sense_buffer[7] + 8;
		if (s > SCSI_SENSE_BUFFERSIZE)
			s = SCSI_SENSE_BUFFERSIZE;
	
		info = ((sense_buffer[3] << 24) | (sense_buffer[4] << 16) |
			(sense_buffer[5] << 8) | sense_buffer[6]);
		if (info || valid) {
			printk("Info fld=0x%x", info);
			if (!valid)	/* info data not according to standard */
				printk(" (nonstd)");
			printk(", ");
		}
		if (sense_buffer[2] & 0x80)
			printk( "FMK ");	/* current command has read a filemark */
		if (sense_buffer[2] & 0x40)
			printk( "EOM ");	/* end-of-medium condition exists */
		if (sense_buffer[2] & 0x20)
			printk( "ILI ");	/* incorrect block length requested */
	
		switch (code) {
		case 0x0:
			error = "Current";	/* error concerns current command */
			break;
		case 0x1:
			error = "Deferred";	/* error concerns some earlier command */
			/* e.g., an earlier write to disk cache succeeded, but
			   now the disk discovers that it cannot write the data */
			break;
		default:
			error = "Invalid";
		}

		printk("%s ", error);

		sense_txt = scsi_sense_key_string(sense_buffer[2]);
		if (sense_txt)
			printk("%s: sense key %s\n", name, sense_txt);
		else
			printk("%s: sense = %2x %2x\n", name,
			       sense_buffer[0], sense_buffer[2]);

		asc = ascq = 0;
		if (sense_buffer[7] + 7 >= 13) {
			asc = sense_buffer[12];
			ascq = sense_buffer[13];
		}
		if (asc || ascq)
			scsi_show_extd_sense(asc, ascq);

	} else {	/* non-extended sense data */

		/*
		 * Standard says:
		 *    sense_buffer[0] & 0200 : address valid
		 *    sense_buffer[0] & 0177 : vendor-specific error code
		 *    sense_buffer[1] & 0340 : vendor-specific
		 *    sense_buffer[1..3] : 21-bit logical block address
		 */

		sense_txt = scsi_sense_key_string(sense_buffer[0]);
		if (sense_txt)
			printk("%s: old sense key %s\n", name, sense_txt);
		else
			printk("%s: sense = %2x %2x\n", name,
			       sense_buffer[0], sense_buffer[2]);

		printk("Non-extended sense class %d code 0x%0x\n",
		       sense_class, code);
		s = 4;
	}
    
#if !(CONSTANTS & CONST_SENSE)
	{
		int i;
	printk("Raw sense data:");
	for (i = 0; i < s; ++i) 
		printk("0x%02x ", sense_buffer[i]);
	printk("\n");
	}
#endif
}

void print_sense(const char *devclass, struct scsi_cmnd *cmd)
{
	print_sense_internal(devclass, cmd->sense_buffer, cmd->request);
}

void print_req_sense(const char *devclass, struct scsi_request *sreq)
{
	print_sense_internal(devclass, sreq->sr_sense_buffer, sreq->sr_request);
}

#if (CONSTANTS & CONST_MSG) 
static const char *one_byte_msgs[] = {
/* 0x00 */ "Command Complete", NULL, "Save Pointers",
/* 0x03 */ "Restore Pointers", "Disconnect", "Initiator Error", 
/* 0x06 */ "Abort", "Message Reject", "Nop", "Message Parity Error",
/* 0x0a */ "Linked Command Complete", "Linked Command Complete w/flag",
/* 0x0c */ "Bus device reset", "Abort Tag", "Clear Queue", 
/* 0x0f */ "Initiate Recovery", "Release Recovery"
};

#define NO_ONE_BYTE_MSGS (sizeof(one_byte_msgs)  / sizeof (const char *))

static const char *two_byte_msgs[] = {
/* 0x20 */ "Simple Queue Tag", "Head of Queue Tag", "Ordered Queue Tag"
/* 0x23 */ "Ignore Wide Residue"
};

#define NO_TWO_BYTE_MSGS (sizeof(two_byte_msgs)  / sizeof (const char *))

static const char *extended_msgs[] = {
/* 0x00 */ "Modify Data Pointer", "Synchronous Data Transfer Request",
/* 0x02 */ "SCSI-I Extended Identify", "Wide Data Transfer Request"
};

#define NO_EXTENDED_MSGS (sizeof(two_byte_msgs)  / sizeof (const char *))
#endif /* (CONSTANTS & CONST_MSG) */

int print_msg (const unsigned char *msg) {
    int len = 0, i;
    if (msg[0] == EXTENDED_MESSAGE) {
	len = 3 + msg[1];
#if (CONSTANTS & CONST_MSG)
	if (msg[2] < NO_EXTENDED_MSGS)
	    printk ("%s ", extended_msgs[msg[2]]); 
	else 
	    printk ("Extended Message, reserved code (0x%02x) ", (int) msg[2]);
	switch (msg[2]) {
	case EXTENDED_MODIFY_DATA_POINTER:
	    printk("pointer = %d", (int) (msg[3] << 24) | (msg[4] << 16) | 
		   (msg[5] << 8) | msg[6]);
	    break;
	case EXTENDED_SDTR:
	    printk("period = %d ns, offset = %d", (int) msg[3] * 4, (int) 
		   msg[4]);
	    break;
	case EXTENDED_WDTR:
	    printk("width = 2^%d bytes", msg[3]);
	    break;
	default:
	    for (i = 2; i < len; ++i) 
		printk("%02x ", msg[i]);
	}
#else
	for (i = 0; i < len; ++i)
	    printk("%02x ", msg[i]);
#endif
	/* Identify */
    } else if (msg[0] & 0x80) {
#if (CONSTANTS & CONST_MSG)
	printk("Identify disconnect %sallowed %s %d ",
	       (msg[0] & 0x40) ? "" : "not ",
	       (msg[0] & 0x20) ? "target routine" : "lun",
	       msg[0] & 0x7);
#else
	printk("%02x ", msg[0]);
#endif
	len = 1;
	/* Normal One byte */
    } else if (msg[0] < 0x1f) {
#if (CONSTANTS & CONST_MSG)
	if (msg[0] < NO_ONE_BYTE_MSGS)
	    printk(one_byte_msgs[msg[0]]);
	else
	    printk("reserved (%02x) ", msg[0]);
#else
	printk("%02x ", msg[0]);
#endif
	len = 1;
	/* Two byte */
    } else if (msg[0] <= 0x2f) {
#if (CONSTANTS & CONST_MSG)
	if ((msg[0] - 0x20) < NO_TWO_BYTE_MSGS)
	    printk("%s %02x ", two_byte_msgs[msg[0] - 0x20], 
		   msg[1]);
	else 
	    printk("reserved two byte (%02x %02x) ", 
		   msg[0], msg[1]);
#else
	printk("%02x %02x", msg[0], msg[1]);
#endif
	len = 2;
    } else 
#if (CONSTANTS & CONST_MSG)
	printk(reserved);
#else
    printk("%02x ", msg[0]);
#endif
    return len;
}

void print_Scsi_Cmnd(struct scsi_cmnd *cmd) {
    printk("scsi%d : destination target %d, lun %d\n", 
	   cmd->device->host->host_no, 
	   cmd->device->id, 
	   cmd->device->lun);
    printk("        command = ");
    print_command(cmd->cmnd);
}

#if (CONSTANTS & CONST_HOST)
static const char * hostbyte_table[]={
"DID_OK", "DID_NO_CONNECT", "DID_BUS_BUSY", "DID_TIME_OUT", "DID_BAD_TARGET", 
"DID_ABORT", "DID_PARITY", "DID_ERROR", "DID_RESET", "DID_BAD_INTR",
"DID_PASSTHROUGH", "DID_SOFT_ERROR", NULL};

void print_hostbyte(int scsiresult)
{   static int maxcode=0;
    int i;
   
    if(!maxcode) {
	for(i=0;hostbyte_table[i];i++) ;
	maxcode=i-1;
    }
    printk("Hostbyte=0x%02x",host_byte(scsiresult));
    if(host_byte(scsiresult)>maxcode) {
	printk("is invalid "); 
	return;
    }
    printk("(%s) ",hostbyte_table[host_byte(scsiresult)]);
}
#else
void print_hostbyte(int scsiresult)
{   printk("Hostbyte=0x%02x ",host_byte(scsiresult));
}
#endif

#if (CONSTANTS & CONST_DRIVER)
static const char * driverbyte_table[]={
"DRIVER_OK", "DRIVER_BUSY", "DRIVER_SOFT",  "DRIVER_MEDIA", "DRIVER_ERROR", 
"DRIVER_INVALID", "DRIVER_TIMEOUT", "DRIVER_HARD",NULL };

static const char * driversuggest_table[]={"SUGGEST_OK",
"SUGGEST_RETRY", "SUGGEST_ABORT", "SUGGEST_REMAP", "SUGGEST_DIE",
unknown,unknown,unknown, "SUGGEST_SENSE",NULL};


void print_driverbyte(int scsiresult)
{   static int driver_max=0,suggest_max=0;
    int i,dr=driver_byte(scsiresult)&DRIVER_MASK, 
	su=(driver_byte(scsiresult)&SUGGEST_MASK)>>4;

    if(!driver_max) {
        for(i=0;driverbyte_table[i];i++) ;
        driver_max=i;
	for(i=0;driversuggest_table[i];i++) ;
	suggest_max=i;
    }
    printk("Driverbyte=0x%02x",driver_byte(scsiresult));
    printk("(%s,%s) ",
	dr<driver_max  ? driverbyte_table[dr]:"invalid",
	su<suggest_max ? driversuggest_table[su]:"invalid");
}
#else
void print_driverbyte(int scsiresult)
{   printk("Driverbyte=0x%02x ",driver_byte(scsiresult));
}
#endif
