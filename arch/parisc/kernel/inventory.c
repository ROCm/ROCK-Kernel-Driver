
/* Copyright (c) 1999 The Puffin Group */
/* Written by David Kennedy and Alex deVries */

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pdc.h>

/*
** Debug options
**    DEBUG_PAT	Dump details which PDC PAT provides about ranges/devices.
*/
#undef DEBUG_PAT

extern char *parisc_getHWtype(unsigned short hw_type);

extern struct hp_device * register_module(void *hpa);
extern void print_devices(char * buf);


int pdc_hpa_processor(void *address);

#ifndef __LP64__ 
static	u8 iodc_data[32] __attribute__ ((aligned (64)));
static	struct pdc_model model __attribute__ ((aligned (8)));
#endif
static	unsigned long pdc_result[32] __attribute__ ((aligned (8))) = {0,0,0,0};
static	struct pdc_hpa processor_hpa __attribute__ ((aligned (8)));
static	struct pdc_system_map module_result __attribute__ ((aligned (8)));
static	struct pdc_module_path module_path __attribute__ ((aligned (8)));

#ifdef __LP64__
#include <asm/pdcpat.h>

int pdc_pat = 0;

/*
**  The module object is filled via PDC_PAT_CELL[Return Cell Module].
**  If a module is found, register module will get the IODC bytes via
**  pdc_iodc_read() using the PA view of conf_base_addr for the hpa parameter.
**
**  The IO view can be used by PDC_PAT_CELL[Return Cell Module]
**  only for SBAs and LBAs.  This view will cause an invalid
**  argument error for all other cell module types.
**
*/

static int
pat_query_module( ulong pcell_loc, ulong mod_index)
{
	extern int num_devices;
	extern struct hp_device devices[];

	pdc_pat_cell_mod_maddr_block_t pa_pdc_cell;
        struct hp_device * dev = &devices[num_devices];
	uint64_t temp;             /* 64-bit scratch value */
	long status;               /* PDC return value status */

	/* return cell module (PA or Processor view) */
	status = pdc_pat_cell_module(& pdc_result, pcell_loc, mod_index,
		PA_VIEW, & pa_pdc_cell);

	if (status != PDC_RET_OK) {
		/* no more cell modules or error */
		return status;
	}

	/*
	** save parameters in the hp_device
	** (The idea being the device driver will call pdc_pat_cell_module()
	** and store the results in it's own data structure.)
	*/
	dev->pcell_loc = pcell_loc;
	dev->mod_index = mod_index;

	/* save generic info returned from the call */
	/* REVISIT: who is the consumer of this? not sure yet... */
	dev->mod_info = pa_pdc_cell.mod_info;	/* pass to PAT_GET_ENTITY() */
	dev->pmod_loc = pa_pdc_cell.mod_location;
	dev->mod_path = pa_pdc_cell.mod_path;

	temp = pa_pdc_cell.cba;
	register_module((void *) PAT_GET_CBA(temp));	/* fills in dev->hpa */

#ifdef DEBUG_PAT
	/* dump what we see so far... */
	switch (PAT_GET_ENTITY(dev->mod_info)) {
		ulong i;

	case PAT_ENTITY_PROC:
		printk ("PAT_ENTITY_PROC: id_eid 0x%lx\n", pa_pdc_cell.mod[0]);
		break;

	case PAT_ENTITY_MEM:
		printk ("PAT_ENTITY_MEM: amount 0x%lx min_gni_base 0x%lx min_gni_len 0x%lx\n",
			pa_pdc_cell.mod[0],
			pa_pdc_cell.mod[1],
			pa_pdc_cell.mod[2]);
		break;
	case PAT_ENTITY_CA:
		printk ("PAT_ENTITY_CA: %ld\n",pcell_loc);
		break;

	case PAT_ENTITY_PBC:
		printk ("PAT_ENTITY_PBC: ");
		goto print_ranges;

	case PAT_ENTITY_SBA:
		printk ("PAT_ENTITY_SBA: ");
		goto print_ranges;

	case PAT_ENTITY_LBA:
		printk ("PAT_ENTITY_LBA: ");

print_ranges:
		printk ("ranges %ld\n", pa_pdc_cell.mod[1]);
		for (i = 0; i < pa_pdc_cell.mod[1]; i++) {
			printk ("	%ld: 0x%016lx 0x%016lx 0x%016lx\n", i,
				pa_pdc_cell.mod[2+i*3],	/* type */
				pa_pdc_cell.mod[3+i*3],	/* start */
				pa_pdc_cell.mod[4+i*3]);	/* finish (ie end) */
		}
		printk("\n");
		break;
	}
#endif /* DEBUG_PAT */
	return PDC_RET_OK;
}


static int do_pat_inventory(void)
{
	ulong mod_index=0;
	int status;
	ulong cell_num;
	ulong pcell_loc;

	pdc_pat = (pdc_pat_cell_get_number(&pdc_result) == PDC_OK);
	if (!pdc_pat)
	{
		return 0;
	}

	cell_num = pdc_result[0];	/* Cell number call was made */

	/* As of PDC PAT ARS 2.5, ret[1] is NOT architected! */
	pcell_loc = pdc_result[1];	/* Physical location of the cell */

#ifdef DEBUG_PAT
	printk("CELL_GET_NUMBER: 0x%lx 0x%lx\n", cell_num, pcell_loc);
#endif

        status = pdc_pat_cell_num_to_loc(&pdc_result, cell_num);
        if (status == PDC_BAD_OPTION)
	{
		/* Prelude (and it's successors: Lclass, A400/500) only
		** implement PDC_PAT_CELL sub-options 0 and 2.
		** "Home cook'n is best anyhow!"
		*/
	} else if (PDC_OK == status) {
		/* so far only Halfdome supports this */
		pcell_loc = pdc_result[0];
	} else {
		panic("WTF? CELL_GET_NUMBER give me invalid cell number?");
	}

	while (PDC_RET_OK == pat_query_module(pcell_loc, mod_index))
	{
		mod_index++;
	}

	return mod_index;
}
#endif /* __LP64__ */

static int do_newer_workstation_inventory(void)
{
	long status;
	int i, num = 0;

	/* So the idea here is to simply try one SYSTEM_MAP call.  If 
	   that one works, great, otherwise do it another way */

	status = pdc_system_map_find_mods(&module_result,&module_path,0);

	if (status == PDC_RET_OK) {
		/* This is for newer non-PDC-PAT boxes */

		printk("a newer box...\n");
		for(i=0, status=PDC_RET_OK; status != PDC_RET_NE_PROC && 
			status != PDC_RET_NE_MOD ;i++) {

			status = pdc_system_map_find_mods(&module_result,&module_path,i);
			if (status == PDC_RET_OK) {
				num++;
				register_module(module_result.mod_addr);
			}
		}
	}

	return (num > 0);
}

#ifndef __LP64__
static	struct  pdc_memory_map r_addr __attribute__ ((aligned (8)));

static int really_do_oldhw_inventory(void)
{
	int i, mod, num = 0;
	int status;
	unsigned int hw_type;
	unsigned int func;

	/* This is undocumented at the time of writing, but basically 
	   we're setting up mod_path so that bc[0..4]=0xff, and step
	   through mod to get the "Path Structure for GSC Modules".  If
	   it works, use the returned HPA and determine the hardware type.  */

	for (i=0;i<6;i++) module_path.bc[i]=0xff;

	for (mod=0;mod<16;mod++) {
		char *stype = NULL;

		module_path.mod=mod;
		status = pdc_mem_map_hpa(&r_addr, &module_path);
		if (status!=PDC_RET_OK) continue;
	
		status = pdc_iodc_read(&pdc_result,(void *) r_addr.hpa,
			0, &iodc_data,32 );
		if (status!=PDC_RET_OK) continue;
		hw_type = iodc_data[3]&0x1f;

		switch (hw_type)
		{
		case HPHW_NPROC:	/* 0 */
			stype="Processor"; break;

		case HPHW_MEMORY:	/* 1 */
			stype="Memory"; break;

		case HPHW_B_DMA:	/* 2 */
			stype="Type B DMA"; break;

		case HPHW_A_DMA:	/* 4 */
			stype="Type A DMA"; break;

		case HPHW_A_DIRECT:	/* 5 */
			stype="Type A Direct"; break;

		case HPHW_BCPORT:	/* 7 */
			stype="Bus Converter Port"; break;

		case HPHW_CONSOLE:	/* 9 */
			stype="Console"; break;

		case HPHW_FIO:		/* 10 - Graphics */
			stype="Foreign I/O (Graphics)"; break;

		case HPHW_BA:		/* 11 - Bus Adapter */
			stype="Bus Adapter"; break;

		case HPHW_IOA:		/* 12 */
			stype="I/O Adapter"; break;

		case HPHW_BRIDGE:	/* 13 */
			stype="Bridge"; break;

		case HPHW_FABRIC:	/* 14 */
			stype="Fabric"; break;

		case HPHW_FAULTY:	/* 31 */
			stype="Faulty HW"; break;

		case HPHW_OTHER:	/* 42 */
		default:	
			printk("Don't know this hw_type: %d\n", hw_type);
			break;
		}

		// This is kluged. But don't want to replicate code for
		// most of the above cases.
		if (stype) {
#ifdef DBG_PDC_QUERY
			// parisc/kernel/drivers.c
			extern int num_devices; 
			extern struct hp_device devices[];
			struct hp_hardware *h;
#endif

			status = pdc_mem_map_hpa(&r_addr, &module_path);
			if (status==PDC_RET_OK && register_module((void *) r_addr.hpa) != NULL)
					num++;


		    if (hw_type == HPHW_BA) {
			/* Now, we're checking for devices for each
			   module.  I seem to think that the
			   modules in question are Lasi (2), 2nd Lasi (6)
			   Wax (5).  To do this, set bc[5]=0, and set
			   bc[4] to the module, and step through the
			   functions. */

			for (i=0;i<4;i++) module_path.bc[i]=0xff;
			module_path.bc[4]=mod;
			for (func=0;func<16;func++) {
				module_path.mod = func;
				module_path.bc[5]=0;
				status = pdc_mem_map_hpa(&r_addr, &module_path);
				if (status!=PDC_RET_OK) continue;
				if (register_module((void *) r_addr.hpa) != NULL) 
					num++;
			}
		}
		// reset module_path.bc[]
		for (i=0;i<6;i++) module_path.bc[i]=0xff;


#ifdef DBG_PDC_QUERY
//
// Let print_devices() dump everything which is registered.
//
			h = devices[num_devices-1].reference;

			if (h) stype = h->name;
			printk("Found %s at %d\n", stype, module_path.mod);
#endif
		}
	}
	return num;
}

static int
do_old_inventory(void)
{
        unsigned int bus_id;
	long status;
	int ok = 0;

	printk(" an older box...\n");

	/* Here, we're going to check the model, and decide
	   if we should even bother trying. */

	status = pdc_model_info(&model);

	bus_id = (model.hversion >> (4+7) ) &0x1f;

	/* Here, we're checking the HVERSION of the CPU.
	   We're only checking the 0th CPU, since it'll
	   be the same on an SMP box. */

	switch (bus_id) {
		case 0x4: 	/* 720, 730, 750, 735, 755 */
		case 0x6: 	/* 705, 710 */
		case 0x7: 	/* 715, 725 */
		case 0x8: 	/* 745, 747, 742 */
		case 0xA: 	/* 712 and similiar */
		case 0xC: 	/* 715/64, at least */

		/* Do inventory using MEM_MAP */
		really_do_oldhw_inventory();
		ok = 1;
		break;
	default: 	/* Everything else */
		printk("This is a very very old machine, with a bus_id of 0x%x.\n",bus_id);
		panic("This will probably never run Linux.\n");
	}

	return ok;
}

#endif /* !__LP64__ */

void do_inventory(void){
	if((pdc_hpa_processor(&processor_hpa))<0){
		printk(KERN_INFO "Couldn't get the HPA of the processor.\n" );
	}

	printk("Searching for devices in PDC firmware... ");
	printk("processor hpa 0x%lx\n", processor_hpa.hpa);

	if (!(
		do_newer_workstation_inventory()
#ifdef __LP64__
		|| do_pat_inventory()
#else /* __LP64__ */
		|| do_old_inventory()
#endif /* __LP64__ */
	    ))
	{
	    panic("I can't get the hardware inventory on this machine");
	}
	print_devices(NULL);
}

