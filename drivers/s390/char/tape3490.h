
/***************************************************************************
 *
 *  drivers/s390/char/tape3490.h
 *    tape device discipline for 3490E tapes.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Carsten Otte <cotte@de.ibm.com>
 *
 *  UNDER CONSTRUCTION: Work in progress...:-)
 ****************************************************************************
 */

#ifndef _TAPE3490_H

#define _TAPE3490_H


typedef struct _tape3490_disc_data_t {
    __u8 modeset_byte;
} tape3490_disc_data_t  __attribute__ ((packed, aligned(8)));
tape_discipline_t * tape3490_init (void);
#endif // _TAPE3490_H
