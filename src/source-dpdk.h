#ifndef __SOURCE_DPDK_H__
#define __SOURCE_DPDK_H__

int DPDKInitConfig(void);
void DPDKAllocateThreadVars(void);
void DpdkClean(void);

void TmModuleReceiveDpdkRegister(void);
void TmModuleDecodeDpdkRegister(void);
void TmModuleVerdictDpdkRegister(void);


void run_dpdk_bypass(void);
#endif /* __SOURCE_DPDK_H__ */