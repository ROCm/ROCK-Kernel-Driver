/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: irSect.h
 *    
 *    Description:
 *      Defines variables to hold the absolute starting address and size
 *      of the Linux kernel "image" and the initial RAM disk "initrd"   
 *      sections within the boot loader.
 *      
 */

#ifndef __IRSECT_H__
#define __IRSECT_H__

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int imageSect_start;
extern unsigned int imageSect_size;

extern unsigned int initrdSect_start;
extern unsigned int initrdSect_size;


#ifdef __cplusplus
}
#endif

#endif /* __IRSECT_H__ */
