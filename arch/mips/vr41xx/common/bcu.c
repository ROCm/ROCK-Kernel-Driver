/*
 * FILE NAME
 *	arch/mips/vr41xx/common/bcu.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Bus Control Unit routines for the NEC VR4100 series.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
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
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Paul Mundt <lethal@chaoticdreams.org>
 *  - Calculate mips_counter_frequency properly on VR4131.
 *
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/init.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/vr41xx/vr41xx.h>

#define VR4111_CLKSPEEDREG	KSEG1ADDR(0x0b000014)
#define VR4122_CLKSPEEDREG	KSEG1ADDR(0x0f000014)
#define VR4131_CLKSPEEDREG	VR4122_CLKSPEEDREG
 #define CLKSP(x)		((x) & 0x001f)

 #define DIV2B			0x8000
 #define DIV3B			0x4000
 #define DIV4B			0x2000

 #define DIVT(x)		(((x) & 0xf000) >> 12)
 #define DIVVT(x)		(((x) & 0x0f00) >> 8)

 #define TDIVMODE(x)		(2 << (((x) & 0x1000) >> 12))
 #define VTDIVMODE(x)		(((x) & 0x0700) >> 8)

unsigned long vr41xx_vtclock = 0;

static inline u16 read_clkspeed(void)
{
	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121: return readw(VR4111_CLKSPEEDREG);
	case CPU_VR4122: return readw(VR4122_CLKSPEEDREG);
	case CPU_VR4131: return readw(VR4131_CLKSPEEDREG);
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	return 0;
}

static inline unsigned long calculate_pclock(u16 clkspeed)
{
	unsigned long pclock = 0;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		pclock = 18432000 * 64;
		break;
	case CPU_VR4122:
		pclock = 18432000 * 98;
		break;
	case CPU_VR4131:
		pclock = 18432000 * 108;
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	pclock /= CLKSP(clkspeed);
	printk(KERN_INFO "PClock: %ldHz\n", pclock);

	return pclock;
}

static inline unsigned long calculate_vtclock(u16 clkspeed, unsigned long pclock)
{
	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
		/* The NEC VR4111 doesn't have the VTClock. */
		break;
	case CPU_VR4121:
		vr41xx_vtclock = pclock;
		/* DIVVT == 9 Divide by 1.5 . VTClock = (PClock * 6) / 9 */
		if (DIVVT(clkspeed) == 9)
			vr41xx_vtclock = pclock * 6;
		/* DIVVT == 10 Divide by 2.5 . VTClock = (PClock * 4) / 10 */
		else if (DIVVT(clkspeed) == 10)
			vr41xx_vtclock = pclock * 4;
		vr41xx_vtclock /= DIVVT(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vr41xx_vtclock);
		break;
	case CPU_VR4122:
		if(VTDIVMODE(clkspeed) == 7)
			vr41xx_vtclock = pclock / 1;
		else if(VTDIVMODE(clkspeed) == 1)
			vr41xx_vtclock = pclock / 2;
		else
			vr41xx_vtclock = pclock / VTDIVMODE(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vr41xx_vtclock);
		break;
	case CPU_VR4131:
		vr41xx_vtclock = pclock / VTDIVMODE(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vr41xx_vtclock);
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	return vr41xx_vtclock;
}

static inline unsigned long calculate_tclock(u16 clkspeed, unsigned long pclock,
                                             unsigned long vtclock)
{
	unsigned long tclock = 0;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
		if (!(clkspeed & DIV2B))
        		tclock = pclock / 2;
		else if (!(clkspeed & DIV3B))
        		tclock = pclock / 3;
		else if (!(clkspeed & DIV4B))
        		tclock = pclock / 4;
		break;
	case CPU_VR4121:
		tclock = pclock / DIVT(clkspeed);
		break;
	case CPU_VR4122:
	case CPU_VR4131:
		tclock = vtclock / TDIVMODE(clkspeed);
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	printk(KERN_INFO "TClock: %ldHz\n", tclock);

	return tclock;
}

static inline unsigned long calculate_mips_counter_frequency(unsigned long tclock)
{
	/*
	 * VR4131 Revision 2.0 and 2.1 use a value of (tclock / 2).
	 */
	if ((current_cpu_data.processor_id == PRID_VR4131_REV2_0) ||
	    (current_cpu_data.processor_id == PRID_VR4131_REV2_1))
		tclock /= 2;
	else
		tclock /= 4;

	return tclock;
}

void __init vr41xx_bcu_init(void)
{
	unsigned long pclock, vtclock, tclock;
	u16 clkspeed;

	clkspeed = read_clkspeed();

	pclock = calculate_pclock(clkspeed);
	vtclock = calculate_vtclock(clkspeed, pclock);
	tclock = calculate_tclock(clkspeed, pclock, vtclock);

	mips_counter_frequency = calculate_mips_counter_frequency(tclock);
}
