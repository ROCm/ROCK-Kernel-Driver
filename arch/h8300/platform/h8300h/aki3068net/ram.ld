/* AE-3068 (aka. aki3068net) RAM */

OUTPUT_ARCH(h8300h)
ENTRY("__start")

MEMORY 
	{
	ram    : ORIGIN = 0x400000, LENGTH = 0x600000-0x400000
	eram   : ORIGIN = 0x600000, LENGTH = 0
        iram   : ORIGIN = 0xffbf20, LENGTH = 0x4000
	}
