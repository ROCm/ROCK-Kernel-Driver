/*
 * linux/include/asm-arm/arch-omap/memory.h
 *
 * Memory map for OMAP-1510 and 1610
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This file was derived from linux/include/asm-arm/arch-intergrator/memory.h
 * Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * Task size: 3GB
 */
#define TASK_SIZE		(0xbf000000UL)
#define TASK_SIZE_26		(0x04000000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(0x40000000)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET		(0xC0000000UL)
#define PHYS_OFFSET		(0x10000000UL)

/*
 * OMAP-1510 Local Bus address offset
 */
#define OMAP1510_LB_OFFSET	(0x30000000UL)

/*
 * The DRAM is contiguous.
 */
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - PHYS_OFFSET)

/*
 * Conversion between SDRAM and fake PCI bus, used by USB
 * NOTE: Physical address must be converted to Local Bus address
 *	 on OMAP-1510 only
 */

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 */
#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

/*
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus. See dmadev_uses_omap_lbus().
 */
#ifdef CONFIG_ARCH_OMAP1510
#define bus_to_lbus(x)	((x) + (OMAP1510_LB_OFFSET - PHYS_OFFSET))
#define lbus_to_bus(x)	((x) - (OMAP1510_LB_OFFSET - PHYS_OFFSET))
#endif

#define PHYS_TO_NID(addr) (0)
#endif

