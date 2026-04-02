// SPDX-License-Identifier: GPL-2.0-only
/*
 * CVITEK C906L (little core) reset syscall
 *
 * Uses custom SBI extension calls to reset/unreset the C906L core
 * for FreeRTOS dual-core operation. Requires CVITEK-patched OpenSBI.
 *
 * TODO: Implement using sbi_ecall() with CVITEK vendor extension ID
 */

#include <linux/clk.h>
#include <linux/syscalls.h>
#include <asm/sbi.h>

/* CVITEK SBI extension for C906L core management */
#define SBI_EXT_CVITEK_C906L	0x09000001
#define SBI_FUNCT_RST		0
#define SBI_FUNCT_UNRST		1
#define SBI_FUNCT_RESET		2

static int sbi_rst_c906l(void)
{
	struct sbiret ret = sbi_ecall(SBI_EXT_CVITEK_C906L, SBI_FUNCT_RST,
				      0, 0, 0, 0, 0, 0);
	return ret.error ? -EIO : ret.value;
}

static int sbi_unrst_c906l(unsigned long address)
{
	struct sbiret ret = sbi_ecall(SBI_EXT_CVITEK_C906L, SBI_FUNCT_UNRST,
				      address, 0, 0, 0, 0, 0);
	return ret.error ? -EIO : ret.value;
}

static int sbi_reset_c906l(unsigned long address)
{
	struct sbiret ret = sbi_ecall(SBI_EXT_CVITEK_C906L, SBI_FUNCT_RESET,
				      address, 0, 0, 0, 0, 0);
	return ret.error ? -EIO : ret.value;
}

SYSCALL_DEFINE2(reset_c906l, int, reset, const unsigned long, address)
{
	int ret = -1;
	struct clk *efuse_clk = NULL;

	efuse_clk = clk_get_sys(NULL, "clk_efuse");
	if (IS_ERR(efuse_clk)) {
		pr_err("%s: efuse clock not found %ld\n", __func__,
			PTR_ERR(efuse_clk));
		return -1;
	}

	clk_prepare_enable(efuse_clk);
	switch (reset) {
	case 0:
		ret = sbi_rst_c906l();
		break;
	case 1:
		ret = sbi_unrst_c906l(address);
		break;
	case 2:
		ret = sbi_reset_c906l(address);
		break;
	default:
		break;
	}

	clk_disable_unprepare(efuse_clk);
	return ret;
}
