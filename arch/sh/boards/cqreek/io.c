#define IDE_OFFSET 0xA4000000UL
#define ISA_OFFSET 0xA4A00000UL

unsigned long cqreek_port2addr(unsigned long port)
{
	if (0x0000<=port && port<=0x0040)
		return IDE_OFFSET + port;
	if ((0x01f0<=port && port<=0x01f7) || port == 0x03f6)
		return IDE_OFFSET + port;

	return ISA_OFFSET + port;
}


