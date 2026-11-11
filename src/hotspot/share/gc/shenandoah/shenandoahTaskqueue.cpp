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
#include "logging/log.hpp"
#include "logging/logStream.hpp"
#include "memory/resourceArea.hpp"
#include "utilities/spinYield.hpp"
#include "utilities/stack.inline.hpp"

ShenandoahTaskQueueChunk::ShenandoahTaskQueueChunk() : _next(nullptr), _index(0) {
  _tasks = NEW_C_HEAP_ARRAY(ShenandoahMarkTask, MAX_TASK_CHUNK_SIZE, mtGC);
}

ShenandoahTaskQueueChunk::~ShenandoahTaskQueueChunk() {
  FREE_C_HEAP_ARRAY(_tasks);
}

bool ShenandoahTaskQueueChunk::push(ShenandoahMarkTask t) {
  if (_index < MAX_TASK_CHUNK_SIZE) {
    _tasks[_index++] = t;
    return true;
  } else {
    return false;
  }
}

bool ShenandoahTaskQueueChunk::pop(ShenandoahMarkTask &t) {
  if (_index > 0) {
    t = _tasks[--_index];
    return true;
  } else {
    return false;
  }
}

bool ShenandoahTaskQueueChunk::is_empty() {
  return _index == 0;
}

void ShenandoahTaskQueueChunk::clear() {
  _index = 0;
}

size_t ShenandoahTaskQueueChunk::size() {
  return _index;
}

ShenandoahTaskQueueOverflow::ShenandoahTaskQueueOverflow() : _head(nullptr) {
}

ShenandoahTaskQueueChunk* ShenandoahTaskQueueOverflow::pop() {
  // FIXME: ABA here!
  while (true) {
    auto head = AtomicAccess::load(&_head);
    if (head == nullptr) {
      return nullptr;
    }
    auto old = AtomicAccess::cmpxchg(&_head, head, head->next(), memory_order_seq_cst);
    if (old == head) {
      return head;
    }
  }
}

void ShenandoahTaskQueueOverflow::push(ShenandoahTaskQueueChunk *chunk) {
  while (true) {
    auto head = AtomicAccess::load(&_head);
    chunk->set_next(head);
    auto old = AtomicAccess::cmpxchg(&_head, head, chunk, memory_order_seq_cst);
    if (old == head) {
      return;
    }
  }
}

bool ShenandoahTaskQueueOverflow::is_empty() {
  return AtomicAccess::load(&_head) == nullptr;
}

void ShenandoahTaskQueueOverflow::clear() {
  while (true) {
    auto chunk = pop();
    if (chunk != nullptr) {
      delete chunk;
    } else {
      break;
    }
  }
}

ShenandoahTaskQueue::~ShenandoahTaskQueue() {
  delete _chunk;
}

void ShenandoahTaskQueue::push(ShenandoahMarkTask task) {
  if (!_chunk->push(task)) {
    _overflow->push(_chunk);
    _chunk = new ShenandoahTaskQueueChunk();
    bool succ = _chunk->push(task);
    assert(succ, "Must succeed");
  }
}

bool ShenandoahTaskQueue::pop(ShenandoahMarkTask& task) {
  if (_chunk->pop(task)) {
    return true;
  }

  auto chunk = _overflow->pop();
  if (chunk != nullptr) {
    delete _chunk;
    _chunk = chunk;
    return _chunk->pop(task);
  }

  return false;
}

bool ShenandoahTaskQueue::is_empty() {
  return _chunk->is_empty();
}

void ShenandoahTaskQueue::clear() {
  _chunk->clear();
}

ShenandoahTaskQueueChunk * ShenandoahTaskQueue::disown_chunk() {
  if (_chunk->is_empty()) {
    return nullptr;
  }
  auto chunk = _chunk;
  _chunk = new ShenandoahTaskQueueChunk();
  return chunk;
}

ShenandoahTaskQueueSet::ShenandoahTaskQueueSet(uint size) : _size(size) {
  _overflow = new ShenandoahTaskQueueOverflow();
  _queues = NEW_C_HEAP_ARRAY(ShenandoahTaskQueue*, size, mtGC);
  for (uint i = 0; i < size; ++i) {
    _queues[i] = new ShenandoahTaskQueue(_overflow);
  }
}

ShenandoahTaskQueueSet::~ShenandoahTaskQueueSet() {
  for (int i = 0; i < _size; ++i) {
    delete _queues[i];
  }
  FREE_C_HEAP_ARRAY(_queues);
  _overflow->clear();
  delete _overflow;
}

ShenandoahTaskQueue * ShenandoahTaskQueueSet::queue(uint i) {
  return _queues[i];
}

bool ShenandoahTaskQueueSet::is_empty() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    if (!q->is_empty()) {
      return false;
    }
  }
  return _overflow->is_empty();
}

void ShenandoahTaskQueueSet::clear() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    q->clear();
  }

  _overflow->clear();
}

bool ShenandoahTaskQueueSet::has_overflow() {
  return !_overflow->is_empty();
}

void ShenandoahTaskQueueSet::rebalance() {
  for (int i = 0; i < _size; i++) {
    ShenandoahTaskQueue* q = queue(i);
    assert(q != nullptr, "Sanity");
    auto chunk = q->disown_chunk();
    if (chunk != nullptr) {
      _overflow->push(chunk);
    }
  }
}

bool ShenandoahTaskTerminator::offer_termination() {
  AtomicAccess::inc(&_offered_workers);
  SpinYield spin;
  while (AtomicAccess::load(&_expected_workers) != AtomicAccess::load(&_offered_workers)) {
    if (_queues->has_overflow()) {
      AtomicAccess::dec(&_offered_workers);
      return false;
    }
    if (_heap->cancelled_gc()) {
      break;
    }
    spin.wait();
  }
  return true;
}
