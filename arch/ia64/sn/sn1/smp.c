/*
 * SN1 Platform specific SMP Support
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 Jack Steiner <steiner@sgi.com>
 */



#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/sn/mmzone_sn1.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/current.h>
#include <asm/sn/sn_cpuid.h>




/*
 * The following structure is used to pass params thru smp_call_function
 * to other cpus for flushing TLB ranges.
 */
typedef struct {
	unsigned long	start;
	unsigned long	end;
	unsigned long	nbits;
} ptc_params_t;


/*
 * The following table/struct is for remembering PTC coherency domains. It
 * is also used to translate sapicid into cpuids. We dont want to start 
 * cpus unless we know their cache domain.
 */
#ifdef PTC_NOTYET
sn_sapicid_info_t	sn_sapicid_info[NR_CPUS];
#endif



#ifdef PTC_NOTYET
/*
 * NOTE: This is probably not good enough, but I dont want to try to make
 * it better until I get some statistics on a running system. 
 * At a minimum, we should only send IPIs to 1 processor in each TLB domain
 * & have it issue a ptc.g on it's own FSB. Also, serialize per FSB, not 
 * globally.
 *
 * More likely, we will have to do some work to reduce the frequency of calls to
 * this routine.
 */

static void
sn1_ptc_local(void *arg)
{
	ptc_params_t	*params = arg;
	unsigned long	start, end, nbits;

	start = params->start;
	end = params->end;
	nbits = params->nbits;

	do {
		__asm__ __volatile__ ("ptc.l %0,%1" :: "r"(start), "r"(nbits<<2) : "memory");
		start += (1UL << nbits);
	} while (start < end);
}


void
sn1_ptc_global (unsigned long start, unsigned long end, unsigned long nbits)
{
	ptc_params_t	params;

	params.start = start;
	params.end = end;
	params.nbits = nbits;

	if (smp_call_function(sn1_ptc_local, &params, 1, 0) != 0)
		panic("Unable to do ptc_global - timed out");

	sn1_ptc_local(&params);
}
#endif




void
sn1_send_IPI(int cpuid, int vector, int delivery_mode, int redirect)
{
	long		*p, nasid, slice;
	static int 	off[4] = {0x1800080, 0x1800088, 0x1a00080, 0x1a00088};

	/*
	 * ZZZ - Replace with standard macros when available.
	 */
	nasid = cpuid_to_nasid(cpuid);
	slice = cpuid_to_slice(cpuid);
	p = (long*)(0xc0000a0000000000LL | (nasid<<33) | off[slice]);

#if defined(ZZZBRINGUP)
	{
	static int count=0;
	if (count++ < 10) printk("ZZ sendIPI 0x%x->0x%x, vec %d, nasid 0x%lx, slice %ld, adr 0x%lx\n",
		smp_processor_id(), cpuid, vector, nasid, slice, (long)p);
	}
#endif
	mb();
	*p = (delivery_mode << 8) | (vector & 0xff);
	
}


#ifdef CONFIG_SMP

static void __init
process_sal_ptc_domain_info(ia64_sal_ptc_domain_info_t *di, int domain)
{
#ifdef PTC_NOTYET
	ia64_sal_ptc_domain_proc_entry_t	*pe;
	int 					i, sapicid, cpuid;

	pe = __va(di->proc_list);
	for (i=0; i<di->proc_count; i++, pe++) {
		sapicid = id_eid_to_sapicid(pe->id, pe->eid);
		cpuid = cpu_logical_id(sapicid);
		sn_sapicid_info[cpuid].domain = domain;
		sn_sapicid_info[cpuid].sapicid = sapicid;
	}
#endif
}


static void __init
process_sal_desc_ptc(ia64_sal_desc_ptc_t *ptc)
{
	ia64_sal_ptc_domain_info_t	*di;
	int i;

	di = __va(ptc->domain_info);
	for (i=0; i<ptc->num_domains; i++, di++) {
		process_sal_ptc_domain_info(di, i);	
	}
}


void __init
init_sn1_smp_config(void)
{

	if (!ia64_ptc_domain_info)  {
		printk("SMP: Can't find PTC domain info. Forcing UP mode\n");
		smp_num_cpus = 1;
		return;
	}

#ifdef PTC_NOTYET
	memset (sn_sapicid_info, -1, sizeof(sn_sapicid_info));
	process_sal_desc_ptc(ia64_ptc_domain_info);
#endif

}

#else /* CONFIG_SMP */

void __init
init_sn1_smp_config(void)
{

#ifdef PTC_NOTYET
	sn_sapicid_info[0].sapicid = hard_processor_sapicid();
#endif
}

#endif /* CONFIG_SMP */
