/* $Id: dcr.h,v 1.3 2001/03/01 23:23:33 davem Exp $ */
#ifndef _SPARC64_DCR_H
#define _SPARC64_DCR_H

/* UltraSparc-III Dispatch Control Register, ASR 0x12 */
#define DCR_BPE		0x0000000000000020 /* Branch Predict Enable		*/
#define DCR_RPE		0x0000000000000010 /* Return Address Prediction Enable*/
#define DCR_SI		0x0000000000000008 /* Single Instruction Disable	*/
#define DCR_MS		0x0000000000000001 /* Multi-Scalar dispatch		*/

#endif /* _SPARC64_DCR_H */
