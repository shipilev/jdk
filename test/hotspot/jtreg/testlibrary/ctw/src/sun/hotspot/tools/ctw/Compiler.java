/*
 * Copyright (c) 2013, 2025, Oracle and/or its affiliates. All rights reserved.
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

package sun.hotspot.tools.ctw;

import jdk.internal.access.SharedSecrets;
import jdk.internal.misc.Unsafe;
import jdk.internal.reflect.ConstantPool;
import jdk.test.whitebox.WhiteBox;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinTask;
import java.util.concurrent.RecursiveAction;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Collectors;

/**
 * Provide method to compile whole class.
 * Also contains compiled methods and classes counters.
 */
public class Compiler {

    // Call GC after compiling as many methods. This would remove the stale methods.
    // This threshold should balance the GC overhead and the cost of keeping lots
    // of stale methods around.
    private static final long GC_METHOD_THRESHOLD = Long.getLong("gcMethodThreshold", 500);

    private static final Unsafe UNSAFE = Unsafe.getUnsafe();
    private static final WhiteBox WHITE_BOX = WhiteBox.getWhiteBox();
    private static final AtomicLong METHOD_COUNT = new AtomicLong();
    private static final AtomicLong METHODS_SINCE_LAST_GC = new AtomicLong();

    private Compiler() { }

    /**
     * @return count of processed methods
     */
    public static long getMethodCount() {
        return METHOD_COUNT.get();
    }

    /**
     * Compiles all methods and constructors.
     *
     * @param aClass class to compile
     * @param id an id of the class
     * @throws NullPointerException if {@code class}
     */
    public static void compileClass(Class<?> aClass, long id) {
        Objects.requireNonNull(aClass);

        String className = aClass.getName();

        // Initialize all constant pool entries, if requested.
        if (Utils.COMPILE_THE_WORLD_PRELOAD_CLASSES) {
            ConstantPool constantPool = SharedSecrets.getJavaLangAccess().getConstantPool(aClass);
            preloadClasses(className, id, constantPool);
        }

        // Attempt to initialize the class. Avoid class init deadlocks: only one class init at a time.
        // If initialization is not possible due to NCDFE, accept this, and try compile anyway.
        try {
            synchronized (Compiler.class) {
                UNSAFE.ensureClassInitialized(aClass);
            }
        } catch (NoClassDefFoundError e) {
            CompileTheWorld.OUT.printf("[%d]\t%s\tNOTE unable to init class : %s%n",
                id, className, e);
        }

        // Getting constructor/methods with unresolvable signatures would fail with NCDFE.
        // Try to get as much as possible, and compile everything else.
        // TODO: Would be good to have a Whitebox method that returns the subset of resolvable
        // constructors/methods without throwing NCDFE. This would extend the testing scope.
        Executable[] constructors = new Executable[0];
        Executable[] methods = new Executable[0];

        try {
            constructors = aClass.getDeclaredConstructors();
        } catch (NoClassDefFoundError e) {
            CompileTheWorld.OUT.printf("[%d]\t%s\tNOTE unable to get constructors : %s%n",
                id, className, e);
        }

        try {
            methods = aClass.getDeclaredMethods();
        } catch (NoClassDefFoundError e) {
            CompileTheWorld.OUT.printf("[%d]\t%s\tNOTE unable to get methods : %s%n",
                id, className, e);
        }

        Executable[] all = new Executable[constructors.length + methods.length];
        System.arraycopy(constructors, 0, all, 0, constructors.length);
        System.arraycopy(methods, 0, all, constructors.length, methods.length);

        // Populate profile for all methods to expand the scope of
        // compiler optimizations. Do this before compilations start.
        for (Executable e : all) {
            WHITE_BOX.markMethodProfiled(e);
        }

        // Make sure methods is not compiled at any level before starting
        // progressive compilations. No deopt in-between tiers is needed,
        // as long as we increase the compilation levels one by one.
        WHITE_BOX.deoptimizeMethods(all);

        // Now schedule the compilations.
        List<RecursiveAction> tasks = new ArrayList<>();
        tasks.add(new CompileInitTask(aClass, id));
        for (Executable method : all) {
            tasks.add(new CompileTask(className, method));
        }
        ForkJoinTask.invokeAll(tasks);
        METHOD_COUNT.addAndGet(all.length);

        // Compilations are done. Ditch all the compiled versions of the code,
        // make the methods eligible for sweeping sooner.
        new DeoptimizeMethods(all).fork();
    }

    static class DeoptimizeMethods extends RecursiveAction {
        final Executable[] methods;

        public DeoptimizeMethods(Executable[] methods) {
            this.methods = methods;
        }

        @Override
        public void compute() {
            WHITE_BOX.deoptimizeMethods(methods);

            // See if we need to run GC
            METHODS_SINCE_LAST_GC.addAndGet(methods.length);
            while (true) {
                long current = METHODS_SINCE_LAST_GC.get();
                if (current < GC_METHOD_THRESHOLD) {
                    return;
                }
                if (METHODS_SINCE_LAST_GC.compareAndSet(current, 0)) {
                    System.out.println(current + " methods deopted, forcing GC to cleanup");
                    System.gc();
                    return;
                }
            }
        }
    }

    static class CompileTask extends RecursiveAction {
        final String className;
        final Executable method;

        public CompileTask(String className, Executable method) {
            this.className = className;
            this.method = method;
        }

        @Override
        public void compute() {
            int startLevel = Utils.INITIAL_COMP_LEVEL;
            int endLevel = Utils.TIERED_COMPILATION ? Utils.TIERED_STOP_AT_LEVEL : startLevel;
            for (int compLevel = startLevel; compLevel <= endLevel; ++compLevel) {
                if (!WHITE_BOX.isMethodCompilable(method, compLevel)) {
                    if (Utils.IS_VERBOSE) {
                        log(className, method, "not compilable at " + compLevel);
                    }
                    continue;
                }

                try {
                    WHITE_BOX.enqueueMethodForCompilation(method, compLevel);

                    if (!Utils.BACKGROUND_COMPILATION) {
                        for (int i = 0;
                             i < 10 && WHITE_BOX.isMethodQueuedForCompilation(method);
                             ++i) {
                           try {
                                Thread.sleep(1000);
                            } catch (InterruptedException e) {
                                Thread.currentThread().interrupt();
                            }
                        }
                    }

                    int tmp = WHITE_BOX.getMethodCompilationLevel(method);
                    if (tmp != compLevel) {
                        log(className, method, "WARNING compilation level = " + tmp + ", but not " + compLevel);
                    } else if (Utils.IS_VERBOSE) {
                        log(className, method, "compilation level = " + tmp + ". OK");
                    }
                } catch (Throwable t) {
                    log(className, method, "ERROR at level " + compLevel);
                    t.printStackTrace(CompileTheWorld.ERR);
                }
            }
        }
    }

    static class CompileInitTask extends RecursiveAction {
        final Class<?> klass;
        final long id;

        public CompileInitTask(Class<?> klass, long id) {
            this.klass = klass;
            this.id = id;
        }

        @Override
        public void compute() {
            int startLevel = Utils.INITIAL_COMP_LEVEL;
            int endLevel = Utils.TIERED_COMPILATION ? Utils.TIERED_STOP_AT_LEVEL : startLevel;
            for (int i = startLevel; i <= endLevel; ++i) {
                try {
                    WHITE_BOX.enqueueInitializerForCompilation(klass, i);
                } catch (Throwable t) {
                    CompileTheWorld.OUT.println(String.format("[%d]\t%s::<clinit>\tERROR at level %d : %s",
                        id, klass.getName(), i, t));
                    t.printStackTrace(CompileTheWorld.ERR);
                }
            }
        }
    }

    private static void preloadClasses(String className, long id,
            ConstantPool constantPool) {
        for (int i = 0, n = constantPool.getSize(); i < n; ++i) {
            try {
                if (constantPool.getTagAt(i) == ConstantPool.Tag.CLASS) {
                    constantPool.getClassAt(i);
                }
            } catch (NoClassDefFoundError e) {
                CompileTheWorld.OUT.printf("[%d]\t%s\tNOTE unable to preload : %s%n",
                    id, className, e);
            } catch (Throwable t) {
                CompileTheWorld.OUT.printf("[%d]\t%s\tWARNING preloading failed : %s%n",
                    id, className, t);
                t.printStackTrace(CompileTheWorld.ERR);
            }
        }
    }


        private static String methodName(String className, Executable method) {
            return String.format("%s::%s(%s)",
                    className,
                    method.getName(),
                    Arrays.stream(method.getParameterTypes())
                          .map(Class::getName)
                          .collect(Collectors.joining(", ")));
        }

        private static void log(String className, Executable method, String message) {
            StringBuilder builder = new StringBuilder("[")
                    .append(0) // TODO FIXME
                    .append("]\t")
                    .append(methodName(className, method));
            if (message != null) {
                builder.append('\t')
                       .append(message);
            }
            CompileTheWorld.ERR.println(builder);
        }


}
