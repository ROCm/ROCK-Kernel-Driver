/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Include file for Alchemy Semiconductor's Au1000 CPU.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AU1000_H_
#define _AU1000_H_

/* SDRAM Controller */
#define CS_MODE_0                0x14000000
#define CS_MODE_1                0x14000004
#define CS_MODE_2                0x14000008

#define CS_CONFIG_0              0x1400000C
#define CS_CONFIG_1              0x14000010
#define CS_CONFIG_2              0x14000014

#define REFRESH_CONFIG           0x14000018
#define PRECHARGE_CMD            0x1400001C
#define AUTO_REFRESH_CMD         0x14000020

#define WRITE_EXTERN_0           0x14000024
#define WRITE_EXTERN_1           0x14000028
#define WRITE_EXTERN_2           0x1400002C

#define SDRAM_SLEEP              0x14000030
#define TOGGLE_CKE               0x14000034

/* Static Bus Controller */
#define STATIC_CONFIG_0          0x14001000
#define STATIC_TIMING_0          0x14001004
#define STATIC_ADDRESS_0         0x14001008

#define STATIC_CONFIG_1          0x14001010
#define STATIC_TIMING_1          0x14001014
#define STATIC_ADDRESS_1         0x14001018

#define STATIC_CONFIG_2          0x14001020
#define STATIC_TIMING_2          0x14001024
#define STATIC_ADDRESS_2         0x14001028

#define STATIC_CONFIG_3          0x14001030
#define STATIC_TIMING_3          0x14001034
#define STATIC_ADDRESS_3         0x14001038

/* DMA Controller 0 */
#define DMA0_MODE_SET            0x14002000
#define DMA0_MODE_CLEAR          0x14002004
#define DMA0_PERIPHERAL_ADDR     0x14002008
#define DMA0_BUFFER0_START       0x1400200C
#define DMA0_BUFFER0_COUNT       0x14002010
#define DMA0_BUFFER1_START       0x14002014
#define DMA0_BUFFER1_COUNT       0x14002018

/* DMA Controller 1 */
#define DMA1_MODE_SET            0x14002100
#define DMA1_MODE_CLEAR          0x14002104
#define DMA1_PERIPHERAL_ADDR     0x14002108
#define DMA1_BUFFER0_START       0x1400210C
#define DMA1_BUFFER0_COUNT       0x14002110
#define DMA1_BUFFER1_START       0x14002114
#define DMA1_BUFFER1_COUNT       0x14002118

/* DMA Controller 2 */
#define DMA2_MODE_SET            0x14002200
#define DMA2_MODE_CLEAR          0x14002204
#define DMA2_PERIPHERAL_ADDR     0x14002208
#define DMA2_BUFFER0_START       0x1400220C
#define DMA2_BUFFER0_COUNT       0x14002210
#define DMA2_BUFFER1_START       0x14002214
#define DMA2_BUFFER1_COUNT       0x14002218

/* DMA Controller 3 */
#define DMA3_MODE_SET            0x14002300
#define DMA3_MODE_CLEAR          0x14002304
#define DMA3_PERIPHERAL_ADDR     0x14002308
#define DMA3_BUFFER0_START       0x1400230C
#define DMA3_BUFFER0_COUNT       0x14002310
#define DMA3_BUFFER1_START       0x14002314
#define DMA3_BUFFER1_COUNT       0x14002318

/* DMA Controller 4 */
#define DMA4_MODE_SET            0x14002400
#define DMA4_MODE_CLEAR          0x14002404
#define DMA4_PERIPHERAL_ADDR     0x14002408
#define DMA4_BUFFER0_START       0x1400240C
#define DMA4_BUFFER0_COUNT       0x14002410
#define DMA4_BUFFER1_START       0x14002414
#define DMA4_BUFFER1_COUNT       0x14002418

/* DMA Controller 5 */
#define DMA5_MODE_SET            0x14002500
#define DMA5_MODE_CLEAR          0x14002504
#define DMA5_PERIPHERAL_ADDR     0x14002508
#define DMA5_BUFFER0_START       0x1400250C
#define DMA5_BUFFER0_COUNT       0x14002510
#define DMA5_BUFFER1_START       0x14002514
#define DMA5_BUFFER1_COUNT       0x14002518

/* DMA Controller 6 */
#define DMA6_MODE_SET            0x14002600
#define DMA6_MODE_CLEAR          0x14002604
#define DMA6_PERIPHERAL_ADDR     0x14002608
#define DMA6_BUFFER0_START       0x1400260C
#define DMA6_BUFFER0_COUNT       0x14002610
#define DMA6_BUFFER1_START       0x14002614
#define DMA6_BUFFER1_COUNT       0x14002618

/* DMA Controller 7 */
#define DMA7_MODE_SET            0x14002700
#define DMA7_MODE_CLEAR          0x14002704
#define DMA7_PERIPHERAL_ADDR     0x14002708
#define DMA7_BUFFER0_START       0x1400270C
#define DMA7_BUFFER0_COUNT       0x14002710
#define DMA7_BUFFER1_START       0x14002714
#define DMA7_BUFFER1_COUNT       0x14002718

/* Interrupt Controller 0 */
#define INTC0_CONFIG0_READ        0x10400040
#define INTC0_CONFIG0_SET         0x10400040
#define INTC0_CONFIG0_CLEAR       0x10400044

#define INTC0_CONFIG1_READ        0x10400048
#define INTC0_CONFIG1_SET         0x10400048
#define INTC0_CONFIG1_CLEAR       0x1040004C

#define INTC0_CONFIG2_READ        0x10400050
#define INTC0_CONFIG2_SET         0x10400050
#define INTC0_CONFIG2_CLEAR       0x10400054

#define INTC0_REQ0_INT            0x10400054
#define INTC0_SOURCE_READ         0x10400058
#define INTC0_SOURCE_SET          0x10400058
#define INTC0_SOURCE_CLEAR        0x1040005C
#define INTC0_REQ1_INT            0x1040005C

#define INTC0_ASSIGN_REQ_READ     0x10400060
#define INTC0_ASSIGN_REQ_SET      0x10400060
#define INTC0_ASSIGN_REQ_CLEAR    0x10400064

#define INTC0_WAKEUP_READ         0x10400068
#define INTC0_WAKEUP_SET          0x10400068
#define INTC0_WAKEUP_CLEAR        0x1040006C

#define INTC0_MASK_READ           0x10400070
#define INTC0_MASK_SET            0x10400070
#define INTC0_MASK_CLEAR          0x10400074

#define INTC0_R_EDGE_DETECT       0x10400078
#define INTC0_R_EDGE_DETECT_CLEAR 0x10400078
#define INTC0_F_EDGE_DETECT_CLEAR 0x1040007C

#define INTC0_TEST_BIT            0x10400080

/* Interrupt Controller 1 */
#define INTC1_CONFIG0_READ        0x11800040
#define INTC1_CONFIG0_SET         0x11800040
#define INTC1_CONFIG0_CLEAR       0x11800044

#define INTC1_CONFIG1_READ        0x11800048
#define INTC1_CONFIG1_SET         0x11800048
#define INTC1_CONFIG1_CLEAR       0x1180004C

#define INTC1_CONFIG2_READ        0x11800050
#define INTC1_CONFIG2_SET         0x11800050
#define INTC1_CONFIG2_CLEAR       0x11800054

#define INTC1_REQ0_INT            0x11800054
#define INTC1_SOURCE_READ         0x11800058
#define INTC1_SOURCE_SET          0x11800058
#define INTC1_SOURCE_CLEAR        0x1180005C
#define INTC1_REQ1_INT            0x1180005C

#define INTC1_ASSIGN_REQ_READ     0x11800060
#define INTC1_ASSIGN_REQ_SET      0x11800060
#define INTC1_ASSIGN_REQ_CLEAR    0x11800064

#define INTC1_WAKEUP_READ         0x11800068
#define INTC1_WAKEUP_SET          0x11800068
#define INTC1_WAKEUP_CLEAR        0x1180006C

#define INTC1_MASK_READ           0x11800070
#define INTC1_MASK_SET            0x11800070
#define INTC1_MASK_CLEAR          0x11800074

#define INTC1_R_EDGE_DETECT       0x11800078
#define INTC1_R_EDGE_DETECT_CLEAR 0x11800078
#define INTC1_F_EDGE_DETECT_CLEAR 0x1180007C

#define INTC1_TEST_BIT            0x11800080

/* Interrupt Configuration Modes */
#define INTC_INT_DISABLED                0
#define INTC_INT_RISE_EDGE             0x1
#define INTC_INT_FALL_EDGE             0x2
#define INTC_INT_RISE_AND_FALL_EDGE    0x3
#define INTC_INT_HIGH_LEVEL            0x5
#define INTC_INT_LOW_LEVEL             0x6
#define INTC_INT_HIGH_AND_LOW_LEVEL    0x7

/* Interrupt Numbers */
#define AU1000_UART0_INT          0
#define AU1000_UART1_INT          1
#define AU1000_UART2_INT          2
#define AU1000_UART3_INT          3
#define AU1000_SSI0_INT           4
#define AU1000_SSI1_INT           5
#define AU1000_DMA0_INT           6
#define AU1000_DMA1_INT           7
#define AU1000_DMA2_INT           8
#define AU1000_DMA3_INT           9
#define AU1000_DMA4_INT           10
#define AU1000_DMA5_INT           11
#define AU1000_DMA6_INT           12
#define AU1000_DMA7_INT           13
#define AU1000_PC0_INT            14
#define AU1000_PC0_MATCH0_INT     15
#define AU1000_PC0_MATCH1_INT     16
#define AU1000_PC0_MATCH2_INT     17
#define AU1000_PC1_INT            18
#define AU1000_PC1_MATCH0_INT     19
#define AU1000_PC1_MATCH1_INT     20
#define AU1000_PC1_MATCH2_INT     21
#define AU1000_IRDA_TX_INT        22
#define AU1000_IRDA_RX_INT        23
#define AU1000_USB_DEV_REQ_INT    24
#define AU1000_USB_DEV_SUS_INT    25
#define AU1000_USB_HOST_INT       26
#define AU1000_ACSYNC_INT         27
#define AU1000_MAC0_DMA_INT       28
#define AU1000_MAC1_DMA_INT       29
#define AU1000_ETH0_IRQ           AU1000_MAC0_DMA_INT
#define AU1000_ETH1_IRQ           AU1000_MAC1_DMA_INT
#define AU1000_I2S_UO_INT         30
#define AU1000_AC97_INT           31
#define AU1000_LAST_INTC0_INT     AU1000_AC97_INT
#define AU1000_GPIO_0             32
#define AU1000_GPIO_1             33
#define AU1000_GPIO_2             34
#define AU1000_GPIO_3             35
#define AU1000_GPIO_4             36
#define AU1000_GPIO_5             37
#define AU1000_GPIO_6             38
#define AU1000_GPIO_7             39
#define AU1000_GPIO_8             40
#define AU1000_GPIO_9             41
#define AU1000_GPIO_10            42
#define AU1000_GPIO_11            43
#define AU1000_GPIO_12            44
#define AU1000_GPIO_13            45
#define AU1000_GPIO_14            46
#define AU1000_GPIO_15            47
#define AU1000_GPIO_16            48
#define AU1000_GPIO_17            49
#define AU1000_GPIO_18            50
#define AU1000_GPIO_19            51
#define AU1000_GPIO_20            52
#define AU1000_GPIO_21            53
#define AU1000_GPIO_22            54
#define AU1000_GPIO_23            55
#define AU1000_GPIO_24            56
#define AU1000_GPIO_25            57
#define AU1000_GPIO_26            58
#define AU1000_GPIO_27            59
#define AU1000_GPIO_28            60
#define AU1000_GPIO_29            61
#define AU1000_GPIO_30            62
#define AU1000_GPIO_31            63

/* Programmable Counters 0 and 1 */
#define PC_BASE                   0x11900000
#define PC_COUNTER_CNTRL          (PC_BASE + 0x14)
  #define PC_CNTRL_E1S            (1<<23)
  #define PC_CNTRL_T1S            (1<<20)
  #define PC_CNTRL_M21            (1<<19)
  #define PC_CNTRL_M11            (1<<18)
  #define PC_CNTRL_M01            (1<<17)
  #define PC_CNTRL_C1S            (1<<16)
  #define PC_CNTRL_BP             (1<<14)
  #define PC_CNTRL_EN1            (1<<13)
  #define PC_CNTRL_BT1            (1<<12)
  #define PC_CNTRL_EN0            (1<<11)
  #define PC_CNTRL_BT0            (1<<10)
  #define PC_CNTRL_E0             (1<<8)
  #define PC_CNTRL_E0S            (1<<7)
  #define PC_CNTRL_32S            (1<<5)
  #define PC_CNTRL_T0S            (1<<4)
  #define PC_CNTRL_M20            (1<<3)
  #define PC_CNTRL_M10            (1<<2)
  #define PC_CNTRL_M00            (1<<1)
  #define PC_CNTRL_C0S            (1<<0)

/* Programmable Counter 0 Registers */
#define PC0_TRIM                  (PC_BASE + 0)
#define PC0_COUNTER_WRITE         (PC_BASE + 4)
#define PC0_MATCH0                (PC_BASE + 8)
#define PC0_MATCH1                (PC_BASE + 0xC)
#define PC0_MATCH2                (PC_BASE + 0x10)
#define PC0_COUNTER_READ          (PC_BASE + 0x40)

/* Programmable Counter 1 Registers */
#define PC1_TRIM                  (PC_BASE + 0x44)
#define PC1_COUNTER_WRITE         (PC_BASE + 0x48)
#define PC1_MATCH0                (PC_BASE + 0x4C)
#define PC1_MATCH1                (PC_BASE + 0x50)
#define PC1_MATCH2                (PC_BASE + 0x54)
#define PC1_COUNTER_READ          (PC_BASE + 0x58)


/* I2S Controller */
#define I2S_DATA                  0x11000000
#define I2S_CONFIG_STATUS         0x11000001
#define I2S_CONTROL               0x11000002

/* Ethernet Controllers  */
#define AU1000_ETH0_BASE          0x10500000
#define AU1000_ETH1_BASE          0x10510000

/* 4 byte offsets from AU1000_ETH_BASE */
#define MAC_CONTROL                     0x0
  #define MAC_RX_ENABLE               (1<<2) 
  #define MAC_TX_ENABLE               (1<<3)
  #define MAC_DEF_CHECK               (1<<5) 
  #define MAC_SET_BL(X)       (((X)&0x3)<<6)
  #define MAC_AUTO_PAD                (1<<8)
  #define MAC_DISABLE_RETRY          (1<<10)
  #define MAC_DISABLE_BCAST          (1<<11)
  #define MAC_LATE_COL               (1<<12)
  #define MAC_HASH_MODE              (1<<13)
  #define MAC_HASH_ONLY              (1<<15)
  #define MAC_PASS_ALL               (1<<16)
  #define MAC_INVERSE_FILTER         (1<<17)
  #define MAC_PROMISCUOUS            (1<<18)
  #define MAC_PASS_ALL_MULTI         (1<<19)
  #define MAC_FULL_DUPLEX            (1<<20)
  #define MAC_NORMAL_MODE                 0
  #define MAC_INT_LOOPBACK           (1<<21)
  #define MAC_EXT_LOOPBACK           (1<<22)
  #define MAC_DISABLE_RX_OWN         (1<<23)
  #define MAC_BIG_ENDIAN             (1<<30)
  #define MAC_RX_ALL                 (1<<31)
#define MAC_ADDRESS_HIGH                0x4
#define MAC_ADDRESS_LOW                 0x8
#define MAC_MCAST_HIGH                  0xC
#define MAC_MCAST_LOW                  0x10
#define MAC_MII_CNTRL                  0x14
  #define MAC_MII_BUSY                (1<<0)
  #define MAC_MII_READ                     0 
  #define MAC_MII_WRITE               (1<<1)
  #define MAC_SET_MII_SELECT_REG(X)   (((X)&0x1f)<<6)
  #define MAC_SET_MII_SELECT_PHY(X)   (((X)&0x1f)<<11)
#define MAC_MII_DATA                   0x18
#define MAC_FLOW_CNTRL                 0x1C
  #define MAC_FLOW_CNTRL_BUSY         (1<<0)
  #define MAC_FLOW_CNTRL_ENABLE       (1<<1)
  #define MAC_PASS_CONTROL            (1<<2)
  #define MAC_SET_PAUSE(X)        (((X)&0xffff)<<16)
#define MAC_VLAN1_TAG                  0x20
#define MAC_VLAN2_TAG                  0x24

/* Ethernet Controller Enable */
#define MAC0_ENABLE               0x10520000
#define MAC1_ENABLE               0x10520004
  #define MAC_EN_CLOCK_ENABLE         (1<<0)
  #define MAC_EN_RESET0               (1<<1)
  #define MAC_EN_TOSS                 (1<<2)
  #define MAC_EN_CACHEABLE            (1<<3)
  #define MAC_EN_RESET1               (1<<4)
  #define MAC_EN_RESET2               (1<<5)
  #define MAC_DMA_RESET               (1<<6)

/* Ethernet Controller DMA Channels */

#define MAC0_TX_DMA_ADDR         0x14004000
#define MAC1_TX_DMA_ADDR         0x14004200
/* offsets from MAC_TX_RING_ADDR address */
#define MAC_TX_BUFF0_STATUS             0x0
  #define TX_FRAME_ABORTED            (1<<0)
  #define TX_JAB_TIMEOUT              (1<<1)
  #define TX_NO_CARRIER               (1<<2)
  #define TX_LOSS_CARRIER             (1<<3)
  #define TX_EXC_DEF                  (1<<4)
  #define TX_LATE_COLL_ABORT          (1<<5)
  #define TX_EXC_COLL                 (1<<6)
  #define TX_UNDERRUN                 (1<<7)
  #define TX_DEFERRED                 (1<<8)
  #define TX_LATE_COLL                (1<<9)
  #define TX_COLL_CNT_MASK         (0xF<<10)
  #define TX_PKT_RETRY               (1<<31)
#define MAC_TX_BUFF0_ADDR                0x4
  #define TX_DMA_ENABLE               (1<<0)
  #define TX_T_DONE                   (1<<1)
  #define TX_GET_DMA_BUFFER(X)    (((X)>>2)&0x3)
#define MAC_TX_BUFF0_LEN                 0x8
#define MAC_TX_BUFF1_STATUS             0x10
#define MAC_TX_BUFF1_ADDR               0x14
#define MAC_TX_BUFF1_LEN                0x18
#define MAC_TX_BUFF2_STATUS             0x20
#define MAC_TX_BUFF2_ADDR               0x24
#define MAC_TX_BUFF2_LEN                0x28
#define MAC_TX_BUFF3_STATUS             0x30
#define MAC_TX_BUFF3_ADDR               0x34
#define MAC_TX_BUFF3_LEN                0x38

#define MAC0_RX_DMA_ADDR         0x14004100
#define MAC1_RX_DMA_ADDR         0x14004300
/* offsets from MAC_RX_RING_ADDR */
#define MAC_RX_BUFF0_STATUS              0x0
  #define RX_FRAME_LEN_MASK           0x3fff
  #define RX_WDOG_TIMER              (1<<14)
  #define RX_RUNT                    (1<<15)
  #define RX_OVERLEN                 (1<<16)
  #define RX_COLL                    (1<<17)
  #define RX_ETHER                   (1<<18)
  #define RX_MII_ERROR               (1<<19)
  #define RX_DRIBBLING               (1<<20)
  #define RX_CRC_ERROR               (1<<21)
  #define RX_VLAN1                   (1<<22)
  #define RX_VLAN2                   (1<<23)
  #define RX_LEN_ERROR               (1<<24)
  #define RX_CNTRL_FRAME             (1<<25)
  #define RX_U_CNTRL_FRAME           (1<<26)
  #define RX_MCAST_FRAME             (1<<27)
  #define RX_BCAST_FRAME             (1<<28)
  #define RX_FILTER_FAIL             (1<<29)
  #define RX_PACKET_FILTER           (1<<30)
  #define RX_MISSED_FRAME            (1<<31)
  
  #define RX_ERROR (RX_WDOG_TIMER | RX_RUNT | RX_OVERLEN |  \
                    RX_COLL | RX_MII_ERROR | RX_CRC_ERROR | \
                    RX_LEN_ERROR | RX_U_CNTRL_FRAME | RX_MISSED_FRAME)
#define MAC_RX_BUFF0_ADDR                0x4
  #define RX_DMA_ENABLE               (1<<0)
  #define RX_T_DONE                   (1<<1)
  #define RX_GET_DMA_BUFFER(X)    (((X)>>2)&0x3)
  #define RX_SET_BUFF_ADDR(X)     ((X)&0xffffffc0)
#define MAC_RX_BUFF1_STATUS              0x10
#define MAC_RX_BUFF1_ADDR                0x14
#define MAC_RX_BUFF2_STATUS              0x20
#define MAC_RX_BUFF2_ADDR                0x24
#define MAC_RX_BUFF3_STATUS              0x30
#define MAC_RX_BUFF3_ADDR                0x34


/* UARTS 0-3 */
#define UART0_ADDR                0x11100000
#define UART1_ADDR                0x11200000
#define UART2_ADDR                0x11300000
#define UART3_ADDR                0x11400000

#define UART_RX		0	/* Receive buffer */
#define UART_TX		4	/* Transmit buffer */
#define UART_IER	8	/* Interrupt Enable Register */
#define UART_IIR	0xC	/* Interrupt ID Register */
#define UART_FCR	0x10	/* FIFO Control Register */
#define UART_LCR	0x14	/* Line Control Register */
#define UART_MCR	0x18	/* Modem Control Register */
#define UART_LSR	0x1C	/* Line Status Register */
#define UART_MSR	0x20	/* Modem Status Register */
#define UART_CLK	0x28	/* Baud Rat4e Clock Divider */
#define UART_MOD_CNTRL	0x100	/* Module Control */

#define UART_FCR_ENABLE_FIFO	0x01 /* Enable the FIFO */
#define UART_FCR_CLEAR_RCVR	0x02 /* Clear the RCVR FIFO */
#define UART_FCR_CLEAR_XMIT	0x04 /* Clear the XMIT FIFO */
#define UART_FCR_DMA_SELECT	0x08 /* For DMA applications */
#define UART_FCR_TRIGGER_MASK	0xF0 /* Mask for the FIFO trigger range */
#define UART_FCR_R_TRIGGER_1	0x00 /* Mask for receive trigger set at 1 */
#define UART_FCR_R_TRIGGER_4	0x40 /* Mask for receive trigger set at 4 */
#define UART_FCR_R_TRIGGER_8	0x80 /* Mask for receive trigger set at 8 */
#define UART_FCR_R_TRIGGER_14   0xA0 /* Mask for receive trigger set at 14 */
#define UART_FCR_T_TRIGGER_0	0x00 /* Mask for transmit trigger set at 0 */
#define UART_FCR_T_TRIGGER_4	0x10 /* Mask for transmit trigger set at 4 */
#define UART_FCR_T_TRIGGER_8    0x20 /* Mask for transmit trigger set at 8 */
#define UART_FCR_T_TRIGGER_12	0x30 /* Mask for transmit trigger set at 12 */

/*
 * These are the definitions for the Line Control Register
 */
#define UART_LCR_SBC	0x40	/* Set break control */
#define UART_LCR_SPAR	0x20	/* Stick parity (?) */
#define UART_LCR_EPAR	0x10	/* Even parity select */
#define UART_LCR_PARITY	0x08	/* Parity Enable */
#define UART_LCR_STOP	0x04	/* Stop bits: 0=1 stop bit, 1= 2 stop bits */
#define UART_LCR_WLEN5  0x00	/* Wordlength: 5 bits */
#define UART_LCR_WLEN6  0x01	/* Wordlength: 6 bits */
#define UART_LCR_WLEN7  0x02	/* Wordlength: 7 bits */
#define UART_LCR_WLEN8  0x03	/* Wordlength: 8 bits */

/*
 * These are the definitions for the Line Status Register
 */
#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

/*
 * These are the definitions for the Interrupt Identification Register
 */
#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */
#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */

/*
 * These are the definitions for the Interrupt Enable Register
 */
#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_MCR_OUT2	0x08	/* Out2 complement */
#define UART_MCR_OUT1	0x04	/* Out1 complement */
#define UART_MCR_RTS	0x02	/* RTS complement */
#define UART_MCR_DTR	0x01	/* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_RI	0x40	/* Ring Indicator */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_DDCD	0x08	/* Delta DCD */
#define UART_MSR_TERI	0x04	/* Trailing edge ring indicator */
#define UART_MSR_DDSR	0x02	/* Delta DSR */
#define UART_MSR_DCTS	0x01	/* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F	/* Any of the delta bits! */



/* SSIO */
#define SSI0_STATUS                0x11600000
#define SSI0_INT                   0x11600004
#define SSI0_INT_ENABLE            0x11600008
#define SSI0_CONFIG                0x11600020
#define SSI0_ADATA                 0x11600024
#define SSI0_CLKDIV                0x11600028
#define SSI0_CONTROL               0x11600100

/* SSI1 */
#define SSI1_STATUS                0x11680000
#define SSI1_INT                   0x11680004
#define SSI1_INT_ENABLE            0x11680008
#define SSI1_CONFIG                0x11680020
#define SSI1_ADATA                 0x11680024
#define SSI1_CLKDIV                0x11680028
#define SSI1_CONTROL               0x11680100

/* IrDA Controller */
#define IR_RING_PTR_STATUS        0x11500000
#define IR_RING_BASE_ADDR_H       0x11500004
#define IR_RING_BASE_ADDR_L       0x11500008
#define IR_RING_SIZE              0x1150000C
#define IR_RING_PROMPT            0x11500010
#define IR_RING_ADDR_CMPR         0x11500014
#define IR_CONFIG_1               0x11500020
#define IR_SIR_FLAGS              0x11500024
#define IR_ENABLE                 0x11500028
#define IR_READ_PHY_CONFIG        0x1150002C
#define IR_WRITE_PHY_CONFIG       0x11500030
#define IR_MAX_PKT_LEN            0x11500034
#define IR_RX_BYTE_CNT            0x11500038
#define IR_CONFIG_2               0x1150003C
#define IR_INTERFACE_CONFIG       0x11500040

/* GPIO */
#define TSTATE_STATE_READ         0x11900100
#define TSTATE_STATE_SET          0x11900100
#define OUTPUT_STATE_READ         0x11900108
#define OUTPUT_STATE_SET          0x11900108
#define OUTPUT_STATE_CLEAR        0x1190010C
#define PIN_STATE                 0x11900110

/* Power Management */
#define PM_SCRATCH_0                 0x11900018
#define PM_SCRATCH_1                 0x1190001C
#define PM_WAKEUP_SOURCE_MASK        0x11900034
#define PM_ENDIANESS                 0x11900038
#define PM_POWERUP_CONTROL           0x1190003C
#define PM_WAKEUP_CAUSE              0x1190005C
#define PM_SLEEP_POWER               0x11900078
#define PM_SLEEP                     0x1190007C

/* Clock Controller */
#define FQ_CNTRL_1                0x11900020
#define FQ_CNTRL_2                0x11900024
#define CLOCK_SOURCE_CNTRL        0x11900028
#define CPU_PLL_CNTRL             0x11900060
#define AUX_PLL_CNTRL             0x11900064

/* AC97 Controller */
#define AC97_CONFIG               0x10000000
#define AC97_STATUS               0x10000004
#define AC97_DATA                 0x10000008
#define AC97_CMD                  0x1000000C
#define AC97_CNTRL                0x10000010

#endif
