/* $Id: serial.c,v 1.17 2003/07/04 08:27:37 starvik Exp $
 *
 * Serial port driver for the ETRAX 100LX chip
 *
 *    Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003  Axis Communications AB
 *
 *    Many, many authors. Based once upon a time on serial.c for 16x50.
 *
 * $Log: serial.c,v $
 * Revision 1.17  2003/07/04 08:27:37  starvik
 * Merge of Linux 2.5.74
 *
 * Revision 1.16  2003/06/13 10:05:19  johana
 * Help the user to avoid trouble by:
 * Forcing mixed mode for status/control lines if not all pins are used.
 *
 * Revision 1.15  2003/06/13 09:43:01  johana
 * Merged in the following changes from os/linux/arch/cris/drivers/serial.c
 * + some minor changes to reduce diff.
 *
 * Revision 1.49  2003/05/30 11:31:54  johana
 * Merged in change-branch--serial9bit that adds CMSPAR support for sticky
 * parity (mark/space)
 *
 * Revision 1.48  2003/05/30 11:03:57  johana
 * Implemented rs_send_xchar() by disabling the DMA and writing manually.
 * Added e100_disable_txdma_channel() and e100_enable_txdma_channel().
 * Fixed rs_throttle() and rs_unthrottle() to properly call rs_send_xchar
 * instead of setting info->x_char and check the CRTSCTS flag before
 * controlling the rts pin.
 *
 * Revision 1.14  2003/04/09 08:12:44  pkj
 * Corrected typo changes made upstream.
 *
 * Revision 1.13  2003/04/09 05:20:47  starvik
 * Merge of Linux 2.5.67
 *
 * Revision 1.11  2003/01/22 06:48:37  starvik
 * Fixed warnings issued by GCC 3.2.1
 *
 * Revision 1.9  2002/12/13 09:07:47  starvik
 * Alert user that RX_TIMEOUT_TICKS==0 doesn't work
 *
 * Revision 1.8  2002/12/11 13:13:57  starvik
 * Added arch/ to v10 specific includes
 * Added fix from Linux 2.4 in serial.c (flush_to_flip_buffer)
 *
 * Revision 1.7  2002/12/06 07:13:57  starvik
 * Corrected work queue stuff
 * Removed CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST
 *
 * Revision 1.6  2002/11/21 07:17:46  starvik
 * Change static inline to extern inline where otherwise outlined with gcc-3.2
 *
 * Revision 1.5  2002/11/14 15:59:49  starvik
 * Linux 2.5 port of the latest serial driver from 2.4. The work queue stuff
 * probably doesn't work yet.
 *
 * Revision 1.42  2002/11/05 09:08:47  johana
 * Better implementation of rs_stop() and rs_start() that uses the XOFF
 * register to start/stop transmission.
 * change_speed() also initilises XOFF register correctly so that
 * auto_xoff is enabled when IXON flag is set by user.
 * This gives fast XOFF response times.
 *
 * Revision 1.41  2002/11/04 18:40:57  johana
 * Implemented rs_stop() and rs_start().
 * Simple tests using hwtestserial indicates that this should be enough
 * to make it work.
 *
 * Revision 1.40  2002/10/14 05:33:18  starvik
 * RS-485 uses fast timers even if SERIAL_FAST_TIMER is disabled
 *
 * Revision 1.39  2002/09/30 21:00:57  johana
 * Support for CONFIG_ETRAX_SERx_DTR_RI_DSR_CD_MIXED where the status and
 * control pins can be mixed between PA and PB.
 * If no serial port uses MIXED old solution is used
 * (saves a few bytes and cycles).
 * control_pins struct uses masks instead of bit numbers.
 * Corrected dummy values and polarity in line_info() so
 * /proc/tty/driver/serial is now correct.
 * (the E100_xxx_GET() macros is really active low - perhaps not obvious)
 *
 * Revision 1.38  2002/08/23 11:01:36  starvik
 * Check that serial port is enabled in all interrupt handlers to avoid
 * restarts of DMA channels not assigned to serial ports
 *
 * Revision 1.37  2002/08/13 13:02:37  bjornw
 * Removed some warnings because of unused code
 *
 * Revision 1.36  2002/08/08 12:50:01  starvik
 * Serial interrupt is shared with synchronous serial port driver
 *
 * Revision 1.35  2002/06/03 10:40:49  starvik
 * Increased RS-485 RTS toggle timer to 2 characters
 *
 * Revision 1.34  2002/05/28 18:59:36  johana
 * Whitespace and comment fixing to be more like etrax100ser.c 1.71.
 *
 * Revision 1.33  2002/05/28 17:55:43  johana
 * RS-485 uses FAST_TIMER if enabled, and starts a short (one char time)
 * timer from tranismit_chars (interrupt context).
 * The timer toggles RTS in interrupt context when expired giving minimum
 * latencies.
 *
 * Revision 1.32  2002/05/22 13:58:00  johana
 * Renamed rs_write() to raw_write() and made it inline.
 * New rs_write() handles RS-485 if configured and enabled
 * (moved code from e100_write_rs485()).
 * RS-485 ioctl's uses copy_from_user() instead of verify_area().
 *
 * Revision 1.31  2002/04/22 11:20:03  johana
 * Updated copyright years.
 *
 * Revision 1.30  2002/04/22 09:39:12  johana
 * RS-485 support compiles.
 *
 * Revision 1.29  2002/01/14 16:10:01  pkj
 * Allocate the receive buffers dynamically. The static 4kB buffer was
 * too small for the peaks. This means that we can get rid of the extra
 * buffer and the copying to it. It also means we require less memory
 * under normal operations, but can use more when needed (there is a
 * cap at 64kB for safety reasons). If there is no memory available
 * we panic(), and die a horrible death...
 *
 * Revision 1.28  2001/12/18 15:04:53  johana
 * Cleaned up write_rs485() - now it works correctly without padding extra
 * char.
 * Added sane default initialisation of rs485.
 * Added #ifdef around dummy variables.
 *
 * Revision 1.27  2001/11/29 17:00:41  pkj
 * 2kB seems to be too small a buffer when using 921600 bps,
 * so increase it to 4kB (this was already done for the elinux
 * version of the serial driver).
 *
 * Revision 1.26  2001/11/19 14:20:41  pkj
 * Minor changes to comments and unused code.
 *
 * Revision 1.25  2001/11/12 20:03:43  pkj
 * Fixed compiler warnings.
 *
 * Revision 1.24  2001/11/12 15:10:05  pkj
 * Total redesign of the receiving part of the serial driver.
 * Uses eight chained descriptors to write to a 4kB buffer.
 * This data is then serialised into a 2kB buffer. From there it
 * is copied into the TTY's flip buffers when they become available.
 * A lot of copying, and the sizes of the buffers might need to be
 * tweaked, but all in all it should work better than the previous
 * version, without the need to modify the TTY code in any way.
 * Also note that erroneous bytes are now correctly marked in the
 * flag buffers (instead of always marking the first byte).
 *
 * Revision 1.23  2001/10/30 17:53:26  pkj
 * * Set info->uses_dma to 0 when a port is closed.
 * * Mark the timer1 interrupt as a fast one (SA_INTERRUPT).
 * * Call start_flush_timer() in start_receive() if
 *   CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST is defined.
 *
 * Revision 1.22  2001/10/30 17:44:03  pkj
 * Use %lu for received and transmitted counters in line_info().
 *
 * Revision 1.21  2001/10/30 17:40:34  pkj
 * Clean-up. The only change to functionality is that
 * CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS(=5) is used instead of
 * MAX_FLUSH_TIME(=8).
 *
 * Revision 1.20  2001/10/30 15:24:49  johana
 * Added char_time stuff from 2.0 driver.
 *
 * Revision 1.19  2001/10/30 15:23:03  johana
 * Merged with 1.13.2 branch + fixed indentation
 * and changed CONFIG_ETRAX100_XYS to CONFIG_ETRAX_XYZ
 *
 * Revision 1.18  2001/09/24 09:27:22  pkj
 * Completed ext_baud_table[] in cflag_to_baud() and cflag_to_etrax_baud().
 *
 * Revision 1.17  2001/08/24 11:32:49  ronny
 * More fixes for the CONFIG_ETRAX_SERIAL_PORT0 define.
 *
 * Revision 1.16  2001/08/24 07:56:22  ronny
 * Added config ifdefs around ser0 irq requests.
 *
 * Revision 1.15  2001/08/16 09:10:31  bjarne
 * serial.c - corrected the initialization of rs_table, the wrong defines
 *            where used.
 *            Corrected a test in timed_flush_handler.
 *            Changed configured to enabled.
 * serial.h - Changed configured to enabled.
 *
 * Revision 1.14  2001/08/15 07:31:23  bjarne
 * Introduced two new members to the e100_serial struct.
 * configured - Will be set to 1 if the port has been configured in .config
 * uses_dma   - Should be set to 1 if the port uses DMA. Currently it is set 
 *              to 1
 *              when a port is opened. This is used to limit the DMA interrupt
 *              routines to only manipulate DMA channels actually used by the
 *              serial driver.
 *
 * Revision 1.13.2.2  2001/10/17 13:57:13  starvik
 * Receiver was broken by the break fixes
 *
 * Revision 1.13.2.1  2001/07/20 13:57:39  ronny
 * Merge with new stuff from etrax100ser.c. Works but haven't checked stuff
 * like break handling.
 *
 * Revision 1.13  2001/05/09 12:40:31  johana
 * Use DMA_NBR and IRQ_NBR defines from dma.h and irq.h
 *
 * Revision 1.12  2001/04/19 12:23:07  bjornw
 * CONFIG_RS485 -> CONFIG_ETRAX_RS485
 *
 * Revision 1.11  2001/04/05 14:29:48  markusl
 * Updated according to review remarks i.e.
 * -Use correct types in port structure to avoid compiler warnings
 * -Try to use IO_* macros whenever possible
 * -Open should never return -EBUSY
 *
 * Revision 1.10  2001/03/05 13:14:07  bjornw
 * Another spelling fix
 *
 * Revision 1.9  2001/02/23 13:46:38  bjornw
 * Spellling check
 *
 * Revision 1.8  2001/01/23 14:56:35  markusl
 * Made use of ser1 optional
 * Needed by USB
 *
 * Revision 1.7  2001/01/19 16:14:48  perf
 * Added kernel options for serial ports 234.
 * Changed option names from CONFIG_ETRAX100_XYZ to CONFIG_ETRAX_XYZ.
 *
 * Revision 1.6  2000/11/22 16:36:09  bjornw
 * Please marketing by using the correct case when spelling Etrax.
 *
 * Revision 1.5  2000/11/21 16:43:37  bjornw
 * Fixed so it compiles under CONFIG_SVINTO_SIM
 *
 * Revision 1.4  2000/11/15 17:34:12  bjornw
 * Added a timeout timer for flushing input channels. The interrupt-based
 * fast flush system should be easy to merge with this later (works the same
 * way, only with an irq instead of a system timer_list)
 *
 * Revision 1.3  2000/11/13 17:19:57  bjornw
 * * Incredibly, this almost complete rewrite of serial.c worked (at least
 *   for output) the first time.
 *
 *   Items worth noticing:
 *
 *      No Etrax100 port 1 workarounds (does only compile on 2.4 anyway now)
 *      RS485 is not ported (why can't it be done in userspace as on x86 ?)
 *      Statistics done through async_icount - if any more stats are needed,
 *      that's the place to put them or in an arch-dep version of it.
 *      timeout_interrupt and the other fast timeout stuff not ported yet
 *      There be dragons in this 3k+ line driver
 *
 * Revision 1.2  2000/11/10 16:50:28  bjornw
 * First shot at a 2.4 port, does not compile totally yet
 *
 * Revision 1.1  2000/11/10 16:47:32  bjornw
 * Added verbatim copy of rev 1.49 etrax100ser.c from elinux
 *
 * Revision 1.49  2000/10/30 15:47:14  tobiasa
 * Changed version number.
 *
 * Revision 1.48  2000/10/25 11:02:43  johana
 * Changed %ul to %lu in printf's
 *
 * Revision 1.47  2000/10/18 15:06:53  pkj
 * Compile correctly with CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST and
 * CONFIG_ETRAX_SERIAL_PROC_ENTRY together.
 * Some clean-up of the /proc/serial file.
 *
 * Revision 1.46  2000/10/16 12:59:40  johana
 * Added CONFIG_ETRAX_SERIAL_PROC_ENTRY for statistics and debug info.
 *
 * Revision 1.45  2000/10/13 17:10:59  pkj
 * Do not flush DMAs while flipping TTY buffers.
 *
 * Revision 1.44  2000/10/13 16:34:29  pkj
 * Added a delay in ser_interrupt() for 2.3ms when an error is detected.
 * We do not know why this delay is required yet, but without it the
 * irmaflash program does not work (this was the program that needed
 * the ser_interrupt() to be needed in the first place). This should not
 * affect normal use of the serial ports.
 *
 * Revision 1.43  2000/10/13 16:30:44  pkj
 * New version of the fast flush of serial buffers code. This time
 * it is localized to the serial driver and uses a fast timer to
 * do the work.
 *
 * Revision 1.42  2000/10/13 14:54:26  bennyo
 * Fix for switching RTS when using rs485
 *
 * Revision 1.41  2000/10/12 11:43:44  pkj
 * Cleaned up a number of comments.
 *
 * Revision 1.40  2000/10/10 11:58:39  johana
 * Made RS485 support generic for all ports.
 * Toggle rts in interrupt if no delay wanted.
 * WARNING: No true transmitter empty check??
 * Set d_wait bit when sending data so interrupt is delayed until
 * fifo flushed. (Fix tcdrain() problem)
 *
 * Revision 1.39  2000/10/04 16:08:02  bjornw
 * * Use virt_to_phys etc. for DMA addresses
 * * Removed CONFIG_FLUSH_DMA_FAST hacks
 * * Indentation fix
 *
 * Revision 1.38  2000/10/02 12:27:10  mattias
 * * added variable used when using fast flush on serial dma.
 *   (CONFIG_FLUSH_DMA_FAST)
 *
 * Revision 1.37  2000/09/27 09:44:24  pkj
 * Uncomment definition of SERIAL_HANDLE_EARLY_ERRORS.
 *
 * Revision 1.36  2000/09/20 13:12:52  johana
 * Support for CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS:
 *   Number of timer ticks between flush of receive fifo (1 tick = 10ms).
 *   Try 0-3 for low latency applications. Approx 5 for high load
 *   applications (e.g. PPP). Maybe this should be more adaptive some day...
 *
 * Revision 1.35  2000/09/20 10:36:08  johana
 * Typo in get_lsr_info()
 *
 * Revision 1.34  2000/09/20 10:29:59  johana
 * Let rs_chars_in_buffer() check fifo content as well.
 * get_lsr_info() might work now (not tested).
 * Easier to change the port to debug.
 *
 * Revision 1.33  2000/09/13 07:52:11  torbjore
 * Support RS485
 *
 * Revision 1.32  2000/08/31 14:45:37  bjornw
 * After sending a break we need to reset the transmit DMA channel
 *
 * Revision 1.31  2000/06/21 12:13:29  johana
 * Fixed wait for all chars sent when closing port.
 * (Used to always take 1 second!)
 * Added shadows for directions of status/ctrl signals.
 *
 * Revision 1.30  2000/05/29 16:27:55  bjornw
 * Simulator ifdef moved a bit
 *
 * Revision 1.29  2000/05/09 09:40:30  mattias
 * * Added description of dma registers used in timeout_interrupt
 * * Removed old code
 *
 * Revision 1.28  2000/05/08 16:38:58  mattias
 * * Bugfix for flushing fifo in timeout_interrupt
 *   Problem occurs when bluetooth stack waits for a small number of bytes
 *   containing an event acknowledging free buffers in bluetooth HW
 *   As before, data was stuck in fifo until more data came on uart and
 *   flushed it up to the stack.
 *
 * Revision 1.27  2000/05/02 09:52:28  jonasd
 * Added fix for peculiar etrax behaviour when eop is forced on an empty
 * fifo. This is used when flashing the IRMA chip. Disabled by default.
 *
 * Revision 1.26  2000/03/29 15:32:02  bjornw
 * 2.0.34 updates
 *
 * Revision 1.25  2000/02/16 16:59:36  bjornw
 * * Receive DMA directly into the flip-buffer, eliminating an intermediary
 *   receive buffer and a memcpy. Will avoid some overruns.
 * * Error message on debug port if an overrun or flip buffer overrun occurs.
 * * Just use the first byte in the flag flip buffer for errors.
 * * Check for timeout on the serial ports only each 5/100 s, not 1/100.
 *
 * Revision 1.24  2000/02/09 18:02:28  bjornw
 * * Clear serial errors (overrun, framing, parity) correctly. Before, the
 *   receiver would get stuck if an error occurred and we did not restart
 *   the input DMA.
 * * Cosmetics (indentation, some code made into inlines)
 * * Some more debug options
 * * Actually shut down the serial port (DMA irq, DMA reset, receiver stop)
 *   when the last open is closed. Corresponding fixes in startup().
 * * rs_close() "tx FIFO wait" code moved into right place, bug & -> && fixed
 *   and make a special case out of port 1 (R_DMA_CHx_STATUS is broken for that)
 * * e100_disable_rx/enable_rx just disables/enables the receiver, not RTS
 *
 * Revision 1.23  2000/01/24 17:46:19  johana
 * Wait for flush of DMA/FIFO when closing port.
 *
 * Revision 1.22  2000/01/20 18:10:23  johana
 * Added TIOCMGET ioctl to return modem status.
 * Implemented modem status/control that works with the extra signals
 * (DTR, DSR, RI,CD) as well.
 * 3 different modes supported:
 * ser0 on PB (Bundy), ser1 on PB (Lisa) and ser2 on PA (Bundy)
 * Fixed DEF_TX value that caused the serial transmitter pin (txd) to go to 0 when
 * closing the last filehandle, NASTY!.
 * Added break generation, not tested though!
 * Use SA_SHIRQ when request_irq() for ser2 and ser3 (shared with) par0 and par1.
 * You can't use them at the same time (yet..), but you can hopefully switch
 * between ser2/par0, ser3/par1 with the same kernel config.
 * Replaced some magic constants with defines
 *
 *
 */

static char *serial_version = "$Revision: 1.17 $";

#include <linux/config.h>
#include <linux/version.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <linux/delay.h>

#include <asm/arch/svinto.h>

/* non-arch dependent serial structures are in linux/serial.h */
#include <linux/serial.h>
/* while we keep our own stuff (struct e100_serial) in a local .h file */
#include "serial.h"
#include <asm/fasttimer.h>

#ifdef CONFIG_ETRAX_SERIAL_FAST_TIMER
#ifndef CONFIG_ETRAX_FAST_TIMER
#error "Enable FAST_TIMER to use SERIAL_FAST_TIMER"
#endif
#endif

#if defined(CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS) && \
           (CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS == 0)
#error "RX_TIMEOUT_TICKS == 0 not allowed, use 1"
#endif

/*
 * All of the compatibilty code so we can compile serial.c against
 * older kernels is hidden in serial_compat.h
 */
#if defined(LOCAL_HEADERS)
#include "serial_compat.h"
#endif

#define _INLINE_ inline

struct tty_driver *serial_driver;

/* serial subtype definitions */
#ifndef SERIAL_TYPE_NORMAL
#define SERIAL_TYPE_NORMAL	1
#endif

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

//#define SERIAL_DEBUG_INTR
//#define SERIAL_DEBUG_OPEN 
//#define SERIAL_DEBUG_FLOW
//#define SERIAL_DEBUG_DATA
//#define SERIAL_DEBUG_THROTTLE
//#define SERIAL_DEBUG_IO  /* Debug for Extra control and status pins */
#define SERIAL_DEBUG_LINE 0 /* What serport we want to debug */

/* Enable this to use serial interrupts to handle when you
   expect the first received event on the serial port to
   be an error, break or similar. Used to be able to flash IRMA
   from eLinux */
#define SERIAL_HANDLE_EARLY_ERRORS

#define TTY_THROTTLE_LIMIT (TTY_FLIPBUF_SIZE/10)

#define SERIAL_DESCR_BUF_SIZE 256

/* Add an x here to log a lot of timer stuff */
#define TIMERD(x)

#define DEBUG_LOG(line, string, value)

#ifndef CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS
/* Default number of timer ticks before flushing rx fifo 
 * When using "little data, low latency applications: use 0
 * When using "much data applications (PPP)" use ~5
 */
#define CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS 5 
#endif

static void change_speed(struct e100_serial *info);
static void rs_wait_until_sent(struct tty_struct *tty, int timeout);
static int rs_write(struct tty_struct * tty, int from_user,
                    const unsigned char *buf, int count);
static inline int raw_write(struct tty_struct * tty, int from_user,
                            const unsigned char *buf, int count);
#ifdef CONFIG_ETRAX_RS485
static int e100_write_rs485(struct tty_struct * tty, int from_user,
                            const unsigned char *buf, int count);
#endif
static int get_lsr_info(struct e100_serial * info, unsigned int *value);


#define DEF_BAUD 0x99   /* 115.2 kbit/s */
#define STD_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)
#define DEF_RX 0x20  /* or SERIAL_CTRL_W >> 8 */
/* Default value of tx_ctrl register: has txd(bit 7)=1 (idle) as default */
#define DEF_TX 0x80  /* or SERIAL_CTRL_B */

/* offsets from R_SERIALx_CTRL */

#define REG_DATA 0
#define REG_TR_DATA 0
#define REG_STATUS 1
#define REG_TR_CTRL 1
#define REG_REC_CTRL 2
#define REG_BAUD 3
#define REG_XOFF 4  /* this is a 32 bit register */

/* The bitfields are the same for all serial ports */
#define SER_RXD_MASK         IO_MASK(R_SERIAL0_STATUS, rxd)
#define SER_DATA_AVAIL_MASK  IO_MASK(R_SERIAL0_STATUS, data_avail)
#define SER_FRAMING_ERR_MASK IO_MASK(R_SERIAL0_STATUS, framing_err)
#define SER_PAR_ERR_MASK     IO_MASK(R_SERIAL0_STATUS, par_err)
#define SER_OVERRUN_MASK     IO_MASK(R_SERIAL0_STATUS, overrun)

#define SER_ERROR_MASK (SER_OVERRUN_MASK | SER_PAR_ERR_MASK | SER_FRAMING_ERR_MASK)

/* Values for info->errorcode */
#define ERRCODE_SET_BREAK    (TTY_BREAK)
#define ERRCODE_INSERT        0x100
#define ERRCODE_INSERT_BREAK (ERRCODE_INSERT | TTY_BREAK)

#define FORCE_EOP(info)  *R_SET_EOP = 1U << info->iseteop;

/*
 * General note regarding the use of IO_* macros in this file: 
 *
 * We will use the bits defined for DMA channel 6 when using various
 * IO_* macros (e.g. IO_STATE, IO_MASK, IO_EXTRACT) and _assume_ they are
 * the same for all channels (which of course they are).
 *
 * We will also use the bits defined for serial port 0 when writing commands
 * to the different ports, as these bits too are the same for all ports.
 */


/* this is the data for the four serial ports in the etrax100 */
/*  DMA2(ser2), DMA4(ser3), DMA6(ser0) or DMA8(ser1) */
/* R_DMA_CHx_CLR_INTR, R_DMA_CHx_FIRST, R_DMA_CHx_CMD */

static struct e100_serial rs_table[] = {
	{ DEF_BAUD, (unsigned char *)R_SERIAL0_CTRL, 1U << 12, /* uses DMA 6 and 7 */
	  R_DMA_CH6_CLR_INTR, R_DMA_CH6_FIRST, R_DMA_CH6_CMD,
	  R_DMA_CH6_STATUS, R_DMA_CH6_HWSW, R_DMA_CH6_DESCR,
	  R_DMA_CH7_CLR_INTR, R_DMA_CH7_FIRST, R_DMA_CH7_CMD,
	  R_DMA_CH7_STATUS, R_DMA_CH7_HWSW, R_DMA_CH7_DESCR,
	  STD_FLAGS, DEF_RX, DEF_TX, 2,
#ifdef CONFIG_ETRAX_SERIAL_PORT0
          1
#else
          0
#endif
},  /* ttyS0 */
#ifndef CONFIG_SVINTO_SIM
	{ DEF_BAUD, (unsigned char *)R_SERIAL1_CTRL, 1U << 16, /* uses DMA 8 and 9 */
	  R_DMA_CH8_CLR_INTR, R_DMA_CH8_FIRST, R_DMA_CH8_CMD,
	  R_DMA_CH8_STATUS, R_DMA_CH8_HWSW, R_DMA_CH8_DESCR,
	  R_DMA_CH9_CLR_INTR, R_DMA_CH9_FIRST, R_DMA_CH9_CMD,
	  R_DMA_CH9_STATUS, R_DMA_CH9_HWSW, R_DMA_CH9_DESCR,
	  STD_FLAGS, DEF_RX, DEF_TX, 3 ,
#ifdef CONFIG_ETRAX_SERIAL_PORT1
          1
#else
          0
#endif
},  /* ttyS1 */

	{ DEF_BAUD, (unsigned char *)R_SERIAL2_CTRL, 1U << 4,  /* uses DMA 2 and 3 */
	  R_DMA_CH2_CLR_INTR, R_DMA_CH2_FIRST, R_DMA_CH2_CMD,
	  R_DMA_CH2_STATUS, R_DMA_CH2_HWSW, R_DMA_CH2_DESCR,
	  R_DMA_CH3_CLR_INTR, R_DMA_CH3_FIRST, R_DMA_CH3_CMD,
	  R_DMA_CH3_STATUS, R_DMA_CH3_HWSW, R_DMA_CH3_DESCR,
	  STD_FLAGS, DEF_RX, DEF_TX, 0,
#ifdef CONFIG_ETRAX_SERIAL_PORT2
          1
#else
          0
#endif
 },  /* ttyS2 */

	{ DEF_BAUD, (unsigned char *)R_SERIAL3_CTRL, 1U << 8,  /* uses DMA 4 and 5 */
	  R_DMA_CH4_CLR_INTR, R_DMA_CH4_FIRST, R_DMA_CH4_CMD,
	  R_DMA_CH4_STATUS, R_DMA_CH4_HWSW, R_DMA_CH4_DESCR,
	  R_DMA_CH5_CLR_INTR, R_DMA_CH5_FIRST, R_DMA_CH5_CMD,
	  R_DMA_CH5_STATUS, R_DMA_CH5_HWSW, R_DMA_CH5_DESCR,
	  STD_FLAGS, DEF_RX, DEF_TX, 1,
#ifdef CONFIG_ETRAX_SERIAL_PORT3
          1
#else
          0
#endif
 }   /* ttyS3 */
#endif
};


#define NR_PORTS (sizeof(rs_table)/sizeof(struct e100_serial))
#ifdef CONFIG_ETRAX_SERIAL_FAST_TIMER
static struct fast_timer fast_timers[NR_PORTS];
#endif

#ifdef CONFIG_ETRAX_SERIAL_PROC_ENTRY
#define PROCSTAT(x) x
struct ser_statistics_type {
	int overrun_cnt;
	int early_errors_cnt;
	int ser_ints_ok_cnt;
	int errors_cnt;
	unsigned long int processing_flip;
	unsigned long processing_flip_still_room;
	unsigned long int timeout_flush_cnt;
	int rx_dma_ints;
	int tx_dma_ints;
	int rx_tot;
	int tx_tot;
};

static struct ser_statistics_type ser_stat[NR_PORTS];

#else

#define PROCSTAT(x)

#endif /* CONFIG_ETRAX_SERIAL_PROC_ENTRY */

/* RS-485 */
#if defined(CONFIG_ETRAX_RS485)
#ifdef CONFIG_ETRAX_FAST_TIMER
static struct fast_timer fast_timers_rs485[NR_PORTS];
#endif
#if defined(CONFIG_ETRAX_RS485_ON_PA)
static int rs485_pa_bit = CONFIG_ETRAX_RS485_ON_PA_BIT;
#endif
#endif

/* Info and macros needed for each ports extra control/status signals. */
#define E100_STRUCT_PORT(line, pinname) \
 ((CONFIG_ETRAX_SER##line##_##pinname##_ON_PA_BIT >= 0)? \
		(R_PORT_PA_DATA): ( \
 (CONFIG_ETRAX_SER##line##_##pinname##_ON_PB_BIT >= 0)? \
		(R_PORT_PB_DATA):&dummy_ser[line]))

#define E100_STRUCT_SHADOW(line, pinname) \
 ((CONFIG_ETRAX_SER##line##_##pinname##_ON_PA_BIT >= 0)? \
		(&port_pa_data_shadow): ( \
 (CONFIG_ETRAX_SER##line##_##pinname##_ON_PB_BIT >= 0)? \
		(&port_pb_data_shadow):&dummy_ser[line]))
#define E100_STRUCT_MASK(line, pinname) \
 ((CONFIG_ETRAX_SER##line##_##pinname##_ON_PA_BIT >= 0)? \
		(1<<CONFIG_ETRAX_SER##line##_##pinname##_ON_PA_BIT): ( \
 (CONFIG_ETRAX_SER##line##_##pinname##_ON_PB_BIT >= 0)? \
		(1<<CONFIG_ETRAX_SER##line##_##pinname##_ON_PB_BIT):DUMMY_##pinname##_MASK))

#define DUMMY_DTR_MASK 1
#define DUMMY_RI_MASK  2
#define DUMMY_DSR_MASK 4
#define DUMMY_CD_MASK  8
static unsigned char dummy_ser[NR_PORTS] = {0xFF, 0xFF, 0xFF,0xFF};

/* If not all status pins are used or disabled, use mixed mode */
#ifdef CONFIG_ETRAX_SERIAL_PORT0

#define SER0_PA_BITSUM (CONFIG_ETRAX_SER0_DTR_ON_PA_BIT+CONFIG_ETRAX_SER0_RI_ON_PA_BIT+CONFIG_ETRAX_SER0_DSR_ON_PA_BIT+CONFIG_ETRAX_SER0_CD_ON_PA_BIT)

#if SER0_PA_BITSUM != -4
#  if CONFIG_ETRAX_SER0_DTR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER0_RI_ON_PA_BIT == -1
#   ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER0_DSR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER0_CD_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#define SER0_PB_BITSUM (CONFIG_ETRAX_SER0_DTR_ON_PB_BIT+CONFIG_ETRAX_SER0_RI_ON_PB_BIT+CONFIG_ETRAX_SER0_DSR_ON_PB_BIT+CONFIG_ETRAX_SER0_CD_ON_PB_BIT)

#if SER0_PB_BITSUM != -4
#  if CONFIG_ETRAX_SER0_DTR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER0_RI_ON_PB_BIT == -1
#   ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER0_DSR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER0_CD_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#endif /* PORT0 */


#ifdef CONFIG_ETRAX_SERIAL_PORT1

#define SER1_PA_BITSUM (CONFIG_ETRAX_SER1_DTR_ON_PA_BIT+CONFIG_ETRAX_SER1_RI_ON_PA_BIT+CONFIG_ETRAX_SER1_DSR_ON_PA_BIT+CONFIG_ETRAX_SER1_CD_ON_PA_BIT)

#if SER1_PA_BITSUM != -4
#  if CONFIG_ETRAX_SER1_DTR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER1_RI_ON_PA_BIT == -1
#   ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER1_DSR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER1_CD_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#define SER0_PB_BITSUM (CONFIG_ETRAX_SER1_DTR_ON_PB_BIT+CONFIG_ETRAX_SER1_RI_ON_PB_BIT+CONFIG_ETRAX_SER1_DSR_ON_PB_BIT+CONFIG_ETRAX_SER1_CD_ON_PB_BIT)

#if SER1_PB_BITSUM != -4
#  if CONFIG_ETRAX_SER1_DTR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER1_RI_ON_PB_BIT == -1
#   ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER1_DSR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER1_CD_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#endif /* PORT1 */

#ifdef CONFIG_ETRAX_SERIAL_PORT2

#define SER2_PA_BITSUM (CONFIG_ETRAX_SER2_DTR_ON_PA_BIT+CONFIG_ETRAX_SER2_RI_ON_PA_BIT+CONFIG_ETRAX_SER2_DSR_ON_PA_BIT+CONFIG_ETRAX_SER2_CD_ON_PA_BIT)

#if SER2_PA_BITSUM != -4
#  if CONFIG_ETRAX_SER2_DTR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER2_RI_ON_PA_BIT == -1
#   ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER2_DSR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER2_CD_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#define SER2_PB_BITSUM (CONFIG_ETRAX_SER2_DTR_ON_PB_BIT+CONFIG_ETRAX_SER2_RI_ON_PB_BIT+CONFIG_ETRAX_SER2_DSR_ON_PB_BIT+CONFIG_ETRAX_SER2_CD_ON_PB_BIT)

#if SER2_PB_BITSUM != -4
#  if CONFIG_ETRAX_SER2_DTR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER2_RI_ON_PB_BIT == -1
#   ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER2_DSR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER2_CD_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#endif /* PORT2 */

#ifdef CONFIG_ETRAX_SERIAL_PORT3

#define SER3_PA_BITSUM (CONFIG_ETRAX_SER3_DTR_ON_PA_BIT+CONFIG_ETRAX_SER3_RI_ON_PA_BIT+CONFIG_ETRAX_SER3_DSR_ON_PA_BIT+CONFIG_ETRAX_SER3_CD_ON_PA_BIT)

#if SER3_PA_BITSUM != -4
#  if CONFIG_ETRAX_SER3_DTR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER3_RI_ON_PA_BIT == -1
#   ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER3_DSR_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER3_CD_ON_PA_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#define SER3_PB_BITSUM (CONFIG_ETRAX_SER3_DTR_ON_PB_BIT+CONFIG_ETRAX_SER3_RI_ON_PB_BIT+CONFIG_ETRAX_SER3_DSR_ON_PB_BIT+CONFIG_ETRAX_SER3_CD_ON_PB_BIT)

#if SER3_PB_BITSUM != -4
#  if CONFIG_ETRAX_SER3_DTR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#   endif
# if CONFIG_ETRAX_SER3_RI_ON_PB_BIT == -1
#   ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#     define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#   endif
#  endif
#  if CONFIG_ETRAX_SER3_DSR_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#  if CONFIG_ETRAX_SER3_CD_ON_PB_BIT == -1
#    ifndef CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED
#      define CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED 1
#    endif
#  endif
#endif

#endif /* PORT3 */


#if defined(CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_MIXED) || \
    defined(CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_MIXED) || \
    defined(CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_MIXED) || \
    defined(CONFIG_ETRAX_SER3_DTR_RI_DSR_CD_MIXED)
#define CONFIG_ETRAX_SERX_DTR_RI_DSR_CD_MIXED
#endif

#ifdef CONFIG_ETRAX_SERX_DTR_RI_DSR_CD_MIXED
/* The pins can be mixed on PA and PB */
#define CONTROL_PINS_PORT_NOT_USED(line) \
  &dummy_ser[line], &dummy_ser[line], \
  &dummy_ser[line], &dummy_ser[line], \
  &dummy_ser[line], &dummy_ser[line], \
  &dummy_ser[line], &dummy_ser[line], \
  DUMMY_DTR_MASK, DUMMY_RI_MASK, DUMMY_DSR_MASK, DUMMY_CD_MASK
    

struct control_pins
{
	volatile unsigned char *dtr_port;
	unsigned char          *dtr_shadow;
	volatile unsigned char *ri_port;
	unsigned char          *ri_shadow;
	volatile unsigned char *dsr_port;
	unsigned char          *dsr_shadow;
	volatile unsigned char *cd_port;
	unsigned char          *cd_shadow;

	unsigned char dtr_mask;
	unsigned char ri_mask;
	unsigned char dsr_mask;
	unsigned char cd_mask;
};

static const struct control_pins e100_modem_pins[NR_PORTS] = 
{
	/* Ser 0 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT0
	E100_STRUCT_PORT(0,DTR), E100_STRUCT_SHADOW(0,DTR),
	E100_STRUCT_PORT(0,RI),  E100_STRUCT_SHADOW(0,RI),
	E100_STRUCT_PORT(0,DSR), E100_STRUCT_SHADOW(0,DSR),
	E100_STRUCT_PORT(0,CD),  E100_STRUCT_SHADOW(0,CD),
	E100_STRUCT_MASK(0,DTR),
	E100_STRUCT_MASK(0,RI),
	E100_STRUCT_MASK(0,DSR),
	E100_STRUCT_MASK(0,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(0)
#endif	
	},

	/* Ser 1 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT1	  
	E100_STRUCT_PORT(1,DTR), E100_STRUCT_SHADOW(1,DTR),
	E100_STRUCT_PORT(1,RI),  E100_STRUCT_SHADOW(1,RI),
	E100_STRUCT_PORT(1,DSR), E100_STRUCT_SHADOW(1,DSR),
	E100_STRUCT_PORT(1,CD),  E100_STRUCT_SHADOW(1,CD),
	E100_STRUCT_MASK(1,DTR),
	E100_STRUCT_MASK(1,RI),
	E100_STRUCT_MASK(1,DSR),
	E100_STRUCT_MASK(1,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(1)
#endif		
	},

	/* Ser 2 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT2	  
	E100_STRUCT_PORT(2,DTR), E100_STRUCT_SHADOW(2,DTR),
	E100_STRUCT_PORT(2,RI),  E100_STRUCT_SHADOW(2,RI),
	E100_STRUCT_PORT(2,DSR), E100_STRUCT_SHADOW(2,DSR),
	E100_STRUCT_PORT(2,CD),  E100_STRUCT_SHADOW(2,CD),
	E100_STRUCT_MASK(2,DTR),
	E100_STRUCT_MASK(2,RI),
	E100_STRUCT_MASK(2,DSR),
	E100_STRUCT_MASK(2,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(2)
#endif		
	},

	/* Ser 3 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT3	  
	E100_STRUCT_PORT(3,DTR), E100_STRUCT_SHADOW(3,DTR),
	E100_STRUCT_PORT(3,RI),  E100_STRUCT_SHADOW(3,RI),
	E100_STRUCT_PORT(3,DSR), E100_STRUCT_SHADOW(3,DSR),
	E100_STRUCT_PORT(3,CD),  E100_STRUCT_SHADOW(3,CD),
	E100_STRUCT_MASK(3,DTR),
	E100_STRUCT_MASK(3,RI),
	E100_STRUCT_MASK(3,DSR),
	E100_STRUCT_MASK(3,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(3)
#endif		
	}
};
#else  /* CONFIG_ETRAX_SERX_DTR_RI_DSR_CD_MIXED */

/* All pins are on either PA or PB for each serial port */
#define CONTROL_PINS_PORT_NOT_USED(line) \
  &dummy_ser[line], &dummy_ser[line], \
  DUMMY_DTR_MASK, DUMMY_RI_MASK, DUMMY_DSR_MASK, DUMMY_CD_MASK
    

struct control_pins
{
	volatile unsigned char *port;
	unsigned char          *shadow;

	unsigned char dtr_mask;
	unsigned char ri_mask;
	unsigned char dsr_mask;
	unsigned char cd_mask;
};

#define dtr_port port
#define dtr_shadow shadow
#define ri_port port
#define ri_shadow shadow
#define dsr_port port
#define dsr_shadow shadow
#define cd_port port
#define cd_shadow shadow

static const struct control_pins e100_modem_pins[NR_PORTS] = 
{
	/* Ser 0 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT0
	E100_STRUCT_PORT(0,DTR), E100_STRUCT_SHADOW(0,DTR),
	E100_STRUCT_MASK(0,DTR),
	E100_STRUCT_MASK(0,RI),
	E100_STRUCT_MASK(0,DSR),
	E100_STRUCT_MASK(0,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(0)
#endif	
	},

	/* Ser 1 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT1	  
	E100_STRUCT_PORT(1,DTR), E100_STRUCT_SHADOW(1,DTR),
	E100_STRUCT_MASK(1,DTR),
	E100_STRUCT_MASK(1,RI),
	E100_STRUCT_MASK(1,DSR),
	E100_STRUCT_MASK(1,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(1)
#endif		
	},

	/* Ser 2 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT2	  
	E100_STRUCT_PORT(2,DTR), E100_STRUCT_SHADOW(2,DTR),
	E100_STRUCT_MASK(2,DTR),
	E100_STRUCT_MASK(2,RI),
	E100_STRUCT_MASK(2,DSR),
	E100_STRUCT_MASK(2,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(2)
#endif		
	},

	/* Ser 3 */
	{
#ifdef CONFIG_ETRAX_SERIAL_PORT3	  
	E100_STRUCT_PORT(3,DTR), E100_STRUCT_SHADOW(3,DTR),
	E100_STRUCT_MASK(3,DTR),
	E100_STRUCT_MASK(3,RI),
	E100_STRUCT_MASK(3,DSR),
	E100_STRUCT_MASK(3,CD)
#else
	CONTROL_PINS_PORT_NOT_USED(3)
#endif		
	}
};
#endif /* !CONFIG_ETRAX_SERX_DTR_RI_DSR_CD_MIXED */

#if defined(CONFIG_ETRAX_RS485) && defined(CONFIG_ETRAX_RS485_ON_PA)
unsigned char rs485_pa_port = CONFIG_ETRAX_RS485_ON_PA_BIT;
#endif


#define E100_RTS_MASK 0x20
#define E100_CTS_MASK 0x40


/* All serial port signals are active low:
 * active   = 0 -> 3.3V to RS-232 driver -> -12V on RS-232 level
 * inactive = 1 -> 0V   to RS-232 driver -> +12V on RS-232 level
 *
 * These macros returns the pin value: 0=0V, >=1 = 3.3V on ETRAX chip
 */

/* Output */
#define E100_RTS_GET(info) ((info)->rx_ctrl & E100_RTS_MASK)
/* Input */
#define E100_CTS_GET(info) ((info)->port[REG_STATUS] & E100_CTS_MASK)

/* These are typically PA or PB and 0 means 0V, 1 means 3.3V */
/* Is an output */
#define E100_DTR_GET(info) ((*e100_modem_pins[(info)->line].dtr_shadow) & e100_modem_pins[(info)->line].dtr_mask)

/* Normally inputs */
#define E100_RI_GET(info) ((*e100_modem_pins[(info)->line].ri_port) & e100_modem_pins[(info)->line].ri_mask)
#define E100_CD_GET(info) ((*e100_modem_pins[(info)->line].cd_port) & e100_modem_pins[(info)->line].cd_mask)

/* Input */
#define E100_DSR_GET(info) ((*e100_modem_pins[(info)->line].dsr_port) & e100_modem_pins[(info)->line].dsr_mask)


/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char *tmp_buf;
#ifdef DECLARE_MUTEX
static DECLARE_MUTEX(tmp_buf_sem);
#else
static struct semaphore tmp_buf_sem = MUTEX;
#endif

/* Calculate the chartime depending on baudrate, numbor of bits etc. */
static void update_char_time(struct e100_serial * info)
{
	tcflag_t cflags = info->tty->termios->c_cflag;
	int bits;

	/* calc. number of bits / data byte */
	/* databits + startbit and 1 stopbit */
	if ((cflags & CSIZE) == CS7)
		bits = 9;
	else
		bits = 10;  

	if (cflags & CSTOPB)     /* 2 stopbits ? */
		bits++;

	if (cflags & PARENB)     /* parity bit ? */
		bits++;

	/* calc timeout */
	info->char_time_usec = ((bits * 1000000) / info->baud) + 1;
}

/*
 * This function maps from the Bxxxx defines in asm/termbits.h into real
 * baud rates.
 */

static int 
cflag_to_baud(unsigned int cflag)
{
	static int baud_table[] = {
		0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
		4800, 9600, 19200, 38400 };

	static int ext_baud_table[] = {
		0, 57600, 115200, 230400, 460800, 921600, 1843200, 6250000,
                0, 0, 0, 0, 0, 0, 0, 0 };

	if (cflag & CBAUDEX)
		return ext_baud_table[(cflag & CBAUD) & ~CBAUDEX];
	else 
		return baud_table[cflag & CBAUD];
}

/* and this maps to an etrax100 hardware baud constant */

static unsigned char 
cflag_to_etrax_baud(unsigned int cflag)
{
	char retval;

	static char baud_table[] = {
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 2, -1, 3, 4, 5, 6, 7 };

	static char ext_baud_table[] = {
		-1, 8, 9, 10, 11, 12, 13, 14, -1, -1, -1, -1, -1, -1, -1, -1 };

	if (cflag & CBAUDEX)
		retval = ext_baud_table[(cflag & CBAUD) & ~CBAUDEX];
	else 
		retval = baud_table[cflag & CBAUD];

	if (retval < 0) {
		printk(KERN_WARNING "serdriver tried setting invalid baud rate, flags %x.\n", cflag);
		retval = 5; /* choose default 9600 instead */
	}

	return retval | (retval << 4); /* choose same for both TX and RX */
}


/* Various static support functions */

/* Functions to set or clear DTR/RTS on the requested line */
/* It is complicated by the fact that RTS is a serial port register, while
 * DTR might not be implemented in the HW at all, and if it is, it can be on
 * any general port.
 */


static inline void 
e100_dtr(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	unsigned char mask = e100_modem_pins[info->line].dtr_mask;

#ifdef SERIAL_DEBUG_IO  
	printk("ser%i dtr %i mask: 0x%02X\n", info->line, set, mask);
	printk("ser%i shadow before 0x%02X get: %i\n", 
	       info->line, *e100_modem_pins[info->line].dtr_shadow,
	       E100_DTR_GET(info));
#endif
	/* DTR is active low */
	{
		unsigned long flags;

		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].dtr_shadow &= ~mask;
		*e100_modem_pins[info->line].dtr_shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].dtr_port = *e100_modem_pins[info->line].dtr_shadow;
		restore_flags(flags);
	}
	
#ifdef SERIAL_DEBUG_IO
	printk("ser%i shadow after 0x%02X get: %i\n", 
	       info->line, *e100_modem_pins[info->line].dtr_shadow, 
	       E100_DTR_GET(info));
#endif
#endif
}

/* set = 0 means 3.3V on the pin, bitvalue: 0=active, 1=inactive  
 *                                          0=0V    , 1=3.3V
 */
static inline void 
e100_rts(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	info->rx_ctrl &= ~E100_RTS_MASK;
	info->rx_ctrl |= (set ? 0 : E100_RTS_MASK);  /* RTS is active low */
	info->port[REG_REC_CTRL] = info->rx_ctrl;
#ifdef SERIAL_DEBUG_IO  
	printk("ser%i rts %i\n", info->line, set);
#endif
#endif
}


/* If this behaves as a modem, RI and CD is an output */
static inline void 
e100_ri_out(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	/* RI is active low */
	{
		unsigned char mask = e100_modem_pins[info->line].ri_mask;
		unsigned long flags;

		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].ri_shadow &= ~mask;
		*e100_modem_pins[info->line].ri_shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].ri_port = *e100_modem_pins[info->line].ri_shadow;
		restore_flags(flags);
	}
#endif
}
static inline void 
e100_cd_out(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	/* CD is active low */
	{
		unsigned char mask = e100_modem_pins[info->line].cd_mask;
		unsigned long flags;

		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].cd_shadow &= ~mask;
		*e100_modem_pins[info->line].cd_shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].cd_port = *e100_modem_pins[info->line].cd_shadow;
		restore_flags(flags);
	}
#endif
}

static inline void
e100_disable_rx(struct e100_serial *info)
{
#ifndef CONFIG_SVINTO_SIM
	/* disable the receiver */
	info->port[REG_REC_CTRL] =
		(info->rx_ctrl &= ~IO_MASK(R_SERIAL0_REC_CTRL, rec_enable));
#endif
}

static inline void 
e100_enable_rx(struct e100_serial *info)
{
#ifndef CONFIG_SVINTO_SIM
	/* enable the receiver */
	info->port[REG_REC_CTRL] =
		(info->rx_ctrl |= IO_MASK(R_SERIAL0_REC_CTRL, rec_enable));
#endif
}

/* the rx DMA uses both the dma_descr and the dma_eop interrupts */

static inline void
e100_disable_rxdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("rxdma_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK2_CLR = (info->irq << 2) | (info->irq << 3);
}

static inline void
e100_enable_rxdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("rxdma_irq(%d): 1\n",info->line);
#endif
	*R_IRQ_MASK2_SET = (info->irq << 2) | (info->irq << 3);
}

/* the tx DMA uses only dma_descr interrupt */

static inline void
e100_disable_txdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("txdma_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK2_CLR = info->irq;
}

static inline void
e100_enable_txdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("txdma_irq(%d): 1\n",info->line);
#endif
	*R_IRQ_MASK2_SET = info->irq;
}

static inline void
e100_disable_txdma_channel(struct e100_serial *info)
{
	unsigned long flags;
  
	/* Disable output DMA channel for the serial port in question
	 * ( set to something other then serialX)
	 */
	save_flags(flags);
	cli();  
	if (info->line == 0) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma6);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma6, unused);
	} else if (info->line == 1) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma8);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma8, usb);
	} else if (info->line == 2) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma2);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma2, par0);
	} else if (info->line == 3) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma4);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma4, par1);
	}
	*R_GEN_CONFIG = genconfig_shadow;
	restore_flags(flags);
}


static inline void
e100_enable_txdma_channel(struct e100_serial *info)
{
	unsigned long flags;
  
	save_flags(flags);
	cli();  
	/* Enable output DMA channel for the serial port in question */
	if (info->line == 0) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma6);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma6, serial0);
	} else if (info->line == 1) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma8);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma8, serial1);
	} else if (info->line == 2) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma2);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma2, serial2);
	} else if (info->line == 3) {
		genconfig_shadow &=  ~IO_MASK(R_GEN_CONFIG, dma4);
		genconfig_shadow |= IO_STATE(R_GEN_CONFIG, dma4, serial3);
	}
	*R_GEN_CONFIG = genconfig_shadow;
	restore_flags(flags);
}


#ifdef SERIAL_HANDLE_EARLY_ERRORS
/* in order to detect and fix errors on the first byte
   we have to use the serial interrupts as well. */

static inline void
e100_disable_serial_data_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("ser_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK1_CLR = (1U << (8+2*info->line));
}

static inline void
e100_enable_serial_data_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("ser_irq(%d): 1\n",info->line);
	printk("**** %d = %d\n",
	       (8+2*info->line),
	       (1U << (8+2*info->line)));
#endif
	*R_IRQ_MASK1_SET = (1U << (8+2*info->line));
}
#endif

#if defined(CONFIG_ETRAX_RS485)
/* Enable RS-485 mode on selected port. This is UGLY. */
static int
e100_enable_rs485(struct tty_struct *tty,struct rs485_control *r)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;

#if defined(CONFIG_ETRAX_RS485_ON_PA)	
	*R_PORT_PA_DATA = port_pa_data_shadow |= (1 << rs485_pa_bit);
#endif

	info->rs485.rts_on_send = 0x01 & r->rts_on_send;
	info->rs485.rts_after_sent = 0x01 & r->rts_after_sent;
	info->rs485.delay_rts_before_send = r->delay_rts_before_send;
	info->rs485.enabled = r->enabled;
/*	printk("rts: on send = %i, after = %i, enabled = %i",
		    info->rs485.rts_on_send,
		    info->rs485.rts_after_sent,
		    info->rs485.enabled
	);
*/		
	return 0;
}

static int
e100_write_rs485(struct tty_struct *tty, int from_user,
                 const unsigned char *buf, int count)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	int old_enabled = info->rs485.enabled;

	/* rs485 is always implicitly enabled if we're using the ioctl() 
	 * but it doesn't have to be set in the rs485_control
	 * (to be backward compatible with old apps)
	 * So we store, set and restore it.
	 */
	info->rs485.enabled = 1;
	/* rs_write now deals with RS485 if enabled */
	count = rs_write(tty, from_user, buf, count);
	info->rs485.enabled = old_enabled;
	return count;
}

#ifdef CONFIG_ETRAX_FAST_TIMER
/* Timer function to toggle RTS when using FAST_TIMER */
static void rs485_toggle_rts_timer_function(unsigned long data)
{
	struct e100_serial *info = (struct e100_serial *)data;

	fast_timers_rs485[info->line].function = NULL;
	e100_rts(info, info->rs485.rts_after_sent);
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
	e100_enable_rx(info);
	e100_enable_rxdma_irq(info);
#endif
}
#endif
#endif /* CONFIG_ETRAX_RS485 */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter using the XOFF registers, as necessary.
 * ------------------------------------------------------------
 */

static void 
rs_stop(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	if (info) {
		unsigned long flags;
		unsigned long xoff;
		
		save_flags(flags); cli();
		xoff = IO_FIELD(R_SERIAL0_XOFF, xoff_char, STOP_CHAR(info->tty));
		xoff |= IO_STATE(R_SERIAL0_XOFF, tx_stop, stop);
		if (tty->termios->c_iflag & IXON ) {
			xoff |= IO_STATE(R_SERIAL0_XOFF, auto_xoff, enable);
		}
	
		*((unsigned long *)&info->port[REG_XOFF]) = xoff;
		restore_flags(flags);
	}
}

static void 
rs_start(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	if (info) {
		unsigned long flags;
		unsigned long xoff;

		save_flags(flags); cli();
		xoff = IO_FIELD(R_SERIAL0_XOFF, xoff_char, STOP_CHAR(tty));
		xoff |= IO_STATE(R_SERIAL0_XOFF, tx_stop, enable);
		if (tty->termios->c_iflag & IXON ) {
			xoff |= IO_STATE(R_SERIAL0_XOFF, auto_xoff, enable);
		}
	
		*((unsigned long *)&info->port[REG_XOFF]) = xoff;
		
		restore_flags(flags);
	}
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static _INLINE_ void 
rs_sched_event(struct e100_serial *info,
				    int event)
{
	info->event |= 1 << event;
	schedule_work(&info->work);
}

/* The output DMA channel is free - use it to send as many chars as possible
 * NOTES:
 *   We don't pay attention to info->x_char, which means if the TTY wants to
 *   use XON/XOFF it will set info->x_char but we won't send any X char!
 * 
 *   To implement this, we'd just start a DMA send of 1 byte pointing at a
 *   buffer containing the X char, and skip updating xmit. We'd also have to
 *   check if the last sent char was the X char when we enter this function
 *   the next time, to avoid updating xmit with the sent X value.
 */

static void 
transmit_chars(struct e100_serial *info)
{
	unsigned int c, sentl;
	struct etrax_dma_descr *descr;

#ifdef CONFIG_SVINTO_SIM
	/* This will output too little if tail is not 0 always since
	 * we don't reloop to send the other part. Anyway this SHOULD be a
	 * no-op - transmit_chars would never really be called during sim
	 * since rs_write does not write into the xmit buffer then.
	 */
	if (info->xmit.tail)
		printk("Error in serial.c:transmit_chars(), tail!=0\n");
	if (info->xmit.head != info->xmit.tail) {
		SIMCOUT(info->xmit.buf + info->xmit.tail,
			CIRC_CNT(info->xmit.head,
				 info->xmit.tail,
				 SERIAL_XMIT_SIZE));
		info->xmit.head = info->xmit.tail;  /* move back head */
		info->tr_running = 0;
	}
	return;
#endif
	/* acknowledge both dma_descr and dma_eop irq in R_DMA_CHx_CLR_INTR */
	*info->oclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);

#ifdef SERIAL_DEBUG_INTR
	if (info->line == SERIAL_DEBUG_LINE)
		printk("tc\n");
#endif
	if (!info->tr_running) {
		/* weirdo... we shouldn't get here! */
		printk(KERN_WARNING "Achtung: transmit_chars with !tr_running\n");
		return;
	}

	descr = &info->tr_descr;

	/* first get the amount of bytes sent during the last DMA transfer,
	   and update xmit accordingly */

	/* if the stop bit was not set, all data has been sent */
	if (!(descr->status & d_stop)) {
		sentl = descr->sw_len;
	} else 
		/* otherwise we find the amount of data sent here */
		sentl = descr->hw_len;

	/* update stats */
	info->icount.tx += sentl;

	/* update xmit buffer */
	info->xmit.tail = (info->xmit.tail + sentl) & (SERIAL_XMIT_SIZE - 1);

	/* if there is only a few chars left in the buf, wake up the blocked
	   write if any */
	if (CIRC_CNT(info->xmit.head,
		     info->xmit.tail,
		     SERIAL_XMIT_SIZE) < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);

	/* find out the largest amount of consecutive bytes we want to send now */

	c = CIRC_CNT_TO_END(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);

	if (c <= 0) {
		/* our job here is done, don't schedule any new DMA transfer */
		info->tr_running = 0;

#if defined(CONFIG_ETRAX_RS485) && defined(CONFIG_ETRAX_FAST_TIMER)
		if (info->rs485.enabled) {
			/* Set a short timer to toggle RTS */
			start_one_shot_timer(&fast_timers_rs485[info->line],
			                     rs485_toggle_rts_timer_function,
			                     (unsigned long)info,
			                     info->char_time_usec*2,
			                     "RS-485");
		}
#endif /* RS485 */
		return;
	}

	/* ok we can schedule a dma send of c chars starting at info->xmit.tail */
	/* set up the descriptor correctly for output */

	descr->ctrl = d_int | d_eol | d_wait; /* Wait needed for tty_wait_until_sent() */
	descr->sw_len = c;
	descr->buf = virt_to_phys(info->xmit.buf + info->xmit.tail);
	descr->status = 0;

	*info->ofirstadr = virt_to_phys(descr); /* write to R_DMAx_FIRST */
	*info->ocmdadr = 1;       /* dma command start -> R_DMAx_CMD */
	
	/* DMA is now running (hopefully) */
} /* transmit_chars */

static void 
start_transmit(struct e100_serial *info)
{
#if 0
	if (info->line == SERIAL_DEBUG_LINE)
		printk("x\n");
#endif

	info->tr_descr.sw_len = 0;
	info->tr_descr.hw_len = 0;
	info->tr_descr.status = 0;
	info->tr_running = 1;

	transmit_chars(info);
} /* start_transmit */

#ifdef CONFIG_ETRAX_SERIAL_FAST_TIMER
static int serial_fast_timer_started = 0;
static int serial_fast_timer_expired = 0;
static void flush_timeout_function(unsigned long data);
#define START_FLUSH_FAST_TIMER(info, string) {\
  unsigned long timer_flags; \
  save_flags(timer_flags); \
  cli(); \
  if (fast_timers[info->line].function == NULL) { \
    serial_fast_timer_started++; \
    TIMERD(DEBUG_LOG(info->line, "start_timer %i ", info->line)); \
    TIMERD(DEBUG_LOG(info->line, "num started: %i\n", serial_fast_timer_started)); \
    start_one_shot_timer(&fast_timers[info->line], \
                         flush_timeout_function, \
                         (unsigned long)info, \
                         info->char_time_usec*4, \
                         string); \
  } \
  else { \
    TIMERD(DEBUG_LOG(info->line, "timer %i already running\n", info->line)); \
  } \
  restore_flags(timer_flags); \
}

#else
#define START_FLUSH_FAST_TIMER(info, string)
#endif

static struct etrax_recv_buffer *
alloc_recv_buffer(unsigned int size)
{
	struct etrax_recv_buffer *buffer;

	if (!(buffer = kmalloc(sizeof *buffer + size, GFP_ATOMIC)))
		return NULL;

	buffer->next = NULL;
	buffer->length = 0;
	buffer->error = TTY_NORMAL;

	return buffer;
}

static void
append_recv_buffer(struct e100_serial *info, struct etrax_recv_buffer *buffer)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if (!info->first_recv_buffer)
		info->first_recv_buffer = buffer;
	else
		info->last_recv_buffer->next = buffer;

	info->last_recv_buffer = buffer;

	info->recv_cnt += buffer->length;
	if (info->recv_cnt > info->max_recv_cnt)
		info->max_recv_cnt = info->recv_cnt;

	restore_flags(flags);
}

static int
add_char_and_flag(struct e100_serial *info, unsigned char data, unsigned char flag)
{
	struct etrax_recv_buffer *buffer;

	if (!(buffer = alloc_recv_buffer(4)))
		return 0;

	buffer->length = 1;
	buffer->error = flag;
	buffer->buffer[0] = data;
	
	append_recv_buffer(info, buffer);

	info->icount.rx++;

	return 1;
}

extern _INLINE_ unsigned int
handle_descr_data(struct e100_serial *info, struct etrax_dma_descr *descr, unsigned int recvl)
{
	struct etrax_recv_buffer *buffer = phys_to_virt(descr->buf) - sizeof *buffer;

	if (info->recv_cnt + recvl > 65536) {
		printk(KERN_CRIT
		       "%s: Too much pending incoming serial data! Dropping %u bytes.\n", __FUNCTION__, recvl);
		return 0;
	}

	buffer->length = recvl;

	if (info->errorcode == ERRCODE_SET_BREAK)
		buffer->error = TTY_BREAK;
	info->errorcode = 0;

	append_recv_buffer(info, buffer);

	if (!(buffer = alloc_recv_buffer(SERIAL_DESCR_BUF_SIZE)))
		panic("%s: Failed to allocate memory for receive buffer!\n", __FUNCTION__);

	descr->buf = virt_to_phys(buffer->buffer);
	
	return recvl;
}

static _INLINE_ unsigned int
handle_all_descr_data(struct e100_serial *info)
{
	struct etrax_dma_descr *descr;
	unsigned int recvl;
	unsigned int ret = 0;

	while (1)
	{
		descr = &info->rec_descr[info->cur_rec_descr];

		if (descr == phys_to_virt(*info->idescradr))
			break;

		if (++info->cur_rec_descr == SERIAL_RECV_DESCRIPTORS)
			info->cur_rec_descr = 0;
	
		/* find out how many bytes were read */

		/* if the eop bit was not set, all data has been received */
		if (!(descr->status & d_eop)) {
			recvl = descr->sw_len;
		} else {
			/* otherwise we find the amount of data received here */
			recvl = descr->hw_len;
		}

		/* Reset the status information */
		descr->status = 0;

		DEBUG_LOG(info->line, "recvl %lu\n", recvl);

		/* update stats */
		info->icount.rx += recvl;

		ret += handle_descr_data(info, descr, recvl);
	}

	return ret;
}

static _INLINE_ void 
receive_chars(struct e100_serial *info)
{
	struct tty_struct *tty;
	unsigned char rstat;

#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	return;
#endif

	/* Acknowledge both dma_descr and dma_eop irq in R_DMA_CHx_CLR_INTR */
	*info->iclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);

	tty = info->tty;
	if (!tty) /* Something wrong... */
		return;

#ifdef SERIAL_HANDLE_EARLY_ERRORS
	e100_enable_serial_data_irq(info);
#endif	

	if (info->errorcode == ERRCODE_INSERT_BREAK)
		add_char_and_flag(info, '\0', TTY_BREAK);

	handle_all_descr_data(info);

	/* Read the status register to detect errors */
	rstat = info->port[REG_STATUS];

	if (rstat & SER_ERROR_MASK) {
		/* If we got an error, we must reset it by reading the
		 * data_in field
		 */
		unsigned char data = info->port[REG_DATA];

		PROCSTAT(ser_stat[info->line].errors_cnt++);
		DEBUG_LOG(info->line, "#dERR: s d 0x%04X\n",
			  ((rstat & SER_ERROR_MASK) << 8) | data);

		if (rstat & SER_PAR_ERR_MASK)
			add_char_and_flag(info, data, TTY_PARITY);
		else if (rstat & SER_OVERRUN_MASK)
			add_char_and_flag(info, data, TTY_OVERRUN);
		else if (rstat & SER_FRAMING_ERR_MASK)
			add_char_and_flag(info, data, TTY_FRAME);
	}

	START_FLUSH_FAST_TIMER(info, "receive_chars");

	/* Restart the receiving DMA */
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, restart);
}

static _INLINE_ int
start_recv_dma(struct e100_serial *info)
{
	struct etrax_dma_descr *descr = info->rec_descr;
	struct etrax_recv_buffer *buffer;
        int i;

	/* Set up the receiving descriptors */
	for (i = 0; i < SERIAL_RECV_DESCRIPTORS; i++) {
		if (!(buffer = alloc_recv_buffer(SERIAL_DESCR_BUF_SIZE)))
			panic("%s: Failed to allocate memory for receive buffer!\n", __FUNCTION__);

		descr[i].ctrl = d_int;
		descr[i].buf = virt_to_phys(buffer->buffer);
		descr[i].sw_len = SERIAL_DESCR_BUF_SIZE;
		descr[i].hw_len = 0;
		descr[i].status = 0;
		descr[i].next = virt_to_phys(&descr[i+1]);
	}

	/* Link the last descriptor to the first */
	descr[i-1].next = virt_to_phys(&descr[0]);

	/* Start with the first descriptor in the list */
	info->cur_rec_descr = 0;

	/* Start the DMA */
	*info->ifirstadr = virt_to_phys(&descr[info->cur_rec_descr]);
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, start);

	/* Input DMA should be running now */
	return 1;
}

static void 
start_receive(struct e100_serial *info)
{
#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	return;
#endif

	/* reset the input dma channel to be sure it works */

	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	while (IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->icmdadr) ==
	       IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));

	info->tty->flip.count = 0;

	start_recv_dma(info);
}


static _INLINE_ void 
status_handle(struct e100_serial *info, unsigned short status)
{
}

/* the bits in the MASK2 register are laid out like this:
   DMAI_EOP DMAI_DESCR DMAO_EOP DMAO_DESCR
   where I is the input channel and O is the output channel for the port.
   info->irq is the bit number for the DMAO_DESCR so to check the others we
   shift info->irq to the left.
*/

/* dma output channel interrupt handler
   this interrupt is called from DMA2(ser2), DMA4(ser3), DMA6(ser0) or
   DMA8(ser1) when they have finished a descriptor with the intr flag set.
*/

static irqreturn_t
tr_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct e100_serial *info;
	unsigned long ireg;
	int i;
	int handled = 0;
        
#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	{
		const char *s = "What? tr_interrupt in simulator??\n";
		SIMCOUT(s,strlen(s));
	}
	return IRQ_HANDLED;
#endif
	
	/* find out the line that caused this irq and get it from rs_table */
	
	ireg = *R_IRQ_MASK2_RD;  /* get the active irq bits for the dma channels */
	
	for (i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (!info->enabled || !info->uses_dma) 
			continue; 
		/* check for dma_descr (don't need to check for dma_eop in output dma for serial */
		if (ireg & info->irq) {  
			handled = 1;
			/* we can send a new dma bunch. make it so. */
			DEBUG_LOG(info->line, "tr_interrupt %i\n", i);
			/* Read jiffies_usec first, 
			 * we want this time to be as late as possible
			 */
 			PROCSTAT(ser_stat[info->line].tx_dma_ints++);
			info->last_tx_active_usec = GET_JIFFIES_USEC();
			info->last_tx_active = jiffies;
			transmit_chars(info);
		}
		
		/* FIXME: here we should really check for a change in the
		   status lines and if so call status_handle(info) */
	}
	return IRQ_RETVAL(handled);
}

/* dma input channel interrupt handler */

static irqreturn_t
rec_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct e100_serial *info;
	unsigned long ireg;
	int i;
	int handled = 0;

#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	{
		const char *s = "What? rec_interrupt in simulator??\n";
		SIMCOUT(s,strlen(s));
	}
	return;
#endif
	
	/* find out the line that caused this irq and get it from rs_table */
	
	ireg = *R_IRQ_MASK2_RD;  /* get the active irq bits for the dma channels */
	
	for (i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (!info->enabled || !info->uses_dma) 
			continue; 
		/* check for both dma_eop and dma_descr for the input dma channel */
		if (ireg & ((info->irq << 2) | (info->irq << 3))) {
			handled = 1; 
			/* we have received something */
			receive_chars(info);
		}
		
		/* FIXME: here we should really check for a change in the
		   status lines and if so call status_handle(info) */
	}
	return IRQ_RETVAL(handled);
}

static _INLINE_ int
force_eop_if_needed(struct e100_serial *info)
{
	/* We check data_avail bit to determine if data has 
	 * arrived since last time
	 */ 
	unsigned char rstat = info->port[REG_STATUS];

	/* error or datavail? */
	if (rstat & SER_ERROR_MASK) { 
		/* Some error has occurred. If there has been valid data, an
		 * EOP interrupt will be made automatically. If no data, the
		 * normal ser_interrupt should be enabled and handle it.
		 * So do nothing!
		 */
		DEBUG_LOG(info->line, "timeout err: rstat 0x%03X\n",
		          rstat | (info->line << 8));
		return 0;
	}

	if (rstat & SER_DATA_AVAIL_MASK) { 
		/* Ok data, no error, count it */
		TIMERD(DEBUG_LOG(info->line, "timeout: rstat 0x%03X\n",
		          rstat | (info->line << 8)));
		/* Read data to clear status flags */
		(void)info->port[REG_DATA];

		info->forced_eop = 0;
		START_FLUSH_FAST_TIMER(info, "magic");
		return 0;
	}

	/* hit the timeout, force an EOP for the input
	 * dma channel if we haven't already
	 */
	if (!info->forced_eop) {
		info->forced_eop = 1;
		PROCSTAT(ser_stat[info->line].timeout_flush_cnt++);
		DEBUG_LOG(info->line, "timeout EOP %i\n", info->line);
		FORCE_EOP(info);
	}

	return 1;
}

static _INLINE_ void
flush_to_flip_buffer(struct e100_serial *info)
{
	struct tty_struct *tty;
	struct etrax_recv_buffer *buffer;
	unsigned int length;
	unsigned long flags;

	if (!info->first_recv_buffer)
		return;

	save_flags(flags);
	cli();

	if (!(tty = info->tty)) {
		restore_flags(flags);
		return;
	}

	length = tty->flip.count;

	while ((buffer = info->first_recv_buffer) && length < TTY_FLIPBUF_SIZE) {
		unsigned int count = buffer->length;

		if (length + count > TTY_FLIPBUF_SIZE)
			count = TTY_FLIPBUF_SIZE - length;

		memcpy(tty->flip.char_buf_ptr + length, buffer->buffer, count);
		memset(tty->flip.flag_buf_ptr + length, TTY_NORMAL, count);
		tty->flip.flag_buf_ptr[length] = buffer->error;

		length += count;
		info->recv_cnt -= count;

		if (count == buffer->length) {
			info->first_recv_buffer = buffer->next;
			kfree(buffer);
		} else {
			buffer->length -= count;
			memmove(buffer->buffer, buffer->buffer + count, buffer->length);
			buffer->error = TTY_NORMAL;
		}
	}

	if (!info->first_recv_buffer)
		info->last_recv_buffer = NULL;

	tty->flip.count = length;

	restore_flags(flags);

	/* this includes a check for low-latency */
	tty_flip_buffer_push(tty);
}

static _INLINE_ void
check_flush_timeout(struct e100_serial *info)
{
	force_eop_if_needed(info);

	flush_to_flip_buffer(info);

	if (info->first_recv_buffer)
		START_FLUSH_FAST_TIMER(info, "flip");
}

#ifdef CONFIG_ETRAX_SERIAL_FAST_TIMER
static void flush_timeout_function(unsigned long data)
{
	struct e100_serial *info = (struct e100_serial *)data;

	fast_timers[info->line].function = NULL;
	serial_fast_timer_expired++;
	TIMERD(DEBUG_LOG(info->line, "flush_timout %i ", info->line));
	TIMERD(DEBUG_LOG(info->line, "num expired: %i\n", serial_fast_timer_expired));
	check_flush_timeout(info);
}

#else

/* dma fifo/buffer timeout handler
   forces an end-of-packet for the dma input channel if no chars 
   have been received for CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS/100 s.
*/

static struct timer_list flush_timer;

static void 
timed_flush_handler(unsigned long ptr)
{
	struct e100_serial *info;
	int i;

#ifdef CONFIG_SVINTO_SIM
	return;
#endif
	
	for (i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (info->uses_dma) 
			check_flush_timeout(info);
	}

	/* restart flush timer */
	mod_timer(&flush_timer, jiffies + CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS);
}
#endif

#ifdef SERIAL_HANDLE_EARLY_ERRORS

/* If there is an error (ie break) when the DMA is running and
 * there are no bytes in the fifo the DMA is stopped and we get no
 * eop interrupt. Thus we have to monitor the first bytes on a DMA
 * transfer, and if it is without error we can turn the serial
 * interrupts off.
 */

/*
BREAK handling on ETRAX 100:
ETRAX will generate interrupt although there is no stop bit between the
characters.

Depending on how long the break sequence is, the end of the breaksequence
will look differently:
| indicates start/end of a character.

B= Break character (0x00) with framing error.
E= Error byte with parity error received after B characters.
F= "Faked" valid byte received immediately after B characters.
V= Valid byte

1.
    B          BL         ___________________________ V
.._|__________|__________|                           |valid data |

Multiple frame errors with data == 0x00 (B),
the timing matches up "perfectly" so no extra ending char is detected.
The RXD pin is 1 in the last interrupt, in that case
we set info->errorcode = ERRCODE_INSERT_BREAK, but we can't really
know if another byte will come and this really is case 2. below 
(e.g F=0xFF or 0xFE)
If RXD pin is 0 we can expect another character (see 2. below).


2.

    B          B          E or F__________________..__ V
.._|__________|__________|______    |                 |valid data
                          "valid" or 
                          parity error

Multiple frame errors with data == 0x00 (B),
but the part of the break trigs is interpreted as a start bit (and possibly
some 0 bits followed by a number of 1 bits and a stop bit).
Depending on parity settings etc. this last character can be either
a fake "valid" char (F) or have a parity error (E).

If the character is valid it will be put in the buffer,
we set info->errorcode = ERRCODE_SET_BREAK so the receive interrupt
will set the flags so the tty will handle it,
if it's an error byte it will not be put in the buffer
and we set info->errorcode = ERRCODE_INSERT_BREAK.

To distinguish a V byte in 1. from an F byte in 2. we keep a timestamp
of the last faulty char (B) and compares it with the current time:
If the time elapsed time is less then 2*char_time_usec we will assume
it's a faked F char and not a Valid char and set 
info->errorcode = ERRCODE_SET_BREAK. 

Flaws in the above solution:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
We use the timer to distinguish a F character from a V character,
if a V character is to close after the break we might make the wrong decision.

TODO: The break will be delayed until an F or V character is received.

*/

extern irqreturn_t _INLINE_ handle_ser_interrupt(struct e100_serial *info)
{
	unsigned char rstat = info->port[REG_STATUS];

#ifdef SERIAL_DEBUG_INTR
	printk("Interrupt from serport %d\n", i);
#endif
/*	DEBUG_LOG(info->line, "ser_interrupt stat %03X\n", rstat | (i << 8)); */
	if (rstat & SER_ERROR_MASK) {
		unsigned char data;

		info->last_rx_active_usec = GET_JIFFIES_USEC();
		info->last_rx_active = jiffies;
		/* If we got an error, we must reset it by reading the
		 * data_in field
		 */
		data = info->port[REG_DATA];

		if (!data && (rstat & SER_FRAMING_ERR_MASK)) {
			/* Most likely a break, but we get interrupts over and
			 * over again.
			 */

			if (!info->break_detected_cnt) {
				DEBUG_LOG(info->line, "#BRK start\n", 0);
			}
			if (rstat & SER_RXD_MASK) {
				/* The RX pin is high now, so the break
				 * must be over, but....
				 * we can't really know if we will get another
				 * last byte ending the break or not. 
				 * And we don't know if the byte (if any) will 
				 * have an error or look valid.
				 */
				DEBUG_LOG(info->line, "# BL BRK\n", 0);
				info->errorcode = ERRCODE_INSERT_BREAK;
			}
			info->break_detected_cnt++;
		} else {
			/* The error does not look like a break, but could be
			 * the end of one
			 */
			if (info->break_detected_cnt) {
				DEBUG_LOG(info->line, "EBRK %i\n", info->break_detected_cnt);
				info->errorcode = ERRCODE_INSERT_BREAK;
			} else {
				if (info->errorcode == ERRCODE_INSERT_BREAK)
					add_char_and_flag(info, '\0', TTY_BREAK);

				if (rstat & SER_PAR_ERR_MASK)
					add_char_and_flag(info, data, TTY_PARITY);
				else if (rstat & SER_OVERRUN_MASK)
					add_char_and_flag(info, data, TTY_OVERRUN);
				else if (rstat & SER_FRAMING_ERR_MASK)
					add_char_and_flag(info, data, TTY_FRAME);
				info->errorcode = 0;
			}
			info->break_detected_cnt = 0;
			DEBUG_LOG(info->line, "#iERR s d %04X\n",
			          ((rstat & SER_ERROR_MASK) << 8) | data);
		}
		PROCSTAT(ser_stat[info->line].early_errors_cnt++);
	} else { /* It was a valid byte, now let the DMA do the rest */
		unsigned long curr_time_u = GET_JIFFIES_USEC();
		unsigned long curr_time = jiffies;
		
		if (info->break_detected_cnt) {
			/* Detect if this character is a new valid char or the
			 * last char in a break sequence: If LSBits are 0 and
			 * MSBits are high AND the time is close to the
			 * previous interrupt we should discard it.
			 */
			long elapsed_usec = 
			  (curr_time - info->last_rx_active) * (1000000/HZ) + 
			  curr_time_u - info->last_rx_active_usec;
			if (elapsed_usec < 2*info->char_time_usec) {
				DEBUG_LOG(info->line, "FBRK %i\n", info->line);
				/* Report as BREAK (error) and let
				 * receive_chars() handle it
				 */
				info->errorcode = ERRCODE_SET_BREAK;
			} else {
				DEBUG_LOG(info->line, "Not end of BRK (V)%i\n", info->line);
			}
			DEBUG_LOG(info->line, "num brk %i\n", info->break_detected_cnt);
		}

#ifdef SERIAL_DEBUG_INTR
		printk("** OK, disabling ser_interrupts\n");
#endif
		e100_disable_serial_data_irq(info);

		info->break_detected_cnt = 0;

		PROCSTAT(ser_stat[info->line].ser_ints_ok_cnt++);
		DEBUG_LOG(info->line, "ser_int OK %d\n", info->line);
	}

	/* Restarting the DMA never hurts */
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, restart);
	START_FLUSH_FAST_TIMER(info, "ser_int");
	return IRQ_HANDLED;
} /* handle_ser_interrupt */

static irqreturn_t
ser_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct e100_serial *info;
	int i;
	int handled = 0;

	for (i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;

		if (!info->enabled || !info->uses_dma) 
			continue; 

		/* Which line caused the irq? */
		if (*R_IRQ_MASK1_RD & (1U << (8+2*info->line))) { 
			handled = 1;
			handle_ser_interrupt(info);
		}
	}
	return IRQ_RETVAL(handled);
} /* ser_interrupt */
#endif

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * rs_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */
static void 
do_softint(void *private_)
{
	struct e100_serial	*info = (struct e100_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;
	
	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

static int 
startup(struct e100_serial * info)
{
	unsigned long flags;
	unsigned long xmit_page;
	int i;

	xmit_page = get_zeroed_page(GFP_KERNEL);
	if (!xmit_page)
		return -ENOMEM;

	save_flags(flags); 
	cli();

	/* if it was already initialized, skip this */

	if (info->flags & ASYNC_INITIALIZED) {
		restore_flags(flags);
		free_page(xmit_page);
		return 0;
	}

	if (info->xmit.buf)
		free_page(xmit_page);
	else
		info->xmit.buf = (unsigned char *) xmit_page;

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (xmit_buf 0x%p, recv_buf 0x%p)...\n", info->line, info->xmit.buf, info->recv.buf);
#endif

#ifdef CONFIG_SVINTO_SIM
	/* Bits and pieces collected from below.  Better to have them
	   in one ifdef:ed clause than to mix in a lot of ifdefs,
	   right? */
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);

	info->xmit.head = info->xmit.tail = 0;
	info->first_recv_buffer = info->last_recv_buffer = NULL;
	info->recv_cnt = info->max_recv_cnt = 0;

	for (i = 0; i < SERIAL_RECV_DESCRIPTORS; i++)
		info->rec_descr[i].buf = NULL;

	/* No real action in the simulator, but may set info important
	   to ioctl. */
	change_speed(info);
#else

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	/*
	 * Reset the DMA channels and make sure their interrupts are cleared
	 */

	info->uses_dma = 1;
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	*info->ocmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);

	/* Wait until reset cycle is complete */
	while (IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->icmdadr) ==
	       IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));

	while (IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->ocmdadr) ==
	       IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));

	/* Make sure the irqs are cleared */
	*info->iclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);
	*info->oclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);

	info->xmit.head = info->xmit.tail = 0;
	info->first_recv_buffer = info->last_recv_buffer = NULL;
	info->recv_cnt = info->max_recv_cnt = 0;
	
	for (i = 0; i < SERIAL_RECV_DESCRIPTORS; i++)
		info->rec_descr[i].buf = 0;

	/*
	 * and set the speed and other flags of the serial port
	 * this will start the rx/tx as well
	 */
#ifdef SERIAL_HANDLE_EARLY_ERRORS
	e100_enable_serial_data_irq(info);
#endif	
	change_speed(info);

	/* dummy read to reset any serial errors */

	(void)info->port[REG_DATA];

	/* enable the interrupts */

	e100_enable_txdma_irq(info);
	e100_enable_rxdma_irq(info);

	info->tr_running = 0; /* to be sure we don't lock up the transmitter */

	/* setup the dma input descriptor and start dma */
	
	start_receive(info);
	
	/* for safety, make sure the descriptors last result is 0 bytes written */
	
	info->tr_descr.sw_len = 0;
	info->tr_descr.hw_len = 0;
	info->tr_descr.status = 0;

	/* enable RTS/DTR last */

	e100_rts(info, 1);
	e100_dtr(info, 1);
		
#endif /* CONFIG_SVINTO_SIM */
	
	info->flags |= ASYNC_INITIALIZED;
	
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void 
shutdown(struct e100_serial * info)
{
	unsigned long flags;
	struct etrax_dma_descr *descr = info->rec_descr;
	struct etrax_recv_buffer *buffer;
	int i;

#ifndef CONFIG_SVINTO_SIM	
	/* shut down the transmitter and receiver */

	e100_disable_rx(info);
	info->port[REG_TR_CTRL] = (info->tx_ctrl &= ~0x40);

	e100_disable_rxdma_irq(info);
	e100_disable_txdma_irq(info);

	info->tr_running = 0;

	/* reset both dma channels */

	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	*info->ocmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	info->uses_dma = 0;

#endif /* CONFIG_SVINTO_SIM */

	if (!(info->flags & ASYNC_INITIALIZED))
		return;
	
#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....\n", info->line,
	       info->irq);
#endif
	
	save_flags(flags);
	cli(); /* Disable interrupts */
	
	if (info->xmit.buf) {
		free_page((unsigned long)info->xmit.buf);
		info->xmit.buf = NULL;
	}

	for (i = 0; i < SERIAL_RECV_DESCRIPTORS; i++)
		if (descr[i].buf) {
			buffer = phys_to_virt(descr[i].buf) - sizeof *buffer;
			kfree(buffer);
			descr[i].buf = 0;
		}

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		/* hang up DTR and RTS if HUPCL is enabled */
		e100_dtr(info, 0);
		e100_rts(info, 0); /* could check CRTSCTS before doing this */
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}


/* change baud rate and other assorted parameters */

static void 
change_speed(struct e100_serial *info)
{
	unsigned int cflag;
	unsigned long xoff;

	/* first some safety checks */
	
	if (!info->tty || !info->tty->termios)
		return;
	if (!info->port)
		return;
	
	cflag = info->tty->termios->c_cflag;
	
	/* possibly, the tx/rx should be disabled first to do this safely */
	
	/* change baud-rate and write it to the hardware */
	
	info->baud = cflag_to_baud(cflag);
	
#ifndef CONFIG_SVINTO_SIM
	info->port[REG_BAUD] = cflag_to_etrax_baud(cflag);
	/* start with default settings and then fill in changes */

	/* 8 bit, no/even parity */
	info->rx_ctrl &= ~(IO_MASK(R_SERIAL0_REC_CTRL, rec_bitnr) |
			   IO_MASK(R_SERIAL0_REC_CTRL, rec_par_en) |
			   IO_MASK(R_SERIAL0_REC_CTRL, rec_par));

	/* 8 bit, no/even parity, 1 stop bit, no cts */
	info->tx_ctrl &= ~(IO_MASK(R_SERIAL0_TR_CTRL, tr_bitnr) |
			   IO_MASK(R_SERIAL0_TR_CTRL, tr_par_en) |
			   IO_MASK(R_SERIAL0_TR_CTRL, tr_par) |
			   IO_MASK(R_SERIAL0_TR_CTRL, stop_bits) |
			   IO_MASK(R_SERIAL0_TR_CTRL, auto_cts));
	
	if ((cflag & CSIZE) == CS7) {
		/* set 7 bit mode */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_7bit);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_7bit);
	}
	
	if (cflag & CSTOPB) {
		/* set 2 stop bit mode */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, stop_bits, two_bits);
	}	  
	
	if (cflag & PARENB) {
		/* enable parity */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, enable);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, enable);
	}
	
	if (cflag & CMSPAR) {
		/* enable stick parity */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_stick_par, stick);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_stick_par, stick);
		if (!(cflag & PARODD)) {
			/* set mark parity */
			info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_par, odd);
			info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_par, odd);
		}
	} else {
		if (cflag & PARODD) {
			/* set odd parity */
			info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_par, odd);
			info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_par, odd);
		}
	}
	
	if (cflag & CRTSCTS) {
		/* enable automatic CTS handling */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, auto_cts, active);
	}
	
	/* make sure the tx and rx are enabled */
	
	info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_enable, enable);
	info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable);

	/* actually write the control regs to the hardware */
	
	info->port[REG_TR_CTRL] = info->tx_ctrl;
	info->port[REG_REC_CTRL] = info->rx_ctrl;
	xoff = IO_FIELD(R_SERIAL0_XOFF, xoff_char, STOP_CHAR(info->tty));
	xoff |= IO_STATE(R_SERIAL0_XOFF, tx_stop, enable);
	if (info->tty->termios->c_iflag & IXON ) {
		xoff |= IO_STATE(R_SERIAL0_XOFF, auto_xoff, enable);
	}
	
	*((unsigned long *)&info->port[REG_XOFF]) = xoff;
#endif /* !CONFIG_SVINTO_SIM */

	update_char_time(info);
} /* change_speed */

/* start transmitting chars NOW */

static void 
rs_flush_chars(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (info->tr_running ||
	    info->xmit.head == info->xmit.tail ||
	    tty->stopped ||
	    tty->hw_stopped ||
	    !info->xmit.buf)
		return;

#ifdef SERIAL_DEBUG_FLOW
	printk("rs_flush_chars\n");
#endif
	
	/* this protection might not exactly be necessary here */
	
	save_flags(flags);
	cli();
	start_transmit(info);
	restore_flags(flags);
}

extern inline int 
raw_write(struct tty_struct * tty, int from_user,
	  const unsigned char *buf, int count)
{
	int	c, ret = 0;
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
	
	/* first some sanity checks */
	
	if (!tty || !info->xmit.buf || !tmp_buf)
		return 0;
	
#ifdef SERIAL_DEBUG_DATA
	if (info->line == SERIAL_DEBUG_LINE)
		printk("raw_write (%d), status %d\n", 
		       count, info->port[REG_STATUS]);
#endif

#ifdef CONFIG_SVINTO_SIM
	/* Really simple.  The output is here and now. */
	SIMCOUT(buf, count);
	return count;
#endif
	save_flags(flags);
	
	/* the cli/restore_flags pairs below are needed because the
	 * DMA interrupt handler moves the info->xmit values. the memcpy
	 * needs to be in the critical region unfortunately, because we
	 * need to read xmit values, memcpy, write xmit values in one
	 * atomic operation... this could perhaps be avoided by more clever
	 * design.
	 */
	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			int c1;
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      SERIAL_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			cli();
			c1 = CIRC_SPACE_TO_END(info->xmit.head,
					       info->xmit.tail,
					       SERIAL_XMIT_SIZE);
			if (c1 < c)
				c = c1;
			memcpy(info->xmit.buf + info->xmit.head, tmp_buf, c);
			info->xmit.head = ((info->xmit.head + c) &
					   (SERIAL_XMIT_SIZE-1));
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		cli();	
		while (1) {
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      SERIAL_XMIT_SIZE);

			if (count < c)
				c = count;
			if (c <= 0)
				break;
		
			memcpy(info->xmit.buf + info->xmit.head, buf, c);
			info->xmit.head = (info->xmit.head + c) &
				(SERIAL_XMIT_SIZE-1);
			buf += c;
			count -= c;
			ret += c;
		}
		restore_flags(flags);
	}
	
	/* enable transmitter if not running, unless the tty is stopped
	 * this does not need IRQ protection since if tr_running == 0
	 * the IRQ's are not running anyway for this port.
	 */
	
	if (info->xmit.head != info->xmit.tail &&
	    !tty->stopped &&
	    !tty->hw_stopped &&
	    !info->tr_running) {
		start_transmit(info);
	}
 	
	return ret;
} /* raw_write() */

static int 
rs_write(struct tty_struct * tty, int from_user,
	 const unsigned char *buf, int count)
{
#if defined(CONFIG_ETRAX_RS485)
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	if (info->rs485.enabled)
	{
		/* If we are in RS-485 mode, we need to toggle RTS and disable
		 * the receiver before initiating a DMA transfer
		 */
#ifdef CONFIG_ETRAX_FAST_TIMER
		/* Abort any started timer */
		fast_timers_rs485[info->line].function = NULL;
		del_fast_timer(&fast_timers_rs485[info->line]);
#endif
		e100_rts(info, info->rs485.rts_on_send);
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
		e100_disable_rx(info);
		e100_disable_rxdma_irq(info);
#endif

		if (info->rs485.delay_rts_before_send > 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout((info->rs485.delay_rts_before_send * HZ)/1000);
		}
	}
#endif /* CONFIG_ETRAX_RS485 */

	count = raw_write(tty, from_user, buf, count);

#if defined(CONFIG_ETRAX_RS485)
	if (info->rs485.enabled)
	{
		unsigned int val;
		/* If we are in RS-485 mode the following has to be done:
		 * wait until DMA is ready
		 * wait on transmit shift register
		 * toggle RTS
		 * enable the receiver
		 */	

		/* Sleep until all sent */
		tty_wait_until_sent(tty, 0);
#ifdef CONFIG_ETRAX_FAST_TIMER
		/* Now sleep a little more so that shift register is empty */
		schedule_usleep(info->char_time_usec * 2);
#endif
		/* wait on transmit shift register */
		do{
			get_lsr_info(info, &val);
		}while (!(val & TIOCSER_TEMT));

		e100_rts(info, info->rs485.rts_after_sent);
	
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
		e100_enable_rx(info);
		e100_enable_rxdma_irq(info);
#endif
	}
#endif /* CONFIG_ETRAX_RS485 */

	return count;
} /* rs_write */


/* how much space is available in the xmit buffer? */

static int 
rs_write_room(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	
	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

/* How many chars are in the xmit buffer?
 * This does not include any chars in the transmitter FIFO.
 * Use wait_until_sent for waiting for FIFO drain.
 */

static int 
rs_chars_in_buffer(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

/* discard everything in the xmit buffer */

static void 
rs_flush_buffer(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	info->xmit.head = info->xmit.tail = 0;
	restore_flags(flags);

	wake_up_interruptible(&tty->write_wait);

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 *
 * Since we use DMA we don't check for info->x_char in transmit_chars,
 * just disable DMA channel and write the character when possible.
 */
static void rs_send_xchar(struct tty_struct *tty, char ch)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	e100_disable_txdma_channel(info);

	/* Wait for tr_ready */
	while (!(info->port[REG_STATUS] & IO_MASK(R_SERIAL0_STATUS, tr_ready)))
		/* wait */;

	/* Write the XON/XOFF char */
	info->port[REG_TR_DATA] = ch;

	e100_enable_txdma_channel(info);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void 
rs_throttle(struct tty_struct * tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];

	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	/* Do RTS before XOFF since XOFF might take some time */
	if (tty->termios->c_cflag & CRTSCTS) {
		/* Turn off RTS line (do this atomic) */
		save_flags(flags); 
		cli();
		e100_rts(info, 0);
		restore_flags(flags);
	}
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));

}

static void 
rs_unthrottle(struct tty_struct * tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];

	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	/* Do RTS before XOFF since XOFF might take some time */
	if (tty->termios->c_cflag & CRTSCTS) {
		/* Assert RTS line (do this atomic) */
		save_flags(flags); 
		cli();
		e100_rts(info, 1);
		restore_flags(flags);
	}

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			rs_send_xchar(tty, START_CHAR(tty));
	}

}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int 
get_serial_info(struct e100_serial * info,
		struct serial_struct * retinfo)
{
	struct serial_struct tmp;
	
	/* this is all probably wrong, there are a lot of fields
	 * here that we don't have in e100_serial and maybe we
	 * should set them to something else than 0.
	 */

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = (int)info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int
set_serial_info(struct e100_serial *info,
		struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct e100_serial old_info;
	int retval = 0;

	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;

	old_info = *info;
	
	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		goto check_and_exit;
	}
	
	if (info->count > 1)
		return -EBUSY;
	
	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */
	
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
		       (new_serial.flags & ASYNC_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

 check_and_exit:
	if (info->flags & ASYNC_INITIALIZED) {
		change_speed(info);
	} else
		retval = startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int 
get_lsr_info(struct e100_serial * info, unsigned int *value)
{
	unsigned int result = TIOCSER_TEMT;
#ifndef CONFIG_SVINTO_SIM
	unsigned long curr_time = jiffies;
	unsigned long curr_time_usec = GET_JIFFIES_USEC();
	unsigned long elapsed_usec = 
		(curr_time - info->last_tx_active) * 1000000/HZ + 
		curr_time_usec - info->last_tx_active_usec;

	if (info->xmit.head != info->xmit.tail || 
	    elapsed_usec < 2*info->char_time_usec) {
		result = 0;
	}
#endif

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

#ifdef SERIAL_DEBUG_IO 
struct state_str
{
	int state;
	const char *str;
};

const struct state_str control_state_str[] = {
	{TIOCM_DTR, "DTR" },
	{TIOCM_RTS, "RTS"},
	{TIOCM_ST, "ST?" },
	{TIOCM_SR, "SR?" },
	{TIOCM_CTS, "CTS" },
	{TIOCM_CD, "CD" },
	{TIOCM_RI, "RI" },
	{TIOCM_DSR, "DSR" },
	{0, NULL }
};

char *get_control_state_str(int MLines, char *s)
{
	int i = 0;

	s[0]='\0';
	while (control_state_str[i].str != NULL) {
		if (MLines & control_state_str[i].state) {
			if (s[0] != '\0') {
				strcat(s, ", ");
			}
			strcat(s, control_state_str[i].str);
		}
		i++;
	}
	return s;
}
#endif

static int 
get_modem_info(struct e100_serial * info, unsigned int *value)
{
	unsigned int result;
	/* Polarity isn't verified */
#if 0 /*def SERIAL_DEBUG_IO  */

	printk("get_modem_info: RTS: %i DTR: %i CD: %i RI: %i DSR: %i CTS: %i\n",
	       E100_RTS_GET(info),
	       E100_DTR_GET(info),
	       E100_CD_GET(info),
	       E100_RI_GET(info),
	       E100_DSR_GET(info),
	       E100_CTS_GET(info));
#endif

	result =  
		(!E100_RTS_GET(info) ? TIOCM_RTS : 0)
		| (!E100_DTR_GET(info) ? TIOCM_DTR : 0)
		| (!E100_RI_GET(info) ? TIOCM_RNG : 0)
		| (!E100_DSR_GET(info) ? TIOCM_DSR : 0)
		| (!E100_CD_GET(info) ? TIOCM_CAR : 0)
		| (!E100_CTS_GET(info) ? TIOCM_CTS : 0);

#ifdef SERIAL_DEBUG_IO 
	printk("e100ser: modem state: %i 0x%08X\n", result, result);
	{
		char s[100];
		
		get_control_state_str(result, s);
		printk("state: %s\n", s);
	}
#endif  
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}


static int
set_modem_info(struct e100_serial * info, unsigned int cmd,
	       unsigned int *value)
{
	unsigned int arg;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) {
			e100_rts(info, 1);
		}
		if (arg & TIOCM_DTR) {
			e100_dtr(info, 1);
		}
		/* Handle FEMALE behaviour */
		if (arg & TIOCM_RI) {
			e100_ri_out(info, 1);
		}
		if (arg & TIOCM_CD) {
			e100_cd_out(info, 1);
		}
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			e100_rts(info, 0);
		}
		if (arg & TIOCM_DTR) {
			e100_dtr(info, 0);
		}
		/* Handle FEMALE behaviour */
		if (arg & TIOCM_RI) {
			e100_ri_out(info, 0);
		}
		if (arg & TIOCM_CD) {
			e100_cd_out(info, 0);
		}
		break;
	case TIOCMSET:
		e100_rts(info, arg & TIOCM_RTS);
		e100_dtr(info, arg & TIOCM_DTR);
		/* Handle FEMALE behaviour */
		e100_ri_out(info, arg & TIOCM_RI);
		e100_cd_out(info, arg & TIOCM_CD);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


static void 
rs_break(struct tty_struct *tty, int break_state)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (!info->port)
		return;
	
	save_flags(flags);
	cli();
	if (break_state == -1) {
		/* Go to manual mode and set the txd pin to 0 */
		info->tx_ctrl &= 0x3F; /* Clear bit 7 (txd) and 6 (tr_enable) */
	} else {
		info->tx_ctrl |= (0x80 | 0x40); /* Set bit 7 (txd) and 6 (tr_enable) */
	}
	info->port[REG_TR_CTRL] = info->tx_ctrl;
	restore_flags(flags);
}

static int 
rs_ioctl(struct tty_struct *tty, struct file * file,
	 unsigned int cmd, unsigned long arg)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	
	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}
	
	switch (cmd) {
		case TIOCMGET:
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
		case TIOCGSERIAL:
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			if (copy_to_user((struct e100_serial *) arg,
					 info, sizeof(struct e100_serial)))
				return -EFAULT;
			return 0;

#if defined(CONFIG_ETRAX_RS485)
		case TIOCSERSETRS485:
		{
			struct rs485_control rs485ctrl;
			if (copy_from_user(&rs485ctrl, (struct rs485_control*)arg, sizeof(rs485ctrl)))
				return -EFAULT;

			return e100_enable_rs485(tty, &rs485ctrl);
		}

		case TIOCSERWRRS485:
		{
			struct rs485_write rs485wr;
			if (copy_from_user(&rs485wr, (struct rs485_write*)arg, sizeof(rs485wr)))
				return -EFAULT;

			return e100_write_rs485(tty, 1, rs485wr.outc, rs485wr.outc_size);
		}
#endif
			
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static void 
rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
	
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * S structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void 
rs_close(struct tty_struct *tty, struct file * filp)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (!info)
		return;
  
	/* interrupts are disabled for this entire function */
  
	save_flags(flags); 
	cli();
  
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
  
#ifdef SERIAL_DEBUG_OPEN
	printk("[%d] rs_close ttyS%d, count = %d\n", current->pid, 
	       info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_CRIT
		       "rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_CRIT "rs_close: bad serial port count for ttyS%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the serial receiver and the DMA receive interrupt.
	 */
#ifdef SERIAL_HANDLE_EARLY_ERRORS 
	e100_disable_serial_data_irq(info);
#endif

#ifndef CONFIG_SVINTO_SIM
	e100_disable_rx(info);
	e100_disable_rxdma_irq(info);

	if (info->flags & ASYNC_INITIALIZED) {
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important as we have a transmit FIFO!
		 */
		rs_wait_until_sent(tty, HZ);
	}
#endif

	shutdown(info);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (info->blocked_open) {
		if (info->close_delay) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);

	/* port closed */

#if defined(CONFIG_ETRAX_RS485)
	if (info->rs485.enabled) {
		info->rs485.enabled = 0;
#if defined(CONFIG_ETRAX_RS485_ON_PA)
		*R_PORT_PA_DATA = port_pa_data_shadow &= ~(1 << rs485_pa_bit);
#endif
	}
#endif
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	unsigned long orig_jiffies;
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long curr_time = jiffies;
	unsigned long curr_time_usec = GET_JIFFIES_USEC();
	long elapsed_usec = 
		(curr_time - info->last_tx_active) * (1000000/HZ) + 
		curr_time_usec - info->last_tx_active_usec;

	/*
	 * Check R_DMA_CHx_STATUS bit 0-6=number of available bytes in FIFO
	 * R_DMA_CHx_HWSW bit 31-16=nbr of bytes left in DMA buffer (0=64k)
	 */
	orig_jiffies = jiffies;
	while (info->xmit.head != info->xmit.tail || /* More in send queue */
	       (*info->ostatusadr & 0x007f) ||  /* more in FIFO */
	       (elapsed_usec < 2*info->char_time_usec)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
		curr_time = jiffies;
		curr_time_usec = GET_JIFFIES_USEC();
		elapsed_usec = 
			(curr_time - info->last_tx_active) * (1000000/HZ) + 
			curr_time_usec - info->last_tx_active_usec;
	}
	set_current_state(TASK_RUNNING);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void 
rs_hangup(struct tty_struct *tty)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	
	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int 
block_til_ready(struct tty_struct *tty, struct file * filp,
		struct e100_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long	flags;
	int		retval;
	int		do_clocal = 0, extra_count = 0;
	
	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	
	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	save_flags(flags); 
	cli();
	if (!tty_hung_up_p(filp)) {
		extra_count++;
		info->count--;
	}
	restore_flags(flags);
	info->blocked_open++;
	while (1) {
		save_flags(flags);
		cli();
                /* assert RTS and DTR */
		e100_rts(info, 1);
		e100_dtr(info, 1);
		restore_flags(flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
                if (!(info->flags & ASYNC_CLOSING) && do_clocal)
			/* && (do_clocal || DCD_IS_ASSERTED) */
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyS%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (extra_count)
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened. 
 * It performs the serial-specific initialization for the tty structure.
 */
static int 
rs_open(struct tty_struct *tty, struct file * filp)
{
	struct e100_serial	*info;
	int 			retval, line;
	unsigned long           page;

	/* find which port we want to open */

	line = tty->index;
  
	if (line < 0 || line >= NR_PORTS)
		return -ENODEV;

	/* find the corresponding e100_serial struct in the table */
	info = rs_table + line;

	/* don't allow the opening of ports that are not enabled in the HW config */
	if (!info->enabled)
		return -ENODEV; 
  
#ifdef SERIAL_DEBUG_OPEN
        printk("[%d] rs_open %s, count = %d\n", current->pid, tty->name,
 	       info->count);
#endif

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	if (!tmp_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page) {
			return -ENOMEM;
		}
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}

	/*
	 * If the port is in the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * Start up the serial port
	 */

	retval = startup(info);
	if (retval)
		return retval;
  
	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}
  
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open ttyS%d successful...\n", info->line);
#endif
	return 0;
}

/*
 * /proc fs routines....
 */

extern inline int line_info(char *buf, struct e100_serial *info)
{
	char	stat_buf[30];
	int	ret;

	ret = sprintf(buf, "%d: uart:E100 port:%lX irq:%d",
		      info->line, (unsigned long)info->port, info->irq);

	if (!info->port || (info->type == PORT_UNKNOWN)) {
		ret += sprintf(buf+ret, "\n");
		return ret;
	}

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (!E100_RTS_GET(info))
		strcat(stat_buf, "|RTS");
	if (!E100_CTS_GET(info))
		strcat(stat_buf, "|CTS");
	if (!E100_DTR_GET(info))
		strcat(stat_buf, "|DTR");
	if (!E100_DSR_GET(info))
		strcat(stat_buf, "|DSR");
	if (!E100_CD_GET(info))
		strcat(stat_buf, "|CD");
	if (!E100_RI_GET(info))
		strcat(stat_buf, "|RI");

	ret += sprintf(buf+ret, " baud:%d", info->baud);

	ret += sprintf(buf+ret, " tx:%lu rx:%lu",
		       (unsigned long)info->icount.tx,
		       (unsigned long)info->icount.rx);

	ret += sprintf(buf+ret, " rx_pend:%lu/%lu",
		       (unsigned long)info->recv_cnt,
		       (unsigned long)info->max_recv_cnt);

	if (info->icount.frame)
		ret += sprintf(buf+ret, " fe:%lu",
			       (unsigned long)info->icount.frame);
	
	if (info->icount.parity)
		ret += sprintf(buf+ret, " pe:%lu",
			       (unsigned long)info->icount.parity);
	
	if (info->icount.brk)
		ret += sprintf(buf+ret, " brk:%lu",
			       (unsigned long)info->icount.brk);	

	if (info->icount.overrun)
		ret += sprintf(buf+ret, " oe:%lu",
			       (unsigned long)info->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	ret += sprintf(buf+ret, " %s\n", stat_buf+1);
	return ret;
}

int rs_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int i, len = 0, l;
	off_t	begin = 0;

	len += sprintf(page, "serinfo:1.0 driver:%s\n",
		       serial_version);
	for (i = 0; i < NR_PORTS && len < 4000; i++) {
		if (!rs_table[i].enabled) 
			continue; 
		l = line_info(page + len, &rs_table[i]);
		len += l;
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/* Finally, routines used to initialize the serial driver. */

static void 
show_serial_version(void)
{
	printk(KERN_INFO
	       "ETRAX 100LX serial-driver %s, (c) 2000-2003 Axis Communications AB\r\n",
	       &serial_version[11]); /* "$Revision: x.yy" */
}

/* rs_init inits the driver at boot (using the module_init chain) */

static struct tty_operations rs_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.chars_in_buffer = rs_chars_in_buffer,
	.flush_buffer = rs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,	
        .unthrottle = rs_unthrottle,
	.set_termios = rs_set_termios,
	.stop = rs_stop,
	.start = rs_start,
	.hangup = rs_hangup,
	.break_ctl = rs_break,
	.send_xchar = rs_send_xchar,
	.wait_until_sent = rs_wait_until_sent,
	.read_proc = rs_read_proc,
};

static int __init
rs_init(void)
{
	int i;
	struct e100_serial *info;
	struct tty_driver *driver = alloc_tty_driver(NR_PORTS);

	if (!driver)
		return -ENOMEM;

	show_serial_version();

	/* Setup the timed flush handler system */

#if !defined(CONFIG_ETRAX_SERIAL_FAST_TIMER)
	init_timer(&flush_timer);
	flush_timer.function = timed_flush_handler;
	mod_timer(&flush_timer, jiffies + CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS);
#endif

	/* Initialize the tty_driver structure */
  
	driver->driver_name = "serial";
	driver->name = "ttyS";
	driver->major = TTY_MAJOR;
	driver->minor_start = 64;
	driver->type = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype = SERIAL_TYPE_NORMAL;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_cflag =
		B115200 | CS8 | CREAD | HUPCL | CLOCAL; /* is normally B9600 default... */
	driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	tty_set_operations(driver, &rs_ops);
	if (tty_register_driver(driver))
		panic("Couldn't register serial driver\n");
        serial_driver = driver;
  
	/* do some initializing for the separate ports */
  
	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		info->uses_dma = 0;   
		info->line = i;
		info->tty = 0;
		info->type = PORT_ETRAX;
		info->tr_running = 0;
		info->forced_eop = 0;
		info->flags = 0;
		info->close_delay = 5*HZ/10;
		info->closing_wait = 30*HZ;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->xmit.buf = NULL;
		info->xmit.tail = info->xmit.head = 0;
		info->first_recv_buffer = info->last_recv_buffer = NULL;
		info->recv_cnt = info->max_recv_cnt = 0;
		info->last_tx_active_usec = 0;
		info->last_tx_active = 0;

#if defined(CONFIG_ETRAX_RS485)
		/* Set sane defaults */
		info->rs485.rts_on_send = 0;
		info->rs485.rts_after_sent = 1;
		info->rs485.delay_rts_before_send = 0;
		info->rs485.enabled = 0;
#endif
		INIT_WORK(&info->work, do_softint, info);

		if (info->enabled) {
			printk(KERN_INFO "%s%d at 0x%x is a builtin UART with DMA\n",
			       serial_driver->name, info->line, (unsigned int)info->port);
		}
	}
#ifdef CONFIG_ETRAX_FAST_TIMER
#ifdef CONFIG_ETRAX_SERIAL_FAST_TIMER
	memset(fast_timers, 0, sizeof(fast_timers));
#endif
#ifdef CONFIG_ETRAX_RS485
	memset(fast_timers_rs485, 0, sizeof(fast_timers_rs485));
#endif
	fast_timer_init();
#endif

#ifndef CONFIG_SVINTO_SIM
	/* Not needed in simulator.  May only complicate stuff. */
	/* hook the irq's for DMA channel 6 and 7, serial output and input, and some more... */
#ifdef CONFIG_ETRAX_SERIAL_PORT0
	if (request_irq(SER0_DMA_TX_IRQ_NBR, tr_interrupt, SA_INTERRUPT, "serial 0 dma tr", NULL))
		panic("irq22");
	if (request_irq(SER0_DMA_RX_IRQ_NBR, rec_interrupt, SA_INTERRUPT, "serial 0 dma rec", NULL))
		panic("irq23");
#endif
#ifdef SERIAL_HANDLE_EARLY_ERRORS
	if (request_irq(SERIAL_IRQ_NBR, ser_interrupt, SA_SHIRQ | SA_INTERRUPT, "serial ", NULL))
		panic("irq8");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT1
	if (request_irq(SER1_DMA_TX_IRQ_NBR, tr_interrupt, SA_INTERRUPT, "serial 1 dma tr", NULL))
		panic("irq24");
	if (request_irq(SER1_DMA_RX_IRQ_NBR, rec_interrupt, SA_INTERRUPT, "serial 1 dma rec", NULL))
		panic("irq25");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT2
	/* DMA Shared with par0 (and SCSI0 and ATA) */
	if (request_irq(SER2_DMA_TX_IRQ_NBR, tr_interrupt, SA_SHIRQ, "serial 2 dma tr", NULL))
		panic("irq18");
	if (request_irq(SER2_DMA_RX_IRQ_NBR, rec_interrupt, SA_SHIRQ, "serial 2 dma rec", NULL))
		panic("irq19");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT3
	/* DMA Shared with par1 (and SCSI1 and Extern DMA 0) */
	if (request_irq(SER3_DMA_TX_IRQ_NBR, tr_interrupt, SA_SHIRQ, "serial 3 dma tr", NULL))
		panic("irq20");
	if (request_irq(SER3_DMA_RX_IRQ_NBR, rec_interrupt, SA_SHIRQ, "serial 3 dma rec", NULL))
		panic("irq21");
#endif

#endif /* CONFIG_SVINTO_SIM */

	return 0;
}

/* this makes sure that rs_init is called during kernel boot */

module_init(rs_init);

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
int 
register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
}
