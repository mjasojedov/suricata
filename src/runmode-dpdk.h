#ifndef __RUNMODE_DPDK_H__
#define __RUNMODE_DPDK_H__

void RunModeDpdkRegister(void);
const char *RunModeDpdkGetDefaultMode(void);
int RunModeDpdkSingle(void);
int RunModeDpdkWorkers(void);
int RunModeDpdkIpsAutoFp(void);
int RunModeDpdkIpsWorkers(void);

void *DPDKGetThread(int number);
int DPDKConfigGetTheadsCount(void *arg);
void *ParseDPDKConfig(const char *arg);

#endif /* __RUNMODE_DPDK_H__ */