#ifndef _NET_H_SYMBA
#define _NET_H_SYMBA

/* transmit status bit definitions */
#define TX_STATUS_TXOK         (1<<13)     /* success */
#define TX_STATUS_TDLC         (1<<12)     /* dropped for late colls */
#define TX_STATUS_TCXSDFR      (1<<11)     /* excessive deferral */
#define TX_STATUS_TDEC         (1<<10)     /* excessive collisions */
#define TX_STATUS_TAUR         (1<<9)      /* abort on underrun/"jumbo" */
#define TX_STATUS_PDFRD        (1<<8)      /* packet deferred */
#define TX_STATUS_BCAST        (1<<7)      /* broadcast ok */
#define TX_STATUS_MCAST        (1<<6)      /* multicast ok */
#define TX_STATUS_CRCERR       (1<<5)      /* CRC error */
#define TX_STATUS_LC           (1<<4)      /* late collision */
#define TX_STATUS_CCNT_MASK    0xf         /* collision count */

#define T_TXOK      (1<<13)
#define T_TDLC      (1<<12)
#define T_TCXSDFR   (1<<11)
#define T_TDEC      (1<<10)
#define T_TAUR      (1<<9)
#define T_PDFRD     (1<<8)
#define T_BCAST     (1<<7)
#define T_MCAST     (1<<6)
#define T_LC        (1<<4)
#define T_CCNT_MASK 0xf

/* receive status bit definitions */
#define RX_STATUS_RXOVRN       (1<<23)     /* overrun */
#define RX_STATUS_CEPS         (1<<22)     /* carrier event already seen */
#define RX_STATUS_RXOK         (1<<21)     /* success */
#define RX_STATUS_BCAST        (1<<20)     /* broadcast ok */
#define RX_STATUS_MCAST        (1<<19)     /* multicast ok */
#define RX_STATUS_CRCERR       (1<<18)     /* CRC error */
#define RX_STATUS_DR           (1<<17)     /* dribble nibble */
#define RX_STATUS_RCV          (1<<16)     /* rx code violation */
#define RX_STATUS_PTL          (1<<15)     /* pkt > 1518 bytes */
#define RX_STATUS_PTS          (1<<14)     /* pkt < 64 bytes */
#define RX_STATUS_LEN_MASK     0x1fff      /* length mask */

#define EEPROM_LENGTH          100


/*  Serial EEPROM interface  */
#define EE_STATUS              0xf0
#define EE_CONTROL             0xf1
#define EE_WORD_ADDR           0xf2
#define EE_READ_DATA           0xf3
#define EE_WRITE_DATA          0xf4
#define EE_FEATURE_ENB         0xf5

/*  Use on EE_STATUS  */
#define EE_SEB                 (1<<8)
#define EE_SEE                     1

/*  Serial EEPROM commands */
#define EE_CONTROL_SEQ_READB   (1<<4)
#define EE_CONTROL_RND_WRITEB  (1<<5)
#define EE_CONTROL_RND_READB   ((1<<4)|(1<<5))

/*  Enable writing to serial EEPROM */
#define EE_WRITE_ENB                1

/*  The 885 configuration register */
#define MAC_CONFIG             0xa0
#define  MAC_CONFIG_SRST       1<<15
#define  MAC_CONFIG_ITXA       1<<13
#define  MAC_CONFIG_RXEN       1<<12
#define  MAC_CONFIG_INTLB      1<<10
#define  MAC_CONFIG_MODE_MASK  (1<<8|1<<9)
#define  MAC_CONFIG_MODE_TP    1<<8
#define  MAC_CONFIG_HUGEN      1<<5
#define  MAC_CONFIG_RETRYL     1<<4
#define  MAC_CONFIG_CRCEN      1<<3
#define  MAC_CONFIG_PADEN      1<<2
#define  MAC_CONFIG_FULLD      1<<1
#define  MAC_CONFIG_NOCFR      1<<0





#define TX_WAIT_SELECT         0x18
#define RX_CHANNEL_CONTROL     0x40

/* Tx channel status */
#define TX_DBDMA_REG           0x00
#define TX_CHANNEL_CONTROL     0x00
#define TX_CHANNEL_STATUS      0x04
#define  TX_STATUS_RUN         1<<15
#define  TX_STATUS_PAUSE       1<<14
#define  TX_STATUS_WAKE        1<<12
#define  TX_STATUS_DEAD        1<<11
#define  TX_STATUS_ACTIVE      1<<10
#define  TX_STATUS_BT          1<<8
#define  TX_STATUS_TXABORT     1<<7
#define  TX_STATUS_TXSR        1<<6

#define  TX_CHANNEL_RUN        TX_STATUS_RUN
#define  TX_CHANNEL_PAUSE      TX_STATUS_PAUSE
#define  TX_CHANNEL_WAKE       TX_STATUS_WAKE
#define  TX_CHANNEL_DEAD       TX_STATUS_DEAD
#define  TX_CHANNEL_ACTIVE     TX_STATUS_ACTIVE
#define  TX_CHANNEL_BT         TX_STATUS_BT
#define  TX_CHANNEL_TXABORT    TX_STATUS_TXABORT
#define  TX_CHANNEL_TXSR       TX_STATUS_TXSR

#define  TX_DBDMA_ENABLE       (TX_CHANNEL_WAKE | TX_CHANNEL_PAUSE | \
                                TX_CHANNEL_RUN )

/* Transmit command ptr lo register */
#define TX_CMD_PTR_LO          0x0c

/* Transmit interrupt select register */
#define TX_INT_SELECT          0x10

/* Transmit branch select register */
#define TX_BRANCH_SELECT       0x14

/* Transmit wait select register */
#define TX_WAIT_SELECT         0x18
#define  TX_WAIT_STAT_RECV     0x40

/* Rx channel status */
#define RX_DBDMA_REG           0x40
#define RX_CHANNEL_CONTROL     0x40
#define RX_CHANNEL_STATUS      0x44
#define  RX_STATUS_RUN         1<<15
#define  RX_STATUS_PAUSE       1<<14
#define  RX_STATUS_WAKE        1<<12
#define  RX_STATUS_DEAD        1<<11
#define  RX_STATUS_ACTIVE      1<<10
#define  RX_STATUS_BT          1<<8
#define  RX_STATUS_EOP         1<<6

#define  RX_CHANNEL_RUN        RX_STATUS_RUN
#define  RX_CHANNEL_PAUSE      RX_STATUS_PAUSE
#define  RX_CHANNEL_WAKE       RX_STATUS_WAKE
#define  RX_CHANNEL_DEAD       RX_STATUS_DEAD
#define  RX_CHANNEL_ACTIVE     RX_STATUS_ACTIVE
#define  RX_CHANNEL_BT         RX_STATUS_BT
#define  RX_CHANNEL_EOP        RX_STATUS_EOP

#define  RX_DBDMA_ENABLE       (RX_CHANNEL_WAKE | RX_CHANNEL_PAUSE | \
                                RX_CHANNEL_RUN)

/*  Receive command ptr lo  */
#define RX_CMD_PTR_LO          0x4c

/*  Receive interrupt select register */
#define RX_INT_SELECT          0x50
#define  RX_INT_SELECT_EOP     0x40

/*  Receive branch select  */
#define RX_BRANCH_SELECT       0x54
#define  RX_BRANCH_SELECT_EOP  0x40

/*  Receive wait select  */
#define RX_WAIT_SELECT         0x58
#define  RX_WAIT_SELECT_EOP    0x40

/*  Event status register  */
#define EVENT_STATUS           0x80
#define  EVENT_TXSR            1<<2
#define  EVENT_EOP             1<<1
#define  EVENT_TXABORT         1<<0

/*  Interrupt enable register  */
#define INTERRUPT_ENABLE       0x82

/*  Interrupt clear register  */
#define INTERRUPT_CLEAR        0x84

/*  Interrupt status register  */
#define INTERRUPT_STATUS_REG   0x86

/*  bits for the above three interrupt registers */
#define  INTERRUPT_INTE        1<<15   /* interrupt enable */
#define  INTERRUPT_WI          1<<9    /* wakeup interrupt */
#define  INTERRUPT_ERI         1<<8    /* early receive interrupt */
#define  INTERRUPT_PPET        1<<7    /* PCI Tx parity error */
#define  INTERRUPT_PBFT        1<<6    /* PCI Tx bus fault */
#define  INTERRUPT_IIDT        1<<5    /* illegal instruction Tx */
#define  INTERRUPT_DIT         1<<4    /* DBDMA Tx interrupt */
#define  INTERRUPT_PPER        1<<3    /* PCI Rx parity error */
#define  INTERRUPT_PBFR        1<<2    /* PCI Rx bus fault */
#define  INTERRUPT_IIDR        1<<1    /* illegal instruction Rx */
#define  INTERRUPT_DIR         1<<0    /* DBDMA Rx interrupt */

#define  INTERRUPT_TX_MASK     (INTERRUPT_PBFT|INTERRUPT_IIDT| \
                                INTERRUPT_PPET|INTERRUPT_DIT)
#define  INTERRUPT_RX_MASK     (INTERRUPT_PBFR|INTERRUPT_IIDR| \
                                INTERRUPT_PPER|INTERRUPT_DIR)

/*  chip revision register */
#define CHIP_REVISION_REG      0x8c
#define  CHIP_PCIREV_MASK      (0xf<<16)
#define  CHIP_PCIDEV_MASK      0xff

/*  Tx threshold register */
#define TX_THRESHOLD           0x94

/*  General purpose register */
#define GEN_PURPOSE_REG        0x9e

/*  General purpose pin control reg */
#define GEN_PIN_CONTROL_REG    0x9f

/*  DBDMA control register  */
#define DBDMA_CONTROL          0x90
#define  DBDMA_SRST            1<<31
#define  DBDMA_TDPCE           1<<23
#define  DBDMA_BE              1<<22
#define  DBDMA_TAP_MASK        (1<<19|1<<20|1<<21)
#define  DBDMA_RAP_MASK        (1<<16|1<<17|1<<18)
#define  DBDMA_DPMRLE          1<<15
#define  DBDMA_WIE             1<<14
#define  DBDMA_MP              1<<13
#define  DBDMA_SME             1<<12
#define  DBDMA_CME             1<<11
#define  DBDMA_DDPE            1<<10
#define  DBDMA_TDPE            1<<9
#define  DBDMA_EXTE            1<<8
#define  DBDMA_BST_MASK        (1<<4|1<<5|1<<6)
#define  DBDMA_BSR_MASK        (1<<0|1<<1|1<<2)

#define  DBDMA_BURST_1         (0x00)
#define  DBDMA_BURST_2         (0x01)
#define  DBDMA_BURST_4         (0x02)
#define  DBDMA_BURST_8         (0x03)
#define  DBDMA_BURST_16        (0x04)
#define  DBDMA_BURST_32        (0x05)
#define  DBDMA_BURST_64        (0x06)
#define  DBDMA_BURST_128       (0x07)

#define  DBDMA_TX_BST_SHIFT    (4)
#define  DBDMA_RX_BST_SHIFT    (0)

#define  DBDMA_TX_ARBITRATION_DEFAULT ( 1 << 19 )
#define  DBDMA_RX_ARBITRATION_DEFAULT ( 2 << 16 )


/*  Back-to-back interpacket gap register */
#define BTOB_INTP_GAP          0xa2
#define  BTOB_INTP_DEFAULT     0x18

/*  Non-back-to-back interpacket gap register */
#define NBTOB_INTP_GAP         0xa4

/*  MIIM command register */
#define MIIM_COMMAND           0xa6
#define  MIIM_SCAN             1<<1
#define  MIIM_RSTAT            1<<0

/*  MII address register */
#define MII_ADDRESS            0xa8
#define  MII_FIAD_MASK         (1<<8|1<<9|1<<10|1<<11|1<<12)
#define  MII_RGAD_MASK         (1<<0|1<<1|1<<2|1<<3|1<<4)

#define TPPMD_CONTROL_REG      0xa8
#define  TPPMD_FO              1<<1
#define  TPPMD_LB              1<<0

/*  MII read and write registers */
#define MII_WRITE_DATA         0xaa
#define MII_READ_DATA          0xac

/*  MII indicators */
#define MII_INDICATOR          0xae
#define  MII_NVALID            1<<2
#define  MII_SCAN              1<<1
#define  MII_BUSY              1<<0

/*  Address filter  */
#define ADDRESS_FILTER         0xd0
#define  ADDRESS_RPPRM         1<<3       /* multicast promis. mode */
#define  ADDRESS_RPPRO         1<<2       /* promiscuous mode */
#define  ADDRESS_RPAMC         1<<1       /* accept multicasts */
#define  ADDRESS_RPABC         1<<0       /* accept broadcasts */

/*  Station addresses

    Note that if the serial EEPROM is disabled, these values are all
    zero.  If, like us, you get the chips when they're fresh, they're
    also zero and you have to initialize the address */
#define STATION_ADDRESS_0      0xd2
#define STATION_ADDRESS_1      0xd4
#define STATION_ADDRESS_2      0xd6

/*  Hash tables  */
#define HASH_TABLE_0           0xd8
#define HASH_TABLE_1           0xda
#define HASH_TABLE_2           0xdc
#define HASH_TABLE_3           0xde

/* PHY indentifiers */
#define PHY_IDENTIFIER_0       0xe4
#define PHY_IDENTIFIER_1       0xe6

/*  MII Auto-negotiation register definitions  */

#define MII_AUTO_NEGOTIATION_CONTROL        (0x0000)
#define   MANC_PHY_RESET                      (0x8000)
#define   MANC_PHY_LOOPBACK_ENABLE            (0x4000)
#define   MANC_PHY_LOOPBACK_DISABLE           (0x0000)
#define   MANC_PHY_SPEED_100                  (0x2000)
#define   MANC_PHY_SPEED_10                   (0x0000)
#define   MANC_AUTO_NEGOTIATION_ENABLE        (0x1000)
#define   MANC_AUTO_NEGOTIATION_DISABLE       (0x0000)
#define   MANC_PHY_POWER_DOWN                 (0x0800)
#define   MANC_PHY_POWER_UP                   (0x0000)
#define   MANC_ISOLATE_ENABLE                 (0x0400)
#define   MANC_ISOLATE_DISABLE                (0x0000)
#define   MANC_RESTART_AUTO_NEGOTIATION       (0x0200)
#define   MANC_FULL_DUPLEX                    (0x0100)
#define   MANC_HALF_DUPLEX                    (0x0000)

#define MII_AUTO_NEGOTIATION_STATUS         (0x0001)
#define   MANS_100BASE_T4_HALF_DUPLEX         (0x8000)
#define   MANS_100BASE_X_FULL_DUPLEX          (0x4000)
#define   MANS_100BASE_X_HALF_DUPLEX          (0x2000)
#define   MANS_10MBS_FULL_DUPLEX              (0x1000)
#define   MANS_10MBS_HALF_DUPLEX              (0x0800)
#define   MANS_AUTO_NEGOTIATION_COMPLETE      (0x0020)
#define   MANS_REMOTE_FAULT                   (0x0010)
#define   MANS_AUTO_NEGOTIATION_ABILITY       (0x0008)
#define   MANS_LINK_STATUS                    (0x0004)
#define   MANS_JABBER_DETECT                  (0x0002)
#define   MANS_EXTENDED_CAPABILITY            (0x0001)

#define MII_PHY_IDENTIFIER_1                (0x0002)
#define MII_PHY_IDENTIFIER_2                (0x0003)

#define MII_AUTO_NEGOTIATION_ADVERTISEMENT  (0x0004)
#define   MANA_NEXT_PAGE                      (0x8000)
#define   MANA_REMOTE_FAULT                   (0x2000)
#define   MANA_TECHNOLOGY_ABILITY_MASK        (0x1FE0)
#define     MANATECH_10BASET_HALF_DUPLEX        (0x0020)
#define     MANATECH_10BASET_FULL_DUPLEX        (0x0040)
#define     MANATECH_100BASETX_HALF_DUPLEX      (0x0080)
#define     MANATECH_100BASETX_FULL_DUPLEX      (0x0100)
#define     MANATECH_100BASET4                  (0x0200)
#define   MANA_SELECTOR_MASK                  (0x001F)
#define     MANASELECTOR_802_3                  (0x0001)

#define MII_AUTO_NEGOTIATION_LINK_PARTNER   (0x0005)
#define   MANLP_NEXT_PAGE                     (0x8000)
#define   MANLP_ACKNOWLEDGE                   (0x4000)
#define   MANLP_REMOTE_FAULT                  (0x2000)
#define   MANLP_TECHNOLOGY_ABILITY_MASK       (0x1FE0)
#define   MANLP_SELECTOR_MASK                 (0x001F)

#define MII_AUTO_NEGOTIATION_EXPANSION      (0x0006)
#define   MANE_PARALLEL_DETECTION_FAULT       (0x0010)
#define   MANE_LINK_PARTNER_NEXT_PAGE_ABLE    (0x0008)
#define   MANE_NEXT_PAGE_ABLE                 (0x0004)
#define   MANE_PAGE_RECEIVED                  (0x0002)
#define   MANE_LINK_PARTNER_AUTO_ABLE         (0x0001)

#define MII_AUTO_NEGOTIATION_NEXT_PAGE_TRANSMIT (0x0007)
#define   MANNPT_NEXT_PAGE                    (0x8000)
#define   MANNPT_MESSAGE_PAGE                 (0x2000)
#define   MANNPT_ACKNOWLEDGE_2                (0x1000)
#define   MANNPT_TOGGLE                       (0x0800)
#define   MANNPT_MESSAGE_FIELD_MASK           (0x07FF)

#endif
