/*
 * eeh.c
 * Copyright (C) 2001 Dave Engebretsen & Todd Inglett IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Change Activity:
 * 2001/10/27 : engebret : Created.
 * End Change Activity 
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/bootmem.h>
#include <asm/paca.h>
#include <asm/processor.h>
#include <asm/naca.h>
#include <asm/io.h>
#include "pci.h"

#define BUID_HI(buid) ((buid) >> 32)
#define BUID_LO(buid) ((buid) & 0xffffffff)
#define CONFIG_ADDR(busno, devfn) (((((busno) & 0xff) << 8) | ((devfn) & 0xf8)) << 8)

unsigned long eeh_total_mmio_reads;
unsigned long eeh_total_mmio_ffs;
unsigned long eeh_false_positives;
/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;

static int eeh_implemented;
#define EEH_MAX_OPTS 4096
static char *eeh_opts;
static int eeh_opts_last;
static int eeh_check_opts_config(struct pci_dev *dev);


unsigned long eeh_token(unsigned long phb, unsigned long bus, unsigned long devfn, unsigned long offset)
{
	if (phb > 0xff)
		panic("eeh_token: phb 0x%lx is too large\n", phb);
	if (offset & 0x0fffffff00000000)
		panic("eeh_token: offset 0x%lx is out of range\n", offset);
	return ((IO_UNMAPPED_REGION_ID << 60) | (phb << 48UL) | ((bus & 0xff) << 40UL) | (devfn << 32UL) | (offset & 0xffffffff));
}



int eeh_get_state(unsigned long ea) {
	return 0;
}


/* Check for an eeh failure at the given token address.
 * The given value has been read and it should be 1's (0xff, 0xffff or 0xffffffff).
 *
 * Probe to determine if an error actually occurred.  If not return val.
 * Otherwise panic.
 */
unsigned long eeh_check_failure(void *token, unsigned long val)
{
	unsigned long config_addr = (unsigned long)token >> 24;	/* PPBBDDRR */
	unsigned long phbidx = (config_addr >> 24) & 0xff;
	struct pci_controller *phb;
	unsigned long ret, rets[2];

	config_addr &= 0xffff00;  /* 00BBDD00 */

	if (phbidx >= global_phb_number) {
		panic("EEH: checking token %p phb index of %ld is greater than max of %d\n", token, phbidx, global_phb_number-1);
	}
	phb = phbtab[phbidx];
	eeh_false_positives++;

	ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
			config_addr, BUID_HI(phb->buid), BUID_LO(phb->buid));
	if (ret == 0 && rets[1] == 1 && rets[2] != 0) {
		struct pci_dev *dev;
		int bus = ((unsigned long)token >> 40) & 0xffff; /* include PHB# in bus */
		int devfn = (config_addr >> 8) & 0xff;

		dev = pci_find_slot(bus, devfn);
		if (dev)
			panic("EEH:  MMIO failure (%ld) on device:\n  %s %s\n",
			      rets[2], dev->slot_name, dev->name);
		else
			panic("EEH:  MMIO failure (%ld) on device buid %lx, config_addr %lx\n", rets[2], phb->buid, config_addr);
	}
	return val;	/* good case */
}

void eeh_init(void) {
	ibm_set_eeh_option = rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset = rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state = rtas_token("ibm,read-slot-reset-state");
	if (ibm_set_eeh_option != RTAS_UNKNOWN_SERVICE) {
		printk("PCI Enhanced I/O Error Handling Enabled\n");
		eeh_implemented = 1;
	}
}


/* Given a PCI device check if eeh should be configured or not.
 * This may look at firmware properties and/or kernel cmdline options.
 */
int is_eeh_configured(struct pci_dev *dev)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_controller *phb = PCI_GET_PHB_PTR(dev);
	unsigned long ret, rets[2];

	if (dn == NULL || phb == NULL || phb->buid == 0 || !eeh_implemented)
		return 0;

	/* Hack: turn off eeh for display class devices.
	 * This fixes matrox accel framebuffer.
	 */
	if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY)
		return 0;

	if (!eeh_check_opts_config(dev))
		return 0;

	ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
			CONFIG_ADDR(dn->busno, dn->devfn),
			BUID_HI(phb->buid), BUID_LO(phb->buid));
	if (ret == 0 && rets[1] == 1) {
		printk("EEH: %s %s is EEH capable.\n", dev->slot_name, dev->name);
		return 1;
	}
	return 0;
}

int eeh_set_option(struct pci_dev *dev, int option)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_controller *phb = PCI_GET_PHB_PTR(dev);

	if (dn == NULL || phb == NULL || phb->buid == 0 || !eeh_implemented)
		return -2;

	return rtas_call(ibm_set_eeh_option, 4, 1, NULL,
			 CONFIG_ADDR(dn->busno, dn->devfn),
			 BUID_HI(phb->buid), BUID_LO(phb->buid), option);
}


static int eeh_proc_falsepositive_read(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;
	len = sprintf(page, "eeh_false_positives=%ld\n"
		      "eeh_total_mmio_ffs=%ld\n"
		      "eeh_total_mmio_reads=%ld\n",
		      eeh_false_positives, eeh_total_mmio_ffs, eeh_total_mmio_reads);
	return len;
}

/* Implementation of /proc/ppc64/eeh
 * For now it is one file showing false positives.
 */
void eeh_init_proc(struct proc_dir_entry *top)
{
	struct proc_dir_entry *ent = create_proc_entry("eeh", S_IRUGO, top);
	if (ent) {
		ent->nlink = 1;
		ent->data = NULL;
		ent->read_proc = (void *)eeh_proc_falsepositive_read;
	}
}

/*
 * Test if "dev" should be configured on or off.
 * This processes the options literally from right to left.
 * This lets the user specify stupid combinations of options,
 * but at least the result should be very predictable.
 */
static int eeh_check_opts_config(struct pci_dev *dev)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_controller *phb = PCI_GET_PHB_PTR(dev);
	char devname[32], classname[32], phbname[32];
	char *strs[8], *s;
	int nstrs, i;
	int ret = 0;

	if (dn == NULL || phb == NULL || phb->buid == 0 || !eeh_implemented)
		return 0;
	/* Build list of strings to match */
	nstrs = 0;
	s = (char *)get_property(dn, "ibm,loc-code", 0);
	if (s)
		strs[nstrs++] = s;
	sprintf(devname, "dev%04x:%04x", dev->vendor, dev->device);
	strs[nstrs++] = devname;
	sprintf(classname, "class%04x", dev->class);
	strs[nstrs++] = classname;
	sprintf(phbname, "pci@%lx", phb->buid);
	strs[nstrs++] = phbname;
	strs[nstrs++] = "";	/* yes, this matches the empty string */

	/* Now see if any string matches the eeh_opts list.
	 * The eeh_opts list entries start with + or -.
	 */
	for (s = eeh_opts; s && (s < (eeh_opts + eeh_opts_last)); s += strlen(s)+1) {
		for (i = 0; i < nstrs; i++) {
			if (strcasecmp(strs[i], s+1) == 0) {
				ret = (strs[0] == '+') ? 1 : 0;
			}
		}
	}
	return ret;
}

/* Handle kernel eeh-on & eeh-off cmd line options for eeh.
 *
 * We support:
 *	eeh-off=loc1,loc2,loc3...
 *
 * and this option can be repeated so
 *      eeh-off=loc1,loc2 eeh=loc3
 * is the same as eeh-off=loc1,loc2,loc3
 *
 * loc is an IBM location code that can be found in a manual or
 * via openfirmware (or the Hardware Management Console).
 *
 * We also support these additional "loc" values:
 *
 *	dev#:#    vendor:device id in hex (e.g. dev1022:2000)
 *	class#    class id in hex (e.g. class0200)
 *	pci@buid  all devices under phb (e.g. pci@fef00000)
 *
 * If no location code is specified all devices are assumed
 * so eeh-off means eeh by default is off.
 */

/* This is implemented as a null separated list of strings.
 * Each string looks like this:  "+X" or "-X"
 * where X is a loc code, dev, class or pci string (as shown above)
 * or empty which is used to indicate all.
 *
 * We interpret this option string list during the buswalk
 * so that it will literally behave left-to-right even if
 * some combinations don't make sense.  Give the user exactly
 * what they want! :)
 */

static int __init eeh_parm(char *str, int state)
{
	char *s, *cur, *curend;
	if (!eeh_opts) {
		eeh_opts = alloc_bootmem(EEH_MAX_OPTS);
		eeh_opts[eeh_opts_last++] = '+'; /* default */
		eeh_opts[eeh_opts_last++] = '\0';
	}
	if (*str == '\0') {
		eeh_opts[eeh_opts_last++] = state ? '+' : '-';
		eeh_opts[eeh_opts_last++] = '\0';
		return 1;
	}
	if (*str == '=')
		str++;
	for (s = str; s && *s != '\0'; s = curend) {
		cur = s;
		while (*cur == ',')
			cur++;	/* ignore empties.  Don't treat as "all-on" or "all-off" */
		curend = strchr(cur, ',');
		if (!curend)
			curend = cur + strlen(cur);
		if (*cur) {
			int curlen = curend-cur;
			char *sym = eeh_opts+eeh_opts_last;
			if (eeh_opts_last + curlen > EEH_MAX_OPTS-2) {
				printk("EEH: sorry...too many eeh cmd line options\n");
				return 1;
			}
			eeh_opts[eeh_opts_last++] = state ? '+' : '-';
			strncpy(eeh_opts+eeh_opts_last, cur, curlen);
			eeh_opts_last += curlen;
			eeh_opts[eeh_opts_last++] = '\0';
		}
	}
	return 1;
}

static int __init eehoff_parm(char *str)
{
	return eeh_parm(str, 0);
}
static int __init eehon_parm(char *str)
{
	return eeh_parm(str, 1);
}


__setup("eeh-off", eehoff_parm);
__setup("eeh-on", eehon_parm);
