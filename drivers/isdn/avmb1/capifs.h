/*
 * $Id: capifs.h,v 1.2.6.1 2001/05/17 20:41:51 kai Exp $
 * 
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 */

void capifs_new_ncci(char type, unsigned int num, kdev_t device);
void capifs_free_ncci(char type, unsigned int num);
