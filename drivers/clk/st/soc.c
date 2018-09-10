/*
 * Copyright (C) ST-Microelectronics SA 2014
 * Author:  Maxime Coquelin <maxime.coquelin@st.com> for ST-Microelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/clk-private.h>
#include <linux/clk-provider.h>
#include <linux/syscore_ops.h>

static LIST_HEAD(st_clk_list);

struct st_soc_clk {
	struct list_head list;
	struct clk *clk, *pclk, *pclk_save;
	bool disable_on_resume, restore_on_resume;
	unsigned long rate_save;
};

struct st_soc_clk_init {
	char *name;
	bool always_on, disable_on_resume, restore_on_resume;
	char *pclk;
};

#define SOC_CLK_INIT(_n)		\
	{				\
		.name = _n,		\
		.always_on = false,	\
	}

#define SOC_CLK_INIT_ALWAYS_ON(_n)	\
	{				\
		.name = _n,		\
		.always_on = true,	\
	}

#define SOC_CLK_INIT_STDBY_PARENT(_n, _pc)		\
	{				\
		.name = _n,	 \
		.pclk = _pc, \
	}

#define SOC_CLK_INIT_DIS_ON_RESUME(_n)	\
	{				\
		.name = _n,		\
		.disable_on_resume = true,	\
	}

#define SOC_CLK_RESTORE_ON_RESUME(_n)	\
	{				\
		.name = _n,		\
		.restore_on_resume = true,	\
	}

struct st_clk_tree {
	char *clk;
	char *pclk;
	unsigned long rate;
};

static const struct st_clk_tree *st_clktree;
static const struct st_clk_tree *st_ext_clktree;

#define CLKTREE_SET(_c, _pc, _r) \
	{			 \
		.clk = _c,	 \
		.pclk = _pc,	 \
		.rate = _r,	 \
	}

#define CLKTREE_SET_RATE(__c, __r) \
	CLKTREE_SET(__c, NULL, __r)

#define CLKTREE_SET_PARENT(__c, __p) \
	CLKTREE_SET(__c, __p, 0)

static const struct st_clk_tree stih415_clktree[] = {
	CLKTREE_SET_PARENT("CLK_S_PIX_MAIN", "CLK_M_PIX_MAIN_PIPE"),
	CLKTREE_SET_PARENT("CLK_S_PIX_AUX", "CLK_M_PIX_AUX_PIPE"),
	CLKTREE_SET_RATE("CLK_S_THSENS", 996000),
	{},
};

static const struct st_soc_clk_init stih415_soc_clks[] = {
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_REG_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLOCKGEN_DDR0"),
	SOC_CLK_INIT_ALWAYS_ON("CLOCKGEN_DDR1"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_IF_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_STAC_PHY"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_VTAC_TX_PHY"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_IF_1"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICM_DISP"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_CPU"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_ERAM"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_HVA_LMI"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_GPU"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_REG_10"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_STAC_PHY"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_STAC_SYS"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_TS"),
	{},
};

static const struct st_clk_tree stih416_clktree[] = {
	CLKTREE_SET_RATE("CLK_S_C_FS0_CH2", 625000),
	CLKTREE_SET("CLK_S_THSENS", "CLK_S_C_FS0_CH2", 156250),
	CLKTREE_SET_RATE("CLOCKGEN_E.pll", 510000000),
	CLKTREE_SET("CLK_M_MPELPC", "CLOCKGEN_E.pll", 600000),
	CLKTREE_SET_RATE("CLK_S_C_FS0_CH0", 297000000),
	CLKTREE_SET_PARENT("CLK_S_VCC_HD", "CLK_S_C_FS0_CH0"),
	CLKTREE_SET("CLK_S_PIX_MAIN", "CLK_S_VCC_HD", 297000000),
	CLKTREE_SET_PARENT("CLK_M_F_VCC_HD", "CLK_S_PIX_MAIN"),
	CLKTREE_SET_PARENT("CLK_M_PIX_MAIN_PIPE", "CLK_M_F_VCC_HD"),
	CLKTREE_SET("CLK_S_PIX_HDMI", "CLK_S_VCC_HD", 297000000),
	CLKTREE_SET("CLK_S_HDMI_REJECT_PLL", "CLK_S_VCC_HD", 297000000),

	CLKTREE_SET_RATE("CLK_S_C_VCC_SD", 108000000),
	CLKTREE_SET("CLK_S_PIX_AUX", "CLK_S_C_VCC_SD", 13500000),
	CLKTREE_SET_PARENT("CLK_M_F_VCC_SD", "CLK_S_PIX_AUX"),
	CLKTREE_SET_PARENT("CLK_M_PIX_AUX_PIPE", "CLK_M_F_VCC_SD"),
	CLKTREE_SET_PARENT("CLK_S_HDDAC", "CLK_S_C_VCC_SD"),
	CLKTREE_SET_PARENT("CLK_S_PIX_HD", "CLK_S_C_VCC_SD"),

	CLKTREE_SET_PARENT("CLK_S_TMDS_HDMI", "CLK_S_TMDS_FROMPHY"),

	CLKTREE_SET_RATE("CLK_M_HVA_FS", 350000000),
	CLKTREE_SET_PARENT("CLK_M_HVA", "CLK_M_HVA_FS"),
	{},
};

static const struct st_soc_clk_init stih416_soc_clks[] = {
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_REG_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_REG_LP_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLOCKGEN_DDR0"),
	SOC_CLK_INIT_ALWAYS_ON("CLOCKGEN_DDR1"),
	SOC_CLK_INIT("CLK_S_ICN_IF_0"),
	SOC_CLK_INIT("CLK_S_ICN_IF_2"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_STAC_PHY"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_ICN_IF_1"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_S_STAC_SYS"),
	SOC_CLK_INIT("CLK_M_ICM_LMI"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_CPU"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_STAC"),
	SOC_CLK_INIT("CLK_M_TX_ICN_TS"),
	SOC_CLK_INIT("CLK_M_ICN_REG_12"),
	SOC_CLK_INIT("CLK_M_ICN_REG_10"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_REG_11"),
	SOC_CLK_INIT("CLK_M_RX_ICN_TS"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_REG_13"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_STAC_PHY"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_STAC_SYS"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_COMPO"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_REG_14"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_VDP_2"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_TX_ICN_DMU_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_TX_ICN_DMU_1"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_RX_ICN_DMU_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_RX_ICN_DMU_1"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_ST231"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_VP8"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_ICN_BDISP"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_TX_ICN_GPU"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_RX_ICN_GPU"),
	SOC_CLK_INIT("CLK_M_RX_ICN_VDP_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_PIX_MAIN_PIPE"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_PIX_AUX_PIPE"),
	{},
};

static const struct st_clk_tree stih410_ext_clktree[] = {

	/* Clockgen C0 extension */
	CLKTREE_SET_RATE("CLK_MMC_0", 200000000),
	CLKTREE_SET_PARENT("CLK_MAIN_DISP", "CLK_S_C0_PLL1_ODF_0"),
	CLKTREE_SET_PARENT("CLK_AUX_DISP", "CLK_S_C0_PLL1_ODF_0"),
	CLKTREE_SET("CLK_COMPO_DVP", "CLK_S_C0_PLL1_ODF_0", 400000000),
	{},
};

static const struct st_clk_tree stih412_ext_clktree[] = {

	/* Clockgen C0 extension */
	/* Shared with STiH410 */
	CLKTREE_SET_PARENT("CLK_MAIN_DISP", "CLK_S_C0_PLL1_ODF_0"),
	CLKTREE_SET_PARENT("CLK_AUX_DISP", "CLK_S_C0_PLL1_ODF_0"),
	CLKTREE_SET_PARENT("CLK_COMPO_DVP", "CLK_S_C0_PLL1_ODF_0"),
	/* Extension for STiH312, STiH412 */
	CLKTREE_SET("CLK_ICN_GPU", "CLK_S_C0_PLL1_ODF_0", 400000000),
	{},
};

static const struct st_soc_clk_init stih410_soc_clks[] = {
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TMDS_HDMI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_LPC_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_VSENS_COMPO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MCHI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TSOUT_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TSOUT_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FRC1_REMOTE"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_STFE_FRC1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_SDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DENC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_AUX_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_REF_HDMIPHY"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_HDMI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DVO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_DVO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_HDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_HDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP4"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP3"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_PIP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_MAIN_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_USB2_PHY"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCMR10_MASTER"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_SPDIFF"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_HWPE_HADES"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PP_HADES"),
	SOC_CLK_INIT("CLK_COMPO_DVP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_AUX_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MAIN_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FLASH_PROMIP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_STFE_FRC2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_IC_BDISP_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_IC_BDISP_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_MMC_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MMC_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PROC_TP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_HVA"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FDMA"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_NAND"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DSS_LPC"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_PROC_STFE"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FC_HADES"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_JPEGDEC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_IC_LMI1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_ST231_AUD_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_ST231_GP_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_ST231_DMU"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_ICN_GPU"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_C0_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_QUADFS.pll"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_QUADFS.pll"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_QUADFS.pll"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_IC_LMI0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_SBC"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_RX_ICN_DMU"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_RX_ICN_HVA"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_TX_ICN_DMU"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ETH_PHY", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ETH_REF_PHYCLK", "CLK_SYSIN"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_LMI"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_A9"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_EXT2FA9", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_PP_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_VID_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_TX_ICN_DISP_1", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_TX_ICN_HADES", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_RX_ICN_HADES", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_CLUST_HADES", "CLK_SYSIN"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_REG_16"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ICN_CPU", "CLK_SYSIN"),
	{},
};

static const struct st_clk_tree stih407_clktree[] = {
	/* Clockgen D0 */
	CLKTREE_SET_RATE("CLK_S_D0_QUADFS.pll", 660000000),
	CLKTREE_SET_RATE("CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCM_0", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET_RATE("CLK_S_D0_FS0_CH1", 50000000),
	CLKTREE_SET("CLK_PCM_1", "CLK_S_D0_FS0_CH1", 50000000),
	CLKTREE_SET_RATE("CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCM_2", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET_RATE("CLK_S_D0_FS0_CH3", 50000000),
	CLKTREE_SET("CLK_SPDIFF", "CLK_S_D0_FS0_CH3", 50000000),

	/* Clockgen D2 */
	CLKTREE_SET_RATE("CLK_S_D2_QUADFS.pll", 660000000),
	CLKTREE_SET_PARENT("CLK_TMDS_HDMI", "CLK_TMDSOUT_HDMI"),

	/* Clockgen D3 */
	CLKTREE_SET_RATE("CLK_S_D3_QUADFS.pll", 660000000),
	CLKTREE_SET_RATE("CLK_S_D3_FS0_CH0", 27000000),
	CLKTREE_SET("CLK_STFE_FRC1", "CLK_S_D3_FS0_CH0", 27000000),
	CLKTREE_SET("CLK_FRC1_REMOTE", "CLK_S_D3_FS0_CH0", 27000000),
	CLKTREE_SET_RATE("CLK_S_D3_FS0_CH1", 108000000),
	CLKTREE_SET("CLK_TSOUT_0", "CLK_S_D3_FS0_CH1", 108000000),
	CLKTREE_SET("CLK_TSOUT_1", "CLK_S_D3_FS0_CH1", 108000000),
	CLKTREE_SET("CLK_MCHI", "CLK_S_D3_FS0_CH1", 27000000),
	CLKTREE_SET("CLK_LPC_0", "CLK_SYSIN", 600000),

	/* Clockgen C0 */
	CLKTREE_SET_RATE("CLK_S_C0_FS0_CH2", 27000000),
	CLKTREE_SET("CLK_DSS_LPC", "CLK_S_C0_FS0_CH2", 27000000),
	CLKTREE_SET_RATE("CLK_S_C0_FS0_CH3", 250000000),
	CLKTREE_SET_RATE("CLK_ETH_REF_PHYCLK", 125000000),
	CLKTREE_SET_RATE("CLK_ETH_PHY", 125000000),
	CLKTREE_SET_RATE("CLK_COMPO_DVP", 400000000),
	{},
};

static const struct st_soc_clk_init stih407_soc_clks[] = {
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TMDS_HDMI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_LPC_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_VSENS_COMPO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MCHI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TSOUT_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_TSOUT_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FRC1_REMOTE"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_STFE_FRC1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_SDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DENC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_AUX_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_REF_HDMIPHY"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_HDMI"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DVO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_DVO"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_HDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_HDDAC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP4"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP3"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_GDP1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_PIP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PIX_MAIN_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_SPDIFF"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PCM_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FDMA"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_NAND"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_HVA"),
	SOC_CLK_INIT("CLK_PROC_STFE"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_PROC_TP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MMC_0"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_MMC_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_JPEGDEC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_IC_BDISP_0"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_IC_BDISP_1"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_DSS_LPC"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_STFE_FRC2"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_FLASH_PROMIP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_MAIN_DISP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_AUX_DISP"),
	SOC_CLK_INIT("CLK_COMPO_DVP"),
	SOC_CLK_INIT_DIS_ON_RESUME("CLK_ICN_GPU"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_C0_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D0_QUADFS.pll"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D2_QUADFS.pll"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH0"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH1"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH2"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_FS0_CH3"),
	SOC_CLK_RESTORE_ON_RESUME("CLK_S_D3_QUADFS.pll"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_IC_LMI0"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ICN_CPU", "CLK_SYSIN"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_SBC"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_RX_ICN_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_RX_ICN_HVA", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_TX_ICN_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ETH_PHY", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ETH_REF_PHYCLK", "CLK_SYSIN"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_LMI"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_M_A9"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_EXT2FA9", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ST231_AUD_0", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ST231_GP_1", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_ST231_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_PP_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_VID_DMU", "CLK_SYSIN"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_TX_ICN_DISP_1", "CLK_SYSIN"),
	{},
};

static const struct st_clk_tree stid127_clktree[] = {
	CLKTREE_SET_RATE("CLK_USB_SRC", 540000000),
	CLKTREE_SET_RATE("CLK_ETH0", 25000000),
	CLKTREE_SET_RATE("CLOCKGEN_CCM_USB.gatediv", 540000000),
	CLKTREE_SET_RATE("CLK_USB_REF", 12000000),
	CLKTREE_SET_RATE("CLOCKGEN_CCM_LPC.gatediv", 540000),
	CLKTREE_SET_RATE("CLK_THERMAL_SENSE", 135000),
	CLKTREE_SET_RATE("CLK_LPC_COMMS", 40000),
	CLKTREE_SET_RATE("CLK_ZSI", 49152000),
	CLKTREE_SET_RATE("CLK_FDMA_TEL", 400000000),
	CLKTREE_SET_RATE("CLOCKGEN_CCM_TEL.gatediv", 49152000),
	CLKTREE_SET_RATE("CLK_ZSI_APPL", 1024000),
	CLKTREE_SET_RATE("CLK_DOC_VCO", 540000000),
	CLKTREE_SET_RATE("CLK_FP", 200000000),
	CLKTREE_SET_RATE("CLK_D3_XP70", 324000000),
	CLKTREE_SET_RATE("CLK_IFE", 216000000),
	{},
};

static const struct st_soc_clk_init stid127_soc_clks[] = {
	SOC_CLK_INIT("CLK_FDMA_TEL"),
	SOC_CLK_INIT("CLK_FP"),
	SOC_CLK_INIT("CLK_IFE"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP_DQAM", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT("CLK_D3_XP70"),
	SOC_CLK_INIT("CLK_TSOUT_SRC"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_GLOBAL_ROUTER",
				  "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_ROUTER", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_GLOBAL_NETWORK",
				  "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_CM_ST40", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_A9_EXT2F", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_MAIN", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_IC_DDR"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_CPU", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP_CPU", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_DMA", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP_ETH", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_PCIE", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_GLOBAL_PCI", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_GLOBAL_PCI_TARG",
				  "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP_D3", "CLK_S_A0_OSC_PREDIV"),
	SOC_CLK_INIT_STDBY_PARENT("CLK_IC_LP_HD", "CLK_S_A0_OSC_PREDIV"),
	{},
};

static const struct st_clk_tree sti8416_clktree[] = {
	/* Some Part of this configuration might move to Target Pack Later*/
	CLKTREE_SET("CLK_ST231_DMU_0", "CLK_S_B0_PLL0_ODF_0", 700000000),
	CLKTREE_SET("CLK_ST231_DMU_1", "CLK_S_B0_PLL0_ODF_0", 700000000),
	CLKTREE_SET("CLK_VID_DMU_0", "CLK_S_B0_PLL0_ODF_0", 700000000),
	CLKTREE_SET("CLK_VID_DMU_1", "CLK_S_B0_PLL0_ODF_0", 700000000),
	CLKTREE_SET("CLK_PP_DMU_0", "CLK_S_B0_PLL0_ODF_0", 280000000),
	CLKTREE_SET("CLK_PP_DMU_1", "CLK_S_B0_PLL0_ODF_0", 280000000),
	CLKTREE_SET("CLK_ICN_CAL2", "CLK_S_B0_PLL0_ODF_0", 700000000),
	CLKTREE_SET("CLK_ICN_VDEC1", "CLK_S_B0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_ICN_DISP", "CLK_S_B0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_ICN_HSIF", "CLK_S_B0_FS0_CH2", 333000000),
	CLKTREE_SET("CLK_IC_VDEC1", "CLK_S_B0_FS0_CH2", 333000000),
	CLKTREE_SET("CLK_VID_VP8", "CLK_S_B0_FS0_CH2", 333000000),
	CLKTREE_SET("CLK_IC_DISP", "CLK_S_B0_FS0_CH2", 333000000),
	CLKTREE_SET("CLK_ICN_REG_2", "CLK_S_B0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_ICN_REG_3", "CLK_S_B0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_PWM_0", "CLK_S_B0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_PWM_1", "CLK_S_B0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_PROMIP_DISP", "CLK_S_B0_PLL0_ODF_0", 50000000),

	CLKTREE_SET("CLK_PLL_VSAFE", "CLK_S_B1_PLL0_ODF_0", 213000000),
	CLKTREE_SET("CLK_ICN_NET", "CLK_S_B1_FS0_CH0", 333000000),
	CLKTREE_SET("CLK_ICN_TS", "CLK_S_B1_FS0_CH0", 333000000),
	CLKTREE_SET("CLK_ICN_VIDIN", "CLK_S_B1_PLL0_ODF_0", 533000000),
	CLKTREE_SET("CLK_IC_VIDIN", "CLK_S_B1_FS0_CH0", 333000000),
	CLKTREE_SET("CLK_ICN_REG_4", "CLK_S_B1_PLL0_ODF_0", 213000000),
	CLKTREE_SET("CLK_PROC_FVDP_MAIN", "CLK_S_B1_FS0_CH1", 400000000),
	CLKTREE_SET("CLK_PROC_FVDP_ENC_0", "CLK_S_B1_FS0_CH1", 400000000),
	CLKTREE_SET("CLK_PROC_FVDP_ENC_1", "CLK_S_B1_FS0_CH1", 400000000),
	CLKTREE_SET("CLK_VCPU_FVDP_MAIN", "CLK_S_B1_PLL0_ODF_0", 355000000),
	CLKTREE_SET("CLK_CORE_FP2", "CLK_S_B1_PLL0_ODF_0", 266000000),
	CLKTREE_SET("CLK_PROC_STBE", "CLK_S_B1_PLL0_ODF_0", 533000000),
	CLKTREE_SET("CLK_FRC2", "CLK_S_B1_FS0_CH2", 27000000),
	CLKTREE_SET("CLK_TSOUT_0", "CLK_S_B1_FS0_CH2", 108000000),
	CLKTREE_SET("CLK_TSOUT_1", "CLK_S_B1_FS0_CH2", 108000000),
	CLKTREE_SET("CLK_MCHI", "CLK_S_B1_FS0_CH2", 27000000),
	CLKTREE_SET("CLK_32KB_HSIF", "CLK_SYSIN", 937500),
	CLKTREE_SET("CLK_EXT_ETH_PHY_25", "CLK_S_B1_FS0_CH3", 25000000),
	CLKTREE_SET("CLK_ETH_125_50", "CLK_S_B1_FS0_CH3", 125000000),
	CLKTREE_SET("CLK_PROMIP_NTW", "CLK_S_B1_FS0_CH1", 50000000),
	CLKTREE_SET("CLK_ETH0_PHY_CLK_INT", "CLK_S_B1_FS0_CH3", 125000000),
	CLKTREE_SET("CLK_ETH1_PHY_CLK_INT", "CLK_S_B1_FS0_CH3", 125000000),
	CLKTREE_SET("CLK_ETH2_PHY_CLK_INT", "CLK_S_B1_FS0_CH3", 125000000),

	CLKTREE_SET("CLK_MMC_0", "CLK_S_C0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_NAND", "CLK_S_C0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_FLASH", "CLK_S_C0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_ICN_BD", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_ST231_GP0", "CLK_S_C0_PLL1_ODF_0", 700000000),
	CLKTREE_SET("CLK_ST231_GP1", "CLK_S_C0_PLL1_ODF_0", 700000000),
	CLKTREE_SET("CLK_ST231_SEC", "CLK_S_C0_PLL1_ODF_0", 700000000),
	CLKTREE_SET("CLK_CLUST_HVC", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_HWPE_HVC", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_FDMA_0_1", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_HVA", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_GBS", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_ICN_CCI", "CLK_S_C0_PLL0_ODF_0", 600000000),
	CLKTREE_SET("CLK_ICN_L0L1", "CLK_S_C0_PLL1_ODF_0", 700000000),
	CLKTREE_SET("CLK_ICN_ST231", "CLK_S_C0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_ICN_GBS", "CLK_S_C0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_ICN_VENC", "CLK_S_C0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_ICN_VDEC0", "CLK_S_C0_FS0_CH0", 500000000),
	CLKTREE_SET("CLK_PROC_JPEG", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_ICN_TSIF", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_ICN_FH", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_ICN_CCL2", "CLK_S_C0_PLL1_ODF_0", 700000000),
	CLKTREE_SET("CLK_IC_VDEC0", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_IC_VENC", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_IC_GBS", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_ICN_REG_0", "CLK_S_C0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_ICN_REG_1", "CLK_S_C0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_MMC_1", "CLK_S_C0_PLL0_ODF_0", 200000000),
	CLKTREE_SET("CLK_MMC_2", "CLK_S_C0_PLL0_ODF_0", 50000000),
	CLKTREE_SET("CLK_DDR4SS_0_EXT", "CLK_S_C0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_DDR4SS_1_EXT", "CLK_S_C0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_TIMER", "CLK_S_C0_FS0_CH2", 100000000),
	CLKTREE_SET("CLK_PROC_STFE", "CLK_S_C0_FS0_CH0", 250000000),
	CLKTREE_SET("CLK_ATCLK", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_TRACE", "CLK_S_C0_PLL0_ODF_0", 150000000),
	CLKTREE_SET("CLK_DSS", "CLK_S_C0_FS0_CH3", 36864000),
	CLKTREE_SET("CLK_FRC_1", "CLK_S_C0_FS0_CH0", 27000000),
	CLKTREE_SET("CLK_FRC1_PAD", "CLK_S_C0_FS0_CH0", 27000000),
	CLKTREE_SET("CLK_EXT_A5X", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_PMB_A5X", "CLK_S_C0_PLL0_ODF_0", 50000000),
	CLKTREE_SET("CLK_PMB_GPU", "CLK_S_C0_PLL0_ODF_0", 50000000),
	CLKTREE_SET("CLK_32KB_BOOTDEV", "CLK_SYSIN", 937500),
	CLKTREE_SET("CLK_SYS_OUT", "CLK_S_C0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_PROMIP_CPU", "CLK_S_C0_PLL0_ODF_0", 100000000),
	CLKTREE_SET("CLK_PP_HVC", "CLK_S_C0_PLL0_ODF_0", 400000000),
	CLKTREE_SET("CLK_FC_VDEC0", "CLK_S_C0_PLL1_ODF_0", 350000000),
	CLKTREE_SET("CLK_OSC_GPU", "CLK_SYSIN", 30000000),

	CLKTREE_SET("CLK_PCM_0", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCM_1", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCM_2", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCM_3", "CLK_S_D0_FS0_CH3", 50000000),
	CLKTREE_SET("CLK_PCM_4", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCMR_0_MASTER", "CLK_SYSIN", 30000000),
	CLKTREE_SET("CLK_PCMR_1_MASTER", "CLK_SYSIN", 30000000),
	CLKTREE_SET("CLK_PCMR_2_MASTER", "CLK_SYSIN", 30000000),
	CLKTREE_SET("CLK_PCMR_3_MASTER", "CLK_SYSIN", 30000000),
	CLKTREE_SET("CLK_PCMR_4_MASTER", "CLK_SYSIN", 30000000),
	CLKTREE_SET("CLK_PCM_TELSS", "CLK_S_D0_FS0_CH1", 8192000),
	CLKTREE_SET("CLK_ZARLINK", "CLK_S_D0_FS0_CH1", 49152000),
	CLKTREE_SET("CLK_PCMIN0_MCLK_OUT", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCMIN1_MCLK_OUT", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCMIN2_MCLK_OUT", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCMIN3_MCLK_OUT", "CLK_S_D0_FS0_CH3", 50000000),
	CLKTREE_SET("CLK_PCMIN4_MCLK_OUT", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCM_0_MCRU", "CLK_S_D0_FS0_CH0", 50000000),
	CLKTREE_SET("CLK_PCM_1_MCRU", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCM_2_MCRU", "CLK_S_D0_FS0_CH2", 50000000),
	CLKTREE_SET("CLK_PCM_3_MCRU", "CLK_S_D0_FS0_CH3", 50000000),

	CLKTREE_SET("CLK_PIX_MAIN_4K", "CLK_S_D1_FS0_CH0", 594000000),
	CLKTREE_SET("CLK_PIX_HDMI_TX", "CLK_S_D1_FS0_CH0", 594000000),
	CLKTREE_SET("CLK_PIX_DP_TX", "CLK_S_D1_FS0_CH0", 594000000),
	CLKTREE_SET("CLK_PIX_MAIN", "CLK_S_D1_FS0_CH0", 297000000),
	CLKTREE_SET("CLK_PIX_AUX_0", "CLK_S_D1_FS0_CH0", 297000000),
	CLKTREE_SET("CLK_PIX_AUX_1", "CLK_S_D1_FS0_CH0", 297000000),
	CLKTREE_SET("CLK_PIX_HDDAC", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_HDDAC", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_DVO", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_PIX_DVO", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_DENC", "CLK_S_D1_FS0_CH1", 27000000),
	CLKTREE_SET("CLK_SDDAC", "CLK_S_D1_FS0_CH1", 108000000),
	CLKTREE_SET("CLK_PROC_4K_COMPO", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_VP_0", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_VP_1", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_ALP_0", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_ALP_1", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_CAPT_0", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_CAPT_1", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_0", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_1", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_2", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_3", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_4", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_GDP_5", "CLK_S_D1_FS0_CH2", 400000000),
	CLKTREE_SET("CLK_PIX_LVDS", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_FRC_0", "CLK_S_D1_FS0_CH1", 27000000),
	CLKTREE_SET("CLK_FRC_0_PAD", "CLK_S_D1_FS0_CH1", 27000000),
	CLKTREE_SET("CLK_TMDS", "CLK_S_D1_FS0_CH0", 594000000),
	CLKTREE_SET("CLK_TMDS_DIV2", "CLK_S_D1_FS0_CH0", 297000000),
	CLKTREE_SET("CLK_REF_HDMI_PHY", "CLK_S_D1_FS0_CH0", 297000000),
	CLKTREE_SET("CLK_PIX_LVDS_PLL", "CLK_S_D1_FS0_CH0", 148500000),
	CLKTREE_SET("CLK_PROC_COMPO", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_PROC_HQVDP_0", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_IQI_HQVDP_0", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_PROC_HQVDP_1", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_IQI_HQVDP_1", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_FDMA_TELSS", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_FDMA_2", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_FREE_RUN_HDMI_PHASE_REG", "CLK_S_D1_FS0_CH3",
		    100000000),
	CLKTREE_SET("CLK_DP_LVDS_CCDS", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_DP_EXT_JE_REF_CLK", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_SCAN_DPTX_AUX_CLK", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_SCAN_DPTX_LSCLK", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_SCAN_UART_DIV_CLK", "CLK_S_D1_FS0_CH3", 400000000),
	CLKTREE_SET("CLK_SCAN_CLK_PLL_SYSCLK", "CLK_S_D1_FS0_CH3", 400000000),

	CLKTREE_SET("CLK_DP_TX_PLL_REF", "CLK_S_D2_FS0_CH0", 27000000),
	CLKTREE_SET("CLK_BYPASS_DP_PHY", "CLK_S_D2_FS0_CH0", 27000000),
	{},
};

static const struct st_soc_clk_init sti8416_soc_clks[] = {
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_REG_2"),
	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_REG_3"),

	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_REG_4"),

	SOC_CLK_INIT_ALWAYS_ON("CLK_ICN_REG_0"),
	{},
};

struct st_clk_data {
	const struct st_soc_clk_init *soc_clks;
	const struct st_clk_tree *clk_tree;
	const struct st_clk_tree *ext_clk_tree;
};

static void st_configure_clktree(const struct st_clk_tree *clktree)
{
	struct clk *clk, *pclk;
	int ret;

	if (!clktree)
		return;

	for (; clktree->clk; clktree++) {
		clk = __clk_lookup(clktree->clk);
		if (!clk) {
			pr_err("%s: %s not found\n", __func__, clktree->clk);
			continue;
		}

		pclk = __clk_lookup(clktree->pclk);
		if (pclk && clk_set_parent(clk, pclk))
			pr_err("%s: %s Failed to set %s parent\n", __func__,
			       clktree->clk, clktree->pclk);

		if (clktree->rate && (clk_get_rate(clk) != clktree->rate)) {
			ret = clk_set_rate(clk, clktree->rate);
			if (ret)
				pr_err("%s: %s Failed to set rate\n", __func__,
				       clktree->clk);
		}
	}
}

static int st_soc_add_clk(struct clk *clk,
			  const struct st_soc_clk_init *soc_init_clk)
{
	struct st_soc_clk *soc_clk;

	soc_clk = kzalloc(sizeof(struct st_soc_clk), GFP_KERNEL);
	if (!soc_clk)
		return -ENOMEM;

	soc_clk->clk = clk;
	soc_clk->disable_on_resume = soc_init_clk->disable_on_resume;
	soc_clk->restore_on_resume = soc_init_clk->restore_on_resume;
	if (soc_init_clk->pclk) {
		soc_clk->pclk = __clk_lookup(soc_init_clk->pclk);
		if (!soc_clk->pclk) {
			pr_err("%s: %s not found\n", __func__,
			       soc_init_clk->pclk);
			return -ENODEV;
		}
	}

	list_add_tail(&soc_clk->list, &st_clk_list);

	return 0;
}

static int st_soc_clk_init(const struct st_soc_clk_init *init)
{
	const struct st_soc_clk_init *soc_clk;
	int ret = 0;

	if (!init)
		return 0;

	for (soc_clk = init; soc_clk->name; soc_clk++) {
		struct clk *clk;

		clk = __clk_lookup(soc_clk->name);
		if (!clk) {
			pr_err("%s: %s not found\n", __func__, soc_clk->name);
			continue;
		}

		if (!soc_clk->always_on) {
			ret = st_soc_add_clk(clk, soc_clk);
			if (ret)
				return ret;
		}

		if (soc_clk->disable_on_resume || soc_clk->restore_on_resume)
			continue;

		ret = clk_prepare_enable(clk);
		if (ret)
			pr_err("%s: Fail to enable %s\n", __func__, clk->name);
	}

	return ret;
}

static void st_suspend_soc_clks(void)
{
	struct st_soc_clk *st_clk;

	list_for_each_entry(st_clk, &st_clk_list, list) {
		struct clk *clk = st_clk->clk;

		st_clk->rate_save = __clk_get_rate(clk);
		if (st_clk->pclk) {
			st_clk->pclk_save = clk_get_parent(clk);
			if (__clk_get_enable_count(clk) == 1)
				if (clk_set_parent(clk, st_clk->pclk))
					pr_err("%s: Fail to set parent during suspend %s\n",
					       __func__, clk->name);
		} else if (st_clk->disable_on_resume ||
			   st_clk->restore_on_resume) {
			continue;
		} else {
			clk_disable_unprepare(clk);
		}
	}
}

static void st_resume_soc_clks(void)
{
	struct st_soc_clk *st_clk;

	list_for_each_entry_reverse(st_clk, &st_clk_list, list) {
		struct clk *clk = st_clk->clk;

		if (st_clk->pclk) {
			if (__clk_get_enable_count(clk) == 1)
				if (clk_set_parent(clk, st_clk->pclk_save))
					pr_err("%s: Fail set_parent %s\n",
					       __func__, clk->name);
			if (st_clk->rate_save != clk_get_rate(clk))
				clk_set_rate(clk, st_clk->rate_save);
		} else if (st_clk->disable_on_resume) {
			if (clk->ops->set_parent)
				clk_set_parent(clk, clk_get_parent(clk));
			if (st_clk->rate_save != clk_get_rate(clk))
				clk_set_rate(clk, st_clk->rate_save);
			if (__clk_get_enable_count(clk) == 0 &&
			    __clk_is_enabled(clk)) {
				if (clk_prepare_enable(clk))
					pr_err("%s: Fail to enable %s\n",
					       __func__, clk->name);
				else
					clk_disable_unprepare(clk);
			}
		} else if (st_clk->restore_on_resume) {
			if (clk->ops->set_parent)
				clk_set_parent(clk, clk_get_parent(clk));
			if (st_clk->rate_save != clk_get_rate(clk))
				clk_set_rate(clk, st_clk->rate_save);
		} else {
			if (clk->ops->set_parent)
				clk_set_parent(clk, clk_get_parent(clk));
			if (st_clk->rate_save != clk_get_rate(clk))
				clk_set_rate(clk, st_clk->rate_save);
			if (clk_prepare_enable(clk))
				pr_err("%s: Fail to enable %s\n", __func__,
				       clk->name);
		}
	}
}

static int st_clk_suspend(void)
{
	st_suspend_soc_clks();

	return 0;
}

static void st_clk_resume(void)
{
	st_resume_soc_clks();
	if (st_clktree != stih407_clktree)
		st_configure_clktree(st_clktree);
}

static struct syscore_ops st_clk_syscore_ops = {
	.suspend	= st_clk_suspend,
	.resume		= st_clk_resume,
};

static int st_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct st_clk_data *data = NULL;
	int ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	data = (struct st_clk_data *)match->data;
	st_clktree = data->clk_tree;
	st_ext_clktree = data->ext_clk_tree;

	ret = st_soc_clk_init(data->soc_clks);
	if (ret) {
		dev_err(dev, "Failed to init clks (%d)\n", ret);
		return ret;
	}

	st_configure_clktree(st_clktree);
	st_configure_clktree(st_ext_clktree);
	register_syscore_ops(&st_clk_syscore_ops);

	dev_info(dev, "SoC clock tree intialized\n");

	return 0;
}

static struct st_clk_data stih416_clkdata = {
	.soc_clks = stih416_soc_clks,
	.clk_tree = stih416_clktree,
};

static struct st_clk_data stih410_clkdata = {
	.soc_clks = stih410_soc_clks,
	.clk_tree = stih407_clktree,
	.ext_clk_tree = stih410_ext_clktree,
};

static struct st_clk_data stih412_clkdata = {
	.soc_clks = stih410_soc_clks,
	.clk_tree = stih407_clktree,
	.ext_clk_tree = stih412_ext_clktree,
};

static struct st_clk_data stih407_clkdata = {
	.soc_clks = stih407_soc_clks,
	.clk_tree = stih407_clktree,
};

static struct st_clk_data stid127_clkdata = {
	.soc_clks = stid127_soc_clks,
	.clk_tree = stid127_clktree,
};

static struct st_clk_data sti8416_clkdata = {
	.soc_clks = sti8416_soc_clks,
	.clk_tree = sti8416_clktree,
};

static struct of_device_id st_clk_match[] = {
	{ .compatible = "st,stih416-clk", .data = &stih416_clkdata },
	{ .compatible = "st,stih410-clk", .data = &stih410_clkdata },
	{ .compatible = "st,stih412-clk", .data = &stih412_clkdata },
	{ .compatible = "st,stih407-clk", .data = &stih407_clkdata },
	{ .compatible = "st,stid127-clk", .data = &stid127_clkdata },
	{ .compatible = "st,sti8416-clk", .data = &sti8416_clkdata },
	{},
};

static struct platform_driver st_clk_driver = {
	.probe = st_clk_probe,
	.driver = {
		.name = "st-clk",
		.owner = THIS_MODULE,
		.of_match_table = st_clk_match,
	},
};

static int __init st_clk_init(void)
{
	return platform_driver_register(&st_clk_driver);
}
arch_initcall(st_clk_init);
