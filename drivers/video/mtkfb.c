// SPDX-License-Identifier: ISC
/*
 * MediaTek MT8196 framebuffer driver for coreboot
 *
 * Based on depthcharge:
 * - src/board/rauru/board.c
 * - src/drivers/video/mtk_ddp.c
 */

#include <dm.h>
#include <log.h>
#include <video.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <linux/delay.h>

#define FB_WIDTH	1920
#define FB_HEIGHT	1200
#define FB_BPP		4

#define EXDMA_BASE		0x32850000UL
#define BLENDER_BASE		0x328E0000UL
#define EXDMA_EN		0x020
#define EXDMA_L_EN		0x040
#define OVL_L0_ADDR		0xF40

#define DVO0_BASE		0x324C0000UL
#define DVO_PATTERN_CTRL	(DVO0_BASE + 0x100)

static void mtk_exdma_configure(ulong fb_pa, ulong fb_size)
{
	flush_dcache_range(fb_pa, fb_pa + fb_size);

	writel((u32)fb_pa, EXDMA_BASE + OVL_L0_ADDR);
	setbits_le32(EXDMA_BASE + EXDMA_EN,   BIT(0));
	setbits_le32(EXDMA_BASE + EXDMA_L_EN, BIT(0));

	setbits_le32(BLENDER_BASE + EXDMA_L_EN, BIT(0));
}

static int mtk_video_bind(struct udevice *dev)
{
	struct video_uc_plat *uc_plat = dev_get_uclass_plat(dev);

	uc_plat->size = FB_WIDTH * FB_HEIGHT * FB_BPP;

	return 0;
}

static int mtk_video_probe(struct udevice *dev)
{
	struct video_uc_plat *uc_plat = dev_get_uclass_plat(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);

	uc_priv->xsize = FB_WIDTH;
	uc_priv->ysize = FB_HEIGHT;
	uc_priv->bpix  = VIDEO_BPP32; /* XRGB8888 */

	mtk_exdma_configure(uc_plat->base, uc_plat->size);

	/* Clear hardware test pattern */
	writel(0, DVO_PATTERN_CTRL);

	video_set_flush_dcache(dev, true);

	return 0;
}

static const struct udevice_id mtk_video_ids[] = {
	{ .compatible = "mediatek,mt8196-display-debug" },
	{ }
};

U_BOOT_DRIVER(mtk_video) = {
	.name		= "mtkfb_video",
	.id		= UCLASS_VIDEO,
	.of_match	= mtk_video_ids,
	.bind		= mtk_video_bind,
	.probe		= mtk_video_probe,
	.flags		= DM_FLAG_PRE_RELOC,
};
