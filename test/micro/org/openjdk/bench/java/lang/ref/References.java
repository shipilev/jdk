//
// * Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 2 only, as
// published by the Free Software Foundation.
//
// This code is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// version 2 for more details (a copy is included in the LICENSE file that
// accompanied this code).
//
// You should have received a copy of the GNU General Public License version
// 2 along with this work; if not, write to the Free Software Foundation,
// Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
// or visit www.oracle.com if you need additional information or have any
// questions.
//
//
package org.openjdk.bench.java.lang.ref;

import java.lang.ref.*;
import java.util.concurrent.TimeUnit;
import org.openjdk.jmh.annotations.*;
import org.openjdk.jmh.infra.Blackhole;

@OutputTimeUnit(TimeUnit.NANOSECONDS)
@State(Scope.Thread)
@BenchmarkMode(Mode.AverageTime)
@Warmup(iterations = 5, time = 1, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 5, time = 1, timeUnit = TimeUnit.SECONDS)
@Fork(1)
public class References {

    final Reference<Object> soft = new SoftReference<>(new Object());
    final Reference<Object> weak = new WeakReference<>(new Object());
    final Reference<Object> phantom = new PhantomReference<>(new Object(), null);

    @Benchmark
    public void soft_clear() {
       soft.clear();
    }

    @Benchmark
    public void soft_create_clear(Blackhole bh) {
       Object ref = new Object();
       bh.consume(ref);
       Reference<Object> soft = new SoftReference<>(ref);
       soft.clear();
    }

    @Benchmark
    public void weak_clear() {
       weak.clear();
    }

    @Benchmark
    public void weak_create_clear(Blackhole bh) {
       Object ref = new Object();
       bh.consume(ref);
       Reference<Object> weak = new WeakReference<>(ref);
       weak.clear();
    }

    @Benchmark
    public void phantom_clear() {
       phantom.clear();
    }

    @Benchmark
    public void phantom_create_clear(Blackhole bh) {
       Object ref = new Object();
       bh.consume(ref);
       Reference<Object> phantom = new PhantomReference<>(ref, null);
       phantom.clear();
    }

}
