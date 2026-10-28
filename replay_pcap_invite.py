#!/usr/bin/env python3
"""Replay the exact INVITE datagram from 5060_dump_2.pcap (gateway dialect:
sip: URIs, multipart/mixed SDP+ISUP) against the receiver."""
import socket, struct, time

PCAP = "/Users/ekremkaracan/Downloads/5060_dump_2.pcap"
data = open(PCAP, "rb").read()

# find the INVITE and take bytes up to the end of the SIP message
i = data.find(b"INVITE sip:123456")
assert i > 0
inv = data[i:i + 1292]          # verified earlier: full datagram length
assert inv.endswith(b"--Yl53mNEqQA7y--")

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 0))
print("replaying %d-byte pcap INVITE from port %d" % (len(inv), s.getsockname()[1]))
s.sendto(inv, ("172.26.13.15", 5060))
s.settimeout(3.0)
try:
    reply, addr = s.recvfrom(4096)
    print(">>> REPLY from %s:%d:" % addr)
    print(reply.decode("latin1"))
except socket.timeout:
    print(">>> no reply within 3 s")
