#!/usr/bin/env python3
"""UDP-only SIP probe against 172.26.13.15:5060.
1) OPTIONS sanity check (as in the pcap keepalives)
2) INVITE identical in spirit to the pcap one, but single-part
   application/sdp body (no multipart, no ISUP).
Stops as soon as any reply to the INVITE arrives.
"""
import socket, time, sys

SERVER = ("172.26.13.15", 5060)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    sock.bind(("0.0.0.0", 5050))
    lport = 5050
except OSError:
    sock.bind(("0.0.0.0", 0))
    lport = sock.getsockname()[1]

probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.connect(SERVER)
local_ip = probe.getsockname()[0]
probe.close()

print("local socket: %s:%d -> server %s:%d" % (local_ip, lport, *SERVER))

import secrets
def tok(n=10):
    return secrets.token_hex(n // 2)

def send_and_wait(msg, attempts, label):
    """Send msg, retransmit with SIP Timer E backoff, return first reply."""
    waits = [0.5, 1.0, 2.0, 4.0]  # Timer E intervals
    for i in range(attempts):
        sock.sendto(msg, SERVER)
        t0 = time.time()
        wait = waits[min(i, len(waits) - 1)]
        sock.settimeout(wait)
        try:
            data, addr = sock.recvfrom(4096)
            print("[%s] reply in %.0f ms from %s:%d"
                  % (label, (time.time() - t0) * 1000, addr[0], addr[1]))
            return data
        except socket.timeout:
            print("[%s] attempt %d/%d: no reply within %.1f s"
                  % (label, i + 1, attempts, wait))
    return None

# ---- 1. OPTIONS sanity check (mirrors the pcap keepalive) ----
branch_o = "z9hG4bK" + tok(10)
callid_o = "%s.options@%s" % (tok(12), local_ip)
options = (
    "OPTIONS sip:%s:%d SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
    "From: <sip:%s:%d>;tag=%s\r\n"
    "To: <sip:%s:%d>\r\n"
    "Call-ID: %s\r\n"
    "CSeq: 1 OPTIONS\r\n"
    "Max-Forwards: 70\r\n"
    "Content-Length: 0\r\n\r\n"
) % (SERVER[0], SERVER[1], local_ip, lport, branch_o,
     local_ip, lport, tok(10), SERVER[0], SERVER[1], callid_o)
reply = send_and_wait(options.encode(), 2, "OPTIONS")
if reply:
    print(reply.decode("latin1"))
else:
    print("!! No reply to OPTIONS either - server/path unreachable from here.")

# ---- 2. INVITE: same shape as pcap, single-part SDP body ----
branch_i = "z9hG4bK" + tok(10)
fromtag = tok(14)
callid_i = "%s.invite@%s" % (tok(12), local_ip)

sdp = (
    "v=0\r\n"
    "o=- 846417116 846417116 IN IP4 10.240.18.36\r\n"
    "s=-\r\n"
    "c=IN IP4 10.240.18.36\r\n"
    "t=0 0\r\n"
    "m=audio 32954 RTP/AVP 8 0 96\r\n"
    "c=IN IP4 10.240.18.36\r\n"
    "a=rtpmap:8 PCMA/8000/1\r\n"
    "a=rtpmap:0 PCMU/8000/1\r\n"
    "a=rtpmap:96 telephone-event/8000/1\r\n"
    "a=fmtp:96 0-15\r\n"
    "a=sendrecv\r\n"
)
sdp_b = sdp.encode()

invite = (
    "INVITE sip:123456@%s:%d;user=phone SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
    "From: <sip:555395269@%s:%d;user=phone>;tag=%s\r\n"
    "To: <sip:123456@%s:%d;user=phone>\r\n"
    "Call-ID: %s\r\n"
    "CSeq: 1 INVITE\r\n"
    "Contact: <sip:%s:%d>\r\n"
    "Max-Forwards: 70\r\n"
    "Supported: 100rel,early-session\r\n"
    "Allow: INVITE,PRACK,ACK,UPDATE,CANCEL,BYE,OPTIONS,INFO,SUBSCRIBE,REFER,NOTIFY\r\n"
    "Privacy: none\r\n"
    "P-Asserted-Identity: <sip:555395269@%s:%d;user=phone>\r\n"
    "Content-Type: application/sdp\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
) % (SERVER[0], SERVER[1], local_ip, lport, branch_i,
     local_ip, lport, fromtag, SERVER[0], SERVER[1], callid_i,
     local_ip, lport, local_ip, lport, len(sdp_b))

print("\n--- INVITE being sent (single-part SDP) ---")
print(invite + sdp)
reply = send_and_wait(invite.encode() + sdp_b, 4, "INVITE")
if reply:
    print("--- INVITE reply received ---")
    print(reply.decode("latin1"))
    print(">>> RESULT: server answered the INVITE. Test done (stopping here).")
else:
    print(">>> RESULT: no reply to INVITE after 4 attempts (~8 s).")
