#ifndef SCION_LIB_H
#define SCION_LIB_H

#include <stddef.h>
#include <stdint.h>

struct scion_boot_info;
struct scion_stage2_hdr;

/* Console UART */
void uart_init(uint64_t base);
void uart_putc(char c);
int  uart_tstc(void);
int  uart_getc(void);
void uart_flush(void);

/* Formatted output & strings */
void   kputs(const char *s);
void   kprintf(const char *fmt, ...);
void  *memset(void *d, int c, size_t n);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    parse_u64(const char *s, uint64_t *out);

/* Checksum & Cryptographic primitives (src/lib/crypto.c) */
uint32_t crc32(const void *data, size_t len);
void     sha256(const void *data, size_t len, uint8_t out[32]);
int      crypto_selftest(void);

/* Timer & delays (src/platform/timer.c) */
void     platform_timer_init(void);
uint64_t timer_get_freq(void);
uint64_t timer_get_ticks(void);
uint64_t timer_ticks_to_us(uint64_t ticks);
void     udelay(uint64_t us);
void     mdelay(uint64_t ms);

/* Watchdog, power & brick-proof recovery (src/platform/wdt.c, src/platform/safety.c) */
#define SCION_BTN_VOLUP   (1u << 0)
#define SCION_BTN_VOLDOWN (1u << 1)
#define SCION_BTN_BIXBY   (1u << 2)

void     platform_wdt_disable(void);
void     platform_wdt_kick(void);
void     platform_wdt_arm_failsafe(uint32_t timeout_sec);
void     platform_reboot(void);
void     platform_poweroff(void);
int      platform_addr_is_write_protected(uint64_t addr, uint64_t len);
uint32_t platform_buttons_read(void);
uint32_t platform_bootcount_get(void);
int      platform_bootguard_check(struct scion_boot_info *bi);
void     platform_bootguard_clear(void);
void     platform_enter_usbdl(const char *reason);
void     platform_print_safety_status(void);

/* Clocks & PMU (src/platform/clock.c) */
void     platform_clock_init(void);
uint32_t platform_read_boot_flags(void);

/* DRAM & Memory management (src/platform/dram.c) */
int platform_dram_init(struct scion_boot_info *bi);
int dram_walk_test(uint64_t base, uint64_t size);
int boot_mem_add(struct scion_boot_info *bi, uint64_t base, uint64_t size, uint32_t flags);

/* PSCI & Secondary cores (src/psci/psci.c) */
void     psci_init(struct scion_boot_info *bi);
int      psci_cpu_on(uint32_t cpu_id, uint64_t entry_pa);
uint32_t psci_secondary_count(void);

/* Exceptions & EL3 -> EL2 transition (src/exceptions.c, src/start.S) */
void     exceptions_init(void);
uint32_t current_el(void);
void     el_drop_and_jump(uint64_t entry_pa, uint64_t arg0);

/* Stage-2 image loader (src/loader.c) */
int stage2_build_From_fn(struct scion_stage2_hdr *hdr, void *dst_payload,
                         const void *fn_src, size_t fn_size, const char *name);
int stage2_load_and_verify(struct scion_boot_info *bi,
                           const struct scion_stage2_hdr *hdr,
                           uint64_t *entry_out);

/* Interactive early debug shell (src/shell.c) */
void shell_run(struct scion_boot_info *bi);
int  shell_check_abort(unsigned timeout_ms);

#endif /* SCION_LIB_H */
