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


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
//#define NUM_MBUFS 8191
#define NUM_MBUFS 64*1024
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mq_mode = ETH_MQ_RX_NONE,
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,
    },
};

int DpdkPortInit_new(int port, struct rte_mempool *mbuf_pool, uint16_t nb_queues)
{
    // struct rte_eth_conf port_conf = port_conf_default;
    // uint16_t rx_rings, tx_rings; // initialized by configuration in .yaml
    // uint16_t nb_rxd = DEFAULT_RX_RING_SIZE;
    // uint16_t nb_txd = DEFAULT_TX_RING_SIZE;
    // int retval;
    // uint16_t q;
    // struct rte_eth_dev_info dev_info;
    // struct rte_eth_txconf txconf;

    // if (!rte_eth_dev_is_valid_port(port)) {
    //     return -1;
    // }

    // rte_eth_dev_info_get(port, &dev_info);

    // if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
    //     port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    // }

    // // todo
    // /* Get threads/queues configuration form suricata.yaml file. */
    // // if(SetRingsCount(&rx_rings, &tx_rings, (void *)&dev_info) != EXIT_SUCCESS) {
    // //     SCLogInfo("Unable to set rings count.");
    // //     return EXIT_FAILURE;
    // // }

    // /* Configure the Ethernet device. */
    // retval = rte_eth_dev_configure(port, nb_queues, nb_queues, &port_conf);
    // if (retval != 0) {
    //     return retval;
    // }

    // retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    // if (retval != 0) {
    //     return retval;
    // }

    // /* Allocate and set up 1 RX queue per Ethernet port. */
    // for (q = 0; q < nb_queues; q++) {
    //     retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
    //             rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    //     if (retval < 0) {
    //         return retval;
    //     }
    // }

    // txconf = dev_info.default_txconf;
    // txconf.offloads = port_conf.txmode.offloads;

    // /* Allocate and set up 1 TX queue per Ethernet port. */
    // for (q = 0; q < nb_queues; q++) {
    //     retval = rte_eth_tx_queue_setup(port, q, nb_txd,
    //             rte_eth_dev_socket_id(port), &txconf);
    //     if (retval < 0) {
    //         return retval;
    //     }
    // }

    // /* Start the Ethernet port. */
    // retval = rte_eth_dev_start(port);
    // if (retval < 0) {
    //     return retval;
    // }

    // rte_eth_promiscuous_enable(port);

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

    // txconf = dev_info.default_txconf;
	// txconf.offloads = port_conf.txmode.offloads;
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

/*****************************************************************/
/*****************************************************************/







/* Prototypes */
int DpdkPortInit(int port, struct rte_mempool *mbuf_pool);
// int DpdkPortSetup(void);


/**
 *  \brief  Get count of queues from suricata.yaml configuration
 *          and set the size of rx/tx_rings.
 * 
 *  \param  rx_rings    pointer to receive rings/queues
 *  \param  tx_rings    pointer to transmit rings/queues
 *  \param  dev_info    pointer to structure of information about NIC port
 * 
 */
int SetRingsCount(uint16_t *rx_rings, uint16_t *tx_rings, 
                  void *device_info)
{
    ConfNode *dpdk_node;
    ConfNode *if_root = NULL;
    const char *threadsstr = NULL;
    uint16_t threads_count;
    uint16_t maximum_theards;
    struct rte_eth_dev_info *dev_info = (struct rte_eth_dev_info *)device_info;

    dpdk_node = ConfGetNode("dpdk");
    if (dpdk_node == NULL) {
        SCLogWarning(SC_ERR_CONF_YAML_ERROR, "Unable to find 'dpdk' config in .yaml configuration file. "
                     "Using default value for threads count.");
        threads_count = UtilCpuGetNumProcessorsOnline() * threading_detect_ratio;
    } else {
        if_root = ConfFindDeviceConfig(dpdk_node, "default");
        if (ConfGetChildValueWithDefault(if_root, NULL, "threads", &threadsstr) != 1) {
            SCLogError(SC_ERR_CONF_YAML_ERROR, "'threads' configuration in 'dpdk'"
                                            "section not found!");
            return EXIT_FAILURE;
        } else {
            if (threadsstr != NULL) {
                if (strcmp(threadsstr, "auto") == 0) {
                    threads_count = UtilCpuGetNumProcessorsOnline() * threading_detect_ratio;
                } else {
                    threads_count = atoi(threadsstr);
                }
            }
        }
    }

    // maximum_theards = (dev_info->max_rx_queues < dev_info->max_tx_queues)
    //                                             ? dev_info->max_rx_queues
    //                                             : dev_info->max_tx_queues;

    maximum_theards = RTE_MIN(dev_info->max_rx_queues, dev_info->max_tx_queues);

    if(maximum_theards < threads_count) {
        SCLogWarning(SC_ERR_CONF_YAML_ERROR, "'threads' count configuration in 'dpdk'"
                                           "section is too high! Maximum allowed threads for" 
                                           "the port is %d", maximum_theards);
        threads_count = maximum_theards;
    }

    *rx_rings = threads_count;
    *tx_rings = threads_count;
    
    return EXIT_SUCCESS;
}

int DpdkPortInit(int port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = port_conf_default;
    uint16_t rx_rings, tx_rings; // initialized by configuration in .yaml
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port)) {
        return -1;
    }

    rte_eth_dev_info_get(port, &dev_info);

    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    // todo
    /* Get threads/queues configuration form suricata.yaml file. */
    if(SetRingsCount(&rx_rings, &tx_rings, (void *)&dev_info) != EXIT_SUCCESS) {
        SCLogInfo("Unable to set rings count.");
        return EXIT_FAILURE;
    }

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0) {
        return retval;
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) {
        return retval;
    }

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0) {
            return retval;
        }
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;

    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0) {
            return retval;
        }
    }

    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        return retval;
    }

    rte_eth_promiscuous_enable(port);
    
    return EXIT_SUCCESS;
}

int DpdkPortSetup(void)
{
    struct rte_mempool *mbuf_pool;
    int portid;
    unsigned nb_ports;

    nb_ports = rte_eth_dev_count_avail();
    if(nb_ports != 1) {
        return EXIT_FAILURE;
    }

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS , //128 * 1024
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id()); 
    if (mbuf_pool == NULL) {
        SCLogError(SC_ERR_GID_FAILED, "DPDK mbuf_pool creation failed.");
        return EXIT_FAILURE;
    }

    RTE_ETH_FOREACH_DEV(portid) {
        if (DpdkPortInit(portid, mbuf_pool) != 0) {
            SCLogError(SC_ERR_GID_FAILED, "DPDK port initialization failed.");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}


TmEcode RegisterDpdkPort(void) 
{
    char port_name[RTE_ETH_NAME_MAX_LEN];
    struct rte_eth_dev_info dev_info;
    LiveDevice *live_port;

    if (DpdkPortSetup() != EXIT_SUCCESS) {
        return TM_ECODE_FAILED;
    }
    SCLogInfo("DPDK port initialized.");
    
    if(rte_eth_dev_get_name_by_port(0, (char *)port_name) != 0) {
        exit(TM_ECODE_FAILED);
    }
    
    rte_eth_dev_info_get(0, &dev_info);

    LiveRegisterDevice(port_name);
    live_port = LiveGetDevice(port_name);
    SCLogInfo("DPDK port initialized.");
    live_port->queues_count = dev_info.nb_rx_queues;   //->nb_rx_queues == ->nb_tx_queues
    
    SCLogDebug("DPDK Port \"%s\" registered as LiveDevice.", port_name);

    return TM_ECODE_OK;
}

#endif