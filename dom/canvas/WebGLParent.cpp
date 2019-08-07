/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLParent.h"

#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/layers/LayerTransactionParent.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "HostIpdlWebGLBridge.h"
#include "HostWebGLContext.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace dom {

WebGLParent::WebGLParent(
    WebGLVersion aVersion,
    UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
    UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource) {
  WeakPtr<WebGLParent> weakParent = this;
  mHostBridge = new HostIpdlWebGLBridge(
      weakParent, aVersion, std::move(aCommandSink), std::move(aErrorSource));
}

WebGLParent::~WebGLParent() { FreeHostBridge(); }

void WebGLParent::FreeHostBridge() {
  // Tell host bridge to destroy itself on render thread, then forget
  // the only reference to it on the IPDL thread.
  if (mHostBridge) {
    mHostBridge->Destroy();
    mHostBridge = nullptr;
  }
}

mozilla::ipc::IPCResult WebGLParent::RecvUpdateLayerCompositableHandle(
    layers::PLayerTransactionParent* aLayerTransaction,
    const CompositableHandle& aHandle) {
  if (!mHostBridge) {
    // Already destroyed the context
    return IPC_OK();
  }

  auto layerTransParent =
      static_cast<layers::LayerTransactionParent*>(aLayerTransaction);
  FindAndSetCompositableHost(layerTransParent, aHandle);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebGLParent::RecvUpdateWRCompositableHandle(
    layers::PWebRenderBridgeParent* aWrBridge,
    const CompositableHandle& aHandle) {
  if (!mHostBridge) {
    // Already destroyed the context
    return IPC_OK();
  }

  auto wrBridgeParent = static_cast<layers::WebRenderBridgeParent*>(aWrBridge);
  FindAndSetCompositableHost(wrBridgeParent, aHandle);
  return IPC_OK();
}

void WebGLParent::FindAndSetCompositableHost(
    layers::CompositableParentManager* aCompositableMgr,
    const CompositableHandle& aHandle) {
  mCompositableHost = aCompositableMgr->FindCompositable(aHandle);
  MOZ_RELEASE_ASSERT(mCompositableHost,
                     "Failed to find CompositableHost for WebGL instance");
}

mozilla::ipc::IPCResult WebGLParent::RecvPresentToCompositable(
    const layers::SurfaceDescriptor& aSurfDesc, bool aToPremultAlpha,
    layers::LayersBackend aBackend,
    const wr::MaybeExternalImageId& aExternalImageId) {
  if (!mCompositableHost) {
    return IPC_OK();
  }

  wr::MaybeExternalImageId externalImageId = aExternalImageId;
  layers::TextureFlags flags = layers::TextureFlags::ORIGIN_BOTTOM_LEFT;
  if (aToPremultAlpha) {
    flags |= layers::TextureFlags::NON_PREMULTIPLIED;
  }

  RefPtr<layers::TextureHost> host = layers::TextureHost::Create(
      aSurfDesc, null_t(),
      static_cast<layers::CompositorBridgeParent*>(Manager()), aBackend, flags,
      externalImageId);

  if (!host) {
    MOZ_ASSERT_UNREACHABLE("Present failed to create TextureHost.");
    return IPC_OK();
  }

  AutoTArray<layers::CompositableHost::TimedTexture, 1> textures;
  layers::CompositableHost::TimedTexture* t = textures.AppendElement();
  t->mTexture = host;
  t->mTimeStamp = TimeStamp::Now();
  t->mPictureRect = nsIntRect(nsIntPoint(0, 0), nsIntSize(host->GetSize()));
  t->mFrameID = 0;
  t->mProducerID = 0;
  mCompositableHost->UseTextureHost(textures);
  return IPC_OK();
}

}  // namespace dom
}  // namespace mozilla
