#ifndef _SOUND_PC9800_H_
#define _SOUND_PC9800_H_

#include <asm/io.h>

#define PC9800_SOUND_IO_ID	0xa460

/* Sound Functions ID. */
#define PC9800_SOUND_ID()	((inb(PC9800_SOUND_IO_ID) >> 4) & 0x0f)

#define PC9800_SOUND_ID_DO	0x0	/* PC-98DO+ Internal */
#define PC9800_SOUND_ID_GS	0x1	/* PC-98GS Internal */
#define PC9800_SOUND_ID_73	0x2	/* PC-9801-73 (base 0x18x) */
#define PC9800_SOUND_ID_73A	0x3	/* PC-9801-73/76 (base 0x28x) */
#define PC9800_SOUND_ID_86	0x4	/* PC-9801-86 and compatible (base 0x18x) */
#define PC9800_SOUND_ID_86A	0x5	/* PC-9801-86 (base 0x28x) */
#define PC9800_SOUND_ID_NF	0x6	/* PC-9821Nf/Np Internal */
#define PC9800_SOUND_ID_XMATE	0x7	/* X-Mate Internal and compatible */
#define PC9800_SOUND_ID_118	0x8	/* PC-9801-118 and compatible(CanBe Internal, etc.) */

#define PC9800_SOUND_ID_UNKNOWN	0xf	/* Unknown (No Sound System or PC-9801-26) */

#endif
