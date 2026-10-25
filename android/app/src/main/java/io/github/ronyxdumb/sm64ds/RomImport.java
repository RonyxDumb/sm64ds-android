package io.github.ronyxdumb.sm64ds;

import java.io.*;
import java.util.Locale;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** No archive paths are ever used as output paths. The caller owns publication. */
public final class RomImport {
    public static final long LIMIT = 128L * 1024 * 1024;
    public interface Progress { void update(long bytes); }
    public static void copy(InputStream input, File destination, Progress progress) throws IOException {
        try (PushbackInputStream source = new PushbackInputStream(new BufferedInputStream(input), 4)) {
            byte[] magic = new byte[4];
            int count = 0, n;
            while (count < 4 && (n = source.read(magic, count, 4 - count)) > 0) count += n;
            source.unread(magic, 0, count);
            if (count == 4 && magic[0] == 'P' && magic[1] == 'K') {
                try (ZipInputStream zip = new ZipInputStream(source)) {
                    int entries = 0;
                    long expanded = 0;
                    boolean found = false;
                    ZipEntry entry;
                    while ((entry = zip.getNextEntry()) != null) {
                        if (++entries > 128) throw new IOException("Archive contains too many entries.");
                        boolean nds = !entry.isDirectory() && entry.getName().toLowerCase(Locale.ROOT).endsWith(".nds");
                        if (nds && found) throw new IOException("Select an archive containing exactly one .nds ROM.");
                        if (entry.getSize() > LIMIT) throw new IOException("Archive entry exceeds 128 MiB.");
                        if (nds) {
                            try (FileOutputStream out = new FileOutputStream(destination)) {
                                expanded += transfer(zip, out, LIMIT - expanded, progress);
                                out.getFD().sync();
                            }
                            found = true;
                        } else {
                            expanded += transfer(zip, null, LIMIT - expanded, bytes -> {});
                        }
                        zip.closeEntry(); // verifies ZIP CRC before publication
                    }
                    if (!found) throw new IOException("The archive contains no .nds ROM.");
                }
            } else {
                try (FileOutputStream out = new FileOutputStream(destination)) {
                    transfer(source, out, LIMIT, progress);
                    out.getFD().sync();
                }
            }
        } catch (IOException | RuntimeException e) {
            destination.delete();
            throw e;
        }
    }
    private static long transfer(InputStream in, OutputStream out, long budget, Progress progress) throws IOException {
        byte[] buffer = new byte[65536];
        long total = 0, lastUpdate = 0;
        int n;
        while ((n = in.read(buffer)) != -1) {
            total += n;
            if (total > budget) throw new IOException("Input exceeds the 128 MiB import limit.");
            if (out != null) out.write(buffer, 0, n);
            if (total - lastUpdate >= 262144) { progress.update(total); lastUpdate = total; }
        }
        progress.update(total);
        return total;
    }
    /** Exact reference dump: rejects silent corruption outside the sampled model. */
    public static void verifySupportedRom(File rom) throws IOException {
        final byte[] digest;
        try {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            try (InputStream in = new BufferedInputStream(new FileInputStream(rom))) {
                byte[] block = new byte[65536];
                long total = 0;
                int n;
                while ((n = in.read(block)) != -1) {
                    total += n;
                    if (total > LIMIT) throw new IOException("ROM exceeds the import limit.");
                    sha.update(block, 0, n);
                }
            }
            digest = sha.digest();
        } catch (NoSuchAlgorithmException impossible) { throw new AssertionError(impossible); }
        StringBuilder hex = new StringBuilder();
        for (byte b : digest) hex.append(String.format(Locale.ROOT, "%02x", b & 255));
        if (!hex.toString().equals("ce5829aaa79f06c67140ed1f45cbca738b0a9d0180538c8c1498472d859db032"))
            throw new IOException("Unsupported or corrupted ROM. This build requires the original European ASMP revision 0 reference dump.");
    }
    private RomImport() {}
}
