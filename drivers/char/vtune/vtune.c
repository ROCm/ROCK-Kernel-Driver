/*
 *  vtune.c
 *
 *  Copyright (C) 2002-2004 Intel Corporation
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: vtune.c
 *
 *	Description: sampling driver main program
 *
 *	Author(s): George Artz, Intel Corp.
 *                 Juan Villacis, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>     /* malloc */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#ifdef KERNEL_26X
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vermagic.h>
#ifdef DSA_SUPPORT_MMAP
#include <linux/mm.h>
#endif
#ifdef linux32_64
#include <asm/thread_info.h>
#endif
#endif
#include <asm/uaccess.h>
#ifdef ENFORCE_ROOT_ONLY_ACCESS
#include <linux/capability.h>
#endif
#ifdef linux32
#include <asm/unistd.h>
#endif
#ifdef PERFMON_SUPPORTED
#include <asm/perfmon.h>
#endif

#ifdef linux32_64
#include "asm/apic.h"
#include "apic.h"
#endif

#include "vtdef.h"
#include "vtuneshared.h"
#include "vtoshooks.h"
#include "vtproto.h"
#include "vtglobal.h"
#include "familyf_msr.h"

long enum_user_mode_modules(void);

#ifdef linux64
void set_PP_SW(void);
#endif

// The following is added by Fleming for the algorithm of eliminating dup
// entries for a single process when calling do_fork and do_execve in
// consecutive order.

typedef struct tag_MR_pointer {
    struct tag_MR_pointer *m_p_prev;
    struct tag_MR_pointer *m_p_next;
    module_record *m_p_mrp;
} MRPOINTER, *PMRPOINTER;
// g_MR_pointer_head used to be "last", g_MR_pointer_tail used to be
// "gMRpointer"
PMRPOINTER g_MR_pointer_tail = NULL, g_MR_pointer_head = NULL;

typedef struct tag_pid_record_pointer {
    struct tag_pid_record_pointer *m_p_next;
    pid_record *m_p_pid_record;
} pid_record_pointer, *P_pid_record_pointer;
P_pid_record_pointer g_pid_record_pointer_tail = NULL, g_pid_record_pointer_head = NULL;

ssize_t samp_write_pid_record_file(struct file *fp, char *ubuf, size_t cnt,
                   loff_t * pp);
void add_PID_create_record_to_list(pid_t pid);
void free_PID_create_record_list(void);

// For eliminating the wake_up_interruptible( ) in the ebs isr
// Added by Fleming
__u32 g_b_wake_up_interruptible = 0;
__u32 b_stop_sampling = FALSE;
__u32 restart_EBS = FALSE;
struct irqaction *pebs_irqaction = NULL;

spinlock_t vtune_modlist_lock = SPIN_LOCK_UNLOCKED;
spinlock_t pidlist_lock = SPIN_LOCK_UNLOCKED;
spinlock_t mgid_lock = SPIN_LOCK_UNLOCKED;

__u32 total_loads;      // debug code 
__u32 total_loads_init;     // debug code 
__u32 total_pid_updates;    // debug code

// module_record buffer
#define MR_BUFFER_DATA_SIZE (32*1024)
typedef struct _MR_BUF {    // 
    LIST_ENTRY link;    //
    __u32 buf_size;     // byte length of buffer
    __u32 bytes_in_buf; // count of bytes in buffer
    char buf[MR_BUFFER_DATA_SIZE];  
} MR_BUF, *PMR_BUF;

PMR_BUF p_current_mr_buf;       // current module record buffer    
LIST_ENTRY mr_buf_free_list_head;   // head of free mr  bufs           
LIST_ENTRY mr_buf_out_list_head;    // head of mr  bufs to be output   

PMR_BUF p_mr_buf_out;       // pointer to module record buffer that has
                // been popped off
                // ..output list and contains modules record
                // ready to be written to .tb5 file 
__u32 mr_buf_out_offset;    // offset to next module record in the output
                // buffer  

// Process create Record buffer        
#define PID_BUFFER_DATA_SIZE (16*1024)
typedef struct _PID_BUF {   // 
    LIST_ENTRY link;    //
    __u32 buf_size;     // byte length of buffer
    __u32 bytes_in_buf; // count of bytes in buffer
    char buf[PID_BUFFER_DATA_SIZE]; 
} PID_BUF, *PPID_BUF;

PPID_BUF p_current_pid_buf;     // current pid record buffer
LIST_ENTRY pid_buf_free_list_head;  // head of free pid  bufs
LIST_ENTRY pid_buf_out_list_head;   // head of pr  bufs to be output

PPID_BUF p_pid_buf_out;         // pointer to process create record
                    // buffer that has been popped off
// ..output list and contains pid
// record ready to be written to .tb5 file 
__u32 pid_buf_out_offset;       // offset to next pid record in the
                    // output buffer  

__u32 track_module_loads = FALSE;   // track module loads
__u32 track_process_creates = FALSE;    // track process creates
static atomic_t samp_isr_active = ATOMIC_INIT(0);

__u32 mgid;             // module group ID

void install_OS_hooks(void);
void un_install_OS_hooks(void);
int vdrv_resume_EBS(void);

#ifdef linux32
__u32 validate_EBS_regs(void);
void SAMP_Set_Apic_Virtual_Wire_Mode(void);
void disable_lbr_capture(void);
#elif defined(linux32_64)
__u32 validate_EBS_regs(void);
#endif

void add_to_mr_buf_out_offset(__u32 rec_length);

module_record *get_next_mr_rec_from_out_list(void);

PMR_BUF pop_mr_buf_from_head_of_free_list(void);

PMR_BUF pop_mr_buf_from_head_of_out_list(void);

PMR_BUF alloc_new_mr_buf(void);

void add_mr_buf_to_free_list(PMR_BUF pmr_buf);

void free_mr_bufs(void);

void move_current_mr_buf_to_out_list(void);

void add_to_pid_buf_out_offset(__u32 rec_length);

pid_record *get_next_pid_rec_from_out_list(void);

PPID_BUF alloc_new_pid_buf(void);

void add_pid_buf_to_free_list(PPID_BUF p_pid_buf);

void free_pid_bufs(void);

void move_current_pid_buf_to_out_list(void);

#ifdef DSA_SUPPORT_MMAP
#ifdef KERNEL_26X
#define mem_map_reserve(p)    set_bit(PG_reserved, &((p)->flags))
#define mem_map_unreserve(p)  clear_bit(PG_reserved, &((p)->flags))
#else
#include <linux/wrapper.h>   /* for call to mem_map_unreserve and mem_map_reserve */
#endif
__u32 *dsa_kmalloc_ptr = 0;  /* pointer to unaligned dsa area */
__u32 g_dsa_size = sizeof(driver_shared_area);
__u32 g_dsa_kmalloc_size = 0;
#endif

PER_CPU eachCPU[MAX_PROCESSORS];

#if defined(linux32) || defined(linux32_64)
__u32 package_status[MAX_PROCESSORS + 1];
#endif

#ifdef USE_NMI

#include <asm/nmi.h>

int nmi_interrupts;

static nmi_callback_t
ebs_nmi_callback(struct pt_regs * regs, int cpu)
{
  ebs_intr(regs);

  nmi_interrupts++;

  return (1);
}

static int
register_nmi_callback(void)
{
    nmi_interrupts = 0;

    set_nmi_callback((nmi_callback_t)ebs_nmi_callback);

    return (0);
}

static int
unregister_nmi_callback(void)
{
    unset_nmi_callback();

    VDK_PRINT_DEBUG("number of NMI interrupts generated: %d\n",nmi_interrupts);

    return (0);
}

#endif // USE_NMI

#ifdef linux32
void
get_cpu_info_for_current_cpu(void *p)
{
    __u32 i, j, eax, ebx, ecx, edx;
    unsigned long ul_logical_processors_per_package, ul_logical_processors_shift;
    cpu_information *p_cpu_infor0;
    cpuid_output *pout;
    cpu_map *pmap;

    p_cpu_infor0 = (cpu_information *) p;

    //
    // Skip cpu if it's out of requested range 
    //
    if (smp_processor_id() >= p_cpu_infor0->ul_num_cpus_available) {
        return;
    }

    pmap =
        (cpu_map *) ((__u32) p_cpu_infor0 + p_cpu_infor0->ul_offset_to_cpu_map_array +
            (smp_processor_id() * sizeof (cpu_map)));
    pout =
        (cpuid_output *) ((__u32) p_cpu_infor0 +
                 p_cpu_infor0->ul_offset_to_cpuid_output_array +
                 (smp_processor_id() *
                  (sizeof (cpuid_output) *
                   (p_cpu_infor0->ul_num_EAX_inputs +
                p_cpu_infor0->ul_num_EAX_extended_inputs))));
    VDK_PRINT_DEBUG("get_cpu_infoForCurrentCpu: p_cpu_infor0 0x%x  pmap 0x%x pout 0x%x \n",
		    p_cpu_infor0, pmap, pout);

    //
    // Fill in cpu_map for current cpu
    //
    cpuid(1, &eax, &ebx, &ecx, &edx);
    pmap->ul_cpu_num = smp_processor_id();
    if (edx & CPUID_HT_MASK) {
        ul_logical_processors_per_package = (ebx & 0xff0000) >> 16;
        if (ul_logical_processors_per_package == 0) {
            ul_logical_processors_per_package = 1;
        }
        ul_logical_processors_shift = 0;
        ul_logical_processors_per_package--;
        while (1) {
            ul_logical_processors_shift++;
            ul_logical_processors_per_package >>= 1;
            if (!ul_logical_processors_per_package) {
                break;
            }
        }
        pmap->ul_hardware_thread_num =
            (ebx >> 24) & ul_logical_processors_shift;
        pmap->ul_package_num = ((ebx >> 24) >> ul_logical_processors_shift);
        VDK_PRINT_DEBUG("cpu %d HT enabled HW threads per package %d shift %d \n",
			smp_processor_id(), (ebx & 0xff0000) >> 16,
			ul_logical_processors_shift);
    } else {
        //
        // If one HW thread per core (HT disabled), then
        // set package# = OS cpu number, and HT thread =0.
        pmap->ul_hardware_thread_num = 0;
        pmap->ul_package_num = smp_processor_id();
        VDK_PRINT_DEBUG("cpu %d HT disabled \n", smp_processor_id());
    }

    //
    // get cpuid data for standard inputs for current cpu
    //
    for (i = 1; i <= p_cpu_infor0->ul_num_EAX_inputs; i++, pout++) {
        pout->ul_cpu_num = smp_processor_id();
        pout->ul_EAX_input = i;
        cpuid(i, &pout->ul_EAX_output, &pout->ul_EBX_output,
              &pout->ul_ECX_output, &pout->ul_EDX_output);
    }

    //                                                 
    // get cpuid data for extended inputs for current cpu
    //
    VDK_PRINT_DEBUG("ext outputs pCOUinfoR0 0x%x pout 0x%x std inputs %d  ext inputs %d pout->ul_EDX_output 0x%x \n",
		    p_cpu_infor0, pout, p_cpu_infor0->ul_num_EAX_inputs,
		    p_cpu_infor0->ul_num_EAX_extended_inputs, pout->ul_EDX_output);
    for (i = 0x80000001, j = 0; j < p_cpu_infor0->ul_num_EAX_extended_inputs;
         i++, j++, pout++) {
        pout->ul_cpu_num = smp_processor_id();
        pout->ul_EAX_input = i;
        cpuid(i, &pout->ul_EAX_output, &pout->ul_EBX_output,
              &pout->ul_ECX_output, &pout->ul_EDX_output);
    }

    return;
}

int
get_cpu_info(cpu_information * p_cpu_info)
{
    __u32 buf_size, num_cpus;
    cpu_information cpu_info;
    cpu_information *p_cpu_infor0;
    __u32 map_offset, map_size, out_offset, out_size;

    //
    // Validate input
    //

    if (!p_cpu_info) {
        return (-EFAULT);
    }

    if (!access_ok(VERIFY_WRITE, p_cpu_info, sizeof(cpu_information))) {
        return (-EFAULT);
    }

    copy_from_user(&cpu_info, p_cpu_info, sizeof(cpu_information));
    
    num_cpus = cpu_info.ul_num_cpus_available;

    if (num_cpus > MAX_PROCESSORS) {
        return (-EINVAL);
    }

    map_offset = cpu_info.ul_offset_to_cpu_map_array;
    out_offset = cpu_info.ul_offset_to_cpuid_output_array;

    if (!map_offset || (map_offset < sizeof(cpu_information))) {
        return (-EINVAL);
    }

    if (!out_offset || (out_offset < sizeof(cpu_information))) {
        return (-EINVAL);
    }

    map_size = num_cpus * sizeof(cpu_map);
    out_size = num_cpus * (sizeof(cpuid_output) * (cpu_info.ul_num_EAX_inputs + cpu_info.ul_num_EAX_extended_inputs));

    if (map_offset > out_offset) {
        if ((out_offset + out_size) > map_offset) {
            return (-EINVAL);
        }
        buf_size = map_offset + map_size;
    }
    else {
        if (out_offset > map_offset) {
            if ((map_offset + map_size) > out_offset) {
                return (-EINVAL);
            }
            buf_size = out_offset + out_size;
        }
        else {
            return (-EINVAL);
        }
    }

    VDK_PRINT_DEBUG("get_cpu_info: vmalloc buf_size = %d NumCpus %d mapOffset 0x%x outOffset 0x%x \n",
		    buf_size, p_cpu_info->ul_num_cpus_available,
		    p_cpu_info->ul_offset_to_cpu_map_array,
		    p_cpu_info->ul_offset_to_cpuid_output_array);


    if (!access_ok(VERIFY_WRITE, p_cpu_info, buf_size)) {
        return (-EFAULT);
    }

    //
    // Allocate buffer for cpu info
    //

    p_cpu_infor0 = vmalloc(buf_size);

    if (!p_cpu_infor0) {
        return (-ENOMEM);
    }
    copy_from_user(p_cpu_infor0, p_cpu_info, buf_size);

#ifdef CONFIG_SMP
    smp_call_function(get_cpu_info_for_current_cpu, (void *) p_cpu_infor0, 1, 1);
#endif
    get_cpu_info_for_current_cpu((void *) p_cpu_infor0);

    copy_to_user((void *) p_cpu_info, (void *) p_cpu_infor0, buf_size);

    vfree(p_cpu_infor0);

    return (0);
}
#endif              // linux32

#ifdef ENABLE_TGID

/*++

Routine description:

   Determine the process id of thread group or parent process 
   that spawned the threads

Arguments:

return value:

--*/   
int get_thread_group_id(struct task_struct *proc_task)
{
  int pid;
  void *proc_vm, *p_proc_vm;
  struct task_struct *orig_proc_task;

  read_lock(&tasklist_lock);
  if (proc_task->pid != proc_task->tgid)
  {
    pid = proc_task->tgid;
  }
  else
  {
	  proc_vm = proc_task->mm;
	  p_proc_vm = VT_GET_PARENT(proc_task)->mm;
	  while(proc_vm == p_proc_vm)
	  {
		  orig_proc_task = proc_task;
		  proc_task = VT_GET_PARENT(proc_task);
		  if(orig_proc_task == proc_task)
			  break;
		  proc_vm = proc_task->mm;
		  p_proc_vm = VT_GET_PARENT(proc_task)->mm;
	  }
	  pid = proc_task->pid;
  }
  read_unlock(&tasklist_lock);

  return(pid);
}

int find_thread_id(thread_info *p_thread_id)
{
  thread_info user_thread;
  struct task_struct *process_task;

  if (!p_thread_id) {
    return (-EFAULT);
  }

  if (!access_ok(VERIFY_WRITE, p_thread_id, sizeof(thread_info))) {
    return (-EFAULT);
  }

  copy_from_user(&user_thread, p_thread_id, sizeof(thread_info));
  
  if (user_thread.tgrp_id <=0)
  {
    return(-EINVAL);
  }
    
  read_lock(&tasklist_lock);
  process_task = find_task_by_pid(user_thread.tgrp_id);
  read_unlock(&tasklist_lock);

  user_thread.tgrp_id = get_thread_group_id(process_task);
    
  copy_to_user((void *) p_thread_id, (void *) &user_thread, 
	       sizeof (thread_info));
  return(0);
}

#endif // ENABLE_TGID

/*
 *
 *  Function: samp_read_cpu_perf_counters
 *
 *  Description: 
 *  Read and Construct Event Totals
 *
 *  Parms:
 *      Entry:      prBuf
 *  
 *      Return:     status
 *
 */
int samp_read_cpu_perf_counters(RDPMC_BUF *pr_buf)
{
    RDPMC_BUF   *pr_buf_r0;

    pr_buf_r0 = vmalloc(sizeof(RDPMC_BUF));

    if (!pr_buf_r0) {
        return(-ENOMEM);
    }

    if (copy_from_user(pr_buf_r0, pr_buf, sizeof(RDPMC_BUF))) {
        VDK_PRINT_DEBUG("samp_read_cpu_perf_counters: copy_from_user failed \n");
        return(-EINVAL);
    }

    //
    // The caller should set cpuMaskIn and pmcMask.
    // Set defaults if the fields were not set by caller.
    //
    if (pr_buf_r0->cpu_mask_in == 0) {
        pr_buf_r0->cpu_mask_in = -1;
    }
    if (pr_buf_r0->pmc_mask.quad_part == 0) {
        pr_buf_r0->pmc_mask.low_part = -1;
    }
    pr_buf_r0->cpu_mask_out = 0;
    pr_buf_r0->duration = pdsa->duration;

#ifdef SMP_ON
    smp_call_function(read_cpu_perf_counters_for_current_cpu, (void *) pr_buf_r0, 1, 1);  
#endif
    read_cpu_perf_counters_for_current_cpu((void *) pr_buf_r0);  

#ifdef USE_NMI
    copy_to_user(pr_buf,pr_buf_r0,sizeof(RDPMC_BUF));
#else
    memcpy((void *) pr_buf, (void*) pr_buf_r0, sizeof(RDPMC_BUF));
#endif

    vfree(pr_buf_r0);

    return(0);
}

#ifdef linux64

#ifdef PERFMON_SUPPORTED
static pfm_intr_handler_desc_t desc;
#endif

/* ------------------------------------------------------------------------- */
/*!
 * @fn          int install_perf_isr(void) 
 * @brief       Assign the PMU interrupt to the driver
 *
 * @return	zero if successful, non-zero error value if something failed
 *
 * Install the driver ebs handler onto the PMU interrupt. If perfmon is
 * compiled in then we ask perfmon for the interrupt, otherwise we ask the
 * kernel...
 *
 * <I>Special Notes:</I>
 *
 * @Note This routine is for Itanium(R)-based systems only!
 *
 *	For IA32, the LBRs are not frozen when a PMU interrupt is taken.
 * Since the LBRs capture information on every branch, for the LBR
 * registers to be useful, we need to freeze them as quickly as
 * possible after the interrupt. This means hooking the IDT directly
 * to call a driver specific interrupt handler. That happens in the
 * vtxsys.S file via samp_get_set_idt_entry. The real routine being
 * called first upon PMU interrupt is t_ebs (in vtxsys.S) and that
 * routine calls ebs_intr()...
 *
 */
int
install_perf_isr(void)
{
    int status = -EPERM;

    VDK_PRINT_DEBUG("install_perf_isr: entered... pmv 0x%p \n", itp_get_pmv());

#ifdef PERFMON_SUPPORTED
    /*
     * if PERFMON_SUPPORTED is set, we can use the perfmon.c interface 
     * to steal perfmon.c's interrupt handler for our use
     * perfmon.c has already done register_percpu_irq()
     */

     ebs_irq = VTUNE_PERFMON_IRQ;
     desc.handler = &ebs_intr;
     status = pfm_install_alternate_syswide_subsystem(&desc);
     if (status) {
         VDK_PRINT_ERROR("install_perf_isr: pfm_install_alternate_syswide_subsystem returned %d\n",status);
     }
#else // PERFMON_SUPPORTED

    if (pebs_irqaction) {
        return (status);
    }

#ifdef SA_PERCPU_IRQ_SUPPORTED

    ebs_irq = VTUNE_PERFMON_IRQ;
    pebs_irqaction = (struct irqaction *) 1;
    status = request_irq(VTUNE_PERFMON_IRQ, ebs_intr,
			 SA_INTERRUPT | SA_PERCPU_IRQ, "VTune Sampling", NULL);

#else // SA_PERCPU_IRQ_SUPPORTED

    {
        static char name[] = "VTune";

        pebs_irqaction = kmalloc(sizeof (struct irqaction), GFP_ATOMIC);
        if (pebs_irqaction) {
            memset(pebs_irqaction, 0, sizeof (struct irqaction));
            ebs_irq = VTUNE_PERFMON_IRQ;
            pebs_irqaction->handler = ebs_intr;
            pebs_irqaction->flags = SA_INTERRUPT;
            pebs_irqaction->name = name;
            pebs_irqaction->dev_id = NULL;

            register_percpu_irq(ebs_irq, pebs_irqaction);
            status = 0;
        }
    }
#endif // SA_PERCPU_IRQ_SUPPORTED

#endif // PERFMON_SUPPORTED

    VDK_PRINT_DEBUG("install_perf_isr: exit...... rc=0x%x pmv=0x%p \n", status, itp_get_pmv());

    return (status);
}

void
uninstall_perf_isr(void)
{
#ifdef PERFMON_SUPPORTED
    int status;
#endif

    VDK_PRINT_DEBUG("uninstall_perf_isr: entered... pmv=0x%p \n", itp_get_pmv());

#ifdef PERFMON_SUPPORTED
    /*
     * if PERFMON_SUPPORTED is set, we used the perfmon.c interface 
     * to steal perfmon.c's interrupt handler for our use
     * Now we must release it back.
     * Don't free_irq() because perfmon.c still wants to use it
     */
     status = pfm_remove_alternate_syswide_subsystem(&desc);
     VDK_PRINT_DEBUG("pfm_remove_alternate_syswide_subsystem returned %d\n", status); 
     // grumble, no way to return this error to caller, log it and hope
     if (status != 0) 
         VDK_PRINT_WARNING("uninstall_perf_isr: pfm_remove_alternate_syswide_subsystem returned: %d\n",status);

#else // PERFMON_SUPPORTED

    if (xchg(&pebs_irqaction, 0)) {
        free_irq(ebs_irq, NULL);
    }

#endif // PERFMON_SUPPORTED

    VDK_PRINT_DEBUG("uninstall_perf_isr: exit... pmv=0x%p \n", itp_get_pmv());

    return;
}

#endif // linux64

/*
 *
 *
 *   Function:  init_driver_OS
 *
 *   description:
 *   Initialize OS specific portions of driver
 *
 *   Parms:
 *       entry:  None
 *   
 *       return: status
 *
 *
 */
int
init_driver_OS(void)
{
    /* Determine CPU support */
    vdrvgetsysinfo();

    /* Initialize Global Driver Locks */
    spin_lock_init(&sample_exec_lock);
    spin_lock_init(&sample_int_lock);
    spin_lock_init(&reg3f1_write_lock);

    p_current_mr_buf = 0;
    INIT_LIST_HEAD(&mr_buf_free_list_head);
    INIT_LIST_HEAD(&mr_buf_out_list_head);

    p_current_pid_buf = 0;
    INIT_LIST_HEAD(&pid_buf_free_list_head);
    INIT_LIST_HEAD(&pid_buf_out_list_head);

#if defined(linux32) || defined(linux32_64)
    app_base_low = LINUX32_APP_BASE_LOW;
    app_base_high = LINUX32_APP_BASE_HIGH;
#elif defined(linux64)
    app_base_low = LINUX64_APP_BASE_LOW;
    app_base_high = LINUX64_APP_BASE_HIGH;
#else
#error Unknown architecture
#endif

    install_OS_hooks();

    return (0);
}

/*
 *
 *
 *   Function:  init_module
 *
 *   description:
 *   initialization of driver resources and system registration.
 *
 *   Parms:
 *       entry:  None
 *   
 *       return: status
 *
 *
 */
int
init_module(void)
{
    int result;

    /* Obtain an available device number and obtain a lock on the device */

    result = register_chrdev(0, "vtune", &vtune_fops);

    if (result < 0) {
        VDK_PRINT_ERROR("Module register Failed!\n");
        return (result);
    }

    /* set major node number (created when first arg to register_chrdev is 0) */
    vtune_major = result;

    /* Send load message to system log file */
    VDK_PRINT_BANNER("loaded");

#ifdef USE_NMI
    VDK_PRINT_WARNING("Event calibration is disabled for this kernel.\n");
    VDK_PRINT_WARNING("Sampling with precise events is disabled for this kernel.\n");
#endif

    memset(eachCPU, 0, sizeof(eachCPU)); // see also init_driver() in vtlib*.c

    return (0);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void stop_sampling(BOOLEAN do_wakeup) 
 * @brief       Is what it says it is...
 *
 * @param       do_wakeup IN  - wakeup readers
 *
 * @return	none
 *
 * Stop any sampling that is going on and does some cleanup
 *
 * <I>Special Notes:</I>
 *
 *	None
 */
void
stop_sampling(BOOLEAN do_wakeup)
{
    VDK_PRINT_DEBUG("stop_sampling: entered current pid %d \n", current->pid);

    /* START - Critical Section */
    spin_lock(&sample_exec_lock);

    samp_info.sampling_active = FALSE;
    track_module_loads = FALSE;
    track_process_creates = FALSE;
    //
    // Wait for any in progress interrupt handlers to complete
    //
    while (atomic_read(&samp_isr_active)) ;

    if (samp_info.flags & SINFO_STARTED) {

        samp_info.flags &= ~(SINFO_STARTED | SINFO_DO_STOP);
        samp_info.flags |= (SINFO_DO_WRITE | SINFO_STOPPING);
        signal_thread_event = FALSE;

        //
        // Save current time as stop time, cancel max sample time timer,
        // and signal the thread to write data and free resources.
        //

        pdsa->duration = (unsigned long) ((jiffies - start_time) * 10);
        samp_info.sample_time = pdsa->duration; // 12-15-97
        pdsa->tot_samples = pdsa->sample_count;

        //
        // Clear current sample count
        // to avoid confusing future
        // sampling sessions.
        //
        samp_info.sample_count = 0;
        pdsa->sample_count = 0; // 04-01-96
        pdsa->pause_count = 0;
        pdsa->running = 0;

        spin_unlock(&sample_exec_lock);

        VDK_PRINT_DEBUG("sampling stopped. total_loads %d total_loads_init %d  total_pid_updates %d buf size %d \n",
			total_loads, total_loads_init, total_pid_updates,
			MR_BUFFER_DATA_SIZE);

        if (sample_method & METHOD_EBS) {
            vdrv_stop_EBS();
        }

        if (xchg(&g_start_delay_timer_ON, 0)) {
            del_timer(&delay_tmr);
        }
        if (xchg(&g_max_samp_timer_ON, 0)) {
            del_timer(&time_out_tmr);
        }

        if (do_wakeup) {
            wake_up_interruptible(&pc_write);
        }
    } else {
        spin_unlock(&sample_exec_lock);
    }

#if defined(ALLOW_LBRS) && defined(linux32)
    disable_lbr_capture();
#endif

    return;
}

void
samp_cleanup(void)
{
    void *pbuf;

    samp_info.flags = SINFO_STOP_COMPLETE;

    /* Free sampling buffer back to OS */
    if ((pbuf = xchg(&buf_start, 0))) {
        vfree(pbuf);
    }

    if (pdsa) {
        pdsa->pause_count = 0;
        pdsa->running = 0;
    }

#ifdef USE_NMI
      unregister_nmi_callback();
      return;
#endif

#ifdef linux64
    uninstall_perf_isr();
#endif

    return;
}

void
abort_sampling(void)
{
    stop_sampling(TRUE);
    samp_cleanup();

    return;
}

/*
 *
 *
 *   Function:  cleanup_module
 *
 *   description:
 *
 *   Parms:
 *       entry:  None
 *   
 *       return: None
 *
 *
 */
void
cleanup_module(void)
{
    stop_sampling(TRUE);
    samp_cleanup();

    driver_unload();
    un_install_OS_hooks();
    unregister_chrdev(vtune_major, "vtune");

    /* Send unload message to system log file */
    VDK_PRINT_BANNER("unloaded");

    return;
}

/*
 *
 *
 *   Function:  vtune_open
 *
 *   description:
 *
 *   Parms:
 *       entry:  *inode
 *               *filp
 *
 *       return: status
 *
 *
 */
int
vtune_open(struct inode *inode, struct file *filp)
{  
#ifdef ENFORCE_ROOT_ONLY_ACCESS
    if (!capable(CAP_SYS_PTRACE)) {
        VDK_PRINT_ERROR("module open failed, root access required\n");
        return (-EPERM);
    }
#endif
  
    if (MINOR(inode->i_rdev) == 1) {
        if (file_count) {
            return (-EBUSY);
        } else {
            file_count++;
        }
    }
    driver_open();

    return (0);
}

/*
 *
 *
 *   Function:  vtune_write
 *
 *   description:
 *
 *   Parms:
 *       entry:  *filp
 *               *buf
 *               count
 *               *ppos
 *   
 *       return: status
 *
 *
 */
ssize_t
vtune_write(struct file * filp, const char *buf, size_t count, loff_t * ppos)
{
    return (-ENOSYS);
}



/*
 *
 *
 *   Function:  vtune_ioctl
 *
 *   description:
 *
 *   Parms:
 *       entry:  *inode
 *               *filp
 *               cmd
 *               *arg
 *
 *       return: status
 *
 *
 */
int
vtune_ioctl(struct inode *inode, struct file *filp,
	    unsigned int cmd, unsigned long arg)
{
#ifdef USE_NMI
    samp_parm_ex spx;
#else
    samp_parm_ex *spx;
#endif
    samp_parm6   *sp6 = NULL;
    int sp_offset = 0;
    int sp_length = 0;

    int status = 0;

#ifdef ENFORCE_ROOT_ONLY_ACCESS
    if (!capable(CAP_SYS_PTRACE))
      return (-EPERM);
#endif

    switch (cmd) {
    case VTUNE_CONFIG_EX:
    case VTUNE_START_EX:

        //
        // Verify access to samp parm header
        //
        if (!access_ok(VERIFY_READ, arg, sizeof(samp_parm_header))) {
            status = -EINVAL;
            break;
        }

#ifdef USE_NMI
	memset(&spx,0,sizeof(samp_parm_ex));
	copy_from_user(&spx, (samp_parm_ex *)arg, sizeof(samp_parm_ex));
        sp_offset = spx.hdr.sp_offset;
        sp_length = spx.hdr.sp_length;
#else
        spx = (samp_parm_ex *) arg;
        sp_offset = spx->hdr.sp_offset;
        sp_length = spx->hdr.sp_length;
#endif
	VDK_PRINT_DEBUG("vtune_ioctl(): sp_offset=0x%x, sp_length=0x%x\n",sp_offset,sp_length);

        if (sp_offset < sizeof(samp_parm_header)) {
            status = -EINVAL;
            break;
        }

        //
        // Verify read access to entire samp parm structure
        // 
        if (!access_ok(VERIFY_READ, arg, sp_offset + sp_length)) {
            status = -EINVAL;
            break;
        }

#ifdef USE_NMI
        switch (spx.hdr.sp_version) {
#else
        switch (spx->hdr.sp_version) {
#endif
        case 6:
            //
            // Configure or start sampling
            //
#ifdef USE_NMI
            sp6 = kmalloc(sp_length, GFP_ATOMIC);
	    if (sp6==NULL) {
	      status = -ENOMEM;
	      break;
	    }
	    copy_from_user(sp6, (samp_parm6 *)(arg + sp_offset), sp_length);
#else
            sp6 = (void *) spx + sp_offset;
#endif
            if (cmd == VTUNE_CONFIG_EX) {
                status = samp_configure6(sp6, sp_length);
            }
            else {
#ifdef USE_NMI
	      if (! sp6->calibration) // temporarily disable calibration for hugemem
#endif
                status = start_sampling6(sp6, sp_length);
            }
#ifdef USE_NMI
	    kfree(sp6);
#endif
            break;
        default:
            status = -EINVAL;
            break;
        }

        break;
    case VTUNE_READPERF:

        if (!access_ok(VERIFY_WRITE, arg, sizeof(RDPMC_BUF))) {
            status = -EINVAL;
            break;
        }
        status = samp_read_cpu_perf_counters((RDPMC_BUF *) arg);
        break;
    case VTUNE_STOP:

        status = vtune_sampuserstop();
        break;
    case VTUNE_STAT:

        if (!access_ok(VERIFY_WRITE, arg, sizeof(sampinfo_t))) {
            status = -EINVAL;
            break;
        }
        samp_info.sample_rec_length = pdsa->sample_rec_length;
        copy_to_user((void *) arg, (void *) &samp_info, sizeof (sampinfo_t));
        status = 0;
        break;
    case VTUNE_SYSINFO:

        if (!access_ok(VERIFY_WRITE, arg, sizeof(vtune_sys_info))) {
            status = -EINVAL;
            break;
        }
        copy_to_user((void *) arg, (void *) &vtune_sys_info, sizeof (vtune_sys_info));
        status = 0;
        break;
#ifdef linux32
    case VTUNE_GETCPUINFO:

        status = get_cpu_info((cpu_information *) arg);
        break;
#endif
    case VTUNE_ABORT:

        abort_sampling();
        status = 0;
        break;
#ifdef ENABLE_TGID
    case VTUNE_GETTGRP:
        status = find_thread_id((thread_info *) arg);
        break;
#endif
    default:
        status = -EINVAL;
        break;
    }

    return (status);
}

/*
 *
 *
 *   Function:  samp_write_pc_file
 *
 *   description:
 *
 *   Parms:
 *       entry:  *filp
 *               *buf
 *               count
 *               *ppos
 *
 *       return: Size
 *
 *
 */
size_t
samp_write_pc_file(struct file * filp, char *buf, size_t count, loff_t * ppos)
{
    size_t buf_len, bytes_not_copied;
    BOOLEAN do_copy, do_stop;

    VDK_PRINT_DEBUG("samp_write_pc_file: entered current pid %d \n", current->pid);

    for (;;) {
        if (xchg(&b_stop_sampling, 0)) {
            stop_sampling(FALSE);
        }

        spin_lock(&sample_exec_lock);
        do_copy = do_stop = FALSE;

        buf_len = (size_t) p_sample_buf - (size_t) buf_start;
        if ((samp_info.flags & SINFO_DO_WRITE) && buf_len) {
            do_copy = TRUE;
        }
        if (samp_info.flags & SINFO_STOPPING) {
            do_stop = TRUE;
        }
        samp_info.flags &= ~SINFO_DO_WRITE;
        spin_unlock(&sample_exec_lock);

        bytes_not_copied = 0;
        if (do_copy) {
            if (samp_parms.calibration) {
                do_copy = FALSE;
            } else {
                bytes_not_copied =
                    copy_to_user(buf, buf_start, buf_len);
                VDK_PRINT_DEBUG("samp_write_pc_file: pid 0x%x copy_to_user size %d bytes_not_copied %d \n",
				current->pid, buf_len,
				bytes_not_copied);
            }
            p_sample_buf = buf_start;
            current_buffer_count = 0;
            memset((char *) buf_start, 0, buf_length);
        }
        if (do_copy) {
            if (bytes_not_copied == 0) {
                if (sample_method & METHOD_EBS) {
                    restart_EBS = TRUE;
                }
                break;
            } else {
                stop_sampling(FALSE);
                do_stop = TRUE;
            }
        }
        if (do_stop) {
            VDK_PRINT_DEBUG("samp_write_pc_file: pid 0x%x stopping \n", current->pid);
            stop_sampling(FALSE);
            samp_cleanup();
            buf_len = 0;
            break;
        }
        spin_lock(&sample_exec_lock);
        if (samp_info.flags & SINFO_STARTED) {
            if (xchg(&restart_EBS, 0)) {
                samp_info.sampling_active = TRUE;
                spin_unlock(&sample_exec_lock);
                VDK_PRINT_DEBUG("samp_write_pc_file: pid 0x%x resume EBS \n", current->pid);
                vdrv_resume_EBS();
            } else {
                spin_unlock(&sample_exec_lock);
            }
        } else {
            spin_unlock(&sample_exec_lock);
            buf_len = 0;
            break;
        }
        VDK_PRINT_DEBUG("samp_write_pc_file: pid 0x%x entering sleep \n", current->pid);
	//        wait_event_interruptible(pc_write, (!samp_info.sampling_active));
	interruptible_sleep_on(&pc_write);  // fixes Ctrl-C problem during collection
    }

    return (buf_len);
}

/*
 *
 *
 *   Function:  samp_write_module_file
 *
 *   description:
 *
 *   Parms:
 *       entry:  *fp
 *               *ubuf
 *               cnt
 *               *pp
 *
 *   return: status
 *
 *
 */
ssize_t
samp_write_module_file(struct file * fp, char *ubuf, size_t cnt, loff_t * pp)
{
    unsigned int rec_len, bytes_written, bytes_in_ubuf;
    char *pubuf;
    module_record *mra;

    bytes_in_ubuf = cnt;
    pubuf = ubuf;
    bytes_written = 0;
    while ((mra = get_next_mr_rec_from_out_list())) {
        rec_len = mra->rec_length;

        if (!mra->pid_rec_index_raw) {  // skip modules records that were created before a failed sys_call (eg fork, clone etc.)
            add_to_mr_buf_out_offset(rec_len);
            continue;
        }
        if (bytes_in_ubuf < rec_len) {
            break;
        }
        if (copy_to_user(pubuf, mra, rec_len)) {
            VDK_PRINT_DEBUG("samp_write_module_file: copy_to_user failed \n");
            return (-EFAULT);
        }
        add_to_mr_buf_out_offset(rec_len);

        bytes_written += rec_len;
        pubuf += rec_len;
        bytes_in_ubuf -= rec_len;
    }
    fp->f_pos += bytes_written;
    VDK_PRINT_DEBUG("samp_write_module_file: cnt %d bytes_written %d \n", cnt, bytes_written);

    return (bytes_written);
}

/*
 *
 *
 *   Function:  samp_write_pid_file
 *
 *   description:
 *
 *   Parms:
 *       entry:  *fp
 *               *ubuf
 *               cnt
 *               *pp
 *
 *   return: status
 *
 *
 */
ssize_t
samp_write_pid_file(struct file * fp, char *ubuf, size_t cnt, loff_t * pp)
{
    unsigned int rec_len, bytes_written, bytes_in_ubuf;
    char *pubuf;
    pid_record *pra;

    bytes_in_ubuf = cnt;
    pubuf = ubuf;
    bytes_written = 0;
    while ((pra = get_next_pid_rec_from_out_list())) {
        rec_len = sizeof (pid_record);

        if (bytes_in_ubuf < rec_len) {
            break;
        }
        if (copy_to_user(pubuf, pra, rec_len)) {
            VDK_PRINT_DEBUG("samp_write_pid_file: copy_to_user failed \n");
            return (-EFAULT);
        }
        add_to_pid_buf_out_offset(rec_len);

        bytes_written += rec_len;
        pubuf += rec_len;
        bytes_in_ubuf -= rec_len;
    }
    fp->f_pos += bytes_written;
    VDK_PRINT_DEBUG("samp_write_pid_file: cnt %d bytes_written %d \n", cnt, bytes_written);

    return (bytes_written);
}

/*
 *
 *
 *   Function:  vtune_read
 *
 *   description:
 *
 *   Parms:
 *       entry:  *filp
 *               *buf
 *               count
 *               *ppos
 *
 *       return: Size
 *
 *
 */
ssize_t
vtune_read(struct file * filp, char *buf, size_t count, loff_t * ppos)
{
    ssize_t n;
    struct inode *inode = filp->f_dentry->d_inode;

    VDK_PRINT_DEBUG("vtune_read: entered  current pid %d \n", current->pid);

#ifdef ENFORCE_ROOT_ONLY_ACCESS
    if (!capable(CAP_SYS_PTRACE))
      return (-EPERM);
#endif

    //
    // Add semaphore here for one thread at a time
    //

    if (xchg(&b_stop_sampling, 0)) {
        stop_sampling(FALSE);
    }

    n = 0;
    switch (MINOR(inode->i_rdev)) {
    case PCMINOR:
        n = samp_write_pc_file(filp, buf, count, ppos);
        break;
    case MDMINOR:
        n = samp_write_module_file(filp, buf, count, ppos);
        if (!n && !(samp_info.flags & SINFO_STARTED)) {
            move_current_mr_buf_to_out_list();
            n = samp_write_module_file(filp, buf, count, ppos);
            if (!n) {
                free_mr_bufs();
            }
        }
        break;
    case PIDMINOR:
        n = samp_write_pid_file(filp, buf, count, ppos);
        if (!n && !(samp_info.flags & SINFO_STARTED)) {
            move_current_pid_buf_to_out_list();
            n = samp_write_pid_file(filp, buf, count, ppos);
            if (!n) {
                free_pid_bufs();
            }
        }
        break;
    default:
        break;
    }
    return (n);
}

/*
 *
 *
 *   Function:  vtune_release
 *
 *   description:
 *
 *   Parms:
 *       entry:  *inode
 *               *filp
 *
 *       return: status
 *
 *
 *
 */
int
vtune_release(struct inode *inode, struct file *filp)
{
    if (MINOR(inode->i_rdev) == 1)
        file_count--;

    return (0);
}

/*
 *
 *
 *   Function: vdrvgetsysinfo
 *
 *   description:
 *   This routines provides CPU info for local storage and
 *   export that will be placed into the TB3 file header. For
 *   local lookup we reference the kernel's exported
 *   "boot_cpu_data" struct. For export we execute the
 *   CPUID data and store it accordingly
 *
 *
 *   Parms:
 *       entry:  *callerinfo
 *               *system_cpu_data
 *
 *       return: status
 *
 *
 */

int
vdrvgetsysinfo(void)
{
    int iloop;

    memset(&vtune_sys_info, 0, sizeof (sys_samp_info));
    /* Determine CPUID and features */
#if defined(linux32) || defined(linux32_64)
    g_this_CPUID = cpuid_eax(1);
    g_this_CPU_features = cpuid_edx(1);
    g_CPU_family = (char) ((g_this_CPUID & CPUFAMILY) >> 8);
    g_CPU_model = (char) ((g_this_CPUID & CPUMODEL) >> 4);
#elif defined(linux64)
    g_this_CPUID = itp_get_cpuid(3);   // Itanium(R) processor cpuid reg 3 - version information 39:32 arch rev, 31:24 family, 23:16 model, 15:8 rev, 7:0 largest cpuid index
    g_this_CPU_features = itp_get_cpuid(4);    //
    g_CPU_family = (__u32) ((g_this_CPUID & ITP_CPUID_REG3_FAMILY) >> 24);
    g_CPU_model = (__u32) ((g_this_CPUID & ITP_CPUID_REG3_MODEL) >> 16);
#else
#error Unknown processor
#endif

#ifdef CONFIG_SMP
    /* Total CPUs detected */
    vtune_sys_info.num_processors = smp_num_cpus;
#else
    vtune_sys_info.num_processors = 1;
#endif

    /* Zero Out local storage */
    for (iloop = 0; iloop < MAX_PROCESSORS; iloop++)
        vtune_sys_info.cpu_I_dmap[iloop] = 0;

#ifdef CONFIG_SMP
    /* Fill in cpu_ID info for all processors */
    for (iloop = 0; (iloop < smp_num_cpus) && (iloop < MAX_PROCESSORS); iloop++)
        vtune_sys_info.cpu_I_dmap[iloop] = g_this_CPUID;
#else
    vtune_sys_info.cpu_I_dmap[0] = g_this_CPUID;
#endif

    /* Store additional CPU info */
    vtune_sys_info.cpu_feature_bits = g_this_CPU_features;

    vtune_sys_info.feature_set |=
        FSET_DRV_PROCESS_CREATES_TRACKED_IN_MODULE_RECS;

    /* Driver Version info */
    vtune_sys_info.sysdrv_version_major = SYS_VERSION_MAJOR;
    vtune_sys_info.sysdrv_version_minor = SYS_VERSION_MINOR;

#if defined(linux32) || defined(linux32_64)
    /* Kernel selector info (needed for drilling down to x86-based kernel modules) */
    vtune_sys_info.kernel_cs = __KERNEL_CS; // see asm/segment.h
#endif

    VDK_PRINT_DEBUG("vdrvgetsysinfo: CPUID=%08lx  features=%08lx  Family=%X  numcpus=%d \n",
		    g_this_CPUID, g_this_CPU_features, g_CPU_family,
		    vtune_sys_info.num_processors);

    return (0);
}

/*
 *
 *
 *   Function:  vtune_sampuserstop
 *
 *   description:
 *   This routine stops sampling, writes frees resources and 
 *   optionally copies the sampling information structure to the 
 *   callers buffer. If sampling mode is EBS we need to disable
 *   the APIC delivery INTR and signal the task wake up
 *   manually from here.
 *   
 *   Parms:
 *       entry:  out_buf  - pointer to caller's optional 
 *                         samp_info buffer
 *   
 *       return: status
 *
 *
 */
int
vtune_sampuserstop(void)
{
    VDK_PRINT_DEBUG("vtune_sampuserstop: pid 0x%x entered \n", current->pid);
    stop_sampling(TRUE);

    return (0);
}

/*
 *
 *
 *   Function:   start_sampling6
 *
 *   description:
 *   Sets up sampling session. For TBS sampling  we arm the timer
 *   handler in the kernel patch. For EBS we call the specific
 *   setup routine to enable the performance counter overflow
 *   detect. Startup delay and interval timers are also initialized
 *   here.
 *
 *   Note:   This routine is now a merged source of the original
 *           driver Config and Start Sampling functions.
 *
 *   Parms:
 *       entry:  samp_parm6
 *   
 *       return: status
 *
 *
 */
int
start_sampling6(samp_parm6 * sp6, int sp6_len)
{
    int i, errCode;
    unsigned long max_sample_time = 0;

    /* Check Sampling State */
    if ((samp_info.sampling_active == TRUE)
        || (samp_info.flags & (SINFO_STARTED | SINFO_STOPPING | SINFO_WRITE)))
        return (-EBUSY);

    /* Configure for SP6 Parms */
    errCode = samp_configure6(sp6, sp6_len);
    if (errCode)
        return (errCode);

    /* Xfer current SP6 to global store */
    memcpy(&samp_parms, sp6, sizeof (samp_parm6));

    /* Save sample rate in microseconds and milliseconds. Set default if necessary. */
    if (!sample_rate_us)
        sample_rate_us = 1000;


    if (sample_method & METHOD_EBS) {
        for (i=0; i<MAX_PROCESSORS; i++)
	{
	  eachCPU[i].processor_status = 0;
	  eachCPU[i].processor_EBS_status = 0;
	}

#if defined(linux32) || defined(linux32_64)
        memset(package_status, 0, sizeof (package_status));
	/*
        memset(processor_status, 0, sizeof (processor_status));
        memset(processor_EBS_status, 0, sizeof (processor_EBS_status));
	*/

        //
        // Validate EBS regs
        //
        if (validate_EBS_regs()) {
            // return (STATUS_INVALID_PARAMETER);
        }
        //
        // Make sure apic is enabled. This fixes "no samples" problem after 
        // going into and out of "standby" mode on laptop 
        //
        // CSS: TODO: When the C version has been tested, we can just
        // remove the x86 assembly version...
        // Is this really needed? Seems like the answer is no and we can just check
        // for the apic being re-initialized by the os and then we re-do if needed
        // (i.e. keep track of what we did in apic init, or maybe even call apic init
        // again...)
        //
#if defined(linux32)
        SAMP_Set_Apic_Virtual_Wire_Mode();
#elif defined(linux32_64) 
        if (!IsApicEnabled()) {
            //
            // CSS: TODO: 
            // Should we just do a full apic init?
            //
            VDK_PRINT_WARNING("APIC not enabled. Putting APIC in virtual wire mode.\n");
            SetVirtualWireMode();
        }
#else
#error Unknown architecture
#endif
#endif
    }

    /* Sanity check to we cannot go lower than 1ms */
    sample_rate_ms = sample_rate_us / 1000;
    sample_rate_ms = (sample_rate_ms >= 1) ? sample_rate_ms : 1;

    samp_info.sample_time = 0;
    samp_info.profile_ints_idt = 0;
    samp_info.profile_ints = 0;
    samp_info.sample_count = 0;
    samp_info.test_IO_errs = 0;
    current_buffer_count = 0;
    sample_max_samples = samp_parms.maximum_samples;

    pdsa->sample_session_count++;   //                     06-25-97
    pdsa->sample_count = 0;
    pdsa->sample_skip_count = 0;    //                     01-15-99
    pdsa->suspend_start_count = 0;  //                     01-15-99
    pdsa->suspend_end_count = 0;    //                     01-15-99

    pdsa->duration = 0;
    pdsa->tot_samples = 0;
    pdsa->tot_idt_ints = 0;
    pdsa->tot_profile_ints = 0;
    pdsa->tot_skip_samp = 0;


    pebs_err = 0;       // clear count for Pentium(R) 4 processor  05-31-00

    /* Remove any previous allocations before initializing sample buffer. */
    if (buf_start != NULL)
        vfree(buf_start);

    /* Make sure to check/set default buffer size */
    if (!samp_parms.samps_per_buffer)
        samp_parms.samps_per_buffer = 1000;

    /* Allocate sample buffer per user requested size and initialize. 
     * For safeguard of memory boundary issues with vmalloc() we alloc
     * extra memory for sampling headroom. buffer size should match
     * copy_to_user() recipient in user mode. */
    buf_length = samp_parms.samps_per_buffer * sizeof (sample_record_PC);
    VDK_PRINT_DEBUG("start_sampling6: samps_per_buffer %d  buf_length %d \n",
		    samp_parms.samps_per_buffer, buf_length);
    buf_start = (void *) vmalloc(buf_length);

    /* Check for allocation success */
    if (!buf_start) {
        VDK_PRINT_ERROR("Unable to allocate buffer space for sampling!\n");
        return (-ENOMEM);
    }

    /* Initialize buffer Contents */
    memset((char *) buf_start, 0, buf_length);


    /* Init Sampling Stats - Sanity Check */
    samp_info.flags = SINFO_STOP_COMPLETE;
    samp_info.sampling_active = FALSE;
    signal_thread_event = FALSE;
    samp_info.sample_count = 0;

    /* Initialize Sampling buffer */
    buf_end = (void *) ((unsigned long) buf_start + buf_length);
    p_sample_buf = buf_start;
    memset((char *) buf_start, 0, buf_length);
    current_buffer_count = 0;

    /* Initialize Module load info */
    num_mod_rec = 0;
    num_pid_create_rec = 0;
    // The following pointer should also be initialized!
    g_MR_pointer_head = NULL;

    /* Reset Timer status */
    if (g_start_delay_timer_ON) {
        g_start_delay_timer_ON = FALSE;
        del_timer(&delay_tmr);
    }
    if (g_max_samp_timer_ON) {
        g_max_samp_timer_ON = FALSE;
        del_timer(&time_out_tmr);
    }

    /* Start delay Timer */
    if (samp_parms.start_delay) {
        g_start_delay_timer_ON = TRUE;
        init_timer(&delay_tmr);
        delay_tmr.expires = jiffies + samp_parms.start_delay * HZ;
        delay_tmr.function = (void (*)(unsigned long)) samp_start_delay;
        add_timer(&delay_tmr);
        interruptible_sleep_on(&samp_delay);
    }

    /* Set max Sampling Time */
    b_stop_sampling = FALSE;
    restart_EBS = FALSE;
    if (samp_parms.max_interval) {
        g_max_samp_timer_ON = TRUE;
        max_sample_time = samp_parms.max_interval * HZ;
        init_timer(&time_out_tmr);
        time_out_tmr.function =
            (void (*)(unsigned long)) samp_max_sample_time_expired;
        time_out_tmr.expires = jiffies + max_sample_time;
        add_timer(&time_out_tmr);
    }

    VDK_PRINT_DEBUG("sampling started pid 0x%x \n", current->pid);

    //
    // Preallocate buffers to track system-wide module loads           
    //
    mgid = 0;
    p_current_mr_buf = 0;
    p_mr_buf_out = 0;
    mr_buf_out_offset = 0;
    free_mr_bufs();
    free_pid_bufs();

    samp_info.flags = SINFO_STARTED;
    samp_info.sampling_active = TRUE;

    /* Kick off all counters on each CPU */
    for (i = 0; i < MAX_PROCESSORS; i++) {
        eachCPU[i].start_all = TRUE;
    }

    /* Track start time for elapsed measurement */
    start_time = jiffies;

    total_loads = total_loads_init = total_pid_updates = 0; // Debug Code

    track_module_loads = track_process_creates = FALSE;
    if (!samp_parms.calibration) {
        for (i = 0; i < 4; i++) {
            add_mr_buf_to_free_list(alloc_new_mr_buf());
        }
        for (i = 0; i < 4; i++) {
            add_pid_buf_to_free_list(alloc_new_pid_buf());
        }
        enum_user_mode_modules();
        total_loads_init = total_loads; // Debug Code
        track_module_loads = track_process_creates = TRUE;
    }

    if (sample_method & METHOD_EBS) {
#ifdef linux64
        set_PP_SW();
        errCode = install_perf_isr();
	if (errCode != 0) {
	  VDK_PRINT_ERROR("Unable to install interrupt handler (error=%d)!\n",errCode);
	  stop_sampling(TRUE);
	  samp_cleanup();
	  return (errCode);
	}
#elif defined(linux32) || defined(linux32_64)
	vdrv_init_emon_regs();
#ifdef USE_NMI
	register_nmi_callback();
#endif
#else
#error Unkown Architecture
#endif
	vdrv_start_EBS(); // causes unexpected IRQ trap at vector 20 when NMI is enabled
    }

    return (0);
}

/*
 *
 *
 *   Function:  samp_start_delay
 *
 *   description:
 *   Wake up timer delay handler to start sampling.
 *
 *   Parms:
 *       entry:  ptr
 *   
 *       return: None
 *
 *
 */
void
samp_start_delay(unsigned long ptr)
{
    /* Wake up - Start Sampling! */
    g_start_delay_timer_ON = FALSE;
    wake_up_interruptible(&samp_delay);

    return;
}

/*
 *
 *
 *   Function:  samp_max_sample_time_expired
 *
 *   description: 
 *   Terminates sampling from user timeout spec,
 *
 *   Parms:
 *       entry:  ptr         - pointer to caller's option
 *                             samp_info buffer
 *   
 *       return: None
 *
 *
 */
void
samp_max_sample_time_expired(unsigned long ptr)
{
    g_max_samp_timer_ON = FALSE;
    xchg(&b_stop_sampling, 1);
    wake_up_interruptible(&pc_write);

    return;
}

/*
 *
 *
 *   Function:  samp_get_stats
 *
 *   description:
 *   This routine copies the sampling information structure
 *   to the callers buffer.
 *
 *   Parms:
 *       entry:  out_buf       - pointer to caller's optional 
 *                              samp_info buffer
 *   
 *       return: status
 *
 *
 */
int
samp_get_stats(sampinfo_t * out_buf)
{
    if (out_buf)
        copy_to_user(out_buf, &samp_info, sizeof (sampinfo_t));

    return (0);
}

/*
 *
 *
 *   Function:  samp_get_parm
 *
 *   description:
 *   Retrieves the current sampling parameters/configuration 
 *   contained in samp_parms.
 *
 *   Parms:
 *       entry:  out_buf      - pointer to caller's optional 
 *                             samp_info buffer
 *
 *       return: status  -  OK
 *       
 *
 */
int
samp_get_parm(samp_parm3 * out_buf)
{
    if (out_buf)
        copy_to_user(out_buf, &samp_parms, sizeof (samp_parm3));

    return (0);
}

/*
******************************************************************************
                              E B S   S A M P L I N G
******************************************************************************
*/

/* ------------------------------------------------------------------------- */
/*!
 * @brief       Handle the PMU interrupt
 *
 * @return	none
 *
 * Routine that handles the PMU interrupt and does the actual sampling.
 *
 * <I>Special Notes:</I>
 *
 *	The parameters are different depending on if you are compiling IA32 or
 * Itanium(R)-based systems. In the end, what is really significant is that
 * for IA32, this routine is being called by assembly code, not the usual 
 * Linux* OS interrupt handler. For IA32, we actually hijack the IDT directly 
 * which lets us capture LBR information which would otherwise be lost...
 *
 * @todo Is there anyway to make the func declaration the same? Is it worth it?
 *
 */
int
ebs_intr(int irq, void *arg, struct pt_regs *regs)
{
    INT_FRAME int_frame;
    u32 wake_up_thread;

#ifdef linux32

#if defined(DEBUG) && defined(ALLOW_LBRS)
    //
    // If no lbrs, regs == &regs + 8
    //   (4 for save of quick_freeze_msr, 4 for 1st argument to this routine)
    // If lbrs are being captured, regs == &regs + 16) 
    //   (above + old msr/lbr enable values saved on the stack)
    //
    if ((((char *) regs) != (((char *) &regs) + 8)) &&
    	(((char *) regs) != (((char *) &regs) + 16))) {
#ifndef USE_NMI
	// using printk's during NMI interrupt handling can cause problems
	VDK_PRINT_ERROR("interrupt stack appears wrong. regs is 0x%p, &regs is 0x%p\n", regs, &regs);
#endif
    }
#endif

    samp_stop_emon();
    int_frame.seg_cs = regs->xcs;
    int_frame.eip = regs->eip;
    int_frame.E_flags = regs->eflags;

#elif linux64
    int_frame.iip.quad_part = regs->cr_iip;
    int_frame.ipsr.quad_part = regs->cr_ipsr;
    if (int_frame.ipsr.quad_part & IA64_PSR_IS) {   // check addressing mode at time of profile int (Itanium(R) instructions or IA32)
        unsigned long eflag, csd;

        asm("mov %0=ar.eflag;"  // get IA32 eflags
            "mov %1=ar.csd;"    // get IA32 unscrambled code segment descriptor
          :    "=r"(eflag), "=r"(csd));
        int_frame.E_flags = (__u32) eflag;
        int_frame.csd = csd;
        int_frame.seg_cs = (__u32) regs->r17;
    }
#elif linux32_64
    samp_stop_emon();
    int_frame.iip.quad_part = regs->rip;
    int_frame.ipsr.quad_part = regs->eflags;
    int_frame.seg_cs = regs->cs;
    int_frame.csd = 0;
#else
#error unsupported architecture
#endif

    atomic_inc(&samp_isr_active);
    wake_up_thread = samp_emon_interrupt(&int_frame);

    //
    // If time to signal thread then call wake_up_interruptible() to
    // handle the "wake_up_thread" condition.
    //
    if (wake_up_thread) {
        if (samp_info.flags & SINFO_DO_STOP) {
            xchg(&b_stop_sampling, 1);
        }
        wake_up_interruptible(&pc_write);
    }

    if (samp_info.sampling_active) {
        samp_start_emon(FALSE);
    }

    atomic_dec(&samp_isr_active);

#if defined(linux32_64)
    ack_APIC_irq();     // C code responsible for ACK'ing the APIC
#endif

    return IRQ_HANDLED;
}

/*
 *
 *
 *   Function:  vdrv_start_EBS
 *
 *   description:
 *   Dispatches control to activate sampling on all CPUs.
 *
 *   Parms:
 *       entry:  Dispatch Function
 *               NULL
 *               1
 *               0
 *
 *       return: status
 *
 *
 */
void
vdrv_init_emon_regs(void)
{
#if defined(linux32) || defined(linux32_64)
#ifdef CONFIG_SMP
    smp_call_function(samp_init_emon_regs, NULL, 1, 1); // wait=TRUE to wait for function to complete on other cpu's
#endif

    samp_init_emon_regs(NULL);  //
#endif

    return;
}

/*
 *
 *
 *   Function:  vdrv_start_EBS
 *
 *   description:
 *   Dispatches control to activate sampling on all CPUs.
 *
 *   Parms:
 *       entry:  Dispatch Function
 *               NULL
 *               1
 *               0
 *
 *       return: status
 *
 *
 */
int
vdrv_start_EBS(void)
{
#ifdef CONFIG_SMP
    smp_call_function(samp_start_profile_interrupt, NULL, 1, 0);
#endif

    samp_start_profile_interrupt(NULL);

    return (0);
}

/*
 *
 *   Function:  vdrv_resume_EBS
 *
 *   description:
 *   Dispatches control to resume sampling on all CPUs.
 *
 *   Parms:
 *       entry:  Dispatch Function
 *               NULL
 *               1
 *               0
 *
 *       return: status
 *
 */
int
vdrv_resume_EBS(void)
{
#ifdef CONFIG_SMP
    smp_call_function(samp_start_emon, (void *) 1, 1, 0);    // call samp_start_emon() with do_start=TRUE
#endif

    samp_start_emon((void *) 1);

    return (0);
}

/*
 *
 *   Function:  vdrv_stop_EBS
 *
 *   description:
 *   Dispatches control to deactivate sampling on all CPUs.
 *
 *   Parms:
 *       entry:  Dispatch Function
 *               NULL
 *               1
 *               0
 *
 *       return: None
 *
 */
void
vdrv_stop_EBS(void)
{
#ifdef CONFIG_SMP
    smp_call_function(samp_stop_profile_interrupt, NULL, 1, 1);     // wait=TRUE
#endif

    samp_stop_profile_interrupt(NULL);

    return;
}

/*
******************************************************************************
                    M O D U L E / P R O C E S S    T R A C K I N G
******************************************************************************
*/

void
alloc_module_group_ID(PMGID_INFO pmgid_info)
{
    if (!pmgid_info) {
        return;
    }

    memset(pmgid_info, 0, sizeof (MGID_INFO));
    spin_lock(&mgid_lock);
    mgid++;
    pmgid_info->mgid = mgid;
    spin_unlock(&mgid_lock);

    return;
}

PMR_BUF
get_next_mr_buf_on_out_list(PMR_BUF pmr_buf)
{
    __u32 i;
    LIST_ENTRY *p_list;

    if (!pmr_buf) {
        if (p_mr_buf_out) {
            return (p_mr_buf_out);
        }
    }

    i = (pmr_buf) ? 0 : 1;
    list_for_each(p_list, &mr_buf_out_list_head) {
        if (i) {
            return ((PMR_BUF) p_list);
        }
        if (p_list == &pmr_buf->link) {
            i++;
        }
    }

    return (0);
}

void
update_pid_for_module_group(__u32 pid, PMGID_INFO pmgid_info)
{
    __u32 mr_mgid, first_mr_found, last_mr_found;
    void_ptr mr_first, mr_last, pbuf, pbuf_end;
    PMR_BUF pmr_buf;
    module_record *mra;

    if (!(samp_info.flags & SINFO_STARTED) || !pmgid_info) {
        return;
    }

    total_pid_updates++;    // Debug Code

    VDK_PRINT_DEBUG("update_pid_for_module_group: pid 0x%x mgid %d mrfirst 0x%x mr_last 0x%x \n",
		    pid, pmgid_info->mgid, pmgid_info->mr_first,
		    pmgid_info->mr_last);

    spin_lock(&vtune_modlist_lock);
    mr_first = pmgid_info->mr_first;
    mr_last = pmgid_info->mr_last;
    mr_mgid = pmgid_info->mgid;

    for (;;) {
        if ((mr_first == 0) || (mr_last == 0)) {
            mr_first = mr_last = 0;
        }
        //
        // Look in current module buffer for module records in module group.
        //
        first_mr_found = last_mr_found = 0;
        pmr_buf = p_current_mr_buf;
        VDK_PRINT_DEBUG("update_pid_for_module_group: p_current_mr_buf 0x%x \n", p_current_mr_buf);
        if (pmr_buf) {
            pbuf = &pmr_buf->buf;
            pbuf_end = pbuf + pmr_buf->bytes_in_buf;
            if ((mr_first >= pbuf) && (mr_first <= pbuf_end)) {
                first_mr_found = TRUE;
                pbuf = mr_first;
            }
            if ((mr_last >= pbuf) && (mr_last < pbuf_end)) {
                pbuf_end = mr_last + 1;
            }
            for (; pbuf < pbuf_end; pbuf += mra->rec_length) {
                mra = (module_record *) pbuf;
                VDK_PRINT_DEBUG("update_pid_for_module_group: checking record mra 0x%x pid_rec_index %d \n",
				mra, mra->pid_rec_index);
                if (!mra->pid_rec_index_raw
                    && (mra->pid_rec_index == mr_mgid)) {
                    mra->pid_rec_index = pid;
                    mra->pid_rec_index_raw = 1;
                    VDK_PRINT_DEBUG("update_pid_for_module_group  module record updated pid 0x%x mgid %d mra 0x%x \n",
				    pid, pmgid_info->mgid, mra);
                }
            }
            if (first_mr_found) {
                break;
            }
        }
        //
        // Scan module record buffers on output list from oldest to newest.
        // Update pid for each module record in the module group.
        //
        pmr_buf = 0;
        first_mr_found = last_mr_found = FALSE;
        while ((pmr_buf = get_next_mr_buf_on_out_list(pmr_buf))) {
            VDK_PRINT_DEBUG("Update... 2 pmr_buf 0x%x \n", pmr_buf);
            pbuf = &pmr_buf->buf;
            pbuf_end = pbuf + pmr_buf->bytes_in_buf;
            if (!first_mr_found) {
                if ((mr_first >= pbuf) && (mr_first <= pbuf_end)) {
                    first_mr_found = TRUE;
                    pbuf = mr_first;
                } else {
                    if (mr_first) {
                        continue;   // if mr_first specified, then skip to next (newer) mr buffer
                    }
                }
            }
            if ((mr_last >= pbuf) && (mr_last < pbuf_end)) {
                pbuf_end = mr_last + 1;
                last_mr_found = TRUE;
            }
            for (; pbuf < pbuf_end; pbuf += mra->rec_length) {
                mra = (module_record *) pbuf;
                VDK_PRINT_DEBUG("update_pid_for_module_group 2 checking record mra 0x%x pid_rec_index %d \n",
				mra, mra->pid_rec_index);
                if (!mra->pid_rec_index_raw
                    && mra->pid_rec_index == mr_mgid) {
                    mra->pid_rec_index = pid;
                    mra->pid_rec_index_raw = 1;
                    VDK_PRINT_DEBUG("update_pid_for_module_group 2  module record updated pid 0x%x mgid %d mra 0x%x \n",
				    pid, pmgid_info->mgid, mra);
                }
            }
            if (last_mr_found) {
                break;
            }
        }
        break;
    }

    spin_unlock(&vtune_modlist_lock);

    return;
}

#define insert_tail_list(a,b) list_add_tail(b, a)

PLIST_ENTRY
remove_head_list(PLIST_ENTRY entry)
{
    PLIST_ENTRY poped_entry = NULL;

    if (entry->next && (entry->next != entry)) {
        poped_entry = entry->next;
        list_del(poped_entry);
    }

    return (poped_entry);
}

PMR_BUF
pop_mr_buf_from_head_of_free_list(void)
{
    PLIST_ENTRY entry;

    entry = remove_head_list(&mr_buf_free_list_head);
    if (entry == &mr_buf_free_list_head) {
        entry = NULL;
    }

    return ((PMR_BUF) entry);
}

PMR_BUF
pop_mr_buf_from_head_of_out_list(void)
{
    PLIST_ENTRY entry;

    entry = remove_head_list(&mr_buf_out_list_head);
    if (entry == &mr_buf_out_list_head) {
        entry = NULL;
    }

    return ((PMR_BUF) entry);
}

void
add_mr_buf_to_out_list(PMR_BUF pmr_buf)
{
    insert_tail_list(&mr_buf_out_list_head, &pmr_buf->link);

    return;
}

void
init_mr_buf(PMR_BUF pmr_buf)
{
    if (pmr_buf) {
        memset(pmr_buf, 0, sizeof (MR_BUF));
        pmr_buf->buf_size = MR_BUFFER_DATA_SIZE;
    }

    return;
}

PMR_BUF
alloc_new_mr_buf(void)
{
    PMR_BUF pmr_buf;

    pmr_buf = (PMR_BUF) allocate_pool(non_paged_pool, sizeof (MR_BUF));
    if (pmr_buf) {
        init_mr_buf(pmr_buf);
        VDK_PRINT_DEBUG("segs buffer allocated. addr 0x%x \n", pmr_buf);  // 07-11-97
    } else {
        VDK_PRINT_DEBUG("segs buffer alloc failed.\n");  // 07-11-97
    }

    return (pmr_buf);
}

void
free_mr_bufs(void)
{
    PMR_BUF pmr_buf;

    while ((pmr_buf = pop_mr_buf_from_head_of_free_list())) {
        free_pool(pmr_buf);
        VDK_PRINT_DEBUG("segs buffer freed from free list 0x%x \n", pmr_buf);
    }

    while ((pmr_buf = pop_mr_buf_from_head_of_out_list())) {
        free_pool(pmr_buf);
        VDK_PRINT_DEBUG("segs buffer freed from out list  0x%x \n", pmr_buf);
    }

    return;
}

void
add_mr_buf_to_free_list(PMR_BUF pmr_buf)
{
    if (pmr_buf) {
        init_mr_buf(pmr_buf);
        insert_tail_list(&mr_buf_free_list_head, &pmr_buf->link);
    }

    return;
}

PMR_BUF
get_current_mr_buf(void)
{
    if (!p_current_mr_buf) {
        if (!(p_current_mr_buf = pop_mr_buf_from_head_of_free_list())) {
            p_current_mr_buf = alloc_new_mr_buf();
        }
    }

    return (p_current_mr_buf);
}

void
move_current_mr_buf_to_out_list(void)
{
    PMR_BUF pmr_buf;

    pmr_buf = p_current_mr_buf;
    p_current_mr_buf = 0;
    if (pmr_buf)
        add_mr_buf_to_out_list(pmr_buf);
    
    return;
}

void_ptr
copy_to_mr_buf(void_ptr pdata, __u32 size)
{
    void_ptr pmr;
    PMR_BUF pmr_buf;

    if (!(pmr_buf = get_current_mr_buf())) {
        return (0);
    }

    if ((pmr_buf->bytes_in_buf + size) > pmr_buf->buf_size) {   // will data fit in buffer?
        move_current_mr_buf_to_out_list();  // ..no
        return (copy_to_mr_buf(pdata, size));   // try again
    }

    pmr = &pmr_buf->buf;
    pmr += pmr_buf->bytes_in_buf;
    memcpy(pmr, pdata, size);
    pmr_buf->bytes_in_buf += size;

    if (pmr_buf->bytes_in_buf == pmr_buf->buf_size) {
        move_current_mr_buf_to_out_list();
    }

    return (pmr);
}

void
add_to_mr_buf_out_offset(__u32 rec_length)
{
    mr_buf_out_offset += rec_length;

    return;
}

module_record *
get_next_mr_rec_from_out_list(void)
{
    PMR_BUF pmr_buf;
    module_record *pmr = 0;

    //
    // return address of next module record in 
    //
    if (p_mr_buf_out) {
        if (p_mr_buf_out->bytes_in_buf) {
            pmr = (module_record *) & p_mr_buf_out->buf[mr_buf_out_offset];
            // mr_buf_out_offset += pmr->rec_length;
            if (mr_buf_out_offset < p_mr_buf_out->bytes_in_buf) {
                return ((module_record *) & p_mr_buf_out->
                    buf[mr_buf_out_offset]);
            }
        }
        pmr = 0;
        pmr_buf = xchg(&p_mr_buf_out, 0);
        mr_buf_out_offset = 0;
        add_mr_buf_to_free_list(pmr_buf);
    }

    pmr_buf = pop_mr_buf_from_head_of_out_list();
    if (pmr_buf) {
        p_mr_buf_out = pmr_buf;
        mr_buf_out_offset = 0;
        if (p_mr_buf_out->bytes_in_buf) {
            return ((module_record *) & p_mr_buf_out->buf);
        }
    }

    return (0);
}

//
// Routines to manage pid record buffers
//

PPID_BUF
pop_pid_buf_from_head_of_free_list(void)
{
    PLIST_ENTRY entry;

    entry = remove_head_list(&pid_buf_free_list_head);
    if (entry == &pid_buf_free_list_head) {
        entry = NULL;
    }

    return ((PPID_BUF) entry);
}

PPID_BUF
pop_pid_buf_from_head_of_out_list(void)
{
    PLIST_ENTRY entry;

    entry = remove_head_list(&pid_buf_out_list_head);
    if (entry == &pid_buf_out_list_head) {
        entry = NULL;
    }

    return ((PPID_BUF) entry);
}

void
add_pid_buf_to_out_list(PPID_BUF p_pid_buf)
{
    insert_tail_list(&pid_buf_out_list_head, &p_pid_buf->link);

    return;
}

void
init_pid_buf(PPID_BUF p_pid_buf)
{
    if (p_pid_buf) {
        memset(p_pid_buf, 0, sizeof (MR_BUF));
        p_pid_buf->buf_size = MR_BUFFER_DATA_SIZE;
    }

    return;
}

PPID_BUF
alloc_new_pid_buf(void)
{
    PPID_BUF p_pid_buf;

    p_pid_buf = (PPID_BUF) allocate_pool(non_paged_pool, sizeof (MR_BUF));
    if (p_pid_buf) {
        init_pid_buf(p_pid_buf);
        VDK_PRINT_DEBUG("pid buffer allocated. addr 0x%x \n", p_pid_buf); // 07-11-97
    } else {
        VDK_PRINT_DEBUG("pid buffer alloc failed.\n"); // 07-11-97
    }

    return (p_pid_buf);
}

void
free_pid_bufs(void)
{
    PPID_BUF p_pid_buf;

    while ((p_pid_buf = pop_pid_buf_from_head_of_free_list())) {
        free_pool(p_pid_buf);
        VDK_PRINT_DEBUG("pid buffer freed from free list 0x%x \n", p_pid_buf);
    }

    while ((p_pid_buf = pop_pid_buf_from_head_of_out_list())) {
        free_pool(p_pid_buf);
        VDK_PRINT_DEBUG("pid buffer freed from free out  0x%x \n", p_pid_buf);
    }

    return;
}

void
add_pid_buf_to_free_list(PPID_BUF p_pid_buf)
{
    if (p_pid_buf) {
        init_pid_buf(p_pid_buf);
        insert_tail_list(&pid_buf_free_list_head, &p_pid_buf->link);
    }

    return;
}

PPID_BUF
get_current_pid_buf(void)
{
    if (!p_current_pid_buf) {
        if (!(p_current_pid_buf = pop_pid_buf_from_head_of_free_list())) {
            p_current_pid_buf = alloc_new_pid_buf();
        }
    }

    return (p_current_pid_buf);
}

void
move_current_pid_buf_to_out_list(void)
{
    PPID_BUF p_pid_buf;

    p_pid_buf = p_current_pid_buf;
    p_current_pid_buf = 0;
    if (p_pid_buf) {
        add_pid_buf_to_out_list(p_pid_buf);
        /*  Not currently used on Linux*
           KeSetEvent(&samp_threadEvent,     // signal sampler thread
           (KPRIORITY) 0,
           FALSE);
         */
    }

    return;
}

BOOLEAN
copy_to_pid_buf(void_ptr pdata, __u32 size)
{
    void_ptr i;
    PPID_BUF p_pid_buf;

    if (!(p_pid_buf = get_current_pid_buf())) {
        return (FALSE);
    }

    if ((p_pid_buf->bytes_in_buf + size) > p_pid_buf->buf_size) {   // will data fit in buffer?
        move_current_pid_buf_to_out_list(); // ..no
        return (copy_to_pid_buf(pdata, size));  // try again
    }

    i = &p_pid_buf->buf;
    i += p_pid_buf->bytes_in_buf;
    memcpy(i, pdata, size);
    p_pid_buf->bytes_in_buf += size;

    if (p_pid_buf->bytes_in_buf == p_pid_buf->buf_size) {
        move_current_pid_buf_to_out_list();
    }

    return (TRUE);
}

void
add_to_pid_buf_out_offset(__u32 rec_length)
{
    pid_buf_out_offset += rec_length;

    return;
}

pid_record *
get_next_pid_rec_from_out_list(void)
{
    PPID_BUF p_pid_buf;
    pid_record *ppr = 0;

    //
    // return address of next pid record in 
    //
    if (p_pid_buf_out) {
        if (p_pid_buf_out->bytes_in_buf) {
            ppr = (pid_record *) & p_pid_buf_out->buf[pid_buf_out_offset];
            // mr_buf_out_offset += pmr->rec_length;
            if (pid_buf_out_offset < p_pid_buf_out->bytes_in_buf) {
                return ((pid_record *) & p_pid_buf_out->
                    buf[pid_buf_out_offset]);
            }
        }
        ppr = 0;
        p_pid_buf = xchg(&p_pid_buf_out, 0);
        pid_buf_out_offset = 0;
        add_pid_buf_to_free_list(p_pid_buf);
    }

    p_pid_buf = pop_pid_buf_from_head_of_out_list();
    if (p_pid_buf) {
        p_pid_buf_out = p_pid_buf;
        pid_buf_out_offset = 0;
        if (p_pid_buf_out->bytes_in_buf) {
            return ((pid_record *) & p_pid_buf_out->buf);
        }
    }

    return (0);
}

unsigned short
get_exec_mode(struct task_struct *p)
{
#if defined(linux32)
	return ((unsigned short) MODE_32BIT);
#elif defined(linux32_64)
	if (!p)
		return (MODE_UNKNOWN);
#ifdef KERNEL_26X
	if (p->thread_info->flags & TIF_IA32)
#else
	if (p->thread.flags & THREAD_IA32)
#endif
		return ((unsigned short) MODE_32BIT);
	else
		return ((unsigned short) MODE_64BIT);
#elif defined(linux64)
	return ((unsigned short) MODE_64BIT);
#endif
}

int
samp_load_image_notify_routine(char *name, __u32_PTR base,__u32_PTR size,
__u32 pid, __u32 options, PMGID_INFO pmgid_info, unsigned short  mode)
{
    void_ptr p_mr_rec;
    char *raw_path;
    module_record *mra;
    char buf[sizeof (module_record) + MAXNAMELEN + 32];

    if ((samp_info.flags & SINFO_STARTED)
        && !(samp_info.flags & SINFO_STOPPING)) {

        total_loads++;  // Debug code

        mra = (module_record *) buf;
        raw_path = (char *) ((__u32_PTR) mra + sizeof (module_record));

        memset(mra, 0, sizeof (module_record));

        mra->segment_type = mode;
        mra->load_addr64.quad_part = (__u32_PTR) base;
        mra->length64.quad_part = size;
#ifdef KERNEL_26X
        mra->segment_number = 1; // for user modules
#endif
        mra->global_module_Tb5 = options & LOPTS_GLOBAL_MODULE;
        mra->first_module_rec_in_process = options & LOPTS_1ST_MODREC;
        mra->unload_sample_count = 0xFFFFFFFF;
        if (pmgid_info && pmgid_info->mgid) {
            mra->pid_rec_index = pmgid_info->mgid;
        } else {
            mra->pid_rec_index = pid;
            mra->pid_rec_index_raw = 1; // raw pid
#if defined(DEBUG)
            if (total_loads_init) {
                VDK_PRINT_DEBUG("samp_load_image_notify: setting pid_rec_index_raw pid 0x%x %s \n", pid, name);
            }
#endif
        }
        strncpy(raw_path, name, MAXNAMELEN);
        raw_path[MAXNAMELEN] = 0;
        mra->path_length = (__u16) strlen(raw_path) + 1;
        mra->rec_length =
            (__u16) ((sizeof (module_record) + mra->path_length + 7) & ~7);

#if defined(linux32)
	mra->selector = (pid==0) ? __KERNEL_CS : __USER_CS;
#endif
#if defined(linux32_64)
	if (mode==MODE_64BIT)
	  mra->selector = (pid==0) ? __KERNEL_CS : __USER_CS;
	else if (mode==MODE_32BIT)
	  mra->selector = (pid==0) ? __KERNEL32_CS : __USER32_CS;
	// 0 otherwise ...
#endif
        //
        // See if this module is the Exe for the process
        // 10/21/97 
        //
#if defined(linux64) || defined(linux32)
        if ((mra->load_addr64.quad_part >= app_base_low) &&
            (mra->load_addr64.quad_part < app_base_high))
            mra->exe = 1;
	else
	  mra->exe = 0;
#endif
#if defined(linux32_64)
	// 0x0000000040000000 = 1GB  SLES 8
	//         0x40000000  TASK_UNMAPPED_32 in include/asm/processor.h SLES 8
	// 0x0000000000400000 = 4MB  RH EL 3 64-bit
	// 0x08048000 = ?  RH EL 3 32-bit
	if (mode==MODE_64BIT)
	  if ( ((mra->load_addr64.quad_part >= 0x0000000000400000) &&
		(mra->load_addr64.quad_part <  0x0000000000400000 + 0x00000000002FFFFF))
	       ||
	       ((mra->load_addr64.quad_part >= 0x0000000040000000) &&
		(mra->load_addr64.quad_part <  0x0000000040000000 + 0x00000000002FFFFF)) )
	    mra->exe = 1;
	  else
	    mra->exe = 0;
	else if (mode==MODE_32BIT)
	  if ((mra->load_addr64.quad_part >= 0x08048000) &&
	      (mra->load_addr64.quad_part <  0x08048000 + 0x00FFFFFF))
	    mra->exe = 1;
	  else
	    mra->exe = 0;
	else
	  mra->exe = 0;  // unknown
#endif // linux32_64

        spin_lock(&vtune_modlist_lock);
        mra->load_sample_count = pdsa->sample_count;    // get sample count while lock is held
        // ..to ensure modules records are in sample count
        // ..oreder in the sample file (.tb5 file)
        p_mr_rec = copy_to_mr_buf(mra, mra->rec_length);

        if (p_mr_rec && pmgid_info && pmgid_info->mgid) {
            if (!pmgid_info->mr_first) {
                pmgid_info->mr_first = pmgid_info->mr_last = p_mr_rec;  // save address of module record in module group
            } else {
                pmgid_info->mr_last = p_mr_rec;
            }
        }
        spin_unlock(&vtune_modlist_lock);
    }

    return (STATUS_SUCCESS);
}

//
// create Process Notify Routine
//
void
samp_create_process_notify_routine(__u32 parent_id, __u32 process_id, __u32 create)
{
    char buf[sizeof (pid_record) + 16]; // pid record + some for roundup
    pid_record *pr;

    if (create) {
        VDK_PRINT_DEBUG("create process..... pid 0x%x \n", process_id);
    } else {
        VDK_PRINT_DEBUG("terminate process.. pid 0x%x \n", process_id);
        // FreeProcessinfo(process_id);
        // don't need to track terminates since we deduce terminates
        // from creates 
        return;

    }

    if ((samp_info.flags & SINFO_STARTED)
        && !(samp_info.flags & SINFO_STOPPING)) {

        pr = (pid_record *) buf;
        memset(pr, 0, sizeof (pid_record));
        pr->rec_length = (sizeof (pid_record) + 7) & ~7;    // mult of 8
        pr->pid_event = (create) ? 0 : 1;   // pid_event = 0 = process create, 1 = process terminate
        pr->os_pid = (__u32) process_id;
        pr->sample_count = pdsa->sample_count;
        pr->sample_count_term = 0xFFFFFFFF;
        pr->path_length = 0;
        pr->filename_offset = 0;

        spin_lock(&pidlist_lock);

        copy_to_pid_buf(pr, pr->rec_length);

        spin_unlock(&pidlist_lock);
    }

    return;
}

#ifdef DSA_SUPPORT_MMAP
int 
vtune_mmap(struct file *filp, struct vm_area_struct *vma)
{
	__u32 u32_size;
	__u32 u32_offset;

	VDK_PRINT_DEBUG("vtune_mmap:entering vtune_mmap\n");

	vma->vm_flags |= VM_LOCKED;
/*	vma->vm_private_data =  */
	
	u32_offset = vma->vm_pgoff << PAGE_SHIFT;
	u32_size = vma->vm_end - vma->vm_start;

	if(u32_offset & ~PAGE_MASK)
	{
		VDK_PRINT_ERROR("vtune_mmap: Aborting mmap because offset not page aligned. Offset: %d\n", u32_offset);
		return (-ENXIO);
	}

	if(!g_dsa_kmalloc_size || (u32_size > g_dsa_kmalloc_size))
	{
		VDK_PRINT_ERROR("vtune_mmap: mmap trying to map something bigger than driver shared area. Size to map: %d\n", u32_size);
/*		return (-ENXIO); */
	}

#ifdef REMAP_PAGE_RANGE_REQUIRES_EXTRA_ARGS
	if(remap_page_range(vma, vma->vm_start,__pa(pdsa),pdsa->length,vma->vm_page_prot))
#else
	if(remap_page_range(vma->vm_start,__pa(pdsa),pdsa->length,vma->vm_page_prot))
#endif
	{
		VDK_PRINT_ERROR("vtune_mmap: Aborting mmap because remap_page_range failed\n");
		return (-EAGAIN);
	}

	VDK_PRINT_DEBUG("vtune_mmap: pdsa->length=%d\n", pdsa->length);
	VDK_PRINT_DEBUG("vtune_mmap: pdsa->driver_version=%d\n", pdsa->driver_version);
	VDK_PRINT_DEBUG("vtune_mmap: pdsa->pause_count=%d\n", pdsa->pause_count);
	VDK_PRINT_DEBUG("vtune_mmap: pdsa->area_version=%d\n", pdsa->area_version);
	VDK_PRINT_DEBUG("vtune_mmap: pdsa->mega_hertz=%d\n", pdsa->mega_hertz);
	VDK_PRINT_DEBUG("vtune_mmap: pdsa->tot_samples=%d\n", pdsa->tot_samples);

	VDK_PRINT_DEBUG("vtune_mmap:exiting vtune_mmap\n");

	return (0);
}

/*
  Routine description:
	Allocates memory for the DSA. If memory was not allocated on a page boundary, then the pointer to
	the DSA is set on a page boundary in the memory block. The pages of the DSA are then reserved so
	the call to remap_page_range in vtune_mmap can access them.

   Arguments:
	none
   Return value:
	non-NULL pointer if successful
*/
driver_shared_area *
create_dsa(void)
{
	unsigned long virt_addr;
	/* pointer to page aligned area */
	__u32 *kmalloc_area = NULL;
	
	g_dsa_kmalloc_size = g_dsa_size + 2*PAGE_SIZE; /* size of memory block allocated for DSA */
	VDK_PRINT_DEBUG("create_dsa: dsa size: %d\n",g_dsa_size);
	/* 
	 * allocate space for the driver shared area plus 2 pages. The extra 2 pages could be
	 * be necessary when the pointer to the DSA is moved onto a page boundary. 
	 * We want the DSA pointer on a page boundry because ????????????????
	 * If the DSA pointer is moved to a page boundry we want an extra page on each end of the DSA,
	 * so that we don't expose ??????
	 */
	if(dsa_kmalloc_ptr != NULL)
	{
		VDK_PRINT_ERROR("create_dsa: DSA already allocated\n");
		return (NULL);
	}
	VDK_PRINT_DEBUG("create_dsa: kmalloc bytes: %d\n",(g_dsa_size + 2*PAGE_SIZE));
	dsa_kmalloc_ptr = kmalloc( g_dsa_kmalloc_size, GFP_ATOMIC);
	if(dsa_kmalloc_ptr == NULL)
	{
		VDK_PRINT_ERROR("create_dsa: couldn't kmalloc DSA\n");
		return (NULL);
	}
	VDK_PRINT_DEBUG("create_dsa: kmalloc_ptr: %p\n",dsa_kmalloc_ptr);
    
	/* 
	 * Just zero out the driver_shared_area (not all of the allocated pages). 
	 * There is no reason why we couldn't zero out everything if we wanted to. 
	 */
	memset(dsa_kmalloc_ptr, 0, g_dsa_kmalloc_size);
	
	/* move pointer onto a page boundry */
	kmalloc_area = (__u32 *)(((unsigned long)dsa_kmalloc_ptr + PAGE_SIZE -1) & PAGE_MASK);
	VDK_PRINT_DEBUG("create_dsa: kmalloc_area: %p\n",kmalloc_area);

	/* reserve the page so remap_page_range can access it */
	for (virt_addr=(unsigned long)kmalloc_area; virt_addr < (unsigned long)kmalloc_area + g_dsa_size; virt_addr += PAGE_SIZE)
	{
	  VDK_PRINT_DEBUG("create_dsa: reserving dsa address: %p which is page address:%p\n",(void*)virt_addr, virt_to_page(virt_addr));
	  mem_map_reserve(virt_to_page(virt_addr));
	}

	/* 
	 * set the length of the driver_shared_area; we will use this when the DSA is destroyed
	 * to un-reserve the pages
	 */
	((driver_shared_area*)kmalloc_area)->length = g_dsa_size;
	
	VDK_PRINT_DEBUG("create_dsa: created DSA size:%d\n",g_dsa_size);
	return ((driver_shared_area*)kmalloc_area);
}


/*
  Routine description:
       Checks to see if the pointer to the memory allocated for the DSA is not null and if not sets
       it to zero. Then calculates a pointer to the first reserved page and un-reserves the reserved 
       pages of the DSA. After that the allocated memory for the DSA is freed.

  Arguments:
       none

  Return value:
       0 always
*/
int 
destroy_dsa(void)
{
	unsigned long virt_addr;
	void_ptr p;
	/* pointer to page aligned area */
	__u32 *kmalloc_area = NULL;

	/* 
	 * Free all of the pages we allocated; remember dsa_ptr could point to a page boundary inside
	 * the total allocated area, so we need to use the pointer we got back from the 
	 * kmalloc to free all of the pages.
	 */
	if ((p = xchg(&dsa_kmalloc_ptr, 0))) 
	{
		/* move pointer onto a page boundry to get the first page we reserved */
		kmalloc_area = (__u32 *)(((unsigned long)p + PAGE_SIZE -1) & PAGE_MASK);
		VDK_PRINT_DEBUG("create_dsa: kmalloc_area: %p\n",kmalloc_area);

		/* reserve the page so remap_page_range can access it */
		for(virt_addr=(unsigned long)kmalloc_area; virt_addr < (unsigned long)kmalloc_area + g_dsa_size; virt_addr += PAGE_SIZE)
		{
		  VDK_PRINT_DEBUG("create_dsa: reserving dsa address: %p which is page address:%p\n",
				  (void*)virt_addr, virt_to_page(virt_addr));
		  mem_map_unreserve(virt_to_page(virt_addr));
		}

		free_pool(p);
		VDK_PRINT_DEBUG("destroy_dsa: DSA freed\n");
	}

	return (0);
}

#endif // DSA_SUPPORT_MMAP

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void sample_skipped(void)
 * @brief       Bookkeeping when a sample is skipped (i.e driver is "paused")
 *
 * @return	none
 *
 * Currently skipping a smaple just means updating a count in the
 * driver shared area.
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
void
sample_skipped(void)
{
    pdsa->sample_skip_count++;  // profile interrupt occured but PC sample
                                // ..is being skipped
    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          BOOLEAN check_pause_mode(void) 
 * @brief       See if the driver is skipping samples (ie. is "paused")
 *
 * @return	true if skipping samples, false otherwise
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
BOOLEAN
check_pause_mode(void)
{
    return ((pdsa->pause_count > 0) ? TRUE : FALSE);
}

#ifndef OLDER_KERNEL_SUPPORT
MODULE_LICENSE("GPL");
#endif

#ifdef KERNEL_26X
MODULE_INFO(vermagic, VERMAGIC_STRING);
#else
EXPORT_NO_SYMBOLS;
#endif
