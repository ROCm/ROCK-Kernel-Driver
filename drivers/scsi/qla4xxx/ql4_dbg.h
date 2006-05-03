/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *
 ****************************************************************************/

/*
 * Driver debug definitions.
 */
#define QLP1    0x00000002  // Unrecoverable error messages
#define QLP2    0x00000004  // Unexpected completion path error messages
#define QLP3    0x00000008  // Function trace messages
#define QLP4    0x00000010  // IOCTL trace messages
#define QLP5    0x00000020  // I/O & Request/Response queue trace messages
#define QLP6    0x00000040  // Watchdog messages (current state)
#define QLP7    0x00000080  // Initialization
#define QLP8    0x00000100  // Internal command queue traces
#define QLP9    0x00000200  // Unused
#define QLP10   0x00000400  // Extra Debug messages (dump buffers)
#define QLP11   0x00000800  // Mailbox & ISR Details
#define QLP12   0x00001000  // Enter/Leave routine messages
#define QLP13   0x00002000  // Display data for Inquiry, TUR, ReqSense, RptLuns
#define QLP14   0x00004000  // Temporary
#define QLP15   0x00008000  // Display jiffies for IOCTL calls
#define QLP16   0x00010000  // Extended proc print statements (srb info)
#define QLP17   0x00020000  // Display NVRAM Accesses
#define QLP18   0x00040000  // unused
#define QLP19	0x00080000  // PDU info
#define QLP20   0x00100000  // iSNS info
#define QLP24   0x01000000  // Scatter/Gather info

extern uint32_t ql_dbg_level;

/*
 *  Debug Print Routine Prototypes.
 */
#define QL4PRINT(m,x) do {if(((m) & ql_dbg_level) != 0) (x);} while(0);
#define ENTER(x) do {QL4PRINT(QLP12, printk("qla4xxx: Entering %s()\n", x));} while(0);
#define LEAVE(x) do {QL4PRINT(QLP12, printk("qla4xxx: Leaving  %s()\n", x));} while(0);

uint8_t qla4xxx_get_debug_level(uint32_t *dbg_level);
uint8_t qla4xxx_set_debug_level(uint32_t dbg_level);

void     qla4xxx_dump_bytes(uint32_t, void *, uint32_t);
void     qla4xxx_dump_words(uint32_t, void *, uint32_t);
void     qla4xxx_dump_dwords(uint32_t, void *, uint32_t);
void     qla4xxx_print_scsi_cmd(uint32_t dbg_mask, struct scsi_cmnd *cmd);
void     qla4xxx_print_srb_info(uint32_t dbg_mask, srb_t *srb);

/*
 * Driver debug definitions.
 */
/* #define QL_DEBUG_LEVEL_1  */	/* Output register accesses to COM1 */

/* #define QL_DEBUG_LEVEL_3  */	/* Output function trace msgs to COM1 */
/* #define QL_DEBUG_LEVEL_4  */	
/* #define QL_DEBUG_LEVEL_5  */	
/* #define QL_DEBUG_LEVEL_9  */	

 #define QL_DEBUG_LEVEL_2   /* Output error msgs to COM1 */

#define DEBUG(x)	do {} while (0);

#if defined(QL_DEBUG_LEVEL_2)
#define DEBUG2(x)      do {if(extended_error_logging == 2) x;} while (0);
#define DEBUG2_3(x)   do {x;} while (0);
#else
#define DEBUG2(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)	do {x;} while (0);
#else
#define DEBUG3(x)	do {} while (0);
  #if !defined(QL_DEBUG_LEVEL_2)
  #define DEBUG2_3(x)	do {} while (0);
  #endif
#endif
#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)	do {x;} while (0);
#else
#define DEBUG4(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)	do {x;} while (0);
#else
#define DEBUG5(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_9)
#define DEBUG9(x)	do {x;} while (0);
#else
#define DEBUG9(x)	do {} while (0);
#endif

void     __dump_dwords(void *, uint32_t);
void     __dump_words(void *, uint32_t);
void     __dump_mailbox_registers(uint32_t, scsi_qla_host_t *ha);
void     __dump_registers(uint32_t, scsi_qla_host_t *ha);
void     qla4xxx_dump_registers(uint32_t, scsi_qla_host_t *ha);

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

