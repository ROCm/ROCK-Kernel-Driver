/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/* In general, this file is organized in a hierarchy from lower-level
 * to higher-level layers, as follows:
 *
 *	UART routines
 *	Bedrock/L1 "PPP-like" protocol implementation
 *	System controller "message" interface (allows multiplexing
 *		of various kinds of requests and responses with
 *		console I/O)
 *	Console interfaces (there are two):
 *	  (1) "elscuart", used in the IP35prom and (maybe) some
 *		debugging situations elsewhere, and
 *	  (2) "l1_cons", the glue that allows the L1 to act
 *		as the system console for the stdio libraries
 *
 * Routines making use of the system controller "message"-style interface
 * can be found in l1_command.c.  Their names are leftover from early SN0, 
 * when the "module system controller" (msc) was known as the "entry level
 * system controller" (elsc).  The names and signatures of those functions 
 * remain unchanged in order to keep the SN0 -> SN1 system controller
 * changes fairly localized.
 */


#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/ksys/i2c.h>
#include <asm/sn/cmn_err.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>

#include <asm/sn/sn1/uart16550.h>


#if defined(EEPROM_DEBUG)
#define db_printf(x) printk x
#else
#define db_printf(x)
#endif

// From irix/kern/sys/SN/SN1/bdrkhspecregs.h
#define    HSPEC_UART_0              0x00000080    /* UART Registers         */

/*********************************************************************
 * Hardware-level (UART) driver routines.
 */

/* macros for reading/writing registers */

#define LD(x)		(*(volatile uint64_t *)(x))
#define SD(x, v)        (LD(x) = (uint64_t) (v))

/* location of uart receive/xmit data register */
#define L1_UART_BASE(n)	((ulong)REMOTE_HSPEC_ADDR((n), HSPEC_UART_0))
#define LOCAL_HUB	LOCAL_HUB_ADDR

#define ADDR_L1_REG(n, r)	\
    (L1_UART_BASE(n) | ( (r) << 3 ))

#define READ_L1_UART_REG(n, r) \
    ( LD(ADDR_L1_REG((n), (r))) )

#define WRITE_L1_UART_REG(n, r, v) \
    ( SD(ADDR_L1_REG((n), (r)), (v)) )


/* Avoid conflicts with symmon...*/
#define CONS_HW_LOCK(x)
#define CONS_HW_UNLOCK(x)

#define L1_CONS_HW_LOCK(sc)	CONS_HW_LOCK(sc->uart == BRL1_LOCALUART)
#define L1_CONS_HW_UNLOCK(sc)	CONS_HW_UNLOCK(sc->uart == BRL1_LOCALUART)

#if DEBUG
static int debuglock_ospl; /* For CONS_HW_LOCK macro */
#endif

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

#ifdef BRINGUP
#define UART_DELAY(x)	{ int i; i = x * 1000; while (--i); }
#else
#define UART_DELAY(x)	us_delay(x)
#endif

/*
 *	Some macros for handling Endian-ness
 */

#ifdef	LITTLE_ENDIAN
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
#else	/* BIG_ENDIAN */
#define COPY_INT_TO_BUFFER(_b, _i, _n)			\
	{						\
		bcopy((char *)&_n, _b, sizeof(_n));	\
		_i += sizeof(_n);			\
	}

#define COPY_BUFFER_TO_INT(_b, _i, _n)			\
	{						\
		bcopy(&_b[_i], &_n, sizeof(_n));	\
		_i += sizeof(_n);			\
	}

#define COPY_BUFFER_TO_BUFFER(_b, _i, _bn)		\
	{						\
            bcopy(&(_b[_i]), _bn, sizeof(int));		\
            _i += sizeof(int);				\
	}
#endif	/* LITTLE_ENDIAN */

int atomicAddInt(int *int_ptr, int value);
int atomicClearInt(int *int_ptr, int value);
void kmem_free(void *where, int size);

#define BCOPY(x,y,z)	memcpy(y,x,z)

extern char *bcopy(const char * src, char * dest, int count);


int 
get_L1_baud(void)
{
    return UART_BAUD_RATE;
}


/* uart driver functions */

static void
uart_delay( rtc_time_t delay_span )
{
    UART_DELAY( delay_span );
}

#define UART_PUTC_READY(n)	(READ_L1_UART_REG((n), REG_LSR) & LSR_XHRE)

static int
uart_putc( l1sc_t *sc ) 
{
#ifdef BRINGUP
    /* need a delay to avoid dropping chars */
    UART_DELAY(57);
#endif
    WRITE_L1_UART_REG( sc->nasid, REG_DAT,
		       sc->send[sc->sent] );
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

    L1_CONS_HW_LOCK( sc );

    WRITE_L1_UART_REG( nasid, REG_LCR, LCR_DLAB );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_DLH, (clkdiv >> 8) & 0xff );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_DLL, clkdiv & 0xff );
	uart_delay( UART_DELAY_SPAN );

    /* set operating parameters and set DLAB to 0 */
    WRITE_L1_UART_REG( nasid, REG_LCR, LCR_BITS8 | LCR_STOP1 );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_MCR, MCR_RTS | MCR_AFE );
	uart_delay( UART_DELAY_SPAN );

    /* disable interrupts */
    WRITE_L1_UART_REG( nasid, REG_ICR, 0x0 );
	uart_delay( UART_DELAY_SPAN );

    /* enable FIFO mode and reset both FIFOs */
    WRITE_L1_UART_REG( nasid, REG_FCR, FCR_FIFOEN );
	uart_delay( UART_DELAY_SPAN );
    WRITE_L1_UART_REG( nasid, REG_FCR,
	FCR_FIFOEN | FCR_RxFIFO | FCR_TxFIFO );

    L1_CONS_HW_UNLOCK( sc );
}

static void
uart_intr_enable( l1sc_t *sc, u_char mask )
{
    u_char lcr_reg, icr_reg;
    nasid_t nasid = sc->nasid;

    L1_CONS_HW_LOCK(sc);

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

    L1_CONS_HW_UNLOCK(sc);
}

static void
uart_intr_disable( l1sc_t *sc, u_char mask )
{
    u_char lcr_reg, icr_reg;
    nasid_t nasid = sc->nasid;

    L1_CONS_HW_LOCK(sc);

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

    L1_CONS_HW_UNLOCK(sc);
}

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

#ifdef SABLE
#define RTR_UART_PUTC_TIMEOUT	0
#define RTR_UART_DELAY_SPAN	0
#define RTR_UART_INIT_TIMEOUT	0
#else
#define RTR_UART_PUTC_TIMEOUT	UART_PUTC_TIMEOUT*10
#define RTR_UART_DELAY_SPAN	UART_DELAY_SPAN
#define RTR_UART_INIT_TIMEOUT	UART_INIT_TIMEOUT*10
#endif

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

#define L1SC_SEND_LOCK(l,pl)						\
     { if( (l)->uart == BRL1_LOCALUART )				\
	 (pl) = mutex_spinlock_spl( &((l)->send_lock), spl7 ); }

#define L1SC_SEND_UNLOCK(l,pl)				\
     { if( (l)->uart == BRL1_LOCALUART )		\
	 mutex_spinunlock( &((l)->send_lock), (pl)); }

#define L1SC_RECV_LOCK(l,pl)						\
     { if( (l)->uart == BRL1_LOCALUART )				\
	 (pl) = mutex_spinlock_spl( &((l)->recv_lock), spl7 ); }

#define L1SC_RECV_UNLOCK(l,pl)				\
     { if( (l)->uart == BRL1_LOCALUART )		\
	 mutex_spinunlock( &((l)->recv_lock), (pl)); }


/*********************************************************************
 * subchannel manipulation 
 *
 * The SUBCH_[UN]LOCK macros are used to arbitrate subchannel
 * allocation.  SUBCH_DATA_[UN]LOCK control access to data structures
 * associated with particular subchannels (e.g., receive queues).
 *
 */


#ifdef SPINLOCKS_WORK
#define SUBCH_LOCK(sc,pl) \
     (pl) = mutex_spinlock_spl( &((sc)->subch_lock), spl7 )
#define SUBCH_UNLOCK(sc,pl) \
     mutex_spinunlock( &((sc)->subch_lock), (pl) )

#define SUBCH_DATA_LOCK(sbch,pl) \
     (pl) = mutex_spinlock_spl( &((sbch)->data_lock), spl7 )
#define SUBCH_DATA_UNLOCK(sbch,pl) \
     mutex_spinunlock( &((sbch)->data_lock), (pl) )
#else
#define SUBCH_LOCK(sc,pl) 
#define SUBCH_UNLOCK(sc,pl)
#define SUBCH_DATA_LOCK(sbch,pl)
#define SUBCH_DATA_UNLOCK(sbch,pl)
#endif	/* SPINLOCKS_WORK */

/*
 * set a function to be called for subchannel ch in the event of
 * a transmission low-water interrupt from the uart
 */
void
subch_set_tx_notify( l1sc_t *sc, int ch, brl1_notif_t func )
{
    int pl;
    L1SC_SEND_LOCK( sc, pl );
    sc->subch[ch].tx_notify = func;
    
    /* some upper layer is asking to be notified of low-water, but if the 
     * send buffer isn't already in use, we're going to need to get the
     * interrupts going on the uart...
     */
    if( func && !sc->send_in_use )
	uart_enable_xmit_intr( sc );
    L1SC_SEND_UNLOCK(sc, pl );
}

/*
 * set a function to be called for subchannel ch when data is received
 */
void
subch_set_rx_notify( l1sc_t *sc, int ch, brl1_notif_t func )
{
#ifdef SPINLOCKS_WORK
    int pl;
#endif
    brl1_sch_t *subch = &(sc->subch[ch]);

    SUBCH_DATA_LOCK( subch, pl );
    sc->subch[ch].rx_notify = func;
    SUBCH_DATA_UNLOCK( subch, pl );
}



/* get_myid is an internal function that reads the PI_CPU_NUM
 * register of the local bedrock to determine which of the
 * four possible CPU's "this" one is
 */
static int
get_myid( void )
{
    return( LD(LOCAL_HUB(PI_CPU_NUM)) );
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

extern l1sc_t * get_elsc( void );

/*
 * brl1_discard_packet is a dummy "receive callback" used to get rid
 * of packets we don't want
 */
void brl1_discard_packet( l1sc_t *sc, int ch )
{
    int pl;
    brl1_sch_t *subch = &sc->subch[ch];
    sc_cq_t *q = subch->iqp;
    SUBCH_DATA_LOCK( subch, pl );
    q->opos = q->ipos;
    atomicClearInt( &(subch->packet_arrived), ~((unsigned)0) );
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
    /* In the kernel, we track the depth of the C brick's UART's
     * fifo in software, and only check if the UART is accepting
     * characters when our count indicates that the fifo should
     * be full.
     *
     * For remote (router) UARTs, and also for the local (C brick)
     * UART in the prom, we check with the UART before sending every
     * character.
     */
    if( sc->uart == BRL1_LOCALUART ) 
    {
	CONS_HW_LOCK(1);
	if( !(sc->fifo_space) && UART_PUTC_READY( sc->nasid ) )
//	    sc->fifo_space = UART_FIFO_DEPTH;
	    sc->fifo_space = 1000;
	
	while( (sc->sent < sc->send_len) && (sc->fifo_space) ) {
	    uart_putc( sc );
	    sc->fifo_space--;
	    sc->sent++;
	}

	CONS_HW_UNLOCK(1);
    }

    else

    /* The following applies to all UARTs in the prom, and to remote
     * (router) UARTs in the kernel...
     */

#define TIMEOUT_RETRIES	30

    {
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
		if( tries < TIMEOUT_RETRIES ) {
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

int
brl1_send( l1sc_t *sc, char *msg, int len, u_char type_and_subch, int wait )
{
    int pl;
    int index;
    int pkt_len = 0;
    unsigned short crc = INIT_CRC;
    char *send_ptr = sc->send;

    L1SC_SEND_LOCK(sc, pl);

    if( sc->send_in_use ) {
	if( !wait ) {
	    L1SC_SEND_UNLOCK(sc, pl);
	    return 0; /* couldn't send anything; wait for buffer to drain */
	}
	else {
	    /* buffer's in use, but we're synchronous I/O, so we're going
	     * to send whatever's in there right now and take the buffer
	     */
	    while( sc->sent < sc->send_len )
		brl1_send_chars( sc );
	}
    }
    else {
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

    do {
	brl1_send_chars( sc );
    } while( (sc->sent < sc->send_len) && wait );

    if( sc->sent == sc->send_len ) {
	/* success! release the send buffer */
	sc->send_in_use = 0;
    }
    else if( !wait ) {
	/* enable low-water interrupts so buffer will be drained */
	uart_enable_xmit_intr(sc);
    }
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
int
brl1_send_cont( l1sc_t *sc )
{
    int pl;
    int done = 0;
    brl1_notif_t callups[BRL1_NUM_SUBCHANS];
    brl1_notif_t *callup;
    brl1_sch_t *subch;
    int index;

    L1SC_SEND_LOCK(sc, pl);
    brl1_send_chars( sc );
    done = (sc->sent == sc->send_len);
    if( done ) {

	sc->send_in_use = 0;
	uart_disable_xmit_intr(sc);

	/* collect pointers to callups *before* unlocking */
	subch = sc->subch;
	callup = callups;
	for( index = 0; index < BRL1_NUM_SUBCHANS; index++ ) {
	    *callup = subch->tx_notify;
	    subch++;
	    callup++;
	}
    }
    L1SC_SEND_UNLOCK(sc, pl);

    if( done ) {
	/* call any upper layer that's asked for low-water notification */
	callup = callups;
	for( index = 0; index < BRL1_NUM_SUBCHANS; index++ ) {
	    if( *callup )
		(*(*callup))( sc, index );
	    callup++;
	}
    }
    return 0;
}


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

#define STATE_SET(l,s)	((l)->brl1_state = (s))
#define STATE_GET(l)	((l)->brl1_state)

#define LAST_HDR_SET(l,h)	((l)->brl1_last_hdr = (h))
#define LAST_HDR_GET(l)		((l)->brl1_last_hdr)

#define SEQSTAMP_INCR(l)
#define SEQSTAMP_GET(l)

#define VALID_HDR(c)				\
    ( SUBCH((c)) <= SC_CONS_SYSTEM		\
	? PKT_TYPE((c)) == BRL1_REQUEST		\
	: ( PKT_TYPE((c)) == BRL1_RESPONSE ||	\
	    PKT_TYPE((c)) == BRL1_EVENT ) )

#define IS_TTY_PKT(l) \
         ( SUBCH(LAST_HDR_GET(l)) <= SC_CONS_SYSTEM ? 1 : 0 )


int
brl1_receive( l1sc_t *sc )
{
    int result;		/* value to be returned by brl1_receive */
    int c;		/* most-recently-read character	     	*/
    int pl;		/* priority level for UART receive lock */
    int done;		/* set done to break out of recv loop	*/
    sc_cq_t *q;		/* pointer to queue we're working with	*/

    result = BRL1_NO_MESSAGE;

    L1SC_RECV_LOCK( sc, pl );
    L1_CONS_HW_LOCK( sc );

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
		int sch_pl;		/* cookie for subchannel lock */
		brl1_notif_t callup;	/* "data ready" callup */

		/* whatever else may happen, we've seen a flag and we're
		 * starting a new packet
		 */
		STATE_SET( sc, BRL1_FLAG );
		SEQSTAMP_INCR(sc); /* bump the packet sequence counter */
		
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
		SUBCH_DATA_LOCK( subch, sch_pl );
		
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
		atomicAddInt( &(subch->packet_arrived), 1);
		callup = subch->rx_notify;
		SUBCH_DATA_UNLOCK( subch, sch_pl );

		if( callup ) {
		    L1_CONS_HW_UNLOCK( sc );
		    L1SC_RECV_UNLOCK( sc, pl );
		    (*callup)( sc, SUBCH(LAST_HDR_GET(sc)) );
		    L1SC_RECV_LOCK( sc, pl );
		    L1_CONS_HW_LOCK( sc );
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
    
    L1_CONS_HW_UNLOCK( sc );
    L1SC_RECV_UNLOCK(sc, pl);

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
    sc->getc_f = (uart == BRL1_LOCALUART ? uart_getc : rtr_uart_getc);
    sc->putc_f = (uart == BRL1_LOCALUART ? uart_putc : rtr_uart_putc);
    sc->sol = 1;
    subch = sc->subch;

    /* initialize L1 subchannels
     */

    /* assign processor TTY channels */
    for( i = 0; i < CPUS_PER_NODE; i++, subch++ ) {
	subch->use = BRL1_SUBCH_RSVD;
	subch->packet_arrived = 0;
	spinlock_init( &(subch->data_lock), NULL );
	sv_init( &(subch->arrive_sv), SV_FIFO, NULL );
	subch->tx_notify = NULL;
	/* (for now, drop elscuart packets in the kernel) */
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* assign system TTY channel (first free subchannel after each
     * processor's individual TTY channel has been assigned)
     */
    subch->use = BRL1_SUBCH_RSVD;
    subch->packet_arrived = 0;
    spinlock_init( &(subch->data_lock), NULL );
    sv_init( &(subch->arrive_sv), SV_FIFO, NULL );
    subch->tx_notify = NULL;
    if( sc->uart == BRL1_LOCALUART ) {
	subch->iqp = kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP,
				       NASID_TO_COMPACT_NODEID(nasid) );
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
	subch->packet_arrived = 0;
	subch->tx_notify = NULL;
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* remaining subchannels are free */
    for( ; i < BRL1_NUM_SUBCHANS; i++, subch++ ) {
	subch->use = BRL1_SUBCH_FREE;
	subch->packet_arrived = 0;
	subch->tx_notify = NULL;
	subch->rx_notify = brl1_discard_packet;
	subch->iqp = &sc->garbage_q;
    }

    /* initialize synchronization structures
     */
    spinlock_init( &(sc->send_lock), NULL );
    spinlock_init( &(sc->recv_lock), NULL );
    spinlock_init( &(sc->subch_lock), NULL );

    if( sc->uart == BRL1_LOCALUART ) {
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
	sc->modid = 
	    (sc->modid < 0 ? INVALID_MODULE : sc->modid);

	sc->verbose = 1;
    }
}


/*********************************************************************
 * These are interrupt-related functions used in the kernel to service
 * the L1.
 */

/*
 * brl1_intrd is the function which is called in a loop by the
 * xthread that services L1 interrupts.
 */
#ifdef IRIX
void
brl1_intrd( struct eframe_s *ep )
{
    u_char isr_reg;
    l1sc_t *sc = get_elsc();

    isr_reg = READ_L1_UART_REG(sc->nasid, REG_ISR);

    while( isr_reg & (ISR_RxRDY | ISR_TxRDY) ) {

	if( isr_reg & ISR_RxRDY ) {
	    brl1_receive(sc);
	}
	if( (isr_reg & ISR_TxRDY) || 
	    (sc->send_in_use && UART_PUTC_READY(sc->nasid)) ) 
	{
	    brl1_send_cont(sc);
	}
	isr_reg = READ_L1_UART_REG(sc->nasid, REG_ISR);
    }

    /* uart interrupts were blocked at bedrock when the the interrupt
     * was initially answered; reenable them now
     */
    intr_unblock_bit( sc->intr_cpu, UART_INTR );
    ep = ep; /* placate the compiler */
}
#endif



/* brl1_intr is called directly from the uart interrupt; after it runs, the
 * interrupt "daemon" xthread is signalled to continue.
 */
#ifdef IRIX
void
brl1_intr( struct eframe_s *ep )
{
    /* Disable the UART interrupt, giving the xthread time to respond.
     * When the daemon (xthread) finishes doing its thing, it will
     * unblock the interrupt.
     */
    intr_block_bit( get_elsc()->intr_cpu, UART_INTR );
    ep = ep; /* placate the compiler */
}


/* set up uart interrupt handling for this node's uart
 */
void
brl1_connect_intr( l1sc_t *sc )
{
    cpuid_t last_cpu;

    sc->intr_cpu = nodepda->node_first_cpu;

    if( intr_connect_level(sc->intr_cpu, UART_INTR, INTPEND0_MAXMASK,
			   (intr_func_t)brl1_intrd, 0, 
			   (intr_func_t)brl1_intr) )
	cmn_err(CE_PANIC, "brl1_connect_intr: Can't connect UART interrupt.");

    uart_enable_recv_intr( sc );
}
#endif	/* IRIX */

#ifdef SABLE
/* this function is called periodically to generate fake interrupts
 * and allow brl1_intrd to send/receive characters
 */
void
hubuart_service( void )
{
    l1sc_t *sc = get_elsc();
    /* note that we'll lose error state by reading the lsr_reg.
     * This is probably ok in the frictionless domain of sable.
     */
    int lsr_reg;
    nasid_t nasid = sc->nasid;
    lsr_reg = READ_L1_UART_REG( nasid, REG_LSR );
    if( lsr_reg & (LSR_RCA | LSR_XSRE) ) {
        REMOTE_HUB_PI_SEND_INTR(0, 0, UART_INTR);
    }
}
#endif /* SABLE */


/*********************************************************************
 * The following function allows the kernel to "go around" the
 * uninitialized l1sc structure to allow console output during
 * early system startup.
 */

/* These are functions to use from serial_in/out when in protocol
 * mode to send and receive uart control regs.
 */
void
brl1_send_control(int offset, int value)
{
	nasid_t nasid = get_nasid();
	WRITE_L1_UART_REG(nasid, offset, value); 
}

int
brl1_get_control(int offset)
{
	nasid_t nasid = get_nasid();
	return(READ_L1_UART_REG(nasid, offset)); 
}

#define PUTCHAR(ch) \
    { \
        while( !(READ_L1_UART_REG( nasid, REG_LSR ) & LSR_XHRE) );  \
        WRITE_L1_UART_REG( nasid, REG_DAT, (ch) ); \
    }

int
brl1_send_console_packet( char *str, int len )
{
    int sent = len;
    char crc_char;
    unsigned short crc = INIT_CRC;
    nasid_t nasid = get_nasid();

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

    return sent - len;
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

int
l1_cons_poll( l1sc_t *sc )
{
    /* in case this gets called before the l1sc_t structure for the module_t
     * struct for this node is initialized (i.e., if we're called with a
     * zero l1sc_t pointer)...
     */
    if( !sc ) {
	return 0;
    }

    if( sc->subch[SC_CONS_SYSTEM].packet_arrived ) {
	return 1;
    }

    brl1_receive( sc );

    if( sc->subch[SC_CONS_SYSTEM].packet_arrived ) {
	return 1;
    }
    return 0;
}


/* pull a character off of the system console queue (if one is available)
 */
int
l1_cons_getc( l1sc_t *sc )
{
    int c;
#ifdef SPINLOCKS_WORK
    int pl;
#endif
    brl1_sch_t *subch = &(sc->subch[SC_CONS_SYSTEM]);
    sc_cq_t *q = subch->iqp;

    if( !l1_cons_poll( sc ) ) {
	return 0;
    }

    SUBCH_DATA_LOCK( subch, pl );
    if( cq_empty( q ) ) {
	subch->packet_arrived = 0;
	SUBCH_DATA_UNLOCK( subch, pl );
	return 0;
    }
    cq_rem( q, c );
    if( cq_empty( q ) )
	subch->packet_arrived = 0;
    SUBCH_DATA_UNLOCK( subch, pl );

    return c;
}


/* initialize the system console subchannel
 */
void
l1_cons_init( l1sc_t *sc )
{
#ifdef SPINLOCKS_WORK
    int pl;
#endif
    brl1_sch_t *subch = &(sc->subch[SC_CONS_SYSTEM]);

    SUBCH_DATA_LOCK( subch, pl );
    subch->packet_arrived = 0;
    cq_init( subch->iqp );
    SUBCH_DATA_UNLOCK( subch, pl );
}


/*
 * Write a message to the L1 on the system console subchannel.
 *
 * Danger: don't use a non-zero value for the wait parameter unless you're
 * someone important (like a kernel error message).
 */
int
l1_cons_write( l1sc_t *sc, char *msg, int len, int wait )
{
    return( brl1_send( sc, msg, len, (SC_CONS_SYSTEM | BRL1_EVENT), wait ) );
}


/* 
 * Read as many characters from the system console receive queue as are
 * available there (up to avail bytes).
 */
int
l1_cons_read( l1sc_t *sc, char *buf, int avail )
{
    int pl;
    int before_wrap, after_wrap;
    brl1_sch_t *subch = &(sc->subch[SC_CONS_SYSTEM]);
    sc_cq_t *q = subch->iqp;

    if( !(subch->packet_arrived) )
	return 0;

    SUBCH_DATA_LOCK( subch, pl );
    if( q->opos > q->ipos ) {
	before_wrap = BRL1_QSIZE - q->opos;
	if( before_wrap >= avail ) {
	    before_wrap = avail;
	    after_wrap = 0;
	}
	else {
	    avail -= before_wrap;
	    after_wrap = q->ipos;
	    if( after_wrap > avail )
		after_wrap = avail;
	}
    }
    else {
	before_wrap = q->ipos - q->opos;
	if( before_wrap > avail )
	    before_wrap = avail;
	after_wrap = 0;
    }


    BCOPY( q->buf + q->opos, buf, before_wrap  );
    if( after_wrap )
        BCOPY( q->buf, buf + before_wrap, after_wrap  );
    q->opos = ((q->opos + before_wrap + after_wrap) % BRL1_QSIZE);

    subch->packet_arrived = 0;
    SUBCH_DATA_UNLOCK( subch, pl );

    return( before_wrap + after_wrap );
}
	

/*
 * Install a callback function for the system console subchannel 
 * to allow an upper layer to be notified when the send buffer 
 * has been emptied.
 */
void
l1_cons_tx_notif( l1sc_t *sc, brl1_notif_t func )
{
    subch_set_tx_notify( sc, SC_CONS_SYSTEM, func );
}


/*
 * Install a callback function for the system console subchannel
 * to allow an upper layer to be notified when a packet has been
 * received.
 */
void
l1_cons_rx_notif( l1sc_t *sc, brl1_notif_t func )
{
    subch_set_rx_notify( sc, SC_CONS_SYSTEM, func );
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

/* sc_data_ready is called to signal threads that are blocked on 
 * l1 input.
 */
void
sc_data_ready( l1sc_t *sc, int ch )
{
    brl1_sch_t *subch = &(sc->subch[ch]);
    sv_signal( &(subch->arrive_sv) );
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
    int pl;
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

    subch->packet_arrived = 0;
    subch->target = target;
    sv_init( &(subch->arrive_sv), SV_FIFO, NULL );
    spinlock_init( &(subch->data_lock), NULL );
    subch->tx_notify = NULL;
    subch->rx_notify = sc_data_ready;
    subch->iqp = kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP,
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
    brl1_sch_t *subch;
    int pl;

    SUBCH_LOCK( sc, pl );
    subch = &(sc->subch[ch]);
    if( subch->use != BRL1_SUBCH_RSVD ) {
        /* we're trying to close a subchannel that's not open */
        return SC_NOPEN;
    }

    subch->packet_arrived = 0;
    subch->use = BRL1_SUBCH_FREE;

    sv_broadcast( &(subch->arrive_sv) );
    sv_destroy( &(subch->arrive_sv) );
    spinlock_destroy( &(subch->data_lock) );

    ASSERT( subch->iqp && (subch->iqp != &sc->garbage_q) );
    kmem_free( subch->iqp, sizeof(sc_cq_t) );
    subch->iqp = &sc->garbage_q;

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
    if( sc->subch[ch].use == BRL1_SUBCH_FREE )
    {
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
        after_wrap;     /*   pieces to acommodate queue wraparound      */

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
    atomicAddInt( &(subch->packet_arrived), -1 );
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
    int pl;             /* lock cookie */
    int is_msg = 0;
    brl1_sch_t *subch = &(sc->subch[ch]);

    rtc_time_t exp_time = rtc_time() + block;

    /* sanity check-- make sure this is an open subchannel */
    if( subch->use == BRL1_SUBCH_FREE )
	return( SC_NOPEN );

    do {

        /* kick the next lower layer and see if it pulls anything in
         */
	brl1_receive( sc );
	is_msg = subch->packet_arrived;

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
    int pl;             /* lock cookie */
    int is_msg = 0;
    brl1_sch_t *subch = &(sc->subch[ch]);

    do {
	SUBCH_DATA_LOCK(subch, pl);
	is_msg = subch->packet_arrived;
	if( !is_msg && block ) {
	    /* wake me when you've got something */
	    subch->rx_notify = sc_data_ready;
	    sv_wait( &(subch->arrive_sv), 0, &(subch->data_lock), pl );
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
 * hairy if there were ever an L1 command reqeuest that spanned multiple
 * packets.  (On the other hand, that would require some additional
 * rewriting of the L1 command interface anyway.)
 */
#define __RETRIES	50
#define __WAIT_SEND	( sc->uart != BRL1_LOCALUART )
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
#ifdef notyet
    if( sc->uart == BRL1_LOCALUART ) {
	return( sc_recv_intr( sc, ch, resp, len, __WAIT_RECV ) );
    }
    else
#endif
    {
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

    if( subch->packet_arrived )
	return 1;

    brl1_receive( sc );

    if( subch->packet_arrived )
	return 1;

    return 0;
}

/* for now, sc_init just calls brl1_init
 */
void
sc_init( l1sc_t *sc, nasid_t nasid, net_vec_t uart )
{
    if ( !IS_RUNNING_ON_SIMULATOR() )
    	brl1_init( sc, nasid, uart );
}

/* sc_dispatch_env_event handles events sent from the system control
 * network's environmental monitor tasks.
 */
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
	
	/* write the event to syslog */
#ifdef IRIX
	cmn_err_tag( ESPcode, CE_WARN, &(args[i]) );
#endif
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
	    cmn_err( CE_WARN, "Error receiving sysctl event on nasid %d\n",
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
    int pl;
    int result;
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
	cmn_err( CE_WARN, "sysctl event subchannel in use! "
		 "Not monitoring sysctl events.\n" );
	return;
    }
    subch->use = BRL1_SUBCH_RSVD;
    SUBCH_UNLOCK( sc, pl );

    subch->packet_arrived = 0;
    subch->target = BRL1_LOCALUART;
    sv_init( &(subch->arrive_sv), SV_FIFO, NULL );
    spinlock_init( &(subch->data_lock), NULL );
    subch->tx_notify = NULL;
    subch->rx_notify = sc_data_ready;
    subch->iqp = kmem_zalloc_node( sizeof(sc_cq_t), KM_NOSLEEP,
				   NASID_TO_COMPACT_NODEID(sc->nasid) );
    ASSERT( subch->iqp );
    cq_init( subch->iqp );

#ifdef LINUX_KERNEL_THREADS
    /* set up a thread to listen for events */
    sthread_create( "sysctl event handler", 0, 0, 0, msc_shutdown_pri,
		    KT_PS, (st_func_t *) sc_event,
		    (void *)sc, (void *)(uint64_t)BRL1_EVENT_SUBCH, 0, 0 );
#endif

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
    cmn_err( CE_WARN, "failed to set sysctl event-monitoring subchannel.  "
	     "Sysctl events will not be monitored.\n" );
}


/*********************************************************************
 * elscuart functions.  These provide a uart-like interface to the
 * bedrock/l1 protocol console channels.  They are similar in form
 * and intent to the elscuart_* functions defined for SN0 in elsc.c.
 *
 */

int _elscuart_flush( l1sc_t *sc );

/* Leave room in queue for CR/LF */
#define ELSCUART_LINE_MAX       (BRL1_QSIZE - 2)


/*
 * _elscuart_putc provides an entry point to the L1 interface driver;
 * writes a single character to the output queue.  Flushes at the
 * end of each line, and translates newlines into CR/LF.
 *
 * The kernel should generally use l1_cons_write instead, since it assumes
 * buffering, translation, prefixing, etc. are done at a higher
 * level.
 *
 */
int
_elscuart_putc( l1sc_t *sc, int c )
{
    sc_cq_t *q;
    
    q = &(sc->oq[ MAP_OQ(L1_ELSCUART_SUBCH(get_myid())) ]);

    if( c != '\n' && c != '\r' && cq_used(q) >= ELSCUART_LINE_MAX ) {
        cq_add( q, '\r' );
        cq_add( q, '\n' );
         _elscuart_flush( sc );
        sc->sol = 1;
    }

    if( sc->sol && c != '\r' ) {
        char            prefix[16], *s;

        if( cq_room( q ) < 8 && _elscuart_flush(sc) < 0 )
        {
            return -1;
        }
	
	if( sc->verbose )
	{
#ifdef  SUPPORT_PRINTING_M_FORMAT
	    sprintf( prefix,
		     "%c %d%d%d %M:",
		     'A' + get_myid(),
		     sc->nasid / 100,
		     (sc->nasid / 10) % 10,
		     sc->nasid / 10,
		     sc->modid );
#else
	    sprintf( prefix,
		     "%c %d%d%d 0x%x:",
		     'A' + get_myid(),
		     sc->nasid / 100,
		     (sc->nasid / 10) % 10,
		     sc->nasid / 10,
		     sc->modid );
#endif
	    
	    for( s = prefix; *s; s++ )
		cq_add( q, *s );
	}	    
	sc->sol = 0;

    }

    if( cq_room( q ) < 2 && _elscuart_flush(sc) < 0 )
    {
        return -1;
    }

    if( c == '\n' ) {
        cq_add( q, '\r' );
        sc->sol = 1;
    }

    cq_add( q, (u_char) c );

    if( c == '\n' ) {
        /* flush buffered line */
        if( _elscuart_flush( sc ) < 0 )
        {
            return -1;
        }
    }

    if( c== '\r' )
    {
        sc->sol = 1;
    }

    return 0;
}


/*
 * _elscuart_getc reads a character from the input queue.  This
 * routine blocks.
 */
int
_elscuart_getc( l1sc_t *sc )
{
    int r;

    while( (r = _elscuart_poll( sc )) == 0 );

    if( r < 0 ) {
	/* some error occured */
	return r;
    }

    return _elscuart_readc( sc );
}



/*
 * _elscuart_poll returns 1 if characters are ready for the
 * calling processor, 0 if they are not
 */
int
_elscuart_poll( l1sc_t *sc )
{
    int result;

    if( sc->cons_listen ) {
        result = l1_cons_poll( sc );
        if( result )
            return result;
    }

    return sc_poll( sc, L1_ELSCUART_SUBCH(get_myid()) );
}



/* _elscuart_readc is to be used only when _elscuart_poll has
 * indicated that a character is waiting.  Pulls a character
 * of this processor's console queue and returns it.
 *
 */
int
_elscuart_readc( l1sc_t *sc )
{
    int c, pl;
    sc_cq_t *q;
    brl1_sch_t *subch;

    if( sc->cons_listen ) {
	subch = &(sc->subch[ SC_CONS_SYSTEM ]);
	q = subch->iqp;
	
	SUBCH_DATA_LOCK( subch, pl );
        if( !cq_empty( q ) ) {
            cq_rem( q, c );
	    if( cq_empty( q ) ) {
		subch->packet_arrived = 0;
	    }
	    SUBCH_DATA_UNLOCK( subch, pl );
            return c;
        }
	SUBCH_DATA_UNLOCK( subch, pl );
    }

    subch = &(sc->subch[ L1_ELSCUART_SUBCH(get_myid()) ]);
    q = subch->iqp;

    SUBCH_DATA_LOCK( subch, pl );
    if( cq_empty( q ) ) {
	SUBCH_DATA_UNLOCK( subch, pl );
        return -1;
    }

    cq_rem( q, c );
    if( cq_empty ( q ) ) {
	subch->packet_arrived = 0;
    }
    SUBCH_DATA_UNLOCK( subch, pl );

    return c;
}


/*
 * _elscuart_flush flushes queued output to the the L1.
 * This routine blocks until the queue is flushed.
 */
int
_elscuart_flush( l1sc_t *sc )
{
    int r, n;
    char buf[BRL1_QSIZE];
    sc_cq_t *q = &(sc->oq[ MAP_OQ(L1_ELSCUART_SUBCH(get_myid())) ]);

    while( (n = cq_used(q)) ) {

        /* buffer queue contents */
        r = BRL1_QSIZE - q->opos;

        if( n > r ) {
            BCOPY( q->buf + q->opos, buf, r  );
            BCOPY( q->buf, buf + r, n - r  );
        } else {
            BCOPY( q->buf + q->opos, buf, n  );
        }

        /* attempt to send buffer contents */
        r = brl1_send( sc, buf, cq_used( q ), 
		       (BRL1_EVENT | L1_ELSCUART_SUBCH(get_myid())), 1 );

        /* if no error, dequeue the sent characters; otherwise,
         * return the error
         */
        if( r >= SC_SUCCESS ) {
            q->opos = (q->opos + r) % BRL1_QSIZE;
        }
        else {
            return r;
        }
    }

    return 0;
}



/* _elscuart_probe returns non-zero if the L1 (and
 * consequently the elscuart) can be accessed
 */
int
_elscuart_probe( l1sc_t *sc )
{
#ifndef CONFIG_SERIAL_SGI_L1_PROTOCOL
    return 0;
#else
    char ver[BRL1_QSIZE];
    extern int elsc_version( l1sc_t *, char * );
    if ( IS_RUNNING_ON_SIMULATOR() )
    	return 0;
    return( elsc_version(sc, ver) >= 0 );
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}



/* _elscuart_init zeroes out the l1sc_t console
 * queues for this processor's console subchannel.
 */
void
_elscuart_init( l1sc_t *sc )
{
    int pl;
    brl1_sch_t *subch = &sc->subch[L1_ELSCUART_SUBCH(get_myid())];

    SUBCH_DATA_LOCK(subch, pl);

    subch->packet_arrived = 0;
    cq_init( subch->iqp );
    cq_init( &sc->oq[MAP_OQ(L1_ELSCUART_SUBCH(get_myid()))] );

    SUBCH_DATA_UNLOCK(subch, pl);
}


#ifdef IRIX

/* elscuart_syscon_listen causes the processor on which it's
 * invoked to "listen" to the system console subchannel (that
 * is, subchannel 4) for console input.
 */
void
elscuart_syscon_listen( l1sc_t *sc )
{
    int pl;
    brl1_sch_t *subch = &(sc->subch[SC_CONS_SYSTEM]);

    /* if we're already listening, don't bother */
    if( sc->cons_listen )
        return;

    SUBCH_DATA_LOCK( subch, pl );

    subch->use = BRL1_SUBCH_RSVD;
    subch->packet_arrived = 0;

    SUBCH_DATA_UNLOCK( subch, pl );


    sc->cons_listen = 1;
}
#endif	/* IRIX */
