/******************************************************************************
 * 
 * Module Name: os.c - Linux OSL functions
 *		$Revision: 28 $
 *
 *****************************************************************************/

/*
 *  os.c - OS-dependent functions
 *
 *  Copyright (C) 2000 Andrew Henroid
 *  Copyright (C) 2001 Andrew Grover
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* Changes
 *
 * Christopher Liebman <liebman@sponsera.com> 2001-5-15
 * - Fixed improper kernel_thread parameters 
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <acpi.h>
#include "driver.h"

#define _COMPONENT	ACPI_OS_SERVICES
	MODULE_NAME	("os")

typedef struct 
{
    OSD_EXECUTION_CALLBACK  function;
    void		    *context;
} ACPI_OS_DPC;


/*****************************************************************************
 *			       Debugger Stuff
 *****************************************************************************/

#ifdef ENABLE_DEBUGGER

#include <linux/kdb.h>

/* stuff for debugger support */
int acpi_in_debugger = 0;
extern NATIVE_CHAR line_buf[80];

#endif


/*****************************************************************************
 *				    Globals
 *****************************************************************************/

static int acpi_irq_irq = 0;
static OSD_HANDLER acpi_irq_handler = NULL;
static void *acpi_irq_context = NULL;


/******************************************************************************
 *				   Functions
 *****************************************************************************/

ACPI_STATUS
acpi_os_initialize(void)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_terminate(void)
{
	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_irq_irq,
						 acpi_irq_handler);
	}
	return AE_OK;
}

s32
acpi_os_printf(const NATIVE_CHAR *fmt,...)
{
	s32 size;
	va_list args;
	va_start(args, fmt);
	size = acpi_os_vprintf(fmt, args);
	va_end(args);
	return size;
}

s32
acpi_os_vprintf(const NATIVE_CHAR *fmt, va_list args)
{
	static char buffer[512];
	int size = vsprintf(buffer, fmt, args);

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		kdb_printf("%s", buffer);
	} else {
		printk("%s", buffer);
	}
#else
	printk("%s", buffer);
#endif

	return size;
}

void *
acpi_os_allocate(u32 size)
{
	return kmalloc(size, GFP_KERNEL);
}

void *
acpi_os_callocate(u32 size)
{
	void *ptr = acpi_os_allocate(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void
acpi_os_free(void *ptr)
{
	kfree(ptr);
}

ACPI_STATUS
acpi_os_map_memory(ACPI_PHYSICAL_ADDRESS phys, u32 size, void **virt)
{
	if (phys > ULONG_MAX) {
		printk(KERN_ERR "ACPI: Cannot map memory that high\n");
		return AE_ERROR;
	}

	if ((unsigned long) phys < virt_to_phys(high_memory)) {
		struct page *page;
		*virt = phys_to_virt((unsigned long) phys);
	
		/* Check for stamping */
		page = virt_to_page(*virt);
		if(page && !test_bit(PG_reserved, &page->flags))
			printk(KERN_WARNING "ACPI attempting to access kernel owned memory at %08lX.\n", (unsigned long)phys);

		return AE_OK;
	}

	*virt = ioremap((unsigned long) phys, size);
	if (!*virt)
		return AE_ERROR;

	return AE_OK;
}

void
acpi_os_unmap_memory(void *virt, u32 size)
{
	if (virt >= high_memory)
		iounmap(virt);
}

ACPI_STATUS
acpi_os_get_physical_address(void *virt, ACPI_PHYSICAL_ADDRESS *phys)
{
	if(!phys || !virt)
		return AE_BAD_PARAMETER;

	*phys = virt_to_phys(virt);

	return AE_OK;
}

static void
acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	(*acpi_irq_handler)(acpi_irq_context);
}

ACPI_STATUS
acpi_os_install_interrupt_handler(u32 irq, OSD_HANDLER handler, void *context)
{
	acpi_irq_irq = irq;
	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq,
			acpi_irq,
			SA_SHIRQ,
			"acpi",
			acpi_irq)) {
		printk(KERN_ERR "ACPI: SCI (IRQ%d) allocation failed\n", irq);
		return AE_ERROR;
	}
	return AE_OK;
}

ACPI_STATUS
acpi_os_remove_interrupt_handler(u32 irq, OSD_HANDLER handler)
{
	if (acpi_irq_handler) {
		free_irq(irq, acpi_irq);
		acpi_irq_handler = NULL;
	}

	return AE_OK;
}

/*
 * Running in interpreter thread context, safe to sleep
 */

void
acpi_os_sleep(u32 sec, u32 ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ * sec + (ms * HZ) / 1000);
}

void
acpi_os_sleep_usec(u32 us)
{
	udelay(us);
}

u8
acpi_os_in8(ACPI_IO_ADDRESS port)
{
	return inb(port);
}

u16
acpi_os_in16(ACPI_IO_ADDRESS port)
{
	return inw(port);
}

u32
acpi_os_in32(ACPI_IO_ADDRESS port)
{
	return inl(port);
}

void
acpi_os_out8(ACPI_IO_ADDRESS port, u8 val)
{
	outb(val, port);
}

void
acpi_os_out16(ACPI_IO_ADDRESS port, u16 val)
{
	outw(val, port);
}

void
acpi_os_out32(ACPI_IO_ADDRESS port, u32 val)
{
	outl(val, port);
}

UINT8
acpi_os_mem_in8 (ACPI_PHYSICAL_ADDRESS phys_addr)
{
	return (*(u8*) (u32) phys_addr);
}

UINT16
acpi_os_mem_in16 (ACPI_PHYSICAL_ADDRESS phys_addr)
{
	return (*(u16*) (u32) phys_addr);
}

UINT32
acpi_os_mem_in32 (ACPI_PHYSICAL_ADDRESS phys_addr)
{
	return (*(u32*) (u32) phys_addr);
}

void
acpi_os_mem_out8 (ACPI_PHYSICAL_ADDRESS phys_addr, UINT8 value)
{
	*(u8*) phys_to_virt(phys_addr) = value;
}

void
acpi_os_mem_out16 (ACPI_PHYSICAL_ADDRESS phys_addr, UINT16 value)
{
	*(u16*) phys_to_virt(phys_addr) = value;
}

void
acpi_os_mem_out32 (ACPI_PHYSICAL_ADDRESS phys_addr, UINT32 value)
{
	*(u32*) phys_to_virt(phys_addr) = value;
}

ACPI_STATUS
acpi_os_read_pci_cfg_byte(
	u32 bus,
	u32 func,
	u32 addr,
	u8 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_byte(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_read_pci_cfg_word(
	u32 bus,
	u32 func,
	u32 addr,
	u16 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_word(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_read_pci_cfg_dword(
	u32 bus,
	u32 func,
	u32 addr,
	u32 * val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!val || !dev || pci_read_config_dword(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_byte(
	u32 bus,
	u32 func,
	u32 addr,
	u8 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_byte(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_word(
	u32 bus,
	u32 func,
	u32 addr,
	u16 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_word(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_write_pci_cfg_dword(
	u32 bus,
	u32 func,
	u32 addr,
	u32 val)
{
	int devfn = PCI_DEVFN((func >> 16) & 0xffff, func & 0xffff);
	struct pci_dev *dev = pci_find_slot(bus & 0xffff, devfn);
	if (!dev || pci_write_config_dword(dev, addr, val))
		return AE_ERROR;
	return AE_OK;
}

ACPI_STATUS
acpi_os_load_module (
	char *module_name)
{
	FUNCTION_TRACE("acpi_os_load_module");

	if (!module_name)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	if (0 > request_module(module_name)) {
		DEBUG_PRINT(ACPI_WARN, ("Unable to load module [%s].\n", module_name));
		return_ACPI_STATUS(AE_ERROR);
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
acpi_os_unload_module (
	char *module_name)
{
	FUNCTION_TRACE("acpi_os_unload_module");

	if (!module_name)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	/* TODO: How on Linux? */
	/* this is done automatically for all modules with
	use_count = 0, I think. see: MOD_INC_USE_COUNT -ASG */

	return_ACPI_STATUS(AE_OK);
}


/*
 * See acpi_os_queue_for_execution(), too
 */
static int
acpi_os_queue_exec (
	void *context)
{
	ACPI_OS_DPC		*dpc = (ACPI_OS_DPC*)context;

	FUNCTION_TRACE("acpi_os_queue_exec");

	daemonize();
	strcpy(current->comm, "kacpidpc");
    
	if (!dpc || !dpc->function)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	DEBUG_PRINT(ACPI_INFO, ("Executing function [%p(%p)].\n", dpc->function, dpc->context));

	dpc->function(dpc->context);

	acpi_os_free(dpc);

	return_VALUE(1);
}

static void
acpi_os_schedule_exec (
	void *context)
{
	ACPI_OS_DPC		*dpc = NULL;
	int			thread_pid = -1;

	FUNCTION_TRACE("acpi_os_schedule_exec");

	dpc = (ACPI_OS_DPC*)context;
	if (!dpc) {
		DEBUG_PRINT(ACPI_ERROR, ("Invalid (NULL) context.\n"));
		return;
	}

	DEBUG_PRINT(ACPI_INFO, ("Creating new thread to run function [%p(%p)].\n", dpc->function, dpc->context));

	thread_pid = kernel_thread(acpi_os_queue_exec, dpc, 
		(CLONE_FS | CLONE_FILES | SIGCHLD));
	if (thread_pid < 0) {
		DEBUG_PRINT(ACPI_ERROR, ("Call to kernel_thread() failed.\n"));
		acpi_os_free(dpc);
	}

	return_VOID;
}

ACPI_STATUS
acpi_os_queue_for_execution(
	u32			priority,
	OSD_EXECUTION_CALLBACK	function,
	void			*context)
{
	ACPI_STATUS 		status = AE_OK;
	ACPI_OS_DPC 		*dpc = NULL;

	FUNCTION_TRACE("acpi_os_queue_for_execution");

	DEBUG_PRINT(ACPI_INFO, ("Scheduling function [%p(%p)] for deferred execution.\n", function, context));

	if (!function)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	/*
	 * Allocate/initialize DPC structure.  Note that this memory will be
	 * freed by the callee.
	 */
	dpc = acpi_os_callocate(sizeof(ACPI_OS_DPC));
	if (!dpc) 
		return AE_NO_MEMORY;

	dpc->function = function;
	dpc->context = context;

	/*
	 * Queue via DPC:
	 * --------------
	 * Note that we have to use two different processes for queuing DPCs:
	 *	 Interrupt-Level: Use schedule_task; can't spawn a new thread.
	 *	    Kernel-Level: Spawn a new kernel thread, as schedule_task has
	 *			  its limitations (e.g. single-threaded model), and
	 *			  all other task queues run at interrupt-level.
	 */
	switch (priority) {

	case OSD_PRIORITY_GPE:
	{
		static struct tq_struct task;

		memset(&task, 0, sizeof(struct tq_struct));

		task.routine = acpi_os_schedule_exec;
		task.data = (void*)dpc;

		if (schedule_task(&task) < 0) {
			DEBUG_PRINT(ACPI_ERROR, ("Call to schedule_task() failed.\n"));
			status = AE_ERROR;
		}
	}
	break;

	default:
		acpi_os_schedule_exec(dpc);
		break;
	}

	return_ACPI_STATUS(status);
}


ACPI_STATUS
acpi_os_create_semaphore(
	u32		max_units,
	u32		initial_units,
	ACPI_HANDLE	*handle)
{
    struct semaphore	    *sem = NULL;

    FUNCTION_TRACE("acpi_os_create_semaphore");

    sem = acpi_os_callocate(sizeof(struct semaphore));
    if (!sem)
	return_ACPI_STATUS(AE_NO_MEMORY);

    sema_init(sem, initial_units);

    *handle = (ACPI_HANDLE*)sem;

    DEBUG_PRINT(ACPI_INFO, ("Creating semaphore[%p|%d].\n", *handle, initial_units));

    return_ACPI_STATUS(AE_OK);
}


/* 
 * TODO: A better way to delete semaphores?  Linux doesn't have a
 * 'delete_semaphore()' function -- may result in an invalid
 * pointer dereference for non-synchronized consumers.	Should
 * we at least check for blocked threads and signal/cancel them?
 */

ACPI_STATUS
acpi_os_delete_semaphore(
	ACPI_HANDLE handle)
{
    struct semaphore	    *sem = (struct semaphore*)handle;

    FUNCTION_TRACE("acpi_os_delete_semaphore");

    if (!sem) 
	return AE_BAD_PARAMETER;

    DEBUG_PRINT(ACPI_INFO, ("Deleting semaphore[%p].\n", handle));

    acpi_os_free(sem); sem =  NULL;

    return_ACPI_STATUS(AE_OK);
}


/*
 * TODO: The kernel doesn't have a 'down_timeout' function -- had to
 * improvise.  The process is to sleep for one scheduler quantum
 * until the semaphore becomes available.  Downside is that this
 * may result in starvation for timeout-based waits when there's
 * lots of semaphore activity.
 *
 * TODO: Support for units > 1?
 */
ACPI_STATUS
acpi_os_wait_semaphore(
	ACPI_HANDLE	handle,
	u32			units,
	u32			timeout)
{
	ACPI_STATUS		status = AE_OK;
	struct semaphore	*sem = (struct semaphore*)handle;
	int			ret = 0;

	FUNCTION_TRACE("acpi_os_wait_semaphore");

	if (!sem || (units < 1)) 
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS(AE_SUPPORT);

	DEBUG_PRINT(ACPI_INFO, ("Waiting for semaphore[%p|%d|%d]\n", handle, units, timeout));

	switch (timeout)
	{
		/*
		 * No Wait:
		 * --------
		 * A zero timeout value indicates that we shouldn't wait - just
		 * acquire the semaphore if available otherwise return AE_TIME
		 * (a.k.a. 'would block').
		 */
		case 0:
		ret = down_trylock(sem);
		if (ret < 0)
			status = AE_TIME;
		break;

		/*
		 * Wait Indefinitely:
		 * ------------------
		 */
		case WAIT_FOREVER:
		ret = down_interruptible(sem);
		if (ret < 0)
			status = AE_ERROR;
		break;

		/*
		 * Wait w/ Timeout:
		 * ----------------
		 */
		default:
		// TODO: A better timeout algorithm?
		{
			int i = 0;
			static const int quantum_ms = 1000/HZ;

			ret = down_trylock(sem);
			for (i = timeout; (i > 0 && ret < 0); i -= quantum_ms) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
				ret = down_trylock(sem);
			}
	
			if (ret < 0)
			 status = AE_TIME;
			}
		break;
	}

	if (ACPI_FAILURE(status)) {
		DEBUG_PRINT(ACPI_INFO, ("Failed to acquire semaphore[%p|%d|%d]\n", handle, units, timeout));
	}
	else {
		DEBUG_PRINT(ACPI_INFO, ("Acquired semaphore[%p|%d|%d]\n", handle, units, timeout));
	}

	return_ACPI_STATUS(status);
}


/*
 * TODO: Support for units > 1?
 */
ACPI_STATUS
acpi_os_signal_semaphore(
    ACPI_HANDLE 	    handle, 
    u32 		    units)
{
	struct semaphore *sem = (struct semaphore *) handle;

	FUNCTION_TRACE("acpi_os_signal_semaphore");

	if (!sem || (units < 1)) 
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS(AE_SUPPORT);

	DEBUG_PRINT(ACPI_INFO, ("Signaling semaphore[%p|%d]\n", handle, units));

	up(sem);

	return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
acpi_os_breakpoint(NATIVE_CHAR *msg)
{
	acpi_os_printf("breakpoint: %s", msg);
	return AE_OK;
}


void
acpi_os_dbg_trap(char *msg)

{
	acpi_os_printf("trap: %s", msg);
}

void
acpi_os_dbg_assert(void *failure, void *file, u32 line, NATIVE_CHAR *msg)
{
	acpi_os_printf("assert: %s", msg);
}

u32
acpi_os_get_line(NATIVE_CHAR *buffer)
{

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		u32 chars;

		kdb_read(buffer, sizeof(line_buf));

		/* remove the CR kdb includes */ 
		chars = strlen(buffer) - 1;
		buffer[chars] = '\0';
	}
#endif

	return 0;
}

/*
 * We just have to assume we're dealing with valid memory
 */

BOOLEAN
acpi_os_readable(void *ptr, u32 len)
{
	return 1;
}

BOOLEAN
acpi_os_writable(void *ptr, u32 len)
{
	return 1;
}

u32
acpi_os_get_thread_id (void)
{
	if (!in_interrupt())
		return current->pid;

	/*acpi_os_printf("acpi_os_get_thread_id called from interrupt level!\n");*/

	return 0;
}
