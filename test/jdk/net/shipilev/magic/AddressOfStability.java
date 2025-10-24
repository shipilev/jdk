/*
 * Copyright (c) 2020, Red Hat, Inc. All rights reserved.
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

/*
 * @test
 * @summary Test for Magic.addressOf with 32-bit compressed oops
 * @library /test/lib
 *
 * @run main/othervm -Xmx128m
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -Xint
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx128m
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:TieredStopAtLevel=1
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx128m
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:-TieredCompilation
 *                   AddressOfStability
 */

/*
 * @test
 * @summary Test for Magic.addressOf with zero-based compressed oops
 * @library /test/lib
 * @requires vm.bits == 64
 *
 * @run main/othervm -Xmx4g
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -Xint
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx4g
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:TieredStopAtLevel=1
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx4g
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:-TieredCompilation
 *                   AddressOfStability
 */

/*
 * @test
 * @summary Test for Magic.addressOf without compressed oops
 * @library /test/lib
 * @requires vm.bits == 64
 *
 * @run main/othervm -Xmx128m -XX:-UseCompressedOops
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -Xint
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx128m -XX:-UseCompressedOops
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:TieredStopAtLevel=1
 *                   AddressOfStability
 *
 * @run main/othervm -Xmx128m -XX:-UseCompressedOops
 *                   -XX:+UnlockDiagnosticVMOptions -XX:+AbortVMOnCompilationFailure -Xcheck:jni
 *                   -XX:CompileCommand=exclude,AddressOfStability::testInterpreted
 *                   -XX:-TieredCompilation
 *                   AddressOfStability
 */

import java.lang.reflect.Field;

import net.shipilev.Magic;

public class AddressOfStability {

    public static void main(String ... args) throws Exception {
        Object o = new Object();
        for (int c = 0; c < MagicUtil.ITERS; c++) {
            long addrInt1 = testInterpreted(o);
            long addrComp = testCompiled(o);
            long addrInt2 = testInterpreted(o);

            if (addrInt1 == addrInt2) {
                // No moves were detected, compare with compiled.
                MagicUtil.assertEquals(addrInt1, addrComp);
            }
        }
    }

    private static long testCompiled(Object o) {
        return Magic.addressOf(o);
    }

    private static long testInterpreted(Object o) {
        return Magic.addressOf(o);
    }

}
