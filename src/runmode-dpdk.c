#include "util-dpdk-common.h"
#include "util-dpdk.h" 
#include "util-runmodes.h"
#include "runmodes.h"
#include "runmode-dpdk.h"

#include "source-nfq.h"

static const char *default_mode = NULL;

const char *RunModeDpdkGetDefaultMode(void)
{
    return default_mode;
}

void RunModeDpdkRegister(void)
{
    default_mode = "autofp";

    /* ************************ IDS ************************ */
    RunModeRegisterNewRunMode(RUNMODE_DPDK, "single", 
                              "Single threaded DPDK mode.",
                              RunModeDpdkSingle);
    // RunModeRegisterNewRunMode(RUNMODE_DPDK, "workers", 
    //                           "Workers DPDK mode, each thread does all"
    //                           " tasks from acquisition to logging",
    //                           RunModeDpdkWorkers);

    /* ************************ IPS ************************ */
    RunModeRegisterNewRunMode(RUNMODE_DPDK, "autofp",
                              "Multi threaded DPDK IPS mode with"
                              " respect to flow",
                              RunModeDpdkIpsAutoFp);

    RunModeRegisterNewRunMode(RUNMODE_DPDK, "workers",
                              "Workers DPDK mode, each thread does all"
                              " tasks from acquisition to logging",
                              RunModeDpdkIpsWorkers);
    return;
}

void *DPDKGetThread(int number)
{
    return NULL;
}

int DPDKConfigGetTheadsCount(void *arg)
{
    return 1;
}


void *ParseDPDKConfig(const char *arg)
{
    return NULL;
}


/**
 * \brief Single thread version of the DPDK processing.
 */
int RunModeDpdkSingle(void)
{
    SCEnter();
    
#ifdef HAVE_DPDK
    int retval;
    char *live_dev = NULL;
    // const char live_dev[RTE_ETH_NAME_MAX_LEN];
    // uint16_t port;

    // RTE_ETH_FOREACH_DEV(port) {
    //     if(rte_eth_dev_get_name_by_port(port, (char *)live_dev) != 0) {
    //         exit(EXIT_FAILURE);
    //     }
    // }

    RunModeInitialize();
    TimeModeSetLive();

    retval = RunModeSetLiveCaptureSingle(ParseDPDKConfig, 
                                         DPDKConfigGetTheadsCount,
                                         "ReceiveDPDK",
                                         "DecodeDPDK", thread_name_single,
                                         live_dev);
    if (retval != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeDpdkSingle initialized");
#endif

    SCReturnInt(0);
}

/**
 * \brief Workers version of the DPDK LIVE processing.
 *
 * Start N threads with each thread doing all the work.
 *
 */
int RunModeDpdkWorkers(void)
{
    SCEnter();
    
#ifdef HAVE_DPDK
    int retval;
    char *live_dev = NULL;
    // const char live_dev[RTE_ETH_NAME_MAX_LEN];
    // uint16_t port;
    // RTE_ETH_FOREACH_DEV(port) {
    //     if(rte_eth_dev_get_name_by_port(port, (char *)live_dev) != 0) {
    //         exit(EXIT_FAILURE);
    //     }
    // }

    RunModeInitialize();
    TimeModeSetLive();

    retval = RunModeSetLiveCaptureWorkers(ParseDPDKConfig, 
                                          DPDKConfigGetTheadsCount,
                                          "ReceiveDPDK",
                                          "DecodeDPDK", thread_name_workers,
                                          live_dev);
    if (retval != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeDpdkWorkers initialized");
#endif

    SCReturnInt(0);
}


int RunModeDpdkIpsAutoFp(void)
{
    SCEnter();
   
#ifdef HAVE_DPDK
    int retval;
    RunModeInitialize();
    TimeModeSetLive();
    LiveDeviceHasNoStats();

    retval = RunModeSetIPSAutoFp(DPDKGetThread,
                                "ReceiveDPDK",
                                "VerdictDPDK",
                                "DecodeDPDK");
#endif /* HAVE_DPDK */
    return retval;
}


/**
 * \brief Workers version of the IPS DPDK LIVE processing.
 *
 * Start N threads with each thread doing all the work.
 *
 */
int RunModeDpdkIpsWorkers(void)
{
     SCEnter();
    
#ifdef HAVE_DPDK
    int retval;

    RunModeInitialize();
    TimeModeSetLive();
    LiveDeviceHasNoStats();

    retval = RunModeSetIPSWorker(DPDKGetThread,
                                "ReceiveDPDK",
                                "VerdictDPDK",
                                "DecodeDPDK");

    if (retval != 0) {
        SCLogError(SC_ERR_RUNMODE, "Runmode start failed");
        exit(EXIT_FAILURE);
    }

    SCLogInfo("RunModeDpdkIpsWorkers initialized");
#endif

    SCReturnInt(0);
}

