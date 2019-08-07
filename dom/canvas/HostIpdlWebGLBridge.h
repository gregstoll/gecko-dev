/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HOSTIPDLWEBGLBRIDGE_H_
#define HOSTIPDLWEBGLBRIDGE_H_

#include "mozilla/WeakPtr.h"
#include "mozilla/dom/WebGLParent.h"

namespace mozilla {

/**
 * We need to perform the WebGLParent operations on the compositor thread
 * because that is the IPDL actor thread.  We need to perform the
 * HostWebGLContext operations on the renderer thread because GL is not
 * thread-safe and the renderer thread already uses GL.  By our
 * ownership model, the WebGLParent owns the HostWebGLContext.  We use a
 * HostIpdlWebGLBridge to make sure that the HostWebGLContext is not
 * accessed on the wrong thread, even for construction/destruction.
 * This object is owned by the WebGLParent.
 */
class HostIpdlWebGLBridge {
 public:
  HostIpdlWebGLBridge(WeakPtr<dom::WebGLParent>& weakParent,
                      WebGLVersion aVersion,
                      UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
                      UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HostIpdlWebGLBridge)

  static WebGLGfxFeatures GetWebGLFeatures();

  void Destroy();

 private:
  static MessageLoop* WebGLRenderThreadMessageLoop();
  static MessageLoop* WebGLIpdlThreadMessageLoop();

  template <typename MethodType, typename... Args>
  void DispatchToRenderThread(MethodType method, Args... args);

  template <typename MethodType, typename... Args>
  void DispatchToIpdlThread(MethodType method, Args... args);

  void Construct(WebGLVersion aVersion,
                 UniquePtr<mozilla::HostWebGLCommandSink>&& aCommandSink,
                 UniquePtr<mozilla::HostWebGLErrorSource>&& aErrorSource,
                 WebGLGfxFeatures aFeatures);

  bool BeginCommandQueueDrain();
  bool RunCommandQueue();
  void SendQueueFailed();

  ~HostIpdlWebGLBridge();

  // We force access to parent and host through these getters in order to
  // guarantee thread-safety.  Note that these objects are only accessible
  // on one thread so, if a thread has access to these methods then it can
  // be certain that the underlying object will not be simultaneously
  // tampered with by another thread.
  // The WebGLParent must only be accessed on the IPDL thread.
  WeakPtr<dom::WebGLParent>& GetWeakGLParent();
  // The HostWebGLContext must only be accessed on the Renderer thread.
  UniquePtr<HostWebGLContext>& GetHost();

  // Only the friend functions can access these members directly.
  class {
    WeakPtr<dom::WebGLParent> mWeakGLParent;
    UniquePtr<HostWebGLContext> mHost;
    friend WeakPtr<dom::WebGLParent>& HostIpdlWebGLBridge::GetWeakGLParent();
    friend UniquePtr<HostWebGLContext>& HostIpdlWebGLBridge::GetHost();
  } mMembers;

  bool mShouldDestroy = false;

  // Runnable that repeatedly processes our WebGL command queue
  RefPtr<Runnable> mRunCommandsRunnable;
};

}  // namespace mozilla

#endif  // HOSTIPDLWEBGLBRIDGE_H_
