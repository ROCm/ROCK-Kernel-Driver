/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PIC_H
#define _ASM_IA64_SN_PCI_PIC_H

/*
 * PIC AS DEVICE ZERO
 * ------------------
 *
 * PIC handles PCI/X busses.  PCI/X requires that the 'bridge' (i.e. PIC)
 * be designated as 'device 0'.   That is a departure from earlier SGI
 * PCI bridges.  Because of that we use config space 1 to access the
 * config space of the first actual PCI device on the bus. 
 * Here's what the PIC manual says:
 *
 *     The current PCI-X bus specification now defines that the parent
 *     hosts bus bridge (PIC for example) must be device 0 on bus 0. PIC
 *     reduced the total number of devices from 8 to 4 and removed the
 *     device registers and windows, now only supporting devices 0,1,2, and
 *     3. PIC did leave all 8 configuration space windows. The reason was
 *     there was nothing to gain by removing them. Here in lies the problem.
 *     The device numbering we do using 0 through 3 is unrelated to the device
 *     numbering which PCI-X requires in configuration space. In the past we
 *     correlated Configs pace and our device space 0 <-> 0, 1 <-> 1, etc.
 *     PCI-X requires we start a 1, not 0 and currently the PX brick
 *     does associate our:
 * 
 *         device 0 with configuration space window 1,
 *         device 1 with configuration space window 2, 
 *         device 2 with configuration space window 3,
 *         device 3 with configuration space window 4.
 *
 * The net effect is that all config space access are off-by-one with 
 * relation to other per-slot accesses on the PIC.   
 * Here is a table that shows some of that:
 *
 *                               Internal Slot#
 *           |
 *           |     0         1        2         3
 * ----------|---------------------------------------
 * config    |  0x21000   0x22000  0x23000   0x24000
 *           |
 * even rrb  |  0[0]      n/a      1[0]      n/a	[] == implied even/odd
 *           |
 * odd rrb   |  n/a       0[1]     n/a       1[1]
 *           |
 * int dev   |  00       01        10        11
 *           |
 * ext slot# |  1        2         3         4
 * ----------|---------------------------------------
 */


#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/sn/xtalk/xwidget.h>	/* generic widget header */
#else
#include <xtalk/xwidget.h>
#endif

#include <asm/sn/pci/pciio.h>


/*********************************************************************
 *    bus provider function table
 *
 *	Normally, this table is only handed off explicitly
 *	during provider initialization, and the PCI generic
 *	layer will stash a pointer to it in the vertex; however,
 *	exporting it explicitly enables a performance hack in
 *	the generic PCI provider where if we know at compile
 *	time that the only possible PCI provider is a
 *	pcibr, we can go directly to this ops table.
 */

extern pciio_provider_t pci_pic_provider;


/*********************************************************************
 * misc defines
 *
 */
#define PIC_WIDGET_PART_NUM_BUS0 0xd102
#define PIC_WIDGET_PART_NUM_BUS1 0xd112
#define PIC_WIDGET_MFGR_NUM 0x24
#define PIC_WIDGET_REV_A  0x1

#define IS_PIC_PART_REV_A(rev) \
	((rev == (PIC_WIDGET_PART_NUM_BUS0 << 4 | PIC_WIDGET_REV_A)) || \
	(rev == (PIC_WIDGET_PART_NUM_BUS1 << 4 | PIC_WIDGET_REV_A)))

/*********************************************************************
 * register offset defines
 *
 */
	/* Identification Register  -- read-only */
#define PIC_IDENTIFICATION 0x00000000

	/* Status Register  -- read-only */
#define PIC_STATUS 0x00000008

	/* Upper Address Holding Register Bus Side Errors  -- read-only */
#define PIC_UPPER_ADDR_REG_BUS_SIDE_ERRS 0x00000010

	/* Lower Address Holding Register Bus Side Errors  -- read-only */
#define PIC_LOWER_ADDR_REG_BUS_SIDE_ERRS 0x00000018

	/* Control Register  -- read/write */
#define PIC_CONTROL 0x00000020

	/* PCI Request Time-out Value Register  -- read/write */
#define PIC_PCI_REQ_TIME_OUT_VALUE 0x00000028

	/* Interrupt Destination Upper Address Register  -- read/write */
#define PIC_INTR_DEST_UPPER_ADDR 0x00000030

	/* Interrupt Destination Lower Address Register  -- read/write */
#define PIC_INTR_DEST_LOWER_ADDR 0x00000038

	/* Command Word Holding Register Bus Side  -- read-only */
#define PIC_CMD_WORD_REG_BUS_SIDE 0x00000040

	/* LLP Configuration Register (Bus 0 Only)  -- read/write */
#define PIC_LLP_CFG_REG_(BUS_0_ONLY) 0x00000048

	/* PCI Target Flush Register  -- read-only */
#define PIC_PCI_TARGET_FLUSH 0x00000050

	/* Command Word Holding Register Link Side  -- read-only */
#define PIC_CMD_WORD_REG_LINK_SIDE 0x00000058

	/* Response Buffer Error Upper Address Holding  -- read-only */
#define PIC_RESP_BUF_ERR_UPPER_ADDR_ 0x00000060

	/* Response Buffer Error Lower Address Holding  -- read-only */
#define PIC_RESP_BUF_ERR_LOWER_ADDR_ 0x00000068

	/* Test Pin Control Register  -- read/write */
#define PIC_TEST_PIN_CONTROL 0x00000070

	/* Address Holding Register Link Side Errors  -- read-only */
#define PIC_ADDR_REG_LINK_SIDE_ERRS 0x00000078

	/* Direct Map Register  -- read/write */
#define PIC_DIRECT_MAP 0x00000080

	/* PCI Map Fault Address Register  -- read-only */
#define PIC_PCI_MAP_FAULT_ADDR 0x00000090

	/* Arbitration Priority Register  -- read/write */
#define PIC_ARBITRATION_PRIORITY 0x000000A0

	/* Internal Ram Parity Error Register  -- read-only */
#define PIC_INTERNAL_RAM_PARITY_ERR 0x000000B0

	/* PCI Time-out Register  -- read/write */
#define PIC_PCI_TIME_OUT 0x000000C0

	/* PCI Type 1 Configuration Register  -- read/write */
#define PIC_PCI_TYPE_1_CFG 0x000000C8

	/* PCI Bus Error Upper Address Holding Register  -- read-only */
#define PIC_PCI_BUS_ERR_UPPER_ADDR_ 0x000000D0

	/* PCI Bus Error Lower Address Holding Register  -- read-only */
#define PIC_PCI_BUS_ERR_LOWER_ADDR_ 0x000000D8

	/* PCIX Error Address Register  -- read-only */
#define PIC_PCIX_ERR_ADDR 0x000000E0

	/* PCIX Error Attribute Register  -- read-only */
#define PIC_PCIX_ERR_ATTRIBUTE 0x000000E8

	/* PCIX Error Data Register  -- read-only */
#define PIC_PCIX_ERR_DATA 0x000000F0

	/* PCIX Read Request Timeout Error Register  -- read-only */
#define PIC_PCIX_READ_REQ_TIMEOUT_ERR 0x000000F8

	/* Interrupt Status Register  -- read-only */
#define PIC_INTR_STATUS 0x00000100

	/* Interrupt Enable Register  -- read/write */
#define PIC_INTR_ENABLE 0x00000108

	/* Reset Interrupt Status Register  -- write-only */
#define PIC_RESET_INTR_STATUS 0x00000110

	/* Interrupt Mode Register  -- read/write */
#define PIC_INTR_MODE 0x00000118

	/* Interrupt Device Register  -- read/write */
#define PIC_INTR_DEVICE 0x00000120

	/* Host Error Field Register  -- read/write */
#define PIC_HOST_ERR_FIELD 0x00000128

	/* Interrupt Pin 0 Host Address Register  -- read/write */
#define PIC_INTR_PIN_0_HOST_ADDR 0x00000130

	/* Interrupt Pin 1 Host Address Register  -- read/write */
#define PIC_INTR_PIN_1_HOST_ADDR 0x00000138

	/* Interrupt Pin 2 Host Address Register  -- read/write */
#define PIC_INTR_PIN_2_HOST_ADDR 0x00000140

	/* Interrupt Pin 3 Host Address Register  -- read/write */
#define PIC_INTR_PIN_3_HOST_ADDR 0x00000148

	/* Interrupt Pin 4 Host Address Register  -- read/write */
#define PIC_INTR_PIN_4_HOST_ADDR 0x00000150

	/* Interrupt Pin 5 Host Address Register  -- read/write */
#define PIC_INTR_PIN_5_HOST_ADDR 0x00000158

	/* Interrupt Pin 6 Host Address Register  -- read/write */
#define PIC_INTR_PIN_6_HOST_ADDR 0x00000160

	/* Interrupt Pin 7 Host Address Register  -- read/write */
#define PIC_INTR_PIN_7_HOST_ADDR 0x00000168

	/* Error Interrupt View Register  -- read-only */
#define PIC_ERR_INTR_VIEW 0x00000170

	/* Multiple Interrupt Register  -- read-only */
#define PIC_MULTIPLE_INTR 0x00000178

	/* Force Always Interrupt 0 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_0 0x00000180

	/* Force Always Interrupt 1 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_1 0x00000188

	/* Force Always Interrupt 2 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_2 0x00000190

	/* Force Always Interrupt 3 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_3 0x00000198

	/* Force Always Interrupt 4 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_4 0x000001A0

	/* Force Always Interrupt 5 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_5 0x000001A8

	/* Force Always Interrupt 6 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_6 0x000001B0

	/* Force Always Interrupt 7 Register  -- write-only */
#define PIC_FORCE_ALWAYS_INTR_7 0x000001B8

	/* Force w/Pin Interrupt 0 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_0 0x000001C0

	/* Force w/Pin Interrupt 1 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_1 0x000001C8

	/* Force w/Pin Interrupt 2 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_2 0x000001D0

	/* Force w/Pin Interrupt 3 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_3 0x000001D8

	/* Force w/Pin Interrupt 4 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_4 0x000001E0

	/* Force w/Pin Interrupt 5 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_5 0x000001E8

	/* Force w/Pin Interrupt 6 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_6 0x000001F0

	/* Force w/Pin Interrupt 7 Register  -- write-only */
#define PIC_FORCE_PIN_INTR_7 0x000001F8

	/* Device 0 Register  -- read/write */
#define PIC_DEVICE_0 0x00000200

	/* Device 1 Register  -- read/write */
#define PIC_DEVICE_1 0x00000208

	/* Device 2 Register  -- read/write */
#define PIC_DEVICE_2 0x00000210

	/* Device 3 Register  -- read/write */
#define PIC_DEVICE_3 0x00000218

	/* Device 0 Write Request Buffer Register  -- read-only */
#define PIC_DEVICE_0_WRITE_REQ_BUF 0x00000240

	/* Device 1 Write Request Buffer Register  -- read-only */
#define PIC_DEVICE_1_WRITE_REQ_BUF 0x00000248

	/* Device 2 Write Request Buffer Register  -- read-only */
#define PIC_DEVICE_2_WRITE_REQ_BUF 0x00000250

	/* Device 3 Write Request Buffer Register  -- read-only */
#define PIC_DEVICE_3_WRITE_REQ_BUF 0x00000258

	/* Even Device Response Buffer Register  -- read/write */
#define PIC_EVEN_DEVICE_RESP_BUF 0x00000280

	/* Odd Device Response Buffer Register  -- read/write */
#define PIC_ODD_DEVICE_RESP_BUF 0x00000288

	/* Read Response Buffer Status Register  -- read-only */
#define PIC_READ_RESP_BUF_STATUS 0x00000290

	/* Read Response Buffer Clear Register  -- write-only */
#define PIC_READ_RESP_BUF_CLEAR 0x00000298

	/* PCI RR 0 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_0_UPPER_ADDR_MATCH 0x00000300

	/* PCI RR 0 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_0_LOWER_ADDR_MATCH 0x00000308

	/* PCI RR 1 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_1_UPPER_ADDR_MATCH 0x00000310

	/* PCI RR 1 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_1_LOWER_ADDR_MATCH 0x00000318

	/* PCI RR 2 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_2_UPPER_ADDR_MATCH 0x00000320

	/* PCI RR 2 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_2_LOWER_ADDR_MATCH 0x00000328

	/* PCI RR 3 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_3_UPPER_ADDR_MATCH 0x00000330

	/* PCI RR 3 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_3_LOWER_ADDR_MATCH 0x00000338

	/* PCI RR 4 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_4_UPPER_ADDR_MATCH 0x00000340

	/* PCI RR 4 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_4_LOWER_ADDR_MATCH 0x00000348

	/* PCI RR 5 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_5_UPPER_ADDR_MATCH 0x00000350

	/* PCI RR 5 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_5_LOWER_ADDR_MATCH 0x00000358

	/* PCI RR 6 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_6_UPPER_ADDR_MATCH 0x00000360

	/* PCI RR 6 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_6_LOWER_ADDR_MATCH 0x00000368

	/* PCI RR 7 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_7_UPPER_ADDR_MATCH 0x00000370

	/* PCI RR 7 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_7_LOWER_ADDR_MATCH 0x00000378

	/* PCI RR 8 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_8_UPPER_ADDR_MATCH 0x00000380

	/* PCI RR 8 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_8_LOWER_ADDR_MATCH 0x00000388

	/* PCI RR 9 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_9_UPPER_ADDR_MATCH 0x00000390

	/* PCI RR 9 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_9_LOWER_ADDR_MATCH 0x00000398

	/* PCI RR 10 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_10_UPPER_ADDR_MATCH 0x000003A0

	/* PCI RR 10 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_10_LOWER_ADDR_MATCH 0x000003A8

	/* PCI RR 11 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_11_UPPER_ADDR_MATCH 0x000003B0

	/* PCI RR 11 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_11_LOWER_ADDR_MATCH 0x000003B8

	/* PCI RR 12 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_12_UPPER_ADDR_MATCH 0x000003C0

	/* PCI RR 12 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_12_LOWER_ADDR_MATCH 0x000003C8

	/* PCI RR 13 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_13_UPPER_ADDR_MATCH 0x000003D0

	/* PCI RR 13 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_13_LOWER_ADDR_MATCH 0x000003D8

	/* PCI RR 14 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_14_UPPER_ADDR_MATCH 0x000003E0

	/* PCI RR 14 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_14_LOWER_ADDR_MATCH 0x000003E8

	/* PCI RR 15 Upper Address Match Register  -- read-only */
#define PIC_PCI_RR_15_UPPER_ADDR_MATCH 0x000003F0

	/* PCI RR 15 Lower Address Match Register  -- read-only */
#define PIC_PCI_RR_15_LOWER_ADDR_MATCH 0x000003F8

	/* Buffer 0 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_0_FLUSH_CNT_WITH_DATA_TOUCH 0x00000400

	/* Buffer 0 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_0_FLUSH_CNT_W_O_DATA_TOUCH 0x00000408

	/* Buffer 0 Request in Flight Count Register  -- read/write */
#define PIC_BUF_0_REQ_IN_FLIGHT_CNT 0x00000410

	/* Buffer 0 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_0_PREFETCH_REQ_CNT 0x00000418

	/* Buffer 0 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_0_TOTAL_PCI_RETRY_CNT 0x00000420

	/* Buffer 0 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_0_MAX_PCI_RETRY_CNT 0x00000428

	/* Buffer 0 Max Latency Count Register  -- read/write */
#define PIC_BUF_0_MAX_LATENCY_CNT 0x00000430

	/* Buffer 0 Clear All Register  -- read/write */
#define PIC_BUF_0_CLEAR_ALL 0x00000438

	/* Buffer 2 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_2_FLUSH_CNT_WITH_DATA_TOUCH 0x00000440

	/* Buffer 2 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_2_FLUSH_CNT_W_O_DATA_TOUCH 0x00000448

	/* Buffer 2 Request in Flight Count Register  -- read/write */
#define PIC_BUF_2_REQ_IN_FLIGHT_CNT 0x00000450

	/* Buffer 2 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_2_PREFETCH_REQ_CNT 0x00000458

	/* Buffer 2 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_2_TOTAL_PCI_RETRY_CNT 0x00000460

	/* Buffer 2 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_2_MAX_PCI_RETRY_CNT 0x00000468

	/* Buffer 2 Max Latency Count Register  -- read/write */
#define PIC_BUF_2_MAX_LATENCY_CNT 0x00000470

	/* Buffer 2 Clear All Register  -- read/write */
#define PIC_BUF_2_CLEAR_ALL 0x00000478

	/* Buffer 4 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_4_FLUSH_CNT_WITH_DATA_TOUCH 0x00000480

	/* Buffer 4 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_4_FLUSH_CNT_W_O_DATA_TOUCH 0x00000488

	/* Buffer 4 Request in Flight Count Register  -- read/write */
#define PIC_BUF_4_REQ_IN_FLIGHT_CNT 0x00000490

	/* Buffer 4 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_4_PREFETCH_REQ_CNT 0x00000498

	/* Buffer 4 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_4_TOTAL_PCI_RETRY_CNT 0x000004A0

	/* Buffer 4 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_4_MAX_PCI_RETRY_CNT 0x000004A8

	/* Buffer 4 Max Latency Count Register  -- read/write */
#define PIC_BUF_4_MAX_LATENCY_CNT 0x000004B0

	/* Buffer 4 Clear All Register  -- read/write */
#define PIC_BUF_4_CLEAR_ALL 0x000004B8

	/* Buffer 6 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_6_FLUSH_CNT_WITH_DATA_TOUCH 0x000004C0

	/* Buffer 6 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_6_FLUSH_CNT_W_O_DATA_TOUCH 0x000004C8

	/* Buffer 6 Request in Flight Count Register  -- read/write */
#define PIC_BUF_6_REQ_IN_FLIGHT_CNT 0x000004D0

	/* Buffer 6 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_6_PREFETCH_REQ_CNT 0x000004D8

	/* Buffer 6 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_6_TOTAL_PCI_RETRY_CNT 0x000004E0

	/* Buffer 6 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_6_MAX_PCI_RETRY_CNT 0x000004E8

	/* Buffer 6 Max Latency Count Register  -- read/write */
#define PIC_BUF_6_MAX_LATENCY_CNT 0x000004F0

	/* Buffer 6 Clear All Register  -- read/write */
#define PIC_BUF_6_CLEAR_ALL 0x000004F8

	/* Buffer 8 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_8_FLUSH_CNT_WITH_DATA_TOUCH 0x00000500

	/* Buffer 8 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_8_FLUSH_CNT_W_O_DATA_TOUCH 0x00000508

	/* Buffer 8 Request in Flight Count Register  -- read/write */
#define PIC_BUF_8_REQ_IN_FLIGHT_CNT 0x00000510

	/* Buffer 8 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_8_PREFETCH_REQ_CNT 0x00000518

	/* Buffer 8 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_8_TOTAL_PCI_RETRY_CNT 0x00000520

	/* Buffer 8 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_8_MAX_PCI_RETRY_CNT 0x00000528

	/* Buffer 8 Max Latency Count Register  -- read/write */
#define PIC_BUF_8_MAX_LATENCY_CNT 0x00000530

	/* Buffer 8 Clear All Register  -- read/write */
#define PIC_BUF_8_CLEAR_ALL 0x00000538

	/* Buffer 10 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_10_FLUSH_CNT_WITH_DATA_TOUCH 0x00000540

	/* Buffer 10 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_10_FLUSH_CNT_W_O_DATA_TOUCH 0x00000548

	/* Buffer 10 Request in Flight Count Register  -- read/write */
#define PIC_BUF_10_REQ_IN_FLIGHT_CNT 0x00000550

	/* Buffer 10 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_10_PREFETCH_REQ_CNT 0x00000558

	/* Buffer 10 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_10_TOTAL_PCI_RETRY_CNT 0x00000560

	/* Buffer 10 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_10_MAX_PCI_RETRY_CNT 0x00000568

	/* Buffer 10 Max Latency Count Register  -- read/write */
#define PIC_BUF_10_MAX_LATENCY_CNT 0x00000570

	/* Buffer 10 Clear All Register  -- read/write */
#define PIC_BUF_10_CLEAR_ALL 0x00000578

	/* Buffer 12 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_12_FLUSH_CNT_WITH_DATA_TOUCH 0x00000580

	/* Buffer 12 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_12_FLUSH_CNT_W_O_DATA_TOUCH 0x00000588

	/* Buffer 12 Request in Flight Count Register  -- read/write */
#define PIC_BUF_12_REQ_IN_FLIGHT_CNT 0x00000590

	/* Buffer 12 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_12_PREFETCH_REQ_CNT 0x00000598

	/* Buffer 12 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_12_TOTAL_PCI_RETRY_CNT 0x000005A0

	/* Buffer 12 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_12_MAX_PCI_RETRY_CNT 0x000005A8

	/* Buffer 12 Max Latency Count Register  -- read/write */
#define PIC_BUF_12_MAX_LATENCY_CNT 0x000005B0

	/* Buffer 12 Clear All Register  -- read/write */
#define PIC_BUF_12_CLEAR_ALL 0x000005B8

	/* Buffer 14 Flush Count with Data Touch Register  -- read/write */
#define PIC_BUF_14_FLUSH_CNT_WITH_DATA_TOUCH 0x000005C0

	/* Buffer 14 Flush Count w/o Data Touch Register  -- read/write */
#define PIC_BUF_14_FLUSH_CNT_W_O_DATA_TOUCH 0x000005C8

	/* Buffer 14 Request in Flight Count Register  -- read/write */
#define PIC_BUF_14_REQ_IN_FLIGHT_CNT 0x000005D0

	/* Buffer 14 Prefetch Request Count Register  -- read/write */
#define PIC_BUF_14_PREFETCH_REQ_CNT 0x000005D8

	/* Buffer 14 Total PCI Retry Count Register  -- read/write */
#define PIC_BUF_14_TOTAL_PCI_RETRY_CNT 0x000005E0

	/* Buffer 14 Max PCI Retry Count Register  -- read/write */
#define PIC_BUF_14_MAX_PCI_RETRY_CNT 0x000005E8

	/* Buffer 14 Max Latency Count Register  -- read/write */
#define PIC_BUF_14_MAX_LATENCY_CNT 0x000005F0

	/* Buffer 14 Clear All Register  -- read/write */
#define PIC_BUF_14_CLEAR_ALL 0x000005F8

	/* PCIX Read Buffer 0 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_0_ADDR 0x00000A00

	/* PCIX Read Buffer 0 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_0_ATTRIBUTE 0x00000A08

	/* PCIX Read Buffer 1 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_1_ADDR 0x00000A10

	/* PCIX Read Buffer 1 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_1_ATTRIBUTE 0x00000A18

	/* PCIX Read Buffer 2 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_2_ADDR 0x00000A20

	/* PCIX Read Buffer 2 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_2_ATTRIBUTE 0x00000A28

	/* PCIX Read Buffer 3 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_3_ADDR 0x00000A30

	/* PCIX Read Buffer 3 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_3_ATTRIBUTE 0x00000A38

	/* PCIX Read Buffer 4 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_4_ADDR 0x00000A40

	/* PCIX Read Buffer 4 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_4_ATTRIBUTE 0x00000A48

	/* PCIX Read Buffer 5 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_5_ADDR 0x00000A50

	/* PCIX Read Buffer 5 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_5_ATTRIBUTE 0x00000A58

	/* PCIX Read Buffer 6 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_6_ADDR 0x00000A60

	/* PCIX Read Buffer 6 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_6_ATTRIBUTE 0x00000A68

	/* PCIX Read Buffer 7 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_7_ADDR 0x00000A70

	/* PCIX Read Buffer 7 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_7_ATTRIBUTE 0x00000A78

	/* PCIX Read Buffer 8 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_8_ADDR 0x00000A80

	/* PCIX Read Buffer 8 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_8_ATTRIBUTE 0x00000A88

	/* PCIX Read Buffer 9 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_9_ADDR 0x00000A90

	/* PCIX Read Buffer 9 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_9_ATTRIBUTE 0x00000A98

	/* PCIX Read Buffer 10 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_10_ADDR 0x00000AA0

	/* PCIX Read Buffer 10 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_10_ATTRIBUTE 0x00000AA8

	/* PCIX Read Buffer 11 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_11_ADDR 0x00000AB0

	/* PCIX Read Buffer 11 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_11_ATTRIBUTE 0x00000AB8

	/* PCIX Read Buffer 12 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_12_ADDR 0x00000AC0

	/* PCIX Read Buffer 12 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_12_ATTRIBUTE 0x00000AC8

	/* PCIX Read Buffer 13 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_13_ADDR 0x00000AD0

	/* PCIX Read Buffer 13 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_13_ATTRIBUTE 0x00000AD8

	/* PCIX Read Buffer 14 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_14_ADDR 0x00000AE0

	/* PCIX Read Buffer 14 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_14_ATTRIBUTE 0x00000AE8

	/* PCIX Read Buffer 15 Address Register  -- read-only */
#define PIC_PCIX_READ_BUF_15_ADDR 0x00000AF0

	/* PCIX Read Buffer 15 Attribute Register  -- read-only */
#define PIC_PCIX_READ_BUF_15_ATTRIBUTE 0x00000AF8

	/* PCIX Write Buffer 0 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_0_ADDR 0x00000B00

	/* PCIX Write Buffer 0 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_0_ATTRIBUTE 0x00000B08

	/* PCIX Write Buffer 0 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_0_VALID 0x00000B10

	/* PCIX Write Buffer 1 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_1_ADDR 0x00000B20

	/* PCIX Write Buffer 1 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_1_ATTRIBUTE 0x00000B28

	/* PCIX Write Buffer 1 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_1_VALID 0x00000B30

	/* PCIX Write Buffer 2 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_2_ADDR 0x00000B40

	/* PCIX Write Buffer 2 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_2_ATTRIBUTE 0x00000B48

	/* PCIX Write Buffer 2 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_2_VALID 0x00000B50

	/* PCIX Write Buffer 3 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_3_ADDR 0x00000B60

	/* PCIX Write Buffer 3 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_3_ATTRIBUTE 0x00000B68

	/* PCIX Write Buffer 3 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_3_VALID 0x00000B70

	/* PCIX Write Buffer 4 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_4_ADDR 0x00000B80

	/* PCIX Write Buffer 4 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_4_ATTRIBUTE 0x00000B88

	/* PCIX Write Buffer 4 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_4_VALID 0x00000B90

	/* PCIX Write Buffer 5 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_5_ADDR 0x00000BA0

	/* PCIX Write Buffer 5 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_5_ATTRIBUTE 0x00000BA8

	/* PCIX Write Buffer 5 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_5_VALID 0x00000BB0

	/* PCIX Write Buffer 6 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_6_ADDR 0x00000BC0

	/* PCIX Write Buffer 6 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_6_ATTRIBUTE 0x00000BC8

	/* PCIX Write Buffer 6 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_6_VALID 0x00000BD0

	/* PCIX Write Buffer 7 Address Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_7_ADDR 0x00000BE0

	/* PCIX Write Buffer 7 Attribute Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_7_ATTRIBUTE 0x00000BE8

	/* PCIX Write Buffer 7 Valid Register  -- read-only */
#define PIC_PCIX_WRITE_BUF_7_VALID 0x00000BF0

/*********************************************************************
 * misc typedefs
 *
 */
typedef uint64_t picreg_t;
typedef uint64_t picate_t;

/*****************************************************************************
 *********************** PIC MMR structure mapping ***************************
 *****************************************************************************/

/* NOTE: PIC WAR. PV#854697.  PIC does not allow writes just to [31:0]
 * of a 64-bit register.  When writing PIC registers, always write the 
 * entire 64 bits.
 */

typedef volatile struct pic_s {

    /* 0x000000-0x00FFFF -- Local Registers */

    /* 0x000000-0x000057 -- Standard Widget Configuration */
    picreg_t		p_wid_id;			/* 0x000000 */
    picreg_t		p_wid_stat;			/* 0x000008 */
    picreg_t		p_wid_err_upper;		/* 0x000010 */
    picreg_t		p_wid_err_lower;		/* 0x000018 */
    #define p_wid_err p_wid_err_lower
    picreg_t		p_wid_control;			/* 0x000020 */
    picreg_t		p_wid_req_timeout;		/* 0x000028 */
    picreg_t		p_wid_int_upper;		/* 0x000030 */
    picreg_t		p_wid_int_lower;		/* 0x000038 */
    #define p_wid_int p_wid_int_lower
    picreg_t		p_wid_err_cmdword;		/* 0x000040 */
    picreg_t		p_wid_llp;			/* 0x000048 */
    picreg_t		p_wid_tflush;			/* 0x000050 */

    /* 0x000058-0x00007F -- Bridge-specific Widget Configuration */
    picreg_t		p_wid_aux_err;			/* 0x000058 */
    picreg_t		p_wid_resp_upper;		/* 0x000060 */
    picreg_t		p_wid_resp_lower;		/* 0x000068 */
    #define p_wid_resp p_wid_resp_lower
    picreg_t		p_wid_tst_pin_ctrl;		/* 0x000070 */
    picreg_t		p_wid_addr_lkerr;		/* 0x000078 */

    /* 0x000080-0x00008F -- PMU & MAP */
    picreg_t		p_dir_map;			/* 0x000080 */
    picreg_t		_pad_000088;			/* 0x000088 */

    /* 0x000090-0x00009F -- SSRAM */
    picreg_t		p_map_fault;			/* 0x000090 */
    picreg_t		_pad_000098;			/* 0x000098 */

    /* 0x0000A0-0x0000AF -- Arbitration */
    picreg_t		p_arb;				/* 0x0000A0 */
    picreg_t		_pad_0000A8;			/* 0x0000A8 */

    /* 0x0000B0-0x0000BF -- Number In A Can or ATE Parity Error */
    picreg_t		p_ate_parity_err;		/* 0x0000B0 */
    picreg_t		_pad_0000B8;			/* 0x0000B8 */

    /* 0x0000C0-0x0000FF -- PCI/GIO */
    picreg_t		p_bus_timeout;			/* 0x0000C0 */
    picreg_t		p_pci_cfg;			/* 0x0000C8 */
    picreg_t		p_pci_err_upper;		/* 0x0000D0 */
    picreg_t		p_pci_err_lower;		/* 0x0000D8 */
    #define p_pci_err p_pci_err_lower
    picreg_t		_pad_0000E0[4];			/* 0x0000{E0..F8} */

    /* 0x000100-0x0001FF -- Interrupt */
    picreg_t		p_int_status;			/* 0x000100 */
    picreg_t		p_int_enable;			/* 0x000108 */
    picreg_t		p_int_rst_stat;			/* 0x000110 */
    picreg_t		p_int_mode;			/* 0x000118 */
    picreg_t		p_int_device;			/* 0x000120 */
    picreg_t		p_int_host_err;			/* 0x000128 */
    picreg_t		p_int_addr[8];			/* 0x0001{30,,,68} */
    picreg_t		p_err_int_view;			/* 0x000170 */
    picreg_t		p_mult_int;			/* 0x000178 */
    picreg_t		p_force_always[8];		/* 0x0001{80,,,B8} */
    picreg_t		p_force_pin[8];			/* 0x0001{C0,,,F8} */

    /* 0x000200-0x000298 -- Device */
    picreg_t		p_device[4];			/* 0x0002{00,,,18} */
    picreg_t		_pad_000220[4];			/* 0x0002{20,,,38} */
    picreg_t		p_wr_req_buf[4];		/* 0x0002{40,,,58} */
    picreg_t		_pad_000260[4];			/* 0x0002{60,,,78} */
    picreg_t		p_rrb_map[2];			/* 0x0002{80,,,88} */
    #define p_even_resp p_rrb_map[0]			/* 0x000280 */
    #define p_odd_resp  p_rrb_map[1]			/* 0x000288 */
    picreg_t		p_resp_status;			/* 0x000290 */
    picreg_t		p_resp_clear;			/* 0x000298 */

    picreg_t		_pad_0002A0[12];		/* 0x0002{A0..F8} */

    /* 0x000300-0x0003F8 -- Buffer Address Match Registers */
    struct {
	picreg_t	upper;				/* 0x0003{00,,,F0} */
	picreg_t	lower;				/* 0x0003{08,,,F8} */
    } p_buf_addr_match[16];

    /* 0x000400-0x0005FF -- Performance Monitor Registers (even only) */
    struct {
	picreg_t	flush_w_touch;			/* 0x000{400,,,5C0} */
	picreg_t	flush_wo_touch;			/* 0x000{408,,,5C8} */
	picreg_t	inflight;			/* 0x000{410,,,5D0} */
	picreg_t	prefetch;			/* 0x000{418,,,5D8} */
	picreg_t	total_pci_retry;		/* 0x000{420,,,5E0} */
	picreg_t	max_pci_retry;			/* 0x000{428,,,5E8} */
	picreg_t	max_latency;			/* 0x000{430,,,5F0} */
	picreg_t	clear_all;			/* 0x000{438,,,5F8} */
    } p_buf_count[8];

    
    /* 0x000600-0x0009FF -- PCI/X registers */
    picreg_t		p_pcix_bus_err_addr;		/* 0x000600 */
    picreg_t		p_pcix_bus_err_attr;		/* 0x000608 */
    picreg_t		p_pcix_bus_err_data;		/* 0x000610 */
    picreg_t		p_pcix_pio_split_addr;		/* 0x000618 */
    picreg_t		p_pcix_pio_split_attr;		/* 0x000620 */
    picreg_t		p_pcix_dma_req_err_attr;	/* 0x000628 */
    picreg_t		p_pcix_dma_req_err_addr;	/* 0x000630 */
    picreg_t		p_pcix_timeout;			/* 0x000638 */

    picreg_t		_pad_000640[120];		/* 0x000{640,,,9F8} */

    /* 0x000A00-0x000BFF -- PCI/X Read&Write Buffer */
    struct {
	picreg_t	p_buf_addr;			/* 0x000{A00,,,AF0} */
	picreg_t	p_buf_attr;			/* 0X000{A08,,,AF8} */
    } p_pcix_read_buf_64[16];

    struct {
	picreg_t	p_buf_addr;			/* 0x000{B00,,,BE0} */
	picreg_t	p_buf_attr;			/* 0x000{B08,,,BE8} */
	picreg_t	p_buf_valid;			/* 0x000{B10,,,BF0} */
	picreg_t	__pad1;				/* 0x000{B18,,,BF8} */
    } p_pcix_write_buf_64[8];

    /* End of Local Registers -- Start of Address Map space */

    char		_pad_000c00[0x010000 - 0x000c00];

    /* 0x010000-0x011fff -- Internal ATE RAM (Auto Parity Generation) */
    picate_t		p_int_ate_ram[1024];		/* 0x010000-0x011fff */

    /* 0x012000-0x013fff -- Internal ATE RAM (Manual Parity Generation) */
    picate_t		p_int_ate_ram_mp[1024];		/* 0x012000-0x013fff */

    char		_pad_014000[0x18000 - 0x014000];

    /* 0x18000-0x197F8 -- PIC Write Request Ram */
    picreg_t		p_wr_req_lower[256];		/* 0x18000 - 0x187F8 */
    picreg_t		p_wr_req_upper[256];		/* 0x18800 - 0x18FF8 */
    picreg_t		p_wr_req_parity[256];		/* 0x19000 - 0x197F8 */

    char		_pad_019800[0x20000 - 0x019800];

    /* 0x020000-0x027FFF -- PCI Device Configuration Spaces */
    union {
	uint8_t		c[0x1000 / 1];			/* 0x02{0000,,,7FFF} */
	uint16_t	s[0x1000 / 2];			/* 0x02{0000,,,7FFF} */
	uint32_t	l[0x1000 / 4];			/* 0x02{0000,,,7FFF} */
	uint64_t	d[0x1000 / 8];			/* 0x02{0000,,,7FFF} */
	union {
	    uint8_t	c[0x100 / 1];
	    uint16_t	s[0x100 / 2];
	    uint32_t	l[0x100 / 4];
	    uint64_t	d[0x100 / 8];
	} f[8];
    } p_type0_cfg_dev[8];				/* 0x02{0000,,,7FFF} */

    /* 0x028000-0x028FFF -- PCI Type 1 Configuration Space */
    union {
	uint8_t		c[0x1000 / 1];			/* 0x028000-0x029000 */
	uint16_t	s[0x1000 / 2];			/* 0x028000-0x029000 */
	uint32_t	l[0x1000 / 4];			/* 0x028000-0x029000 */
	uint64_t	d[0x1000 / 8];			/* 0x028000-0x029000 */
	union {
	    uint8_t	c[0x100 / 1];
	    uint16_t	s[0x100 / 2];
	    uint32_t	l[0x100 / 4];
	    uint64_t	d[0x100 / 8];
	} f[8];
    } p_type1_cfg;					/* 0x028000-0x029000 */

    char		_pad_029000[0x030000-0x029000];

    /* 0x030000-0x030007 -- PCI Interrupt Acknowledge Cycle */
    union {
	uint8_t		c[8 / 1];
	uint16_t	s[8 / 2];
	uint32_t	l[8 / 4];
	uint64_t	d[8 / 8];
    } p_pci_iack;					/* 0x030000-0x030007 */

    char		_pad_030007[0x040000-0x030008];

    /* 0x040000-0x030007 -- PCIX Special Cycle */
    union {
	uint8_t		c[8 / 1];
	uint16_t	s[8 / 2];
	uint32_t	l[8 / 4];
	uint64_t	d[8 / 8];
    } p_pcix_cycle;					/* 0x040000-0x040007 */
} pic_t;

#endif                          /* _ASM_IA64_SN_PCI_PIC_H */
