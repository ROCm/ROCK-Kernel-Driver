/*
 * linux/arch/arm/mach-omap/irq.h
 *
 * OMAP specific interrupt bank definitions
 *
 * Copyright (C) 2004 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
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

#define OMAP_IRQ_TYPE710	1
#define OMAP_IRQ_TYPE730	2
#define OMAP_IRQ_TYPE1510	3
#define OMAP_IRQ_TYPE1610	4
#define OMAP_IRQ_TYPE1710	5

#define MAX_NR_IRQ_BANKS	4

#define BANK_NR_IRQS		32

struct omap_irq_desc {
	unsigned int   	cpu_type;
	unsigned int	start_irq;
	unsigned long	level_map;
	unsigned long	base_reg;
	unsigned long	mask_reg;
	unsigned long	ack_reg;
	struct irqchip	*handler;
};

struct omap_irq_bank {
	unsigned int	start_irq;
	unsigned long	level_map;
	unsigned long	base_reg;
	unsigned long	mask_reg;
	unsigned long	ack_reg;
	struct irqchip	*handler;
};

static void omap_offset_ack_irq(unsigned int irq);
static void omap_offset_mask_irq(unsigned int irq);
static void omap_offset_unmask_irq(unsigned int irq);
static void omap_offset_mask_ack_irq(unsigned int irq);

/* NOTE: These will not work if irq bank offset != 0x100 */
#define IRQ_TO_BANK(irq)	(irq >> 5)
#define IRQ_BIT(irq)		(irq & 0x1f)
#define BANK_OFFSET(bank)	((bank - 1) * 0x100)

static struct irqchip omap_offset_irq = {
	.ack	=  omap_offset_mask_ack_irq,
	.mask	=  omap_offset_mask_irq,
	.unmask	=  omap_offset_unmask_irq,
};

/*
 * OMAP-730 interrupt banks
 */
static struct omap_irq_desc omap730_bank0_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE730,
	.start_irq	= 0,
	.level_map	= 0xb3f8e22f,
	.base_reg	= OMAP_IH1_BASE,
	.mask_reg	= OMAP_IH1_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH1_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap730_bank1_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE730,
	.start_irq	= 32,
	.level_map	= 0xfdb9c1f2,
	.base_reg	= OMAP_IH2_BASE,
	.mask_reg	= OMAP_IH2_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap730_bank2_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE730,
	.start_irq	= 64,
	.level_map	= 0x800040f3,
	.base_reg	= OMAP_IH2_BASE + 0x100,
	.mask_reg	= OMAP_IH2_BASE + 0x100 + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG, /* Not replicated */
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

/*
 * OMAP-1510 interrupt banks
 */
static struct omap_irq_desc omap1510_bank0_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1510,
	.start_irq	= 0,
	.level_map	= 0xb3febfff,
	.base_reg	= OMAP_IH1_BASE,
	.mask_reg	= OMAP_IH1_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH1_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap1510_bank1_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1510,
	.start_irq	= 32,
	.level_map	= 0xffbfffed,
	.base_reg	= OMAP_IH2_BASE,
	.mask_reg	= OMAP_IH2_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

/*
 * OMAP-1610 interrupt banks
 */
static struct omap_irq_desc omap1610_bank0_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1610,
	.start_irq	= 0,
	.level_map	= 0xb3fefe8f,
	.base_reg	= OMAP_IH1_BASE,
	.mask_reg	= OMAP_IH1_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH1_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap1610_bank1_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1610,
	.start_irq	= 32,
	.level_map	= 0xfffff7ff,
	.base_reg	= OMAP_IH2_BASE,
	.mask_reg	= OMAP_IH2_BASE + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG,
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap1610_bank2_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1610,
	.start_irq	= 64,
	.level_map	= 0xffffffff,
	.base_reg	= OMAP_IH2_BASE + 0x100,
	.mask_reg	= OMAP_IH2_BASE + 0x100 + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG, /* Not replicated */
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};

static struct omap_irq_desc omap1610_bank3_irqs __initdata = {
	.cpu_type	= OMAP_IRQ_TYPE1610,
	.start_irq	= 96,
	.level_map	= 0xffffffff,
	.base_reg	= OMAP_IH2_BASE + 0x200,
	.mask_reg	= OMAP_IH2_BASE + 0x200 + IRQ_MIR,
	.ack_reg	= OMAP_IH2_BASE + IRQ_CONTROL_REG, /* Not replicated */
	.handler	= &omap_offset_irq,	/* IH2 regs at 0x100 offsets */
};
