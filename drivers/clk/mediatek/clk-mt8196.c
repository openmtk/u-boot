// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 MediaTek Inc.
 * Author: Guangjie Song <guangjie.song@mediatek.com>
 *
 * Based on the ChromeOS Linux drivers from:
 * https://gitlab.collabora.com/google/chromeos-kernel/-/tree/mt8196
 */

#include <asm/io.h>
#include <dm.h>
#include <dt-bindings/clock/mt8196-clk.h>
#include <linux/bitops.h>

#include "clk-mtk.h"

#define MUX_GATE_CLR_SET_UPD(_id, _parents, _mux_ofs, _mux_set_ofs, \
	_mux_clr_ofs, _shift, _width, _gate, _upd_ofs, _upd) \
	MUX_CLR_SET_UPD_FLAGS(_id, _parents, _mux_ofs, _mux_set_ofs, \
		_mux_clr_ofs, _shift, _width, _gate, \
		_upd_ofs, _upd, CLK_MUX_SETCLR_UPD)

#define MT8196_PLL_FMAX		(3800UL * MHZ)
#define MT8196_PLL_FMIN		(1500UL * MHZ)
#define MT8196_INTEGER_BITS	8

#define MAINPLL_CON0	0x250
#define UNIVPLL_CON0	0x264

#define PLL(_id, _con0, _pcwbits, _pd_shift) {	\
	.id = _id,				\
	.reg = _con0,				\
	.pwr_reg = (_con0) + 0xc,		\
	.en_mask = 0,				\
	.fmax = MT8196_PLL_FMAX,		\
	.fmin = MT8196_PLL_FMIN,		\
	.pcwbits = _pcwbits,			\
	.pcwibits = MT8196_INTEGER_BITS,	\
	.pd_reg = (_con0) + 4,			\
	.pd_shift = _pd_shift,			\
	.pcw_reg = (_con0) + 4,			\
	.pcw_shift = 0,				\
	.pcw_chg_reg = (_con0) + 4,		\
}

enum {
	CLK_APMIXED_EXT_CLK26M = 0,
};

static const ulong apmixed_ext_clk_rates[] = {
	[CLK_APMIXED_EXT_CLK26M] = 26000000,
};

static const struct mtk_pll_data apmixed_plls[] = {
	PLL(CLK_APMIXED_MAINPLL, MAINPLL_CON0, 22, 24),
	PLL(CLK_APMIXED_UNIVPLL, UNIVPLL_CON0, 22, 24),
};

static const struct mtk_clk_tree mt8196_apmixedsys_clk_tree = {
	.pll_parent    = EXT_PARENT(CLK_APMIXED_EXT_CLK26M),
	.ext_clk_rates = apmixed_ext_clk_rates,
	.num_ext_clks  = ARRAY_SIZE(apmixed_ext_clk_rates),
	.plls          = apmixed_plls,
	.num_plls      = ARRAY_SIZE(apmixed_plls),
};

#define CLK_CFG_UPDATE		0x0004
#define CLK_CFG_UPDATE1		0x0008
#define CLK_CFG_7			0x0080
#define CLK_CFG_7_SET		0x0084
#define CLK_CFG_7_CLR		0x0088
#define CLK_CFG_10			0x00b0
#define CLK_CFG_10_SET		0x00b4
#define CLK_CFG_10_CLR		0x00b8

#define TOP_MUX_SPI1_BCLK_SHIFT	29
#define TOP_MUX_USB_TOP_1P_SHIFT	10
#define TOP_MUX_SSUSB_XHCI_1P_SHIFT	11
#define TOP_MUX_SSUSB_FMCNT_P1_SHIFT	12

#define TOP_FACTOR_AP(_id, _parent, _mult, _div) \
	FACTOR(_id, _parent, _mult, _div, CLK_PARENT_APMIXED)

enum {
	CLK_EXT_CLK26M = 0,
};

static const ulong top_ext_clock_rates[] = {
	[CLK_EXT_CLK26M] = 26000000,
};

static const struct mtk_fixed_factor top_divs[] = {
	TOP_FACTOR_AP(CLK_CK_MAINPLL_D4_D4,   CLK_APMIXED_MAINPLL, 1, 16),
	TOP_FACTOR_AP(CLK_CK_MAINPLL_D6_D2,   CLK_APMIXED_MAINPLL, 1, 12),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_D4_D4,   CLK_APMIXED_UNIVPLL, 1, 16),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_D5_D4,   CLK_APMIXED_UNIVPLL, 1, 20),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_D6_D2,   CLK_APMIXED_UNIVPLL, 1, 12),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_D6_D4,   CLK_APMIXED_UNIVPLL, 1, 24),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_192M,    CLK_APMIXED_UNIVPLL, 1, 13),
	TOP_FACTOR_AP(CLK_CK_UNIVPLL_192M_D4, CLK_APMIXED_UNIVPLL, 1, 52),
};

static const struct mtk_parent spi_b_parents[] = {
	EXT_PARENT(CLK_EXT_CLK26M),
	TOP_PARENT(CLK_CK_UNIVPLL_D6_D4),
	TOP_PARENT(CLK_CK_UNIVPLL_D5_D4),
	TOP_PARENT(CLK_CK_MAINPLL_D4_D4),
	TOP_PARENT(CLK_CK_UNIVPLL_D4_D4),
	TOP_PARENT(CLK_CK_MAINPLL_D6_D2),
	TOP_PARENT(CLK_CK_UNIVPLL_192M),
	TOP_PARENT(CLK_CK_UNIVPLL_D6_D2),
};

static const struct mtk_parent usb_1p_parents[] = {
	EXT_PARENT(CLK_EXT_CLK26M),
	TOP_PARENT(CLK_CK_UNIVPLL_D5_D4),
};

static const struct mtk_parent usb_fmcnt_p1_parents[] = {
	EXT_PARENT(CLK_EXT_CLK26M),
	TOP_PARENT(CLK_CK_UNIVPLL_192M_D4),
};

static const struct mtk_composite top_muxes[] = {
	MUX_GATE_CLR_SET_UPD(CLK_CK_SPI1_BCLK_SEL, spi_b_parents,
		CLK_CFG_7, CLK_CFG_7_SET, CLK_CFG_7_CLR,
		8, 3, 15, CLK_CFG_UPDATE, TOP_MUX_SPI1_BCLK_SHIFT),
	MUX_GATE_CLR_SET_UPD(CLK_CK_USB_TOP_1P_SEL, usb_1p_parents,
		CLK_CFG_10, CLK_CFG_10_SET, CLK_CFG_10_CLR,
		8, 1, 15, CLK_CFG_UPDATE1, TOP_MUX_USB_TOP_1P_SHIFT),
	MUX_GATE_CLR_SET_UPD(CLK_CK_USB_XHCI_1P_SEL, usb_1p_parents,
		CLK_CFG_10, CLK_CFG_10_SET, CLK_CFG_10_CLR,
		16, 1, 23, CLK_CFG_UPDATE1, TOP_MUX_SSUSB_XHCI_1P_SHIFT),
	MUX_GATE_CLR_SET_UPD(CLK_CK_USB_FMCNT_P1_SEL, usb_fmcnt_p1_parents,
		CLK_CFG_10, CLK_CFG_10_SET, CLK_CFG_10_CLR,
		24, 1, 31, CLK_CFG_UPDATE1, TOP_MUX_SSUSB_FMCNT_P1_SHIFT),
};

static const int mt8196_id_top_offs_map[] = {
	[CLK_CK_MAINPLL_D4_D4]    = 0,
	[CLK_CK_MAINPLL_D6_D2]    = 1,
	[CLK_CK_UNIVPLL_D4_D4]    = 2,
	[CLK_CK_UNIVPLL_D5_D4]    = 3,
	[CLK_CK_UNIVPLL_D6_D2]    = 4,
	[CLK_CK_UNIVPLL_D6_D4]    = 5,
	[CLK_CK_UNIVPLL_192M]     = 6,
	[CLK_CK_UNIVPLL_192M_D4]  = 7,
	[CLK_CK_SPI1_BCLK_SEL]    = 8,
	[CLK_CK_USB_TOP_1P_SEL]   = 9,
	[CLK_CK_USB_XHCI_1P_SEL]  = 10,
	[CLK_CK_USB_FMCNT_P1_SEL] = 11,
};

static const struct mtk_clk_tree mt8196_cksys_clk_tree = {
	.ext_clk_rates    = top_ext_clock_rates,
	.num_ext_clks     = ARRAY_SIZE(top_ext_clock_rates),
	.id_offs_map      = mt8196_id_top_offs_map,
	.id_offs_map_size = ARRAY_SIZE(mt8196_id_top_offs_map),
	.fdivs_offs       = 0,
	.muxes_offs       = ARRAY_SIZE(top_divs),
	.fdivs            = top_divs,
	.muxes            = top_muxes,
	.num_fdivs        = ARRAY_SIZE(top_divs),
	.num_muxes        = ARRAY_SIZE(top_muxes),
};

static const struct mtk_gate_regs perao1_cg_regs = {
	.set_ofs = 0x2c,
	.clr_ofs = 0x30,
	.sta_ofs = 0x14,
};

#define GATE_PERAO1(_id, _parent, _shift) \
	GATE_FLAGS(_id, _parent, &perao1_cg_regs, _shift, \
		CLK_PARENT_TOPCKGEN | CLK_GATE_SETCLR)

static const struct mtk_gate peri_ao_clks[] = {
	GATE_PERAO1(CLK_PERAO_SPI1_BCLK, CLK_CK_SPI1_BCLK_SEL, 2),
};

static const struct mtk_clk_tree mt8196_pericfg_ao_clk_tree = {
	.ext_clk_rates = top_ext_clock_rates,
	.num_ext_clks  = ARRAY_SIZE(top_ext_clock_rates),
};

#define VLP_CLK_CFG_UPDATE1	0x0008
#define VLP_CLK_CFG_8		0x0090
#define VLP_CLK_CFG_8_SET	0x0094
#define VLP_CLK_CFG_8_CLR	0x0098

#define VLP_MUX_USB_TOP_SHIFT	2
#define VLP_MUX_USB_XHCI_SHIFT	3

enum {
	CLK_VLP_EXT_CLK26M = 0,
	CLK_VLP_EXT_MAINPLL_D9,
};

static const ulong vlp_ext_clock_rates[] = {
	[CLK_VLP_EXT_CLK26M]     = 26000000,
	[CLK_VLP_EXT_MAINPLL_D9] = 0,
};

static const struct mtk_parent vlp_usb_parents[] = {
	EXT_PARENT(CLK_VLP_EXT_CLK26M),
	EXT_PARENT(CLK_VLP_EXT_MAINPLL_D9),
};

static const struct mtk_composite vlp_muxes[] = {
	MUX_GATE_CLR_SET_UPD(CLK_VLP_CK_USB_TOP_SEL, vlp_usb_parents,
		VLP_CLK_CFG_8, VLP_CLK_CFG_8_SET, VLP_CLK_CFG_8_CLR,
		8, 1, 15, VLP_CLK_CFG_UPDATE1, VLP_MUX_USB_TOP_SHIFT),
	MUX_GATE_CLR_SET_UPD(CLK_VLP_CK_USB_XHCI_SEL, vlp_usb_parents,
		VLP_CLK_CFG_8, VLP_CLK_CFG_8_SET, VLP_CLK_CFG_8_CLR,
		16, 1, 23, VLP_CLK_CFG_UPDATE1, VLP_MUX_USB_XHCI_SHIFT),
};

static const int mt8196_vlp_id_offs_map[CLK_VLP_CK_USB_XHCI_SEL + 1] = {
	[CLK_VLP_CK_USB_TOP_SEL]  = 0,
	[CLK_VLP_CK_USB_XHCI_SEL] = 1,
};

static const struct mtk_clk_tree mt8196_vlp_clk_tree = {
	.ext_clk_rates    = vlp_ext_clock_rates,
	.num_ext_clks     = ARRAY_SIZE(vlp_ext_clock_rates),
	.id_offs_map      = mt8196_vlp_id_offs_map,
	.id_offs_map_size = ARRAY_SIZE(mt8196_vlp_id_offs_map),
	.fdivs_offs       = 0,
	.muxes_offs       = 0,
	.fdivs            = NULL,
	.muxes            = vlp_muxes,
	.num_fdivs        = 0,
	.num_muxes        = ARRAY_SIZE(vlp_muxes),
};

static int mt8196_apmixedsys_probe(struct udevice *dev)
{
	return mtk_common_clk_init(dev, &mt8196_apmixedsys_clk_tree);
}

static int mt8196_cksys_probe(struct udevice *dev)
{
	return mtk_common_clk_init(dev, &mt8196_cksys_clk_tree);
}

static int mt8196_pericfg_ao_probe(struct udevice *dev)
{
	return mtk_common_clk_gate_init(dev, &mt8196_pericfg_ao_clk_tree,
				peri_ao_clks,
				ARRAY_SIZE(peri_ao_clks),
				CLK_PERAO_SPI1_BCLK);
}

static int mt8196_vlp_probe(struct udevice *dev)
{
	struct mtk_clk_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -ENOENT;

	priv->tree = &mt8196_vlp_clk_tree;
	priv->parent = NULL;

	return 0;
}

static const struct udevice_id mt8196_apmixed_compat[] = {
	{ .compatible = "mediatek,mt8196-apmixedsys" },
	{ }
};

static const struct udevice_id mt8196_cksys_compat[] = {
	{ .compatible = "mediatek,mt8196-cksys" },
	{ }
};

static const struct udevice_id mt8196_pericfg_ao_compat[] = {
	{ .compatible = "mediatek,mt8196-pericfg-ao" },
	{ }
};

static const struct udevice_id mt8196_vlp_compat[] = {
	{ .compatible = "mediatek,mt8196-vlp-cksys" },
	{ .compatible = "mediatek,mt8196-vlp_cksys" },
	{ }
};

U_BOOT_DRIVER(mtk_clk_apmixedsys) = {
	.name      = "mtk_clk_apmixedsys",
	.id        = UCLASS_CLK,
	.of_match  = mt8196_apmixed_compat,
	.probe     = mt8196_apmixedsys_probe,
	.priv_auto = sizeof(struct mtk_clk_priv),
	.ops       = &mtk_clk_apmixedsys_ops,
	.flags     = DM_FLAG_PRE_RELOC,
};

U_BOOT_DRIVER(mtk_clk_topckgen) = {
	.name      = "mtk_clk_topckgen",
	.id        = UCLASS_CLK,
	.of_match  = mt8196_cksys_compat,
	.probe     = mt8196_cksys_probe,
	.priv_auto = sizeof(struct mtk_clk_priv),
	.ops       = &mtk_clk_topckgen_ops,
	.flags     = DM_FLAG_PRE_RELOC,
};

U_BOOT_DRIVER(mt8196_pericfg_ao) = {
	.name      = "mt8196_pericfg_ao",
	.id        = UCLASS_CLK,
	.of_match  = mt8196_pericfg_ao_compat,
	.probe     = mt8196_pericfg_ao_probe,
	.priv_auto = sizeof(struct mtk_cg_priv),
	.ops       = &mtk_clk_gate_ops,
	.flags     = DM_FLAG_PRE_RELOC,
};

U_BOOT_DRIVER(mt8196_vlpcksys) = {
	.name      = "mt8196_vlpcksys",
	.id        = UCLASS_CLK,
	.of_match  = mt8196_vlp_compat,
	.probe     = mt8196_vlp_probe,
	.priv_auto = sizeof(struct mtk_clk_priv),
	.ops       = &mtk_clk_topckgen_ops,
	.flags     = DM_FLAG_PRE_RELOC,
};
