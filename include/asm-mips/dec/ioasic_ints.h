/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Definitions for the interrupt related bits in the JUNKIO Asic
 * interrupt status register (and the interrupt mask register, of course)
 *
 * Created with Information from:
 *
 * "DEC 3000 300/400/500/600/700/800/900 AXP Models System Programmer's Manual"
 *
 * and the Mach Sources
 */

/* 
 * the upper 16 bits are common to all JUNKIO machines
 * (except the FLOPPY and ISDN bits, which are Maxine sepcific)
 */
#define SCC0_TRANS_PAGEEND	0x80000000	/* Serial DMA Errors	*/
#define SCC0_TRANS_MEMRDERR	0x40000000	/* see below		*/
#define SCC0_RECV_HALFPAGE	0x20000000
#define	SCC0_RECV_PAGOVRRUN	0x10000000
#define SCC1_TRANS_PAGEEND	0x08000000	/* end of page reached	*/
#define SCC1_TRANS_MEMRDERR	0x04000000	/* SCC1 DMA memory err	*/
#define SCC1_RECV_HALFPAGE	0x02000000	/* SCC1 half page	*/
#define	SCC1_RECV_PAGOVRRUN	0x01000000	/* SCC1 receive overrun	*/
#define FLOPPY_DMA_ERROR	0x00800000	/* FDI DMA error	*/
#define	ISDN_TRANS_PTR_LOADED	0x00400000	/* xmitbuf ptr loaded	*/
#define ISDN_RECV_PTR_LOADED	0x00200000	/* rcvbuf ptr loaded	*/
#define ISDN_DMA_MEMRDERR	0x00100000	/* read or ovrrun error	*/
#define SCSI_PTR_LOADED		0x00080000
#define SCSI_PAGOVRRUN		0x00040000	/* page overrun? */
#define SCSI_DMA_MEMRDERR	0x00020000
#define LANCE_DMA_MEMRDERR	0x00010000

/*
 * the lower 16 bits are system specific
 */

/*
 * The following three seem to be in common
 */
#define SCSI_CHIP		0x00000200
#define LANCE_CHIP		0x00000100
#define SCC1_CHIP		0x00000080	/* NOT on maxine	*/
#define SCC0_CHIP		0x00000040

/*
 * The rest is different
 */

/* kmin aka 3min aka kn02ba aka DS5000_1xx */
#define KMIN_TIMEOUT		0x00001000	/* CPU IO-Write Timeout	*/
#define KMIN_CLOCK		0x00000020
#define KMIN_SCSI_FIFO		0x00000004	/* SCSI Data Ready	*/

/* kn02ca aka maxine */
#define MAXINE_FLOPPY		0x00008000	/* FDI Interrupt        */
#define MAXINE_TC0		0x00001000	/* TC Option 0      	*/
#define MAXINE_ISDN		0x00000800	/* ISDN	Chip		*/
#define MAXINE_FLOPPY_HDS	0x00000080	/* Floppy Status      	*/
#define MAXINE_TC1		0x00000020	/* TC Option 1		*/
#define MAXINE_FLOPPY_XDS	0x00000010	/* Floppy Status      	*/
#define MAXINE_VINT		0x00000008	/* Video Frame		*/
#define MAXINE_N_VINT		0x00000004	/* Not Video frame	*/
#define MAXINE_DTOP_TRANS	0x00000002	/* DTI Xmit-Rdy		*/
#define MAXINE_DTOP_RECV	0x00000001	/* DTI Recv-Available	*/

/* kn03 aka 3max+ aka DS5000_2x0 */
#define KN03_TC2		0x00002000
#define KN03_TC1		0x00001000
#define KN03_TC0		0x00000800
#define KN03_SCSI_FIFO		0x00000004	/* ??? Info from Mach	*/

/*
 * Now form groups, i.e. all serial interrupts, all SCSI interrupts and so on. 
 */
#define SERIAL_INTS	(SCC0_TRANS_PAGEEND | SCC0_TRANS_MEMRDERR | \
			SCC0_RECV_HALFPAGE | SCC0_RECV_PAGOVRRUN | \
			SCC1_TRANS_PAGEEND | SCC1_TRANS_MEMRDERR | \
			SCC1_RECV_HALFPAGE | SCC1_RECV_PAGOVRRUN | \
			SCC1_CHIP | SCC0_CHIP)

#define XINE_SERIAL_INTS	(SCC0_TRANS_PAGEEND | SCC0_TRANS_MEMRDERR | \
			SCC0_RECV_HALFPAGE | SCC0_RECV_PAGOVRRUN | \
			SCC0_CHIP)

#define SCSI_DMA_INTS	(/* SCSI_PTR_LOADED | */ SCSI_PAGOVRRUN | \
			SCSI_DMA_MEMRDERR)

#define KMIN_SCSI_INTS	(SCSI_PTR_LOADED | SCSI_PAGOVRRUN | \
			SCSI_DMA_MEMRDERR | SCSI_CHIP | KMIN_SCSI_FIFO)

#define LANCE_INTS	(LANCE_DMA_MEMRDERR | LANCE_CHIP)

/*
 * For future use ...
 */
#define XINE_FLOPPY_INTS (MAXINE_FLOPPY | MAXINE_FLOPPY_HDS | \
			FLOPPY_DMA_ERROR | MAXINE_FLOPPY_XDS)

#define XINE_ISDN_INTS	(MAXINE_ISDN | ISDN_TRANS_PTR_LOADED | \
			ISDN_RECV_PTR_LOADED | ISDN_DMA_MEMRDERR)

#define XINE_DTOP_INTS	(MAXINE_DTOP_TRANS | DTOP_RECV | \
			ISDN_TRANS_PTR_LOADED | ISDN_RECV_PTR_LOADED | \
			ISDN_DMA_MEMRDERR)

