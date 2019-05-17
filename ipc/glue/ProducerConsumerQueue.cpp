/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProducerConsumerQueue.h"

namespace mozilla {

PcqStatus ProducerView::Write(const void* aBuffer, size_t aBufferSize) {
  MOZ_ASSERT(aBuffer && (aBufferSize > 0));
  return mProducer->WriteObject(mRead, mWrite, aBuffer, aBufferSize);
}

PcqStatus ConsumerView::Read(void* aBuffer, size_t aBufferSize) {
  return mConsumer->ReadObject(mRead, mWrite, aBuffer, aBufferSize);
}

}  // namespace mozilla
