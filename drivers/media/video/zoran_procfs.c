/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles the procFS entries (/proc/ZORAN[%d])
 * 
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Currently maintained by:
 *   Ronald Bultje    <rbultje@ronald.bitfreak.net>
 *   Laurent Pinchart <laurent.pinchart@skynet.be>
 *   Mailinglist      <mjpeg-users@lists.sf.net>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <linux/spinlock.h>
#include <linux/sem.h>

#include <linux/ctype.h>
#include <asm/io.h>

#include "videocodec.h"
#include "zoran.h"
#include "zoran_procfs.h"

extern int *zr_debug;

#define dprintk(num, format, args...) \
	do { \
		if (*zr_debug >= num) \
			printk(format, ##args); \
	} while (0)

#ifdef CONFIG_PROC_FS
struct procfs_params_zr36067 {
	char *name;
	short reg;
	u32 mask;
	short bit;
};

static const struct procfs_params_zr36067 zr67[] = {
	{"HSPol", 0x000, 1, 30},
	{"HStart", 0x000, 0x3ff, 10},
	{"HEnd", 0x000, 0x3ff, 0},

	{"VSPol", 0x004, 1, 30},
	{"VStart", 0x004, 0x3ff, 10},
	{"VEnd", 0x004, 0x3ff, 0},

	{"ExtFl", 0x008, 1, 26},
	{"TopField", 0x008, 1, 25},
	{"VCLKPol", 0x008, 1, 24},
	{"DupFld", 0x008, 1, 20},
	{"LittleEndian", 0x008, 1, 0},

	{"HsyncStart", 0x10c, 0xffff, 16},
	{"LineTot", 0x10c, 0xffff, 0},

	{"NAX", 0x110, 0xffff, 16},
	{"PAX", 0x110, 0xffff, 0},

	{"NAY", 0x114, 0xffff, 16},
	{"PAY", 0x114, 0xffff, 0},

	/* {"",,,}, */

	{NULL, 0, 0, 0},
};

struct procfs_io {
	char *buffer;
	char *end;
	int neof;
	int count;
	int count_current;
};

static void
setparam (struct zoran *zr,
	  char         *name,
	  char         *sval)
{
	int i = 0, reg0, reg, val;

	while (zr67[i].name != NULL) {
		if (!strncmp(name, zr67[i].name, strlen(zr67[i].name))) {
			reg = reg0 = btread(zr67[i].reg);
			reg &= ~(zr67[i].mask << zr67[i].bit);
			if (!isdigit(sval[0]))
				break;
			val = simple_strtoul(sval, NULL, 0);
			if ((val & ~zr67[i].mask))
				break;
			reg |= (val & zr67[i].mask) << zr67[i].bit;
			dprintk(4,
				KERN_INFO
				"%s: setparam: setting ZR36067 register 0x%03x: 0x%08x=>0x%08x %s=%d\n",
				ZR_DEVNAME(zr), zr67[i].reg, reg0, reg,
				zr67[i].name, val);
			btwrite(reg, zr67[i].reg);
			break;
		}
		i++;
	}
}

static int
print_procfs (struct procfs_io *io,
	      const char       *fmt,
	      ...)
{
	va_list args;
	int i;

	if (io->buffer >= io->end) {
		io->neof++;
		return 0;
	}
	if (io->count > io->count_current++)
		return 0;
	va_start(args, fmt);
	i = vsprintf(io->buffer, fmt, args);
	io->buffer += i;
	va_end(args);
	return i;
}

static void
zoran_procfs_output (struct procfs_io *io,
		     void             *data)
{
	int i;
	struct zoran *zr;
	zr = (struct zoran *) data;

	print_procfs(io, "ZR36067 registers:");
	for (i = 0; i < 0x130; i += 4) {
		if (!(i % 16)) {
			print_procfs(io, "\n%03X", i);
		};
		print_procfs(io, " %08X ", btread(i));
	};
	print_procfs(io, "\n");
}

static int
zoran_read_proc (char  *buffer,
		 char **start,
		 off_t  offset,
		 int    size,
		 int   *eof,
		 void  *data)
{
	struct procfs_io io;
	int nbytes;

	io.buffer = buffer;
	io.end = buffer + size - 128;	// Just to make it a little bit safer
	io.count = offset;
	io.count_current = 0;
	io.neof = 0;
	zoran_procfs_output(&io, data);
	*start = (char *) (io.count_current - io.count);
	nbytes = (int) (io.buffer - buffer);
	*eof = !io.neof;
	return nbytes;

	return 0;
}

static int
zoran_write_proc (struct file   *file,
		  const char    *buffer,
		  unsigned long  count,
		  void          *data)
{
	char *string, *sp;
	char *line, *ldelim, *varname, *svar, *tdelim;
	struct zoran *zr;

	if (count > 32768)	/* Stupidity filter */
		return -EINVAL;

	zr = (struct zoran *) data;

	string = sp = vmalloc(count + 1);
	if (!string) {
		dprintk(1,
			KERN_ERR
			"%s: write_proc: can not allocate memory\n",
			ZR_DEVNAME(zr));
		return -ENOMEM;
	}
	if (copy_from_user(string, buffer, count)) {
		vfree (string);
		return -EFAULT;
	}
	string[count] = 0;
	dprintk(4, KERN_INFO "%s: write_proc: name=%s count=%lu data=%x\n",
		ZR_DEVNAME(zr), file->f_dentry->d_name.name, count, (int) data);
	ldelim = " \t\n";
	tdelim = "=";
	line = strpbrk(sp, ldelim);
	while (line) {
		*line = 0;
		svar = strpbrk(sp, tdelim);
		if (svar) {
			*svar = 0;
			varname = sp;
			svar++;
			setparam(zr, varname, svar);
		}
		sp = line + 1;
		line = strpbrk(sp, ldelim);
	}
	vfree(string);

	return count;
}
#endif

int
zoran_proc_init (struct zoran *zr)
{
#ifdef CONFIG_PROC_FS
	char name[8];

	snprintf(name, 7, "zoran%d", zr->id);
	if ((zr->zoran_proc = create_proc_entry(name, 0, 0))) {
		zr->zoran_proc->read_proc = zoran_read_proc;
		zr->zoran_proc->write_proc = zoran_write_proc;
		zr->zoran_proc->data = zr;
		zr->zoran_proc->owner = THIS_MODULE;
		dprintk(2,
			KERN_INFO
			"%s: procfs entry /proc/%s allocated. data=%p\n",
			ZR_DEVNAME(zr), name, zr->zoran_proc->data);
	} else {
		dprintk(1, KERN_ERR "%s: Unable to initialise /proc/%s\n",
			ZR_DEVNAME(zr), name);
		return 1;
	}
#endif
	return 0;
}

void
zoran_proc_cleanup (struct zoran *zr)
{
#ifdef CONFIG_PROC_FS
	char name[8];

	snprintf(name, 7, "zoran%d", zr->id);
	if (zr->zoran_proc) {
		remove_proc_entry(name, 0);
	}
	zr->zoran_proc = NULL;
#endif
}
