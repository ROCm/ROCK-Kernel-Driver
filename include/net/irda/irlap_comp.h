/*********************************************************************
 *                
 * Filename:      irlap_comp.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Oct  9 09:21:12 1998
 * Modified at:   Sat Dec 12 12:23:16 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef IRLAP_COMP_H
#define IRLAP_COMP_H

#include <linux/ppp-comp.h>

#define CI_BZIP2  27 /* Random pick */

extern hashbin_t *irlap_compressors;

int irda_register_compressor( struct compressor *cp);
void irda_unregister_compressor( struct compressor *cp);

int irda_set_compression( struct irlap_cb *self, int proto);
void irlap_compressor_init( struct irlap_cb *self, int compress);
void irda_free_compression( struct irlap_cb *self);

struct sk_buff *irlap_compress_frame( struct irlap_cb *self, 
				      struct sk_buff *skb);
struct sk_buff *irlap_decompress_frame( struct irlap_cb *self, 
					struct sk_buff *skb);

#endif

