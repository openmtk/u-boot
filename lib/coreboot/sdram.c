// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011 The Chromium OS Authors.
 * (C) Copyright 2010,2011
 * Graeme Russ, <graeme.russ@gmail.com>
 */

#include <init.h>
#include <cb_sysinfo.h>
#include <errno.h>
#include <linux/sizes.h>
#include <asm/global_data.h>
#ifdef CONFIG_ARM64
#include <asm/armv8/mmu.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_ARM64
/*
 * ARM64 coreboot-payload boards all need an mm_region table to turn
 * on the MMU. Rather than each board building its own, provide a
 * generic one here that gets populated during coreboot_dram_init()
 * from the sysinfo memrange walk. Boards just reference the mem_map
 * symbol and call icache_enable() / dcache_enable() from their
 * enable_caches().
 *
 * Emits exactly two regions plus a sentinel:
 *
 *   1. MT_DEVICE_NGNRNE for [0x1000, lowest RAM). Catches SoC MMIO
 *      without each board enumerating it. Page 0 stays unmapped so
 *      NULL-pointer accesses trap.
 *
 *   2. MT_NORMAL (cacheable, inner-shareable) from lowest RAM to
 *      highest RAM end. CB_MEM_RAM, CB_MEM_TABLE and CB_MEM_TAG are
 *      all physically RAM and share this single span; reserved holes
 *      inside DRAM (BL31, TEE, etc.) are harmless to map cacheable
 *      and collapsing into one span keeps the page tables compact on
 *      boards with many separate RAM ranges.
 */
/*
 * Force into .data: this gets populated in coreboot_dram_init()
 * which runs pre-relocation, and U-Boot zeroes .bss again after
 * relocation. A .bss-resident mem_map would lose its contents and
 * dcache_enable() would map nothing.
 */
static struct mm_region coreboot_mem_map[3] __section(".data");
struct mm_region *mem_map = coreboot_mem_map;

static bool coreboot_is_ram_type(unsigned int type)
{
	return type == CB_MEM_RAM || type == CB_MEM_TABLE ||
	       type == CB_MEM_TAG;
}

static void coreboot_fill_mem_map(u64 lowest, u64 highest_end)
{
	coreboot_mem_map[0].virt = SZ_4K;
	coreboot_mem_map[0].phys = SZ_4K;
	coreboot_mem_map[0].size = lowest - SZ_4K;
	coreboot_mem_map[0].attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				    PTE_BLOCK_NON_SHARE |
				    PTE_BLOCK_PXN | PTE_BLOCK_UXN;

	coreboot_mem_map[1].virt = lowest;
	coreboot_mem_map[1].phys = lowest;
	coreboot_mem_map[1].size = highest_end - lowest;
	coreboot_mem_map[1].attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
				    PTE_BLOCK_INNER_SHARE;

	/* Sentinel is already zero-initialised by BSS. */
}

/*
 * The generic get_page_table_size() walks mem_map and sums up the
 * page tables every region needs. The Device region we emit starts
 * at 0x1000 (to keep NULL-pointer accesses unmapped), which forces
 * subdivision down to 4K granularity at the bottom of the address
 * space. The computation gets pathological on boards with multi-GB
 * physical RAM and we end up either reserving an absurd amount of
 * RAM for page tables or crashing arch_reserve_mmu() outright.
 * Override with a fixed 1 MB instead, the same approach
 * mach-snapdragon uses.
 */
u64 get_page_table_size(void)
{
	return SZ_1M;
}
#endif /* CONFIG_ARM64 */

/*
 * This function looks for the highest region of memory lower than 4GB which
 * has enough space for U-Boot where U-Boot is aligned on a page boundary. It
 * overrides the default implementation found elsewhere which simply picks the
 * end of ram, wherever that may be. The location of the stack, the relocation
 * address, and how far U-Boot is moved by relocation are set in the global
 * data structure.
 */
phys_addr_t coreboot_board_get_usable_ram_top(phys_size_t total_size)
{
	uintptr_t dest_addr = 0;
	int i;

	for (i = 0; i < lib_sysinfo.n_memranges; i++) {
		struct memrange *memrange = &lib_sysinfo.memrange[i];
		/* Force U-Boot to relocate to a page aligned address. */
		uint64_t start = roundup(memrange->base, 1 << 12);
		uint64_t end = memrange->base + memrange->size;

		/* Ignore non-memory regions. */
		if (memrange->type != CB_MEM_RAM)
			continue;

		/* Filter memory over 4GB. */
		if (end > 0xffffffffULL)
			end = 0x100000000ULL;
		/* Skip this region if it's too small. */
		if (end - start < total_size)
			continue;

		/* Use this address if it's the largest so far. */
		if (end > dest_addr)
			dest_addr = end;
	}

	/* If no suitable area was found, return an error. */
	if (!dest_addr)
		panic("No available memory found for relocation");

	return (ulong)dest_addr;
}

int coreboot_dram_init(void)
{
	int i;
	phys_size_t ram_base = ~0UL;
	phys_size_t ram_size = 0;
#ifdef CONFIG_ARM64
	u64 mmu_lowest = ~0ULL;
	u64 mmu_highest_end = 0;
#endif

	for (i = 0; i < lib_sysinfo.n_memranges; i++) {
		struct memrange *memrange = &lib_sysinfo.memrange[i];
		unsigned long long end = memrange->base + memrange->size;

		if (memrange->type == CB_MEM_RAM) {
			if (ram_base > memrange->base)
				ram_base = memrange->base;
			if (end > ram_size)
				ram_size += memrange->size;
		}

#ifdef CONFIG_ARM64
		/*
		 * Track the physical span of everything RAM-like (RAM,
		 * coreboot tables, tag storage) so the ARM64 mem_map can
		 * cover it all as a single MT_NORMAL region, without needing
		 * a second pass over the memrange list.
		 */
		if (coreboot_is_ram_type(memrange->type)) {
			if (memrange->base < mmu_lowest)
				mmu_lowest = memrange->base;
			if (end > mmu_highest_end)
				mmu_highest_end = end;
		}
#endif
	}

	gd->ram_base = ram_base;
	gd->ram_size = ram_size;
	if (ram_size == 0)
		return -1;

#ifdef CONFIG_ARM64
	if (mmu_lowest != ~0ULL)
		coreboot_fill_mem_map(mmu_lowest, mmu_highest_end);
#endif

	return 0;
}

int coreboot_dram_init_banksize(void)
{
	int i, j;

	if (CONFIG_NR_DRAM_BANKS) {
		for (i = 0, j = 0; i < lib_sysinfo.n_memranges; i++) {
			struct memrange *memrange = &lib_sysinfo.memrange[i];

			if (memrange->type == CB_MEM_RAM) {
				gd->bd->bi_dram[j].start = memrange->base;
				gd->bd->bi_dram[j].size = memrange->size;
				j++;
				if (j >= CONFIG_NR_DRAM_BANKS)
					break;
			}
		}
	}

	return 0;
}
