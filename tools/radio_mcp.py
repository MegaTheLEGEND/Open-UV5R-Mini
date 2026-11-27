#!/usr/bin/env python3
"""An MCP server (stdio, no extra packages) that gives an AI agent the radio's remote-control monitor as tools:
the screen's text, its state, key presses, memory and BK4819 registers, the open app's own commands, and the log.

It talks to tools/radiomon.py (which owns the serial port), so start that first:

    python tools/radiomon.py serve --port COM7

Registered for this repo in .mcp.json ("uv5r-radio"); a Claude Code session started afterwards has the tools.
Only for ENABLE_DEBUGGING builds (src/app/monitor.h). It cannot flash anything, and never should.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import radiomon  # noqa: E402

KEYS = '0-9, *, #, MENU, UP, DOWN, EXIT, VM, AB, LAMP, EQ'

TOOLS = [
    ('radio_state', 'Screen, open app, frequency, RX/TX, RSSI, carrier, channel, uptime.', {}, lambda a: 'state'),
    ('radio_screen', 'The text on the radio LCD now (rows labelled by pixel row in hex).', {}, lambda a: 'screen'),
    ('radio_key', 'Press a key as if a finger held it for ms (default 120; 800 = the held action). Keys: ' + KEYS,
     {'key': {'type': 'string'}, 'ms': {'type': 'integer'}},
     lambda a: 'key %s %d' % (a['key'].upper(), int(a.get('ms') or 120))),
    ('bk4819_read', 'Read a BK4819 register (hex, e.g. "58").', {'reg': {'type': 'string'}}, lambda a: 'bk ' + a['reg']),
    ('bk4819_write', 'Write a BK4819 register (hex reg and value). Can disturb receive/transmit: know why first.',
     {'reg': {'type': 'string'}, 'value': {'type': 'string'}}, lambda a: 'bk %s %s' % (a['reg'], a['value'])),
    ('bk4819_dump', 'All BK4819 registers 00-7F.', {}, lambda a: 'bkdump'),
    ('mem_read', 'Read 32-bit words of MCU memory (hex address, count up to 64).',
     {'addr': {'type': 'string'}, 'count': {'type': 'integer'}}, lambda a: 'r %s %x' % (a['addr'], int(a.get('count') or 1))),
    ('mem_write', 'Write one 32-bit word of MCU memory (hex). Dangerous: a wrong write crashes the radio.',
     {'addr': {'type': 'string'}, 'value': {'type': 'string'}}, lambda a: 'w %s %s' % (a['addr'], a['value'])),
    ('app_command', 'A command for the open app (APRS: "help", "stat", "raw", "mode 1", "sync FEFE FEFE", "beacon", '
     '"conf", "heard N0CALL>APRS:!4903.50N/07201.75W-hi"). "beacon" transmits.',
     {'args': {'type': 'string'}}, lambda a: 'app ' + a['args']),
    ('radio_raw', 'Any monitor command line (see src/app/monitor.h), without the "!".',
     {'command': {'type': 'string'}}, lambda a: a['command']),
]


def call(name, args):
    if name == 'radio_log':
        return radiomon.tail(int(args.get('lines') or 40))
    for n, _, _, make in TOOLS:
        if n == name:
            cmd = make(args)
            t = 8.0 if 'beacon' in cmd else 4.0
            try:
                return radiomon.send(cmd, t)
            except OSError as e:
                return 'radiomon is not running (python tools/radiomon.py serve --port COM7): %s' % e
    raise KeyError(name)


def tool_list():
    out = []
    for n, d, props, _ in TOOLS:
        out.append({'name': n, 'description': d,
                    'inputSchema': {'type': 'object', 'properties': props,
                                    'required': [k for k in props if k not in ('ms', 'count')]}})
    out.append({'name': 'radio_log', 'description': 'The end of the cable log: replies and events (#aprs, #fsk).',
                'inputSchema': {'type': 'object', 'properties': {'lines': {'type': 'integer'}}}})
    return out


def reply(id_, result=None, error=None):
    m = {'jsonrpc': '2.0', 'id': id_}
    if error:
        m['error'] = error
    else:
        m['result'] = result
    sys.stdout.write(json.dumps(m) + '\n')
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        method, id_ = msg.get('method'), msg.get('id')
        if id_ is None:                      # notifications
            continue
        if method == 'initialize':
            reply(id_, {'protocolVersion': msg.get('params', {}).get('protocolVersion', '2024-11-05'),
                        'capabilities': {'tools': {}},
                        'serverInfo': {'name': 'uv5r-radio', 'version': '0.1'}})
        elif method == 'tools/list':
            reply(id_, {'tools': tool_list()})
        elif method == 'tools/call':
            p = msg.get('params', {})
            try:
                text = call(p.get('name'), p.get('arguments') or {})
                reply(id_, {'content': [{'type': 'text', 'text': text}],
                            'isError': '@err' in text.split('\n')[-1]})
            except KeyError:
                reply(id_, error={'code': -32602, 'message': 'no such tool'})
        elif method == 'ping':
            reply(id_, {})
        else:
            reply(id_, error={'code': -32601, 'message': 'not supported'})


if __name__ == '__main__':
    main()
