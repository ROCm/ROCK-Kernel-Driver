/* Simple code to turn various tables in an ELF file into alias definitions.
   This deals with kernel datastructures where they should be
   dealt with: in the kernel source.
   (C) 2002 Rusty Russell IBM Corporation.
*/
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

/* 32 bits: if it turns out to be 64, we add explicitly (see EXTRA_SIZE). */
typedef int kernel_ulong_t;
#include "../include/linux/types.h"
#include "../include/linux/mod_devicetable.h"

static int switch_endian;

static void __endian(const void *src, void *dest, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
		((unsigned char*)dest)[i] = ((unsigned char*)src)[size - i-1];
}

#define TO_NATIVE(x)						\
({								\
	typeof(x) __x;						\
	if (switch_endian) __endian(&(x), &(__x), sizeof(__x));	\
	else __x = x;						\
	__x;							\
})

#define ADD(str, sep, cond, field)                              \
do {                                                            \
        strcat(str, sep);                                       \
        if (cond)                                               \
                sprintf(str + strlen(str),                      \
                        sizeof(field) == 1 ? "%02X" :           \
                        sizeof(field) == 2 ? "%04X" :           \
                        sizeof(field) == 4 ? "%08X" : "",       \
                        field);                                 \
        else                                                    \
                sprintf(str + strlen(str), "*");                \
} while(0)

/* Looks like "usb:vNpNdlNdhNdcNdscNdpNicNiscNipN" */
static int do_usb_entry(const char *filename,
			struct usb_device_id *id, char *alias)
{
	id->match_flags = TO_NATIVE(id->match_flags);
	id->idVendor = TO_NATIVE(id->idVendor);
	id->idProduct = TO_NATIVE(id->idProduct);
	id->bcdDevice_lo = TO_NATIVE(id->bcdDevice_lo);
	id->bcdDevice_hi = TO_NATIVE(id->bcdDevice_hi);

	strcpy(alias, "usb:");
	ADD(alias, "v", id->match_flags&USB_DEVICE_ID_MATCH_VENDOR,
	    id->idVendor);
	ADD(alias, "p", id->match_flags&USB_DEVICE_ID_MATCH_PRODUCT,
	    id->idProduct);
	ADD(alias, "dl", id->match_flags&USB_DEVICE_ID_MATCH_DEV_LO,
	    id->bcdDevice_lo);
	ADD(alias, "dh", id->match_flags&USB_DEVICE_ID_MATCH_DEV_HI,
	    id->bcdDevice_hi);
	ADD(alias, "dc", id->match_flags&USB_DEVICE_ID_MATCH_DEV_CLASS,
	    id->bDeviceClass);
	ADD(alias, "dsc",
	    id->match_flags&USB_DEVICE_ID_MATCH_DEV_SUBCLASS,
	    id->bDeviceSubClass);
	ADD(alias, "dp",
	    id->match_flags&USB_DEVICE_ID_MATCH_DEV_PROTOCOL,
	    id->bDeviceProtocol);
	ADD(alias, "ic",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_CLASS,
	    id->bInterfaceClass);
	ADD(alias, "isc",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	    id->bInterfaceSubClass);
	ADD(alias, "ip",
	    id->match_flags&USB_DEVICE_ID_MATCH_INT_PROTOCOL,
	    id->bInterfaceProtocol);
	return 1;
}

/* Looks like: pci:vNdNsvNsdNcN. */
static int do_pci_entry(const char *filename,
			struct pci_device_id *id, char *alias)
{
	id->vendor = TO_NATIVE(id->vendor);
	id->device = TO_NATIVE(id->device);
	id->subvendor = TO_NATIVE(id->subvendor);
	id->subdevice = TO_NATIVE(id->subdevice);
	id->class = TO_NATIVE(id->class);
	id->class_mask = TO_NATIVE(id->class_mask);

	strcpy(alias, "pci:");
	ADD(alias, "v", id->vendor != PCI_ANY_ID, id->vendor);
	ADD(alias, "d", id->device != PCI_ANY_ID, id->device);
	ADD(alias, "sv", id->subvendor != PCI_ANY_ID, id->subvendor);
	ADD(alias, "sd", id->subdevice != PCI_ANY_ID, id->subdevice);
	if (id->class_mask != 0 && id->class_mask != ~0) {
		fprintf(stderr,
			"file2alias: Can't handle class_mask in %s:%04X\n",
			filename, id->class_mask);
		return 0;
	}
	ADD(alias, "c", id->class_mask == ~0, id->class);
	return 1;
}

/* Ignore any prefix, eg. v850 prepends _ */
static inline int sym_is(const char *symbol, const char *name)
{
	const char *match;

	match = strstr(symbol, name);
	if (!match)
		return 0;
	return match[strlen(symbol)] == '\0';
}

/* Returns 1 if we output anything. */
static int do_table(void *symval, unsigned long size,
		    unsigned long id_size,
		    void *function,
		    const char *filename, int *first)
{
	unsigned int i;
	char alias[500];
	int (*do_entry)(const char *, void *entry, char *alias) = function;
	int wrote = 0;

	if (size % id_size || size < id_size) {
		fprintf(stderr, "WARNING: %s ids %lu bad size (each on %lu)\n",
			filename, size, id_size);
		return 0;
	}
	/* Leave last one: it's the terminator. */
	size -= id_size;

	for (i = 0; i < size; i += id_size) {
		if (do_entry(filename, symval+i, alias)) {
			/* Always end in a wildcard, for future extension */
			if (alias[strlen(alias)-1] != '*')
				strcat(alias, "*");
			if (*first) {
				printf("#include <linux/module.h>\n\n");
				*first = 0;
			}
			printf("MODULE_ALIAS(\"%s\");\n", alias);
			wrote = 1;
		}
	}
	return wrote;
}

/* This is the best way of doing this without making a complete mess
   of the code. */
#undef analyse_file
#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Sym
#undef EXTRA_SIZE
#define analyse_file analyze_file32
#define Elf_Ehdr Elf32_Ehdr 
#define Elf_Shdr Elf32_Shdr 
#define Elf_Sym Elf32_Sym
#define EXTRA_SIZE 0
#include "file2alias_inc.c"
#undef analyse_file
#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Sym
#undef EXTRA_SIZE
#define analyse_file analyze_file64
#define Elf_Ehdr Elf64_Ehdr 
#define Elf_Shdr Elf64_Shdr 
#define Elf_Sym Elf64_Sym
#define EXTRA_SIZE 4
#include "file2alias_inc.c"

static void *grab_file(const char *filename, unsigned long *size)
{
	struct stat st;
	void *map;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0) {
		close(fd);
		return NULL;
	}
	*size = st.st_size;
	map = mmap(NULL, *size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mmap == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	close(fd);
	return map;
}

/* Look through files for __mod_*_device_table: emit alias definitions
   for compiling in. */
int main(int argc, char *argv[])
{
	void *file;
	unsigned long size;
	int endian;
	union { short s; char c[2]; } endian_test;

	endian_test.s = 1;
	if (endian_test.c[1] == 1) endian = ELFDATA2MSB;
	else if (endian_test.c[0] == 1) endian = ELFDATA2LSB;
	else
		abort();

	for (; argv[1]; argv++) {
		file = grab_file(argv[1], &size);
		if (!file) {
			fprintf(stderr, "file2alias: opening %s: %s\n",
				argv[1], strerror(errno));
			continue;
		}

		if (size < SELFMAG || memcmp(file, ELFMAG, SELFMAG) != 0)
			goto bad_elf;
		
		if (((unsigned char *)file)[EI_DATA] != endian)
			switch_endian = 1;

		switch (((unsigned char *)file)[EI_CLASS]) {
		case ELFCLASS32:
			analyze_file32(file, size, argv[1]);
			break;
		case ELFCLASS64:
			analyze_file64(file, size, argv[1]);
			break;
		default:
			goto bad_elf;
		}
		munmap(file, size);
		continue;
		
	bad_elf:
		fprintf(stderr, "file2alias: %s is not elf\n", argv[1]);
		return 1;
	}
	return 0;
}
