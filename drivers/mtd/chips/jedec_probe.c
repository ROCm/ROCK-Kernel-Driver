/* 
   Common Flash Interface probe code.
   (C) 2000 Red Hat. GPL'd.
   $Id: jedec_probe.c,v 1.3 2001/10/02 15:05:12 dwmw2 Exp $
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/gen_probe.h>


/* Manufacturers */
#define MANUFACTURER_AMD	0x0001
#define MANUFACTURER_FUJITSU	0x0004
#define MANUFACTURER_ATMEL	0x001f
#define MANUFACTURER_ST		0x0020
#define MANUFACTURER_SST	0x00BF
#define MANUFACTURER_TOSHIBA	0x0098

/* AMD */
#define AM29F800BB	0x2258
#define AM29F800BT	0x22D6
#define AM29LV800BB	0x225B
#define AM29LV800BT	0x22DA
#define AM29LV160DT	0x22C4
#define AM29LV160DB	0x2249

/* Atmel */
#define AT49BV16X4	0x00c0
#define AT49BV16X4T	0x00c2

/* Fujitsu */
#define MBM29LV160TE	0x22C4
#define MBM29LV160BE	0x2249

/* ST - www.st.com */
#define M29W800T	0x00D7
#define M29W160DT	0x22C4
#define M29W160DB	0x2249

/* SST */
#define SST39LF800	0x2781
#define SST39LF160	0x2782

/* Toshiba */
#define TC58FVT160	0x00C2
#define TC58FVB160	0x0043


struct amd_flash_info {
	const __u16 mfr_id;
	const __u16 dev_id;
	const char *name;
	const int DevSize;
	const int InterfaceDesc;
	const int NumEraseRegions;
	const ulong regions[4];
};

#define ERASEINFO(size,blocks) (size<<8)|(blocks-1)

#define SIZE_1MiB 20
#define SIZE_2MiB 21
#define SIZE_4MiB 22

static const struct amd_flash_info jedec_table[] = {
	{
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29LV160DT,
		name: "AMD AM29LV160DT",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,31),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29LV160DB,
		name: "AMD AM29LV160DB",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,31)
		}
	}, {
		mfr_id: MANUFACTURER_TOSHIBA,
		dev_id: TC58FVT160,
		name: "Toshiba TC58FVT160",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,31),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_FUJITSU,
		dev_id: MBM29LV160TE,
		name: "Fujitsu MBM29LV160TE",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,31),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_TOSHIBA,
		dev_id: TC58FVB160,
		name: "Toshiba TC58FVB160",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,31)
		}
	}, {
		mfr_id: MANUFACTURER_FUJITSU,
		dev_id: MBM29LV160BE,
		name: "Fujitsu MBM29LV160BE",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,31)
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29LV800BB,
		name: "AMD AM29LV800BB",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,15),
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29F800BB,
		name: "AMD AM29F800BB",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,15),
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29LV800BT,
		name: "AMD AM29LV800BT",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,15),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29F800BT,
		name: "AMD AM29F800BT",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,15),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_AMD,
		dev_id: AM29LV800BB,
		name: "AMD AM29LV800BB",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,15),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_ST,
		dev_id: M29W800T,
		name: "ST M29W800T",
		DevSize: SIZE_1MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,15),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_ST,
		dev_id: M29W160DT,
		name: "ST M29W160DT",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x10000,31),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x04000,1)
		}
	}, {
		mfr_id: MANUFACTURER_ST,
		dev_id: M29W160DB,
		name: "ST M29W160DB",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 4,
		regions: {ERASEINFO(0x04000,1),
			  ERASEINFO(0x02000,2),
			  ERASEINFO(0x08000,1),
			  ERASEINFO(0x10000,31)
		}
	}, {
		mfr_id: MANUFACTURER_ATMEL,
		dev_id: AT49BV16X4,
		name: "Atmel AT49BV16X4",
		DevSize: SIZE_2MiB,
		NumEraseRegions: 3,
		regions: {ERASEINFO(0x02000,8),
			  ERASEINFO(0x08000,2),
			  ERASEINFO(0x10000,30)
		}
	}, {
                mfr_id: MANUFACTURER_ATMEL,
                dev_id: AT49BV16X4T,
                name: "Atmel AT49BV16X4T",
                DevSize: SIZE_2MiB,
                NumEraseRegions: 3,
                regions: {ERASEINFO(0x10000,30),
                          ERASEINFO(0x08000,2),
			  ERASEINFO(0x02000,8)
                }
	} 
};


static int cfi_jedec_setup(struct cfi_private *p_cfi, int index);

static int jedec_probe_chip(struct map_info *map, __u32 base,
			    struct flchip *chips, struct cfi_private *cfi);

struct mtd_info *jedec_probe(struct map_info *map);
#define jedec_read_mfr(map, base, osf) cfi_read(map, base)
#define jedec_read_id(map, base, osf) cfi_read(map, (base)+(osf))

static int cfi_jedec_setup(struct cfi_private *p_cfi, int index)
{
	int i,num_erase_regions;

	printk("Found: %s\n",jedec_table[index].name);

	num_erase_regions = jedec_table[index].NumEraseRegions;
	
	p_cfi->cfiq = kmalloc(sizeof(struct cfi_ident) + num_erase_regions * 4, GFP_KERNEL);
	if (!p_cfi->cfiq) {
		//xx printk(KERN_WARNING "%s: kmalloc failed for CFI ident structure\n", map->name);
		return 0;
	}

	memset(p_cfi->cfiq,0,sizeof(struct cfi_ident));	
	
	p_cfi->cfiq->P_ID = P_ID_AMD_STD;
	p_cfi->cfiq->NumEraseRegions = jedec_table[index].NumEraseRegions;
	p_cfi->cfiq->DevSize = jedec_table[index].DevSize;

	for (i=0; i<num_erase_regions; i++){
		p_cfi->cfiq->EraseRegionInfo[i] = jedec_table[index].regions[i];
	}	
	return 1; 	/* ok */
}

static int jedec_probe_chip(struct map_info *map, __u32 base,
			      struct flchip *chips, struct cfi_private *cfi)
{
	int i;
	int osf = cfi->interleave * cfi->device_type;
	int retried = 0;

	if (!cfi->numchips) {
		switch (cfi->device_type) {
		case CFI_DEVICETYPE_X8:
			cfi->addr_unlock1 = 0x555; 
			cfi->addr_unlock2 = 0x2aa; 
			break;
		case CFI_DEVICETYPE_X16:
			cfi->addr_unlock1 = 0xaaa;
			if (map->buswidth == cfi->interleave) {
				/* X16 chip(s) in X8 mode */
				cfi->addr_unlock2 = 0x555;
			} else {
				cfi->addr_unlock2 = 0x554;
			}
			break;
		case CFI_DEVICETYPE_X32:
			cfi->addr_unlock1 = 0x1555; 
			cfi->addr_unlock2 = 0xaaa; 
			break;
		default:
			printk(KERN_NOTICE "Eep. Unknown jedec_probe device type %d\n", cfi->device_type);
		return 0;
		}
	}

 retry:
	/* Reset */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);

	/* Autoselect Mode */
	cfi_send_gen_cmd(0xaa, cfi->addr_unlock1, base, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x55, cfi->addr_unlock2, base, map, cfi, CFI_DEVICETYPE_X8, NULL);
	cfi_send_gen_cmd(0x90, cfi->addr_unlock1, base, map, cfi, CFI_DEVICETYPE_X8, NULL);

	if (!cfi->numchips) {
		/* This is the first time we're called. Set up the CFI 
		   stuff accordingly and return */
		
		cfi->mfr = jedec_read_mfr(map, base, osf);
		cfi->id = jedec_read_id(map, base, osf);
		
		for (i=0; i<sizeof(jedec_table)/sizeof(jedec_table[0]); i++) {
			if (cfi->mfr == jedec_table[i].mfr_id &&
			    cfi->id == jedec_table[i].dev_id)
				return cfi_jedec_setup(cfi, i);
		}
		if (!retried++) {
			/* Deal with whichever strange chips these were */
			cfi->addr_unlock1 |= cfi->addr_unlock1 << 8;
			cfi->addr_unlock2 |= cfi->addr_unlock2 << 8;
			goto retry;
		}
		return 0;
	}
	
	/* Check each previous chip to see if it's an alias */
	for (i=0; i<cfi->numchips; i++) {
		/* This chip should be in read mode if it's one
		   we've already touched. */
		if (jedec_read_mfr(map, base, osf) == cfi->mfr &&
		    jedec_read_id(map, base, osf) == cfi->id) {
			/* Eep. This chip also looks like it's in autoselect mode.
			   Is it an alias for the new one? */
			
			cfi_send_gen_cmd(0xF0, 0, chips[i].start, map, cfi, cfi->device_type, NULL);
			/* If the device IDs go away, it's an alias */
			if (jedec_read_mfr(map, base, osf) != cfi->mfr ||
			    jedec_read_id(map, base, osf) != cfi->id) {
				printk(KERN_DEBUG "%s: Found an alias at 0x%x for the chip at 0x%lx\n",
				       map->name, base, chips[i].start);
				return 0;
			}
			
			/* Yes, it's actually got the device IDs as data. Most
			 * unfortunate. Stick the new chip in read mode
			 * too and if it's the same, assume it's an alias. */
			/* FIXME: Use other modes to do a proper check */
			cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
			if (jedec_read_mfr(map, base, osf) == cfi->mfr &&
			    jedec_read_id(map, base, osf) == cfi->id) {
				printk(KERN_DEBUG "%s: Found an alias at 0x%x for the chip at 0x%lx\n",
				       map->name, base, chips[i].start);
				return 0;
			}
		}
	}
		
	/* OK, if we got to here, then none of the previous chips appear to
	   be aliases for the current one. */
	if (cfi->numchips == MAX_CFI_CHIPS) {
		printk(KERN_WARNING"%s: Too many flash chips detected. Increase MAX_CFI_CHIPS from %d.\n", map->name, MAX_CFI_CHIPS);
		/* Doesn't matter about resetting it to Read Mode - we're not going to talk to it anyway */
		return -1;
	}
	chips[cfi->numchips].start = base;
	chips[cfi->numchips].state = FL_READY;
	cfi->numchips++;
		
	/* Put it back into Read Mode */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);

	printk(KERN_INFO "%s: Found %d x%d devices at 0x%x in %d-bit mode\n",
	       map->name, cfi->interleave, cfi->device_type*8, base, 
	       map->buswidth*8);
	
	return 1;
}

static struct chip_probe jedec_chip_probe = {
	name: "JEDEC",
	probe_chip: jedec_probe_chip
};

struct mtd_info *jedec_probe(struct map_info *map)
{
	/*
	 * Just use the generic probe stuff to call our CFI-specific
	 * chip_probe routine in all the possible permutations, etc.
	 */
	return mtd_do_chip_probe(map, &jedec_chip_probe);
}

static struct mtd_chip_driver jedec_chipdrv = {
	probe: jedec_probe,
	name: "jedec_probe",
	module: THIS_MODULE
};

int __init jedec_probe_init(void)
{
	register_mtd_chip_driver(&jedec_chipdrv);
	return 0;
}

static void __exit jedec_probe_exit(void)
{
	unregister_mtd_chip_driver(&jedec_chipdrv);
}

module_init(jedec_probe_init);
module_exit(jedec_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Erwin Authried <eauth@softsys.co.at> et al.");
MODULE_DESCRIPTION("Probe code for JEDEC-compliant flash chips");
