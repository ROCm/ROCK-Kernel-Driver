#ifndef __INCLUDED_SAA7146__
#define __INCLUDED_SAA7146__

struct	saa7146_video_dma {
	u32 base_odd;
	u32 base_even;
	u32 prot_addr;
	u32 pitch;
	u32 base_page;
	u32 num_line_byte;
};

struct saa7146_debi_transfer {

	u8	timeout;	/* have a look at the specs for reasonable values, p.110 ff */
	u8	swap;
	u8	slave16;
	u8	increment;	/* only for block transfers */
	u8	intel;
	u8	tien;

	u16	address;
	u16	num_bytes;
	u8	direction;
	u32	mem;		/* either a "pointer" (actually the physical address) of the debi-memory (block-transfer, use virt_to_bus to supply it) or 4 bytes (as one u32-value) for immediate transfer */
};

struct saa7146_modes_constants {
	u8  v_offset;
	u16 v_field;
	u16 v_calc;
	
	u8  h_offset;
	u16 h_pixels;
	u16 h_calc;
	
	u16 v_max_out;
	u16 h_max_out;
};

struct	saa7146_mmap_struct
{
	const char *adr;
	unsigned long size;
};

#define SAA7146_PAL	0
#define SAA7146_NTSC	1
#define SAA7146_SECAM	2

#define SAA7146_HPS_SOURCE_PORT_A	0x00
#define SAA7146_HPS_SOURCE_PORT_B	0x01
#define SAA7146_HPS_SOURCE_YPB_CPA	0x02
#define SAA7146_HPS_SOURCE_YPA_CPB	0x03

#define SAA7146_HPS_SYNC_PORT_A	0x00
#define SAA7146_HPS_SYNC_PORT_B	0x01


/* Number of vertical active lines */
#define V_ACTIVE_LINES_PAL	576
#define V_ACTIVE_LINES_NTSC	480
#define V_ACTIVE_LINES_SECAM	576

/* Number of lines in a field for HPS to process */
#define V_FIELD_PAL	288
#define V_FIELD_NTSC	240
#define V_FIELD_SECAM	288

/* Number of lines of vertical offset before processing */
#define V_OFFSET_NTSC	0x10 /* PLI */
#define V_OFFSET_PAL	0x15
#define V_OFFSET_SECAM	0x14

/* Number of horizontal pixels to process */
#define H_PIXELS_NTSC	708
#define H_PIXELS_PAL	720
#define H_PIXELS_SECAM	720

/* Horizontal offset of processing window */
#define H_OFFSET_NTSC	0x40 /* PLI Try 0x3f and find all red colors turning into blue !!?? */
#define H_OFFSET_PAL	0x3a
#define H_OFFSET_SECAM	0x14

/* some memory-sizes */
#define GRABBING_MEM_SIZE	0x240000	/* 1024 * 576 * 4*/
#define CLIPPING_MEM_SIZE	20000 	        /* 1024 * 625 / 32 */
#define I2C_MEM_SIZE		0x000800	/* 2048 */
#define	RPS_MEM_SIZE		0x000800	/* 2048 */

/************************************************************************/
/* UNSORTED								*/
/************************************************************************/

#define ME1    0x0000000800
#define PV1    0x0000000008

/************************************************************************/
/* CLIPPING								*/
/************************************************************************/

/* some defines for the various clipping-modes */
#define SAA7146_CLIPPING_RECT		0x4
#define SAA7146_CLIPPING_RECT_INVERTED	0x5
#define SAA7146_CLIPPING_MASK		0x6
#define SAA7146_CLIPPING_MASK_INVERTED	0x7

/************************************************************************/
/* RPS									*/
/************************************************************************/

#define CMD_NOP		0x00000000  /* No operation */
#define CMD_CLR_EVENT	0x00000000  /* Clear event */
#define CMD_SET_EVENT	0x10000000  /* Set signal event */
#define CMD_PAUSE	0x20000000  /* Pause */
#define CMD_CHECK_LATE	0x30000000  /* Check late */
#define CMD_UPLOAD	0x40000000  /* Upload */
#define CMD_STOP	0x50000000  /* Stop */
#define CMD_INTERRUPT	0x60000000  /* Interrupt */
#define CMD_JUMP	0x80000000  /* Jump */
#define CMD_WR_REG	0x90000000  /* Write (load) register */
#define CMD_RD_REG	0xa0000000  /* Read (store) register */
#define CMD_WR_REG_MASK	0xc0000000  /* Write register with mask */

/************************************************************************/
/* OUTPUT FORMATS 							*/
/************************************************************************/

/* output formats; each entry holds three types of information */
/* composed is used in the sense of "not-planar" */

#define RGB15_COMPOSED	0x213
/* this means: yuv2rgb-conversation-mode=2, dither=yes(=1), format-mode = 3 */
#define RGB16_COMPOSED	0x210
#define RGB24_COMPOSED	0x201
#define RGB32_COMPOSED	0x202

#define YUV411_COMPOSED		0x003
/* this means: yuv2rgb-conversation-mode=0, dither=no(=0), format-mode = 3 */
#define YUV422_COMPOSED		0x000
#define YUV411_DECOMPOSED	0x00b
#define YUV422_DECOMPOSED	0x009
#define YUV420_DECOMPOSED	0x00a

/************************************************************************/
/* MISC 								*/
/************************************************************************/

/* Bit mask constants */
#define MASK_00   0x00000001    /* Mask value for bit 0 */
#define MASK_01   0x00000002    /* Mask value for bit 1 */
#define MASK_02   0x00000004    /* Mask value for bit 2 */
#define MASK_03   0x00000008    /* Mask value for bit 3 */
#define MASK_04   0x00000010    /* Mask value for bit 4 */
#define MASK_05   0x00000020    /* Mask value for bit 5 */
#define MASK_06   0x00000040    /* Mask value for bit 6 */
#define MASK_07   0x00000080    /* Mask value for bit 7 */
#define MASK_08   0x00000100    /* Mask value for bit 8 */
#define MASK_09   0x00000200    /* Mask value for bit 9 */
#define MASK_10   0x00000400    /* Mask value for bit 10 */
#define MASK_11   0x00000800    /* Mask value for bit 11 */
#define MASK_12   0x00001000    /* Mask value for bit 12 */
#define MASK_13   0x00002000    /* Mask value for bit 13 */
#define MASK_14   0x00004000    /* Mask value for bit 14 */
#define MASK_15   0x00008000    /* Mask value for bit 15 */
#define MASK_16   0x00010000    /* Mask value for bit 16 */
#define MASK_17   0x00020000    /* Mask value for bit 17 */
#define MASK_18   0x00040000    /* Mask value for bit 18 */
#define MASK_19   0x00080000    /* Mask value for bit 19 */
#define MASK_20   0x00100000    /* Mask value for bit 20 */
#define MASK_21   0x00200000    /* Mask value for bit 21 */
#define MASK_22   0x00400000    /* Mask value for bit 22 */
#define MASK_23   0x00800000    /* Mask value for bit 23 */
#define MASK_24   0x01000000    /* Mask value for bit 24 */
#define MASK_25   0x02000000    /* Mask value for bit 25 */
#define MASK_26   0x04000000    /* Mask value for bit 26 */
#define MASK_27   0x08000000    /* Mask value for bit 27 */
#define MASK_28   0x10000000    /* Mask value for bit 28 */
#define MASK_29   0x20000000    /* Mask value for bit 29 */
#define MASK_30   0x40000000    /* Mask value for bit 30 */
#define MASK_31   0x80000000    /* Mask value for bit 31 */

#define MASK_B0   0x000000ff    /* Mask value for byte 0 */
#define MASK_B1   0x0000ff00    /* Mask value for byte 1 */
#define MASK_B2   0x00ff0000    /* Mask value for byte 2 */
#define MASK_B3   0xff000000    /* Mask value for byte 3 */

#define MASK_W0   0x0000ffff    /* Mask value for word 0 */
#define MASK_W1   0xffff0000    /* Mask value for word 1 */

#define MASK_PA   0xfffffffc    /* Mask value for physical address */
#define MASK_PR   0xfffffffe 	/* Mask value for protection register */
#define MASK_ER   0xffffffff    /* Mask value for the entire register */

#define MASK_NONE 0x00000000    /* No mask */

/************************************************************************/
/* REGISTERS 								*/
/************************************************************************/

#define BASE_ODD1         0x00  /* Video DMA 1 registers  */
#define BASE_EVEN1        0x04
#define PROT_ADDR1        0x08
#define PITCH1            0x0C
#define BASE_PAGE1        0x10  /* Video DMA 1 base page */
#define NUM_LINE_BYTE1    0x14

#define BASE_ODD2         0x18  /* Video DMA 2 registers */
#define BASE_EVEN2        0x1C
#define PROT_ADDR2        0x20
#define PITCH2            0x24
#define BASE_PAGE2        0x28  /* Video DMA 2 base page */
#define NUM_LINE_BYTE2    0x2C

#define BASE_ODD3         0x30  /* Video DMA 3 registers */
#define BASE_EVEN3        0x34
#define PROT_ADDR3        0x38
#define PITCH3            0x3C         
#define BASE_PAGE3        0x40  /* Video DMA 3 base page */
#define NUM_LINE_BYTE3    0x44

#define PCI_BT_V1         0x48  /* Video/FIFO 1 */
#define PCI_BT_V2         0x49  /* Video/FIFO 2 */
#define PCI_BT_V3         0x4A  /* Video/FIFO 3 */
#define PCI_BT_DEBI       0x4B  /* DEBI */
#define PCI_BT_A          0x4C  /* Audio */

#define DD1_INIT          0x50  /* Init setting of DD1 interface */

#define DD1_STREAM_B      0x54  /* DD1 B video data stream handling */
#define DD1_STREAM_A      0x56  /* DD1 A video data stream handling */

#define BRS_CTRL          0x58  /* BRS control register */
#define HPS_CTRL          0x5C  /* HPS control register */
#define HPS_V_SCALE       0x60  /* HPS vertical scale */
#define HPS_V_GAIN        0x64  /* HPS vertical ACL and gain */
#define HPS_H_PRESCALE    0x68  /* HPS horizontal prescale   */
#define HPS_H_SCALE       0x6C  /* HPS horizontal scale */
#define BCS_CTRL          0x70  /* BCS control */
#define CHROMA_KEY_RANGE  0x74
#define CLIP_FORMAT_CTRL  0x78  /* HPS outputs formats & clipping */

#define DEBI_CONFIG       0x7C
#define DEBI_COMMAND      0x80
#define DEBI_PAGE         0x84
#define DEBI_AD           0x88	

#define I2C_TRANSFER      0x8C	
#define I2C_STATUS        0x90	

#define BASE_A1_IN        0x94	/* Audio 1 input DMA */
#define PROT_A1_IN        0x98
#define PAGE_A1_IN        0x9C
  
#define BASE_A1_OUT       0xA0  /* Audio 1 output DMA */
#define PROT_A1_OUT       0xA4
#define PAGE_A1_OUT       0xA8

#define BASE_A2_IN        0xAC  /* Audio 2 input DMA */
#define PROT_A2_IN        0xB0
#define PAGE_A2_IN        0xB4

#define BASE_A2_OUT       0xB8  /* Audio 2 output DMA */
#define PROT_A2_OUT       0xBC
#define PAGE_A2_OUT       0xC0

#define RPS_PAGE0         0xC4  /* RPS task 0 page register */
#define RPS_PAGE1         0xC8  /* RPS task 1 page register */

#define RPS_THRESH0       0xCC  /* HBI threshold for task 0 */
#define RPS_THRESH1       0xD0  /* HBI threshold for task 1 */

#define RPS_TOV0          0xD4  /* RPS timeout for task 0 */
#define RPS_TOV1          0xD8  /* RPS timeout for task 1 */

#define IER               0xDC  /* Interrupt enable register */

#define GPIO_CTRL         0xE0  /* GPIO 0-3 register */

#define EC1SSR            0xE4  /* Event cnt set 1 source select */
#define EC2SSR            0xE8  /* Event cnt set 2 source select */
#define ECT1R             0xEC  /* Event cnt set 1 thresholds */
#define ECT2R             0xF0  /* Event cnt set 2 thresholds */

#define ACON1             0xF4
#define ACON2             0xF8

#define MC1               0xFC   /* Main control register 1 */
#define MC2               0x100  /* Main control register 2  */

#define RPS_ADDR0         0x104  /* RPS task 0 address register */
#define RPS_ADDR1         0x108  /* RPS task 1 address register */

#define ISR               0x10C  /* Interrupt status register */                                                             
#define PSR               0x110  /* Primary status register */
#define SSR               0x114  /* Secondary status register */

#define EC1R              0x118  /* Event counter set 1 register */
#define EC2R              0x11C  /* Event counter set 2 register */         

#define PCI_VDP1          0x120  /* Video DMA pointer of FIFO 1 */
#define PCI_VDP2          0x124  /* Video DMA pointer of FIFO 2 */
#define PCI_VDP3          0x128  /* Video DMA pointer of FIFO 3 */
#define PCI_ADP1          0x12C  /* Audio DMA pointer of audio out 1 */
#define PCI_ADP2          0x130  /* Audio DMA pointer of audio in 1 */
#define PCI_ADP3          0x134  /* Audio DMA pointer of audio out 2 */
#define PCI_ADP4          0x138  /* Audio DMA pointer of audio in 2 */
#define PCI_DMA_DDP       0x13C  /* DEBI DMA pointer */

#define LEVEL_REP         0x140,
#define A_TIME_SLOT1      0x180,  /* from 180 - 1BC */
#define A_TIME_SLOT2      0x1C0,  /* from 1C0 - 1FC */

/************************************************************************/
/* ISR-MASKS 								*/
/************************************************************************/

#define SPCI_PPEF       0x80000000  /* PCI parity error */
#define SPCI_PABO       0x40000000  /* PCI access error (target or master abort) */
#define SPCI_PPED       0x20000000  /* PCI parity error on 'real time data' */
#define SPCI_RPS_I1     0x10000000  /* Interrupt issued by RPS1 */
#define SPCI_RPS_I0     0x08000000  /* Interrupt issued by RPS0 */
#define SPCI_RPS_LATE1  0x04000000  /* RPS task 1 is late */
#define SPCI_RPS_LATE0  0x02000000  /* RPS task 0 is late */
#define SPCI_RPS_E1     0x01000000  /* RPS error from task 1 */
#define SPCI_RPS_E0     0x00800000  /* RPS error from task 0 */
#define SPCI_RPS_TO1    0x00400000  /* RPS timeout task 1 */
#define SPCI_RPS_TO0    0x00200000  /* RPS timeout task 0 */
#define SPCI_UPLD       0x00100000  /* RPS in upload */
#define SPCI_DEBI_S     0x00080000  /* DEBI status */
#define SPCI_DEBI_E     0x00040000  /* DEBI error */
#define SPCI_IIC_S      0x00020000  /* I2C status */
#define SPCI_IIC_E      0x00010000  /* I2C error */
#define SPCI_A2_IN      0x00008000  /* Audio 2 input DMA protection / limit */
#define SPCI_A2_OUT     0x00004000  /* Audio 2 output DMA protection / limit */
#define SPCI_A1_IN      0x00002000  /* Audio 1 input DMA protection / limit */
#define SPCI_A1_OUT     0x00001000  /* Audio 1 output DMA protection / limit */
#define SPCI_AFOU       0x00000800  /* Audio FIFO over- / underflow */
#define SPCI_V_PE       0x00000400  /* Video protection address */
#define SPCI_VFOU       0x00000200  /* Video FIFO over- / underflow */
#define SPCI_FIDA       0x00000100  /* Field ID video port A */
#define SPCI_FIDB       0x00000080  /* Field ID video port B */
#define SPCI_PIN3       0x00000040  /* GPIO pin 3 */
#define SPCI_PIN2       0x00000020  /* GPIO pin 2 */
#define SPCI_PIN1       0x00000010  /* GPIO pin 1 */
#define SPCI_PIN0       0x00000008  /* GPIO pin 0 */
#define SPCI_ECS        0x00000004  /* Event counter 1, 2, 4, 5 */
#define SPCI_EC3S       0x00000002  /* Event counter 3 */
#define SPCI_EC0S       0x00000001  /* Event counter 0 */

/************************************************************************/
/* I2C									*/
/************************************************************************/

/* time we wait after certain i2c-operations */
#define SAA7146_I2C_DELAY 		10

#define	SAA7146_I2C_ABORT	(1<<7)
#define	SAA7146_I2C_SPERR	(1<<6)
#define	SAA7146_I2C_APERR	(1<<5)
#define	SAA7146_I2C_DTERR	(1<<4)
#define	SAA7146_I2C_DRERR	(1<<3)
#define	SAA7146_I2C_AL		(1<<2)
#define	SAA7146_I2C_ERR		(1<<1)
#define	SAA7146_I2C_BUSY	(1<<0)

#define	SAA7146_I2C_START	(0x3)
#define	SAA7146_I2C_CONT	(0x2)
#define	SAA7146_I2C_STOP	(0x1)
#define	SAA7146_I2C_NOP		(0x0)

#define SAA7146_I2C_BUS_BIT_RATE_6400	(0x500)
#define SAA7146_I2C_BUS_BIT_RATE_3200	(0x100)
#define SAA7146_I2C_BUS_BIT_RATE_480	(0x400)
#define SAA7146_I2C_BUS_BIT_RATE_320	(0x600)
#define SAA7146_I2C_BUS_BIT_RATE_240	(0x700)
#define SAA7146_I2C_BUS_BIT_RATE_120	(0x000)
#define SAA7146_I2C_BUS_BIT_RATE_80	(0x200)
#define SAA7146_I2C_BUS_BIT_RATE_60	(0x300)


#endif
