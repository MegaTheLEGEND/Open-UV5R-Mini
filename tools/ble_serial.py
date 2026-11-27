#!/usr/bin/env python3
r"""Bridge the radio's Bluetooth serial link (BLE, Nordic UART Service) to a TCP port, for CHIRP or a terminal.

On the radio: MENU -> BLUETOOTH -> BT CHIRP (or BT SERIAL). It advertises as "UV5R-MINI".
On the PC:

    pip install bleak                       # once
    python tools/ble_serial.py              # finds UV5R-MINI, listens on localhost:5005
    python tools/ble_serial.py --scan       # list BLE devices with the serial service

Then in CHIRP: Radio -> Download from radio, port "Custom...", type  socket://localhost:5005 , vendor Baofeng,
model UV-5R Mini. Upload the same way. A terminal works too (e.g. `ncat localhost 5005`).

The link: the PC writes to RX (6E400002-...) and gets TX (6E400003-...) notifications, 20 bytes each (ATT MTU 23,
src/bt/gatt.c). One TCP client at a time; when it disconnects the BLE link stays up for the next one.
"""
import argparse
import asyncio
import sys

NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'   # we write
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'   # we get notified
CHUNK = 20


def need_bleak():
    try:
        import bleak  # noqa: F401
    except ImportError:
        sys.exit('needs the bleak package: pip install bleak')


async def scan(timeout):
    from bleak import BleakScanner
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for dev, adv in found.values():
        if NUS in [u.lower() for u in adv.service_uuids] or (dev.name or '').startswith('UV5R'):
            print(f'{dev.address}  {adv.rssi:4d} dBm  {dev.name or "?"}')


async def find(name, address, timeout):
    from bleak import BleakScanner
    if address:
        return address
    print(f'looking for {name} ...')
    dev = await BleakScanner.find_device_by_filter(
        lambda d, a: (d.name == name) or (NUS in [u.lower() for u in a.service_uuids]), timeout=timeout)
    if not dev:
        sys.exit(f'{name} not found: is BT CHIRP or BT SERIAL open on the radio?')
    return dev


async def run(args):
    from bleak import BleakClient
    target = await find(args.name, args.address, args.timeout)
    to_tcp = asyncio.Queue()
    writer_box = {}

    def on_notify(_, data: bytearray):
        to_tcp.put_nowait(bytes(data))

    async with BleakClient(target) as cl:
        print(f'connected to {cl.address}')
        await cl.start_notify(TX, on_notify)

        async def pump_out():
            while True:
                data = await to_tcp.get()
                w = writer_box.get('w')
                if w:
                    w.write(data)
                    await w.drain()
                if args.verbose:
                    print('radio>', data.hex())

        async def handle(reader, writer):
            if writer_box.get('w'):
                writer.close()
                return
            print('client connected')
            writer_box['w'] = writer
            try:
                while True:
                    data = await reader.read(256)
                    if not data:
                        break
                    if args.verbose:
                        print('pc>   ', data.hex())
                    for i in range(0, len(data), CHUNK):
                        await cl.write_gatt_char(RX, data[i:i + CHUNK], response=False)
            finally:
                writer_box.pop('w', None)
                writer.close()
                print('client gone')

        server = await asyncio.start_server(handle, args.host, args.port)
        print(f'listening on {args.host}:{args.port}  (CHIRP port: socket://localhost:{args.port})')
        pump = asyncio.create_task(pump_out())
        try:
            while cl.is_connected:
                await asyncio.sleep(0.5)
            print('radio disconnected')
        finally:
            pump.cancel()
            server.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--name', default='UV5R-MINI', help='the radio\'s BLE name (default UV5R-MINI)')
    ap.add_argument('--address', help='connect to this BLE address instead of searching')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=5005)
    ap.add_argument('--timeout', type=float, default=10.0, help='seconds to search')
    ap.add_argument('--scan', action='store_true', help='list devices with the serial service, then stop')
    ap.add_argument('-v', '--verbose', action='store_true', help='print the bytes both ways')
    args = ap.parse_args()
    need_bleak()
    try:
        asyncio.run(scan(args.timeout) if args.scan else run(args))
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
