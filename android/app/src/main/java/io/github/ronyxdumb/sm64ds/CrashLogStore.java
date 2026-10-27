package io.github.ronyxdumb.sm64ds;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Persistent, ROM-free diagnostic text shared by Android and native signal writers. */
public final class CrashLogStore {
    private static final long MAX_BYTES = 512L * 1024L;
    private static final int RETAIN_BYTES = 384 * 1024;
    private final File file;

    public CrashLogStore(File file) {
        this.file = file;
    }

    public File file() { return file; }

    public synchronized void append(String category, String detail) {
        try {
            File parent = file.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                throw new IOException("Cannot create diagnostic directory");
            shrinkIfNeeded();
            String timestamp = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.ROOT)
                .format(new Date());
            String safeCategory = category == null ? "EVENT" : category.replace('\n', ' ');
            String message = "[" + timestamp + "] [" + safeCategory + "] " +
                (detail == null ? "" : detail) + "\n";
            try (FileOutputStream out = new FileOutputStream(file, true)) {
                out.write(message.getBytes(StandardCharsets.UTF_8));
                // This code runs for rare events only, not on every game frame.
                out.getFD().sync();
            }
        } catch (IOException exception) {
            // Logging must not crash the game, including while recording a crash.
            System.err.println("SM64DS diagnostic logging failed: " + exception);
        }
    }

    private void shrinkIfNeeded() throws IOException {
        if (!file.exists() || file.length() <= MAX_BYTES) return;
        // Modify the same inode: native signal handlers keep an open O_APPEND
        // descriptor and must continue writing to this exact file.
        try (RandomAccessFile data = new RandomAccessFile(file, "rw")) {
            long from = Math.max(0L, data.length() - RETAIN_BYTES);
            byte[] tail = new byte[(int)(data.length() - from)];
            data.seek(from);
            data.readFully(tail);
            data.seek(0);
            data.write("[Older diagnostics trimmed]\n".getBytes(StandardCharsets.UTF_8));
            data.write(tail);
            data.setLength(data.getFilePointer());
        }
    }

    public synchronized void copyTo(OutputStream destination) throws IOException {
        if (!file.isFile()) throw new IOException("No diagnostic file has been created.");
        try (FileInputStream source = new FileInputStream(file)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = source.read(buffer)) != -1)
                destination.write(buffer, 0, count);
        }
        destination.flush();
    }

    public synchronized String tail(int maxBytes) throws IOException {
        if (!file.isFile()) return "Nessun log disponibile.";
        int limit = Math.max(256, Math.min(64 * 1024, maxBytes));
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long start = Math.max(0L, input.length() - limit);
            byte[] buffer = new byte[(int)(input.length() - start)];
            input.seek(start);
            input.readFully(buffer);
            return (start > 0 ? "[Parte iniziale omessa]\n" : "") +
                new String(buffer, StandardCharsets.UTF_8);
        }
    }
}
