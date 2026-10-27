import io.github.ronyxdumb.sm64ds.CrashLogStore;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public final class CrashLogStoreTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Path directory = Files.createTempDirectory("sm64ds-crash-report-");
        Path path = directory.resolve("logs").resolve("sm64ds-crash-report.txt");
        CrashLogStore log = new CrashLogStore(path.toFile());
        log.append("SESSION", "Android native startup");
        log.append("FATAL-NATIVE", "SIGSEGV pc=0x1000 fault=0xdeadbeef");
        check(log.tail(8000).contains("SIGSEGV pc=0x1000"));
        check(new CrashLogStore(path.toFile()).tail(8000).contains("Android native startup"));
        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        log.copyTo(exported);
        check(new String(exported.toByteArray(), StandardCharsets.UTF_8).contains("FATAL-NATIVE"));
        // A runaway repeated log must not grow forever, but the most recent
        // fatal report must survive after the file crosses its size bound.
        log.append("BIG", "x".repeat(550_000));
        log.append("LATEST", "native scene 3 aborted");
        check(Files.size(path) < 512 * 1024);
        check(log.tail(16000).contains("native scene 3 aborted"));
        Files.delete(path);
        Files.delete(directory.resolve("logs"));
        Files.delete(directory);
        System.out.println("Persistent crash log/export/rotation tests passed");
    }
}
