#ifndef __RUNMODE_DPDK_H__
#define __RUNMODE_DPDK_H__

#include "util-device.h"

typedef struct DpdkIfaceConfig_
{
    uint16_t queue_num;
    LiveDevice *live_dev;
} DpdkIfaceConfig;

void RunModeDpdkRegister(void);
const char *RunModeDpdkGetDefaultMode(void);

// todo  move to runmode-dpdk.c
int RunModeDpdkSingle(void);
int RunModeDpdkWorkers(void);
int RunModeDpdkIpsAutoFp(void);
int RunModeDpdkIpsWorkers(void);
void *DPDKGetThread(int number);
int DPDKConfigGetTheadsCount(void *arg);
void *ParseDPDKConfig(const char *arg);



#endif /* __RUNMODE_DPDK_H__ */