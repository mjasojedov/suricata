#include "util-dpdk-common.h"
#include "util-dpdk.h" 

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
};

int dpdk_argv_count = 1;
char dpdk_argv[32][50] = {{"suricata"}};

/* Prototypes */
int DpdkPortInit(int port, struct rte_mempool *mbuf_pool);

// int ParseDpdkConfig(void)
// {
//     SCEnter();
//     struct rte_cfgfile *file = rte_cfgfile_load("dpdk.cfg", 0);
//     if(file == NULL) {
//         //error opening config file
//     }

//     /* Parse EAL parameters from configuration file */
//     if(rte_cfgfile_has_section(file, "EAL")) {
//         int num_entries =  rte_cfgfile_section_num_entries(file, "EAL");
//         struct rte_cfgfile_entry entries[num_entries];
//         SCLogNotice(" DPDK configuration | EAL section: %d entries", num_entries);

//         if(rte_cfgfile_section_entries(file, "EAL", entries, num_entries) != -1) {
//             for(int i = 0; i < num_entries; i++) {
//                 snprintf(dpdk_argv[i * 2 + 1], 50, "%s", entries[i].name);
// 				snprintf(dpdk_argv[i * 2 + 2], 50, "%s", entries[i].value);
// 				SCLogDebug(" - argument: (%s) (%s)", dpdk_argv[i * 2 + 1], dpdk_argv[i * 2 + 2]);
// 			        dpdk_argv_count += (((entries[i].name) ? 1 : 0) + ((entries[i].value) ? 1 : 0));
//             }
//         }
//     }

//     rte_cfgfile_close(file);
//     return EXIT_SUCCESS;
// }

int DpdkPortInit(int port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = 1;
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
    if (retval != 0) {
        printf("Error during getting device (port %u) info: %s\n",
                port, strerror(-retval));
        return retval;
    }
    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |=
            DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;
    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0)
            return retval;
    }
    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0)
            return retval;
    }
    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;
    rte_eth_promiscuous_enable(port);
    if (retval != 0)
        return retval;
    return 0;
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

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
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