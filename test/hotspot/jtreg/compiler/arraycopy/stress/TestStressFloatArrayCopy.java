/*
 * Copyright (c) 2021, Red Hat, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/**
 * @test id=avx3
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=3 TestStressFloatArrayCopy
 */

/**
 * @test id=avx2
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=2 TestStressFloatArrayCopy
 */

/**
 * @test id=avx1
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=1 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=0 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse3
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=0 -XX:UseSSE=3 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse2
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=0 -XX:UseSSE=2 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse1
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=0 -XX:UseSSE=1 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse0
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:UseAVX=0 -XX:UseSSE=0 TestStressFloatArrayCopy
 */

/**
 * @test id=avx3-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=3 TestStressFloatArrayCopy
 */

/**
 * @test id=avx2-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=2 TestStressFloatArrayCopy
 */

/**
 * @test id=avx1-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=1 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=0 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse3-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=0 -XX:UseSSE=3 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse2-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=0 -XX:UseSSE=2 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse1-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=0 -XX:UseSSE=1 TestStressFloatArrayCopy
 */

/**
 * @test id=avx0-sse0-no-unaligned
 * @key stress randomness
 * @library /test/lib
 * @run main/othervm -XX:-UseUnalignedLoadStores -XX:UseAVX=0 -XX:UseSSE=0 TestStressFloatArrayCopy
 */

import java.util.Random;
import jdk.test.lib.Utils;

public class TestStressFloatArrayCopy extends AbstractStressArrayCopy {

    private static final float[] orig = new float[MAX_SIZE];
    private static final float[] test = new float[MAX_SIZE];

    protected void testWith(int size, int l, int r, int len) {
        // Seed the test from the original
        System.arraycopy(orig, 0, test, 0, size);

        // Check the seed is correct
        for (int c = 0; c < size; c++) {
            if (test[c] != orig[c]) {
                throwSeedError(size, c);
            }
        }

        // Perform the tested copy
        System.arraycopy(test, l, test, r, len);

        // Check the copy has proper contents
        for (int c = 0; c < len; c++) {
            if (test[r+c] != orig[l+c]) {
                throwError(l, r, len, r+c);
            }
        }

        // Check anything else was not affected: head and tail
        for (int c = 0; c < r; c++) {
            if (test[c] != orig[c]) {
                throwError(l, r, len, c);
            }
        }
        for (int c = r + len; c < size; c++) {
            if (test[c] != orig[c]) {
                throwError(l, r, len, c);
            }
        }
    }

    public static void main(String... args) {
        Random rand = Utils.getRandomInstance();
        for (int c = 0; c < orig.length; c++) {
            orig[c] = rand.nextFloat();
        }
        new TestStressFloatArrayCopy().run(rand);
    }

}
