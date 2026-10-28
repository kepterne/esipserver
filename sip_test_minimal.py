#!/usr/bin/env python3
"""Ultra-minimal UDP INVITE - strip everything optional.
Variation A: bare INVITE, no ;user=phone, no PAI/Privacy/Supported/Allow.
Variation B: bare INVITE with ;user=phone in R-URI (as in pcap).
Stops at first reply.
"""
import socket, time, secrets

SERVER = ("172.26.13.15", 5060)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 5060)) if False else sock.bind(("0.0.0.0", 0))
lport = sock.getsockname()[1]
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); probe.connect(SERVER)
local_ip = probe.getsockname()[0]; probe.close()
print("local socket: %s:%d" % (local_ip, lport))

def tok(n=12): return secrets.token_hex(n // 2)

def attempt(ruri):
    sdp = ("v=0\r\no=- 1 1 IN IP4 %s\r\ns=-\r\nc=IN IP4 %s\r\nt=0 0\r\n"
           "m=audio 40000 RTP/AVP 0\r\n" % (local_ip, local_ip)).encode()
    msg = (
        "INVITE %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:555395269@%s>;tag=%s\r\n"
        "To: <sip:123456@%s>\r\n"
        "Call-ID: %s@%s\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:%s:%d>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n\r\n" % (
            ruri, local_ip, lport, tok(10), local_ip, tok(10),
            SERVER[0], tok(12), local_ip, local_ip, lport, len(sdp))
    ).encode() + sdp
    waits = [0.5, 1.0, 2.0]
    for i, w in enumerate(waits):
        sock.sendto(msg, SERVER)
        sock.settimeout(w)
        try:
            data, addr = sock.recvfrom(4096)
            return data
        except socket.timeout:
            print("  attempt %d/%d: no reply within %.1f s" % (i + 1, len(waits), w))
    return None

for label, ruri in [("A: bare R-URI sip:123456@host",
                     "sip:123456@%s:%d" % SERVER),
                    ("B: R-URI with ;user=phone",
                     "sip:123456@%s:%d;user=phone" % SERVER)]:
    print("\n=== Variation %s ===" % label)
    r = attempt(ruri)
    if r:
        print("--- REPLY ---")
        print(r.decode("latin1"))
        print(">>> RESULT: got an INVITE reply on variation %s; stopping." % label[0])
        break
    else:
        print("  -> no reply")
else:
    print(">>> RESULT: no reply to any INVITE variation.")
