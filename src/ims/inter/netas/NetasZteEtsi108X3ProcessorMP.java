package ims.inter.netas;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Drop-in wrapper around NetasZteEtsi108X3Processor that accepts gateway INVITEs
 * the base processor rejects:
 *  - multipart/mixed bodies (SDP + ISUP): the application/sdp part is extracted
 *    and the INVITE is rebuilt as a single-part application/sdp message;
 *  - missing isub= correlation parameters: default values are injected into
 *    To / From / P-Asserted-Identity URIs.
 * Everything else passes through untouched. Deploy by setting
 *   mgcf.x3.triProc=netas.NetasZteEtsi108X3ProcessorMP
 */
public class NetasZteEtsi108X3ProcessorMP extends NetasZteEtsi108X3Processor {

    private static final String DEFAULT_ISUB_TO = "0f68308318f68308318f";
    private static final String DEFAULT_ISUB_FROM = "2312191151059841300004f1f";
    private static final String ISUB_SUFFIX = ";isub-encoding=user";

    public NetasZteEtsi108X3ProcessorMP() {
        super();
    }

    @Override
    public void process(long captureTime, byte[] data, int off, int len, boolean packed)
            throws IOException {
        byte[] fixed = fix(data, off, len);
        super.process(captureTime, fixed, 0, fixed.length, packed);
    }

    private static byte[] fix(byte[] data, int off, int len) {
        String s = new String(data, off, len, StandardCharsets.ISO_8859_1);
        if (!s.startsWith("INVITE ")) {
            byte[] out = new byte[len];
            System.arraycopy(data, off, out, 0, len);
            return out;
        }
        int split = s.indexOf("\r\n\r\n");
        if (split < 0) {
            byte[] out = new byte[len];
            System.arraycopy(data, off, out, 0, len);
            return out;
        }
        String[] headLines = s.substring(0, split).split("\r\n", -1);
        String body = s.substring(split + 4);

        String boundary = findBoundary(headLines);
        if (boundary != null) {
            String sdp = extractSdpPart(body, boundary);
            if (sdp != null) {
                body = sdp;
                replaceHeader(headLines, "Content-Type", "Content-Type: application/sdp");
            }
        }

        injectIsub(headLines, "To:", DEFAULT_ISUB_TO);
        injectIsub(headLines, "From:", DEFAULT_ISUB_FROM);
        injectIsub(headLines, "P-Asserted-Identity:", DEFAULT_ISUB_FROM);

        replaceHeader(headLines, "Content-Length", "Content-Length: " + body.length());

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < headLines.length; i++) {
            if (headLines[i] == null) continue;
            sb.append(headLines[i]);
            if (i < headLines.length - 1) sb.append("\r\n");
        }
        sb.append("\r\n\r\n").append(body);
        return sb.toString().getBytes(StandardCharsets.ISO_8859_1);
    }

    /** Find boundary= value of a multipart Content-Type header line. */
    private static String findBoundary(String[] headLines) {
        for (String line : headLines) {
            if (line == null || !startsWithIgnoreCase(line, "Content-Type:")) continue;
            String low = line.toLowerCase();
            if (!low.contains("multipart")) return null;
            int b = low.indexOf("boundary=");
            if (b < 0) return null;
            String v = line.substring(b + 9).trim();
            if (v.startsWith("\"")) v = v.substring(1, v.indexOf('"', 1));
            else {
                int end = v.indexOf(';');
                if (end >= 0) v = v.substring(0, end);
                v = v.trim();
            }
            return v.isEmpty() ? null : v;
        }
        return null;
    }

    /** Extract the body of the first application/sdp MIME part. */
    private static String extractSdpPart(String body, String boundary) {
        String delim = "--" + boundary;
        int pos = body.indexOf(delim);
        while (pos >= 0) {
            int lineEnd = body.indexOf('\n', pos);
            if (lineEnd < 0) return null;
            int partStart = lineEnd + 1;
            int next = body.indexOf(delim, partStart);
            String part = next < 0 ? body.substring(partStart)
                                   : body.substring(partStart, next);
            if (part.endsWith("\r\n")) part = part.substring(0, part.length() - 2);
            int hdrEnd = part.indexOf("\r\n\r\n");
            if (hdrEnd >= 0) {
                String hdrs = part.substring(0, hdrEnd).toLowerCase();
                if (hdrs.contains("content-type") && hdrs.contains("application/sdp")) {
                    return part.substring(hdrEnd + 4);
                }
            }
            pos = next;
        }
        return null;
    }

    /** Replace (or drop continuations of) a header line; null marks removal. */
    private static void replaceHeader(String[] headLines, String name, String newLine) {
        for (int i = 0; i < headLines.length; i++) {
            if (headLines[i] == null) continue;
            boolean isTarget = startsWithIgnoreCase(headLines[i], name + ":")
                    || startsWithIgnoreCase(headLines[i], name + " :");
            if (!isTarget) continue;
            headLines[i] = newLine;
            // drop folded continuation lines of the replaced header
            for (int j = i + 1; j < headLines.length; j++) {
                if (headLines[j] != null && (headLines[j].startsWith(" ")
                        || headLines[j].startsWith("\t"))) {
                    headLines[j] = null;
                } else {
                    break;
                }
            }
            return;
        }
    }

    /** Append ;isub=...;isub-encoding=user inside the addr-spec if missing. */
    private static void injectIsub(String[] headLines, String header, String isubValue) {
        for (int i = 0; i < headLines.length; i++) {
            String line = headLines[i];
            if (line == null || !startsWithIgnoreCase(line, header)) continue;
            if (line.contains("isub=")) return; // already has correlation params
            int gt = line.indexOf('>');
            String inject = ";isub=" + isubValue + ISUB_SUFFIX;
            if (gt > 0) {
                headLines[i] = line.substring(0, gt) + inject + line.substring(gt);
            } else {
                headLines[i] = line + inject;
            }
            return;
        }
    }

    private static boolean startsWithIgnoreCase(String s, String prefix) {
        return s.length() >= prefix.length()
                && s.substring(0, prefix.length()).equalsIgnoreCase(prefix);
    }
}
