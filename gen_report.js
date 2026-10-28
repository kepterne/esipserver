// esipserv / LI link progress report — docx generation
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  ImageRun, PageBreak, Header, Footer, PageNumber, NumberFormat,
  AlignmentType, HeadingLevel, WidthType, BorderStyle, ShadingType,
  TableOfContents, SectionType,
} = require("docx");
const fs = require("fs");
const { imageSize } = require("image-size");

const SHOTS = "/Users/ekremkaracan/.zcode/workspace/draft-3da28232-e432-49a8-a3f7-f6a0b87208f3/shots";
const OUT = "/Users/ekremkaracan/Downloads/esipserv_progress_report.docx";

// ---------- palette DS-1 (Deep Sea) ----------
const P = {
  bg: "0B1C2C", accent: "529286",
  titleColor: "FFFFFF", subtitleColor: "B0B8C0", metaColor: "90989F", footerColor: "687078",
  body: "182030", primary: "0B1220",
  table: { headerBg: "529286", headerText: "FFFFFF", accentLine: "529286", innerLine: "BECFCC", surface: "E8ECEB" },
};

// ---------- helpers ----------
function h1(t) { return new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 360, after: 160, line: 312 }, children: [new TextRun({ text: t, bold: true, color: P.primary })] }); }
function h2(t) { return new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240, after: 120, line: 312 }, children: [new TextRun({ text: t, bold: true, color: P.primary })] }); }
function body(t, opts) {
  return new Paragraph({
    alignment: AlignmentType.JUSTIFIED,
    spacing: { line: 312, after: 120 },
    children: [new TextRun({ text: t, size: 22, color: P.body, ...(opts || {}) })],
  });
}
function bodyRuns(runs) {
  return new Paragraph({ alignment: AlignmentType.JUSTIFIED, spacing: { line: 312, after: 120 }, children: runs });
}
function bullet(t, bold0) {
  const runs = [];
  if (bold0) runs.push(new TextRun({ text: bold0, bold: true, size: 22, color: P.body }));
  runs.push(new TextRun({ text: t, size: 22, color: P.body }));
  return new Paragraph({ bullet: { level: 0 }, spacing: { line: 312, after: 80 }, children: runs });
}
function caption(t) {
  return new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 60, after: 240 },
    children: [new TextRun({ text: t, italics: true, size: 18, color: "606060" })] });
}
function figure(png, cap, maxW, maxH) {
  const buf = fs.readFileSync(SHOTS + "/" + png);
  const dim = imageSize(buf);
  let w = maxW || 560, h = Math.round(w * dim.height / dim.width);
  if (maxH && h > maxH) { h = maxH; w = Math.round(h * dim.width / dim.height); }
  return [
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 120 }, keepNext: true,
      children: [new ImageRun({ data: buf, transformation: { width: w, height: h }, type: "png" })] }),
    caption(cap),
  ];
}
function tbl(title, header, rows, widths) {
  const mk = (text, isH, i) => new TableCell({
    children: [new Paragraph({ spacing: { line: 312 }, children: [new TextRun({ text, bold: !!isH, size: 20, color: isH ? P.table.headerText : P.body })] })],
    shading: isH ? { type: ShadingType.CLEAR, fill: P.table.headerBg } : undefined,
    margins: { top: 60, bottom: 60, left: 120, right: 120 },
    width: widths ? { size: widths[i], type: WidthType.PERCENTAGE } : undefined,
  });
  return [
    new Paragraph({ keepNext: true, spacing: { before: 160, after: 80 }, children: [new TextRun({ text: title, bold: true, size: 21, color: P.primary })] }),
    new Table({
      width: { size: 100, type: WidthType.PERCENTAGE },
      borders: {
        top: { style: BorderStyle.SINGLE, size: 4, color: P.table.accentLine },
        bottom: { style: BorderStyle.SINGLE, size: 4, color: P.table.accentLine },
        left: { style: BorderStyle.NONE }, right: { style: BorderStyle.NONE },
        insideHorizontal: { style: BorderStyle.SINGLE, size: 1, color: P.table.innerLine },
        insideVertical: { style: BorderStyle.NONE },
      },
      rows: [
        new TableRow({ tableHeader: true, cantSplit: true, children: header.map((x, i) => mk(x, true, i)) }),
        ...rows.map(r => new TableRow({ cantSplit: true, children: r.map((x, i) => mk(x, false, i)) })),
      ],
    }),
  ];
}

// ---------- cover (R1 recipe) ----------
function calcTitleLayout(title, maxWidthTwips, preferredPt, minPt) {
  preferredPt = preferredPt || 40; minPt = minPt || 24;
  const charsPerLine = pt => Math.floor(maxWidthTwips / (pt * 10)); // latin ~ half CJK width
  let titlePt = preferredPt, lines;
  const split = () => {
    const cpl = charsPerLine(titlePt); const out = []; let rem = title;
    while (rem.length > cpl) {
      let br = -1;
      for (let i = cpl; i >= Math.floor(cpl * 0.6); i--)
        if (" -_/:,.".includes(rem[i - 1] || "")) { br = i; break; }
      if (br === -1) br = cpl;
      out.push(rem.slice(0, br).trim()); rem = rem.slice(br).trim();
    }
    if (rem) out.push(rem);
    if (out.length > 1 && out[out.length - 1].length <= 3) { const l = out.pop(); out[out.length - 1] += " " + l; }
    return out;
  };
  while (titlePt >= minPt) { lines = split(); if (lines.length <= 3) break; titlePt -= 2; }
  if (!lines || lines.length > 3) { titlePt = minPt; lines = split(); }
  return { titlePt, titleLines: lines };
}
function calcCoverSpacing(p) {
  const SAFETY = 1200, usable = 16838 - SAFETY;
  const titleH = p.titleLineCount * (p.titlePt * 23 + 200);
  const subH = p.hasSubtitle ? (12 * 23 + 600) : 0;
  const enH = p.hasEnglishLabel ? (9 * 23 + 600) : 0;
  const metaH = p.metaLineCount * (10 * 23 + 100);
  const content = titleH + subH + enH + metaH + (p.fixedHeight || 800) + 900;
  const rem = Math.max(usable - content, 400);
  const FOOTER_MIN = 800;
  const rawTop = Math.floor(rem * 0.45), rawBottom = Math.floor(rem * 0.45);
  const bottomSpacing = Math.max(rawBottom, FOOTER_MIN);
  const topSpacing = Math.max(rawTop - Math.max(0, FOOTER_MIN - rawBottom), 400);
  return { topSpacing, midSpacing: Math.max(rem - topSpacing - bottomSpacing, 0), bottomSpacing };
}
function buildCoverR1(cfg) {
  const padL = 1200, padR = 800;
  const { titlePt, titleLines } = calcTitleLayout(cfg.title, 11906 - padL - padR - 300, 40, 24);
  const sp = calcCoverSpacing({ titleLineCount: titleLines.length, titlePt,
    hasSubtitle: !!cfg.subtitle, hasEnglishLabel: !!cfg.englishLabel,
    metaLineCount: (cfg.metaLines || []).length, fixedHeight: 400 });
  const accentLeft = { style: BorderStyle.SINGLE, size: 8, color: P.accent, space: 12 };
  const ch = [];
  ch.push(new Paragraph({ spacing: { before: sp.topSpacing } }));
  ch.push(new Paragraph({
    indent: { left: padL, right: padR }, spacing: { after: 500 },
    border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: P.accent, space: 8 } },
    children: [new TextRun({ text: cfg.englishLabel.split("").join("  "), size: 18, color: P.accent, characterSpacing: 40 })],
  }));
  titleLines.forEach((line, i) => ch.push(new Paragraph({
    indent: { left: padL },
    spacing: { after: i < titleLines.length - 1 ? 100 : 300, line: Math.ceil(titlePt * 23), lineRule: "atLeast" },
    children: [new TextRun({ text: line, size: titlePt * 2, bold: true, color: P.titleColor })],
  })));
  ch.push(new Paragraph({ indent: { left: padL }, spacing: { after: 800 },
    children: [new TextRun({ text: cfg.subtitle, size: 24, color: P.subtitleColor })] }));
  (cfg.metaLines || []).forEach(line => ch.push(new Paragraph({
    indent: { left: padL + 200 }, spacing: { after: 80 }, border: { left: accentLeft },
    children: [new TextRun({ text: line, size: 24, color: P.metaColor })],
  })));
  ch.push(new Paragraph({ spacing: { before: sp.bottomSpacing } }));
  ch.push(new Paragraph({
    indent: { left: padL, right: padR },
    border: { top: { style: BorderStyle.SINGLE, size: 6, color: P.accent, space: 8 } },
    children: [
      new TextRun({ text: cfg.footerLeft, size: 18, color: P.footerColor }),
      new TextRun({ text: "        " + cfg.footerRight, size: 18, color: P.footerColor }),
    ],
  }));
  // wrapper table 16838 exact, no borders, dark bg
  const wrap = new Table({
    width: { size: 11906, type: WidthType.DXA },
    borders: { top: { style: BorderStyle.NONE }, bottom: { style: BorderStyle.NONE }, left: { style: BorderStyle.NONE }, right: { style: BorderStyle.NONE }, insideHorizontal: { style: BorderStyle.NONE }, insideVertical: { style: BorderStyle.NONE } },
    rows: [new TableRow({ height: { value: 16838, rule: "exact" }, children: [new TableCell({
      shading: { type: ShadingType.CLEAR, fill: P.bg },
      children: ch,
      margins: { top: 0, bottom: 0, left: 0, right: 0 },
    })] })],
  });
  return [wrap];
}

// ---------- content ----------
const exec = [
  h1("Executive Summary"),
  body("This report documents a complete investigation-and-build cycle on the mega_cs lawful-interception environment: from a captured SIP trace in which the receiver silently dropped every INVITE from the MGCF gateway, through root-cause analysis on the voidalims Java receiver, to a from-scratch C replacement (esipserv) with multi-codec HI3 recording and live browser monitoring."),
  bullet("The received INVITEs were never the problem: no post-2018 SIP feature was in play. The Java receiver failed during its own initialization (missing rtp.receiver.port in static mode), and independently requires the ZTE ETSI-108 dialect (isub correlation parameters + single-part SDP body).", "Root cause. "),
  bullet("A missing one-line configuration fix plus restart restored the receiver - it answered its first INVITE in its recorded history with 200 OK + SDP.", "Fix on .15. "),
  bullet("esipserv (C, statically linked) now implements the full X3 receiver role: multipart/no-isub INVITEs, isub extraction, per-party SSRC demux, PCMA/PCMU/AMR recording to per-packet .ehif BER records plus playable .au/.amr copies, HI2 IRI begin/end records, and a web UI with WebSocket live audio, per-party volume and a rolling RMS level graph.", "New receiver. "),
  bullet("A 64-bit watermark hook (esipwm_embed, keyed by the per-call correlation number) is wired into the decoded-PCM path, config-gated, with the .ehif archive kept watermark-free.", "Watermark ready. "),
];

const s1 = [
  h1("1. Initial Problem"),
  body("The starting point was a packet capture (5060_dump_2.pcap) of SIP traffic between the gateway (10.240.16.1:5050) and the receiver (172.26.13.15:5060). The receiver answered OPTIONS keepalives within ~6 ms but ignored a call INVITE completely - the gateway retransmitted it seven times over 32 seconds (RFC 3261 Timer E pattern) and gave up. The original question was whether anything new since 2018 prevented the receiver from parsing the INVITE."),
  ...figure("c1_pcap.png", "Figure 1 - Capture overview: OPTIONS answered, INVITE retransmitted 7x with zero replies."),
  body("The INVITE itself is a 1292-byte multipart/mixed message carrying an SDP offer (PCMA/PCMU/telephone-event) and a 40-byte binary ISUP IAM. Several irregularities stood out: the ISUP Content-Type parameters base/version appear swapped compared to RFC 3204, the Accept header is folded across lines, and Content-Length has a double space - all legal or cosmetic, all later proven irrelevant."),
  ...figure("c2_invite.png", "Figure 2 - The multipart INVITE (SDP + ISUP) exactly as captured."),
];

const s2 = [
  h1("2. Root-Cause Investigation"),
  body("Differential probing from the lab network showed the receiver answered OPTIONS but silently discarded REGISTER, unknown methods, malformed junk and every INVITE - including a byte-perfect ZTE-dialect INVITE. That eliminated content parsing as the immediate blocker and pointed at a method-aware policy. Receiver-side inspection (SSH) then ruled out iptables/firewalld/nftables (empty), SELinux (cannot distinguish methods) and identified the listener as a Java process - the voidalims LI receiver. Its TRACE-level log caught every INVITE dying with ERROR run - Cannot process!."),
  ...figure("c3_rootcause.png", "Figure 3 - Receiver log: the failure, the decompiled cause, the one-line config fix, and the first success."),
  ...tbl("Table 1 - Candidate causes vs. evidence",
    ["Candidate", "Verdict", "Evidence"],
    [
      ["Post-2018 SIP extension", "Eliminated", "No pn-*/STIR/Feature-Caps anywhere; probes with plain SDP also dropped"],
      ["Multipart body / ISUP content-type", "Not the trigger", "Single-part SDP INVITEs equally dropped"],
      ["Network firewall / SELinux", "Eliminated", "iptables empty; OPTIONS reaches the app on the same socket"],
      ["App peer ACL", "Superseded", "Both lab IPs and localhost INVITEs fail identically"],
      ["Receiver init failure", "CONFIRMED", "IllegalStateException: RTP port config is missing (decompiled)"],
    ], [30, 20, 50]),
];

const s3 = [
  h1("3. Dialect Requirements for the Gateway"),
  body("With the receiver fixed, a systematic INVITE matrix established exactly what the NetasZteEtsi108X3Processor accepts. Two independent requirements emerged, both mandatory for the production gateway."),
  ...figure("c4_matrix.png", "Figure 4 - Test matrix: body type x isub presence on the fixed receiver."),
  bullet("To URI must carry isub=<X>f<numeric>f<string> (split on literal f); From or P-Asserted-Identity must carry isub=<string>f<int>f... - missing isub is a NullPointerException and a silent drop.", "Correlation. "),
  bullet("The body must be a single application/sdp part. multipart/mixed fails SDP parsing even with valid isub. The URI scheme (sip:/tel:) is irrelevant; codecs, user=phone, PAI and keepalives pass as-is.", "Body. "),
];

const s4 = [
  h1("4. HI2 Records Decoded"),
  body("The system's own .ehif output was reverse-engineered with a purpose-built BER walker. Records are ETSI TS 101 671-style HI2 IRIs: one BER sequence per file carrying LIID, microsecond timestamps, operator/network-element identification, a correlation number, and the IRI payload (BEGIN with caller/callee, END with a status code). This format was then reproduced byte-compatibly by esipserv."),
  ...figure("c5_ehif.png", "Figure 5 - Decoded VOIDA .ehif IRI-Begin record (TurkTelekom operator, caller/callee content)."),
];

const s5 = [
  h1("5. esipserv: the C X3 Receiver"),
  body("Rather than patching the Java side, the team's own gateway software (esipserv, plain C on the lin build server) was extended into a full X3 receiver equivalent. It is statically linked (no glibc dependency), deploys as a single binary, and passed the project's own regression suite 7/7 after every change."),
  ...figure("c6_hi3.png", "Figure 6 - Regression suite 7/7 PASS, live call log with isub extraction, per-party recording files."),
  bullet("Accepts the gateway's real dialect: multipart SDP+ISUP INVITEs (SDP part extracted, remote RTP address learned), missing-isub INVITEs, folded headers; answers 100/180/200 with its own SDP.", "SIP. "),
  bullet("Per-packet BER records (LIID, microsecond timestamp, sequence, SSRC, RTP timestamp/sequence, payload) framed in the same handover-interface-stream container; identification block on the first record.", "HI3 archive. "),
  bullet("Per-party SSRC demux (a/b/c/d legs), per-leg .au (PCMA/PCMU) or real .amr files (RFC 4867 CMR stripped), sequence-loss counters, RMS levels.", "Playable copies. "),
  bullet("IRI-Begin/End records in the decoded HI2 format, keyed by an FNV-1a correlation derived from the Call-ID.", "HI2. "),
  ...figure("c8_arch.png", "Figure 7 - Deployment architecture and data flow."),
];

const s6 = [
  h1("6. Live Monitoring over the Web"),
  body("A built-in HTTP/WebSocket server (single-threaded, poll-loop friendly) provides live monitoring: /calls (JSON status), /live (offset-polled fallback stream), /dl (recording download) and /ws/<callid> - a real RFC 6455 WebSocket that pushes each RTP packet the moment it arrives as [party-byte][native audio], announces the codec once, and streams periodic RMS levels for the graph. The player page decodes G.711 in JavaScript (both A-law and u-law), plays each party through its own gain slider, and draws a rolling two-channel level graph. Browser-side quirks were hardened during a real-browser test session (default-rate AudioContext, getChannelData, autoplay resume), which also caught and fixed two genuine bugs: a broken onclick quoting in the LIVE button and URL-encoded call-IDs not matching dialogs server-side."),
  ...figure("01_player_calls.png", "Figure 8 - Live player: call list with codec, parties, packet/loss counters."),
  ...figure("02_player_live.png", "Figure 9 - LIVE mode: two-party call streaming over WebSocket; RMS graph (green = party A, cyan = party B), per-party volume sliders.", 580, 430),
  ...figure("c7_ws.png", "Figure 10 - Raw WebSocket capture: byte-perfect framing (hello + party-tagged audio + RMS status frames)."),
];

const s7 = [
  h1("7. Configuration and Open Items"),
  ...tbl("Table 2 - esipserv configuration keys (esipserv.json; defaults shown)",
    ["Key", "Default", "Meaning"],
    [
      ["esipserv.sip.port / sip.rtpport", "5070 / 5072", "SIP UDP port; static RTP receiver port"],
      ["esipserv.sip.rtprecv", "1", "RTP listener on/off"],
      ["esipserv.hi2.enable / hi2.folder", "1 / hi2", "IRI begin/end .ehif output"],
      ["esipserv.hi3.au / hi3.folder", "1 / hi3", "playable per-party copies"],
      ["esipserv.hi3.watermark", "0", "enable esipwm_embed() on decoded PCM"],
      ["esipserv.hi2.liid / operator / neid", "0400020204030701 / TurkTelekom / esipserv", "record identification fields"],
      ["esipserv.web.enable / web.port", "1 / 8080", "HTTP + WebSocket monitoring"],
    ], [38, 22, 40]),
  h2("Open items"),
  bullet("The real watermark implementation (separate project) drops into esipwm_embed(short* pcm, int n, uint64_t id); the hook carries the per-call 64-bit correlation and is verified end-to-end except for the embedding itself.", "Watermark. "),
  bullet("The .ehif media format is same-family but not byte-identical to the voida Serializer output; aligning byte-for-byte needs a V-*.ehif sample from 172.26.13.15 (unreachable during this session; binary staged at /opt/esipserv awaits an update push).", "Byte-compat. "),
  bullet("Verify the player once in Safari (Chrome-class browsers verified at protocol level and via the in-app browser test). Firewall 5060/8080 on internet-facing hosts - scanner traffic was observed.", "Ops. "),
];

// ---------- assemble ----------
const pageSize = { size: { width: 11906, height: 16838 }, margin: { top: 1440, bottom: 1440, left: 1701, right: 1417 } };
const footer = (fmt) => new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ children: [PageNumber.CURRENT], size: 18, color: "888888" })] })] });
const header = new Header({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "SIP Interception Link - Investigation & Build Report", size: 16, color: "999999" })] })] });

const doc = new Document({
  styles: { default: { document: {
    run: { font: { ascii: "Calibri", eastAsia: "Microsoft YaHei" }, size: 22, color: P.body },
    paragraph: { spacing: { line: 312 } },
  },
  heading1: { run: { font: { ascii: "Calibri", eastAsia: "SimHei" }, size: 32, bold: true, color: P.primary }, paragraph: { spacing: { before: 360, after: 160, line: 312 } } },
  heading2: { run: { font: { ascii: "Calibri", eastAsia: "SimHei" }, size: 26, bold: true, color: P.primary }, paragraph: { spacing: { before: 240, after: 120, line: 312 } } },
  }},
  sections: [
    { properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 0, bottom: 0, left: 0, right: 0 } } },
      children: buildCoverR1({
        title: "SIP Interception Link: Investigation and Build Report",
        subtitle: "From silently dropped INVITEs to a live-monitoring X3 receiver",
        englishLabel: "LAWFUL INTERCEPTION X2 / X3",
        metaLines: [
          "Environment: mega_cs lab - 172.26.13.15 (voidalims), lin build server (esipserv)",
          "Scope: root-cause analysis, interop dialect, HI2/HI3 recording, live web monitoring",
          "Date: 27-28 October 2026",
        ],
        footerLeft: "esipserv project", footerRight: "progress report v1.0",
      }) },
    { properties: { type: SectionType.NEXT_PAGE, page: { ...pageSize, pageNumbers: { start: 1, formatType: NumberFormat.LOWER_ROMAN } } },
      footers: { default: footer() },
      children: [
        new Paragraph({ spacing: { before: 240, after: 240 }, children: [new TextRun({ text: "Contents", bold: true, size: 32, color: P.primary })] }),
        new TableOfContents("Table of Contents", { hyperlink: true, headingStyleRange: "1-2" }),
        new Paragraph({ spacing: { before: 120 }, children: [new TextRun({ text: "Right-click the table of contents and choose Update Field to refresh page numbers.", italics: true, size: 18, color: "909090" })] }),
      ] },
    { properties: { type: SectionType.NEXT_PAGE, page: { ...pageSize, pageNumbers: { start: 1, formatType: NumberFormat.DECIMAL } } },
      headers: { default: header }, footers: { default: footer() },
      children: [...exec, ...s1, ...s2, ...s3, ...s4, ...s5, ...s6, ...s7] },
  ],
});

Packer.toBuffer(doc).then(buf => { fs.writeFileSync(OUT, buf); console.log("written", OUT, buf.length, "bytes"); });
