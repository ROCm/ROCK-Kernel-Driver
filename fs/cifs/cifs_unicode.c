/*
 *   fs/cifs/cifs_unicode.c
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2002
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include "cifs_unicode.h"
#include "cifs_uniupr.h"
#include "cifspdu.h"
#include "cifs_debug.h"

/*
 * NAME:	toUpper()
 *
 * FUNCTION:	Upper case ASCII string (in place) using the current codepage
 *
 */

void
toUpper(const struct nls_table *n, char *mixed_string)
{
	unsigned int i;
	char temp;

	for (i = 0; i < strlen(mixed_string); i++) {
		temp = mixed_string[i];
		mixed_string[i] = n->charset2upper[(int) temp];
	}
}

/*
 * NAME:	cifs_strfromUCS()
 *
 * FUNCTION:	Convert little-endian unicode string to character string
 *
 */
int
cifs_strfromUCS_le(char *to, const wchar_t * from,	/* LITTLE ENDIAN */
		   int len, const struct nls_table *codepage)
{
	int i;
	int outlen = 0;

	for (i = 0; (i < len) && from[i]; i++) {
		int charlen;
		/* 2.4.0 kernel or greater */
		charlen =
		    codepage->uni2char(le16_to_cpu(from[i]), &to[outlen],
				       NLS_MAX_CHARSET_SIZE);
		if (charlen > 0) {
			outlen += charlen;
		} else {
			to[outlen++] = '?';
		}
	}
	to[outlen] = 0;
	return outlen;
}

/*
 * NAME:	cifs_strtoUCS()
 *
 * FUNCTION:	Convert character string to unicode string
 *
 */
int
cifs_strtoUCS(wchar_t * to, const char *from, int len,
	      const struct nls_table *codepage)
{
	int charlen;
	int i;

	for (i = 0; len && *from; i++, from += charlen, len -= charlen) {

		/* works for 2.4.0 kernel or later */
		charlen = codepage->char2uni(from, len, &to[i]);
		if (charlen < 1) {
			cERROR(1,
			       ("cifs_strtoUCS: char2uni returned %d",
				charlen));
			to[i] = cpu_to_le16(0x003f);	/* a question mark */
			charlen = 1;
		}
		to[i] = cpu_to_le16(to[i]);

	}

	to[i] = 0;
	return i;
}

/*
 * NAME:	get_UCSname2()
 *
 * FUNCTION:	Allocate and translate to unicode string
 *
 */
/*int
get_UCSname2(struct component_name *uniName, struct dentry *dentry,
	    struct nls_table *nls_tab)
{
	int length = dentry->d_name.len;

	if (length > 255)
		return ENAMETOOLONG;

	uniName->name = kmalloc((length + 1) * sizeof (wchar_t), GFP_KERNEL);

	if (uniName->name == NULL)
		return ENOSPC;

	uniName->namlen = cifs_strtoUCS(uniName->name, dentry->d_name.name,
					length, nls_tab);

	return 0;
} */
