#ifndef __UTIL_DPDK_H__
#define __UTIL_DPDK_H__

#include "tm-threads-common.h"

int DpdkPortInit_new(int port, struct rte_mempool *mbuf_pool, uint16_t nb_queues);

int DpdkPortSetup(void);
TmEcode RegisterDpdkPort(void);
int SetRingsCount(uint16_t *rx_rings, uint16_t *tx_rings, 
                  void *dev_info);



#endif