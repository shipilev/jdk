/* @test id=serial
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UseSerialGC TestPinnedDeadlock
 */

/* @test id=parallel
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UseParallelGC TestPinnedDeadlock
 */

/* @test id=g1
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UseG1GC TestPinnedDeadlock
 */

/* @test id=shenandoah
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UseShenandoahGC TestPinnedDeadlock
 */

/* @test id=z
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UseZGC TestPinnedDeadlock
 */

/* @test id=epsilon
 * @library /test/lib
 * @run main/othervm/native/timeout=10 -Xmx128m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC TestPinnedDeadlock
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
