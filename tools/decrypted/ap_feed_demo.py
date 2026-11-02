"""Make another slot's finds show up in a player's in-game item feed.

Usage: ap_feed_demo.py PORT SLOT DELAY COUNT GAP

Connects to ws://127.0.0.1:PORT as SLOT (a Crash Team Racing slot of the seed,
not the one the game plays), scouts its locations, waits DELAY seconds, then
checks COUNT of the locations that hold another player's items, progression
first, one every GAP seconds. The game connected as that other player shows
each one as "<ITEM> FROM <SLOT>". tools/decrypted/readme-shots.ps1 uses it.
Standard library only, on top of ap_backlog.py's minimal websocket client.
"""
import json
import os
import socket
import struct
import sys
import time

from ap_backlog import recv_packets, ws_connect, ws_recv, ws_send


def wait(s, secs):
    # Keep answering pings, or the server drops a client that sits silent.
    end = time.time() + secs
    s.settimeout(0.5)
    while time.time() < end:
        try:
            op, payload = ws_recv(s)
        except socket.timeout:
            continue
        if op == 9:
            mask = os.urandom(4)
            s.sendall(struct.pack('!BB', 0x8A, 0x80 | len(payload)) + mask +
                      bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    port, slot = int(sys.argv[1]), sys.argv[2]
    delay, count, gap = float(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
    s = ws_connect('127.0.0.1', port)
    recv_packets(s, lambda p: p.get('cmd') == 'RoomInfo')
    # A game client, not TextOnly: the server ignores location checks from those.
    ws_send(s, json.dumps([{
        'cmd': 'Connect', 'game': 'Crash Team Racing', 'name': slot, 'password': '', 'uuid': 'ap-feed-demo',
        'version': {'major': 0, 'minor': 6, 'build': 7, 'class': 'Version'},
        'items_handling': 0, 'tags': [], 'slot_data': False}]))
    got = recv_packets(s, lambda p: p.get('cmd') in ('Connected', 'ConnectionRefused'))
    con = next((p for p in got if p.get('cmd') == 'Connected'), None)
    if con is None:
        print('connect failed:', got[-1:] if got else 'no reply')
        return 1
    ws_send(s, json.dumps([{'cmd': 'LocationScouts', 'locations': con['missing_locations'], 'create_as_hint': 0}]))
    got = recv_packets(s, lambda p: p.get('cmd') == 'LocationInfo')
    info = next(p for p in got if p.get('cmd') == 'LocationInfo')
    theirs = [n for n in info['locations'] if n['player'] != con['slot']]
    theirs.sort(key=lambda n: -(n['flags'] & 1))
    wait(s, delay)
    for n in theirs[:count]:
        ws_send(s, json.dumps([{'cmd': 'LocationChecks', 'locations': [n['location']]}]))
        print('checked', n['location'], time.strftime('%X'), flush=True)
        wait(s, gap)
    s.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
