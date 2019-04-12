/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TEX_UNPACK_BLOB_H_
#define TEX_UNPACK_BLOB_H_

#include "GLContextTypes.h"
#include "mozilla/RefPtr.h"
#include "WebGLStrongTypes.h"
#include "WebGLTypes.h"

namespace mozilla {

class ClientWebGLContext;
class UniqueBuffer;
class WebGLContext;
class WebGLTexture;

namespace dom {
class Element;
class HTMLCanvasElement;
class HTMLVideoElement;
}  // namespace dom

namespace ipc {
template<typename T> struct PcqParamTraits;
class ConsumerView;
class ProducerView;
enum class PcqStatus;
}

namespace gfx {
class DataSourceSurface;
}  // namespace gfx

namespace layers {
class Image;
class ImageContainer;
}  // namespace layers

namespace webgl {

struct PackingInfo;
struct DriverUnpackInfo;

class TexUnpackBlob {
 public:
  const uint32_t mAlignment = 0;
  const uint32_t mRowLength = 0;
  const uint32_t mImageHeight = 0;
  const uint32_t mSkipPixels = 0;
  const uint32_t mSkipRows = 0;
  const uint32_t mSkipImages = 0;
  const uint32_t mWidth = 0;
  const uint32_t mHeight = 0;
  const uint32_t mDepth = 0;

  const gfxAlphaType mSrcAlphaType;

  bool mNeedsExactUpload;

 protected:
  TexUnpackBlob(const WebGLPixelStore& pixelStore, TexImageTarget target,
                uint32_t rowLength, uint32_t width, uint32_t height,
                uint32_t depth, gfxAlphaType srcAlphaType);

  bool ConvertIfNeeded(WebGLContext* webgl, const uint32_t rowLength,
                       const uint32_t rowCount, WebGLTexelFormat srcFormat,
                       const uint8_t* const srcBegin, const ptrdiff_t srcStride,
                       WebGLTexelFormat dstFormat, const ptrdiff_t dstStride,

                       const uint8_t** const out_begin,
                       UniqueBuffer* const out_anchoredBuffer) const;

 public:
  virtual ~TexUnpackBlob() {}

  virtual TexUnpackBytes* AsTexUnpackBytes() { return nullptr; }

  virtual bool HasData() const { return true; }

  virtual bool Validate(WebGLContext* webgl, const webgl::PackingInfo& pi) = 0;

  // Returns false when we've generated a WebGL error.
  // Returns true but with a non-zero *out_error if we still need to generate a
  // WebGL error.
  virtual bool TexOrSubImage(bool isSubImage, bool needsRespec,
                             WebGLTexture* tex, TexImageTarget target,
                             GLint level, const webgl::DriverUnpackInfo* dui,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             const webgl::PackingInfo& pi,
                             GLenum* const out_error) const = 0;
};

class TexUnpackBytes final : public TexUnpackBlob {
 public:
  const bool mIsClientData;
  const uint8_t* const mPtr;
  const size_t mAvailBytes;

  TexUnpackBytes(const WebGLPixelStore& pixelStore, TexImageTarget target,
                 uint32_t width, uint32_t height, uint32_t depth,
                 bool isClientData, const uint8_t* ptr, size_t availBytes);

  TexUnpackBytes* AsTexUnpackBytes() override { return this; }

  virtual bool HasData() const override { return !mIsClientData || bool(mPtr); }

  virtual bool Validate(WebGLContext* webgl,
                        const webgl::PackingInfo& pi) override;
  virtual bool TexOrSubImage(bool isSubImage, bool needsRespec,
                             WebGLTexture* tex, TexImageTarget target,
                             GLint level, const webgl::DriverUnpackInfo* dui,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             const webgl::PackingInfo& pi,
                             GLenum* const out_error) const override;

  static mozilla::ipc::PcqStatus
  Read(mozilla::ipc::ConsumerView& aView,
       UniquePtr<webgl::TexUnpackBytes>* aTexBytes);
  mozilla::ipc::PcqStatus Write(mozilla::ipc::ProducerView& aView);
  size_t Size() const;
};

class TexUnpackImage final : public TexUnpackBlob {
 public:
  const RefPtr<layers::Image> mImage;

  TexUnpackImage(const WebGLContext* webgl, TexImageTarget target,
                 uint32_t rowLength,
                 uint32_t width, uint32_t height, uint32_t depth,
                 layers::Image* image, gfxAlphaType srcAlphaType);

  ~TexUnpackImage();  // Prevent needing to define layers::Image in the header.

  virtual bool Validate(WebGLContext* webgl,
                        const webgl::PackingInfo& pi) override;
  virtual bool TexOrSubImage(bool isSubImage, bool needsRespec,
                             WebGLTexture* tex, TexImageTarget target,
                             GLint level, const webgl::DriverUnpackInfo* dui,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             const webgl::PackingInfo& dstPI,
                             GLenum* const out_error) const override;
};

class TexUnpackSurface final : public TexUnpackBlob {
 public:
  const RefPtr<gfx::DataSourceSurface> mSurf;

  TexUnpackSurface(const ClientWebGLContext* webgl, TexImageTarget target,
                   uint32_t width, uint32_t height, uint32_t depth,
                   gfx::DataSourceSurface* surf, gfxAlphaType srcAlphaType);

  TexUnpackSurface(const WebGLContext* webgl, TexImageTarget target,
                   uint32_t width, uint32_t height, uint32_t depth,
                   gfx::DataSourceSurface* surf, gfxAlphaType srcAlphaType);

  virtual bool Validate(WebGLContext* webgl,
                        const webgl::PackingInfo& pi) override;
  virtual bool TexOrSubImage(bool isSubImage, bool needsRespec,
                             WebGLTexture* tex, TexImageTarget target,
                             GLint level, const webgl::DriverUnpackInfo* dui,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             const webgl::PackingInfo& dstPI,
                             GLenum* const out_error) const override;

  mozilla::ipc::PcqStatus Write(mozilla::ipc::ProducerView& aView);
  size_t Size() const;
};

}  // namespace webgl

/**
 * Simple wrapper for a TexUnpackBlobs so that they can be sent to another process.
 * The derived type of that underlying blob may change when represented in a remote
 * process.
 */
class PcqTexUnpack final {
 public:
  PcqTexUnpack(MaybeWebGLTexUnpackVariant&& aMaybeBlob)
    : mMaybeBlob(std::move(aMaybeBlob)) {
  }

  // Take the owned blob.  Returns null if the blob was already taken or if it
  // is not a TexUnpackBytes.
  UniquePtr<webgl::TexUnpackBlob> TakeBlob(WebGLContext* aContext);

  PcqTexUnpack() = default;     // for PcqParamTraits and std::tuple
  PcqTexUnpack(PcqTexUnpack&&)= default;

  mozilla::ipc::PcqStatus Write(mozilla::ipc::ProducerView& aProducerView);

  static mozilla::ipc::PcqStatus
  Read(PcqTexUnpack* aPcqTexUnpack, mozilla::ipc::ConsumerView& aConsumerView);

  size_t MinSize() const;

 protected:
  PcqTexUnpack(const PcqTexUnpack&) = delete;
  PcqTexUnpack& operator=(const PcqTexUnpack&) = delete;

  friend mozilla::ipc::PcqParamTraits<PcqTexUnpack>;
  MaybeWebGLTexUnpackVariant mMaybeBlob;
};

}  // namespace mozilla

#endif  // TEX_UNPACK_BLOB_H_
