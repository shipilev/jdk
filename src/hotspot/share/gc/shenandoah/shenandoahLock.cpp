/*
 * Copyright (c) 2019, Red Hat, Inc. All rights reserved.
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

#include "precompiled.hpp"

#include "runtime/os.hpp"

#include "gc/shenandoah/shenandoahLock.hpp"
#include "runtime/atomic.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/os.inline.hpp"

void ShenandoahLock::SpinAcquire(volatile int * adr) {
  if (Atomic::cmpxchg(adr, 0, 1) == 0) {
    return;   // normal fast-path return
  }

  // Slow-path : We've encountered contention -- Spin/Yield/Block strategy.
  int ctr = 0;
  int Yields = 0;
  for (;;) {
    while (*adr != 0) {
      ++ctr;
      if ((ctr & 0xFFF) == 0 || !os::is_MP()) {
        Thread* current = Thread::current();
        if (current->is_Java_thread()) {
          ThreadBlockInVM tbim(JavaThread::cast(current));
          if (Yields > 5) {
            os::naked_short_sleep(1);
          } else {
            os::naked_yield();
            ++Yields;
          }
        } else {
          if (Yields > 5) {
            os::naked_short_sleep(1);
          } else {
            os::naked_yield();
            ++Yields;
          }
        }
      } else {
        SpinPause();
      }
    }
    if (Atomic::cmpxchg(adr, 0, 1) == 0) return;
  }
}

void ShenandoahLock::SpinRelease(volatile int * adr) {
  OrderAccess::fence();      // guarantee at least release consistency.
  *adr = 0;
}


void ShenandoahLock::lock() {
#ifdef ASSERT
  assert(_owner != Thread::current(), "reentrant locking attempt, would deadlock");
#endif
  SpinAcquire(&_state);
#ifdef ASSERT
  assert(_state == locked, "must be locked");
  assert(_owner == nullptr, "must not be owned");
  _owner = Thread::current();
#endif
}

void ShenandoahLock::unlock() {
#ifdef ASSERT
  assert (_owner == Thread::current(), "sanity");
  _owner = nullptr;
#endif
  SpinRelease(&_state);
}

ShenandoahSimpleLock::ShenandoahSimpleLock() {
  assert(os::mutex_init_done(), "Too early!");
}

void ShenandoahSimpleLock::lock() {
  _lock.lock();
}

void ShenandoahSimpleLock::unlock() {
  _lock.unlock();
}

ShenandoahReentrantLock::ShenandoahReentrantLock() :
  ShenandoahSimpleLock(), _owner(nullptr), _count(0) {
  assert(os::mutex_init_done(), "Too early!");
}

ShenandoahReentrantLock::~ShenandoahReentrantLock() {
  assert(_count == 0, "Unbalance");
}

void ShenandoahReentrantLock::lock() {
  Thread* const thread = Thread::current();
  Thread* const owner = Atomic::load(&_owner);

  if (owner != thread) {
    ShenandoahSimpleLock::lock();
    Atomic::store(&_owner, thread);
  }

  _count++;
}

void ShenandoahReentrantLock::unlock() {
  assert(owned_by_self(), "Invalid owner");
  assert(_count > 0, "Invalid count");

  _count--;

  if (_count == 0) {
    Atomic::store(&_owner, (Thread*)nullptr);
    ShenandoahSimpleLock::unlock();
  }
}

bool ShenandoahReentrantLock::owned_by_self() const {
  Thread* const thread = Thread::current();
  Thread* const owner = Atomic::load(&_owner);
  return owner == thread;
}
