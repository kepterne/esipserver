#!/usr/bin/env python3
"""Coordinated probe burst: OPTIONS + REGISTER + INVITE, one per second.
Watch on the receiver with: tcpdump -i any -nn udp port 5060
"""
import socket, secrets, time, datetime

SERVER = ("172.26.13.15", 5060)

def tok(n=10):
    return secrets.token_hex(n // 2)

p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); p.connect(SERVER)
IP = p.getsockname()[0]; p.close()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 0))
P = sock.getsockname()[1]
print("burst from %s:%d at %s" % (IP, P, datetime.datetime.now().strftime("%H:%M:%S")))

sdp = ("v=0\r\no=- 1 1 IN IP4 %s\r\ns=-\r\nc=IN IP4 %s\r\nt=0 0\r\n"
       "m=audio 41000 RTP/AVP 0\r\n" % (IP, IP)).encode()

def fire(method, ruri, extra="", body=b""):
    callid = "burst-%s-%s@%s" % (method.lower(), tok(6), IP)
    msg = (
        "%s %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bKburst%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:555395269@%s>;tag=burst%s\r\n"
        "To: <sip:123456@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 %s\r\n"
        "Contact: <sip:%s:%d>\r\n"
        "%s"
        "Content-Length: %d\r\n\r\n"
    ) % (method, ruri, IP, P, tok(8), IP, tok(6), SERVER[0], callid, method,
         IP, P, extra, len(body))
    m = msg.encode() + body
    sock.sendto(m, SERVER)
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print("[%s] SENT %-9s Call-ID=%s (%d bytes)" % (ts, method, callid, len(m)))
    sock.settimeout(1.0)
    try:
        data, addr = sock.recvfrom(4096)
        print("         -> REPLY: %s" % data.split(b"\r\n", 1)[0].decode("latin1"))
    except socket.timeout:
        print("         -> no reply within 1 s")
    return callid

fire("OPTIONS", "sip:%s:%d" % SERVER)
time.sleep(1)
fire("REGISTER", "sip:%s:%d" % SERVER, "Expires: 300\r\n")
time.sleep(1)
fire("INVITE", "sip:123456@%s:%d;user=phone" % SERVER,
     "Content-Type: application/sdp\r\n", sdp)
print("\nburst complete at %s - check tcpdump + app log on receiver"
      % datetime.datetime.now().strftime("%H:%M:%S"))
