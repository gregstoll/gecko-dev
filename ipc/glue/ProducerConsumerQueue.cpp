/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProducerConsumerQueue.h"

namespace mozilla {

// TODO: Move to header for performance
PcqStatus ProducerView::Write(const void* aBuffer, size_t aBufferSize) {
  MOZ_ASSERT(aBuffer && (aBufferSize > 0));
  if (detail::NeedsSharedMemory(aBufferSize, mProducer->Size())) {
    RefPtr<SharedMemoryBasic> smem = MakeRefPtr<SharedMemoryBasic>();
    if (!smem->Create(aBufferSize) || !smem->Map(aBufferSize)) {
      return PcqStatus::PcqFatalError;
    }
    SharedMemoryBasic::Handle handle;
    if (!smem->ShareToProcess(mProducer->mOtherPid, &handle)) {
      return PcqStatus::PcqFatalError;
    }
    memcpy(smem->memory(), aBuffer, aBufferSize);
    smem->CloseHandle();
    return WriteParam(handle);
  }

  return mProducer->WriteObject(mRead, mWrite, aBuffer, aBufferSize);
}

size_t ProducerView::MinSizeBytes(size_t aNBytes) {
  return detail::NeedsSharedMemory(aNBytes, mProducer->Size()) ?
    MinSizeParam((SharedMemoryBasic::Handle*)nullptr) : aNBytes;
}

// TODO: Move to header for performance
PcqStatus ConsumerView::Read(void* aBuffer, size_t aBufferSize) {
  struct PcqReadBytesMatcher {
    PcqStatus operator()(PcqStatus x) { MOZ_ASSERT(!IsSuccess(x)); return x; }
    PcqStatus operator()(RefPtr<SharedMemoryBasic>& smem) {
      MOZ_ASSERT(smem);
      PcqStatus ret;
      if (smem->memory()) {
        if (mBuffer) {
          memcpy(mBuffer, smem->memory(), mBufferSize);
        }
        ret = PcqStatus::Success;
      } else {
        ret = PcqStatus::PcqFatalError;
      }
      // TODO: Problem: CloseHandle should only be called on the remove/skip call.  A peek should not CloseHandle!
      smem->CloseHandle();
      return ret;
    }
    PcqStatus operator()(void* x) {
      // It should have read directly into mBuffer (unless both are null)
      MOZ_ASSERT(x == mBuffer);
      return PcqStatus::Success;
    }

    void* mBuffer;
    size_t mBufferSize;
  };

  MOZ_ASSERT(aBufferSize > 0);
  // Reads directly into aBuffer (or skips if aBuffer is null) for small items.
  // Returns shared memory for large items.  Otherwise, returns an error. 
  PcqReadBytesVariant result = ReadVariant(aBuffer, aBufferSize);
  return result.match(PcqReadBytesMatcher { aBuffer, aBufferSize });
}

// TODO: Move to header for performance
ipc::ConsumerView::PcqReadBytesVariant ConsumerView::ReadVariant(void* aBuffer, size_t aBufferSize) {
  // TODO: Find some way to MOZ_RELEASE_ASSERT that buffersize exactly matches what
  // was in queue.  This doesn't appear to be possible with the information available.
  if (detail::NeedsSharedMemory(aBufferSize, mConsumer->Size())) {
    // Always read shared-memory -- don't just skip.
    SharedMemoryBasic::Handle handle;
    PcqStatus status = ReadParam(&handle);
    if (!IsSuccess(status)) {
      return AsVariant(status);
    }

    // TODO: This needs to return the same refptr even when peeking/during transactions
    // that get aborted/rewound.  So this is wrong.
    RefPtr<SharedMemoryBasic> sharedMem = MakeRefPtr<SharedMemoryBasic>();
    if (!sharedMem->IsHandleValid(handle) ||
        !sharedMem->SetHandle(handle, ipc::SharedMemory::RightsReadWrite)) {
      return AsVariant(PcqStatus::PcqFatalError);
    }
    return AsVariant(sharedMem);
  }

  PcqStatus status =
    mConsumer->ReadObject(mRead, mWrite, aBuffer, aBufferSize);
  if (!IsSuccess(status)) {
    return AsVariant(status);
  }
  return AsVariant(aBuffer);
}

size_t ConsumerView::MinSizeBytes(size_t aNBytes) {
  return detail::NeedsSharedMemory(aNBytes, mConsumer->Size()) ?
    MinSizeParam((SharedMemoryBasic::Handle*)nullptr) : aNBytes;
}

}  // namespace mozilla
