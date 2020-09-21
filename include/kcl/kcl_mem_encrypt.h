/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_MEM_ENCRYPT_H
#define KCL_KCL_MEM_ENCRYPT_H

#ifdef HAVE_LINUX_MEM_ENCRYPT_H
#include <linux/mem_encrypt.h>
#ifndef HAVE_MEM_ENCRYPT_ACTIVE
static inline bool mem_encrypt_active(void)
{
	return sme_me_mask;
}
#endif
#else
static inline bool mem_encrypt_active(void)
{
    return false;
}
#endif
#endif
