/*
 *  os.c - OS-dependent functions
 *
 *  Copyright (C) 2000 Andrew Henroid
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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <asm/delay.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("os")

static int acpi_irq_irq = 0;
static OSD_HANDLER acpi_irq_handler = NULL;
static void *acpi_irq_context = NULL;

#ifdef ENABLE_DEBUGGER

#include <linux/kdb.h>

/* stuff for debugger support */
int acpi_in_debugger = 0;
extern NATIVE_CHAR line_buf[80];

#endif


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
		*virt = phys_to_virt((unsigned long) phys);
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
	*(u8*) (u32) phys_addr = value;
}

void
acpi_os_mem_out16 (ACPI_PHYSICAL_ADDRESS phys_addr, UINT16 value)
{
	*(u16*) (u32) phys_addr = value;
}

void
acpi_os_mem_out32 (ACPI_PHYSICAL_ADDRESS phys_addr, UINT32 value)
{
	*(u32*) (u32) phys_addr = value;
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

/*
 * Queue for interpreter thread
 */

ACPI_STATUS
acpi_os_queue_for_execution(
			    u32 priority,
			    OSD_EXECUTION_CALLBACK callback,
			    void *context)
{
	if (acpi_run(callback, context))
		return AE_ERROR;
	return AE_OK;
}

/*
 * Semaphores are unused, interpreter access is single threaded
 */

ACPI_STATUS
acpi_os_create_semaphore(u32 max_units, u32 init, ACPI_HANDLE * handle)
{
	/* a hack to fake out sems until we implement them */
	*handle = (ACPI_HANDLE) handle;
	return AE_OK;
}

ACPI_STATUS
acpi_os_delete_semaphore(ACPI_HANDLE handle)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_wait_semaphore(ACPI_HANDLE handle, u32 units, u32 timeout)
{
	return AE_OK;
}

ACPI_STATUS
acpi_os_signal_semaphore(ACPI_HANDLE handle, u32 units)
{
	return AE_OK;
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
