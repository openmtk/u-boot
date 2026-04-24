#include <cb_sysinfo.h>
#include <cpu_func.h>
#include <log.h>

int dram_init(void)
{
	return coreboot_dram_init();
}

phys_addr_t board_get_usable_ram_top(phys_size_t total_size)
{
	return coreboot_board_get_usable_ram_top(total_size);
}

int dram_init_banksize(void)
{
	return coreboot_dram_init_banksize();
}

#if !IS_ENABLED(CONFIG_SYSRESET)
/* When SYSRESET is enabled, the uclass provides reset_cpu(). */
void reset_cpu(void) { /* Stub */ }
#endif

int board_init(void)
{
	return 0;
}

int board_late_init(void)
{
	return 0;
}

void enable_caches(void)
{
	icache_enable();
	dcache_enable();
}
