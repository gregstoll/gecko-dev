/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLPARENT_H_
#define WEBGLPARENT_H_

#include "mozilla/GfxMessageUtils.h"
#include "mozilla/dom/PWebGLParent.h"
#include "mozilla/dom/WebGLCrossProcessCommandQueue.h"
#include "mozilla/dom/WebGLErrorQueue.h"

namespace mozilla {

class HostIpdlWebGLBridge;
class HostWebGLContext;

namespace gfx {
class VRLayerParent;
}

namespace layers {
class CompositableHost;
class CompositableParentManager;
}  // namespace layers

namespace dom {

class WebGLParent;

class WebGLParent : public PWebGLParent, public SupportsWeakPtr<WebGLParent> {
 public:
  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(WebGLParent)

  WebGLParent(WebGLVersion aVersion,
              UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
              UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource);

 protected:
  friend PWebGLParent;

  explicit WebGLParent(UniquePtr<HostWebGLContext>&& aHost);

  mozilla::ipc::IPCResult RecvUpdateLayerCompositableHandle(
      layers::PLayerTransactionParent* aLayerTransaction,
      const CompositableHandle& aHandle);

  mozilla::ipc::IPCResult RecvUpdateWRCompositableHandle(
      layers::PWebRenderBridgeParent* aWrBridge,
      const CompositableHandle& aHandle);

  void FindAndSetCompositableHost(
      layers::CompositableParentManager* aCompositableMgr,
      const CompositableHandle& aHandle);

  mozilla::ipc::IPCResult RecvPresentToCompositable(
      const layers::SurfaceDescriptor& aSurfDesc, bool aToPremultAlpha,
      layers::LayersBackend aBackend,
      const wr::MaybeExternalImageId& aExternalImageId);

  mozilla::ipc::IPCResult Recv__delete__() override {
    FreeHostBridge();
    return IPC_OK();
  }

  void ActorDestroy(ActorDestroyReason aWhy) override { FreeHostBridge(); }

  ~WebGLParent();

  void FreeHostBridge();

  RefPtr<HostIpdlWebGLBridge> mHostBridge;
  RefPtr<layers::CompositableHost> mCompositableHost;
};

}  // namespace dom
}  // namespace mozilla

#endif  // WEBGLPARENT_H_
