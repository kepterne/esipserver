#!/usr/bin/env python3
"""Parse pcap, extract UDP payloads on port 5060, byte-level SIP analysis."""
import struct, sys, collections

PCAP = "/Users/ekremkaracan/Downloads/5060_dump_2.pcap"

def read_pcap(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:4]
    if magic == b"\xd4\xc3\xb2\xa1":      # little-endian, usec
        endian = "<"; nano = False
    elif magic == b"\xa1\xb2\xc3\xd4":    # big-endian, usec
        endian = ">"; nano = False
    elif magic == b"\x4d\x3c\xb2\xa1":    # little-endian, nsec
        endian = "<"; nano = True
    else:
        sys.exit("unknown magic %r" % magic)
    off = 24
    pkts = []
    while off + 16 <= len(data):
        ts_sec, ts_frac, incl_len, orig_len = struct.unpack(endian + "IIII", data[off:off+16])
        off += 16
        pkt = data[off:off+incl_len]
        off += incl_len
        pkts.append((ts_sec, ts_frac, pkt))
    return pkts

def udp_payloads(pkts):
    out = []
    for ts_sec, ts_frac, pkt in pkts:
        if len(pkt) < 14: continue
        ethertype = struct.unpack("!H", pkt[12:14])[0]
        ip = pkt[14:]
        if ethertype == 0x0800:      # IPv4
            pass
        elif ethertype == 0x8100:    # VLAN
            ip = pkt[18:]
        else:
            continue
        ihl = (ip[0] & 0x0F) * 4
        proto = ip[9]
        src = ".".join(str(b) for b in ip[12:16])
        dst = ".".join(str(b) for b in ip[16:20])
        if proto != 17:  # UDP only
            continue
        udp = ip[ihl:]
        sport, dport, ulen = struct.unpack("!HHH", udp[:6])
        payload = udp[8:8+ulen-8] if ulen >= 8 else b""
        out.append((ts_sec, src, sport, dst, dport, payload))
    return out

pkts = read_pcap(PCAP)
flows = udp_payloads(pkts)
sips = [(t, s, sp, d, dp, p) for (t, s, sp, d, dp, p) in flows
        if (sp == 5060 or dp == 5060) and p]

print("total IP packets: %d ; UDP 5060 payloads: %d" % (len(pkts), len(sips)))

# Get unique messages (dedupe retransmissions by payload)
uniq = collections.OrderedDict()
for t, s, sp, d, dp, p in sips:
    uniq.setdefault(p, []).append((t, s, sp, d, dp))

print("unique payloads: %d\n" % len(uniq))
for p, occurrences in uniq.items():
    t, s, sp, d, dp = occurrences[0]
    first_line = p.split(b"\r\n", 1)[0].split(b"\n", 1)[0].decode("latin1")
    print("=" * 78)
    print("%s:%d -> %s:%d  x%d  first: %s"
          % (s, sp, d, dp, len(occurrences), first_line[:70]))

print()
# Focus on the INVITE: byte-level dump
invite = None
for p in uniq:
    if p.startswith(b"INVITE"):
        invite = p
        break
if invite:
    print("#" * 78)
    print("INVITE raw byte analysis, len=%d" % len(invite))
    print("#" * 78)
    # Show every byte with escapes, line by line
    lines = invite.split(b"\n")
    for i, ln in enumerate(lines):
        print("%3d| %r" % (i, ln))
    print()
    # Line-ending audit
    crlf = invite.count(b"\r\n")
    lones = len([i for i, ln in enumerate(lines[:-1])]) - crlf
    print("CRLF count: %d ; total LF: %d ; lone LF (bad): %d" % (crlf, invite.count(b"\n"), invite.count(b"\n") - crlf))
    print("lone CR count: %d" % (invite.count(b"\r") - crlf))
    # Header/body split
    if b"\r\n\r\n" in invite:
        head, body = invite.split(b"\r\n\r\n", 1)
        print("header bytes: %d ; body bytes: %d ; declared Content-Length check below")
        import re
        m = re.search(rb"Content-Length:[ \t]*(\d+)", head)
        if m:
            decl = int(m.group(1))
            print("declared Content-Length: %d ; actual after blank line: %d ; match: %s"
                  % (decl, len(body), decl == len(body)))
        # check what transport gave us: full datagram
        print("whole datagram len: %d ; head+4+body: %d" % (len(invite), len(head)+4+len(body)))
    # MIME boundary audit
    print()
    print("ISUP part hex dump:")
    idx = invite.find(b"application/isup")
    if idx >= 0:
        seg = invite[idx:]
        bl = seg.find(b"\r\n\r\n")
        isup = seg[bl+4:]
        end = isup.find(b"--")
        isup = isup[:end] if end >= 0 else isup
        print("isup len=%d" % len(isup))
        h = isup.hex()
        for j in range(0, len(h), 32):
            off = j // 2
            print("  %04x  %s" % (off, " ".join(h[k:k+2] for k in range(j, min(j+32, len(h)), 2))))
