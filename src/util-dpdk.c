
#include "util-dpdk-common.h"
#include "util-dpdk.h" 


int dpdk_argv_count = 1;
char dpdk_argv[32][50] = {{"suricata"}};

int ParseDpdkConfig(void)
{
    SCEnter();
    struct rte_cfgfile *file = rte_cfgfile_load("dpdk.cfg", 0);
    if(file == NULL) {
        //error opening config file
    }

    /* Parse EAL parameters from configuration file */
    if(rte_cfgfile_has_section(file, "EAL")) {
        int num_entries =  rte_cfgfile_section_num_entries(file, "EAL");
        struct rte_cfgfile_entry entries[num_entries];
        SCLogNotice(" DPDK configuration | EAL section: %d entries", num_entries);

        if(rte_cfgfile_section_entries(file, "EAL", entries, num_entries) != -1) {
            for(int i = 0; i < num_entries; i++) {
                snprintf(dpdk_argv[i * 2 + 1], 50, "%s", entries[i].name);
				snprintf(dpdk_argv[i * 2 + 2], 50, "%s", entries[i].value);
				SCLogDebug(" - argument: (%s) (%s)", dpdk_argv[i * 2 + 1], dpdk_argv[i * 2 + 2]);
			        dpdk_argv_count += (((entries[i].name) ? 1 : 0) + ((entries[i].value) ? 1 : 0));
            }
        }
    }

    rte_cfgfile_close(file);
    return EXIT_SUCCESS;
}

int DpdkEalInit(void) 
{
    if(rte_eal_init(dpdk_argv_count, (char **)dpdk_argv) == -1) {
        //error
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}