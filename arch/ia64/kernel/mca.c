/*
 * File: 	mca.c
 * Purpose: 	Generic MCA handling layer
 *
 * Updated for latest kernel
 * Copyright (C) 2000 Intel
 * Copyright (C) Chuck Fleckenstein (cfleck@co.intel.com)
 *  
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander(vijay@engr.sgi.com)
 *
 * 00/03/29 C. Fleckenstein  Fixed PAL/SAL update issues, began MCA bug fixes, logging issues, 
 *                           added min save state dump, added INIT handler. 
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/smp_lock.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sal.h>
#include <asm/mca.h>

#include <asm/irq.h>

 
typedef struct ia64_fptr {
	unsigned long fp;
	unsigned long gp;
} ia64_fptr_t;

ia64_mc_info_t			ia64_mc_info;
ia64_mca_sal_to_os_state_t	ia64_sal_to_os_handoff_state;
ia64_mca_os_to_sal_state_t	ia64_os_to_sal_handoff_state;
u64				ia64_mca_proc_state_dump[256];
u64				ia64_mca_stack[1024];
u64				ia64_mca_stackframe[32];
u64				ia64_mca_bspstore[1024];
u64				ia64_init_stack[INIT_TASK_SIZE] __attribute__((aligned(16)));

static void			ia64_mca_cmc_vector_setup(int 		enable, 
							  int_vector_t 	cmc_vector);
static void			ia64_mca_wakeup_ipi_wait(void);
static void			ia64_mca_wakeup(int cpu);
static void			ia64_mca_wakeup_all(void);
static void			ia64_log_init(int,int);
static void			ia64_log_get(int,int, prfunc_t);
static void			ia64_log_clear(int,int,int, prfunc_t);
extern void		        ia64_monarch_init_handler (void);
extern void		        ia64_slave_init_handler (void);

/*
 * hack for now, add platform dependent handlers
 * here
 */
#ifndef PLATFORM_MCA_HANDLERS
void
mca_handler_platform (void)
{

}

void
cmci_handler_platform (int cmc_irq, void *arg, struct pt_regs *ptregs)
{

}
/*
 * This routine will be used to deal with platform specific handling
 * of the init, i.e. drop into the kernel debugger on server machine,
 * or if the processor is part of some parallel machine without a
 * console, then we would call the appropriate debug hooks here.
 */
void
init_handler_platform (struct pt_regs *regs) 
{
	/* if a kernel debugger is available call it here else just dump the registers */
	show_regs(regs);		/* dump the state info */
}

void
log_print_platform ( void *cur_buff_ptr, prfunc_t prfunc)
{
}
	
void
ia64_mca_init_platform (void)
{
}

#endif /* PLATFORM_MCA_HANDLERS */

static char *min_state_labels[] = {
	"nat",
	"r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
	"r9", "r10","r11", "r12","r13","r14", "r15",
	"b0r16","b0r17", "b0r18", "b0r19", "b0r20",
	"b0r21", "b0r22","b0r23", "b0r24", "b0r25",
	"b0r26", "b0r27", "b0r28","b0r29", "b0r30", "b0r31",
	"r16", "r17", "r18","r19", "r20", "r21","r22",
	"r23", "r24","r25", "r26", "r27","r28", "r29", "r30","r31",
	"preds", "br0", "rsc",
	"iip", "ipsr", "ifs",
	"xip", "xpsr", "xfs"
};

int ia64_pmss_dump_bank0=0;  /* dump bank 0 ? */

/*
 * routine to process and prepare to dump min_state_save
 * information for debugging purposes.
 *
 */
void
ia64_process_min_state_save (pal_min_state_area_t *pmss, struct pt_regs *ptregs)
{
	int i, max=57;
	u64 *tpmss_ptr=(u64 *)pmss;

	/* dump out the min_state_area information */

	for (i=0;i<max;i++) {

		if(!ia64_pmss_dump_bank0) {
			if(strncmp("B0",min_state_labels[i],2)==0) {
				tpmss_ptr++;  /* skip to next entry */
				continue;
			}
		} 

		printk("%5s=0x%16.16lx ",min_state_labels[i],*tpmss_ptr++);

		if (((i+1)%3)==0 || ((!strcmp("GR16",min_state_labels[i]))
				     && !ia64_pmss_dump_bank0))
			printk("\n");
	}
	/* hang city for now, until we include debugger or copy to ptregs to show: */
	while (1);
}
 
/*
 * ia64_mca_cmc_vector_setup
 *	Setup the correctable machine check vector register in the processor
 * Inputs
 *	Enable (1 - enable cmc interrupt , 0 - disable)
 *	CMC handler entry point (if enabled)
 *
 * Outputs
 *	None
 */
static void
ia64_mca_cmc_vector_setup(int 		enable, 
			  int_vector_t 	cmc_vector)
{
	cmcv_reg_t	cmcv;

	cmcv.cmcv_regval 	= 0;
	cmcv.cmcv_mask  	= enable;
	cmcv.cmcv_vector 	= cmc_vector;
	ia64_set_cmcv(cmcv.cmcv_regval);
}


#if defined(MCA_TEST)

sal_log_processor_info_t	slpi_buf;

void
mca_test(void)
{
	slpi_buf.slpi_valid.slpi_psi = 1;
	slpi_buf.slpi_valid.slpi_cache_check = 1;
	slpi_buf.slpi_valid.slpi_tlb_check = 1;
	slpi_buf.slpi_valid.slpi_bus_check = 1;
	slpi_buf.slpi_valid.slpi_minstate = 1;
	slpi_buf.slpi_valid.slpi_bank1_gr = 1;
	slpi_buf.slpi_valid.slpi_br = 1;
	slpi_buf.slpi_valid.slpi_cr = 1;
	slpi_buf.slpi_valid.slpi_ar = 1;
	slpi_buf.slpi_valid.slpi_rr = 1;
	slpi_buf.slpi_valid.slpi_fr = 1;

	ia64_os_mca_dispatch();
}

#endif /* #if defined(MCA_TEST) */

/*
 * ia64_mca_init
 *	Do all the mca specific initialization on a per-processor basis.
 *
 *	1. Register spinloop and wakeup request interrupt vectors
 *
 *	2. Register OS_MCA handler entry point
 *
 *	3. Register OS_INIT handler entry point
 *
 *	4. Initialize CMCV register to enable/disable CMC interrupt on the
 *	   processor and hook a handler in the platform-specific ia64_mca_init.
 *
 *	5. Initialize MCA/CMC/INIT related log buffers maintained by the OS.
 *
 * Inputs
 *	None
 * Outputs
 *	None
 */
void __init
ia64_mca_init(void)
{
	ia64_fptr_t *mon_init_ptr = (ia64_fptr_t *)ia64_monarch_init_handler;
	ia64_fptr_t *slave_init_ptr = (ia64_fptr_t *)ia64_slave_init_handler;
	int i;

	IA64_MCA_DEBUG("ia64_mca_init : begin\n");

	/* Clear the Rendez checkin flag for all cpus */
	for(i = 0 ; i < IA64_MAXCPUS; i++)
		ia64_mc_info.imi_rendez_checkin[i] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;

	/* NOTE : The actual irqs for the rendez, wakeup and 
	 * cmc interrupts are requested in the platform-specific
	 * mca initialization code.
	 */
	/* 
	 * Register the rendezvous spinloop and wakeup mechanism with SAL
	 */

	/* Register the rendezvous interrupt vector with SAL */
	if (ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_INT,
				   SAL_MC_PARAM_MECHANISM_INT,
				   IA64_MCA_RENDEZ_INT_VECTOR,
				   IA64_MCA_RENDEZ_TIMEOUT))
		return;

	/* Register the wakeup interrupt vector with SAL */
	if (ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_WAKEUP,
				   SAL_MC_PARAM_MECHANISM_INT,
				   IA64_MCA_WAKEUP_INT_VECTOR,
				   0))
		return;

	IA64_MCA_DEBUG("ia64_mca_init : registered mca rendezvous spinloop and wakeup mech.\n");
	/*
	 * Setup the correctable machine check vector
	 */
	ia64_mca_cmc_vector_setup(IA64_CMC_INT_ENABLE, 
				  IA64_MCA_CMC_INT_VECTOR);

	IA64_MCA_DEBUG("ia64_mca_init : correctable mca vector setup done\n");

	ia64_mc_info.imi_mca_handler 		= __pa(ia64_os_mca_dispatch);
	/*
	 * XXX - disable SAL checksum by setting size to 0; should be
	 *	__pa(ia64_os_mca_dispatch_end) - __pa(ia64_os_mca_dispatch);
	 */
	ia64_mc_info.imi_mca_handler_size	= 0; 
	/* Register the os mca handler with SAL */
	if (ia64_sal_set_vectors(SAL_VECTOR_OS_MCA,
				 ia64_mc_info.imi_mca_handler,
				 __pa(ia64_get_gp()),
				 ia64_mc_info.imi_mca_handler_size,
				 0,0,0))

		return;

	IA64_MCA_DEBUG("ia64_mca_init : registered os mca handler with SAL\n");

	/* 
	 * XXX - disable SAL checksum by setting size to 0, should be
	 * IA64_INIT_HANDLER_SIZE 
	 */
	ia64_mc_info.imi_monarch_init_handler 		= __pa(mon_init_ptr->fp);
	ia64_mc_info.imi_monarch_init_handler_size	= 0;
	ia64_mc_info.imi_slave_init_handler 		= __pa(slave_init_ptr->fp);
	ia64_mc_info.imi_slave_init_handler_size	= 0;

	IA64_MCA_DEBUG("ia64_mca_init : os init handler at %lx\n",ia64_mc_info.imi_monarch_init_handler);

	/* Register the os init handler with SAL */
	if (ia64_sal_set_vectors(SAL_VECTOR_OS_INIT,
				 ia64_mc_info.imi_monarch_init_handler,
				 __pa(ia64_get_gp()),
				 ia64_mc_info.imi_monarch_init_handler_size,
				 ia64_mc_info.imi_slave_init_handler,
				 __pa(ia64_get_gp()),
				 ia64_mc_info.imi_slave_init_handler_size))


		return;

	IA64_MCA_DEBUG("ia64_mca_init : registered os init handler with SAL\n");

	/* Initialize the areas set aside by the OS to buffer the 
	 * platform/processor error states for MCA/INIT/CMC
	 * handling.
	 */
	ia64_log_init(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PROCESSOR);
	ia64_log_init(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PLATFORM);
	ia64_log_init(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PROCESSOR);
	ia64_log_init(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PLATFORM);
	ia64_log_init(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PROCESSOR);
	ia64_log_init(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PLATFORM);

	ia64_mca_init_platform();
	
	IA64_MCA_DEBUG("ia64_mca_init : platform-specific mca handling setup done\n");

#if defined(MCA_TEST)
	mca_test();
#endif /* #if defined(MCA_TEST) */

	printk("Mca related initialization done\n");
}

/*
 * ia64_mca_wakeup_ipi_wait
 *	Wait for the inter-cpu interrupt to be sent by the
 * 	monarch processor once it is done with handling the
 *	MCA.
 * Inputs 
 *	None
 * Outputs
 *	None
 */
void
ia64_mca_wakeup_ipi_wait(void)
{
	int	irr_num = (IA64_MCA_WAKEUP_INT_VECTOR >> 6);
	int	irr_bit = (IA64_MCA_WAKEUP_INT_VECTOR & 0x3f);
	u64	irr = 0;

	do {
		switch(irr_num) {
		case 0:
			irr = ia64_get_irr0();
			break;
		case 1:
			irr = ia64_get_irr1();
			break;
		case 2:
			irr = ia64_get_irr2();
			break;
		case 3:
			irr = ia64_get_irr3();
			break;
		}
	} while (!(irr & (1 << irr_bit))) ;
}

/*
 * ia64_mca_wakeup
 *	Send an inter-cpu interrupt to wake-up a particular cpu
 * 	and mark that cpu to be out of rendez.
 * Inputs
 *	cpuid
 * Outputs
 *	None
 */
void
ia64_mca_wakeup(int cpu)
{
	platform_send_ipi(cpu, IA64_MCA_WAKEUP_INT_VECTOR, IA64_IPI_DM_INT, 0);
	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;
	
}
/*
 * ia64_mca_wakeup_all
 *	Wakeup all the cpus which have rendez'ed previously.
 * Inputs
 *	None
 * Outputs
 *	None
 */
void
ia64_mca_wakeup_all(void)
{
	int cpu;

	/* Clear the Rendez checkin flag for all cpus */
	for(cpu = 0 ; cpu < smp_num_cpus; cpu++)
		if (ia64_mc_info.imi_rendez_checkin[cpu] == IA64_MCA_RENDEZ_CHECKIN_DONE)
			ia64_mca_wakeup(cpu);

}
/*
 * ia64_mca_rendez_interrupt_handler
 *	This is handler used to put slave processors into spinloop 
 *	while the monarch processor does the mca handling and later
 *	wake each slave up once the monarch is done.
 * Inputs 
 *	None
 * Outputs
 *	None
 */
void
ia64_mca_rendez_int_handler(int rendez_irq, void *arg, struct pt_regs *ptregs)
{
	int flags, cpu = 0;
	/* Mask all interrupts */
        save_and_cli(flags);

#ifdef CONFIG_SMP
	cpu = cpu_logical_id(hard_smp_processor_id());
#endif
	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_DONE;
	/* Register with the SAL monarch that the slave has
	 * reached SAL
	 */
	ia64_sal_mc_rendez();

	/* Wait for the wakeup IPI from the monarch 
	 * This waiting is done by polling on the wakeup-interrupt
	 * vector bit in the processor's IRRs
	 */
	ia64_mca_wakeup_ipi_wait();

	/* Enable all interrupts */
	restore_flags(flags);


}


/*
 * ia64_mca_wakeup_int_handler
 *	The interrupt handler for processing the inter-cpu interrupt to the
 * 	slave cpu which was spinning in the rendez loop.
 *	Since this spinning is done by turning off the interrupts and
 *	polling on the wakeup-interrupt bit in the IRR, there is 
 *	nothing useful to be done in the handler.
 *  Inputs
 *	wakeup_irq	(Wakeup-interrupt bit)
 *	arg		(Interrupt handler specific argument)
 *	ptregs		(Exception frame at the time of the interrupt)
 *  Outputs
 *	
 */
void
ia64_mca_wakeup_int_handler(int wakeup_irq, void *arg, struct pt_regs *ptregs)
{
	
}

/*
 * ia64_return_to_sal_check
 *	This is function called before going back from the OS_MCA handler
 * 	to the OS_MCA dispatch code which finally takes the control back
 * 	to the SAL.
 *	The main purpose of this routine is to setup the OS_MCA to SAL
 *	return state which can be used by the OS_MCA dispatch code 
 *	just before going back to SAL.
 * Inputs
 *	None
 * Outputs
 *	None
 */

void
ia64_return_to_sal_check(void)
{
	/* Copy over some relevant stuff from the sal_to_os_mca_handoff
	 * so that it can be used at the time of os_mca_to_sal_handoff
	 */
	ia64_os_to_sal_handoff_state.imots_sal_gp = 
		ia64_sal_to_os_handoff_state.imsto_sal_gp;

	ia64_os_to_sal_handoff_state.imots_sal_check_ra = 
		ia64_sal_to_os_handoff_state.imsto_sal_check_ra;

	/* For now ignore the MCA */
	ia64_os_to_sal_handoff_state.imots_os_status = IA64_MCA_CORRECTED;
}
/* 
 * ia64_mca_ucmc_handler
 *	This is uncorrectable machine check handler called from OS_MCA
 *	dispatch code which is in turn called from SAL_CHECK().
 *	This is the place where the core of OS MCA handling is done.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format. This handler code is supposed to be run only on the
 *	monarch processor. Once the  monarch is done with MCA handling
 *	further MCA logging is enabled by clearing logs.
 *	Monarch also has the duty of sending wakeup-IPIs to pull the
 *	slave processors out of rendez. spinloop.
 * Inputs
 *	None
 * Outputs
 *	None
 */
void
ia64_mca_ucmc_handler(void)
{

	/* Get the MCA processor log */
	ia64_log_get(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);
	/* Get the MCA platform log */
	ia64_log_get(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PLATFORM, (prfunc_t)printk);

	ia64_log_print(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);

	/* 
	 * Do some error handling - Platform-specific mca handler is called at this point
	 */

	mca_handler_platform() ;

	/* Clear the SAL MCA logs */
	ia64_log_clear(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PROCESSOR, 1, printk);
	ia64_log_clear(SAL_INFO_TYPE_MCA, SAL_SUB_INFO_TYPE_PLATFORM, 1, printk);

	/* Wakeup all the processors which are spinning in the rendezvous
	 * loop.
	 */
	ia64_mca_wakeup_all();
	ia64_return_to_sal_check();
}

/* 
 * ia64_mca_cmc_int_handler
 *	This is correctable machine check interrupt handler.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format. 
 * Inputs
 *	None
 * Outputs
 *	None
 */
void
ia64_mca_cmc_int_handler(int cmc_irq, void *arg, struct pt_regs *ptregs)
{	
	/* Get the CMC processor log */
	ia64_log_get(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);
	/* Get the CMC platform log */
	ia64_log_get(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PLATFORM, (prfunc_t)printk);


	ia64_log_print(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);
	cmci_handler_platform(cmc_irq, arg, ptregs);

	/* Clear the CMC SAL logs now that they have been saved in the OS buffer */
	ia64_sal_clear_state_info(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PROCESSOR);
	ia64_sal_clear_state_info(SAL_INFO_TYPE_CMC, SAL_SUB_INFO_TYPE_PLATFORM);
}

/*
 * IA64_MCA log support
 */
#define IA64_MAX_LOGS		2	/* Double-buffering for nested MCAs */
#define IA64_MAX_LOG_TYPES	3	/* MCA, CMC, INIT */
#define IA64_MAX_LOG_SUBTYPES	2	/* Processor, Platform */

typedef struct ia64_state_log_s {
	spinlock_t	isl_lock;
	int		isl_index;
	ia64_psilog_t	isl_log[IA64_MAX_LOGS];	/* need space to store header + error log */
} ia64_state_log_t;

static ia64_state_log_t	ia64_state_log[IA64_MAX_LOG_TYPES][IA64_MAX_LOG_SUBTYPES];

#define IA64_LOG_LOCK_INIT(it, sit)	spin_lock_init(&ia64_state_log[it][sit].isl_lock)
#define IA64_LOG_LOCK(it, sit)	 	spin_lock_irqsave(&ia64_state_log[it][sit].isl_lock, s)
#define IA64_LOG_UNLOCK(it, sit) 	spin_unlock_irqrestore(&ia64_state_log[it][sit].isl_lock,\
							       s)
#define IA64_LOG_NEXT_INDEX(it, sit)	ia64_state_log[it][sit].isl_index
#define IA64_LOG_CURR_INDEX(it, sit) 	1 - ia64_state_log[it][sit].isl_index
#define IA64_LOG_INDEX_INC(it, sit)	\
	ia64_state_log[it][sit].isl_index = 1 - ia64_state_log[it][sit].isl_index
#define IA64_LOG_INDEX_DEC(it, sit)	\
	ia64_state_log[it][sit].isl_index = 1 - ia64_state_log[it][sit].isl_index
#define IA64_LOG_NEXT_BUFFER(it, sit) 	(void *)(&(ia64_state_log[it][sit].isl_log[IA64_LOG_NEXT_INDEX(it,sit)]))
#define IA64_LOG_CURR_BUFFER(it, sit) 	(void *)(&(ia64_state_log[it][sit].isl_log[IA64_LOG_CURR_INDEX(it,sit)]))

/*
 * C portion of the OS INIT handler
 *
 * Called from ia64_<monarch/slave>_init_handler
 *
 * Inputs: pointer to pt_regs where processor info was saved.
 *
 * Returns: 
 *   0 if SAL must warm boot the System
 *   1 if SAL must retrun to interrupted context using PAL_MC_RESUME
 *
 */

void
ia64_init_handler (struct pt_regs *regs)
{
	sal_log_processor_info_t *proc_ptr;
	ia64_psilog_t *plog_ptr;

	printk("Entered OS INIT handler\n");

	/* Get the INIT processor log */
	ia64_log_get(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);
	/* Get the INIT platform log */
	ia64_log_get(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PLATFORM, (prfunc_t)printk);

#ifdef IA64_DUMP_ALL_PROC_INFO 
	ia64_log_print(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PROCESSOR, (prfunc_t)printk);
#endif 

	/* 
	 * get pointer to min state save area
	 *
	 */
	plog_ptr=(ia64_psilog_t *)IA64_LOG_CURR_BUFFER(SAL_INFO_TYPE_INIT,
						       SAL_SUB_INFO_TYPE_PROCESSOR);
	proc_ptr = &plog_ptr->devlog.proclog;
	
	ia64_process_min_state_save(&proc_ptr->slpi_min_state_area,regs);

	init_handler_platform(regs);              /* call platform specific routines */

	/* Clear the INIT SAL logs now that they have been saved in the OS buffer */
	ia64_sal_clear_state_info(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PROCESSOR);
	ia64_sal_clear_state_info(SAL_INFO_TYPE_INIT, SAL_SUB_INFO_TYPE_PLATFORM);
}

/*
 * ia64_log_init
 * 	Reset the OS ia64 log buffer
 * Inputs 	:	info_type 	(SAL_INFO_TYPE_{MCA,INIT,CMC})
 *			sub_info_type	(SAL_SUB_INFO_TYPE_{PROCESSOR,PLATFORM})
 * Outputs	: 	None
 */
void
ia64_log_init(int sal_info_type, int sal_sub_info_type)
{
	IA64_LOG_LOCK_INIT(sal_info_type, sal_sub_info_type);
	IA64_LOG_NEXT_INDEX(sal_info_type, sal_sub_info_type) = 0;
	memset(IA64_LOG_NEXT_BUFFER(sal_info_type, sal_sub_info_type), 0, 
	       sizeof(ia64_psilog_t) * IA64_MAX_LOGS);
}

/* 
 * ia64_log_get
 * 	Get the current MCA log from SAL and copy it into the OS log buffer.
 * Inputs 	:	info_type 	(SAL_INFO_TYPE_{MCA,INIT,CMC})
 *			sub_info_type	(SAL_SUB_INFO_TYPE_{PROCESSOR,PLATFORM})
 * Outputs	: 	None
 *
 */
void
ia64_log_get(int sal_info_type, int sal_sub_info_type, prfunc_t prfunc)
{
	sal_log_header_t	*log_buffer;
	int			s,total_len=0;

	IA64_LOG_LOCK(sal_info_type, sal_sub_info_type);


	/* Get the process state information */
	log_buffer = IA64_LOG_NEXT_BUFFER(sal_info_type, sal_sub_info_type);

	if (!(total_len=ia64_sal_get_state_info(sal_info_type, sal_sub_info_type ,(u64 *)log_buffer)))
		prfunc("ia64_mca_log_get : Getting processor log failed\n");

	IA64_MCA_DEBUG("ia64_log_get: retrieved %d bytes of error information\n",total_len);

	IA64_LOG_INDEX_INC(sal_info_type, sal_sub_info_type);

	IA64_LOG_UNLOCK(sal_info_type, sal_sub_info_type);

}

/* 
 * ia64_log_clear
 * 	Clear the current MCA log from SAL and dpending on the clear_os_buffer flags
 *	clear the OS log buffer also
 * Inputs 	:	info_type 	(SAL_INFO_TYPE_{MCA,INIT,CMC})
 *			sub_info_type	(SAL_SUB_INFO_TYPE_{PROCESSOR,PLATFORM})
 *			clear_os_buffer 
 *			prfunc		(print function)
 * Outputs	: 	None
 *
 */
void
ia64_log_clear(int sal_info_type, int sal_sub_info_type, int clear_os_buffer, prfunc_t prfunc)
{
	if (ia64_sal_clear_state_info(sal_info_type, sal_sub_info_type))
		prfunc("ia64_mca_log_get : Clearing processor log failed\n");

	if (clear_os_buffer) {
		sal_log_header_t	*log_buffer;
		int			s;

		IA64_LOG_LOCK(sal_info_type, sal_sub_info_type);

		/* Get the process state information */
		log_buffer = IA64_LOG_CURR_BUFFER(sal_info_type, sal_sub_info_type);

		memset(log_buffer, 0, sizeof(ia64_psilog_t));

		IA64_LOG_INDEX_DEC(sal_info_type, sal_sub_info_type);
	
		IA64_LOG_UNLOCK(sal_info_type, sal_sub_info_type);
	}

}

/*
 * ia64_log_processor_regs_print
 *	Print the contents of the saved processor register(s) in the format
 *		<reg_prefix>[<index>] <value>
 *		
 * Inputs	:	regs 		(Register save buffer)
 *			reg_num 	(# of registers)
 *			reg_class	(application/banked/control/bank1_general)
 *			reg_prefix	(ar/br/cr/b1_gr)
 * Outputs	: 	None
 *
 */
void
ia64_log_processor_regs_print(u64 	*regs, 
			      int 	reg_num,
			      char 	*reg_class,
			      char 	*reg_prefix,
			      prfunc_t	prfunc)
{
	int i;

	prfunc("+%s Registers\n", reg_class);
	for (i = 0; i < reg_num; i++)
		prfunc("+ %s[%d] 0x%lx\n", reg_prefix, i, regs[i]);
}

static char *pal_mesi_state[] = {
	"Invalid",
	"Shared",
	"Exclusive",
	"Modified",
	"Reserved1",
	"Reserved2",
	"Reserved3",
	"Reserved4"
};

static char *pal_cache_op[] = {
	"Unknown",
	"Move in",
	"Cast out",
	"Coherency check",
	"Internal",
	"Instruction fetch",
	"Implicit Writeback",
	"Reserved"
};

/*
 * ia64_log_cache_check_info_print
 *	Display the machine check information related to cache error(s).
 * Inputs	:	i		(Multiple errors are logged, i - index of logged error)
 *			info		(Machine check info logged by the PAL and later 
 *					 captured by the SAL)
 *			target_addr	(Address which caused the cache error)
 * Outputs	: 	None
 */
void 
ia64_log_cache_check_info_print(int 			i, 
				pal_cache_check_info_t 	info,
				u64			target_addr,
				prfunc_t		prfunc)
{
	prfunc("+ Cache check info[%d]\n+", i);
	prfunc("  Level: L%d",info.level);
	if (info.mv)
		prfunc(" ,Mesi: %s",pal_mesi_state[info.mesi]);
	prfunc(" ,Index: %d,", info.index);
	if (info.ic)
		prfunc(" ,Cache: Instruction");
	if (info.dc)
		prfunc(" ,Cache: Data");
	if (info.tl)
		prfunc(" ,Line: Tag");
	if (info.dl)
		prfunc(" ,Line: Data");
	prfunc(" ,Operation: %s,", pal_cache_op[info.op]);
	if (info.wv)
		prfunc(" ,Way: %d,", info.way);
	if (info.tv)
		prfunc(" ,Target Addr: 0x%lx", target_addr);
	if (info.mc)
		prfunc(" ,MC: Corrected");
	prfunc("\n");
}

/*
 * ia64_log_tlb_check_info_print
 *	Display the machine check information related to tlb error(s).
 * Inputs	:	i		(Multiple errors are logged, i - index of logged error)
 *			info		(Machine check info logged by the PAL and later 
 *					 captured by the SAL)
 * Outputs	: 	None
 */

void
ia64_log_tlb_check_info_print(int 			i,
			      pal_tlb_check_info_t	info,
			      prfunc_t			prfunc)
{
	prfunc("+ TLB Check Info [%d]\n+", i);
	if (info.itc)
		prfunc("  Failure: Instruction Translation Cache");
	if (info.dtc)
		prfunc("  Failure: Data Translation Cache");
	if (info.itr) {
		prfunc("  Failure: Instruction Translation Register");
		prfunc(" ,Slot: %d", info.tr_slot);
	}
	if (info.dtr) {
		prfunc("  Failure: Data Translation Register");
		prfunc(" ,Slot: %d", info.tr_slot);
	}
	if (info.mc)
		prfunc(" ,MC: Corrected");
	prfunc("\n");
}

/*
 * ia64_log_bus_check_info_print
 *	Display the machine check information related to bus error(s).
 * Inputs	:	i		(Multiple errors are logged, i - index of logged error)
 *			info		(Machine check info logged by the PAL and later 
 *					 captured by the SAL)
 *			req_addr	(Address of the requestor of the transaction)
 *			resp_addr	(Address of the responder of the transaction)
 *			target_addr	(Address where the data was to be delivered to  or
 *					 obtained from)
 * Outputs	: 	None
 */
void
ia64_log_bus_check_info_print(int 			i,
			      pal_bus_check_info_t	info,
			      u64			req_addr,
			      u64			resp_addr,
			      u64			targ_addr,
			      prfunc_t			prfunc)
{
	prfunc("+ BUS Check Info [%d]\n+", i);
	prfunc(" Status Info: %d", info.bsi);
	prfunc(" ,Severity: %d", info.sev);
	prfunc(" ,Transaction Type: %d", info.type);
	prfunc(" ,Transaction Size: %d", info.size);
	if (info.cc)
		prfunc(" ,Cache-cache-transfer");
	if (info.ib)
		prfunc(" ,Error: Internal");
	if (info.eb)
		prfunc(" ,Error: External");
	if (info.mc)
		prfunc(" ,MC: Corrected");
	if (info.tv)
		prfunc(" ,Target Address: 0x%lx", targ_addr);
	if (info.rq)
		prfunc(" ,Requestor Address: 0x%lx", req_addr);
	if (info.tv)
		prfunc(" ,Responder Address: 0x%lx", resp_addr);
	prfunc("\n");
}

/*
 * ia64_log_processor_info_print
 *	Display the processor-specific information logged by PAL as a part
 *	of MCA or INIT or CMC.
 * Inputs 	:	lh	(Pointer of the sal log header which specifies the format
 *				 of SAL state info as specified by the SAL spec).
 * Outputs	: 	None
 */
void
ia64_log_processor_info_print(sal_log_header_t *lh, prfunc_t prfunc)
{
	sal_log_processor_info_t	*slpi;
	int				i;

	if (!lh)
		return;

	if (lh->slh_log_type != SAL_SUB_INFO_TYPE_PROCESSOR)
		return;

	slpi = (sal_log_processor_info_t *)((char *)lh+sizeof(sal_log_header_t));  /* point to proc info */

	if (!slpi) {
		prfunc("No Processor Error Log found\n");
		return;
	}

	/* Print branch register contents if valid */
	if (slpi->slpi_valid.slpi_br) 
		ia64_log_processor_regs_print(slpi->slpi_br, 8, "Branch", "br", prfunc);

	/* Print control register contents if valid */
	if (slpi->slpi_valid.slpi_cr) 
		ia64_log_processor_regs_print(slpi->slpi_cr, 128, "Control", "cr", prfunc);

	/* Print application register contents if valid */
	if (slpi->slpi_valid.slpi_ar) 
		ia64_log_processor_regs_print(slpi->slpi_br, 128, "Application", "ar", prfunc);

	/* Print region register contents if valid */
	if (slpi->slpi_valid.slpi_rr) 
		ia64_log_processor_regs_print(slpi->slpi_rr, 8, "Region", "rr", prfunc);

	/* Print floating-point register contents if valid */
	if (slpi->slpi_valid.slpi_fr) 
		ia64_log_processor_regs_print(slpi->slpi_fr, 128, "Floating-point", "fr", 
					      prfunc);

	/* Print the cache check information if any*/
	for (i = 0 ; i < MAX_CACHE_ERRORS; i++)
		ia64_log_cache_check_info_print(i, 
					slpi->slpi_cache_check_info[i].slpi_cache_check,
					slpi->slpi_cache_check_info[i].slpi_target_address,
						prfunc);
	/* Print the tlb check information if any*/
	for (i = 0 ; i < MAX_TLB_ERRORS; i++)
		ia64_log_tlb_check_info_print(i,slpi->slpi_tlb_check_info[i], prfunc);

	/* Print the bus check information if any*/
	for (i = 0 ; i < MAX_BUS_ERRORS; i++)
		ia64_log_bus_check_info_print(i, 
					slpi->slpi_bus_check_info[i].slpi_bus_check,
					slpi->slpi_bus_check_info[i].slpi_requestor_addr,
					slpi->slpi_bus_check_info[i].slpi_responder_addr,
					slpi->slpi_bus_check_info[i].slpi_target_addr,
					      prfunc);

}

/*
 * ia64_log_print
 * 	Display the contents of the OS error log information
 * Inputs 	:	info_type 	(SAL_INFO_TYPE_{MCA,INIT,CMC})
 *			sub_info_type	(SAL_SUB_INFO_TYPE_{PROCESSOR,PLATFORM})
 * Outputs	: 	None
 */
void
ia64_log_print(int sal_info_type, int sal_sub_info_type, prfunc_t prfunc)
{
	char 	*info_type, *sub_info_type;

	switch(sal_info_type) {
	case SAL_INFO_TYPE_MCA:
		info_type = "MCA";
		break;
	case SAL_INFO_TYPE_INIT:
		info_type = "INIT";
		break;
	case SAL_INFO_TYPE_CMC:
		info_type = "CMC";
		break;
	default:
		info_type = "UNKNOWN";
		break;
	}

	switch(sal_sub_info_type) {
	case SAL_SUB_INFO_TYPE_PROCESSOR:
		sub_info_type = "PROCESSOR";
		break;
	case SAL_SUB_INFO_TYPE_PLATFORM:
		sub_info_type = "PLATFORM";
		break;
	default:
		sub_info_type = "UNKNOWN";
		break;
	}

	prfunc("+BEGIN HARDWARE ERROR STATE [%s %s]\n", info_type, sub_info_type);
	if (sal_sub_info_type == SAL_SUB_INFO_TYPE_PROCESSOR)
		ia64_log_processor_info_print(
				      IA64_LOG_CURR_BUFFER(sal_info_type, sal_sub_info_type),
				      prfunc);
	else
		log_print_platform(IA64_LOG_CURR_BUFFER(sal_info_type, sal_sub_info_type),prfunc);
	prfunc("+END HARDWARE ERROR STATE [%s %s]\n", info_type, sub_info_type);
}
