/* $Id: capifs.h,v 1.2.6.2 2001/09/23 22:24:33 kai Exp $
 * 
 * Copyright 2000 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

void capifs_new_ncci(char type, unsigned int num, dev_t device);
void capifs_free_ncci(char type, unsigned int num);
