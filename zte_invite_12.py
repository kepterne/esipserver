#!/usr/bin/env python3
"""Send an INVITE in the exact Netas/ZTE ETSI-108 dialect that
NetasZteEtsi108X3Processor expects (template found inside receiver.jar):
tel: R-URI with isub, isub in From/To/P-Asserted-Identity, single application/sdp
body with PCMA/AMR/telephone-event. UDP only. Stops at first reply."""
import socket, secrets, time, datetime

SERVER = ("172.26.97.12", 5060)

def tok(n=10):
    return secrets.token_hex(n // 2)

p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); p.connect(SERVER)
IP = p.getsockname()[0]; p.close()
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.bind(("0.0.0.0", 0))
P = sock.getsockname()[1]
print("ZTE-dialect INVITE from %s:%d at %s" % (IP, P,
      datetime.datetime.now().strftime("%H:%M:%S")))

sdp = (
    "v=0\r\n"
    "o=- 756491338 756491338 IN IP4 {ip}\r\n"
    "s=-\r\n"
    "c=IN IP4 {ip}\r\n"
    "t=0 0\r\n"
    "m=audio 40002 RTP/AVP 8 96 0 97\r\n"
    "a=rtpmap:8 PCMA/8000/1\r\n"
    "a=rtpmap:96 AMR/8000/1\r\n"
    "a=fmtp:96 mode-set=7\r\n"
    "a=rtpmap:0 PCMU/8000/1\r\n"
    "a=rtpmap:97 telephone-event/8000/1\r\n"
    "a=fmtp:97 0-15\r\n"
    "a=sendrecv\r\n"
).format(ip=IP)
sdp_b = sdp.encode()

callid = "%s@testistmgcf.ttimscore.com.tr" % tok(14)
inv = (
    "INVITE tel:903121000011;isub=0f68308318f68308318f;isub-encoding=user SIP/2.0\r\n"
    "Via: SIP/2.0/UDP {ip}:{port};branch=z9hG4bK{br}\r\n"
    "From: <tel:00903121112525;isub=2312191151059841300004f1f;isub-encoding=user>;tag={tag}\r\n"
    "To: <tel:903121000011;isub=0f68308318f68308318f;isub-encoding=user>\r\n"
    "Call-ID: {cid}\r\n"
    "CSeq: 1 INVITE\r\n"
    "Contact: <sip:{ip}:{port}>\r\n"
    "Max-Forwards: 70\r\n"
    "Supported: 100rel\r\n"
    "Privacy: none\r\n"
    "P-Asserted-Identity: <tel:00903121112525;isub=2312191151059841300004f1f;isub-encoding=user>\r\n"
    "Content-Type: application/sdp\r\n"
    "Content-Length: {cl}\r\n"
    "\r\n"
).format(ip=IP, port=P, br=tok(10), tag=tok(14), cid=callid, cl=len(sdp_b))
msg = inv.encode() + sdp_b
print(inv)

for i, w in enumerate([1.0, 2.0]):
    sock.sendto(msg, SERVER)
    sock.settimeout(w)
    try:
        data, addr = sock.recvfrom(4096)
        print(">>> REPLY from %s:%d:" % addr)
        print(data.decode("latin1"))
        break
    except socket.timeout:
        print("no reply within %.1f s (attempt %d)" % (w, i + 1))
