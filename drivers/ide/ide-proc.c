/*
 *  linux/drivers/ide/ide-proc.c	Version 1.05	Mar 05, 2003
 *
 *  Copyright (C) 1997-1998	Mark Lord
 *  Copyright (C) 2003		Red Hat <alan@redhat.com>
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
#include <linux/module.h>

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
#include <linux/seq_file.h>

#include <asm/io.h>

static int proc_ide_write_config(struct file *file, const char __user *buffer,
				 unsigned long count, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	ide_hwgroup_t *mygroup = (ide_hwgroup_t *)(hwif->hwgroup);
	ide_hwgroup_t *mategroup = NULL;
	unsigned long timeout;
	unsigned long flags;
	const char *start = NULL, *msg = NULL;
	struct entry { u32 val; u16 reg; u8 size; u8 pci; } *prog, *q, *r;
	int want_pci = 0;
	char *buf, *s;
	int err;

	if (hwif->mate && hwif->mate->hwgroup)
		mategroup = (ide_hwgroup_t *)(hwif->mate->hwgroup);

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;

	if (count >= PAGE_SIZE)
		return -EINVAL;

	s = buf = (char *)__get_free_page(GFP_USER);
	if (!buf)
		return -ENOMEM;

	err = -ENOMEM;
	q = prog = (struct entry *)__get_free_page(GFP_USER);
	if (!prog)
		goto out;

	err = -EFAULT;
	if (copy_from_user(buf, buffer, count))
		goto out1;

	buf[count] = '\0';

	while (isspace(*s))
		s++;

	while (*s) {
		char *p;
		int digits;

		start = s;

		if ((char *)(q + 1) > (char *)prog + PAGE_SIZE) {
			msg = "too many entries";
			goto parse_error;
		}

		switch (*s++) {
			case 'R':	q->pci = 0;
					break;
			case 'P':	q->pci = 1;
					want_pci = 1;
					break;
			default:	msg = "expected 'R' or 'P'";
					goto parse_error;
		}

		q->reg = simple_strtoul(s, &p, 16);
		digits = p - s;
		if (!digits || digits > 4 || (q->pci && q->reg > 0xff)) {
			msg = "bad/missing register number";
			goto parse_error;
		}
		if (*p++ != ':') {
			msg = "missing ':'";
			goto parse_error;
		}
		q->val = simple_strtoul(p, &s, 16);
		digits = s - p;
		if (digits != 2 && digits != 4 && digits != 8) {
			msg = "bad data, 2/4/8 digits required";
			goto parse_error;
		}
		q->size = digits / 2;

		if (q->pci) {
#ifdef CONFIG_BLK_DEV_IDEPCI
			if (q->reg & (q->size - 1)) {
				msg = "misaligned access";
				goto parse_error;
			}
#else
			msg = "not a PCI device";
			goto parse_error;
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		}

		q++;

		if (*s && !isspace(*s++)) {
			msg = "expected whitespace after data";
			goto parse_error;
		}
		while (isspace(*s))
			s++;
	}

	/*
	 * What follows below is fucking insane, even for IDE people.
	 * For now I've dealt with the obvious problems on the parsing
	 * side, but IMNSHO we should simply remove the write access
	 * to /proc/ide/.../config, killing that FPOS completely.
	 */

	err = -EBUSY;
	timeout = jiffies + (3 * HZ);
	spin_lock_irqsave(&ide_lock, flags);
	while (mygroup->busy ||
	       (mategroup && mategroup->busy)) {
		spin_unlock_irqrestore(&ide_lock, flags);
		if (time_after(jiffies, timeout)) {
			printk("/proc/ide/%s/config: channel(s) busy, cannot write\n", hwif->name);
			goto out1;
		}
		spin_lock_irqsave(&ide_lock, flags);
	}

#ifdef CONFIG_BLK_DEV_IDEPCI
	if (want_pci && (!hwif->pci_dev || hwif->pci_dev->vendor)) {
		spin_unlock_irqrestore(&ide_lock, flags);
		printk("proc_ide: PCI registers not accessible for %s\n",
			hwif->name);
		err = -EINVAL;
		goto out1;
	}
#endif	/* CONFIG_BLK_DEV_IDEPCI */

	for (r = prog; r < q; r++) {
		unsigned int reg = r->reg, val = r->val;
		if (r->pci) {
#ifdef CONFIG_BLK_DEV_IDEPCI
			int rc = 0;
			struct pci_dev *dev = hwif->pci_dev;
			switch (q->size) {
				case 1:	msg = "byte";
					rc = pci_write_config_byte(dev, reg, val);
					break;
				case 2:	msg = "word";
					rc = pci_write_config_word(dev, reg, val);
					break;
				case 4:	msg = "dword";
					rc = pci_write_config_dword(dev, reg, val);
					break;
			}
			if (rc) {
				spin_unlock_irqrestore(&ide_lock, flags);
				printk("proc_ide_write_config: error writing %s at bus %02x dev %02x reg 0x%x value 0x%x\n",
					msg, dev->bus->number, dev->devfn, reg, val);
				printk("proc_ide_write_config: error %d\n", rc);
				err = -EIO;
				goto out1;
			}
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		} else {	/* not pci */
			switch (r->size) {
				case 1:	hwif->OUTB(val, reg);
					break;
				case 2:	hwif->OUTW(val, reg);
					break;
				case 4:	hwif->OUTL(val, reg);
					break;
			}
		}
	}
	spin_unlock_irqrestore(&ide_lock, flags);
	err = count;
out1:
	free_page((unsigned long)prog);
out:
	free_page((unsigned long)buf);
	return err;

parse_error:
	printk("parse error\n");
	printk("proc_ide: error: %s: '%s'\n", msg, start);
	err = -EINVAL;
	goto out1;
}

static int proc_ide_read_config
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char		*out = page;
	int		len;

#ifdef CONFIG_BLK_DEV_IDEPCI
	ide_hwif_t	*hwif = (ide_hwif_t *)data;
	struct pci_dev	*dev = hwif->pci_dev;
	if ((hwif->pci_dev && hwif->pci_dev->vendor) && dev && dev->bus) {
		int reg = 0;

		out += sprintf(out, "pci bus %02x device %02x vendor %04x "
				"device %04x channel %d\n",
			dev->bus->number, dev->devfn,
			hwif->pci_dev->vendor, hwif->pci_dev->device,
			hwif->channel);
		do {
			u8 val;
			int rc = pci_read_config_byte(dev, reg, &val);
			if (rc) {
				printk("proc_ide_read_config: error %d reading"
					" bus %02x dev %02x reg 0x%02x\n",
					rc, dev->bus->number, dev->devfn, reg);
				out += sprintf(out, "??%c",
					(++reg & 0xf) ? ' ' : '\n');
			} else
				out += sprintf(out, "%02x%c",
					val, (++reg & 0xf) ? ' ' : '\n');
		} while (reg < 0x100);
	} else
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		out += sprintf(out, "(none)\n");
	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
	const char	*name;

	/*
	 * Neither ide_unknown nor ide_forced should be set at this point.
	 */
	switch (hwif->chipset) {
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

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;
	int		err = 0;

	len = sprintf(page, "\n");
	
	if (drive)
	{
		unsigned short *val = (unsigned short *) page;
		
		/*
		 *	The current code can't handle a driverless
		 *	identify query taskfile. Now the right fix is
		 *	to add a 'default' driver but that is a bit
		 *	more work. 
		 *
		 *	FIXME: this has to be fixed for hotswap devices
		 */
		 
		if(DRIVER(drive))
			err = taskfile_lib_get_identify(drive, page);
		else	/* This relies on the ID changes */
			val = (unsigned short *)drive->id;

		if(!err)
		{						
			char *out = ((char *)page) + (SECTOR_WORDS * 4);
			page = out;
			do {
				out += sprintf(out, "%04x%c",
					le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
				val += 1;
			} while (i < (SECTOR_WORDS * 2));
			len = out - page;
		}
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_settings
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_settings_t	*setting = (ide_settings_t *) drive->settings;
	char		*out = page;
	int		len, rc, mul_factor, div_factor;

	down(&ide_setting_sem);
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
	up(&ide_setting_sem);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

#define MAX_LEN	30

static int proc_ide_write_settings(struct file *file, const char __user *buffer,
				   unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		name[MAX_LEN + 1];
	int		for_real = 0;
	unsigned long	n;
	ide_settings_t	*setting;
	char *buf, *s;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (count >= PAGE_SIZE)
		return -EINVAL;

	s = buf = (char *)__get_free_page(GFP_USER);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		free_page((unsigned long)buf);
		return -EFAULT;
	}

	buf[count] = '\0';

	/*
	 * Skip over leading whitespace
	 */
	while (count && isspace(*s)) {
		--count;
		++s;
	}
	/*
	 * Do one full pass to verify all parameters,
	 * then do another to actually write the new settings.
	 */
	do {
		char *p = s;
		n = count;
		while (n > 0) {
			unsigned val;
			char *q = p;

			while (n > 0 && *p != ':') {
				--n;
				p++;
			}
			if (*p != ':')
				goto parse_error;
			if (p - q > MAX_LEN)
				goto parse_error;
			memcpy(name, q, p - q);
			name[p - q] = 0;

			if (n > 0) {
				--n;
				p++;
			} else
				goto parse_error;

			val = simple_strtoul(p, &q, 10);
			n -= q - p;
			p = q;
			if (n > 0 && !isspace(*p))
				goto parse_error;
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}

			down(&ide_setting_sem);
			setting = ide_find_setting_by_name(drive, name);
			if (!setting)
			{
				up(&ide_setting_sem);
				goto parse_error;
			}
			if (for_real)
				ide_write_setting(drive, setting, val * setting->div_factor / setting->mul_factor);
			up(&ide_setting_sem);
		}
	} while (!for_real++);
	free_page((unsigned long)buf);
	return count;
parse_error:
	free_page((unsigned long)buf);
	printk("proc_ide_write_settings(): parse error\n");
	return -EINVAL;
}

int proc_ide_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	int		len;

	len = sprintf(page,"%llu\n",
		      (long long) (DRIVER(drive)->capacity(drive)));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	out += sprintf(out,"physical     %d/%d/%d\n",
			drive->cyl, drive->head, drive->sect);
	out += sprintf(out,"logical      %d/%d/%d\n",
			drive->bios_cyl, drive->bios_head, drive->bios_sect);

	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

EXPORT_SYMBOL(proc_ide_read_geometry);

static int proc_ide_read_dmodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	struct hd_driveid *id = drive->id;
	int		len;

	len = sprintf(page, "%.40s\n",
		(id && id->model[0]) ? (char *)id->model : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_driver
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_driver_t	*driver = drive->driver;
	int		len;

	len = sprintf(page, "%s version %s\n",
			driver->name, driver->version);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_write_driver
	(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char name[32];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (count > 31)
		count = 31;
	if (copy_from_user(name, buffer, count))
		return -EFAULT;
	name[count] = '\0';
	if (ide_replace_subdriver(drive, name))
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

		if (!drive->present)
			continue;
		if (drive->proc)
			continue;

		drive->proc = proc_mkdir(drive->name, parent);
		if (drive->proc)
			ide_add_proc_entries(drive->proc, generic_drive_entries, drive);
		sprintf(name,"ide%d/%s", (drive->name[2]-'a')/2, drive->name);
		ent = proc_symlink(drive->name, proc_ide_root, name);
		if (!ent) return;
	}
}

static void destroy_proc_ide_device(ide_hwif_t *hwif, ide_drive_t *drive)
{
	ide_driver_t *driver = drive->driver;

	if (drive->proc) {
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

EXPORT_SYMBOL(create_proc_ide_interfaces);

#ifdef CONFIG_BLK_DEV_IDEPCI
void ide_pci_create_host_proc(const char *name, get_info_t *get_info)
{
	create_proc_info_entry(name, 0, proc_ide_root, get_info);
}

EXPORT_SYMBOL_GPL(ide_pci_create_host_proc);
#endif

void destroy_proc_ide_interfaces(void)
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

EXPORT_SYMBOL(destroy_proc_ide_interfaces);

extern struct seq_operations ide_drivers_op;
static int ide_drivers_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ide_drivers_op);
}
static struct file_operations ide_drivers_operations = {
	.open		= ide_drivers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

void proc_ide_create(void)
{
	struct proc_dir_entry *entry;

	if (!proc_ide_root)
		return;

	create_proc_ide_interfaces();

	entry = create_proc_entry("drivers", 0, proc_ide_root);
	if (entry)
		entry->proc_fops = &ide_drivers_operations;
}

void proc_ide_destroy(void)
{
	remove_proc_entry("ide/drivers", proc_ide_root);
	destroy_proc_ide_interfaces();
	remove_proc_entry("ide", NULL);
}
