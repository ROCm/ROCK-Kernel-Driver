/*
 * dce100_resource.h
 *
 *  Created on: 2016-01-20
 *      Author: qyang
 */

#ifndef DCE100_RESOURCE_H_
#define DCE100_RESOURCE_H_

struct adapter_service;
struct core_dc;
struct resource_pool;
struct dc_validation_set;

bool dce100_construct_resource_pool(
	struct adapter_service *adapter_serv,
	uint8_t num_virtual_links,
	struct core_dc *dc,
	struct resource_pool *pool);

void dce100_destruct_resource_pool(struct resource_pool *pool);

#endif /* DCE100_RESOURCE_H_ */
