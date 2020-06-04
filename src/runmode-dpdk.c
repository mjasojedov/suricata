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
#include "util-dpdk.h" 
#include "util-runmodes.h"
#include "runmodes.h"
#include "runmode-dpdk.h"
#include "source-nfq.h"


static const char *default_mode = NULL;

/* Prototypes */
int RunModeDpdkSingle(void);
int RunModeDpdkWorkers(void);
int RunModeDpdkIpsAutoFp(void);
int RunModeDpdkIpsWorkers(void);
void *DPDKGetThread(int number);
int DPDKConfigGetTheadsCount(void *arg);
void *ParseDPDKConfig(const char *arg);


const char *RunModeDpdkGetDefaultMode(void)
{
    return default_mode;
}

void RunModeDpdkRegister(void)
{
    default_mode = "workers";

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

