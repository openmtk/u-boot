// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek SPI master driver for ChromeOS-style use (talking to a
 * Chrome EC, etc.). Ported from depthcharge's
 * src/drivers/bus/spi/mtk.c. Coreboot has already configured pins,
 * clocks and divisor; this driver only programs packet
 * length/loop and starts a transfer.
 *
 * The MTK SPI controller can drive CS itself but only for the
 * duration of a single hardware-issued packet -- CS deasserts when
 * the packet loop counter hits zero. cros-ec's xfer pattern in
 * U-Boot needs CS held across multiple controller transactions
 * (claim_bus -> spi_xfer(BEGIN) -> ... -> spi_xfer(END) ->
 * release_bus), which would require a working pause-mode chain
 * with no inter-packet glitches; that's racy on this IP. So we
 * drive CS as a plain GPIO via the cs-gpios DT property and let
 * pinctrl put the pad in GPIO mode.
 */
#include <dm.h>
#include <errno.h>
#include <malloc.h>
#include <spi.h>
#include <time.h>
#include <asm/gpio.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/io.h>

struct mtk_spi_regs {
	u32 cfg0;
	u32 cfg1;
	u32 tx_src;
	u32 rx_dst;
	u32 tx_data;
	u32 rx_data;
	u32 cmd;
	u32 status0;
	u32 status1;
	u32 pad_macro_sel;
};

#define SPI_CMD_ACT		BIT(0)
#define SPI_CMD_RESUME		BIT(1)
#define SPI_CMD_RST		BIT(2)
#define SPI_CMD_PAUSE_EN	BIT(4)

#define SPI_CFG1_PACKET_LOOP_SHIFT	8
#define SPI_CFG1_PACKET_LEN_SHIFT	16
#define SPI_CFG1_PACKET_LOOP_MASK	(0xffU << 8)
#define SPI_CFG1_PACKET_LEN_MASK	(0x3ffU << 16)

#define MTK_FIFO_DEPTH			32U
#define MTK_TIMEOUT_MS			1000
#define MTK_ARBITRARY_VALUE		0xdeaddeadU

enum {
	MTK_SPI_IDLE = 0,
	MTK_SPI_PAUSE_IDLE = 1,
};

struct mtk_spi {
	struct mtk_spi_regs *regs;
	struct gpio_desc cs_gpio;
	int state;
};

static int mtk_spi_claim_bus(struct udevice *dev)
{
	struct mtk_spi *bus = dev_get_priv(dev_get_parent(dev));

	setbits_le32(&bus->regs->cmd, SPI_CMD_PAUSE_EN);
	bus->state = MTK_SPI_IDLE;
	dm_gpio_set_value(&bus->cs_gpio, 1);	/* assert (active-low) */
	udelay(30);				/* google,cros-ec-spi-pre-delay */
	return 0;
}

static int mtk_spi_release_bus(struct udevice *dev)
{
	struct mtk_spi *bus = dev_get_priv(dev_get_parent(dev));

	setbits_le32(&bus->regs->cmd, SPI_CMD_RST);
	clrbits_le32(&bus->regs->cmd, SPI_CMD_RST | SPI_CMD_PAUSE_EN);
	bus->state = MTK_SPI_IDLE;
	dm_gpio_set_value(&bus->cs_gpio, 0);	/* deassert */
	return 0;
}

static int mtk_spi_wait(struct mtk_spi_regs *regs)
{
	ulong start;

	start = get_timer(0);
	while (!(readl(&regs->status1) & 1U)) {
		if (get_timer(start) > MTK_TIMEOUT_MS)
			return -ETIMEDOUT;
	}
	start = get_timer(0);
	/*
	 * coreboot's MTK_SPI_PAUSE_FINISH_INT_STATUS = 3 is a MASK over
	 * bits 0,1 (finish / pause_int); the loop exits when ANY bit is
	 * set, not both. An earlier `== 3` comparison hung because on
	 * mt8196 only one of the two bits fires.
	 */
	while ((readl(&regs->status0) & 3U) == 0U) {
		if (get_timer(start) > MTK_TIMEOUT_MS)
			return -ETIMEDOUT;
	}
	return 0;
}

/*
 * FIFO-mode chunk: up to MTK_FIFO_DEPTH bytes per call. Mirrors
 * coreboot/src/soc/mediatek/common/spi.c::do_transfer.
 */
static int mtk_spi_chunk(struct mtk_spi *bus, u8 *in, const u8 *out, u32 size)
{
	struct mtk_spi_regs *regs = bus->regs;
	u32 reg_val = 0;
	u32 i;
	int ret;

	clrsetbits_le32(&regs->cfg1,
			SPI_CFG1_PACKET_LEN_MASK | SPI_CFG1_PACKET_LOOP_MASK,
			(size - 1) << SPI_CFG1_PACKET_LEN_SHIFT);

	if (out) {
		for (i = 0; i < size; i++) {
			reg_val |= (u32)out[i] << ((i % 4) * 8);
			if (i % 4 == 3) {
				writel(reg_val, &regs->tx_data);
				reg_val = 0;
			}
		}
		if (i % 4 != 0)
			writel(reg_val, &regs->tx_data);
	} else {
		/*
		 * Full-duplex controller: must clock out something for an
		 * RX-only transfer. EC ignores MOSI here.
		 */
		u32 word_count = (size + 3) / 4;

		for (i = 0; i < word_count; i++)
			writel(MTK_ARBITRARY_VALUE, &regs->tx_data);
	}

	if (bus->state == MTK_SPI_IDLE) {
		setbits_le32(&regs->cmd, SPI_CMD_ACT);
		bus->state = MTK_SPI_PAUSE_IDLE;
	} else {
		setbits_le32(&regs->cmd, SPI_CMD_RESUME);
	}

	ret = mtk_spi_wait(regs);
	if (ret)
		return ret;

	if (in) {
		for (i = 0; i < size; i++) {
			if (i % 4 == 0)
				reg_val = readl(&regs->rx_data);
			in[i] = (reg_val >> ((i % 4) * 8)) & 0xffU;
		}
	} else {
		/* Drain RX FIFO so it doesn't carry over. */
		u32 word_count = (size + 3) / 4;

		for (i = 0; i < word_count; i++)
			(void)readl(&regs->rx_data);
	}
	return 0;
}

static int mtk_spi_xfer(struct udevice *dev, unsigned int bitlen,
			const void *dout, void *din, unsigned long flags)
{
	struct mtk_spi *bus = dev_get_priv(dev_get_parent(dev));
	struct mtk_spi_regs *regs = bus->regs;
	u32 size = bitlen / 8U;
	u32 offset = 0;
	int ret = 0;

	while (size) {
		u32 chunk = size > MTK_FIFO_DEPTH ? MTK_FIFO_DEPTH : size;
		const u8 *out = dout ? (const u8 *)dout + offset : NULL;
		u8 *in = din ? (u8 *)din + offset : NULL;

		ret = mtk_spi_chunk(bus, in, out, chunk);
		if (ret) {
			setbits_le32(&regs->cmd, SPI_CMD_RST);
			clrbits_le32(&regs->cmd, SPI_CMD_RST);
			bus->state = MTK_SPI_IDLE;
			break;
		}
		offset += chunk;
		size -= chunk;
	}

	return ret;
}

static int mtk_spi_set_speed(struct udevice *dev, uint hz)
{
	return 0;
}

static int mtk_spi_set_mode(struct udevice *dev, uint mode)
{
	return 0;
}

static int mtk_spi_probe(struct udevice *dev)
{
	struct mtk_spi *bus = dev_get_priv(dev);
	fdt_addr_t addr;
	int ret;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;

	bus->regs = (struct mtk_spi_regs *)(uintptr_t)addr;
	bus->state = MTK_SPI_IDLE;

	ret = gpio_request_by_name(dev, "cs-gpios", 0, &bus->cs_gpio,
				   GPIOD_IS_OUT);
	if (ret)
		return ret;

	/* Reset controller to a known state. */
	setbits_le32(&bus->regs->cmd, SPI_CMD_RST);
	clrbits_le32(&bus->regs->cmd, SPI_CMD_RST);

	/* Deassert CS so we don't leave the EC clocking out a dangling
	 * reply when probe finishes. */
	dm_gpio_set_value(&bus->cs_gpio, 0);
	return 0;
}

static const struct dm_spi_ops mtk_chromeos_spi_ops = {
	.claim_bus	= mtk_spi_claim_bus,
	.release_bus	= mtk_spi_release_bus,
	.xfer		= mtk_spi_xfer,
	.set_speed	= mtk_spi_set_speed,
	.set_mode	= mtk_spi_set_mode,
};

static const struct udevice_id mtk_chromeos_spi_ids[] = {
	{ .compatible = "mediatek,mt8196-spi" },
	{ }
};

U_BOOT_DRIVER(mtk_chromeos_spi) = {
	.name		= "mtk_chromeos_spi",
	.id		= UCLASS_SPI,
	.of_match	= mtk_chromeos_spi_ids,
	.ops		= &mtk_chromeos_spi_ops,
	.priv_auto	= sizeof(struct mtk_spi),
	.probe		= mtk_spi_probe,
};
