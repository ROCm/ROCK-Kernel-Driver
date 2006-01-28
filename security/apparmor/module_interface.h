#ifndef __MODULEINTERFACE_H
#define __MODULEINTERFACE_H

/* Codes of the types of basic structures that are understood */
#define SD_CODE_BYTE (sizeof(u8))
#define INTERFACE_ID "INTERFACE"

#define SUBDOMAIN_INTERFACE_VERSION 2

enum sd_code {
	SD_U8,
	SD_U16,
	SD_U32,
	SD_U64,
	SD_NAME,	/* same as string except it is items name */
	SD_DYN_STRING,
	SD_STATIC_BLOB,
	SD_STRUCT,
	SD_STRUCTEND,
	SD_LIST,
	SD_LISTEND,
	SD_OFFSET,
	SD_BAD
};

/* sd_ext tracks the kernel buffer and read position in it.  The interface
 * data is copied into a kernel buffer in subdomainfs and then handed off to
 * the activate routines.
 */
struct sd_ext {
	void *start;
	void *end;
	void *pos;	/* pointer to current position in the buffer */
	u32 version;
};

#endif /* __MODULEINTERFACE_H */
