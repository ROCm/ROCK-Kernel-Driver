/* 
   Common Flash Interface probe code.
   (C) 2000 Red Hat. GPL'd.
   $Id: cfi_probe.c,v 1.12 2000/07/03 13:29:16 dwmw2 Exp $
*/


#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>


struct mtd_info *cfi_probe(struct map_info *);

static void print_cfi_ident(struct cfi_ident *);
static void check_cmd_set(struct map_info *, int, unsigned long);
static struct cfi_private *cfi_cfi_probe(struct map_info *);

static const char im_name[] = "cfi_probe";

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in mtd are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */
struct mtd_info *cfi_probe(struct map_info *map)
{
	struct mtd_info *mtd = NULL;
	struct cfi_private *cfi;
	/* First probe the map to see if we have CFI stuff there. */
	cfi = cfi_cfi_probe(map);
	
	if (!cfi)
		return NULL;

	map->fldrv_priv = cfi;
	map->im_name = im_name;

	/* OK we liked it. Now find a driver for the command set it talks */

	check_cmd_set(map, 1, cfi->chips[0].start); /* First the primary cmdset */
	check_cmd_set(map, 0, cfi->chips[0].start); /* Then the secondary */
	
	/* check_cmd_set() will have used inter_module_get to increase
	   the use count of the module which provides the command set 
	   driver. If we're quitting, we have to decrease it again.
	*/

	if(cfi->cmdset_setup) {
		mtd = cfi->cmdset_setup(map);
		
		if (mtd)
			return mtd;
		inter_module_put(cfi->im_name);
	}
	printk("No supported Vendor Command Set found\n");
	
	kfree(cfi);
	map->fldrv_priv = NULL;
	return NULL;

}

static int cfi_probe_new_chip(struct map_info *map, unsigned long base,
			      struct flchip *chips, struct cfi_private *cfi)
{
	switch (map->buswidth) {
		
	case 1: {
		unsigned char tmp = map->read8(map, base + 0x55);

		/* If there's a device there, put it in Query Mode */
		map->write8(map, 0x98, base+0x55);
		
		if (map->read8(map,base+0x10) == 'Q' &&
		    map->read8(map,base+0x11) == 'R' &&
		    map->read8(map,base+0x12) == 'Y') {
			printk("%s: Found a CFI device at 0x%lx in 8 bit mode\n", map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				printk("FIXME: Do alias check at line %d of cfi_probe.c\n", __LINE__);
				/* Put it back into Read Mode */
				map->write8(map, 0x98, base+0x55);
			}
			return 1;
		} else {
			if (map->read8(map, base + 0x55) == 0x98) {
				/* It looks like RAM. Put back the stuff we overwrote */
				map->write8(map, tmp, base+0x55);
			}
			return 0;
		}
	}
	
	case 2: {
		__u16 tmp = map->read16(map, base + 0xaa);
		
		/* If there's a device there, put it into Query Mode */
		map->write16(map, 0x9898, base+0xAA);
		
		if (map->read16(map, base+0x20) == cpu_to_le16(0x0051) &&
		    map->read16(map, base+0x22) == cpu_to_le16(0x0052) &&
		    map->read16(map, base+0x24) == cpu_to_le16(0x0059)) {
			printk("%s: Found a CFI device at 0x%lx in 16 bit mode\n", map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				int i;

				for (i=0; i < cfi->numchips; i++) {
					/* This chip should be in read mode if it's one 
					   we've already touched. */
					if (map->read16(map, chips[i].start+0x20) == cpu_to_le16(0x0051) &&
					    map->read16(map, chips[i].start+0x22) == cpu_to_le16(0x0052) &&
					    map->read16(map, chips[i].start+0x24) == cpu_to_le16(0x0059)){
						/* Either the old chip has got 'Q''R''Y' in a most
						   unfortunate place, or it's an alias of the new
						   chip. Double-check that it's in read mode, and check. */
						map->write16(map, 0xffff, chips[i].start+0x20);
						if (map->read16(map, chips[i].start+0x20) == cpu_to_le16(0x0051) &&
						    map->read16(map, chips[i].start+0x22) == cpu_to_le16(0x0052) &&
						    map->read16(map, chips[i].start+0x24) == cpu_to_le16(0x0059)) {
							/* Yes it's got QRY for data. Most unfortunate. 
							   Stick the old one in read mode too. */
							map->write16(map, 0xffff, base);
							if (map->read16(map, base+0x20) == cpu_to_le16(0x0051) &&
							    map->read16(map, base+0x22) == cpu_to_le16(0x0052) &&
							    map->read16(map, base+0x24) == cpu_to_le16(0x0059)) {
								/* OK, so has the new one. Assume it's an alias */
								printk("T'was probably an alias for the chip at 0x%lx\n", chips[i].start);
								return 1;
							} /* else no, they're different, fall through. */
						} else {
							/* No the old one hasn't got QRY for data. Therefore,
							   it's an alias of the new one. */
							map->write16(map, 0xffff, base+0xaa);
							/* Just to be paranoid. */
							map->write16(map, 0xffff, chips[i].start+0xaa);
							printk("T'was an alias for the chip at 0x%lx\n", chips[i].start);
							return 1;
						}
					} 
					/* No, the old one didn't look like it's in query mode. Next. */
				}
				
				/* OK, if we got to here, then none of the previous chips appear to
				   be aliases for the current one. */
				if (cfi->numchips == MAX_CFI_CHIPS) {
					printk("%s: Too many flash chips detected. Increase MAX_CFI_CHIPS from %d.\n", map->name, MAX_CFI_CHIPS);
					/* Doesn't matter about resetting it to Read Mode - we're not going to talk to it anyway */
					return 1;
				}
				printk("Not an alias. Adding\n");
				chips[cfi->numchips].start = base;
				chips[cfi->numchips].state = FL_READY;
				chips[cfi->numchips].mutex = &chips[cfi->numchips]._spinlock;
				cfi->numchips++;

				/* Put it back into Read Mode */
				map->write16(map, 0xffff, base+0xaa);
			}
			
			return 1;
		}	
		else if (map->read16(map, base+0x20) == 0x5151 &&
			 map->read16(map, base+0x22) == 0x5252 &&
			 map->read16(map, base+0x24) == 0x5959) {
			printk("%s: Found a coupled pair of CFI devices at %lx in 8 bit mode\n",
			       map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				printk("FIXME: Do alias check at line %d of cfi_probe.c\n", __LINE__);

				/* Put it back into Read Mode */
				map->write16(map, 0xffff, base+0xaa);
			}

			return 2;
		} else {
			if (map->read16(map, base+0xaa) == 0x9898) {
				/* It looks like RAM. Put back the stuff we overwrote */
				map->write16(map, tmp, base+0xaa);
			}
			return 0;
		}
	}

		
	case 4: {
		__u32 tmp = map->read32(map, base+0x154);
		
		/* If there's a device there, put it into Query Mode */
		map->write32(map, 0x98989898, base+0x154);
		
		if (map->read32(map, base+0x40) == cpu_to_le32(0x00000051) &&
		    map->read32(map, base+0x44) == cpu_to_le32(0x00000052) &&
		    map->read32(map, base+0x48) == cpu_to_le32(0x00000059)) {
				/* This isn't actually in the CFI spec - only x8 and x16 are. */
			printk("%s: Found a CFI device at %lx in 32 bit mode\n", map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				printk("FIXME: Do alias check at line %d of cfi_probe.c\n", __LINE__);

				/* Put it back into read mode */
				map->write32(map, 0xffffffff, base+0x154);
			}
			return 1;
		} 
		else if (map->read32(map, base+0x40) == cpu_to_le32(0x00510051) &&
			 map->read32(map, base+0x44) == cpu_to_le32(0x00520052) &&
			 map->read32(map, base+0x48) == cpu_to_le32(0x00590059)) {
			printk("%s: Found a coupled pair of CFI devices at location %lx in 16 bit mode\n", map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				printk("FIXME: Do alias check at line %d of cfi_probe.c\n", __LINE__);

				/* Put it back into read mode */
				map->write32(map, 0xffffffff, base+0x154);
			}
			return 2;
		}
		else if (map->read32(map, base+0x40) == 0x51515151 &&
			 map->read32(map, base+0x44) == 0x52525252 &&
			 map->read32(map, base+0x48) == 0x59595959) {
			printk("%s: Found four side-by-side CFI devices at location %lx in 8 bit mode\n", map->name, base);
			if (chips) {
				/* Check previous chips for aliases */
				printk("FIXME: Do alias check at line %d of cfi_probe.c\n", __LINE__);

				/* Put it back into read mode */
				map->write32(map, 0xffffffff, base+0x154);
			}
			return 4;
		} else {
			if (map->read32(map, base+0x154) == 0x98989898) {
				/* It looks like RAM. Put back the stuff we overwrote */
				map->write32(map, tmp, base+0x154);
			}
			return 0;
		}
	}	
	default:
		printk(KERN_WARNING "cfi_probe called with strange buswidth %d\n", map->buswidth);
		return 0;
	}
}

static struct cfi_private *cfi_cfi_probe(struct map_info *map)
{
	unsigned long base=0;
	struct cfi_private cfi;
	struct cfi_private *retcfi;
	struct flchip chip[MAX_CFI_CHIPS];
	int i;

	memset(&cfi, 0, sizeof(cfi));

	/* The first invocation (with chips == NULL) leaves the device in Query Mode */
	cfi.interleave = cfi_probe_new_chip(map, 0, NULL, NULL);

	if (!cfi.interleave) {
		printk("%s: Found no CFI device at location zero\n", map->name);
		/* Doesn't appear to be CFI-compliant at all */
		return NULL;
	}

	/* Read the Basic Query Structure from the device */

	for (i=0; i<sizeof(struct cfi_ident); i++) {
		((unsigned char *)&cfi.cfiq)[i] = map->read8(map,base + ((0x10 + i)*map->buswidth));
	}

	/* Do any necessary byteswapping */
	cfi.cfiq.P_ID = le16_to_cpu(cfi.cfiq.P_ID);
	cfi.cfiq.P_ADR = le16_to_cpu(cfi.cfiq.P_ADR);
	cfi.cfiq.A_ID = le16_to_cpu(cfi.cfiq.A_ID);
	cfi.cfiq.A_ADR = le16_to_cpu(cfi.cfiq.A_ADR);
	cfi.cfiq.InterfaceDesc = le16_to_cpu(cfi.cfiq.InterfaceDesc);
	cfi.cfiq.MaxBufWriteSize = le16_to_cpu(cfi.cfiq.MaxBufWriteSize);
	
#if 1
	/* Dump the information therein */
	print_cfi_ident(&cfi.cfiq);

	for (i=0; i<cfi.cfiq.NumEraseRegions; i++) {
		__u16 EraseRegionInfoNum = (map->read8(map,base + ((0x2d + (4*i))*map->buswidth))) + 
			(((map->read8(map,(0x2e + (4*i))*map->buswidth)) << 8));
		__u16 EraseRegionInfoSize = (map->read8(map, base + ((0x2f + (4*i))*map->buswidth))) + 
			(map->read8(map, base + ((0x30 + (4*i))*map->buswidth)) << 8);
		
		printk("  Erase Region #%d: BlockSize 0x%4.4X bytes, %d blocks\n",
		       i, EraseRegionInfoSize * 256, EraseRegionInfoNum+1);
	}
	
	printk("\n");
#endif	

	/* Switch the chip back into Read Mode, to make the alias detection work */
	switch(map->buswidth) {
	case 1:
		map->write8(map, 0xff, 0x55);
		break;
	case 2:
		map->write16(map, 0xffff, 0xaa);
		break;
	case 4:
		map->write32(map, 0xffffffff, 0x154);
		break;
	}

	/* OK. We've worked out what it is and we're happy with it. Now see if there are others */

	chip[0].start = 0;
	chip[0].state = FL_READY;
	chip[0].mutex = &chip[0]._spinlock;

	cfi.chipshift =  cfi.cfiq.DevSize;
	cfi.numchips = 1;

	if (!cfi.chipshift) {
		printk("cfi.chipsize is zero. This is bad. cfi.cfiq.DevSize is %d\n", cfi.cfiq.DevSize);
		return NULL;
	}

	for (base = (1<<cfi.chipshift); base < map->size; base += (1<<cfi.chipshift))
		cfi_probe_new_chip(map, base, &chip[0], &cfi);

	retcfi = kmalloc(sizeof(struct cfi_private) + cfi.numchips * sizeof(struct flchip), GFP_KERNEL);

	if (!retcfi)
		return NULL;

	memcpy(retcfi, &cfi, sizeof(cfi));
	memcpy(&retcfi->chips[0], chip, sizeof(struct flchip) * cfi.numchips);
	for (i=0; i< retcfi->numchips; i++) {
		init_waitqueue_head(&retcfi->chips[i].wq);
		spin_lock_init(&retcfi->chips[i]._spinlock);
	}
	return retcfi;
}

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
	if (cfip->qry[0] != 'Q' || cfip->qry[1] != 'R' || cfip->qry[2] != 'Y') {
		printk("Invalid CFI ident structure.\n");
		return;
	}	
		
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
	
	printk("Device size: 0x%X bytes (%d Mb)\n", 1 << cfip->DevSize, 1<< (cfip->DevSize - 20));
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

static void check_cmd_set(struct map_info *map, int primary, unsigned long base)
{
	__u16 adr;
	struct cfi_private *cfi = map->fldrv_priv;
	__u16 type = primary?cfi->cfiq.P_ID:cfi->cfiq.A_ID;
	char probename[32];
	void (*probe_function)(struct map_info *, int, unsigned long);
	
	if (type == P_ID_NONE || type == P_ID_RESERVED)
		return;
	
	sprintf(probename, "cfi_cmdset_%4.4X", type);
	
	probe_function = inter_module_get_request(probename, probename);
	if (probe_function) {
		(*probe_function)(map, primary, base);
		return;
	}

	/* This was a command set we don't know about. Print only the basic info */
	adr = primary?cfi->cfiq.P_ADR:cfi->cfiq.A_ADR;
	
	if (!adr) {
		printk(" No Extended Query Table\n");
	}
	else if (map->read8(map,base+(adr*map->buswidth)) != (primary?'P':'A') ||
		 map->read8(map,base+((adr+1)*map->buswidth)) != (primary?'R':'L') ||
		 map->read8(map,base+((adr+2)*map->buswidth)) != (primary?'I':'T')) {
		printk ("Invalid Extended Query Table at %4.4X: %2.2X %2.2X %2.2X\n",
			adr,
			map->read8(map,base+(adr*map->buswidth)),
			map->read8(map,base+((adr+1)*map->buswidth)),
			map->read8(map,base+((adr+2)*map->buswidth)));
	}
	else {
		printk(" Extended Query Table version %c.%c\n",
		       map->read8(map,base+((adr+3)*map->buswidth)), 
		       map->read8(map,base+((adr+4)*map->buswidth)));
	}
}

static int __init cfi_probe_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &cfi_probe);
	return 0;
}

static void __exit cfi_probe_exit(void)
{
	inter_module_unregister(im_name);
}

module_init(cfi_probe_init);
module_exit(cfi_probe_exit);
