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
#include <stdint.h>
#include <endian.h>

#include "elfconfig.h"

/* We use the ELF typedefs, since we can't rely on stdint.h being present. */

#if KERNEL_ELFCLASS == ELFCLASS32
typedef Elf32_Addr     kernel_ulong_t;
#else
typedef Elf64_Addr     kernel_ulong_t;
#endif

typedef Elf32_Word     __u32;
typedef Elf32_Half     __u16;
typedef unsigned char  __u8;

/* Big exception to the "don't include kernel headers into userspace, which
 * even potentially has different endianness and word sizes, since 
 * we handle those differences explicitly below */
#include "../include/linux/mod_devicetable.h"

#if KERNEL_ELFCLASS == ELFCLASS32

#define Elf_Ehdr Elf32_Ehdr 
#define Elf_Shdr Elf32_Shdr 
#define Elf_Sym  Elf32_Sym

#else

#define Elf_Ehdr Elf64_Ehdr 
#define Elf_Shdr Elf64_Shdr 
#define Elf_Sym  Elf64_Sym

#endif

#if KERNEL_ELFDATA != HOST_ELFDATA

static void __endian(const void *src, void *dest, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
		((unsigned char*)dest)[i] = ((unsigned char*)src)[size - i-1];
}



#define TO_NATIVE(x)						\
({								\
	typeof(x) __x;						\
	__endian(&(x), &(__x), sizeof(__x));			\
	__x;							\
})

#else /* endianness matches */

#define TO_NATIVE(x) (x)

#endif


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

/* This contains the cookie-cutter code for ELF handling (32 v 64). */
static void analyze_file(Elf_Ehdr *hdr,
			unsigned int size,
			const char *filename)
{
	unsigned int i, num_syms = 0;
	Elf_Shdr *sechdrs;
	Elf_Sym *syms = NULL;
	char *secstrings, *strtab = NULL;
	int first = 1;

	if (size < sizeof(*hdr))
		goto truncated;

	sechdrs = (void *)hdr + TO_NATIVE(hdr->e_shoff);
	hdr->e_shoff = TO_NATIVE(hdr->e_shoff);
	hdr->e_shstrndx = TO_NATIVE(hdr->e_shstrndx);
	hdr->e_shnum = TO_NATIVE(hdr->e_shnum);
	for (i = 0; i < hdr->e_shnum; i++) {
		sechdrs[i].sh_type = TO_NATIVE(sechdrs[i].sh_type);
		sechdrs[i].sh_offset = TO_NATIVE(sechdrs[i].sh_offset);
		sechdrs[i].sh_size = TO_NATIVE(sechdrs[i].sh_size);
		sechdrs[i].sh_link = TO_NATIVE(sechdrs[i].sh_link);
	}

	/* Find symbol table. */
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_offset > size)
			goto truncated;
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			syms = (void *)hdr + sechdrs[i].sh_offset;
			num_syms = sechdrs[i].sh_size / sizeof(syms[0]);
		} else if (sechdrs[i].sh_type == SHT_STRTAB)
			strtab = (void *)hdr + sechdrs[i].sh_offset;
	}

	if (!strtab || !syms) {
		fprintf(stderr, "table2alias: %s no symtab?\n", filename);
		abort();
	}

	for (i = 0; i < num_syms; i++) {
		const char *symname;
		void *symval;

		syms[i].st_shndx = TO_NATIVE(syms[i].st_shndx);
		syms[i].st_name = TO_NATIVE(syms[i].st_name);
		syms[i].st_value = TO_NATIVE(syms[i].st_value);
		syms[i].st_size = TO_NATIVE(syms[i].st_size);

		if (!syms[i].st_shndx || syms[i].st_shndx >= hdr->e_shnum)
			continue;

		symname = strtab + syms[i].st_name;
		symval = (void *)hdr
			+ sechdrs[syms[i].st_shndx].sh_offset
			+ syms[i].st_value;
		if (sym_is(symname, "__mod_pci_device_table"))
			do_table(symval, syms[i].st_size,
				 sizeof(struct pci_device_id),
				 do_pci_entry, filename, &first);
		else if (sym_is(symname, "__mod_usb_device_table"))
			do_table(symval, syms[i].st_size,
				 sizeof(struct usb_device_id),
				 do_usb_entry, filename, &first);
	}
	return;

 truncated:
	fprintf(stderr, "table2alias: %s is truncated.\n", filename);
	abort();
}

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

	for (; argv[1]; argv++) {
		file = grab_file(argv[1], &size);
		if (!file) {
			fprintf(stderr, "file2alias: opening %s: %s\n",
				argv[1], strerror(errno));
			abort();
		}
		analyze_file(file, size, argv[1]);
		munmap(file, size);
	}
	return 0;
}
