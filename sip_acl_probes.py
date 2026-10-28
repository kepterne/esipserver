#!/usr/bin/env python3
"""Differential probes to find out why/where INVITEs are dropped.
A: REGISTER            -> normally 401/403/200
B: unknown method      -> normally 405/501
C: malformed request   -> normally 400
D: bodyless INVITE     -> normally 400/488 if INVITE path alive
E: INVITE from sport 5060 -> rules out port-based peer matching
All UDP. Stop conditions per probe: first reply or 2 timeouts.
"""
import socket, secrets, time

SERVER = ("172.26.13.15", 5060)

def tok(n=10):
    return secrets.token_hex(n // 2)

def local_ip():
    p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    p.connect(SERVER)
    ip = p.getsockname()[0]
    p.close()
    return ip

IP = local_ip()

def mksock(port=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", port or 0))
    return s

def send_and_wait(sock, msg, label, attempts=2, waits=(0.5, 1.5)):
    port = sock.getsockname()[1]
    for i in range(attempts):
        sock.sendto(msg, SERVER)
        sock.settimeout(waits[min(i, len(waits) - 1)])
        try:
            data, addr = sock.recvfrom(4096)
            first = data.split(b"\r\n", 1)[0].decode("latin1")
            print("[%s] (sport %d) REPLY: %s" % (label, port, first))
            for line in data.split(b"\r\n")[1:]:
                if line and not line.startswith(b"Via") and not line.startswith(b"Call-ID"):
                    print("    " + line.decode("latin1"))
            return data
        except socket.timeout:
            print("[%s] (sport %d) no reply (attempt %d)" % (label, port, i + 1))
    return None

def base_headers(method, ruri, sport, extra=""):
    return (
        "%s %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:555395269@%s>;tag=%s\r\n"
        "To: <sip:123456@%s>\r\n"
        "Call-ID: %s@%s\r\n"
        "CSeq: 1 %s\r\n"
        "%s"
        "Content-Length: %d\r\n\r\n"
    ) % (method, ruri, IP, sport, tok(10), IP, tok(8), SERVER[0],
         tok(12), IP, method, extra, 0)

eph = mksock()
P = eph.getsockname()[1]

# A. REGISTER
reg = (
    "REGISTER sip:%s:%d SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%s\r\n"
    "Max-Forwards: 70\r\n"
    "From: <sip:555395269@%s:%d>;tag=%s\r\n"
    "To: <sip:555395269@%s:%d>\r\n"
    "Call-ID: %s@%s\r\n"
    "CSeq: 1 REGISTER\r\n"
    "Contact: <sip:555395269@%s:%d>\r\n"
    "Expires: 300\r\n"
    "Content-Length: 0\r\n\r\n"
) % (SERVER[0], SERVER[1], IP, P, tok(10), IP, P, tok(8), SERVER[0], SERVER[1],
     tok(12), IP, IP, P)
send_and_wait(eph, reg.encode(), "A REGISTER")

# B. Unknown method
foo = base_headers("FOOBAR", "sip:%s:%d" % SERVER, P)
send_and_wait(eph, foo.encode(), "B unknown method")

# C. Malformed
send_and_wait(eph, b"THIS IS NOT SIP\r\n\r\n", "C malformed")

# D. Bodyless INVITE
inv0 = base_headers("INVITE", "sip:123456@%s:%d" % SERVER, P,
                    "Contact: <sip:%s:%d>\r\n" % (IP, P))
send_and_wait(eph, inv0.encode(), "D INVITE no body")

# E. INVITE with SDP from source port 5060
try:
    s5060 = mksock(5060)
    ok = True
except OSError as e:
    print("cannot bind 5060 locally: %s" % e)
    s5060 = mksock(5061)
    ok = False
    print("falling back to sport 5061")
sp = s5060.getsockname()[1]
sdp = ("v=0\r\no=- 1 1 IN IP4 %s\r\ns=-\r\nc=IN IP4 %s\r\nt=0 0\r\n"
       "m=audio 4000%d RTP/AVP 0\r\n" % (IP, IP, sp % 10)).encode()
inve = (
    "INVITE sip:123456@%s:%d;user=phone SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%s\r\n"
    "Max-Forwards: 70\r\n"
    "From: <sip:555395269@%s>;tag=%s\r\n"
    "To: <sip:123456@%s>\r\n"
    "Call-ID: %s@%s\r\n"
    "CSeq: 1 INVITE\r\n"
    "Contact: <sip:%s:%d>\r\n"
    "Content-Type: application/sdp\r\n"
    "Content-Length: %d\r\n\r\n"
) % (SERVER[0], SERVER[1], IP, sp, tok(10), IP, tok(8), SERVER[0],
     tok(12), IP, IP, sp, len(sdp))
send_and_wait(s5060, inve.encode() + sdp, "E INVITE sport %d" % sp)

print("\nlocal IP: %s" % IP)
