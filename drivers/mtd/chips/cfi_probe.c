/* 
   Common Flash Interface probe code.
   (C) 2000 Red Hat. GPL'd.
   $Id: cfi_probe.c,v 1.60 2001/06/03 01:32:57 nico Exp $
*/

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

/* #define DEBUG_CFI */

#ifdef DEBUG_CFI
static void print_cfi_ident(struct cfi_ident *);
#endif

int cfi_jedec_setup(struct cfi_private *p_cfi, int index);
int cfi_jedec_lookup(int index, int mfr_id, int dev_id);

static void check_cmd_set(struct map_info *, int, unsigned long);
static struct cfi_private *cfi_cfi_probe(struct map_info *);
struct mtd_info *cfi_probe(struct map_info *map);


static struct mtd_chip_driver cfi_chipdrv = {
	probe: cfi_probe,
	name: "cfi",
	module: THIS_MODULE
};


struct mtd_info *cfi_probe(struct map_info *map)
{
	struct mtd_info *mtd = NULL;
	struct cfi_private *cfi;

	/* First probe the map to see if we have CFI stuff there. */
	cfi = cfi_cfi_probe(map);
	
	if (!cfi)
		return NULL;

	map->fldrv_priv = cfi;
	/* OK we liked it. Now find a driver for the command set it talks */

	check_cmd_set(map, 1, cfi->chips[0].start); /* First the primary cmdset */
	if (!map->fldrv)
		check_cmd_set(map, 0, cfi->chips[0].start); /* Then the secondary */
	
	/* check_cmd_set() will have used inter_module_get to increase
	   the use count of the module which provides the command set 
	   driver. If we're quitting, we have to decrease it again.
	*/

	if(map->fldrv) {
		mtd = map->fldrv->probe(map);
		/* Undo the use count we held onto from inter_module_get */
#ifdef MODULE
		if(map->fldrv->module)
		  __MOD_DEC_USE_COUNT(map->fldrv->module);
#endif
		if (mtd)
			return mtd;
	}
	printk(KERN_WARNING"cfi_probe: No supported Vendor Command Set found\n");
	
	kfree(cfi->cfiq);
	kfree(cfi);
	map->fldrv_priv = NULL;
	return NULL;
}

static __u32 cfi_send_cmd(u_char cmd, __u32 base, struct map_info *map, struct cfi_private *cfi)
{
	return cfi_send_gen_cmd(cmd, 0x55, base, map, cfi, cfi->device_type, NULL);
}

/* check for QRY, or search for jedec id.
   in: interleave,type,mode
   ret: table index, <0 for error
 */
static int cfi_check_qry_or_id(struct map_info *map, __u32 base, int index,
				struct cfi_private *cfi)
{
	__u32 manufacturer_id, device_id;
	int osf = cfi->interleave * cfi->device_type;	// scale factor

	//printk("cfi_check_qry_or_id: base=0x%08lx interl=%d type=%d index=%d\n",base,cfi->interleave,cfi->device_type,index);

	switch(cfi->cfi_mode){
	case 0:
		if (cfi_read(map,base+osf*0x10)==cfi_build_cmd('Q',map,cfi) &&
		    cfi_read(map,base+osf*0x11)==cfi_build_cmd('R',map,cfi) &&
		    cfi_read(map,base+osf*0x12)==cfi_build_cmd('Y',map,cfi))
			return 0;	// ok !
		break;
		
	case 1:
		manufacturer_id = cfi_read(map,base+0*osf);
		device_id 	= cfi_read(map,base+1*osf);
		//printk("cfi_check_qry_or_id: man=0x%lx,id=0x%lx\n",manufacturer_id, device_id);
		
		return cfi_jedec_lookup(index,manufacturer_id,device_id);	
	}
	
	return -1; 	// nothing found
}

static void cfi_qry_mode(struct map_info *map, __u32 base, struct cfi_private *cfi)
{
	switch(cfi->cfi_mode){
	case 0:
		/* Query */
		cfi_send_cmd(0x98, base, map, cfi);
		break;
		
	case 1:
		
		/* Autoselect */
		cfi_send_gen_cmd(0xaa, cfi->addr_unlock1, base, map, cfi, CFI_DEVICETYPE_X8, NULL);
		cfi_send_gen_cmd(0x55, cfi->addr_unlock2, base, map, cfi, CFI_DEVICETYPE_X8, NULL);
		cfi_send_gen_cmd(0x90, cfi->addr_unlock1, base, map, cfi, CFI_DEVICETYPE_X8, NULL);
		break;
	}
}

static int cfi_probe_chip_1(struct map_info *map, __u32 base,
			  struct flchip *chips, struct cfi_private *cfi)
{
	int index;
	__u32 tmp,ofs;
	
	ofs = cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, &tmp);
	
	cfi_qry_mode(map,base,cfi);
	
	index=cfi_check_qry_or_id(map,base,-1,cfi);
	if (index<0) return -1;
	
	if (chips){
		int i;

		for (i=0; i<cfi->numchips; i++){
			/* This chip should be in read mode if it's one
			   we've already touched. */
			if (cfi_check_qry_or_id(map,chips[i].start,index,cfi) >= 0){
				cfi_send_gen_cmd(0xF0, 0, chips[i].start, map, cfi, cfi->device_type, NULL);
				if (cfi_check_qry_or_id(map,chips[i].start,index,cfi) >= 0){
					/* Yes it's got QRY for data. Most unfortunate.
					   Stick the old one in read mode too. */
					cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
					if (cfi_check_qry_or_id(map,base,index,cfi) >= 0){
						/* OK, so has the new one. Assume it's an alias */
						printk(KERN_DEBUG "%s: Found an alias at 0x%x for the chip at 0x%lx\n",
						       map->name, base, chips[i].start);
						return -1;
					}
				} else {
					/* 
					 * FIXME: Is this supposed to work?
					 * The third argument is already
					 * multiplied as this within the
					 * function definition. (Nicolas Pitre)
					 */
					cfi_send_gen_cmd(0xF0, 0, base+0xaa*cfi->interleave * cfi->device_type, map, cfi, cfi->device_type, NULL);
					cfi_send_gen_cmd(0xF0, 0, chips[i].start+0xaa*cfi->interleave * cfi->device_type, map, cfi, cfi->device_type, NULL);
					return -1;
				}
			}
		} /* for i */
		
		/* OK, if we got to here, then none of the previous chips appear to
		   be aliases for the current one. */
		if (cfi->numchips == MAX_CFI_CHIPS) {
			printk(KERN_WARNING"%s: Too many flash chips detected. Increase MAX_CFI_CHIPS from %d.\n", map->name, MAX_CFI_CHIPS);
			/* Doesn't matter about resetting it to Read Mode - we're not going to talk to it anyway */
			return -1;
		}
		chips[cfi->numchips].start = base;
		chips[cfi->numchips].state = FL_READY;
		chips[cfi->numchips].mutex = &chips[cfi->numchips]._spinlock;
		cfi->numchips++;
		
		/* Put it back into Read Mode */
		cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	}
	printk(KERN_INFO "%s: Found %d x%d devices at 0x%x in %d-bit mode\n", map->name, 
	       cfi->interleave, cfi->device_type*8, base, map->buswidth*8);
	
	return index;
}

/*  put dev into qry mode, and try cfi and jedec modes for the given type/interleave
 */
static int cfi_probe_chip(struct map_info *map, __u32 base,
			  struct flchip *chips, struct cfi_private *cfi)
{
	int index;
	cfi->cfi_mode=0;	/* cfi mode */
	
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
		return 0;
	}
	index = cfi_probe_chip_1(map,base,chips,cfi);
	if (index>=0) return index;
	
	cfi->cfi_mode=1;	/* jedec mode */
	index = cfi_probe_chip_1(map,base,chips,cfi);
	if (index>=0) return index;
	
	cfi->addr_unlock1 = 0x5555;
	cfi->addr_unlock2 = 0x2aaa; 
	index = cfi_probe_chip_1(map,base,chips,cfi);
	
	return index;
}

/*
 * Since probeing for CFI chips requires writing to the device problems may
 * occur if the flash is not present and RAM is accessed instead.  For now we
 * assume that the flash is present so we don't check for RAM or replace
 * possibly overwritten data.
 */
static int cfi_probe_new_chip(struct map_info *map, unsigned long base,
			      struct flchip *chips, struct cfi_private *cfi)
{
int index;
	switch (map->buswidth) {
#ifdef CFIDEV_BUSWIDTH_1		
	case CFIDEV_BUSWIDTH_1:
		cfi->interleave = CFIDEV_INTERLEAVE_1;
		cfi->device_type = CFI_DEVICETYPE_X8;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;

		cfi->device_type = CFI_DEVICETYPE_X16;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;
		break;			
#endif

#ifdef CFIDEV_BUSWIDTH_2		
	case CFIDEV_BUSWIDTH_2:
#ifdef CFIDEV_INTERLEAVE_1
		cfi->interleave = CFIDEV_INTERLEAVE_1;
		cfi->device_type = CFI_DEVICETYPE_X16;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;
#endif
#ifdef CFIDEV_INTERLEAVE_2
		cfi->interleave = CFIDEV_INTERLEAVE_2;
		cfi->device_type = CFI_DEVICETYPE_X8;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;

		cfi->device_type = CFI_DEVICETYPE_X16;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;

#endif
		break;			
#endif

#ifdef CFIDEV_BUSWIDTH_4
	case CFIDEV_BUSWIDTH_4:
#ifdef CFIDEV_INTERLEAVE_4
		cfi->interleave = CFIDEV_INTERLEAVE_4;
		cfi->device_type = CFI_DEVICETYPE_X16;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;

		cfi->device_type = CFI_DEVICETYPE_X32;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;

		cfi->device_type = CFI_DEVICETYPE_X8;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;
#endif
#ifdef CFIDEV_INTERLEAVE_2
		cfi->interleave = CFIDEV_INTERLEAVE_2;
		cfi->device_type = CFI_DEVICETYPE_X16;
		index = cfi_probe_chip(map,base,chips,cfi);
		if (index>=0) return index;
#endif
#ifdef CFIDEV_INTERLEAVE_1
                cfi->interleave = CFIDEV_INTERLEAVE_1;
                cfi->device_type = CFI_DEVICETYPE_X32;
                index = cfi_probe_chip(map,base,chips,cfi);
                if (index>=0) return index;
#endif
		break;
#endif
	default:
		printk(KERN_WARNING "cfi_probe called with unsupported buswidth %d\n", map->buswidth);
		return -1;
	} // switch
	return -1;
}

static struct cfi_private *cfi_cfi_probe(struct map_info *map)
{
	unsigned long base=0;
	struct cfi_private cfi;
	struct cfi_private *retcfi;
	struct flchip chip[MAX_CFI_CHIPS];
	int i,index; 
	char num_erase_regions;
 	int ofs_factor;

	memset(&cfi, 0, sizeof(cfi));

	/* The first invocation (with chips == NULL) leaves the device in Query Mode */
	index = cfi_probe_new_chip(map, 0, NULL, &cfi);

	if (index<0) {
		printk(KERN_WARNING"%s: Found no CFI device at location zero\n", map->name);
		/* Doesn't appear to be CFI-compliant at all */
		return NULL;
	}

	/* Read the Basic Query Structure from the device */

 	ofs_factor = cfi.interleave*cfi.device_type;

	/* First, work out the amount of space to allocate */
	if (cfi.cfi_mode==0){
		num_erase_regions = cfi_read_query(map, base + (0x10 + 28)*ofs_factor);

#ifdef DEBUG_CFI
		printk("Number of erase regions: %d\n", num_erase_regions);
#endif

		cfi.cfiq = kmalloc(sizeof(struct cfi_ident) + num_erase_regions * 4, GFP_KERNEL);
		if (!cfi.cfiq) {
			printk(KERN_WARNING "%s: kmalloc failed for CFI ident structure\n", map->name);
			return NULL;
		}

		memset(cfi.cfiq,0,sizeof(struct cfi_ident));	

		cfi.fast_prog=1;		/* CFI supports fast programming */

			/* CFI flash */
		for (i=0; i<(sizeof(struct cfi_ident) + num_erase_regions * 4); i++) {
			((unsigned char *)cfi.cfiq)[i] = cfi_read_query(map,base + (0x10 + i)*ofs_factor);
		}

		/* Do any necessary byteswapping */
		cfi.cfiq->P_ID = le16_to_cpu(cfi.cfiq->P_ID);

		cfi.cfiq->P_ADR = le16_to_cpu(cfi.cfiq->P_ADR);
		cfi.cfiq->A_ID = le16_to_cpu(cfi.cfiq->A_ID);
		cfi.cfiq->A_ADR = le16_to_cpu(cfi.cfiq->A_ADR);
		cfi.cfiq->InterfaceDesc = le16_to_cpu(cfi.cfiq->InterfaceDesc);
		cfi.cfiq->MaxBufWriteSize = le16_to_cpu(cfi.cfiq->MaxBufWriteSize);

		for (i=0; i<cfi.cfiq->NumEraseRegions; i++) {
			cfi.cfiq->EraseRegionInfo[i] = le32_to_cpu(cfi.cfiq->EraseRegionInfo[i]);

#ifdef DEBUG_CFI		
			printk("  Erase Region #%d: BlockSize 0x%4.4X bytes, %d blocks\n",
		       		i, (cfi.cfiq->EraseRegionInfo[i] >> 8) & ~0xff, 
		       		(cfi.cfiq->EraseRegionInfo[i] & 0xffff) + 1);
#endif
		}
	}
	else{
		/* JEDEC flash */
		if (cfi_jedec_setup(&cfi,index)<0){
			printk(KERN_WARNING "cfi_jedec_setup failed\n");
			return NULL;
		}
	}

	if (cfi.cfiq->NumEraseRegions == 0) {
		printk(KERN_WARNING "Number of erase regions is zero\n");
		kfree(cfi.cfiq);
		return NULL;
	}

#ifdef DEBUG_CFI
	/* Dump the information therein */
	print_cfi_ident(cfi.cfiq);
#endif

	cfi_send_cmd(0xFF, base, map, &cfi);

	/* OK. We've worked out what it is and we're happy with it. Now see if there are others */

	chip[0].start = 0;
	chip[0].state = FL_READY;
	chip[0].mutex = &chip[0]._spinlock;

	cfi.chipshift = cfi.cfiq->DevSize;
	cfi.numchips = 1;

	if (!cfi.chipshift) {
		printk(KERN_ERR"cfi.chipsize is zero. This is bad. cfi.cfiq->DevSize is %d\n", cfi.cfiq->DevSize);
		kfree(cfi.cfiq);
		return NULL;
	}
	switch (cfi.interleave) {
	    case 2: cfi.chipshift += 1; break;
	    case 4: cfi.chipshift += 2; break;
	}

	for (base = (1<<cfi.chipshift); base < map->size; base += (1<<cfi.chipshift))
		cfi_probe_chip_1(map, base, &chip[0], &cfi);

	retcfi = kmalloc(sizeof(struct cfi_private) + cfi.numchips * sizeof(struct flchip), GFP_KERNEL);

	if (!retcfi) {
		printk(KERN_WARNING "%s: kmalloc failed for CFI private structure\n", map->name);
		kfree(cfi.cfiq);
		return NULL;
	}
	memcpy(retcfi, &cfi, sizeof(cfi));
	memcpy(&retcfi->chips[0], chip, sizeof(struct flchip) * cfi.numchips);
	for (i=0; i< retcfi->numchips; i++) {
		init_waitqueue_head(&retcfi->chips[i].wq);
		spin_lock_init(&retcfi->chips[i]._spinlock);
		retcfi->chips[i].mutex = &retcfi->chips[i]._spinlock;
	}
	return retcfi;
}

#ifdef DEBUG_CFI
static char *vendorname(__u16 vendor) 
{
	switch (vendor) {
	case P_ID_NONE:
		return "None";
		
	case P_ID_INTEL_EXT:
		return "Intel/Sharp Extended";
		
	case P_ID_AMD_STD:
		return "AMD/Fujitsu Standard";
		
	case P_ID_INTEL_STD:
		return "Intel/Sharp Standard";
		
	case P_ID_AMD_EXT:
		return "AMD/Fujitsu Extended";
		
	case P_ID_MITSUBISHI_STD:
		return "Mitsubishi Standard";
		
	case P_ID_MITSUBISHI_EXT:
		return "Mitsubishi Extended";
		
	case P_ID_RESERVED:
		return "Not Allowed / Reserved for Future Use";
		
	default:
		return "Unknown";
	}
}


static void print_cfi_ident(struct cfi_ident *cfip)
{
#if 0
	if (cfip->qry[0] != 'Q' || cfip->qry[1] != 'R' || cfip->qry[2] != 'Y') {
		printk("Invalid CFI ident structure.\n");
		return;
	}	
#endif		
	printk("Primary Vendor Command Set: %4.4X (%s)\n", cfip->P_ID, vendorname(cfip->P_ID));
	if (cfip->P_ADR)
		printk("Primary Algorithm Table at %4.4X\n", cfip->P_ADR);
	else
		printk("No Primary Algorithm Table\n");
	
	printk("Alternative Vendor Command Set: %4.4X (%s)\n", cfip->A_ID, vendorname(cfip->A_ID));
	if (cfip->A_ADR)
		printk("Alternate Algorithm Table at %4.4X\n", cfip->A_ADR);
	else
		printk("No Alternate Algorithm Table\n");
		
		
	printk("Vcc Minimum: %x.%x V\n", cfip->VccMin >> 4, cfip->VccMin & 0xf);
	printk("Vcc Maximum: %x.%x V\n", cfip->VccMax >> 4, cfip->VccMax & 0xf);
	if (cfip->VppMin) {
		printk("Vpp Minimum: %x.%x V\n", cfip->VppMin >> 4, cfip->VppMin & 0xf);
		printk("Vpp Maximum: %x.%x V\n", cfip->VppMax >> 4, cfip->VppMax & 0xf);
	}
	else
		printk("No Vpp line\n");
	
	printk("Typical byte/word write timeout: %d 탎\n", 1<<cfip->WordWriteTimeoutTyp);
	printk("Maximum byte/word write timeout: %d 탎\n", (1<<cfip->WordWriteTimeoutMax) * (1<<cfip->WordWriteTimeoutTyp));
	
	if (cfip->BufWriteTimeoutTyp || cfip->BufWriteTimeoutMax) {
		printk("Typical full buffer write timeout: %d 탎\n", 1<<cfip->BufWriteTimeoutTyp);
		printk("Maximum full buffer write timeout: %d 탎\n", (1<<cfip->BufWriteTimeoutMax) * (1<<cfip->BufWriteTimeoutTyp));
	}
	else
		printk("Full buffer write not supported\n");
	
	printk("Typical block erase timeout: %d 탎\n", 1<<cfip->BlockEraseTimeoutTyp);
	printk("Maximum block erase timeout: %d 탎\n", (1<<cfip->BlockEraseTimeoutMax) * (1<<cfip->BlockEraseTimeoutTyp));
	if (cfip->ChipEraseTimeoutTyp || cfip->ChipEraseTimeoutMax) {
		printk("Typical chip erase timeout: %d 탎\n", 1<<cfip->ChipEraseTimeoutTyp); 
		printk("Maximum chip erase timeout: %d 탎\n", (1<<cfip->ChipEraseTimeoutMax) * (1<<cfip->ChipEraseTimeoutTyp));
	}
	else
		printk("Chip erase not supported\n");
	
	printk("Device size: 0x%X bytes (%d MiB)\n", 1 << cfip->DevSize, 1<< (cfip->DevSize - 20));
	printk("Flash Device Interface description: 0x%4.4X\n", cfip->InterfaceDesc);
	switch(cfip->InterfaceDesc) {
	case 0:
		printk("  - x8-only asynchronous interface\n");
		break;
		
	case 1:
		printk("  - x16-only asynchronous interface\n");
		break;
		
	case 2:
		printk("  - supports x8 and x16 via BYTE# with asynchronous interface\n");
		break;
		
	case 3:
		printk("  - x32-only asynchronous interface\n");
		break;
		
	case 65535:
		printk("  - Not Allowed / Reserved\n");
		break;
		
	default:
		printk("  - Unknown\n");
		break;
	}
	
	printk("Max. bytes in buffer write: 0x%x\n", 1<< cfip->MaxBufWriteSize);
	printk("Number of Erase Block Regions: %d\n", cfip->NumEraseRegions);
	
}
#endif /* DEBUG_CFI */

typedef void cfi_cmdset_fn_t(struct map_info *, int, unsigned long);

extern cfi_cmdset_fn_t cfi_cmdset_0001;
extern cfi_cmdset_fn_t cfi_cmdset_0002;

static void cfi_cmdset_unknown(struct map_info *map, int primary, unsigned long base)
{
	__u16 adr;
	struct cfi_private *cfi = map->fldrv_priv;
	__u16 type = primary?cfi->cfiq->P_ID:cfi->cfiq->A_ID;
#ifdef HAVE_INTER_MODULE
	char probename[32];
	cfi_cmdset_fn_t *probe_function;

	sprintf(probename, "cfi_cmdset_%4.4X", type);
		
	probe_function = inter_module_get_request(probename, probename);

	if (probe_function) {
		(*probe_function)(map, primary, base);
		return;
	}	
#endif
	printk(KERN_NOTICE "Support for command set %04X not present\n", type);
	/* This was a command set we don't know about. Print only the basic info */
	adr = primary?cfi->cfiq->P_ADR:cfi->cfiq->A_ADR;
	
	if (!adr) {
		printk(" No Extended Query Table\n");
	}
	else {
 		int ofs_factor = cfi->interleave * cfi->device_type;

		if (cfi_read_query(map,base + adr*ofs_factor) != (primary?'P':'A') ||
		    cfi_read_query(map,base + (adr+1)*ofs_factor) != (primary?'R':'L') ||
		    cfi_read_query(map,base + (adr+2)*ofs_factor) != (primary?'I':'T')) {
			printk ("Invalid Extended Query Table at %4.4X: %2.2X %2.2X %2.2X\n",
				adr,
				cfi_read_query(map,base + adr*ofs_factor),
				cfi_read_query(map,base + (adr+1)*ofs_factor),
				cfi_read_query(map,base + (adr+2)*ofs_factor));
		}
		else {
			printk(" Extended Query Table version %c.%c\n",
			       cfi_read_query(map,base + (adr+3)*ofs_factor), 
			       cfi_read_query(map,base + (adr+4)*ofs_factor));
		}
	}
	cfi_send_cmd(0xff, base, map, cfi);
}

static void check_cmd_set(struct map_info *map, int primary, unsigned long base)
{
	struct cfi_private *cfi = map->fldrv_priv;
	__u16 type = primary?cfi->cfiq->P_ID:cfi->cfiq->A_ID;
	
	if (type == P_ID_NONE || type == P_ID_RESERVED)
		return;
	/* Put it in query mode */
	cfi_qry_mode(map,base,cfi);

	switch(type){
		/* Urgh. Ifdefs. The version with weak symbols was
		 * _much_ nicer. Shame it didn't seem to work on
		 * anything but x86, really.
		 * But we can't rely in inter_module_get() because
		 * that'd mean we depend on link order.
		 */
#ifdef CONFIG_MTD_CFI_INTELEXT
	case 0x0001:
	case 0x0003:
		return cfi_cmdset_0001(map, primary, base);
#endif
#ifdef CONFIG_MTD_CFI_AMDSTD
	case 0x0002:
		return cfi_cmdset_0002(map, primary, base);
#endif
	}

	return cfi_cmdset_unknown(map, primary, base);
}


#if LINUX_VERSION_CODE < 0x20212 && defined(MODULE)
#define cfi_probe_init init_module
#define cfi_probe_exit cleanup_module
#endif

mod_init_t cfi_probe_init(void)
{
	register_mtd_chip_driver(&cfi_chipdrv);
	return 0;
}

mod_exit_t cfi_probe_exit(void)
{
	unregister_mtd_chip_driver(&cfi_chipdrv);
}

module_init(cfi_probe_init);
module_exit(cfi_probe_exit);
