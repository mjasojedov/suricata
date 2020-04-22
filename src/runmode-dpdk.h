#ifndef __RUNMODE_DPDK_H__
#define __RUNMODE_DPDK_H__

#include "util-device.h"

void RunModeDpdkRegister(void);
const char *RunModeDpdkGetDefaultMode(void);
int RunModeDpdkSingle(void);
int RunModeDpdkWorkers(void);
int RunModeDpdkIpsAutoFp(void);
int RunModeDpdkIpsWorkers(void);

void *DPDKGetThread(int number);
int DPDKConfigGetTheadsCount(void *arg);
void *ParseDPDKConfig(const char *arg);

typedef struct DpdkIfaceConfig_
{
    uint16_t queue_num;
    LiveDevice *live_dev;
} DpdkIfaceConfig;

#endif /* __RUNMODE_DPDK_H__ */