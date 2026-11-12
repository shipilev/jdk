/*
 * Copyright (c) 2016, 2021, Red Hat, Inc. All rights reserved.
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


#include "gc/shenandoah/shenandoahHeap.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.inline.hpp"
#include "memory/memoryReserver.hpp"
#include "utilities/spinYield.hpp"

bool ShenandoahTaskQueueChunk::is_empty() {
  return _index == 0;
}

void ShenandoahTaskQueueChunk::clear() {
  _index = 0;
}

size_t ShenandoahTaskQueueChunk::size() {
  return _index;
}


ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop(uint64_t* head) {
  uint64_t old = AtomicAccess::load_acquire(head);
  while (true) {
    uint32_t idx = decode_idx(old);
    if (idx == ShenandoahTaskQueueChunkPool::NULL_IDX) {
      return nullptr;
    }
    ShenandoahTaskQueueChunk* chunk = _pool->chunk_at(idx);
    uint64_t next = encode(old, chunk->next_idx(), true);
    uint64_t prev = AtomicAccess::cmpxchg(head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return chunk;
    }
    old = prev;
  }
}

void ShenandoahTaskQueueSet::push(uint64_t* head, ShenandoahTaskQueueChunk* chunk) {
  uint64_t old = AtomicAccess::load_acquire(head);
  while (true) {
    uint32_t idx = _pool->index_of(chunk);
    chunk->set_next_idx(decode_idx(old));
    uint64_t next = encode(old, idx, false);
    uint64_t prev = AtomicAccess::cmpxchg(head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return;
    }
    old = prev;
  }
}

bool ShenandoahTaskQueueSet::is_shared_empty() {
  return decode_idx(AtomicAccess::load(&_shared_list_head)) == ShenandoahTaskQueueChunkPool::NULL_IDX;
}

ShenandoahTaskQueue::ShenandoahTaskQueue(ShenandoahTaskQueueSet* qs) :
  _qs(qs) {
  _chunk = _qs->pop_free();
}

ShenandoahTaskQueue::~ShenandoahTaskQueue() {
  _chunk->clear();
  _qs->push_free(_chunk);
}

bool ShenandoahTaskQueue::is_empty() {
  return _chunk->is_empty();
}

void ShenandoahTaskQueue::clear() {
  _chunk->clear();
}

void ShenandoahTaskQueue::flush_to_shared() {
  if (!_chunk->is_empty()) {
    _qs->push_shared(_chunk);
    _chunk = _qs->pop_free();
  }
}

ShenandoahTaskQueueSet::ShenandoahTaskQueueSet(uint size) : _size(size), _shared_list_head(0), _free_list_head(0) {
  _pool = new ShenandoahTaskQueueChunkPool();
  _queues = NEW_C_HEAP_ARRAY(ShenandoahTaskQueue*, size, mtGC);
  for (uint i = 0; i < size; ++i) {
    _queues[i] = new ShenandoahTaskQueue(this);
  }
}

ShenandoahTaskQueueSet::~ShenandoahTaskQueueSet() {
  for (int i = 0; i < _size; ++i) {
    delete _queues[i];
  }
  FREE_C_HEAP_ARRAY(_queues);
  delete _pool;
}

bool ShenandoahTaskQueueSet::is_empty() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    if (!q->is_empty()) {
      return false;
    }
  }
  return is_shared_empty();
}

void ShenandoahTaskQueueSet::clear() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    q->clear();
  }

  ShenandoahTaskQueueChunk* chunk;
  while ((chunk = pop_shared()) != nullptr) {
    chunk->clear();
    push_free(chunk);
  }
}

bool ShenandoahTaskQueueSet::has_shared() {
  return !is_shared_empty();
}

void ShenandoahTaskQueueSet::rebalance() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    q->flush_to_shared();
  }
}

bool ShenandoahTaskTerminator::offer_termination(bool cancellable) {
  AtomicAccess::dec(&_active_workers, memory_order_relaxed);
  SpinYield spin;
  while (true) {
    if (AtomicAccess::load(&_active_workers) == 0) {
      // Consensus: everyone terminated
      return true;
    }
    if (cancellable && _heap->cancelled_gc()) {
      // Requested termination
      return true;
    }
    if (_queues->has_shared()) {
      // New work appeared, no termination
      AtomicAccess::inc(&_active_workers, memory_order_relaxed);
      return false;
    }
    spin.wait();
  }
}

ShenandoahTaskQueueChunkPool::ShenandoahTaskQueueChunkPool() :
  _expand_lock(Mutex::stackwatermark - 1, "ShenandoahTaskQueueChunkPool_lock", true),
  _next_idx(1), _committed(0) {

  // Size up for the absolute worst case: the smallest objects (8 bytes), each containing a pointer.
  // TODO: 2-level indexing would allow us to reserve as we go.
  size_t size = align_up(MaxHeapSize / 8, os::vm_allocation_granularity());
  assert(size / CHUNK_STRIDE <= UINT32_MAX, "index must fit in 32 bits");

  _rs = MemoryReserver::reserve(size, os::vm_allocation_granularity(), os::vm_page_size(), mtGC);
  if (!_rs.is_reserved() || !_vs.initialize(_rs, 0)) {
    vm_exit_out_of_memory(size, OOM_MALLOC_ERROR, "Cannot reserve Shenandoah mark chunk pool");
  }
}

ShenandoahTaskQueueChunkPool::~ShenandoahTaskQueueChunkPool() {
  _vs.release();
  MemoryReserver::release(_rs);
}
