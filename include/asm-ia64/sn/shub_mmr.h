/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2004 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_SHUB_MMR_H
#define _ASM_IA64_SN_SHUB_MMR_H

/* ==================================================================== */
/*                        Register "SH_IPI_INT"                         */
/*               SHub Inter-Processor Interrupt Registers               */
/* ==================================================================== */
#define SH_IPI_INT                               0x0000000110000380UL
#define SH_IPI_INT_MASK                          0x8ff3ffffffefffffUL
#define SH_IPI_INT_INIT                          0x0000000000000000UL

/*   SH_IPI_INT_TYPE                                                    */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_IPI_INT_TYPE_SHFT                     0
#define SH_IPI_INT_TYPE_MASK                     0x0000000000000007UL

/*   SH_IPI_INT_AGT                                                     */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_IPI_INT_AGT_SHFT                      3
#define SH_IPI_INT_AGT_MASK                      0x0000000000000008UL

/*   SH_IPI_INT_PID                                                     */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_IPI_INT_PID_SHFT                      4
#define SH_IPI_INT_PID_MASK                      0x00000000000ffff0UL

/*   SH_IPI_INT_BASE                                                    */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_IPI_INT_BASE_SHFT                     21
#define SH_IPI_INT_BASE_MASK                     0x0003ffffffe00000UL

/*   SH_IPI_INT_IDX                                                     */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_IPI_INT_IDX_SHFT                      52
#define SH_IPI_INT_IDX_MASK                      0x0ff0000000000000UL

/*   SH_IPI_INT_SEND                                                    */
/*   Description:  Send Interrupt Message to PI, This generates a puls  */
#define SH_IPI_INT_SEND_SHFT                     63
#define SH_IPI_INT_SEND_MASK                     0x8000000000000000UL

/* ==================================================================== */
/*                     Register "SH_EVENT_OCCURRED"                     */
/*                    SHub Interrupt Event Occurred                     */
/* ==================================================================== */
#define SH_EVENT_OCCURRED                        0x0000000110010000UL
#define SH_EVENT_OCCURRED_ALIAS                  0x0000000110010008UL

/* ==================================================================== */
/*                     Register "SH_PI_CAM_CONTROL"                     */
/*                      CRB CAM MMR Access Control                      */
/* ==================================================================== */
#ifndef __ASSEMBLY__
#define SH_PI_CAM_CONTROL                        0x0000000120050300UL
#else
#define SH_PI_CAM_CONTROL                        0x0000000120050300
#endif

/* ==================================================================== */
/*                        Register "SH_SHUB_ID"                         */
/*                            SHub ID Number                            */
/* ==================================================================== */
#define SH_SHUB_ID                               0x0000000110060580UL
#define SH_SHUB_ID_REVISION_SHFT                 28
#define SH_SHUB_ID_REVISION_MASK                 0x00000000f0000000

/* ==================================================================== */
/*                         Register "SH_PTC_0"                          */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH_PTC_0                                 0x00000001101a0000UL
#define SH_PTC_1                                 0x00000001101a0080UL

/* ==================================================================== */
/*                          Register "SH_RTC"                           */
/*                           Real-time Clock                            */
/* ==================================================================== */
#define SH_RTC                                   0x00000001101c0000UL
#define SH_RTC_MASK                              0x007fffffffffffffUL

/* ==================================================================== */
/*                 Register "SH_MEMORY_WRITE_STATUS_0|1"                */
/*                    Memory Write Status for CPU 0 & 1                 */
/* ==================================================================== */
#define SH_MEMORY_WRITE_STATUS_0                 0x0000000120070000UL
#define SH_MEMORY_WRITE_STATUS_1                 0x0000000120070080UL

/* ==================================================================== */
/*                   Register "SH_PIO_WRITE_STATUS_0|1"                 */
/*                      PIO Write Status for CPU 0 & 1                  */
/* ==================================================================== */
#ifndef __ASSEMBLY__
#define SH_PIO_WRITE_STATUS_0                    0x0000000120070200UL
#define SH_PIO_WRITE_STATUS_1                    0x0000000120070280UL

/*   SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK                               */
/*   Description:  Deadlock response detected                           */
#define SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_SHFT 1
#define SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_MASK 0x0000000000000002

/*   SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT                          */
/*   Description:  Count of currently pending PIO writes                */
#define SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_SHFT 56
#define SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK 0x3f00000000000000UL
#else
#define SH_PIO_WRITE_STATUS_0                    0x0000000120070200
#define SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_SHFT 56
#define SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_SHFT 1
#endif

/* ==================================================================== */
/*                Register "SH_PIO_WRITE_STATUS_0_ALIAS"                */
/* ==================================================================== */
#ifndef __ASSEMBLY__
#define SH_PIO_WRITE_STATUS_0_ALIAS              0x0000000120070208UL
#else
#define SH_PIO_WRITE_STATUS_0_ALIAS              0x0000000120070208
#endif

/* ==================================================================== */
/*                     Register "SH_EVENT_OCCURRED"                     */
/*                    SHub Interrupt Event Occurred                     */
/* ==================================================================== */
/*   SH_EVENT_OCCURRED_UART_INT                                         */
/*   Description:  Pending Junk Bus UART Interrupt                      */
#define SH_EVENT_OCCURRED_UART_INT_SHFT          20
#define SH_EVENT_OCCURRED_UART_INT_MASK          0x0000000000100000

/*   SH_EVENT_OCCURRED_IPI_INT                                          */
/*   Description:  Pending IPI Interrupt                                */
#define SH_EVENT_OCCURRED_IPI_INT_SHFT           28
#define SH_EVENT_OCCURRED_IPI_INT_MASK           0x0000000010000000

/*   SH_EVENT_OCCURRED_II_INT0                                          */
/*   Description:  Pending II 0 Interrupt                               */
#define SH_EVENT_OCCURRED_II_INT0_SHFT           29
#define SH_EVENT_OCCURRED_II_INT0_MASK           0x0000000020000000

/*   SH_EVENT_OCCURRED_II_INT1                                          */
/*   Description:  Pending II 1 Interrupt                               */
#define SH_EVENT_OCCURRED_II_INT1_SHFT           30
#define SH_EVENT_OCCURRED_II_INT1_MASK           0x0000000040000000

/* ==================================================================== */
/*                         Register "SH_PTC_0"                          */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH_PTC_0                                 0x00000001101a0000UL
#define SH_PTC_0_MASK                            0x80000000fffffffd
#define SH_PTC_0_INIT                            0x0000000000000000

/*   SH_PTC_0_A                                                         */
/*   Description:  Type                                                 */
#define SH_PTC_0_A_SHFT                          0
#define SH_PTC_0_A_MASK                          0x0000000000000001

/*   SH_PTC_0_PS                                                        */
/*   Description:  Page Size                                            */
#define SH_PTC_0_PS_SHFT                         2
#define SH_PTC_0_PS_MASK                         0x00000000000000fc

/*   SH_PTC_0_RID                                                       */
/*   Description:  Region ID                                            */
#define SH_PTC_0_RID_SHFT                        8
#define SH_PTC_0_RID_MASK                        0x00000000ffffff00

/*   SH_PTC_0_START                                                     */
/*   Description:  Start                                                */
#define SH_PTC_0_START_SHFT                      63
#define SH_PTC_0_START_MASK                      0x8000000000000000

/* ==================================================================== */
/*                         Register "SH_PTC_1"                          */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH_PTC_1                                 0x00000001101a0080UL
#define SH_PTC_1_MASK                            0x9ffffffffffff000
#define SH_PTC_1_INIT                            0x0000000000000000

/*   SH_PTC_1_VPN                                                       */
/*   Description:  Virtual page number                                  */
#define SH_PTC_1_VPN_SHFT                        12
#define SH_PTC_1_VPN_MASK                        0x1ffffffffffff000

/*   SH_PTC_1_START                                                     */
/*   Description:  PTC_1 Start                                          */
#define SH_PTC_1_START_SHFT                      63
#define SH_PTC_1_START_MASK                      0x8000000000000000

/*
 * Register definitions
 */

/* ==================================================================== */
/*                    Register "SH_RTC1_INT_CONFIG"                     */
/*                SHub RTC 1 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH_RTC1_INT_CONFIG                       0x0000000110001480
#define SH_RTC1_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC1_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC1_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC1_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC1_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC1_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC1_INT_CONFIG_AGT_SHFT              3
#define SH_RTC1_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC1_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC1_INT_CONFIG_PID_SHFT              4
#define SH_RTC1_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC1_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC1_INT_CONFIG_BASE_SHFT             21
#define SH_RTC1_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC1_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC1_INT_CONFIG_IDX_SHFT              52
#define SH_RTC1_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC1_INT_ENABLE"                     */
/*                SHub RTC 1 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH_RTC1_INT_ENABLE                       0x0000000110001500
#define SH_RTC1_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC1_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC1_INT_ENABLE_RTC1_ENABLE                                     */
/*   Description:  Enable RTC 1 Interrupt                               */
#define SH_RTC1_INT_ENABLE_RTC1_ENABLE_SHFT      0
#define SH_RTC1_INT_ENABLE_RTC1_ENABLE_MASK      0x0000000000000001

/* ==================================================================== */
/*                    Register "SH_RTC2_INT_CONFIG"                     */
/*                SHub RTC 2 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH_RTC2_INT_CONFIG                       0x0000000110001580
#define SH_RTC2_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC2_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC2_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC2_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC2_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC2_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC2_INT_CONFIG_AGT_SHFT              3
#define SH_RTC2_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC2_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC2_INT_CONFIG_PID_SHFT              4
#define SH_RTC2_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC2_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC2_INT_CONFIG_BASE_SHFT             21
#define SH_RTC2_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC2_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC2_INT_CONFIG_IDX_SHFT              52
#define SH_RTC2_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC2_INT_ENABLE"                     */
/*                SHub RTC 2 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH_RTC2_INT_ENABLE                       0x0000000110001600
#define SH_RTC2_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC2_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC2_INT_ENABLE_RTC2_ENABLE                                     */
/*   Description:  Enable RTC 2 Interrupt                               */
#define SH_RTC2_INT_ENABLE_RTC2_ENABLE_SHFT      0
#define SH_RTC2_INT_ENABLE_RTC2_ENABLE_MASK      0x0000000000000001

/* ==================================================================== */
/*                    Register "SH_RTC3_INT_CONFIG"                     */
/*                SHub RTC 3 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH_RTC3_INT_CONFIG                       0x0000000110001680
#define SH_RTC3_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC3_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC3_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC3_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC3_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC3_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC3_INT_CONFIG_AGT_SHFT              3
#define SH_RTC3_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC3_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC3_INT_CONFIG_PID_SHFT              4
#define SH_RTC3_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC3_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC3_INT_CONFIG_BASE_SHFT             21
#define SH_RTC3_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC3_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC3_INT_CONFIG_IDX_SHFT              52
#define SH_RTC3_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC3_INT_ENABLE"                     */
/*                SHub RTC 3 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH_RTC3_INT_ENABLE                       0x0000000110001700
#define SH_RTC3_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC3_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC3_INT_ENABLE_RTC3_ENABLE                                     */
/*   Description:  Enable RTC 3 Interrupt                               */
#define SH_RTC3_INT_ENABLE_RTC3_ENABLE_SHFT      0
#define SH_RTC3_INT_ENABLE_RTC3_ENABLE_MASK      0x0000000000000001

/*   SH_EVENT_OCCURRED_RTC1_INT                                         */
/*   Description:  Pending RTC 1 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC1_INT_SHFT          24
#define SH_EVENT_OCCURRED_RTC1_INT_MASK          0x0000000001000000

/*   SH_EVENT_OCCURRED_RTC2_INT                                         */
/*   Description:  Pending RTC 2 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC2_INT_SHFT          25
#define SH_EVENT_OCCURRED_RTC2_INT_MASK          0x0000000002000000

/*   SH_EVENT_OCCURRED_RTC3_INT                                         */
/*   Description:  Pending RTC 3 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC3_INT_SHFT          26
#define SH_EVENT_OCCURRED_RTC3_INT_MASK          0x0000000004000000

/* ==================================================================== */
/*                        Register "SH_INT_CMPB"                        */
/*                  RTC Compare Value for Processor B                   */
/* ==================================================================== */

#define SH_INT_CMPB                              0x00000001101b0080
#define SH_INT_CMPB_MASK                         0x007fffffffffffff
#define SH_INT_CMPB_INIT                         0x0000000000000000

/*   SH_INT_CMPB_REAL_TIME_CMPB                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPB_REAL_TIME_CMPB_SHFT          0
#define SH_INT_CMPB_REAL_TIME_CMPB_MASK          0x007fffffffffffff

/* ==================================================================== */
/*                        Register "SH_INT_CMPC"                        */
/*                  RTC Compare Value for Processor C                   */
/* ==================================================================== */

#define SH_INT_CMPC                              0x00000001101b0100
#define SH_INT_CMPC_MASK                         0x007fffffffffffff
#define SH_INT_CMPC_INIT                         0x0000000000000000

/*   SH_INT_CMPC_REAL_TIME_CMPC                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPC_REAL_TIME_CMPC_SHFT          0
#define SH_INT_CMPC_REAL_TIME_CMPC_MASK          0x007fffffffffffff

/* ==================================================================== */
/*                        Register "SH_INT_CMPD"                        */
/*                  RTC Compare Value for Processor D                   */
/* ==================================================================== */

#define SH_INT_CMPD                              0x00000001101b0180
#define SH_INT_CMPD_MASK                         0x007fffffffffffff
#define SH_INT_CMPD_INIT                         0x0000000000000000

/*   SH_INT_CMPD_REAL_TIME_CMPD                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPD_REAL_TIME_CMPD_SHFT          0
#define SH_INT_CMPD_REAL_TIME_CMPD_MASK          0x007fffffffffffff

#endif /* _ASM_IA64_SN_SHUB_MMR_H */
