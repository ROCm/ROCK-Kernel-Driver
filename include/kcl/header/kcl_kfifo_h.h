/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_KFIFO_H_H
#define AMDKCL_KFIFO_H_H

#if defined(HAVE_LINUX_KFIFO_NEW_H)
#include <linux/kfifo-new.h>
#else
#include <linux/kfifo.h>
#endif
#endif
