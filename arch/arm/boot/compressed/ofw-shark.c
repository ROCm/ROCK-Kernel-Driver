/*
 * linux/arch/arm/boot/compressed/ofw-shark.c
 *
 * by Alexander Schulz <aschulz@netwinder.org>
 *
 * This file is used to get some basic information
 * about the memory layout of the shark we are running
 * on. Memory is usually divided in blocks a 8 MB.
 * And bootargs are copied from OpenFirmware.
 */


#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/setup.h>
#include <asm/page.h>


asmlinkage void
create_params (unsigned long *buffer)
{
	/* Is there a better address? Also change in mach-shark/arch.c */
	struct param_struct *params = (struct param_struct *) 0x08003000;
	int j,i,m,k,nr_banks,size;

	for (j=0;j<256;j++) params->u1.unused[j]=0;

	size=0;
	nr_banks=(unsigned int) buffer[0];
	if (nr_banks > NR_BANKS) nr_banks = NR_BANKS;
	for (j=0;j<nr_banks;j++){
		/* search the lowest address and put it into the next entry   */
		/* not a fast sort algorithm, but there are at most 8 entries */
		/* and this is used only once anyway                          */
		m=0xffffffff;
		for (i=0;i<(unsigned int) buffer[0];i++){
			if (buffer[2*i+1]<m) {
				m=buffer[2*i+1];
				k=i;
			}
		}
	  
		/* using pages_in_bank for startaddress and size               */
		/* start is in bytes, size in pages and not bigger than 0x2000 */
		/* This is decoded in fixup_shark in arch/arm/kernel/arch.c    */
		params->u1.s.pages_in_bank[j]=buffer[2*k+1]|(buffer[2*k+2]/PAGE_SIZE);
		size += buffer[2*k+2];

		buffer[2*k+1]=0xffffffff;                    /* mark as copied             */
	}
	
	params->u1.s.page_size = PAGE_SIZE;
	params->u1.s.nr_pages  = size/PAGE_SIZE;
	params->u1.s.flags     = FLAG_READONLY;
	
	/* Copy over the bootargs */
	for (j=0;j<128/4;j++) {
		((unsigned long *) params->commandline)[j]=buffer[33+j];
	}
}


typedef int (*ofw_handle_t)(void *);

/* Everything below is called with a wrong MMU setting.
 * This means: no string constants, no initialization of
 * arrays, no global variables! This is ugly but I didn't
 * want to write this in assembler :-)
 */

int
of_decode_int(const unsigned char *p)
{
	unsigned int i = *p++ << 8;
	i = (i + *p++) << 8;
	i = (i + *p++) << 8;
	return (i + *p);
}
  
int
OF_finddevice(ofw_handle_t openfirmware, char *name)
{
	unsigned int args[8];
	char service[12];

	service[0]='f';
	service[1]='i';
	service[2]='n';
	service[3]='d';
	service[4]='d';
	service[5]='e';
	service[6]='v';
	service[7]='i';
	service[8]='c';
	service[9]='e';
	service[10]='\0';

	args[0]=(unsigned int)service;
	args[1]=1;
	args[2]=1;
	args[3]=(unsigned int)name;

	if (openfirmware(args) == -1)
		return -1;
	return args[4];
}

int
OF_getproplen(ofw_handle_t openfirmware, int handle, char *prop)
{
	unsigned int args[8];
	char service[12];

	service[0]='g';
	service[1]='e';
	service[2]='t';
	service[3]='p';
	service[4]='r';
	service[5]='o';
	service[6]='p';
	service[7]='l';
	service[8]='e';
	service[9]='n';
	service[10]='\0';

	args[0] = (unsigned int)service;
	args[1] = 2;
	args[2] = 1;
	args[3] = (unsigned int)handle;
	args[4] = (unsigned int)prop;

	if (openfirmware(args) == -1)
		return -1;
	return args[5];
}
  
int
OF_getprop(ofw_handle_t openfirmware, int handle, char *prop, void *buf, unsigned int buflen)
{
	unsigned int args[8];
	char service[8];

	service[0]='g';
	service[1]='e';
	service[2]='t';
	service[3]='p';
	service[4]='r';
	service[5]='o';
	service[6]='p';
	service[7]='\0';

	args[0] = (unsigned int)service;
	args[1] = 4;
	args[2] = 1;
	args[3] = (unsigned int)handle;
	args[4] = (unsigned int)prop;
	args[5] = (unsigned int)buf;
	args[6] = buflen;

	if (openfirmware(args) == -1)
		return -1;
	return args[7];
}
  
asmlinkage void ofw_init(ofw_handle_t o, int *nomr, int *pointer)
{
	int phandle,i,mem_len,buffer[32];
	char temp[12];
  
	temp[0]='/';
	temp[1]='m';
	temp[2]='e';
	temp[3]='m';
	temp[4]='o';
	temp[5]='r';
	temp[6]='y';
	temp[7]='\0';

	phandle=OF_finddevice(o,temp);

	temp[0]='r';
	temp[1]='e';
	temp[2]='g';
	temp[3]='\0';

	mem_len = OF_getproplen(o,phandle, temp);
	OF_getprop(o,phandle, temp, buffer, mem_len);
	*nomr=mem_len >> 3;

	for (i=0; i<=mem_len/4; i++) pointer[i]=of_decode_int((const unsigned char *)&buffer[i]);

	temp[0]='/';
	temp[1]='c';
	temp[2]='h';
	temp[3]='o';
	temp[4]='s';
	temp[5]='e';
	temp[6]='n';
	temp[7]='\0';

	phandle=OF_finddevice(o,temp);

	temp[0]='b';
	temp[1]='o';
	temp[2]='o';
	temp[3]='t';
	temp[4]='a';
	temp[5]='r';
	temp[6]='g';
	temp[7]='s';
	temp[8]='\0';

	mem_len = OF_getproplen(o,phandle, temp);
	OF_getprop(o,phandle, temp, buffer, mem_len);
	for (i=0; i<=mem_len/4; i++) pointer[i+32]=buffer[i];
  
}
