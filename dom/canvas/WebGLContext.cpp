/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include <algorithm>
#include <queue>

#include "AccessCheck.h"
#include "gfxConfig.h"
#include "gfxContext.h"
#include "gfxCrashReporterUtils.h"
#include "gfxPattern.h"
#include "gfxPrefs.h"
#include "gfxUtils.h"
#include "MozFramebuffer.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "GLReadTexImageHelper.h"
#include "GLScreenBuffer.h"
#include "ImageContainer.h"
#include "ImageEncoder.h"
#include "Layers.h"
#include "LayerUserData.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/EnumeratedArrayCycleCollection.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessPriorityManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/Telemetry.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsIClassInfoImpl.h"
#include "nsIConsoleService.h"
#include "nsIGfxInfo.h"
#include "nsIObserverService.h"
#include "nsIVariant.h"
#include "nsIWidget.h"
#include "nsIXPConnect.h"
#include "nsServiceManagerUtils.h"
#include "SharedSurfaceGL.h"
#include "SVGObserverUtils.h"
#include "prenv.h"
#include "ScopedGLHelpers.h"
#include "VRManagerChild.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"

// Local
#include "CanvasUtils.h"
#include "ClientWebGLContext.h"
#include "HostWebGLContext.h"
#include "WebGL1Context.h"
#include "WebGLActiveInfo.h"
#include "WebGLBuffer.h"
#include "WebGLChild.h"
#include "WebGLContextLossHandler.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLMemoryTracker.h"
#include "WebGLObjectModel.h"
#include "WebGLProgram.h"
#include "WebGLQuery.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLSync.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "WebGLVertexAttribData.h"

#ifdef MOZ_WIDGET_COCOA
#  include "nsCocoaFeatures.h"
#endif

#ifdef XP_WIN
#  include "WGLLibrary.h"
#endif

// Generated
#include "mozilla/dom/WebGLRenderingContextBinding.h"

namespace mozilla {

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::gl;
using namespace mozilla::layers;

bool WebGLContextOptions::operator==(const WebGLContextOptions& r) const {
  return memcmp(this, &r, sizeof(WebGLContextOptions)) == 0;
}

bool WebGLPreferences::operator==(const WebGLPreferences& r) const {
  return (shouldResistFingerprinting == r.shouldResistFingerprinting) &&
         (enableDebugRendererInfo == r.enableDebugRendererInfo) &&
         (privilegedExtensionsEnabled == r.privilegedExtensionsEnabled) &&
         (rendererStringOverride == r.rendererStringOverride) &&
         (vendorStringOverride == r.vendorStringOverride);
}

WebGLContext::WebGLContext(const WebGLGfxFeatures& aFeatures)
    : gl(mGL_OnlyClearInDestroyResourcesAndContext),  // const reference
      mMaxPerfWarnings(gfxPrefs::WebGLMaxPerfWarnings()),
      mNumPerfWarnings(0),
      mMaxAcceptableFBStatusInvals(
          gfxPrefs::WebGLMaxAcceptableFBStatusInvals()),
      mHost(nullptr),
      mBackend(LayersBackend::LAYERS_NONE),
      mDataAllocGLCallCount(0),
      mBypassShaderValidation(false),
      mFeatures(aFeatures),
      mEmptyTFO(0),
      mContextLossHandler(this),
      mNeedsFakeNoAlpha(false),
      mNeedsFakeNoDepth(false),
      mNeedsFakeNoStencil(false),
      mAllowFBInvalidation(gfxPrefs::WebGLFBInvalidation()),
      mMsaaSamples((uint8_t)gfxPrefs::WebGLMsaaSamples()) {
  mGeneration = 0;
  mShouldPresent = true;
  mOptionsFrozen = false;
  mDisableExtensions = false;
  mIsMesa = false;
  mWebGLError = 0;

  mViewportX = 0;
  mViewportY = 0;
  mViewportWidth = 0;
  mViewportHeight = 0;

  mDitherEnabled = 1;
  mRasterizerDiscardEnabled = 0;  // OpenGL ES 3.0 spec p244
  mScissorTestEnabled = 0;
  mStencilTestEnabled = 0;

  if (NS_IsMainThread()) {
    // XXX mtseng: bug 709490, not thread safe
    WebGLMemoryTracker::AddWebGLContext(this);
  }

  mAllowContextRestore = true;
  mDisallowContextRestore = false;
  mLastLossWasSimulated = false;

  mAlreadyGeneratedWarnings = 0;
  mAlreadyWarnedAboutFakeVertexAttrib0 = false;
  mAlreadyWarnedAboutViewportLargerThanDest = false;

  mMaxWarnings = gfxPrefs::WebGLMaxWarningsPerContext();
  if (mMaxWarnings < -1) {
    GenerateWarning(
        "webgl.max-warnings-per-context size is too large (seems like a "
        "negative value wrapped)");
    mMaxWarnings = 0;
  }

  mDisableFragHighP = false;

  mDrawCallsSinceLastFlush = 0;
}

WebGLContext::~WebGLContext() {
  DestroyResourcesAndContext();
  if (NS_IsMainThread()) {
    // XXX mtseng: bug 709490, not thread safe
    WebGLMemoryTracker::RemoveWebGLContext(this);
  }
}

template <typename T>
void ClearLinkedList(LinkedList<T>& list) {
  while (!list.isEmpty()) {
    list.getLast()->DeleteOnce();
  }
}

void WebGLContext::DestroyResourcesAndContext() {
  if (!gl) return;

  mDefaultFB = nullptr;
  mResolvedDefaultFB = nullptr;

  mBound2DTextures.Clear();
  mBoundCubeMapTextures.Clear();
  mBound3DTextures.Clear();
  mBound2DArrayTextures.Clear();
  mBoundSamplers.Clear();
  mBoundArrayBuffer = nullptr;
  mBoundCopyReadBuffer = nullptr;
  mBoundCopyWriteBuffer = nullptr;
  mBoundPixelPackBuffer = nullptr;
  mBoundPixelUnpackBuffer = nullptr;
  mBoundTransformFeedbackBuffer = nullptr;
  mBoundUniformBuffer = nullptr;
  mCurrentProgram = nullptr;
  mActiveProgramLinkInfo = nullptr;
  mBoundDrawFramebuffer = nullptr;
  mBoundReadFramebuffer = nullptr;
  mBoundRenderbuffer = nullptr;
  mBoundVertexArray = nullptr;
  mDefaultVertexArray = nullptr;
  mBoundTransformFeedback = nullptr;
  mDefaultTransformFeedback = nullptr;
#if defined(MOZ_WIDGET_ANDROID)
  mVRScreen = nullptr;
#endif

  mQuerySlot_SamplesPassed = nullptr;
  mQuerySlot_TFPrimsWritten = nullptr;
  mQuerySlot_TimeElapsed = nullptr;

  mIndexedUniformBufferBindings.clear();

  //////

  ClearLinkedList(mBuffers);
  ClearLinkedList(mFramebuffers);
  ClearLinkedList(mPrograms);
  ClearLinkedList(mQueries);
  ClearLinkedList(mRenderbuffers);
  ClearLinkedList(mSamplers);
  ClearLinkedList(mShaders);
  ClearLinkedList(mSyncs);
  ClearLinkedList(mTextures);
  ClearLinkedList(mTransformFeedbacks);
  ClearLinkedList(mVertexArrays);

  //////

  if (mEmptyTFO) {
    gl->fDeleteTransformFeedbacks(1, &mEmptyTFO);
    mEmptyTFO = 0;
  }

  //////

  if (mFakeVertexAttrib0BufferObject) {
    gl->fDeleteBuffers(1, &mFakeVertexAttrib0BufferObject);
    mFakeVertexAttrib0BufferObject = 0;
  }

  // disable all extensions except "WEBGL_lose_context". see bug #927969
  // spec: http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
  for (size_t i = 0; i < size_t(WebGLExtensionID::Max); ++i) {
    WebGLExtensionID extension = WebGLExtensionID(i);

    if (!IsExtensionEnabled(extension) ||
        (extension == WebGLExtensionID::WEBGL_lose_context))
      continue;

    mExtensions[extension]->MarkLost();
    mExtensions[extension] = nullptr;
  }

  // We just got rid of everything, so the context had better
  // have been going away.
  if (GLContext::ShouldSpew()) {
    printf_stderr("--- WebGL context destroyed: %p\n", gl.get());
  }

  MOZ_ASSERT(gl);
  gl->MarkDestroyed();
  mGL_OnlyClearInDestroyResourcesAndContext = nullptr;
  MOZ_ASSERT(!gl);
}

void ClientWebGLContext::Invalidate() {
  if (!mCanvasElement) return;

  mCapturedFrameInvalidated = true;

  if (mInvalidated) return;

  SVGObserverUtils::InvalidateDirectRenderingObservers(mCanvasElement);

  mInvalidated = true;
  mCanvasElement->InvalidateCanvasContent(nullptr);
}

void WebGLContext::OnMemoryPressure() {
  bool shouldLoseContext = mLoseContextOnMemoryPressure;

  if (!mCanLoseContextInForeground &&
      ProcessPriorityManager::CurrentProcessIsForeground()) {
    shouldLoseContext = false;
  }

  if (shouldLoseContext) ForceLoseContext();
}

//
// nsICanvasRenderingContextInternal
//

bool WebGLContext::CreateAndInitGL(
    bool forceEnabled, std::vector<FailureReason>* const out_failReasons) {
  // Can't use WebGL in headless mode.
  if (gfxPlatform::IsHeadless()) {
    FailureReason reason;
    reason.info =
        "Can't use WebGL in headless mode (https://bugzil.la/1375585).";
    out_failReasons->push_back(reason);
    GenerateWarning("%s", reason.info.BeginReading());
    return false;
  }

  // WebGL can't be used when recording/replaying.
  if (recordreplay::IsRecordingOrReplaying()) {
    FailureReason reason;
    reason.info =
        "Can't use WebGL when recording or replaying "
        "(https://bugzil.la/1506467).";
    out_failReasons->push_back(reason);
    GenerateWarning("%s", reason.info.BeginReading());
    return false;
  }

  // WebGL2 is separately blocked:
  if (IsWebGL2()) {
    if (!mFeatures.allowWebGL2) {
      FailureReason& reason = mFeatures.webGL2FailureReason;
      out_failReasons->push_back(reason);
      GenerateWarning("%s", reason.info.BeginReading());
      return false;
    }
  }

  gl::CreateContextFlags flags = (gl::CreateContextFlags::NO_VALIDATION |
                                  gl::CreateContextFlags::PREFER_ROBUSTNESS);
  bool tryNativeGL = true;
  bool tryANGLE = false;

  if (forceEnabled) {
    flags |= gl::CreateContextFlags::FORCE_ENABLE_HARDWARE;
  }

  if (IsWebGL2()) {
    flags |= gl::CreateContextFlags::PREFER_ES3;
  } else if (!gfxPrefs::WebGL1AllowCoreProfile()) {
    flags |= gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE;
  }

  switch (mOptions.powerPreference) {
    case dom::WebGLPowerPreference::Low_power:
      break;

    case dom::WebGLPowerPreference::High_performance:
      flags |= gl::CreateContextFlags::HIGH_POWER;
      break;

      // Eventually add a heuristic, but for now default to high-performance.
      // We can even make it dynamic by holding on to a
      // ForceDiscreteGPUHelperCGL iff we decide it's a high-performance
      // application:
      // - Non-trivial canvas size
      // - Many draw calls
      // - Same origin with root page (try to stem bleeding from WebGL
      // ads/trackers)
    default:
      if (!gfxPrefs::WebGLDefaultLowPower()) {
        flags |= gl::CreateContextFlags::HIGH_POWER;
      }
      break;
  }

  // If "Use hardware acceleration when available" option is disabled:
  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING)) {
    flags &= ~gl::CreateContextFlags::HIGH_POWER;
  }

#ifdef XP_MACOSX
  const nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
  nsString vendorID, deviceID;

  // Avoid crash for Intel HD Graphics 3000 on OSX. (Bug 1413269)
  gfxInfo->GetAdapterVendorID(vendorID);
  gfxInfo->GetAdapterDeviceID(deviceID);
  if (vendorID.EqualsLiteral("0x8086") &&
      (deviceID.EqualsLiteral("0x0116") || deviceID.EqualsLiteral("0x0126"))) {
    flags |= gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE;
  }
#endif

  // --

  const auto surfaceCaps = [&]() {
    auto ret = gl::SurfaceCaps::ForRGBA();
    ret.premultAlpha = mOptions.premultipliedAlpha;
    ret.preserve = mOptions.preserveDrawingBuffer;

    if (!mOptions.alpha) {
      ret.premultAlpha = true;
    }
    return ret;
  }();

  // --

  const bool useEGL = PR_GetEnv("MOZ_WEBGL_FORCE_EGL");

#ifdef XP_WIN
  tryNativeGL = false;
  tryANGLE = true;

  if (gfxPrefs::WebGLDisableWGL()) {
    tryNativeGL = false;
  }

  if (gfxPrefs::WebGLDisableANGLE() || PR_GetEnv("MOZ_WEBGL_FORCE_OPENGL") ||
      useEGL) {
    tryNativeGL = true;
    tryANGLE = false;
  }
#endif

  if (tryNativeGL && !forceEnabled) {
    if (!mFeatures.allowOpenGL) {
      FailureReason& reason = mFeatures.openGLFailureReason;
      out_failReasons->push_back(reason);
      GenerateWarning("%s", reason.info.BeginReading());
      tryNativeGL = false;
    }
  }

  // --

  typedef decltype(
      gl::GLContextProviderEGL::CreateOffscreen) fnCreateOffscreenT;
  const auto fnCreate = [&](fnCreateOffscreenT* const pfnCreateOffscreen,
                            const char* const info) {
    const gfx::IntSize dummySize(1, 1);
    nsCString failureId;
    const RefPtr<GLContext> gl =
        pfnCreateOffscreen(dummySize, surfaceCaps, flags, &failureId);
    if (!gl) {
      out_failReasons->push_back(FailureReason(failureId, info));
    }
    return gl;
  };

  const auto newGL = [&]() -> RefPtr<gl::GLContext> {
    if (tryNativeGL) {
      if (useEGL)
        return fnCreate(&gl::GLContextProviderEGL::CreateOffscreen, "useEGL");

      const auto ret =
          fnCreate(&gl::GLContextProvider::CreateOffscreen, "tryNativeGL");
      if (ret) return ret;
    }

    if (tryANGLE) {
      // Force enable alpha channel to make sure ANGLE use correct framebuffer
      // format
      MOZ_ASSERT(surfaceCaps.alpha);
      return fnCreate(&gl::GLContextProviderEGL::CreateOffscreen, "tryANGLE");
    }
    return nullptr;
  }();

  if (!newGL) {
    out_failReasons->push_back(
        FailureReason("FEATURE_FAILURE_WEBGL_EXHAUSTED_DRIVERS",
                      "Exhausted GL driver options."));
    return false;
  }

  // --

  FailureReason reason;

  mGL_OnlyClearInDestroyResourcesAndContext = newGL;
  MOZ_RELEASE_ASSERT(gl);
  if (!InitAndValidateGL(&reason)) {
    DestroyResourcesAndContext();
    MOZ_RELEASE_ASSERT(!gl);

    // The fail reason here should be specific enough for now.
    out_failReasons->push_back(reason);
    return false;
  }

  return true;
}

// Fallback for resizes:

bool WebGLContext::EnsureDefaultFB() {
  if (mDefaultFB) {
    MOZ_ASSERT(mDefaultFB->mSize == mRequestedSize);
    return true;
  }

  const bool depthStencil = mOptions.depth || mOptions.stencil;
  auto attemptSize = mRequestedSize;

  while (attemptSize.width || attemptSize.height) {
    attemptSize.width = std::max(attemptSize.width, 1);
    attemptSize.height = std::max(attemptSize.height, 1);

    [&]() {
      if (mOptions.antialias) {
        MOZ_ASSERT(!mDefaultFB);
        mDefaultFB =
            MozFramebuffer::Create(gl, attemptSize, mMsaaSamples, depthStencil);
        if (mDefaultFB) return;
        if (mOptionsFrozen) return;
      }

      MOZ_ASSERT(!mDefaultFB);
      mDefaultFB = MozFramebuffer::Create(gl, attemptSize, 0, depthStencil);
    }();

    if (mDefaultFB) break;

    attemptSize.width /= 2;
    attemptSize.height /= 2;
  }

  if (!mDefaultFB) {
    GenerateWarning("Backbuffer resize failed. Losing context.");
    ForceLoseContext();
    return false;
  }

  mDefaultFB_IsInvalid = true;

  if (mDefaultFB->mSize != mRequestedSize) {
    GenerateWarning(
        "Requested size %dx%d was too large, but resize"
        " to %dx%d succeeded.",
        mRequestedSize.width, mRequestedSize.height, mDefaultFB->mSize.width,
        mDefaultFB->mSize.height);
  }
  mRequestedSize = mDefaultFB->mSize;
  return true;
}

void WebGLContext::ThrowEvent_WebGLContextCreationError(
    const nsACString& text) {
  MOZ_ASSERT(mHost);
  mHost->PostContextCreationError(nsCString(text));
}

WebGLContext::DoSetDimensionsData WebGLContext::DoSetDimensions(
    int32_t signedWidth, int32_t signedHeight) {
  const FuncScope funcScope(*this, "<SetDimensions>");
  (void)IsContextLost();  // We handle this ourselves.

  if (signedWidth < 0 || signedHeight < 0) {
    if (!gl) {
      Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID,
                            NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_SIZE"));
    }
    GenerateWarning(
        "Canvas size is too large (seems like a negative value wrapped)");
    return {NS_ERROR_OUT_OF_MEMORY, false};
  }

  uint32_t width = signedWidth;
  uint32_t height = signedHeight;

  // Early success return cases

  // Zero-sized surfaces can cause problems.
  if (width == 0) width = 1;

  if (height == 0) height = 1;

  // If we already have a gl context, then we just need to resize it
  if (gl) {
    if (uint32_t(mRequestedSize.width) == width &&
        uint32_t(mRequestedSize.height) == height) {
      return {NS_OK, false};
    }

    if (IsContextLost()) return {NS_OK, false};

    // If we've already drawn, we should commit the current buffer.
    PresentScreenBuffer();

    if (IsContextLost()) {
      GenerateWarning("WebGL context was lost due to swap failure.");
      return {NS_OK, false};
    }

    // Kill our current default fb(s), for later lazy allocation.
    mRequestedSize = {width, height};
    mDefaultFB = nullptr;

    mResetLayer = true;
    return {NS_OK, false};
  }

  nsCString failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_UNKOWN");
  auto autoTelemetry = mozilla::MakeScopeExit([&] {
    Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID, failureId);
  });

  // End of early return cases.
  // At this point we know that we're not just resizing an existing context,
  // we are initializing a new context.

  // We're going to create an entirely new context.  If our
  // generation is not 0 right now (that is, if this isn't the first
  // context we're creating), we may have to dispatch a context lost
  // event.

  // If incrementing the generation would cause overflow,
  // don't allow it.  Allowing this would allow us to use
  // resource handles created from older context generations.
  if (!(mGeneration + 1).isValid()) {
    // exit without changing the value of mGeneration
    failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_TOO_MANY");
    const nsLiteralCString text("Too many WebGL contexts created this run.");
    ThrowEvent_WebGLContextCreationError(text);
    return {NS_ERROR_FAILURE, true};
  }

  // increment the generation number - Do this early because later
  // in CreateOffscreenGL(), "default" objects are created that will
  // pick up the old generation.
  ++mGeneration;

  bool disabled = gfxPrefs::WebGLDisabled();

  // TODO: When we have software webgl support we should use that instead.
  disabled |= gfxPlatform::InSafeMode();

  if (disabled) {
    if (gfxPlatform::InSafeMode()) {
      failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_SAFEMODE");
    } else {
      failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_DISABLED");
    }
    const nsLiteralCString text("WebGL is currently disabled.");
    ThrowEvent_WebGLContextCreationError(text);
    return {NS_ERROR_FAILURE, true};
  }

  if (gfxPrefs::WebGLDisableFailIfMajorPerformanceCaveat()) {
    mOptions.failIfMajorPerformanceCaveat = false;
  }

  if (mOptions.failIfMajorPerformanceCaveat) {
    nsCOMPtr<nsIGfxInfo> gfxInfo = services::GetGfxInfo();
    if (!mFeatures.hasAcceleratedLayers) {
      failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_PERF_CAVEAT");
      const nsLiteralCString text(
          "failIfMajorPerformanceCaveat: Compositor is not"
          " hardware-accelerated.");
      ThrowEvent_WebGLContextCreationError(text);
      return {NS_ERROR_FAILURE, true};
    }
  }

  // Alright, now let's start trying.
  bool forceEnabled = gfxPrefs::WebGLForceEnabled();
  ScopedGfxFeatureReporter reporter("WebGL", forceEnabled);

  MOZ_ASSERT(!gl);
  std::vector<FailureReason> failReasons;
  if (!CreateAndInitGL(forceEnabled, &failReasons)) {
    nsCString text("WebGL creation failed: ");
    for (const auto& cur : failReasons) {
      // Don't try to accumulate using an empty key if |cur.key| is empty.
      if (cur.key.IsEmpty()) {
        Telemetry::Accumulate(
            Telemetry::CANVAS_WEBGL_FAILURE_ID,
            NS_LITERAL_CSTRING("FEATURE_FAILURE_REASON_UNKNOWN"));
      } else {
        Telemetry::Accumulate(Telemetry::CANVAS_WEBGL_FAILURE_ID, cur.key);
      }

      text.AppendLiteral("\n* ");
      text.Append(cur.info);
    }
    failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_REASON");
    ThrowEvent_WebGLContextCreationError(text);
    return {NS_ERROR_FAILURE, true};
  }
  MOZ_ASSERT(gl);

  if (mOptions.failIfMajorPerformanceCaveat) {
    if (gl->IsWARP()) {
      DestroyResourcesAndContext();
      MOZ_ASSERT(!gl);

      failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_PERF_WARP");
      const nsLiteralCString text(
          "failIfMajorPerformanceCaveat: Driver is not"
          " hardware-accelerated.");
      ThrowEvent_WebGLContextCreationError(text);
      return {NS_ERROR_FAILURE, true};
    }

#ifdef XP_WIN
    if (gl->GetContextType() == gl::GLContextType::WGL &&
        !gl::sWGLLib.HasDXInterop2()) {
      DestroyResourcesAndContext();
      MOZ_ASSERT(!gl);

      failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_DXGL_INTEROP2");
      const nsLiteralCString text("Caveat: WGL without DXGLInterop2.");
      ThrowEvent_WebGLContextCreationError(text);
      return {NS_ERROR_FAILURE, true};
    }
#endif
  }

  MOZ_ASSERT(!mDefaultFB);
  mRequestedSize = {width, height};
  if (!EnsureDefaultFB()) {
    MOZ_ASSERT(!gl);

    failureId = NS_LITERAL_CSTRING("FEATURE_FAILURE_WEBGL_BACKBUFFER");
    const nsLiteralCString text("Initializing WebGL backbuffer failed.");
    ThrowEvent_WebGLContextCreationError(text);
    return {NS_ERROR_FAILURE, true};
  }

  if (GLContext::ShouldSpew()) {
    printf_stderr("--- WebGL context created: %p\n", gl.get());
  }

  // Update our internal stuff:

  mOptions.antialias &= bool(mDefaultFB->mSamples);

  if (!mOptions.alpha) {
    // We always have alpha.
    mNeedsFakeNoAlpha = true;
  }

  if (mOptions.depth || mOptions.stencil) {
    // We always have depth+stencil if we have either.
    if (!mOptions.depth) {
      mNeedsFakeNoDepth = true;
    }
    if (!mOptions.stencil) {
      mNeedsFakeNoStencil = true;
    }
  }

  mNeedsFakeNoStencil_UserFBs = false;
#ifdef MOZ_WIDGET_COCOA
  if (!nsCocoaFeatures::IsAtLeastVersion(10, 12) &&
      gl->Vendor() == GLVendor::Intel) {
    mNeedsFakeNoStencil_UserFBs = true;
  }
#endif

  mResetLayer = true;
  mOptionsFrozen = true;

  //////
  // Initial setup.

  gl->mImplicitMakeCurrent = true;

  const auto& size = mDefaultFB->mSize;

  mViewportX = mViewportY = 0;
  mViewportWidth = size.width;
  mViewportHeight = size.height;
  gl->fViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight);

  mScissorRect = {0, 0, size.width, size.height};
  mScissorRect.Apply(*gl);

  //////
  // Check everything

  AssertCachedBindings();
  AssertCachedGlobalState();

  mShouldPresent = true;

  //////

  reporter.SetSuccessful();

  failureId = NS_LITERAL_CSTRING("SUCCESS");

  gl->ResetSyncCallCount("WebGLContext Initialization");
  return {NS_OK, true};
}

void WebGLContext::SetPreferences(const WebGLPreferences& aPrefs) {
  mPrefs = aPrefs;
}

void ClientWebGLContext::LoseOldestWebGLContextIfLimitExceeded() {
  const auto maxWebGLContexts = gfxPrefs::WebGLMaxContexts();
  auto maxWebGLContextsPerPrincipal = gfxPrefs::WebGLMaxContextsPerPrincipal();

  // maxWebGLContextsPerPrincipal must be less than maxWebGLContexts
  MOZ_ASSERT(maxWebGLContextsPerPrincipal <= maxWebGLContexts);
  maxWebGLContextsPerPrincipal =
      std::min(maxWebGLContextsPerPrincipal, maxWebGLContexts);

  if (!NS_IsMainThread()) {
    // XXX mtseng: bug 709490, WebGLMemoryTracker is not thread safe.
    return;
  }

  // it's important to update the index on a new context before losing old
  // contexts, otherwise new unused contexts would all have index 0 and we
  // couldn't distinguish older ones when choosing which one to lose first.
  UpdateLastUseIndex();

  CompositorBridgeChild* cbc = CompositorBridgeChild::Get();
  MOZ_ASSERT(cbc);
  nsTArray<PWebGLChild*> childArray;
  cbc->ManagedPWebGLChild(childArray);

  // quick exit path, should cover a majority of cases
  if (childArray.Length() <= maxWebGLContextsPerPrincipal) return;

  // note that here by "context" we mean "non-lost context". See the check for
  // IsContextLost() below. Indeed, the point of this function is to maybe lose
  // some currently non-lost context.

  uint64_t oldestIndex = UINT64_MAX;
  uint64_t oldestIndexThisPrincipal = UINT64_MAX;
  ClientWebGLContext* oldestContext = nullptr;
  ClientWebGLContext* oldestContextThisPrincipal = nullptr;
  size_t numContexts = 0;
  size_t numContextsThisPrincipal = 0;

  for (size_t i = 0; i < childArray.Length(); ++i) {
    ClientWebGLContext* context =
        static_cast<WebGLChild*>(childArray[i])->GetContext();
    MOZ_ASSERT(context);
    if (!context) {
      continue;
    }

    // don't want to lose ourselves.
    if (context == this) continue;

    if (!context->GetCanvas()) {
      // Zombie context: the canvas is already destroyed, but something else
      // (typically the compositor) is still holding on to the context.
      // Killing zombies is a no-brainer.
      context->LoseContext();
      continue;
    }

    numContexts++;
    if (context->mLastUseIndex < oldestIndex) {
      oldestIndex = context->mLastUseIndex;
      oldestContext = context;
    }

    nsIPrincipal* ourPrincipal = GetCanvas()->NodePrincipal();
    nsIPrincipal* theirPrincipal = context->GetCanvas()->NodePrincipal();
    bool samePrincipal;
    nsresult rv = ourPrincipal->Equals(theirPrincipal, &samePrincipal);
    if (NS_SUCCEEDED(rv) && samePrincipal) {
      numContextsThisPrincipal++;
      if (context->mLastUseIndex < oldestIndexThisPrincipal) {
        oldestIndexThisPrincipal = context->mLastUseIndex;
        oldestContextThisPrincipal = context;
      }
    }
  }

  if (numContextsThisPrincipal > maxWebGLContextsPerPrincipal) {
    PostWarning(nsPrintfCString(
        "Exceeded %u live WebGL contexts for this principal, losing the "
        "least recently used one.",
        maxWebGLContextsPerPrincipal));
    MOZ_ASSERT(oldestContextThisPrincipal);  // if we reach this point, this
                                             // can't be null
    oldestContextThisPrincipal->LoseContext();
  } else if (numContexts > maxWebGLContexts) {
    PostWarning(
        nsPrintfCString("Exceeded %u live WebGL contexts, losing the least "
                        "recently used one.",
                        maxWebGLContexts));
    MOZ_ASSERT(oldestContext);  // if we reach this point, this can't be null
    oldestContext->LoseContext();
  }
}

// -

namespace webgl {

ScopedPrepForResourceClear::ScopedPrepForResourceClear(
    const WebGLContext& webgl_)
    : webgl(webgl_) {
  const auto& gl = webgl.gl;

  if (webgl.mScissorTestEnabled) {
    gl->fDisable(LOCAL_GL_SCISSOR_TEST);
  }
  if (webgl.mRasterizerDiscardEnabled) {
    gl->fDisable(LOCAL_GL_RASTERIZER_DISCARD);
  }

  // "The clear operation always uses the front stencil write mask
  //  when clearing the stencil buffer."
  webgl.DoColorMask(0x0f);
  gl->fDepthMask(true);
  gl->fStencilMaskSeparate(LOCAL_GL_FRONT, 0xffffffff);

  gl->fClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  gl->fClearDepth(1.0f);  // Depth formats are always cleared to 1.0f, not 0.0f.
  gl->fClearStencil(0);
}

ScopedPrepForResourceClear::~ScopedPrepForResourceClear() {
  const auto& gl = webgl.gl;

  if (webgl.mScissorTestEnabled) {
    gl->fEnable(LOCAL_GL_SCISSOR_TEST);
  }
  if (webgl.mRasterizerDiscardEnabled) {
    gl->fEnable(LOCAL_GL_RASTERIZER_DISCARD);
  }

  // DoColorMask() is lazy.
  gl->fDepthMask(webgl.mDepthWriteMask);
  gl->fStencilMaskSeparate(LOCAL_GL_FRONT, webgl.mStencilWriteMaskFront);

  gl->fClearColor(webgl.mColorClearValue[0], webgl.mColorClearValue[1],
                  webgl.mColorClearValue[2], webgl.mColorClearValue[3]);
  gl->fClearDepth(webgl.mDepthClearValue);
  gl->fClearStencil(webgl.mStencilClearValue);
}

}  // namespace webgl

// -

void WebGLContext::OnEndOfFrame() const {
  if (gfxPrefs::WebGLSpewFrameAllocs()) {
    GeneratePerfWarning("[webgl.perf.spew-frame-allocs] %" PRIu64
                        " data allocations this frame.",
                        mDataAllocGLCallCount);
  }
  mDataAllocGLCallCount = 0;
  gl->ResetSyncCallCount("WebGLContext PresentScreenBuffer");
}

void WebGLContext::BlitBackbufferToCurDriverFB() const {
  DoColorMask(0x0f);

  if (mScissorTestEnabled) {
    gl->fDisable(LOCAL_GL_SCISSOR_TEST);
  }

  [&]() {
    const auto& size = mDefaultFB->mSize;

    if (gl->IsSupported(GLFeature::framebuffer_blit)) {
      gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, mDefaultFB->mFB);
      gl->fBlitFramebuffer(0, 0, size.width, size.height, 0, 0, size.width,
                           size.height, LOCAL_GL_COLOR_BUFFER_BIT,
                           LOCAL_GL_NEAREST);
      return;
    }
    if (mDefaultFB->mSamples &&
        gl->IsExtensionSupported(GLContext::APPLE_framebuffer_multisample)) {
      gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, mDefaultFB->mFB);
      gl->fResolveMultisampleFramebufferAPPLE();
      return;
    }

    gl->BlitHelper()->DrawBlitTextureToFramebuffer(mDefaultFB->ColorTex(), size,
                                                   size);
  }();

  if (mScissorTestEnabled) {
    gl->fEnable(LOCAL_GL_SCISSOR_TEST);
  }
}

Maybe<ICRData> WebGLContext::InitializeCanvasRenderer(
    layers::LayersBackend backend) {
  if (!gl) {
    return Nothing();
  }

  ICRData ret;
  ret.size = DrawingBufferSize();
  ret.hasAlpha = mOptions.alpha;
  ret.supportsAlpha = gl->Caps().alpha;
  ret.isPremultAlpha = IsPremultAlpha();

  TextureFlags flags = TextureFlags::ORIGIN_BOTTOM_LEFT;
  if ((!IsPremultAlpha()) && mOptions.alpha) {
    flags |= TextureFlags::NON_PREMULTIPLIED;
  }

  UniquePtr<gl::SurfaceFactory> factory = gl::GLScreenBuffer::CreateFactory(
      gl, gl->Caps(), nullptr, backend, gl->IsANGLE(), flags);
  mBackend = backend;

  if (!factory) {
    return Nothing();
  }

  bool isRemoteHostProcess = !XRE_IsContentProcess();
  if ((factory->mType == SharedSurfaceType::Basic) && isRemoteHostProcess) {
    MOZ_ASSERT_UNREACHABLE("Basic surfaces do not work with remoted WebGL.");
    return Nothing();
  }

  gl->Screen()->Morph(std::move(factory));

  mVRReady = true;
  return Some(ret);
}

// For an overview of how WebGL compositing works, see:
// https://wiki.mozilla.org/Platform/GFX/WebGL/Compositing
bool WebGLContext::PresentScreenBuffer(GLScreenBuffer* const targetScreen) {
  const FuncScope funcScope(*this, "<PresentScreenBuffer>");
  if (IsContextLost()) return false;

  mDrawCallsSinceLastFlush = 0;

  if (!mShouldPresent) return false;

  if (!ValidateAndInitFB(nullptr)) return false;

  const auto& screen = targetScreen ? targetScreen : gl->Screen();
  if ((!screen->IsReadBufferReady() || screen->Size() != mDefaultFB->mSize) &&
      !screen->Resize(mDefaultFB->mSize)) {
    GenerateWarning("screen->Resize failed. Losing context.");
    ForceLoseContext();
    return false;
  }

  gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
  BlitBackbufferToCurDriverFB();

#ifdef DEBUG
  if (!mOptions.alpha) {
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
    uint32_t pixel = 3;
    gl->fReadPixels(0, 0, 1, 1, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, &pixel);
    MOZ_ASSERT((pixel & 0xff000000) == 0xff000000);
  }
#endif

  if (!screen->PublishFrame(screen->Size())) {
    GenerateWarning("PublishFrame failed. Losing context.");
    ForceLoseContext();
    return false;
  }

  if (!mOptions.preserveDrawingBuffer) {
    if (gl->IsSupported(gl::GLFeature::invalidate_framebuffer)) {
      gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mDefaultFB->mFB);
      const GLenum attachments[] = {LOCAL_GL_COLOR_ATTACHMENT0};
      gl->fInvalidateFramebuffer(LOCAL_GL_FRAMEBUFFER, 1, attachments);
    }
    mDefaultFB_IsInvalid = true;
  }
  mResolvedDefaultFB = nullptr;

  mShouldPresent = false;
  OnEndOfFrame();

  return true;
}

RefPtr<DataSourceSurface> GetTempSurface(const IntSize& aSize,
                                         SurfaceFormat& aFormat) {
  uint32_t stride = GetAlignedStride<8>(aSize.width, BytesPerPixel(aFormat));
  return Factory::CreateDataSourceSurfaceWithStride(aSize, aFormat, stride);
}

void WriteFrontToFile(gl::GLContext* gl, GLScreenBuffer* screen,
                      const char* fname, bool needsPremult) {
  auto frontbuffer = screen->Front()->Surf();
  IntSize readSize(frontbuffer->mSize);
  SurfaceFormat format = frontbuffer->mHasAlpha ? SurfaceFormat::B8G8R8A8
                                                : SurfaceFormat::B8G8R8X8;
  RefPtr<DataSourceSurface> resultSurf = GetTempSurface(readSize, format);
  if (NS_WARN_IF(!resultSurf)) {
    MOZ_ASSERT_UNREACHABLE("FAIL");
    return;
  }

  if (!gl->Readback(frontbuffer, resultSurf)) {
    NS_WARNING("Failed to read back canvas surface.");
    MOZ_ASSERT_UNREACHABLE("FAIL");
    return;
  }
  if (needsPremult) {
    gfxUtils::PremultiplyDataSurface(resultSurf, resultSurf);
  }
  MOZ_ASSERT(resultSurf);
  gfxUtils::WriteAsPNG(resultSurf, fname);
}

layers::SurfaceDescriptor WebGLContext::Present() {
  layers::SurfaceDescriptor surfDesc = null_t();

  if (!PresentScreenBuffer()) {
    return surfDesc;
  }

  if (XRE_IsContentProcess()) {
    // That's all!
    return surfDesc;
  }

  // Set the CompositableHost to use the front buffer as the display,
  const auto& screen = gl->Screen();
  if (!screen->Front()->Surf()) {
    GenerateWarning(
        "Present failed due to missing front buffer. Losing context.");
    ForceLoseContext();
    return surfDesc;
  }

  if (mBackend == LayersBackend::LAYERS_NONE) {
    GenerateWarning(
        "Present was not given a valid compositor layer type. Losing context.");
    ForceLoseContext();
    return surfDesc;
  }

  // TODO: Due to an unfortunate initialization process, under some
  // circumstances (that I have not pinned down), we sometimes get here while
  // still holding the placeholder Basic surface created during setup.  This has
  // only been seen when running mochitests.  The underlying SurfaceFactory has
  // already have been replaced.  Note that Basic surfaces are only permitted in
  // non-remoted WebGL, and Present() ends earlier when WebGL is not run
  // remotely (ie when WebGL is run in the content process).
  if (screen->Front()->Surf()->mType == SharedSurfaceType::Basic) {
    NS_WARNING("Attempted to Present surface of Basic type in remoted WebGL.");
    return surfDesc;
  }

  // Hold screen surface until next Present.
  mSurface = screen->Front();
  mSurface->Surf()->ToSurfaceDescriptor(&surfDesc);
  return surfDesc;
}

void WebGLContext::DummyReadFramebufferOperation() {
  if (!mBoundReadFramebuffer) return;  // Infallible.

  const auto status = mBoundReadFramebuffer->CheckFramebufferStatus();
  if (status != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
    ErrorInvalidFramebufferOperation("Framebuffer must be complete.");
  }
}

bool WebGLContext::Has64BitTimestamps() const {
  // 'sync' provides glGetInteger64v either by supporting ARB_sync, GL3+, or
  // GLES3+.
  return gl->IsSupported(GLFeature::sync);
}

static bool CheckContextLost(GLContext* gl, bool* const out_isGuilty) {
  MOZ_ASSERT(gl);

  const auto resetStatus = gl->fGetGraphicsResetStatus();
  if (resetStatus == LOCAL_GL_NO_ERROR) {
    *out_isGuilty = false;
    return false;
  }

  // Assume guilty unless we find otherwise!
  bool isGuilty = true;
  switch (resetStatus) {
    case LOCAL_GL_INNOCENT_CONTEXT_RESET_ARB:
    case LOCAL_GL_PURGED_CONTEXT_RESET_NV:
      // Either nothing wrong, or not our fault.
      isGuilty = false;
      break;
    case LOCAL_GL_GUILTY_CONTEXT_RESET_ARB:
      NS_WARNING(
          "WebGL content on the page definitely caused the graphics"
          " card to reset.");
      break;
    case LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB:
      NS_WARNING(
          "WebGL content on the page might have caused the graphics"
          " card to reset");
      // If we can't tell, assume not-guilty.
      // Todo: Implement max number of "unknown" resets per document or time.
      isGuilty = false;
      break;
    default:
      gfxCriticalError() << "Unexpected glGetGraphicsResetStatus: "
                         << gfx::hexa(resetStatus);
      break;
  }

  if (isGuilty) {
    NS_WARNING(
        "WebGL context on this page is considered guilty, and will"
        " not be restored.");
  }

  *out_isGuilty = isGuilty;
  return true;
}

void WebGLContext::RunContextLossTimer() { mContextLossHandler.RunTimer(); }

class UpdateContextLossStatusTask : public CancelableRunnable {
  RefPtr<WebGLContext> mWebGL;

 public:
  explicit UpdateContextLossStatusTask(WebGLContext* webgl)
      : CancelableRunnable("UpdateContextLossStatusTask"), mWebGL(webgl) {}

  NS_IMETHOD Run() override {
    if (mWebGL) mWebGL->UpdateContextLossStatus();

    return NS_OK;
  }

  nsresult Cancel() override {
    mWebGL = nullptr;
    return NS_OK;
  }
};

void WebGLContext::EnqueueUpdateContextLossStatus() {
  MOZ_ASSERT(MessageLoop::current());
  MessageLoop::current()->PostTask(
      do_AddRef(new UpdateContextLossStatusTask(this)));
}

// We use this timer for many things. Here are the things that it is activated
// for:
// 1) If a script is using the MOZ_WEBGL_lose_context extension.
// 2) If we are using EGL and _NOT ANGLE_, we query periodically to see if the
//    CONTEXT_LOST_WEBGL error has been triggered.
// 3) If we are using ANGLE, or anything that supports ARB_robustness, query the
//    GPU periodically to see if the reset status bit has been set.
// In all of these situations, we use this timer to send the script context lost
// and restored events asynchronously. For example, if it triggers a context
// loss, the webglcontextlost event will be sent to it the next time the
// robustness timer fires.
// Note that this timer mechanism is not used unless one of these 3 criteria are
// met.
// At a bare minimum, from context lost to context restores, it would take 3
// full timer iterations: detection, webglcontextlost, webglcontextrestored.
void WebGLContext::UpdateContextLossStatus() {
  MOZ_ASSERT(mHost);
  mContextLossHandler.ClearTimer();

  if (mContextStatus == ContextStatus::NotLost) {
    // We don't know that we're lost, but we might be, so we need to
    // check. If we're guilty, don't allow restores, though.

    bool isGuilty = true;
    MOZ_ASSERT(gl);  // Shouldn't be missing gl if we're NotLost.
    bool isContextLost = CheckContextLost(gl, &isGuilty);

    if (isContextLost) {
      if (isGuilty) mAllowContextRestore = false;

      ForceLoseContext();
    }

    // Fall through.
  }

  if (mContextStatus == ContextStatus::LostAwaitingEvent) {
    // The context has been lost and we haven't yet triggered the
    // callback, so do that now.
    mHost->OnLostContext();

    // We sent the callback, so we're just 'regular lost' now.
    mContextStatus = ContextStatus::Lost;

    // This is cleared if the context lost event handler permits it
    // (i.e. is not the default handler)
    mDisallowContextRestore = true;
    return;
  }

  if (mContextStatus == ContextStatus::Lost) {
    // Context is lost, and we've already sent the callback. We
    // should try to restore the context if we're both allowed to,
    // and supposed to.

    // Are we allowed to restore the context?
    if (mDisallowContextRestore || (!mAllowContextRestore)) return;

    // If we're only simulated-lost, we shouldn't auto-restore, and
    // instead we should wait for restoreContext() to be called.
    if (mLastLossWasSimulated) return;

    ForceRestoreContext();
    return;
  }

  if (mContextStatus == ContextStatus::LostAwaitingRestore) {
    // Context is lost, but we should try to restore it.

    if (mAllowContextRestore) {
      DoSetDimensionsData sdData =
          DoSetDimensions(mRequestedSize.width, mRequestedSize.height);
      if (NS_FAILED(sdData.result)) {
        // Assume broken forever.
        mAllowContextRestore = false;
      }
    }
    if (!mAllowContextRestore) {
      // We might decide this after thinking we'd be OK restoring
      // the context, so downgrade.
      mContextStatus = ContextStatus::Lost;
      return;
    }

    // Revival!
    mContextStatus = ContextStatus::NotLost;
    mHost->OnRestoredContext();
    return;
  }
}

void WebGLContext::ForceLoseContext(bool simulateLosing) {
  printf_stderr("WebGL(%p)::ForceLoseContext\n", this);
  MOZ_ASSERT(gl);
  mContextStatus = ContextStatus::LostAwaitingEvent;
  mWebGLError = LOCAL_GL_CONTEXT_LOST_WEBGL;

  // Burn it all!
  DestroyResourcesAndContext();
  mLastLossWasSimulated = simulateLosing;

  // Queue up a task, since we know the status changed.
  EnqueueUpdateContextLossStatus();
}

void WebGLContext::ForceRestoreContext() {
  printf_stderr("WebGL(%p)::ForceRestoreContext\n", this);
  mContextStatus = ContextStatus::LostAwaitingRestore;
  mAllowContextRestore = true;  // Hey, you did say 'force'.
  mDisallowContextRestore = false;

  // Queue up a task, since we know the status changed.
  EnqueueUpdateContextLossStatus();
}

UniquePtr<RawSurface> WebGLContext::GetSurfaceSnapshot() {
  const FuncScope funcScope(*this, "<GetSurfaceSnapshot>");
  if (IsContextLost()) return nullptr;

  if (!BindDefaultFBForRead()) return nullptr;

  const auto surfFormat =
      mOptions.alpha ? SurfaceFormat::B8G8R8A8 : SurfaceFormat::B8G8R8X8;
  const auto& size = mDefaultFB->mSize;

  size_t nBytes = size.width * 4 * size.height;
  MOZ_ASSERT(nBytes > 0);
  auto ret = MakeUnique<RawSurface>(size, surfFormat, size.width * 4, nBytes,
                                    new uint8_t[nBytes], true /* ownsData */);
  if (!ret) {
    ErrorOutOfMemory("Failed to create host Surface");
    return nullptr;
  }

  MOZ_ASSERT(ret->HasData());

  RefPtr<DataSourceSurface> dss = Factory::CreateWrappingDataSourceSurface(
      ret->Data(), ret->Stride(), ret->Size(), ret->Format());
  if (NS_WARN_IF(!dss)) return nullptr;

  ReadPixelsIntoDataSurface(gl, dss);
  return ret;
}

void WebGLContext::DidRefresh() {
  if (gl) {
    gl->FlushIfHeavyGLCallsSinceLastFlush();
  }
}

////////////////////////////////////////////////////////////////////////////////

gfx::IntSize WebGLContext::DrawingBufferSize() {
  const gfx::IntSize zeros{0, 0};
  if (IsContextLost()) return zeros;

  if (!EnsureDefaultFB()) return zeros;

  return mDefaultFB->mSize;
}

bool WebGLContext::ValidateAndInitFB(const WebGLFramebuffer* const fb,
                                     const GLenum incompleteFbError) {
  if (fb) return fb->ValidateAndInitAttachments(incompleteFbError);

  if (!EnsureDefaultFB()) return false;

  if (mDefaultFB_IsInvalid) {
    // Clear it!
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mDefaultFB->mFB);
    const webgl::ScopedPrepForResourceClear scopedPrep(*this);
    if (!mOptions.alpha) {
      gl->fClearColor(0, 0, 0, 1);
    }
    const GLbitfield bits = LOCAL_GL_COLOR_BUFFER_BIT |
                            LOCAL_GL_DEPTH_BUFFER_BIT |
                            LOCAL_GL_STENCIL_BUFFER_BIT;
    gl->fClear(bits);

    mDefaultFB_IsInvalid = false;
  }
  return true;
}

void WebGLContext::DoBindFB(const WebGLFramebuffer* const fb,
                            const GLenum target) const {
  const GLenum driverFB = fb ? fb->mGLName : mDefaultFB->mFB;
  gl->fBindFramebuffer(target, driverFB);
}

bool WebGLContext::BindCurFBForDraw() {
  const auto& fb = mBoundDrawFramebuffer;
  if (!ValidateAndInitFB(fb)) return false;

  DoBindFB(fb);
  return true;
}

bool WebGLContext::BindCurFBForColorRead(
    const webgl::FormatUsageInfo** const out_format, uint32_t* const out_width,
    uint32_t* const out_height, const GLenum incompleteFbError) {
  const auto& fb = mBoundReadFramebuffer;

  if (fb) {
    if (!ValidateAndInitFB(fb, incompleteFbError)) return false;
    if (!fb->ValidateForColorRead(out_format, out_width, out_height))
      return false;

    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fb->mGLName);
    return true;
  }

  if (!BindDefaultFBForRead()) return false;

  if (mDefaultFB_ReadBuffer == LOCAL_GL_NONE) {
    ErrorInvalidOperation(
        "Can't read from backbuffer when readBuffer mode is NONE.");
    return false;
  }

  auto effFormat = mOptions.alpha ? webgl::EffectiveFormat::RGBA8
                                  : webgl::EffectiveFormat::RGB8;

  *out_format = mFormatUsage->GetUsage(effFormat);
  MOZ_ASSERT(*out_format);

  *out_width = mDefaultFB->mSize.width;
  *out_height = mDefaultFB->mSize.height;
  return true;
}

bool WebGLContext::BindDefaultFBForRead() {
  if (!ValidateAndInitFB(nullptr)) return false;

  if (!mDefaultFB->mSamples) {
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mDefaultFB->mFB);
    return true;
  }

  if (!mResolvedDefaultFB) {
    mResolvedDefaultFB =
        MozFramebuffer::Create(gl, mDefaultFB->mSize, 0, false);
    if (!mResolvedDefaultFB) {
      gfxCriticalNote << FuncName() << ": Failed to create mResolvedDefaultFB.";
      return false;
    }
  }

  gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mResolvedDefaultFB->mFB);
  BlitBackbufferToCurDriverFB();

  gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mResolvedDefaultFB->mFB);
  return true;
}

void WebGLContext::DoColorMask(const uint8_t bitmask) const {
  if (mDriverColorMask != bitmask) {
    mDriverColorMask = bitmask;
    gl->fColorMask(
        bool(mDriverColorMask & (1 << 0)), bool(mDriverColorMask & (1 << 1)),
        bool(mDriverColorMask & (1 << 2)), bool(mDriverColorMask & (1 << 3)));
  }
}

////////////////////////////////////////////////////////////////////////////////

ScopedDrawCallWrapper::ScopedDrawCallWrapper(WebGLContext& webgl)
    : mWebGL(webgl) {
  uint8_t driverColorMask = mWebGL.mColorWriteMask;
  bool driverDepthTest = mWebGL.mDepthTestEnabled;
  bool driverStencilTest = mWebGL.mStencilTestEnabled;
  const auto& fb = mWebGL.mBoundDrawFramebuffer;
  if (!fb) {
    if (mWebGL.mDefaultFB_DrawBuffer0 == LOCAL_GL_NONE) {
      driverColorMask = 0;  // Is this well-optimized enough for depth-first
                            // rendering?
    } else {
      driverColorMask &= ~(uint8_t(mWebGL.mNeedsFakeNoAlpha) << 3);
    }
    driverDepthTest &= !mWebGL.mNeedsFakeNoDepth;
    driverStencilTest &= !mWebGL.mNeedsFakeNoStencil;
  } else {
    if (mWebGL.mNeedsFakeNoStencil_UserFBs &&
        fb->DepthAttachment().HasAttachment() &&
        !fb->StencilAttachment().HasAttachment()) {
      driverStencilTest = false;
    }
  }

  const auto& gl = mWebGL.gl;
  mWebGL.DoColorMask(driverColorMask);
  if (mWebGL.mDriverDepthTest != driverDepthTest) {
    // "When disabled, the depth comparison and subsequent possible updates to
    // the
    //  depth buffer value are bypassed and the fragment is passed to the next
    //  operation." [GLES 3.0.5, p177]
    mWebGL.mDriverDepthTest = driverDepthTest;
    gl->SetEnabled(LOCAL_GL_DEPTH_TEST, mWebGL.mDriverDepthTest);
  }
  if (mWebGL.mDriverStencilTest != driverStencilTest) {
    // "When disabled, the stencil test and associated modifications are not
    // made, and
    //  the fragment is always passed." [GLES 3.0.5, p175]
    mWebGL.mDriverStencilTest = driverStencilTest;
    gl->SetEnabled(LOCAL_GL_STENCIL_TEST, mWebGL.mDriverStencilTest);
  }
}

ScopedDrawCallWrapper::~ScopedDrawCallWrapper() {
  if (mWebGL.mBoundDrawFramebuffer) return;

  mWebGL.mResolvedDefaultFB = nullptr;
  mWebGL.mShouldPresent = true;
}

// -

void WebGLContext::ScissorRect::Apply(gl::GLContext& gl) const {
  gl.fScissor(x, y, w, h);
}

////////////////////////////////////////

IndexedBufferBinding::IndexedBufferBinding() : mRangeStart(0), mRangeSize(0) {}

uint64_t IndexedBufferBinding::ByteCount() const {
  if (!mBufferBinding) return 0;

  uint64_t bufferSize = mBufferBinding->ByteLength();
  if (!mRangeSize)  // BindBufferBase
    return bufferSize;

  if (mRangeStart >= bufferSize) return 0;
  bufferSize -= mRangeStart;

  return std::min(bufferSize, mRangeSize);
}

////////////////////////////////////////

ScopedUnpackReset::ScopedUnpackReset(const WebGLContext* const webgl)
    : ScopedGLWrapper<ScopedUnpackReset>(webgl->gl), mWebGL(webgl) {
  // clang-format off
    if (mWebGL->mPixelStore.mUnpackAlignment != 4) mGL->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 4);

    if (mWebGL->IsWebGL2()) {
        if (mWebGL->mPixelStore.mUnpackRowLength   != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH  , 0);
        if (mWebGL->mPixelStore.mUnpackImageHeight != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT, 0);
        if (mWebGL->mPixelStore.mUnpackSkipPixels  != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_PIXELS , 0);
        if (mWebGL->mPixelStore.mUnpackSkipRows    != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS   , 0);
        if (mWebGL->mPixelStore.mUnpackSkipImages  != 0) mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES , 0);

        if (mWebGL->mBoundPixelUnpackBuffer) mGL->fBindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, 0);
    }
  // clang-format on
}

void ScopedUnpackReset::UnwrapImpl() {
  // clang-format off
    mGL->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, mWebGL->mPixelStore.mUnpackAlignment);

    if (mWebGL->IsWebGL2()) {
        mGL->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH  , mWebGL->mPixelStore.mUnpackRowLength  );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT, mWebGL->mPixelStore.mUnpackImageHeight);
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_PIXELS , mWebGL->mPixelStore.mUnpackSkipPixels );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS   , mWebGL->mPixelStore.mUnpackSkipRows   );
        mGL->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES , mWebGL->mPixelStore.mUnpackSkipImages );

        GLuint pbo = 0;
        if (mWebGL->mBoundPixelUnpackBuffer) {
            pbo = mWebGL->mBoundPixelUnpackBuffer->mGLName;
        }

        mGL->fBindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, pbo);
    }
  // clang-format on
}

////////////////////

void ScopedFBRebinder::UnwrapImpl() {
  const auto fnName = [&](WebGLFramebuffer* fb) {
    return fb ? fb->mGLName : 0;
  };

  if (mWebGL->IsWebGL2()) {
    mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                          fnName(mWebGL->mBoundDrawFramebuffer));
    mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER,
                          fnName(mWebGL->mBoundReadFramebuffer));
  } else {
    MOZ_ASSERT(mWebGL->mBoundDrawFramebuffer == mWebGL->mBoundReadFramebuffer);
    mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER,
                          fnName(mWebGL->mBoundDrawFramebuffer));
  }
}

////////////////////

static GLenum TargetIfLazy(GLenum target) {
  switch (target) {
    case LOCAL_GL_PIXEL_PACK_BUFFER:
    case LOCAL_GL_PIXEL_UNPACK_BUFFER:
      return target;

    default:
      return 0;
  }
}

ScopedLazyBind::ScopedLazyBind(gl::GLContext* gl, GLenum target,
                               const WebGLBuffer* buf)
    : ScopedGLWrapper<ScopedLazyBind>(gl),
      mTarget(buf ? TargetIfLazy(target) : 0),
      mBuf(buf) {
  if (mTarget) {
    mGL->fBindBuffer(mTarget, mBuf->mGLName);
  }
}

void ScopedLazyBind::UnwrapImpl() {
  if (mTarget) {
    mGL->fBindBuffer(mTarget, 0);
  }
}

////////////////////////////////////////

bool Intersect(const int32_t srcSize, const int32_t read0,
               const int32_t readSize, int32_t* const out_intRead0,
               int32_t* const out_intWrite0, int32_t* const out_intSize) {
  MOZ_ASSERT(srcSize >= 0);
  MOZ_ASSERT(readSize >= 0);
  const auto read1 = int64_t(read0) + readSize;

  int32_t intRead0 = read0;  // Clearly doesn't need validation.
  int64_t intWrite0 = 0;
  int64_t intSize = readSize;

  if (read1 <= 0 || read0 >= srcSize) {
    // Disjoint ranges.
    intSize = 0;
  } else {
    if (read0 < 0) {
      const auto diff = int64_t(0) - read0;
      MOZ_ASSERT(diff >= 0);
      intRead0 = 0;
      intWrite0 = diff;
      intSize -= diff;
    }
    if (read1 > srcSize) {
      const auto diff = int64_t(read1) - srcSize;
      MOZ_ASSERT(diff >= 0);
      intSize -= diff;
    }

    if (!CheckedInt<int32_t>(intWrite0).isValid() ||
        !CheckedInt<int32_t>(intSize).isValid()) {
      return false;
    }
  }

  *out_intRead0 = intRead0;
  *out_intWrite0 = intWrite0;
  *out_intSize = intSize;
  return true;
}

// --

uint64_t AvailGroups(const uint64_t totalAvailItems,
                     const uint64_t firstItemOffset, const uint32_t groupSize,
                     const uint32_t groupStride) {
  MOZ_ASSERT(groupSize && groupStride);
  MOZ_ASSERT(groupSize <= groupStride);

  if (totalAvailItems <= firstItemOffset) return 0;
  const size_t availItems = totalAvailItems - firstItemOffset;

  size_t availGroups = availItems / groupStride;
  const size_t tailItems = availItems % groupStride;
  if (tailItems >= groupSize) {
    availGroups += 1;
  }
  return availGroups;
}

////////////////////////////////////////////////////////////////////////////////

CheckedUint32 WebGLContext::GetUnpackSize(bool isFunc3D, uint32_t width,
                                          uint32_t height, uint32_t depth,
                                          uint8_t bytesPerPixel) {
  if (!width || !height || !depth) return 0;

  ////////////////

  const auto& maybeRowLength = mPixelStore.mUnpackRowLength;
  const auto& maybeImageHeight = mPixelStore.mUnpackImageHeight;

  const auto usedPixelsPerRow =
      CheckedUint32(mPixelStore.mUnpackSkipPixels) + width;
  const auto stridePixelsPerRow =
      (maybeRowLength ? CheckedUint32(maybeRowLength) : usedPixelsPerRow);

  const auto usedRowsPerImage =
      CheckedUint32(mPixelStore.mUnpackSkipRows) + height;
  const auto strideRowsPerImage =
      (maybeImageHeight ? CheckedUint32(maybeImageHeight) : usedRowsPerImage);

  const uint32_t skipImages = (isFunc3D ? mPixelStore.mUnpackSkipImages : 0);
  const CheckedUint32 usedImages = CheckedUint32(skipImages) + depth;

  ////////////////

  CheckedUint32 strideBytesPerRow = bytesPerPixel * stridePixelsPerRow;
  strideBytesPerRow =
      RoundUpToMultipleOf(strideBytesPerRow, mPixelStore.mUnpackAlignment);

  const CheckedUint32 strideBytesPerImage =
      strideBytesPerRow * strideRowsPerImage;

  ////////////////

  CheckedUint32 usedBytesPerRow = bytesPerPixel * usedPixelsPerRow;
  // Don't round this to the alignment, since alignment here is really just used
  // for establishing stride, particularly in WebGL 1, where you can't set
  // ROW_LENGTH.

  CheckedUint32 totalBytes = strideBytesPerImage * (usedImages - 1);
  totalBytes += strideBytesPerRow * (usedRowsPerImage - 1);
  totalBytes += usedBytesPerRow;

  return totalBytes;
}

#if defined(MOZ_WIDGET_ANDROID)
layers::SurfaceDescriptor WebGLContext::PrepareVRFrame() {
  layers::SurfaceDescriptor surfDesc = null_t();
  if (!gl) {
    return surfDesc;
  }

  EnsureVRReady();

  // Create a custom GLScreenBuffer for VR.
  if (!mVRScreen) {
    auto caps = gl->Screen()->mCaps;
    mVRScreen = GLScreenBuffer::Create(gl, gfx::IntSize(1, 1), caps);
  }

  MOZ_ASSERT(mVRScreen);

  // Swap buffers as though composition has occurred.
  // We will then share the resulting front buffer to be submitted to the VR
  // compositor.
  PresentScreenBuffer(mVRScreen.get());

  if (IsContextLost()) {
    return surfDesc;
  }

  // Keep the SharedSurfaceTextureClient alive long enough for
  // 1 extra frame, accomodating overlapped asynchronous rendering.
  mLastVRSurface = mSurface;

  mSurface = mVRScreen->Front();
  if (!mSurface || !mSurface->Surf()) {
    return surfDesc;
  }

  // Make sure that the WebGL buffer is committed to the attached SurfaceTexture
  // on Android.
  mSurface->Surf()->ProducerAcquire();
  mSurface->Surf()->Commit();
  mSurface->Surf()->ProducerRelease();

  mSurface->Surf()->ToSurfaceDescriptor(&surfDesc);
  return surfDesc;
}
#else
SurfaceDescriptor WebGLContext::PrepareVRFrame() {
  SurfaceDescriptor surfDesc;
  if (!gl) {
    return surfDesc;
  }

  EnsureVRReady();
  /**
   * Swap buffers as though composition has occurred.
   * We will then share the resulting front buffer to be submitted to the VR
   * compositor.
   */
  PresentScreenBuffer();

  gl::GLScreenBuffer* screen = gl->Screen();
  if (!screen) {
    return surfDesc;
  }

  // Keep the SharedSurfaceTextureClient alive long enough for
  // 1 extra frame, accomodating overlapped asynchronous rendering.
  mLastVRSurface = mSurface;

  mSurface = screen->Front();
  if (!mSurface || !mSurface->Surf()) {
    return surfDesc;
  }

  mSurface->Surf()->ToSurfaceDescriptor(&surfDesc);
  return surfDesc;
}
#endif  // ifdefined(MOZ_WIDGET_ANDROID)

void WebGLContext::EnsureVRReady() {
  if (mVRReady) {
    return;
  }

  // Make not composited canvases work with WebVR. See bug #1492554
  // WebGLContext::InitializeCanvasRenderer is only called when the 2D
  // compositor renders a WebGL canvas for the first time. This causes canvases
  // not added to the DOM not to work properly with WebVR. Here we mimic what
  // InitializeCanvasRenderer does internally as a workaround.
  const auto caps = gl->Screen()->mCaps;
  auto flags = TextureFlags::ORIGIN_BOTTOM_LEFT;
  if (!IsPremultAlpha() && mOptions.alpha) {
    flags |= TextureFlags::NON_PREMULTIPLIED;
  }
  auto factory = gl::GLScreenBuffer::CreateFactory(gl, caps, nullptr, flags);
  gl->Screen()->Morph(std::move(factory));
#if defined(MOZ_WIDGET_ANDROID)
  // On Android we are using a different GLScreenBuffer for WebVR, so we need
  // a resize here because PresentScreenBuffer() may not be called for the
  // gl->Screen() after we set the new factory.
  gl->Screen()->Resize(DrawingBufferSize());
#endif
  mVRReady = true;
}

////////////////////////////////////////////////////////////////////////////////

static inline size_t SizeOfViewElem(const dom::ArrayBufferView& view) {
  const auto& elemType = view.Type();
  if (elemType == js::Scalar::MaxTypedArrayViewType)  // DataViews.
    return 1;

  return js::Scalar::byteSize(elemType);
}

bool WebGLContext::ValidateArrayBufferView(const dom::ArrayBufferView& view,
                                           GLuint elemOffset,
                                           GLuint elemCountOverride,
                                           const GLenum errorEnum,
                                           uint8_t** const out_bytes,
                                           size_t* const out_byteLen) const {
  view.ComputeLengthAndData();
  uint8_t* const bytes = view.DataAllowShared();
  const size_t byteLen = view.LengthAllowShared();

  const auto& elemSize = SizeOfViewElem(view);

  size_t elemCount = byteLen / elemSize;
  if (elemOffset > elemCount) {
    GenerateError(errorEnum, "Invalid offset into ArrayBufferView.");
    return false;
  }
  elemCount -= elemOffset;

  if (elemCountOverride) {
    if (elemCountOverride > elemCount) {
      GenerateError(errorEnum, "Invalid sub-length for ArrayBufferView.");
      return false;
    }
    elemCount = elemCountOverride;
  }

  *out_bytes = bytes + (elemOffset * elemSize);
  *out_byteLen = elemCount * elemSize;
  return true;
}

bool ClientWebGLContext::ValidateArrayBufferView(
    const dom::ArrayBufferView& view, GLuint elemOffset,
    GLuint elemCountOverride, const GLenum errorEnum, uint8_t** const out_bytes,
    size_t* const out_byteLen, bool allowZeroLengthResult) const {
  view.ComputeLengthAndData();
  uint8_t* const bytes = view.DataAllowShared();
  const size_t byteLen = view.LengthAllowShared();

  const auto& elemSize = SizeOfViewElem(view);

  size_t elemCount = byteLen / elemSize;
  if (elemOffset > elemCount) {
    EnqueueErrorPrintf(errorEnum, "Invalid offset into ArrayBufferView.");
    return false;
  }
  elemCount -= elemOffset;

  if (elemCountOverride) {
    if (elemCountOverride > elemCount) {
      EnqueueErrorPrintf(errorEnum, "Invalid sub-length for ArrayBufferView.");
      return false;
    }
    elemCount = elemCountOverride;
  }

  if (!allowZeroLengthResult && (0 == elemCount)) {
    EnqueueErrorPrintf(errorEnum, "Zero-length array in ArrayBufferView.");
    return false;
  }

  *out_bytes = bytes + (elemOffset * elemSize);
  *out_byteLen = elemCount * elemSize;
  return true;
}

////

void WebGLContext::UpdateMaxDrawBuffers() {
  mGLMaxColorAttachments =
      gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_COLOR_ATTACHMENTS);
  mGLMaxDrawBuffers = gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_DRAW_BUFFERS);

  // WEBGL_draw_buffers:
  // "The value of the MAX_COLOR_ATTACHMENTS_WEBGL parameter must be greater
  // than or
  //  equal to that of the MAX_DRAW_BUFFERS_WEBGL parameter."
  mGLMaxDrawBuffers = std::min(mGLMaxDrawBuffers, mGLMaxColorAttachments);
}

// --

const char* WebGLContext::FuncName() const {
  const char* ret;
  if (MOZ_LIKELY(mFuncScope)) {
    ret = mFuncScope->mFuncName;
  } else {
    MOZ_ASSERT(false);
    ret = "<funcName unknown>";
  }
  return ret;
}

// -

WebGLContext::FuncScope::FuncScope(const WebGLContext& webgl,
                                   const char* const funcName)
    : mWebGL(webgl), mFuncName(bool(mWebGL.mFuncScope) ? nullptr : funcName) {
  if (MOZ_UNLIKELY(!mFuncName)) {
#ifdef DEBUG
    mStillNeedsToCheckContextLost = false;
#endif
    return;
  }

  mWebGL.mFuncScope = this;
}

WebGLContext::FuncScope::~FuncScope() {
  if (MOZ_UNLIKELY(!mFuncName)) return;

  MOZ_ASSERT(!mStillNeedsToCheckContextLost);
  mWebGL.mFuncScope = nullptr;
}

bool WebGLContext::IsContextLost() const {
  if (MOZ_LIKELY(mFuncScope)) {
    mFuncScope->OnCheckContextLost();
  }
  return mContextStatus != ContextStatus::NotLost;
}

// --

void WebGLContext::MakeQueriesAndSyncsAvailable() {
  for (const auto& cur : mUnavailableQueries) {
    cur->mCanBeAvailable = true;
  }
  mUnavailableQueries.clear();

  for (const auto& cur : mUnavailableSyncs) {
    cur->mCanBeAvailable = true;
  }
  mUnavailableSyncs.clear();
}

}  // namespace mozilla
