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
#ifdef CONFIG_BLK_DEV_AMD7409
extern byte amd7409_proc;
int (*amd7409_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_AMD7409 */
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
#ifdef CONFIG_BLK_DEV_OSB4
extern byte osb4_proc;
int (*osb4_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_OSB4 */
#ifdef CONFIG_BLK_DEV_PDC202XX
extern byte pdc202xx_proc;
int (*pdc202xx_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_PDC202XX */
#ifdef CONFIG_BLK_DEV_PIIX
extern byte piix_proc;
int (*piix_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_PIIX */
#ifdef CONFIG_BLK_DEV_SIS5513
extern byte sis_proc;
int (*sis_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_SIS5513 */
#ifdef CONFIG_BLK_DEV_SLC90E66
extern byte slc90e66_proc;
int (*slc90e66_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_SLC90E66 */
#ifdef CONFIG_BLK_DEV_VIA82CXXX
extern byte via_proc;
int (*via_display_info)(char *, char **, off_t, int) = NULL;
#endif /* CONFIG_BLK_DEV_VIA82CXXX */

static int ide_getxdigit(char c)
{
	int digit;
	if (isdigit(c))
		digit = c - '0';
	else if (isxdigit(c))
		digit = tolower(c) - 'a' + 10;
	else
		digit = -1;
	return digit;
}

static int xx_xx_parse_error (const char *data, unsigned long len, const char *msg)
{
	char errbuf[16];
	int i;
	if (len >= sizeof(errbuf))
		len = sizeof(errbuf) - 1;
	for (i = 0; i < len; ++i) {
		char c = data[i];
		if (!c || c == '\n')
			c = '\0';
		else if (iscntrl(c))
			c = '?';
		errbuf[i] = c;
	}
	errbuf[i] = '\0';
	printk("proc_ide: error: %s: '%s'\n", msg, errbuf);
	return -EINVAL;
}

static struct proc_dir_entry * proc_ide_root = NULL;

static int proc_ide_write_config
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	int		for_real = 0;
	unsigned long	startn = 0, n, flags;
	const char	*start = NULL, *msg = NULL;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
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
	 * then do another to actually write the regs.
	 */
	save_flags(flags);	/* all CPUs */
	do {
		const char *p;
		if (for_real) {
			unsigned long timeout = jiffies + (3 * HZ);
			ide_hwgroup_t *mygroup = (ide_hwgroup_t *)(hwif->hwgroup);
			ide_hwgroup_t *mategroup = NULL;
			if (hwif->mate && hwif->mate->hwgroup)
				mategroup = (ide_hwgroup_t *)(hwif->mate->hwgroup);
			cli();	/* all CPUs; ensure all writes are done together */
			while (mygroup->busy || (mategroup && mategroup->busy)) {
				sti();	/* all CPUs */
				if (0 < (signed long)(jiffies - timeout)) {
					printk("/proc/ide/%s/config: channel(s) busy, cannot write\n", hwif->name);
					restore_flags(flags);	/* all CPUs */
					return -EBUSY;
				}
				cli();	/* all CPUs */
			}
		}
		p = buffer;
		n = count;
		while (n > 0) {
			int d, digits;
			unsigned int reg = 0, val = 0, is_pci;
			start = p;
			startn = n--;
			switch (*p++) {
				case 'R':	is_pci = 0;
						break;
				case 'P':	is_pci = 1;
#ifdef CONFIG_BLK_DEV_IDEPCI
						if (hwif->pci_dev && !IDE_PCI_DEVID_EQ(hwif->pci_devid, IDE_PCI_DEVID_NULL))
							break;
#endif	/* CONFIG_BLK_DEV_IDEPCI */
						msg = "not a PCI device";
						goto parse_error;
				default:	msg = "expected 'R' or 'P'";
						goto parse_error;
			}
			digits = 0;
			while (n > 0 && (d = ide_getxdigit(*p)) >= 0) {
				reg = (reg << 4) | d;
				--n;
				++p;
				++digits;
			}
			if (!digits || (digits > 4) || (is_pci && reg > 0xff)) {
				msg = "bad/missing register number";
				goto parse_error;
			}
			if (n-- == 0 || *p++ != ':') {
				msg = "missing ':'";
				goto parse_error;
			}
			digits = 0;
			while (n > 0 && (d = ide_getxdigit(*p)) >= 0) {
				val = (val << 4) | d;
				--n;
				++p;
				++digits;
			}
			if (digits != 2 && digits != 4 && digits != 8) {
				msg = "bad data, 2/4/8 digits required";
				goto parse_error;
			}
			if (n > 0 && !isspace(*p)) {
				msg = "expected whitespace after data";
				goto parse_error;
			}
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}
#ifdef CONFIG_BLK_DEV_IDEPCI
			if (is_pci && (reg & ((digits >> 1) - 1))) {
				msg = "misaligned access";
				goto parse_error;
			}
#endif	/* CONFIG_BLK_DEV_IDEPCI */
			if (for_real) {
#if 0
				printk("proc_ide_write_config: type=%c, reg=0x%x, val=0x%x, digits=%d\n", is_pci ? "PCI" : "non-PCI", reg, val, digits);
#endif
				if (is_pci) {
#ifdef CONFIG_BLK_DEV_IDEPCI
					int rc = 0;
					struct pci_dev *dev = hwif->pci_dev;
					switch (digits) {
						case 2:	msg = "byte";
							rc = pci_write_config_byte(dev, reg, val);
							break;
						case 4:	msg = "word";
							rc = pci_write_config_word(dev, reg, val);
							break;
						case 8:	msg = "dword";
							rc = pci_write_config_dword(dev, reg, val);
							break;
					}
					if (rc) {
						restore_flags(flags);	/* all CPUs */
						printk("proc_ide_write_config: error writing %s at bus %02x dev %02x reg 0x%x value 0x%x\n",
							msg, dev->bus->number, dev->devfn, reg, val);
						printk("proc_ide_write_config: error %d\n", rc);
						return -EIO;
					}
#endif	/* CONFIG_BLK_DEV_IDEPCI */
				} else {	/* not pci */
#if !defined(__mc68000__) && !defined(CONFIG_APUS)

/*
 * Geert Uytterhoeven
 *
 * unless you can explain me what it really does.
 * On m68k, we don't have outw() and outl() yet,
 * and I need a good reason to implement it.
 * 
 * BTW, IMHO the main remaining portability problem with the IDE driver 
 * is that it mixes IO (ioport) and MMIO (iomem) access on different platforms.
 * 
 * I think all accesses should be done using
 * 
 *     ide_in[bwl](ide_device_instance, offset)
 *     ide_out[bwl](ide_device_instance, value, offset)
 * 
 * so the architecture specific code can #define ide_{in,out}[bwl] to the
 * appropriate function.
 * 
 */
					switch (digits) {
						case 2:	outb(val, reg);
							break;
						case 4:	outw(val, reg);
							break;
						case 8:	outl(val, reg);
							break;
					}
#endif /* !__mc68000__ && !CONFIG_APUS */
				}
			}
		}
	} while (!for_real++);
	restore_flags(flags);	/* all CPUs */
	return count;
parse_error:
	restore_flags(flags);	/* all CPUs */
	printk("parse error\n");
	return xx_xx_parse_error(start, startn, msg);
}

static int proc_ide_read_config
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char		*out = page;
	int		len;

#ifdef CONFIG_BLK_DEV_IDEPCI
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	struct pci_dev	*dev = hwif->pci_dev;
	if (!IDE_PCI_DEVID_EQ(hwif->pci_devid, IDE_PCI_DEVID_NULL) && dev && dev->bus) {
		int reg = 0;

		out += sprintf(out, "pci bus %02x device %02x vid %04x did %04x channel %d\n",
			dev->bus->number, dev->devfn, hwif->pci_devid.vid, hwif->pci_devid.did, hwif->channel);
		do {
			byte val;
			int rc = pci_read_config_byte(dev, reg, &val);
			if (rc) {
				printk("proc_ide_read_config: error %d reading bus %02x dev %02x reg 0x%02x\n",
					rc, dev->bus->number, dev->devfn, reg);
				out += sprintf(out, "??%c", (++reg & 0xf) ? ' ' : '\n');
			} else
				out += sprintf(out, "%02x%c", val, (++reg & 0xf) ? ' ' : '\n');
		} while (reg < 0x100);
	} else
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		out += sprintf(out, "(none)\n");
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}


static int ide_getdigit(char c)
{
	int digit;
	if (isdigit(c))
		digit = c - '0';
	else
		digit = -1;
	return digit;
}

static int proc_ide_read_drivers
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char		*out = page;
	int		len;
	ide_module_t	*p = ide_modules;
	ide_driver_t	*driver;

	while (p) {
		driver = (ide_driver_t *) p->info;
		if (driver)
			out += sprintf(out, "%s version %s\n", driver->name, driver->version);
		p = p->next;
	}
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
	const char	*name;

	switch (hwif->chipset) {
		case ide_unknown:	name = "(none)";	break;
		case ide_generic:	name = "generic";	break;
		case ide_pci:		name = "pci";		break;
		case ide_cmd640:	name = "cmd640";	break;
		case ide_dtc2278:	name = "dtc2278";	break;
		case ide_ali14xx:	name = "ali14xx";	break;
		case ide_qd6580:	name = "qd6580";	break;
		case ide_umc8672:	name = "umc8672";	break;
		case ide_ht6560b:	name = "ht6560b";	break;
		case ide_pdc4030:	name = "pdc4030";	break;
		case ide_rz1000:	name = "rz1000";	break;
		case ide_trm290:	name = "trm290";	break;
		case ide_cmd646:	name = "cmd646";	break;
		case ide_cy82c693:	name = "cy82c693";	break;
		case ide_4drives:	name = "4drives";	break;
		case ide_pmac:		name = "mac-io";	break;
		default:		name = "(unknown)";	break;
	}
	len = sprintf(page, "%s\n", name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_mate
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
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
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	page[0] = hwif->channel ? '1' : '0';
	page[1] = '\n';
	len = 2;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_get_identify(ide_drive_t *drive, byte *buf)
{
	return ide_wait_cmd(drive, (drive->media == ide_disk) ? WIN_IDENTIFY : WIN_PIDENTIFY, 0, 0, 1, buf);
}

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (drive && !proc_ide_get_identify(drive, page)) {
		unsigned short *val = ((unsigned short *)page) + 2;
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
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_settings_t	*setting = (ide_settings_t *) drive->settings;
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
	ide_drive_t	*drive = (ide_drive_t *) data;
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
			len = IDE_MIN(p - start, MAX_LEN);
			strncpy(name, start, IDE_MIN(len, MAX_LEN));
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
			setting = ide_find_setting_by_name(drive, name);
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
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_driver_t    *driver = (ide_driver_t *) drive->driver;
	int		len;

	if (!driver)
		len = sprintf(page, "(none)\n");
        else
		len = sprintf(page,"%li\n", ((ide_driver_t *)drive->driver)->capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
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
	ide_drive_t	*drive = (ide_drive_t *) data;
	struct hd_driveid *id = drive->id;
	int		len;

	len = sprintf(page, "%.40s\n", (id && id->model[0]) ? (char *)id->model : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_driver
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_driver_t	*driver = (ide_driver_t *) drive->driver;
	int		len;

	if (!driver)
		len = sprintf(page, "(none)\n");
	else
		len = sprintf(page, "%s version %s\n", driver->name, driver->version);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_write_driver
	(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (ide_replace_subdriver(drive, buffer))
		return -EINVAL;
	return count;
}

static int proc_ide_read_media
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	const char	*media;
	int		len;

	switch (drive->media) {
		case ide_disk:	media = "disk\n";
				break;
		case ide_cdrom:	media = "cdrom\n";
				break;
		case ide_tape:	media = "tape\n";
				break;
		case ide_floppy:media = "floppy\n";
				break;
		default:	media = "UNKNOWN\n";
				break;
	}
	strcpy(page,media);
	len = strlen(media);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t generic_drive_entries[] = {
	{ "driver",	S_IFREG|S_IRUGO,	proc_ide_read_driver,	proc_ide_write_driver },
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
		ide_driver_t *driver = drive->driver;

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

void destroy_proc_ide_drives(ide_hwif_t *hwif)
{
	int	d;

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];
		ide_driver_t *driver = drive->driver;

		if (!drive->proc)
			continue;
		if (driver)
			ide_remove_proc_entries(drive->proc, driver->proc);
		ide_remove_proc_entries(drive->proc, generic_drive_entries);
		remove_proc_entry(drive->name, proc_ide_root);
		remove_proc_entry(drive->name, hwif->proc);
		drive->proc = NULL;
	}
}

static ide_proc_entry_t hwif_entries[] = {
	{ "channel",	S_IFREG|S_IRUGO,	proc_ide_read_channel,	NULL },
	{ "config",	S_IFREG|S_IRUGO|S_IWUSR,proc_ide_read_config,	proc_ide_write_config },
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

	create_proc_read_entry("drivers", 0, proc_ide_root,
				proc_ide_read_drivers, NULL);

#ifdef CONFIG_BLK_DEV_AEC62XX
	if ((aec62xx_display_info) && (aec62xx_proc))
		create_proc_info_entry("aec62xx", 0, proc_ide_root, aec62xx_display_info);
#endif /* CONFIG_BLK_DEV_AEC62XX */
#ifdef CONFIG_BLK_DEV_ALI15X3
	if ((ali_display_info) && (ali_proc))
		create_proc_info_entry("ali", 0, proc_ide_root, ali_display_info);
#endif /* CONFIG_BLK_DEV_ALI15X3 */
#ifdef CONFIG_BLK_DEV_AMD7409
	if ((amd7409_display_info) && (amd7409_proc))
		create_proc_info_entry("amd7409", 0, proc_ide_root, amd7409_display_info);
#endif /* CONFIG_BLK_DEV_AMD7409 */
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
#ifdef CONFIG_BLK_DEV_OSB4
	if ((osb4_display_info) && (osb4_proc))
		create_proc_info_entry("osb4", 0, proc_ide_root, osb4_display_info);
#endif /* CONFIG_BLK_DEV_OSB4 */
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
#ifdef CONFIG_BLK_DEV_SLC90E66
	if ((slc90e66_display_info) && (slc90e66_proc))
		create_proc_info_entry("slc90e66", 0, proc_ide_root, slc90e66_display_info);
#endif /* CONFIG_BLK_DEV_SLC90E66 */
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
#ifdef CONFIG_BLK_DEV_AMD7409
	if ((amd7409_display_info) && (amd7409_proc))
		remove_proc_entry("ide/amd7409",0);
#endif /* CONFIG_BLK_DEV_AMD7409 */
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
#ifdef CONFIG_BLK_DEV_OSB4
	if ((osb4_display_info) && (osb4_proc))
		remove_proc_entry("ide/osb4",0);
#endif /* CONFIG_BLK_DEV_OSB4 */
#ifdef CONFIG_BLK_DEV_PDC202XX
	if ((pdc202xx_display_info) && (pdc202xx_proc))
		remove_proc_entry("ide/pdc202xx",0);
#endif /* CONFIG_BLK_DEV_PDC202XX */
#ifdef CONFIG_BLK_DEV_PIIX
	if ((piix_display_info) && (piix_proc))
		remove_proc_entry("ide/piix",0);
#endif /* CONFIG_BLK_DEV_PIIX */
#ifdef CONFIG_BLK_DEV_SIS5513
	if ((sis_display_info) && (sis_proc))
		remove_proc_entry("ide/sis", 0);
#endif /* CONFIG_BLK_DEV_SIS5513 */
#ifdef CONFIG_BLK_DEV_SLC90E66
	if ((slc90e66_display_info) && (slc90e66_proc))
		remove_proc_entry("ide/slc90e66",0);
#endif /* CONFIG_BLK_DEV_SLC90E66 */
#ifdef CONFIG_BLK_DEV_VIA82CXXX
	if ((via_display_info) && (via_proc))
		remove_proc_entry("ide/via",0);
#endif /* CONFIG_BLK_DEV_VIA82CXXX */

	remove_proc_entry("ide/drivers", 0);
	destroy_proc_ide_interfaces();
	remove_proc_entry("ide", 0);
}
