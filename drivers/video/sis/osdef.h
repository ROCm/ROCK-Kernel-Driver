#define LINUX_KERNEL

#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#define SiS_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)

/**********************************************************************/

#ifdef OutPortByte
#undef OutPortByte
#endif				/* OutPortByte */

#ifdef OutPortWord
#undef OutPortWord
#endif				/* OutPortWord */

#ifdef OutPortLong
#undef OutPortLong
#endif				/* OutPortLong */

#ifdef InPortByte
#undef InPortByte
#endif				/* InPortByte */

#ifdef InPortWord
#undef InPortWord
#endif				/* InPortWord */

#ifdef InPortLong
#undef InPortLong
#endif				/* InPortLong */

#define OutPortByte(p,v) outb((u8)(v),(u16)(p))
#define OutPortWord(p,v) outw((u16)(v),(u16)(p))
#define OutPortLong(p,v) outl((u32)(v),(u16)(p))
#define InPortByte(p)    inb((u16)(p))
#define InPortWord(p)    inw((u16)(p))
#define InPortLong(p)    inl((u16)(p))
