/*
 * arch/ppc/platforms/85xx/mpc8560.c
 * 
 * MPC8560 I/O descriptions
 * 
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 * 
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <asm/mpc85xx.h>
#include <asm/ocp.h>

/* These should be defined in platform code */
extern struct ocp_gfar_data mpc85xx_tsec1_def;
extern struct ocp_gfar_data mpc85xx_tsec2_def;
extern struct ocp_mpc_i2c_data mpc85xx_i2c1_def;

/* We use offsets for paddr since we do not know at compile time
 * what CCSRBAR is, platform code should fix this up in
 * setup_arch
 *
 * Only the first IRQ is given even if a device has
 * multiple lines associated with ita
 */
struct ocp_def core_ocp[] = {
        { .vendor       = OCP_VENDOR_FREESCALE,
          .function     = OCP_FUNC_IIC,
          .index        = 0,
          .paddr        = MPC85xx_IIC1_OFFSET,
          .irq          = MPC85xx_IRQ_IIC1,
          .pm           = OCP_CPM_NA,
          .additions    = &mpc85xx_i2c1_def,
        },
        { .vendor       = OCP_VENDOR_FREESCALE,
          .function     = OCP_FUNC_GFAR,
          .index        = 0,
          .paddr        = MPC85xx_ENET1_OFFSET,
          .irq          = MPC85xx_IRQ_TSEC1_TX,
          .pm           = OCP_CPM_NA,
          .additions    = &mpc85xx_tsec1_def,
        },
        { .vendor       = OCP_VENDOR_FREESCALE,
          .function     = OCP_FUNC_GFAR,
          .index        = 1,
          .paddr        = MPC85xx_ENET2_OFFSET,
          .irq          = MPC85xx_IRQ_TSEC2_TX,
          .pm           = OCP_CPM_NA,
          .additions    = &mpc85xx_tsec2_def,
        },
        { .vendor       = OCP_VENDOR_FREESCALE,
          .function     = OCP_FUNC_DMA,
          .index        = 0,
          .paddr        = MPC85xx_DMA_OFFSET,
          .irq          = MPC85xx_IRQ_DMA0,
          .pm           = OCP_CPM_NA,
        },
        { .vendor       = OCP_VENDOR_FREESCALE,
          .function     = OCP_FUNC_PERFMON,
          .index        = 0,
          .paddr        = MPC85xx_PERFMON_OFFSET,
          .irq          = MPC85xx_IRQ_PERFMON,
          .pm           = OCP_CPM_NA,
        },
        { .vendor       = OCP_VENDOR_INVALID
        }
};
