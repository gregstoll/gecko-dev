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
struct IsTriviallySerializable<WebGLContextOptions> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLPixelStore> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexImageData> : TrueType {};

template <>
struct IsTriviallySerializable<WebGLTexPboOffset> : TrueType {};

template <>
struct IsTriviallySerializable<SetDimensionsData> : TrueType {};

template <>
struct IsTriviallySerializable<ICRData> : TrueType {};

template <>
struct IsTriviallySerializable<gfx::IntSize> : TrueType {};

template <>
struct IsTriviallySerializable<webgl::TexUnpackBlob> : TrueType {};

template <>
struct PcqParamTraits<ExtensionSets> {
  using ParamType = ExtensionSets;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    PcqStatus status = aProducerView.WriteParam(aArg.mNonSystem);
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mSystem) : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    PcqStatus status =
        aConsumerView.ReadParam(aArg ? &aArg->mNonSystem : nullptr);
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mSystem : nullptr)
               : status;
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
    PcqStatus status = aProducerView.WriteParam(aArg.mElemCount);
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mElemType) : status;
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseUserName)
                               : status;
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mIsArray) : status;
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mElemSize) : status;
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseMappedName)
                               : status;
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mBaseType)
                             : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    PcqStatus status =
        aConsumerView.ReadParam(aArg ? &aArg->mElemCount : nullptr);
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mElemType : nullptr)
                 : status;
    status =
        IsSuccess(status)
            ? aConsumerView.ReadParam(aArg ? &aArg->mBaseUserName : nullptr)
            : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mIsArray : nullptr)
                 : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mElemSize : nullptr)
                 : status;
    status =
        IsSuccess(status)
            ? aConsumerView.ReadParam(aArg ? &aArg->mBaseMappedName : nullptr)
            : status;
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mBaseType : nullptr)
               : status;
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
    PcqStatus status = aProducerView.WriteParam(aArg.mLength);
    return ((aArg.mLength > 0) && IsSuccess(status))
               ? aProducerView.Write(aArg.mData, aArg.mLength * sizeof(T))
               : status;
  }

  template <typename ElementType =
                typename RemoveCV<typename ParamType::ElementType>::Type>
  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    size_t len;
    PcqStatus status = aConsumerView.ReadParam(&len);
    if ((len == 0) || (!IsSuccess(status))) {
      return status;
    }

    if (!aArg) {
      return aConsumerView.Read(nullptr, len * sizeof(T));
    }

    struct RawBufferReadMatcher {
      PcqStatus operator()(RefPtr<mozilla::ipc::SharedMemoryBasic>& smem) {
        if (!smem) {
          return PcqStatus::PcqFatalError;
        }
        mArg->mSmem = smem;
        mArg->mData = static_cast<ElementType*>(smem->memory());
        mArg->mLength = mLength;
        mArg->mOwnsData = false;
        return PcqStatus::Success;
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

enum TexUnpackTypes : uint8_t { Bytes, Surface, Image, Pbo };

template <>
struct PcqParamTraits<webgl::TexUnpackBytes> {
  using ParamType = webgl::TexUnpackBytes;

  static PcqStatus Write(ProducerView& aProducerView, const ParamType& aArg) {
    // Write TexUnpackBlob base class, then the RawBuffer.
    PcqStatus status = aProducerView.WriteParam(
        static_cast<const webgl::TexUnpackBlob&>(aArg));
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mPtr) : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    // Read TexUnpackBlob base class, then the RawBuffer.
    PcqStatus status =
        aConsumerView.ReadParam(static_cast<webgl::TexUnpackBlob*>(aArg));
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mPtr : nullptr)
               : status;
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
    PcqStatus status = aProducerView.WriteParam(
        static_cast<const webgl::TexUnpackBlob&>(aArg));
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mSize) : status;
    status =
        IsSuccess(status) ? aProducerView.WriteParam(aArg.mFormat) : status;
    status = IsSuccess(status) ? aProducerView.WriteParam(aArg.mData) : status;
    return IsSuccess(status) ? aProducerView.WriteParam(aArg.mStride) : status;
  }

  static PcqStatus Read(ConsumerView& aConsumerView, ParamType* aArg) {
    PcqStatus status =
        aConsumerView.ReadParam(static_cast<webgl::TexUnpackBlob*>(aArg));
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mSize : nullptr)
                 : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mFormat : nullptr)
                 : status;
    status = IsSuccess(status)
                 ? aConsumerView.ReadParam(aArg ? &aArg->mData : nullptr)
                 : status;
    return IsSuccess(status)
               ? aConsumerView.ReadParam(aArg ? &aArg->mStride : nullptr)
               : status;
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
        PcqStatus status = mProducerView.WriteParam(TexUnpackTypes::Bytes);
        return IsSuccess(status) ? mProducerView.WriteParam(x) : status;
      }
      PcqStatus operator()(const UniquePtr<webgl::TexUnpackSurface>& x) {
        PcqStatus status = mProducerView.WriteParam(TexUnpackTypes::Surface);
        return IsSuccess(status) ? mProducerView.WriteParam(x) : status;
      }
      PcqStatus operator()(const UniquePtr<webgl::TexUnpackImage>& x) {
        MOZ_ASSERT_UNREACHABLE("TODO:");
        return PcqStatus::PcqFatalError;
      }
      PcqStatus operator()(const WebGLTexPboOffset& x) {
        PcqStatus status = mProducerView.WriteParam(TexUnpackTypes::Pbo);
        return IsSuccess(status) ? mProducerView.WriteParam(x) : status;
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
    PcqStatus status = aConsumerView.ReadParam(&unpackType);
    if (!IsSuccess(status)) {
      return status;
    }
    switch (unpackType) {
      case TexUnpackTypes::Bytes:
        *aArg = AsVariant(UniquePtr<webgl::TexUnpackBytes>());
        status = aConsumerView.ReadParam(
            &aArg->as<UniquePtr<webgl::TexUnpackBytes>>());
        return status;
      case TexUnpackTypes::Surface:
        *aArg = AsVariant(UniquePtr<webgl::TexUnpackSurface>());
        status = aConsumerView.ReadParam(
            &aArg->as<UniquePtr<webgl::TexUnpackSurface>>());
        return status;
      case TexUnpackTypes::Image:
        MOZ_ASSERT_UNREACHABLE("TODO:");
        return PcqStatus::PcqFatalError;
      case TexUnpackTypes::Pbo:
        *aArg = AsVariant(WebGLTexPboOffset());
        status = aConsumerView.ReadParam(&aArg->as<WebGLTexPboOffset>());
        return status;
    }
    MOZ_ASSERT_UNREACHABLE("Illegal texture unpack type");
    return PcqStatus::PcqFatalError;
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
