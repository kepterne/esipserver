#!/usr/bin/env python3
data = open("/Users/ekremkaracan/Downloads/5060_dump_2.pcap", "rb").read()
i = data.find(b"INVITE sip:123456")
inv = data[i:i+1292]
idx = inv.find(b"application/isup;base")
seg = inv[idx:]
bl = seg.find(b"\r\n\r\n")
isup = seg[bl+4:]
end = isup.find(b"\r\n--")
isup = isup[:end]
print("ISUP len:", len(isup))
print(" ".join("%02x" % b for b in isup))

def bcd(bs):
    out = ""
    for b in bs:
        out += str(b & 0xF)
        if (b >> 4) != 0xF:
            out += str(b >> 4)
    return out

cic = (isup[0] << 8) | isup[1]
mt = isup[2]
print("CIC =", cic, "; message type = 0x%02x" % mt)
p = 3
print("nature_of_connection=0x%02x FCI=0x%02x%02x category=0x%02x TMR=0x%02x"
      % (isup[p], isup[p+1], isup[p+2], isup[p+3], isup[p+4]))
ptr_called, ptr_opt = isup[p+5], isup[p+6]
print("ptr_called=%d ptr_opt=%d" % (ptr_called, ptr_opt))

q = p + 6 + ptr_called - 1
l = isup[q]
called = isup[q+1:q+1+l]
print("CalledPartyNumber len=%d NAI=0x%02x odd=%d digits=%s"
      % (l, called[0] & 0x7f, (called[0] >> 7) & 1, bcd(called[1:])))

q = p + 6 + ptr_opt - 1
names = {
    0x0a: "CallingPartyNumber",
    0x1c: "ApplicationTransportParameter",
    0x2a: "PropagationDelayCounter",
    0x31: "CallHistoryInformation",
    0x39: "ParameterCompatibilityInformation",
    0x70: "GenericNotificationIndicator",
    0x04: "CallingGeographicLocation",
    0x11: "CarrierIdentification",
    0x29: "GenericDigits",
    0x32: "MLPPPrecedence",
    0x34: "HTRInformation",
    0x35: "OriginatingISCPointCode",
    0x3a: "FacilityIndicatorGroup2",
    0x62: "CallTransferReference",
    0x6c: "RemoteOperations",
    0x74: "ServiceCode",
    0x77: "TransactionRequestCapabilitiesGRQ",
    0x06: "AccessTransport",
    0x08: "BackwardCallIndicators",
    0x0e: "BackwardGVNS",
    0x1f: "NetworkSpecificFacility",
    0x23: "ConnectedNumber",
    0x24: "OptionBackwardCallIndicators",
    0x2f: "RedirectingNumber",
}
while q < len(isup) and isup[q] != 0:
    pid, pl = isup[q], isup[q+1]
    val = isup[q+2:q+2+pl]
    extra = ""
    if pid == 0x0a and pl >= 2:
        extra = " NAI=0x%02x digits=%s" % (val[0] & 0x7f, bcd(val[1:]))
    print("  opt param 0x%02x (%s) len=%d%s"
          % (pid, names.get(pid, "?"), pl, extra))
    q += 2 + pl
