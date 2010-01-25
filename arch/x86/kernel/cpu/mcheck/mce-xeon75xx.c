/*
 * Xeon 7500 series specific machine check support code.
 * Copyright 2009, 2010 Intel Corporation
 * Author: Andi Kleen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Implement Xeon 7500 series specific code to retrieve the physical address
 * and DIMM information for corrected memory errors.
 *
 * Interface: mce->aux0/aux1 is mapped to a struct pfa_dimm with pad
 * redefined to DIMM valid bits. Consumers check CPUID and bank and
 * then interpret aux0/aux1
 */

/* #define DEBUG 1 */	/* disable for production */
#define pr_fmt(x) "MCE: " x

#include <linux/moduleparam.h>
#include <linux/pci_ids.h>
#include <linux/hrtimer.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/processor.h>
#include <asm/e820.h>
#include <asm/mce.h>
#include <asm/io.h>

#include "mce-internal.h"

#define PFA_SIG "$PFA"
#define PFA_SIG_LEN 4

/* DIMM description */
struct aux_pfa_dimm {
	u8  fbd_channel_id;
	u8  ddr_channel_id;
	u8  ddr_dimm_id;
	u8  ddr_rank_id;
	u8  ddr_dimm_bank_id;
	u8  ddr_dimm_row_id;
	u8  ddr_dimm_column_id;
	u8  valid;
} __attribute__((packed));

struct pfa_dimm {
	u8  fbd_channel_id;
	u8  ddr_channel_id;
	u8  ddr_dimm_id;
	u8  ddr_rank_id;
	u8  ddr_dimm_bank_id;
	u32 ddr_dimm_row_id;
	u32 ddr_dimm_column_id;
} __attribute__((packed));

/* Memory translation table in memory. */
struct pfa_table {
	u8  sig[PFA_SIG_LEN];	/* Signature: '$PFA' */
	u16 len;		/* total length */
	u16 revision;		/* 0x11 */
	u8  checksum;		/* 8bit sum to zero */
	u8  db_value;		/* mailbox port command value */
	u8  db_port;		/* mailbox port */
	/* end of header; end of checksum */
	u8  command;		/* input command */
	u32 valid;		/* valid input/output bits */
	u16 status;		/* output status */
	u8  socket_id;		/* input socket id*/
	u8  bank_id;		/* input MCE bank id */
	u32 pad1;
	u64 mbox_address;
	u64 physical_addr;	/* physical address */
	struct pfa_dimm dimm[2];
	/*
	 * topology information follows: not used for now.
	 */
} __attribute__((packed));

/* DIMM valid bits in valid: DIMM0: 8..12; DIMM1 16..20 */
#define DIMM_VALID_BITS(val, num) (((val) >> (4 + (num) * 8)) & DIMM_VALID_ALL)
#define DIMM_SET_VALID(val, num)  ((val) << (4 + (num) * 8))

enum {
	MCE_BANK_MBOX0		= 8,
	MCE_BANK_MBOX1		= 9,

	PFA_REVISION		= 0x11,		/* v1.1 */

	/* Status bits for valid field */
	PFA_VALID_MA            = (1 << 0),
	PFA_VALID_SOCKETID      = (1 << 1),
	PFA_VALID_BANKID        = (1 << 2),
	PFA_VALID_PA            = (1 << 3),

	/* DIMM valid bits in valid */
	/* use with DIMM_VALID_BITS/DIMM_SET_VALID for pfa->valid */
	DIMM_VALID_FBD_CHAN      = (1 << 0),
	DIMM_VALID_DDR_CHAN      = (1 << 1),
	DIMM_VALID_DDR_DIMM      = (1 << 2),
	DIMM_VALID_DDR_RANK      = (1 << 3),
	DIMM_VALID_DIMM_BANK     = (1 << 4),
	DIMM_VALID_DIMM_ROW      = (1 << 5),
	DIMM_VALID_DIMM_COLUMN   = (1 << 6),
	DIMM_VALID_ALL		 = 0x7f,

	PFA_DIMM_VALID_MASK	 = DIMM_SET_VALID(DIMM_VALID_ALL, 0)
				 | DIMM_SET_VALID(DIMM_VALID_ALL, 1),

	/* Values for status field */
	PFA_STATUS_SUCCESS      = 0,
	PFA_STATUS_SOCKET_INVALID  = (1 << 1),
	PFA_STATUS_MBOX_INVALID = (1 << 2),
	PFA_STATUS_MA_INVALID   = (1 << 3),
	PFA_STATUS_PA_INVALID   = (1 << 4),

	/* Values for command field */
	PFA_CMD_GET_MEM_CORR_ERR_PA = 0,
	PFA_CMD_PA_TO_DIMM_ADDR     = 1,
	PFA_CMD_DIMM_TO_PA	    = 2,
	PFA_CMD_GET_TOPOLOGY	    = 3,

	/* PCI device IDs and the base register */
	ICH_PFA_CFG             = 0x8c, /* SCRATCH4 */
	PCI_DEVICE_ID_BXB_ICH_LEGACY0 = 0x3422,
};

static struct pfa_table *pfa_table __read_mostly;
static int memerr_max_conv_rate __read_mostly = 100;
static int memerr_min_interval __read_mostly = 500;
static int pfa_lost;	/* for diagnosis */

enum {
	RATE_LIMIT_PERIOD = USEC_PER_SEC, /* in us; period of rate limit */
};

module_param(memerr_max_conv_rate, int, 0644);
MODULE_PARM_DESC(memerr_max_conv_rate,
	"Maximum number of memory error conversions each second; 0 to disable");
module_param(memerr_min_interval, int, 0644);
MODULE_PARM_DESC(memerr_min_interval,
	"Minimum time delta between two memory conversions; in us; default 500");

static int notest;
static int nocsum;
module_param(notest, int, 0);
module_param(nocsum, int, 0);

static u64 encode_dimm(struct pfa_dimm *d, u8 valid)
{
	union {
		struct aux_pfa_dimm d;
		u64 v;
	} p;

	BUILD_BUG_ON(sizeof(struct aux_pfa_dimm) != sizeof(u64));
	p.d.fbd_channel_id = d->fbd_channel_id;
	p.d.ddr_channel_id = d->ddr_channel_id;
	p.d.ddr_dimm_id = d->ddr_dimm_id;
	p.d.ddr_rank_id = d->ddr_rank_id;
	p.d.ddr_dimm_bank_id = d->ddr_dimm_bank_id;
	p.d.ddr_dimm_row_id = d->ddr_dimm_row_id;
	if (p.d.ddr_dimm_row_id != d->ddr_dimm_row_id) /* truncated? */
		valid &= ~DIMM_VALID_DIMM_ROW;
	p.d.ddr_dimm_column_id = d->ddr_dimm_column_id;
	if (p.d.ddr_dimm_column_id != d->ddr_dimm_column_id)
		valid &= ~DIMM_VALID_DIMM_COLUMN;
	p.d.valid = valid;
	pr_debug("PFA fbd_ch %u ddr_ch %u dimm %u rank %u bank %u valid %x\n",
		 d->fbd_channel_id,
		 d->ddr_channel_id,
		 d->ddr_dimm_id,
		 d->ddr_rank_id,
		 d->ddr_dimm_bank_id,
		 valid);
	return p.v;
}

static u8 csum(u8 *table, u16 len)
{
	u8 sum = 0;
	int i;
	for (i = 0; i < len; i++)
		sum += *table++;
	return sum;
}

/*
 * Execute a command through the mailbox interface.
 */
static int
pfa_command(unsigned bank, unsigned socketid, unsigned command, unsigned valid)
{
	pfa_table->bank_id = bank;
	pfa_table->socket_id = socketid;
	pfa_table->valid = valid | PFA_VALID_SOCKETID;
	pfa_table->command = command;

	outb(pfa_table->db_value, pfa_table->db_port);

	mb();	/* Reread fields after they got changed */

	if (pfa_table->status != PFA_STATUS_SUCCESS) {
		pr_debug("Memory PFA command %d failed: socket:%d bank:%d status:%x\n",
			command, socketid, bank, pfa_table->status);
		return -pfa_table->status;
	}
	return 0;
}

/*
 * Retrieve physical address and DIMMs.
 */
static int translate_memory_error(struct mce *m)
{
	struct pfa_table *pfa = pfa_table;
	u64 status;
	int ret;
	u32 valid;
	int cpu = smp_processor_id();

	/* Make sure our structures match the specification */
	BUILD_BUG_ON(offsetof(struct pfa_table, physical_addr) != 0x20);
	BUILD_BUG_ON(offsetof(struct pfa_table, status) != 0x10);
	BUILD_BUG_ON(offsetof(struct pfa_table, physical_addr) != 0x20);
	BUILD_BUG_ON(offsetof(struct pfa_table, dimm[1].ddr_dimm_column_id) !=
			0x3e);

	/* Ask for PA/DIMMs of last error */
	if (pfa_command(m->bank, m->socketid,
			PFA_CMD_GET_MEM_CORR_ERR_PA, PFA_VALID_BANKID) < 0)
		return -1;

	/*
	 * Recheck machine check bank. If the overflow bit was set
	 * there was a race. Don't use the information in this case.
	 */
	rdmsrl(MSR_IA32_MCx_STATUS(m->bank), status);
	if (status & MCI_STATUS_OVER) {
		pr_debug("%d: overflow race on bank %d\n", cpu, m->bank);
		return -1;
	}

	ret = -1;
	valid = pfa->valid;
	if (valid & PFA_VALID_PA) {
		m->status |= MCI_STATUS_ADDRV;
		m->addr = pfa_table->physical_addr;
		pr_debug("%d: got physical address %llx valid %x\n",
			cpu, m->addr, valid);
		ret = 0;
	}

	/* When DIMM information was supplied pass it out */
	if (valid & PFA_DIMM_VALID_MASK) {
		m->aux0 = encode_dimm(&pfa->dimm[0], DIMM_VALID_BITS(valid, 0));
		m->aux1 = encode_dimm(&pfa->dimm[1], DIMM_VALID_BITS(valid, 1));
		ret = 0;
	}

	return ret;
}

/*
 * Xeon 75xx specific mce poll method to retrieve the physical address
 * and DIMM information.
 */
static void xeon75xx_mce_poll(struct mce *m)
{
	static DEFINE_SPINLOCK(convert_lock); /* Protect table and static */
	static unsigned long cperm;
	static ktime_t last, last_int;
	unsigned long flags;
	ktime_t now;
	s64 delta;

	/* Memory error? */
	if (m->bank != MCE_BANK_MBOX0 && m->bank != MCE_BANK_MBOX1)
		return;
	if (m->status & MCI_STATUS_OVER)
		return;
	if (memerr_max_conv_rate == 0)
		return;

	spin_lock_irqsave(&convert_lock, flags);
	/*
	 * Rate limit conversions. The conversion takes some time,
	 * but it's not good to use all the CPU time during a error
	 * flood.
	 * Enforce maximum number per second and minimum interval.
	 * The ktime call should use TSC on this machine and be fast.
	 */
	now = ktime_get();
	delta = ktime_us_delta(now, last);
	if (delta >= RATE_LIMIT_PERIOD) {
		cperm = 0;
		last = now;
	}
	if (ktime_us_delta(now, last_int) >= memerr_min_interval &&
	   ++cperm <= memerr_max_conv_rate) {
		if (translate_memory_error(m) < 0) {
			/* On error stop converting for the next second */
			cperm = memerr_max_conv_rate;
			pr_debug("PFA translation failed\n");
		}
	} else
		pfa_lost++;
	last_int = now;
	spin_unlock_irqrestore(&convert_lock, flags);
}

static struct pci_device_id bxb_mce_pciids[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_BXB_ICH_LEGACY0) },
	{}
};

static int __init xeon75xx_mce_init(void)
{
	u32 addr = 0;
	struct pci_dev *dev;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6 ||
	    boot_cpu_data.x86_model != 0x2e)
		return -ENODEV;

	/*
	 * Get table address from register in IOH.
	 * This just looks up the device, because we don't want to "own" it.
	 */
	dev = NULL;
	while ((dev = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, dev))
			!= NULL) {
		if (!pci_match_id(bxb_mce_pciids, dev))
			continue;
		pci_read_config_dword(dev, ICH_PFA_CFG, &addr);
		if (addr)
			break;
	}
	pci_dev_put(dev);
	if (!addr)
		return -ENODEV;

	if (!e820_all_mapped(addr, addr + PAGE_SIZE, E820_RESERVED)) {
		pr_info("PFA table at %x not e820 reserved\n", addr);
		return -ENODEV;
	}

	pfa_table = (__force struct pfa_table *)ioremap_cache(addr, PAGE_SIZE);
	if (!pfa_table) {
		pr_err("Cannot map PFA table at %x\n", addr);
		return -EIO;
	}

	if (memcmp(&pfa_table->sig, PFA_SIG, PFA_SIG_LEN) ||
	    pfa_table->len < sizeof(struct pfa_table) ||
	    /* assume newer versions are compatible */
	    pfa_table->revision < PFA_REVISION) {
		pr_info("PFA table at %x invalid\n", addr);
		goto error_unmap;
	}

	if (!nocsum && csum((u8 *)pfa_table,
				offsetof(struct pfa_table, command))) {
		pr_info("PFA table at %x length %u has invalid checksum\n",
			addr, pfa_table->len);
		goto error_unmap;
	}

	/* Not strictly needed today */
	if (pfa_table->len > PAGE_SIZE) {
		unsigned len = roundup(pfa_table->len, PAGE_SIZE);
		iounmap(pfa_table);
		pfa_table = (__force void *)ioremap_cache(addr, len);
		if (!pfa_table) {
			pr_err("Cannot remap %u bytes PFA table at %x\n",
				len, addr);
			return -EIO;
		}
	}

	if (!notest) {
		int status = pfa_command(0, 0, PFA_CMD_GET_TOPOLOGY, 0);
		if (status < 0) {
			pr_err("Test of PFA table failed: %x\n", -status);
			goto error_unmap;
		}
	}

	pr_info("Found Xeon75xx PFA memory error translation table at %x\n",
		addr);
	mb();
	mce_cpu_specific_poll = xeon75xx_mce_poll;
	return 0;

error_unmap:
	iounmap(pfa_table);
	return -ENODEV;
}

MODULE_DEVICE_TABLE(pci, bxb_mce_pciids);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andi Kleen");
MODULE_DESCRIPTION("Intel Xeon 75xx specific DIMM error reporting");

#ifdef CONFIG_MODULE
static void __exit xeon75xx_mce_exit(void)
{
	mce_cpu_specific_poll = NULL;
	wmb();
	/* Wait for all machine checks to finish before really unloading */
	synchronize_rcu();
	iounmap(pfa_table);
}

module_init(xeon75xx_mce_init);
module_exit(xeon75xx_mce_exit);
#else
/* When built-in run as soon as the PCI subsystem is up */
fs_initcall(xeon75xx_mce_init);
#endif
