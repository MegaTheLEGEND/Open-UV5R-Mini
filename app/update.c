/* Open-UV5R-Mini: cable update mode. See app/update.h and docs/UPDATE-MODE.md. */
#include <stddef.h>
#include "app/update.h"

#define FRAME_MAX      (7u + 8u + 0x200u)   /* our reads are at most 0x200 bytes */
#define READ_CHUNK     0x200u
#define READ_TIMEOUT   1000u                /* ms per try */
#define READ_TRIES     4u
#define PING_WAIT      0xFFFFFFFFu          /* update mode waits for the host until switched off */

static const UPDATE_Port *g_port;
static uint32_t g_baud = UPDATE_BAUD_DEFAULT;
static uint8_t  g_rx[FRAME_MAX];
static uint8_t  g_frame[FRAME_MAX];      /* data of the last good frame */
static uint32_t g_rx_len;
static uint8_t  g_loader[UPDATE_MAX_LOADER];

/* ---------------------------------------------------------------------------------------------- helpers */
uint16_t UPDATE_Crc16(const uint8_t *p, uint32_t n, uint16_t crc)
{
	while (n--) {
		crc ^= (uint16_t)(*p++ << 8);
		for (int i = 0; i < 8; i++)
			crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
	}
	return crc;
}

/* JieLi ENC: XOR with the low byte of a CRC16-CCITT LFSR, stepped per byte. */
void UPDATE_Decrypt(uint8_t *p, uint32_t n, uint16_t key)
{
	while (n--) {
		*p++ ^= (uint8_t)key;
		key = (key & 0x8000u) ? (uint16_t)((key << 1) ^ 0x1021u) : (uint16_t)(key << 1);
	}
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int name_is(const uint8_t *p, const char *s)
{
	while (*s)
		if (*p++ != (uint8_t)*s++)
			return 0;
	return *p == 0;
}

/* ------------------------------------------------------------------------------------------------ frames */
static void send(uint8_t cmd, const uint8_t *data, uint32_t len)
{
	uint8_t f[7 + 16];
	uint16_t crc;

	if (len > 16)
		return;
	f[0] = 0xAA;
	f[1] = 0x55;
	wr16(&f[2], (uint16_t)(1 + len));
	f[4] = cmd;
	for (uint32_t i = 0; i < len; i++)
		f[5 + i] = data[i];
	crc = UPDATE_Crc16(f, 5 + len, 0);
	wr16(&f[5 + len], crc);
	g_port->write(f, (int)(7 + len));
}

/* Next good frame within timeout_ms: returns its command and leaves its data at g_rx + 5, length *len.
 * Bytes before AA 55 and frames with a bad CRC are dropped. Returns -1 on time-out. */
static int recv(uint32_t timeout_ms, uint32_t *len)
{
	uint8_t b[64];

	for (;;) {
		/* parse what is buffered */
		while (g_rx_len >= 2 && !(g_rx[0] == 0xAA && g_rx[1] == 0x55)) {
			for (uint32_t i = 1; i < g_rx_len; i++)
				g_rx[i - 1] = g_rx[i];
			g_rx_len--;
		}
		if (g_rx_len >= 4) {
			uint32_t flen = rd16(&g_rx[2]);
			uint32_t total = flen + 6;
			if (flen == 0 || total > sizeof(g_rx)) {
				g_rx[0] = 0;                       /* not a frame: resync after this sync */
				continue;
			}
			if (g_rx_len >= total) {
				int cmd = g_rx[4];
				int good = rd16(&g_rx[total - 2]) == UPDATE_Crc16(g_rx, total - 2, 0);
				*len = flen - 1;
				if (good) {
					for (uint32_t i = 0; i < flen - 1; i++)
						g_frame[i] = g_rx[5 + i];
					for (uint32_t i = total; i < g_rx_len; i++)
						g_rx[i - total] = g_rx[i];
					g_rx_len -= total;
					return cmd;
				}
				g_rx[0] = 0;                           /* bad CRC: maybe a false sync; look again after it */
				continue;
			}
		}
		{
			int n = g_port->read(b, (int)sizeof(b), timeout_ms);
			if (n <= 0) {
				if (g_rx_len)
					g_rx[0] = 0;                       /* a part frame went quiet: drop its sync */
				return -1;
			}
			for (int i = 0; i < n && g_rx_len < sizeof(g_rx); i++)
				g_rx[g_rx_len++] = b[i];
		}
	}
}

/* The data of the frame recv() returned last. */
static const uint8_t *frame_data(uint32_t len)
{
	(void)len;
	return g_frame;
}

/* Read len bytes of the host's .ufw at off. 0 = done. */
static int host_read(uint32_t off, uint8_t *dst, uint32_t len)
{
	uint8_t req[8];

	wr32(&req[0], off);
	wr32(&req[4], len);
	for (uint32_t t = 0; t < READ_TRIES; t++) {
		uint32_t n;
		int cmd;

		send(UPDATE_CMD_READ, req, 8);
		while ((cmd = recv(READ_TIMEOUT, &n)) >= 0) {
			const uint8_t *d = frame_data(n);
			if (cmd == UPDATE_CMD_READ && n == 8 + len && rd32(d) == off && rd32(d + 4) == len) {
				for (uint32_t i = 0; i < len; i++)
					dst[i] = d[8 + i];
				return 0;
			}
		}
	}
	return -1;
}

/* ------------------------------------------------------------------------------------------ the update */
void UPDATE_BuildRecord(uint8_t rec[UPDATE_RECORD_SIZE], uint32_t loader_addr, uint32_t baud)
{
	/* UPDATA_PARM as the stock app writes it (sub_01E42EE4 with the UART filler sub_01E43590) */
	for (uint32_t i = 0; i < UPDATE_RECORD_SIZE; i++)
		rec[i] = 0;
	wr16(&rec[2], 0x5A04);            /* parm_type: UART_UPDATA */
	wr16(&rec[4], 0x5A01);            /* parm_result: UPDATA_READY */
	wr16(&rec[6], 0x5441);            /* magic */
	wr32(&rec[40], UPDATE_PIN_RX);    /* parm_priv: sub_01E43590 writes h[0x198C0+520] = 61 here, */
	wr32(&rec[44], UPDATE_PIN_TX);    /* and h[0x198C0+518] = 62 here */
	wr32(&rec[48], baud);
	wr32(&rec[52], 0);
	wr32(&rec[72], loader_addr);      /* ota_addr: where the loader was stored */
	wr16(&rec[0], UPDATE_Crc16(&rec[2], 78, 0));
}

static int program(uint32_t addr, const uint8_t *p, uint32_t len)
{
	while (len) {
		uint32_t n = 0x100u - (addr & 0xFFu);
		if (n > len)
			n = len;
		if (g_port->flash_program(addr, p, n))
			return -1;
		addr += n;
		p += n;
		len -= n;
	}
	return 0;
}

static int verify(uint32_t addr, const uint8_t *p, uint32_t len)
{
	uint8_t b[64];
	while (len) {
		uint32_t n = len < sizeof(b) ? len : sizeof(b);
		if (g_port->flash_read(addr, b, n))
			return -1;
		for (uint32_t i = 0; i < n; i++)
			if (b[i] != p[i])
				return -1;
		addr += n;
		p += n;
		len -= n;
	}
	return 0;
}

/* Store the loader as the stock app does on an nrf-v0.27 radio. Returns its header address (ota_addr), 0 on error.
 * Layout (matches what the stock allocator writes, checked by tests/update_host.py):
 *   hdr  = REC_FLASH - (len + 32) - 4, 4-aligned     0xFA0F4 for the 15,848-byte L2 loader
 *   hdr  : 00 00 FF FF | addr u32 | len+32 u32 | 00 FF FF FF | "LOADER.BIN" NUL-padded to 16
 *   hdr+32: the loader
 *   REC_FLASH (0xFDF00): the first 80 bytes of the hand-over record: FF FF 04 5A, FF..., ota_addr at +72
 */
uint32_t UPDATE_StoreLoader(const uint8_t *loader, uint32_t len)
{
	uint8_t h[32], r[80];
	uint32_t hdr = (UPDATE_REC_FLASH - (len + 32u) - 4u) & ~3u;

	if (len == 0 || len > UPDATE_MAX_LOADER || hdr < UPDATE_STORE_LO || UPDATE_STORE_LO < UPDATE_SETTINGS_END)
		return 0;
	for (uint32_t a = UPDATE_STORE_LO; a < UPDATE_VM_END; a += 0x1000u)
		if (g_port->flash_erase_4k(a))
			return 0;

	for (uint32_t i = 0; i < sizeof(h); i++)
		h[i] = 0;
	wr16(&h[2], 0xFFFF);
	wr32(&h[4], hdr);
	wr32(&h[8], len + 32u);
	h[13] = h[14] = h[15] = 0xFF;
	{
		static const char name[] = "LOADER.BIN";
		for (uint32_t i = 0; name[i]; i++)
			h[16 + i] = (uint8_t)name[i];
	}
	if (program(hdr, h, sizeof(h)) || program(hdr + 32u, loader, len)
	    || verify(hdr, h, sizeof(h)) || verify(hdr + 32u, loader, len))
		return 0;

	for (uint32_t i = 0; i < sizeof(r); i++)
		r[i] = 0xFF;
	wr16(&r[2], 0x5A04);
	wr32(&r[72], hdr);
	if (program(UPDATE_REC_FLASH, r, sizeof(r)) || verify(UPDATE_REC_FLASH, r, sizeof(r)))
		return 0;
	return hdr;
}

static int fail(int code)
{
	send(UPDATE_CMD_RESULT, (const uint8_t[]){ (uint8_t)code }, 1);
	return code;
}

static int download(void)
{
	uint8_t hdr[0x40], rec[0x50], ent[0x20];
	uint32_t nrec, ota_off = 0, ota_len = 0, lo, ll, addr;
	uint8_t record[UPDATE_RECORD_SIZE];

	/* container header: one ENC block, CRC of bytes 2..63, chip name */
	if (host_read(0, hdr, sizeof(hdr)))
		return UPDATE_ERR_HOST;
	UPDATE_Decrypt(hdr, sizeof(hdr), 0xFFFF);
	if (rd16(hdr) != UPDATE_Crc16(hdr + 2, 0x3E, 0) || !(hdr[0x10] == 'A' && hdr[0x11] == 'C' && hdr[0x12] == '6'
	    && hdr[0x13] == '9' && hdr[0x14] == '5' && hdr[0x15] == 'N'))
		return fail(UPDATE_ERR_FILE);
	nrec = rd16(&hdr[8]);
	if (nrec == 0 || nrec > 8)
		return fail(UPDATE_ERR_FILE);

	/* the records (each its own ENC block): find ota.bin */
	for (uint32_t i = 0; i < nrec; i++) {
		if (host_read(0x40 + 0x50 * i, rec, sizeof(rec)))
			return UPDATE_ERR_HOST;
		UPDATE_Decrypt(rec, sizeof(rec), 0xFFFF);
		if (name_is(&rec[0x40], "ota.bin")) {
			ota_off = rd32(&rec[8]);
			ota_len = rd32(&rec[12]);
		}
	}
	if (!ota_len)
		return fail(UPDATE_ERR_FILE);

	/* ota.bin directory entry: the loader */
	if (host_read(ota_off, ent, sizeof(ent)))
		return UPDATE_ERR_HOST;
	lo = rd32(&ent[4]);
	ll = rd32(&ent[8]);
	if (!name_is(&ent[16], "uart_user.bin") || rd16(ent) != UPDATE_Crc16(ent + 2, 30, 0)
	    || ll == 0 || ll > UPDATE_MAX_LOADER || lo + ll > ota_len)
		return fail(UPDATE_ERR_FILE);
	for (uint32_t i = 0; i < ll; i += READ_CHUNK) {
		uint32_t n = ll - i < READ_CHUNK ? ll - i : READ_CHUNK;
		if (host_read(ota_off + lo + i, &g_loader[i], n))
			return UPDATE_ERR_HOST;
	}
	if (rd16(&ent[2]) != UPDATE_Crc16(g_loader, ll, 0))
		return fail(UPDATE_ERR_FILE);

	/* store it, then hand over to the bootloader: the record goes to flash (survives a power cut) and RAM */
	addr = UPDATE_StoreLoader(g_loader, ll);
	if (!addr)
		return fail(UPDATE_ERR_STORE);
	UPDATE_BuildRecord(record, addr, g_baud);
	if (program(UPDATE_REC_FLASH, record, 80) || verify(UPDATE_REC_FLASH, record, 80))
		return fail(UPDATE_ERR_STORE);
	g_port->handover(record, sizeof(record));
	return UPDATE_OK;
}

int UPDATE_Run(const UPDATE_Port *port)
{
	g_port = port;
	g_rx_len = 0;
	g_baud = UPDATE_BAUD_DEFAULT;
	port->set_baud(g_baud);

	for (;;) {
		uint32_t n;
		int cmd = recv(PING_WAIT, &n);

		if (cmd < 0)
			continue;
		if (cmd == UPDATE_CMD_PING) {
			send(UPDATE_CMD_RATE, NULL, 0);          /* "update mode here" */
		} else if (cmd == UPDATE_CMD_RATE && n >= 4) {
			uint32_t baud = rd32(frame_data(n));
			if (baud != g_baud && baud >= 1200 && baud <= 1000000) {
				g_baud = baud;                         /* new rate: switch and echo it */
				port->set_baud(baud);
				send(UPDATE_CMD_RATE, frame_data(n), 4);
			} else if (baud == g_baud) {
				int r = download();
				if (r != UPDATE_OK)
					return r;
				return UPDATE_OK;
			}
		}
	}
}
