/* $Id: cfi_jedec.c,v 1.5 2001/06/02 14:52:23 dwmw2 Exp $ */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>

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
        }, {
		0
	} 
};

int cfi_jedec_lookup(int index, int mfr_id, int dev_id)
{
  	if (index>=0){
		if (jedec_table[index].mfr_id == mfr_id &&
		    jedec_table[index].dev_id == dev_id) return index;
  	}
  	else{
		for (index=0; jedec_table[index].mfr_id; index++){
		    if (jedec_table[index].mfr_id == mfr_id &&
		        jedec_table[index].dev_id == dev_id) return index;
		}
  	}
	return -1;
}

int cfi_jedec_setup(struct cfi_private *p_cfi, int index)
{
int i,num_erase_regions;

	printk("Found: %s\n",jedec_table[index].name);

	num_erase_regions = jedec_table[index].NumEraseRegions;
	
	p_cfi->cfiq = kmalloc(sizeof(struct cfi_ident) + num_erase_regions * 4, GFP_KERNEL);
	if (!p_cfi->cfiq) {
		//xx printk(KERN_WARNING "%s: kmalloc failed for CFI ident structure\n", map->name);
		return -1;
	}

	memset(p_cfi->cfiq,0,sizeof(struct cfi_ident));	
	
	p_cfi->cfiq->P_ID = P_ID_AMD_STD;
	p_cfi->cfiq->NumEraseRegions = jedec_table[index].NumEraseRegions;
	p_cfi->cfiq->DevSize = jedec_table[index].DevSize;

	for (i=0; i<num_erase_regions; i++){
		p_cfi->cfiq->EraseRegionInfo[i] = jedec_table[index].regions[i];
	}	
	return 0; 	/* ok */
}

