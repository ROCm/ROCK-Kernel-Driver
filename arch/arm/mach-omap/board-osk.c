/*
 * linux/arch/arm/mach-omap/board-osk.c
 *
 * Board specific init for OMAP5912 OSK
 *
 * Written by Dirk Behme <dirk.behme@de.bosch.com>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/clocks.h>
#include <asm/arch/gpio.h>
#include <asm/arch/fpga.h>

#include "common.h"

static struct map_desc osk5912_io_desc[] __initdata = {
{ OMAP_OSK_ETHR_BASE, OMAP_OSK_ETHR_START, OMAP_OSK_ETHR_SIZE,MT_DEVICE },
{ OMAP_OSK_NOR_FLASH_BASE, OMAP_OSK_NOR_FLASH_START, OMAP_OSK_NOR_FLASH_SIZE,
	MT_DEVICE },
};

static struct resource osk5912_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_OSK_ETHR_START,		/* Physical */
		.end	= OMAP_OSK_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,				/* Really GPIO 0 */
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device osk5912_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(osk5912_smc91x_resources),
	.resource	= osk5912_smc91x_resources,
};

static struct platform_device *osk5912_devices[] __initdata = {
	&osk5912_smc91x_device,
};

void osk_init_irq(void)
{
	omap_init_irq();
}

static void __init osk_init(void)
{
        platform_add_devices(osk5912_devices, ARRAY_SIZE(osk5912_devices));
}

static void __init osk_map_io(void)
{
	omap_map_io();
	iotable_init(osk5912_io_desc, ARRAY_SIZE(osk5912_io_desc));

}

MACHINE_START(OMAP_OSK, "TI-OSK")
	MAINTAINER("Dirk Behme <dirk.behme@de.bosch.com>")
	BOOT_MEM(0x10000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	MAPIO(osk_map_io)
	INITIRQ(osk_init_irq)
	INIT_MACHINE(osk_init)
MACHINE_END
