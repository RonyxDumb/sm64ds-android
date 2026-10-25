import io.github.ronyxdumb.sm64ds.RomImport;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
public final class RomImportTest {
    private static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    private static byte[] zip(String... names) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (String name : names) { zip.putNextEntry(new ZipEntry(name)); zip.write(new byte[]{1,2,3}); zip.closeEntry(); }
        }
        return bytes.toByteArray();
    }
    private static void rejected(byte[] input, File output) throws Exception {
        try { RomImport.copy(new ByteArrayInputStream(input), output, n -> {}); throw new AssertionError("accepted invalid input"); }
        catch (IOException expected) { check(!output.exists()); }
    }
    public static void main(String[] args) throws Exception {
        Path dir = Files.createTempDirectory("sm64ds-import-");
        File out = dir.resolve("pending").toFile();
        RomImport.copy(new ByteArrayInputStream(new byte[]{1,2,3}), out, n -> {});
        check(Arrays.equals(Files.readAllBytes(out.toPath()),new byte[]{1,2,3}));
        RomImport.copy(new ByteArrayInputStream(zip("../../outside.nds")), out, n -> {});
        check(out.length()==3);
        try { RomImport.verifySupportedRom(out); throw new AssertionError("bad digest accepted"); }
        catch(IOException expected) { } check(!dir.resolve("outside.nds").toFile().exists());
        rejected(zip("a.nds","b.nds"),out);
        rejected(zip("readme.txt"),out);
        // Highly compressible bomb: small archive, >128 MiB expanded.
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(bytes)) {
            z.putNextEntry(new ZipEntry("bomb.nds"));
            byte[] block = new byte[1024*1024];
            for(int i=0;i<129;i++) z.write(block);
            z.closeEntry();
        }
        rejected(bytes.toByteArray(),out);
        // Provider disconnect must remove partial staging data.
        InputStream broken = new InputStream() { int count; public int read() throws IOException { if (++count>50) throw new IOException("disconnected"); return 0; } };
        try { RomImport.copy(broken,out,n->{});throw new AssertionError(); } catch(IOException expected) { check(!out.exists()); }
        if (args.length > 0) {
            try (InputStream in = new FileInputStream(args[0])) { RomImport.copy(in,out,n->{}); }
            check(out.length()==16777216); RomImport.verifySupportedRom(out);
            try (RandomAccessFile corrupt = new RandomAccessFile(out,"rw")) { corrupt.seek(0x100000); corrupt.writeByte(corrupt.readByte() ^ 1); }
            try { RomImport.verifySupportedRom(out); throw new AssertionError("corruption accepted"); } catch(IOException expected) { }
            out.delete();
        }
        Files.delete(dir);
        System.out.println("ROM import tests passed");
    }
}
