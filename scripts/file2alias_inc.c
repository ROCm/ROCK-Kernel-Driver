/* This contains the cookie-cutter code for ELF handling (32 v 64).
   Return true if anything output. */
static void analyse_file(Elf_Ehdr *hdr,
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
	if (switch_endian) {
		hdr->e_shoff = TO_NATIVE(hdr->e_shoff);
		hdr->e_shstrndx = TO_NATIVE(hdr->e_shstrndx);
		hdr->e_shnum = TO_NATIVE(hdr->e_shnum);
		for (i = 0; i < hdr->e_shnum; i++) {
			sechdrs[i].sh_type = TO_NATIVE(sechdrs[i].sh_type);
			sechdrs[i].sh_offset = TO_NATIVE(sechdrs[i].sh_offset);
			sechdrs[i].sh_size = TO_NATIVE(sechdrs[i].sh_size);
			sechdrs[i].sh_link = TO_NATIVE(sechdrs[i].sh_link);
		}
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
		return;
	}

	for (i = 0; i < num_syms; i++) {
		const char *symname;
		void *symval;

		if (switch_endian) {
			syms[i].st_shndx = TO_NATIVE(syms[i].st_shndx);
			syms[i].st_name = TO_NATIVE(syms[i].st_name);
			syms[i].st_value = TO_NATIVE(syms[i].st_value);
			syms[i].st_size = TO_NATIVE(syms[i].st_size);
		}

		if (!syms[i].st_shndx || syms[i].st_shndx >= hdr->e_shnum)
			continue;

		symname = strtab + syms[i].st_name;
		symval = (void *)hdr
			+ sechdrs[syms[i].st_shndx].sh_offset
			+ syms[i].st_value;
		if (sym_is(symname, "__mod_pci_device_table"))
			do_table(symval, syms[i].st_size,
				 sizeof(struct pci_device_id) + EXTRA_SIZE * 1,
				 do_pci_entry, filename, &first);
		else if (sym_is(symname, "__mod_usb_device_table"))
			do_table(symval, syms[i].st_size,
				 sizeof(struct usb_device_id) + EXTRA_SIZE * 1,
				 do_usb_entry, filename, &first);
	}
	return;

 truncated:
	fprintf(stderr, "table2alias: %s is truncated.\n", filename);
	return;
}
