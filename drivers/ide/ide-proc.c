/*
 *  linux/drivers/ide/ide-proc.c	Version 1.03	January  2, 1998
 *
 *  Copyright (C) 1997-1998	Mark Lord
 */

/*
 * This is the /proc/ide/ filesystem implementation.
 *
 * The major reason this exists is to provide sufficient access
 * to driver and config data, such that user-mode programs can
 * be developed to handle chipset tuning for most PCI interfaces.
 * This should provide better utilities, and less kernel bloat.
 *
 * The entire pci config space for a PCI interface chipset can be
 * retrieved by just reading it.  e.g.    "cat /proc/ide3/config"
 *
 * To modify registers *safely*, do something like:
 *   echo "P40:88" >/proc/ide/ide3/config
 * That expression writes 0x88 to pci config register 0x40
 * on the chip which controls ide3.  Multiple tuples can be issued,
 * and the writes will be completed as an atomic set:
 *   echo "P40:88 P41:35 P42:00 P43:00" >/proc/ide/ide3/config
 *
 * All numbers must be specified using pairs of ascii hex digits.
 * It is important to note that these writes will be performed
 * after waiting for the IDE controller (both interfaces)
 * to be completely idle, to ensure no corruption of I/O in progress.
 *
 * Non-PCI registers can also be written, using "R" in place of "P"
 * in the above examples.  The size of the port transfer is determined
 * by the number of pairs of hex digits given for the data.  If a two
 * digit value is given, the write will be a byte operation; if four
 * digits are used, the write will be performed as a 16-bit operation;
 * and if eight digits are specified, a 32-bit "dword" write will be
 * performed.  Odd numbers of digits are not permitted.
 *
 * If there is an error *anywhere* in the string of registers/data
 * then *none* of the writes will be performed.
 *
 * Drive/Driver settings can be retrieved by reading the drive's
 * "settings" files.  e.g.    "cat /proc/ide0/hda/settings"
 * To write a new value "val" into a specific setting "name", use:
 *   echo "name:val" >/proc/ide/ide0/hda/settings
 *
 * Also useful, "cat /proc/ide0/hda/[identify, smart_values,
 * smart_thresholds, capabilities]" will issue an IDENTIFY /
 * PACKET_IDENTIFY / SMART_READ_VALUES / SMART_READ_THRESHOLDS /
 * SENSE CAPABILITIES command to /dev/hda, and then dump out the
 * returned data as 256 16-bit words.  The "hdparm" utility will
 * be updated someday soon to use this mechanism.
 *
 * Feel free to develop and distribute fancy GUI configuration
 * utilities for your favorite PCI chipsets.  I'll be working on
 * one for the Promise 20246 someday soon.  -ml
 *
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ctype.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifdef CONFIG_BLK_DEV_AEC62XX
extern byte aec62xx_proc;
int (*aec62xx_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_AEC62XX */
#ifdef CONFIG_BLK_DEV_ALI15X3
extern byte ali_proc;
int (*ali_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_ALI15X3 */
#ifdef CONFIG_BLK_DEV_AMD74XX
extern byte amd74xx_proc;
int (*amd74xx_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_AMD74XX */
#ifdef CONFIG_BLK_DEV_CMD64X
extern byte cmd64x_proc;
int (*cmd64x_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_CMD64X */
#ifdef CONFIG_BLK_DEV_CS5530
extern byte cs5530_proc;
int (*cs5530_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_CS5530 */
#ifdef CONFIG_BLK_DEV_HPT34X
extern byte hpt34x_proc;
int (*hpt34x_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_HPT34X */
#ifdef CONFIG_BLK_DEV_HPT366
extern byte hpt366_proc;
int (*hpt366_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_HPT366 */
#ifdef CONFIG_BLK_DEV_PDC202XX
extern byte pdc202xx_proc;
int (*pdc202xx_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_PDC202XX */
#ifdef CONFIG_BLK_DEV_PIIX
extern byte piix_proc;
int (*piix_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_PIIX */
#ifdef CONFIG_BLK_DEV_SVWKS
extern byte svwks_proc;
int (*svwks_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_SVWKS */
#ifdef CONFIG_BLK_DEV_SIS5513
extern byte sis_proc;
int (*sis_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_SIS5513 */
#ifdef CONFIG_BLK_DEV_VIA82CXXX
extern byte via_proc;
int (*via_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_VIA82CXXX */

static struct proc_dir_entry * proc_ide_root = NULL;

static int ide_getdigit(char c)
{
	int digit;
	if (isdigit(c))
		digit = c - '0';
	else
		digit = -1;
	return digit;
}

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = data;
	int		len;
	const char	*name;

	switch (hwif->chipset) {
		case ide_unknown:	name = "(none)";	break;
		case ide_generic:	name = "generic";	break;
		case ide_pci:		name = "pci";		break;
		case ide_cmd640:	name = "cmd640";	break;
		case ide_dtc2278:	name = "dtc2278";	break;
		case ide_ali14xx:	name = "ali14xx";	break;
		case ide_qd65xx:	name = "qd65xx";	break;
		case ide_umc8672:	name = "umc8672";	break;
		case ide_ht6560b:	name = "ht6560b";	break;
		case ide_pdc4030:	name = "pdc4030";	break;
		case ide_rz1000:	name = "rz1000";	break;
		case ide_trm290:	name = "trm290";	break;
		case ide_cmd646:	name = "cmd646";	break;
		case ide_cy82c693:	name = "cy82c693";	break;
		case ide_pmac:		name = "mac-io";	break;
		default:		name = "(unknown)";	break;
	}
	len = sprintf(page, "%s\n", name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_mate
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = data;
	int		len;

	if (hwif && hwif->mate && hwif->mate->present)
		len = sprintf(page, "%s\n", hwif->mate->name);
	else
		len = sprintf(page, "(none)\n");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_channel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = data;
	int		len;

	page[0] = hwif->channel ? '1' : '0';
	page[1] = '\n';
	len = 2;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_get_identify(ide_drive_t *drive, byte *buf)
{
	struct hd_drive_task_hdr taskfile;
	struct hd_drive_hob_hdr hobfile;
	memset(&taskfile, 0, sizeof(struct hd_drive_task_hdr));
	memset(&hobfile, 0, sizeof(struct hd_drive_hob_hdr));

	taskfile.sector_count = 0x01;
	taskfile.command = (drive->type == ATA_DISK) ? WIN_IDENTIFY : WIN_PIDENTIFY ;

	return ide_wait_taskfile(drive, &taskfile, &hobfile, buf);
}

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = data;
	int		len = 0, i = 0;

	if (drive && !proc_ide_get_identify(drive, page)) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	else
		len = sprintf(page, "\n");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_settings
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = data;
	ide_settings_t	*setting = drive->settings;
	char		*out = page;
	int		len, rc, mul_factor, div_factor;

	out += sprintf(out, "name\t\t\tvalue\t\tmin\t\tmax\t\tmode\n");
	out += sprintf(out, "----\t\t\t-----\t\t---\t\t---\t\t----\n");
	while(setting) {
		mul_factor = setting->mul_factor;
		div_factor = setting->div_factor;
		out += sprintf(out, "%-24s", setting->name);
		if ((rc = ide_read_setting(drive, setting)) >= 0)
			out += sprintf(out, "%-16d", rc * mul_factor / div_factor);
		else
			out += sprintf(out, "%-16s", "write-only");
		out += sprintf(out, "%-16d%-16d", (setting->min * mul_factor + div_factor - 1) / div_factor, setting->max * mul_factor / div_factor);
		if (setting->rw & SETTING_READ)
			out += sprintf(out, "r");
		if (setting->rw & SETTING_WRITE)
			out += sprintf(out, "w");
		out += sprintf(out, "\n");
		setting = setting->next;
	}
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

#define MAX_LEN	30

static int proc_ide_write_settings
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = data;
	char		name[MAX_LEN + 1];
	int		for_real = 0, len;
	unsigned long	n;
	const char	*start = NULL;
	ide_settings_t	*setting;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	/*
	 * Skip over leading whitespace
	 */
	while (count && isspace(*buffer)) {
		--count;
		++buffer;
	}
	/*
	 * Do one full pass to verify all parameters,
	 * then do another to actually write the new settings.
	 */
	do {
		const char *p;
		p = buffer;
		n = count;
		while (n > 0) {
			int d, digits;
			unsigned int val = 0;
			start = p;

			while (n > 0 && *p != ':') {
				--n;
				p++;
			}
			if (*p != ':')
				goto parse_error;
			len = min(p - start, MAX_LEN);
			strncpy(name, start, min(len, MAX_LEN));
			name[len] = 0;

			if (n > 0) {
				--n;
				p++;
			} else
				goto parse_error;

			digits = 0;
			while (n > 0 && (d = ide_getdigit(*p)) >= 0) {
				val = (val * 10) + d;
				--n;
				++p;
				++digits;
			}
			if (n > 0 && !isspace(*p))
				goto parse_error;
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}

			/* Find setting by name */
			setting = drive->settings;

			while (setting) {
			    if (strcmp(setting->name, name) == 0)
				break;
			    setting = setting->next;
			}
			if (!setting)
				goto parse_error;

			if (for_real)
				ide_write_setting(drive, setting, val * setting->div_factor / setting->mul_factor);
		}
	} while (!for_real++);
	return count;
parse_error:
	printk("proc_ide_write_settings(): parse error\n");
	return -EINVAL;
}

int proc_ide_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t *drive = data;
	struct ata_operations *driver = drive->driver;
	int len;

	if (!driver)
		len = sprintf(page, "(none)\n");
        else
		len = sprintf(page,"%llu\n", (unsigned long long) ata_capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = data;
	char		*out = page;
	int		len;

	out += sprintf(out,"physical     %d/%d/%d\n", drive->cyl, drive->head, drive->sect);
	out += sprintf(out,"logical      %d/%d/%d\n", drive->bios_cyl, drive->bios_head, drive->bios_sect);
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_dmodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = data;
	struct hd_driveid *id = drive->id;
	int		len;

	len = sprintf(page, "%.40s\n", (id && id->model[0]) ? (char *)id->model : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_media
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = data;
	const char	*type;
	int		len;

	switch (drive->type) {
		case ATA_DISK:	type = "disk\n";
				break;
		case ATA_ROM:	type = "cdrom\n";
				break;
		case ATA_TAPE:	type = "tape\n";
				break;
		case ATA_FLOPPY:type = "floppy\n";
				break;
		default:	type = "UNKNOWN\n";
				break;
	}
	strcpy(page,type);
	len = strlen(type);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t generic_drive_entries[] = {
	{ "identify",	S_IFREG|S_IRUSR,	proc_ide_read_identify,	NULL },
	{ "media",	S_IFREG|S_IRUGO,	proc_ide_read_media,	NULL },
	{ "model",	S_IFREG|S_IRUGO,	proc_ide_read_dmodel,	NULL },
	{ "settings",	S_IFREG|S_IRUSR|S_IWUSR,proc_ide_read_settings,	proc_ide_write_settings },
	{ NULL,	0, NULL, NULL }
};

void ide_add_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p, void *data)
{
	struct proc_dir_entry *ent;

	if (!dir || !p)
		return;
	while (p->name != NULL) {
		ent = create_proc_entry(p->name, p->mode, dir);
		if (!ent) return;
		ent->nlink = 1;
		ent->data = data;
		ent->read_proc = p->read_proc;
		ent->write_proc = p->write_proc;
		p++;
	}
}

void ide_remove_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p)
{
	if (!dir || !p)
		return;
	while (p->name != NULL) {
		remove_proc_entry(p->name, dir);
		p++;
	}
}

static void create_proc_ide_drives(ide_hwif_t *hwif)
{
	int	d;
	struct proc_dir_entry *ent;
	struct proc_dir_entry *parent = hwif->proc;
	char name[64];

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];
		struct ata_operations *driver = drive->driver;

		if (!drive->present)
			continue;
		if (drive->proc)
			continue;

		drive->proc = proc_mkdir(drive->name, parent);
		if (drive->proc) {
			ide_add_proc_entries(drive->proc, generic_drive_entries, drive);
			if (driver) {
				ide_add_proc_entries(drive->proc, generic_subdriver_entries, drive);
				ide_add_proc_entries(drive->proc, driver->proc, drive);
			}
		}
		sprintf(name,"ide%d/%s", (drive->name[2]-'a')/2, drive->name);
		ent = proc_symlink(drive->name, proc_ide_root, name);
		if (!ent) return;
	}
}

static void destroy_proc_ide_device(ide_hwif_t *hwif, ide_drive_t *drive)
{
	struct ata_operations *driver = drive->driver;

	if (drive->proc) {
		if (driver)
			ide_remove_proc_entries(drive->proc, driver->proc);
		ide_remove_proc_entries(drive->proc, generic_drive_entries);
		remove_proc_entry(drive->name, proc_ide_root);
		remove_proc_entry(drive->name, hwif->proc);
		drive->proc = NULL;
	}
}

void destroy_proc_ide_drives(ide_hwif_t *hwif)
{
	int	d;

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];

		if (drive->proc)
			destroy_proc_ide_device(hwif, drive);
	}
}

static ide_proc_entry_t hwif_entries[] = {
	{ "channel",	S_IFREG|S_IRUGO,	proc_ide_read_channel,	NULL },
	{ "mate",	S_IFREG|S_IRUGO,	proc_ide_read_mate,	NULL },
	{ "model",	S_IFREG|S_IRUGO,	proc_ide_read_imodel,	NULL },
	{ NULL,	0, NULL, NULL }
};

void create_proc_ide_interfaces(void)
{
	int	h;

	for (h = 0; h < MAX_HWIFS; h++) {
		ide_hwif_t *hwif = &ide_hwifs[h];

		if (!hwif->present)
			continue;
		if (!hwif->proc) {
			hwif->proc = proc_mkdir(hwif->name, proc_ide_root);
			if (!hwif->proc)
				return;
			ide_add_proc_entries(hwif->proc, hwif_entries, hwif);
		}
		create_proc_ide_drives(hwif);
	}
}

static void destroy_proc_ide_interfaces(void)
{
	int	h;

	for (h = 0; h < MAX_HWIFS; h++) {
		ide_hwif_t *hwif = &ide_hwifs[h];
		int exist = (hwif->proc != NULL);
#if 0
		if (!hwif->present)
			continue;
#endif
		if (exist) {
			destroy_proc_ide_drives(hwif);
			ide_remove_proc_entries(hwif->proc, hwif_entries);
			remove_proc_entry(hwif->name, proc_ide_root);
			hwif->proc = NULL;
		} else
			continue;
	}
}

void proc_ide_create(void)
{
	proc_ide_root = proc_mkdir("ide", 0);
	if (!proc_ide_root) return;

	create_proc_ide_interfaces();

#ifdef CONFIG_BLK_DEV_AEC62XX
	if ((aec62xx_display_info) && (aec62xx_proc))
		create_proc_info_entry("aec62xx", 0, proc_ide_root, aec62xx_display_info);
#endif /* CONFIG_BLK_DEV_AEC62XX */
#ifdef CONFIG_BLK_DEV_ALI15X3
	if ((ali_display_info) && (ali_proc))
		create_proc_info_entry("ali", 0, proc_ide_root, ali_display_info);
#endif /* CONFIG_BLK_DEV_ALI15X3 */
#ifdef CONFIG_BLK_DEV_AMD74XX
	if ((amd74xx_display_info) && (amd74xx_proc))
		create_proc_info_entry("amd74xx", 0, proc_ide_root, amd74xx_display_info);
#endif /* CONFIG_BLK_DEV_AMD74XX */
#ifdef CONFIG_BLK_DEV_CMD64X
	if ((cmd64x_display_info) && (cmd64x_proc))
		create_proc_info_entry("cmd64x", 0, proc_ide_root, cmd64x_display_info);
#endif /* CONFIG_BLK_DEV_CMD64X */
#ifdef CONFIG_BLK_DEV_CS5530
	if ((cs5530_display_info) && (cs5530_proc))
		create_proc_info_entry("cs5530", 0, proc_ide_root, cs5530_display_info);
#endif /* CONFIG_BLK_DEV_CS5530 */
#ifdef CONFIG_BLK_DEV_HPT34X
	if ((hpt34x_display_info) && (hpt34x_proc))
		create_proc_info_entry("hpt34x", 0, proc_ide_root, hpt34x_display_info);
#endif /* CONFIG_BLK_DEV_HPT34X */
#ifdef CONFIG_BLK_DEV_HPT366
	if ((hpt366_display_info) && (hpt366_proc))
		create_proc_info_entry("hpt366", 0, proc_ide_root, hpt366_display_info);
#endif /* CONFIG_BLK_DEV_HPT366 */
#ifdef CONFIG_BLK_DEV_SVWKS
	if ((svwks_display_info) && (svwks_proc))
		create_proc_info_entry("svwks", 0, proc_ide_root, svwks_display_info);
#endif /* CONFIG_BLK_DEV_SVWKS */
#ifdef CONFIG_BLK_DEV_PDC202XX
	if ((pdc202xx_display_info) && (pdc202xx_proc))
		create_proc_info_entry("pdc202xx", 0, proc_ide_root, pdc202xx_display_info);
#endif /* CONFIG_BLK_DEV_PDC202XX */
#ifdef CONFIG_BLK_DEV_PIIX
	if ((piix_display_info) && (piix_proc))
		create_proc_info_entry("piix", 0, proc_ide_root, piix_display_info);
#endif /* CONFIG_BLK_DEV_PIIX */
#ifdef CONFIG_BLK_DEV_SIS5513
	if ((sis_display_info) && (sis_proc))
		create_proc_info_entry("sis", 0, proc_ide_root, sis_display_info);
#endif /* CONFIG_BLK_DEV_SIS5513 */
#ifdef CONFIG_BLK_DEV_VIA82CXXX
	if ((via_display_info) && (via_proc))
		create_proc_info_entry("via", 0, proc_ide_root, via_display_info);
#endif /* CONFIG_BLK_DEV_VIA82CXXX */
}

void proc_ide_destroy(void)
{
	/*
	 * Mmmm.. does this free up all resources,
	 * or do we need to do a more proper cleanup here ??
	 */
#ifdef CONFIG_BLK_DEV_AEC62XX
	if ((aec62xx_display_info) && (aec62xx_proc))
		remove_proc_entry("ide/aec62xx",0);
#endif /* CONFIG_BLK_DEV_AEC62XX */
#ifdef CONFIG_BLK_DEV_ALI15X3
	if ((ali_display_info) && (ali_proc))
		remove_proc_entry("ide/ali",0);
#endif /* CONFIG_BLK_DEV_ALI15X3 */
#ifdef CONFIG_BLK_DEV_AMD74XX
	if ((amd74xx_display_info) && (amd74xx_proc))
		remove_proc_entry("ide/amd74xx",0);
#endif /* CONFIG_BLK_DEV_AMD74XX */
#ifdef CONFIG_BLK_DEV_CMD64X
	if ((cmd64x_display_info) && (cmd64x_proc))
		remove_proc_entry("ide/cmd64x",0);
#endif /* CONFIG_BLK_DEV_CMD64X */
#ifdef CONFIG_BLK_DEV_CS5530
	if ((cs5530_display_info) && (cs5530_proc))
		remove_proc_entry("ide/cs5530",0);
#endif /* CONFIG_BLK_DEV_CS5530 */
#ifdef CONFIG_BLK_DEV_HPT34X
	if ((hpt34x_display_info) && (hpt34x_proc))
		remove_proc_entry("ide/hpt34x",0);
#endif /* CONFIG_BLK_DEV_HPT34X */
#ifdef CONFIG_BLK_DEV_HPT366
	if ((hpt366_display_info) && (hpt366_proc))
		remove_proc_entry("ide/hpt366",0);
#endif /* CONFIG_BLK_DEV_HPT366 */
#ifdef CONFIG_BLK_DEV_PDC202XX
	if ((pdc202xx_display_info) && (pdc202xx_proc))
		remove_proc_entry("ide/pdc202xx",0);
#endif /* CONFIG_BLK_DEV_PDC202XX */
#ifdef CONFIG_BLK_DEV_PIIX
	if ((piix_display_info) && (piix_proc))
		remove_proc_entry("ide/piix",0);
#endif /* CONFIG_BLK_DEV_PIIX */
#ifdef CONFIG_BLK_DEV_SVWKS
	if ((svwks_display_info) && (svwks_proc))
		remove_proc_entry("ide/svwks",0);
#endif /* CONFIG_BLK_DEV_SVWKS */
#ifdef CONFIG_BLK_DEV_SIS5513
	if ((sis_display_info) && (sis_proc))
		remove_proc_entry("ide/sis", 0);
#endif /* CONFIG_BLK_DEV_SIS5513 */
#ifdef CONFIG_BLK_DEV_VIA82CXXX
	if ((via_display_info) && (via_proc))
		remove_proc_entry("ide/via",0);
#endif /* CONFIG_BLK_DEV_VIA82CXXX */

	remove_proc_entry("ide/drivers", 0);
	destroy_proc_ide_interfaces();
	remove_proc_entry("ide", 0);
}
