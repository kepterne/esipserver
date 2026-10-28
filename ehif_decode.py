#!/usr/bin/env python3
"""Generic BER decoder for .ehif (ETSI LI handover-interface) files.
File layout: 'handover-interface-stream,v1.1,ehif' NUL + 1-byte body length + BER."""
import sys, datetime

def read_tlv(buf, off):
    """Return (tag_bytes, tag_num, constructed, content, next_off)."""
    start = off
    b = buf[off]; off += 1
    tag_num = b & 0x1F
    tag_bytes = bytes([b])
    if tag_num == 0x1F:                      # high-tag-number form
        tag_num = 0
        while True:
            b = buf[off]; off += 1
            tag_num = (tag_num << 7) | (b & 0x7F)
            tag_bytes += bytes([b])
            if not b & 0x80:
                break
    constructed = bool(b if False else tag_bytes[0] & 0x20) if len(tag_bytes) == 1 else None
    # class + constructed from first byte
    first = buf[start]
    cls = first >> 6
    constructed = bool(first & 0x20)
    # length
    lb = buf[off]; off += 1
    if lb < 0x80:
        length = lb
    elif lb == 0x80:
        raise ValueError("indefinite length not supported at %d" % start)
    else:
        n = lb & 0x7F
        length = int.from_bytes(buf[off:off + n], "big"); off += n
    content = buf[off:off + length]
    return tag_bytes, tag_num, cls, constructed, content, off + length, start

def looks_text(data):
    if not data:
        return False
    try:
        s = data.decode("ascii")
        return all(32 <= ord(c) < 127 for c in s)
    except UnicodeDecodeError:
        return False

def fmt_ts(s):
    # e.g. 261027134012018758 / 20261027171207.6
    if s and s[0] == "2" and len(s) >= 14 and s[:14].isdigit():
        try:
            base = datetime.datetime.strptime(s[:14], "%Y%m%d%H%M%S")
            return base.isoformat() + s[14:]
        except ValueError:
            pass
    if s and len(s) >= 12 and s[:12].isdigit():
        try:
            base = datetime.datetime.strptime("20" + s[:12], "%Y%m%d%H%M%S")
            return base.isoformat() + "." + s[12:]
        except ValueError:
            pass
    return None

def walk(buf, off, end, depth, out):
    while off < end:
        tag, num, cls, cons, content, noff, start = read_tlv(buf, off)
        if noff > end + 8:
            break
        clsname = {0: "UNIV", 1: "APP", 2: "CTX", 3: "PRIV"}[cls]
        label = "%s[%s]" % (clsname, num) if cls != 0 else \
                {16: "SEQ", 2: "INT", 4: "OCT", 6: "STR", 23: "UTCT", 24: "GT"}.get(num, "U%d" % num)
        pad = "  " * depth
        if cons and cls == 0 and num == 16 or (cons and content):
            # try to recurse; if it fails, show as leaf
            try:
                out.append("%s%s (len %d) @" % (pad, label, len(content)))
                walk(content, 0, len(content), depth + 1, out)
            except Exception:
                out.append("%s%s (len %d) RAW %s" % (pad, label, len(content), content.hex()))
        else:
            v = None
            if cls == 0 and num == 2:
                v = str(int.from_bytes(content, "big", signed=True)) if content else "0"
            elif looks_text(content):
                s = content.decode()
                t = fmt_ts(s)
                v = '"%s"' % s + (("  -> %s" % t) if t else "")
            else:
                v = content.hex() if content else "(empty)"
            out.append("%s%s len=%d  %s" % (pad, label, len(content), v))
        off = noff

def decode_file(path):
    data = open(path, "rb").read()
    hdr_end = data.find(b"\0")
    header = data[:hdr_end].decode("latin1")
    rest = data[hdr_end + 1:]
    print("=" * 74)
    print("FILE: %s (%d bytes)" % (path.split("/")[-1], len(data)))
    print("header: %r ; records: %d bytes of framed body" % (header, len(rest)))
    # framed record stream: each record = 1-byte length + BER
    off = 0
    recno = 0
    while off < len(rest):
        if off + 2 > len(rest):
            break
        ln = rest[off]
        rec = rest[off + 1:off + 1 + ln]
        if len(rec) < ln or ln == 0:
            print("[record %d] bad frame (len=%d) — stopping" % (recno, ln))
            break
        recno += 1
        out = []
        walk(rec, 0, len(rec), 1, out)
        if recno <= 3 or recno % 20 == 0:
            print("[record %d] len=%d" % (recno, ln))
            print("\n".join(out))
        off += 1 + ln
    print("[total records: %d]" % recno)

for p in sys.argv[1:]:
    decode_file(p)
