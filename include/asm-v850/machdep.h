/*
 * include/asm-v850/machdep.h -- Machine-dependent definitions
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_MACHDEP_H__
#define __V850_MACHDEP_H__

#include <linux/config.h>

/* chips */
#ifdef CONFIG_V850E_MA1
#include <asm/ma1.h>
#endif
#ifdef CONFIG_V850E_TEG
#include <asm/teg.h>
#endif

/* These are both chips _and_ platforms, so put them in the middle... */
#ifdef CONFIG_V850E2_ANNA
#include <asm/anna.h>
#endif
#ifdef CONFIG_V850E_AS85EP1
#include <asm/as85ep1.h>
#endif

/* platforms */
#ifdef CONFIG_RTE_CB_MA1
#include <asm/rte_ma1_cb.h>
#endif
#ifdef CONFIG_RTE_CB_NB85E
#include <asm/rte_nb85e_cb.h>
#endif
#ifdef CONFIG_V850E_SIM
#include <asm/sim.h>
#endif
#ifdef CONFIG_V850E2_SIM85E2C
#include <asm/sim85e2c.h>
#endif
#ifdef CONFIG_V850E2_FPGA85E2C
#include <asm/fpga85e2c.h>
#endif

#endif /* __V850_MACHDEP_H__ */
