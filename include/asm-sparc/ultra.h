/* $Id: ultra.h,v 1.2 1995/11/25 02:33:10 davem Exp $
 * ultra.h: Definitions and defines for the TI V9 UltraSparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_ULTRA_H
#define _SPARC_ULTRA_H

/* Spitfire MMU control register:
 *
 * ----------------------------------------------------------
 * |        | IMPL  | VERS  |     |  MID  |                 |
 * ----------------------------------------------------------
 *  64        31-28   27-24  23-22  21-17   16             0
 *
 * IMPL: Implementation of this Spitfire.
 * VERS: Version of this Spitfire.
 * MID: Module ID of this processor.
 */

#define SPITFIRE_MIDMASK     0x00000000003e0000

/* Spitfire Load Store Unit control register:
 *
 * ---------------------------------------------------------------------
 * | RSV | PWR | PWW | VWR | VWW | RSV | PMASK | DME | IME | DCE | ICE |
 * ---------------------------------------------------------------------
 *  63-25  24    23     22    21    20   19-4      3     2     1     0
 *
 * PWR: Physical Watchpoint Read enable: 0=off 1=on
 * PWW: Physical Watchpoint Write enable: 0=off 1=on
 * VWR: Virtual Watchpoint Read enable: 0=off 1=on
 * VWW: Virtual Watchpoint Write enable: 0=off 1=on
 * PMASK: Parity MASK  ???
 * DME: Data MMU Enable: 0=off 1=on
 * IME: Instruction MMU Enable: 0=off 1=on
 * DCE: Data Cache Enable: 0=off 1=on
 * ICE: Instruction Cache Enable: 0=off 1=on
 */

#define SPITFIRE_LSU_PWR      0x01000000
#define SPITFIRE_LSU_PWW      0x00800000
#define SPITFIRE_LSU_VWR      0x00400000
#define SPITFIRE_LSU_VWW      0x00200000
#define SPITFIRE_LSU_PMASK    0x000ffff0
#define SPITFIRE_LSU_DME      0x00000008
#define SPITFIRE_LSU_IME      0x00000004
#define SPITFIRE_LSU_DCE      0x00000002
#define SPITFIRE_LSU_ICE      0x00000001

#endif /* !(_SPARC_ULTRA_H) */
