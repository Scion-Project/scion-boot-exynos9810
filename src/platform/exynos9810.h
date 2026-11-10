/*
 * Exynos 9810 (starlte / crownlte) and QEMU virt platform definitions.
 *
 * Addresses confirmed from the Exynos 9810 vendor device tree and GPL kernel
 * headers are documented below. Any unconfirmed register retains an explicit
 * 0xDEADxxxx placeholder so IS_PLACEHOLDER() catches accidental live use.
 */
#ifndef EXYNOS9810_H
#define EXYNOS9810_H

#include <stdint.h>

static inline void mmio_w32(uint64_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t mmio_r32(uint64_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static inline void mmio_setbits32(uint64_t a, uint32_t set)
{
	mmio_w32(a, mmio_r32(a) | set);
}

static inline void mmio_clrbits32(uint64_t a, uint32_t clr)
{
	mmio_w32(a, mmio_r32(a) & ~clr);
}

#if defined(PLATFORM_QEMU)

#define PLATFORM_NAME       "qemu-virt (smoke test)"
#define UART_BASE           0x09000000UL   /* QEMU virt PL011 */
#define UART_KIND_PL011     1
#define ARCH_TIMER_DEFAULT_HZ 62500000ULL  /* 62.5 MHz fallback if CNTFRQ==0 */

#elif defined(PLATFORM_STARLTE)

#define PLATFORM_NAME       "starlte (Exynos 9810)"
#define UART_KIND_EXYNOS    1
#define ARCH_TIMER_DEFAULT_HZ 26000000ULL  /* Exynos 9810 26 MHz FIN/OSCCLK */

/* -------------------------------------------------------------------------
 * PMU (Power Management Unit): system-controller@14060000
 * ------------------------------------------------------------------------- */
#define PMU_BASE            0x14060000UL
#define PMU_OM_STAT         (PMU_BASE + 0x0000) /* Operating Mode status (boot dev) */
#define PMU_SWRESET         (PMU_BASE + 0x0400) /* write 0x1 to warm-reset SoC */
#define PMU_RST_STAT        (PMU_BASE + 0x0404) /* reset cause status */

/* Alive scratchpad / inform registers preserved across warm reset */
#define PMU_INFORM0         (PMU_BASE + 0x0800) /* S5P_INFORM0 */
#define PMU_INFORM1         (PMU_BASE + 0x0804) /* S5P_INFORM1 */
#define PMU_INFORM2         (PMU_BASE + 0x0808) /* boot mode / recovery magic */
#define PMU_INFORM3         (PMU_BASE + 0x080C) /* reset reason */
#define PMU_SYSIP_DAT0      (PMU_BASE + 0x0810) /* secondary CPU jump / IPC */

/* Watchdog reset mask & power hold */
#define PMU_MASK_WDT_RESET  (PMU_BASE + 0x040C)
#define PMU_PS_HOLD_CONTROL (PMU_BASE + 0x330C) /* bit 8: PS_HOLD output high */

/* Per-CPU power control: Cluster 0 = Cortex-A55 (0..3), Cluster 1 = M3 (4..7) */
#define PMU_CPU_STRIDE      0x80UL
#define PMU_CPU_CONFIG(n)   (PMU_BASE + 0x2000UL + ((uint64_t)(n) * PMU_CPU_STRIDE))
#define PMU_CPU_STATUS(n)   (PMU_BASE + 0x2004UL + ((uint64_t)(n) * PMU_CPU_STRIDE))
#define PMU_CPU_OPTION(n)   (PMU_BASE + 0x2008UL + ((uint64_t)(n) * PMU_CPU_STRIDE))
#define PMU_LOCAL_PWR_CFG   0x0000000Fu

/* Common Samsung bootloader INFORM2/INFORM3 magic values */
#define EXYNOS_BOOT_RECOVERY_MAGIC 0x5245434Fu /* 'RECO' */
#define EXYNOS_BOOT_WARM_MAGIC     0x5741524Du /* 'WARM' */
#define EXYNOS_BOOT_USBDL_MAGIC    0x55534244u /* 'USBD' */
#define EXYNOS_BOOT_ODIN_MAGIC     0x12345678u /* stock Odin download mode */

/* Boot-loop guard tag stored in PMU_INFORM1 (upper 24 bits = 'SCB', low 8 = count) */
#define SCION_BOOTCOUNT_MAGIC_MASK 0xFFFFFF00u
#define SCION_BOOTCOUNT_MAGIC_VAL  0x53434200u /* 'SCB\0' */
#define SCION_MAX_BOOT_ATTEMPTS    3u

/* PS_HOLD values (PMU_PS_HOLD_CONTROL @ 0x1406330C) */
#define PMU_PS_HOLD_VAL_LATCH      0x00005300u /* keep PMIC power rails on */
#define PMU_PS_HOLD_VAL_OFF        0x00005200u /* release PS_HOLD -> clean poweroff */

/* -------------------------------------------------------------------------
 * Exynos 9810 BootROM (IROM) & IRAM (0x02020000) USB Download Mode (usbdl)
 * Compatible with halal-beef/hubble & halal-beef/houston-pub (04e8:1234 DNW).
 *
 * Credits:
 *   - Umer Uddin (@halal-beef) — creator of Hubble & Houston
 *   - @Robotix22 — Exynos 9810 (starlte) IROM/IRAM research (open-mini-bl1)
 * ------------------------------------------------------------------------- */
#define EXYNOS9810_IROM_BASE       0x00000000UL
#define EXYNOS9810_IROM_SIZE       0x00020000UL
#define EXYNOS9810_IRAM_BASE       0x02020000UL
#define EXYNOS9810_IRAM_SIZE       0x00040000UL

#define IRAM_STATUS_REG0           (EXYNOS9810_IRAM_BASE + 0x0070UL)
#define IRAM_STATUS_REG1           (EXYNOS9810_IRAM_BASE + 0x0074UL)
#define PTR_USB_RECEIVE            (EXYNOS9810_IRAM_BASE + 0x00DCUL)
#define PTR_USB_REINIT             (EXYNOS9810_IRAM_BASE + 0x00E0UL)
#define PTR_SET_STATUS_BIT         (EXYNOS9810_IRAM_BASE + 0x00E8UL)
#define PTR_USLEEP_RANGE           (EXYNOS9810_IRAM_BASE + 0x00F4UL)
#define IRAM_USB_STRUCT_ADDR       (EXYNOS9810_IRAM_BASE + 0x1000UL)
#define IRAM_USBDL_BUF_ADDR        (EXYNOS9810_IRAM_BASE + 0x4800UL)
#define IRAM_USBDL_BUF_SIZE        0x2000UL
#define IRAM_USB_TRB_ADDR          0x02037000UL

#define USB_PHY_CTRL_REG           0x11100004UL
#define DWC3_DEPCMD_PAR1           0x10C0C834UL
#define DWC3_DEPCMD_PAR0           0x10C0C838UL
#define DWC3_DEPCMD                0x10C0C83CUL

/* -------------------------------------------------------------------------
 * Alive Pin Controller (pinctrl@14050000) — Hardware Recovery Buttons
 *   GPA0_3 = Volume Up   (active-low)
 *   GPA0_4 = Volume Down (active-low)
 *   GPA0_6 = Bixby/Wink  (active-low)
 * ------------------------------------------------------------------------- */
#define ALIVE_PINCTRL_BASE         0x14050000UL
#define GPA0_CON                   (ALIVE_PINCTRL_BASE + 0x0020UL)
#define GPA0_DAT                   (ALIVE_PINCTRL_BASE + 0x0024UL)
#define GPA0_PUD                   (ALIVE_PINCTRL_BASE + 0x0028UL)
#define GPA0_PIN_VOLUP             (1u << 3)
#define GPA0_PIN_VOLDOWN           (1u << 4)
#define GPA0_PIN_BIXBY             (1u << 6)

/* -------------------------------------------------------------------------
 * Display & Enhancement Controller (DECON0 / decon_f@16030000) & Framebuffer
 * Confirmed from exynos9810-starlte DT and uniLoader (Markuss Broks /
 * Ivaylo Ivanov): S-Boot leaves DECON_HW_SW_TRIG_CONTROL unset; writing
 * 0x1281 enables automatic scanout of the 1440x2960x32bpp window at 0xCC000000.
 * ------------------------------------------------------------------------- */
#define DECON_F_BASE               0x16030000UL
#define DECON_HW_SW_TRIG_CONTROL   0x0070UL
#define DECON_TRIG_UNMASK_VAL      0x00001281u

#define STARLTE_FB_BASE            0xCC000000ULL
#define STARLTE_FB_WIDTH           1440u
#define STARLTE_FB_HEIGHT          2960u
#define STARLTE_FB_BPP             32u
#define STARLTE_FB_STRIDE          (STARLTE_FB_WIDTH * 4u)
#define STARLTE_FB_SIZE            0x01100000ULL /* 17 MiB carveout */

/* -------------------------------------------------------------------------
 * High-Speed I2C (HSI2C): hsi2c@14360000 (MAX77705 MUIC @ 0x66)
 * ------------------------------------------------------------------------- */
#define HSI2C_CTL                  0x0000UL
#define HSI2C_FIFO_CTL             0x0004UL
#define HSI2C_TRAILING_CTL         0x0008UL
#define HSI2C_INT_EN               0x0020UL
#define HSI2C_INT_STAT             0x0024UL
#define HSI2C_FIFO_STAT            0x0030UL
#define HSI2C_TXDATA               0x0034UL
#define HSI2C_RXDATA               0x0038UL
#define HSI2C_CONF                 0x0040UL
#define HSI2C_AUTO_CONF            0x0044UL
#define HSI2C_TIMEOUT              0x0048UL
#define HSI2C_MANUAL_CMD           0x004CUL
#define HSI2C_TRANS_STATUS         0x0050UL
#define HSI2C_ADDR                 0x0070UL

/* MAX77705 MUIC registers (I2C 7-bit address 0x66, or 0x25 depending on block) */
#define MAX77705_MUIC_REG_ID       0x00u
#define MAX77705_MUIC_REG_STATUS1  0x01u
#define MAX77705_MUIC_REG_STATUS2  0x02u
#define MAX77705_MUIC_REG_CTRL1    0x07u

/* -------------------------------------------------------------------------
 * Write-Protected Hardware Ranges (OTP / eFuse / UFS Flash Controller)
 * Stage 1 must NEVER write to non-volatile storage or fuse banks.
 * ------------------------------------------------------------------------- */
#define OTP_CON_BASE               0x10000000UL
#define OTP_CON_SIZE               0x00010000UL
#define UFS_HCI_BASE               0x11120000UL
#define UFS_HCI_SIZE               0x00010000UL

/* -------------------------------------------------------------------------
 * USI / Early Console UART: uart0 / ecduart @ 0x10440000
 * ------------------------------------------------------------------------- */
#define UART_BASE           0x10440000UL

/* MUIC (MAX77705 @ I2C 0x66 on hsi2c@14360000) */
#define MUIC_I2C_BASE       0x14360000UL
#define MUIC_I2C_ADDR       0x66

/* -------------------------------------------------------------------------
 * Watchdog Timer (S3C6410/Exynos WDT): wdt@10050000 (CL0) and wdt@10060000 (CL1)
 * ------------------------------------------------------------------------- */
#define WDT_CL0_BASE        0x10050000UL
#define WDT_CL1_BASE        0x10060000UL
#define WDT_WTCON           0x00
#define WDT_WTDAT           0x04
#define WDT_WTCNT           0x08
#define WDT_WTCLRINT        0x0C
#define WDT_WTCON_RSTEN     (1u << 0)
#define WDT_WTCON_INTEN     (1u << 2)
#define WDT_WTCON_ENABLE    (1u << 5)

/* -------------------------------------------------------------------------
 * Multi-Core Timer (MCT): mct@10040000
 * ------------------------------------------------------------------------- */
#define MCT_BASE            0x10040000UL
#define MCT_CFG             (MCT_BASE + 0x0000)
#define MCT_G_CNT_L         (MCT_BASE + 0x0100)
#define MCT_G_CNT_U         (MCT_BASE + 0x0104)
#define MCT_G_CNT_WSTAT     (MCT_BASE + 0x0110)
#define MCT_G_TCON          (MCT_BASE + 0x0240)
#define MCT_G_TCON_START    (1u << 8)

/* -------------------------------------------------------------------------
 * Clock Management Units (CMU) — bases confirmed from Exynos 9810 DT
 * ------------------------------------------------------------------------- */
#define CMU_TOP_BASE        0x12060000UL   /* clock-controller@12060000 (CMU_TOP) */
#define CMU_BUSC_BASE       0x12200000UL   /* clock-controller@12200000 (CMU_BUSC) */
#define CMU_PERIC0_BASE     0x10400000UL   /* clock-controller@10400000 (UART0/USI) */
#define CMU_APM_BASE        0x14000000UL   /* clock-controller@14000000 (CMU_APM) */
#define CMU_MIF0_BASE       0x1A000000UL   /* clock-controller@1a000000 (DRAM ch0) */
#define CMU_MIF1_BASE       0x1A010000UL   /* clock-controller@1a010000 (DRAM ch1) */
#define CMU_MIF2_BASE       0x1A020000UL   /* clock-controller@1a020000 (DRAM ch2) */
#define CMU_MIF3_BASE       0x1A030000UL   /* clock-controller@1a030000 (DRAM ch3) */

/* Standard Exynos9810 PLL_CON0 offset & lock status bit */
#define CMU_PLL_LOCKTIME    0x0000UL
#define CMU_PLL_CON0        0x0100UL
#define CMU_PLL_CON0_STABLE (1u << 29)
#define CMU_PLL_CON0_ENABLE (1u << 31)

/* -------------------------------------------------------------------------
 * DREX / DMC (LPDDR4x 4-channel controller + PHY)
 * Leave as placeholders until full PHY training sequence is wired.
 * ------------------------------------------------------------------------- */
#define DMC0_BASE           0xDEAD0003UL   /* PLACEHOLDER */
#define DMC_PHY0_BASE       0xDEAD0004UL   /* PLACEHOLDER */

#else
#error "Define PLATFORM_QEMU or PLATFORM_STARLTE"
#endif

/* A placeholder is any 0xDEADxxxx constant: fail fast if one is used live. */
#define IS_PLACEHOLDER(x) (((x) & 0xFFFF0000UL) == 0xDEAD0000UL)

/* Boot-loop guard threshold (shared across platforms) */
#ifndef SCION_MAX_BOOT_ATTEMPTS
#define SCION_MAX_BOOT_ATTEMPTS    3u
#endif

#endif /* EXYNOS9810_H */
