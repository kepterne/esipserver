#!/usr/bin/env python3
"""The missing matrix cell: multipart/mixed body (SDP+ISUP from the pcap)
COMBINED with isub= correlation params -> sent to 172.26.13.15 (siptest)."""
import socket, secrets

SERVER = ("172.26.13.15", 5060)
PCAP = "/Users/ekremkaracan/Downloads/5060_dump_2.pcap"

data = open(PCAP, "rb").read()
i = data.find(b"INVITE sip:123456")
inv = data[i:i + 1292]
body = inv.split(b"\r\n\r\n", 1)[1]           # 555-byte multipart body
assert body.startswith(b"--Yl53mNEqQA7y") and body.endswith(b"--Yl53mNEqQA7y--")

def tok(n=10):
    return secrets.token_hex(n // 2)

p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); p.connect(SERVER)
IP = p.getsockname()[0]; p.close()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", 0))
P = s.getsockname()[1]

ISUB_TO = "0f68308318f68308318f"
ISUB_FROM = "2312191151059841300004f1f"
callid = "%s-mp@%s" % (tok(12), IP)

msg = (
    "INVITE sip:123456@%s:%d;user=phone;isub=%s;isub-encoding=user SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%s\r\n"
    "From: <sip:555395269@%s:%d;user=phone;isub=%s;isub-encoding=user>;tag=%s\r\n"
    "To: <sip:123456@%s:%d;user=phone;isub=%s;isub-encoding=user>\r\n"
    "Call-ID: %s\r\n"
    "CSeq: 1 INVITE\r\n"
    "Contact: <sip:%s:%d>\r\n"
    "Max-Forwards: 70\r\n"
    "Supported: 100rel,early-session\r\n"
    "Privacy: none\r\n"
    "P-Asserted-Identity: <sip:555395269@%s:%d;user=phone;isub=%s;isub-encoding=user>\r\n"
    "Content-Type: multipart/mixed;boundary=Yl53mNEqQA7y\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
) % (SERVER[0], SERVER[1], ISUB_TO, IP, P, tok(10),
     IP, P, ISUB_FROM, tok(8),
     SERVER[0], SERVER[1], ISUB_TO,
     callid, IP, P,
     IP, P, ISUB_FROM, len(body))

print("multipart+isub INVITE from %s:%d (%d-byte body)" % (IP, P, len(body)))
s.sendto(msg.encode() + body, SERVER)
s.settimeout(3)
try:
    d, a = s.recvfrom(4096)
    print(">>> REPLY:")
    print(d.decode("latin1"))
except socket.timeout:
    print(">>> no reply within 3 s")
