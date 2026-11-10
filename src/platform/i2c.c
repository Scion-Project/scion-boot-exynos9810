/*
 * Exynos 9810 High-Speed I2C (HSI2C) master read helper & MAX77705 MUIC probe.
 *
 * Confirmed from vendor DT:
 *   hsi2c@14360000 -> max77705 MUIC @ 0x66 (controls USB-C UART/USB path mux)
 *
 * Uses bounded polling timeouts so an unclocked bus or NACK can never stall boot.
 */
#include <stdint.h>
#include "exynos9810.h"
#include "lib.h"

/* HSI2C_CTL bits */
#define HSI2C_CTL_CS_ENB         (1u << 0)
#define HSI2C_CTL_MASTER         (1u << 3)
#define HSI2C_CTL_RXCHON         (1u << 6)
#define HSI2C_CTL_TXCHON         (1u << 7)
#define HSI2C_CTL_SW_RST         (1u << 31)

/* HSI2C_MANUAL_CMD bits */
#define HSI2C_CMD_READ_DATA      (1u << 4)
#define HSI2C_CMD_SEND_ACK       (1u << 5)
#define HSI2C_CMD_STOP_COND      (1u << 6)
#define HSI2C_CMD_RESTART_COND   (1u << 7)
#define HSI2C_CMD_START_COND     (1u << 8)
#define HSI2C_CMD_SEND_DATA      (1u << 2)
#define HSI2C_CMD_TX_DATA_SHIFT  24

/* HSI2C_INT_STAT bits */
#define HSI2C_INT_XFER_DONE      (1u << 7)
#define HSI2C_INT_NO_DEV_ACK     (1u << 9)

#ifdef PLATFORM_STARLTE
static int hsi2c_wait_xfer(uint64_t base)
{
	for (unsigned t = 0; t < 2000; t++) {
		uint32_t st = mmio_r32(base + HSI2C_INT_STAT);
		if (st & HSI2C_INT_NO_DEV_ACK) {
			mmio_w32(base + HSI2C_INT_STAT, st);
			return -2; /* NACK */
		}
		if (st & HSI2C_INT_XFER_DONE) {
			mmio_w32(base + HSI2C_INT_STAT, st);
			return 0;
		}
		udelay(2);
	}
	return -1; /* Timeout */
}

static int hsi2c_manual_cmd(uint64_t base, uint32_t cmd)
{
	mmio_w32(base + HSI2C_INT_STAT, 0xFFFFFFFFu);
	mmio_w32(base + HSI2C_MANUAL_CMD, cmd);
	return hsi2c_wait_xfer(base);
}
#endif

int hsi2c_read_reg(uint64_t bus_base, uint8_t chip_addr, uint8_t reg, uint8_t *val_out)
{
	if (!val_out)
		return -1;
#ifdef PLATFORM_STARLTE
	/* Ensure master mode is enabled (leave S-Boot timing registers intact) */
	mmio_setbits32(bus_base + HSI2C_CTL,
	               HSI2C_CTL_MASTER | HSI2C_CTL_TXCHON | HSI2C_CTL_RXCHON);

	/* 1. START + (chip_addr << 1 | W) */
	uint32_t w_addr = (uint32_t)(chip_addr << 1) << HSI2C_CMD_TX_DATA_SHIFT;
	if (hsi2c_manual_cmd(bus_base, w_addr | HSI2C_CMD_START_COND | HSI2C_CMD_SEND_DATA) != 0)
		goto stop_err;

	/* 2. Send register offset */
	uint32_t w_reg = (uint32_t)reg << HSI2C_CMD_TX_DATA_SHIFT;
	if (hsi2c_manual_cmd(bus_base, w_reg | HSI2C_CMD_SEND_DATA) != 0)
		goto stop_err;

	/* 3. RESTART + (chip_addr << 1 | R) */
	uint32_t r_addr = (uint32_t)((chip_addr << 1) | 1u) << HSI2C_CMD_TX_DATA_SHIFT;
	if (hsi2c_manual_cmd(bus_base, r_addr | HSI2C_CMD_RESTART_COND | HSI2C_CMD_SEND_DATA) != 0)
		goto stop_err;

	/* 4. Read 1 byte with NACK (SEND_ACK=0) + STOP */
	if (hsi2c_manual_cmd(bus_base, HSI2C_CMD_READ_DATA | HSI2C_CMD_STOP_COND) != 0)
		goto stop_err;

	*val_out = (uint8_t)((mmio_r32(bus_base + HSI2C_MANUAL_CMD) >> 16) & 0xFFu);
	return 0;

stop_err:
	(void)hsi2c_manual_cmd(bus_base, HSI2C_CMD_STOP_COND);
	return -1;
#else
	/* Under QEMU virt, return simulated MAX77705 MUIC ID/status */
	(void)bus_base;
	if (chip_addr == 0x66 || chip_addr == 0x25) {
		if (reg == 0x00) { *val_out = 0x05; return 0; } /* MAX77705 pass5 */
		if (reg == 0x01) { *val_out = 0x1C; return 0; } /* UART jig attached */
		if (reg == 0x07) { *val_out = 0x09; return 0; } /* UT1/UR1 muxed */
		*val_out = 0x00;
		return 0;
	}
	return -1;
#endif
}

void muic_init_and_probe(void)
{
#ifdef PLATFORM_STARLTE
	uint8_t id = 0, st1 = 0, ctrl1 = 0;
	if (hsi2c_read_reg(MUIC_I2C_BASE, MUIC_I2C_ADDR, MAX77705_MUIC_REG_ID, &id) == 0) {
		(void)hsi2c_read_reg(MUIC_I2C_BASE, MUIC_I2C_ADDR, MAX77705_MUIC_REG_STATUS1, &st1);
		(void)hsi2c_read_reg(MUIC_I2C_BASE, MUIC_I2C_ADDR, MAX77705_MUIC_REG_CTRL1, &ctrl1);
		kprintf("muic: MAX77705 @ 0x%02x on %p (ID=0x%02x STATUS1=0x%02x CTRL1=0x%02x)\n",
		        MUIC_I2C_ADDR, (void *)MUIC_I2C_BASE, id, st1, ctrl1);
	} else {
		kprintf("muic: hsi2c@%p probe skipped (S-Boot mux active)\n",
		        (void *)MUIC_I2C_BASE);
	}
#else
	uint8_t id = 0, ctrl1 = 0;
	(void)hsi2c_read_reg(0x14360000UL, 0x66, 0x00, &id);
	(void)hsi2c_read_reg(0x14360000UL, 0x66, 0x07, &ctrl1);
	kprintf("muic: simulated MAX77705 (ID=0x%02x CTRL1=0x%02x)\n", id, ctrl1);
#endif
}
