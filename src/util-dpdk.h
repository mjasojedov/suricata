#ifndef __UTIL_DPDK_H__
#define __UTIL_DPDK_H__

#include "tm-threads-common.h"

int DpdkPortSetup(void);
TmEcode RegisterDpdkPort(void);
int SetRingsCount(uint16_t *rx_rings, uint16_t *tx_rings, 
                  void *dev_info);

#endif