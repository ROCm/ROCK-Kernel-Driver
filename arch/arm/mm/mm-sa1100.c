/*
 *  linux/arch/arm/mm/mm-sa1100.c
 *
 *  Copyright (C) 1998-1999 Russell King
 *  Copyright (C) 1999 Hugo Fiennes
 *
 *  Extra MM routines for the SA1100 architecture
 *
 *  1999/12/04 Nicolas Pitre <nico@cam.org>
 *	Converted memory definition for struct meminfo initialisations.
 *	Memory is listed physically now.
 *
 *  2000/04/07 Nicolas Pitre <nico@cam.org>
 *	Reworked for run-time selection of memory definitions
 *
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach-types.h>

#include <asm/mach/map.h>
 
static struct map_desc standard_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */ \
  { 0xf6000000, 0x20000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA0 IO */
  { 0xf7000000, 0x30000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA1 IO */
  { 0xf8000000, 0x80000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCM */
  { 0xfa000000, 0x90000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* SCM */
  { 0xfc000000, 0xa0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* MER */
  { 0xfe000000, 0xb0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* LCD + DMA */
  LAST_DESC
};

/*
 * Typically, static virtual address mappings are as follow:
 * 
 * 0xe8000000-0xefffffff:	flash memory (especially when multiple flash
 * 				banks need to be mapped contigously)
 * 0xf0000000-0xf3ffffff:	miscellaneous stuff (CPLDs, etc.)
 * 0xf4000000-0xf4ffffff:	SA-1111
 * 0xf5000000-0xf5ffffff:	reserved (used by cache flushing area)
 * 0xf6000000-0xffffffff:	reserved (internal SA1100 IO defined above)
 * 
 * Below 0xe8000000 is reserved for vm allocation.
 */

static struct map_desc assabet_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_ASSABET
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* System Registers */
  { 0xf1000000, 0x12000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Board Control Register */
  { 0xf2800000, 0x4b800000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* MQ200 */
  { 0xf4000000, 0x40000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
#endif
  LAST_DESC
};

static struct map_desc bitsy_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_BITSY
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x49000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* EGPIO 0 */
#endif
  LAST_DESC
};

static struct map_desc cerf_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_CERF
  { 0xe8000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x08000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Crystal Chip */
#endif
  LAST_DESC
};

static struct map_desc empeg_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_EMPEG
  { EMPEG_FLASHBASE, 0x00000000, 0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
#endif
  LAST_DESC
};

static struct map_desc graphicsclient_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
  { 0xe8000000, 0x08000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xf0000000, 0x10000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CPLD */
#endif
  LAST_DESC
};

static struct map_desc lart_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_LART
  { 0xe8000000, 0x00000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash memory */
  { 0xec000000, 0x08000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash, alternative location */
#endif
  LAST_DESC
};

static struct map_desc nanoengine_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_NANOENGINE
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* System Registers */
  { 0xf1000000, 0x18A00000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Internal PCI Config Space */
#endif
  LAST_DESC
};

static struct map_desc thinclient_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_THINCLIENT
#if 0
  { 0xe8000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 when JP1 2-4 */
#else
  { 0xe8000000, 0x08000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 when JP1 3-4 */
#endif
  { 0xf0000000, 0x10000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CPLD */
#endif
  LAST_DESC
};

static struct map_desc tifon_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_TIFON
  { 0xe8000000, 0x00000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xe8800000, 0x08000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 2 */
#endif
  LAST_DESC
};

static struct map_desc victor_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_VICTOR
  { 0xe8000000, 0x00000000, 0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
#endif
  LAST_DESC
};

static struct map_desc xp860_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_XP860
  { 0xf4000000, 0x40000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
  { 0xf0000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* SCSI */
  { 0xf1000000, 0x18000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* LAN */
#endif
  LAST_DESC
};

void __init sa1100_map_io(void)
{
	struct map_desc *desc = NULL;

	iotable_init(standard_io_desc);

	if (machine_is_assabet())
		desc = assabet_io_desc;
	else if (machine_is_nanoengine())
		desc = nanoengine_io_desc;
	else if (machine_is_bitsy())
		desc = bitsy_io_desc;
	else if (machine_is_cerf())
		desc = cerf_io_desc;
	else if (machine_is_empeg())
		desc = empeg_io_desc;
	else if (machine_is_graphicsclient())
		desc = graphicsclient_io_desc;
	else if (machine_is_lart())
	        desc = lart_io_desc;
	else if (machine_is_thinclient())
		desc = thinclient_io_desc;
	else if (machine_is_tifon())
		desc = tifon_io_desc;
	else if (machine_is_victor())
		desc = victor_io_desc;
	else if (machine_is_xp860())
		desc = xp860_io_desc;

	if (desc)
		iotable_init(desc);
}


#ifdef CONFIG_DISCONTIGMEM

/*
 * Our node_data structure for discontigous memory.
 * There is 4 possible nodes i.e. the 4 SA1100 RAM banks.
 */

static bootmem_data_t node_bootmem_data[4];

pg_data_t sa1100_node_data[4] =
{ { bdata: &node_bootmem_data[0] },
  { bdata: &node_bootmem_data[1] },
  { bdata: &node_bootmem_data[2] },
  { bdata: &node_bootmem_data[3] } };

#endif

  
/*
 * On Assabet, we must probe for the Neponset board *before* paging_init() 
 * has occured to actually determine the amount of RAM available.  To do so, 
 * we map the appropriate IO section in the page table here in order to 
 * access GPIO registers.
 */
void __init map_sa1100_gpio_regs( void )
{
	unsigned long phys = _GPLR & PMD_MASK;
	unsigned long virt = io_p2v(phys);
	int prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_DOMAIN(DOMAIN_IO);
	pmd_t pmd;
	pmd_val(pmd) = phys | prot;
	set_pmd(pmd_offset(pgd_offset_k(virt), virt), pmd);
}

