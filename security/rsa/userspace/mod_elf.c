#include <argp.h>
#include <assert.h>
#include <dwarf.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <gelf.h>
#include <inttypes.h>
#include <langinfo.h>
//#include <libdwarf.h>
#include <libebl.h>
//#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>


size_t section_num;

/* Print the section headers.  */
static void print_shdr (Elf *elf, GElf_Ehdr *ehdr)
{
	size_t cnt;
	size_t shstrndx;

	/* Get the section header string table index.  */
	if (elf_getshstrndx(elf, &shstrndx) < 0) {
		fprintf(stderr, "cannot get section header string table index");
		return;
	}

	for (cnt = 0; cnt < section_num; ++cnt) {
		char flagbuf[20];
		char *cp;
		Elf_Scn *scn = elf_getscn(elf, cnt);
		GElf_Shdr shdr_mem;
		GElf_Shdr *shdr;
		char *name;
		char *buf;
		int i;

		if (scn == NULL)
			error (EXIT_FAILURE, 0, "cannot get section: %s", elf_errmsg (-1));

		/* Get the section header.  */
		shdr = gelf_getshdr (scn, &shdr_mem);
		if (shdr == NULL)
			error (EXIT_FAILURE, 0, "cannot get section header: %s", elf_errmsg (-1));

		name = elf_strptr(elf, shstrndx, shdr->sh_name);
		buf = elf_strptr(elf, shstrndx, shdr->sh_addr);
		printf("%s - ", name);
		for (i = 0; i < 200; ++i) {
			printf("%c", buf[i]);
		}
		printf("\n");

		//if (shdr->sh_flags & SHF_ALLOC)
      printf ("[%2zu] %-20s %-12s %0*" PRIx64 " %0*" PRIx64 " %0*" PRIx64
	      " %2" PRId64 " %-5s %2" PRId32 " %3" PRId32
	      " %2" PRId64 "\n",
	      cnt,
	      elf_strptr(elf, shstrndx, shdr->sh_name)
	      ?: "<corrupt>",
	      //ebl_section_type_name (ebl, shdr->sh_type, buf, sizeof (buf)),
	      "foobar",
	      ehdr->e_ident[EI_CLASS] == ELFCLASS32 ? 8 : 16, shdr->sh_addr,
	      ehdr->e_ident[EI_CLASS] == ELFCLASS32 ? 6 : 8, shdr->sh_offset,
	      ehdr->e_ident[EI_CLASS] == ELFCLASS32 ? 6 : 8, shdr->sh_size,
	      shdr->sh_entsize, flagbuf, shdr->sh_link, shdr->sh_info,
	      shdr->sh_addralign);
    }

  fputc_unlocked ('\n', stdout);
}





int main (int argc, char *argv[])
{
	char *filename = "foo";
	int fd;
	Elf *elf;

	elf_version(EV_CURRENT);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "error \"%s\" trying to open %s\n", strerror(errno), filename);
		exit(1);
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		fprintf(stderr, "Can't get elf descriptor for %s\n", filename);
		goto exit;
	}

	Elf_Kind kind = elf_kind(elf);
	switch(kind) {
		case ELF_K_ELF:
				printf("ELF_K_ELF\n");
				break;
		default:
				fprintf(stderr, "Not a proper elf file for us to process.\n");
				goto end;
	}

	GElf_Ehdr ehdr_mem;
	GElf_Ehdr *ehdr = gelf_getehdr(elf, &ehdr_mem);
	if (ehdr == NULL) {
		fprintf(stderr, "Can not read elf header.\n");
		goto end;
	}

	if (elf_getshnum(elf, &section_num) < 0) {
		fprintf(stderr, "Can not determine number of sections.\n");
		goto end;
	}

	printf("section_num = %d\n", section_num);

	print_shdr(elf, ehdr);


end:	

	elf_end(elf);
exit:
	close(fd);

	return 0;
}

