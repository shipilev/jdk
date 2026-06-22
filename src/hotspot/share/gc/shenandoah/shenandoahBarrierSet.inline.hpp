/*
 * Copyright (c) 2015, 2022, Red Hat, Inc. All rights reserved.
 * Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
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

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP

#include "gc/shenandoah/shenandoahBarrierSet.hpp"

#include "gc/shared/accessBarrierSupport.inline.hpp"
#include "gc/shared/cardTable.hpp"
#include "gc/shenandoah/mode/shenandoahMode.hpp"
#include "gc/shenandoah/shenandoahAsserts.hpp"
#include "gc/shenandoah/shenandoahCardTable.hpp"
#include "gc/shenandoah/shenandoahCollectionSet.inline.hpp"
#include "gc/shenandoah/shenandoahForwarding.inline.hpp"
#include "gc/shenandoah/shenandoahGeneration.hpp"
#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahHeapRegion.hpp"
#include "gc/shenandoah/shenandoahMarkingContext.inline.hpp"
#include "gc/shenandoah/shenandoahThreadLocalData.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/oop.inline.hpp"



template <DecoratorSet decorators, typename T>
inline void ShenandoahBarrierSet::write_ref_field_post(T* field) {
  assert(ShenandoahCardBarrier, "Should have been checked by caller");
  if (_heap->is_in_young(field)) {
    // Young field stores do not require card mark.
    return;
  }
  T heap_oop = RawAccess<>::oop_load(field);
  if (CompressedOops::is_null(heap_oop)) {
    // Null reference store do not require card mark.
    return;
  }
  oop obj = CompressedOops::decode_not_null(heap_oop);
  if (!_heap->is_in_young(obj)) {
    // Not an old->young reference store.
    return;
  }
  volatile CardTable::CardValue* byte = card_table()->byte_for(field);
  *byte = CardTable::dirty_card_val();
}


template <typename T>
inline oop ShenandoahBarrierSet::oop_cmpxchg(DecoratorSet decorators, T* addr, oop compare_value, oop new_value) {
  shenandoah_assert_not_in_cset_except(nullptr, compare_value, (compare_value == nullptr || ShenandoahHeap::heap()->cancelled_gc()));
  shenandoah_assert_not_in_cset_except(nullptr, new_value, (new_value == nullptr || ShenandoahHeap::heap()->cancelled_gc()));

  // Handle the previous value through SATB, as we are about to perform the store.
  oop prev = RawAccess<>::oop_load(addr);
  satb_enqueue(prev);

  // Perform LRB on location to fix it up for this and all following accesses.
  // This guarantees there are no false negatives due to concurrent evacuation,
  // and the value loaded later by CAS is sanitized by some LRB, or is null.
  load_reference_barrier(decorators, prev, addr);

  return RawAccess<>::oop_atomic_cmpxchg(addr, compare_value, new_value);
}

template <typename T>
inline oop ShenandoahBarrierSet::oop_xchg(DecoratorSet decorators, T* addr, oop new_value) {
  shenandoah_assert_not_in_cset_except(nullptr, new_value, (new_value == nullptr || ShenandoahHeap::heap()->cancelled_gc()));

  // Handle the previous value through SATB, as we are about to perform the store.
  oop prev = RawAccess<>::oop_load(addr);
  satb_enqueue(prev);

  // Perform LRB on location to fix it up for this and all following accesses.
  // This is purely opportunistic: we would not have any false negatives here.
  // This guarantees the value loaded later by XCHG is sanitized by some LRB, or is null.
  load_reference_barrier(decorators, prev, addr);

  return RawAccess<>::oop_atomic_xchg(addr, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_not_in_heap(T* addr) {
  assert((decorators & ON_UNKNOWN_OOP_REF) == 0, "must be absent");
  ShenandoahBarrierSet* const bs = ShenandoahBarrierSet::barrier_set();
  return bs->oop_load(decorators, addr);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap(T* addr) {
  assert((decorators & ON_UNKNOWN_OOP_REF) == 0, "must be absent");
  ShenandoahBarrierSet* const bs = ShenandoahBarrierSet::barrier_set();
  return bs->oop_load(decorators, addr);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap_at(oop base, ptrdiff_t offset) {
  ShenandoahBarrierSet* const bs = ShenandoahBarrierSet::barrier_set();
  DecoratorSet resolved_decorators = AccessBarrierSupport::resolve_possibly_unknown_oop_ref_strength<decorators>(base, offset);
  return bs->oop_load(resolved_decorators, AccessInternal::oop_field_addr<decorators>(base, offset));
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_store_common(T* addr, oop value) {
  shenandoah_assert_marked_if(nullptr, value,
                              !CompressedOops::is_null(value) && ShenandoahHeap::heap()->is_evacuation_in_progress()
                              && !(ShenandoahHeap::heap()->active_generation()->is_young()
                                   && ShenandoahHeap::heap()->heap_region_containing(value)->is_old()));
  shenandoah_assert_not_in_cset_if(addr, value, value != nullptr && !ShenandoahHeap::heap()->cancelled_gc());
  ShenandoahBarrierSet* const bs = ShenandoahBarrierSet::barrier_set();
  bs->satb_barrier(decorators, addr);
  Raw::oop_store(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_store_not_in_heap(T* addr, oop value) {
  assert((decorators & ON_UNKNOWN_OOP_REF) == 0, "Reference strength must be known");
  oop_store_common(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_store_in_heap(T* addr, oop value) {
  shenandoah_assert_not_in_cset_loc_except(addr, ShenandoahHeap::heap()->cancelled_gc());
  shenandoah_assert_not_forwarded_except  (addr, value, value == nullptr || ShenandoahHeap::heap()->cancelled_gc() || !ShenandoahHeap::heap()->is_concurrent_mark_in_progress());

  oop_store_common(addr, value);
  if (ShenandoahCardBarrier) {
    ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
    bs->write_ref_field_post<decorators>(addr);
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
  oop_store_in_heap(AccessInternal::oop_field_addr<decorators>(base, offset), value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "CAS only with keep-alive");
  assert((decorators & ON_STRONG_OOP_REF) != 0, "CAS only for strong refs");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
  return bs->oop_cmpxchg(decorators, addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "CAS only with keep-alive");
  assert((decorators & ON_STRONG_OOP_REF) != 0, "CAS only for strong refs");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
  oop result = bs->oop_cmpxchg(decorators, addr, compare_value, new_value);
  if (ShenandoahCardBarrier) {
    bs->write_ref_field_post<decorators>(addr);
  }
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "CAS only with keep-alive");
  assert((decorators & (ON_STRONG_OOP_REF | ON_UNKNOWN_OOP_REF)) != 0, "CAS only for strong refs OR unknown refs (Unsafe)");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();

  // Unsafe.compareAndExchange/Set come here with ON_UNKNOWN_OOP_REF set.
  // These are normally strong refs, but one can use Unsafe on Reference.referent.
  // We cannot deal with that case. If application does Unsafe operations on
  // Reference.referent field, this likely breaks weak reference semantics already.
  // We upgrade the access to strong in (sometimes futile) attempt to maintain heap
  // integrity, and assert in debug builds for better diagnostics.
  DecoratorSet resolved_decorators = AccessBarrierSupport::resolve_possibly_unknown_oop_ref_strength<decorators>(base, offset);
  assert((resolved_decorators & ON_STRONG_OOP_REF) != 0, "Application error: CAS on weak location");
  resolved_decorators = (resolved_decorators & ~ON_DECORATOR_MASK) | ON_STRONG_OOP_REF;

  auto addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  oop result = bs->oop_cmpxchg(resolved_decorators, addr, compare_value, new_value);
  if (ShenandoahCardBarrier) {
    bs->write_ref_field_post<decorators>(addr);
  }
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "XCHG only with keep-alive");
  assert((decorators & ON_STRONG_OOP_REF) != 0, "XCHG only for strong refs");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
  return bs->oop_xchg(decorators, addr, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap(T* addr, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "XCHG only with keep-alive");
  assert((decorators & ON_STRONG_OOP_REF) != 0, "XCHG only for strong refs");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
  oop result = bs->oop_xchg(decorators, addr, new_value);
  if (ShenandoahCardBarrier) {
    bs->write_ref_field_post<decorators>(addr);
  }
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
  assert((decorators & AS_NO_KEEPALIVE) == 0, "XCHG only with keep-alive");
  assert((decorators & (ON_STRONG_OOP_REF | ON_UNKNOWN_OOP_REF)) != 0, "XCHG only for strong refs OR unknown refs (Unsafe)");
  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();

  // Unsafe.getAndSet comes here with ON_UNKNOWN_OOP_REF set.
  // These are normally strong refs, but one can use Unsafe on Reference.referent.
  // We cannot deal with that case. If application does Unsafe operations on
  // Reference.referent field, this likely breaks weak reference semantics already.
  // We upgrade the access to strong in (sometimes futile) attempt to maintain heap
  // integrity, and assert in debug builds for better diagnostics.
  DecoratorSet resolved_decorators = AccessBarrierSupport::resolve_possibly_unknown_oop_ref_strength<decorators>(base, offset);
  assert((resolved_decorators & ON_STRONG_OOP_REF) != 0, "Application error: XCHG on weak location");
  resolved_decorators = (resolved_decorators & ~ON_DECORATOR_MASK) | ON_STRONG_OOP_REF;

  auto addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  oop result = bs->oop_xchg(resolved_decorators, addr, new_value);
  if (ShenandoahCardBarrier) {
    bs->write_ref_field_post<decorators>(addr);
  }
  return result;
}


template <DecoratorSet decorators, typename BarrierSetT>
void ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::clone_in_heap(oop src, oop dst, size_t size) {
  // Hot code path, called from compiler/runtime. Make sure fast path is fast.

  // Fix up src before doing the copy, if needed.
  const char gc_state = ShenandoahThreadLocalData::gc_state(Thread::current());
  if (gc_state != 0 && ShenandoahCloneBarrier) {
    ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
    if ((gc_state & ShenandoahHeap::EVACUATION) != 0) {
      bs->clone_evacuation(src);
    } else if ((gc_state & ShenandoahHeap::UPDATE_REFS) != 0) {
      bs->clone_update(src);
    }
  }

  Raw::clone(src, dst, size);

  // Current allocator never allocates in old, so clone destination is guaranteed to be in young.
  // Otherwise we need card barriers.
  shenandoah_assert_in_young_if(nullptr, dst, ShenandoahCardBarrier);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
OopCopyResult ShenandoahBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                                                                                  arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                                                                                  size_t length) {
  T* src = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
  T* dst = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);

  ShenandoahBarrierSet* bs = ShenandoahBarrierSet::barrier_set();
  bs->arraycopy_barrier(src, dst, length);
  OopCopyResult result = Raw::oop_arraycopy_in_heap(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
  if (ShenandoahCardBarrier) {
    bs->write_ref_array((HeapWord*) dst, length);
  }
  return result;
}


#endif // SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP
