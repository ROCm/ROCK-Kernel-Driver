/*
 *	$Id: proc.c,v 1.13 1998/05/12 07:36:07 mj Exp $
 *
 *	Procfs interface for the PCI bus.
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@suse.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>

#define PCI_CFG_SPACE_SIZE 256

static loff_t
proc_bus_pci_lseek(struct file *file, loff_t off, int whence)
{
	loff_t new;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = PCI_CFG_SPACE_SIZE + off;
		break;
	default:
		return -EINVAL;
	}
	if (new < 0 || new > PCI_CFG_SPACE_SIZE)
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t
proc_bus_pci_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	const struct inode *ino = file->f_dentry->d_inode;
	const struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	unsigned int pos = *ppos;
	unsigned int cnt, size;

	/*
	 * Normal users can read only the standardized portion of the
	 * configuration space as several chips lock up when trying to read
	 * undefined locations (think of Intel PIIX4 as a typical example).
	 */

	if (capable(CAP_SYS_ADMIN))
		size = PCI_CFG_SPACE_SIZE;
	else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;
	else
		size = 64;

	if (pos >= size)
		return 0;
	if (nbytes >= size)
		nbytes = size;
	if (pos + nbytes > size)
		nbytes = size - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_WRITE, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		pci_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		pci_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		pci_read_config_dword(dev, pos, &val);
		__put_user(cpu_to_le32(val), (unsigned int *) buf);
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		pci_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		pci_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static ssize_t
proc_bus_pci_write(struct file *file, const char *buf, size_t nbytes, loff_t *ppos)
{
	const struct inode *ino = file->f_dentry->d_inode;
	const struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	int pos = *ppos;
	int cnt;

	if (pos >= PCI_CFG_SPACE_SIZE)
		return 0;
	if (nbytes >= PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE;
	if (pos + nbytes > PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_READ, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pci_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		__get_user(val, (unsigned int *) buf);
		pci_write_config_dword(dev, pos, le32_to_cpu(val));
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pci_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static struct file_operations proc_bus_pci_operations = {
	llseek:	proc_bus_pci_lseek,
	read:	proc_bus_pci_read,
	write:	proc_bus_pci_write,
};

#if BITS_PER_LONG == 32
#define LONG_FORMAT "\t%08lx"
#else
#define LONG_FORMAT "\t%16lx"
#endif

static int
get_pci_dev_info(char *buf, char **start, off_t pos, int count)
{
	const struct pci_dev *dev;
	off_t at = 0;
	int len, i, cnt;

	cnt = 0;
	pci_for_each_dev(dev) {
		const struct pci_driver *drv = pci_dev_driver(dev);
		len = sprintf(buf, "%02x%02x\t%04x%04x\t%x",
			dev->bus->number,
			dev->devfn,
			dev->vendor,
			dev->device,
			dev->irq);
		/* Here should be 7 and not PCI_NUM_RESOURCES as we need to preserve compatibility */
		for(i=0; i<7; i++)
			len += sprintf(buf+len, LONG_FORMAT,
				       dev->resource[i].start | (dev->resource[i].flags & PCI_REGION_FLAG_MASK));
		for(i=0; i<7; i++)
			len += sprintf(buf+len, LONG_FORMAT, dev->resource[i].start < dev->resource[i].end ?
				       dev->resource[i].end - dev->resource[i].start + 1 : 0);
		buf[len++] = '\t';
		if (drv)
			len += sprintf(buf+len, "%s", drv->name);
		buf[len++] = '\n';
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
			if (cnt >= count)
				/*
				 * proc_file_read() gives us 1KB of slack so it's OK if the
				 * above printfs write a little beyond the buffer end (we
				 * never write more than 1KB beyond the buffer end).
				 */
				break;
		}
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_pci_dir;

int pci_proc_attach_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct proc_dir_entry *de, *e;
	char name[16];

	if (!(de = bus->procdir)) {
		sprintf(name, "%02x", bus->number);
		de = bus->procdir = proc_mkdir(name, proc_bus_pci_dir);
		if (!de)
			return -ENOMEM;
	}
	sprintf(name, "%02x.%x", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	e = dev->procent = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUSR, de);
	if (!e)
		return -ENOMEM;
	e->proc_fops = &proc_bus_pci_operations;
	e->data = dev;
	e->size = PCI_CFG_SPACE_SIZE;
	return 0;
}

int pci_proc_detach_device(struct pci_dev *dev)
{
	struct proc_dir_entry *e;

	if ((e = dev->procent)) {
		if (atomic_read(&e->count))
			return -EBUSY;
		remove_proc_entry(e->name, dev->bus->procdir);
		dev->procent = NULL;
	}
	return 0;
}


/*
 *  Backward compatible /proc/pci interface.
 */

/*
 * Convert some of the configuration space registers of the device at
 * address (bus,devfn) into a string (possibly several lines each).
 * The configuration string is stored starting at buf[len].  If the
 * string would exceed the size of the buffer (SIZE), 0 is returned.
 */
static int sprint_dev_config(struct pci_dev *dev, char *buf, int size)
{
	u32 class_rev;
	unsigned char latency, min_gnt, max_lat, *class;
	int reg, len = 0;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	pci_read_config_byte (dev, PCI_LATENCY_TIMER, &latency);
	pci_read_config_byte (dev, PCI_MIN_GNT, &min_gnt);
	pci_read_config_byte (dev, PCI_MAX_LAT, &max_lat);
	if (len + 160 > size)
		return -1;
	len += sprintf(buf + len, "  Bus %2d, device %3d, function %2d:\n",
		       dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	class = pci_class_name(class_rev >> 16);
	if (class)
		len += sprintf(buf+len, "    %s", class);
	else
		len += sprintf(buf+len, "    Class %04x", class_rev >> 16);
	len += sprintf(buf+len, ": %s (rev %d).\n", dev->name, class_rev & 0xff);

	if (dev->irq) {
		if (len + 40 > size)
			return -1;
		len += sprintf(buf + len, "      IRQ %d.\n", dev->irq);
	}

	if (latency || min_gnt || max_lat) {
		if (len + 80 > size)
			return -1;
		len += sprintf(buf + len, "      Master Capable.  ");
		if (latency)
		  len += sprintf(buf + len, "Latency=%d.  ", latency);
		else
		  len += sprintf(buf + len, "No bursts.  ");
		if (min_gnt)
		  len += sprintf(buf + len, "Min Gnt=%d.", min_gnt);
		if (max_lat)
		  len += sprintf(buf + len, "Max Lat=%d.", max_lat);
		len += sprintf(buf + len, "\n");
	}

	for (reg = 0; reg < 6; reg++) {
		struct resource *res = dev->resource + reg;
		unsigned long base, end, flags;

		if (len + 40 > size)
			return -1;
		base = res->start;
		end = res->end;
		flags = res->flags;
		if (!end)
			continue;

		if (flags & PCI_BASE_ADDRESS_SPACE_IO) {
			len += sprintf(buf + len,
				       "      I/O at 0x%lx [0x%lx].\n",
				       base, end);
		} else {
			const char *pref, *type = "unknown";

			if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
				pref = "P";
			else
				pref = "Non-p";
			switch (flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
			      case PCI_BASE_ADDRESS_MEM_TYPE_32:
				type = "32 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				type = "20 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_64:
				type = "64 bit"; break;
			}
			len += sprintf(buf + len,
				       "      %srefetchable %s memory at "
				       "0x%lx [0x%lx].\n", pref, type,
				       base,
				       end);
		}
	}

	return len;
}

/*
 * Return list of PCI devices as a character string for /proc/pci.
 * BUF is a buffer that is PAGE_SIZE bytes long.
 */
static int pci_read_proc(char *buf, char **start, off_t off,
				int count, int *eof, void *data)
{
	int nprinted, len, begin = 0;
	struct pci_dev *dev;

	len = sprintf(buf, "PCI devices found:\n");

	*eof = 1;
	pci_for_each_dev(dev) {
		nprinted = sprint_dev_config(dev, buf + len, PAGE_SIZE - len);
		if (nprinted < 0) {
			*eof = 0;
			break;
		}
		len += nprinted;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
		if (len+begin >= off+count)
			break;
	}
	off -= begin;
	*start = buf + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
		len = 0;
	return len;
}

static int __init pci_proc_init(void)
{
	if (pci_present()) {
		struct pci_dev *dev;
		proc_bus_pci_dir = proc_mkdir("pci", proc_bus);
		create_proc_info_entry("devices", 0, proc_bus_pci_dir,
					get_pci_dev_info);
		pci_for_each_dev(dev) {
			pci_proc_attach_device(dev);
		}
		create_proc_read_entry("pci", 0, NULL, pci_read_proc, NULL);
	}
	return 0;
}

__initcall(pci_proc_init);
