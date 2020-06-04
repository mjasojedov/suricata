/* Copyright (C) 2020 Igor Mjasojedov
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Igor Mjasojedov <mjasojedov.igor13@gmail.com>
 */

#include "util-dpdk-common.h"
#include "suricata-common.h"
#include "conf.h"
#include "util-dpdk.h" 
#include "util-conf.h"
#include "util-device.h"
#include "util-cpu.h"

#ifdef HAVE_DPDK

#define DEFAULT_RX_RING_SIZE 1024
#define DEFAULT_TX_RING_SIZE 1024

int DpdkPortInit(int port, struct rte_mempool *mbuf_pool, uint16_t nb_queues)
{
    struct rte_eth_dev_info dev_info;
    struct rte_eth_conf port_conf = {
		.rxmode = {
			.mq_mode = ETH_MQ_RX_NONE,
			.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
		},
		.txmode = {
			.mq_mode = ETH_MQ_TX_NONE,
		},
	};

	int socket_id = rte_eth_dev_socket_id(port);
	uint16_t q_id;
	int retval;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

    rte_eth_dev_info_get(port, &dev_info);
    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    }

	retval = rte_eth_dev_configure(port, nb_queues, nb_queues, &port_conf);
	if (retval != 0)
		return retval;

	uint16_t nb_rxd = DEFAULT_RX_RING_SIZE;
	uint16_t nb_txd = DEFAULT_TX_RING_SIZE;
	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	for (q_id = 0; q_id < nb_queues; q_id++) {
		retval = rte_eth_rx_queue_setup(port, q_id, nb_rxd, socket_id, NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	for (q_id = 0; q_id < nb_queues; q_id++) {
		retval = rte_eth_tx_queue_setup(port, q_id, nb_txd, socket_id, NULL);
		if (retval < 0)
			return retval;
	}

	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	rte_eth_promiscuous_enable(port);

    return EXIT_SUCCESS;
}

#endif /* HAVE_DPDK */