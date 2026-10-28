#!/usr/bin/env python3
"""Keep probing 172.26.97.12:5060 until ANY response arrives.
- pcap OPTIONS every 5 s
- pcap multipart INVITE every 30 s
- exits on first received datagram (prints it), or after MAX_MIN minutes."""
import socket, time, datetime, sys

PCAP = "/Users/ekremkaracan/Downloads/5060_dump_2.pcap"
TARGET = ("172.26.97.12", 5060)
MAX_MIN = 30

data = open(PCAP, "rb").read()
oi = data.find(b"OPTIONS sip:")
options = data[oi:data.find(b"\r\n\r\n", oi) + 4]
ii = data.find(b"INVITE sip:123456")
invite = data[ii:ii + 1292]
assert invite.endswith(b"--Yl53mNEqQA7y--")

def ts():
    return datetime.datetime.now().strftime("%H:%M:%S")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 0))
print("[%s] probing %s:%d every 5s (OPTIONS) / 30s (INVITE), max %d min"
      % (ts(), TARGET[0], TARGET[1], MAX_MIN), flush=True)

deadline = time.time() + MAX_MIN * 60
n = 0
while time.time() < deadline:
    n += 1
    sock.sendto(options, TARGET)
    line = "[%s] round %d: OPTIONS sent" % (ts(), n)
    if n % 6 == 0:
        sock.sendto(invite, TARGET)
        line += " + multipart INVITE sent"
    print(line, flush=True)
    sock.settimeout(4.5)
    try:
        d, a = sock.recvfrom(4096)
        print("\n[%s] >>>>>> RESPONSE from %s:%d <<<<<<" % (ts(), a[0], a[1]), flush=True)
        print(d.decode("latin1"), flush=True)
        sys.exit(0)
    except socket.timeout:
        pass
print("[%s] giving up after %d minutes without any response" % (ts(), MAX_MIN), flush=True)
sys.exit(1)
