/*
 * Copyright (c) 2016, 2019, Red Hat, Inc. All rights reserved.
 * Copyright (c) 2024, 2026, Oracle and/or its affiliates. All rights reserved.
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

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop_shared() {
  return pop(&_shared_list_head);
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueSet::pop_free() {
  auto chunk = pop(&_free_list_head);
  if (chunk == nullptr) {
    chunk = _pool->allocate();
  }
  return chunk;
}

inline void ShenandoahTaskQueueSet::push_shared(ShenandoahTaskQueueChunk *chunk) {
  push(&_shared_list_head, chunk);
}

inline void ShenandoahTaskQueueSet::push_free(ShenandoahTaskQueueChunk *chunk) {
  push(&_free_list_head, chunk);
}

bool ShenandoahTaskQueueChunk::push(ShenandoahMarkTask t) {
  if (_index < MAX_TASK_CHUNK_SIZE) {
    _tasks[_index++] = t;
    return true;
  }
  return false;
}

bool ShenandoahTaskQueueChunk::pop(ShenandoahMarkTask& t) {
  if (_index > 0) {
    t = _tasks[--_index];
    return true;
  }
  return false;
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueChunkPool::chunk_at(uint32_t idx) {
  if (idx == NULL_IDX) {
    return nullptr;
  }
  return reinterpret_cast<ShenandoahTaskQueueChunk*>(_vs.low() + idx * CHUNK_STRIDE);
}

inline uint32_t ShenandoahTaskQueueChunkPool::index_of(ShenandoahTaskQueueChunk* chunk) {
  if (chunk == nullptr) {
    return NULL_IDX;
  }
  intptr_t raw_chunk = (intptr_t)chunk;
  intptr_t raw_base = (intptr_t)_vs.low();
  assert(raw_chunk > raw_base, "Should be");
  size_t off = raw_chunk - raw_base;
  assert(is_aligned(off, CHUNK_STRIDE), "Sanity: alignment");
  return checked_cast<uint32_t>(off / CHUNK_STRIDE);
}

inline ShenandoahTaskQueueChunk* ShenandoahTaskQueueChunkPool::allocate() {
  MutexLocker ml(&_expand_lock, Mutex::_no_safepoint_check_flag);
  uint32_t idx = _next_idx;
  if (_next_idx >= _committed) {
    size_t expand = COMMIT_CHUNKS * CHUNK_STRIDE;
    if (!_vs.expand_by(expand)) {
      vm_exit_out_of_memory(expand, OOM_MMAP_ERROR, "Shenandoah: Task chunk pool exhausted");
    }
    _committed = _vs.committed_size() / CHUNK_STRIDE;
  }
  _next_idx = idx + 1;
  return new (chunk_at(idx)) ShenandoahTaskQueueChunk();
}

#endif // SHARE_GC_SHENANDOAH_SHENANDOAHTASKQUEUE_INLINE_HPP
