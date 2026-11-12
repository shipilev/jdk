/*
 * Copyright (c) 2016, 2019, Red Hat, Inc. All rights reserved.
 * Copyright (c) 2024, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHTASKQUEUE_INLINE_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHTASKQUEUE_INLINE_HPP

#include "gc/shenandoah/shenandoahTaskqueue.hpp"

void ShenandoahTaskQueue::push(ShenandoahMarkTask task) {
  if (!_chunk->push(task)) {
    _qs->push_shared(_chunk);
    _chunk = _qs->pop_free();
    bool succ = _chunk->push(task);
    assert(succ, "Must succeed");
  }
}

bool ShenandoahTaskQueue::pop(ShenandoahMarkTask& task) {
  if (_chunk->pop(task)) {
    return true;
  }

  auto chunk = _qs->pop_shared();
  if (chunk != nullptr) {
    _qs->push_free(_chunk);
    _chunk = chunk;
    return _chunk->pop(task);
  }

  return false;
}

static uint32_t decode_idx(uint64_t encoded) {
  return (uint32_t) encoded;
}
static uint64_t encode(uint64_t old, uint32_t idx, bool bump) {
  uint64_t ver = (old >> 32) + (bump ? 1 : 0);
  return (ver << 32) | idx;
}

inline bool ShenandoahTaskQueueSet::is_shared_empty() {
  return decode_idx(AtomicAccess::load(&_shared_list_head)) == NULL_IDX;
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop(uint64_t* head) {
  uint64_t old = AtomicAccess::load_acquire(head);
  while (true) {
    uint32_t idx = decode_idx(old);
    if (idx == NULL_IDX) {
      return nullptr;
    }
    ShenandoahTaskQueueChunk* chunk = chunk_at(idx);
    uint64_t next = encode(old, chunk->next_idx(), true);
    uint64_t prev = AtomicAccess::cmpxchg(head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return chunk;
    }
    old = prev;
  }
}

inline void ShenandoahTaskQueueSet::push(uint64_t* head, ShenandoahTaskQueueChunk* chunk) {
  uint64_t old = AtomicAccess::load_acquire(head);
  while (true) {
    uint32_t idx = index_of(chunk);
    chunk->set_next_idx(decode_idx(old));
    uint64_t next = encode(old, idx, false);
    uint64_t prev = AtomicAccess::cmpxchg(head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return;
    }
    old = prev;
  }
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop_shared() {
  return pop(&_shared_list_head);
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop_free() {
  auto chunk = pop(&_free_list_head);
  if (chunk == nullptr) {
    chunk = allocate_chunk();
  }
  return chunk;
}

inline void ShenandoahTaskQueueSet::push_shared(ShenandoahTaskQueueChunk* chunk) {
  push(&_shared_list_head, chunk);
}

inline void ShenandoahTaskQueueSet::push_free(ShenandoahTaskQueueChunk* chunk) {
  push(&_free_list_head, chunk);
}

bool ShenandoahTaskQueueChunk::push(ShenandoahMarkTask t) {
  if (_top < MAX_TASK_CHUNK_SIZE) {
    _tasks[_top++] = t;
    return true;
  }
  return false;
}

bool ShenandoahTaskQueueChunk::pop(ShenandoahMarkTask& t) {
  if (_top > 0) {
    t = _tasks[--_top];
    return true;
  }
  return false;
}

inline uint32_t ShenandoahTaskQueueChunk::next_idx() {
  return AtomicAccess::load(&_next_chunk_idx);
}

inline void ShenandoahTaskQueueChunk::set_next_idx(uint32_t next) {
  AtomicAccess::store(&_next_chunk_idx, next);
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::chunk_at(uint32_t idx) {
  if (idx == NULL_IDX) {
    return nullptr;
  }
  return reinterpret_cast<ShenandoahTaskQueueChunk*>(_vs_chunks.low() + idx * CHUNK_SIZE);
}

inline uint32_t ShenandoahTaskQueueSet::index_of(ShenandoahTaskQueueChunk* chunk) {
  if (chunk == nullptr) {
    return NULL_IDX;
  }
  intptr_t raw_chunk = (intptr_t)chunk;
  intptr_t raw_base = (intptr_t)_vs_chunks.low();
  assert(raw_chunk > raw_base, "Should be");
  size_t off = raw_chunk - raw_base;
  assert(is_aligned(off, CHUNK_SIZE), "Sanity: alignment");
  return checked_cast<uint32_t>(off / CHUNK_SIZE);
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::allocate_chunk() {
  MutexLocker ml(&_expand_lock, Mutex::_no_safepoint_check_flag);
  if (_chunks_allocated >= _chunks_committed) {
    size_t expand = COMMIT_CHUNKS * CHUNK_SIZE;
    if (!_vs_chunks.expand_by(expand)) {
      vm_exit_out_of_memory(expand, OOM_MMAP_ERROR, "Shenandoah: ShenandoahTaskQueueSet pool exhausted");
    }
    _chunks_committed += COMMIT_CHUNKS;
  }
  auto chunk = new (chunk_at(_chunks_allocated)) ShenandoahTaskQueueChunk();
  _chunks_allocated++;
  return chunk;
}

#endif // SHARE_GC_SHENANDOAH_SHENANDOAHTASKQUEUE_INLINE_HPP
