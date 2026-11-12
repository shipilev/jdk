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


#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.inline.hpp"
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

ShenandoahTaskQueueChunk* ShenandoahTaskChunkStack::pop_or_create() {
  auto chunk = pop();
  if (chunk == nullptr) {
    chunk = new ShenandoahTaskQueueChunk();
  }
  return chunk;
}

ShenandoahTaskQueueChunk* ShenandoahTaskChunkStack::pop() {
  uint64_t old = AtomicAccess::load_acquire(&_head);
  while (true) {
    ShenandoahTaskQueueChunk* c = decode(old);
    if (c == nullptr) {
      return nullptr;
    }
    uint64_t next = encode(old, c->next(), true);
    uint64_t prev = AtomicAccess::cmpxchg(&_head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return c;
    }
    old = prev;
  }
}

void ShenandoahTaskChunkStack::push(ShenandoahTaskQueueChunk* chunk) {
  uint64_t old = AtomicAccess::load_acquire(&_head);
  while (true) {
    chunk->set_next(decode(old));
    uint64_t next = encode(old, chunk, false);
    uint64_t prev = AtomicAccess::cmpxchg(&_head, old, next, memory_order_seq_cst);
    if (prev == old) {
      return;
    }
    old = prev;
  }
}

bool ShenandoahTaskChunkStack::is_empty() {
  return decode(AtomicAccess::load(&_head)) == nullptr;
}

void ShenandoahTaskChunkStack::clear() {
  ShenandoahTaskQueueChunk* chunk;
  while ((chunk = pop()) != nullptr) {
    delete chunk;
  }
}

ShenandoahTaskQueue::ShenandoahTaskQueue(ShenandoahTaskChunkStack* free_list, ShenandoahTaskChunkStack* shared_list) :
  _free_list(free_list), _shared_list(shared_list) {
  _chunk = _free_list->pop_or_create();
}

ShenandoahTaskQueue::~ShenandoahTaskQueue() {
  _chunk->clear();
  _free_list->push(_chunk);
}

bool ShenandoahTaskQueue::is_empty() {
  return _chunk->is_empty();
}

void ShenandoahTaskQueue::clear() {
  _chunk->clear();
}

void ShenandoahTaskQueue::flush_to_shared() {
  if (!_chunk->is_empty()) {
    _shared_list->push(_chunk);
    _chunk = _free_list->pop_or_create();
  }
}

ShenandoahTaskQueueSet::ShenandoahTaskQueueSet(uint size) : _size(size) {
  _free_list = new ShenandoahTaskChunkStack();
  _shared_list = new ShenandoahTaskChunkStack();
  _queues = NEW_C_HEAP_ARRAY(ShenandoahTaskQueue*, size, mtGC);
  for (uint i = 0; i < size; ++i) {
    _queues[i] = new ShenandoahTaskQueue(_free_list, _shared_list);
  }
}

ShenandoahTaskQueueSet::~ShenandoahTaskQueueSet() {
  for (int i = 0; i < _size; ++i) {
    delete _queues[i];
  }
  FREE_C_HEAP_ARRAY(_queues);
  _shared_list->clear();
  delete _shared_list;
  _free_list->clear();
  delete _free_list;
}

bool ShenandoahTaskQueueSet::is_empty() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    if (!q->is_empty()) {
      return false;
    }
  }
  return _shared_list->is_empty();
}

void ShenandoahTaskQueueSet::clear() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    q->clear();
  }

  _shared_list->clear();
  _free_list->clear();
}

bool ShenandoahTaskQueueSet::has_shared() {
  return !_shared_list->is_empty();
}

void ShenandoahTaskQueueSet::rebalance() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    q->flush_to_shared();
  }
}

bool ShenandoahTaskTerminator::offer_termination() {
  AtomicAccess::dec(&_active_workers, memory_order_relaxed);
  SpinYield spin;
  while (true) {
    if (AtomicAccess::load(&_active_workers) == 0) {
      // Consensus: everyone terminated
      return true;
    }
    if (_heap->cancelled_gc()) {
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
