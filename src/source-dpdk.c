#include "util-dpdk-common.h"
#include "source-dpdk.h"
#include "util-dpdk.h"

#include "suricata-common.h"
#include "suricata.h"
#include "host.h"
#include "decode.h"
#include "packet-queue.h"
#include "threads.h"
#include "threadvars.h"
#include "tm-queuehandlers.h"
#include "tm-threads.h"
#include "tm-threads-common.h"
#include "conf.h"
#include "util-debug.h"
#include "util-error.h"
#include "util-privs.h"
#include "util-device.h"
#include "util-mem.h"
#include "util-profiling.h"
#include "tmqh-packetpool.h"
#include "pkt-var.h"

#ifdef HAVE_DPDK

#define DEFAULT_MEMPOOL_SIZE 64*1024
#define DEFAULT_MEMPOOL_CACHE_SIZE 250
#define DEFAULT_PKT_SIZE 1518
#define BURST_SIZE 32 // todo

typedef enum DPDKMode_ {
	ETHDEV_MODE = 0,
	ETHDEV_RING_MODE = 1,
	NATIVE_RING_MODE = 2,
	__MODES_COUNT,
} DPDKMode;

struct context {
	DPDKMode mode;
	int socket_id;
	uint16_t port_id;
	uint16_t nb_queues;
	const char *port_name;
	struct rte_mempool *pool;
	struct rte_ring **rx_rings;
	struct rte_ring **tx_rings;
};

/**
 * \brief Structure to hold thread specific variables.
 */
typedef struct DpdkTheadVars_
{
    /* counters */
    uint64_t pkts;
    uint64_t bytes;
    uint64_t accepted;
    uint64_t dropped;
    
    const char port[RTE_ETH_NAME_MAX_LEN];

    uint16_t queue_num;

    struct rte_mbuf *ReceivedMbufs[BURST_SIZE];
    struct rte_mbuf *AcceptedMbufs[BURST_SIZE];
    struct rte_mbuf *DeclinedMbufs[BURST_SIZE];

    int AcceptedIndex;
    int DeclinedIndex;

    ThreadVars *tv;
    TmSlot *slot;
    /* If possible livedevice the capture thread is rattached to. */
    LiveDevice *livedev;
} DpdkTheadVars;

struct context *dpdk_config;
static DpdkTheadVars *dpdk_th_vars;

int DPDKInitConfig(void);
void DPDKAllocateThreadVars(void);
void DPDKContextsClean(void);

/* **************************** Setup Functions **************************** */
int DPDKInitConfig(void) 
{
    int retval = EXIT_SUCCESS;
    // Get dpdk mode from .yaml configuration file
    const char *strMode = NULL;
    const char *strExternPort = NULL;
    
    int processType = rte_eal_process_type();
    dpdk_config = SCMalloc(sizeof(struct context));
    if (unlikely(dpdk_config == NULL)) {
        SCReturnInt(TM_ECODE_FAILED);
    }
    memset(dpdk_config, 0, sizeof(struct context));


    if (ConfGetValue("dpdk.mode", &strMode) != 1) {
        dpdk_config->mode = ETHDEV_MODE; // default value
    } else if (!strcmp(strMode, "ethdev") && processType == RTE_PROC_PRIMARY) {
        dpdk_config->mode = ETHDEV_MODE;
    } else if (!strcmp(strMode, "ethdev-ring") && processType != RTE_PROC_PRIMARY) {
        dpdk_config->mode = ETHDEV_RING_MODE;
    } else if (!strcmp(strMode, "native-ring") && processType != RTE_PROC_PRIMARY) {
        dpdk_config->mode = NATIVE_RING_MODE;
    } else {
        //error message
        rte_eal_cleanup();
        exit(EXIT_FAILURE);
    }
 
    dpdk_config->port_id = 0;
    dpdk_config->port_name = (ConfGetValue("dpdk.port-extern", &strExternPort) != 1)
                                ? "suricata"
                                : strExternPort;
    dpdk_config->socket_id = rte_socket_id();
	dpdk_config->nb_queues = rte_lcore_count() - 1; // minus master core

    // struct rte_ring *rx_rings[dpdk_config->nb_queues];
	// struct rte_ring *tx_rings[dpdk_config->nb_queues];

    dpdk_config->rx_rings = SCMalloc(sizeof(struct rte_ring *) * dpdk_config->nb_queues);
    if (unlikely(dpdk_config->rx_rings == NULL)) {
        SCReturnInt(TM_ECODE_FAILED);
    }
    memset(dpdk_config->rx_rings, 0, sizeof(struct rte_ring *) * dpdk_config->nb_queues);

    dpdk_config->tx_rings = SCMalloc(sizeof(struct rte_ring *) * dpdk_config->nb_queues);
    if (unlikely(dpdk_config->tx_rings == NULL)) {
        SCReturnInt(TM_ECODE_FAILED);
    }
    memset(dpdk_config->tx_rings, 0, sizeof(struct rte_ring *) * dpdk_config->nb_queues);

	// dpdk_config->rx_rings = rx_rings;
	// dpdk_config->tx_rings = tx_rings;

    if (dpdk_config->mode == ETHDEV_RING_MODE || dpdk_config->mode == NATIVE_RING_MODE) {
		uint16_t i;
		for (i = 0; i < dpdk_config->nb_queues; i++) {
			char name[64];

			snprintf(name, sizeof(name), "port_%s_tx%" PRIu16, dpdk_config->port_name, i);
			dpdk_config->rx_rings[i] = rte_ring_lookup(name);
			if (dpdk_config->rx_rings[i] == NULL) {
                SCLogInfo("rte_ring_lookup(): cannot get rx ring '%s'\n", name);
				retval = EXIT_FAILURE;
				//goto rxtx_rings_fail;
                SCReturnInt(EXIT_FAILURE);
			}

			snprintf(name, sizeof(name), "port_%s_rx%" PRIu16, dpdk_config->port_name, i);
			dpdk_config->tx_rings[i] = rte_ring_lookup(name);
			if (dpdk_config->tx_rings[i] == NULL) {
                SCLogInfo("rte_ring_lookup(): cannot get tx ring '%s'\n", name);
				retval = EXIT_FAILURE;
				//goto rxtx_rings_fail;
                SCReturnInt(EXIT_FAILURE);
			}
		}
	}

    if (dpdk_config->mode == ETHDEV_MODE) {
		dpdk_config->pool = rte_pktmbuf_pool_create("pktmbuf_pool", DEFAULT_MEMPOOL_SIZE,
				DEFAULT_MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, dpdk_config->socket_id);

		if (dpdk_config->pool == NULL) {
            SCLogInfo("rte_pktmbuf_pool_create() has failed");
			retval = EXIT_FAILURE;
			//goto mempool_fail;
            SCReturnInt(EXIT_FAILURE);
		}

	} else if (dpdk_config->mode == ETHDEV_RING_MODE) {
		char name[64];
		snprintf(name, sizeof(name), "port_%s", dpdk_config->port_name);
		dpdk_config->pool = rte_mempool_lookup(name);
		if (dpdk_config->pool == NULL) {
            SCLogInfo("rte_mempool_lookup(): cannot get mempool '%s'", name);
			retval = EXIT_FAILURE;
            //goto mempool_fail;
			SCReturnInt(EXIT_FAILURE);
		}

		dpdk_config->port_id = rte_eth_from_rings("extern_port",
                                dpdk_config->rx_rings,
                                dpdk_config->nb_queues,
                                dpdk_config->tx_rings,
                                dpdk_config->nb_queues,
                                dpdk_config->socket_id);
	}

    if (dpdk_config->mode == ETHDEV_MODE || dpdk_config->mode == ETHDEV_RING_MODE) {
		if (DpdkPortInit_new(dpdk_config->port_id, dpdk_config->pool, dpdk_config->nb_queues) != 0) {
            SCLogInfo("ethdev_init(): cannot init port %" PRIu16 "", dpdk_config->port_id);
            retval = EXIT_FAILURE;
            SCReturnInt(EXIT_FAILURE);
		}
	}

    SCReturnInt(retval);

}

void DPDKAllocateThreadVars(void)
{
    void *ptr = SCRealloc(dpdk_th_vars, (rte_lcore_count() - 1) * sizeof(DpdkTheadVars));
    if (ptr == NULL) {
        SCLogError(SC_ERR_MEM_ALLOC, "Unable to allocate DPDKThreadVars");
        DPDKContextsClean();
        exit(EXIT_FAILURE);
    }

    dpdk_th_vars = (DpdkTheadVars *)ptr;
}

/**
 * \brief Clean global contexts. Must be called on exit.
 */
void DPDKContextsClean(void)
{
    if (dpdk_th_vars != NULL) {
        SCFree(dpdk_th_vars);
        dpdk_th_vars = NULL;
    }
}

void DpdkClean()
{
    SCEnter();
    if (dpdk_config->mode == ETHDEV_MODE || dpdk_config->mode == ETHDEV_RING_MODE) {
		rte_eth_dev_close(dpdk_config->port_id);
	}

	if (dpdk_config->mode == ETHDEV_MODE)
		rte_mempool_free(dpdk_config->pool);

	rte_eal_cleanup();

}


/* ****************************** Prototypes ******************************* */
TmEcode ReceiveDpdkInit(ThreadVars *tv, const void *initdata, void **data);
void ReceiveDpdkThreadExitStats(ThreadVars *tv, void *data);
TmEcode ReceiveDpdkDeinit(ThreadVars *tv, void *data);
TmEcode ReceiveDpdkLoop(ThreadVars *tv, void *data, void *slot);

TmEcode DecodeDpdkInit(ThreadVars *tv, const void *initdata, void **data);
TmEcode DecodeDpdkDeinit(ThreadVars *tv, void *data);
TmEcode DecodeDpdk(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                   PacketQueue *postq);

TmEcode VerdictDpdkInit(ThreadVars *tv, const void *initdata, void **data);
TmEcode VerdictDpdkDeinit(ThreadVars *tv, void *data);
TmEcode DPDKSetVerdict(ThreadVars *tv, DpdkTheadVars *DpdkTv, Packet *p);
TmEcode VerdictDpdk(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                   PacketQueue *postq);
void VerdictDPDKThreadExitStats(ThreadVars *tv, void *data);
/* ************************************************************************* */

void TmModuleReceiveDpdkRegister(void)
{
    SCEnter();

    tmm_modules[TMM_RECEIVEDPDK].name = "ReceiveDPDK";
	tmm_modules[TMM_RECEIVEDPDK].ThreadInit = ReceiveDpdkInit;
	tmm_modules[TMM_RECEIVEDPDK].Func = NULL;
	tmm_modules[TMM_RECEIVEDPDK].PktAcqLoop = ReceiveDpdkLoop;
	tmm_modules[TMM_RECEIVEDPDK].PktAcqBreakLoop = NULL;
	tmm_modules[TMM_RECEIVEDPDK].ThreadExitPrintStats = ReceiveDpdkThreadExitStats;
	tmm_modules[TMM_RECEIVEDPDK].ThreadDeinit = ReceiveDpdkDeinit;
	tmm_modules[TMM_RECEIVEDPDK].RegisterTests = NULL;
	tmm_modules[TMM_RECEIVEDPDK].cap_flags = SC_CAP_NET_RAW;
	tmm_modules[TMM_RECEIVEDPDK].flags = TM_FLAG_RECEIVE_TM;

	SCReturn;
}

void TmModuleDecodeDpdkRegister(void)
{
    SCEnter();

    tmm_modules[TMM_DECODEDPDK].name = "DecodeDPDK";
	tmm_modules[TMM_DECODEDPDK].ThreadInit = DecodeDpdkInit;
	tmm_modules[TMM_DECODEDPDK].Func = DecodeDpdk;
	tmm_modules[TMM_DECODEDPDK].PktAcqLoop = NULL;
	tmm_modules[TMM_DECODEDPDK].PktAcqBreakLoop = NULL;
	tmm_modules[TMM_DECODEDPDK].ThreadExitPrintStats = NULL;
	tmm_modules[TMM_DECODEDPDK].ThreadDeinit = DecodeDpdkDeinit;
	tmm_modules[TMM_DECODEDPDK].RegisterTests = NULL;
	tmm_modules[TMM_DECODEDPDK].cap_flags = 0;
	tmm_modules[TMM_DECODEDPDK].flags = TM_FLAG_DECODE_TM;

	SCReturn;
}

void TmModuleVerdictDpdkRegister(void)
{
    SCEnter();

    tmm_modules[TMM_VERDICTDPDK].name = "VerdictDPDK";
	tmm_modules[TMM_VERDICTDPDK].ThreadInit = VerdictDpdkInit;
	tmm_modules[TMM_VERDICTDPDK].Func = VerdictDpdk;
	tmm_modules[TMM_VERDICTDPDK].PktAcqLoop = NULL;
	tmm_modules[TMM_VERDICTDPDK].PktAcqBreakLoop = NULL;
	tmm_modules[TMM_VERDICTDPDK].ThreadExitPrintStats = VerdictDPDKThreadExitStats;
	tmm_modules[TMM_VERDICTDPDK].ThreadDeinit = VerdictDpdkDeinit;
	tmm_modules[TMM_VERDICTDPDK].RegisterTests = NULL;
	tmm_modules[TMM_VERDICTDPDK].cap_flags = 0;

    SCReturn;
}


/* ==================================================================== */
/* ************************** Receive module ************************** */

TmEcode ReceiveDpdkInit(ThreadVars *tv, const void *initdata, void **data)
{
    SCEnter();
    // DpdkTheadVars *DpdkTv = SCMalloc(sizeof(DpdkTheadVars));
    // if (unlikely(DpdkTv == NULL)) {
    //     SCReturnInt(TM_ECODE_FAILED);
    // }
    // memset(DpdkTv, 0, sizeof(DpdkTheadVars));
 
    //DpdkIfaceConfig *aconf = (DpdkIfaceConfig *)initdata;
    DpdkTheadVars *DpdkTv =  &dpdk_th_vars[tv->ring_id];
    DpdkTv->tv = tv;
    DpdkTv->pkts = 0;
    DpdkTv->bytes = 0;
    // rte_eth_dev_get_name_by_port(0, (char *)DpdkTv->port); //todo
    // DpdkTv->livedev = LiveGetDevice(DpdkTv->port);

    //DpdkTv->livedev = aconf->live_dev;
    //DpdkTv->queue_num = aconf->queue_num;
    DpdkTv->queue_num = tv->ring_id;

    *data = (void *)DpdkTv;
    SCReturnInt(TM_ECODE_OK);
}

TmEcode ReceiveDpdkDeinit(ThreadVars *tv, void *data)
{
    SCEnter();

    SCReturnInt(TM_ECODE_OK);
}

TmEcode ReceiveDpdkLoop(ThreadVars *tv, void *data, void *slot)
{
    SCEnter();

    uint16_t port;
    struct rte_mbuf *tmpMbuf;
    void *pktAddress;
    DpdkTheadVars *DpdkTv = (DpdkTheadVars *)data;
    TmSlot *s = (TmSlot *)slot;
    Packet *p;
    struct rte_mbuf *bufs[BURST_SIZE];
	uint64_t counter = 0;
    uint16_t nb_rx, nb_tx;

    struct rte_ring *rx_ring = dpdk_config->rx_rings[tv->ring_id];
    struct rte_ring *tx_ring = dpdk_config->tx_rings[tv->ring_id];


    DpdkTv->slot = s->slot_next;

    /* Run until the application is quit or killed. */
    for (;;) {
        if (unlikely(suricata_ctl_flags & (SURICATA_STOP))) {
            SCLogDebug("Received STOP Signal!");
    		SCLogNotice("%lu", counter);
            SCReturnInt(TM_ECODE_OK);
        }
        
        if (dpdk_config->mode == NATIVE_RING_MODE) {
            //nb_rx = rte_ring_dequeue_burst(dpdk_config->rx_rings[DpdkTv->queue_num], (void **) bufs, BURST_SIZE, NULL);
            nb_rx = rte_ring_dequeue_burst(rx_ring, (void **) DpdkTv->ReceivedMbufs, BURST_SIZE, NULL);
        } else {
            nb_rx = rte_eth_rx_burst(dpdk_config->port_id, tv->ring_id, (void **) DpdkTv->ReceivedMbufs, BURST_SIZE);
        }

        if (unlikely(nb_rx == 0)) {
            continue;
        } 
        DpdkTv->pkts += (uint64_t) nb_rx;
        
        // // WORKING!
        // for(int i=0; i<nb_rx; i++) {
        //     nb_tx = rte_eth_tx_burst(0, 0, (void**)DpdkTv->ReceivedMbufs, 1);
        //     if (nb_tx != 1) {
        //         rte_pktmbuf_free(DpdkTv->ReceivedMbufs[i]);
        //     }
        // }
        // continue;
        // // ------------------
        
        for(int i=0; i < nb_rx; i++) {
            tmpMbuf = DpdkTv->ReceivedMbufs[i];
            //tmpMbuf = bufs[i];
            p = PacketGetFromQueueOrAlloc();
            if(unlikely(p == NULL)) {
                SCLogError(SC_ERR_MEM_ALLOC, "Failed to get Packet Buffer for DPDK mbuff!");
                SCReturnInt(1);
            }
            PKT_SET_SRC(p, PKT_SRC_WIRE);
            p->datalink = LINKTYPE_ETHERNET;
            gettimeofday(&p->ts, NULL);
            p->DpdkMBufPtr = DpdkTv->ReceivedMbufs;
            p->mbufIndex = i;
            pktAddress = tmpMbuf->buf_addr + rte_pktmbuf_headroom(tmpMbuf);
            if(PacketCopyData(p, (uint8_t *)pktAddress, rte_pktmbuf_pkt_len(tmpMbuf)) == -1) {
                TmqhOutputPacketpool(DpdkTv->tv, p);
                SCReturnInt(1);
            }
    
            if(TmThreadsSlotProcessPkt(DpdkTv->tv, DpdkTv->slot, p) != TM_ECODE_OK) {
                TmqhOutputPacketpool(DpdkTv->tv, p);
                SCReturnInt(1);
            }


            //DpdkTv->bytes += rte_pktmbuf_pkt_len(tmpMbuf);
            //if(PACKET_TEST_ACTION(p, ACTION_DROP)) {
            //    rte_pktmbuf_free(bufs[i]);
            //}
        }
        //DpdkTv->pkts += (uint64_t) nb_rx;
        
        if (dpdk_config->mode == NATIVE_RING_MODE) {
            nb_tx = rte_ring_enqueue_burst(tx_ring, (void**)DpdkTv->AcceptedMbufs, DpdkTv->AcceptedIndex, NULL);
        } else {
            nb_tx = rte_eth_tx_burst(dpdk_config->port_id, tv->ring_id, (void**)DpdkTv->AcceptedMbufs, DpdkTv->AcceptedIndex);
        }
        // nb_tx = rte_ring_enqueue_burst(tx_ring, (void**)DpdkTv->AcceptedMbufs, DpdkTv->AcceptedIndex, NULL);
        /** Cleaning rings... **/
		for (int i = nb_tx; i < DpdkTv->AcceptedIndex; i++) { //Accepeted
			rte_pktmbuf_free(DpdkTv->AcceptedMbufs[i]);
        }
        for (int i = 0; i < DpdkTv->DeclinedIndex; i++) {
			rte_pktmbuf_free(DpdkTv->DeclinedMbufs[i]);
        }
        DpdkTv->AcceptedIndex = 0;
        DpdkTv->DeclinedIndex = 0;


        /***  WORKING ***/
        // nb_tx = rte_ring_enqueue_burst(tx_ring, (void **) bufs, nb_rx, NULL);
        // DpdkTv->pkts += (uint64_t) nb_rx;
        // for(uint16_t i = nb_tx; i<nb_rx; ++i) {
        //      rte_pktmbuf_free(bufs[i]);
        // }
        /* ************* */
        // NOT WORKING
        //for(uint16_t i = 0; i<nb_rx; ++i) {
        //     rte_pktmbuf_free(bufs[i]);
        //}

    }

    SCReturnInt(TM_ECODE_OK);
}


void ReceiveDpdkThreadExitStats(ThreadVars *tv, void *data)
{
    SCEnter();
    DpdkTheadVars *DpdkTv = (DpdkTheadVars *)data;

    SCLogNotice("(%s): Received -- pkts: %" PRIu64 ", bytes %" PRIu64 "",
              tv->name,
              DpdkTv->pkts,
              DpdkTv->bytes);

    //(void) SC_ATOMIC_ADD(DpdkTv->livedev->pkts, DpdkTv->pkts);
    return;
}


/* =================================================================== */
/* ************************** Decode module ************************** */

TmEcode DecodeDpdkInit(ThreadVars *tv, const void *initdata, void **data)
{
    SCEnter();
    DecodeThreadVars *dtv = NULL;
    
    dtv = DecodeThreadVarsAlloc(tv);

    if(dtv == NULL) {
        SCReturnInt(TM_ECODE_FAILED);
    }

    DecodeRegisterPerfCounters(dtv, tv);
    *data = (void *)dtv;

    SCReturnInt(TM_ECODE_OK);
}


TmEcode DecodeDpdkDeinit(ThreadVars *tv, void *data)
{
    SCEnter();

    if(data != NULL) {
        DecodeThreadVarsFree(tv, data);
    }
    
    SCReturnInt(TM_ECODE_OK);
}

TmEcode DecodeDpdk(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                   PacketQueue *postq)
{
    SCEnter();
    DecodeThreadVars *dtv = (DecodeThreadVars *)data;

    if(p->flags & PKT_PSEUDO_STREAM_END) {
        return TM_ECODE_OK;
    }

    DecodeUpdatePacketCounters(tv, dtv, p);

    DecodeEthernet(tv, dtv, p, GET_PKT_DATA(p), GET_PKT_LEN(p), pq);

    PacketDecodeFinalize(tv, dtv, p);

    SCReturnInt(TM_ECODE_OK);
}


/* ==================================================================== */
/* ************************** Verdict module ************************** */

TmEcode VerdictDpdkInit(ThreadVars *tv, const void *initdata, void **data)
{
    SCEnter();

    // todo via initdata
    // Takes slot_data from ReceiveDPDK module
    //TmSlot *receiveSlot = tv->tm_slots; 
    // SC_ATOMIC_DECLARE(void *, slot_data); 
    // (void)SC_ATOMIC_SET((void *)slot_data, (void *)receiveSlot->slot_data);
    
    // DpdkTheadVars *DpdkTv = (DpdkTheadVars *)slot_data;
    // DpdkTv->accepted = 0;
    // DpdkTv->dropped = 0;
    //*data = (void *)receiveSlot->slot_da;

    // DpdkIfaceConfig *aconf = (DpdkIfaceConfig *)initdata;
    // DpdkTheadVars *DpdkTv = SCMalloc(sizeof(DpdkTheadVars));
    // if (unlikely(DpdkTv == NULL)) {
    //     SCReturnInt(TM_ECODE_FAILED);
    // }
    // memset(DpdkTv, 0, sizeof(DpdkTheadVars));
    
    DpdkTheadVars *DpdkTv = &dpdk_th_vars[tv->ring_id];
    DpdkTv->accepted = 0;
    DpdkTv->dropped = 0;
    DpdkTv->AcceptedIndex = 0;
    DpdkTv->DeclinedIndex = 0;
    //rte_eth_dev_get_name_by_port(0, (char *)DpdkTv->port); //todo
    //DpdkTv->livedev = LiveGetDevice(DpdkTv->port);
    //DpdkTv->livedev = aconf->live_dev;
    // DpdkTv->queue_num = aconf->queue_num;
    *data = (void *)DpdkTv;

    SCReturnInt(TM_ECODE_OK);
}

TmEcode VerdictDpdkDeinit(ThreadVars *tv, void *data)
{
    SCEnter();
  
    SCReturnInt(TM_ECODE_OK);
}

TmEcode DPDKSetVerdict(ThreadVars *tv, DpdkTheadVars *DpdkTv, Packet *p)
{
    SCEnter();
    uint16_t port;
    uint16_t nb_tx;
    //struct rte_mbuf *m;
    struct rte_ring *tx_ring = dpdk_config->tx_rings[tv->ring_id];

    if (PACKET_TEST_ACTION(p, ACTION_DROP)) {
        (DpdkTv->dropped)++;
        //rte_pktmbuf_free(DpdkTv->ReceivedMbufs[p->mbufIndex]); //v2
        DpdkTv->DeclinedMbufs[(DpdkTv->DeclinedIndex)++] = DpdkTv->ReceivedMbufs[p->mbufIndex]; 
    } else {
        (DpdkTv->accepted)++;
        DpdkTv->AcceptedMbufs[(DpdkTv->AcceptedIndex)++] = DpdkTv->ReceivedMbufs[p->mbufIndex];
        
        /* Send processing packet. */
        // v2
        // if (dpdk_config->mode == NATIVE_RING_MODE) {
        //     nb_tx = rte_ring_enqueue_burst(tx_ring, (void**)DpdkTv->ReceivedMbufs, 1, NULL);
        //     if (nb_tx != 1) {
        //         rte_pktmbuf_free(DpdkTv->ReceivedMbufs[p->mbufIndex]);
        //     }
        // } else {
        //     nb_tx = rte_eth_tx_burst(0, 0, (void**)DpdkTv->ReceivedMbufs, 1);
        //      if (nb_tx != 1) {
        //         rte_pktmbuf_free(DpdkTv->ReceivedMbufs[p->mbufIndex]);
        //     }
        // }
    }
  
    SCReturnInt(TM_ECODE_OK);
}
 
TmEcode VerdictDpdk(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                    PacketQueue *postq)
{
    SCEnter();

    TmEcode retval = TM_ECODE_OK;
    //SCReturnInt(retval); // todo
    DpdkTheadVars *DpdkTv = (DpdkTheadVars *)data;

    /* can't verdict a "fake" packet */
    if (p->flags & PKT_PSEUDO_STREAM_END) {
        SCLogNotice("XXXXX ");
        SCReturnInt(TM_ECODE_OK);
    }

    if (IS_TUNNEL_PKT(p)) {
        bool verdict = VerdictTunnelPacket(p);

        if (verdict == true) {
            retval = DPDKSetVerdict(tv, DpdkTv, p->root ? p->root : p);
        }
    } else {
        retval = DPDKSetVerdict(tv, DpdkTv, p);
    }

    SCReturnInt(retval);
}

/**
 * \brief This function prints stats for the VerdictThread
 *
 *
 * \param tv pointer to ThreadVars
 * \param data pointer that gets cast into DPDKThreadVars for DpdkTv
 */
void VerdictDPDKThreadExitStats(ThreadVars *tv, void *data)
{
    SCEnter();

    DpdkTheadVars *DpdkTv = (DpdkTheadVars *)data;
    SCLogNotice("(%s) IPS: Pkts accepted %" PRIu64 ", " 
                                "dropped %" PRIu64 "",
                                tv->name,
                                DpdkTv->accepted,
                                DpdkTv->dropped);

    //(void) SC_ATOMIC_ADD(DpdkTv->livedev->drop, DpdkTv->dropped);
    return;  
}

/************************************************************************/
/************************************************************************/
/* BYPASS */
static volatile int g_should_stop = 0;
static uint64_t counters[5] = {0,0,0,0,0};

static void handle_sig(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		g_should_stop = 1;
		break;
	}
}

static int should_stop(void)
{
	return g_should_stop;
}

static void thread_ring_loop(struct rte_ring *rx_ring, struct rte_ring *tx_ring, uint16_t id)
{
	struct rte_mbuf *pkts[BURST_SIZE];
	uint16_t rx_count;
	uint16_t tx_count;

	while (!should_stop()) {
		rx_count = rte_ring_dequeue_burst(rx_ring, (void **) pkts, BURST_SIZE, NULL);
		//tx_count = rte_ring_enqueue_burst(tx_ring, (void **) pkts, rx_count, NULL);
        counters[id] += rx_count;
		uint16_t i;
		for (i = 0; i < rx_count; ++i)
			rte_pktmbuf_free(pkts[i]);
	}
    printf("Core %u\t Received pkts: %lu", id, counters[id]);
}

static void thread_ethdev_loop(uint16_t port_id, uint16_t queue_id, uint16_t id)
{
	struct rte_mbuf *pkts[BURST_SIZE];
	uint16_t rx_count;
	uint16_t tx_count;

	while (!should_stop()) {
		rx_count = rte_eth_rx_burst(port_id, queue_id, pkts, BURST_SIZE);
		//tx_count = rte_eth_tx_burst(port_id, queue_id, pkts, rx_count);
        counters[id] += rx_count;
		uint16_t i;
		for (i = 0; i < rx_count; ++i)
			rte_pktmbuf_free(pkts[i]);
	}
    printf("Core %u\n Received pkts: %lu", id, counters[id]);
}


static int thread_main(void *arg)
{
    struct context *ctx	= arg;
	unsigned lcore_id = rte_lcore_id();

	switch(ctx->mode) {
		case ETHDEV_MODE:
		case ETHDEV_RING_MODE:
			thread_ethdev_loop(ctx->port_id, lcore_id, lcore_id);
			break;

		case NATIVE_RING_MODE:
			thread_ring_loop(ctx->rx_rings[lcore_id], ctx->tx_rings[lcore_id], lcore_id);
			break;
	}

	return 0;
}

void run_dpdk_bypass(void) {
    struct context ctx;

    // dpdk_config = SCMalloc(sizeof(struct context));
    // if (unlikely(dpdk_config == NULL)) {
    //     SCReturnInt(TM_ECODE_FAILED);
    // }
    // memset(dpdk_config, 0, sizeof(struct context));
    ctx.mode = NATIVE_RING_MODE;
    ctx.port_id = 0;
    ctx.port_name = "suricata";
    ctx.socket_id = rte_socket_id();
	ctx.nb_queues = rte_lcore_count(); // minus master core

    struct rte_ring *rx_rings[ctx.nb_queues];
	struct rte_ring *tx_rings[ctx.nb_queues];
	ctx.rx_rings = rx_rings;
	ctx.tx_rings = tx_rings;

    if (ctx.mode == ETHDEV_RING_MODE || ctx.mode == NATIVE_RING_MODE) {
		uint16_t i;
		for (i = 0; i < ctx.nb_queues; i++) {
			char name[64];

			snprintf(name, sizeof(name), "port_%s_tx%" PRIu16, ctx.port_name, i);
			ctx.rx_rings[i] = rte_ring_lookup(name);
			if (ctx.rx_rings[i] == NULL) {
                SCLogInfo("rte_ring_lookup(): cannot get rx ring '%s'\n", name);
				//retval = EXIT_FAILURE;
				//goto rxtx_rings_fail;
                return EXIT_FAILURE;
			}

			snprintf(name, sizeof(name), "port_%s_rx%" PRIu16, ctx.port_name, i);
			ctx.tx_rings[i] = rte_ring_lookup(name);
			if (ctx.tx_rings[i] == NULL) {
                SCLogInfo("rte_ring_lookup(): cannot get tx ring '%s'\n", name);
				//retval = EXIT_FAILURE;
				//goto rxtx_rings_fail;
                return EXIT_FAILURE;
			}
		}
	}

    if (ctx.mode == ETHDEV_RING_MODE) {
		char name[64];
		snprintf(name, sizeof(name), "port_%s", ctx.port_name);
		ctx.pool = rte_mempool_lookup(name);
		if (ctx.pool == NULL) {
            SCLogInfo("rte_mempool_lookup(): cannot get mempool '%s'", name);
			//retval = EXIT_FAILURE;
            //goto mempool_fail;
			return EXIT_FAILURE;
		}

		ctx.port_id = rte_eth_from_rings("extern_port",
                                ctx.rx_rings,
                                ctx.nb_queues,
                                ctx.tx_rings,
                                ctx.nb_queues,
                                ctx.socket_id);
	}

    if (ctx.mode == ETHDEV_MODE || ctx.mode == ETHDEV_RING_MODE) {
		if (DpdkPortInit_new(ctx.port_id, ctx.pool, ctx.nb_queues) != 0) {
            SCLogInfo("ethdev_init(): cannot init port %" PRIu16 "", ctx.port_id);
            //retval = EXIT_FAILURE;
            SCReturnInt(EXIT_FAILURE);
		}
	}

    printf("\nStarting all cores ... [Ctrl+C to quit]\n\n");

	signal(SIGINT, &handle_sig);
	signal(SIGTERM, &handle_sig);


    rte_eal_mp_remote_launch(thread_main, &ctx, SKIP_MASTER);
	rte_eal_mp_wait_lcore();

    if (ctx.mode == ETHDEV_RING_MODE) {
		rte_eth_dev_close(ctx.port_id);
	}

    rte_eal_cleanup();
}




#endif
