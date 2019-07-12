/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLPCQPARAMTRAITS_H_
#define WEBGLPCQPARAMTRAITS_H_

#include "mozilla/ipc/ProducerConsumerQueue.h"
#include "TexUnpackBlob.h"
#include "WebGLActiveInfo.h"
#include "WebGLContext.h"
#include "WebGLTypes.h"

namespace mozilla {

namespace ipc {
template <typename T>
struct PcqParamTraits;

template <typename WebGLType>
struct IsTriviallySerializable<WebGLId<WebGLType>> : TrueType {};

template <>
struct IsTriviallySerializable<FloatOrInt> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLShaderPrecisionFormat> : TrueType {};

template <>
struct IsTriviallySerializable<SimpleWebGLContextOptions> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLPixelStore> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexImageData> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexPboOffset> : TrueType {};

template <>
struct IsTriviallySerializable<ICRData> : TrueType {};

template <>
struct IsTriviallySerializable<gfx::IntSize> : TrueType {};

template <>
struct IsTriviallySerializable<SyncResponse> : TrueType {};

template <>
struct PcqParamTraits<WebGLContextOptions> {
  using ParamType = WebGLContextOptions;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(
        static_cast<const SimpleWebGLContextOptions&>(aArg));
    aProducerView.WriteParam(aArg.rendererStringOverride);
    return aProducerView.WriteParam(aArg.vendorStringOverride);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg ? static_cast<SimpleWebGLContextOptions*>(aArg)
                                 : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->rendererStringOverride : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->vendorStringOverride
                                        : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(
               aArg ? static_cast<const SimpleWebGLContextOptions*>(aArg)
                    : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->rendererStringOverride : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->vendorStringOverride : nullptr);
  }
};

template <>
struct PcqParamTraits<SetDimensionsData> {
  using ParamType = SetDimensionsData;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.mOptions);
    aProducerView.WriteParam(aArg.mOptionsFrozen);
    aProducerView.WriteParam(aArg.mResetLayer);
    aProducerView.WriteParam(aArg.mMaybeLostOldContext);
    aProducerView.WriteParam(aArg.mResult);
    return aProducerView.WriteParam(aArg.mPixelStore);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg ? &aArg->mOptions : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mOptionsFrozen : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mResetLayer : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mMaybeLostOldContext : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mResult : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mPixelStore : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? &aArg->mOptions : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mOptionsFrozen : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mResetLayer : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mMaybeLostOldContext : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mResult : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mPixelStore : nullptr);
  }
};

template <>
struct PcqParamTraits<ExtensionSets> {
  using ParamType = ExtensionSets;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.mNonSystem);
    return aProducerView.WriteParam(aArg.mSystem);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg ? &aArg->mNonSystem : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mSystem : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? (&aArg->mNonSystem) : nullptr) +
           aView.MinSizeParam(aArg ? (&aArg->mSystem) : nullptr);
  }
};

template <>
struct PcqParamTraits<WebGLActiveInfo> {
  using ParamType = WebGLActiveInfo;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.mElemCount);
    aProducerView.WriteParam(aArg.mElemType);
    aProducerView.WriteParam(aArg.mBaseUserName);
    aProducerView.WriteParam(aArg.mIsArray);
    aProducerView.WriteParam(aArg.mElemSize);
    aProducerView.WriteParam(aArg.mBaseMappedName);
    return aProducerView.WriteParam(aArg.mBaseType);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg ? &aArg->mElemCount : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mElemType : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mBaseUserName : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mIsArray : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mElemSize : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mBaseMappedName : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mBaseType : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? &aArg->mElemCount : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mElemType : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseUserName : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mIsArray : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mElemSize : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseMappedName : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mBaseType : nullptr);
  }
};

template <typename T>
struct PcqParamTraits<RawBuffer<T>> {
  using ParamType = RawBuffer<T>;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.mLength);
    return (aArg.mLength > 0)
               ? aProducerView.Write(aArg.mData, aArg.mLength * sizeof(T))
               : aProducerView.GetStatus();
  }

  template <typename ElementType =
                typename RemoveCV<typename ParamType::ElementType>::Type>
  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    size_t len;
    PcqStatus status = aConsumerView.ReadParam(&len);
    if (!status) {
      return status;
    }

    if (len == 0) {
      if (aArg) {
        aArg->mLength = 0;
        aArg->mData = nullptr;
        aArg->mSmem = nullptr;
        aArg->mOwnsData = false;
      }
      return PcqStatus::kSuccess;
    }

    if (!aArg) {
      return aConsumerView.Read(nullptr, len * sizeof(T));
    }

    struct RawBufferReadMatcher {
      PcqStatus operator()(RefPtr<mozilla::ipc::SharedMemoryBasic>& smem) {
        if (!smem) {
          return PcqStatus::kFatalError;
        }
        mArg->mSmem = smem;
        mArg->mData = static_cast<ElementType*>(smem->memory());
        mArg->mLength = mLength;
        mArg->mOwnsData = false;
        return PcqStatus::kSuccess;
      }
      PcqStatus operator()() {
        mArg->mSmem = nullptr;
        ElementType* buf = new ElementType[mLength];
        mArg->mData = buf;
        mArg->mLength = mLength;
        mArg->mOwnsData = true;
        return mConsumerView.Read(buf, mLength * sizeof(T));
      }

      ConsumerView& mConsumerView;
      ParamType* mArg;
      size_t mLength;
    };

    return aConsumerView.ReadVariant(
        len * sizeof(T), RawBufferReadMatcher{aConsumerView, aArg, len});
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.template MinSizeParam<size_t>() +
           aView.MinSizeBytes(aArg ? aArg->mLength * sizeof(T) : 0);
  }
};

template <>
struct PcqParamTraits<RawSurface> {
  using ParamType = RawSurface;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(static_cast<const RawBuffer<>&>(aArg));
    aProducerView.WriteParam(aArg.mStride);
    aProducerView.WriteParam(aArg.mSize);
    return aProducerView.WriteParam(aArg.mFormat);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(static_cast<RawBuffer<>*>(aArg));
    aConsumerView.ReadParam(aArg ? &aArg->mStride : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mSize : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mFormat : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(static_cast<const RawBuffer<>*>(aArg)) +
           aView.MinSizeParam(aArg ? &aArg->mStride : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mSize : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mFormat : nullptr);
  }
};

// NB: TexUnpackBlob is not IsTriviallySerializable because
// it has a vtable.
template <>
struct PcqParamTraits<webgl::TexUnpackBlob> {
  using ParamType = webgl::TexUnpackBlob;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.mAlignment);
    aProducerView.WriteParam(aArg.mRowLength);
    aProducerView.WriteParam(aArg.mImageHeight);
    aProducerView.WriteParam(aArg.mSkipPixels);
    aProducerView.WriteParam(aArg.mSkipRows);
    aProducerView.WriteParam(aArg.mSkipImages);
    aProducerView.WriteParam(aArg.mWidth);
    aProducerView.WriteParam(aArg.mHeight);
    aProducerView.WriteParam(aArg.mDepth);
    aProducerView.WriteParam(aArg.mSrcAlphaType);
    return aProducerView.WriteParam(aArg.mNeedsExactUpload);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg ? &aArg->mAlignment : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mRowLength : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mImageHeight : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mSkipPixels : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mSkipRows : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mSkipImages : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mWidth : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mHeight : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mDepth : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mSrcAlphaType : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mNeedsExactUpload : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(aArg ? &aArg->mAlignment : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mRowLength : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mImageHeight : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mSkipPixels : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mSkipRows : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mSkipImages : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mWidth : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mHeight : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mDepth : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mSrcAlphaType : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mNeedsExactUpload : nullptr);
  }
};

enum TexUnpackTypes : uint8_t { Bytes, Surface, Image, Pbo };

template <>
struct PcqParamTraits<webgl::TexUnpackBytes> {
  using ParamType = webgl::TexUnpackBytes;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    // Write TexUnpackBlob base class, then the RawBuffer.
    aProducerView.WriteParam(static_cast<const webgl::TexUnpackBlob&>(aArg));
    return aProducerView.WriteParam(aArg.mPtr);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    // Read TexUnpackBlob base class, then the RawBuffer.
    aConsumerView.ReadParam(static_cast<webgl::TexUnpackBlob*>(aArg));
    return aConsumerView.ReadParam(aArg ? &aArg->mPtr : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(static_cast<const webgl::TexUnpackBlob*>(aArg)) +
           aView.MinSizeParam(aArg ? &aArg->mPtr : nullptr);
  }
};

template <>
struct PcqParamTraits<webgl::TexUnpackSurface> {
  using ParamType = webgl::TexUnpackSurface;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(static_cast<const webgl::TexUnpackBlob&>(aArg));
    aProducerView.WriteParam(aArg.mSize);
    aProducerView.WriteParam(aArg.mFormat);
    aProducerView.WriteParam(aArg.mData);
    return aProducerView.WriteParam(aArg.mStride);
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(static_cast<webgl::TexUnpackBlob*>(aArg));
    aConsumerView.ReadParam(aArg ? &aArg->mSize : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mFormat : nullptr);
    aConsumerView.ReadParam(aArg ? &aArg->mData : nullptr);
    return aConsumerView.ReadParam(aArg ? &aArg->mStride : nullptr);
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    return aView.MinSizeParam(static_cast<const webgl::TexUnpackBlob*>(aArg)) +
           aView.MinSizeParam(aArg ? &aArg->mSize : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mFormat : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mData : nullptr) +
           aView.MinSizeParam(aArg ? &aArg->mStride : nullptr);
  }
};

// Specialization of PcqParamTraits that adapts the TexUnpack type in order to
// efficiently convert types.  For example, a TexUnpackSurface may deserialize
// as a TexUnpackBytes.
template <>
struct PcqParamTraits<WebGLTexUnpackVariant> {
  using ParamType = WebGLTexUnpackVariant;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    struct TexUnpackWriteMatcher {
      PcqStatus operator()(const UniquePtr<webgl::TexUnpackBytes>& x) {
        mProducerView.WriteParam(TexUnpackTypes::Bytes);
        return mProducerView.WriteParam(x);
      }
      PcqStatus operator()(const UniquePtr<webgl::TexUnpackSurface>& x) {
        mProducerView.WriteParam(TexUnpackTypes::Surface);
        return mProducerView.WriteParam(x);
      }
      PcqStatus operator()(const UniquePtr<webgl::TexUnpackImage>& x) {
        MOZ_ASSERT_UNREACHABLE("TODO:");
        return PcqStatus::kFatalError;
      }
      PcqStatus operator()(const WebGLTexPboOffset& x) {
        mProducerView.WriteParam(TexUnpackTypes::Pbo);
        return mProducerView.WriteParam(x);
      }
      ProducerView& mProducerView;
    };
    return aArg.match(TexUnpackWriteMatcher{aProducerView});
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    if (!aArg) {
      // Not a great estimate but we can't do much better.
      return aConsumerView.template ReadParam<TexUnpackTypes>();
    }
    TexUnpackTypes unpackType;
    if (!aConsumerView.ReadParam(&unpackType)) {
      return aConsumerView.GetStatus();
    }
    switch (unpackType) {
      case TexUnpackTypes::Bytes:
        *aArg = AsVariant(UniquePtr<webgl::TexUnpackBytes>());
        return aConsumerView.ReadParam(
            &aArg->as<UniquePtr<webgl::TexUnpackBytes>>());
      case TexUnpackTypes::Surface:
        *aArg = AsVariant(UniquePtr<webgl::TexUnpackSurface>());
        return aConsumerView.ReadParam(
            &aArg->as<UniquePtr<webgl::TexUnpackSurface>>());
      case TexUnpackTypes::Image:
        MOZ_ASSERT_UNREACHABLE("TODO:");
        return PcqStatus::kFatalError;
      case TexUnpackTypes::Pbo:
        *aArg = AsVariant(WebGLTexPboOffset());
        return aConsumerView.ReadParam(&aArg->as<WebGLTexPboOffset>());
    }
    MOZ_ASSERT_UNREACHABLE("Illegal texture unpack type");
    return PcqStatus::kFatalError;
  }

  template <typename View>
  static size_t MinSize(View& aView, const ParamType* aArg) {
    size_t ret = aView.template MinSizeParam<TexUnpackTypes>();
    if (!aArg) {
      return ret;
    }

    struct TexUnpackMinSizeMatcher {
      size_t operator()(const UniquePtr<webgl::TexUnpackBytes>& x) {
        return mView.MinSizeParam(&x);
      }
      size_t operator()(const UniquePtr<webgl::TexUnpackSurface>& x) {
        return mView.MinSizeParam(&x);
      }
      size_t operator()(const UniquePtr<webgl::TexUnpackImage>& x) {
        MOZ_ASSERT_UNREACHABLE("TODO:");
        return 0;
      }
      size_t operator()(const WebGLTexPboOffset& x) {
        return mView.MinSizeParam(&x);
      }
      View& mView;
    };
    return ret + aArg->match(TexUnpackMinSizeMatcher{aView});
  }
};

}  // namespace ipc
}  // namespace mozilla

#endif  // WEBGLPCQPARAMTRAITS_H_
