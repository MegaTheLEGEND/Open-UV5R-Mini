/* Open-UV5R-Mini: the radio's own calibration, external flash 0xF000 (read only).
 *
 * Layout traced from the stock app (here, and in Codeplug Studio's docs/reference/radios/baofeng/uv-5r-mini/
 * calibration.md, which agrees):
 *   PA bias frames of 64 bytes by power index: 0xF000 High (0), 0xF080 Low (1), 0xF040 a third level (2). In each
 *     frame four 16-byte rows: +0x00 UHF (400 + 10 i MHz), +0x10 VHF (130 + 3 i MHz, i <= 15), +0x20 200 MHz
 *     band (200 + 5 i), +0x30 350 MHz band (350 + 5 i; all zero on radios read so far, so no TX there).
 *     RF register 0x36 = bias << 8 | gain, gain 0xFF / 0xD7 / 0xFF by power index (stock table 0x01E5DF02);
 *     bias 0 = PA off (0x007F).
 *   Squelch: open thresholds by level 0-9 at 0xF0C0, close at 0xF0D0, minus a per-frequency offset: UHF 0xF0E0
 *     ((MHz - 400) / 10), VHF 0xF0F0 ((MHz - 130) / 5, at most 9), 200 MHz 0xF100, 350 MHz 0xF110 ((MHz - x) / 5).
 *     Below 130 MHz and at 180-199 MHz the level is ignored: open = level-9 entry, close = open - 3.
 *   0xF000 = 0xFF means not calibrated.
 */
#ifndef APP_CALIB_H
#define APP_CALIB_H

#include <stdbool.h>
#include <stdint.h>

#define CALIB_ADDR 0xF000u
#define CALIB_LEN  0x220u

enum { POWER_HIGH = 0, POWER_LOW = 1 };

void    CALIB_Set(const uint8_t *data);            /* CALIB_LEN bytes read from CALIB_ADDR */
bool    CALIB_Present(void);
uint8_t CALIB_PaBias(uint32_t freq_10hz, uint8_t power);
uint8_t CALIB_PaGain(uint8_t power);
void    CALIB_Squelch(uint32_t freq_10hz, uint8_t level, uint8_t *open, uint8_t *close);

#endif
