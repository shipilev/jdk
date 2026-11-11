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

#include "gc/shared/taskqueue.inline.hpp"
#include "utilities/stack.inline.hpp"

void ShenandoahTaskQueue::push(ShenandoahMarkTask task) {
  if (!_chunk->push(task)) {
    _shared_list->push(_chunk);
    _chunk = _free_list->pop_or_create();
    bool succ = _chunk->push(task);
    assert(succ, "Must succeed");
  }
}

bool ShenandoahTaskQueue::pop(ShenandoahMarkTask& task) {
  if (_chunk->pop(task)) {
    return true;
  }

  auto chunk = _shared_list->pop();
  if (chunk != nullptr) {
    _free_list->push(_chunk);
    _chunk = chunk;
    return _chunk->pop(task);
  }

  return false;
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

#endif // SHARE_GC_SHENANDOAH_SHENANDOAHTASKQUEUE_INLINE_HPP
