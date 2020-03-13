#include "util-dpdk-common.h"
#include "util-dpdk.h" 
#include "runmodes.h"
#include "runmode-dpdk.h"

static const char *default_mode = NULL;

const char *RunModeDpdkGetDefaultMode(void)
{
    return default_mode;
}

void RunModeDpdkRegister(void)
{
    default_mode = "single";
    RunModeRegisterNewRunMode(RUNMODE_DPDK, "single", 
                              "Workers Ndp mode, each thread does all"
                              " tasks from acquisition to logging",
                              RunModeDpdkSingle);
    return;
}

int RunModeDpdkSingle(void) {
    return 0;
}