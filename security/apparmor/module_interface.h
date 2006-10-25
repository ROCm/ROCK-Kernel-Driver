#ifndef __MODULEINTERFACE_H
#define __MODULEINTERFACE_H

/* Codes of the types of basic structures that are understood */
#define AA_CODE_BYTE (sizeof(u8))
#define INTERFACE_ID "INTERFACE"

#define APPARMOR_INTERFACE_VERSION 2

enum aa_code {
	AA_U8,
	AA_U16,
	AA_U32,
	AA_U64,
	AA_NAME,	/* same as string except it is items name */
	AA_DYN_STRING,
	AA_STATIC_BLOB,
	AA_STRUCT,
	AA_STRUCTEND,
	AA_LIST,
	AA_LISTEND,
	AA_OFFSET,
	AA_BAD
};

/* aa_ext tracks the kernel buffer and read position in it.  The interface
 * data is copied into a kernel buffer in apparmorfs and then handed off to
 * the activate routines.
 */
struct aa_ext {
	void *start;
	void *end;
	void *pos;	/* pointer to current position in the buffer */
	u32 version;
};

#endif /* __MODULEINTERFACE_H */
