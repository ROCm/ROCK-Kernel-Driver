/*
 * Copyright (C) 1999 Ulf Carlsson (ulfc@bun.falkenberg.se)
 * Copyright (C) 2001 Ralf Baechle (ralf@gnu.org)
 */

#define VINO_BASE		0x00080000	/* In EISA address space */

#define VINO_REVID		0x0000
#define VINO_CTRL		0x0008
#define VINO_INTSTAT		0x0010	/* Interrupt status */
#define VINO_I2C_CTRL		0x0018
#define VINO_I2C_DATA		0x0020
#define VINO_A_ALPHA		0x0028	/* Channel A ... */
#define VINO_A_CLIPS		0x0030	/* Clipping start */
#define VINO_A_CLIPE		0x0038	/* Clipping end */
#define VINO_A_FRAMERT		0x0040	/* Framerate */
#define VINO_A_FLDCNT		0x0048	/* Field counter */
#define VINO_A_LNSZ		0x0050
#define VINO_A_LNCNT		0x0058
#define VINO_A_PGIX		0x0060	/* Page index */
#define VINO_A_DESC_PTR		0x0068	/* Ptr to next four descriptors */
#define VINO_A_DESC_TLB_PTR	0x0070	/* Ptr to start of descriptor table */
#define VINO_A_DESC_DATA0	0x0078	/* Descriptor data 0 */
#define VINO_A_DESC_DATA1	0x0080	/* ... */
#define VINO_A_DESC_DATA2	0x0088
#define VINO_A_DESC_DATA3	0x0090
#define VINO_A_FIFO_THRESHOLD	0x0098	/* FIFO threshold */
#define VINO_A_FIFO_RP		0x00a0
#define VINO_A_FIFO_WP		0x00a8
#define VINO_B_ALPHA		0x00b0	/* Channel B ... */
#define VINO_B_CLIPS		0x00b8
#define VINO_B_CLIPE		0x00c0
#define VINO_B_FRAMERT		0x00c8
#define VINO_B_FLDCNT		0x00d0
#define VINO_B_LNSZ		0x00d8
#define VINO_B_LNCNT		0x00e0
#define VINO_B_PGIX		0x00e8
#define VINO_B_DESC_PTR		0x00f0
#define VINO_B_DESC_TLB_PTR	0x00f8
#define VINO_B_DESC_DATA0	0x0100
#define VINO_B_DESC_DATA1	0x0108
#define VINO_B_DESC_DATA2	0x0110
#define VINO_B_DESC_DATA3	0x0118
#define VINO_B_FIFO_THRESHOLD	0x0120
#define VINO_B_FIFO_RP		0x0128
#define VINO_B_FIFO_WP		0x0130

/* Bits in the VINO_REVID register */

#define VINO_REVID_REV_MASK		0x000f	/* bits 0:3 */
#define VINO_REVID_ID_MASK		0x00f0	/* bits 4:7 */

/* Bits in the VINO_CTRL register */

#define VINO_CTRL_LITTLE_ENDIAN		(1<<0)
#define VINO_CTRL_A_FIELD_TRANS_INT	(1<<1)	/* Field transferred int */
#define VINO_CTRL_A_FIFO_OF_INT		(1<<2)	/* FIFO overflow int */
#define VINO_CTRL_A_END_DESC_TBL_INT	(1<<3)	/* End of desc table int */
#define VINO_CTRL_B_FIELD_TRANS_INT	(1<<4)	/* Field transferred int */
#define VINO_CTRL_B_FIFO_OF_INT		(1<<5)	/* FIFO overflow int */
#define VINO_CTRL_B_END_DESC_TLB_INT	(1<<6)	/* End of desc table int */
#define VINO_CTRL_A_DMA_ENBL		(1<<7)
#define VINO_CTRL_A_INTERLEAVE_ENBL	(1<<8)
#define VINO_CTRL_A_SYNC_ENBL		(1<<9)
#define VINO_CTRL_A_SELECT		(1<<10)	/* 1=D1 0=Philips */
#define VINO_CTRL_A_RGB			(1<<11)	/* 1=RGB 0=YUV */
#define VINO_CTRL_A_LUMA_ONLY		(1<<12)
#define VINO_CTRL_A_DEC_ENBL		(1<<13)	/* Decimation */
#define VINO_CTRL_A_DEC_SCALE_MASK	0x1c000	/* bits 14:17 */
#define VINO_CTRL_A_DEC_HOR_ONLY	(1<<17)	/* Horizontal only */
#define VINO_CTRL_A_DITHER		(1<<18)	/* 24 -> 8 bit dither */
#define VINO_CTRL_B_DMA_ENBL		(1<<19)
#define VINO_CTRL_B_INTERLEAVE_ENBL	(1<<20)
#define VINO_CTRL_B_SYNC_ENBL		(1<<21)
#define VINO_CTRL_B_SELECT		(1<<22)	/* 1=D1 0=Philips */
#define VINO_CTRL_B_RGB			(1<<22)	/* 1=RGB 0=YUV */
#define VINO_CTRL_B_LUMA_ONLY		(1<<23)
#define VINO_CTRL_B_DEC_ENBL		(1<<24)	/* Decimation */
#define VINO_CTRL_B_DEC_SCALE_MASK	0x1c000000	/* bits 25:28 */
#define VINO_CTRL_B_DEC_HOR_ONLY	(1<<29)	/* Decimation horizontal only */
#define VINO_CTRL_B_DITHER		(1<<30)	/* ChanB 24 -> 8 bit dither */

/* Bits in the Interrupt and Status register */

#define VINO_INTSTAT_A_FIELD_TRANS	(1<<0)	/* Field transferred int */
#define VINO_INTSTAT_A_FIFO_OF		(1<<1)	/* FIFO overflow int */
#define VINO_INTSTAT_A_END_DESC_TBL	(1<<2)	/* End of desc table int */
#define VINO_INTSTAT_B_FIELD_TRANS	(1<<3)	/* Field transferred int */
#define VINO_INTSTAT_B_FIFO_OF		(1<<4)	/* FIFO overflow int */
#define VINO_INTSTAT_B_END_DESC_TBL	(1<<5)	/* End of desc table int */

/* Bits in the Clipping Start register */

#define VINO_CLIPS_START		0x3ff		/* bits 0:9 */
#define VINO_CLIPS_ODD_MASK		0x7fc00		/* bits 10:18 */
#define VINO_CLIPS_EVEN_MASK		0xff80000	/* bits 19:27 */

/* Bits in the Clipping End register */

#define VINO_CLIPE_END			0x3ff		/* bits 0:9 */
#define VINO_CLIPE_ODD_MASK		0x7fc00		/* bits 10:18 */
#define VINO_CLIPE_EVEN_MASK		0xff80000	/* bits 19:27 */

/* Bits in the Frame Rate register */

#define VINO_FRAMERT_PAL		(1<<0)	/* 0=NTSC 1=PAL */
#define VINO_FRAMERT_RT_MASK		0x1ffe		/* bits 1:12 */

/* Bits in the VINO_I2C_CTRL */

#define VINO_CTRL_I2C_IDLE		(1<<0)	/* write: 0=force idle
						 * read: 0=idle 1=not idle */
#define VINO_CTRL_I2C_DIR		(1<<1)	/* 0=read 1=write */
#define VINO_CTRL_I2C_MORE_BYTES	(1<<2)	/* 0=last byte 1=more bytes */
#define VINO_CTRL_I2C_TRANS_BUSY	(1<<4)	/* 0=trans done 1=trans busy */
#define VINO_CTRL_I2C_ACK		(1<<5)	/* 0=ack received 1=ack not */
#define VINO_CTRL_I2C_BUS_ERROR		(1<<7)	/* 0=no bus err 1=bus err */
