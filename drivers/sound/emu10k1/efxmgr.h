/*     
 **********************************************************************
 *     sblive_fx.h
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     Date                 Author          Summary of changes 
 *     ----                 ------          ------------------ 
 *     October 20, 1999     Bertrand Lee    base code release 
 * 
 ********************************************************************** 
 * 
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version. 
 * 
 *     This program is distributed in the hope that it will be useful, 
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *     GNU General Public License for more details. 
 * 
 *     You should have received a copy of the GNU General Public 
 *     License along with this program; if not, write to the Free 
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, 
 *     USA. 
 * 
 ********************************************************************** 
 */

#ifndef _EFXMGR_H
#define _EFXMGR_H

#define WRITE_EFX(a, b, c) sblive_writeptr((a), MICROCODEBASE + (b), 0, (c))

#define OP(op, z, w, x, y) \
        do { WRITE_EFX(card, (pc) * 2, ((x) << 10) | (y)); \
        WRITE_EFX(card, (pc) * 2 + 1, ((op) << 20) | ((z) << 10) | (w)); \
        ++pc; } while (0)


#endif /* _EFXMGR_H */
