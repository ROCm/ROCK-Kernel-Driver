/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides EtherFabric NIC - EFXXXX (aka Falcon) interrupt
 * vector definitions.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

/*************---- Interrupt Vector Format C Header ----*************/
#define DW0_OFST 0x0 /* Double-word 0: Event queue FIFO interrupts */
  #define EVQ_FIFO_HF_LBN 1
  #define EVQ_FIFO_HF_WIDTH 1
  #define EVQ_FIFO_AF_LBN 0
  #define EVQ_FIFO_AF_WIDTH 1
#define DW1_OFST 0x4 /* Double-word 1: Interrupt indicator */
  #define INT_FLAG_LBN 0
  #define INT_FLAG_WIDTH 1
#define DW2_OFST 0x8 /* Double-word 2: Fatal interrupts */
  #define FATAL_INT_LBN 0
  #define FATAL_INT_WIDTH 1
