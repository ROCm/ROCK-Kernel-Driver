/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */

/* In general, this file is organized in a hierarchy from lower-level
 * to higher-level layers, as follows:
 *
 *	UART routines
 *	Bedrock/L1 "PPP-like" protocol implementation
 *	System controller "message" interface (allows multiplexing
 *		of various kinds of requests and responses with
 *		console I/O)
 *	Console interface:
 *	  "l1_cons", the glue that allows the L1 to act
 *		as the system console for the stdio libraries
 *
 * Routines making use of the system controller "message"-style interface
 * can be found in l1_command.c.
 */


#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/uart16550.h>
#include <asm/sn/simulator.h>


/* Make all console writes atomic */
#define SYNC_CONSOLE_WRITE	1


/*********************************************************************
 * Hardware-level (UART) driver routines.
 */

/* macros for reading/writing registers */

#define LD(x)			(*(volatile uint64_t *)(x))
#define SD(x, v)        	(LD(x) = (uint64_t) (v))

/* location of uart receive/xmit data register */
#if defined(CONFIG_IA64_SGI_SN1)
#define L1_UART_BASE(n)		((ulong)REMOTE_HSPEC_ADDR((n), 0x00000080))
#define LOCK_HUB		REMOTE_HUB_ADDR
#elif defined(CONFIG_IA64_SGI_SN2)
#define L1_UART_BASE(n)		((ulong)REMOTE_HUB((n), SH_JUNK_BUS_UART0))
#define LOCK_HUB		REMOTE_HUB
typedef u64 rtc_time_t;
#endif


#define ADDR_L1_REG(n, r)	( L1_UART_BASE(n) | ( (r) << 3 ) )
#define READ_L1_UART_REG(n, r)	( LD(ADDR_L1_REG((n), (r))) )
#define WRITE_L1_UART_REG(n, r, v) ( SD(ADDR_L1_REG((n), (r)), (v)) )

/* upper layer interface calling methods */
#define SERIAL_INTERRUPT_MODE	0
#define SERIAL_POLLED_MODE	1


/* UART-related #defines */

#define UART_BAUD_RATE		57600
#define UART_FIFO_DEPTH		16
#define UART_DELAY_SPAN		10
#define UART_PUTC_TIMEOUT	50000
#define UART_INIT_TIMEOUT	100000

/* error codes */
#define UART_SUCCESS		  0
#define UART_TIMEOUT		(-1)
#define UART_LINK		(-2)
#define UART_NO_CHAR		(-3)
#define UART_VECTOR		(-4)

#define UART_DELAY(x)		udelay(x)

/* Some debug counters */
#define L1C_INTERRUPTS		0
#define L1C_OUR_R_INTERRUPTS	1
#define L1C_OUR_X_INTERRUPTS	2
#define L1C_SEND_CALLUPS	3
#define L1C_RECEIVE_CALLUPS	4
#define L1C_SET_BAUD		5
#define L1C_ALREADY_LOCKED	L1C_SET_BAUD
#define L1C_R_IRQ		6
#define L1C_R_IRQ_RET		7
#define L1C_LOCK_TIMEOUTS	8
#define L1C_LOCK_COUNTER	9
#define L1C_UNLOCK_COUNTER	10
#define L1C_REC_STALLS		11
#define L1C_CONNECT_CALLS	12
#define L1C_SIZE		L1C_CONNECT_CALLS	/* Set to the last one */

uint64_t L1_collectibles[L1C_SIZE + 1];


/*
 *	Some macros for handling Endian-ness
 */

#define COPY_INT_TO_BUFFER(_b, _i, _n)		\
	{					\
		_b[_i++] = (_n >> 24) & 0xff;	\
		_b[_i++] = (_n >> 16) & 0xff;	\
		_b[_i++] = (_n >>  8) & 0xff;	\
		_b[_i++] =  _n        & 0xff;	\
	}

#define COPY_BUFFER_TO_INT(_b, _i, _n)		\
	{					\
		_n  = (_b[_i++] << 24) & 0xff;	\
		_n |= (_b[_i++] << 16) & 0xff;	\
		_n |= (_b[_i++] <<  8) & 0xff;	\
		_n |=  _b[_i++]        & 0xff;	\
	}

#define COPY_BUFFER_TO_BUFFER(_b, _i, _bn)	\
	{					\
	    char *_xyz = (char *)_bn;		\
	    _xyz[3] = _b[_i++];			\
	    _xyz[2] = _b[_i++];			\
	    _xyz[1] = _b[_i++];			\
	    _xyz[0] = _b[_i++];			\
	}

void snia_kmem_free(void *where, int size);

#define ALREADY_LOCKED		1
#define NOT_LOCKED		0
static int early_l1_serial_out(nasid_t, char *, int, int /* defines above*/ );

#define BCOPY(x,y,z)	memcpy(y,x,z)

uint8_t L1_interrupts_connected;		/* Non-zero when we are in interrupt mode */


/*
 * Console locking defines and functions.
 *
 */

uint8_t L1_cons_is_inited = 0;			/* non-zero when console is init'd */
nasid_t Master_console_nasid = (nasid_t)-1;
extern nasid_t console_nasid;

u64 ia64_sn_get_console_nasid(void);

inline nasid_t
get_master_nasid(void)
{
#if defined(CONFIG_IA64_SGI_SN1)
	nasid_t nasid = Master_console_nasid;

	if ( nasid == (nasid_t)-1 ) {
		nasid = (nasid_t)ia64_sn_get_console_nasid();
		if ( (nasid < 0) || (nasid >= MAX_NASIDS) ) {
			/* Out of bounds, use local */
			console_nasid = nasid = get_nasid();
		}
		else {
			/* Got a valid nasid, set the console_nasid */
			char xx[100];
/* zzzzzz - force nasid to 0 for now */
			sprintf(xx, "Master console is set to nasid %d (%d)\n", 0, (int)nasid);
nasid = 0;
/* end zzzzzz */
			xx[99] = (char)0;
			early_l1_serial_out(nasid, xx, strlen(xx), NOT_LOCKED);
			Master_console_nasid = console_nasid = nasid;
		}
	}
	return(nasid);
#else
	return((nasid_t)0);
#endif	/* CONFIG_IA64_SGI_SN1 */
}


#if defined(CONFIG_IA64_SGI_SN1)

#define HUB_LOCK		16

#define PRIMARY_LOCK_TIMEOUT    10000000
#define HUB_LOCK_REG(n)         LOCK_HUB(n, MD_PERF_CNT0)

#define SET_BITS(reg, bits)     SD(reg, LD(reg) |  (bits))
#define CLR_BITS(reg, bits)     SD(reg, LD(reg) & ~(bits))
#define TST_BITS(reg, bits)     ((LD(reg) & (bits)) != 0)

#define HUB_TEST_AND_SET(n)	LD(LOCK_HUB(n,LB_SCRATCH_REG3_RZ))
#define HUB_CLEAR(n)		SD(LOCK_HUB(n,LB_SCRATCH_REG3),0)

#define RTC_TIME_MAX		((rtc_time_t) ~0ULL)

/*
 * primary_lock
 *
 *   Allows CPU's 0-3  to mutually exclude the hub from one another by
 *   obtaining a blocking lock.  Does nothing if only one CPU is active.
 *
 *   This lock should be held just long enough to set or clear a global
 *   lock bit.  After a relatively short timeout period, this routine
 *   figures something is wrong, and steals the lock. It does not set
 *   any other CPU to "dead".
 */
inline void
primary_lock(nasid_t nasid)
{
	rtc_time_t          expire;

	expire = rtc_time() + PRIMARY_LOCK_TIMEOUT;

	while (HUB_TEST_AND_SET(nasid)) {
		if (rtc_time() > expire) {
			HUB_CLEAR(nasid);
		}
	}
}

/*
 * primary_unlock (internal)
 *
 *   Counterpart to primary_lock
 */

inline void
primary_unlock(nasid_t nasid)
{
	HUB_CLEAR(nasid);
}

/*
 * hub_unlock
 *
 *   Counterpart to hub_lock_timeout and hub_lock
 */

inline void
hub_unlock(nasid_t nasid, int level)
{
	uint64_t mask = 1ULL << level;

	primary_lock(nasid);
	CLR_BITS(HUB_LOCK_REG(nasid), mask);
	primary_unlock(nasid);
}

/*
 * hub_lock_timeout
 *
 *   Uses primary_lock to implement multiple lock levels.
 *
 *   There are 20 lock levels from 0 to 19 (limited by the number of bits
 *   in HUB_LOCK_REG).  To prevent deadlock, multiple locks should be
 *   obtained in order of increasingly higher level, and released in the
 *   reverse order.
 *
 *   A timeout value of 0 may be used for no timeout.
 *
 *   Returns 0 if successful, -1 if lock times out.
 */

inline int
hub_lock_timeout(nasid_t nasid, int level, rtc_time_t timeout)
{
	uint64_t mask = 1ULL << level;
	rtc_time_t expire = (timeout ?  rtc_time() + timeout : RTC_TIME_MAX);
	int done    = 0;

	while (! done) {
		while (TST_BITS(HUB_LOCK_REG(nasid), mask)) {
			if (rtc_time() > expire)
				return -1;
		}

		primary_lock(nasid);

		if (! TST_BITS(HUB_LOCK_REG(nasid), mask)) {
			SET_BITS(HUB_LOCK_REG(nasid), mask);
			done = 1;
		}
		primary_unlock(nasid);
	}
	return 0;
}


#define LOCK_TIMEOUT	(0x1500000 * 1) /* 0x1500000 is ~30 sec */

void
lock_console(nasid_t nasid)
{
	int ret;

	/* If we already have it locked, just return */
	L1_collectibles[L1C_LOCK_COUNTER]++;

	ret = hub_lock_timeout(nasid, HUB_LOCK, (rtc_time_t)LOCK_TIMEOUT);
	if ( ret != 0 ) {
		L1_collectibles[L1C_LOCK_TIMEOUTS]++;
		/* timeout */
		hub_unlock(nasid, HUB_LOCK);
		/* If the 2nd lock fails, just pile ahead.... */
		hub_lock_timeout(nasid, HUB_LOCK, (rtc_time_t)LOCK_TIMEOUT);
		L1_collectibles[L1C_LOCK_TIMEOUTS]++;
	}
}

inline void
unlock_console(nasid_t nasid)
{
	L1_collectibles[L1C_UNLOCK_COUNTER]++;
	hub_unlock(nasid, HUB_LOCK);
}

#else /* SN2 */
inline void lock_console(nasid_t n)	{}
inline void unlock_console(nasid_t n)	{}

#endif	/* CONFIG_IA64_SGI_SN1 */

int 
get_L1_baud(void)
{
    return UART_BAUD_RATE;
}


/* uart driver functions */

static inline void
uart_delay( rtc_time_t delay_span )
{
    UART_DELAY( delay_span );
}

#define UART_PUTC_READY(n)      (READ_L1_UART_REG((n), REG_LSR) & LSR_XHRE)

static int
uart_putc( l1sc_t *sc ) 
{
    WRITE_L1_UART_REG( sc->nasid, REG_DAT, sc->send[sc->sent] );
    return UART_SUCCESS;
}


static int
uart_getc( l1sc_t *sc )
{
    u_char lsr_reg = 0;
    nasid_t nasid = sc->nasid;

    if( (lsr_reg = READ_L1_UART_REG( nasid, REG_LSR )) & 
	(LSR_RCA | LSR_PARERR | LSR_FRMERR) ) 
    {
	if( lsr_reg & LSR_RCA ) 
	    return( (u_char)READ_L1_UART_REG( nasid, REG_DAT ) );
	else if( lsr_reg & (LSR_PARERR | LSR_FRMERR) ) {
	    return UART_LINK;
	}
    }

    return UART_NO_CHAR;
}


#define PROM_SER_CLK_SPEED	12000000
#define PROM_SER_DIVISOR(x)	(PROM_SER_CLK_SPEED / ((x) * 16))

static void
uart_init( l1sc_t *sc, int baud )
{
    rtc_time_t expire;
    int clkdiv;
    nasid_t nasid;

    clkdiv = PROM_SER_DIVISOR(baud);
    expire = rtc_time() + UART_INIT_TIMEOUT;
    nasid = sc->nasid;
    
    /* make sure the transmit FIFO is empty */
    while( !(READ_L1_UART_REG( nasid, REG_LSR ) & LSR_XSRE) ) {
	uart_delay( UART_DELAY_SPAN );
	if( rtc_time() > expire ) {
	    break;
	}
    }

    if ( sc->uart == BRL1_LOCALHUB_UART )
	lock_console(nasid);

    /* Setup for the proper baud rate */
    WRITE_L1_UART_REG( nasid, REG_LCR, LCR_DLAB );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_DLH, (clkdiv >> 8) & 0xff );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_DLL, clkdiv & 0xff );
	uart_delay( UART_DELAY_SPAN );

    /* set operating parameters and set DLAB to 0 */

    /* 8bit, one stop, clear request to send, auto flow control */
    WRITE_L1_UART_REG( nasid, REG_LCR, LCR_BITS8 | LCR_STOP1 );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_MCR, MCR_RTS | MCR_AFE );
	uart_delay( UART_DELAY_SPAN );

    /* disable interrupts */
    WRITE_L1_UART_REG( nasid, REG_ICR, 0x0 );
	uart_delay( UART_DELAY_SPAN );

    /* enable FIFO mode and reset both FIFOs, trigger on 1 */
    WRITE_L1_UART_REG( nasid, REG_FCR, FCR_FIFOEN );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_FCR, FCR_FIFOEN | FCR_RxFIFO | FCR_TxFIFO | RxLVL0);

    if ( sc->uart == BRL1_LOCALHUB_UART )
	unlock_console(nasid);
}

/* This requires the console lock */

#if	defined(CONFIG_IA64_SGI_SN1)

static void
uart_intr_enable( l1sc_t *sc, u_char mask )
{
    u_char lcr_reg, icr_reg;
    nasid_t nasid = sc->nasid;

    if ( sc->uart == BRL1_LOCALHUB_UART )
	lock_console(nasid);

    /* make sure that the DLAB bit in the LCR register is 0
     */
    lcr_reg = READ_L1_UART_REG( nasid, REG_LCR );
    lcr_reg &= ~(LCR_DLAB);
    WRITE_L1_UART_REG( nasid, REG_LCR, lcr_reg );

    /* enable indicated interrupts
     */
    icr_reg = READ_L1_UART_REG( nasid, REG_ICR );
    icr_reg |= mask;
    WRITE_L1_UART_REG( nasid, REG_ICR, icr_reg /*(ICR_RIEN | ICR_TIEN)*/ );

    if ( sc->uart == BRL1_LOCALHUB_UART )
	unlock_console(nasid);
}

/* This requires the console lock */
static void
uart_intr_disable( l1sc_t *sc, u_char mask )
{
    u_char lcr_reg, icr_reg;
    nasid_t nasid = sc->nasid;

    if ( sc->uart == BRL1_LOCALHUB_UART )
	lock_console(nasid);

    /* make sure that the DLAB bit in the LCR register is 0
     */
    lcr_reg = READ_L1_UART_REG( nasid, REG_LCR );
    lcr_reg &= ~(LCR_DLAB);
    WRITE_L1_UART_REG( nasid, REG_LCR, lcr_reg );

    /* enable indicated interrupts
     */
    icr_reg = READ_L1_UART_REG( nasid, REG_ICR );
    icr_reg &= mask;
    WRITE_L1_UART_REG( nasid, REG_ICR, icr_reg /*(ICR_RIEN | ICR_TIEN)*/ );

    if ( sc->uart == BRL1_LOCALHUB_UART )
	unlock_console(nasid);
}
#endif	/* CONFIG_IA64_SGI_SN1 */

#define uart_enable_xmit_intr(sc) \
	uart_intr_enable((sc), ICR_TIEN)

#define uart_disable_xmit_intr(sc) \
        uart_intr_disable((sc), ~(ICR_TIEN))

#define uart_enable_recv_intr(sc) \
        uart_intr_enable((sc), ICR_RIEN)

#define uart_disable_recv_intr(sc) \
        uart_intr_disable((sc), ~(ICR_RIEN))


/*********************************************************************
 * Routines for accessing a remote (router) UART
 */

#define READ_RTR_L1_UART_REG(p, n, r, v)		\
    {							\
	if( vector_read_node( (p), (n), 0,		\
			      RR_JBUS1(r), (v) ) ) {	\
	    return UART_VECTOR;				\
	}						\
    }

#define WRITE_RTR_L1_UART_REG(p, n, r, v)		\
    {							\
	if( vector_write_node( (p), (n), 0,		\
			       RR_JBUS1(r), (v) ) ) {	\
	    return UART_VECTOR;				\
	}						\
    }

#define RTR_UART_PUTC_TIMEOUT	UART_PUTC_TIMEOUT*10
#define RTR_UART_DELAY_SPAN	UART_DELAY_SPAN
#define RTR_UART_INIT_TIMEOUT	UART_INIT_TIMEOUT*10

static int
rtr_uart_putc( l1sc_t *sc )
{
    uint64_t regval, c;
    nasid_t nasid = sc->nasid;
    net_vec_t path = sc->uart;
    rtc_time_t expire = rtc_time() + RTR_UART_PUTC_TIMEOUT;

    c = (sc->send[sc->sent] & 0xffULL);
    
    while( 1 ) 
    {
        /* Check for "tx hold reg empty" bit. */
	READ_RTR_L1_UART_REG( path, nasid, REG_LSR, &regval );
	if( regval & LSR_XHRE )
	{
	    WRITE_RTR_L1_UART_REG( path, nasid, REG_DAT, c );
	    return UART_SUCCESS;
	}

	if( rtc_time() >= expire ) 
	{
	    return UART_TIMEOUT;
	}
	uart_delay( RTR_UART_DELAY_SPAN );
    }
}


static int
rtr_uart_getc( l1sc_t *sc )
{
    uint64_t regval;
    nasid_t nasid = sc->nasid;
    net_vec_t path = sc->uart;

    READ_RTR_L1_UART_REG( path, nasid, REG_LSR, &regval );
    if( regval & (LSR_RCA | LSR_PARERR | LSR_FRMERR) )
    {
	if( regval & LSR_RCA )
	{
	    READ_RTR_L1_UART_REG( path, nasid, REG_DAT, &regval );
	    return( (int)regval );
	}
	else
	{
	    return UART_LINK;
	}
    }

    return UART_NO_CHAR;
}


static int
rtr_uart_init( l1sc_t *sc, int baud )
{
    rtc_time_t expire;
    int clkdiv;
    nasid_t nasid;
    net_vec_t path;
    uint64_t regval;

    clkdiv = PROM_SER_DIVISOR(baud);
    expire = rtc_time() + RTR_UART_INIT_TIMEOUT;
    nasid = sc->nasid;
    path = sc->uart;

    /* make sure the transmit FIFO is empty */
    while(1) {
	READ_RTR_L1_UART_REG( path, nasid, REG_LSR, &regval );
	if( regval & LSR_XSRE ) {
	    break;
	}
	if( rtc_time() > expire ) {
	    break;
	}
	uart_delay( RTR_UART_DELAY_SPAN );
    }

    WRITE_RTR_L1_UART_REG( path, nasid, REG_LCR, LCR_DLAB  );
	uart_delay( UART_DELAY_SPAN );
    WRITE_RTR_L1_UART_REG( path, nasid, REG_DLH, (clkdiv >> 8) & 0xff  );
	uart_delay( UART_DELAY_SPAN );
    WRITE_RTR_L1_UART_REG( path, nasid, REG_DLL, clkdiv & 0xff  );
	uart_delay( UART_DELAY_SPAN );

    /* set operating parameters and set DLAB to 0 */
    WRITE_RTR_L1_UART_REG( path, nasid, REG_LCR, LCR_BITS8 | LCR_STOP1  );
	uart_delay( UART_DELAY_SPAN );
    WRITE_RTR_L1_UART_REG( path, nasid, REG_MCR, MCR_RTS | MCR_AFE  );
	uart_delay( UART_DELAY_SPAN );

    /* disable interrupts */
    WRITE_RTR_L1_UART_REG( path, nasid, REG_ICR, 0x0  );
	uart_delay( UART_DELAY_SPAN );

    /* enable FIFO mode and reset both FIFOs */
    WRITE_RTR_L1_UART_REG( path, nasid, REG_FCR, FCR_FIFOEN  );
	uart_delay( UART_DELAY_SPAN );
    WRITE_RTR_L1_UART_REG( path, nasid, REG_FCR,
	FCR_FIFOEN | FCR_RxFIFO | FCR_TxFIFO );

    return 0;
}

/*********************************************************************
 * locking macros 
 */

#define L1SC_SEND_LOCK(l,p)   { if ((l)->uart == BRL1_LOCALHUB_UART) spin_lock_irqsave(&((l)->send_lock),p); }
#define L1SC_SEND_UNLOCK(l,p) { if ((l)->uart == BRL1_LOCALHUB_UART) spin_unlock_irqrestore(&((l)->send_lock), p); }
#define L1SC_RECV_LOCK(l,p)   { if ((l)->uart == BRL1_LOCALHUB_UART) spin_lock_irqsave(&((l)->recv_lock), p); } 
#define L1SC_RECV_UNLOCK(l,p) { if ((l)->uart == BRL1_LOCALHUB_UART) spin_unlock_irqrestore(&((l)->recv_lock), p); }


/*********************************************************************
 * subchannel manipulation 
 *
 * The SUBCH_[UN]LOCK macros are used to arbitrate subchannel
 * allocation.  SUBCH_DATA_[UN]LOCK control access to data structures
 * associated with particular subchannels (e.g., receive queues).
 *
 */
#define SUBCH_LOCK(sc, p)		spin_lock_irqsave( &((sc)->subch_lock), p )
#define SUBCH_UNLOCK(sc, p)		spin_unlock_irqrestore( &((sc)->subch_lock), p )
#define SUBCH_DATA_LOCK(sbch, p) 	spin_lock_irqsave( &((sbch)->data_lock), p )
#define SUBCH_DATA_UNLOCK(sbch, p)	spin_unlock_irqrestore( &((sbch)->data_lock), p )


/*
 * set a function to be called for subchannel ch in the event of
 * a transmission low-water interrupt from the uart
 */
void
subch_set_tx_notify( l1sc_t *sc, int ch, brl1_notif_t func )
{
    unsigned long pl = 0;

    L1SC_SEND_LOCK( sc, pl );
#if	!defined(SYNC_CONSOLE_WRITE)
    if ( func && !sc->send_in_use )
	uart_enable_xmit_intr( sc );
#endif
    sc->subch[ch].tx_notify = func;
    L1SC_SEND_UNLOCK(sc, pl );
}

/*
 * set a function to be called for subchannel ch when data is received
 */
void
subch_set_rx_notify( l1sc_t *sc, int ch, brl1_notif_t func )
{
    unsigned long pl = 0;
    brl1_sch_t *subch = &(sc->subch[ch]);

    SUBCH_DATA_LOCK( subch, pl );
    sc->subch[ch].rx_notify = func;
    SUBCH_DATA_UNLOCK( subch, pl );
}

/*********************************************************************
 * Queue manipulation macros
 *
 *
 */
#define NEXT(p)         (((p) + 1) & (BRL1_QSIZE-1)) /* assume power of 2 */

#define cq_init(q)      bzero((q), sizeof (*(q)))
#define cq_empty(q)     ((q)->ipos == (q)->opos)
#define cq_full(q)      (NEXT((q)->ipos) == (q)->opos)
#define cq_used(q)      ((q)->opos <= (q)->ipos ?                       \
                         (q)->ipos - (q)->opos :                        \
                         BRL1_QSIZE + (q)->ipos - (q)->opos)
#define cq_room(q)      ((q)->opos <= (q)->ipos ?                       \
                         BRL1_QSIZE - 1 + (q)->opos - (q)->ipos :       \
                         (q)->opos - (q)->ipos - 1)
#define cq_add(q, c)    ((q)->buf[(q)->ipos] = (u_char) (c),            \
                         (q)->ipos = NEXT((q)->ipos))
#define cq_rem(q, c)    ((c) = (q)->buf[(q)->opos],                     \
                         (q)->opos = NEXT((q)->opos))
#define cq_discard(q)	((q)->opos = NEXT((q)->opos))

#define cq_tent_full(q)	(NEXT((q)->tent_next) == (q)->opos)
#define cq_tent_len(q)	((q)->ipos <= (q)->tent_next ?			\
			 (q)->tent_next - (q)->ipos :			\
			 BRL1_QSIZE + (q)->tent_next - (q)->ipos)
#define cq_tent_add(q, c)						\
			((q)->buf[(q)->tent_next] = (u_char) (c),	\
			 (q)->tent_next = NEXT((q)->tent_next))
#define cq_commit_tent(q)						\
			((q)->ipos = (q)->tent_next)
#define cq_discard_tent(q)						\
			((q)->tent_next = (q)->ipos)




/*********************************************************************
 * CRC-16 (for checking bedrock/L1 packets).
 *
 * These are based on RFC 1662 ("PPP in HDLC-like framing").
 */

static unsigned short fcstab[256] = {
      0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
      0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
      0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
      0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
      0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
      0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
      0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
      0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
      0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
      0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
      0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
      0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
      0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
      0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
      0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
      0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
      0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
      0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
      0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
      0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
      0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
      0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
      0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
      0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
      0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
      0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
      0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
      0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
      0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
      0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
      0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
      0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

#define INIT_CRC	0xFFFF	/* initial CRC value	  */
#define	GOOD_CRC	0xF0B8	/* "good" final CRC value */

static unsigned short crc16_calc( unsigned short crc, u_char c )
{
    return( (crc >> 8) ^ fcstab[(crc ^ c) & 0xff] );
}


/***********************************************************************
 * The following functions implement the PPP-like bedrock/L1 protocol
 * layer.
 *
 */

#define BRL1_FLAG_CH	0x7e
#define BRL1_ESC_CH	0x7d
#define BRL1_XOR_CH	0x20

/* L1<->Bedrock packet types */
#define BRL1_REQUEST    0x00
#define BRL1_RESPONSE   0x20
#define BRL1_EVENT      0x40

#define BRL1_PKT_TYPE_MASK      0xE0
#define BRL1_SUBCH_MASK         0x1F

#define PKT_TYPE(tsb)   ((tsb) & BRL1_PKT_TYPE_MASK)
#define SUBCH(tsb)	((tsb) & BRL1_SUBCH_MASK)

/* timeouts */
#define BRL1_INIT_TIMEOUT	500000

/*
 * brl1_discard_packet is a dummy "receive callback" used to get rid
 * of packets we don't want
 */
void brl1_discard_packet( int dummy0, void *dummy1, struct pt_regs *dummy2, l1sc_t *sc, int ch )
{
    unsigned long pl = 0;
    brl1_sch_t *subch = &sc->subch[ch];

    sc_cq_t *q = subch->iqp;
    SUBCH_DATA_LOCK( subch, pl );
    q->opos = q->ipos;
    atomic_set(&(subch->packet_arrived), 0);
    SUBCH_DATA_UNLOCK( subch, pl );
}


/*
 * brl1_send_chars sends the send buffer in the l1sc_t structure
 * out through the uart.  Assumes that the caller has locked the
 * UART (or send buffer in the kernel).
 *
 * This routine doesn't block-- if you want it to, call it in
 * a loop.
 */
static int
brl1_send_chars( l1sc_t *sc )
{
    /* We track the depth of the C brick's UART's
     * fifo in software, and only check if the UART is accepting
     * characters when our count indicates that the fifo should
     * be full.
     *
     * For remote (router) UARTs, we check with the UART before sending every
     * character.
     */
    if( sc->uart == BRL1_LOCALHUB_UART ) {
	if( !(sc->fifo_space) && UART_PUTC_READY( sc->nasid ) )
	    sc->fifo_space = UART_FIFO_DEPTH;
	
	while( (sc->sent < sc->send_len) && (sc->fifo_space) ) {
	    uart_putc( sc );
	    sc->fifo_space--;
	    sc->sent++;
	}
    }
    else {

	/* remote (router) UARTs */

	int result;
	int tries = 0;

	while( sc->sent < sc->send_len ) {
	    result = sc->putc_f( sc );
	    if( result >= 0 ) {
		(sc->sent)++;
		continue;
	    }
	    if( result == UART_TIMEOUT ) {
		tries++;
		/* send this character in TIMEOUT_RETRIES... */
		if( tries < 30 /* TIMEOUT_RETRIES */ ) {
		    continue;
		}
		/* ...or else... */
		else {
		    /* ...drop the packet. */
		    sc->sent = sc->send_len;
		    return sc->send_len;
		}
	    }
	    if( result < 0 ) {
		return result;
	    }
	}
    }
    return sc->sent;
}


/* brl1_send formats up a packet and (at least begins to) send it
 * to the uart.  If the send buffer is in use when this routine obtains
 * the lock, it will behave differently depending on the "wait" parameter.
 * For wait == 0 (most I/O), it will return 0 (as in "zero bytes sent"),
 * hopefully encouraging the caller to back off (unlock any high-level 
 * spinlocks) and allow the buffer some time to drain.  For wait==1 (high-
 * priority I/O along the lines of kernel error messages), we will flush
 * the current contents of the send buffer and beat on the uart
 * until our message has been completely transmitted.
 */

static int
brl1_send( l1sc_t *sc, char *msg, int len, u_char type_and_subch, int wait )
{
    unsigned long pl = 0;
    int index;
    int pkt_len = 0;
    unsigned short crc = INIT_CRC;
    char *send_ptr = sc->send;


    if( sc->send_in_use && !(wait) ) {
	/* We are in the middle of sending, but can wait until done */
	return 0;
    }
    else if( sc->send_in_use ) {
	/* buffer's in use, but we're synchronous I/O, so we're going
	 * to send whatever's in there right now and take the buffer
	 */
	int counter = 0;

	if ( sc->uart == BRL1_LOCALHUB_UART )
		lock_console(sc->nasid);
	L1SC_SEND_LOCK(sc, pl);
	while( sc->sent < sc->send_len ) {
		brl1_send_chars( sc );
		if ( counter++ > 0xfffff ) {
			char *str = "Looping waiting for uart to clear (1)\n";
			early_l1_serial_out(sc->nasid, str, strlen(str), ALREADY_LOCKED);
			break;
		}
	}
    }
    else {
	if ( sc->uart == BRL1_LOCALHUB_UART )
		lock_console(sc->nasid);
	L1SC_SEND_LOCK(sc, pl);
	sc->send_in_use = 1;
    }
    *send_ptr++ = BRL1_FLAG_CH;
    *send_ptr++ = type_and_subch;
    pkt_len += 2;
    crc = crc16_calc( crc, type_and_subch );

    /* limit number of characters accepted to max payload size */
    if( len > (BRL1_QSIZE - 1) )
	len = (BRL1_QSIZE - 1);

    /* copy in the message buffer (inserting PPP 
     * framing info where necessary)
     */
    for( index = 0; index < len; index++ ) {

	switch( *msg ) {
	    
	  case BRL1_FLAG_CH:
	    *send_ptr++ = BRL1_ESC_CH;
	    *send_ptr++ = (*msg) ^ BRL1_XOR_CH;
	    pkt_len += 2;
	    break;
	    
	  case BRL1_ESC_CH:
	    *send_ptr++ = BRL1_ESC_CH;
	    *send_ptr++ = (*msg) ^ BRL1_XOR_CH;
	    pkt_len += 2;
	    break;
	    
	  default:
	    *send_ptr++ = *msg;
	    pkt_len++;
	}
	crc = crc16_calc( crc, *msg );
	msg++;
    }
    crc ^= 0xffff;

    for( index = 0; index < sizeof(crc); index++ ) {
	char crc_char = (char)(crc & 0x00FF);
	if( (crc_char == BRL1_ESC_CH) || (crc_char == BRL1_FLAG_CH) ) {
	    *send_ptr++ = BRL1_ESC_CH;
	    pkt_len++;
	    crc_char ^= BRL1_XOR_CH;
	}
	*send_ptr++ = crc_char;
	pkt_len++;
	crc >>= 8;
    }
    
    *send_ptr++ = BRL1_FLAG_CH;
    pkt_len++;

    sc->send_len = pkt_len;
    sc->sent = 0;

    {
	int counter = 0;
	do {
		brl1_send_chars( sc );
		if ( counter++ > 0xfffff ) {
			char *str = "Looping waiting for uart to clear (2)\n";
			early_l1_serial_out(sc->nasid, str, strlen(str), ALREADY_LOCKED);
			break;
		}
	} while( (sc->sent < sc->send_len) && wait );
    }

    if ( sc->uart == BRL1_LOCALHUB_UART )
	unlock_console(sc->nasid);

    if( sc->sent == sc->send_len ) {
	/* success! release the send buffer and call the callup */
#if	!defined(SYNC_CONSOLE_WRITE)
	brl1_notif_t callup;
#endif

	sc->send_in_use = 0;
	/* call any upper layer that's asked for notification */
#if	defined(XX_SYNC_CONSOLE_WRITE)
	/*
	 * This is probably not a good idea - since the l1_ write func can be called multiple
	 * time within the callup function.
	 */
	callup = subch->tx_notify;
	if( callup && (SUBCH(type_and_subch) == SC_CONS_SYSTEM) ) {
		L1_collectibles[L1C_SEND_CALLUPS]++;
		(*callup)(sc->subch[SUBCH(type_and_subch)].irq_frame.bf_irq,
				sc->subch[SUBCH(type_and_subch)].irq_frame.bf_dev_id,
				sc->subch[SUBCH(type_and_subch)].irq_frame.bf_regs, sc, SUBCH(type_and_subch));
	}
#endif	/* SYNC_CONSOLE_WRITE */
    }
#if	!defined(SYNC_CONSOLE_WRITE)
    else if ( !wait ) {
	/* enable low-water interrupts so buffer will be drained */
	uart_enable_xmit_intr(sc);
    }
#endif

    L1SC_SEND_UNLOCK(sc, pl);

    return len;
}

/* brl1_send_cont is intended to be called as an interrupt service
 * routine.  It sends until the UART won't accept any more characters,
 * or until an error is encountered (in which case we surrender the
 * send buffer and give up trying to send the packet).  Once the
 * last character in the packet has been sent, this routine releases
 * the send buffer and calls any previously-registered "low-water"
 * output routines.
 */

#if	!defined(SYNC_CONSOLE_WRITE)

int
brl1_send_cont( l1sc_t *sc )
{
    unsigned long pl = 0;
    int done = 0;
    brl1_notif_t callups[BRL1_NUM_SUBCHANS];
    brl1_notif_t *callup;
    brl1_sch_t *subch;
    int index;

    /*
     * I'm not sure how I think this is to be handled - whether the lock is held
     * over the interrupt - but it seems like it is a bad idea....
     */

    if ( sc->uart == BRL1_LOCALHUB_UART )
	lock_console(sc->nasid);
    L1SC_SEND_LOCK(sc, pl);
    brl1_send_chars( sc );
    done = (sc->sent == sc->send_len);
    if( done ) {
	sc->send_in_use = 0;
#if	!defined(SYNC_CONSOLE_WRITE)
	uart_disable_xmit_intr(sc);
#endif
    }
    if ( sc->uart == BRL1_LOCALHUB_UART )
	unlock_console(sc->nasid);
    /* Release the lock */
    L1SC_SEND_UNLOCK(sc, pl);

    return 0;
}
#endif	/* SYNC_CONSOLE_WRITE */

/* internal function -- used by brl1_receive to read a character 
 * from the uart and check whether errors occurred in the process.
 */
static int
read_uart( l1sc_t *sc, int *c, int *result )
{
    *c = sc->getc_f( sc );

    /* no character is available */
    if( *c == UART_NO_CHAR ) {
	*result = BRL1_NO_MESSAGE;
	return 0;
    }

    /* some error in UART */
    if( *c < 0 ) {
	*result = BRL1_LINK;
	return 0;
    }

    /* everything's fine */
    *result = BRL1_VALID;
    return 1;
}


/*
 * brl1_receive
 *
 * This function reads a Bedrock-L1 protocol packet into the l1sc_t
 * response buffer.
 *
 * The operation of this function can be expressed as a finite state
 * machine:
 *

START STATE			INPUT		TRANSITION
==========================================================
BRL1_IDLE (reset or error)	flag		BRL1_FLAG
				other		BRL1_IDLE@

BRL1_FLAG (saw a flag (0x7e))	flag		BRL1_FLAG
				escape		BRL1_IDLE@
				header byte	BRL1_HDR
				other		BRL1_IDLE@

BRL1_HDR (saw a type/subch byte)(see below)	BRL1_BODY
						BRL1_HDR

BRL1_BODY (reading packet body)	flag		BRL1_FLAG
				escape		BRL1_ESC
				other		BRL1_BODY

BRL1_ESC (saw an escape (0x7d))	flag		BRL1_FLAG@
				escape		BRL1_IDLE@
				other		BRL1_BODY
==========================================================

"@" denotes an error transition.

 * The BRL1_HDR state is a transient state which doesn't read input,
 * but just provides a way in to code which decides to whom an
 * incoming packet should be directed.
 *
 * brl1_receive can be used to poll for input from the L1, or as 
 * an interrupt service routine.  It reads as much data as is
 * ready from the junk bus UART and places into the appropriate
 * input queues according to subchannel.  The header byte is
 * stripped from console-type data, but is retained for message-
 * type data (L1 responses).  A length byte will also be
 * prepended to message-type packets.
 *
 * This routine is non-blocking; if the caller needs to block
 * for input, it must call brl1_receive in a loop.
 *
 * brl1_receive returns when there is no more input, the queue
 * for the current incoming message is full, or there is an
 * error (parity error, bad header, bad CRC, etc.).
 */

#define STATE_SET(l,s)		((l)->brl1_state = (s))
#define STATE_GET(l)		((l)->brl1_state)

#define LAST_HDR_SET(l,h)	((l)->brl1_last_hdr = (h))
#define LAST_HDR_GET(l)		((l)->brl1_last_hdr)

#define VALID_HDR(c)				\
    ( SUBCH((c)) <= SC_CONS_SYSTEM		\
	? PKT_TYPE((c)) == BRL1_REQUEST		\
	: ( PKT_TYPE((c)) == BRL1_RESPONSE ||	\
	    PKT_TYPE((c)) == BRL1_EVENT ) )

#define IS_TTY_PKT(l)		( SUBCH(LAST_HDR_GET(l)) <= SC_CONS_SYSTEM ? 1 : 0 )


int
brl1_receive( l1sc_t *sc, int mode )
{
    int result;		/* value to be returned by brl1_receive */
    int c;		/* most-recently-read character	     	*/
    int done;		/* set done to break out of recv loop	*/
    unsigned long pl = 0, cpl = 0;
    sc_cq_t *q;		/* pointer to queue we're working with	*/

    result = BRL1_NO_MESSAGE;

    L1SC_RECV_LOCK(sc, cpl);

    done = 0;
    while( !done )
    {
	switch( STATE_GET(sc) )
	{

	  case BRL1_IDLE:
	    /* Initial or error state.  Waiting for a flag character
             * to resynchronize with the L1.
             */

	    if( !read_uart( sc, &c, &result ) ) {

		/* error reading uart */
		done = 1;
		continue;
	    }
	    
	    if( c == BRL1_FLAG_CH ) {
		/* saw a flag character */
		STATE_SET( sc, BRL1_FLAG );
		continue;
	    }
	    break;
	    
	  case BRL1_FLAG:
	    /* One or more flag characters have been read; look for
	     * the beginning of a packet (header byte).
	     */
	    
	    if( !read_uart( sc, &c, &result ) ) {

		/* error reading uart */
		if( c != UART_NO_CHAR )
		    STATE_SET( sc, BRL1_IDLE );

		done = 1;
		continue;
	    }
	    
	    if( c == BRL1_FLAG_CH ) {
		/* multiple flags are OK */
		continue;
	    }

	    if( !VALID_HDR( c ) ) {
		/* if c isn't a flag it should have been
		 * a valid header, so we have an error
		 */
		result = BRL1_PROTOCOL;
		STATE_SET( sc, BRL1_IDLE );
		done = 1;
		continue;
	    }

	    /* we have a valid header byte */
	    LAST_HDR_SET( sc, c );
	    STATE_SET( sc, BRL1_HDR );

	    break; 

	  case BRL1_HDR:
	    /* A header byte has been read. Do some bookkeeping. */
	    q = sc->subch[ SUBCH( LAST_HDR_GET(sc) ) ].iqp;
	    ASSERT(q);
	    
	    if( !IS_TTY_PKT(sc) ) {
		/* if this is an event or command response rather
		 * than console I/O, we need to reserve a couple
		 * of extra spaces in the queue for the header
		 * byte and a length byte; if we can't, stay in
		 * the BRL1_HDR state.
		 */
		if( cq_room( q ) < 2 ) {
		    result = BRL1_FULL_Q;
		    done = 1;
		    continue;
		}
		cq_tent_add( q, 0 );			/* reserve length byte */
		cq_tent_add( q, LAST_HDR_GET( sc ) );	/* record header byte  */
	    }
	    STATE_SET( sc, BRL1_BODY );

	    break;

	  case BRL1_BODY:
	    /* A header byte has been read.  We are now attempting
	     * to receive the packet body.
	     */

	    q = sc->subch[ SUBCH( LAST_HDR_GET(sc) ) ].iqp;
	    ASSERT(q);

	    /* if the queue we want to write into is full, don't read from
	     * the uart (this provides backpressure to the L1 side)
	     */
	    if( cq_tent_full( q ) ) {
		result = BRL1_FULL_Q;
		done = 1;
		continue;
	    }
	    
	    if( !read_uart( sc, &c, &result ) ) {

		/* error reading uart */
		if( c != UART_NO_CHAR )
		    STATE_SET( sc, BRL1_IDLE );
		done = 1;
		continue;
	    }

	    if( c == BRL1_ESC_CH ) {
		/* prepare to unescape the next character */
		STATE_SET( sc, BRL1_ESC );
		continue;
	    }
	    
	    if( c == BRL1_FLAG_CH ) {
		/* flag signifies the end of a packet */

		unsigned short crc;	/* holds the crc as we calculate it */
		int i;			/* index variable */
		brl1_sch_t *subch;      /* subchannel for received packet */
		brl1_notif_t callup;	/* "data ready" callup */

		/* whatever else may happen, we've seen a flag and we're
		 * starting a new packet
		 */
		STATE_SET( sc, BRL1_FLAG );

		/* if the packet body has less than 2 characters,
		 * it can't be a well-formed packet.  Discard it.
		 */
		if( cq_tent_len( q ) < /* 2 + possible length byte */
		    (2 + (IS_TTY_PKT(sc) ? 0 : 1)) )
		{
		    result = BRL1_PROTOCOL;
		    cq_discard_tent( q );
		    STATE_SET( sc, BRL1_FLAG );
		    done = 1;
		    continue;
		}
		
		/* check CRC */

		/* accumulate CRC, starting with the header byte and
		 * ending with the transmitted CRC.  This should
		 * result in a known good value.
		 */
		crc = crc16_calc( INIT_CRC, LAST_HDR_GET(sc) );
		for( i = (q->ipos + (IS_TTY_PKT(sc) ? 0 : 2)) % BRL1_QSIZE;
		     i != q->tent_next;
		     i = (i + 1) % BRL1_QSIZE )
		{
		    crc = crc16_calc( crc, q->buf[i] );
		}

		/* verify the caclulated crc against the "good" crc value;
		 * if we fail, discard the bad packet and return an error.
		 */
		if( crc != (unsigned short)GOOD_CRC ) {
		    result = BRL1_CRC;
		    cq_discard_tent( q );
		    STATE_SET( sc, BRL1_FLAG );
		    done = 1;
		    continue;
		}
		
		/* so the crc check was ok.  Now we discard the CRC
		 * from the end of the received bytes.
		 */
		q->tent_next += (BRL1_QSIZE - 2);
		q->tent_next %= BRL1_QSIZE;

		/* get the subchannel and lock it */
		subch = &(sc->subch[SUBCH( LAST_HDR_GET(sc) )]);
		SUBCH_DATA_LOCK( subch, pl );
		
		/* if this isn't a console packet, we need to record
		 * a length byte
		 */
		if( !IS_TTY_PKT(sc) ) {
		    q->buf[q->ipos] = cq_tent_len( q ) - 1;
		}
		
		/* record packet for posterity */
		cq_commit_tent( q );
		result = BRL1_VALID;

		/* notify subchannel owner that there's something
		 * on the queue for them
		 */
		atomic_inc(&(subch->packet_arrived));
		callup = subch->rx_notify;
		SUBCH_DATA_UNLOCK( subch, pl );

		if( callup && (mode == SERIAL_INTERRUPT_MODE) ) {
		    L1SC_RECV_UNLOCK( sc, cpl );
		    L1_collectibles[L1C_RECEIVE_CALLUPS]++;
		    (*callup)( sc->subch[SUBCH(LAST_HDR_GET(sc))].irq_frame.bf_irq,
				sc->subch[SUBCH(LAST_HDR_GET(sc))].irq_frame.bf_dev_id,
				sc->subch[SUBCH(LAST_HDR_GET(sc))].irq_frame.bf_regs,
				sc, SUBCH(LAST_HDR_GET(sc)) );
		    L1SC_RECV_LOCK( sc, cpl );
		}
		continue;	/* go back for more! */
	    }
	    
	    /* none of the special cases applied; we've got a normal
	     * body character
	     */
	    cq_tent_add( q, c );

	    break;

	  case BRL1_ESC:
	    /* saw an escape character.  The next character will need
	     * to be unescaped.
	     */

	    q = sc->subch[ SUBCH( LAST_HDR_GET(sc) ) ].iqp;
	    ASSERT(q);

	    /* if the queue we want to write into is full, don't read from
	     * the uart (this provides backpressure to the L1 side)
	     */
	    if( cq_tent_full( q ) ) {
		result = BRL1_FULL_Q;
		done = 1;
		continue;
	    }
	    
	    if( !read_uart( sc, &c, &result ) ) {

		/* error reading uart */
		if( c != UART_NO_CHAR ) {
		    cq_discard_tent( q );
		    STATE_SET( sc, BRL1_IDLE );
		}
		done = 1;
		continue;
	    }
	    
	    if( c == BRL1_FLAG_CH ) {
		/* flag after escape is an error */
		STATE_SET( sc, BRL1_FLAG );
		cq_discard_tent( q );
		result = BRL1_PROTOCOL;
		done = 1;
		continue;
	    }

	    if( c == BRL1_ESC_CH ) {
		/* two consecutive escapes is an error */
		STATE_SET( sc, BRL1_IDLE );
		cq_discard_tent( q );
		result = BRL1_PROTOCOL;
		done = 1;
		continue;
	    }
	    
	    /* otherwise, we've got a character that needs
	     * to be unescaped
	     */
	    cq_tent_add( q, (c ^ BRL1_XOR_CH) );
	    STATE_SET( sc, BRL1_BODY );

	    break;

	} /* end of switch( STATE_GET(sc) ) */
    } /* end of while(!done) */

    L1SC_RECV_UNLOCK( sc, cpl );

    return result;
}	    


/* brl1_init initializes the Bedrock/L1 protocol layer.  This includes
 * zeroing out the send and receive state information.
 */

void
brl1_init( l1sc_t *sc, nasid_t nasid, net_vec_t uart )
{
    int i;
    brl1_sch_t *subch;

    bzero( sc, sizeof( *sc ) );
    sc->nasid = nasid;
    sc->uart = uart;
    sc->getc_f = (uart == BRL1_LOCALHUB_UART ? uart_getc : rtr_uart_getc);
    sc->putc_f = (uart == BRL1_LOCALHUB_UART ? uart_putc : rtr_uart_putc);
    sc->sol = 1;
    subch = sc->subch;

    /* initialize L1 subchannels
     */

    /* assign processor TTY channels */
    for( i = 0; i < CPUS_PER_NODE; i++, subch++ ) {
	subch->use = BRL1_SUBCH_RSVD;
	subch->packet_arrived = ATOMIC_INIT(0);
	spin_lock_init( &(subch->data_lock) );
	sv_init( &(subch->arrive_sv), &(subch->data_lock), SV_MON_SPIN | SV_ORDER_FIFO /* | SV_INTS */ );
	subch->tx_notify = NULL;
	/* (for now, drop elscuart packets in the kernel) */
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* assign system TTY channel (first free subchannel after each
     * processor's individual TTY channel has been assigned)
     */
    subch->use = BRL1_SUBCH_RSVD;
    subch->packet_arrived = ATOMIC_INIT(0);
    spin_lock_init( &(subch->data_lock) );
    sv_init( &(subch->arrive_sv), &subch->data_lock, SV_MON_SPIN | SV_ORDER_FIFO /* | SV_INTS */ );
    subch->tx_notify = NULL;
    if( sc->uart == BRL1_LOCALHUB_UART ) {
	subch->iqp = snia_kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP, NASID_TO_COMPACT_NODEID(nasid) );
	ASSERT( subch->iqp );
	cq_init( subch->iqp );
	subch->rx_notify = NULL;
    }
    else {
	/* we shouldn't be getting console input from remote UARTs */
	subch->iqp = &sc->garbage_q;
	subch->rx_notify = brl1_discard_packet;
    }
    subch++; i++;

    /* "reserved" subchannels (0x05-0x0F); for now, throw away
     * incoming packets
     */
    for( ; i < 0x10; i++, subch++ ) {
	subch->use = BRL1_SUBCH_FREE;
	subch->packet_arrived = ATOMIC_INIT(0);
	subch->tx_notify = NULL;
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* remaining subchannels are free */
    for( ; i < BRL1_NUM_SUBCHANS; i++, subch++ ) {
	subch->use = BRL1_SUBCH_FREE;
	subch->packet_arrived = ATOMIC_INIT(0);
	subch->tx_notify = NULL;
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* initialize synchronization structures
     */
    spin_lock_init( &(sc->subch_lock) );
    spin_lock_init( &(sc->send_lock) );
    spin_lock_init( &(sc->recv_lock) );

    if( sc->uart == BRL1_LOCALHUB_UART ) {
	uart_init( sc, UART_BAUD_RATE );
    }
    else {
	rtr_uart_init( sc, UART_BAUD_RATE );
    }

    /* Set up remaining fields using L1 command functions-- elsc_module_get
     * to read the module id, elsc_debug_get to see whether or not we're
     * in verbose mode.
     */
    {
	extern int elsc_module_get(l1sc_t *);

	sc->modid = elsc_module_get( sc );
	sc->modid = (sc->modid < 0 ? INVALID_MODULE : sc->modid);
	sc->verbose = 1;
    }
}

/*********************************************************************
 * These are interrupt-related functions used in the kernel to service
 * the L1.
 */

/*
 * brl1_intrd is the function which is called on a console interrupt.
 */

#if defined(CONFIG_IA64_SGI_SN1)

static void
brl1_intrd(int irq, void *dev_id, struct pt_regs *stuff)
{
    u_char isr_reg;
    l1sc_t *sc = get_elsc();
    int ret;

    L1_collectibles[L1C_INTERRUPTS]++;
    isr_reg = READ_L1_UART_REG(sc->nasid, REG_ISR);

    /* Save for callup args in console */
    sc->subch[SC_CONS_SYSTEM].irq_frame.bf_irq = irq;
    sc->subch[SC_CONS_SYSTEM].irq_frame.bf_dev_id = dev_id;
    sc->subch[SC_CONS_SYSTEM].irq_frame.bf_regs = stuff;

#if	defined(SYNC_CONSOLE_WRITE)
    while( isr_reg & ISR_RxRDY )
#else
    while( isr_reg & (ISR_RxRDY | ISR_TxRDY) )
#endif
    {
	if( isr_reg & ISR_RxRDY ) {
	    L1_collectibles[L1C_OUR_R_INTERRUPTS]++;
	    ret = brl1_receive(sc, SERIAL_INTERRUPT_MODE);
	    if ( (ret != BRL1_VALID) && (ret != BRL1_NO_MESSAGE) && (ret != BRL1_PROTOCOL) && (ret != BRL1_CRC) )
		L1_collectibles[L1C_REC_STALLS] = ret;
	}
#if	!defined(SYNC_CONSOLE_WRITE)
	if( (isr_reg & ISR_TxRDY) || (sc->send_in_use && UART_PUTC_READY(sc->nasid)) ) {
	    L1_collectibles[L1C_OUR_X_INTERRUPTS]++;
	    brl1_send_cont(sc);
	}
#endif	/* SYNC_CONSOLE_WRITE */
	isr_reg = READ_L1_UART_REG(sc->nasid, REG_ISR);
    }
}
#endif	/* CONFIG_IA64_SGI_SN1 */


/*
 * Install a callback function for the system console subchannel 
 * to allow an upper layer to be notified when the send buffer 
 * has been emptied.
 */
static inline void
l1_tx_notif( brl1_notif_t func )
{
	subch_set_tx_notify( &NODEPDA(NASID_TO_COMPACT_NODEID(get_master_nasid()))->module->elsc,
			SC_CONS_SYSTEM, func );
}


/*
 * Install a callback function for the system console subchannel
 * to allow an upper layer to be notified when a packet has been
 * received.
 */
static inline void
l1_rx_notif( brl1_notif_t func )
{
	subch_set_rx_notify( &NODEPDA(NASID_TO_COMPACT_NODEID(get_master_nasid()))->module->elsc,
				SC_CONS_SYSTEM, func );
}


/* brl1_intr is called directly from the uart interrupt; after it runs, the
 * interrupt "daemon" xthread is signalled to continue.
 */
void
brl1_intr( void )
{
}

#define BRL1_INTERRUPT_LEVEL	65	/* linux request_irq() value */

/* Return the current interrupt level */

//#define CONSOLE_POLLING_ALSO

int
l1_get_intr_value( void )
{
#ifdef	CONSOLE_POLLING_ALSO
	return(0);
#else
	return(BRL1_INTERRUPT_LEVEL);
#endif
}

/* Disconnect the callup functions - throw away interrupts */

void
l1_unconnect_intr(void)
{
	/* UnRegister the upper-level callup functions */
	l1_rx_notif((brl1_notif_t)NULL);
	l1_tx_notif((brl1_notif_t)NULL);
	/* We do NOT unregister the interrupts */
}

/* Set up uart interrupt handling for this node's uart */

void
l1_connect_intr(void *rx_notify, void *tx_notify)
{
	l1sc_t *sc;
	nasid_t nasid;
#if defined(CONFIG_IA64_SGI_SN1)
	int tmp;
#endif
	nodepda_t *console_nodepda;
	int intr_connect_level(cpuid_t, int, ilvl_t, intr_func_t);

	if ( L1_interrupts_connected ) {
		/* Interrupts are connected, so just register the callups */
		l1_rx_notif((brl1_notif_t)rx_notify);
		l1_tx_notif((brl1_notif_t)tx_notify);

		L1_collectibles[L1C_CONNECT_CALLS]++;
		return;
	}
	else
		L1_interrupts_connected = 1;

	nasid = get_master_nasid();
	console_nodepda = NODEPDA(NASID_TO_COMPACT_NODEID(nasid));
	sc = &console_nodepda->module->elsc;
	sc->intr_cpu = console_nodepda->node_first_cpu;

#if defined(CONFIG_IA64_SGI_SN1)
	if ( intr_connect_level(sc->intr_cpu, UART_INTR, INTPEND0_MAXMASK, (intr_func_t)brl1_intr) ) {
		L1_interrupts_connected = 0; /* FAILS !! */
	}
	else {
		void synergy_intr_connect(int, int);

		synergy_intr_connect(UART_INTR, sc->intr_cpu);
		L1_collectibles[L1C_R_IRQ]++;
		tmp = request_irq(BRL1_INTERRUPT_LEVEL, brl1_intrd, SA_INTERRUPT | SA_SHIRQ, "l1_protocol_driver", (void *)sc);
		L1_collectibles[L1C_R_IRQ_RET] = (uint64_t)tmp;
		if ( tmp ) {
			L1_interrupts_connected = 0; /* FAILS !! */
		}
		else {
			/* Register the upper-level callup functions */
			l1_rx_notif((brl1_notif_t)rx_notify);
			l1_tx_notif((brl1_notif_t)tx_notify);

			/* Set the uarts the way we like it */
			uart_enable_recv_intr( sc );
			uart_disable_xmit_intr( sc );
		}
	}
#endif	/* CONFIG_IA64_SGI_SN1 */
}


/* Set the line speed */

void
l1_set_baud(int baud)
{
#if 0
	nasid_t nasid;
	static void uart_init(l1sc_t *, int);
#endif

	L1_collectibles[L1C_SET_BAUD]++;

#if 0
	if ( L1_cons_is_inited ) {
		nasid = get_master_nasid();
		if ( NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module != (module_t *)0 )
			uart_init(&NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module->elsc, baud);
	}
#endif
	return;
}


/* These are functions to use from serial_in/out when in protocol
 * mode to send and receive uart control regs. These are external
 * interfaces into the protocol driver.
 */

void
l1_control_out(int offset, int value)
{
	nasid_t nasid = get_master_nasid();
	WRITE_L1_UART_REG(nasid, offset, value); 
}

/* Console input exported interface. Return a register value.  */

int
l1_control_in_polled(int offset)
{
	static int l1_control_in_local(int, int);

	return(l1_control_in_local(offset, SERIAL_POLLED_MODE));
}

int
l1_control_in(int offset)
{
	static int l1_control_in_local(int, int);

	return(l1_control_in_local(offset, SERIAL_INTERRUPT_MODE));
}

static int
l1_control_in_local(int offset, int mode)
{
	nasid_t nasid;
	int ret, input;
	static int l1_poll(l1sc_t *, int);

	nasid = get_master_nasid();
	ret = READ_L1_UART_REG(nasid, offset); 

	if ( offset == REG_LSR ) {
		ret |= (LSR_XHRE | LSR_XSRE);	/* can send anytime */
		if ( L1_cons_is_inited ) {
			if ( NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module != (module_t *)0 ) {
				input = l1_poll(&NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module->elsc, mode);
				if ( input ) {
					ret |= LSR_RCA;
				}
			}
		}
	}
	return(ret);
}

/*
 * Console input exported interface. Return a character (if one is available)
 */

int
l1_serial_in_polled(void)
{
	static int l1_serial_in_local(int mode);

	return(l1_serial_in_local(SERIAL_POLLED_MODE));
}

int
l1_serial_in(void)
{
	static int l1_serial_in_local(int mode);

	return(l1_serial_in_local(SERIAL_INTERRUPT_MODE));
}

static int
l1_serial_in_local(int mode)
{
	nasid_t nasid;
	l1sc_t *sc;
	int value;
	static int l1_getc( l1sc_t *, int );
	static inline l1sc_t *early_sc_init(nasid_t);

	nasid = get_master_nasid();
	sc = early_sc_init(nasid);
	if ( L1_cons_is_inited ) {
		if ( NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module != (module_t *)0 ) {
			sc = &NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module->elsc;
		}
	}
	value = l1_getc(sc, mode);
	return(value);
}

/* Console output exported interface. Write message to the console.  */

int
l1_serial_out( char *str, int len )
{
	nasid_t nasid = get_master_nasid();
	int l1_write(l1sc_t *, char *, int, int);

	if ( L1_cons_is_inited ) {
		if ( NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module != (module_t *)0 )
			return(l1_write(&NODEPDA(NASID_TO_COMPACT_NODEID(nasid))->module->elsc, str, len,
#if	defined(SYNC_CONSOLE_WRITE)
					1
#else
					!L1_interrupts_connected
#endif
							));
	}
	return(early_l1_serial_out(nasid, str, len, NOT_LOCKED));
}


/*
 * These are the 'early' functions - when we need to do things before we have
 * all the structs setup.
 */

static l1sc_t Early_console;		/* fake l1sc_t */
static int Early_console_inited = 0;

static void
early_brl1_init( l1sc_t *sc, nasid_t nasid, net_vec_t uart )
{
    int i;
    brl1_sch_t *subch;

    bzero( sc, sizeof( *sc ) );
    sc->nasid = nasid;
    sc->uart = uart;
    sc->getc_f = (uart == BRL1_LOCALHUB_UART ? uart_getc : rtr_uart_getc);
    sc->putc_f = (uart == BRL1_LOCALHUB_UART ? uart_putc : rtr_uart_putc);
    sc->sol = 1;
    subch = sc->subch;

    /* initialize L1 subchannels
     */

    /* assign processor TTY channels */
    for( i = 0; i < CPUS_PER_NODE; i++, subch++ ) {
	subch->use = BRL1_SUBCH_RSVD;
	subch->packet_arrived = ATOMIC_INIT(0);
	subch->tx_notify = NULL;
	subch->rx_notify = NULL;
	subch->iqp = &sc->garbage_q;
    }

    /* assign system TTY channel (first free subchannel after each
     * processor's individual TTY channel has been assigned)
     */
    subch->use = BRL1_SUBCH_RSVD;
    subch->packet_arrived = ATOMIC_INIT(0);
    subch->tx_notify = NULL;
    subch->rx_notify = NULL;
    if( sc->uart == BRL1_LOCALHUB_UART ) {
	static sc_cq_t x_iqp;

	subch->iqp = &x_iqp;
	ASSERT( subch->iqp );
	cq_init( subch->iqp );
    }
    else {
	/* we shouldn't be getting console input from remote UARTs */
	subch->iqp = &sc->garbage_q;
    }
    subch++; i++;

    /* "reserved" subchannels (0x05-0x0F); for now, throw away
     * incoming packets
     */
    for( ; i < 0x10; i++, subch++ ) {
	subch->use = BRL1_SUBCH_FREE;
	subch->packet_arrived = ATOMIC_INIT(0);
	subch->tx_notify = NULL;
	subch->rx_notify = NULL;
	subch->iqp = &sc->garbage_q;
    }

    /* remaining subchannels are free */
    for( ; i < BRL1_NUM_SUBCHANS; i++, subch++ ) {
	subch->use = BRL1_SUBCH_FREE;
	subch->packet_arrived = ATOMIC_INIT(0);
	subch->tx_notify = NULL;
	subch->rx_notify = NULL;
	subch->iqp = &sc->garbage_q;
    }
}

static inline l1sc_t *
early_sc_init(nasid_t nasid)
{
	/* This is for early I/O */
	if ( Early_console_inited == 0 ) {
    		early_brl1_init(&Early_console, nasid,  BRL1_LOCALHUB_UART);
		Early_console_inited = 1;
	}
	return(&Early_console);
}

#define PUTCHAR(ch) \
    { \
        while( (!(READ_L1_UART_REG( nasid, REG_LSR ) & LSR_XHRE)) || \
                (!(READ_L1_UART_REG( nasid, REG_MSR ) & MSR_CTS)) ); \
        WRITE_L1_UART_REG( nasid, REG_DAT, (ch) ); \
    }

static int
early_l1_serial_out( nasid_t nasid, char *str, int len, int lock_state )
{
	int ret, sent = 0;
	char *msg = str;
	static int early_l1_send( nasid_t nasid, char *str, int len, int lock_state );

	while ( sent < len ) {
		ret = early_l1_send(nasid, msg, len - sent, lock_state);
		sent += ret;
		msg += ret;
	}
	return(len);
}

static inline int
early_l1_send( nasid_t nasid, char *str, int len, int lock_state )
{
    int sent;
    char crc_char;
    unsigned short crc = INIT_CRC;

    if( len > (BRL1_QSIZE - 1) )
	len = (BRL1_QSIZE - 1);

    sent = len;
    if ( lock_state == NOT_LOCKED )
    	lock_console(nasid);

    PUTCHAR( BRL1_FLAG_CH );
    PUTCHAR( BRL1_EVENT | SC_CONS_SYSTEM );
    crc = crc16_calc( crc, (BRL1_EVENT | SC_CONS_SYSTEM) );

    while( len ) {

	if( (*str == BRL1_FLAG_CH) || (*str == BRL1_ESC_CH) ) {
	    PUTCHAR( BRL1_ESC_CH );
	    PUTCHAR( (*str) ^ BRL1_XOR_CH );
	}
	else {
	    PUTCHAR( *str );
	}
	
	crc = crc16_calc( crc, *str );

	str++; len--;
    }
    
    crc ^= 0xffff;
    crc_char = crc & 0xff;
    if( (crc_char == BRL1_ESC_CH) || (crc_char == BRL1_FLAG_CH) ) {
	crc_char ^= BRL1_XOR_CH;
	PUTCHAR( BRL1_ESC_CH );
    }
    PUTCHAR( crc_char );
    crc_char = (crc >> 8) & 0xff;
    if( (crc_char == BRL1_ESC_CH) || (crc_char == BRL1_FLAG_CH) ) {
	crc_char ^= BRL1_XOR_CH;
	PUTCHAR( BRL1_ESC_CH );
    }
    PUTCHAR( crc_char );
    PUTCHAR( BRL1_FLAG_CH );

    if ( lock_state == NOT_LOCKED )
    	unlock_console(nasid);
    return sent;
}


/*********************************************************************
 * l1_cons functions
 *
 * These allow the L1 to act as the system console.  They're intended
 * to abstract away most of the br/l1 internal details from the
 * _L1_cons_* functions (in the prom-- see "l1_console.c") and
 * l1_* functions (in the kernel-- see "sio_l1.c") that they support.
 *
 */

static int
l1_poll( l1sc_t *sc, int mode )
{
    int ret;

    /* in case this gets called before the l1sc_t structure for the module_t
     * struct for this node is initialized (i.e., if we're called with a
     * zero l1sc_t pointer)...
     */


    if( !sc ) {
	return 0;
    }

    if( atomic_read(&sc->subch[SC_CONS_SYSTEM].packet_arrived) ) {
	return 1;
    }

    ret = brl1_receive( sc, mode );
    if ( (ret != BRL1_VALID) && (ret != BRL1_NO_MESSAGE) && (ret != BRL1_PROTOCOL) && (ret != BRL1_CRC) )
	L1_collectibles[L1C_REC_STALLS] = ret;

    if( atomic_read(&sc->subch[SC_CONS_SYSTEM].packet_arrived) ) {
	return 1;
    }
    return 0;
}


/* pull a character off of the system console queue (if one is available)
 */
static int
l1_getc( l1sc_t *sc, int mode )
{
    unsigned long pl = 0;
    int c;

    brl1_sch_t *subch = &(sc->subch[SC_CONS_SYSTEM]);
    sc_cq_t *q = subch->iqp;

    if( !l1_poll( sc, mode ) ) {
	return 0;
    }

    SUBCH_DATA_LOCK( subch, pl );
    if( cq_empty( q ) ) {
	atomic_set(&subch->packet_arrived, 0);
	SUBCH_DATA_UNLOCK( subch, pl );
	return 0;
    }
    cq_rem( q, c );
    if( cq_empty( q ) )
	atomic_set(&subch->packet_arrived, 0);
    SUBCH_DATA_UNLOCK( subch, pl );

    return c;
}

/*
 * Write a message to the L1 on the system console subchannel.
 *
 * Danger: don't use a non-zero value for the wait parameter unless you're
 * someone important (like a kernel error message).
 */

int
l1_write( l1sc_t *sc, char *msg, int len, int wait )
{
	int sent = 0, ret = 0;

	if ( wait ) {
		while ( sent < len ) {
			ret = brl1_send( sc, msg, len - sent, (SC_CONS_SYSTEM | BRL1_EVENT), wait );
			sent += ret;
			msg += ret;
		}
		ret = len;
	}
	else {
		ret = brl1_send( sc, msg, len, (SC_CONS_SYSTEM | BRL1_EVENT), wait );
	}
	return(ret);
}

/* initialize the system console subchannel
 */
void
l1_init(void)
{
	/* All we do now is remember that we have been called */
	L1_cons_is_inited = 1;
}


/*********************************************************************
 * The following functions and definitions implement the "message"-
 * style interface to the L1 system controller.
 *
 * Note that throughout this file, "sc" generally stands for "system
 * controller", while "subchannels" tend to be represented by
 * variables with names like subch or ch.
 *
 */

#ifdef L1_DEBUG
#define L1_DBG_PRF(x) printf x
#else
#define L1_DBG_PRF(x)
#endif

/*
 * sc_data_ready is called to signal threads that are blocked on l1 input.
 */
void
sc_data_ready( int dummy0, void *dummy1, struct pt_regs *dummy2, l1sc_t *sc, int ch )
{
    unsigned long pl = 0;

    brl1_sch_t *subch = &(sc->subch[ch]);
    SUBCH_DATA_LOCK( subch, pl );
    sv_signal( &(subch->arrive_sv) );
    SUBCH_DATA_UNLOCK( subch, pl );
}

/* sc_open reserves a subchannel to send a request to the L1 (the
 * L1's response will arrive on the same channel).  The number
 * returned by sc_open is the system controller subchannel
 * acquired.
 */
int
sc_open( l1sc_t *sc, uint target )
{
    /* The kernel version implements a locking scheme to arbitrate
     * subchannel assignment.
     */
    int ch;
    unsigned long pl = 0;
    brl1_sch_t *subch;

    SUBCH_LOCK( sc, pl );

    /* Look for a free subchannel. Subchannels 0-15 are reserved
     * for other purposes.
     */
    for( subch = &(sc->subch[BRL1_CMD_SUBCH]), ch = BRL1_CMD_SUBCH; 
			ch < BRL1_NUM_SUBCHANS; subch++, ch++ ) {
        if( subch->use == BRL1_SUBCH_FREE )
            break;
    }

    if( ch == BRL1_NUM_SUBCHANS ) {
        /* there were no subchannels available! */
        SUBCH_UNLOCK( sc, pl );
        return SC_NSUBCH;
    }

    subch->use = BRL1_SUBCH_RSVD;
    SUBCH_UNLOCK( sc, pl );

    atomic_set(&subch->packet_arrived, 0);
    subch->target = target;
    spin_lock_init( &(subch->data_lock) );
    sv_init( &(subch->arrive_sv), &(subch->data_lock), SV_MON_SPIN | SV_ORDER_FIFO /* | SV_INTS */);
    subch->tx_notify = NULL;
    subch->rx_notify = sc_data_ready;
    subch->iqp = snia_kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP,
				   NASID_TO_COMPACT_NODEID(sc->nasid) );
    ASSERT( subch->iqp );
    cq_init( subch->iqp );

    return ch;
}


/* sc_close frees a Bedrock<->L1 subchannel.
 */
int
sc_close( l1sc_t *sc, int ch )
{
    unsigned long pl = 0;
    brl1_sch_t *subch;

    SUBCH_LOCK( sc, pl );
    subch = &(sc->subch[ch]);
    if( subch->use != BRL1_SUBCH_RSVD ) {
        /* we're trying to close a subchannel that's not open */
	SUBCH_UNLOCK( sc, pl );
        return SC_NOPEN;
    }

    atomic_set(&subch->packet_arrived, 0);
    subch->use = BRL1_SUBCH_FREE;

    sv_broadcast( &(subch->arrive_sv) );
    sv_destroy( &(subch->arrive_sv) );
    spin_lock_destroy( &(subch->data_lock) );

    ASSERT( subch->iqp && (subch->iqp != &sc->garbage_q) );
    snia_kmem_free( subch->iqp, sizeof(sc_cq_t) );
    subch->iqp = &sc->garbage_q;
    subch->tx_notify = NULL;
    subch->rx_notify = brl1_discard_packet;

    SUBCH_UNLOCK( sc, pl );

    return SC_SUCCESS;
}


/* sc_construct_msg builds a bedrock-to-L1 request in the supplied
 * buffer.  Returns the length of the message.  The
 * safest course when passing a buffer to be filled in is to use
 * BRL1_QSIZE as the buffer size.
 *
 * Command arguments are passed as type/argument pairs, i.e., to
 * pass the number 5 as an argument to an L1 command, call
 * sc_construct_msg as follows:
 *
 *    char msg[BRL1_QSIZE];
 *    msg_len = sc_construct_msg( msg,
 *				  BRL1_QSIZE,
 *				  target_component,
 *                                L1_ADDR_TASK_BOGUSTASK,
 *                                L1_BOGUSTASK_REQ_BOGUSREQ,
 *                                2,
 *                                L1_ARG_INT, 5 );
 *
 * To pass an additional ASCII argument, you'd do the following:
 *
 *    char *str;
 *    ... str points to a null-terminated ascii string ...
 *    msg_len = sc_construct_msg( msg,
 *                                BRL1_QSIZE,
 *				  target_component,
 *                                L1_ADDR_TASK_BOGUSTASK,
 *                                L1_BOGUSTASK_REQ_BOGUSREQ,
 *                                4,
 *                                L1_ARG_INT, 5,
 *                                L1_ARG_ASCII, str );
 *
 * Finally, arbitrary data of unknown type is passed using the argtype
 * code L1_ARG_UNKNOWN, a data length, and a buffer pointer, e.g.
 *
 *    msg_len = sc_construct_msg( msg,
 *                                BRL1_QSIZE,
 *				  target_component,
 *                                L1_ADDR_TASK_BOGUSTASK,
 *                                L1_BOGUSTASK_REQ_BOGUSREQ,
 *                                3,
 *                                L1_ARG_UNKNOWN, 32, bufptr );
 *
 * ...passes 32 bytes of data starting at bufptr.  Note that no string or
 * "unknown"-type argument should be long enough to overflow the message
 * buffer.
 *
 * To construct a message for an L1 command that requires no arguments,
 * you'd use the following:
 *
 *    msg_len = sc_construct_msg( msg,
 *                                BRL1_QSIZE,
 *				  target_component,
 *                                L1_ADDR_TASK_BOGUSTASK,
 *                                L1_BOGUSTASK_REQ_BOGUSREQ,
 *                                0 );
 *
 * The final 0 means "no varargs".  Notice that this parameter is used to hold
 * the number of additional arguments to sc_construct_msg, _not_ the actual
 * number of arguments used by the L1 command (so 2 per L1_ARG_[INT,ASCII]
 * type argument, and 3 per L1_ARG_UNKOWN type argument).  A call to construct
 * an L1 command which required three integer arguments and two arguments of
 * some arbitrary (unknown) type would pass 12 as the value for this parameter.
 *
 * ENDIANNESS WARNING: The following code does a lot of copying back-and-forth
 * between byte arrays and four-byte big-endian integers.  Depending on the
 * system controller connection and endianness of future architectures, some
 * rewriting might be necessary.
 */
int
sc_construct_msg( l1sc_t  *sc,		/* system controller struct */
		  int	   ch,           /* subchannel for this message */
		  char    *msg,          /* message buffer */
		  int      msg_len,      /* size of message buffer */
                  l1addr_t addr_task,    /* target system controller task */
                  short    req_code,     /* 16-bit request code */
                  int      req_nargs,    /* # of arguments (varargs) passed */
                  ... )                 /* any additional parameters */
{
    uint32_t buf32;   /* 32-bit buffer used to bounce things around */
    void *bufptr;       /* used to hold command argument addresses */
    va_list al;         /* variable argument list */
    int index;          /* current index into msg buffer */
    int argno;          /* current position in varargs list */
    int l1_argno;       /* running total of arguments to l1 */
    int l1_arg_t;       /* argument type/length */
    int l1_argno_byte;  /* offset of argument count byte */

    index = argno = 0;

    /* set up destination address */
    if( (msg_len -= sizeof( buf32 )) < 0 )
	return -1;
    L1_ADDRESS_TO_TASK( &buf32, sc->subch[ch].target, addr_task );
    COPY_INT_TO_BUFFER(msg, index, buf32);

    /* copy request code */
    if( (msg_len -= 2) < 0 )
	return( -1 );
    msg[index++] = ((req_code >> 8) & 0xff);
    msg[index++] = (req_code & 0xff);

    if( !req_nargs ) {
        return index;
    }

    /* reserve a byte for the argument count */
    if( (msg_len -= 1) < 0 )
	return( -1 );
    l1_argno_byte = index++;
    l1_argno = 0;

    /* copy additional arguments */
    va_start( al, req_nargs );
    while( argno < req_nargs ) {
        l1_argno++;
        l1_arg_t = va_arg( al, int ); argno++;
        switch( l1_arg_t )
        {
          case L1_ARG_INT:
	    if( (msg_len -= (sizeof( buf32 ) + 1)) < 0 )
		return( -1 );
            msg[index++] = L1_ARG_INT;
            buf32 = (unsigned)va_arg( al, int ); argno++;
	    COPY_INT_TO_BUFFER(msg, index, buf32);
            break;

          case L1_ARG_ASCII:
            bufptr = va_arg( al, char* ); argno++;
	    if( (msg_len -= (strlen( bufptr ) + 2)) < 0 )
		return( -1 );
            msg[index++] = L1_ARG_ASCII;
            strcpy( (char *)&(msg[index]), (char *)bufptr );
            index += (strlen( bufptr ) + 1); /* include terminating null */
            break;

	  case L1_ARG_UNKNOWN:
              {
                  int arglen;
		  
                  arglen = va_arg( al, int ); argno++;
                  bufptr = va_arg( al, void* ); argno++;
		  if( (msg_len -= (arglen + 1)) < 0 )
		      return( -1 );
                  msg[index++] = L1_ARG_UNKNOWN | arglen;
                  BCOPY( bufptr, &(msg[index]), arglen  );
                  index += arglen;
		  break;
              }
	  
	  default: /* unhandled argument type */
	    return -1;
        }
    }

    va_end( al );
    msg[l1_argno_byte] = l1_argno;

    return index;
}



/* sc_interpret_resp verifies an L1 response to a bedrock request, and
 * breaks the response data up into the constituent parts.  If the
 * response message indicates error, or if a mismatch is found in the
 * expected number and type of arguments, an error is returned.  The
 * arguments to this function work very much like the arguments to
 * sc_construct_msg, above, except that L1_ARG_INTs must be followed
 * by a _pointer_ to an integer that can be filled in by this function.
 */
int
sc_interpret_resp( char *resp,          /* buffer received from L1 */
                   int   resp_nargs,    /* number of _varargs_ passed in */
                   ... )
{
    uint32_t buf32;   /* 32-bit buffer used to bounce things around */
    void *bufptr;       /* used to hold response field addresses */
    va_list al;         /* variable argument list */
    int index;          /* current index into response buffer */
    int argno;          /* current position in varargs list */
    int l1_fldno;       /* number of resp fields received from l1 */
    int l1_fld_t;       /* field type/length */

    index = argno = 0;

#if defined(L1_DEBUG)
#define DUMP_RESP							  \
    {									  \
	int ix;								  \
        char outbuf[512];						  \
        sprintf( outbuf, "sc_interpret_resp error line %d: ", __LINE__ ); \
	for( ix = 0; ix < 16; ix++ ) {					  \
	    sprintf( &outbuf[strlen(outbuf)], "%x ", resp[ix] );	  \
	}								  \
	printk( "%s\n", outbuf );					  \
    }
#else
#define DUMP_RESP
#endif /* L1_DEBUG */

    /* check response code */
    COPY_BUFFER_TO_INT(resp, index, buf32);
    if( buf32 != L1_RESP_OK ) {
	DUMP_RESP;
        return buf32;
    }

    /* get number of response fields */
    l1_fldno = resp[index++];

    va_start( al, resp_nargs );

    /* copy out response fields */
    while( argno < resp_nargs ) {
        l1_fldno--;
        l1_fld_t = va_arg( al, int ); argno++;
        switch( l1_fld_t )
        {
          case L1_ARG_INT:
            if( resp[index++] != L1_ARG_INT ) {
                /* type mismatch */
		va_end( al );
		DUMP_RESP;
		return -1;
            }
            bufptr = va_arg( al, int* ); argno++;
	    COPY_BUFFER_TO_BUFFER(resp, index, bufptr);
            break;

          case L1_ARG_ASCII:
            if( resp[index++] != L1_ARG_ASCII ) {
                /* type mismatch */
		va_end( al );
		DUMP_RESP;
                return -1;
            }
            bufptr = va_arg( al, char* ); argno++;
            strcpy( (char *)bufptr, (char *)&(resp[index]) );
            /* include terminating null */
            index += (strlen( &(resp[index]) ) + 1);
            break;

          default:
	    if( (l1_fld_t & L1_ARG_UNKNOWN) == L1_ARG_UNKNOWN )
	    {
		int *arglen;
		
		arglen = va_arg( al, int* ); argno++;
		bufptr = va_arg( al, void* ); argno++;
		*arglen = ((resp[index++] & ~L1_ARG_UNKNOWN) & 0xff);
		BCOPY( &(resp[index]), bufptr, *arglen  );
		index += (*arglen);
	    }
	    
	    else {
		/* unhandled type */
		va_end( al );
		DUMP_RESP;
		return -1;
	    }
        }
    }
    va_end( al );
  
    if( (l1_fldno != 0) || (argno != resp_nargs) ) {
        /* wrong number of arguments */
	DUMP_RESP;
        return -1;
    }
    return 0;
}




/* sc_send takes as arguments a system controller struct, a
 * buffer which contains a Bedrock<->L1 "request" message,
 * the message length, and the subchannel (presumably obtained
 * from an earlier invocation of sc_open) over which the
 * message is to be sent.  The final argument ("wait") indicates
 * whether the send is to be performed synchronously or not.
 *
 * sc_send returns either zero or an error value.  Synchronous sends 
 * (wait != 0) will not return until the data has actually been sent
 * to the UART.  Synchronous sends generally receive privileged
 * treatment.  The intent is that they be used sparingly, for such
 * purposes as kernel printf's (the "ducons" routines).  Run-of-the-mill
 * console output and L1 requests should NOT use a non-zero value
 * for wait.
 */
int
sc_send( l1sc_t *sc, int ch, char *msg, int len, int wait )
{
    char type_and_subch;
    int result;

    if( (ch < 0) || ( ch >= BRL1_NUM_SUBCHANS) ) {
        return SC_BADSUBCH;
    }

    /* Verify that this is an open subchannel
     */
    if( sc->subch[ch].use == BRL1_SUBCH_FREE ) {
        return SC_NOPEN;
    }

    type_and_subch = (BRL1_REQUEST | ((u_char)ch));
    result = brl1_send( sc, msg, len, type_and_subch, wait );

    /* If we sent as much as we asked to, return "ok". */
    if( result == len )
	return( SC_SUCCESS );

    /* Or, if we sent less, than either the UART is busy or
     * we're trying to send too large a packet anyway.
     */
    else if( result >= 0 && result < len )
	return( SC_BUSY );

    /* Or, if something else went wrong (result < 0), then
     * return that error value.
     */
    else
	return( result );
}



/* subch_pull_msg pulls a message off the receive queue for subch
 * and places it the buffer pointed to by msg.  This routine should only
 * be called when the caller already knows a message is available on the
 * receive queue (and, in the kernel, only when the subchannel data lock
 * is held by the caller).
 */
static void
subch_pull_msg( brl1_sch_t *subch, char *msg, int *len )
{
    sc_cq_t *q;         /* receive queue */
    int before_wrap,    /* packet may be split into two different       */
        after_wrap;     /*   pieces to accommodate queue wraparound      */

    /* pull message off the receive queue */
    q = subch->iqp;

    cq_rem( q, *len );   /* remove length byte and store */
    cq_discard( q );     /* remove type/subch byte and discard */

    if ( *len > 0 )
	(*len)--;        /* don't count type/subch byte in length returned */

    if( (q->opos + (*len)) > BRL1_QSIZE ) {
        before_wrap = BRL1_QSIZE - q->opos;
        after_wrap = (*len) - before_wrap;
    }
    else {
        before_wrap = (*len);
        after_wrap = 0;
    }

    BCOPY( q->buf + q->opos, msg, before_wrap  );
    if( after_wrap ) {
        BCOPY( q->buf, msg + before_wrap, after_wrap  );
	q->opos = after_wrap;
    }
    else {
	q->opos = ((q->opos + before_wrap) & (BRL1_QSIZE - 1));
    }
    atomic_dec(&(subch->packet_arrived));
}


/* sc_recv_poll can be called as a blocking or non-blocking function;
 * it attempts to pull a message off of the subchannel specified
 * in the argument list (ch).
 *
 * The "block" argument, if non-zero, is interpreted as a timeout
 * delay (to avoid permanent waiting).
 */

int
sc_recv_poll( l1sc_t *sc, int ch, char *msg, int *len, uint64_t block )
{
    int is_msg = 0;
    unsigned long pl = 0;
    brl1_sch_t *subch = &(sc->subch[ch]);

    rtc_time_t exp_time = rtc_time() + block;

    /* sanity check-- make sure this is an open subchannel */
    if( subch->use == BRL1_SUBCH_FREE )
	return( SC_NOPEN );

    do {

        /* kick the next lower layer and see if it pulls anything in
         */
	brl1_receive( sc, SERIAL_POLLED_MODE );
	is_msg = atomic_read(&subch->packet_arrived);

    } while( block && !is_msg && (rtc_time() < exp_time) );

    if( !is_msg ) {
	/* no message and we didn't care to wait for one */
	return( SC_NMSG );
    }

    SUBCH_DATA_LOCK( subch, pl );
    subch_pull_msg( subch, msg, len );
    SUBCH_DATA_UNLOCK( subch, pl );

    return( SC_SUCCESS );
}
    

/* Like sc_recv_poll, sc_recv_intr can be called in either a blocking
 * or non-blocking mode.  Rather than polling until an appointed timeout,
 * however, sc_recv_intr sleeps on a syncrhonization variable until a
 * signal from the lower layer tells us that a packet has arrived.
 *
 * sc_recv_intr can't be used with remote (router) L1s.
 */
int
sc_recv_intr( l1sc_t *sc, int ch, char *msg, int *len, uint64_t block )
{
    int is_msg = 0;
    unsigned long pl = 0;
    brl1_sch_t *subch = &(sc->subch[ch]);

    do {
	SUBCH_DATA_LOCK(subch, pl);
	is_msg = atomic_read(&subch->packet_arrived);
	if( !is_msg && block ) {
	    /* wake me when you've got something */
	    subch->rx_notify = sc_data_ready;
	    sv_wait( &(subch->arrive_sv), 0, 0);
	    if( subch->use == BRL1_SUBCH_FREE ) {
		/* oops-- somebody closed our subchannel while we were
		 * sleeping!
		 */

		/* no need to unlock since the channel's closed anyhow */
		return( SC_NOPEN );
	    }
	}
    } while( !is_msg && block );

    if( !is_msg ) {
	/* no message and we didn't care to wait for one */
	SUBCH_DATA_UNLOCK( subch, pl );
	return( SC_NMSG );
    }

    subch_pull_msg( subch, msg, len );
    SUBCH_DATA_UNLOCK( subch, pl );

    return( SC_SUCCESS );
}

/* sc_command implements a (blocking) combination of sc_send and sc_recv.
 * It is intended to be the SN1 equivalent of SN0's "elsc_command", which
 * issued a system controller command and then waited for a response from
 * the system controller before returning.
 *
 * cmd points to the outgoing command; resp points to the buffer in
 * which the response is to be stored.  Both buffers are assumed to
 * be the same length; if there is any doubt as to whether the
 * response buffer is long enough to hold the L1's response, then
 * make it BRL1_QSIZE bytes-- no Bedrock<->L1 message can be any
 * bigger.
 *
 * Be careful using the same buffer for both cmd and resp; it could get
 * hairy if there were ever an L1 command request that spanned multiple
 * packets.  (On the other hand, that would require some additional
 * rewriting of the L1 command interface anyway.)
 */
#define __RETRIES	50
#define __WAIT_SEND	1	// ( sc->uart != BRL1_LOCALHUB_UART )
#define __WAIT_RECV	10000000


int
sc_command( l1sc_t *sc, int ch, char *cmd, char *resp, int *len )
{
#ifndef CONFIG_SERIAL_SGI_L1_PROTOCOL
    return SC_NMSG;
#else
    int result;
    int retries;

    if ( IS_RUNNING_ON_SIMULATOR() )
    	return SC_NMSG;

    retries = __RETRIES;

    while( (result = sc_send( sc, ch, cmd, *len, __WAIT_SEND )) < 0 ) {
	if( result == SC_BUSY ) {
	    retries--;
	    if( retries <= 0 )
		return result;
	    uart_delay(500);
	}
	else {
	    return result;
	}
    }
    
    /* block on sc_recv_* */
    if( (sc->uart == BRL1_LOCALHUB_UART) && L1_interrupts_connected ) {
	return( sc_recv_intr( sc, ch, resp, len, __WAIT_RECV ) );
    }
    else {
	return( sc_recv_poll( sc, ch, resp, len, __WAIT_RECV ) );
    }
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}

/* sc_command_kern is a knuckle-dragging, no-patience version of sc_command
 * used in situations where the kernel has a command that shouldn't be
 * delayed until the send buffer clears.  sc_command should be used instead
 * under most circumstances.
 */

int
sc_command_kern( l1sc_t *sc, int ch, char *cmd, char *resp, int *len )
{
#ifndef CONFIG_SERIAL_SGI_L1_PROTOCOL
    return SC_NMSG;
#else
    int result;

    if ( IS_RUNNING_ON_SIMULATOR() )
    	return SC_NMSG;

    if( (result = sc_send( sc, ch, cmd, *len, 1 )) < 0 ) {
	return result;
    }

    return( sc_recv_poll( sc, ch, resp, len, __WAIT_RECV ) );
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}



/* sc_poll checks the queue corresponding to the given
 * subchannel to see if there's anything available.  If
 * not, it kicks the brl1 layer and then checks again.
 *
 * Returns 1 if input is available on the given queue,
 * 0 otherwise.
 */

int
sc_poll( l1sc_t *sc, int ch )
{
    brl1_sch_t *subch = &(sc->subch[ch]);

    if( atomic_read(&subch->packet_arrived) )
	return 1;

    brl1_receive( sc, SERIAL_POLLED_MODE );

    if( atomic_read(&subch->packet_arrived) )
	return 1;

    return 0;
}

/* for now, sc_init just calls brl1_init */

void
sc_init( l1sc_t *sc, nasid_t nasid, net_vec_t uart )
{
    if ( !IS_RUNNING_ON_SIMULATOR() )
    	brl1_init( sc, nasid, uart );
}

/* sc_dispatch_env_event handles events sent from the system control
 * network's environmental monitor tasks.
 */

#if	defined(LINUX_KERNEL_THREADS)

static void
sc_dispatch_env_event( uint code, int argc, char *args, int maxlen )
{
    int j, i = 0;
    uint32_t ESPcode;

    switch( code ) {
	/* for now, all codes do the same thing: grab two arguments
	 * and print a cmn_err_tag message */
      default:
	/* check number of arguments */
	if( argc != 2 ) {
	    L1_DBG_PRF(( "sc_dispatch_env_event: "
			 "expected 2 arguments, got %d\n", argc ));
	    return;
	}
	
	/* get ESP code (integer argument) */
	if( args[i++] != L1_ARG_INT ) {
	    L1_DBG_PRF(( "sc_dispatch_env_event: "
			 "expected integer argument\n" ));
	    return;
	}
	/* WARNING: highly endian */
	COPY_BUFFER_TO_INT(args, i, ESPcode);

	/* verify string argument */
	if( args[i++] != L1_ARG_ASCII ) {
	    L1_DBG_PRF(( "sc_dispatch_env_event: "
			 "expected an ASCII string\n" ));
	    return;
	}
	for( j = i; j < maxlen; j++ ) {
	    if( args[j] == '\0' ) break; /* found string termination */
	}
	if( j == maxlen ) {
	    j--;
	    L1_DBG_PRF(( "sc_dispatch_env_event: "
			 "message too long-- truncating\n" ));
	}

	/* strip out trailing cr/lf */
	for( ; 
	     j > 1 && ((args[j-1] == 0xd) || (args[j-1] == 0xa)); 
	     j-- );
	args[j] = '\0';
	
	/* strip out leading cr/lf */
	for( ;
	     i < j && ((args[i] == 0xd) || (args[i] == 0xa));
	     i++ );
    }
}


/* sc_event waits for events to arrive from the system controller, and
 * prints appropriate messages to the syslog.
 */

static void
sc_event( l1sc_t *sc, int ch )
{
    char event[BRL1_QSIZE];
    int i;
    int result;
    int event_len;
    uint32_t ev_src;
    uint32_t ev_code;
    int ev_argc;

    while(1) {
	
	bzero( event, BRL1_QSIZE );

	/*
	 * wait for an event 
	 */
	result = sc_recv_intr( sc, ch, event, &event_len, 1 );
	if( result != SC_SUCCESS ) {
	    printk(KERN_WARNING  "Error receiving sysctl event on nasid %d\n",
		     sc->nasid );
	}
	else {
	    /*
	     * an event arrived; break it down into useful pieces
	     */
#if defined(L1_DEBUG) && 0
	    int ix;
	    printf( "Event packet received:\n" );
	    for (ix = 0; ix < 64; ix++) {
		printf( "%x%x ", ((event[ix] >> 4) & ((uint64_t)0xf)),
			(event[ix] & ((uint64_t)0xf)) );
		if( (ix % 16) == 0xf ) printf( "\n" );
	    }
#endif /* L1_DEBUG */

	    i = 0;

	    /* get event source */
	    COPY_BUFFER_TO_INT(event, i, ev_src);
	    COPY_BUFFER_TO_INT(event, i, ev_code);

	    /* get arg count */
	    ev_argc = (event[i++] & 0xffUL);
	    
	    /* dispatch events by task */
	    switch( (ev_src & L1_ADDR_TASK_MASK) >> L1_ADDR_TASK_SHFT )
	    {
	      case L1_ADDR_TASK_ENV: /* environmental monitor event */
		sc_dispatch_env_event( ev_code, ev_argc, &(event[i]), 
				       BRL1_QSIZE - i );
		break;

	      default: /* unhandled task type */
		L1_DBG_PRF(( "Unhandled event type received from system "
			     "controllers: source task %x\n",
			     (ev_src & L1_ADDR_TASK_MASK) >> L1_ADDR_TASK_SHFT
			   ));
	    }
	}
	
    }			
}

/* sc_listen sets up a service thread to listen for incoming events.
 */

void
sc_listen( l1sc_t *sc )
{
    int result;
    unsigned long pl = 0;
    brl1_sch_t *subch;

    char        msg[BRL1_QSIZE];
    int         len;    /* length of message being sent */
    int         ch;     /* system controller subchannel used */

    extern int msc_shutdown_pri;

    /* grab the designated "event subchannel" */
    SUBCH_LOCK( sc, pl );
    subch = &(sc->subch[BRL1_EVENT_SUBCH]);
    if( subch->use != BRL1_SUBCH_FREE ) {
	SUBCH_UNLOCK( sc, pl );
	printk(KERN_WARNING  "sysctl event subchannel in use! "
		 "Not monitoring sysctl events.\n" );
	return;
    }
    subch->use = BRL1_SUBCH_RSVD;
    SUBCH_UNLOCK( sc, pl );

    atomic_set(&subch->packet_arrived, 0);
    subch->target = BRL1_LOCALHUB_UART;
    spin_lock_init( &(subch->data_lock) );
    sv_init( &(subch->arrive_sv), &(subch->data_lock), SV_MON_SPIN | SV_ORDER_FIFO /* | SV_INTS */);
    subch->tx_notify = NULL;
    subch->rx_notify = sc_data_ready;
    subch->iqp = snia_kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP,
				   NASID_TO_COMPACT_NODEID(sc->nasid) );
    ASSERT( subch->iqp );
    cq_init( subch->iqp );

    /* set up a thread to listen for events */
    sthread_create( "sysctl event handler", 0, 0, 0, msc_shutdown_pri,
		    KT_PS, (st_func_t *) sc_event,
		    (void *)sc, (void *)(uint64_t)BRL1_EVENT_SUBCH, 0, 0 );

    /* signal the L1 to begin sending events */
    bzero( msg, BRL1_QSIZE );
    ch = sc_open( sc, L1_ADDR_LOCAL );

    if( (len = sc_construct_msg( sc, ch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_EVENT_SUBCH, 2,
				 L1_ARG_INT, BRL1_EVENT_SUBCH )) < 0 )
    {
	sc_close( sc, ch );
	L1_DBG_PRF(( "Failure in sc_construct_msg (%d)\n", len ));
	goto err_return;
    }

    result = sc_command_kern( sc, ch, msg, msg, &len );
    if( result < 0 )
    {
	sc_close( sc, ch );
	L1_DBG_PRF(( "Failure in sc_command_kern (%d)\n", result ));
	goto err_return;
    }

    sc_close( sc, ch );

    result = sc_interpret_resp( msg, 0 );
    if( result < 0 )
    {
	L1_DBG_PRF(( "Failure in sc_interpret_resp (%d)\n", result ));
	goto err_return;
    }

    /* everything went fine; just return */
    return;
	
err_return:
    /* there was a problem; complain */
    printk(KERN_WARNING  "failed to set sysctl event-monitoring subchannel.  "
	     "Sysctl events will not be monitored.\n" );
}

#endif	/* LINUX_KERNEL_THREADS */
