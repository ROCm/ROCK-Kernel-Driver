/*
 * tonga_asic_capability.h
 *
 *  Created on: 2016-01-18
 *      Author: qyang
 */

#ifndef TONGA_ASIC_CAPABILITY_H_
#define TONGA_ASIC_CAPABILITY_H_

/* Forward declaration */
struct asic_capability;

/* Create and initialize Carrizo data */
void tonga_asic_capability_create(struct asic_capability *cap,
	struct hw_asic_id *init);

#endif /* TONGA_ASIC_CAPABILITY_H_ */
