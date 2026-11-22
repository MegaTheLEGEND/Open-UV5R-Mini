/* Open-UV5R-Mini: the UART at 0x1E2100 that the programming cable talks to (update mode).
 *
 * Register map from the stock v0.27 app (init sub_01E492C0, baud sub_01E431A2, send 0x01E43408, ISR 0x01E5949E):
 *   +0  CON0   bit 0 enable, 2 TX irq, 3 RX irq, 5 time-out irq, 6 RX DMA, 7 RDC (write: latch the DMA count into
 *              HRXCNT); bit 15 TX done (write bit 13 to clear), bit 14 RX buffer full (write bit 12 to clear),
 *              bit 11 RX time-out (write bit 10 to clear)
 *   +4  CON1
 *   +8  BAUD   = clock / baud / 4 - 1   (clock 48 MHz)
 *   +16 OTCNT  RX time-out, in clock ticks
 *   +20 TXADR, +24 TXCNT (writing the count starts the DMA send)
 *   +28 RXSADR, +32 RXEADR, +36 RXCNT (DMA receive buffer), +40 HRXCNT (bytes received so far)
 * The cable is on the USB pins: RX = 61 (D+), TX = 62 (D-), routed through the IO map like the stock app does.
 */
#ifndef DRIVER_UART_H
#define DRIVER_UART_H

#include <stdint.h>

void UART_Init(uint32_t baud);
void UART_SetBaud(uint32_t baud);
void UART_Send(const uint8_t *data, uint32_t len);
/* Up to max bytes, waiting at most timeout_ms for the first; returns the count (0 on time-out). */
int  UART_Receive(uint8_t *buf, int max, uint32_t timeout_ms);
/* A UART register (offset from 0x1E2100), for the update screen's debug lines. */
uint32_t UART_Reg(uint32_t offset);
/* Bytes dropped as echo or overflow (debug). */
uint32_t UART_Dropped(void);

#endif
