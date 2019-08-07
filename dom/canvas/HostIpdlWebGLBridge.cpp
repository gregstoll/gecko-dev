/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HostIpdlWebGLBridge.h"

#include "WebGLParent.h"
#include "mozilla/layers/LayerTransactionParent.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "HostWebGLContext.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/webrender/RenderThread.h"

namespace detail {

// Tell NewRunnableMethod, etc to pass UniquePtrs as rvalue-refs.
template <typename T>
struct NonLValueReferenceStorageClass<mozilla::UniquePtr<T>> {
  typedef StoreCopyPassByRRef<mozilla::UniquePtr<T>> Type;
};

}  // namespace detail

namespace mozilla {

static bool IsWebGLRenderThread() {
  return HostWebGLContext::IsWebGLRenderThread();
}

static bool IsWebGLIpdlThread() {
  // If this context is not remote then we should be on the main thread.
  if (XRE_IsContentProcess()) {
    return NS_IsMainThread();
  }

  // Actors are on the Compositor thread.
  MOZ_ASSERT(XRE_IsGPUProcess() || XRE_IsParentProcess());
  return layers::CompositorThreadHolder::IsInCompositorThread();
}

/* static */
MessageLoop* HostIpdlWebGLBridge::WebGLRenderThreadMessageLoop() {
  return HostWebGLContext::WebGLRenderThreadMessageLoop();
}

/* static */
MessageLoop* HostIpdlWebGLBridge::WebGLIpdlThreadMessageLoop() {
  if (XRE_IsContentProcess()) {
    return layers::CompositorBridgeChild::Get()
               ? layers::CompositorBridgeChild::Get()->GetMessageLoop()
               : nullptr;
  }

  // We use actors on the Compositor thread.
  MOZ_ASSERT(XRE_IsGPUProcess() || XRE_IsParentProcess());
  return layers::CompositorThreadHolder::Loop();
}

WeakPtr<WebGLParent>& HostIpdlWebGLBridge::GetWeakGLParent() {
  MOZ_ASSERT(IsWebGLIpdlThread());
  return mMembers.mWeakGLParent;
}

UniquePtr<HostWebGLContext>& HostIpdlWebGLBridge::GetHost() {
  MOZ_ASSERT(IsWebGLRenderThread());
  return mMembers.mHost;
}

template <typename MethodType, typename... Args>
void HostIpdlWebGLBridge::DispatchToRenderThread(MethodType method,
                                                 Args... args) {
  MOZ_ASSERT(IsWebGLIpdlThread());
  if (IsWebGLRenderThread()) {
    (this->*method)(std::forward<Args>(args)...);
    return;
  }

  MessageLoop* msgLoop = WebGLRenderThreadMessageLoop();
  if (!msgLoop) {
    WEBGL_BRIDGE_LOGE("Failed to find WebGL RenderThread MessageLoop");
    return;
  }

  msgLoop->PostTask(NewNonOwningRunnableMethod<Args...>(
      "HostIpdlWebGLBridge::DispatchToRT", this, method,
      std::forward<Args>(args)...));
}

template <typename MethodType, typename... Args>
void HostIpdlWebGLBridge::DispatchToIpdlThread(MethodType method,
                                               Args... args) {
  MOZ_ASSERT(IsWebGLRenderThread());
  if (IsWebGLIpdlThread()) {
    (this->*method)(std::forward<Args>(args)...);
    return;
  }

  MessageLoop* msgLoop = WebGLIpdlThreadMessageLoop();
  if (!msgLoop) {
    WEBGL_BRIDGE_LOGE("Failed to find WebGL IPDL Thread MessageLoop");
    return;
  }

  msgLoop->PostTask(NewNonOwningRunnableMethod<Args...>(
      "HostIpdlWebGLBridge::DispatchToIpdl", this, method,
      std::forward<Args>(args)...));
}

static bool IsFeatureInBlacklist(const nsCOMPtr<nsIGfxInfo>& gfxInfo,
                                 int32_t feature,
                                 nsCString* const out_blacklistId) {
  int32_t status;
  if (!NS_SUCCEEDED(gfxUtils::ThreadSafeGetFeatureStatus(
          gfxInfo, feature, *out_blacklistId, &status))) {
    return false;
  }

  return status != nsIGfxInfo::FEATURE_STATUS_OK;
}

static bool HasAcceleratedLayers(const nsCOMPtr<nsIGfxInfo>& gfxInfo) {
  int32_t status;

  nsCString discardFailureId;
  gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                       nsIGfxInfo::FEATURE_DIRECT3D_9_LAYERS,
                                       discardFailureId, &status);
  if (status) return true;
  gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                       nsIGfxInfo::FEATURE_DIRECT3D_10_LAYERS,
                                       discardFailureId, &status);
  if (status) return true;
  gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                       nsIGfxInfo::FEATURE_DIRECT3D_10_1_LAYERS,
                                       discardFailureId, &status);
  if (status) return true;
  gfxUtils::ThreadSafeGetFeatureStatus(gfxInfo,
                                       nsIGfxInfo::FEATURE_DIRECT3D_11_LAYERS,
                                       discardFailureId, &status);
  if (status) return true;
  gfxUtils::ThreadSafeGetFeatureStatus(
      gfxInfo, nsIGfxInfo::FEATURE_OPENGL_LAYERS, discardFailureId, &status);
  if (status) return true;

  return false;
}

WebGLGfxFeatures HostIpdlWebGLBridge::GetWebGLFeatures() {
  WebGLGfxFeatures ret;
  const nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
  MOZ_ASSERT(gfxInfo);
  FailureReason reason;

  auto feature = nsIGfxInfo::FEATURE_WEBGL2;
  ret.allowWebGL2 = !IsFeatureInBlacklist(gfxInfo, feature, &reason.key);
  if (!ret.allowWebGL2) {
    reason.info =
        "Refused to create WebGL2 context because of blacklist"
        " entry: ";
    reason.info.Append(reason.key);
    ret.webGL2FailureReason = reason;
  }

  feature = nsIGfxInfo::FEATURE_WEBGL_OPENGL;
  ret.allowOpenGL = !IsFeatureInBlacklist(gfxInfo, feature, &reason.key);
  if (!ret.allowOpenGL) {
    reason.info =
        "Refused to create native OpenGL context because of blacklist"
        " entry: ";
    reason.info.Append(reason.key);
    ret.openGLFailureReason = reason;
  }

  ret.hasAcceleratedLayers = HasAcceleratedLayers(gfxInfo);

  return ret;
}

HostIpdlWebGLBridge::HostIpdlWebGLBridge(
    WeakPtr<WebGLParent>& aWeakParent, WebGLVersion aVersion,
    UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
    UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource) {
  MOZ_ASSERT(IsWebGLIpdlThread());
  GetWeakGLParent() = aWeakParent;
  WebGLGfxFeatures features = GetWebGLFeatures();
  DispatchToRenderThread(&HostIpdlWebGLBridge::Construct, aVersion,
                         std::move(aCommandSink), std::move(aErrorSource),
                         features);
}

void HostIpdlWebGLBridge::Construct(
    WebGLVersion aVersion,
    UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
    UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource,
    WebGLGfxFeatures aFeatures) {
  MOZ_ASSERT(IsWebGLRenderThread());

  UniquePtr<HostWebGLContext>& host = GetHost();
  host = WrapUnique(HostWebGLContext::Create(
      aVersion, aFeatures, std::move(aCommandSink), std::move(aErrorSource)));

  if (!BeginCommandQueueDrain()) {
    host = nullptr;
    return;
  }
}

void HostIpdlWebGLBridge::Destroy() {
  if (!IsWebGLRenderThread()) {
    // The WeakPtrs cannot be left non-null because it will be destroyed
    // on the render thread, which is not thread safe.  Clear it now.
    GetWeakGLParent() = nullptr;

    DispatchToRenderThread(&HostIpdlWebGLBridge::Destroy);
    return;
  }

  // Tell the recurring task to destroy this object.
  mShouldDestroy = true;
}

HostIpdlWebGLBridge::~HostIpdlWebGLBridge() {}

bool HostIpdlWebGLBridge::BeginCommandQueueDrain() {
  MOZ_ASSERT(IsWebGLRenderThread());
  MOZ_ASSERT(!mRunCommandsRunnable);
  UniquePtr<HostWebGLContext>& host = GetHost();
  if (!host) {
    // host creation had failed.  Don't start the recurring task.
    return false;
  }

  // TODO: The memory mgmt
  RefPtr<HostIpdlWebGLBridge> hostHolder = this;
  mRunCommandsRunnable = NS_NewRunnableFunction(
      "RunWebGLCommands", [hostHolder]() { hostHolder->RunCommandQueue(); });
  if (!mRunCommandsRunnable) {
    MOZ_ASSERT_UNREACHABLE("Failed to create RunWebGLCommands Runnable");
    return false;
  }

  // Start the recurring runnable.
  return RunCommandQueue();
}

bool HostIpdlWebGLBridge::RunCommandQueue() {
  MOZ_ASSERT(IsWebGLRenderThread());
  UniquePtr<HostWebGLContext>& host = GetHost();

  // This method can end up releasing the (only) reference to
  // the host, whose destruction would release what could be the
  // only (RefPtr) reference to this object, which would cause
  // this object to be destroyed while still running.  So we hold
  // a reference to ourself until the method completes.
  RefPtr<HostIpdlWebGLBridge> self = this;

  if (mShouldDestroy) {
    // Release objects and do not reissue the task.
    mRunCommandsRunnable = nullptr;
    host = nullptr;
    return true;
  }

  // Drain the queue for up to kMaxWebGLCommandTimeSliceMs, then
  // repeat no sooner than kDrainDelayMs later.
  // TODO: Tune these.
  static const uint32_t kMaxWebGLCommandTimeSliceMs = 1;
  static const uint32_t kDrainDelayMs = 0;
  MOZ_ASSERT(host);

  TimeDuration timeSlice =
      TimeDuration::FromMilliseconds(kMaxWebGLCommandTimeSliceMs);
  CommandResult result = host->RunCommandsForDuration(timeSlice);
  bool success = (result == CommandResult::kSuccess) ||
                 (result == CommandResult::kQueueEmpty);
  if (!success) {
    // Tell client to shut down WebGLParent.  Also do not reissue this task.
    WEBGL_BRIDGE_LOGE("WebGLParent failed while running commands");
    DispatchToIpdlThread(&HostIpdlWebGLBridge::SendQueueFailed);
    mRunCommandsRunnable = nullptr;
    host = nullptr;
    return false;
  }

  // Re-issue the task
  MOZ_ASSERT(mRunCommandsRunnable);
  MessageLoop::current()->PostDelayedTask(do_AddRef(mRunCommandsRunnable),
                                          kDrainDelayMs);
  return true;
}

void HostIpdlWebGLBridge::SendQueueFailed() {
  if (GetWeakGLParent()) {
    Unused << GetWeakGLParent()->SendQueueFailed();
  }
}

}  // namespace mozilla
