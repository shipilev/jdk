/*
 * Copyright (c) 2013, 2017, Oracle and/or its affiliates. All rights reserved.
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

import java.io.IOException;
import java.io.PrintStream;
import java.lang.management.ManagementFactory;

import java.nio.file.Files;
import java.nio.file.Paths;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.*;

public class CompileTheWorld {
    // in case when a static constructor changes System::out and System::err
    // we hold these values of output streams
    static PrintStream OUT = System.out;
    static final PrintStream ERR = System.err;
    /**
     * Entry point. Compiles classes in {@code targets}
     *
     * @param targets each element can be either
     *                'modules:&lt;comma separated list of modules to compile&gt'
     *                 or path to jimage file
     *                 or path to jar/zip, dir contains classes
     *                 or path to .lst file contains list of classes to compile
     */
    public static void main(String[] targets) {
        if (targets.length == 0) {
            throw new IllegalArgumentException("Expect a compile target.");
        }
        String logfile = Utils.LOG_FILE;
        PrintStream os = null;
        if (logfile != null) {
            try {
                os = new PrintStream(Files.newOutputStream(Paths.get(logfile)), true);
            } catch (IOException io) {
            }
        }
        if (os != null) {
            OUT = os;
        }

        boolean passed = false;

        try {
            try {
                if (ManagementFactory.getCompilationMXBean() == null) {
                    throw new RuntimeException(
                            "CTW can not work in interpreted mode");
                }
            } catch (java.lang.NoClassDefFoundError e) {
                // compact1, compact2 support
            }

            long start = System.currentTimeMillis();
                Arrays.stream(targets)
                      .map(PathHandler::create)
                      .flatMap(List::stream)
                      .forEach(p -> {
                          try {
                              p.process();
                          } finally {
                              p.close();
                          }
                        });
            CompileTheWorld.OUT.println(String.format("Done (%d classes, %d methods, %d ms)",
                    PathHandler.getProcessedClassCount(),
                    Compiler.getMethodCount(),
                    System.currentTimeMillis() - start));
            passed = true;
        } catch (Throwable t){
            t.printStackTrace(ERR);
        } finally {
            if (OUT != System.out) {
                try {
                    OUT.close();
                } catch (Throwable ignore) {
                }
            }
            // <clinit> might have started new threads
            System.exit(passed ? 0 : 1);
        }
    }

}

