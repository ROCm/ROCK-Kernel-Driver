/*
 *  acpi_osl.c - OS-dependent functions ($Revision: 83 $)
 *
 *  Copyright (C) 2000       Andrew Henroid
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/tqueue.h>
#include <asm/io.h>
#include "acpi.h"

#ifdef CONFIG_ACPI_EFI
#include <asm/efi.h>
u64 efi_mem_attributes (u64 phys_addr);
#endif

#ifdef CONFIG_IA64
#include <asm/hw_irq.h>
#include <asm/delay.h>
#endif


#define _COMPONENT		ACPI_OS_SERVICES
ACPI_MODULE_NAME	("osl")

#define PREFIX		"ACPI: "

typedef struct
{
    OSD_EXECUTION_CALLBACK  function;
    void		    *context;
} ACPI_OS_DPC;


#ifdef ENABLE_DEBUGGER
#include <linux/kdb.h>
/* stuff for debugger support */
int acpi_in_debugger = 0;
extern NATIVE_CHAR line_buf[80];
#endif /*ENABLE_DEBUGGER*/

static int acpi_irq_irq = 0;
static OSD_HANDLER acpi_irq_handler = NULL;
static void *acpi_irq_context = NULL;

extern struct pci_ops *pci_root_ops;

acpi_status
acpi_os_initialize(void)
{
	/*
	 * Initialize PCI configuration space access, as we'll need to access
	 * it while walking the namespace (bus 0 and root bridges w/ _BBNs).
	 */
#ifdef CONFIG_ACPI_PCI
	if (!pci_root_ops) {
		printk(KERN_ERR PREFIX "Access to PCI configuration space unavailable\n");
		return AE_NULL_ENTRY;
	}
#endif

	return AE_OK;
}

acpi_status
acpi_os_terminate(void)
{
	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_irq_irq,
						 acpi_irq_handler);
	}

	return AE_OK;
}

void
acpi_os_printf(const NATIVE_CHAR *fmt,...)
{
	va_list args;
	va_start(args, fmt);
	acpi_os_vprintf(fmt, args);
	va_end(args);
}

void
acpi_os_vprintf(const NATIVE_CHAR *fmt, va_list args)
{
	static char buffer[512];
	
	vsprintf(buffer, fmt, args);

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		kdb_printf("%s", buffer);
	} else {
		printk("%s", buffer);
	}
#else
	printk("%s", buffer);
#endif
}

void *
acpi_os_allocate(ACPI_SIZE size)
{
	return kmalloc(size, GFP_KERNEL);
}

void
acpi_os_free(void *ptr)
{
	kfree(ptr);
}

acpi_status
acpi_os_get_root_pointer(u32 flags, ACPI_POINTER *addr)
{
#ifdef CONFIG_ACPI_EFI
	addr->pointer_type = ACPI_PHYSICAL_POINTER;
	if (efi.acpi20)
		addr->pointer.physical = (ACPI_PHYSICAL_ADDRESS) virt_to_phys(efi.acpi20);
	else if (efi.acpi)
		addr->pointer.physical = (ACPI_PHYSICAL_ADDRESS) virt_to_phys(efi.acpi);
	else {
		printk(KERN_ERR PREFIX "System description tables not found\n");
		return AE_NOT_FOUND;
	}
#else
	if (ACPI_FAILURE(acpi_find_root_pointer(flags, addr))) {
		printk(KERN_ERR PREFIX "System description tables not found\n");
		return AE_NOT_FOUND;
	}
#endif /*CONFIG_ACPI_EFI*/

	return AE_OK;
}

acpi_status
acpi_os_map_memory(ACPI_PHYSICAL_ADDRESS phys, ACPI_SIZE size, void **virt)
{
#ifdef CONFIG_ACPI_EFI
	if (!(EFI_MEMORY_WB & efi_mem_attributes(phys))) {
		*virt = ioremap(phys, size);
	} else {
		*virt = phys_to_virt(phys);
	}
#else
	if (phys > ULONG_MAX) {
		printk(KERN_ERR PREFIX "Cannot map memory that high\n");
		return AE_BAD_PARAMETER;
	}
	/*
	 * ioremap checks to ensure this is in reserved space
	 */
	*virt = ioremap((unsigned long) phys, size);
#endif

	if (!*virt)
		return AE_NO_MEMORY;

	return AE_OK;
}

void
acpi_os_unmap_memory(void *virt, ACPI_SIZE size)
{
	iounmap(virt);
}

acpi_status
acpi_os_get_physical_address(void *virt, ACPI_PHYSICAL_ADDRESS *phys)
{
	if(!phys || !virt)
		return AE_BAD_PARAMETER;

	*phys = virt_to_phys(virt);

	return AE_OK;
}

acpi_status
acpi_os_table_override (acpi_table_header *existing_table, acpi_table_header **new_table)
{
	if (!existing_table || !new_table)
		return AE_BAD_PARAMETER;

	*new_table = NULL;
	return AE_OK;
}

static void
acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	(*acpi_irq_handler)(acpi_irq_context);
}

acpi_status
acpi_os_install_interrupt_handler(u32 irq, OSD_HANDLER handler, void *context)
{
#ifdef CONFIG_IA64
	irq = gsi_to_vector(irq);
#endif
	acpi_irq_irq = irq;
	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq, acpi_irq, SA_SHIRQ, "acpi", acpi_irq)) {
		printk(KERN_ERR PREFIX "SCI (IRQ%d) allocation failed\n", irq);
		return AE_NOT_ACQUIRED;
	}

	return AE_OK;
}

acpi_status
acpi_os_remove_interrupt_handler(u32 irq, OSD_HANDLER handler)
{
	if (acpi_irq_handler) {
#ifdef CONFIG_IA64
		irq = gsi_to_vector(irq);
#endif
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
acpi_os_stall(u32 us)
{
	if (us > 10000) {
		mdelay(us / 1000);
	}
	else {
		udelay(us);
	}
}

acpi_status
acpi_os_read_port(
	ACPI_IO_ADDRESS	port,
	void		*value,
	u32		width)
{
	u32 dummy;

	if (!value)
		value = &dummy;

	switch (width)
	{
	case 8:
		*(u8*)  value = inb(port);
		break;
	case 16:
		*(u16*) value = inw(port);
		break;
	case 32:
		*(u32*) value = inl(port);
		break;
	default:
		BUG();
	}

	return AE_OK;
}

acpi_status
acpi_os_write_port(
	ACPI_IO_ADDRESS	port,
	acpi_integer	value,
	u32		width)
{
	switch (width)
	{
	case 8:
		outb(value, port);
		break;
	case 16:
		outw(value, port);
		break;
	case 32:
		outl(value, port);
		break;
	default:
		BUG();
	}

	return AE_OK;
}

acpi_status
acpi_os_read_memory(
	ACPI_PHYSICAL_ADDRESS	phys_addr,
	void			*value,
	u32			width)
{
	u32			dummy;
	void			*virt_addr;

#ifdef CONFIG_ACPI_EFI
	int			iomem = 0;

	if (EFI_MEMORY_WB & efi_mem_attributes(phys_addr)) {
		virt_addr = phys_to_virt(phys_addr);
	}
	else {
		iomem = 1;
		virt_addr = ioremap(phys_addr, width);
	}
#else
	virt_addr = phys_to_virt(phys_addr);
#endif
	if (!value)
		value = &dummy;

	switch (width) {
	case 8:
		*(u8*) value = *(u8*) virt_addr;
		break;
	case 16:
		*(u16*) value = *(u16*) virt_addr;
		break;
	case 32:
		*(u32*) value = *(u32*) virt_addr;
		break;
	default:
		BUG();
	}

#ifdef CONFIG_ACPI_EFI
	if (iomem)
		iounmap(virt_addr);
#endif

	return AE_OK;
}

acpi_status
acpi_os_write_memory(
	ACPI_PHYSICAL_ADDRESS	phys_addr,
	acpi_integer		value,
	u32			width)
{
	void			*virt_addr;

#ifdef CONFIG_ACPI_EFI
	int			iomem = 0;

	if (EFI_MEMORY_WB & efi_mem_attributes(phys_addr)) {
		virt_addr = phys_to_virt(phys_addr);
	}
	else {
		iomem = 1;
		virt_addr = ioremap(phys_addr, width);
	}
#else
	virt_addr = phys_to_virt(phys_addr);
#endif

	switch (width) {
	case 8:
		*(u8*) virt_addr = value;
		break;
	case 16:
		*(u16*) virt_addr = value;
		break;
	case 32:
		*(u32*) virt_addr = value;
		break;
	default:
		BUG();
	}

#ifdef CONFIG_ACPI_EFI
	if (iomem)
		iounmap(virt_addr);
#endif

	return AE_OK;
}

#ifdef CONFIG_ACPI_PCI

acpi_status
acpi_os_read_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     reg,
	void                    *value,
	u32                     width)
{
	int			result = 0;
	int			size = 0;
	struct pci_bus		bus;

	if (!value)
		return AE_BAD_PARAMETER;

	switch (width) {
	case 8:
		size = 1;
		break;
	case 16:
		size = 2;
		break;
	case 32:
		size = 4;
		break;
	default:
		BUG();
	}

	bus.number = pci_id->bus;
	result = pci_root_ops->read(&bus, PCI_DEVFN(pci_id->device,
						    pci_id->function),
				    reg, size, value);

	return (result ? AE_ERROR : AE_OK);
}

acpi_status
acpi_os_write_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     reg,
	acpi_integer            value,
	u32                     width)
{
	int			result = 0;
	int			size = 0;
	struct pci_bus		bus;

	switch (width) {
	case 8:
		size = 1;
		break;
	case 16:
		size = 2;
		break;
	case 32:
		size = 4;
		break;
	default:
		BUG();
	}

	bus.number = pci_id->bus;
	result = pci_root_ops->write(&bus, PCI_DEVFN(pci_id->device,
						     pci_id->function),
				     reg, size, value);
	return (result ? AE_ERROR : AE_OK);
}

#else /*!CONFIG_ACPI_PCI*/

acpi_status
acpi_os_write_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     reg,
	acpi_integer            value,
	u32                     width)
{
	return (AE_SUPPORT);
}

acpi_status
acpi_os_read_pci_configuration (
	acpi_pci_id             *pci_id,
	u32                     reg,
	void                    *value,
	u32                     width)
{
	return (AE_SUPPORT);
}

#endif /*CONFIG_ACPI_PCI*/


/*
 * See acpi_os_queue_for_execution()
 */
static int
acpi_os_queue_exec (
	void *context)
{
	ACPI_OS_DPC		*dpc = (ACPI_OS_DPC*)context;

	ACPI_FUNCTION_TRACE ("os_queue_exec");

	daemonize();
	strcpy(current->comm, "kacpidpc");

	if (!dpc || !dpc->function)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Executing function [%p(%p)].\n", dpc->function, dpc->context));

	dpc->function(dpc->context);

	kfree(dpc);

	return_ACPI_STATUS (AE_OK);
}

static void
acpi_os_schedule_exec (
	void *context)
{
	ACPI_OS_DPC		*dpc = NULL;
	int			thread_pid = -1;

	ACPI_FUNCTION_TRACE ("os_schedule_exec");

	dpc = (ACPI_OS_DPC*)context;
	if (!dpc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Creating new thread to run function [%p(%p)].\n", dpc->function, dpc->context));

	thread_pid = kernel_thread(acpi_os_queue_exec, dpc,
		(CLONE_FS | CLONE_FILES | SIGCHLD));
	if (thread_pid < 0) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Call to kernel_thread() failed.\n"));
		acpi_os_free(dpc);
	}
    return_VOID;
}

acpi_status
acpi_os_queue_for_execution(
	u32			priority,
	OSD_EXECUTION_CALLBACK	function,
	void			*context)
{
	acpi_status 		status = AE_OK;
	ACPI_OS_DPC 		*dpc = NULL;

	ACPI_FUNCTION_TRACE ("os_queue_for_execution");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Scheduling function [%p(%p)] for deferred execution.\n", function, context));

	if (!function)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

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
		/*
		 * Allocate/initialize DPC structure.  Note that this memory will be
		 * freed by the callee.  The kernel handles the tq_struct list  in a
		 * way that allows us to also free its memory inside the callee.
		 * Because we may want to schedule several tasks with different
		 * parameters we can't use the approach some kernel code uses of
		 * having a static tq_struct.
		 * We can save time and code by allocating the DPC and tq_structs
		 * from the same memory.
		 */
		struct tq_struct *task;

		dpc = kmalloc(sizeof(ACPI_OS_DPC)+sizeof(struct tq_struct), GFP_ATOMIC);
		if (!dpc)
			return_ACPI_STATUS (AE_NO_MEMORY);

		dpc->function = function;
		dpc->context = context;

		task = (void *)(dpc+1);
		INIT_TQUEUE(task, acpi_os_schedule_exec, (void*)dpc);

		if (schedule_task(task) < 0) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Call to schedule_task() failed.\n"));
			kfree(dpc);
			status = AE_ERROR;
		}
	}
	break;
	default:
		/*
		 * Allocate/initialize DPC structure.  Note that this memory will be
		 * freed by the callee.
		 */
		dpc = kmalloc(sizeof(ACPI_OS_DPC), GFP_KERNEL);
		if (!dpc)
			return_ACPI_STATUS (AE_NO_MEMORY);

		dpc->function = function;
		dpc->context = context;

		acpi_os_schedule_exec(dpc);
		break;
	}

	return_ACPI_STATUS (status);
}


acpi_status
acpi_os_create_semaphore(
	u32		max_units,
	u32		initial_units,
	acpi_handle	*handle)
{
	struct semaphore	*sem = NULL;

	ACPI_FUNCTION_TRACE ("os_create_semaphore");

	sem = acpi_os_allocate(sizeof(struct semaphore));
	if (!sem)
		return_ACPI_STATUS (AE_NO_MEMORY);
	memset(sem, 0, sizeof(struct semaphore));

	sema_init(sem, initial_units);

	*handle = (acpi_handle*)sem;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Creating semaphore[%p|%d].\n", *handle, initial_units));

	return_ACPI_STATUS (AE_OK);
}


/*
 * TODO: A better way to delete semaphores?  Linux doesn't have a
 * 'delete_semaphore()' function -- may result in an invalid
 * pointer dereference for non-synchronized consumers.	Should
 * we at least check for blocked threads and signal/cancel them?
 */

acpi_status
acpi_os_delete_semaphore(
	acpi_handle	handle)
{
	struct semaphore *sem = (struct semaphore*) handle;

	ACPI_FUNCTION_TRACE ("os_delete_semaphore");

	if (!sem)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Deleting semaphore[%p].\n", handle));

	acpi_os_free(sem); sem =  NULL;

	return_ACPI_STATUS (AE_OK);
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
acpi_status
acpi_os_wait_semaphore(
	acpi_handle		handle,
	u32			units,
	u32			timeout)
{
	acpi_status		status = AE_OK;
	struct semaphore	*sem = (struct semaphore*)handle;
	int			ret = 0;

	ACPI_FUNCTION_TRACE ("os_wait_semaphore");

	if (!sem || (units < 1))
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS (AE_SUPPORT);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Waiting for semaphore[%p|%d|%d]\n", handle, units, timeout));

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
		if(down_trylock(sem))
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
	
			if (ret != 0)
			 status = AE_TIME;
			}
		break;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Failed to acquire semaphore[%p|%d|%d]\n", handle, units, timeout));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Acquired semaphore[%p|%d|%d]\n", handle, units, timeout));
	}

	return_ACPI_STATUS (status);
}


/*
 * TODO: Support for units > 1?
 */
acpi_status
acpi_os_signal_semaphore(
    acpi_handle 	    handle,
    u32 		    units)
{
	struct semaphore *sem = (struct semaphore *) handle;

	ACPI_FUNCTION_TRACE ("os_signal_semaphore");

	if (!sem || (units < 1))
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS (AE_SUPPORT);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Signaling semaphore[%p|%d]\n", handle, units));

	up(sem);

	return_ACPI_STATUS (AE_OK);
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

	return 0;
}

acpi_status
acpi_os_signal (
    u32		function,
    void	*info)
{
	switch (function)
	{
	case ACPI_SIGNAL_FATAL:
		printk(KERN_ERR PREFIX "Fatal opcode executed\n");
		break;
	case ACPI_SIGNAL_BREAKPOINT:
		{
			char *bp_info = (char*) info;

			printk(KERN_ERR "ACPI breakpoint: %s\n", bp_info);
		}
	default:
		break;
	}

	return AE_OK;
}
