
#define  EFVI_FALCON_EXTENDED_P_BAR 1

//////////////---- Bus Interface Unit Registers C Header ----//////////////
#define IOM_IND_ADR_REG_OFST 0x0 // IO-mapped indirect access address register
  #define IOM_AUTO_ADR_INC_EN_LBN 16
  #define IOM_AUTO_ADR_INC_EN_WIDTH 1
  #define IOM_IND_ADR_LBN 0
  #define IOM_IND_ADR_WIDTH 16
#define IOM_IND_DAT_REG_OFST 0x4 // IO-mapped indirect access data register
  #define IOM_IND_DAT_LBN 0
  #define IOM_IND_DAT_WIDTH 32
#define ADR_REGION_REG_KER_OFST 0x0 // Address region register
#define ADR_REGION_REG_OFST 0x0 // Address region register
  #define ADR_REGION3_LBN 96
  #define ADR_REGION3_WIDTH 18
  #define ADR_REGION2_LBN 64
  #define ADR_REGION2_WIDTH 18
  #define ADR_REGION1_LBN 32
  #define ADR_REGION1_WIDTH 18
  #define ADR_REGION0_LBN 0
  #define ADR_REGION0_WIDTH 18
#define INT_EN_REG_KER_OFST 0x10 // Kernel driver Interrupt enable register
  #define KER_INT_CHAR_LBN 4
  #define KER_INT_CHAR_WIDTH 1
  #define KER_INT_KER_LBN 3
  #define KER_INT_KER_WIDTH 1
  #define ILL_ADR_ERR_INT_EN_KER_LBN 2
  #define ILL_ADR_ERR_INT_EN_KER_WIDTH 1
  #define SRM_PERR_INT_EN_KER_LBN 1
  #define SRM_PERR_INT_EN_KER_WIDTH 1
  #define DRV_INT_EN_KER_LBN 0
  #define DRV_INT_EN_KER_WIDTH 1
#define INT_EN_REG_CHAR_OFST 0x20 // Char Driver interrupt enable register
  #define CHAR_INT_CHAR_LBN 4
  #define CHAR_INT_CHAR_WIDTH 1
  #define CHAR_INT_KER_LBN 3
  #define CHAR_INT_KER_WIDTH 1
  #define ILL_ADR_ERR_INT_EN_CHAR_LBN 2
  #define ILL_ADR_ERR_INT_EN_CHAR_WIDTH 1
  #define SRM_PERR_INT_EN_CHAR_LBN 1
  #define SRM_PERR_INT_EN_CHAR_WIDTH 1
  #define DRV_INT_EN_CHAR_LBN 0
  #define DRV_INT_EN_CHAR_WIDTH 1
#define INT_ADR_REG_KER_OFST 0x30 // Interrupt host address for Kernel driver
  #define INT_ADR_KER_LBN 0
  #define INT_ADR_KER_WIDTH 64
  #define DRV_INT_KER_LBN 32
  #define DRV_INT_KER_WIDTH 1
  #define EV_FF_HALF_INT_KER_LBN 3
  #define EV_FF_HALF_INT_KER_WIDTH 1
  #define EV_FF_FULL_INT_KER_LBN 2
  #define EV_FF_FULL_INT_KER_WIDTH 1
  #define ILL_ADR_ERR_INT_KER_LBN 1
  #define ILL_ADR_ERR_INT_KER_WIDTH 1
  #define SRAM_PERR_INT_KER_LBN 0
  #define SRAM_PERR_INT_KER_WIDTH 1
#define INT_ADR_REG_CHAR_OFST 0x40 // Interrupt host address for Char driver
  #define INT_ADR_CHAR_LBN 0
  #define INT_ADR_CHAR_WIDTH 64
  #define DRV_INT_CHAR_LBN 32
  #define DRV_INT_CHAR_WIDTH 1
  #define EV_FF_HALF_INT_CHAR_LBN 3
  #define EV_FF_HALF_INT_CHAR_WIDTH 1
  #define EV_FF_FULL_INT_CHAR_LBN 2
  #define EV_FF_FULL_INT_CHAR_WIDTH 1
  #define ILL_ADR_ERR_INT_CHAR_LBN 1
  #define ILL_ADR_ERR_INT_CHAR_WIDTH 1
  #define SRAM_PERR_INT_CHAR_LBN 0
  #define SRAM_PERR_INT_CHAR_WIDTH 1
#define INT_ISR0_B0_OFST 0x90 // B0 only
#define INT_ISR1_B0_OFST 0xA0
#define INT_ACK_REG_KER_A1_OFST 0x50 // Kernel interrupt acknowledge register
  #define RESERVED_LBN 0
  #define RESERVED_WIDTH 32
#define INT_ACK_REG_CHAR_A1_OFST 0x60 // CHAR interrupt acknowledge register
  #define RESERVED_LBN 0
  #define RESERVED_WIDTH 32
//////////////---- Global CSR Registers C Header ----//////////////
#define STRAP_REG_KER_OFST 0x200 // ASIC strap status register
#define STRAP_REG_OFST 0x200 // ASIC strap status register
  #define ONCHIP_SRAM_LBN 16
  #define ONCHIP_SRAM_WIDTH 0
  #define STRAP_ISCSI_EN_LBN 3
  #define STRAP_ISCSI_EN_WIDTH 1
  #define STRAP_PINS_LBN 0
  #define STRAP_PINS_WIDTH 3
#define GPIO_CTL_REG_KER_OFST 0x210 // GPIO control register
#define GPIO_CTL_REG_OFST 0x210 // GPIO control register
  #define GPIO_OEN_LBN 24
  #define GPIO_OEN_WIDTH 4
  #define GPIO_OUT_LBN 16
  #define GPIO_OUT_WIDTH 4
  #define GPIO_IN_LBN 8
  #define GPIO_IN_WIDTH 4
  #define GPIO_PWRUP_VALUE_LBN 0
  #define GPIO_PWRUP_VALUE_WIDTH 4
#define GLB_CTL_REG_KER_OFST 0x220 // Global control register
#define GLB_CTL_REG_OFST 0x220 // Global control register
  #define SWRST_LBN 0
  #define SWRST_WIDTH 1
#define FATAL_INTR_REG_KER_OFST 0x230 // Fatal interrupt register for Kernel
  #define PCI_BUSERR_INT_KER_EN_LBN 43
  #define PCI_BUSERR_INT_KER_EN_WIDTH 1
  #define SRAM_OOB_INT_KER_EN_LBN 42
  #define SRAM_OOB_INT_KER_EN_WIDTH 1
  #define BUFID_OOB_INT_KER_EN_LBN 41
  #define BUFID_OOB_INT_KER_EN_WIDTH 1
  #define MEM_PERR_INT_KER_EN_LBN 40
  #define MEM_PERR_INT_KER_EN_WIDTH 1
  #define RBUF_OWN_INT_KER_EN_LBN 39
  #define RBUF_OWN_INT_KER_EN_WIDTH 1
  #define TBUF_OWN_INT_KER_EN_LBN 38
  #define TBUF_OWN_INT_KER_EN_WIDTH 1
  #define RDESCQ_OWN_INT_KER_EN_LBN 37
  #define RDESCQ_OWN_INT_KER_EN_WIDTH 1
  #define TDESCQ_OWN_INT_KER_EN_LBN 36
  #define TDESCQ_OWN_INT_KER_EN_WIDTH 1
  #define EVQ_OWN_INT_KER_EN_LBN 35
  #define EVQ_OWN_INT_KER_EN_WIDTH 1
  #define EVFF_OFLO_INT_KER_EN_LBN 34
  #define EVFF_OFLO_INT_KER_EN_WIDTH 1
  #define ILL_ADR_INT_KER_EN_LBN 33
  #define ILL_ADR_INT_KER_EN_WIDTH 1
  #define SRM_PERR_INT_KER_EN_LBN 32
  #define SRM_PERR_INT_KER_EN_WIDTH 1
  #define PCI_BUSERR_INT_KER_LBN 11
  #define PCI_BUSERR_INT_KER_WIDTH 1
  #define SRAM_OOB_INT_KER_LBN 10
  #define SRAM_OOB_INT_KER_WIDTH 1
  #define BUFID_OOB_INT_KER_LBN 9
  #define BUFID_OOB_INT_KER_WIDTH 1
  #define MEM_PERR_INT_KER_LBN 8
  #define MEM_PERR_INT_KER_WIDTH 1
  #define RBUF_OWN_INT_KER_LBN 7
  #define RBUF_OWN_INT_KER_WIDTH 1
  #define TBUF_OWN_INT_KER_LBN 6
  #define TBUF_OWN_INT_KER_WIDTH 1
  #define RDESCQ_OWN_INT_KER_LBN 5
  #define RDESCQ_OWN_INT_KER_WIDTH 1
  #define TDESCQ_OWN_INT_KER_LBN 4
  #define TDESCQ_OWN_INT_KER_WIDTH 1
  #define EVQ_OWN_INT_KER_LBN 3
  #define EVQ_OWN_INT_KER_WIDTH 1
  #define EVFF_OFLO_INT_KER_LBN 2
  #define EVFF_OFLO_INT_KER_WIDTH 1
  #define ILL_ADR_INT_KER_LBN 1
  #define ILL_ADR_INT_KER_WIDTH 1
  #define SRM_PERR_INT_KER_LBN 0
  #define SRM_PERR_INT_KER_WIDTH 1
#define FATAL_INTR_REG_OFST 0x240 // Fatal interrupt register for Char
  #define PCI_BUSERR_INT_CHAR_EN_LBN 43
  #define PCI_BUSERR_INT_CHAR_EN_WIDTH 1
  #define SRAM_OOB_INT_CHAR_EN_LBN 42
  #define SRAM_OOB_INT_CHAR_EN_WIDTH 1
  #define BUFID_OOB_INT_CHAR_EN_LBN 41
  #define BUFID_OOB_INT_CHAR_EN_WIDTH 1
  #define MEM_PERR_INT_CHAR_EN_LBN 40
  #define MEM_PERR_INT_CHAR_EN_WIDTH 1
  #define RBUF_OWN_INT_CHAR_EN_LBN 39
  #define RBUF_OWN_INT_CHAR_EN_WIDTH 1
  #define TBUF_OWN_INT_CHAR_EN_LBN 38
  #define TBUF_OWN_INT_CHAR_EN_WIDTH 1
  #define RDESCQ_OWN_INT_CHAR_EN_LBN 37
  #define RDESCQ_OWN_INT_CHAR_EN_WIDTH 1
  #define TDESCQ_OWN_INT_CHAR_EN_LBN 36
  #define TDESCQ_OWN_INT_CHAR_EN_WIDTH 1
  #define EVQ_OWN_INT_CHAR_EN_LBN 35
  #define EVQ_OWN_INT_CHAR_EN_WIDTH 1
  #define EVFF_OFLO_INT_CHAR_EN_LBN 34
  #define EVFF_OFLO_INT_CHAR_EN_WIDTH 1
  #define ILL_ADR_INT_CHAR_EN_LBN 33
  #define ILL_ADR_INT_CHAR_EN_WIDTH 1
  #define SRM_PERR_INT_CHAR_EN_LBN 32
  #define SRM_PERR_INT_CHAR_EN_WIDTH 1
  #define FATAL_INTR_REG_EN_BITS    0xffffffffffffffffULL
  #define PCI_BUSERR_INT_CHAR_LBN 11
  #define PCI_BUSERR_INT_CHAR_WIDTH 1
  #define SRAM_OOB_INT_CHAR_LBN 10
  #define SRAM_OOB_INT_CHAR_WIDTH 1
  #define BUFID_OOB_INT_CHAR_LBN 9
  #define BUFID_OOB_INT_CHAR_WIDTH 1
  #define MEM_PERR_INT_CHAR_LBN 8
  #define MEM_PERR_INT_CHAR_WIDTH 1
  #define RBUF_OWN_INT_CHAR_LBN 7
  #define RBUF_OWN_INT_CHAR_WIDTH 1
  #define TBUF_OWN_INT_CHAR_LBN 6
  #define TBUF_OWN_INT_CHAR_WIDTH 1
  #define RDESCQ_OWN_INT_CHAR_LBN 5
  #define RDESCQ_OWN_INT_CHAR_WIDTH 1
  #define TDESCQ_OWN_INT_CHAR_LBN 4
  #define TDESCQ_OWN_INT_CHAR_WIDTH 1
  #define EVQ_OWN_INT_CHAR_LBN 3
  #define EVQ_OWN_INT_CHAR_WIDTH 1
  #define EVFF_OFLO_INT_CHAR_LBN 2
  #define EVFF_OFLO_INT_CHAR_WIDTH 1
  #define ILL_ADR_INT_CHAR_LBN 1
  #define ILL_ADR_INT_CHAR_WIDTH 1
  #define SRM_PERR_INT_CHAR_LBN 0
  #define SRM_PERR_INT_CHAR_WIDTH 1
#define DP_CTRL_REG_OFST 0x250 // Datapath control register
  #define FLS_EVQ_ID_LBN 0
  #define FLS_EVQ_ID_WIDTH 12
#define MEM_STAT_REG_KER_OFST 0x260 // Memory status register
#define MEM_STAT_REG_OFST 0x260 // Memory status register
  #define MEM_PERR_VEC_LBN 53
  #define MEM_PERR_VEC_WIDTH 38
  #define MBIST_CORR_LBN 38
  #define MBIST_CORR_WIDTH 15
  #define MBIST_ERR_LBN 0
  #define MBIST_ERR_WIDTH 38
#define DEBUG_REG_KER_OFST 0x270 // Debug register
#define DEBUG_REG_OFST 0x270 // Debug register
  #define DEBUG_BLK_SEL2_LBN 47
  #define DEBUG_BLK_SEL2_WIDTH 3
  #define DEBUG_BLK_SEL1_LBN 44
  #define DEBUG_BLK_SEL1_WIDTH 3
  #define DEBUG_BLK_SEL0_LBN 41
  #define DEBUG_BLK_SEL0_WIDTH 3
  #define MISC_DEBUG_ADDR_LBN 36
  #define MISC_DEBUG_ADDR_WIDTH 5
  #define SERDES_DEBUG_ADDR_LBN 31
  #define SERDES_DEBUG_ADDR_WIDTH 5
  #define EM_DEBUG_ADDR_LBN 26
  #define EM_DEBUG_ADDR_WIDTH 5
  #define SR_DEBUG_ADDR_LBN 21
  #define SR_DEBUG_ADDR_WIDTH 5
  #define EV_DEBUG_ADDR_LBN 16
  #define EV_DEBUG_ADDR_WIDTH 5
  #define RX_DEBUG_ADDR_LBN 11
  #define RX_DEBUG_ADDR_WIDTH 5
  #define TX_DEBUG_ADDR_LBN 6
  #define TX_DEBUG_ADDR_WIDTH 5
  #define BIU_DEBUG_ADDR_LBN 1
  #define BIU_DEBUG_ADDR_WIDTH 5
  #define DEBUG_EN_LBN 0
  #define DEBUG_EN_WIDTH 1
#define DRIVER_REG0_KER_OFST 0x280 // Driver scratch register 0
#define DRIVER_REG0_OFST 0x280 // Driver scratch register 0
  #define DRIVER_DW0_LBN 0
  #define DRIVER_DW0_WIDTH 32
#define DRIVER_REG1_KER_OFST 0x290 // Driver scratch register 1
#define DRIVER_REG1_OFST 0x290 // Driver scratch register 1
  #define DRIVER_DW1_LBN 0
  #define DRIVER_DW1_WIDTH 32
#define DRIVER_REG2_KER_OFST 0x2A0 // Driver scratch register 2
#define DRIVER_REG2_OFST 0x2A0 // Driver scratch register 2
  #define DRIVER_DW2_LBN 0
  #define DRIVER_DW2_WIDTH 32
#define DRIVER_REG3_KER_OFST 0x2B0 // Driver scratch register 3
#define DRIVER_REG3_OFST 0x2B0 // Driver scratch register 3
  #define DRIVER_DW3_LBN 0
  #define DRIVER_DW3_WIDTH 32
#define DRIVER_REG4_KER_OFST 0x2C0 // Driver scratch register 4
#define DRIVER_REG4_OFST 0x2C0 // Driver scratch register 4
  #define DRIVER_DW4_LBN 0
  #define DRIVER_DW4_WIDTH 32
#define DRIVER_REG5_KER_OFST 0x2D0 // Driver scratch register 5
#define DRIVER_REG5_OFST 0x2D0 // Driver scratch register 5
  #define DRIVER_DW5_LBN 0
  #define DRIVER_DW5_WIDTH 32
#define DRIVER_REG6_KER_OFST 0x2E0 // Driver scratch register 6
#define DRIVER_REG6_OFST 0x2E0 // Driver scratch register 6
  #define DRIVER_DW6_LBN 0
  #define DRIVER_DW6_WIDTH 32
#define DRIVER_REG7_KER_OFST 0x2F0 // Driver scratch register 7
#define DRIVER_REG7_OFST 0x2F0 // Driver scratch register 7
  #define DRIVER_DW7_LBN 0
  #define DRIVER_DW7_WIDTH 32
#define ALTERA_BUILD_REG_OFST 0x300 // Altera build register
#define ALTERA_BUILD_REG_OFST 0x300 // Altera build register
  #define ALTERA_BUILD_VER_LBN 0
  #define ALTERA_BUILD_VER_WIDTH 32

/* so called CSR spare register 
    - contains separate parity enable bits for the various internal memory blocks */
#define MEM_PARITY_ERR_EN_REG_KER 0x310 
#define MEM_PARITY_ALL_BLOCKS_EN_LBN 64
#define MEM_PARITY_ALL_BLOCKS_EN_WIDTH 38
#define MEM_PARITY_TX_DATA_EN_LBN   72
#define MEM_PARITY_TX_DATA_EN_WIDTH 2

//////////////---- Event & Timer Module Registers C Header ----//////////////

#if EFVI_FALCON_EXTENDED_P_BAR
#define EVQ_RPTR_REG_KER_OFST 0x11B00 // Event queue read pointer register
#else
#define EVQ_RPTR_REG_KER_OFST 0x1B00 // Event queue read pointer register
#endif

#define EVQ_RPTR_REG_OFST 0xFA0000 // Event queue read pointer register array.
  #define EVQ_RPTR_LBN 0
  #define EVQ_RPTR_WIDTH 15

#if EFVI_FALCON_EXTENDED_P_BAR
#define EVQ_PTR_TBL_KER_OFST 0x11A00 // Event queue pointer table for kernel access
#else
#define EVQ_PTR_TBL_KER_OFST 0x1A00 // Event queue pointer table for kernel access
#endif

#define EVQ_PTR_TBL_CHAR_OFST 0xF60000 // Event queue pointer table for char direct access
  #define EVQ_WKUP_OR_INT_EN_LBN 39
  #define EVQ_WKUP_OR_INT_EN_WIDTH 1
  #define EVQ_NXT_WPTR_LBN 24
  #define EVQ_NXT_WPTR_WIDTH 15
  #define EVQ_EN_LBN 23
  #define EVQ_EN_WIDTH 1
  #define EVQ_SIZE_LBN 20
  #define EVQ_SIZE_WIDTH 3
  #define EVQ_BUF_BASE_ID_LBN 0
  #define EVQ_BUF_BASE_ID_WIDTH 20
#define TIMER_CMD_REG_KER_OFST 0x420 // Timer table for kernel access. Page-mapped
#define TIMER_CMD_REG_PAGE4_OFST 0x8420 // Timer table for user-level access. Page-mapped. For lowest 1K queues.
#define TIMER_CMD_REG_PAGE123K_OFST 0x1000420 // Timer table for user-level access. Page-mapped. For upper 3K queues.
#define TIMER_TBL_OFST 0xF70000 // Timer table for char driver direct access
  #define TIMER_MODE_LBN 12
  #define TIMER_MODE_WIDTH 2
  #define TIMER_VAL_LBN 0
  #define TIMER_VAL_WIDTH 12
  #define TIMER_MODE_INT_HLDOFF 2
  #define EVQ_BUF_SIZE_LBN 0
  #define EVQ_BUF_SIZE_WIDTH 1
#define DRV_EV_REG_KER_OFST 0x440 // Driver generated event register
#define DRV_EV_REG_OFST 0x440 // Driver generated event register
  #define DRV_EV_QID_LBN 64
  #define DRV_EV_QID_WIDTH 12
  #define DRV_EV_DATA_LBN 0
  #define DRV_EV_DATA_WIDTH 64
#define EVQ_CTL_REG_KER_OFST 0x450 // Event queue control register
#define EVQ_CTL_REG_OFST 0x450 // Event queue control register
  #define RX_EVQ_WAKEUP_MASK_B0_LBN 15
  #define RX_EVQ_WAKEUP_MASK_B0_WIDTH 6
  #define EVQ_OWNERR_CTL_LBN 14
  #define EVQ_OWNERR_CTL_WIDTH 1
  #define EVQ_FIFO_AF_TH_LBN 8
  #define EVQ_FIFO_AF_TH_WIDTH 6
  #define EVQ_FIFO_NOTAF_TH_LBN 0
  #define EVQ_FIFO_NOTAF_TH_WIDTH 6
//////////////---- SRAM Module Registers C Header ----//////////////
#define BUF_TBL_CFG_REG_KER_OFST 0x600 // Buffer table configuration register
#define BUF_TBL_CFG_REG_OFST 0x600 // Buffer table configuration register
  #define BUF_TBL_MODE_LBN 3
  #define BUF_TBL_MODE_WIDTH 1
#define SRM_RX_DC_CFG_REG_KER_OFST 0x610 // SRAM receive descriptor cache configuration register
#define SRM_RX_DC_CFG_REG_OFST 0x610 // SRAM receive descriptor cache configuration register
  #define SRM_RX_DC_BASE_ADR_LBN 0
  #define SRM_RX_DC_BASE_ADR_WIDTH 21
#define SRM_TX_DC_CFG_REG_KER_OFST 0x620 // SRAM transmit descriptor cache configuration register
#define SRM_TX_DC_CFG_REG_OFST 0x620 // SRAM transmit descriptor cache configuration register
  #define SRM_TX_DC_BASE_ADR_LBN 0
  #define SRM_TX_DC_BASE_ADR_WIDTH 21
#define SRM_CFG_REG_KER_OFST 0x630 // SRAM configuration register
#define SRM_CFG_REG_OFST 0x630 // SRAM configuration register
  #define SRAM_OOB_ADR_INTEN_LBN 5
  #define SRAM_OOB_ADR_INTEN_WIDTH 1
  #define SRAM_OOB_BUF_INTEN_LBN 4
  #define SRAM_OOB_BUF_INTEN_WIDTH 1
  #define SRAM_BT_INIT_EN_LBN 3
  #define SRAM_BT_INIT_EN_WIDTH 1
  #define SRM_NUM_BANK_LBN 2
  #define SRM_NUM_BANK_WIDTH 1
  #define SRM_BANK_SIZE_LBN 0
  #define SRM_BANK_SIZE_WIDTH 2
#define BUF_TBL_UPD_REG_KER_OFST 0x650 // Buffer table update register
#define BUF_TBL_UPD_REG_OFST 0x650 // Buffer table update register
  #define BUF_UPD_CMD_LBN 63
  #define BUF_UPD_CMD_WIDTH 1
  #define BUF_CLR_CMD_LBN 62
  #define BUF_CLR_CMD_WIDTH 1
  #define BUF_CLR_END_ID_LBN 32
  #define BUF_CLR_END_ID_WIDTH 20
  #define BUF_CLR_START_ID_LBN 0
  #define BUF_CLR_START_ID_WIDTH 20
#define SRM_UPD_EVQ_REG_KER_OFST 0x660 // Buffer table update register
#define SRM_UPD_EVQ_REG_OFST 0x660 // Buffer table update register
  #define SRM_UPD_EVQ_ID_LBN 0
  #define SRM_UPD_EVQ_ID_WIDTH 12
#define SRAM_PARITY_REG_KER_OFST 0x670 // SRAM parity register.
#define SRAM_PARITY_REG_OFST 0x670 // SRAM parity register.
  #define FORCE_SRAM_PERR_LBN 0
  #define FORCE_SRAM_PERR_WIDTH 1

#if EFVI_FALCON_EXTENDED_P_BAR
#define BUF_HALF_TBL_KER_OFST 0x18000 // Buffer table in half buffer table mode direct access by kernel driver
#else
#define BUF_HALF_TBL_KER_OFST 0x8000 // Buffer table in half buffer table mode direct access by kernel driver
#endif


#define BUF_HALF_TBL_OFST 0x800000 // Buffer table in half buffer table mode direct access by char driver
  #define BUF_ADR_HBUF_ODD_LBN 44
  #define BUF_ADR_HBUF_ODD_WIDTH 20
  #define BUF_OWNER_ID_HBUF_ODD_LBN 32
  #define BUF_OWNER_ID_HBUF_ODD_WIDTH 12
  #define BUF_ADR_HBUF_EVEN_LBN 12
  #define BUF_ADR_HBUF_EVEN_WIDTH 20
  #define BUF_OWNER_ID_HBUF_EVEN_LBN 0
  #define BUF_OWNER_ID_HBUF_EVEN_WIDTH 12


#if EFVI_FALCON_EXTENDED_P_BAR
#define BUF_FULL_TBL_KER_OFST 0x18000 // Buffer table in full buffer table mode direct access by kernel driver
#else
#define BUF_FULL_TBL_KER_OFST 0x8000 // Buffer table in full buffer table mode direct access by kernel driver
#endif




#define BUF_FULL_TBL_OFST 0x800000 // Buffer table in full buffer table mode direct access by char driver
  #define IP_DAT_BUF_SIZE_LBN 50
  #define IP_DAT_BUF_SIZE_WIDTH 1
  #define BUF_ADR_REGION_LBN 48
  #define BUF_ADR_REGION_WIDTH 2
  #define BUF_ADR_FBUF_LBN 14
  #define BUF_ADR_FBUF_WIDTH 34
  #define BUF_OWNER_ID_FBUF_LBN 0
  #define BUF_OWNER_ID_FBUF_WIDTH 14
#define SRM_DBG_REG_OFST 0x3000000 // SRAM debug access
  #define SRM_DBG_LBN 0
  #define SRM_DBG_WIDTH 64
//////////////---- RX Datapath Registers C Header ----//////////////

#define RX_CFG_REG_KER_OFST 0x800 // Receive configuration register
#define RX_CFG_REG_OFST 0x800 // Receive configuration register

#if !defined(FALCON_64K_RXFIFO) && !defined(FALCON_PRE_02020029)
# if !defined(FALCON_128K_RXFIFO)
#  define FALCON_128K_RXFIFO
# endif
#endif

#if defined(FALCON_128K_RXFIFO)

/* new for B0 */
  #define RX_TOEP_TCP_SUPPRESS_B0_LBN 48
  #define RX_TOEP_TCP_SUPPRESS_B0_WIDTH 1
  #define RX_INGR_EN_B0_LBN 47
  #define RX_INGR_EN_B0_WIDTH 1
  #define RX_TOEP_IPV4_B0_LBN 46
  #define RX_TOEP_IPV4_B0_WIDTH 1
  #define RX_HASH_ALG_B0_LBN 45
  #define RX_HASH_ALG_B0_WIDTH 1
  #define RX_HASH_INSERT_HDR_B0_LBN 44
  #define RX_HASH_INSERT_HDR_B0_WIDTH 1
/* moved for B0 */
  #define RX_DESC_PUSH_EN_B0_LBN 43
  #define RX_DESC_PUSH_EN_B0_WIDTH 1
  #define RX_RDW_PATCH_EN_LBN 42 /* Non head of line blocking */
  #define RX_RDW_PATCH_EN_WIDTH 1
  #define RX_PCI_BURST_SIZE_B0_LBN 39
  #define RX_PCI_BURST_SIZE_B0_WIDTH 3
  #define RX_OWNERR_CTL_B0_LBN 38
  #define RX_OWNERR_CTL_B0_WIDTH 1
  #define RX_XON_TX_TH_B0_LBN 33 
  #define RX_XON_TX_TH_B0_WIDTH 5
  #define RX_XOFF_TX_TH_B0_LBN 28 
  #define RX_XOFF_TX_TH_B0_WIDTH 5
  #define RX_USR_BUF_SIZE_B0_LBN 19
  #define RX_USR_BUF_SIZE_B0_WIDTH 9
  #define RX_XON_MAC_TH_B0_LBN 10
  #define RX_XON_MAC_TH_B0_WIDTH 9
  #define RX_XOFF_MAC_TH_B0_LBN 1
  #define RX_XOFF_MAC_TH_B0_WIDTH 9
  #define RX_XOFF_MAC_EN_B0_LBN 0
  #define RX_XOFF_MAC_EN_B0_WIDTH 1

#elif !defined(FALCON_PRE_02020029)
/* new for B0 */
  #define RX_TOEP_TCP_SUPPRESS_B0_LBN 46
  #define RX_TOEP_TCP_SUPPRESS_B0_WIDTH 1
  #define RX_INGR_EN_B0_LBN 45
  #define RX_INGR_EN_B0_WIDTH 1
  #define RX_TOEP_IPV4_B0_LBN 44
  #define RX_TOEP_IPV4_B0_WIDTH 1
  #define RX_HASH_ALG_B0_LBN 43
  #define RX_HASH_ALG_B0_WIDTH 41
  #define RX_HASH_INSERT_HDR_B0_LBN 42
  #define RX_HASH_INSERT_HDR_B0_WIDTH 1
/* moved for B0 */
  #define RX_DESC_PUSH_EN_B0_LBN 41
  #define RX_DESC_PUSH_EN_B0_WIDTH 1
  #define RX_PCI_BURST_SIZE_B0_LBN 37
  #define RX_PCI_BURST_SIZE_B0_WIDTH 3
  #define RX_OWNERR_CTL_B0_LBN 36
  #define RX_OWNERR_CTL_B0_WIDTH 1
  #define RX_XON_TX_TH_B0_LBN 31
  #define RX_XON_TX_TH_B0_WIDTH 5
  #define RX_XOFF_TX_TH_B0_LBN 26
  #define RX_XOFF_TX_TH_B0_WIDTH 5
  #define RX_USR_BUF_SIZE_B0_LBN 17
  #define RX_USR_BUF_SIZE_B0_WIDTH 9
  #define RX_XON_MAC_TH_B0_LBN 9
  #define RX_XON_MAC_TH_B0_WIDTH 8
  #define RX_XOFF_MAC_TH_B0_LBN 1
  #define RX_XOFF_MAC_TH_B0_WIDTH 8
  #define RX_XOFF_MAC_EN_B0_LBN 0
  #define RX_XOFF_MAC_EN_B0_WIDTH 1

#else
/* new for B0 */
  #define RX_TOEP_TCP_SUPPRESS_B0_LBN 44
  #define RX_TOEP_TCP_SUPPRESS_B0_WIDTH 1
  #define RX_INGR_EN_B0_LBN 43
  #define RX_INGR_EN_B0_WIDTH 1
  #define RX_TOEP_IPV4_B0_LBN 42
  #define RX_TOEP_IPV4_B0_WIDTH 1
  #define RX_HASH_ALG_B0_LBN 41
  #define RX_HASH_ALG_B0_WIDTH 41
  #define RX_HASH_INSERT_HDR_B0_LBN 40
  #define RX_HASH_INSERT_HDR_B0_WIDTH 1
/* moved for B0 */
  #define RX_DESC_PUSH_EN_B0_LBN 35
  #define RX_DESC_PUSH_EN_B0_WIDTH 1
  #define RX_PCI_BURST_SIZE_B0_LBN 35
  #define RX_PCI_BURST_SIZE_B0_WIDTH 2
  #define RX_OWNERR_CTL_B0_LBN 34
  #define RX_OWNERR_CTL_B0_WIDTH 1
  #define RX_XON_TX_TH_B0_LBN 29
  #define RX_XON_TX_TH_B0_WIDTH 5
  #define RX_XOFF_TX_TH_B0_LBN 24
  #define RX_XOFF_TX_TH_B0_WIDTH 5
  #define RX_USR_BUF_SIZE_B0_LBN 15
  #define RX_USR_BUF_SIZE_B0_WIDTH 9
  #define RX_XON_MAC_TH_B0_LBN 8
  #define RX_XON_MAC_TH_B0_WIDTH 7
  #define RX_XOFF_MAC_TH_B0_LBN 1
  #define RX_XOFF_MAC_TH_B0_WIDTH 7
  #define RX_XOFF_MAC_EN_B0_LBN 0
  #define RX_XOFF_MAC_EN_B0_WIDTH 1

#endif

/* A0/A1 */
  #define RX_PUSH_EN_A1_LBN 35
  #define RX_PUSH_EN_A1_WIDTH 1
  #define RX_PCI_BURST_SIZE_A1_LBN 31
  #define RX_PCI_BURST_SIZE_A1_WIDTH 3
  #define RX_OWNERR_CTL_A1_LBN 30
  #define RX_OWNERR_CTL_A1_WIDTH 1
  #define RX_XON_TX_TH_A1_LBN 25
  #define RX_XON_TX_TH_A1_WIDTH 5
  #define RX_XOFF_TX_TH_A1_LBN 20
  #define RX_XOFF_TX_TH_A1_WIDTH 5
  #define RX_USR_BUF_SIZE_A1_LBN 11
  #define RX_USR_BUF_SIZE_A1_WIDTH 9
  #define RX_XON_MAC_TH_A1_LBN 6
  #define RX_XON_MAC_TH_A1_WIDTH 5
  #define RX_XOFF_MAC_TH_A1_LBN 1
  #define RX_XOFF_MAC_TH_A1_WIDTH 5
  #define RX_XOFF_MAC_EN_A1_LBN 0
  #define RX_XOFF_MAC_EN_A1_WIDTH 1

#define RX_FILTER_CTL_REG_OFST 0x810 // Receive filter control registers
  #define SCATTER_ENBL_NO_MATCH_Q_B0_LBN 40
  #define SCATTER_ENBL_NO_MATCH_Q_B0_WIDTH 1
  #define UDP_FULL_SRCH_LIMIT_LBN 32
  #define UDP_FULL_SRCH_LIMIT_WIDTH 8
  #define NUM_KER_LBN 24
  #define NUM_KER_WIDTH 2
  #define UDP_WILD_SRCH_LIMIT_LBN 16
  #define UDP_WILD_SRCH_LIMIT_WIDTH 8
  #define TCP_WILD_SRCH_LIMIT_LBN 8
  #define TCP_WILD_SRCH_LIMIT_WIDTH 8
  #define TCP_FULL_SRCH_LIMIT_LBN 0
  #define TCP_FULL_SRCH_LIMIT_WIDTH 8
#define RX_FLUSH_DESCQ_REG_KER_OFST 0x820 // Receive flush descriptor queue register
#define RX_FLUSH_DESCQ_REG_OFST 0x820 // Receive flush descriptor queue register
  #define RX_FLUSH_DESCQ_CMD_LBN 24
  #define RX_FLUSH_DESCQ_CMD_WIDTH 1
  #define RX_FLUSH_EVQ_ID_LBN 12
  #define RX_FLUSH_EVQ_ID_WIDTH 12
  #define RX_FLUSH_DESCQ_LBN 0
  #define RX_FLUSH_DESCQ_WIDTH 12
#define RX_DESC_UPD_REG_KER_OFST 0x830 // Kernel  receive descriptor update register. Page-mapped
#define RX_DESC_UPD_REG_PAGE4_OFST 0x8830 // Char & user receive descriptor update register. Page-mapped. For lowest 1K queues.
#define RX_DESC_UPD_REG_PAGE123K_OFST 0x1000830 // Char & user receive descriptor update register. Page-mapped. For upper 3K queues.
  #define RX_DESC_WPTR_LBN 96
  #define RX_DESC_WPTR_WIDTH 12
  #define RX_DESC_PUSH_CMD_LBN 95
  #define RX_DESC_PUSH_CMD_WIDTH 1
  #define RX_DESC_LBN 0
  #define RX_DESC_WIDTH 64
  #define RX_KER_DESC_LBN 0
  #define RX_KER_DESC_WIDTH 64
  #define RX_USR_DESC_LBN 0
  #define RX_USR_DESC_WIDTH 32
#define RX_DC_CFG_REG_KER_OFST 0x840 // Receive descriptor cache configuration register
#define RX_DC_CFG_REG_OFST 0x840 // Receive descriptor cache configuration register
  #define RX_DC_SIZE_LBN 0
  #define RX_DC_SIZE_WIDTH 2
#define RX_DC_PF_WM_REG_KER_OFST 0x850 // Receive descriptor cache pre-fetch watermark register
#define RX_DC_PF_WM_REG_OFST 0x850 // Receive descriptor cache pre-fetch watermark register
  #define RX_DC_PF_LWM_LO_LBN 0
  #define RX_DC_PF_LWM_LO_WIDTH 6

#define RX_RSS_TKEY_B0_OFST 0x860 // RSS Toeplitz hash key (B0 only)

#define RX_NODESC_DROP_REG 0x880
  #define RX_NODESC_DROP_CNT_LBN 0
  #define RX_NODESC_DROP_CNT_WIDTH 16

#define XM_TX_CFG_REG_OFST 0x1230
  #define XM_AUTO_PAD_LBN 5
  #define XM_AUTO_PAD_WIDTH 1

#define RX_FILTER_TBL0_OFST 0xF00000 // Receive filter table - even entries
  #define RSS_EN_0_B0_LBN 110
  #define RSS_EN_0_B0_WIDTH 1
  #define SCATTER_EN_0_B0_LBN 109
  #define SCATTER_EN_0_B0_WIDTH 1
  #define TCP_UDP_0_LBN 108
  #define TCP_UDP_0_WIDTH 1
  #define RXQ_ID_0_LBN 96
  #define RXQ_ID_0_WIDTH 12
  #define DEST_IP_0_LBN 64
  #define DEST_IP_0_WIDTH 32
  #define DEST_PORT_TCP_0_LBN 48
  #define DEST_PORT_TCP_0_WIDTH 16
  #define SRC_IP_0_LBN 16
  #define SRC_IP_0_WIDTH 32
  #define SRC_TCP_DEST_UDP_0_LBN 0
  #define SRC_TCP_DEST_UDP_0_WIDTH 16
#define RX_FILTER_TBL1_OFST 0xF00010 // Receive filter table - odd entries
  #define RSS_EN_1_B0_LBN 110
  #define RSS_EN_1_B0_WIDTH 1
  #define SCATTER_EN_1_B0_LBN 109
  #define SCATTER_EN_1_B0_WIDTH 1
  #define TCP_UDP_1_LBN 108
  #define TCP_UDP_1_WIDTH 1
  #define RXQ_ID_1_LBN 96
  #define RXQ_ID_1_WIDTH 12
  #define DEST_IP_1_LBN 64
  #define DEST_IP_1_WIDTH 32
  #define DEST_PORT_TCP_1_LBN 48
  #define DEST_PORT_TCP_1_WIDTH 16
  #define SRC_IP_1_LBN 16
  #define SRC_IP_1_WIDTH 32
  #define SRC_TCP_DEST_UDP_1_LBN 0
  #define SRC_TCP_DEST_UDP_1_WIDTH 16

#if EFVI_FALCON_EXTENDED_P_BAR
#define RX_DESC_PTR_TBL_KER_OFST 0x11800 // Receive descriptor pointer kernel access
#else
#define RX_DESC_PTR_TBL_KER_OFST 0x1800 // Receive descriptor pointer kernel access
#endif


#define RX_DESC_PTR_TBL_OFST 0xF40000 // Receive descriptor pointer table
  #define RX_ISCSI_DDIG_EN_LBN 88
  #define RX_ISCSI_DDIG_EN_WIDTH 1
  #define RX_ISCSI_HDIG_EN_LBN 87
  #define RX_ISCSI_HDIG_EN_WIDTH 1
  #define RX_DESC_PREF_ACT_LBN 86
  #define RX_DESC_PREF_ACT_WIDTH 1
  #define RX_DC_HW_RPTR_LBN 80
  #define RX_DC_HW_RPTR_WIDTH 6
  #define RX_DESCQ_HW_RPTR_LBN 68
  #define RX_DESCQ_HW_RPTR_WIDTH 12
  #define RX_DESCQ_SW_WPTR_LBN 56
  #define RX_DESCQ_SW_WPTR_WIDTH 12
  #define RX_DESCQ_BUF_BASE_ID_LBN 36
  #define RX_DESCQ_BUF_BASE_ID_WIDTH 20
  #define RX_DESCQ_EVQ_ID_LBN 24
  #define RX_DESCQ_EVQ_ID_WIDTH 12
  #define RX_DESCQ_OWNER_ID_LBN 10
  #define RX_DESCQ_OWNER_ID_WIDTH 14
  #define RX_DESCQ_LABEL_LBN 5
  #define RX_DESCQ_LABEL_WIDTH 5
  #define RX_DESCQ_SIZE_LBN 3
  #define RX_DESCQ_SIZE_WIDTH 2
  #define RX_DESCQ_TYPE_LBN 2
  #define RX_DESCQ_TYPE_WIDTH 1
  #define RX_DESCQ_JUMBO_LBN 1
  #define RX_DESCQ_JUMBO_WIDTH 1
  #define RX_DESCQ_EN_LBN 0
  #define RX_DESCQ_EN_WIDTH 1


#define RX_RSS_INDIR_TBL_B0_OFST 0xFB0000 // RSS indirection table (B0 only)
  #define RX_RSS_INDIR_ENT_B0_LBN 0
  #define RX_RSS_INDIR_ENT_B0_WIDTH 6

//////////////---- TX Datapath Registers C Header ----//////////////
#define TX_FLUSH_DESCQ_REG_KER_OFST 0xA00 // Transmit flush descriptor queue register
#define TX_FLUSH_DESCQ_REG_OFST 0xA00 // Transmit flush descriptor queue register
  #define TX_FLUSH_DESCQ_CMD_LBN 12
  #define TX_FLUSH_DESCQ_CMD_WIDTH 1
  #define TX_FLUSH_DESCQ_LBN 0
  #define TX_FLUSH_DESCQ_WIDTH 12
#define TX_DESC_UPD_REG_KER_OFST 0xA10 // Kernel transmit descriptor update register. Page-mapped
#define TX_DESC_UPD_REG_PAGE4_OFST 0x8A10 // Char & user transmit descriptor update register. Page-mapped
#define TX_DESC_UPD_REG_PAGE123K_OFST 0x1000A10 // Char & user transmit descriptor update register. Page-mapped
  #define TX_DESC_WPTR_LBN 96
  #define TX_DESC_WPTR_WIDTH 12
  #define TX_DESC_PUSH_CMD_LBN 95
  #define TX_DESC_PUSH_CMD_WIDTH 1
  #define TX_DESC_LBN 0
  #define TX_DESC_WIDTH 95
  #define TX_KER_DESC_LBN 0
  #define TX_KER_DESC_WIDTH 64
  #define TX_USR_DESC_LBN 0
  #define TX_USR_DESC_WIDTH 64
#define TX_DC_CFG_REG_KER_OFST 0xA20 // Transmit descriptor cache configuration register
#define TX_DC_CFG_REG_OFST 0xA20 // Transmit descriptor cache configuration register
  #define TX_DC_SIZE_LBN 0
  #define TX_DC_SIZE_WIDTH 2

#if EFVI_FALCON_EXTENDED_P_BAR
#define TX_DESC_PTR_TBL_KER_OFST 0x11900 // Transmit descriptor pointer.
#else
#define TX_DESC_PTR_TBL_KER_OFST 0x1900 // Transmit descriptor pointer.
#endif


#define TX_DESC_PTR_TBL_OFST 0xF50000 // Transmit descriptor pointer
  #define TX_NON_IP_DROP_DIS_B0_LBN 91
  #define TX_NON_IP_DROP_DIS_B0_WIDTH 1
  #define TX_IP_CHKSM_DIS_B0_LBN 90
  #define TX_IP_CHKSM_DIS_B0_WIDTH 1
  #define TX_TCP_CHKSM_DIS_B0_LBN 89
  #define TX_TCP_CHKSM_DIS_B0_WIDTH 1
  #define TX_DESCQ_EN_LBN 88
  #define TX_DESCQ_EN_WIDTH 1
  #define TX_ISCSI_DDIG_EN_LBN 87
  #define TX_ISCSI_DDIG_EN_WIDTH 1
  #define TX_ISCSI_HDIG_EN_LBN 86
  #define TX_ISCSI_HDIG_EN_WIDTH 1
  #define TX_DC_HW_RPTR_LBN 80
  #define TX_DC_HW_RPTR_WIDTH 6
  #define TX_DESCQ_HW_RPTR_LBN 68
  #define TX_DESCQ_HW_RPTR_WIDTH 12
  #define TX_DESCQ_SW_WPTR_LBN 56
  #define TX_DESCQ_SW_WPTR_WIDTH 12
  #define TX_DESCQ_BUF_BASE_ID_LBN 36
  #define TX_DESCQ_BUF_BASE_ID_WIDTH 20
  #define TX_DESCQ_EVQ_ID_LBN 24
  #define TX_DESCQ_EVQ_ID_WIDTH 12
  #define TX_DESCQ_OWNER_ID_LBN 10
  #define TX_DESCQ_OWNER_ID_WIDTH 14
  #define TX_DESCQ_LABEL_LBN 5
  #define TX_DESCQ_LABEL_WIDTH 5
  #define TX_DESCQ_SIZE_LBN 3
  #define TX_DESCQ_SIZE_WIDTH 2
  #define TX_DESCQ_TYPE_LBN 1
  #define TX_DESCQ_TYPE_WIDTH 2
  #define TX_DESCQ_FLUSH_LBN 0
  #define TX_DESCQ_FLUSH_WIDTH 1
#define TX_CFG_REG_KER_OFST 0xA50 // Transmit configuration register
#define TX_CFG_REG_OFST 0xA50 // Transmit configuration register
  #define TX_IP_ID_P1_OFS_LBN 32
  #define TX_IP_ID_P1_OFS_WIDTH 15
  #define TX_IP_ID_P0_OFS_LBN 16
  #define TX_IP_ID_P0_OFS_WIDTH 15
  #define TX_TURBO_EN_LBN 3
  #define TX_TURBO_EN_WIDTH 1 
  #define TX_OWNERR_CTL_LBN 2
  #define TX_OWNERR_CTL_WIDTH 2
  #define TX_NON_IP_DROP_DIS_LBN 1
  #define TX_NON_IP_DROP_DIS_WIDTH 1
  #define TX_IP_ID_REP_EN_LBN 0
  #define TX_IP_ID_REP_EN_WIDTH 1
#define TX_RESERVED_REG_KER_OFST 0xA80 // Transmit configuration register
#define TX_RESERVED_REG_OFST 0xA80 // Transmit configuration register
  #define TX_CSR_PUSH_EN_LBN 89
  #define TX_CSR_PUSH_EN_WIDTH 1
  #define TX_RX_SPACER_LBN 64
  #define TX_RX_SPACER_WIDTH 8
  #define TX_SW_EV_EN_LBN 59
  #define TX_SW_EV_EN_WIDTH 1
  #define TX_RX_SPACER_EN_LBN 57
  #define TX_RX_SPACER_EN_WIDTH 1
  #define TX_CSR_PREF_WD_TMR_LBN 24
  #define TX_CSR_PREF_WD_TMR_WIDTH 16
  #define TX_CSR_ONLY1TAG_LBN 21
  #define TX_CSR_ONLY1TAG_WIDTH 1
  #define TX_PREF_THRESHOLD_LBN 19
  #define TX_PREF_THRESHOLD_WIDTH 2
  #define TX_ONE_PKT_PER_Q_LBN 18
  #define TX_ONE_PKT_PER_Q_WIDTH 1
  #define TX_DIS_NON_IP_EV_LBN 17
  #define TX_DIS_NON_IP_EV_WIDTH 1
  #define TX_DMA_SPACER_LBN 8
  #define TX_DMA_SPACER_WIDTH 8
  #define TX_FLUSH_MIN_LEN_EN_B0_LBN 7
  #define TX_FLUSH_MIN_LEN_EN_B0_WIDTH 1
  #define TX_TCP_DIS_A1_LBN 7
  #define TX_TCP_DIS_A1_WIDTH 1
  #define TX_IP_DIS_A1_LBN 6
  #define TX_IP_DIS_A1_WIDTH 1
  #define TX_MAX_CPL_LBN 2
  #define TX_MAX_CPL_WIDTH 2
  #define TX_MAX_PREF_LBN 0
  #define TX_MAX_PREF_WIDTH 2
#define TX_VLAN_REG_OFST 0xAE0 // Transmit VLAN tag register
  #define TX_VLAN_EN_LBN 127
  #define TX_VLAN_EN_WIDTH 1
  #define TX_VLAN7_PORT1_EN_LBN 125
  #define TX_VLAN7_PORT1_EN_WIDTH 1
  #define TX_VLAN7_PORT0_EN_LBN 124
  #define TX_VLAN7_PORT0_EN_WIDTH 1
  #define TX_VLAN7_LBN 112
  #define TX_VLAN7_WIDTH 12
  #define TX_VLAN6_PORT1_EN_LBN 109
  #define TX_VLAN6_PORT1_EN_WIDTH 1
  #define TX_VLAN6_PORT0_EN_LBN 108
  #define TX_VLAN6_PORT0_EN_WIDTH 1
  #define TX_VLAN6_LBN 96
  #define TX_VLAN6_WIDTH 12
  #define TX_VLAN5_PORT1_EN_LBN 93
  #define TX_VLAN5_PORT1_EN_WIDTH 1
  #define TX_VLAN5_PORT0_EN_LBN 92
  #define TX_VLAN5_PORT0_EN_WIDTH 1
  #define TX_VLAN5_LBN 80
  #define TX_VLAN5_WIDTH 12
  #define TX_VLAN4_PORT1_EN_LBN 77
  #define TX_VLAN4_PORT1_EN_WIDTH 1
  #define TX_VLAN4_PORT0_EN_LBN 76
  #define TX_VLAN4_PORT0_EN_WIDTH 1
  #define TX_VLAN4_LBN 64
  #define TX_VLAN4_WIDTH 12
  #define TX_VLAN3_PORT1_EN_LBN 61
  #define TX_VLAN3_PORT1_EN_WIDTH 1
  #define TX_VLAN3_PORT0_EN_LBN 60
  #define TX_VLAN3_PORT0_EN_WIDTH 1
  #define TX_VLAN3_LBN 48
  #define TX_VLAN3_WIDTH 12
  #define TX_VLAN2_PORT1_EN_LBN 45
  #define TX_VLAN2_PORT1_EN_WIDTH 1
  #define TX_VLAN2_PORT0_EN_LBN 44
  #define TX_VLAN2_PORT0_EN_WIDTH 1
  #define TX_VLAN2_LBN 32
  #define TX_VLAN2_WIDTH 12
  #define TX_VLAN1_PORT1_EN_LBN 29
  #define TX_VLAN1_PORT1_EN_WIDTH 1
  #define TX_VLAN1_PORT0_EN_LBN 28
  #define TX_VLAN1_PORT0_EN_WIDTH 1
  #define TX_VLAN1_LBN 16
  #define TX_VLAN1_WIDTH 12
  #define TX_VLAN0_PORT1_EN_LBN 13
  #define TX_VLAN0_PORT1_EN_WIDTH 1
  #define TX_VLAN0_PORT0_EN_LBN 12
  #define TX_VLAN0_PORT0_EN_WIDTH 1
  #define TX_VLAN0_LBN 0
  #define TX_VLAN0_WIDTH 12
#define TX_FIL_CTL_REG_OFST 0xAF0 // Transmit filter control register
  #define TX_MADR1_FIL_EN_LBN 65
  #define TX_MADR1_FIL_EN_WIDTH 1
  #define TX_MADR0_FIL_EN_LBN 64
  #define TX_MADR0_FIL_EN_WIDTH 1
  #define TX_IPFIL31_PORT1_EN_LBN 63
  #define TX_IPFIL31_PORT1_EN_WIDTH 1
  #define TX_IPFIL31_PORT0_EN_LBN 62
  #define TX_IPFIL31_PORT0_EN_WIDTH 1
  #define TX_IPFIL30_PORT1_EN_LBN 61
  #define TX_IPFIL30_PORT1_EN_WIDTH 1
  #define TX_IPFIL30_PORT0_EN_LBN 60
  #define TX_IPFIL30_PORT0_EN_WIDTH 1
  #define TX_IPFIL29_PORT1_EN_LBN 59
  #define TX_IPFIL29_PORT1_EN_WIDTH 1
  #define TX_IPFIL29_PORT0_EN_LBN 58
  #define TX_IPFIL29_PORT0_EN_WIDTH 1
  #define TX_IPFIL28_PORT1_EN_LBN 57
  #define TX_IPFIL28_PORT1_EN_WIDTH 1
  #define TX_IPFIL28_PORT0_EN_LBN 56
  #define TX_IPFIL28_PORT0_EN_WIDTH 1
  #define TX_IPFIL27_PORT1_EN_LBN 55
  #define TX_IPFIL27_PORT1_EN_WIDTH 1
  #define TX_IPFIL27_PORT0_EN_LBN 54
  #define TX_IPFIL27_PORT0_EN_WIDTH 1
  #define TX_IPFIL26_PORT1_EN_LBN 53
  #define TX_IPFIL26_PORT1_EN_WIDTH 1
  #define TX_IPFIL26_PORT0_EN_LBN 52
  #define TX_IPFIL26_PORT0_EN_WIDTH 1
  #define TX_IPFIL25_PORT1_EN_LBN 51
  #define TX_IPFIL25_PORT1_EN_WIDTH 1
  #define TX_IPFIL25_PORT0_EN_LBN 50
  #define TX_IPFIL25_PORT0_EN_WIDTH 1
  #define TX_IPFIL24_PORT1_EN_LBN 49
  #define TX_IPFIL24_PORT1_EN_WIDTH 1
  #define TX_IPFIL24_PORT0_EN_LBN 48
  #define TX_IPFIL24_PORT0_EN_WIDTH 1
  #define TX_IPFIL23_PORT1_EN_LBN 47
  #define TX_IPFIL23_PORT1_EN_WIDTH 1
  #define TX_IPFIL23_PORT0_EN_LBN 46
  #define TX_IPFIL23_PORT0_EN_WIDTH 1
  #define TX_IPFIL22_PORT1_EN_LBN 45
  #define TX_IPFIL22_PORT1_EN_WIDTH 1
  #define TX_IPFIL22_PORT0_EN_LBN 44
  #define TX_IPFIL22_PORT0_EN_WIDTH 1
  #define TX_IPFIL21_PORT1_EN_LBN 43
  #define TX_IPFIL21_PORT1_EN_WIDTH 1
  #define TX_IPFIL21_PORT0_EN_LBN 42
  #define TX_IPFIL21_PORT0_EN_WIDTH 1
  #define TX_IPFIL20_PORT1_EN_LBN 41
  #define TX_IPFIL20_PORT1_EN_WIDTH 1
  #define TX_IPFIL20_PORT0_EN_LBN 40
  #define TX_IPFIL20_PORT0_EN_WIDTH 1
  #define TX_IPFIL19_PORT1_EN_LBN 39
  #define TX_IPFIL19_PORT1_EN_WIDTH 1
  #define TX_IPFIL19_PORT0_EN_LBN 38
  #define TX_IPFIL19_PORT0_EN_WIDTH 1
  #define TX_IPFIL18_PORT1_EN_LBN 37
  #define TX_IPFIL18_PORT1_EN_WIDTH 1
  #define TX_IPFIL18_PORT0_EN_LBN 36
  #define TX_IPFIL18_PORT0_EN_WIDTH 1
  #define TX_IPFIL17_PORT1_EN_LBN 35
  #define TX_IPFIL17_PORT1_EN_WIDTH 1
  #define TX_IPFIL17_PORT0_EN_LBN 34
  #define TX_IPFIL17_PORT0_EN_WIDTH 1
  #define TX_IPFIL16_PORT1_EN_LBN 33
  #define TX_IPFIL16_PORT1_EN_WIDTH 1
  #define TX_IPFIL16_PORT0_EN_LBN 32
  #define TX_IPFIL16_PORT0_EN_WIDTH 1
  #define TX_IPFIL15_PORT1_EN_LBN 31
  #define TX_IPFIL15_PORT1_EN_WIDTH 1
  #define TX_IPFIL15_PORT0_EN_LBN 30
  #define TX_IPFIL15_PORT0_EN_WIDTH 1
  #define TX_IPFIL14_PORT1_EN_LBN 29
  #define TX_IPFIL14_PORT1_EN_WIDTH 1
  #define TX_IPFIL14_PORT0_EN_LBN 28
  #define TX_IPFIL14_PORT0_EN_WIDTH 1
  #define TX_IPFIL13_PORT1_EN_LBN 27
  #define TX_IPFIL13_PORT1_EN_WIDTH 1
  #define TX_IPFIL13_PORT0_EN_LBN 26
  #define TX_IPFIL13_PORT0_EN_WIDTH 1
  #define TX_IPFIL12_PORT1_EN_LBN 25
  #define TX_IPFIL12_PORT1_EN_WIDTH 1
  #define TX_IPFIL12_PORT0_EN_LBN 24
  #define TX_IPFIL12_PORT0_EN_WIDTH 1
  #define TX_IPFIL11_PORT1_EN_LBN 23
  #define TX_IPFIL11_PORT1_EN_WIDTH 1
  #define TX_IPFIL11_PORT0_EN_LBN 22
  #define TX_IPFIL11_PORT0_EN_WIDTH 1
  #define TX_IPFIL10_PORT1_EN_LBN 21
  #define TX_IPFIL10_PORT1_EN_WIDTH 1
  #define TX_IPFIL10_PORT0_EN_LBN 20
  #define TX_IPFIL10_PORT0_EN_WIDTH 1
  #define TX_IPFIL9_PORT1_EN_LBN 19
  #define TX_IPFIL9_PORT1_EN_WIDTH 1
  #define TX_IPFIL9_PORT0_EN_LBN 18
  #define TX_IPFIL9_PORT0_EN_WIDTH 1
  #define TX_IPFIL8_PORT1_EN_LBN 17
  #define TX_IPFIL8_PORT1_EN_WIDTH 1
  #define TX_IPFIL8_PORT0_EN_LBN 16
  #define TX_IPFIL8_PORT0_EN_WIDTH 1
  #define TX_IPFIL7_PORT1_EN_LBN 15
  #define TX_IPFIL7_PORT1_EN_WIDTH 1
  #define TX_IPFIL7_PORT0_EN_LBN 14
  #define TX_IPFIL7_PORT0_EN_WIDTH 1
  #define TX_IPFIL6_PORT1_EN_LBN 13
  #define TX_IPFIL6_PORT1_EN_WIDTH 1
  #define TX_IPFIL6_PORT0_EN_LBN 12
  #define TX_IPFIL6_PORT0_EN_WIDTH 1
  #define TX_IPFIL5_PORT1_EN_LBN 11
  #define TX_IPFIL5_PORT1_EN_WIDTH 1
  #define TX_IPFIL5_PORT0_EN_LBN 10
  #define TX_IPFIL5_PORT0_EN_WIDTH 1
  #define TX_IPFIL4_PORT1_EN_LBN 9
  #define TX_IPFIL4_PORT1_EN_WIDTH 1
  #define TX_IPFIL4_PORT0_EN_LBN 8
  #define TX_IPFIL4_PORT0_EN_WIDTH 1
  #define TX_IPFIL3_PORT1_EN_LBN 7
  #define TX_IPFIL3_PORT1_EN_WIDTH 1
  #define TX_IPFIL3_PORT0_EN_LBN 6
  #define TX_IPFIL3_PORT0_EN_WIDTH 1
  #define TX_IPFIL2_PORT1_EN_LBN 5
  #define TX_IPFIL2_PORT1_EN_WIDTH 1
  #define TX_IPFIL2_PORT0_EN_LBN 4
  #define TX_IPFIL2_PORT0_EN_WIDTH 1
  #define TX_IPFIL1_PORT1_EN_LBN 3
  #define TX_IPFIL1_PORT1_EN_WIDTH 1
  #define TX_IPFIL1_PORT0_EN_LBN 2
  #define TX_IPFIL1_PORT0_EN_WIDTH 1
  #define TX_IPFIL0_PORT1_EN_LBN 1
  #define TX_IPFIL0_PORT1_EN_WIDTH 1
  #define TX_IPFIL0_PORT0_EN_LBN 0
  #define TX_IPFIL0_PORT0_EN_WIDTH 1
#define TX_IPFIL_TBL_OFST 0xB00 // Transmit IP source address filter table
  #define TX_IPFIL_MASK_LBN 32
  #define TX_IPFIL_MASK_WIDTH 32
  #define TX_IP_SRC_ADR_LBN 0
  #define TX_IP_SRC_ADR_WIDTH 32
#define TX_PACE_REG_A1_OFST 0xF80000 // Transmit pace control register
#define TX_PACE_REG_B0_OFST 0xA90    // Transmit pace control register
  #define TX_PACE_SB_AF_LBN 19
  #define TX_PACE_SB_AF_WIDTH 10
  #define TX_PACE_SB_NOTAF_LBN 9
  #define TX_PACE_SB_NOTAF_WIDTH 10
  #define TX_PACE_FB_BASE_LBN 5
  #define TX_PACE_FB_BASE_WIDTH 4
  #define TX_PACE_BIN_TH_LBN 0
  #define TX_PACE_BIN_TH_WIDTH 5
#define TX_PACE_TBL_A1_OFST 0xF80040 // Transmit pacing table
#define TX_PACE_TBL_FIRST_QUEUE_A1 4
#define TX_PACE_TBL_B0_OFST 0xF80000 // Transmit pacing table
#define TX_PACE_TBL_FIRST_QUEUE_B0 0
  #define TX_PACE_LBN 0
  #define TX_PACE_WIDTH 5

//////////////---- EE/Flash Registers C Header ----//////////////
#define EE_SPI_HCMD_REG_KER_OFST 0x100 // SPI host command register
#define EE_SPI_HCMD_REG_OFST 0x100 // SPI host command register
  #define EE_SPI_HCMD_CMD_EN_LBN 31
  #define EE_SPI_HCMD_CMD_EN_WIDTH 1
  #define EE_WR_TIMER_ACTIVE_LBN 28
  #define EE_WR_TIMER_ACTIVE_WIDTH 1
  #define EE_SPI_HCMD_SF_SEL_LBN 24
  #define EE_SPI_HCMD_SF_SEL_WIDTH 1
  #define EE_SPI_HCMD_DABCNT_LBN 16
  #define EE_SPI_HCMD_DABCNT_WIDTH 5
  #define EE_SPI_HCMD_READ_LBN 15
  #define EE_SPI_HCMD_READ_WIDTH 1
  #define EE_SPI_HCMD_DUBCNT_LBN 12
  #define EE_SPI_HCMD_DUBCNT_WIDTH 2
  #define EE_SPI_HCMD_ADBCNT_LBN 8
  #define EE_SPI_HCMD_ADBCNT_WIDTH 2
  #define EE_SPI_HCMD_ENC_LBN 0
  #define EE_SPI_HCMD_ENC_WIDTH 8
#define EE_SPI_HADR_REG_KER_OFST 0X110 // SPI host address register
#define EE_SPI_HADR_REG_OFST 0X110 // SPI host address register
  #define EE_SPI_HADR_DUBYTE_LBN 24
  #define EE_SPI_HADR_DUBYTE_WIDTH 8
  #define EE_SPI_HADR_ADR_LBN 0
  #define EE_SPI_HADR_ADR_WIDTH 24
#define EE_SPI_HDATA_REG_KER_OFST 0x120 // SPI host data register
#define EE_SPI_HDATA_REG_OFST 0x120 // SPI host data register
  #define EE_SPI_HDATA3_LBN 96
  #define EE_SPI_HDATA3_WIDTH 32
  #define EE_SPI_HDATA2_LBN 64
  #define EE_SPI_HDATA2_WIDTH 32
  #define EE_SPI_HDATA1_LBN 32
  #define EE_SPI_HDATA1_WIDTH 32
  #define EE_SPI_HDATA0_LBN 0
  #define EE_SPI_HDATA0_WIDTH 32
#define EE_BASE_PAGE_REG_KER_OFST 0x130 // Expansion ROM base mirror register
#define EE_BASE_PAGE_REG_OFST 0x130 // Expansion ROM base mirror register
  #define EE_EXP_ROM_WINDOW_BASE_LBN 16
  #define EE_EXP_ROM_WINDOW_BASE_WIDTH 13
  #define EE_EXPROM_MASK_LBN 0
  #define EE_EXPROM_MASK_WIDTH 13
#define EE_VPD_CFG0_REG_KER_OFST 0X140 // SPI/VPD configuration register
#define EE_VPD_CFG0_REG_OFST 0X140 // SPI/VPD configuration register
  #define EE_SF_FASTRD_EN_LBN 127
  #define EE_SF_FASTRD_EN_WIDTH 1
  #define EE_SF_CLOCK_DIV_LBN 120
  #define EE_SF_CLOCK_DIV_WIDTH 7
  #define EE_VPD_WIP_POLL_LBN 119
  #define EE_VPD_WIP_POLL_WIDTH 1
  #define EE_VPDW_LENGTH_LBN 80
  #define EE_VPDW_LENGTH_WIDTH 15
  #define EE_VPDW_BASE_LBN 64
  #define EE_VPDW_BASE_WIDTH 15
  #define EE_VPD_WR_CMD_EN_LBN 56
  #define EE_VPD_WR_CMD_EN_WIDTH 8
  #define EE_VPD_BASE_LBN 32
  #define EE_VPD_BASE_WIDTH 24
  #define EE_VPD_LENGTH_LBN 16
  #define EE_VPD_LENGTH_WIDTH 13
  #define EE_VPD_AD_SIZE_LBN 8
  #define EE_VPD_AD_SIZE_WIDTH 5
  #define EE_VPD_ACCESS_ON_LBN 5
  #define EE_VPD_ACCESS_ON_WIDTH 1
#define EE_VPD_SW_CNTL_REG_KER_OFST 0X150 // VPD access SW control register
#define EE_VPD_SW_CNTL_REG_OFST 0X150 // VPD access SW control register
  #define EE_VPD_CYCLE_PENDING_LBN 31
  #define EE_VPD_CYCLE_PENDING_WIDTH 1
  #define EE_VPD_CYC_WRITE_LBN 28
  #define EE_VPD_CYC_WRITE_WIDTH 1
  #define EE_VPD_CYC_ADR_LBN 0
  #define EE_VPD_CYC_ADR_WIDTH 15
#define EE_VPD_SW_DATA_REG_KER_OFST 0x160 // VPD access SW data register
#define EE_VPD_SW_DATA_REG_OFST 0x160 // VPD access SW data register
  #define EE_VPD_CYC_DAT_LBN 0
  #define EE_VPD_CYC_DAT_WIDTH 32
