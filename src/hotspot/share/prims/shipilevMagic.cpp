/*
 * Copyright (c) 2022, Red Hat, Inc. All rights reserved.
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
 *
 */

#include "jni.h"
#include "jvm.h"
#include "classfile/javaClasses.inline.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "oops/arrayOop.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oopsHierarchy.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/jniHandles.inline.hpp"
#include "runtime/os.hpp"
#include "runtime/orderAccess.hpp"
#include "utilities/macros.hpp"

#include OS_CPU_HEADER_INLINE(os)

/**
 * Implementation of the net.shipilev.Magic
 */

JVM_ENTRY(static jlong, NetShipilevMagic_timestamp(JNIEnv *env, jclass cls)) {
#if defined(X86) && !defined(ZERO)
  return os::rdtsc();
#else
  return -1;
#endif
} JVM_END

JVM_ENTRY(static jlong, NetShipilevMagic_timestamp_serial(JNIEnv *env, jclass cls)) {
#if defined(X86) && !defined(ZERO)
  // Rely on rdtscp intrinsic to make the right thing
  // serialization-wise. In these fallback/interpreter paths,
  // just bite the bullet and do the fence.
  OrderAccess::fence();
  return os::rdtsc();
#else
  return -1;
#endif
} JVM_END

JVM_ENTRY(jlong, NetShipilevMagic_sizeOf(JNIEnv *env, jclass cls, jobject obj))
  assert(obj != nullptr, "object must not be null");

  oop o = JNIHandles::resolve_non_null(obj);
  return o->size()*HeapWordSize;
JVM_END

JVM_ENTRY(jlong, NetShipilevMagic_addressOf(JNIEnv *env, jclass cls, jobject obj))
  assert(obj != nullptr, "object must not be null");

  oop o = JNIHandles::resolve_non_null(obj);
  return cast_from_oop<jlong>(o);
JVM_END

JVM_ENTRY(jlong, NetShipilevMagic_fieldOffsetOf(JNIEnv *env, jclass cls, jobject field))
  assert(field != nullptr, "field must not be null");

  oop f    = JNIHandles::resolve_non_null(field);
  oop m    = java_lang_reflect_Field::clazz(f);
  Klass* k = java_lang_Class::as_Klass(m);
  int slot = java_lang_reflect_Field::slot(f);

  return InstanceKlass::cast(k)->field_offset(slot);
JVM_END

JVM_ENTRY(jlong, NetShipilevMagic_fieldSizeOf(JNIEnv *env, jclass cls, jobject field))
  assert(field != nullptr, "field must not be null");

  oop f    = JNIHandles::resolve_non_null(field);
  oop m    = java_lang_reflect_Field::clazz(f);
  Klass* k = java_lang_Class::as_Klass(m);
  int slot = java_lang_reflect_Field::slot(f);

  Symbol* sig = InstanceKlass::cast(k)->field_signature(slot);
  switch (sig->char_at(0)) {
    case JVM_SIGNATURE_CLASS    :
    case JVM_SIGNATURE_ARRAY    : return type2aelembytes(T_OBJECT);
    case JVM_SIGNATURE_BYTE     : return type2aelembytes(T_BYTE);
    case JVM_SIGNATURE_CHAR     : return type2aelembytes(T_CHAR);
    case JVM_SIGNATURE_FLOAT    : return type2aelembytes(T_FLOAT);
    case JVM_SIGNATURE_DOUBLE   : return type2aelembytes(T_DOUBLE);
    case JVM_SIGNATURE_INT      : return type2aelembytes(T_INT);
    case JVM_SIGNATURE_LONG     : return type2aelembytes(T_LONG);
    case JVM_SIGNATURE_SHORT    : return type2aelembytes(T_SHORT);
    case JVM_SIGNATURE_BOOLEAN  : return type2aelembytes(T_BOOLEAN);
  }

  ShouldNotReachHere();
  return 0;
JVM_END

class GetReferencedObjectsClosure : public BasicOopIterateClosure {
private:
  objArrayOopDesc* const _result;
  int _count;
public:
  GetReferencedObjectsClosure(objArrayOopDesc* result) : _result(result), _count(0) {}

  template <typename T> void do_oop_nv(T* p) {
    oop o = HeapAccess<>::oop_load(p);
    if (!CompressedOops::is_null(o)) {
      _result->obj_at_put(_count++, o);
    }
  }

  int count() { return _count; }

  virtual void do_oop(oop* p)       { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
};

JVM_ENTRY(jint, NetShipilevMagic_getReferencedObjects(JNIEnv *env, jclass cls, jobject obj_ref, jobjectArray ref_buf_ref))
  oop obj = JNIHandles::resolve_non_null(obj_ref);
  objArrayOop ref_buf = objArrayOop(JNIHandles::resolve_non_null(ref_buf_ref));

  assert(Universe::heap()->is_in(obj), "object should be in heap: " PTR_FORMAT, p2i(obj));
  assert(Universe::heap()->is_in(ref_buf), "ref buf should be in heap: " PTR_FORMAT, p2i(ref_buf));

  InstanceKlass* k = InstanceKlass::cast(obj->klass());

  int count = 0;
  {
    InstanceKlass* ik = k;
    while (ik != nullptr) {
      count += ik->nonstatic_oop_field_count();
      ik = ik->super();
    }
  }

  if (count == 0) {
    return 0;
  }

  if (count > ref_buf->length()) {
    return -1;
  }

  GetReferencedObjectsClosure cl(ref_buf);

#ifdef _LP64
  if (UseCompressedOops) {
    k->oop_oop_iterate<narrowOop>(obj, &cl);
  } else
#endif
  {
    k->oop_oop_iterate<oop>(obj, &cl);
  }

  return cl.count();
JVM_END

/// JVM_RegisterUnsafeMethods

#define CC (char*)  /*cast a literal from (const char*)*/
#define FN_PTR(f) CAST_FROM_FN_PTR(void*, &f)

#define LANG "Ljava/lang/"

#define OBJ LANG "Object;"
#define CLS LANG "Class;"
#define FLD LANG "reflect/Field;"

static JNINativeMethod net_shipilev_Magic_methods[] = {
  {CC "timestamp",            CC "()J",                 FN_PTR(NetShipilevMagic_timestamp)},
  {CC "timestampSerial",      CC "()J",                 FN_PTR(NetShipilevMagic_timestamp_serial)},
  {CC "sizeOf0",              CC "(" OBJ ")J",          FN_PTR(NetShipilevMagic_sizeOf)},
  {CC "addressOf0",           CC "(" OBJ ")J",          FN_PTR(NetShipilevMagic_addressOf)},
  {CC "getReferencedObjects", CC "(" OBJ "[" OBJ ")I",  FN_PTR(NetShipilevMagic_getReferencedObjects)},
  {CC "fieldOffsetOf0",       CC "(" FLD ")J",          FN_PTR(NetShipilevMagic_fieldOffsetOf)},
  {CC "fieldSizeOf0",         CC "(" FLD ")J",          FN_PTR(NetShipilevMagic_fieldSizeOf)},
};

#undef LANG
#undef OBJ
#undef CLS
#undef FLD

#undef CC
#undef FN_PTR

JVM_ENTRY(void, JVM_RegisterNetShipilevMagicMethods(JNIEnv *env, jclass cls)) {
  ThreadToNativeFromVM ttnfv(thread);

  int ok = env->RegisterNatives(cls, net_shipilev_Magic_methods, sizeof(net_shipilev_Magic_methods)/sizeof(JNINativeMethod));
  guarantee(ok == 0, "register net.shipilev.Magic natives");
} JVM_END
