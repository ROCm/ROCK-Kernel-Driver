/*
 * PAL & SAL emulation.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */
#include <asm/efi.h>
#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/processor.h>
#include <asm/acpi-ext.h>
#include "fpmem.h"

#define MB	(1024*1024UL)
#define GB	(MB*1024UL)

#define FPROM_BUG()		do {while (1);} while (0)
#define MAX_NODES		128
#define MAX_LSAPICS		512
#define MAX_CPUS		512
#define MAX_CPUS_NODE		4
#define CPUS_PER_NODE		4
#define CPUS_PER_FSB		2
#define CPUS_PER_FSB_MASK	(CPUS_PER_FSB-1)

#define NUM_EFI_DESCS		2

typedef union ia64_nasid_va {
        struct {
                unsigned long off   : 33;       /* intra-region offset */
		unsigned long nasid :  7;	/* NASID */
		unsigned long off2  : 21;	/* fill */
                unsigned long reg   :  3;       /* region number */
        } f;
        unsigned long l;
        void *p;
} ia64_nasid_va;

typedef struct {
	unsigned long	pc;
	unsigned long	gp;
} func_ptr_t;
 
#define IS_VIRTUAL_MODE() 	 ({struct ia64_psr psr; asm("mov %0=psr" : "=r"(psr)); psr.dt;})
#define ADDR_OF(p)		(IS_VIRTUAL_MODE() ? ((void*)((long)(p)+PAGE_OFFSET)) : ((void*) (p)))
#define __fwtab_pa(n,x)		({ia64_nasid_va _v; _v.l = (long) (x); _v.f.nasid = (x) ? (n) : 0; _v.f.reg = 0; _v.l;})

/*
 * The following variables are passed thru registersfrom the configuration file and
 * are set via the _start function.
 */
long		base_nasid;
long		num_cpus;
long		bsp_entry_pc=0;
long		num_nodes;
long		app_entry_pc;
int		bsp_lid;
func_ptr_t	ap_entry;


static char fw_mem[(  sizeof(efi_system_table_t)
		    + sizeof(efi_runtime_services_t)
		    + NUM_EFI_DESCS*sizeof(efi_config_table_t)
		    + sizeof(struct ia64_sal_systab)
		    + sizeof(struct ia64_sal_desc_entry_point)
		    + sizeof(struct ia64_sal_desc_ap_wakeup)
		    + sizeof(acpi_rsdp_t)
		    + sizeof(acpi_rsdt_t)
		    + sizeof(acpi_sapic_t)
		    + MAX_LSAPICS*(sizeof(acpi_entry_lsapic_t))
		    + (1+8*MAX_NODES)*(sizeof(efi_memory_desc_t))
		    + sizeof(ia64_sal_desc_ptc_t) +
		    + MAX_NODES*sizeof(ia64_sal_ptc_domain_info_t) +
		    + MAX_CPUS*sizeof(ia64_sal_ptc_domain_proc_entry_t) +
		    + 1024)] __attribute__ ((aligned (8)));

/*
 * Very ugly, but we need this in the simulator only.  Once we run on
 * real hw, this can all go away.
 */
extern void pal_emulator_static (void);

asm ("
	.text
	.proc pal_emulator_static
pal_emulator_static:
	mov r8=-1
	cmp.eq p6,p7=6,r28		/* PAL_PTCE_INFO */
(p7)	br.cond.sptk.few 1f
	;;
	mov r8=0			/* status = 0 */
	movl r9=0x500000000		/* tc.base */
	movl r10=0x0000000200000003	/* count[0], count[1] */
	movl r11=0x1000000000002000	/* stride[0], stride[1] */
	br.cond.sptk.few rp

1:	cmp.eq p6,p7=14,r28		/* PAL_FREQ_RATIOS */
(p7)	br.cond.sptk.few 1f
	mov r8=0			/* status = 0 */
	movl r9 =0x100000064		/* proc_ratio (1/100) */
	movl r10=0x100000100		/* bus_ratio<<32 (1/256) */
	movl r11=0x10000000a		/* itc_ratio<<32 (1/100) */

1:	cmp.eq p6,p7=22,r28		/* PAL_MC_DRAIN */
(p7)	br.cond.sptk.few 1f
	mov r8=0
	br.cond.sptk.few rp

1:	cmp.eq p6,p7=23,r28		/* PAL_MC_EXPECTED */
(p7)	br.cond.sptk.few 1f
	mov r8=0
	br.cond.sptk.few rp

1:	br.cond.sptk.few rp
	.endp pal_emulator_static\n");


static efi_status_t
efi_get_time (efi_time_t *tm, efi_time_cap_t *tc)
{
	if (tm) {
		memset(tm, 0, sizeof(*tm));
		tm->year = 2000;
		tm->month = 2;
		tm->day = 13;
		tm->hour = 10;
		tm->minute = 11;
		tm->second = 12;
	}

	if (tc) {
		tc->resolution = 10;
		tc->accuracy = 12;
		tc->sets_to_zero = 1;
	}

	return EFI_SUCCESS;
}

static void
efi_reset_system (int reset_type, efi_status_t status, unsigned long data_size, efi_char16_t *data)
{
	while(1);	/* Is there a pseudo-op to stop medusa */
}

static efi_status_t
efi_success (void)
{
	return EFI_SUCCESS;
}

static efi_status_t
efi_unimplemented (void)
{
	return EFI_UNSUPPORTED;
}

static long
sal_emulator (long index, unsigned long in1, unsigned long in2,
	      unsigned long in3, unsigned long in4, unsigned long in5,
	      unsigned long in6, unsigned long in7)
{
	register long r9 asm ("r9") = 0;
	register long r10 asm ("r10") = 0;
	register long r11 asm ("r11") = 0;
	long status;

	/*
	 * Don't do a "switch" here since that gives us code that
	 * isn't self-relocatable.
	 */
	status = 0;
	if (index == SAL_FREQ_BASE) {
		switch (in1) {
		      case SAL_FREQ_BASE_PLATFORM:
			r9 = 500000000;
			break;

		      case SAL_FREQ_BASE_INTERVAL_TIMER:
			/*
			 * Is this supposed to be the cr.itc frequency
			 * or something platform specific?  The SAL
			 * doc ain't exactly clear on this...
			 */
			r9 = 700000000;
			break;

		      case SAL_FREQ_BASE_REALTIME_CLOCK:
			r9 = 1;
			break;

		      default:
			status = -1;
			break;
		}
	} else if (index == SAL_SET_VECTORS) {
		if (in1 == SAL_VECTOR_OS_BOOT_RENDEZ) {
			func_ptr_t	*fp;
			fp = ADDR_OF(&ap_entry);
			fp->pc = in2;
			fp->gp = in3;
		} else {
			status = -1;
		}
		;
	} else if (index == SAL_GET_STATE_INFO) {
		;
	} else if (index == SAL_GET_STATE_INFO_SIZE) {
		;
	} else if (index == SAL_CLEAR_STATE_INFO) {
		;
	} else if (index == SAL_MC_RENDEZ) {
		;
	} else if (index == SAL_MC_SET_PARAMS) {
		;
	} else if (index == SAL_CACHE_FLUSH) {
		;
	} else if (index == SAL_CACHE_INIT) {
		;
	} else if (index == SAL_UPDATE_PAL) {
		;
	} else {
		status = -1;
	}
	asm volatile ("" :: "r"(r9), "r"(r10), "r"(r11));
	return status;
}


/*
 * This is here to work around a bug in egcs-1.1.1b that causes the
 * compiler to crash (seems like a bug in the new alias analysis code.
 */
void *
id (long addr)
{
	return (void *) addr;
}


/*
 * Fix the addresses in a function pointer by adding base node address
 * to pc & gp.
 */
void
fix_function_pointer(void *fp)
{
	func_ptr_t	*_fp;

	_fp = fp;
	_fp->pc = __fwtab_pa(base_nasid, _fp->pc);
	_fp->gp = __fwtab_pa(base_nasid, _fp->gp);
}


void
sys_fw_init (const char *args, int arglen, int bsp)
{
	/*
	 * Use static variables to keep from overflowing the RSE stack
	 */
	static efi_system_table_t *efi_systab;
	static efi_runtime_services_t *efi_runtime;
	static efi_config_table_t *efi_tables;
	static ia64_sal_desc_ptc_t *sal_ptc;
	static ia64_sal_ptc_domain_info_t *sal_ptcdi;
	static ia64_sal_ptc_domain_proc_entry_t *sal_ptclid;
	static acpi_rsdp_t *acpi_systab;
	static acpi_rsdt_t *acpi_rsdt;
	static acpi_sapic_t *acpi_sapic;
	static acpi_entry_lsapic_t *acpi_lsapic;
	static struct ia64_sal_systab *sal_systab;
	static efi_memory_desc_t *efi_memmap, *md;
	static unsigned long *pal_desc, *sal_desc;
	static struct ia64_sal_desc_entry_point *sal_ed;
	static struct ia64_boot_param *bp;
	static struct ia64_sal_desc_ap_wakeup *sal_apwake;
	static unsigned char checksum = 0;
	static char *cp, *cmd_line, *vendor;
	static int mdsize, domain, last_domain ;
	static int cnode, nasid, cpu, num_memmd, cpus_found;

	/*
	 * Pass the parameter base address to the build_efi_xxx routines.
	 */
	build_init(8LL*GB*base_nasid);

	num_nodes = GetNumNodes();
	num_cpus = GetNumCpus();


	memset(fw_mem, 0, sizeof(fw_mem));

	pal_desc = (unsigned long *) &pal_emulator_static;
	sal_desc = (unsigned long *) &sal_emulator;
	fix_function_pointer(&pal_emulator_static);
	fix_function_pointer(&sal_emulator);

	/* Align this to 16 bytes, probably EFI does this  */
	mdsize = (sizeof(efi_memory_desc_t) + 15) & ~15 ;

	cp = fw_mem;
	efi_systab  = (void *) cp; cp += sizeof(*efi_systab);
	efi_runtime = (void *) cp; cp += sizeof(*efi_runtime);
	efi_tables  = (void *) cp; cp += NUM_EFI_DESCS*sizeof(*efi_tables);
	sal_systab  = (void *) cp; cp += sizeof(*sal_systab);
	sal_ed      = (void *) cp; cp += sizeof(*sal_ed);
	sal_ptc     = (void *) cp; cp += sizeof(*sal_ptc);
	sal_apwake  = (void *) cp; cp += sizeof(*sal_apwake);
	acpi_systab = (void *) cp; cp += sizeof(*acpi_systab);
	acpi_rsdt   = (void *) cp; cp += sizeof(*acpi_rsdt);
	acpi_sapic  = (void *) cp; cp += sizeof(*acpi_sapic);
	acpi_lsapic = (void *) cp; cp += num_cpus*sizeof(*acpi_lsapic);
	vendor 	    = (char *) cp; cp += 32;
	efi_memmap  = (void *) cp; cp += 8*32*sizeof(*efi_memmap);
	sal_ptcdi   = (void *) cp; cp += CPUS_PER_FSB*(1+num_nodes)*sizeof(*sal_ptcdi);
	sal_ptclid  = (void *) cp; cp += ((3+num_cpus)*sizeof(*sal_ptclid)+7)/8*8;
	cmd_line    = (void *) cp;

	if (args) {
		if (arglen >= 1024)
			arglen = 1023;
		memcpy(cmd_line, args, arglen);
	} else {
		arglen = 0;
	}
	cmd_line[arglen] = '\0';
#ifdef BRINGUP
	/* for now, just bring up bash */
	strcpy(cmd_line, "init=/bin/bash");
#else
	strcpy(cmd_line, "");
#endif

	memset(efi_systab, 0, sizeof(efi_systab));
	efi_systab->hdr.signature = EFI_SYSTEM_TABLE_SIGNATURE;
	efi_systab->hdr.revision  = EFI_SYSTEM_TABLE_REVISION;
	efi_systab->hdr.headersize = sizeof(efi_systab->hdr);
	efi_systab->fw_vendor = __fwtab_pa(base_nasid, vendor);
	efi_systab->fw_revision = 1;
	efi_systab->runtime = __fwtab_pa(base_nasid, efi_runtime);
	efi_systab->nr_tables = 2;
	efi_systab->tables = __fwtab_pa(base_nasid, efi_tables);
	memcpy(vendor, "S\0i\0l\0i\0c\0o\0n\0-\0G\0r\0a\0p\0h\0i\0c\0s\0\0", 32);

	efi_runtime->hdr.signature = EFI_RUNTIME_SERVICES_SIGNATURE;
	efi_runtime->hdr.revision = EFI_RUNTIME_SERVICES_REVISION;
	efi_runtime->hdr.headersize = sizeof(efi_runtime->hdr);
	efi_runtime->get_time = __fwtab_pa(base_nasid, &efi_get_time);
	efi_runtime->set_time = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->get_wakeup_time = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->set_wakeup_time = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->set_virtual_address_map = __fwtab_pa(base_nasid, &efi_success);
	efi_runtime->get_variable = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->get_next_variable = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->set_variable = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->get_next_high_mono_count = __fwtab_pa(base_nasid, &efi_unimplemented);
	efi_runtime->reset_system = __fwtab_pa(base_nasid, &efi_reset_system);

	efi_tables->guid = SAL_SYSTEM_TABLE_GUID;
	efi_tables->table = __fwtab_pa(base_nasid, sal_systab);
	efi_tables++;
	efi_tables->guid = ACPI_TABLE_GUID;
	efi_tables->table = __fwtab_pa(base_nasid, acpi_systab);
	fix_function_pointer(&efi_unimplemented);
	fix_function_pointer(&efi_get_time);
	fix_function_pointer(&efi_success);
	fix_function_pointer(&efi_reset_system);

	/* fill in the ACPI system table: */
	memcpy(acpi_systab->signature, "RSD PTR ", 8);
	acpi_systab->rsdt = (acpi_rsdt_t*)__fwtab_pa(base_nasid, acpi_rsdt);

	memcpy(acpi_rsdt->header.signature, "RSDT",4);
	acpi_rsdt->header.length = sizeof(acpi_rsdt_t);
	memcpy(acpi_rsdt->header.oem_id, "SGI", 3);
	memcpy(acpi_rsdt->header.oem_table_id, "SN1", 3);
	acpi_rsdt->header.oem_revision = 0x00010001;
	acpi_rsdt->entry_ptrs[0] = __fwtab_pa(base_nasid, acpi_sapic);

	memcpy(acpi_sapic->header.signature, "SPIC ", 4);
	acpi_sapic->header.length = sizeof(acpi_sapic_t)+num_cpus*sizeof(acpi_entry_lsapic_t);
	for (cnode=0; cnode<num_nodes; cnode++) {
		nasid = GetNasid(cnode);
		for(cpu=0; cpu<CPUS_PER_NODE; cpu++) {
			if (!IsCpuPresent(cnode, cpu))
				continue;
			acpi_lsapic->type = ACPI_ENTRY_LOCAL_SAPIC;
			acpi_lsapic->length = sizeof(acpi_entry_lsapic_t);
			acpi_lsapic->acpi_processor_id = cnode*4+cpu;
			acpi_lsapic->flags = LSAPIC_ENABLED|LSAPIC_PRESENT;
			acpi_lsapic->eid = cpu;
			acpi_lsapic->id = nasid;
			acpi_lsapic++;
		}
	}


	/* fill in the SAL system table: */
	memcpy(sal_systab->signature, "SST_", 4);
	sal_systab->size = sizeof(*sal_systab);
	sal_systab->sal_rev_minor = 1;
	sal_systab->sal_rev_major = 0;
	sal_systab->entry_count = 3;

	strcpy(sal_systab->oem_id, "SGI");
	strcpy(sal_systab->product_id, "SN1");

	/* fill in an entry point: */	
	sal_ed->type = SAL_DESC_ENTRY_POINT;
	sal_ed->pal_proc = __fwtab_pa(base_nasid, pal_desc[0]);
	sal_ed->sal_proc = __fwtab_pa(base_nasid, sal_desc[0]);
	sal_ed->gp = __fwtab_pa(base_nasid, sal_desc[1]);

	/* kludge the PTC domain info */
	sal_ptc->type = SAL_DESC_PTC;
	sal_ptc->num_domains = 0;
	sal_ptc->domain_info = __fwtab_pa(base_nasid, sal_ptcdi);
	cpus_found = 0;
	last_domain = -1;
	sal_ptcdi--;
	for (cnode=0; cnode<num_nodes; cnode++) {
		nasid = GetNasid(cnode);
		for(cpu=0; cpu<CPUS_PER_NODE; cpu++) {
			if (IsCpuPresent(cnode, cpu)) {
				domain = cnode*CPUS_PER_NODE + cpu/CPUS_PER_FSB;
				if (domain != last_domain) {
					sal_ptc->num_domains++;
					sal_ptcdi++;
					sal_ptcdi->proc_count = 0;
					sal_ptcdi->proc_list = __fwtab_pa(base_nasid, sal_ptclid);
					last_domain = domain;
				}
				sal_ptcdi->proc_count++;
				sal_ptclid->id = nasid;
				sal_ptclid->eid = cpu;
				sal_ptclid++;
				cpus_found++;
			}
		}
	}

	if (cpus_found != num_cpus)
		FPROM_BUG();

	/* Make the AP WAKEUP entry */
	sal_apwake->type = SAL_DESC_AP_WAKEUP;
	sal_apwake->mechanism = IA64_SAL_AP_EXTERNAL_INT;
	sal_apwake->vector = 18;

	for (cp = (char *) sal_systab; cp < (char *) efi_memmap; ++cp)
		checksum += *cp;

	sal_systab->checksum = -checksum;

	md = &efi_memmap[0];
	num_memmd = build_efi_memmap((void *)md, mdsize) ;

	bp = id(ZERO_PAGE_ADDR + (((long)base_nasid)<<33));
	bp->efi_systab = __fwtab_pa(base_nasid, &fw_mem);
	bp->efi_memmap = __fwtab_pa(base_nasid, efi_memmap);
	bp->efi_memmap_size = num_memmd*mdsize;
	bp->efi_memdesc_size = mdsize;
	bp->efi_memdesc_version = 0x101;
	bp->command_line = __fwtab_pa(base_nasid, cmd_line);
	bp->console_info.num_cols = 80;
	bp->console_info.num_rows = 25;
	bp->console_info.orig_x = 0;
	bp->console_info.orig_y = 24;
	bp->num_pci_vectors = 0;
	bp->fpswa = 0;

	/*
	 * Now pick the BSP & store it LID value in
	 * a global variable. Note if BSP is greater than last cpu,
	 * pick the last cpu.
	 */
	for (cnode=0; cnode<num_nodes; cnode++) {
		for(cpu=0; cpu<CPUS_PER_NODE; cpu++) {
			if (!IsCpuPresent(cnode, cpu))
				continue;
			bsp_lid = (GetNasid(cnode)<<24) | (cpu<<16);
			if (bsp-- > 0)
				continue;
			return;
		}
	}
}
