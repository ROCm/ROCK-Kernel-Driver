/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * This file contains a module version of the ioc4 serial driver. This
 * includes all the support functions needed (support functions, etc.)
 * and the serial driver itself.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/sn/types.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/serial_reg.h>
#include <linux/module.h>
#include <asm/serial.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/driver.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/atomic.h>
#include <asm/delay.h>
#include <asm/semaphore.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/io.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/pci/pciio.h>
#include <linux/pci.h>
#include <asm/sn/ioc4.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/sn2/sn_private.h>
#include <asm/sn/serialio.h>

/* #define DEBUG_INTERRUPTS */
#define SUPPORT_ATOMICS

/* No serial interrupts unless we have SGI_IOC4_SERIAL turned on */
extern void ioc4_ss_connect_interrupt(int, void *, void *);
extern int ioc4_serial_attach(vertex_hdl_t, void *);

/* forward declaration */
static irqreturn_t ioc4_intr(int, void *, struct pt_regs *);


#ifdef SUPPORT_ATOMICS
/*
 * support routines for local atomic operations.
 */

static spinlock_t local_lock;

static inline unsigned int atomicSetInt(atomic_t * a, unsigned int b)
{
	unsigned long s;
	unsigned int ret, new;

	spin_lock_irqsave(&local_lock, s);
	new = ret = atomic_read(a);
	new |= b;
	atomic_set(a, new);
	spin_unlock_irqrestore(&local_lock, s);

	return ret;
}

static unsigned int atomicClearInt(atomic_t * a, unsigned int b)
{
	unsigned long s;
	unsigned int ret, new;

	spin_lock_irqsave(&local_lock, s);
	new = ret = atomic_read(a);
	new &= ~b;
	atomic_set(a, new);
	spin_unlock_irqrestore(&local_lock, s);

	return ret;
}

#else

#define atomicAddInt(a,b)	*(a) += ((unsigned int)(b))

static inline unsigned int atomicSetInt(unsigned int *a, unsigned int b)
{
	unsigned int ret = *a;

	*a |= b;
	return ret;
}

#define atomicSetUint64(a,b)	*(a) |= ((unsigned long long )(b))

static inline unsigned int atomicClearInt(unsigned int *a, unsigned int b)
{
	unsigned int ret = *a;

	*a &= ~b;
	return ret;
}

#define atomicClearUint64(a,b)	*(a) &= ~((unsigned long long)(b))
#endif				/* SUPPORT_ATOMICS */

/* pci device struct */
static const struct pci_device_id __devinitdata ioc4_s_id_table[] = {
	{IOC4_VENDOR_ID_NUM, IOC4_DEVICE_ID_NUM, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 0},
	{0, 0, 0, 0, 0, 0, 0}
};

static int __devinit ioc4_attach(struct pci_dev *,
				 const struct pci_device_id *);

struct pci_driver ioc4_s_driver = {
	name:"IOC4 Serial",
	id_table:ioc4_s_id_table,
	probe:ioc4_attach,
};

static int __init ioc4_serial_detect(void)
{
	int rc;

	rc = pci_register_driver(&ioc4_s_driver);
	return 0;
}

/*
 * per-IOC4 data structure
 */
typedef struct ioc4_soft_s {
	vertex_hdl_t is_ioc4_vhdl;
	vertex_hdl_t is_conn_vhdl;

	struct pci_dev *is_pci_dev;
	ioc4_mem_t *is_ioc4_mem;

	/* Each interrupt type has an entry in the array */
	struct ioc4_intr_type {

		/*
		 * Each in-use entry in this array contains at least
		 * one nonzero bit in sd_bits; no two entries in this
		 * array have overlapping sd_bits values.
		 */
#define MAX_IOC4_INTR_ENTS	(8 * sizeof(ioc4reg_t))
		struct ioc4_intr_info {
			ioc4reg_t sd_bits;
			ioc4_intr_func_f *sd_intr;
			intr_arg_t sd_info;
			vertex_hdl_t sd_vhdl;
			struct ioc4_soft_s *sd_soft;
		} is_intr_info[MAX_IOC4_INTR_ENTS];

		/* Number of entries active in the above array */
		atomic_t is_num_intrs;
		atomic_t is_intr_bits_busy;	/* Bits assigned */
		atomic_t is_intr_ents_free;	/* Free active entries mask */
	} is_intr_type[ioc4_num_intr_types];

	/* is_ir_lock must be held while
	 * modifying sio_ie values, so
	 * we can be sure that sio_ie is
	 * not changing when we read it
	 * along with sio_ir.
	 */
	spinlock_t is_ir_lock;	/* SIO_IE[SC] mod lock */
} ioc4_soft_t;

#define ioc4_soft_set(v,i)	hwgraph_fastinfo_set((v), (arbitrary_info_t)(i))
#define ioc4_soft_get(v)	((ioc4_soft_t *)hwgraph_fastinfo_get(v))

#define SER_DIVISOR(x, clk)		(((clk) + (x) * 8) / ((x) * 16))
#define DIVISOR_TO_BAUD(div, clk)	((clk) / 16 / (div))

/* Some masks */
#define LCR_MASK_BITS_CHAR	(UART_LCR_WLEN5 | UART_LCR_WLEN6 | UART_LCR_WLEN7 | UART_LCR_WLEN8)
#define LCR_MASK_STOP_BITS	(UART_LCR_STOP)

/* #define IOC4_SIO_DEBUG */
/* define USE_64BIT_DMA */

#define PENDING(port) (PCI_INW(&(port)->ip_ioc4->sio_ir) & port->ip_ienb)

/* Default to 4k buffers */
#ifdef IOC4_1K_BUFFERS
#define RING_BUF_SIZE 1024
#define IOC4_BUF_SIZE_BIT 0
#define PROD_CONS_MASK IOC4_PROD_CONS_PTR_1K
#else
#define RING_BUF_SIZE 4096
#define IOC4_BUF_SIZE_BIT IOC4_SBBR_L_SIZE
#define PROD_CONS_MASK IOC4_PROD_CONS_PTR_4K
#endif

#define TOTAL_RING_BUF_SIZE (RING_BUF_SIZE * 4)

#if PAGE_SIZE < TOTAL_RING_BUF_SIZE
#include <sys/pfdat.h>
#endif

#ifdef DPRINTF
#define dprintf(x) printk x
#else
#define dprintf(x)
#endif

#define	contig_memalloc(a,b,c)	kmem_zalloc(PAGE_SIZE * (a))

#define KM_PHYSCONTIG   0x0008
#define VM_DIRECT       KM_PHYSCONTIG
#define VM_PHYSCONTIG   KM_PHYSCONTIG

#ifdef DEBUG
#define PROGRESS()	printk("%s : %d\n", __FUNCTION__, __LINE__)
#define NOT_PROGRESS()	printk("%s : %d - Error\n", __FUNCTION__, __LINE__)
#else
#define PROGRESS()	;
#define NOT_PROGRESS()	;
#endif

static int Active_port_count;	/* The number of active ports on the IOC4 */

static inline void *kvpalloc(size_t size, int flags, int colour)
{
	if (flags & (VM_DIRECT | VM_PHYSCONTIG)) {
		int order = 0;
		while ((PAGE_SIZE << order) < (size << PAGE_SHIFT))
			order++;
		return (void *)__get_free_pages(GFP_KERNEL, order);
	} else
		return vmalloc(size << PAGE_SHIFT);
}

/* Local port info for the IOC4 serial ports.  This contains as its
 * first member the global sio port private data.
 */
typedef struct ioc4port {
	sioport_t ip_sioport;	/* Must be first struct entry! */

	vertex_hdl_t ip_conn_vhdl;	/* vhdl to use for pciio requests */
	vertex_hdl_t ip_port_vhdl;	/* vhdl for the serial port */

	/* Base piomap addr of the ioc4 board this port is on
	 * and associated serial map;  serial map includes uart registers.
	 */
	ioc4_mem_t *ip_ioc4;
	ioc4_sregs_t *ip_serial;
	ioc4_uart_t *ip_uart;

	/* Ring buffer page for this port */
	caddr_t ip_ring_buf_k0;	/* Ring buffer location in K0 space */

	/* Rings for this port */
	struct ring *ip_inring;
	struct ring *ip_outring;

	/* Hook to port specific values for this port */
	struct hooks *ip_hooks;

	int ip_flags;

	/* Cache of DCD/CTS bits last received */
	char ip_modem_bits;

	/* Various rx/tx parameters */
	int ip_baud;
	int ip_tx_lowat;
	int ip_rx_timeout;

	/* Copy of notification bits */
	int ip_notify;

	/* Shadow copies of various registers so we don't need to PIO
	 * read them constantly
	 */
	ioc4reg_t ip_ienb;	/* Enabled interrupts */

	ioc4reg_t ip_sscr;

	ioc4reg_t ip_tx_prod;
	ioc4reg_t ip_rx_cons;

	/* Back pointer to ioc4 soft area */
	void *ip_ioc4_soft;
} ioc4port_t;

#if DEBUG
#define     MAXSAVEPORT 256
static int next_saveport = 0;
static ioc4port_t *saveport[MAXSAVEPORT];
#endif

/* TX low water mark.  We need to notify the driver whenever TX is getting
 * close to empty so it can refill the TX buffer and keep things going.
 * Let's assume that if we interrupt 1 ms before the TX goes idle, we'll
 * have no trouble getting in more chars in time (I certainly hope so).
 */
#define TX_LOWAT_LATENCY      1000
#define TX_LOWAT_HZ          (1000000 / TX_LOWAT_LATENCY)
#define TX_LOWAT_CHARS(baud) (baud / 10 / TX_LOWAT_HZ)

/* Flags per port */
#define INPUT_HIGH	0x01
#define DCD_ON		0x02
#define LOWAT_WRITTEN	0x04
#define READ_ABORTED	0x08
#define TX_DISABLED	0x10

/* Get local port type from global sio port type */
#define LPORT(port) ((ioc4port_t *) (port))

/* Get global port from local port type */
#define GPORT(port) ((sioport_t *) (port))

/* Since each port has different register offsets and bitmasks
 * for everything, we'll store those that we need in tables so we
 * don't have to be constantly checking the port we are dealing with.
 */
struct hooks {
	ioc4reg_t intr_delta_dcd;
	ioc4reg_t intr_delta_cts;
	ioc4reg_t intr_tx_mt;
	ioc4reg_t intr_rx_timer;
	ioc4reg_t intr_rx_high;
	ioc4reg_t intr_tx_explicit;
	ioc4reg_t intr_dma_error;
	ioc4reg_t intr_clear;
	ioc4reg_t intr_all;
	char rs422_select_pin;
};

static struct hooks hooks_array[4] = {
	/* Values for port 0 */
	{
	 IOC4_SIO_IR_S0_DELTA_DCD,
	 IOC4_SIO_IR_S0_DELTA_CTS,
	 IOC4_SIO_IR_S0_TX_MT,
	 IOC4_SIO_IR_S0_RX_TIMER,
	 IOC4_SIO_IR_S0_RX_HIGH,
	 IOC4_SIO_IR_S0_TX_EXPLICIT,
	 IOC4_OTHER_IR_S0_MEMERR,
	 (IOC4_SIO_IR_S0_TX_MT | IOC4_SIO_IR_S0_RX_FULL |
	  IOC4_SIO_IR_S0_RX_HIGH | IOC4_SIO_IR_S0_RX_TIMER |
	  IOC4_SIO_IR_S0_DELTA_DCD | IOC4_SIO_IR_S0_DELTA_CTS |
	  IOC4_SIO_IR_S0_INT | IOC4_SIO_IR_S0_TX_EXPLICIT),
	 IOC4_SIO_IR_S0,
	 IOC4_GPPR_UART0_MODESEL_PIN,
	 },

	/* Values for port 1 */
	{
	 IOC4_SIO_IR_S1_DELTA_DCD,
	 IOC4_SIO_IR_S1_DELTA_CTS,
	 IOC4_SIO_IR_S1_TX_MT,
	 IOC4_SIO_IR_S1_RX_TIMER,
	 IOC4_SIO_IR_S1_RX_HIGH,
	 IOC4_SIO_IR_S1_TX_EXPLICIT,
	 IOC4_OTHER_IR_S1_MEMERR,
	 (IOC4_SIO_IR_S1_TX_MT | IOC4_SIO_IR_S1_RX_FULL |
	  IOC4_SIO_IR_S1_RX_HIGH | IOC4_SIO_IR_S1_RX_TIMER |
	  IOC4_SIO_IR_S1_DELTA_DCD | IOC4_SIO_IR_S1_DELTA_CTS |
	  IOC4_SIO_IR_S1_INT | IOC4_SIO_IR_S1_TX_EXPLICIT),
	 IOC4_SIO_IR_S1,
	 IOC4_GPPR_UART1_MODESEL_PIN,
	 },

	/* Values for port 2 */
	{
	 IOC4_SIO_IR_S2_DELTA_DCD,
	 IOC4_SIO_IR_S2_DELTA_CTS,
	 IOC4_SIO_IR_S2_TX_MT,
	 IOC4_SIO_IR_S2_RX_TIMER,
	 IOC4_SIO_IR_S2_RX_HIGH,
	 IOC4_SIO_IR_S2_TX_EXPLICIT,
	 IOC4_OTHER_IR_S2_MEMERR,
	 (IOC4_SIO_IR_S2_TX_MT | IOC4_SIO_IR_S2_RX_FULL |
	  IOC4_SIO_IR_S2_RX_HIGH | IOC4_SIO_IR_S2_RX_TIMER |
	  IOC4_SIO_IR_S2_DELTA_DCD | IOC4_SIO_IR_S2_DELTA_CTS |
	  IOC4_SIO_IR_S2_INT | IOC4_SIO_IR_S2_TX_EXPLICIT),
	 IOC4_SIO_IR_S2,
	 IOC4_GPPR_UART2_MODESEL_PIN,
	 },

	/* Values for port 3 */
	{
	 IOC4_SIO_IR_S3_DELTA_DCD,
	 IOC4_SIO_IR_S3_DELTA_CTS,
	 IOC4_SIO_IR_S3_TX_MT,
	 IOC4_SIO_IR_S3_RX_TIMER,
	 IOC4_SIO_IR_S3_RX_HIGH,
	 IOC4_SIO_IR_S3_TX_EXPLICIT,
	 IOC4_OTHER_IR_S3_MEMERR,
	 (IOC4_SIO_IR_S3_TX_MT | IOC4_SIO_IR_S3_RX_FULL |
	  IOC4_SIO_IR_S3_RX_HIGH | IOC4_SIO_IR_S3_RX_TIMER |
	  IOC4_SIO_IR_S3_DELTA_DCD | IOC4_SIO_IR_S3_DELTA_CTS |
	  IOC4_SIO_IR_S3_INT | IOC4_SIO_IR_S3_TX_EXPLICIT),
	 IOC4_SIO_IR_S3,
	 IOC4_GPPR_UART3_MODESEL_PIN,
	 }
};

/* Macros to get into the port hooks.  Require a variable called
 * hooks set to port->hooks
 */
#define H_INTR_TX_MT	   hooks->intr_tx_mt
#define H_INTR_RX_TIMER    hooks->intr_rx_timer
#define H_INTR_RX_HIGH	   hooks->intr_rx_high
#define H_INTR_TX_EXPLICIT hooks->intr_tx_explicit
#define H_INTR_DMA_ERROR   hooks->intr_dma_error
#define H_INTR_CLEAR	   hooks->intr_clear
#define H_INTR_DELTA_DCD   hooks->intr_delta_dcd
#define H_INTR_DELTA_CTS   hooks->intr_delta_cts
#define H_INTR_ALL	   hooks->intr_all
#define H_RS422		   hooks->rs422_select_pin

/* A ring buffer entry */
struct ring_entry {
	union {
		struct {
			uint32_t alldata;
			uint32_t allsc;
		} all;
		struct {
			char data[4];	/* data bytes */
			char sc[4];	/* status/control */
		} s;
	} u;
};

/* Test the valid bits in any of the 4 sc chars using "allsc" member */
#define RING_ANY_VALID \
	((uint32_t) (IOC4_RXSB_MODEM_VALID | IOC4_RXSB_DATA_VALID) * 0x01010101)

#define ring_sc     u.s.sc
#define ring_data   u.s.data
#define ring_allsc  u.all.allsc

/* Number of entries per ring buffer. */
#define ENTRIES_PER_RING (RING_BUF_SIZE / (int) sizeof(struct ring_entry))

/* An individual ring */
struct ring {
	struct ring_entry entries[ENTRIES_PER_RING];
};

/* The whole enchilada */
struct ring_buffer {
	struct ring TX_0_OR_2;
	struct ring RX_0_OR_2;
	struct ring TX_1_OR_3;
	struct ring RX_1_OR_3;
};

/* Get a ring from a port struct */
#define RING(port, which) \
    &(((struct ring_buffer *) ((port)->ip_ring_buf_k0))->which)

/* Local functions: */
static int ioc4_open(sioport_t * port);
static int ioc4_config(sioport_t * port, int baud, int byte_size,
		       int stop_bits, int parenb, int parodd);
static int ioc4_enable_hfc(sioport_t * port, int enable);

/* Data transmission */
static int do_ioc4_write(sioport_t * port, char *buf, int len);
static int ioc4_write(sioport_t * port, char *buf, int len);
static int ioc4_break(sioport_t * port, int brk);
static int ioc4_enable_tx(sioport_t * port, int enb);

/* Data reception */
static int ioc4_read(sioport_t * port, char *buf, int len);

/* Event notification */
static int ioc4_notification(sioport_t * port, int mask, int on);
static int ioc4_rx_timeout(sioport_t * port, int timeout);

/* Modem control */
static int ioc4_set_DTR(sioport_t * port, int dtr);
static int ioc4_set_RTS(sioport_t * port, int rts);
static int ioc4_query_DCD(sioport_t * port);
static int ioc4_query_CTS(sioport_t * port);

/* Output mode */
static int ioc4_set_proto(sioport_t * port, enum sio_proto proto);

static struct serial_calldown ioc4_calldown = {
	ioc4_open,
	ioc4_config,
	ioc4_enable_hfc,
	ioc4_write,
	ioc4_break,
	ioc4_enable_tx,
	ioc4_read,
	ioc4_notification,
	ioc4_rx_timeout,
	ioc4_set_DTR,
	ioc4_set_RTS,
	ioc4_query_DCD,
	ioc4_query_CTS,
	ioc4_set_proto,
};

/* Baud rate stuff */
#define SET_BAUD(p, b) set_baud_ti(p, b)
static int set_baud_ti(ioc4port_t *, int);

#ifdef DEBUG
/* Performance characterization logging */
#define DEBUGINC(x,i) stats.x += i

static struct {

	/* Ports present */
	uint ports;

	/* Ports killed */
	uint killed;

	/* Interrupt counts */
	uint total_intr;
	uint port_0_intr;
	uint port_1_intr;
	uint ddcd_intr;
	uint dcts_intr;
	uint rx_timer_intr;
	uint rx_high_intr;
	uint explicit_intr;
	uint mt_intr;
	uint mt_lowat_intr;

	/* Write characteristics */
	uint write_bytes;
	uint write_cnt;
	uint wrote_bytes;
	uint tx_buf_used;
	uint tx_buf_cnt;
	uint tx_pio_cnt;
	/* Read characteristics */
	uint read_bytes;
	uint read_cnt;
	uint drain;
	uint drainwait;
	uint resetdma;
	uint read_ddcd;
	uint rx_overrun;
	uint parity;
	uint framing;
	uint brk;
	uint red_bytes;
	uint rx_buf_used;
	uint rx_buf_cnt;

	/* Errors */
	uint dma_lost;
	uint read_aborted;
	uint read_aborted_detected;
} stats;

#else
#define DEBUGINC(x,i)
#endif

/* Infinite loop detection.
 */
#define MAXITER 1000000
#define SPIN(cond, success) \
{ \
	 int spiniter = 0; \
	 success = 1; \
	 while(cond) { \
		 spiniter++; \
		 if (spiniter > MAXITER) { \
			 success = 0; \
			 break; \
		 } \
	 } \
}

#define ENABLE_FLOW_CONTROL
#define ENABLE_OUTPUT_INTERRUPTS
#define TTY_RESTART 0

/* defining this will get you LOTS of great debug info */
/* #define IOC4_DEBUG_TRACE */
/* #define IOC4_DEBUG_TRACE_FLOW_CONTROL */
/* #define IOC4_DEBUG_TRACE_PROGRESS */
/* #define TRACE_NOTIFICATION_FUNCS */

/* defining this will force the driver to run in polled mode */
/* #define POLLING_FOR_CHARACTERS */

/* number of serial ports on the ioc4 */
#define IOC4_NUM_SERIAL_PORTS	4

/* lower level interface struct */

typedef struct ioc4_control_struct {
	/* the all important port pointer */
	sioport_t *ic_sioport;

	/* Handy reference material */
	struct tty_struct *ic_tty;
	struct async_struct *ic_info;
	int ic_irq;		/* copy of state->irq */
	spinlock_t ic_lock;	/* port lock */
} ioc4_control_t;

/* callback func protos */
static void ioc4_cb_data_ready(sioport_t *);
#ifdef ENABLE_OUTPUT_INTERRUPTS
static void ioc4_cb_output_lowat(sioport_t *);
#endif
static void ioc4_cb_post_ncs(sioport_t *, int);
static void ioc4_cb_ddcd(sioport_t *, int);
static void ioc4_cb_dcts(sioport_t *, int);
static void ioc4_cb_detach(sioport_t *);

/* callup vector for this layer */
static struct serial_callup ioc4_cb_callup = {
	ioc4_cb_data_ready,
#ifdef ENABLE_OUTPUT_INTERRUPTS
	ioc4_cb_output_lowat,
#else
	0,
#endif				/* ENABLE_OUTPUT_INTERRUPTS */
	ioc4_cb_post_ncs,
	ioc4_cb_ddcd,
	ioc4_cb_dcts,
	ioc4_cb_detach
};

/* one of these per port - to keep track of the important port things */
static ioc4_control_t IOC4_control[IOC4_NUM_SERIAL_PORTS];

/* state table */
static struct serial_state IOC4_table[IOC4_NUM_SERIAL_PORTS];

/* some version information */
#define DEVICE_NAME "SGI Altix IOC4 Serial driver"

/* IOC4_irq_lock will be used to protect updating IOC4_irq_list */
static spinlock_t IOC4_irq_lock = SPIN_LOCK_UNLOCKED;
static struct async_struct *IOC4_irq_list[IOC4_NUM_SERIAL_PORTS];

/* used to single thread within the driver - mostly init/fini */
static spinlock_t IOC4_lock = SPIN_LOCK_UNLOCKED;

static struct timer_list IOC4_timer_list;

static unsigned char *IOC4_tmp_buffer;
#ifdef DECLARE_MUTEX
static DECLARE_MUTEX(IOC4_tmp_sem);
#else
static struct semaphore IOC4_tmp_sem = MUTEX;
#endif

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS	256

/* number of characters we want to transmit to the lower level at a time */
#define IOC4_MAX_CHARS	128

/* event types for our task queue -- so far just one */
#define IOC4_EVENT_WRITE_WAKEUP	0

#ifdef POLLING_FOR_CHARACTERS
#define TIMER_WAIT_TIME	(HZ/10)
#else
#define TIMER_WAIT_TIME	(60*HZ)	// (2*HZ/100)
#endif

/* Prototypes */

static void ioc4_shutdown(struct async_struct *);
static int ioc4_ss_startup(ioc4_control_t *);

/* =====================================================================
 *    Function Table of Contents
 */

/* The IOC4 hardware provides no atomic way to determine if interrupts
 * are pending since two reads are required to do so.  The handler must
 * read the SIO_IR and the SIO_IES, and take the logical and of the
 * two.  When this value is zero, all interrupts have been serviced and
 * the handler may return.
 *
 * This has the unfortunate "hole" that, if some other CPU or
 * some other thread or some higher level interrupt manages to
 * modify SIO_IE between our reads of SIO_IR and SIO_IE, we may
 * think we have observed SIO_IR&SIO_IE==0 when in fact this
 * condition never really occurred.
 *
 * To solve this, we use a simple spinlock that must be held
 * whenever modifying SIO_IE; holding this lock while observing
 * both SIO_IR and SIO_IE guarantees that we do not falsely
 * conclude that no enabled interrupts are pending.
 */

static void
ioc4_write_ireg(void *ioc4_soft, ioc4reg_t val, int which,
		ioc4_intr_type_t type)
{
	ioc4_mem_t *mem = ((ioc4_soft_t *) ioc4_soft)->is_ioc4_mem;
	spinlock_t *lp = &((ioc4_soft_t *) ioc4_soft)->is_ir_lock;
	unsigned long s;

	spin_lock_irqsave(lp, s);

	switch (type) {
	case ioc4_sio_intr_type:
		switch (which) {
		case IOC4_W_IES:
			mem->sio_ies_ro = val;
			break;

		case IOC4_W_IEC:
			mem->sio_iec_ro = val;
			break;
		}
		break;

	case ioc4_other_intr_type:
		switch (which) {
		case IOC4_W_IES:
			mem->other_ies_ro = val;
			break;

		case IOC4_W_IEC:
			mem->other_iec_ro = val;
			break;
		}
		break;

	case ioc4_num_intr_types:
		break;
	}
	spin_unlock_irqrestore(lp, s);
}

static inline ioc4reg_t
ioc4_pending_intrs(ioc4_soft_t * ioc4_soft, ioc4_intr_type_t type)
{
	ioc4_mem_t *mem = ioc4_soft->is_ioc4_mem;
	spinlock_t *lp = &ioc4_soft->is_ir_lock;
	unsigned long s;
	ioc4reg_t intrs = (ioc4reg_t) 0;

	ASSERT((type == ioc4_sio_intr_type) || (type == ioc4_other_intr_type));

	spin_lock_irqsave(lp, s);

	switch (type) {
	case ioc4_sio_intr_type:
		intrs = mem->sio_ir & mem->sio_ies_ro;
		break;

	case ioc4_other_intr_type:
		intrs = mem->other_ir & mem->other_ies_ro;

		/* Don't process any ATA interrupte, leave them for the ATA driver */
		intrs &= ~(IOC4_OTHER_IR_ATA_INT | IOC4_OTHER_IR_ATA_MEMERR);
		break;

	case ioc4_num_intr_types:
		break;
	}

	spin_unlock_irqrestore(lp, s);
	return intrs;
}

static int __devinit
ioc4_attach(struct pci_dev *pci_handle, const struct pci_device_id *pci_id)
{
	ioc4_mem_t *mem;
	 /*REFERENCED*/ graph_error_t rc;
	vertex_hdl_t ioc4_vhdl;
	ioc4_soft_t *soft;
	vertex_hdl_t conn_vhdl = PCIDEV_VERTEX(pci_handle);
	int tmp;
	extern pciio_endian_t snia_pciio_endian_set(struct pci_dev *,
						    pciio_endian_t,
						    pciio_endian_t);

	if (pci_enable_device(pci_handle)) {
		printk
		    ("ioc4_attach: Failed to enable device with pci_dev 0x%p... returning\n",
		     (void *)pci_handle);
		return -1;
	}

	pci_set_master(pci_handle);
	snia_pciio_endian_set(pci_handle, PCIDMA_ENDIAN_LITTLE,
			      PCIDMA_ENDIAN_BIG);

	/*
	 * Get PIO mappings through our "primary"
	 * connection point to the IOC4's CFG and
	 * MEM spaces.
	 */

	/*
	 * Map in the ioc4 memory - we'll do config accesses thru the pci_????() interfaces.
	 */

	mem = (ioc4_mem_t *) pci_resource_start(pci_handle, 0);
	if (!mem) {
		printk(KERN_ALERT "%p/" EDGE_LBL_IOC4
		       ": unable to get PIO mapping for my MEM space\n",
		       (void *)pci_handle);
		return -1;
	}

	if (!request_region((unsigned long)mem, sizeof(*mem), "sioc4_mem")) {
		printk(KERN_ALERT
		       "%p/" EDGE_LBL_IOC4
		       ": unable to get request region for my MEM space\n",
		       (void *)pci_handle);
		return -1;
	}

	/*
	 * Create the "ioc4" vertex which hangs off of
	 * the connect points.
	 * This code is slightly paranoid.
	 */
	rc = hwgraph_path_add(conn_vhdl, EDGE_LBL_IOC4, &ioc4_vhdl);
	ASSERT(rc == GRAPH_SUCCESS);

	/*
	 * Allocate the soft structure, fill it in a bit,
	 * and attach it to the ioc4 vertex.
	 */
	soft = kmalloc(sizeof(*(soft)), GFP_KERNEL);
	if (!soft) {
		printk(KERN_ALERT
		       "%p/" EDGE_LBL_IOC4
		       ": unable to get memory for the soft struct\n",
		       (void *)pci_handle);
		return -1;
	}
	memset(soft, 0, sizeof(*(soft)));

	spin_lock_init(&soft->is_ir_lock);
	soft->is_ioc4_vhdl = ioc4_vhdl;
	soft->is_conn_vhdl = conn_vhdl;
	soft->is_ioc4_mem = mem;
	soft->is_pci_dev = pci_handle;

	ioc4_soft_set(ioc4_vhdl, soft);

	/* Init the IOC4 */

	/* SN boot PROMs allocate the PCI
	 * space and set up the pci_addr fields.
	 * Other systems need to set the base address.
	 * This is handled automatically if the PCI infrastructure
	 * is used.
	 *
	 * No need to set the latency timer since the PCI
	 * infrastructure sets it to 1 us.
	 */

	pci_read_config_dword(pci_handle, IOC4_PCI_SCR, &tmp);

	pci_write_config_dword(pci_handle, IOC4_PCI_SCR,
			       tmp | PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE |
			       PCI_CMD_PAR_ERR_RESP | PCI_CMD_SERR_ENABLE);

	PCI_OUTW(&mem->sio_cr, (0xf << IOC4_SIO_CR_CMD_PULSE_SHIFT));

	/* Enable serial port mode select generic PIO pins as outputs */
	PCI_OUTW(&mem->gpcr_s,
		 IOC4_GPCR_UART0_MODESEL | IOC4_GPCR_UART1_MODESEL);

	/* Clear and disable all interrupts */
	IOC4_WRITE_IEC(soft, ~0, ioc4_sio_intr_type);
	PCI_OUTW(&mem->sio_ir, ~0);

	IOC4_WRITE_IEC(soft, ~0, ioc4_other_intr_type);
	PCI_OUTW(&mem->other_ir, ~0);

	/*
	 * Alloc the IOC4 intr before attaching the subdevs, so the
	 * cpu handling the IOC4 intr is known (for setmustrun on
	 * the ioc4 ithreads).
	 */

	/* attach interrupt handler */

	ioc4_ss_connect_interrupt(pci_handle->irq, (void *)ioc4_intr,
				  (void *)soft);

	/* =============================================================
	 *                            Attach Sub-devices
	 *
	 * NB: As subdevs start calling pciio_driver_register(),
	 * we can stop explicitly calling subdev drivers.
	 *
	 * The drivers attached here have not been converted
	 * to stand on their own.  However, they *do* know
	 * to call ioc4_subdev_enabled() to decide whether
	 * to actually attach themselves.
	 *
	 * It would be nice if we could convert these
	 * few remaining drivers over so they would
	 * register as proper PCI device drivers ...
	 */

	ioc4_serial_attach(conn_vhdl, (void *)soft->is_ioc4_mem);	/* DMA serial ports */

	return 0;
}

/*
 * ioc4_intr_connect:
 * Arrange for interrupts for a sub-device
 * to be delivered to the right bit of
 * code with the right parameter.
 *
 * XXX- returning an error instead of panicing
 * might be a good idea (think bugs in loadable
 * ioc4 sub-devices).
 */

static void
ioc4_intr_connect(vertex_hdl_t conn_vhdl,
		  ioc4_intr_type_t type,
		  ioc4reg_t intrbits,
		  ioc4_intr_func_f * intr,
		  intr_arg_t info,
		  vertex_hdl_t owner_vhdl, vertex_hdl_t intr_dev_vhdl)
{
	graph_error_t rc;
	vertex_hdl_t ioc4_vhdl;
	ioc4_soft_t *soft;
	ioc4reg_t old, bits;
	int i;

	ASSERT((type == ioc4_sio_intr_type) || (type == ioc4_other_intr_type));

	rc = hwgraph_traverse(conn_vhdl, EDGE_LBL_IOC4, &ioc4_vhdl);
	if (rc != GRAPH_SUCCESS) {
		printk(KERN_ALERT
		       "ioc4_intr_connect(%p): ioc4_attach not yet called",
		       (void *)owner_vhdl);
		return;
	}

	soft = ioc4_soft_get(ioc4_vhdl);
	ASSERT(soft != NULL);

	/*
	 * Try to allocate a slot in the array
	 * that has been marked free; if there
	 * are none, extend the high water mark.
	 */
	while (1) {
		bits = atomic_read(&soft->is_intr_type[type].is_intr_ents_free);
		if (bits == 0) {
			i = atomic_inc(&soft->is_intr_type[type].is_num_intrs) -
			    1;
			ASSERT(i < MAX_IOC4_INTR_ENTS
			       || (printk("i %d\n", i), 0));
			break;
		}
		bits &= ~(bits - 1);	/* keep only the ls bit */
		old =
		    atomicClearInt(&soft->is_intr_type[type].is_intr_ents_free,
				   bits);
		if (bits & old) {
			ioc4reg_t shf;

			i = 31;
			if ((shf = (bits >> 16)))
				bits = shf;
			else
				i -= 16;
			if ((shf = (bits >> 8)))
				bits = shf;
			else
				i -= 8;
			if ((shf = (bits >> 4)))
				bits = shf;
			else
				i -= 4;
			if ((shf = (bits >> 2)))
				bits = shf;
			else
				i -= 2;
			if ((shf = (bits >> 1)))
				bits = shf;
			else
				i -= 1;
			ASSERT(i < MAX_IOC4_INTR_ENTS
			       || (printk("i %d\n", i), 0));
			break;
		}
	}

	soft->is_intr_type[type].is_intr_info[i].sd_bits = intrbits;
	soft->is_intr_type[type].is_intr_info[i].sd_intr = intr;
	soft->is_intr_type[type].is_intr_info[i].sd_info = info;
	soft->is_intr_type[type].is_intr_info[i].sd_vhdl = owner_vhdl;
	soft->is_intr_type[type].is_intr_info[i].sd_soft = soft;

	/* Make sure there are no bitmask overlaps */
	{
		ioc4reg_t old;

		old =
		    atomicSetInt(&soft->is_intr_type[type].is_intr_bits_busy,
				 intrbits);
		if (old & intrbits) {
			printk("%p: trying to share ioc4 intr bits 0x%X\n",
			       (void *)owner_vhdl, old & intrbits);

#if DEBUG && IOC4_DEBUG
			{
				int x;

				for (x = 0; x < i; x++)
					if (intrbits & soft->is_intr_type[type].
					    is_intr_info[x].sd_bits) {
						printk
						    ("%p: ioc4 intr bits 0x%X already call "
						     "0x%X(0x%X, ...)\n",
						     (void *)soft->
						     is_intr_type[type].
						     is_intr_info[x].sd_vhdl,
						     soft->is_intr_type[type].
						     is_intr_info[i].sd_bits,
						     soft->is_intr_type[type].
						     is_intr_info[i].sd_intr,
						     soft->is_intr_type[type].
						     is_intr_info[i].sd_info);
					}
			}
#endif
			panic
			    ("ioc4_intr_connect: no IOC4 interrupt source sharing allowed");
		}
	}
}

/* Top level IOC4 interrupt handler.  Farms out the interrupt to
 * the various IOC4 device drivers.
 */

static irqreturn_t ioc4_intr(int irq, void *arg, struct pt_regs *regs)
{
	ioc4_soft_t *soft;
	ioc4reg_t this_ir;
	ioc4reg_t this_mir;
	int x, num_intrs = 0;
	ioc4_intr_type_t t;
	int handled = 0;

	soft = (ioc4_soft_t *) arg;

#ifdef DEBUG_INTERRUPTS
	printk("%s : %d arg 0x%p\n", __FUNCTION__, __LINE__, soft);
#endif

	if (!soft)
		return IRQ_NONE;	/* Polled but no console ioc4 registered */

	for (t = ioc4_first_intr_type; t < ioc4_num_intr_types; t++) {
		num_intrs =
		    (int)atomic_read(&soft->is_intr_type[t].is_num_intrs);

		this_mir = this_ir = ioc4_pending_intrs(soft, t);
#ifdef DEBUG_INTERRUPTS
		printk("%s : %d : this_mir 0x%x num_intrs %d\n", __FUNCTION__,
		       __LINE__, this_mir, num_intrs);
#endif

		/* Farm out the interrupt to the various drivers depending on
		 * which interrupt bits are set.
		 */
		for (x = 0; x < num_intrs; x++) {
			struct ioc4_intr_info *ii =
			    &soft->is_intr_type[t].is_intr_info[x];
			if ((this_mir = this_ir & ii->sd_bits)) {
				/* Disable owned interrupts, and call the interrupt handler */
				handled++;
#ifdef DEBUG_INTERRUPTS
				printk("%s : %d handled %d : call handler\n",
				       __FUNCTION__, __LINE__, handled);
#endif
				IOC4_WRITE_IEC(soft, ii->sd_bits, t);
				ii->sd_intr(ii->sd_info, this_mir);
				this_ir &= ~this_mir;
			}
		}

		if (this_ir)
			printk(KERN_ALERT
			       "unknown IOC4 %s interrupt 0x%x, sio_ir = 0x%x, sio_ies = 0x%x, other_ir = 0x%x, other_ies = 0x%x\n",
			       (t == ioc4_sio_intr_type) ? "sio" : "other",
			       this_ir, soft->is_ioc4_mem->sio_ir,
			       soft->is_ioc4_mem->sio_ies_ro,
			       soft->is_ioc4_mem->other_ir,
			       soft->is_ioc4_mem->other_ies_ro);
	}
#ifdef DEBUG_INTERRUPTS
	{
		ioc4_mem_t *mem = soft->is_ioc4_mem;
		spinlock_t *lp = &soft->is_ir_lock;
		unsigned long s;

		spin_lock_irqsave(lp, s);
		printk
		    ("%s : %d : sio_ir 0x%x sio_ies_ro 0x%x other_ir 0x%x other_ies_ro 0x%x mask 0x%x\n",
		     __FUNCTION__, __LINE__, mem->sio_ir, mem->sio_ies_ro,
		     mem->other_ir, mem->other_ies_ro,
		     IOC4_OTHER_IR_ATA_INT | IOC4_OTHER_IR_ATA_MEMERR);

		spin_unlock_irqrestore(lp, s);
	}
#endif
	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static iopaddr_t ring_dmatrans(vertex_hdl_t conn_vhdl, caddr_t vaddr)
{
	extern iopaddr_t pciio_dma_addr(vertex_hdl_t, device_desc_t, paddr_t,
					size_t, pciio_dmamap_t *, unsigned);
	iopaddr_t paddr = (iopaddr_t) vaddr;

	if (conn_vhdl != GRAPH_VERTEX_NONE)
#ifdef	USE_64BIT_DMA
		/* Use 64-bit DMA address when the IOC4 supports it */
		return pciio_dmatrans_addr(conn_vhdl, 0, paddr,
					   TOTAL_RING_BUF_SIZE,
					   PCIIO_DMA_A64 | PCIIO_BYTE_STREAM);

#else
		/* Use 32-bit DMA address for current IOC4 */
		return pciio_dma_addr(conn_vhdl, 0, paddr, TOTAL_RING_BUF_SIZE,
				      NULL, PCIIO_BYTE_STREAM);
#endif

	return paddr;
}

/* If interrupt routine called enable_intrs, then would need to write
 * mask_enable_intrs() routine.
 */
static inline void mask_disable_intrs(ioc4port_t * port, ioc4reg_t mask)
{
	port->ip_ienb &= ~mask;
}

static void enable_intrs(ioc4port_t * port, ioc4reg_t mask)
{
	struct hooks *hooks = port->ip_hooks;

	if ((port->ip_ienb & mask) != mask) {
		IOC4_WRITE_IES(port->ip_ioc4_soft, mask, ioc4_sio_intr_type);
		port->ip_ienb |= mask;
	}

	if (port->ip_ienb)
		IOC4_WRITE_IES(port->ip_ioc4_soft, H_INTR_DMA_ERROR,
			       ioc4_other_intr_type);
}

static void disable_intrs(ioc4port_t * port, ioc4reg_t mask)
{
	struct hooks *hooks = port->ip_hooks;

	if (port->ip_ienb & mask) {
		IOC4_WRITE_IEC(port->ip_ioc4_soft, mask, ioc4_sio_intr_type);
		port->ip_ienb &= ~mask;
	}

	if (!port->ip_ienb)
		IOC4_WRITE_IEC(port->ip_ioc4_soft, H_INTR_DMA_ERROR,
			       ioc4_other_intr_type);
}

/* Service any pending interrupts on the given port */
static void ioc4_serial_intr(intr_arg_t arg, ioc4reg_t sio_ir)
{
	ioc4port_t *port = (ioc4port_t *) arg;
	sioport_t *gp = GPORT(port);
	struct hooks *hooks = port->ip_hooks;
	unsigned rx_high_rd_aborted = 0;
	unsigned int flags;

	PROGRESS();

	/* Possible race condition here: The TX_MT interrupt bit may be
	 * cleared without the intervention of the interrupt handler,
	 * e.g. by a write.  If the top level interrupt handler reads a
	 * TX_MT, then some other processor does a write, starting up
	 * output, then we come in here, see the TX_MT and stop DMA, the
	 * output started by the other processor will hang.  Thus we can
	 * only rely on TX_MT being legitimate if it is read while the
	 * port lock is held.  Therefore this bit must be ignored in the
	 * passed in interrupt mask which was read by the top level
	 * interrupt handler since the port lock was not held at the time
	 * it was read.  We can only rely on this bit being accurate if it
	 * is read while the port lock is held.  So we'll clear it for now,
	 * and reload it later once we have the port lock.
	 */
	sio_ir &= ~(H_INTR_TX_MT);

	SIO_LOCK_PORT(gp, flags);

	dprintf(("interrupt: sio_ir 0x%x\n", sio_ir));

	do {
		ioc4reg_t shadow;

		/* Handle a DCD change */
		if (sio_ir & H_INTR_DELTA_DCD) {
			DEBUGINC(ddcd_intr, 1);

			PROGRESS();
			/* ACK the interrupt */
			PCI_OUTW(&port->ip_ioc4->sio_ir, H_INTR_DELTA_DCD);

			/* If DCD has raised, notify upper layer.  Otherwise
			 * wait for a record to be posted to notify of a dropped DCD.
			 */
			shadow = PCI_INW(&port->ip_serial->shadow);

			if (port->ip_notify & N_DDCD) {
				PROGRESS();
				if (shadow & IOC4_SHADOW_DCD)	/* Notify upper layer of DCD */
					UP_DDCD(gp, 1);
				else
					port->ip_flags |= DCD_ON;	/* Flag delta DCD/no DCD */
			}
		}

		/* Handle a CTS change */
		if (sio_ir & H_INTR_DELTA_CTS) {
			DEBUGINC(dcts_intr, 1);
			PROGRESS();

			/* ACK the interrupt */
			PCI_OUTW(&port->ip_ioc4->sio_ir, H_INTR_DELTA_CTS);

			shadow = PCI_INW(&port->ip_serial->shadow);

			/* Notify upper layer */
			if (port->ip_notify & N_DCTS) {
				if (shadow & IOC4_SHADOW_CTS)
					UP_DCTS(gp, 1);
				else
					UP_DCTS(gp, 0);
			}
		}

		/* RX timeout interrupt.  Must be some data available.  Put this
		 * before the check for RX_HIGH since servicing this condition
		 * may cause that condition to clear.
		 */
		if (sio_ir & H_INTR_RX_TIMER) {
			PROGRESS();
			DEBUGINC(rx_timer_intr, 1);

			/* ACK the interrupt */
			PCI_OUTW(&port->ip_ioc4->sio_ir, H_INTR_RX_TIMER);

			if (port->ip_notify & N_DATA_READY)
				UP_DATA_READY(gp);
		}

		/* RX high interrupt. Must be after RX_TIMER.
		 */
		else if (sio_ir & H_INTR_RX_HIGH) {
			DEBUGINC(rx_high_intr, 1);

			PROGRESS();
			/* Data available, notify upper layer */
			if (port->ip_notify & N_DATA_READY)
				UP_DATA_READY(gp);

			/* We can't ACK this interrupt.  If up_data_ready didn't
			 * cause the condition to clear, we'll have to disable
			 * the interrupt until the data is drained by the upper layer.
			 * If the read was aborted, don't disable the interrupt as
			 * this may cause us to hang indefinitely.  An aborted read
			 * generally means that this interrupt hasn't been delivered
			 * to the cpu yet anyway, even though we see it as asserted 
			 * when we read the sio_ir.
			 */
			if ((sio_ir = PENDING(port)) & H_INTR_RX_HIGH) {
				PROGRESS();
				if ((port->ip_flags & READ_ABORTED) == 0) {
					mask_disable_intrs(port,
							   H_INTR_RX_HIGH);
					port->ip_flags |= INPUT_HIGH;
				} else {
					DEBUGINC(read_aborted_detected, 1);
					/* We will be stuck in this loop forever,
					 * higher level will never get time to finish
					 */
					rx_high_rd_aborted++;
				}
			}
		}

		/* We got a low water interrupt: notify upper layer to
		 * send more data.  Must come before TX_MT since servicing
		 * this condition may cause that condition to clear.
		 */
		if (sio_ir & H_INTR_TX_EXPLICIT) {
			DEBUGINC(explicit_intr, 1);
			PROGRESS();

			port->ip_flags &= ~LOWAT_WRITTEN;

			/* ACK the interrupt */
			PCI_OUTW(&port->ip_ioc4->sio_ir, H_INTR_TX_EXPLICIT);

			if (port->ip_notify & N_OUTPUT_LOWAT)
				UP_OUTPUT_LOWAT(gp);
		}

		/* Handle TX_MT.  Must come after TX_EXPLICIT.
		 */
		else if (sio_ir & H_INTR_TX_MT) {
			DEBUGINC(mt_intr, 1);
			PROGRESS();

			/* If the upper layer is expecting a lowat notification
			 * and we get to this point it probably means that for
			 * some reason the TX_EXPLICIT didn't work as expected
			 * (that can legitimately happen if the output buffer is
			 * filled up in just the right way).  So sent the notification
			 * now.
			 */
			if (port->ip_notify & N_OUTPUT_LOWAT) {
				DEBUGINC(mt_lowat_intr, 1);
				PROGRESS();

				if (port->ip_notify & N_OUTPUT_LOWAT)
					UP_OUTPUT_LOWAT(gp);

				/* We need to reload the sio_ir since the upcall may
				 * have caused another write to occur, clearing
				 * the TX_MT condition.
				 */
				sio_ir = PENDING(port);
			}

			/* If the TX_MT condition still persists even after the upcall,
			 * we've got some work to do.
			 */
			if (sio_ir & H_INTR_TX_MT) {

				PROGRESS();

				/* If we are not currently expecting DMA input, and the
				 * transmitter has just gone idle, there is no longer any
				 * reason for DMA, so disable it.
				 */
				if (!
				    (port->
				     ip_notify & (N_DATA_READY | N_DDCD))) {
					ASSERT(port->
					       ip_sscr & IOC4_SSCR_DMA_EN);
					port->ip_sscr &= ~IOC4_SSCR_DMA_EN;
					PCI_OUTW(&port->ip_serial->sscr,
						 port->ip_sscr);
				}

				/* Prevent infinite TX_MT interrupt */
				mask_disable_intrs(port, H_INTR_TX_MT);
			}
		}

		sio_ir = PENDING(port);

		/* if the read was aborted and only H_INTR_RX_HIGH,
		 * clear H_INTR_RX_HIGH, so we do not loop forever.
		 */

		if (rx_high_rd_aborted && (sio_ir == H_INTR_RX_HIGH)) {
			sio_ir &= ~H_INTR_RX_HIGH;
		}
	} while (sio_ir & H_INTR_ALL);

	SIO_UNLOCK_PORT(gp, flags);

	/* Re-enable interrupts before returning from interrupt handler.
	 * Getting interrupted here is okay.  It'll just v() our semaphore, and
	 * we'll come through the loop again.
	 */

	IOC4_WRITE_IES(port->ip_ioc4_soft, port->ip_ienb, ioc4_sio_intr_type);
}

 /*ARGSUSED*/
/* Service any pending DMA error interrupts on the given port */
static void ioc4_dma_error_intr(intr_arg_t arg, ioc4reg_t other_ir)
{
	ioc4port_t *port = (ioc4port_t *) arg;
	sioport_t *gp = GPORT(port);
	struct hooks *hooks = port->ip_hooks;
	unsigned int flags;

	SIO_LOCK_PORT(gp, flags);

	dprintf(("interrupt: other_ir 0x%x\n", other_ir));

	/* ACK the interrupt */
	PCI_OUTW(&port->ip_ioc4->other_ir, H_INTR_DMA_ERROR);

	printk("DMA error on serial port %p\n", (void *)port->ip_port_vhdl);

	if (port->ip_ioc4->pci_err_addr_l & IOC4_PCI_ERR_ADDR_VLD) {
		printk
		    ("PCI error address is 0x%lx, master is serial port %c %s\n",
		     ((uint64_t) port->ip_ioc4->pci_err_addr_h << 32) | (port->
									 ip_ioc4->
									 pci_err_addr_l
									 &
									 IOC4_PCI_ERR_ADDR_ADDR_MSK),
		     '1' +
		     (char)((port->ip_ioc4->
			     pci_err_addr_l & IOC4_PCI_ERR_ADDR_MST_NUM_MSK) >>
			    1),
		     (port->ip_ioc4->
		      pci_err_addr_l & IOC4_PCI_ERR_ADDR_MST_TYP_MSK)
		     ? "RX" : "TX");

		if (port->ip_ioc4->pci_err_addr_l & IOC4_PCI_ERR_ADDR_MUL_ERR)
			printk("Multiple errors occurred\n");
	}

	SIO_UNLOCK_PORT(gp, flags);

	/* Re-enable DMA error interrupts */
	IOC4_WRITE_IES(port->ip_ioc4_soft, H_INTR_DMA_ERROR,
		       ioc4_other_intr_type);
}

/* Baud rate setting code */
static int set_baud_ti(ioc4port_t * port, int baud)
{
	int actual_baud;
	int diff;
	int lcr;
	unsigned short divisor;

	divisor = SER_DIVISOR(baud, IOC4_SER_XIN_CLK);
	if (!divisor)
		return (1);
	actual_baud = DIVISOR_TO_BAUD(divisor, IOC4_SER_XIN_CLK);

	diff = actual_baud - baud;
	if (diff < 0)
		diff = -diff;

	/* If we're within 1%, we've found a match */
	if (diff * 100 > actual_baud)
		return (1);

	lcr = PCI_INB(&port->ip_uart->i4u_lcr);

	PCI_OUTB(&port->ip_uart->i4u_lcr, lcr | UART_LCR_DLAB);

	PCI_OUTB(&port->ip_uart->i4u_dll, (char)divisor);

	PCI_OUTB(&port->ip_uart->i4u_dlm, (char)(divisor >> 8));

	PCI_OUTB(&port->ip_uart->i4u_lcr, lcr);

	return (0);
}

/* Initialize the sio and ioc4 hardware for a given port */
static int hardware_init(ioc4port_t * port)
{
	ioc4reg_t sio_cr;
	struct hooks *hooks = port->ip_hooks;

	DEBUGINC(ports, 1);

	/* Idle the IOC4 serial interface */
	PCI_OUTW(&port->ip_serial->sscr, IOC4_SSCR_RESET);

	/* Wait until any pending bus activity for this port has ceased */
	do
		sio_cr = PCI_INW(&port->ip_ioc4->sio_cr);
	while (!(sio_cr & IOC4_SIO_CR_SIO_DIAG_IDLE));

	/* Finish reset sequence */
	PCI_OUTW(&port->ip_serial->sscr, 0);

	/* Once RESET is done, reload cached tx_prod and rx_cons values
	 * and set rings to empty by making prod == cons
	 */
	port->ip_tx_prod = PCI_INW(&port->ip_serial->stcir) & PROD_CONS_MASK;
	PCI_OUTW(&port->ip_serial->stpir, port->ip_tx_prod);

	port->ip_rx_cons = PCI_INW(&port->ip_serial->srpir) & PROD_CONS_MASK;
	PCI_OUTW(&port->ip_serial->srcir, port->ip_rx_cons);

	/* Disable interrupts for this 16550 */
	PCI_OUTB(&port->ip_uart->i4u_lcr, 0);	/* clear DLAB */
	PCI_OUTB(&port->ip_uart->i4u_ier, 0);

	/* Set the default baud */
	SET_BAUD(port, port->ip_baud);

	/* Set line control to 8 bits no parity */
	PCI_OUTB(&port->ip_uart->i4u_lcr,
		 UART_LCR_WLEN8 | 0 /* UART_LCR_STOP = 1 */ );

	/* Enable the FIFOs */
	PCI_OUTB(&port->ip_uart->i4u_fcr, UART_FCR_ENABLE_FIFO);
	/* then reset 16550 FIFOs */
	PCI_OUTB(&port->ip_uart->i4u_fcr,
		 UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
		 UART_FCR_CLEAR_XMIT);

	/* Clear modem control register */
	PCI_OUTB(&port->ip_uart->i4u_mcr, 0);

	/* Clear deltas in modem status register */
	PCI_INB(&port->ip_uart->i4u_msr);

	/* Only do this once per port pair */
	if (port->ip_hooks == &hooks_array[0]
	    || port->ip_hooks == &hooks_array[2]) {
		iopaddr_t ring_pci_addr;
		volatile ioc4reg_t *sbbr_l;
		volatile ioc4reg_t *sbbr_h;

		if (port->ip_hooks == &hooks_array[0]) {
			sbbr_l = &port->ip_ioc4->sbbr01_l;
			sbbr_h = &port->ip_ioc4->sbbr01_h;
		} else {
			sbbr_l = &port->ip_ioc4->sbbr23_l;
			sbbr_h = &port->ip_ioc4->sbbr23_h;
		}

		/* Set the DMA address */
		ring_pci_addr = ring_dmatrans(port->ip_conn_vhdl,
					      port->ip_ring_buf_k0);

		PCI_OUTW(sbbr_h, (ioc4reg_t) ((uint64_t) ring_pci_addr >> 32));

		PCI_OUTW(sbbr_l,
			 ((ioc4reg_t) (int64_t) ring_pci_addr |
			  IOC4_BUF_SIZE_BIT));

#ifdef IOC4_SIO_DEBUG
		{
			unsigned int tmp1, tmp2;

			tmp1 = PCI_INW(sbbr_l);
			tmp2 = PCI_INW(sbbr_h);
			printk
			    ("========== %s : sbbr_l [%p]/0x%x sbbr_h [%p]/0x%x\n",
			     __FUNCTION__, (void *)sbbr_l, tmp1,
			     (void *)sbbr_h, tmp2);
		}
#endif
	}

	/* Set the receive timeout value to 10 msec */
	PCI_OUTW(&port->ip_serial->srtr, IOC4_SRTR_HZ / 100);

	/* Set RX threshold, enable DMA */
	/* Set high water mark at 3/4 of full ring */
	port->ip_sscr = (ENTRIES_PER_RING * 3 / 4);

	PCI_OUTW(&port->ip_serial->sscr, port->ip_sscr);

	/* Disable and clear all serial related interrupt bits */
	IOC4_WRITE_IEC(port->ip_ioc4_soft, H_INTR_CLEAR, ioc4_sio_intr_type);
	port->ip_ienb &= ~H_INTR_CLEAR;
	PCI_OUTW(&port->ip_ioc4->sio_ir, H_INTR_CLEAR);

	return (0);
}

/*
 * Device initialization.
 * Called at *_attach() time for each
 * IOC4 with serial ports in the system.
 * If vhdl is GRAPH_VERTEX_NONE, do not do
 * any graph related work; otherwise, it
 * is the IOC4 vertex that should be used
 * for requesting pciio services.
 */

/*
 * Called from lower level to pass on the interesting stuff.
 * This connects this level to the next lowest level.
 */

inline void ioc4_serial_initport(sioport_t * port, int which)
{
	if (which < IOC4_NUM_SERIAL_PORTS) {
		IOC4_control[which].ic_sioport = port;
	}
}

int ioc4_serial_attach(vertex_hdl_t conn_vhdl, void *ioc4)
{
	 /*REFERENCED*/ graph_error_t rc;
	ioc4_mem_t *ioc4_mem;
	vertex_hdl_t port_vhdl, ioc4_vhdl;
	vertex_hdl_t intr_dev_vhdl;
	ioc4port_t *port;
	ioc4port_t *ports[4];
	static char *names[] = { "tty/1", "tty/2", "tty/3", "tty/4" };
	int x, first_port = -1, last_port = -1;
	void *ioc4_soft;
	unsigned int ioc4_revid_min = 62;
	unsigned int ioc4_revid;

	/* IOC4 firmware must be at least rev 62 */
	ioc4_revid = pciio_config_get(conn_vhdl, PCI_CFG_REV_ID, 1);

	if (ioc4_revid < ioc4_revid_min) {
		printk
		    ("IOC4 serial ports not supported on firmware rev %d, please upgrade to rev %d or higher\n",
		     ioc4_revid, ioc4_revid_min);
		return -1;
	}

	first_port = 0;
	last_port = 3;

	/* Get back pointer to the ioc4 soft area */
	rc = hwgraph_traverse(conn_vhdl, EDGE_LBL_IOC4, &ioc4_vhdl);
	ASSERT(rc == GRAPH_SUCCESS);
	ioc4_soft = (void *)hwgraph_fastinfo_get(ioc4_vhdl);

	/* grab the PIO address */
	ioc4_mem = (ioc4_mem_t *) ioc4;
	ASSERT(ioc4_mem != NULL);

	/*
	 * Create port structures for each port
	 */
	port = kmalloc(4 * sizeof(*(port)), GFP_KERNEL);
	if (!port) {
		printk("IOC4 serial ports memory not available for port\n");
		return -1;
	}
	memset(port, 0, 4 * sizeof(*(port)));

#ifdef IOC4_SIO_DEBUG
	printk("%s : [addr 0x%p]\n", __FUNCTION__, (void *)port);
#endif
	ports[0] = port++;
	ports[1] = port++;
	ports[2] = port++;
	ports[3] = port++;

#if DEBUG
	{
		int slot = atomicAddInt(&next_saveport, 4) - 4;
		saveport[slot] = ports[0];
		saveport[slot + 1] = ports[1];
		saveport[slot + 2] = ports[2];
		saveport[slot + 3] = ports[3];
		ASSERT(slot < MAXSAVEPORT);
	}
#endif

#ifdef DEBUG
	if ((caddr_t) port != (caddr_t) & (port->ip_sioport))
		panic("sioport is not first member of ioc4port struct\n");
#endif

	/* Allocate buffers and jumpstart the hardware.
	 */
	for (x = first_port; x < (last_port + 1); x++) {

		port = ports[x];
#ifdef IOC4_SIO_DEBUG
		printk("%s : initialize port %d [addr 0x%p/0x%p]\n",
		       __FUNCTION__, x, (void *)port, (void *)GPORT(port));
#endif
		port->ip_ioc4_soft = ioc4_soft;
		rc = hwgraph_path_add(conn_vhdl, names[x], &port_vhdl);
		ASSERT(rc == GRAPH_SUCCESS);
		port->ip_conn_vhdl = conn_vhdl;
		port->ip_port_vhdl = port_vhdl;
		port->ip_ienb = 0;
		hwgraph_fastinfo_set(port_vhdl, (arbitrary_info_t) port);

		/* Perform upper layer initialization. Create all device node
		 * types including rs422 ports.
		 */
		ioc4_serial_initport(GPORT(port), x);
		port->ip_baud = 9600;

		/* Attach the calldown hooks so upper layer can call our
		 * routines.
		 */
		port->ip_sioport.sio_calldown = &ioc4_calldown;

		/* Map in the IOC4 register area */
		port->ip_ioc4 = ioc4_mem;
	}

	{
		/* Port 0 */
		port = ports[0];
		port->ip_hooks = &hooks_array[0];

		/* Get direct hooks to the serial regs and uart regs
		 * for this port
		 */
		port->ip_serial = &(port->ip_ioc4->port_0);
		port->ip_uart = &(port->ip_ioc4->uart_0);
#ifdef IOC4_SIO_DEBUG
		printk
		    ("==== %s : serial port 0 address 0x%p uart address 0x%p\n",
		     __FUNCTION__, (void *)port->ip_serial,
		     (void *)port->ip_uart);
#endif

		/* If we don't already have a ring buffer,
		 * set one up.
		 */
		if (port->ip_ring_buf_k0 == 0) {

#if PAGE_SIZE >= TOTAL_RING_BUF_SIZE
			if ((port->ip_ring_buf_k0 =
			     kvpalloc(1, VM_DIRECT, 0)) == 0)
				panic
				    ("ioc4_uart driver cannot allocate page\n");
#else
			/* We need to allocate a chunk of memory on a
			 * TOTAL_RING_BUF_SIZE boundary.
			 */
			{
				pgno_t pfn;
				caddr_t vaddr;
				if ((pfn =
				     contig_memalloc(TOTAL_RING_BUF_SIZE /
						     PAGE_SIZE,
						     TOTAL_RING_BUF_SIZE /
						     PAGE_SIZE,
						     VM_DIRECT)) == 0)
					panic
					    ("ioc4_uart driver cannot allocate page\n");
				ASSERT(small_pfn(pfn));
				vaddr = small_pfntova_K0(pfn);
				(void)COLOR_VALIDATION(pfdat + pfn,
						       colorof(vaddr),
						       0, VM_DIRECT);
				port->ip_ring_buf_k0 = vaddr;
			}
#endif
		}
		ASSERT((((int64_t) port->ip_ring_buf_k0) &
			(TOTAL_RING_BUF_SIZE - 1)) == 0);
		memset(port->ip_ring_buf_k0, 0, TOTAL_RING_BUF_SIZE);
		port->ip_inring = RING(port, RX_0_OR_2);
		port->ip_outring = RING(port, TX_0_OR_2);

		/* Initialize the hardware for IOC4 */
		hardware_init(port);

		if (hwgraph_edge_get
		    (ports[0]->ip_port_vhdl, "d",
		     &intr_dev_vhdl) != GRAPH_SUCCESS) {
			intr_dev_vhdl = ports[0]->ip_port_vhdl;
		}

		/* Attach interrupt handlers */
		ioc4_intr_connect(conn_vhdl,
				  ioc4_sio_intr_type,
				  IOC4_SIO_IR_S0,
				  ioc4_serial_intr,
				  ports[0],
				  ports[0]->ip_port_vhdl, intr_dev_vhdl);

		ioc4_intr_connect(conn_vhdl,
				  ioc4_other_intr_type,
				  IOC4_OTHER_IR_S0_MEMERR,
				  ioc4_dma_error_intr,
				  ports[0],
				  ports[0]->ip_port_vhdl, intr_dev_vhdl);
	}
	Active_port_count++;

	{

		/* Port 1 */
		port = ports[1];
		port->ip_hooks = &hooks_array[1];

		port->ip_serial = &(port->ip_ioc4->port_1);
		port->ip_uart = &(port->ip_ioc4->uart_1);
#ifdef IOC4_SIO_DEBUG
		printk
		    ("==== %s : serial port 1 address 0x%p uart address 0x%p\n",
		     __FUNCTION__, (void *)port->ip_serial,
		     (void *)port->ip_uart);
#endif

		port->ip_ring_buf_k0 = ports[0]->ip_ring_buf_k0;
		port->ip_inring = RING(port, RX_1_OR_3);
		port->ip_outring = RING(port, TX_1_OR_3);

		/* Initialize the hardware for IOC4 */
		hardware_init(port);

		if (hwgraph_edge_get
		    (ports[1]->ip_port_vhdl, "d",
		     &intr_dev_vhdl) != GRAPH_SUCCESS) {
			intr_dev_vhdl = ports[1]->ip_port_vhdl;
		}

		/* Attach interrupt handler */
		ioc4_intr_connect(conn_vhdl,
				  ioc4_sio_intr_type,
				  IOC4_SIO_IR_S1,
				  ioc4_serial_intr,
				  ports[1],
				  ports[1]->ip_port_vhdl, intr_dev_vhdl);

		ioc4_intr_connect(conn_vhdl,
				  ioc4_other_intr_type,
				  IOC4_OTHER_IR_S1_MEMERR,
				  ioc4_dma_error_intr,
				  ports[1],
				  ports[1]->ip_port_vhdl, intr_dev_vhdl);
	}
	Active_port_count++;

	{

		/* Port 2 */
		port = ports[2];
		port->ip_hooks = &hooks_array[2];

		/* Get direct hooks to the serial regs and uart regs
		 * for this port
		 */
		port->ip_serial = &(port->ip_ioc4->port_2);
		port->ip_uart = &(port->ip_ioc4->uart_2);
#ifdef IOC4_SIO_DEBUG
		printk
		    ("==== %s : serial port 2 address 0x%p uart address 0x%p\n",
		     __FUNCTION__, (void *)port->ip_serial,
		     (void *)port->ip_uart);
#endif

		/* If we don't already have a ring buffer,
		 * set one up.
		 */
		if (port->ip_ring_buf_k0 == 0) {

#if PAGE_SIZE >= TOTAL_RING_BUF_SIZE
			if ((port->ip_ring_buf_k0 =
			     kvpalloc(1, VM_DIRECT, 0)) == 0)
				panic
				    ("ioc4_uart driver cannot allocate page\n");
#else

			/* We need to allocate a chunk of memory on a
			 * TOTAL_RING_BUF_SIZE boundary.
			 */
			{
				pgno_t pfn;
				caddr_t vaddr;
				if ((pfn =
				     contig_memalloc(TOTAL_RING_BUF_SIZE /
						     PAGE_SIZE,
						     TOTAL_RING_BUF_SIZE /
						     PAGE_SIZE,
						     VM_DIRECT)) == 0)
					panic
					    ("ioc4_uart driver cannot allocate page\n");
				ASSERT(small_pfn(pfn));
				vaddr = small_pfntova_K0(pfn);
				(void)COLOR_VALIDATION(pfdat + pfn,
						       colorof(vaddr),
						       0, VM_DIRECT);
				port->ip_ring_buf_k0 = vaddr;
			}
#endif

		}
		ASSERT((((int64_t) port->ip_ring_buf_k0) &
			(TOTAL_RING_BUF_SIZE - 1)) == 0);
		memset(port->ip_ring_buf_k0, 0, TOTAL_RING_BUF_SIZE);
		port->ip_inring = RING(port, RX_0_OR_2);
		port->ip_outring = RING(port, TX_0_OR_2);

		/* Initialize the hardware for IOC4 */
		hardware_init(port);

		if (hwgraph_edge_get
		    (ports[0]->ip_port_vhdl, "d",
		     &intr_dev_vhdl) != GRAPH_SUCCESS) {
			intr_dev_vhdl = ports[2]->ip_port_vhdl;
		}

		/* Attach interrupt handler */
		ioc4_intr_connect(conn_vhdl,
				  ioc4_sio_intr_type,
				  IOC4_SIO_IR_S2,
				  ioc4_serial_intr,
				  ports[2],
				  ports[2]->ip_port_vhdl, intr_dev_vhdl);

		ioc4_intr_connect(conn_vhdl,
				  ioc4_other_intr_type,
				  IOC4_OTHER_IR_S2_MEMERR,
				  ioc4_dma_error_intr,
				  ports[2],
				  ports[2]->ip_port_vhdl, intr_dev_vhdl);
	}
	Active_port_count++;

	{

		/* Port 3 */
		port = ports[3];
		port->ip_hooks = &hooks_array[3];

		port->ip_serial = &(port->ip_ioc4->port_3);
		port->ip_uart = &(port->ip_ioc4->uart_3);
#ifdef IOC4_SIO_DEBUG
		printk
		    ("==== %s : serial port 3 address 0x%p uart address 0x%p\n",
		     __FUNCTION__, (void *)port->ip_serial,
		     (void *)port->ip_uart);
#endif

		port->ip_ring_buf_k0 = ports[2]->ip_ring_buf_k0;
		port->ip_inring = RING(port, RX_1_OR_3);
		port->ip_outring = RING(port, TX_1_OR_3);

		/* Initialize the hardware for IOC4 */
		hardware_init(port);

		if (hwgraph_edge_get
		    (ports[3]->ip_port_vhdl, "d",
		     &intr_dev_vhdl) != GRAPH_SUCCESS) {
			intr_dev_vhdl = ports[3]->ip_port_vhdl;
		}

		/* Attach interrupt handler */
		ioc4_intr_connect(conn_vhdl,
				  ioc4_sio_intr_type,
				  IOC4_SIO_IR_S3,
				  ioc4_serial_intr,
				  ports[3],
				  ports[3]->ip_port_vhdl, intr_dev_vhdl);

		ioc4_intr_connect(conn_vhdl,
				  ioc4_other_intr_type,
				  IOC4_OTHER_IR_S3_MEMERR,
				  ioc4_dma_error_intr,
				  ports[3],
				  ports[3]->ip_port_vhdl, intr_dev_vhdl);
	}
	Active_port_count++;

#ifdef	DEBUG
	idbg_addfunc("ioc4dump", idbg_ioc4dump);
#endif

	return 0;
}

/* Shut down an IOC4 */
/* ARGSUSED1 */
void ioc4_serial_kill(ioc4port_t * port)
{
	DEBUGINC(killed, 1);

	/* Notify upper layer that this port is no longer usable */
	UP_DETACH(GPORT(port));

	/* Clear everything in the sscr */
	PCI_OUTW(&port->ip_serial->sscr, 0);
	port->ip_sscr = 0;

#ifdef DEBUG
	/* Make sure nobody gets past the lock and accesses the hardware */
	port->ip_ioc4 = 0;
	port->ip_serial = 0;
#endif

}

/*
 * Open a port
 */
static int ioc4_open(sioport_t * port)
{
	ioc4port_t *p = LPORT(port);
	int spin_success;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_OPEN));
#endif

	p->ip_flags = 0;
	p->ip_modem_bits = 0;

	/* Pause the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_DMA_PAUSE);
		SPIN((PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_PAUSE_STATE) ==
		     0, spin_success);
		if (!spin_success) {
			NOT_PROGRESS();
			return (-1);
		}
	}

	/* Reset the input fifo.  If the uart received chars while the port
	 * was closed and DMA is not enabled, the uart may have a bunch of
	 * chars hanging around in its RX fifo which will not be discarded
	 * by rclr in the upper layer. We must get rid of them here.
	 */
	PCI_OUTB(&p->ip_uart->i4u_fcr,
		 UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR);

	/* Set defaults */
	SET_BAUD(p, 9600);

	PCI_OUTB(&p->ip_uart->i4u_lcr,
		 UART_LCR_WLEN8 | 0 /* UART_LCR_STOP == 1 stop */ );

	/* Re-enable DMA, set default threshold to intr whenever there is
	 * data available.
	 */
	p->ip_sscr &= ~IOC4_SSCR_RX_THRESHOLD;
	p->ip_sscr |= 1;	/* default threshold */

	/* Plug in the new sscr.  This implicitly clears the DMA_PAUSE
	 * flag if it was set above
	 */
	PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);

	PCI_OUTW(&p->ip_serial->srtr, 0);

	p->ip_tx_lowat = 1;

	dprintf(("ioc4 open successful\n"));

	return (0);
}

/*
 * Config hardware
 */
static int
ioc4_config(sioport_t * port,
	    int baud, int byte_size, int stop_bits, int parenb, int parodd)
{
	ioc4port_t *p = LPORT(port);
	char lcr, sizebits;
	int spin_success;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_CONFIG));
#endif

	if (SET_BAUD(p, baud))
		return (1);

	switch (byte_size) {
	case 5:
		sizebits = UART_LCR_WLEN5;
		break;
	case 6:
		sizebits = UART_LCR_WLEN6;
		break;
	case 7:
		sizebits = UART_LCR_WLEN7;
		break;
	case 8:
		sizebits = UART_LCR_WLEN8;
		break;
	default:
		dprintf(("invalid byte size port 0x%x size %d\n", port,
			 byte_size));
		return (1);
	}

	/* Pause the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_DMA_PAUSE);
		SPIN((PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_PAUSE_STATE) ==
		     0, spin_success);
		if (!spin_success)
			return (-1);
	}

	/* Clear relevant fields in lcr */
	lcr = PCI_INB(&p->ip_uart->i4u_lcr);
	lcr &= ~(LCR_MASK_BITS_CHAR | UART_LCR_EPAR |
		 UART_LCR_PARITY | LCR_MASK_STOP_BITS);

	/* Set byte size in lcr */
	lcr |= sizebits;

	/* Set parity */
	if (parenb) {
		lcr |= UART_LCR_PARITY;
		if (!parodd)
			lcr |= UART_LCR_EPAR;
	}

	/* Set stop bits */
	if (stop_bits)
		lcr |= UART_LCR_STOP /* 2 stop bits */ ;

	PCI_OUTB(&p->ip_uart->i4u_lcr, lcr);

	dprintf(("ioc4_config: lcr bits 0x%x\n", lcr));

	/* Re-enable the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);
	}

	p->ip_baud = baud;

	/* When we get within this number of ring entries of filling the
	 * entire ring on TX, place an EXPLICIT intr to generate a lowat
	 * notification when output has drained.
	 */
	p->ip_tx_lowat = (TX_LOWAT_CHARS(baud) + 3) / 4;
	if (p->ip_tx_lowat == 0)
		p->ip_tx_lowat = 1;

	ioc4_rx_timeout(port, p->ip_rx_timeout);

	return (0);
}

/*
 * Enable hardware flow control
 */
static int ioc4_enable_hfc(sioport_t * port, int enable)
{
	ioc4port_t *p = LPORT(port);

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_ENABLE_HFC));
#endif

	dprintf(("enable hfc port 0x%p, enb %d\n", (void *)port, enable));

	if (enable)
		p->ip_sscr |= IOC4_SSCR_HFC_EN;
	else
		p->ip_sscr &= ~IOC4_SSCR_HFC_EN;

	PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);

	return (0);
}

/*
 * Return the number of active ports
 */
unsigned int ioc4_down_num_ports(void)
{
	return Active_port_count;
}

/*
 * Write bytes to the hardware.  Returns the number of bytes
 * actually written.
 */
static int do_ioc4_write(sioport_t * port, char *buf, int len)
{
	int prod_ptr, cons_ptr, total;
	struct ring *outring;
	struct ring_entry *entry;
	ioc4port_t *p = LPORT(port);
	struct hooks *hooks = p->ip_hooks;

	DEBUGINC(write_bytes, len);
	DEBUGINC(write_cnt, 1);

	dprintf(("write port 0x%p, len %d\n", (void *)port, len));

	ASSERT(len >= 0);

	prod_ptr = p->ip_tx_prod;
	cons_ptr = PCI_INW(&p->ip_serial->stcir) & PROD_CONS_MASK;
	outring = p->ip_outring;

	/* Maintain a 1-entry red-zone.  The ring buffer is full when
	 * (cons - prod) % ring_size is 1.  Rather than do this subtraction
	 * in the body of the loop, I'll do it now.
	 */
	cons_ptr = (cons_ptr - (int)sizeof(struct ring_entry)) & PROD_CONS_MASK;

	total = 0;
	/* Stuff the bytes into the output */
	while ((prod_ptr != cons_ptr) && (len > 0)) {
		int x;

		/* Go 4 bytes (one ring entry) at a time */
		entry = (struct ring_entry *)((caddr_t) outring + prod_ptr);

		/* Invalidate all entries */
		entry->ring_allsc = 0;

		/* Copy in some bytes */
		for (x = 0; (x < 4) && (len > 0); x++) {
			entry->ring_data[x] = *buf++;
			entry->ring_sc[x] = IOC4_TXCB_VALID;
			len--;
			total++;
		}

		DEBUGINC(tx_buf_used, x);
		DEBUGINC(tx_buf_cnt, 1);

		/* If we are within some small threshold of filling up the entire
		 * ring buffer, we must place an EXPLICIT intr here to generate
		 * a lowat interrupt in case we subsequently really do fill up
		 * the ring and the caller goes to sleep.  No need to place
		 * more than one though.
		 */
		if (!(p->ip_flags & LOWAT_WRITTEN) &&
		    ((cons_ptr - prod_ptr) & PROD_CONS_MASK) <=
		    p->ip_tx_lowat * (int)sizeof(struct ring_entry)) {
			p->ip_flags |= LOWAT_WRITTEN;
			entry->ring_sc[0] |= IOC4_TXCB_INT_WHEN_DONE;
			dprintf(("write placing TX_EXPLICIT\n"));
		}

		/* Go on to next entry */
		prod_ptr =
		    (prod_ptr +
		     (int)sizeof(struct ring_entry)) & PROD_CONS_MASK;
	}

	/* If we sent something, start DMA if necessary */
	if (total > 0 && !(p->ip_sscr & IOC4_SSCR_DMA_EN)) {
		p->ip_sscr |= IOC4_SSCR_DMA_EN;
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);
	}

	/* Store the new producer pointer.  If TX is disabled, we stuff the
	 * data into the ring buffer, but we don't actually start TX.
	 */
	if (!(p->ip_flags & TX_DISABLED)) {
		PCI_OUTW(&p->ip_serial->stpir, prod_ptr);

		/* If we are now transmitting, enable TX_MT interrupt so we
		 * can disable DMA if necessary when the TX finishes.
		 */
		if (total > 0)
			enable_intrs(p, H_INTR_TX_MT);
	}
	p->ip_tx_prod = prod_ptr;

	dprintf(("write port 0x%p, wrote %d\n", (void *)port, total));
	DEBUGINC(wrote_bytes, total);
	return (total);
}

/* Asynchronous write */
static int ioc4_write(sioport_t * port, char *buf, int len)
{
#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_WRITE));
#endif
	return (do_ioc4_write(port, buf, len));
}

/*
 * Set or clear break condition on output
 */
static int ioc4_break(sioport_t * port, int brk)
{
	ioc4port_t *p = LPORT(port);
	char lcr;
	int spin_success;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_BREAK));
#endif

	/* Pause the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_DMA_PAUSE);
		SPIN((PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_PAUSE_STATE) ==
		     0, spin_success);
		if (!spin_success)
			return (-1);
	}

	lcr = PCI_INB(&p->ip_uart->i4u_lcr);
	if (brk) {
		/* Set break */
		PCI_OUTB(&p->ip_uart->i4u_lcr, lcr | UART_LCR_SBC);
	} else {
		/* Clear break */
		PCI_OUTB(&p->ip_uart->i4u_lcr, lcr & ~UART_LCR_SBC);
	}

	/* Re-enable the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);
	}

	dprintf(("break port 0x%p, brk %d\n", (void *)port, brk));

	return (0);
}

static int ioc4_enable_tx(sioport_t * port, int enb)
{
	ioc4port_t *p = LPORT(port);
	struct hooks *hooks = p->ip_hooks;
	int spin_success;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_ENABLE_TX));
#endif

	/* If we're already in the desired state, we're done */
	if ((enb && !(p->ip_flags & TX_DISABLED)) ||
	    (!enb && (p->ip_flags & TX_DISABLED)))
		return (0);

	/* Pause DMA */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_DMA_PAUSE);
		SPIN((PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_PAUSE_STATE) ==
		     0, spin_success);
		if (!spin_success)
			return (-1);
	}

	if (enb) {
		p->ip_flags &= ~TX_DISABLED;
		PCI_OUTW(&p->ip_serial->stpir, p->ip_tx_prod);
		enable_intrs(p, H_INTR_TX_MT);
	} else {
		ioc4reg_t txcons =
		    PCI_INW(&p->ip_serial->stcir) & PROD_CONS_MASK;
		p->ip_flags |= TX_DISABLED;
		disable_intrs(p, H_INTR_TX_MT);

		/* Only move the transmit producer pointer back if the
		 * transmitter is not already empty, otherwise we'll be
		 * generating a bogus entry.
		 */
		if (txcons != p->ip_tx_prod)
			PCI_OUTW(&p->ip_serial->stpir,
				 (txcons +
				  (int)sizeof(struct ring_entry)) &
				 PROD_CONS_MASK);
	}

	/* Re-enable the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN)
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);

	return (0);
}

/*
 * Read in bytes from the hardware.  Return the number of bytes
 * actually read.
 */
static int ioc4_read(sioport_t * port, char *buf, int len)
{
	int prod_ptr, cons_ptr, total, x, spin_success;
	struct ring *inring;
	ioc4port_t *p = LPORT(port);
	struct hooks *hooks = p->ip_hooks;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_READ));
#endif

	dprintf(("read port 0x%p, len %d\n", (void *)port, len));

	DEBUGINC(read_bytes, len);
	DEBUGINC(read_cnt, 1);

	ASSERT(len >= 0);

	/* There is a nasty timing issue in the IOC4. When the RX_TIMER
	 * expires or the RX_HIGH condition arises, we take an interrupt.
	 * At some point while servicing the interrupt, we read bytes from
	 * the ring buffer and re-arm the RX_TIMER.  However the RX_TIMER is
	 * not started until the first byte is received *after* it is armed,
	 * and any bytes pending in the RX construction buffers are not drained
	 * to memory until either there are 4 bytes available or the RX_TIMER
	 * expires.  This leads to a potential situation where data is left
	 * in the construction buffers forever because 1 to 3 bytes were received
	 * after the interrupt was generated but before the RX_TIMER was re-armed.
	 * At that point as long as no subsequent bytes are received the
	 * timer will never be started and the bytes will remain in the
	 * construction buffer forever.  The solution is to execute a DRAIN
	 * command after rearming the timer.  This way any bytes received before
	 * the DRAIN will be drained to memory, and any bytes received after
	 * the DRAIN will start the TIMER and be drained when it expires.
	 * Luckily, this only needs to be done when the DMA buffer is empty
	 * since there is no requirement that this function return all
	 * available data as long as it returns some.
	 */
	/* Re-arm the timer */
	PCI_OUTW(&p->ip_serial->srcir, p->ip_rx_cons | IOC4_SRCIR_ARM);

	prod_ptr = PCI_INW(&p->ip_serial->srpir) & PROD_CONS_MASK;
	cons_ptr = p->ip_rx_cons;

	if (prod_ptr == cons_ptr) {
		int reset_dma = 0;

		/* Input buffer appears empty, do a flush. */

		/* DMA must be enabled for this to work. */
		if (!(p->ip_sscr & IOC4_SSCR_DMA_EN)) {
			p->ip_sscr |= IOC4_SSCR_DMA_EN;
			reset_dma = 1;
		}

		/* Potential race condition: we must reload the srpir after
		 * issuing the drain command, otherwise we could think the RX
		 * buffer is empty, then take a very long interrupt, and when
		 * we come back it's full and we wait forever for the drain to
		 * complete.
		 */
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_RX_DRAIN);
		prod_ptr = PCI_INW(&p->ip_serial->srpir) & PROD_CONS_MASK;

		DEBUGINC(drain, 1);

		/* We must not wait for the DRAIN to complete unless there are
		 * at least 8 bytes (2 ring entries) available to receive the data
		 * otherwise the DRAIN will never complete and we'll deadlock here.
		 * In fact, to make things easier, I'll just ignore the flush if
		 * there is any data at all now available.
		 */
		if (prod_ptr == cons_ptr) {
			DEBUGINC(drainwait, 1);
			SPIN(PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_RX_DRAIN,
			     spin_success);
			if (!spin_success)
				return (-1);

			/* SIGH. We have to reload the prod_ptr *again* since
			 * the drain may have caused it to change
			 */
			prod_ptr =
			    PCI_INW(&p->ip_serial->srpir) & PROD_CONS_MASK;
		}

		if (reset_dma) {
			DEBUGINC(resetdma, 1);
			p->ip_sscr &= ~IOC4_SSCR_DMA_EN;
			PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);
		}
	}
	inring = p->ip_inring;

	p->ip_flags &= ~READ_ABORTED;

	total = 0;
	/* Grab bytes from the hardware */
	while (prod_ptr != cons_ptr && len > 0) {
		struct ring_entry *entry;

		entry = (struct ring_entry *)((caddr_t) inring + cons_ptr);

		/* According to the producer pointer, this ring entry
		 * must contain some data.  But if the PIO happened faster
		 * than the DMA, the data may not be available yet, so let's
		 * wait until it arrives.
		 */
		if ((((volatile struct ring_entry *)entry)->ring_allsc &
		     RING_ANY_VALID) == 0) {

			/* Indicate the read is aborted so we don't disable
			 * the interrupt thinking that the consumer is
			 * congested.
			 */
			p->ip_flags |= READ_ABORTED;

			DEBUGINC(read_aborted, 1);
			len = 0;
			break;

		}

		/* Load the bytes/status out of the ring entry */
		for (x = 0; x < 4 && len > 0; x++) {
			char *sc = &(entry->ring_sc[x]);

			/* Check for change in modem state or overrun */
			if (*sc & IOC4_RXSB_MODEM_VALID) {
				if (p->ip_notify & N_DDCD) {

					/* Notify upper layer if DCD dropped */
					if ((p->ip_flags & DCD_ON)
					    && !(*sc & IOC4_RXSB_DCD)) {

						/* If we have already copied some data, return
						 * it.  We'll pick up the carrier drop on the next
						 * pass.  That way we don't throw away the data
						 * that has already been copied back to the caller's
						 * buffer.
						 */
						if (total > 0) {
							len = 0;
							break;
						}

						p->ip_flags &= ~DCD_ON;

						/* Turn off this notification so the carrier
						 * drop protocol won't see it again when it
						 * does a read.
						 */
						*sc &= ~IOC4_RXSB_MODEM_VALID;

						/* To keep things consistent, we need to update
						 * the consumer pointer so the next reader won't
						 * come in and try to read the same ring entries
						 * again.  This must be done here before the call
						 * to UP_DDCD since UP_DDCD may do a recursive
						 * read!
						 */
						if ((entry->
						     ring_allsc &
						     RING_ANY_VALID) == 0)
							cons_ptr =
							    (cons_ptr + (int)
							     sizeof(struct
								    ring_entry))
							    & PROD_CONS_MASK;

						PCI_OUTW(&p->ip_serial->srcir,
							 cons_ptr);
						p->ip_rx_cons = cons_ptr;

						/* Notify upper layer of carrier drop */
						if (p->ip_notify & N_DDCD)
							UP_DDCD(port, 0);

						DEBUGINC(read_ddcd, 1);

						/* If we had any data to return, we would have
						 * returned it above.
						 */
						return (0);
					}
				}

				/* Notify upper layer that an input overrun occurred */
				if ((*sc & IOC4_RXSB_OVERRUN)
				    && (p->ip_notify & N_OVERRUN_ERROR)) {
					DEBUGINC(rx_overrun, 1);
					UP_NCS(port, NCS_OVERRUN);
				}

				/* Don't look at this byte again */
				*sc &= ~IOC4_RXSB_MODEM_VALID;
			}

			/* Check for valid data or RX errors */
			if (*sc & IOC4_RXSB_DATA_VALID) {
				if ((*sc &
				     (IOC4_RXSB_PAR_ERR | IOC4_RXSB_FRAME_ERR |
				      IOC4_RXSB_BREAK))
				    && (p->
					ip_notify & (N_PARITY_ERROR |
						     N_FRAMING_ERROR |
						     N_BREAK))) {

					/* There is an error condition on the next byte.  If
					 * we have already transferred some bytes, we'll stop
					 * here.  Otherwise if this is the first byte to be read,
					 * we'll just transfer it alone after notifying the
					 * upper layer of its status.
					 */
					if (total > 0) {
						len = 0;
						break;
					} else {
						if ((*sc & IOC4_RXSB_PAR_ERR) &&
						    (p->
						     ip_notify &
						     N_PARITY_ERROR)) {
							DEBUGINC(parity, 1);
							UP_NCS(port,
							       NCS_PARITY);
						}

						if ((*sc & IOC4_RXSB_FRAME_ERR)
						    && (p->
							ip_notify &
							N_FRAMING_ERROR)) {
							DEBUGINC(framing, 1);
							UP_NCS(port,
							       NCS_FRAMING);
						}

						if ((*sc & IOC4_RXSB_BREAK) &&
						    (p->ip_notify & N_BREAK)) {
							DEBUGINC(brk, 1);
							UP_NCS(port, NCS_BREAK);
						}
						len = 1;
					}
				}

				*sc &= ~IOC4_RXSB_DATA_VALID;
				*buf++ = entry->ring_data[x];
				len--;
				total++;
			}
		}

		DEBUGINC(rx_buf_used, x);
		DEBUGINC(rx_buf_cnt, 1);

		/* If we used up this entry entirely, go on to the next one,
		 * otherwise we must have run out of buffer space, so
		 * leave the consumer pointer here for the next read in case
		 * there are still unread bytes in this entry.
		 */
		if ((entry->ring_allsc & RING_ANY_VALID) == 0)
			cons_ptr =
			    (cons_ptr +
			     (int)sizeof(struct ring_entry)) & PROD_CONS_MASK;
	}

	/* Update consumer pointer and re-arm RX timer interrupt */
	PCI_OUTW(&p->ip_serial->srcir, cons_ptr);
	p->ip_rx_cons = cons_ptr;

	/* If we have now dipped below the RX high water mark and we have
	 * RX_HIGH interrupt turned off, we can now turn it back on again.
	 */
	if ((p->ip_flags & INPUT_HIGH) &&
	    (((prod_ptr - cons_ptr) & PROD_CONS_MASK) <
	     ((p->
	       ip_sscr & IOC4_SSCR_RX_THRESHOLD) << IOC4_PROD_CONS_PTR_OFF))) {
		p->ip_flags &= ~INPUT_HIGH;
		enable_intrs(p, H_INTR_RX_HIGH);
	}

	DEBUGINC(red_bytes, total);

	return (total);
}

/*
 * Modify event notification
 */
static int ioc4_notification(sioport_t * port, int mask, int on)
{
	ioc4port_t *p = LPORT(port);
	struct hooks *hooks = p->ip_hooks;
	ioc4reg_t intrbits, sscrbits;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_NOTIFICATION));
#endif
	ASSERT(mask);

	intrbits = sscrbits = 0;

	if (mask & N_DATA_READY)
		intrbits |= (H_INTR_RX_TIMER | H_INTR_RX_HIGH);
	if (mask & N_OUTPUT_LOWAT)
		intrbits |= H_INTR_TX_EXPLICIT;
	if (mask & N_DDCD) {
		intrbits |= H_INTR_DELTA_DCD;
		sscrbits |= IOC4_SSCR_RX_RING_DCD;
	}
	if (mask & N_DCTS)
		intrbits |= H_INTR_DELTA_CTS;

	if (on) {
		enable_intrs(p, intrbits);
		p->ip_notify |= mask;
		p->ip_sscr |= sscrbits;
	} else {
		disable_intrs(p, intrbits);
		p->ip_notify &= ~mask;
		p->ip_sscr &= ~sscrbits;
	}

	/* We require DMA if either DATA_READY or DDCD notification is
	 * currently requested.  If neither of these is requested and
	 * there is currently no TX in progress, DMA may be disabled.
	 */
	if (p->ip_notify & (N_DATA_READY | N_DDCD))
		p->ip_sscr |= IOC4_SSCR_DMA_EN;
	else if (!(p->ip_ienb & H_INTR_TX_MT))
		p->ip_sscr &= ~IOC4_SSCR_DMA_EN;

	PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);
	return (0);
}

/*
 * Set RX timeout and threshold values.  The upper layer passes in a
 * timeout value.  In all cases it would like to be notified at least this
 * often when there are RX chars coming in.  We set the RX timeout and
 * RX threshold (based on baud) to ensure that the upper layer is called
 * at roughly this interval during normal RX.
 * The input timeout value is in ticks.
 */
static int ioc4_rx_timeout(sioport_t * port, int timeout)
{
	int threshold;
	ioc4port_t *p = LPORT(port);

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_RX_TIMEOUT));
#endif

	p->ip_rx_timeout = timeout;

	/* Timeout is in ticks.  Let's figure out how many chars we
	 * can receive at the current baud rate in that interval
	 * and set the RX threshold to that amount.  There are 4 chars
	 * per ring entry, so we'll divide the number of chars that will
	 * arrive in timeout by 4.
	 */
	threshold = timeout * p->ip_baud / 10 / HZ / 4;
	if (threshold == 0)
		threshold = 1;	/* otherwise we'll intr all the time! */

	if ((unsigned)threshold > (unsigned)IOC4_SSCR_RX_THRESHOLD)
		return (1);

	p->ip_sscr &= ~IOC4_SSCR_RX_THRESHOLD;
	p->ip_sscr |= threshold;

	PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);

	/* Now set the RX timeout to the given value */
	timeout = timeout * IOC4_SRTR_HZ / HZ;
	if (timeout > IOC4_SRTR_CNT)
		timeout = IOC4_SRTR_CNT;

	PCI_OUTW(&p->ip_serial->srtr, timeout);

	return (0);
}

static int set_DTRRTS(sioport_t * port, int val, int mask1, int mask2)
{
	ioc4port_t *p = LPORT(port);
	ioc4reg_t shadow;
	int spin_success;
	char mcr;

	/* XXX need lock for pretty much this entire routine.  Makes
	 * me nervous to hold it for so long.  If we crash or hit
	 * a breakpoint in here, we're hosed.
	 */

	/* Pause the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN) {
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr | IOC4_SSCR_DMA_PAUSE);
		SPIN((PCI_INW(&p->ip_serial->sscr) & IOC4_SSCR_PAUSE_STATE) ==
		     0, spin_success);
		if (!spin_success)
			return (-1);
	}

	shadow = PCI_INW(&p->ip_serial->shadow);
	mcr = (shadow & 0xff000000) >> 24;

	/* Set new value */
	if (val) {
		mcr |= mask1;
		shadow |= mask2;
	} else {
		mcr &= ~mask1;
		shadow &= ~mask2;
	}

	PCI_OUTB(&p->ip_uart->i4u_mcr, mcr);

	PCI_OUTW(&p->ip_serial->shadow, shadow);

	/* Re-enable the DMA interface if necessary */
	if (p->ip_sscr & IOC4_SSCR_DMA_EN)
		PCI_OUTW(&p->ip_serial->sscr, p->ip_sscr);

	return (0);
}

static int ioc4_set_DTR(sioport_t * port, int dtr)
{
#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_SET_DTR));
#endif

	dprintf(("set dtr port 0x%p, dtr %d\n", (void *)port, dtr));
	return (set_DTRRTS(port, dtr, UART_MCR_DTR, IOC4_SHADOW_DTR));
}

static int ioc4_set_RTS(sioport_t * port, int rts)
{
#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_SET_RTS));
#endif

	dprintf(("set rts port 0x%p, rts %d\n", (void *)port, rts));
	return (set_DTRRTS(port, rts, UART_MCR_RTS, IOC4_SHADOW_RTS));
}

static int ioc4_query_DCD(sioport_t * port)
{
	ioc4port_t *p = LPORT(port);
	ioc4reg_t shadow;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_QUERY_DCD));
#endif

	dprintf(("get dcd port 0x%p\n", (void *)port));

	shadow = PCI_INW(&p->ip_serial->shadow);

	return (shadow & IOC4_SHADOW_DCD);
}

static int ioc4_query_CTS(sioport_t * port)
{
	ioc4port_t *p = LPORT(port);
	ioc4reg_t shadow;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_QUERY_CTS));
#endif

	dprintf(("get cts port 0x%p\n", (void *)port));

	shadow = PCI_INW(&p->ip_serial->shadow);

	return (shadow & IOC4_SHADOW_CTS);
}

static int ioc4_set_proto(sioport_t * port, enum sio_proto proto)
{
	ioc4port_t *p = LPORT(port);
	struct hooks *hooks = p->ip_hooks;

#ifdef NOT_YET
	ASSERT(L_LOCKED(port, L_SET_PROTOCOL));
#endif

	switch (proto) {
	case PROTO_RS232:
		/* Clear the appropriate GIO pin */
		PCI_OUTW((&p->ip_ioc4->gppr_0 + H_RS422), 0);
		break;

	case PROTO_RS422:
		/* Set the appropriate GIO pin */
		PCI_OUTW((&p->ip_ioc4->gppr_0 + H_RS422), 1);
		break;

	default:
		return (1);
	}

	return (0);
}

/*********************************************************************
 *
 * support functions
 */

inline void ioc4_ss_sched_event(struct async_struct *info, int event)
{
	if (info) {
		info->event |= (1 << event);
		tasklet_schedule(&info->tlet);
	}
}

/*
 * port Read/Input
 */

void inline ioc4_ss_receive_chars(struct async_struct *info)
{
	struct tty_struct *tty;
	unsigned char ch[IOC4_MAX_CHARS];
	int ret;
	int read_count = IOC4_MAX_CHARS - 2;
	struct async_icount *icount;
	ioc4_control_t *control;
	unsigned long flags;

	/* Make sure all the pointers are "good" ones */
	if (!info)
		return;
	if (!info->tty || !info->state)
		return;

	control = (ioc4_control_t *) info->tty->driver_data;
	tty = info->tty;
	icount = &info->state->icount;
	spin_lock_irqsave(&control->ic_lock, flags);

#ifdef IOC4_DEBUG_TRACE
	printk("%s : %d [%d] req = %d/", __FUNCTION__, __LINE__, info->line,
	       read_count);
#endif
	ret = DOWN_READ(control->ic_sioport, (char *)ch, read_count);
#ifdef IOC4_DEBUG_TRACE
	printk("%d\n", ret);
#endif
	if (ret > 0) {
		memcpy(tty->flip.char_buf_ptr, ch, ret);
		tty->flip.char_buf_ptr += ret;
		tty->flip.flag_buf_ptr += ret;
		tty->flip.count += ret;
		icount->rx += ret;
	}
	tty_flip_buffer_push(tty);
	spin_unlock_irqrestore(&control->ic_lock, flags);
}

/*
 * port Write/Output
 */

void inline ioc4_ss_transmit_chars(struct async_struct *info)
{
	int xmit_count, tail, head, loops, ii;
	int result;
	unsigned long flags;
	char *start;
	struct tty_struct *tty = info->tty;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;

	if (info->xmit.head == info->xmit.tail || tty->stopped
	    || tty->hw_stopped) {
		/* Nothing to do. */
#ifdef ENABLE_OUTPUT_INTERRUPTS
		DOWN_NOTIFICATION(control->ic_sioport, N_ALL_OUTPUT, 0);
#endif
		return;
	}

	spin_lock_irqsave(&control->ic_lock, flags);

	head = info->xmit.head;
	tail = info->xmit.tail;
	start = (char *)&info->xmit.buf[tail];

	/* twice around gets the tail to the end of the buffer and then to the head, if needed */
	loops = (head < tail) ? 2 : 1;

	for (ii = 0; ii < loops; ii++) {
		xmit_count =
		    (head < tail) ? (SERIAL_XMIT_SIZE - tail) : (head - tail);
		if (xmit_count > 0) {
#ifdef IOC4_DEBUG_TRACE
			printk("%s : %d [%d] count = %d/", __FUNCTION__,
			       __LINE__, info->line, xmit_count);
#endif
			result =
			    DOWN_WRITE(control->ic_sioport, start, xmit_count);
#ifdef IOC4_DEBUG_TRACE
			printk("%d\n", result);
#endif
			if (result > 0) {
				xmit_count -= result;
				info->state->icount.tx += result;
				tail += result;
				tail &= SERIAL_XMIT_SIZE - 1;
				info->xmit.tail = tail;
				start = (char *)&info->xmit.buf[tail];
			}
		}
	}
	spin_unlock_irqrestore(&control->ic_lock, flags);

	/* CIRC_CNT returns the number of bytes in the buffer */
	if (CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE) <
	    WAKEUP_CHARS) {
		ioc4_ss_sched_event(info, IOC4_EVENT_WRITE_WAKEUP);
	}
#ifdef ENABLE_OUTPUT_INTERRUPTS
	if (info->xmit.head != info->xmit.tail) {
#ifdef TRACE_NOTIFICATION_FUNCS
		printk("%s : %d : Set lowat\n", __FUNCTION__, __LINE__);
#endif
		DOWN_NOTIFICATION(control->ic_sioport, N_OUTPUT_LOWAT, 1);
	} else {
		DOWN_NOTIFICATION(control->ic_sioport, N_OUTPUT_LOWAT, 0);
	}
#endif				/* ENABLE_OUTPUT_INTERRUPTS */
}

/*
 * interrupt handler for polling
 */

static void ioc4_ss_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ioc4_control_t *control = (ioc4_control_t *) dev_id;
	unsigned long port_flags;

	if (!control)
		return;		/* FIXME:  need some kinda error message */

	if (control->ic_irq == 0 || TTY_RESTART) {
		/* polling */
		SIO_LOCK_PORT(control->ic_sioport, port_flags);
		ioc4_ss_receive_chars(control->ic_info);
		ioc4_ss_transmit_chars(control->ic_info);
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	}
}

/*
 * connect up our interrupt. Note that this gets called from a lower layer and
 * connects the interrupt to THAT layer's handler.
 */

void ioc4_ss_connect_interrupt(int irq, void *func, void *arg)
{
	int ii;
#ifdef POLLING_FOR_CHARACTERS
	for (ii = 0; ii < IOC4_NUM_SERIAL_PORTS; ii++) {
		IOC4_control[ii].ic_irq = 0;
	}
#else
	if (!request_irq
	    (irq, func, SA_INTERRUPT | SA_SHIRQ, "sgi-ioc4serial", arg)) {
		for (ii = 0; ii < IOC4_NUM_SERIAL_PORTS; ii++) {
			IOC4_control[ii].ic_irq = irq;
		}
	} else {
		printk("%s : request_irq fails for IRQ 0x%x\n !!!!!!!!!",
		       __FUNCTION__, irq);
	}
#endif				/* POLLING_FOR_CHARACTERS */
}

/*
 * ioc4_ss_do_softint - tasklet handler
 */
static void ioc4_ss_do_softint(unsigned long data)
{
	struct async_struct *info = (struct async_struct *)data;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(IOC4_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
		    && tty->ldisc.write_wakeup) {
			(tty->ldisc.write_wakeup) (tty);
		}
		wake_up_interruptible(&tty->write_wait);
	}
}

/*
 * This function handles polled mode.
 */
static void ioc4_ss_timer(unsigned long dummy)
{
	static unsigned long last_strobe;
	struct async_struct *info;
	unsigned int ii;
	int anyone_cares = TTY_RESTART;

	if ((jiffies - last_strobe) >= TIMER_WAIT_TIME) {
		/* look thru the list and call all those that exist */
		for (ii = 0; ii < IOC4_NUM_SERIAL_PORTS; ii++) {
			info = IOC4_irq_list[ii];
			if (!info)
				continue;
			anyone_cares++;
			ioc4_ss_interrupt(IOC4_control[ii].ic_irq,
					  &IOC4_control[ii], NULL);
		}
	}
	/* If there aren't any currently running, don't reload timer - open will */
	if (anyone_cares) {
		last_strobe = jiffies;
		mod_timer(&IOC4_timer_list, jiffies + TIMER_WAIT_TIME);
	}
}

static int ioc4_ss_get_async_struct(int line, struct async_struct **ret_info)
{
	struct async_struct *info;
	struct serial_state *sstate;

	sstate = IOC4_table + line;
	sstate->count++;
	if (sstate->info) {
		*ret_info = sstate->info;
		PROGRESS();
		return 0;
	}
	info = kmalloc(sizeof(struct async_struct), GFP_KERNEL);
	if (!info) {
		sstate->count--;
		NOT_PROGRESS();
		return -ENOMEM;
	}
	memset(info, 0, sizeof(struct async_struct));
	init_waitqueue_head(&info->open_wait);
	init_waitqueue_head(&info->close_wait);
	init_waitqueue_head(&info->delta_msr_wait);
	info->magic = SERIAL_MAGIC;
	info->port = sstate->port;
	info->flags = sstate->flags;
	info->io_type = sstate->io_type;
	info->xmit_fifo_size = sstate->xmit_fifo_size;
	info->line = line;
	info->state = sstate;
	info->tlet.func = (void *)ioc4_ss_do_softint;
	info->tlet.data = (unsigned long)info;
	if (sstate->info) {
		kfree(info);
		*ret_info = sstate->info;
		NOT_PROGRESS();
		return 0;
	}
	*ret_info = sstate->info = info;
	PROGRESS();
	return 0;
}

static void
ioc4_ss_change_speed(struct async_struct *info, struct termios *old_termios)
{
	int quot = 0, baud_base, baud;
	unsigned cflag, cval;
	int bits;
	int new_baud = 9600, new_parity = 0, new_parity_enable = 1, new_stop =
	    1, new_data = 8;
	ioc4_control_t *control;

	if (!info->tty || !info->tty->termios) {
		PROGRESS();
		return;
	}
	cflag = info->tty->termios->c_cflag;
	control = (ioc4_control_t *) info->tty->driver_data;

	switch (cflag & CSIZE) {
	case CS5:
		new_data = 5;
		cval = 0x00;
		bits = 7;
		break;
	case CS6:
		new_data = 6;
		cval = 0x01;
		bits = 8;
		break;
	case CS7:
		new_data = 7;
		cval = 0x02;
		bits = 9;
		break;
	case CS8:
		new_data = 8;
		cval = 0x03;
		bits = 10;
		break;
	default:
		/* cuz we always need a default ... */
		new_data = 5;
		cval = 0x00;
		bits = 7;
		break;
	}
	if (cflag & CSTOPB) {
		cval |= 0x04;
		bits++;
		new_stop = 1;
	}
	if (cflag & PARENB) {
		cval |= UART_LCR_PARITY;
		bits++;
		new_parity_enable = 1;
	}
	if (!(cflag & PARODD)) {
		cval |= UART_LCR_EPAR;
		new_parity = 1;
	}

	PROGRESS();
	baud = tty_get_baud_rate(info->tty);

	if (!baud)
		baud = 9600;
	baud_base = info->state->baud_base;

	if (baud == 38400 && ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
		quot = info->state->custom_divisor;
	else {
		if (baud == 134) {
			/* Special case since 134 is really 134.5 */
			quot = (2 * baud_base / 269);
		} else if (baud)
			quot = baud_base / baud;
	}
	/* quotient is zero -- bad */
	if (!quot && old_termios) {
		info->tty->termios->c_cflag &= ~CBAUD;
		info->tty->termios->c_cflag |= (old_termios->c_cflag & CBAUD);
		baud = tty_get_baud_rate(info->tty);
		if (!baud)
			baud = 9600;
		if (baud == 38400
		    && ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
			quot = info->state->custom_divisor;
		else {
			if (baud == 134) {
				/* Special case since 134 is really 134.5 */
				quot = (2 * baud_base / 269);
			} else if (baud)
				quot = baud_base / baud;
		}
	}
	/* As a last resort, if the quotient is zero, default to 9600 bps */
	if (quot == 0)
		quot = baud_base / 9600;

	info->quot = quot;
	if (info->quot == 0) {
		/* Just to be absolutely sure */
		new_baud = 9600;
	} else
		new_baud = baud_base / info->quot;

	if (info->xmit_fifo_size == 0)
		info->xmit_fifo_size = 16;
	info->timeout = ((info->xmit_fifo_size * HZ * bits * quot) / baud_base);
	info->timeout += HZ / 50;	/* Add .02 seconds of slop */

#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	info->read_status_mask = N_ALL_INPUT;
#ifdef ENABLE_OUTPUT_INTERRUPTS
	info->read_status_mask |= N_OUTPUT_LOWAT;
#endif

	info->read_status_mask &= ~(N_OVERRUN_ERROR | N_DATA_READY);
	if (I_INPCK(info->tty))
		info->read_status_mask &= ~(N_FRAMING_ERROR | N_PARITY_ERROR);
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask &= ~N_BREAK;

	info->ignore_status_mask = N_ALL_INPUT;
#ifdef ENABLE_OUTPUT_INTERRUPTS
	info->read_status_mask |= N_OUTPUT_LOWAT;
#endif

	if (I_IGNPAR(info->tty))
		info->ignore_status_mask &= ~(N_PARITY_ERROR | N_FRAMING_ERROR);
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask &= ~N_BREAK;
		if (I_IGNPAR(info->tty))
			info->ignore_status_mask &= ~N_OVERRUN_ERROR;
	}
	if ((cflag & CREAD) == 0)
		info->ignore_status_mask &= ~N_DATA_READY;	/* ignore everything */

	if (cflag & CRTSCTS)
		info->flags |= ASYNC_CTS_FLOW;
	else
		info->flags &= ~ASYNC_CTS_FLOW;

	PROGRESS();

	/* Set the configuration and proper notification call */
#ifdef IOC4_DEBUG_TRACE
	printk("%s : set config(%d %d %d %d %d), notification 0x%x\n",
	       __FUNCTION__, new_baud, new_data, new_stop, new_parity_enable,
	       new_parity, info->ignore_status_mask);
#endif
	if ((DOWN_CONFIG(control->ic_sioport, new_baud,	/* baud */
			 new_data,	/* byte size */
			 new_stop,	/* stop bits */
			 new_parity_enable,	/* set parity */
			 new_parity)) >= 0) {	/* parity 1==odd */
		DOWN_NOTIFICATION(control->ic_sioport, info->ignore_status_mask,
				  1);
	}
}

static int get_modem_info(struct async_struct *info, unsigned int *value)
{
	unsigned int result;

#ifdef NOT_YET
	((control_tmp & UART_MCR_RTS) ? TIOCM_RTS : 0)
	    | ((control_tmp & UART_MCR_DTR) ? TIOCM_DTR : 0)
	    | ((status & UART_MSR_RI) ? TIOCM_RNG : 0)
	    | ((status & UART_MSR_DSR) ? TIOCM_DSR : 0)
#else
	result = ((info->state->icount.dcd) ? TIOCM_CAR : 0)
	    | ((info->state->icount.cts) ? TIOCM_CTS : 0);
#endif

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

#define HIGH_BITS_OFFSET ((sizeof(long)-sizeof(int))*8)

static int
set_serial_info(struct async_struct *info, struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct serial_state old_state, *state;
	unsigned int change_irq, change_port;
	int retval = 0;
	ioc4_control_t *control = (ioc4_control_t *) info->tty->driver_data;
	unsigned long new_port, port_flags;

	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;
	state = info->state;
	old_state = *state;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port +=
		    (unsigned long)new_serial.port_high << HIGH_BITS_OFFSET;

	change_irq = new_serial.irq != state->irq;
	change_port = (new_port != ((int)state->port))
	    || (new_serial.hub6 != state->hub6);

	if (!capable(CAP_SYS_ADMIN)) {
		if (change_irq || change_port ||
		    (new_serial.baud_base != state->baud_base) ||
		    (new_serial.type != state->type) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.xmit_fifo_size != state->xmit_fifo_size) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (state->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		state->flags =
		    ((state->flags & ~ASYNC_USR_MASK) | (new_serial.
							 flags &
							 ASYNC_USR_MASK));
		info->flags =
		    ((info->flags & ~ASYNC_USR_MASK) | (new_serial.
							flags &
							ASYNC_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
	    (new_serial.baud_base < 9600)
	    || (new_serial.type < PORT_UNKNOWN)
	    || (new_serial.type > PORT_MAX)) {
		return -EINVAL;
	}

	if ((change_port || change_irq) && (state->count > 1))
		return -EBUSY;

	state->baud_base = new_serial.baud_base;
	state->flags =
	    ((state->flags & ~ASYNC_FLAGS) | (new_serial.flags & ASYNC_FLAGS));
	info->flags =
	    ((state->flags & ~ASYNC_INTERNAL_FLAGS) | (info->
						       flags &
						       ASYNC_INTERNAL_FLAGS));
	state->custom_divisor = new_serial.custom_divisor;
	state->close_delay = new_serial.close_delay * HZ / 100;
	state->closing_wait = new_serial.closing_wait * HZ / 100;
#if (LINUX_VERSION_CODE > 0x20100)
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
#endif
	info->xmit_fifo_size = state->xmit_fifo_size =
	    new_serial.xmit_fifo_size;

	state->type = new_serial.type;
	if (change_port || change_irq) {
		ioc4_shutdown(info);
		state->irq = new_serial.irq;
		info->port = state->port = new_port;
		info->hub6 = state->hub6 = new_serial.hub6;
		if (info->hub6)
			info->io_type = state->io_type = SERIAL_IO_HUB6;
		else if (info->io_type == SERIAL_IO_HUB6)
			info->io_type = state->io_type = SERIAL_IO_PORT;
	}

      check_and_exit:
	if (!state->port || !state->type)
		return 0;
	if (info->flags & ASYNC_INITIALIZED) {
		if (((old_state.flags & ASYNC_SPD_MASK) !=
		     (state->flags & ASYNC_SPD_MASK))
		    || (old_state.custom_divisor != state->custom_divisor)) {
#if (LINUX_VERSION_CODE >= 131394)	/* Linux 2.1.66 */
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				info->tty->alt_speed = 57600;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				info->tty->alt_speed = 115200;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				info->tty->alt_speed = 230400;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				info->tty->alt_speed = 460800;
#endif
			SIO_LOCK_PORT(control->ic_sioport, port_flags);
			PROGRESS();
			ioc4_ss_change_speed(info, (struct termios *)0);
			SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
		}
	} else {
		unsigned long port_flags;

		SIO_LOCK_PORT(control->ic_sioport, port_flags);
		retval =
		    ioc4_ss_startup((ioc4_control_t *) info->tty->driver_data);
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	}
	return retval;
}

static int
get_serial_info(struct async_struct *info, struct serial_struct *retinfo)
{
	struct serial_struct tmp;
	struct serial_state *state = info->state;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = state->type;
	tmp.line = state->line;
	tmp.port = state->port;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = state->port >> HIGH_BITS_OFFSET;
	else
		tmp.port_high = 0;
	tmp.irq = state->irq;
	tmp.flags = state->flags;
	tmp.xmit_fifo_size = state->xmit_fifo_size;
	tmp.baud_base = state->baud_base;
	tmp.close_delay = state->close_delay;
	tmp.closing_wait = state->closing_wait;
	tmp.custom_divisor = state->custom_divisor;
	tmp.hub6 = state->hub6;
	tmp.io_type = state->io_type;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static inline int
ioc4_ss_block_til_ready(struct tty_struct *tty, struct file *filp,
			struct async_struct *info, unsigned long *port_flags)
{
#ifdef ENABLE_FLOW_CONTROL

	DECLARE_WAITQUEUE(wait, current);
	struct serial_state *state = info->state;
	int retval;
	int do_clocal = 0, extra_count = 0;
	unsigned long flags;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;

	/* if we're closing, wait and try again */
	PROGRESS();
	if (tty_hung_up_p(filp) || (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		PROGRESS();
		return ((info->
			 flags & ASYNC_HUP_NOTIFY) ? -EAGAIN : -ERESTARTSYS);
#else
		NOT_PROGRESS();
		return -EAGAIN;
#endif
	}

	PROGRESS();
#ifndef	TEST_FLOW_CONTROL
	if ((filp->f_flags & O_NONBLOCK) || (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		PROGRESS();
		return 0;
	}
#endif

	PROGRESS();
	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	retval = 0;
	PROGRESS();
	add_wait_queue(&info->open_wait, &wait);
#ifdef IOC4_DEBUG_TRACE_FLOW_CONTROL
	printk("block_til_ready before block: ttys%d, count = %d\n",
	       state->line, state->count);
#endif
	spin_lock_irqsave(&control->ic_lock, flags);
	if (!tty_hung_up_p(filp)) {
		extra_count = 1;
		state->count--;
	}
	spin_unlock_irqrestore(&control->ic_lock, flags);
	info->blocked_open++;
	PROGRESS();
	while (1) {
		PROGRESS();
		spin_lock_irqsave(&control->ic_lock, flags);
		PROGRESS();
		if (tty->termios->c_cflag & CBAUD) {
			DOWN_SET_DTR(control->ic_sioport, 1);
			PROGRESS();
			DOWN_SET_RTS(control->ic_sioport, 1);
			PROGRESS();
		}
		PROGRESS();
		spin_unlock_irqrestore(&control->ic_lock, flags);
		PROGRESS();
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) || !(info->flags & ASYNC_INITIALIZED)) {
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
		if (!(info->flags & ASYNC_CLOSING) &&
		    (do_clocal || (DOWN_QUERY_DCD(control->ic_sioport)))) {
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}
		}
#ifdef IOC4_DEBUG_TRACE_FLOW_CONTROL
		printk("block_til_ready blocking: ttys%d, count = %d\n",
		       info->line, state->count);
#endif
		SIO_UNLOCK_PORT(control->ic_sioport, *port_flags);
		PROGRESS();
		schedule();
		PROGRESS();
		SIO_LOCK_PORT(control->ic_sioport, *port_flags);
		PROGRESS();
	}
	PROGRESS();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (extra_count)
		state->count++;
	info->blocked_open--;
#ifdef IOC4_DEBUG_TRACE_FLOW_CONTROL
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       info->line, state->count);
#endif

	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
#endif				/* ENABLE_FLOW_CONTROL */
	return 0;
}

/*********************************************************************
 *
 * callback functions
 */

/* quick function to validate the icount path */
static inline int valid_icount_path(sioport_t * port)
{
	if (!port)
		return 0;
	if (!port->sio_upper)
		return 0;
	if (!((ioc4_control_t *) port->sio_upper)->ic_info)
		return 0;
	if (!((ioc4_control_t *) port->sio_upper)->ic_info->state)
		return 0;
	return 1;
}

/* called when data has arrived */
static void ioc4_cb_data_ready(sioport_t * port)
{
#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
	/* SIO_LOCK_PORT() is set on call here */
	if (valid_icount_path(port)) {
		ioc4_ss_receive_chars(((ioc4_control_t *) port->sio_upper)->
				      ic_info);
	}
}

/* called when the output low water mark is hit */
#ifdef ENABLE_OUTPUT_INTERRUPTS
static void ioc4_cb_output_lowat(sioport_t * port)
{
#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
	/* SIO_LOCK_PORT() is set on the call here */
	if (valid_icount_path(port)) {
		ioc4_control_t *control = (ioc4_control_t *) port->sio_upper;
		ioc4_ss_transmit_chars(control->ic_info);
	}
}
#endif				/* ENABLE_OUTPUT_INTERRUPTS */

/* called for some basic errors */
static void ioc4_cb_post_ncs(sioport_t * port, int ncs)
{
	struct async_icount *icount;

#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
	if (valid_icount_path(port)) {
		icount =
		    &((ioc4_control_t *) port->sio_upper)->ic_info->state->
		    icount;

		if (ncs & NCS_BREAK)
			icount->brk++;
		if (ncs & NCS_FRAMING)
			icount->frame++;
		if (ncs & NCS_OVERRUN)
			icount->overrun++;
		if (ncs & NCS_PARITY)
			icount->parity++;
	}
}

/* received delta DCD interrupt */
static void ioc4_cb_ddcd(sioport_t * port, int dcd)
{
#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
	if (valid_icount_path(port)) {
		ioc4_control_t *control = (ioc4_control_t *) port->sio_upper;
		control->ic_info->state->icount.dcd = dcd;
		wake_up_interruptible(&control->ic_info->delta_msr_wait);
	}
}

/* received delta CTS interrupt */
static void ioc4_cb_dcts(sioport_t * port, int cts)
{
#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
	if (valid_icount_path(port)) {
		ioc4_control_t *control = (ioc4_control_t *) port->sio_upper;
		control->ic_info->state->icount.cts = cts;
		wake_up_interruptible(&control->ic_info->delta_msr_wait);
	}
}

/* the port has been detached */
static void ioc4_cb_detach(sioport_t * port)
{
#ifdef TRACE_NOTIFICATION_FUNCS
	printk("%s : %d\n", __FUNCTION__, __LINE__);
#endif
}

/*
 * startup the interface
 */

static int ioc4_ss_startup(ioc4_control_t * control)
{
	int retval = 0;
	unsigned long page;
	unsigned long flags;
	struct async_struct *info = control->ic_info;
	struct serial_state *state = info->state;

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	spin_lock_irqsave(&control->ic_lock, flags);

	if (info->flags & ASYNC_INITIALIZED) {
		free_page(page);
		NOT_PROGRESS();
		goto errout;
	}

	if (!state->type) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		free_page(page);
		NOT_PROGRESS();
		goto errout;
	}
	if (info->xmit.buf)
		free_page(page);
	else
		info->xmit.buf = (unsigned char *)page;

	info->prev_port = 0;
	spin_lock_irqsave(&IOC4_irq_lock, flags);
	info->next_port = IOC4_irq_list[state->line];
	if (info->next_port)
		info->next_port->prev_port = info;
	IOC4_irq_list[state->line] = info;
	spin_unlock_irqrestore(&IOC4_irq_lock, flags);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit.head = info->xmit.tail = 0;

	if (info->tty) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			info->tty->alt_speed = 57600;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			info->tty->alt_speed = 115200;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			info->tty->alt_speed = 230400;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			info->tty->alt_speed = 460800;
	}

	/* Call down for the open */
	DOWN_OPEN(control->ic_sioport);

	/* setup callback funcs */
	control->ic_sioport->sio_callup = &ioc4_cb_callup;

	/* set the speed of the serial port */
	PROGRESS();
	ioc4_ss_change_speed(info, (struct termios *)0);

	/* enable hardware flow control - do this after ioc4_ss_change_speed because
	 * ASYNC_CTS_FLOW is set there */
#ifdef ENABLE_FLOW_CONTROL
	if (info->flags & ASYNC_CTS_FLOW) {
		DOWN_ENABLE_HFC(control->ic_sioport, 1);
	}
#endif

	info->flags |= ASYNC_INITIALIZED;
	spin_unlock_irqrestore(&control->ic_lock, flags);
	PROGRESS();
	return 0;

      errout:
	spin_unlock_irqrestore(&control->ic_lock, flags);
	NOT_PROGRESS();
	return retval;
}

/*
 * shutdown interface
 */

static void ioc4_shutdown(struct async_struct *info)
{
	struct serial_state *state;
	unsigned long flags, port_flags;
	ioc4_control_t *control;
	unsigned long ioc4_flags;

	PROGRESS();

	if (!info)
		return;
	if (!info->tty || !info->state)
		return;

	control = (ioc4_control_t *) info->tty->driver_data;
	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	state = info->state;
	spin_lock_irqsave(&control->ic_lock, flags);
	wake_up_interruptible(&info->delta_msr_wait);

	spin_lock_irqsave(&IOC4_irq_lock, ioc4_flags);
	if (info->next_port)
		info->next_port->prev_port = info->prev_port;
	if (info->prev_port)
		info->prev_port->next_port = info->next_port;
	else
		IOC4_irq_list[state->line] = info->next_port;
	spin_unlock_irqrestore(&IOC4_irq_lock, ioc4_flags);

	if (info->xmit.buf != (char *)0) {
		unsigned long pg = (unsigned long)info->xmit.buf;
		info->xmit.buf = (char *)0;
		free_page(pg);
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	//      l1_unconnect_intr();
	SIO_LOCK_PORT(control->ic_sioport, port_flags);
	DOWN_NOTIFICATION(control->ic_sioport, N_ALL, 0);
	SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	info->flags &= ~ASYNC_INITIALIZED;
	spin_unlock_irqrestore(&control->ic_lock, flags);
	PROGRESS();
}

/*
 *
 * End of "support" routines.
 *********************************************************************/

/*********************************************************************
 *
 * Serial interface functions
 *
 */

static int ioc4_serial_open(struct tty_struct *tty, struct file *filp)
{
	int retval, line;
	ioc4_control_t *control;
	struct async_struct *info;
	unsigned long port_flags;

	line = tty->index;
	if ((line < 0) || (line >= IOC4_NUM_SERIAL_PORTS)) {
		NOT_PROGRESS();
		return -ENODEV;
	}
	control = &IOC4_control[line];

	if (control == (ioc4_control_t *) 0) {
		NOT_PROGRESS();
		return -ENODEV;
	} else if (control->ic_sioport == (void *)0) {
		NOT_PROGRESS();
		return -ENODEV;
	}

	/* get and init the async struct for this line */
	retval = ioc4_ss_get_async_struct(line, &info);
	if (retval) {
		NOT_PROGRESS();
		return retval;
	}

	/* Make sure we have lots of ptrs to things */
	control->ic_info = info;
	control->ic_tty = tty;
	control->ic_sioport->sio_upper = (void *)control;
	tty->driver_data = control;
	info->tty = tty;

	/* allocate the tmp buffer */
	if (!IOC4_tmp_buffer) {
		unsigned long page;

		page = get_zeroed_page(GFP_KERNEL);
		if (!page) {
			return -ENOMEM;
		}
		if (IOC4_tmp_buffer)
			free_page(page);
		else
			IOC4_tmp_buffer = (unsigned char *)page;
	}

	/* If the port is the middle of closing, bail out now */
	if (tty_hung_up_p(filp) || (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		NOT_PROGRESS();
		return ((info->
			 flags & ASYNC_HUP_NOTIFY) ? -EAGAIN : -ERESTARTSYS);
#else
		NOT_PROGRESS();
		return -EAGAIN;
#endif
	}

	/* Start up the serial port */
	SIO_LOCK_PORT(control->ic_sioport, port_flags);
	retval = ioc4_ss_startup(control);
	if (retval) {
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
		NOT_PROGRESS();
		return retval;
	}

	/* wait for other end to become 'ready' */
	retval = ioc4_ss_block_til_ready(tty, filp, info, &port_flags);
	if (retval) {
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
		NOT_PROGRESS();
		return retval;
	}

	if (info->state->count == 1)
		ioc4_ss_change_speed(info, (struct termios *)0);

#if defined(HAS_TTY_STRUCT_IN_SIGNAL_STRUCT)
	tty->session = current->signal->session;
#else
	tty->session = current->session;
#endif
	tty->pgrp = process_group(current);

	/* start timer if we are polling */
	if (control->ic_irq == 0)
		mod_timer(&IOC4_timer_list, jiffies + TIMER_WAIT_TIME);
#if TTY_RESTART
	mod_timer(&IOC4_timer_list, jiffies + TIMER_WAIT_TIME);
#endif

	SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	return 0;
}

static void ioc4_serial_close(struct tty_struct *tty, struct file *filp)
{
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;
	struct serial_state *state;
	unsigned long flags, port_flags;

	if (!info)
		return;

	state = info->state;

	spin_lock_irqsave(&control->ic_lock, flags);

	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&control->ic_lock, flags);
		return;
	}

	if ((tty->count == 1) && (state->count != 1)) {
		/* tty->count needs to be 1 as does state->count, if not
		 * it's an error */
		printk
		    ("ioc4_serial_close: bad serial port count; tty->count is 1, "
		     "state->count is %d\n", state->count);
		state->count = 1;
	}
	if (--state->count < 0) {
		printk
		    ("ioc4_serial_close: bad serial port count for ttys%d: %d\n",
		     info->line, state->count);
		state->count = 0;
	}
	if (state->count) {
		spin_unlock_irqrestore(&control->ic_lock, flags);
		return;
	}

	info->flags |= ASYNC_CLOSING;
	spin_unlock_irqrestore(&control->ic_lock, flags);

	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);

	ioc4_shutdown(info);
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

	/* clean up */
	control->ic_info = 0;
	control->ic_tty = 0;
	tty->driver_data = 0;
	/* shutdown all notification */
	SIO_LOCK_PORT(control->ic_sioport, port_flags);
	DOWN_NOTIFICATION(control->ic_sioport, N_ALL, 0);
	SIO_UNLOCK_PORT(control->ic_sioport, port_flags);

	spin_lock_irqsave(&IOC4_irq_lock, flags);
	IOC4_irq_list[state->line] = (struct async_struct *)0;
	spin_unlock_irqrestore(&IOC4_irq_lock, flags);

	/* Clear the 'initialized' flag */
	spin_lock_irqsave(&control->ic_lock, flags);
	info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	spin_unlock_irqrestore(&control->ic_lock, flags);
	wake_up_interruptible(&info->close_wait);
}

static int
ioc4_serial_write(struct tty_struct *tty, int from_user,
		  const unsigned char *buf, int count)
{
	int c, ret = 0;
	unsigned long flags;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;

	if (!tty || !info->xmit.buf || !IOC4_tmp_buffer) {
		NOT_PROGRESS();
		return 0;
	}

	if (from_user) {
		down(&IOC4_tmp_sem);
		while (1) {
			int c1;
			spin_lock_irqsave(&control->ic_lock, flags);
			c = CIRC_SPACE_TO_END(info->xmit.head, info->xmit.tail,
					      SERIAL_XMIT_SIZE);

			if (count < c)
				c = count;
			if (c <= 0) {
				spin_unlock_irqrestore(&control->ic_lock,
						       flags);
				break;
			}

			c -= copy_from_user(IOC4_tmp_buffer, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				spin_unlock_irqrestore(&control->ic_lock,
						       flags);
				break;
			}

			c1 = CIRC_SPACE_TO_END(info->xmit.head, info->xmit.tail,
					       SERIAL_XMIT_SIZE);

			if (c1 < c)
				c = c1;

			memcpy(info->xmit.buf + info->xmit.head,
			       IOC4_tmp_buffer, c);
			info->xmit.head =
			    ((info->xmit.head + c) & (SERIAL_XMIT_SIZE - 1));
			spin_unlock_irqrestore(&control->ic_lock, flags);

			buf += c;
			count -= c;
			ret += c;
		}
		up(&IOC4_tmp_sem);
	} else {
		while (1) {
			spin_lock_irqsave(&control->ic_lock, flags);
			c = CIRC_SPACE_TO_END(info->xmit.head, info->xmit.tail,
					      SERIAL_XMIT_SIZE);

			if (count < c)
				c = count;
			if (c <= 0) {
				spin_unlock_irqrestore(&control->ic_lock,
						       flags);
				break;
			}
			memcpy(info->xmit.buf + info->xmit.head, buf, c);
			info->xmit.head =
			    ((info->xmit.head + c) & (SERIAL_XMIT_SIZE - 1));
			spin_unlock_irqrestore(&control->ic_lock, flags);
			buf += c;
			count -= c;
			ret += c;
		}
	}

	if ((info->xmit.head != info->xmit.tail) && !tty->stopped
	    && !tty->hw_stopped) {
		unsigned long port_flags;

		SIO_LOCK_PORT(control->ic_sioport, port_flags);
		ioc4_ss_transmit_chars(info);
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	}
	return ret;
}

static void ioc4_serial_put_char(struct tty_struct *tty, unsigned char ch)
{
	unsigned long flags;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;

	if (!tty || !info->xmit.buf) {
		NOT_PROGRESS();
		return;
	}

	spin_lock_irqsave(&control->ic_lock, flags);
	if (CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE) == 0) {
		spin_unlock_irqrestore(&control->ic_lock, flags);
		return;
	}

	info->xmit.buf[info->xmit.head] = ch;
	info->xmit.head = (info->xmit.head + 1) & (SERIAL_XMIT_SIZE - 1);
	spin_unlock_irqrestore(&control->ic_lock, flags);
}

static void ioc4_serial_flush_chars(struct tty_struct *tty)
{
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;
	unsigned long port_flags;

	if (CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE)) {
		SIO_LOCK_PORT(control->ic_sioport, port_flags);
		ioc4_ss_transmit_chars(info);
		SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	}
}

static int ioc4_serial_write_room(struct tty_struct *tty)
{
	struct async_struct *info =
	    ((ioc4_control_t *) tty->driver_data)->ic_info;

	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static int ioc4_serial_chars_in_buffer(struct tty_struct *tty)
{
	struct async_struct *info =
	    ((ioc4_control_t *) tty->driver_data)->ic_info;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static int
ioc4_serial_ioctl(struct tty_struct *tty, struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	struct async_icount cprev, cnow;	/* kernel counter temps */
	struct serial_icounter_struct icount;
	unsigned long flags;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}
#ifdef IOC4_DEBUG_TRACE_PROGRESS
	printk("%s : cmd 0x%x\n", __FUNCTION__, cmd);
#endif

	switch (cmd) {
	case TIOCMGET:
		PROGRESS();
		return get_modem_info(info, (unsigned int *)arg);

	case TIOCGSERIAL:
		PROGRESS();
		return get_serial_info(info, (struct serial_struct *)arg);

	case TIOCSSERIAL:
		PROGRESS();
		return set_serial_info(info, (struct serial_struct *)arg);

	case TIOCSERCONFIG:
		PROGRESS();
		return 0;

	case TIOCSERGSTRUCT:
		PROGRESS();
		if (copy_to_user
		    ((struct async_struct *)arg, info,
		     sizeof(struct async_struct)))
			return -EFAULT;
		return 0;

		/*
		   * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		   * - mask passed in arg for lines of interest
		   *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		   * Caller should use TIOCGICOUNT to see which one it was
		 */

		/*
		 * here we wait for any of DCD, RI, DSR or CTS to change. TIOCGICOUNT
		 * is the call to figure WHICH of these it is.
		 */
	case TIOCMIWAIT:
		PROGRESS();
		spin_lock_irqsave(&control->ic_lock, flags);
		cprev = info->state->icount;
		spin_unlock_irqrestore(&control->ic_lock, flags);
		while (1) {
			interruptible_sleep_on(&info->delta_msr_wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			spin_lock_irqsave(&control->ic_lock, flags);
			cnow = info->state->icount;	/* atomic copy */
			spin_unlock_irqrestore(&control->ic_lock, flags);
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr
			    && cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO;	/* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng))
			    || ((arg & TIOCM_DSR)
				&& (cnow.dsr != cprev.dsr))
			    || ((arg & TIOCM_CD)
				&& (cnow.dcd != cprev.dcd))
			    || ((arg & TIOCM_CTS)
				&& (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */

	case TIOCGICOUNT:
		PROGRESS();
		spin_lock_irqsave(&control->ic_lock, flags);
		cnow = info->state->icount;
		spin_unlock_irqrestore(&control->ic_lock, flags);
		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;

		if (copy_to_user((void *)arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;
	case TIOCSERGWILD:
	case TIOCSERSWILD:
		printk("TIOCSER?WILD ioctl obsolete, ignored.\n");
		return 0;

	default:
		NOT_PROGRESS();
		return -ENOIOCTLCMD;
	}
	PROGRESS();
	return 0;
}

static void
ioc4_serial_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;
	unsigned int cflag = tty->termios->c_cflag;
	unsigned long port_flags;

	if ((cflag == old_termios->c_cflag)
	    && (RELEVANT_IFLAG(tty->termios->c_iflag) ==
		RELEVANT_IFLAG(old_termios->c_iflag)))
		return;

	SIO_LOCK_PORT(control->ic_sioport, port_flags);
	PROGRESS();
	ioc4_ss_change_speed(info, old_termios);
	SIO_UNLOCK_PORT(control->ic_sioport, port_flags);
	return;
}

static void ioc4_serial_flush_buffer(struct tty_struct *tty)
{
	unsigned long flags;
	ioc4_control_t *control = (ioc4_control_t *) tty->driver_data;
	struct async_struct *info = control->ic_info;

	PROGRESS();

	spin_lock_irqsave(&control->ic_lock, flags);
	info->xmit.head = info->xmit.tail = 0;
	spin_unlock_irqrestore(&control->ic_lock, flags);

	wake_up_interruptible(&tty->write_wait);

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
	    && tty->ldisc.write_wakeup) {
		(tty->ldisc.write_wakeup) (tty);
	}
	PROGRESS();
}

static void ioc4_serial_hangup(struct tty_struct *tty)
{
	struct async_struct *info =
	    ((ioc4_control_t *) tty->driver_data)->ic_info;
	struct serial_state *state = info->state;

	PROGRESS();
	state = info->state;

	ioc4_serial_flush_buffer(tty);
	if (info->flags & ASYNC_CLOSING)
		return;
	ioc4_shutdown(info);
	info->event = 0;
	state->count = 0;
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
	PROGRESS();
}

/*
 * ioc4_serial_read_proc
 *
 * Console /proc interface
 */

static inline int
ioc4_serial_line_info(char *buf, struct serial_state *state, int which)
{
	int ret;

	ret = sprintf(buf, "%d: port:%d irq:%d tx/rx:%d/%d\n", state->line,
		      which, state->irq, state->icount.tx, state->icount.rx);
	return ret;
}

static int
ioc4_serial_read_proc(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	int len = 0;
	int ii, line_len;
	off_t begin = 0;

	len += sprintf(page, "%s\n", DEVICE_NAME);

	for (ii = 0; ii < IOC4_NUM_SERIAL_PORTS; ii++) {
		line_len =
		    ioc4_serial_line_info(page + len, &IOC4_table[ii], ii);
		len += line_len;
		if ((len + begin) > (off + count))
			goto done;
		if ((len + begin) < off) {
			begin += len;
			len = 0;
		}
	}

	*eof = 1;
      done:
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return ((count < begin + len - off) ? count : begin + len - off);
}

/*
 * Boot-time initialization code
 */

/* we'll steal this one - seems safe */
#define SGI_IOC4_SERIAL_MAJOR	63

/* starting minor device number */
#define IOC4_MINOR	1

/* driver subtype - what does this mean? */
#define IOC4_SUBTYPE	1

static struct tty_operations IOC4_driver_ops = {
	/* Call interface */
	.open = ioc4_serial_open,
	.close = ioc4_serial_close,
	.write = ioc4_serial_write,
	.put_char = ioc4_serial_put_char,
	.flush_chars = ioc4_serial_flush_chars,
	.write_room = ioc4_serial_write_room,
	.chars_in_buffer = ioc4_serial_chars_in_buffer,
	.ioctl = ioc4_serial_ioctl,
	.set_termios = ioc4_serial_set_termios,
	.hangup = ioc4_serial_hangup,
	.read_proc = ioc4_serial_read_proc,
};
static struct tty_driver *IOC4_driver;

static int __init ioc4_serial_init(void)
{
	int ii;
	struct serial_state *state;
	unsigned long flags;
	unsigned int num_ports;

	printk("%s Probing\n", DEVICE_NAME);

	ioc4_serial_detect();

	spin_lock_irqsave(&IOC4_lock, flags);
	init_timer(&IOC4_timer_list);

	IOC4_timer_list.function = ioc4_ss_timer;

	spin_lock_irqsave(&IOC4_irq_lock, flags);
	for (ii = 0; ii < IOC4_NUM_SERIAL_PORTS; ii++) {
		IOC4_irq_list[ii] = (struct async_struct *)0;
	}
	spin_unlock_irqrestore(&IOC4_irq_lock, flags);

	num_ports = DOWN_GET_NUMBER_OF_PORTS();
	printk("%s : number of active ports %d\n", DEVICE_NAME, num_ports);

	if (num_ports == 0) {
		/* Nothing to do */
		spin_unlock_irqrestore(&IOC4_lock, flags);
		return 1;
	}

	IOC4_driver = alloc_tty_driver(num_ports);
	if (!IOC4_driver) {
		printk("%s: Couldn't allocate IOC4 serial driver\n",
		       __FUNCTION__);
		return 1;
	}

	IOC4_driver->owner = THIS_MODULE;
	IOC4_driver->driver_name = "IOC4serial";
	IOC4_driver->name = "ttyIOC4";
	IOC4_driver->major = SGI_IOC4_SERIAL_MAJOR;
	IOC4_driver->minor_start = IOC4_MINOR;
	IOC4_driver->type = TTY_DRIVER_TYPE_SERIAL;
	IOC4_driver->subtype = IOC4_SUBTYPE;
	IOC4_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	IOC4_driver->init_termios = tty_std_termios;
	IOC4_driver->init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;

	tty_set_operations(IOC4_driver, &IOC4_driver_ops);

	if (tty_register_driver(IOC4_driver)) {
		printk("%s: Couldn't register IOC4 serial driver\n",
		       __FUNCTION__);
		spin_unlock_irqrestore(&IOC4_lock, flags);
		return 1;
	}

	for (ii = 0, state = IOC4_table; ii < IOC4_NUM_SERIAL_PORTS;
	     ii++, state++) {
		state->magic = SSTATE_MAGIC;
		state->line = ii;
		IOC4_control[ii].ic_lock = SPIN_LOCK_UNLOCKED;
		state->type = PORT_UNKNOWN;
		state->custom_divisor = 0;
		state->close_delay = 5 * HZ / 10;
		state->closing_wait = 30 * HZ;
		state->icount.cts = state->icount.dsr = state->icount.rng =
		    state->icount.dcd = 0;
		state->icount.rx = state->icount.tx = 0;
		state->icount.frame = state->icount.parity = 0;
		state->icount.overrun = state->icount.brk = 0;

		/* default Port setup */
		state->type = PORT_16550A;
		state->xmit_fifo_size = IOC4_MAX_CHARS;
	}
	for (ii = 0, state = IOC4_table; ii < IOC4_NUM_SERIAL_PORTS;
	     ii++, state++) {
		if (state->type == PORT_UNKNOWN)
			continue;
		state->irq = IOC4_control[ii].ic_irq;
		printk(KERN_INFO "IOC4 serial driver port %d irq = %d\n",
		       state->line, state->irq);
	}
	spin_unlock_irqrestore(&IOC4_lock, flags);
	return 0;
}

static void __exit ioc4_serial_fini(void)
{
	unsigned long flags;
	int e;

	spin_lock_irqsave(&IOC4_lock, flags);
	del_timer_sync(&IOC4_timer_list);

	if ((e = tty_unregister_driver(IOC4_driver)))
		printk("IOC4 serial: failed to unregister driver (%d)\n", e);

	spin_unlock_irqrestore(&IOC4_lock, flags);

	if (IOC4_tmp_buffer) {
		unsigned long pg = (unsigned long)IOC4_tmp_buffer;
		IOC4_tmp_buffer = NULL;
		free_page(pg);
	}
}

module_init(ioc4_serial_init);
module_exit(ioc4_serial_fini);

/*
 * Module licensing and description
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Gefre");
MODULE_DESCRIPTION("IOC4 serial driver");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);
