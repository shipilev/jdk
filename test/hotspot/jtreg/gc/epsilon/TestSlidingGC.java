/*
 * Copyright (c) 2017, 2018, Red Hat, Inc. All rights reserved.
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

package gc.epsilon;

/**
 * @test id=default
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=default-verify
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+EpsilonVerify
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=default-uncommit
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+EpsilonUncommit
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=default-uncommit-verify
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+EpsilonUncommit
 *      -XX:+EpsilonVerify
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=coh
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+UseCompactObjectHeaders
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=coh-verify
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+UseCompactObjectHeaders
 *      -XX:+EpsilonVerify
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=coh-uncommit
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+UseCompactObjectHeaders
 *      -XX:+EpsilonUncommit
 *      gc.epsilon.TestSlidingGC
 */

/**
 * @test id=coh-uncommit-verify
 * @key randomness
 * @requires vm.gc.Epsilon & !vm.graal.enabled
 * @summary Epsilon sliding GC works
 * @library /test/lib
 * @run main/othervm/timeout=960 -Xmx512m -XX:+UnlockExperimentalVMOptions -XX:+UseEpsilonGC -XX:+EpsilonSlidingGC
 *      -XX:+UseCompactObjectHeaders
 *      -XX:+EpsilonUncommit
 *      -XX:+EpsilonVerify
 *      gc.epsilon.TestSlidingGC
 */

import java.util.concurrent.*;
import java.util.Random;

import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;
import jdk.test.lib.Utils;

public class TestSlidingGC {

  static int SIZE  = Integer.getInteger("size", 10_000_000);     // ~160 MB retained
  static int COUNT = Integer.getInteger("count", 1_000_000_000); // ~240 GB allocation

  static Object[] sink;

  public static void main(String... args) {
    Random r = Utils.getRandomInstance();
    sink = new Object[SIZE];
    for (int c = 0; c < COUNT; c++) {
      sink[r.nextInt(SIZE)] = new Object();
    }
  }

}
