/* arch/parisc/kernel/pdc.c  - safe pdc access routines
 *
 * Copyright 1999 SuSE GmbH Nuernberg (Philipp Rumpf, prumpf@tux.org)
 * portions Copyright 1999 The Puffin Group, (Alex deVries, David Kennedy)
 *
 * only these routines should be used out of the real kernel (i.e. everything
 * using virtual addresses) for obvious reasons */

/*	I think it would be in everyone's best interest to follow this
 *	guidelines when writing PDC wrappers:
 *
 *	 - the name of the pdc wrapper should match one of the macros
 *	   used for the first two arguments
 *	 - don't use caps for random parts of the name
 *	 - use ASSERT_ALIGN to ensure the aligment of the arguments is
 *	   correct
 *	 - use __pa() to convert virtual (kernel) pointers to physical
 *	   ones.
 *	 - the name of the struct used for pdc return values should equal
 *	   one of the macros used for the first two arguments to the
 *	   corresponding PDC call
 *	 - keep the order of arguments
 *	 - don't be smart (setting trailing NUL bytes for strings, return
 *	   something useful even if the call failed) unless you are sure
 *	   it's not going to affect functionality or performance
 *
 *	Example:
 *	int pdc_cache_info(struct pdc_cache_info *cache_info )
 *	{
 *		ASSERT_ALIGN(cache_info, 8);
 *	
 *		return mem_pdc_call(PDC_CACHE,PDC_CACHE_INFO,__pa(cache_info),0);
 *	}
 *					prumpf	991016	
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/real.h>
#include <asm/system.h>


#define ASSERT_ALIGN(ptr, align)					\
	do { if(((unsigned long)(ptr)) & (align-1)) {			\
		printk("PDC: %s:%d  %s() called with "	\
			"unaligned argument from %p", __FILE__, __LINE__, \
			__FUNCTION__, __builtin_return_address(0));	\
									\
		return -1;						\
	} } while(0)
	
/* verify address can be accessed without an HPMC */
int pdc_add_valid(void *address)
{
	ASSERT_ALIGN(address, 4);

	return mem_pdc_call(PDC_ADD_VALID, PDC_ADD_VALID_VERIFY, (unsigned long)address);
}

#if 0
int pdc_chassis_warn(struct pdc_chassis_warn *address)
{
	ASSERT_ALIGN(address, 4);

	return mem_pdc_call(PDC_CHASSIS, PDC_CHASSIS_WARN, __pa(address), 0);
}
#endif

int pdc_chassis_disp(unsigned long disp)
{
	return mem_pdc_call(PDC_CHASSIS, PDC_CHASSIS_DISP, disp);
}

int pdc_chassis_info(void *pdc_result, void *chassis_info, unsigned long len)
{
	ASSERT_ALIGN(pdc_result, 4);
	ASSERT_ALIGN(chassis_info, 4);
	return mem_pdc_call(PDC_CHASSIS,PDC_RETURN_CHASSIS_INFO, 
	        __pa(pdc_result), __pa(chassis_info), len);
}

int pdc_hpa_processor(void *address)
{
	/* We're using 0 for the last parameter just to make sure.
	   It's actually HVERSION dependant.  And remember, life is
	   hard without a backspace. */
	ASSERT_ALIGN(address, 4);

	return mem_pdc_call(PDC_HPA, PDC_HPA_PROCESSOR, __pa(address),0);
}

#if 0
int pdc_hpa_modules(void *address)
{
	return mem_pdc_call(PDC_HPA, PDC_HPA_MODULES, address);
}
#endif

int pdc_iodc_read(void *address, void * hpa, unsigned int index,
	void * iodc_data, unsigned int iodc_data_size)
{
	ASSERT_ALIGN(address, 4);
	ASSERT_ALIGN(iodc_data, 8);
	return mem_pdc_call(PDC_IODC, PDC_IODC_READ, 
		__pa(address), hpa, index, __pa(iodc_data), iodc_data_size);
}


int pdc_system_map_find_mods(void *pdc_mod_info, 
	void *mod_path, int index)
{
	return mem_pdc_call(PDC_SYSTEM_MAP, PDC_FIND_MODULE,
		__pa(pdc_mod_info), __pa(mod_path), (long)index);
}


int pdc_model_info(struct pdc_model *model) {
	ASSERT_ALIGN(model, 8);
	return mem_pdc_call(PDC_MODEL,PDC_MODEL_INFO,__pa(model),0);
}

/* get system model name from PDC ROM (e.g. 9000/715 or 9000/778/B160L) */ 
int pdc_model_sysmodel(char * name)
{
	struct pdc_model_sysmodel sys_model;
	int retval;
	
	ASSERT_ALIGN(&sys_model, 8);
	ASSERT_ALIGN(name, 4);

	sys_model.mod_len = 0;
	retval = mem_pdc_call(PDC_MODEL,PDC_MODEL_SYSMODEL,__pa(&sys_model),
		    OS_ID_HPUX,__pa(name));
	
	if (retval == PDC_RET_OK) 
	    name[sys_model.mod_len] = '\0'; /* add trailing '\0' */
	else
	    name[0] = 0;
	
	return retval;
}

/* id: 0 = cpu revision, 1 = boot-rom-version */
int pdc_model_versions(struct pdc_model_cpuid *cpu_id, int id) {
	return mem_pdc_call(PDC_MODEL,PDC_MODEL_VERSIONS,__pa(cpu_id),id);
}

int pdc_model_cpuid(struct pdc_model_cpuid *cpu_id) {
	cpu_id->cpuid = 0; /* preset zero (call maybe not implemented!) */
	return mem_pdc_call(PDC_MODEL,6,__pa(cpu_id),0);  /* 6="return CPU ID" */
}

int pdc_cache_info(struct pdc_cache_info *cache_info) {
	ASSERT_ALIGN(cache_info, 8);

	return mem_pdc_call(PDC_CACHE,PDC_CACHE_INFO,__pa(cache_info),0);
}

#ifndef __LP64__
int pdc_btlb_info( struct pdc_btlb_info *btlb ) {
	int status;
	status = mem_pdc_call(PDC_BLOCK_TLB,PDC_BTLB_INFO,__pa(btlb),0);
	if (status<0) btlb->max_size = 0;
	return status;
}

int pdc_mem_map_hpa(void *r_addr, void *mod_path) {
	return mem_pdc_call(PDC_MEM_MAP,PDC_MEM_MAP_HPA,
		__pa(r_addr),__pa(mod_path));
}

int pdc_lan_station_id(char *lan_addr, void *net_hpa) {
	struct pdc_lan_station_id id;
	unsigned char *addr;
	
	if (mem_pdc_call(PDC_LAN_STATION_ID, PDC_LAN_STATION_ID_READ,
			__pa(&id), net_hpa) < 0)
		addr = 0;	/* FIXME: else read MAC from NVRAM */
	    else
		addr = id.addr;
	if (addr)
		memmove( lan_addr, addr, PDC_LAN_STATION_ID_SIZE);
	    else
		memset( lan_addr, 0, PDC_LAN_STATION_ID_SIZE);
	return (addr != 0);
}
#endif


/* Similar to PDC_PAT stuff in pdcpat.c - but added for Forte/Allegro boxes */
int pdc_pci_irt_size(void *r_addr, void *hpa)
{
	return mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL_SIZE,
		__pa(r_addr), hpa);

}

int pdc_pci_irt(void *r_addr, void *hpa, void *tbl)
{
	return mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL,
		__pa(r_addr), hpa, __pa(tbl));
}

/* access the TOD clock */
int pdc_tod_read(struct pdc_tod *tod)
{
	ASSERT_ALIGN(tod, 8);
	return mem_pdc_call(PDC_TOD, PDC_TOD_READ, __pa(tod), 0);
}

int pdc_tod_set(unsigned long sec, unsigned long usec)
{
	return mem_pdc_call(PDC_TOD, PDC_TOD_WRITE, sec, usec);
}
