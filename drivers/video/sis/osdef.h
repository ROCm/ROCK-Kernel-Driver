/* #define WINCE_HEADER */
/* #define WIN2000 */
/* #define TC */
#define LINUX_KERNEL	   /* Kernel framebuffer */
/* #define LINUX_XF86 */   /* XFree86 */

/**********************************************************************/
#ifdef LINUX_KERNEL
	#include <linux/config.h>
	#include <linux/version.h>
	#ifdef CONFIG_FB_SIS_300
 		#define SIS300
	#endif

	#ifdef CONFIG_FB_SIS_315
		#define SIS315H
	#endif
	#if 1
		#define SISFBACCEL	/* Include 2D acceleration */
	#endif
	#if 1
		#define SISFB_PAN	/* Include Y-Panning code */
	#endif
#else
/*	#define SIS300*/
	#define SIS315H
#endif
#ifdef LINUX_XF86
	#define SIS300
	/* #define SIS315H */ /* TW: done above */
#endif

/**********************************************************************/
#ifdef TC
#endif
#ifdef WIN2000
#endif
#ifdef WINCE_HEADER
#endif
#ifdef LINUX_XF86
#endif
#ifdef LINUX_KERNEL
#endif
/**********************************************************************/
#ifdef TC
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize);
#endif
#ifdef WIN2000
#define SiS_SetMemory(MemoryAddress,MemorySize,value) MemFill((PVOID) MemoryAddress,(ULONG) MemorySize,(UCHAR) value);
#endif
#ifdef WINCE_HEADER
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize);
#endif
#ifdef LINUX_XF86
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#endif
#ifdef LINUX_KERNEL
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#endif
/**********************************************************************/

/**********************************************************************/

#ifdef TC
#define SiS_MemoryCopy(Destination,Soruce,Length) memmove(Destination, Soruce, Length);
#endif
#ifdef WIN2000
#define SiS_MemoryCopy(Destination,Soruce,Length)  /*VideoPortMoveMemory((PUCHAR)Destination , Soruce,length);*/
#endif
#ifdef WINCE_HEADER
#define SiS_MemoryCopy(Destination,Soruce,Length) memmove(Destination, Soruce, Length);
#endif
#ifdef LINUX_XF86
#define SiS_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif
#ifdef LINUX_KERNEL
#define SiS_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif

/**********************************************************************/

#ifdef OutPortByte
#undef OutPortByte
#endif /* OutPortByte */

#ifdef OutPortWord
#undef OutPortWord
#endif /* OutPortWord */

#ifdef OutPortLong
#undef OutPortLong
#endif /* OutPortLong */

#ifdef InPortByte
#undef InPortByte
#endif /* InPortByte */

#ifdef InPortWord
#undef InPortWord
#endif /* InPortWord */

#ifdef InPortLong
#undef InPortLong
#endif /* InPortLong */

/**********************************************************************/
/*  TC                                                                */
/**********************************************************************/

#ifdef TC
#define OutPortByte(p,v) outp((unsigned short)(p),(unsigned char)(v))
#define OutPortWord(p,v) outp((unsigned short)(p),(unsigned short)(v))
#define OutPortLong(p,v) outp((unsigned short)(p),(unsigned long)(v))
#define InPortByte(p)    inp((unsigned short)(p))
#define InPortWord(p)    inp((unsigned short)(p))
#define InPortLong(p)    ((inp((unsigned short)(p+2))<<16) | inp((unsigned short)(p)))
#endif

/**********************************************************************/
/*  LINUX XF86                                                        */
/**********************************************************************/

#ifdef LINUX_XF86
#define OutPortByte(p,v) outb((CARD16)(p),(CARD8)(v))
#define OutPortWord(p,v) outw((CARD16)(p),(CARD16)(v))
#define OutPortLong(p,v) outl((CARD16)(p),(CARD32)(v))
#define InPortByte(p)    inb((CARD16)(p))
#define InPortWord(p)    inw((CARD16)(p))
#define InPortLong(p)    inl((CARD16)(p))
#endif

/**********************************************************************/
/*  LINUX KERNEL                                                      */
/**********************************************************************/

#ifdef LINUX_KERNEL
#define OutPortByte(p,v) outb((u8)(v),(u16)(p))
#define OutPortWord(p,v) outw((u16)(v),(u16)(p))
#define OutPortLong(p,v) outl((u32)(v),(u16)(p))
#define InPortByte(p)    inb((u16)(p))
#define InPortWord(p)    inw((u16)(p))
#define InPortLong(p)    inl((u16)(p))
#endif

/**********************************************************************/
/*  WIN 2000                                                          */
/**********************************************************************/

#ifdef WIN2000
#define OutPortByte(p,v) VideoPortWritePortUchar ((PUCHAR) (p), (UCHAR) (v))
#define OutPortWord(p,v) VideoPortWritePortUshort((PUSHORT) (p), (USHORT) (v))
#define OutPortLong(p,v) VideoPortWritePortUlong ((PULONG) (p), (ULONG) (v))
#define InPortByte(p)    VideoPortReadPortUchar  ((PUCHAR) (p))
#define InPortWord(p)    VideoPortReadPortUshort ((PUSHORT) (p))
#define InPortLong(p)    VideoPortReadPortUlong  ((PULONG) (p))
#endif


/**********************************************************************/
/*  WIN CE                                                            */
/**********************************************************************/

#ifdef WINCE_HEADER
#define OutPortByte(p,v) WRITE_PORT_UCHAR ((PUCHAR) (p), (UCHAR) (v))
#define OutPortWord(p,v) WRITE_PORT_USHORT((PUSHORT) (p), (USHORT) (v))
#define OutPortLong(p,v) WRITE_PORT_ULONG ((PULONG) (p), (ULONG) (v))
#define InPortByte(p)    READ_PORT_UCHAR  ((PUCHAR) (p))
#define InPortWord(p)    READ_PORT_USHORT ((PUSHORT) (p))
#define InPortLong(p)    READ_PORT_ULONG  ((PULONG) (p))
#endif
