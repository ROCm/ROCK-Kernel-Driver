/*
 *	cs4232.h
 *
 * Copyright: Christoph Hellwig <chhellwig@gmx.net>
 *
 */

int probe_cs4232 (struct address_info *hw_config);
void attach_cs4232 (struct address_info *hw_config);
int probe_cs4232_mpu (struct address_info *hw_config);
void attach_cs4232_mpu (struct address_info *hw_config);
