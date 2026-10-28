#!/usr/bin/env python
# esipserv test: pcap'd OPTIONS/INVITE replay + ACK/BYE/CANCEL senaryosu
# py2/py3 uyumlu. Kullanim: replay.py <ip> <port> <pcap-dosyasi>
import struct, socket, sys, time

def load_pcap(path):
    data = open(path, 'rb').read()
    endian = '<' if data[:4] == b'\xd4\xc3\xb2\xa1' else '>'
    off, msgs = 24, []
    while off + 16 <= len(data):
        ts, tus, caplen, wl = struct.unpack(endian + 'IIII', data[off:off+16]); off += 16
        pkt = data[off:off+caplen]; off += caplen
        if len(pkt) < 42 or pkt[14+9] != 17:
            continue
        udp = 14 + (pkt[14] & 0x0f) * 4
        sport, dport, ulen = struct.unpack('>HHH', pkt[udp:udp+6])
        payload = pkt[udp+8:udp+ulen]
        msgs.append((sport, dport, payload))
    return msgs

def send_expect(s, payload, want_codes, label, wait=0.4):
    d = b''
    s.sendto(payload, ADDR)
    got = []
    end = time.time() + wait
    while time.time() < end:
        s.settimeout(max(0.05, end - time.time()))
        try:
            d, a = s.recvfrom(4096)
        except socket.timeout:
            break
        head = d.split(b'\r\n')[0].decode('latin1', 'replace')
        code = head.split(' ')[1] if head.startswith('SIP/') and len(head.split(' ')) > 1 else '?'
        got.append(int(code) if code.isdigit() else code)
    ok = all(w in got for w in want_codes)
    print("%-28s -> %s  [%s]" % (label, got, 'PASS' if ok else 'FAIL'))
    return ok, got, d if got else b''

ip, port = sys.argv[1], int(sys.argv[2])
ADDR = (ip, port)
msgs = load_pcap(sys.argv[3])
options = [p for sp, dp, p in msgs if p.startswith(b'OPTIONS')][0]
invite  = [p for sp, dp, p in msgs if p.startswith(b'INVITE')][0]

results = []
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('0.0.0.0', 5050))

# 1) pcap OPTIONS -> 200
ok, got, d = send_expect(s, options, [200], 'OPTIONS (pcap)')
results.append(ok)

# 2) pcap INVITE (multipart + ISUP + NUL'lu govde) -> 100,180,200
ok, got, d = send_expect(s, invite, [100, 180, 200], 'INVITE (pcap)', wait=1.0)
results.append(ok)
ok2 = b'm=audio' in d and b'5072' in d
print("%-28s -> SDP rtpport 5072 var mi: %s" % ('  SDP kontrol', 'PASS' if ok2 else 'FAIL'))
results.append(ok2)

# 3) INVITE retransmit -> cached 200
ok, got, d = send_expect(s, invite, [200], 'INVITE retrans', wait=0.5)
results.append(ok and 100 not in got and 180 not in got)

callid = [l for l in invite.decode('latin1').split('\r\n') if l.startswith('Call-ID')][0].split(':',1)[1].strip()
totag_line = [l for l in d.decode('latin1').split('\r\n') if l.startswith('To:')]
totag = ''
if totag_line and ';tag=' in totag_line[0]:
    totag = totag_line[0].split(';tag=')[1].split(';')[0].strip()

def mk(method, cseq, extra=''):
    return ("%(m)s sip:123456@%(ip)s:%(p)d;user=phone SIP/2.0\r\n"
            "Via: SIP/2.0/UDP 10.240.16.1:5050;branch=z9hG4bKtest%(m)s\r\n"
            "From: <sip:555395269@10.240.16.1:5050;user=phone>;tag=t1\r\n"
            "To: <sip:123456@%(ip)s:%(p)d;user=phone>%(totag)s\r\n"
            "Call-ID: %(cid)s\r\n"
            "CSeq: %(cseq)s\r\n"
            "Max-Forwards: 70\r\n"
            "Content-Length: 0\r\n\r\n" % dict(
                m=method, ip=ip, p=port, cid=callid, cseq=cseq,
                totag=(';tag=' + totag) if totag else ''))

# 4) ACK (200'u onaylar)
s.sendto(mk('ACK', '1 ACK').encode(), ADDR)
time.sleep(0.2)

# 5) BYE -> 200
ok, got, d = send_expect(s, mk('BYE', '2 BYE').encode(), [200], 'BYE')
results.append(ok)

# 6) ikinci cagri: CANCEL senaryosu
cancel_invite = mk('INVITE', '1 INVITE').replace('z9hG4bKtestINVITE', 'z9hG4bKcancel1').replace(callid, 'cancel-test@10.240.16.1').encode()
ok, got, d = send_expect(s, cancel_invite, [100, 180, 200], 'INVITE #2 (cancel test)', wait=0.8)
cancel_msg = mk('CANCEL', '1 CANCEL').replace('z9hG4bKtestCANCEL', 'z9hG4bKcancel1').replace(callid, 'cancel-test@10.240.16.1')
ok2, got, d = send_expect(s, cancel_msg.encode(), [200, 487], 'CANCEL', wait=0.6)
results.append(ok and ok2)

# 7) REGISTER -> 200
reg = mk('REGISTER', '1 REGISTER').replace('z9hG4bKtestREGISTER', 'z9hG4bKreg1')
ok, got, d = send_expect(s, reg.encode(), [200], 'REGISTER')
results.append(ok)

print('====')
print('TOPLAM: %d/%d PASS' % (sum(1 for r in results if r), len(results)))
sys.exit(0 if all(results) else 1)
