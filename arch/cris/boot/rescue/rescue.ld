MEMORY 
	{
	flash : ORIGIN = 0x00000000,
	        LENGTH = 0x00100000
	}

SECTIONS
{
	.text :
	{
		_stext = . ;
		*(.text)
		_etext = . ;
	} > flash
	.data :
	{
		*(.data)
		_edata = . ;
	} > flash
}
