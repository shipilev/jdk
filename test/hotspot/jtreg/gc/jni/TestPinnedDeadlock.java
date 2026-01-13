/* @test
 * @library /test/lib
 * @run main/othervm/native -Xmx128m -XX:+UseParallelGC TestPinnedDeadlock
 */

public class TestPinnedDeadlock {
    static {
        System.loadLibrary("TestPinnedDeadlock");
    }

    private static native void pin(int[] a);
    private static native void unpin(int[] a);

    public static void main(String[] args) {
        int[] cog = new int[10];
        pin(cog);
        System.gc();
        unpin(cog);
    }
}
