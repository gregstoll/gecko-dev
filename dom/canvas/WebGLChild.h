/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLCHILD_H_
#define WEBGLCHILD_H_

#include "mozilla/dom/PWebGLChild.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsWeakReference.h"
#include "nsQueryObject.h"
#include "ClientWebGLContext.h"

namespace mozilla {

class ClientWebGLContext;

namespace dom {

class WebGLChild : public PWebGLChild {
 public:
  mozilla::ipc::IPCResult RecvQueueFailed() override {
    mozilla::ClientWebGLContext* context = GetContext();
    if (context) {
      context->OnQueueFailed();
    }
    return Send__delete__(this) ? IPC_OK() : IPC_FAIL_NO_REASON(this);
  }

  mozilla::ClientWebGLContext* GetContext() {
    nsCOMPtr<nsICanvasRenderingContextInternal> ret = do_QueryReferent(mContext);
    if (!ret) {
      return nullptr;
    }
    return static_cast<mozilla::ClientWebGLContext*>(ret.get());
  }

 protected:
  friend mozilla::ClientWebGLContext;
  void SetContext(mozilla::ClientWebGLContext* aContext) {
    mContext = do_GetWeakReference(aContext);
  }

  nsWeakPtr mContext;
};

}  // namespace dom
}  // namespace mozilla

#endif // WEBGLCHILD_H_
