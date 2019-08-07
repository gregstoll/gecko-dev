/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HostWebGLContext.h"

#include "CompositableHost.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/webrender/RenderThread.h"

#include "TexUnpackBlob.h"
#include "WebGL1Context.h"
#include "WebGL2Context.h"
#include "WebGLBuffer.h"
#include "WebGLCrossProcessCommandQueue.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLSync.h"
#include "WebGLTexture.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "WebGLUniformLocation.h"
#include "WebGLQuery.h"

namespace mozilla {

LazyLogModule gWebGLBridgeLog("webglbridge");

#define DEFINE_OBJECT_ID_MAP_FUNCS(_WebGLType)                                 \
  WebGLId<WebGL##_WebGLType> HostWebGLContext::Insert(                         \
      RefPtr<WebGL##_WebGLType>&& aObj, const WebGLId<WebGL##_WebGLType>& aId) \
      const {                                                                  \
    return m##_WebGLType##Map.Insert(std::move(aObj), aId);                    \
  }                                                                            \
  bool HostWebGLContext::Find(const WebGLId<WebGL##_WebGLType>& aId,           \
                              RefPtr<WebGL##_WebGLType>& aReturnObj,           \
                              const char* aCmdName) const {                    \
    if (!aId.IsValid()) {                                                      \
      const WebGLContext::FuncScope scope(*mContext, aCmdName);                \
      Unused << mContext->IsContextLost();                                     \
      mContext->ErrorInvalidOperation(                                         \
          "Object from a different WebGL context (or older generation of "     \
          "this one) was passed as argument.");                                \
      return false;                                                            \
    }                                                                          \
    aReturnObj = m##_WebGLType##Map.Find(aId);                                 \
    return true;                                                               \
  }                                                                            \
  void HostWebGLContext::Remove(const WebGLId<WebGL##_WebGLType>& aId) const { \
    return m##_WebGLType##Map.Remove(aId);                                     \
  }

DEFINE_OBJECT_ID_MAP_FUNCS(Framebuffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Program);
DEFINE_OBJECT_ID_MAP_FUNCS(Query);
DEFINE_OBJECT_ID_MAP_FUNCS(Renderbuffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Sampler);
DEFINE_OBJECT_ID_MAP_FUNCS(Shader);
DEFINE_OBJECT_ID_MAP_FUNCS(Sync);
DEFINE_OBJECT_ID_MAP_FUNCS(TransformFeedback);
DEFINE_OBJECT_ID_MAP_FUNCS(VertexArray);
DEFINE_OBJECT_ID_MAP_FUNCS(Buffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Texture);
DEFINE_OBJECT_ID_MAP_FUNCS(UniformLocation);

// Use this when failure to find an object by ID indicates that an illegal
// object was given (i.e. the user passed null or an object from another
// WebGL context or from another generation of this context).  This method
// will generate an error and return null in that case.
#define MustFind(aId) FindOrError(aId, __func__)

// Like MustFind except that this will not generate an error if the
// original parameter was NULL.
#define MaybeFind(aId, aReturnObj) Find(aId, aReturnObj, __func__)

/* static */ WebGLContext* HostWebGLContext::MakeWebGLContext(
    WebGLVersion aVersion, const WebGLGfxFeatures& aFeatures) {
  switch (aVersion) {
    case WEBGL1:
      return WebGL1Context::Create(aFeatures);
    case WEBGL2:
      return WebGL2Context::Create(aFeatures);
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal WebGLVersion");
      return nullptr;
  }
}

HostWebGLContext::HostWebGLContext(
    WebGLVersion aVersion, const WebGLGfxFeatures& aFeatures,
    RefPtr<WebGLContext> aContext,
    UniquePtr<HostWebGLCommandSink>&& aCommandSink,
    UniquePtr<HostWebGLErrorSource>&& aErrorSource)
    : WebGLContextEndpoint(aVersion),
      mCommandSink(std::move(aCommandSink)),
      mErrorSource(std::move(aErrorSource)),
      mSetPreferences(false),
      mContext(aContext),
      mClientContext(nullptr) {
  MOZ_ASSERT(IsWebGLRenderThread());

  mContext->SetHost(this);
  if (mCommandSink) {
    mCommandSink->SetHostContext(this);
  }
}

HostWebGLContext::~HostWebGLContext() {
  MOZ_ASSERT(IsWebGLRenderThread());
  if (mContext) {
    mContext->SetHost(nullptr);
  }
}

/* static */ HostWebGLContext* HostWebGLContext::Create(
    WebGLVersion aVersion, const WebGLGfxFeatures& aFeatures,
    UniquePtr<HostWebGLCommandSink>&& aCommandSink,
    UniquePtr<HostWebGLErrorSource>&& aErrorSource) {
  WebGLContext* context = MakeWebGLContext(aVersion, aFeatures);
  if (!context) {
    return nullptr;
  }
  return new HostWebGLContext(aVersion, aFeatures, context,
                              std::move(aCommandSink), std::move(aErrorSource));
}

CommandResult HostWebGLContext::RunCommandsForDuration(TimeDuration aDuration) {
  MOZ_ASSERT(IsWebGLRenderThread());
  return mCommandSink->ProcessUpToDuration(aDuration);
}

/* static */ bool HostWebGLContext::IsWebGLRenderThread() {
  // If this context is not remote then we should be on the main thread.
  if (XRE_IsContentProcess()) {
    return NS_IsMainThread();
  }

  // The context must be on the GPU process.  If we are using WebRender
  // then this is the Renderer thread.  Otherwise it is the Compositor thread.
  MOZ_ASSERT(XRE_IsGPUProcess() || XRE_IsParentProcess());

  // TODO: A better test for whether or not to use WebRender?
  bool useWR = wr::RenderThread::Get();
  if (useWR) {
    return wr::RenderThread::IsInRenderThread();
  }

  return layers::CompositorThreadHolder::IsInCompositorThread();
}

/* static */ MessageLoop* HostWebGLContext::WebGLRenderThreadMessageLoop() {
  if (XRE_IsContentProcess()) {
    return layers::CompositorBridgeChild::Get()
               ? layers::CompositorBridgeChild::Get()->GetMessageLoop()
               : nullptr;
  }

  MOZ_ASSERT(XRE_IsGPUProcess() || XRE_IsParentProcess());
  return wr::RenderThread::Get() ? wr::RenderThread::Loop()
                                 : layers::CompositorThreadHolder::Loop();
}

layers::SurfaceDescriptor HostWebGLContext::Present() {
  return mContext->Present();
}

SurfaceDescriptor HostWebGLContext::PrepareVRFrame() {
  return mContext->PrepareVRFrame();
}

void HostWebGLContext::CreateFramebuffer(const WebGLId<WebGLFramebuffer>& aId) {
  Insert(mContext->CreateFramebuffer(), aId);
}

void HostWebGLContext::CreateProgram(const WebGLId<WebGLProgram>& aId) {
  Insert(mContext->CreateProgram(), aId);
}

void HostWebGLContext::CreateRenderbuffer(
    const WebGLId<WebGLRenderbuffer>& aId) {
  Insert(mContext->CreateRenderbuffer(), aId);
}

void HostWebGLContext::CreateShader(GLenum aType,
                                    const WebGLId<WebGLShader>& aId) {
  Insert(mContext->CreateShader(aType), aId);
}

WebGLId<WebGLUniformLocation> HostWebGLContext::GetUniformLocation(
    const WebGLId<WebGLProgram>& progId, const nsString& name) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return WebGLId<WebGLUniformLocation>::Invalid();
  }
  auto ret =
      RefPtr<WebGLUniformLocation>(mContext->GetUniformLocation(*prog, name));
  if (!ret) {
    return WebGLId<WebGLUniformLocation>::Null();
  }
  return Insert(std::move(ret));
}

WebGLId<WebGLBuffer> HostWebGLContext::CreateBuffer() {
  return Insert(RefPtr<WebGLBuffer>(mContext->CreateBuffer()));
}

WebGLId<WebGLTexture> HostWebGLContext::CreateTexture() {
  return Insert(RefPtr<WebGLTexture>(mContext->CreateTexture()));
}

void HostWebGLContext::CreateSampler(const WebGLId<WebGLSampler>& aId) {
  Insert(GetWebGL2Context()->CreateSampler(), aId);
}

WebGLId<WebGLSync> HostWebGLContext::FenceSync(const WebGLId<WebGLSync>& aId,
                                               GLenum condition,
                                               GLbitfield flags) {
  return Insert(GetWebGL2Context()->FenceSync(condition, flags), aId);
}

void HostWebGLContext::CreateTransformFeedback(
    const WebGLId<WebGLTransformFeedback>& aId) {
  Insert(GetWebGL2Context()->CreateTransformFeedback(), aId);
}

void HostWebGLContext::CreateVertexArray(const WebGLId<WebGLVertexArray>& aId,
                                         bool aFromExtension) {
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::OES_vertex_array_object>();
    MOZ_RELEASE_ASSERT(ext);
    Insert(ext->CreateVertexArrayOES(), aId);
    return;
  }

  Insert(mContext->CreateVertexArray(), aId);
}

void HostWebGLContext::CreateQuery(const WebGLId<WebGLQuery>& aId,
                                   bool aFromExtension) const {
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    Insert(ext->CreateQueryEXT(), aId);
    return;
  }

  Insert(const_cast<WebGL2Context*>(GetWebGL2Context())->CreateQuery(), aId);
}

// ------------------------- Composition -------------------------
Maybe<ICRData> HostWebGLContext::InitializeCanvasRenderer(
    layers::LayersBackend backend) {
  return mContext->InitializeCanvasRenderer(backend);
}

void HostWebGLContext::SetContextOptions(const WebGLContextOptions& options) {
  mContext->SetOptions(options);
}

void HostWebGLContext::SetPreferences(const WebGLPreferences& aPrefs) {
  // Make sure we only set the preferences once.
  if (mSetPreferences) {
    return;
  }
  mContext->SetPreferences(aPrefs);
  mSetPreferences = true;
}

SetDimensionsData HostWebGLContext::SetDimensions(int32_t signedWidth,
                                                  int32_t signedHeight) {
  return mContext->SetDimensions(signedWidth, signedHeight);
}

gfx::IntSize HostWebGLContext::DrawingBufferSize(FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  return mContext->DrawingBufferSize();
}

void HostWebGLContext::OnMemoryPressure() {
  return mContext->OnMemoryPressure();
}

void HostWebGLContext::AllowContextRestore() {
  mContext->AllowContextRestore();
}

void HostWebGLContext::DidRefresh() { mContext->DidRefresh(); }

UniquePtr<RawSurface> HostWebGLContext::GetSurfaceSnapshot(
    FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  return mContext->GetSurfaceSnapshot();
}
// ------------------------- GL State -------------------------
bool HostWebGLContext::IsContextLost() const {
  return mContext->IsContextLost();
}

void HostWebGLContext::Disable(GLenum cap) { mContext->Disable(cap); }

void HostWebGLContext::Enable(GLenum cap) { mContext->Enable(cap); }

bool HostWebGLContext::IsEnabled(GLenum cap) {
  return mContext->IsEnabled(cap);
}

MaybeWebGLVariant HostWebGLContext::GetParameter(GLenum pname) {
  return mContext->GetParameter(pname);
}

void HostWebGLContext::AttachShader(const WebGLId<WebGLProgram>& progId,
                                    const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if ((!prog) || (!shader)) {
    return;
  }
  mContext->AttachShader(*prog, *shader);
}

void HostWebGLContext::BindAttribLocation(const WebGLId<WebGLProgram>& progId,
                                          GLuint location,
                                          const nsString& name) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return;
  }
  mContext->BindAttribLocation(*prog, location, name);
}

void HostWebGLContext::BindFramebuffer(GLenum target,
                                       const WebGLId<WebGLFramebuffer>& fbId) {
  RefPtr<WebGLFramebuffer> fb;
  if (!MaybeFind(fbId, fb)) {
    return;
  }
  mContext->BindFramebuffer(target, fb);
}

void HostWebGLContext::BindRenderbuffer(
    GLenum target, const WebGLId<WebGLRenderbuffer>& rbId) {
  RefPtr<WebGLRenderbuffer> rb;
  if (!MaybeFind(rbId, rb)) {
    return;
  }
  mContext->BindRenderbuffer(target, rb);
}

void HostWebGLContext::BlendColor(GLclampf r, GLclampf g, GLclampf b,
                                  GLclampf a) {
  mContext->BlendColor(r, g, b, a);
}

void HostWebGLContext::BlendEquation(GLenum mode) {
  mContext->BlendEquation(mode);
}

void HostWebGLContext::BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  mContext->BlendEquationSeparate(modeRGB, modeAlpha);
}

void HostWebGLContext::BlendFunc(GLenum sfactor, GLenum dfactor) {
  mContext->BlendFunc(sfactor, dfactor);
}

void HostWebGLContext::BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB,
                                         GLenum srcAlpha, GLenum dstAlpha) {
  mContext->BlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GLenum HostWebGLContext::CheckFramebufferStatus(GLenum target) {
  return mContext->CheckFramebufferStatus(target);
}

void HostWebGLContext::Clear(GLbitfield mask) { mContext->Clear(mask); }

void HostWebGLContext::ClearColor(GLclampf r, GLclampf g, GLclampf b,
                                  GLclampf a) {
  mContext->ClearColor(r, g, b, a);
}

void HostWebGLContext::ClearDepth(GLclampf v) { mContext->ClearDepth(v); }

void HostWebGLContext::ClearStencil(GLint v) { mContext->ClearStencil(v); }

void HostWebGLContext::ColorMask(WebGLboolean r, WebGLboolean g, WebGLboolean b,
                                 WebGLboolean a) {
  mContext->ColorMask(r, g, b, a);
}

void HostWebGLContext::CompileShader(const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return;
  }
  mContext->CompileShader(*shader);
}

void HostWebGLContext::CullFace(GLenum face) { mContext->CullFace(face); }

void HostWebGLContext::DeleteFramebuffer(
    const WebGLId<WebGLFramebuffer>& fbId) {
  RefPtr<WebGLFramebuffer> fb;
  if (!MaybeFind(fbId, fb)) {
    return;
  }
  mContext->DeleteFramebuffer(fb);
}

void HostWebGLContext::DeleteProgram(const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog;
  if (!MaybeFind(progId, prog)) {
    return;
  }
  mContext->DeleteProgram(prog);
}

void HostWebGLContext::DeleteRenderbuffer(
    const WebGLId<WebGLRenderbuffer>& rbId) {
  RefPtr<WebGLRenderbuffer> rb;
  if (!MaybeFind(rbId, rb)) {
    return;
  }
  mContext->DeleteRenderbuffer(rb);
}

void HostWebGLContext::DeleteShader(const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLShader> shader;
  if (!MaybeFind(shaderId, shader)) {
    return;
  }
  mContext->DeleteShader(shader);
}

void HostWebGLContext::DepthFunc(GLenum func) { mContext->DepthFunc(func); }

void HostWebGLContext::DepthMask(WebGLboolean b) { mContext->DepthMask(b); }

void HostWebGLContext::DepthRange(GLclampf zNear, GLclampf zFar) {
  mContext->DepthRange(zNear, zFar);
}

void HostWebGLContext::DetachShader(const WebGLId<WebGLProgram>& progId,
                                    const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if ((!prog) || (!shader)) {
    return;
  }
  mContext->DetachShader(*prog, *shader);
}

void HostWebGLContext::Flush() { mContext->Flush(); }

void HostWebGLContext::Finish() { mContext->Finish(); }

void HostWebGLContext::FramebufferRenderbuffer(
    GLenum target, GLenum attachment, GLenum rbTarget,
    const WebGLId<WebGLRenderbuffer>& rbId) {
  RefPtr<WebGLRenderbuffer> rb;
  if (!MaybeFind(rbId, rb)) {
    return;
  }
  mContext->FramebufferRenderbuffer(target, attachment, rbTarget, rb);
}

void HostWebGLContext::FramebufferTexture2D(GLenum target, GLenum attachment,
                                            GLenum texImageTarget,
                                            const WebGLId<WebGLTexture>& texId,
                                            GLint level) {
  RefPtr<WebGLTexture> tex;
  if (!MaybeFind(texId, tex)) {
    return;
  }
  mContext->FramebufferTexture2D(target, attachment, texImageTarget, tex,
                                 level);
}

void HostWebGLContext::FrontFace(GLenum mode) { mContext->FrontFace(mode); }

Maybe<WebGLActiveInfo> HostWebGLContext::GetActiveAttrib(
    const WebGLId<WebGLProgram>& progId, GLuint index) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return mContext->GetActiveAttrib(*prog, index);
}

Maybe<WebGLActiveInfo> HostWebGLContext::GetActiveUniform(
    const WebGLId<WebGLProgram>& progId, GLuint index) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return mContext->GetActiveUniform(*prog, index);
}

MaybeAttachedShaders HostWebGLContext::GetAttachedShaders(
    const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return mContext->GetAttachedShaders(*prog);
}

GLint HostWebGLContext::GetAttribLocation(const WebGLId<WebGLProgram>& progId,
                                          const nsString& name) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return -1;
  }
  return mContext->GetAttribLocation(*prog, name);
}

MaybeWebGLVariant HostWebGLContext::GetBufferParameter(GLenum target,
                                                       GLenum pname) {
  return mContext->GetBufferParameter(target, pname);
}

GLenum HostWebGLContext::GetError() { return mContext->GetError(); }

MaybeWebGLVariant HostWebGLContext::GetFramebufferAttachmentParameter(
    GLenum target, GLenum attachment, GLenum pname) {
  return mContext->GetFramebufferAttachmentParameter(target, attachment, pname);
}

MaybeWebGLVariant HostWebGLContext::GetProgramParameter(
    const WebGLId<WebGLProgram>& progId, GLenum pname) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return mContext->GetProgramParameter(*prog, pname);
}

nsString HostWebGLContext::GetProgramInfoLog(
    const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return nsString();
  }
  return mContext->GetProgramInfoLog(*prog);
}

MaybeWebGLVariant HostWebGLContext::GetRenderbufferParameter(GLenum target,
                                                             GLenum pname) {
  return mContext->GetRenderbufferParameter(target, pname);
}

MaybeWebGLVariant HostWebGLContext::GetShaderParameter(
    const WebGLId<WebGLShader>& shaderId, GLenum pname) {
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return Nothing();
  }
  return mContext->GetShaderParameter(*shader, pname);
}

MaybeWebGLVariant HostWebGLContext::GetShaderPrecisionFormat(
    GLenum shadertype, GLenum precisiontype) {
  return AsSomeVariant(
      mContext->GetShaderPrecisionFormat(shadertype, precisiontype));
}

nsString HostWebGLContext::GetShaderInfoLog(
    const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return nsString();
  }
  return mContext->GetShaderInfoLog(*shader);
}

nsString HostWebGLContext::GetShaderSource(
    const WebGLId<WebGLShader>& shaderId) {
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return nsString();
  }
  return mContext->GetShaderSource(*shader);
}

MaybeWebGLVariant HostWebGLContext::GetUniform(
    const WebGLId<WebGLProgram>& progId,
    const WebGLId<WebGLUniformLocation>& locId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  RefPtr<WebGLUniformLocation> loc = MustFind(locId);
  if ((!prog) || (!loc)) {
    return Nothing();
  }
  return mContext->GetUniform(*prog, *loc);
}

void HostWebGLContext::Hint(GLenum target, GLenum mode) {
  mContext->Hint(target, mode);
}

void HostWebGLContext::LineWidth(GLfloat width) { mContext->LineWidth(width); }

void HostWebGLContext::LinkProgram(const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return;
  }
  mContext->LinkProgram(*prog);
}

WebGLPixelStore HostWebGLContext::PixelStorei(GLenum pname, GLint param) {
  return mContext->PixelStorei(pname, param);
}

void HostWebGLContext::PolygonOffset(GLfloat factor, GLfloat units) {
  mContext->PolygonOffset(factor, units);
}

void HostWebGLContext::SampleCoverage(GLclampf value, WebGLboolean invert) {
  mContext->SampleCoverage(value, invert);
}

void HostWebGLContext::Scissor(GLint x, GLint y, GLsizei width,
                               GLsizei height) {
  mContext->Scissor(x, y, width, height);
}

void HostWebGLContext::ShaderSource(const WebGLId<WebGLShader>& shaderId,
                                    const nsString& source) {
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return;
  }
  mContext->ShaderSource(*shader, source);
}

void HostWebGLContext::StencilFunc(GLenum func, GLint ref, GLuint mask) {
  mContext->StencilFunc(func, ref, mask);
}

void HostWebGLContext::StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                           GLuint mask) {
  mContext->StencilFuncSeparate(face, func, ref, mask);
}

void HostWebGLContext::StencilMask(GLuint mask) { mContext->StencilMask(mask); }

void HostWebGLContext::StencilMaskSeparate(GLenum face, GLuint mask) {
  mContext->StencilMaskSeparate(face, mask);
}

void HostWebGLContext::StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
  mContext->StencilOp(sfail, dpfail, dppass);
}

void HostWebGLContext::StencilOpSeparate(GLenum face, GLenum sfail,
                                         GLenum dpfail, GLenum dppass) {
  mContext->StencilOpSeparate(face, sfail, dpfail, dppass);
}

void HostWebGLContext::Viewport(GLint x, GLint y, GLsizei width,
                                GLsizei height) {
  mContext->Viewport(x, y, width, height);
}

// ------------------------- Buffer Objects -------------------------
void HostWebGLContext::BindBuffer(GLenum target,
                                  const WebGLId<WebGLBuffer>& bufferId) {
  RefPtr<WebGLBuffer> buffer;
  if (!MaybeFind(bufferId, buffer)) {
    return;
  }
  mContext->BindBuffer(target, buffer);
}

void HostWebGLContext::BindBufferBase(GLenum target, GLuint index,
                                      const WebGLId<WebGLBuffer>& bufferId) {
  RefPtr<WebGLBuffer> buffer;
  if (!MaybeFind(bufferId, buffer)) {
    return;
  }
  mContext->BindBufferBase(target, index, buffer);
}

void HostWebGLContext::BindBufferRange(GLenum target, GLuint index,
                                       const WebGLId<WebGLBuffer>& bufferId,
                                       WebGLintptr offset, WebGLsizeiptr size) {
  RefPtr<WebGLBuffer> buffer = MustFind(bufferId);
  if (!buffer) {
    return;
  }
  mContext->BindBufferRange(target, index, buffer, offset, size);
}

void HostWebGLContext::DeleteBuffer(const WebGLId<WebGLBuffer>& bufId) {
  RefPtr<WebGLBuffer> buf;
  if (!MaybeFind(bufId, buf)) {
    return;
  }
  mContext->DeleteBuffer(buf);
}

void HostWebGLContext::CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                                         GLintptr readOffset,
                                         GLintptr writeOffset,
                                         GLsizeiptr size) {
  GetWebGL2Context()->CopyBufferSubData(readTarget, writeTarget, readOffset,
                                        writeOffset, size);
}

UniquePtr<RawBuffer<>> HostWebGLContext::GetBufferSubData(
    GLenum target, GLintptr srcByteOffset, size_t byteLen) {
  return GetWebGL2Context()->GetBufferSubData(target, srcByteOffset, byteLen);
}

void HostWebGLContext::BufferData(GLenum target, const RawBuffer<>& data,
                                  GLenum usage) {
  mContext->BufferDataImpl(target, data.Length(), data.Data(), usage);
}

void HostWebGLContext::BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                                     const RawBuffer<>& srcData) {
  mContext->BufferSubDataImpl(target, dstByteOffset, srcData.Length(),
                              srcData.Data());
}

// -------------------------- Framebuffer Objects --------------------------
void HostWebGLContext::BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1,
                                       GLint srcY1, GLint dstX0, GLint dstY0,
                                       GLint dstX1, GLint dstY1,
                                       GLbitfield mask, GLenum filter) {
  GetWebGL2Context()->BlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0,
                                      dstX1, dstY1, mask, filter);
}

void HostWebGLContext::FramebufferTextureLayer(
    GLenum target, GLenum attachment, const WebGLId<WebGLTexture>& textureId,
    GLint level, GLint layer, bool toDetach) {
  // Pass a null texture to detach.
  WebGLTexture* tex = nullptr;
  if (!toDetach) {
    tex = MustFind(textureId);
    if (!tex) {
      return;
    }
  }
  GetWebGL2Context()->FramebufferTextureLayer(target, attachment, tex, level,
                                              layer);
}

void HostWebGLContext::InvalidateFramebuffer(
    GLenum target, const nsTArray<GLenum>& attachments) {
  GetWebGL2Context()->InvalidateFramebuffer(target, attachments);
}

void HostWebGLContext::InvalidateSubFramebuffer(
    GLenum target, const nsTArray<GLenum>& attachments, GLint x, GLint y,
    GLsizei width, GLsizei height) {
  GetWebGL2Context()->InvalidateSubFramebuffer(target, attachments, x, y, width,
                                               height);
}

void HostWebGLContext::ReadBuffer(GLenum mode) {
  GetWebGL2Context()->ReadBuffer(mode);
}

// ----------------------- Renderbuffer objects -----------------------
Maybe<nsTArray<int32_t>> HostWebGLContext::GetInternalformatParameter(
    GLenum target, GLenum internalformat, GLenum pname) {
  return GetWebGL2Context()->GetInternalformatParameter(target, internalformat,
                                                        pname);
}

void HostWebGLContext::RenderbufferStorage_base(GLenum target, GLsizei samples,
                                                GLenum internalFormat,
                                                GLsizei width, GLsizei height,
                                                FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->RenderbufferStorage_base(target, samples, internalFormat, width,
                                     height);
}

// --------------------------- Texture objects ---------------------------
void HostWebGLContext::ActiveTexture(GLenum texUnit) {
  return mContext->ActiveTexture(texUnit);
}

void HostWebGLContext::BindTexture(GLenum texTarget,
                                   const WebGLId<WebGLTexture>& texId) {
  RefPtr<WebGLTexture> tex;
  if (!MaybeFind(texId, tex)) {
    return;
  }
  return mContext->BindTexture(texTarget, tex);
}

void HostWebGLContext::DeleteTexture(const WebGLId<WebGLTexture>& texId) {
  RefPtr<WebGLTexture> tex;
  if (!MaybeFind(texId, tex)) {
    return;
  }
  mContext->DeleteTexture(tex);
}

void HostWebGLContext::GenerateMipmap(GLenum texTarget) {
  mContext->GenerateMipmap(texTarget);
}

void HostWebGLContext::CopyTexImage2D(GLenum target, GLint level,
                                      GLenum internalFormat, GLint x, GLint y,
                                      uint32_t width, uint32_t height,
                                      uint32_t depth) {
  mContext->CopyTexImage2D(target, level, internalFormat, x, y, width, height,
                           depth);
}

void HostWebGLContext::TexStorage(uint8_t funcDims, GLenum target,
                                  GLsizei levels, GLenum internalFormat,
                                  GLsizei width, GLsizei height, GLsizei depth,
                                  FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  GetWebGL2Context()->TexStorage(funcDims, target, levels, internalFormat,
                                 width, height, depth);
}

template <typename TexUnpackType>
struct ToTexUnpackTypeMatcher {
  template <typename T, typename mozilla::EnableIf<
                            mozilla::IsConvertible<T*, TexUnpackType*>::value,
                            int>::Type = 0>
  UniquePtr<TexUnpackType> operator()(UniquePtr<T>& x) {
    return std::move(x);
  }
  template <typename T, typename mozilla::EnableIf<
                            !mozilla::IsConvertible<T*, TexUnpackType*>::value,
                            char>::Type = 0>
  UniquePtr<TexUnpackType> operator()(UniquePtr<T>& x) {
    MOZ_ASSERT_UNREACHABLE(
        "Attempted to read TexUnpackBlob as something it was not");
    return nullptr;
  }
  UniquePtr<TexUnpackType> operator()(WebGLTexPboOffset& aPbo) {
    UniquePtr<webgl::TexUnpackBytes> bytes = mContext->ToTexUnpackBytes(aPbo);
    return operator()(bytes);
  }
  WebGLContext* mContext;
};

template <typename TexUnpackType>
UniquePtr<TexUnpackType> AsTexUnpackType(WebGLContext* aContext,
                                         MaybeWebGLTexUnpackVariant&& src) {
  if (!src) {
    return nullptr;
  }
  if ((!src.ref().is<WebGLTexPboOffset>()) &&
      (!aContext->ValidateNullPixelUnpackBuffer())) {
    return nullptr;
  }

  return src.ref().match(ToTexUnpackTypeMatcher<TexUnpackType>{aContext});
}

void HostWebGLContext::TexImage(uint8_t funcDims, GLenum target, GLint level,
                                GLenum internalFormat, GLsizei width,
                                GLsizei height, GLsizei depth, GLint border,
                                GLenum unpackFormat, GLenum unpackType,
                                MaybeWebGLTexUnpackVariant&& src,
                                FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->TexImage(
      funcDims, target, level, internalFormat, width, height, depth, border,
      unpackFormat, unpackType,
      AsTexUnpackType<webgl::TexUnpackBlob>(mContext, std::move(src)));
}

void HostWebGLContext::TexSubImage(uint8_t funcDims, GLenum target, GLint level,
                                   GLint xOffset, GLint yOffset, GLint zOffset,
                                   GLsizei width, GLsizei height, GLsizei depth,
                                   GLenum unpackFormat, GLenum unpackType,
                                   MaybeWebGLTexUnpackVariant&& src,
                                   FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->TexSubImage(
      funcDims, target, level, xOffset, yOffset, zOffset, width, height, depth,
      unpackFormat, unpackType,
      AsTexUnpackType<webgl::TexUnpackBlob>(mContext, std::move(src)));
}

void HostWebGLContext::CompressedTexImage(
    uint8_t funcDims, GLenum target, GLint level, GLenum internalFormat,
    GLsizei width, GLsizei height, GLsizei depth, GLint border,
    MaybeWebGLTexUnpackVariant&& src, const Maybe<GLsizei>& expectedImageSize,
    FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CompressedTexImage(
      funcDims, target, level, internalFormat, width, height, depth, border,
      AsTexUnpackType<webgl::TexUnpackBytes>(mContext, std::move(src)),
      expectedImageSize);
}

void HostWebGLContext::CompressedTexSubImage(
    uint8_t funcDims, GLenum target, GLint level, GLint xOffset, GLint yOffset,
    GLint zOffset, GLsizei width, GLsizei height, GLsizei depth,
    GLenum unpackFormat, MaybeWebGLTexUnpackVariant&& src,
    const Maybe<GLsizei>& expectedImageSize, FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CompressedTexSubImage(
      funcDims, target, level, xOffset, yOffset, zOffset, width, height, depth,
      unpackFormat,
      AsTexUnpackType<webgl::TexUnpackBytes>(mContext, std::move(src)),
      expectedImageSize);
}

void HostWebGLContext::CopyTexSubImage(uint8_t funcDims, GLenum target,
                                       GLint level, GLint xOffset,
                                       GLint yOffset, GLint zOffset, GLint x,
                                       GLint y, uint32_t width, uint32_t height,
                                       uint32_t depth, FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CopyTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
                            x, y, width, height, depth);
}

MaybeWebGLVariant HostWebGLContext::GetTexParameter(GLenum texTarget,
                                                    GLenum pname) {
  return mContext->GetTexParameter(texTarget, pname);
}

void HostWebGLContext::TexParameter_base(GLenum texTarget, GLenum pname,
                                         const FloatOrInt& param) {
  mContext->TexParameter_base(texTarget, pname, param);
}

// ------------------- Programs and shaders --------------------------------
void HostWebGLContext::UseProgram(const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog;
  if (!MaybeFind(progId, prog)) {
    return;
  }
  mContext->UseProgram(prog);
}

void HostWebGLContext::ValidateProgram(const WebGLId<WebGLProgram>& progId) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return;
  }
  mContext->ValidateProgram(*prog);
}

GLint HostWebGLContext::GetFragDataLocation(const WebGLId<WebGLProgram>& progId,
                                            const nsString& name) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return -1;
  }
  return GetWebGL2Context()->GetFragDataLocation(*prog, name);
}

// ------------------------ Uniforms and attributes ------------------------
void HostWebGLContext::UniformNfv(const nsCString& funcName, uint8_t N,
                                  const WebGLId<WebGLUniformLocation>& aLoc,
                                  const RawBuffer<const float>& arr,
                                  GLuint elemOffset, GLuint elemCountOverride) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  mContext->UniformNfv(funcName.BeginReading(), N, loc, arr, elemOffset,
                       elemCountOverride);
}

void HostWebGLContext::UniformNiv(const nsCString& funcName, uint8_t N,
                                  const WebGLId<WebGLUniformLocation>& aLoc,
                                  const RawBuffer<const int32_t>& arr,
                                  GLuint elemOffset, GLuint elemCountOverride) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  mContext->UniformNiv(funcName.BeginReading(), N, loc, arr, elemOffset,
                       elemCountOverride);
}

void HostWebGLContext::UniformNuiv(const nsCString& funcName, uint8_t N,
                                   const WebGLId<WebGLUniformLocation>& aLoc,
                                   const RawBuffer<const uint32_t>& arr,
                                   GLuint elemOffset,
                                   GLuint elemCountOverride) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  mContext->UniformNuiv(funcName.BeginReading(), N, loc, arr, elemOffset,
                        elemCountOverride);
}

void HostWebGLContext::UniformMatrixAxBfv(
    const nsCString& funcName, uint8_t A, uint8_t B,
    const WebGLId<WebGLUniformLocation>& aLoc, bool transpose,
    const RawBuffer<const float>& arr, GLuint elemOffset,
    GLuint elemCountOverride) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  mContext->UniformMatrixAxBfv(funcName.BeginReading(), A, B, loc, transpose,
                               arr, elemOffset, elemCountOverride);
}

void HostWebGLContext::UniformFVec(const WebGLId<WebGLUniformLocation>& aLoc,
                                   const nsTArray<float>& vec) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  switch (vec.Length()) {
    case 1:
      mContext->Uniform1f(loc, vec[0]);
      break;
    case 2:
      mContext->Uniform2f(loc, vec[0], vec[1]);
      break;
    case 3:
      mContext->Uniform3f(loc, vec[0], vec[1], vec[2]);
      break;
    case 4:
      mContext->Uniform4f(loc, vec[0], vec[1], vec[2], vec[3]);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal number of parameters to UniformFVec");
  }
}

void HostWebGLContext::UniformIVec(const WebGLId<WebGLUniformLocation>& aLoc,
                                   const nsTArray<int32_t>& vec) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  switch (vec.Length()) {
    case 1:
      mContext->Uniform1i(loc, vec[0]);
      break;
    case 2:
      mContext->Uniform2i(loc, vec[0], vec[1]);
      break;
    case 3:
      mContext->Uniform3i(loc, vec[0], vec[1], vec[2]);
      break;
    case 4:
      mContext->Uniform4i(loc, vec[0], vec[1], vec[2], vec[3]);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal number of parameters to UniformIVec");
  }
}

void HostWebGLContext::UniformUIVec(const WebGLId<WebGLUniformLocation>& aLoc,
                                    const nsTArray<uint32_t>& vec) {
  auto loc = MustFind(aLoc);
  if (!loc) {
    return;
  }

  switch (vec.Length()) {
    case 1:
      mContext->Uniform1ui(loc, vec[0]);
      break;
    case 2:
      mContext->Uniform2ui(loc, vec[0], vec[1]);
      break;
    case 3:
      mContext->Uniform3ui(loc, vec[0], vec[1], vec[2]);
      break;
    case 4:
      mContext->Uniform4ui(loc, vec[0], vec[1], vec[2], vec[3]);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal number of parameters to UniformUIVec");
  }
}

void HostWebGLContext::VertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                                      GLfloat z, GLfloat w,
                                      FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->VertexAttrib4f(index, x, y, z, w);
}

void HostWebGLContext::VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z,
                                       GLint w, FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  GetWebGL2Context()->VertexAttribI4i(index, x, y, z, w);
}

void HostWebGLContext::VertexAttribI4ui(GLuint index, GLuint x, GLuint y,
                                        GLuint z, GLuint w,
                                        FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  GetWebGL2Context()->VertexAttribI4ui(index, x, y, z, w);
}

void HostWebGLContext::VertexAttribDivisor(GLuint index, GLuint divisor,
                                           bool aFromExtension) {
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::ANGLE_instanced_arrays>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->VertexAttribDivisorANGLE(index, divisor);
  }
  GetWebGL2Context()->VertexAttribDivisor(index, divisor);
}

MaybeWebGLVariant HostWebGLContext::GetIndexedParameter(GLenum target,
                                                        GLuint index) {
  return GetWebGL2Context()->GetIndexedParameter(target, index);
}

MaybeWebGLVariant HostWebGLContext::GetUniformIndices(
    const WebGLId<WebGLProgram>& progId,
    const nsTArray<nsString>& uniformNames) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return GetWebGL2Context()->GetUniformIndices(*prog, uniformNames);
}

MaybeWebGLVariant HostWebGLContext::GetActiveUniforms(
    const WebGLId<WebGLProgram>& progId, const nsTArray<GLuint>& uniformIndices,
    GLenum pname) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return GetWebGL2Context()->GetActiveUniforms(*prog, uniformIndices, pname);
}

GLuint HostWebGLContext::GetUniformBlockIndex(
    const WebGLId<WebGLProgram>& progId, const nsString& uniformBlockName) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return 0;
  }
  return GetWebGL2Context()->GetUniformBlockIndex(*prog, uniformBlockName);
}

MaybeWebGLVariant HostWebGLContext::GetActiveUniformBlockParameter(
    const WebGLId<WebGLProgram>& progId, GLuint uniformBlockIndex,
    GLenum pname) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return GetWebGL2Context()->GetActiveUniformBlockParameter(
      *prog, uniformBlockIndex, pname);
}

nsString HostWebGLContext::GetActiveUniformBlockName(
    const WebGLId<WebGLProgram>& progId, GLuint uniformBlockIndex) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return nsString();
  }
  return GetWebGL2Context()->GetActiveUniformBlockName(*prog,
                                                       uniformBlockIndex);
}

void HostWebGLContext::UniformBlockBinding(const WebGLId<WebGLProgram>& progId,
                                           GLuint uniformBlockIndex,
                                           GLuint uniformBlockBinding) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return;
  }
  return GetWebGL2Context()->UniformBlockBinding(*prog, uniformBlockIndex,
                                                 uniformBlockBinding);
}

void HostWebGLContext::EnableVertexAttribArray(GLuint index) {
  mContext->EnableVertexAttribArray(index);
}

void HostWebGLContext::DisableVertexAttribArray(GLuint index) {
  mContext->DisableVertexAttribArray(index);
}

MaybeWebGLVariant HostWebGLContext::GetVertexAttrib(GLuint index,
                                                    GLenum pname) {
  return mContext->GetVertexAttrib(index, pname);
}

WebGLsizeiptr HostWebGLContext::GetVertexAttribOffset(GLuint index,
                                                      GLenum pname) {
  return mContext->GetVertexAttribOffset(index, pname);
}

void HostWebGLContext::VertexAttribAnyPointer(bool isFuncInt, GLuint index,
                                              GLint size, GLenum type,
                                              bool normalized, GLsizei stride,
                                              WebGLintptr byteOffset,
                                              FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->VertexAttribAnyPointer(isFuncInt, index, size, type, normalized,
                                   stride, byteOffset);
}

// --------------------------- Buffer Operations --------------------------
void HostWebGLContext::ClearBufferfv(GLenum buffer, GLint drawBuffer,
                                     const RawBuffer<const float>& src,
                                     GLuint srcElemOffset) {
  GetWebGL2Context()->ClearBufferfv(buffer, drawBuffer, src, srcElemOffset);
}

void HostWebGLContext::ClearBufferiv(GLenum buffer, GLint drawBuffer,
                                     const RawBuffer<const int32_t>& src,
                                     GLuint srcElemOffset) {
  GetWebGL2Context()->ClearBufferiv(buffer, drawBuffer, src, srcElemOffset);
}

void HostWebGLContext::ClearBufferuiv(GLenum buffer, GLint drawBuffer,
                                      const RawBuffer<const uint32_t>& src,
                                      GLuint srcElemOffset) {
  GetWebGL2Context()->ClearBufferuiv(buffer, drawBuffer, src, srcElemOffset);
}

void HostWebGLContext::ClearBufferfi(GLenum buffer, GLint drawBuffer,
                                     GLfloat depth, GLint stencil) {
  GetWebGL2Context()->ClearBufferfi(buffer, drawBuffer, depth, stencil);
}

// ------------------------------ Readback -------------------------------
void HostWebGLContext::ReadPixels1(GLint x, GLint y, GLsizei width,
                                   GLsizei height, GLenum format, GLenum type,
                                   WebGLsizeiptr offset) {
  mContext->ReadPixels(x, y, width, height, format, type, offset);
}

Maybe<UniquePtr<RawBuffer<>>> HostWebGLContext::ReadPixels2(
    GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
    RawBuffer<>&& aBuffer) {
  return mContext->ReadPixels(x, y, width, height, format, type,
                              std::move(aBuffer));
}

// ----------------------------- Sampler -----------------------------------
void HostWebGLContext::DeleteSampler(const WebGLId<WebGLSampler>& aId) {
  auto sampler = MustFind(aId);
  if (!sampler) {
    return;
  }
  GetWebGL2Context()->DeleteSampler(sampler);
}

void HostWebGLContext::BindSampler(GLuint unit,
                                   const WebGLId<WebGLSampler>& samplerId) {
  RefPtr<WebGLSampler> sampler;
  if (!MaybeFind(samplerId, sampler)) {
    return;
  }
  GetWebGL2Context()->BindSampler(unit, sampler);
}

void HostWebGLContext::SamplerParameteri(const WebGLId<WebGLSampler>& samplerId,
                                         GLenum pname, GLint param) {
  RefPtr<WebGLSampler> sampler = MustFind(samplerId);
  if (!sampler) {
    return;
  }
  GetWebGL2Context()->SamplerParameteri(*sampler, pname, param);
}

void HostWebGLContext::SamplerParameterf(const WebGLId<WebGLSampler>& samplerId,
                                         GLenum pname, GLfloat param) {
  RefPtr<WebGLSampler> sampler = MustFind(samplerId);
  if (!sampler) {
    return;
  }
  GetWebGL2Context()->SamplerParameterf(*sampler, pname, param);
}

MaybeWebGLVariant HostWebGLContext::GetSamplerParameter(
    const WebGLId<WebGLSampler>& samplerId, GLenum pname) {
  RefPtr<WebGLSampler> sampler = MustFind(samplerId);
  if (!sampler) {
    return Nothing();
  }
  return GetWebGL2Context()->GetSamplerParameter(*sampler, pname);
}

// ------------------------------- GL Sync ---------------------------------
void HostWebGLContext::DeleteSync(const WebGLId<WebGLSync>& syncId) {
  RefPtr<WebGLSync> sync = MustFind(syncId);
  if (!sync) {
    return;
  }
  GetWebGL2Context()->DeleteSync(sync);
}

GLenum HostWebGLContext::ClientWaitSync(const WebGLId<WebGLSync>& syncId,
                                        GLbitfield flags, GLuint64 timeout) {
  RefPtr<WebGLSync> sync = MustFind(syncId);
  if (!sync) {
    return LOCAL_GL_WAIT_FAILED;
  }
  return GetWebGL2Context()->ClientWaitSync(*sync, flags, timeout);
}

void HostWebGLContext::WaitSync(const WebGLId<WebGLSync>& syncId,
                                GLbitfield flags, GLint64 timeout) {
  RefPtr<WebGLSync> sync = MustFind(syncId);
  if (!sync) {
    return;
  }
  GetWebGL2Context()->WaitSync(*sync, flags, timeout);
}

MaybeWebGLVariant HostWebGLContext::GetSyncParameter(
    const WebGLId<WebGLSync>& syncId, GLenum pname) {
  RefPtr<WebGLSync> sync = MustFind(syncId);
  if (!sync) {
    return Nothing();
  }
  return GetWebGL2Context()->GetSyncParameter(*sync, pname);
}

// -------------------------- Transform Feedback ---------------------------
void HostWebGLContext::DeleteTransformFeedback(
    const WebGLId<WebGLTransformFeedback>& tfId) {
  RefPtr<WebGLTransformFeedback> tf = MustFind(tfId);
  if (!tf) {
    return;
  }
  GetWebGL2Context()->DeleteTransformFeedback(tf);
}

void HostWebGLContext::BindTransformFeedback(
    GLenum target, const WebGLId<WebGLTransformFeedback>& tfId) {
  RefPtr<WebGLTransformFeedback> tf = MustFind(tfId);
  if (!tf) {
    return;
  }
  GetWebGL2Context()->BindTransformFeedback(target, tf);
}

void HostWebGLContext::BeginTransformFeedback(GLenum primitiveMode) {
  GetWebGL2Context()->BeginTransformFeedback(primitiveMode);
}

void HostWebGLContext::EndTransformFeedback() {
  GetWebGL2Context()->EndTransformFeedback();
}

void HostWebGLContext::PauseTransformFeedback() {
  GetWebGL2Context()->PauseTransformFeedback();
}

void HostWebGLContext::ResumeTransformFeedback() {
  GetWebGL2Context()->ResumeTransformFeedback();
}

void HostWebGLContext::TransformFeedbackVaryings(
    const WebGLId<WebGLProgram>& progId, const nsTArray<nsString>& varyings,
    GLenum bufferMode) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return;
  }
  GetWebGL2Context()->TransformFeedbackVaryings(*prog, varyings, bufferMode);
}

Maybe<WebGLActiveInfo> HostWebGLContext::GetTransformFeedbackVarying(
    const WebGLId<WebGLProgram>& progId, GLuint index) {
  RefPtr<WebGLProgram> prog = MustFind(progId);
  if (!prog) {
    return Nothing();
  }
  return GetWebGL2Context()->GetTransformFeedbackVarying(*prog, index);
}

// ------------------------------ WebGL Debug
// ------------------------------------
void HostWebGLContext::EnqueueError(GLenum aGLError, const nsCString& aMsg) {
  mContext->GenerateEnqueuedError(aGLError, aMsg);
}

void HostWebGLContext::EnqueueWarning(const nsCString& aMsg) {
  mContext->GenerateEnqueuedWarning(aMsg);
}

void HostWebGLContext::ReportOOMAndLoseContext() {
  mContext->ErrorOutOfMemory("Ran out of memory in WebGL IPC.");
  LoseContext(false);
}

// -------------------------------------------------------------------------
// Host-side extension methods.
// -------------------------------------------------------------------------

// Misc. Extensions
void HostWebGLContext::EnableExtension(dom::CallerType callerType,
                                       WebGLExtensionID ext) {
  if (ext >= WebGLExtensionID::Max) {
    MOZ_ASSERT_UNREACHABLE("Illegal extension ID");
    return;
  }
  mContext->EnableExtension(ext, callerType);
}

const Maybe<ExtensionSets> HostWebGLContext::GetSupportedExtensions() {
  return mContext->GetSupportedExtensions();
}

void HostWebGLContext::MakeQueriesAndSyncsAvailable() {
  mContext->MakeQueriesAndSyncsAvailable();
}

void HostWebGLContext::DrawBuffers(const nsTArray<GLenum>& buffers,
                                   bool aFromExtension) {
  if (aFromExtension) {
    auto* ext = mContext->GetExtension<WebGLExtensionID::WEBGL_draw_buffers>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->DrawBuffersWEBGL(buffers);
  }

  return GetWebGL2Context()->DrawBuffers(buffers);
}

Maybe<nsTArray<nsString>> HostWebGLContext::GetASTCExtensionSupportedProfiles()
    const {
  auto* ext =
      mContext->GetExtension<WebGLExtensionID::WEBGL_compressed_texture_astc>();
  MOZ_RELEASE_ASSERT(ext);
  return ext->GetSupportedProfiles();
}

nsString HostWebGLContext::GetTranslatedShaderSource(
    const WebGLId<WebGLShader>& shaderId) const {
  auto* ext = mContext->GetExtension<WebGLExtensionID::WEBGL_debug_shaders>();
  MOZ_RELEASE_ASSERT(ext);
  RefPtr<WebGLShader> shader = MustFind(shaderId);
  if (!shader) {
    return nsString();
  }
  return ext->GetTranslatedShaderSource(*shader);
}

void HostWebGLContext::LoseContext(bool isSimulated) {
  isSimulated ? mContext->LoseContext() : mContext->ForceLoseContext();
}

void HostWebGLContext::RestoreContext() { mContext->RestoreContext(); }

MaybeWebGLVariant HostWebGLContext::MOZDebugGetParameter(GLenum pname) const {
  auto* ext = mContext->GetExtension<WebGLExtensionID::MOZ_debug>();
  MOZ_RELEASE_ASSERT(ext);
  return ext->GetParameter(pname);
}

// VertexArrayObjectEXT
void HostWebGLContext::BindVertexArray(const WebGLId<WebGLVertexArray>& arrayId,
                                       bool aFromExtension) {
  RefPtr<WebGLVertexArray> array = MustFind(arrayId);
  if (!array) {
    return;
  }

  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::OES_vertex_array_object>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->BindVertexArrayOES(array);
  }

  GetWebGL2Context()->BindVertexArray(array);
}

void HostWebGLContext::DeleteVertexArray(
    const WebGLId<WebGLVertexArray>& arrayId, bool aFromExtension) {
  RefPtr<WebGLVertexArray> array = MustFind(arrayId);
  if (!array) {
    return;
  }

  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::OES_vertex_array_object>();
    MOZ_RELEASE_ASSERT(ext);
    ext->DeleteVertexArrayOES(array);
  } else {
    mContext->DeleteVertexArray(array);
  }
}

// -

void HostWebGLContext::DrawArraysInstanced(GLenum mode, GLint first,
                                           GLsizei count, GLsizei primcount,
                                           FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->DrawArraysInstanced(mode, first, count, primcount);
}

void HostWebGLContext::DrawElementsInstanced(GLenum mode, GLsizei count,
                                             GLenum type, WebGLintptr offset,
                                             GLsizei primcount,
                                             FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->DrawElementsInstanced(mode, count, type, offset, primcount);
}

// GLQueryEXT
void HostWebGLContext::DeleteQuery(const WebGLId<WebGLQuery>& queryId,
                                   bool aFromExtension) const {
  auto query = MustFind(queryId);
  if (!query) {
    return;
  }

  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    ext->DeleteQueryEXT(query);
  } else {
    const_cast<WebGL2Context*>(GetWebGL2Context())->DeleteQuery(query);
  }
}

void HostWebGLContext::BeginQuery(GLenum target,
                                  const WebGLId<WebGLQuery>& queryId,
                                  bool aFromExtension) const {
  RefPtr<WebGLQuery> query = MustFind(queryId);
  if (!query) {
    return;
  }
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->BeginQueryEXT(target, *query);
  }

  const_cast<WebGL2Context*>(GetWebGL2Context())->BeginQuery(target, *query);
}

void HostWebGLContext::EndQuery(GLenum target, bool aFromExtension) const {
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->EndQueryEXT(target);
  }

  const_cast<WebGL2Context*>(GetWebGL2Context())->EndQuery(target);
}

void HostWebGLContext::QueryCounter(const WebGLId<WebGLQuery>& queryId,
                                    GLenum target) const {
  auto* ext =
      mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
  MOZ_RELEASE_ASSERT(ext);
  RefPtr<WebGLQuery> query = MustFind(queryId);
  if (!query) {
    return;
  }
  return ext->QueryCounterEXT(*query, target);
}

MaybeWebGLVariant HostWebGLContext::GetQuery(GLenum target, GLenum pname,
                                             bool aFromExtension) const {
  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->GetQueryEXT(target, pname);
  }

  return const_cast<WebGL2Context*>(GetWebGL2Context())
      ->GetQuery(target, pname);
}

MaybeWebGLVariant HostWebGLContext::GetQueryParameter(
    const WebGLId<WebGLQuery>& queryId, GLenum pname,
    bool aFromExtension) const {
  RefPtr<WebGLQuery> query = MustFind(queryId);
  if (!query) {
    return Nothing();
  }

  if (aFromExtension) {
    auto* ext =
        mContext->GetExtension<WebGLExtensionID::EXT_disjoint_timer_query>();
    MOZ_RELEASE_ASSERT(ext);
    return ext->GetQueryObjectEXT(*query, pname);
  }

  return const_cast<WebGL2Context*>(GetWebGL2Context())
      ->GetQueryParameter(*query, pname);
}

void HostWebGLContext::PostWarning(const nsCString& aWarningMsg) const {
  if (mClientContext) {
    mClientContext->PostWarning(aWarningMsg);
    return;
  }
  mErrorSource->RunCommand(WebGLErrorCommand::Warning, aWarningMsg);
}

void HostWebGLContext::PostContextCreationError(const nsCString& aMsg) const {
  if (mClientContext) {
    mClientContext->PostContextCreationError(aMsg);
    return;
  }
  mErrorSource->RunCommand(WebGLErrorCommand::CreationError, aMsg);
}

void HostWebGLContext::OnLostContext() {
  if (mClientContext) {
    mClientContext->OnLostContext();
    return;
  }
  mErrorSource->RunCommand(WebGLErrorCommand::OnLostContext);
}

void HostWebGLContext::OnRestoredContext() {
  if (mClientContext) {
    mClientContext->OnRestoredContext();
    return;
  }
  mErrorSource->RunCommand(WebGLErrorCommand::OnRestoredContext);
}

}  // namespace mozilla
