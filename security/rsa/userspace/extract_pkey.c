/*
 * Distributed Security Module (DSM)
 *
 * Simple extractor of MPIs for e and n in gpg exported keys (or pubrings
 * with only one key apparently)
 * Exported keys come from gpg --export.
 * Output is meant to be copy pasted into kernel code until better mechanism.
 *	
 * Copyright (C) 2002-2003 Ericsson, Inc
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 * 
 * Author: David Gordon Aug 2003 
 * Modifs: Vincent Roy Sept 2003 
 */

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DSI_ELF_SIG_SIZE 512               /* this is a redefinition */
#define DSI_PKEY_N_OFFSET        8         /* offset for pkey->n */
#define DSI_MPI_MAX_SIZE_N 1024 
/* pkey->e MPI follows pkey->n MPI */


void usage();


int main(int argc, char **argv)
{
	int pkey_file, module_file, nread, count, i;
	unsigned int num;
	unsigned int len;
	unsigned char c;
	unsigned char *key;
	unsigned char *temp;
	int key_offset = 0;

	if (argc <= 1)
		usage();

	/* Get pkey MPIs, one for 'n' and the other for 'e' */
	pkey_file = open(argv[1], O_RDONLY);
	if (!pkey_file) {
		printf ("Unable to open pkey_file %s %s\n", argv[1], strerror(errno));
		return -1;
	}
//	module_file = open ("/dev/Digsig", O_WRONLY);
//	if (!module_file) {
//		printf ("Unable to open module char device %s\n", strerror(errno));
//		return -1;
//	}
  
	key = (unsigned char *)malloc (DSI_MPI_MAX_SIZE_N+1);
//	key[key_offset++] = 'n';
	/*
	 * Format of an MPI:
	 * - 2 bytes: length of MPI in BITS
	 * - MPI
	 */

	lseek(pkey_file, DSI_PKEY_N_OFFSET, SEEK_SET);
	read(pkey_file, &c, 1);
	key[key_offset++] = c;
	len = c << 8;

	read(pkey_file, &c, 1);
	key[key_offset++] = c;
	len |= c;
	len = (len + 7) / 8;   /* round up */

	if (len > DSI_ELF_SIG_SIZE) {
		printf("\nLength of 'n' MPI is too large %#x\n", len);
		return -1;
	}

	for (i = 0; i < len; i++) {
		read(pkey_file, &c, 1);
		key[key_offset++] = c;
		if (key_offset == DSI_MPI_MAX_SIZE_N+2) {
			temp = (unsigned char *)malloc (DSI_MPI_MAX_SIZE_N*2+1);
			memcpy (temp, key, key_offset-1);
			free (key);
			key = temp;
		}
	}
	printf("const char rsa_key_n[] = {\n");
	for (i = 0; i < key_offset; i++  ) {
		printf("0x%02x, ", key[i] & 0xff);
	}
	printf("};\n\n");
	printf("const int rsa_key_n_size = %d;\n", key_offset);
//	write (module_file, key, key_offset);

	key_offset = 0;
//	key[key_offset++] = 'e';
	read(pkey_file, &c, 1);
	key[key_offset++] = c;
	len = c << 8;

	read(pkey_file, &c, 1);
	key[key_offset++] = c;
	len |= c;
	len = (len + 7) / 8;   /* round up */

	if (len > DSI_ELF_SIG_SIZE) {
		printf("\nLength of 'e' MPI is too large %#x\n", len);
		return -1;
	}

	for (i = 0; i < len; i++) {
		read(pkey_file, &c, 1);
		key[key_offset++] = c;
	}

	printf("const char rsa_key_e[] = {\n");
	for (i = 0; i < key_offset; i++  ) {
		printf("0x%02x, ", key[i] & 0xff);
	}
	printf("};\n");
	printf("const int rsa_key_e_size = %d;\n", key_offset);
//	write (module_file, key, key_offset);

	return 0;
}


void usage() 
{
	printf("Usage: extract_pkey gpgkey\nYou can export a key with gpg --export\n");
}


