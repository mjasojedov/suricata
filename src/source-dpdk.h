#ifndef __SOURCE_DPDK_H__
#define __SOURCE_DPDK_H__

int DPDKInitConfig(void);
void DPDKAllocateThreadVars(void);

void TmModuleReceiveDpdkRegister(void);
void TmModuleDecodeDpdkRegister(void);
void TmModuleVerdictDpdkRegister(void);

#endif /* __SOURCE_DPDK_H__ */