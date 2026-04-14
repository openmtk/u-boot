// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 MediaTek Inc.
 * Copyright (C) 2026 BayLibre, SAS
 * Copyright (C) 2026 Tobias Heider <me@tobhe.de>
 * Author: Julien Stephan <jstephan@baylibre.com>
 *         Chris-QJ Chen <chris-qj.chen@mediatek.com>
 */

#include <asm/armv8/mmu.h>
#include <asm/system.h>
#include <dm/uclass.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <wdt.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

phys_size_t get_effective_memsize(void)
{
	/* XXX: Limit to 256M for now to prevent corruption of reserved space */
	return min(0xA0000000 - gd->ram_base, gd->ram_size);
}

int mtk_soc_early_init(void)
{
	return 0;
}

void reset_cpu(void)
{
	struct udevice *wdt;

	if (IS_ENABLED(CONFIG_PSCI_RESET)) {
		psci_system_reset();
	} else {
		uclass_first_device(UCLASS_WDT, &wdt);
		if (wdt)
			wdt_expire_now(wdt, 0);
	}
}

int print_cpuinfo(void)
{
	printf("CPU:   MediaTek MT8196\n");
	return 0;
}
