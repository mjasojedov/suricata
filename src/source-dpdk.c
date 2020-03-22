#include "util-dpdk-common.h"
#include "source-dpdk.h"

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

#define BURST_SIZE 32 // todo

/**
 * \brief Structure to hold thread specific variables.
 */
typedef struct DpdkTheadVars_
{
    /* counters */
    uint64_t pkts;

    ThreadVars *tv;
    TmSlot *slot;

    /* If possible livedevice the capture thread is rattached to. */
    LiveDevice *livedev;
} DpdkTheadVars;


/* ***** Prototypes ***** */
TmEcode ReceiveDpdkInit(ThreadVars *tv, const void *initdata, void **data);
void ReceiveDpdkThreadExitStats(ThreadVars *tv, void *data);
TmEcode ReceiveDpdkDeinit(ThreadVars *tv, void *data);
TmEcode ReceiveDpdkLoop(ThreadVars *tv, void *data, void *slot);

TmEcode DecodeDpdkInit(ThreadVars *tv, const void *initdata, void **data);
TmEcode DecodeDpdkDeinit(ThreadVars *tv, void *data);
TmEcode DecodeDpdk(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                   PacketQueue *postq);
/* ********************** */


void TmModuleReceiveDPDKRegister(void)
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

void TmModuleDecodeDPDKRegister(void)
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

/* ==================================================================== */
/* ************************** Receive module ************************** */

TmEcode ReceiveDpdkInit(ThreadVars *tv, const void *initdata, void **data)
{
    SCEnter();

    DpdkTheadVars *DpdkTv = SCMalloc(sizeof(DpdkTheadVars));
    if (unlikely(DpdkTv == NULL)) {
        SCReturnInt(TM_ECODE_FAILED);
    }
    memset(DpdkTv, 0, sizeof(DpdkTheadVars));
    
    DpdkTv->tv = tv;
    DpdkTv->pkts = 0;

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

    DpdkTv->slot = s->slot_next;

    /* Run until the application is quit or killed. */
    for (;;) {
        if (unlikely(suricata_ctl_flags & (SURICATA_STOP))) {
            //DpdkIntelDumpCounters(ptv);
            SCLogDebug(" Received Signal!");
            SCReturnInt(TM_ECODE_OK);
        }
        
        RTE_ETH_FOREACH_DEV(port) {
            /* Get burst of RX packets. */
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
            if (unlikely(nb_rx == 0)) {
                continue;
            } 
            
            for(int i=0; i < nb_rx; i++) {
                tmpMbuf = bufs[i];

                p = PacketGetFromQueueOrAlloc();
                if(p == NULL) {
                    SCReturnInt(1);
                }

                PKT_SET_SRC(p, PKT_SRC_WIRE);
                p->datalink = LINKTYPE_ETHERNET;
                gettimeofday(&p->ts, NULL);
                
                pktAddress = tmpMbuf->buf_addr + rte_pktmbuf_headroom(tmpMbuf);

                if(PacketCopyData(p, (uint8_t *)pktAddress, rte_pktmbuf_pkt_len(tmpMbuf)) == -1) {
                    TmqhOutputPacketpool(DpdkTv->tv, p);
                    SCReturnInt(1);
                }

                if(TmThreadsSlotProcessPkt(DpdkTv->tv, DpdkTv->slot, p) != TM_ECODE_OK) {
                    TmqhOutputPacketpool(DpdkTv->tv, p);
                    SCReturnInt(1);
                }
            }

            DpdkTv->pkts += nb_rx;

            /* --------------------- BYPASS mode --------------------- */
            /* Send burst of TX packets. */
            // const uint16_t nb_tx = rte_eth_tx_burst(port, 0,
            //         bufs, nb_rx);
            // /* Free any unsent packets. */
            // if (unlikely(nb_tx < nb_rx)) {
            //     uint16_t buf;
            //     for (buf = nb_tx; buf < nb_rx; buf++)
            //         rte_pktmbuf_free(bufs[buf]);
            // }
            /* --------------------------------- --------------------- */
        }

    }

    SCReturnInt(TM_ECODE_OK);
}


void ReceiveDpdkThreadExitStats(ThreadVars *tv, void *data)
{
    SCEnter();
    DpdkTheadVars *DpdkTv = (DpdkTheadVars *)data;

    SCLogNotice("(%s) DPDK: Received packets %" PRIu64 "",
              tv->name,
              DpdkTv->pkts);

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