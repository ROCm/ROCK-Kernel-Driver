/* 2001/10/02
 *
 * gerdes_amd7930.h     Header-file included by
 *                      gerdes_amd7930.c
 *
 * Author               Christoph Ersfeld <info@formula-n.de>
 *                      Formula-n Europe AG (www.formula-n.com)
 *                      previously Gerdes AG
 *
 *
 *                      This file is (c) under GNU PUBLIC LICENSE
 */




#define BYTE							unsigned char
#define WORD							unsigned int
#define rByteAMD(cs, reg)					cs->readisac(cs, reg)
#define wByteAMD(cs, reg, val)					cs->writeisac(cs, reg, val)
#define rWordAMD(cs, reg)					ReadWordAmd7930(cs, reg)
#define wWordAMD(cs, reg, val)					WriteWordAmd7930(cs, reg, val)
#define HIBYTE(w)						((unsigned char)((w & 0xff00) / 256))
#define LOBYTE(w)						((unsigned char)(w & 0x00ff))

#define AmdIrqOff(cs)						cs->dc.amd7930.setIrqMask(cs, 0)
#define AmdIrqOn(cs)						cs->dc.amd7930.setIrqMask(cs, 1)

#define AMD_CR		0x00
#define AMD_DR		0x01


#define DBUSY_TIMER_VALUE 80

static WORD initAMD[] = {
	0x0100,

	0x00A5, 3, 0x01, 0x40, 0x58,				// LPR, LMR1, LMR2
	0x0086, 1, 0x0B,					// DMR1 (D-Buffer TH-Interrupts on)
	0x0087, 1, 0xFF,					// DMR2
	0x0092, 1, 0x03,					// EFCR (extended mode d-channel-fifo on)
	0x0090, 4, 0xFE, 0xFF, 0x02, 0x0F,			// FRAR4, SRAR4, DMR3, DMR4 (address recognition )
	0x0084, 2, 0x80, 0x00,					// DRLR
	0x00C0, 1, 0x47,					// PPCR1
	0x00C8, 1, 0x01,					// PPCR2

	0x0102,
	0x0107,
	0x01A1, 1,
	0x0121, 1,
	0x0189, 2,

	0x0045, 4, 0x61, 0x72, 0x00, 0x00,			// MCR1, MCR2, MCR3, MCR4
	0x0063, 2, 0x08, 0x08,					// GX
	0x0064, 2, 0x08, 0x08,					// GR
	0x0065, 2, 0x99, 0x00,					// GER
	0x0066, 2, 0x7C, 0x8B,					// STG
	0x0067, 2, 0x00, 0x00,					// FTGR1, FTGR2
	0x0068, 2, 0x20, 0x20,					// ATGR1, ATGR2
	0x0069, 1, 0x4F,					// MMR1
	0x006A, 1, 0x00,					// MMR2
	0x006C, 1, 0x40,					// MMR3
	0x0021, 1, 0x02,					// INIT
	0x00A3, 1, 0x40,					// LMR1

	0xFFFF};

extern void Amd7930_interrupt(struct IsdnCardState *cs, unsigned char irflags);
extern void Amd7930_init(struct IsdnCardState *cs);
