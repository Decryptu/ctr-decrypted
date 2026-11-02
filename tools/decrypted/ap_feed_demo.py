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
import sys
import time

from ap_backlog import connect, recv_packets, wait, ws_send


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    port, slot = int(sys.argv[1]), sys.argv[2]
    delay, count, gap = float(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
    # A game client, not TextOnly: the server ignores location checks from those.
    try:
        s, got = connect(port, slot, game='Crash Team Racing', tags=(), uuid='ap-feed-demo')
    except ConnectionError as e:
        print(e)
        return 1
    con = next(p for p in got if p.get('cmd') == 'Connected')
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
