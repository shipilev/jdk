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

void ShenandoahLock::contended_lock(bool allow_block_for_safepoint) {
  if (UseNewCode) {
    Thread* thread = Thread::current();
    ResourceMark rm;

    const char* name = thread->is_Java_thread() ? "Java thread" : thread->name();
    log_info(gc)("CONTENDED LOCKING by %s (" PTR_FORMAT ") started", name, p2i(thread));

    jlong time1 = os::javaTimeNanos();
    contended_lock_real(allow_block_for_safepoint);
    jlong time2 = os::javaTimeNanos();

    log_info(gc)("CONTENDED LOCKING by %s (" PTR_FORMAT ") took " JLONG_FORMAT " ns", name, p2i(thread), (time2 - time1));
  } else {
    contended_lock_real(allow_block_for_safepoint);
  }
}

void ShenandoahLock::contended_lock_real(bool allow_block_for_safepoint) {
  Thread* thread = Thread::current();
  if (thread->is_Java_thread()) {
    // Java threads spin a little before yielding and potentially blocking.
    constexpr uint32_t SPINS = 0x1F;
    JavaThread* java_thread = JavaThread::cast(thread);
    if (allow_block_for_safepoint) {
      contended_lock_internal<true, SPINS>(java_thread);
    } else {
      contended_lock_internal<false, SPINS>(java_thread);
    }
  } else {
    // Non-Java threads are not allowed to block, and they spin hard
    // to progress quickly. The normal number of GC threads is low enough
    // for this not to have detrimental effect. This favors GC threads
    // a little over Java threads, which is good for GC progress under
    // extreme contention.
    contended_lock_internal<false, UINT32_MAX>(nullptr);
  }
}

template<bool ALLOW_BLOCK, uint32_t MAX_SPINS>
void ShenandoahLock::contended_lock_internal(JavaThread* java_thread) {
  uint32_t ctr = 0;
  while (!try_lock()) {
    if (ctr < MAX_SPINS && !SafepointSynchronize::is_synchronizing() && os::is_MP()) {
      // Lightly contended. Spin a little if there are multiple processors
      // and no safepoint is pending.
      ctr++;
      SpinPause();
    } else if (ALLOW_BLOCK) {
      // Notify VM we are blocking, and suspend if safepoint was announced
      // while we were backing off.
      ThreadBlockInVM block(java_thread, true);
      os::naked_yield();
    } else {
      os::naked_yield();
    }
  }
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
