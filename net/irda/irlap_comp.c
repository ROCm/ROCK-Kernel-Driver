/*********************************************************************
 *                
 * Filename:      irlap_comp.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Oct  9 09:18:07 1998
 * Modified at:   Tue Oct  5 11:34:52 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Fri May 28  3:11 CST 1999
 * Modified by:   Horst von Brand <vonbrand@sleipnir.valparaiso.cl>
 * Sources:       ppp.c, isdn_ppp.c
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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

#include <linux/string.h>

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>
#include <net/irda/irlap.h>
#include <net/irda/irlap_comp.h>
#include "../../drivers/net/zlib.h"

hashbin_t *irlap_compressors = NULL;

/*
 * Function irda_register_compressor (cp)
 *
 *    Register new compressor with the IrLAP
 *
 */
int irda_register_compressor( struct compressor *cp)
{
	struct irda_compressor *new;

	IRDA_DEBUG( 4, __FUNCTION__ "()\n");

	/* Check if this compressor has been registred before */
	if ( hashbin_find ( irlap_compressors, cp->compress_proto, NULL)) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), Compressor already registered\n");
                return 0;
        }
	
	/* Make new IrDA compressor */
        new = (struct irda_compressor *) 
		kmalloc( sizeof( struct irda_compressor), GFP_KERNEL);
        if (new == NULL)
                return 1;
		
	memset( new, 0, sizeof( struct irda_compressor));
        new->cp = cp;

	/* Insert IrDA compressor into hashbin */
	hashbin_insert( irlap_compressors, (irda_queue_t *) new, cp->compress_proto,
			NULL);
	
        return 0;
}

/*
 * Function irda_unregister_compressor (cp)
 *
 *    Unregister compressor
 *
 */
void irda_unregister_compressor ( struct compressor *cp)
{
	struct irda_compressor *node;

	IRDA_DEBUG( 4, __FUNCTION__ "()\n");

	node = hashbin_remove( irlap_compressors, cp->compress_proto, NULL);
	if ( !node) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), compressor not found!\n");
		return;
	}	
	kfree( node);
}

/*
 * Function irda_set_compression (self, proto)
 *
 *    The the compression protocol to be used by this session
 *
 */
int irda_set_compression( struct irlap_cb *self, int proto)
{
	struct compressor *cp;
	struct irda_compressor *comp;

	__u8 options[CILEN_DEFLATE];

	IRDA_DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return -ENODEV;);
	ASSERT( self->magic == LAP_MAGIC, return -EBADR;);

	/* Initialize options */
	options[0] = CI_DEFLATE;
	options[1] = CILEN_DEFLATE;
	options[2] = DEFLATE_METHOD( DEFLATE_METHOD_VAL);
	options[3] = DEFLATE_CHK_SEQUENCE;

	comp = hashbin_find( irlap_compressors, proto, NULL);
	if ( !comp) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), Unable to find compressor\n");
		return -1;
	}

	cp = comp->cp;
	/* 
	 *  Compressor part
	 */
	if ( self->compressor.state != NULL)
		(*self->compressor.cp->comp_free)( self->compressor.state);
	self->compressor.state = NULL;
	
	self->compressor.cp = cp;
	self->compressor.state = cp->comp_alloc( options, sizeof( options));
	if ( self->compressor.state == NULL) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), Failed!\n");
		return -ENOBUFS;
	}

	/* 
	 *  Decompress part
	 */
	
	if ( self->decompressor.state != NULL)
		irda_decomp_free( self->decompressor.state);
	self->decompressor.state = NULL;
	
	self->decompressor.cp = cp;
	self->decompressor.state = cp->decomp_alloc( options, sizeof( options));
	if ( self->decompressor.state == NULL) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), Failed!\n");
		return -ENOBUFS;
	}
	return 0;
}

/*
 * Function irda_free_compression (self)
 *
 *    
 *
 */
void irda_free_compression( struct irlap_cb *self)
{
	IRDA_DEBUG( 4, __FUNCTION__ "()\n");

	if ( self->compressor.state) {
		irda_comp_free( self->compressor.state);
		self->compressor.state = NULL;
	}
	
	if ( self->decompressor.state) {
		irda_decomp_free( self->decompressor.state);
		self->decompressor.state = NULL;
	}
}

/*
 * Function irlap_compress_init (self)
 *
 *    
 *
 */
void irlap_compressor_init( struct irlap_cb *self, int compress)
{
	int debug = TRUE;
	__u8 options[CILEN_DEFLATE];

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LAP_MAGIC, return;);
	
	/* Initialize options */
	options[0] = CI_DEFLATE;
	options[1] = CILEN_DEFLATE;
	options[2] = DEFLATE_METHOD_VAL;
	options[3] = DEFLATE_CHK_SEQUENCE;
	
	/*
	 *  We're agreeing to send compressed packets.
	 */
	if ( self->compressor.state == NULL) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), state == NULL\n");
		return;
	}
	
	if ((*self->compressor.cp->comp_init)( self->compressor.state, 
					       options, sizeof( options),
					       0, 0, debug)) 
	{
		IRDA_DEBUG( 0, __FUNCTION__ "(), Compressor running!\n");
		/* ppp->flags |= SC_COMP_RUN; */
	}
	
	/*
	 *  Initialize decompressor
	 */
	if ( self->decompressor.state == NULL) {
		IRDA_DEBUG( 0, __FUNCTION__ "(), state == NULL\n");
		return;
	}
	
	if (( self->decompressor.cp->decomp_init)( self->decompressor.state,
						   options, sizeof( options),
						   0, 0, 0, debug)) 
	{
		IRDA_DEBUG( 0, __FUNCTION__ "(), Decompressor running!\n");	
		
		/* ppp->flags |= SC_DECOMP_RUN; */
		/* ppp->flags &= ~(SC_DC_ERROR | SC_DC_FERROR); */
	}
}

/*
 * Function irlap_compress_frame (self, skb)
 *
 *
 *
 */
struct sk_buff *irlap_compress_frame( struct irlap_cb *self, 
				      struct sk_buff *skb)
{
	struct sk_buff *new_skb;
	int count;
	
	ASSERT( skb != NULL, return NULL;);
	
	IRDA_DEBUG(4, __FUNCTION__ "() skb->len=%d, jiffies=%ld\n", (int) skb->len,
	      jiffies);

	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == LAP_MAGIC, return NULL;);

	/* Check if compressor got initialized */
	if ( self->compressor.state == NULL) {
		/* Tell peer that this frame is not compressed */
		skb_push( skb, LAP_COMP_HEADER);
		skb->data[0] = IRDA_NORMAL;

		return skb;
	}

	/* FIXME: Find out what is the max overhead (not 10) */
	new_skb = dev_alloc_skb( skb->len+LAP_MAX_HEADER+10);
	if(!new_skb)
		return skb;

	skb_reserve( new_skb, LAP_MAX_HEADER);
	skb_put( new_skb, skb->len+10);
	
	count = (self->compressor.cp->compress)( self->compressor.state, 
						 skb->data, new_skb->data, 
						 skb->len, new_skb->len);
	if( count <= 0) {
		IRDA_DEBUG(4, __FUNCTION__ "(), Unable to compress frame!\n");
		dev_kfree_skb( new_skb);

		/* Tell peer that this frame is not compressed */
		skb_push( skb, 1);
		skb->data[0] = IRDA_NORMAL;

		return skb;
	}
	skb_trim( new_skb, count);

	/* Tell peer that this frame is compressed */
	skb_push( new_skb, 1);
	new_skb->data[0] = IRDA_COMPRESSED;

	dev_kfree_skb( skb);
	
	IRDA_DEBUG(4, __FUNCTION__ "() new_skb->len=%d\n, jiffies=%ld", 
	      (int) new_skb->len, jiffies);
	
	return new_skb;
}

/*
 * Function irlap_decompress_frame (self, skb)
 *
 *    
 *
 */
struct sk_buff *irlap_decompress_frame( struct irlap_cb *self, 
					struct sk_buff *skb)
{
	struct sk_buff *new_skb;
	int count;

	IRDA_DEBUG( 4, __FUNCTION__ "() skb->len=%d\n", (int) skb->len);

	ASSERT( self != NULL, return NULL;);
	ASSERT( self->magic == LAP_MAGIC, return NULL;);

	ASSERT( self->compressor.state != NULL, return NULL;);

	/* Check if frame is compressed */
	if ( skb->data[0] == IRDA_NORMAL) {

		/* Remove compression header */
		skb_pull( skb, LAP_COMP_HEADER);

		/*
		 * The frame is not compressed. Pass it to the
		 * decompression code so it can update its
		 * dictionary if necessary.
		 */
		irda_incomp( self->decompressor.state, skb->data, skb->len);

		return skb;
	}

	/* Remove compression header */
	skb_pull( skb, LAP_COMP_HEADER);

	new_skb = dev_alloc_skb( 2048); /* FIXME: find the right size */
	if(!new_skb)
		return skb;
	skb_put( new_skb, 2048);

	count = irda_decompress( self->decompressor.state, skb->data, 
				 skb->len, new_skb->data, new_skb->len);
	if ( count <= 0) {
		IRDA_DEBUG( 4, __FUNCTION__ "(), Unable to decompress frame!\n");
		
		dev_kfree_skb( new_skb);
		return skb;
	}

	skb_trim( new_skb, count);
	
	IRDA_DEBUG( 4, __FUNCTION__ "() new_skb->len=%d\n", (int) new_skb->len);

	return new_skb;
}

