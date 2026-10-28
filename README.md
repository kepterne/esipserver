# esipserv — X2/X3 SIP interception receiver (C)

A single-binary, statically linked SIP endpoint that receives intercepted calls
from an MGCF/gateway (Netas/ZTE ETSI-108 dialect), records them as LI handover
files, and streams them live to a browser.

Built and hardened during the mega_cs lab interop effort (Oct 2026). See
`esipserv_progress_report.docx` for the full investigation-and-build report
with screenshots.

## What it does

- **SIP/UDP** (default 5070): answers OPTIONS keepalives, INVITE 100/180/200
  with its own SDP. Accepts the real gateway dialect: multipart SDP+ISUP
  bodies (SDP part extracted, remote RTP address learned), `tel:` or `sip:`
  URIs, missing or present `isub=` correlation parameters, folded headers.
- **RTP receiving** (default 5072, static single port): full RTP header parse,
  per-party demux by SSRC (up to 4 legs per call), sequence-loss tracking,
  late codec adoption when SDP is absent.
- **HI3 recording** (`hi3/`): per-packet BER records — LIID, microsecond
  timestamp, record sequence, SSRC, RTP timestamp/sequence, raw payload — in
  the `handover-interface-stream,v1.1,ehif` container, plus a playable
  per-party copy: `.au` (PCMA/PCMU) or real `.amr` (RFC 4867 octet-aligned,
  CMR stripped). AMR bandwidth-efficient / unknown types pass through raw.
- **HI2 recording** (`hi2/`): IRI-Begin/End `.ehif` records, byte-compatible
  with the voidalims receiver output (operator/NE/correlation identification,
  FNV-1a correlation from Call-ID).
- **Web monitoring** (default 8080):
  - `/` embedded player — WebSocket live audio (codec announced in the hello
    frame, G.711 decoded in JS), per-party volume sliders, rolling RMS graph;
    HTTP polling `/live` fallback for non-WS environments.
  - `/calls` JSON status, `/dl/<callid>` recording download,
    `/ws/<callid>` RFC 6455 push (`[party-byte][native audio]` + RMS status).
- **Watermark hook**: `esipwm_embed(short *pcm, int n, uint64_t id)` is called
  on decoded linear PCM with the per-call 64-bit correlation; the `.ehif`
  archive stays untouched (raw payload), only playable copies and the live
  stream are marked. Default no-op stub; gated by `esipserv.hi3.watermark`.

## Build & run

```sh
cd esip && make            # static x86-64 binary, no glibc dependency
./esipserv 5060            # SIP port argument overrides esipserv.json
```

`esipserv.json` is created with defaults on first start (working-directory
relative). Key knobs: `esipserv.sip.port|rtpport|rtprecv`,
`esipserv.hi2.*`, `esipserv.hi3.au|folder|watermark`, `esipserv.web.enable|port`.

Regression suite: `python3 esip/replay.py 127.0.0.1 5060 5060_dump_2.pcap`
(expects 7/7 PASS).

## Layout

- `esip/` — the C project (esipserv.c, esip.c/h, Makefile, replay.py, test pcap)
- `ehif_decode.py` — BER walker/decoder for HI2 and HI3 `.ehif` files
- `probe scripts` (`sip_test.py`, `burst*.py`, `zte_invite*.py`,
  `multipart_isub_invite.py`, ...) — the interop probe toolkit used during
  the investigation
- `shots/` — screenshots embedded in the report
- `gen_report.js` — regenerates `esipserv_progress_report.docx`

## Status / caveats

Lab- and pilot-grade. Before carrier production: bounded web I/O (slow
clients must not stall the poll loop), hashed dialog lookup beyond ~64
concurrent calls, SIP TLS + peer authentication, signed/rolled output files,
NTP discipline, HA pairing, and byte-compat validation of media `.ehif`
against a real voida `V-*.ehif` sample.
