/*
 * FILE NAME
 *	arch/mips/vr41xx/common/time.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Timer routines for the NEC VR4100 series.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2001,2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - Added support for NEC VR4100 series RTC Unit.
 *
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4100 series are supported.
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/param.h>
#include <asm/time.h>
#include <asm/vr41xx/vr41xx.h>

#define VR4111_ETIMELREG	KSEG1ADDR(0x0b0000c0)
#define VR4122_ETIMELREG	KSEG1ADDR(0x0f000100)

u32 vr41xx_rtc_base = 0;

#ifdef CONFIG_VR41XX_RTC
extern unsigned long vr41xx_rtc_get_time(void);
extern int vr41xx_rtc_set_time(unsigned long sec);
#endif

void vr41xx_time_init(void)
{
	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		vr41xx_rtc_base = VR4111_ETIMELREG;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
                vr41xx_rtc_base = VR4122_ETIMELREG;
                break;
        default:
                panic("Unexpected CPU of NEC VR4100 series");
                break;
        }

#ifdef CONFIG_VR41XX_RTC
        rtc_get_time = vr41xx_rtc_get_time;
        rtc_set_time = vr41xx_rtc_set_time;
#endif
}

void vr41xx_timer_setup(struct irqaction *irq)
{
	u32 count;

	setup_irq(MIPS_COUNTER_IRQ, irq);

	count = read_c0_count();
	write_c0_compare(count + (mips_counter_frequency / HZ));
}
