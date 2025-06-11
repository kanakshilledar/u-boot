// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 * Ported to U-Boot
 */

#include <clk-uclass.h>
#include <dm.h>
#include <dt-bindings/clock/th1520-miscsys.h>
#include <regmap.h>
#include <syscon.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

struct th1520_miscsys_clk_priv {
	struct regmap *miscsys_regmap;
	struct regmap *tee_miscsys_regmap;
};

/* Clock gate register definitions */
#define MISCSYS_GATE_REG_0x100		0x100
#define MISCSYS_GATE_REG_0x104		0x104
#define MISCSYS_GATE_REG_0x108		0x108
#define MISCSYS_GATE_REG_0x10C		0x10c
#define MISCSYS_GATE_REG_0x110		0x110
#define TEE_MISCSYS_GATE_REG_0x120	0x120

struct th1520_gate_info {
	u32 reg_offset;
	u32 bit_idx;
	const char *name;
	bool use_tee_regmap;
};

static const struct th1520_gate_info gate_info[] = {
	[CLKGEN_MISCSYS_MISCSYS_ACLK] = {
		.reg_offset = 0x100, .bit_idx = 0,
		.name = "clkgen_missys_aclk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_USB3_DRD_CLK] = {
		.reg_offset = 0x104, .bit_idx = 0,
		.name = "clkgen_usb3_drd_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_USB3_DRD_CTRL_REF_CLK] = {
		.reg_offset = 0x104, .bit_idx = 1,
		.name = "clkgen_usb3_drd_ctrl_ref_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_USB3_DRD_PHY_REF_CLK] = {
		.reg_offset = 0x104, .bit_idx = 2,
		.name = "clkgen_usb3_drd_phy_ref_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_USB3_DRD_SUSPEND_CLK] = {
		.reg_offset = 0x104, .bit_idx = 3,
		.name = "clkgen_usb3_drd_suspend_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_EMMC_CLK] = {
		.reg_offset = 0x108, .bit_idx = 0,
		.name = "clkgen_emmc_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_SDIO0_CLK] = {
		.reg_offset = 0x10c, .bit_idx = 0,
		.name = "clkgen_sdio0_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_SDIO1_CLK] = {
		.reg_offset = 0x110, .bit_idx = 0,
		.name = "clkgen_sdio1_clk", .use_tee_regmap = false
	},
	[CLKGEN_MISCSYS_AHB2_TEESYS_HCLK] = {
		.reg_offset = 0x120, .bit_idx = 0,
		.name = "clkgen_ahb2_teesys_hclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_APB3_TEESYS_HCLK] = {
		.reg_offset = 0x120, .bit_idx = 1,
		.name = "clkgen_apb3_teesys_hclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_AXI4_TEESYS_ACLK] = {
		.reg_offset = 0x120, .bit_idx = 2,
		.name = "clkgen_axi4_teesys_aclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_EIP120SI_CLK] = {
		.reg_offset = 0x120, .bit_idx = 3,
		.name = "clkgen_eip120si_clk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_EIP120SII_CLK] = {
		.reg_offset = 0x120, .bit_idx = 4,
		.name = "clkgen_eip120sii_clk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_EIP120SIII_CLK] = {
		.reg_offset = 0x120, .bit_idx = 5,
		.name = "clkgen_eip120siii_clk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_TEEDMAC_CLK] = {
		.reg_offset = 0x120, .bit_idx = 6,
		.name = "clkgen_teedmac_clk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_EIP150B_HCLK] = {
		.reg_offset = 0x120, .bit_idx = 7,
		.name = "clkgen_eip150b_hclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_OCRAM_HCLK] = {
		.reg_offset = 0x120, .bit_idx = 8,
		.name = "clkgen_ocram_hclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_EFUSE_PCLK] = {
		.reg_offset = 0x120, .bit_idx = 9,
		.name = "clkgen_efuse_pclk", .use_tee_regmap = true
	},
	[CLKGEN_MISCSYS_TEE_SYSREG_PCLK] = {
		.reg_offset = 0x120, .bit_idx = 10,
		.name = "clkgen_tee_sysreg_pclk", .use_tee_regmap = true
	},
};

static int th1520_miscsys_clk_enable(struct clk *clk)
{
	struct th1520_miscsys_clk_priv *priv = dev_get_priv(clk->dev);
	const struct th1520_gate_info *info;
	struct regmap *regmap;
	u32 val;

	if (clk->id >= CLKGEN_MISCSYS_CLK_END) {
		printf("Invalid clock ID: %lu\n", clk->id);
		return -EINVAL;
	}

	info = &gate_info[clk->id];
	regmap = info->use_tee_regmap ? priv->tee_miscsys_regmap : priv->miscsys_regmap;

	if (!regmap) {
		printf("Missing regmap for clock %s\n", info->name);
		return -ENODEV;
	}

	/* Read current value */
	regmap_read(regmap, info->reg_offset, &val);
	
	/* Set the gate bit to enable clock */
	val |= BIT(info->bit_idx);
	
	/* Write back */
	regmap_write(regmap, info->reg_offset, val);

	debug("Enabled clock %s (reg: 0x%x, bit: %d)\n", 
	      info->name, info->reg_offset, info->bit_idx);

	return 0;
}

static int th1520_miscsys_clk_disable(struct clk *clk)
{
	struct th1520_miscsys_clk_priv *priv = dev_get_priv(clk->dev);
	const struct th1520_gate_info *info;
	struct regmap *regmap;
	u32 val;

	if (clk->id >= CLKGEN_MISCSYS_CLK_END) {
		printf("Invalid clock ID: %lu\n", clk->id);
		return -EINVAL;
	}

	info = &gate_info[clk->id];
	regmap = info->use_tee_regmap ? priv->tee_miscsys_regmap : priv->miscsys_regmap;

	if (!regmap) {
		printf("Missing regmap for clock %s\n", info->name);
		return -ENODEV;
	}

	/* Read current value */
	regmap_read(regmap, info->reg_offset, &val);
	
	/* Clear the gate bit to disable clock */
	val &= ~BIT(info->bit_idx);
	
	/* Write back */
	regmap_write(regmap, info->reg_offset, val);

	debug("Disabled clock %s (reg: 0x%x, bit: %d)\n", 
	      info->name, info->reg_offset, info->bit_idx);

	return 0;
}

static const struct clk_ops th1520_miscsys_clk_ops = {
	.enable = th1520_miscsys_clk_enable,
	.disable = th1520_miscsys_clk_disable,
};

static int th1520_miscsys_clk_probe(struct udevice *dev)
{
	struct th1520_miscsys_clk_priv *priv = dev_get_priv(dev);
	struct udevice *syscon_dev;
	int ret;

	/* Get miscsys regmap */
	ret = uclass_get_device_by_phandle(UCLASS_SYSCON, dev, "miscsys-regmap", &syscon_dev);
	if (ret) {
		printf("Cannot find miscsys regmap: %d\n", ret);
		return ret;
	}
	priv->miscsys_regmap = syscon_get_regmap(syscon_dev);
	if (IS_ERR(priv->miscsys_regmap)) {
		printf("Cannot get miscsys regmap\n");
		return PTR_ERR(priv->miscsys_regmap);
	}

	/* Get tee-miscsys regmap (optional) */
	ret = uclass_get_device_by_phandle(UCLASS_SYSCON, dev, "tee-miscsys-regmap", &syscon_dev);
	if (ret) {
		printf("Warning: Cannot find tee-miscsys regmap: %d\n", ret);
		priv->tee_miscsys_regmap = NULL;
	} else {
		priv->tee_miscsys_regmap = syscon_get_regmap(syscon_dev);
		if (IS_ERR(priv->tee_miscsys_regmap)) {
			printf("Warning: Cannot get tee-miscsys regmap\n");
			priv->tee_miscsys_regmap = NULL;
		}
	}

	printf("TH1520 miscsys clock gate provider initialized\n");
	return 0;
}

static const struct udevice_id th1520_miscsys_clk_ids[] = {
	{ .compatible = "xuantie,miscsys-gate-controller" },
	{ }
};

U_BOOT_DRIVER(th1520_miscsys_clk) = {
	.name = "th1520-miscsys-clk",
	.id = UCLASS_CLK,
	.of_match = th1520_miscsys_clk_ids,
	.priv_auto = sizeof(struct th1520_miscsys_clk_priv),
	.ops = &th1520_miscsys_clk_ops,
	.probe = th1520_miscsys_clk_probe,
	.flags = DM_FLAG_PRE_RELOC,
};