/*
 * linux/arch/sh/boards/sh2000/mach.c
 *
 * Original copyright message:
 * Copyright (C) 2001  SUGIOKA Tochinobu
 *
 * Split into mach.c from setup.c by M. R. Brown <mrbrown@0xd6.org>
 */

#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/machvec.h>
#include <asm/machvec_init.h>
#include <asm/rtc.h>
#include <asm/sh2000/sh2000.h>

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_sh2000 __initmv = {
        .mv_nr_irqs             = 80,

        .mv_inb                 = generic_inb,
        .mv_inw                 = generic_inw,
        .mv_inl                 = generic_inl,
        .mv_outb                = generic_outb,
        .mv_outw                = generic_outw,
        .mv_outl                = generic_outl,

        .mv_inb_p               = generic_inb_p,
        .mv_inw_p               = generic_inw_p,
        .mv_inl_p               = generic_inl_p,
        .mv_outb_p              = generic_outb_p,
        .mv_outw_p              = generic_outw_p,
        .mv_outl_p              = generic_outl_p,

        .mv_insb                = generic_insb,
        .mv_insw                = generic_insw,
        .mv_insl                = generic_insl,
        .mv_outsb               = generic_outsb,
        .mv_outsw               = generic_outsw,
        .mv_outsl               = generic_outsl,

        .mv_readb               = generic_readb,
        .mv_readw               = generic_readw,
        .mv_readl               = generic_readl,
        .mv_writeb              = generic_writeb,
        .mv_writew              = generic_writew,
        .mv_writel              = generic_writel,

        .mv_isa_port2addr       = sh2000_isa_port2addr,

        .mv_ioremap             = generic_ioremap,
        .mv_iounmap             = generic_iounmap,
};
ALIAS_MV(sh2000)
