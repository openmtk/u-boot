// SPDX-License-Identifier: GPL-2.0

#include <asm/u-boot.h>
#include <fdt_simplefb.h>

int ft_board_setup(void *blob, struct bd_info *)
{
#ifdef CONFIG_FDT_SIMPLEFB
	fdt_simplefb_add_node(blob);
#endif
	return 0;
}
