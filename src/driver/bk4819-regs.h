/* Open-UV5R-Mini: BK4819 register numbers used by this radio's stock firmware. */
#ifndef DRIVER_BK4819_REGS_H
#define DRIVER_BK4819_REGS_H

enum BK4819_REGISTER_t {
	BK4819_REG_00 = 0x00, /* soft reset: 0x8000 then 0x0000 */
	BK4819_REG_02 = 0x02, /* interrupt flags clear */
	BK4819_REG_07 = 0x07, /* tone/CTCSS frequency control */
	BK4819_REG_08 = 0x08,
	BK4819_REG_09 = 0x09, /* AGC gain table (16 entries) */
	BK4819_REG_0B = 0x0B, /* DTMF / 5-tone code, bits 8-11 */
	BK4819_REG_0C = 0x0C, /* status (interrupt pending, squelch) */
	BK4819_REG_10 = 0x10, /* AGC tables 10..14 */
	BK4819_REG_11 = 0x11,
	BK4819_REG_12 = 0x12,
	BK4819_REG_13 = 0x13,
	BK4819_REG_14 = 0x14,
	BK4819_REG_19 = 0x19,
	BK4819_REG_1F = 0x1F,
	BK4819_REG_21 = 0x21,
	BK4819_REG_24 = 0x24, /* DTMF / tone detection */
	BK4819_REG_26 = 0x26,
	BK4819_REG_28 = 0x28,
	BK4819_REG_2A = 0x2A,
	BK4819_REG_31 = 0x31, /* VOX / scramble enables */
	BK4819_REG_30 = 0x30, /* RX/TX/VCO/PA enables */
	BK4819_REG_32 = 0x32,
	BK4819_REG_33 = 0x33,
	BK4819_REG_36 = 0x36, /* PA bias / power */
	BK4819_REG_37 = 0x37,
	BK4819_REG_38 = 0x38, /* frequency, low 16 bits (10 Hz units) */
	BK4819_REG_39 = 0x39, /* frequency, high 16 bits */
	BK4819_REG_3E = 0x3E,
	BK4819_REG_3F = 0x3F, /* interrupt enables */
	BK4819_REG_40 = 0x40, /* TX deviation */
	BK4819_REG_43 = 0x43, /* RX/TX filter bandwidth */
	BK4819_REG_47 = 0x47, /* AF output select */
	BK4819_REG_48 = 0x48, /* AF RX gain */
	BK4819_REG_49 = 0x49,
	BK4819_REG_4D = 0x4D, /* squelch: close glitch threshold */
	BK4819_REG_4E = 0x4E, /* squelch: delays, open glitch threshold */
	BK4819_REG_4F = 0x4F, /* squelch: close/open noise thresholds */
	BK4819_REG_50 = 0x50, /* TX mute */
	BK4819_REG_51 = 0x51, /* CTCSS/CDCSS TX control */
	BK4819_REG_52 = 0x52,
	BK4819_REG_54 = 0x54,
	BK4819_REG_55 = 0x55,
	BK4819_REG_58 = 0x58, /* FSK modes, bandwidth, on */
	BK4819_REG_59 = 0x59, /* FSK FIFOs, RX/TX on, sync length */
	BK4819_REG_5A = 0x5A, /* FSK sync word, high */
	BK4819_REG_5B = 0x5B, /* FSK sync word, low */
	BK4819_REG_5C = 0x5C,
	BK4819_REG_5D = 0x5D, /* FSK packet length */
	BK4819_REG_5E = 0x5E, /* FSK FIFO levels */
	BK4819_REG_5F = 0x5F, /* FSK FIFO data */
	BK4819_REG_65 = 0x65, /* noise indicator */
	BK4819_REG_67 = 0x67, /* RSSI */
	BK4819_REG_68 = 0x68, /* CTCSS scan result */
	BK4819_REG_69 = 0x69, /* CDCSS scan result, high */
	BK4819_REG_6A = 0x6A, /* CDCSS scan result, low */
	BK4819_REG_70 = 0x70, /* tone 1/2 enables */
	BK4819_REG_71 = 0x71, /* tone 1 frequency */
	BK4819_REG_72 = 0x72, /* tone 2 frequency */
	BK4819_REG_75 = 0x75,
	BK4819_REG_77 = 0x77,
	BK4819_REG_78 = 0x78, /* squelch: open/close RSSI thresholds */
	BK4819_REG_7B = 0x7B,
	BK4819_REG_7D = 0x7D, /* mic sensitivity */
	BK4819_REG_7E = 0x7E, /* AGC / DC filter */
};

#endif
