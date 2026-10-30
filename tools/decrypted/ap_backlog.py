"""Give a slot a large item backlog on a local Archipelago server.

Usage: ap_backlog.py PORT SLOT COUNT [ITEM]

Connects to ws://127.0.0.1:PORT as a text-only client of SLOT and sends
"!getitem ITEM" COUNT times (default "Wumpa Fruit"); the server must allow the
item cheat (host.yaml server_options.disable_item_cheat: false, the default).
When the game client then connects, the server sends the whole backlog in one
ReceivedItems packet: the reconnect catch-up of ctr-native-ap #147. Standard
library only (a minimal websocket client).
"""
import base64
import json
import os
import socket
import struct
import sys
import time


def ws_connect(host, port):
    s = socket.create_connection((host, port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f'GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n'
               f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
               'Sec-WebSocket-Version: 13\r\n\r\n').encode())
    head = b''
    while b'\r\n\r\n' not in head:
        head += s.recv(1)
    if b' 101 ' not in head.split(b'\r\n')[0]:
        raise RuntimeError(head.decode(errors='replace'))
    return s


def ws_send(s, text):
    data = text.encode()
    mask = os.urandom(4)
    n = len(data)
    if n < 126:
        hdr = struct.pack('!BB', 0x81, 0x80 | n)
    elif n < 65536:
        hdr = struct.pack('!BBH', 0x81, 0x80 | 126, n)
    else:
        hdr = struct.pack('!BBQ', 0x81, 0x80 | 127, n)
    s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def recv_exact(s, n):
    buf = b''
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('closed')
        buf += chunk
    return buf


def ws_recv(s):
    b0, b1 = recv_exact(s, 2)
    n = b1 & 0x7f
    if n == 126:
        n = struct.unpack('!H', recv_exact(s, 2))[0]
    elif n == 127:
        n = struct.unpack('!Q', recv_exact(s, 8))[0]
    payload = recv_exact(s, n)
    return b0 & 0x0f, payload


def recv_packets(s, until, timeout=10.0):
    end = time.time() + timeout
    s.settimeout(1.0)
    seen = []
    while time.time() < end:
        try:
            op, payload = ws_recv(s)
        except socket.timeout:
            continue
        if op != 1:
            continue
        for p in json.loads(payload):
            seen.append(p)
            if until(p):
                return seen
    return seen


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    port, slot, count = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
    item = sys.argv[4] if len(sys.argv) > 4 else 'Wumpa Fruit'
    s = ws_connect('127.0.0.1', port)
    recv_packets(s, lambda p: p.get('cmd') == 'RoomInfo')
    ws_send(s, json.dumps([{
        'cmd': 'Connect', 'game': '', 'name': slot, 'password': '', 'uuid': 'ap-backlog',
        'version': {'major': 0, 'minor': 6, 'build': 8, 'class': 'Version'},
        'items_handling': 0, 'tags': ['TextOnly'], 'slot_data': False}]))
    got = recv_packets(s, lambda p: p.get('cmd') in ('Connected', 'ConnectionRefused'))
    if not any(p.get('cmd') == 'Connected' for p in got):
        print('connect failed:', got[-1:] if got else 'no reply')
        return 1
    # Read the server's reply to each batch before sending the next, or it
    # stops reading and the send blocks.
    def is_reply(p):
        return p.get('cmd') == 'PrintJSON' and 'Cheat console' in ''.join(x.get('text', '') for x in p.get('data', []))
    replies = 0
    for i in range(0, count, 100):
        n = min(100, count - i)
        ws_send(s, json.dumps([{'cmd': 'Say', 'text': f'!getitem {item}'}] * n))
        target = replies + n
        state = {'n': replies}

        def done(p):
            if is_reply(p):
                state['n'] += 1
            return state['n'] >= target
        recv_packets(s, done, timeout=30.0)
        replies = state['n']
    print(f'sent {count} x {item}, {replies} confirmed')
    s.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
