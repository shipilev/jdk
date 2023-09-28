/*
 * Copyright (c) 2022, Red Hat, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

package net.shipilev;

import java.io.*;
import java.lang.reflect.Field;
import java.util.function.ToLongFunction;
import java.util.Arrays;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

import jdk.internal.vm.annotation.IntrinsicCandidate;
import jdk.internal.vm.annotation.DontInline;
import jdk.internal.vm.annotation.IntrinsicCandidate;

public class Magic {

    /*
     * ---------------------- INTERFACE --------------------------
     *         (If you are lazy to look into Javadoc)
     * -----------------------------------------------------------
     */

    /**
     * Returns the current CPU timestamp counter.
     * <p>
     * Maps to RDTSC on X86. No other platform is supported (yet).
     *
     * @return TSC value, or -1 if not supported
     */
    @IntrinsicCandidate
    public static native long timestamp();

    /**
     * Same as {@link #timestamp}, but also provides instruction
     * stream serialization. May be more expensive.
     * <p>
     * Maps to RDTSCP on X86. No other platform is supported (yet).
     *
     * @return TSC value, or -1 if not supported
     */
    @IntrinsicCandidate
    public static native long timestampSerial();

    /**
     * Returns the implementation-specific estimate of the amount of storage
     * consumed by the specified object.
     * <p>
     * The estimate may change during a single invocation of the JVM.
     *
     * @param obj object to estimate the size of
     * @return storage size in bytes
     * @throws NullPointerException if {@code obj} is {@code null}
     * @since 16
     */
    @DontInline // Semantics: make sure the object is not scalar replaced.
    public static long sizeOf(Object obj) {
        Objects.requireNonNull(obj);
        return sizeOf0(obj);
    }

    /**
     * Returns the implementation-specific estimate of the offset of the field
     * within the holding container.
     * <p>
     * For the instance fields, the offset is from the beginning of the holder
     * object. For the static fields, the offset is from the beginning of the
     * unspecified holder area. As such, these offsets are useful for comparing
     * the offsets of two fields, not for any kind of absolute addressing.
     * <p>
     * The estimate may change during a single invocation of the JVM, for example
     * during class redefinition.
     *
     * @param field field to poll
     * @return the field offset in bytes
     * @throws NullPointerException if {@code field} is {@code null}
     * @since 16
     */
    public static long fieldOffsetOf(Field field) {
        Objects.requireNonNull(field);
        return fieldOffsetOf0(field);
    }

    /**
     * Returns the implementation-specific estimate of the field slot size for
     * the specified object field.
     * <p>
     * The estimate may change during a single invocation of the JVM.
     *
     * TODO: Split by staticness?
     *
     * @param field field to poll
     * @return the field size in bytes
     * @throws NullPointerException if {@code field} is {@code null}
     * @since 16
     */
    public static long fieldSizeOf(Field field) {
        Objects.requireNonNull(field);
        return fieldSizeOf0(field);
    }

    /**
     * Returns the implementation-specific estimate of the amount of storage
     * consumed by the specified object and all objects referenced by it.
     * <p>
     * The estimate may change during a single invocation of the JVM. Notably,
     * the estimate is not guaranteed to remain stable if the object references in
     * the walked subgraph change when {@code deepSizeOf} is running.
     *
     * @param obj root object to start the estimate from
     * @return storage size in bytes
     * @throws NullPointerException if {@code obj} is {@code null}
     * @since 16
     */
    public static long deepSizeOf(Object obj) {
        return deepSizeOf0(obj, (o) -> DEEP_SIZE_OF_TRAVERSE | DEEP_SIZE_OF_SHALLOW);
    }


    /**
     * Returns the implementation-specific estimate of the amount of storage
     * consumed by the specified object and all objects referenced by it.
     * <p>
     * The estimate may change during a single invocation of the JVM. Notably,
     * the estimate is not guaranteed to remain stable if the object references in
     * the walked subgraph change when {@code deepSizeOf} is running.
     *
     * @param obj root object to start the estimate from
     * @param includeCheck callback to evaluate an object's size. The callback can
     * return a positive value as a bitmask - valid values are
     * {@link #DEEP_SIZE_OF_SHALLOW} to consider the object's shallow sise and
     * {@link #DEEP_SIZE_OF_TRAVERSE} to traverse ("go deeper") the object's
     * references. A negative value means that the absolute return value is
     * considered and the object's references are not considered.
     * @return storage size in bytes
     * @throws NullPointerException if {@code obj} is {@code null}
     * @since 16
     */
    public static long deepSizeOf(Object obj, ToLongFunction<Object> includeCheck) {
        return deepSizeOf0(obj, includeCheck);
    }

    /**
     * Returns the implementation-specific representation of the memory address
     * where the specified object resides.
     * <p>
     * The estimate may change during a single invocation of the JVM. Notably,
     * in the presence of moving garbage collector, the address can change at any
     * time, including during the call. As such, this method is only useful for
     * low-level debugging and heap introspection in a quiescent application.
     * <p>
     * The JVM is also free to provide non-verbatim answers, for example, adding
     * the random offset in order to hide the real object addresses. Because of this,
     * this method is useful to compare the relative Object positions, or inspecting
     * the object alignments up to some high threshold, but not for accessing the objects
     * via the naked native address.
     *
     * @param obj object to get the address of
     * @return current object address
     * @since 16
     */
    @DontInline // Semantics: make sure the object is not scalar replaced.
    public static long addressOf(Object obj) {
        Objects.requireNonNull(obj);
        return addressOf0(obj);
    }

    /*
     * ---------------------- IMPLEMENTATION --------------------------
     */

    private static native void registerNatives();
    static {
        registerNatives();
    }

    public Magic() {
        throw new IllegalArgumentException("NO INSTANCE MAGIC FOR YOU.");
    }

    /**
     * Bit value for {@link #deepSizeOf(Object, ToLongFunction)}'s callback
     * return value to continue traversal ("go deep") of the references of
     * the object passed to the callback.
     */
    public static final long DEEP_SIZE_OF_TRAVERSE = 1;
    /**
     * Bit value for {@link #deepSizeOf(Object, ToLongFunction)}'s callback
     * return value to consider the shallow size of the object passed to the
     * callback.
     */
    public static final long DEEP_SIZE_OF_SHALLOW = 2;

    private static long handleIncludeCheck(ArrayDeque<Object> q, Object o, ToLongFunction<Object> ic, long ts, long os) {
        long t = ic.applyAsLong(o);
        if (t > 0) {
            if ((t & DEEP_SIZE_OF_TRAVERSE) != 0) {
                q.push(o);
            }
            if ((t & DEEP_SIZE_OF_SHALLOW) != 0) {
                ts += os;
            }
        } else {
            ts -= t;
        }
        return ts;
    }

    @DontInline // Semantics: make sure the object is not scalar replaced.
    private static long deepSizeOf0(Object obj, ToLongFunction<Object> includeCheck) {
        Objects.requireNonNull(obj);

        IdentityHashSet visited = new IdentityHashSet(IdentityHashSet.MINIMUM_CAPACITY);
        ArrayDeque<Object> q = new ArrayDeque<>();

        visited.add(obj);

        long rootSize = sizeOf0(obj);
        long totalSize = handleIncludeCheck(q, obj, includeCheck, 0, rootSize);

        Object[] refBuf = new Object[1];

        while (!q.isEmpty()) {
            Object o = q.pop();
            Class<?> cl = o.getClass();
            if (cl.isArray()) {
                // Separate array path avoids adding a lot of (potentially large) array
                // contents on the queue. No need to handle primitive arrays too.

                if (cl.getComponentType().isPrimitive()) {
                    continue;
                }

                for (Object e : (Object[])o) {
                    if (e != null && visited.add(e)) {
                        long size = sizeOf0(e);
                        totalSize = handleIncludeCheck(q, e, includeCheck, totalSize, size);
                    }
                }
            } else {
                int objs;
                while ((objs = getReferencedObjects(o, refBuf)) < 0) {
                    refBuf = new Object[refBuf.length * 2];
                }

                for (int c = 0; c < objs; c++) {
                    Object e = refBuf[c];
                    if (visited.add(e)) {
                        long size = sizeOf0(e);
                        totalSize = handleIncludeCheck(q, e, includeCheck, totalSize, size);
                    }
                }

                // Null out the buffer: do not keep these objects referenced until next
                // buffer fill, and help the VM code to avoid full SATB barriers on existing
                // buffer elements in getReferencedObjects.
                Arrays.fill(refBuf, 0, objs, null);
            }
        }

        return totalSize;
    }

    /**
     * Peels the referenced objects from the given object and puts them
     * into the reference buffer. Never returns nulls in reference buffer.
     * Returns the number of valid elements in the buffer. If reference bufffer
     * is too small, returns -1.
     *
     * @param obj object to peel
     * @param refBuf reference buffer
     * @return number of valid elements in buffer, -1 if buffer is too small
     */
    @IntrinsicCandidate
    private static native int getReferencedObjects(Object obj, Object[] refBuf);

    private static final class IdentityHashSet {
        private static final int MINIMUM_CAPACITY = 4;
        private static final int MAXIMUM_CAPACITY = 1 << 29;

        private Object[] table;
        private int size;

        public IdentityHashSet(int expectedMaxSize) {
            table = new Object[capacity(expectedMaxSize)];
        }

        private static int capacity(int expectedMaxSize) {
            return
                (expectedMaxSize > MAXIMUM_CAPACITY / 3) ? MAXIMUM_CAPACITY :
                (expectedMaxSize <= 2 * MINIMUM_CAPACITY / 3) ? MINIMUM_CAPACITY :
                Integer.highestOneBit(expectedMaxSize + (expectedMaxSize << 1));
        }

        private static int nextIndex(int i, int len) {
            return (i + 1 < len ? i + 1 : 0);
        }

        public boolean add(Object o) {
            while (true) {
                final Object[] tab = table;
                final int len = tab.length;
                int i = System.identityHashCode(o) & (len - 1);

                for (Object item; (item = tab[i]) != null; i = nextIndex(i, len)) {
                    if (item == o) {
                        return false;
                    }
                }

                final int s = size + 1;
                if (s*3 > len && resize()) continue;

                tab[i] = o;
                size = s;
                return true;
            }
        }

        private boolean resize() {
            Object[] oldTable = table;
            int oldLength = oldTable.length;
            if (oldLength == MAXIMUM_CAPACITY) {
                throw new IllegalStateException("Capacity exhausted.");
            }

            int newLength = oldLength * 2;
            if (newLength <= oldLength) {
                return false;
            }

            Object[] newTable = new Object[newLength];
            for (Object o : oldTable) {
                if (o != null) {
                    int i = System.identityHashCode(o) & (newLength - 1);
                    while (newTable[i] != null) {
                        i = nextIndex(i, newLength);
                    }
                    newTable[i] = o;
                }
            }
            table = newTable;
            return true;
        }
    }

    @IntrinsicCandidate
    private static native long sizeOf0(Object obj);

    @IntrinsicCandidate
    private static native long addressOf0(Object obj);

    // Reflection-like call, is not supposed to be fast?
    private static native long fieldOffsetOf0(Field field);

    // Reflection-like call, is not supposed to be fast?
    private static native long fieldSizeOf0(Field field);

}
