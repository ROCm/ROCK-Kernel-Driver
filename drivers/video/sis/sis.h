#ifndef _SISFB_LOCAL
#define _SISFB_LOCAL
#include <linux/types.h>

#undef NOBIOS
#undef CONFIG_FB_SIS_LINUXBIOS

#ifdef NOBIOS
#undef CONFIG_FB_SIS_LINUXBIOS
#endif

#define TRUE  1
#define FALSE 0
#define NO_ERROR 0

/* Data type conversion */
#define UCHAR   unsigned char
#define USHORT  unsigned short
#define ULONG   unsigned long
#define SHORT   short
#define BOOLEAN int
#define VOID void

#define IND_SIS_CRT2_PORT_04        0x04 - 0x30
#define IND_SIS_CRT2_PORT_10        0x10 - 0x30
#define IND_SIS_CRT2_PORT_12        0x12 - 0x30
#define IND_SIS_CRT2_PORT_14        0x14 - 0x30

#define ClearALLBuffer(x)  ClearBuffer(x)

/* Data struct for setmode codes */
typedef enum _CHIP_TYPE {
    SIS_GENERIC = 0,
    SIS_Glamour,	//300
    SIS_Trojan,		//630
    SIS_Spartan,	//540
    SIS_730,
    MAX_SIS_CHIP
} CHIP_TYPE;

typedef enum _LCD_TYPE {
    LCD1024 = 1,
    LCD1280,
    LCD2048,
    LCD1920,
    LCD1600,
    LCD800,
    LCD640
} LCD_TYPE;


typedef struct _HW_DEVICE_EXTENSION
{
	unsigned long VirtualRomBase;
	char *VirtualVideoMemoryAddress;
	unsigned short IOAddress;
	CHIP_TYPE jChipID;
	int bIntegratedMMEnabled;
	LCD_TYPE usLCDType;
	u8 revision_id;
	u8 uVBChipID;
} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;

#endif
