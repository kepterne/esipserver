#!/usr/bin/env python3
"""Burst pcap-verbatim OPTIONS + multipart INVITE at 172.26.13.9:5060.
INVITE retransmissions follow the pcap pattern: 0, .5, 1, 2, 4, 8, 16s.
Stops the INVITE phase at the first reply."""
import socket, time, datetime

PCAP = "/Users/ekremkaracan/Downloads/5060_dump_2.pcap"
TARGET = ("172.26.13.15", 5070)

data = open(PCAP, "rb").read()

def extract(marker, length):
    i = data.find(marker)
    assert i > 0, marker
    return data[i:i + length]

# OPTIONS datagram: from 'OPTIONS sip:' up to the double CRLF ending
oi = data.find(b"OPTIONS sip:")
oend = data.find(b"\r\n\r\n", oi) + 4
options = data[oi:oend]
# INVITE: known 1292-byte datagram (verified earlier)
ii = data.find(b"INVITE sip:123456")
invite = data[ii:ii + 1292]
assert invite.endswith(b"--Yl53mNEqQA7y--")

def ts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 0))
print("burst to %s:%d from port %d at %s" % (TARGET[0], TARGET[1],
      sock.getsockname()[1], ts()))
print("OPTIONS first line: %s" % options.split(b"\r\n", 1)[0].decode())
print("INVITE  first line: %s" % invite.split(b"\r\n", 1)[0].decode())

print("\n--- 3 OPTIONS (keepalive style, 1s apart) ---")
for n in range(3):
    sock.sendto(options, TARGET)
    sock.settimeout(1.0)
    try:
        d, a = sock.recvfrom(4096)
        print("[%s] OPTIONS #%d -> REPLY: %s" % (ts(), n + 1,
              d.split(b"\r\n", 1)[0].decode("latin1")))
    except socket.timeout:
        print("[%s] OPTIONS #%d -> no reply within 1s" % (ts(), n + 1))
    time.sleep(1)

print("\n--- multipart INVITE with pcap retransmit pattern ---")
waits = [0.5, 1, 2, 4, 8, 16]   # gaps between the 7 sends
got = False
for n in range(7):
    sock.sendto(invite, TARGET)
    print("[%s] INVITE sent (#%d)" % (ts(), n + 1))
    if n < 6:
        sock.settimeout(waits[n])
        try:
            d, a = sock.recvfrom(4096)
            print("[%s] >>> INVITE REPLY from %s:%d:" % (ts(), a[0], a[1]))
            print(d.decode("latin1"))
            got = True
            break
        except socket.timeout:
            pass
if not got:
    print("[%s] no reply to any of the 7 INVITE transmissions" % ts())
