/*! A cross-platform unsafe graphics abstraction.
 *
 * This crate defines a set of traits abstracting over modern graphics APIs,
 * with implementations ("backends") for Vulkan, Metal, Direct3D, and GL.
 *
 * `wgpu-hal` is a spiritual successor to
 * [gfx-hal](https://github.com/gfx-rs/gfx), but with reduced scope, and
 * oriented towards WebGPU implementation goals. It has no overhead for
 * validation or tracking, and the API translation overhead is kept to the bare
 * minimum by the design of WebGPU. This API can be used for resource-demanding
 * applications and engines.
 *
 * The `wgpu-hal` crate's main design choices:
 *
 * - Our traits are meant to be *portable*: proper use
 *   should get equivalent results regardless of the backend.
 *
 * - Our traits' contracts are *unsafe*: implementations perform minimal
 *   validation, if any, and incorrect use will often cause undefined behavior.
 *   This allows us to minimize the overhead we impose over the underlying
 *   graphics system. If you need safety, the [`wgpu-core`] crate provides a
 *   safe API for driving `wgpu-hal`, implementing all necessary validation,
 *   resource state tracking, and so on. (Note that `wgpu-core` is designed for
 *   use via FFI; the [`wgpu`] crate provides more idiomatic Rust bindings for
 *   `wgpu-core`.) Or, you can do your own validation.
 *
 * - In the same vein, returned errors *only cover cases the user can't
 *   anticipate*, like running out of memory or losing the device. Any errors
 *   that the user could reasonably anticipate are their responsibility to
 *   avoid. For example, `wgpu-hal` returns no error for mapping a buffer that's
 *   not mappable: as the buffer creator, the user should already know if they
 *   can map it.
 *
 * - We use *static dispatch*. The traits are not
 *   generally object-safe. You must select a specific backend type
 *   like [`vulkan::Api`] or [`metal::Api`], and then use that
 *   according to the main traits, or call backend-specific methods.
 *
 * - We use *idiomatic Rust parameter passing*,
 *   taking objects by reference, returning them by value, and so on,
 *   unlike `wgpu-core`, which refers to objects by ID.
 *
 * - We map buffer contents *persistently*. This means that the buffer
 *   can remain mapped on the CPU while the GPU reads or writes to it.
 *   You must explicitly indicate when data might need to be
 *   transferred between CPU and GPU, if `wgpu-hal` indicates that the
 *   mapping is not coherent (that is, automatically synchronized
 *   between the two devices).
 *
 * - You must record *explicit barriers* between different usages of a
 *   resource. For example, if a buffer is written to by a compute
 *   shader, and then used as and index buffer to a draw call, you
 *   must use [`CommandEncoder::transition_buffers`] between those two
 *   operations.
 *
 * - Pipeline layouts are *explicitly specified* when setting bind
 *   group. Incompatible layouts disturb groups bound at higher indices.
 *
 * - The API *accepts collections as iterators*, to avoid forcing the user to
 *   store data in particular containers. The implementation doesn't guarantee
 *   that any of the iterators are drained, unless stated otherwise by the
 *   function documentation. For this reason, we recommend that iterators don't
 *   do any mutating work.
 *
 * Unfortunately, `wgpu-hal`'s safety requirements are not fully documented.
 * Ideally, all trait methods would have doc comments setting out the
 * requirements users must meet to ensure correct and portable behavior. If you
 * are aware of a specific requirement that a backend imposes that is not
 * ensured by the traits' documented rules, please file an issue. Or, if you are
 * a capable technical writer, please file a pull request!
 *
 * [`wgpu-core`]: https://crates.io/crates/wgpu-core
 * [`wgpu`]: https://crates.io/crates/wgpu
 * [`vulkan::Api`]: vulkan/struct.Api.html
 * [`metal::Api`]: metal/struct.Api.html
 *
 * ## Primary backends
 *
 * The `wgpu-hal` crate has full-featured backends implemented on the following
 * platform graphics APIs:
 *
 * - Vulkan, available on Linux, Android, and Windows, using the [`ash`] crate's
 *   Vulkan bindings. It's also available on macOS, if you install [MoltenVK].
 *
 * - Metal on macOS, using the [`metal`] crate's bindings.
 *
 * - Direct3D 12 on Windows, using the [`d3d12`] crate's bindings.
 *
 * [`ash`]: https://crates.io/crates/ash
 * [MoltenVK]: https://github.com/KhronosGroup/MoltenVK
 * [`metal`]: https://crates.io/crates/metal
 * [`d3d12`]: ahttps://crates.io/crates/d3d12
 *
 * ## Secondary backends
 *
 * The `wgpu-hal` crate has a partial implementation based on the following
 * platform graphics API:
 *
 * - The GL backend is available anywhere OpenGL, OpenGL ES, or WebGL are
 *   available. See the [`gles`] module documentation for details.
 *
 * [`gles`]: gles/index.html
 *
 * You can see what capabilities an adapter is missing by checking the
 * [`DownlevelCapabilities`][tdc] in [`ExposedAdapter::capabilities`], available
 * from [`Instance::enumerate_adapters`].
 *
 * The API is generally designed to fit the primary backends better than the
 * secondary backends, so the latter may impose more overhead.
 *
 * [tdc]: wgt::DownlevelCapabilities
 *
 * ## Traits
 *
 * The `wgpu-hal` crate defines a handful of traits that together
 * represent a cross-platform abstraction for modern GPU APIs.
 *
 * - The [`Api`] trait represents a `wgpu-hal` backend. It has no methods of its
 *   own, only a collection of associated types.
 *
 * - [`Api::Instance`] implements the [`Instance`] trait. [`Instance::init`]
 *   creates an instance value, which you can use to enumerate the adapters
 *   available on the system. For example, [`vulkan::Api::Instance::init`][Ii]
 *   returns an instance that can enumerate the Vulkan physical devices on your
 *   system.
 *
 * - [`Api::Adapter`] implements the [`Adapter`] trait, representing a
 *   particular device from a particular backend. For example, a Vulkan instance
 *   might have a Lavapipe software adapter and a GPU-based adapter.
 *
 * - [`Api::Device`] implements the [`Device`] trait, representing an active
 *   link to a device. You get a device value by calling [`Adapter::open`], and
 *   then use it to create buffers, textures, shader modules, and so on.
 *
 * - [`Api::Queue`] implements the [`Queue`] trait, which you use to submit
 *   command buffers to a given device.
 *
 * - [`Api::CommandEncoder`] implements the [`CommandEncoder`] trait, which you
 *   use to build buffers of commands to submit to a queue. This has all the
 *   methods for drawing and running compute shaders, which is presumably what
 *   you're here for.
 *
 * - [`Api::Surface`] implements the [`Surface`] trait, which represents a
 *   swapchain for presenting images on the screen, via interaction with the
 *   system's window manager.
 *
 * The [`Api`] trait has various other associated types like [`Api::Buffer`] and
 * [`Api::Texture`] that represent resources the rest of the interface can
 * operate on, but these generally do not have their own traits.
 *
 * [Ii]: Instance::init
 *
 * ## Validation is the calling code's responsibility, not `wgpu-hal`'s
 *
 * As much as possible, `wgpu-hal` traits place the burden of validation,
 * resource tracking, and state tracking on the caller, not on the trait
 * implementations themselves. Anything which can reasonably be handled in
 * backend-independent code should be. A `wgpu_hal` backend's sole obligation is
 * to provide portable behavior, and report conditions that the calling code
 * can't reasonably anticipate, like device loss or running out of memory.
 *
 * The `wgpu` crate collection is intended for use in security-sensitive
 * applications, like web browsers, where the API is available to untrusted
 * code. This means that `wgpu-core`'s validation is not simply a service to
 * developers, to be provided opportunistically when the performance costs are
 * acceptable and the necessary data is ready at hand. Rather, `wgpu-core`'s
 * validation must be exhaustive, to ensure that even malicious content cannot
 * provoke and exploit undefined behavior in the platform's graphics API.
 *
 * Because graphics APIs' requirements are complex, the only practical way for
 * `wgpu` to provide exhaustive validation is to comprehensively track the
 * lifetime and state of all the resources in the system. Implementing this
 * separately for each backend is infeasible; effort would be better spent
 * making the cross-platform validation in `wgpu-core` legible and trustworthy.
 * Fortunately, the requirements are largely similar across the various
 * platforms, so cross-platform validation is practical.
 *
 * Some backends have specific requirements that aren't practical to foist off
 * on the `wgpu-hal` user. For example, properly managing macOS Objective-C or
 * Microsoft COM reference counts is best handled by using appropriate pointer
 * types within the backend.
 *
 * A desire for "defense in depth" may suggest performing additional validation
 * in `wgpu-hal` when the opportunity arises, but this must be done with
 * caution. Even experienced contributors infer the expectations their changes
 * must meet by considering not just requirements made explicit in types, tests,
 * assertions, and comments, but also those implicit in the surrounding code.
 * When one sees validation or state-tracking code in `wgpu-hal`, it is tempting
 * to conclude, "Oh, `wgpu-hal` checks for this, so `wgpu-core` needn't worry
 * about it - that would be redundant!" The responsibility for exhaustive
 * validation always rests with `wgpu-core`, regardless of what may or may not
 * be checked in `wgpu-hal`.
 *
 * To this end, any "defense in depth" validation that does appear in `wgpu-hal`
 * for requirements that `wgpu-core` should have enforced should report failure
 * via the `unreachable!` macro, because problems detected at this stage always
 * indicate a bug in `wgpu-core`.
 *
 * ## Debugging
 *
 * Most of the information on the wiki [Debugging wgpu Applications][wiki-debug]
 * page still applies to this API, with the exception of API tracing/replay
 * functionality, which is only available in `wgpu-core`.
 *
 * [wiki-debug]: https://github.com/gfx-rs/wgpu/wiki/Debugging-wgpu-Applications
 */

#![cfg_attr(docsrs, feature(doc_cfg, doc_auto_cfg))]
#![allow(
    // this happens on the GL backend, where it is both thread safe and non-thread safe in the same code.
    clippy::arc_with_non_send_sync,
    // for `if_then_panic` until it reaches stable
    unknown_lints,
    // We use loops for getting early-out of scope without closures.
    clippy::never_loop,
    // We don't use syntax sugar where it's not necessary.
    clippy::match_like_matches_macro,
    // Redundant matching is more explicit.
    clippy::redundant_pattern_matching,
    // Explicit lifetimes are often easier to reason about.
    clippy::needless_lifetimes,
    // No need for defaults in the internal types.
    clippy::new_without_default,
    // Matches are good and extendable, no need to make an exception here.
    clippy::single_match,
    // Push commands are more regular than macros.
    clippy::vec_init_then_push,
    // "if panic" is a good uniform construct.
    clippy::if_then_panic,
    // We unsafe impl `Send` for a reason.
    clippy::non_send_fields_in_send_ty,
    // TODO!
    clippy::missing_safety_doc,
    // Clashes with clippy::pattern_type_mismatch
    clippy::needless_borrowed_reference,
)]
#![warn(
    trivial_casts,
    trivial_numeric_casts,
    unsafe_op_in_unsafe_fn,
    unused_extern_crates,
    unused_qualifications,
    // We don't match on a reference, unless required.
    clippy::pattern_type_mismatch,
)]

/// DirectX12 API internals.
#[cfg(dx12)]
pub mod dx12;
/// A dummy API implementation.
pub mod empty;
/// GLES API internals.
#[cfg(gles)]
pub mod gles;
/// Metal API internals.
#[cfg(metal)]
pub mod metal;
/// Vulkan API internals.
#[cfg(vulkan)]
pub mod vulkan;

pub mod auxil;
pub mod api {
    #[cfg(dx12)]
    pub use super::dx12::Api as Dx12;
    pub use super::empty::Api as Empty;
    #[cfg(gles)]
    pub use super::gles::Api as Gles;
    #[cfg(metal)]
    pub use super::metal::Api as Metal;
    #[cfg(vulkan)]
    pub use super::vulkan::Api as Vulkan;
}

use std::{
    borrow::{Borrow, Cow},
    fmt,
    num::NonZeroU32,
    ops::{Range, RangeInclusive},
    ptr::NonNull,
    sync::Arc,
};

use bitflags::bitflags;
use parking_lot::Mutex;
use thiserror::Error;
use wgt::WasmNotSendSync;

// - Vertex + Fragment
// - Compute
pub const MAX_CONCURRENT_SHADER_STAGES: usize = 2;
pub const MAX_ANISOTROPY: u8 = 16;
pub const MAX_BIND_GROUPS: usize = 8;
pub const MAX_VERTEX_BUFFERS: usize = 16;
pub const MAX_COLOR_ATTACHMENTS: usize = 8;
pub const MAX_MIP_LEVELS: u32 = 16;
/// Size of a single occlusion/timestamp query, when copied into a buffer, in bytes.
pub const QUERY_SIZE: wgt::BufferAddress = 8;

pub type Label<'a> = Option<&'a str>;
pub type MemoryRange = Range<wgt::BufferAddress>;
pub type FenceValue = u64;

/// Drop guard to signal wgpu-hal is no longer using an externally created object.
pub type DropGuard = Box<dyn std::any::Any + Send + Sync>;

#[derive(Clone, Debug, PartialEq, Eq, Error)]
pub enum DeviceError {
    #[error("Out of memory")]
    OutOfMemory,
    #[error("Device is lost")]
    Lost,
    #[error("Creation of a resource failed for a reason other than running out of memory.")]
    ResourceCreationFailed,
}

#[derive(Clone, Debug, Eq, PartialEq, Error)]
pub enum ShaderError {
    #[error("Compilation failed: {0:?}")]
    Compilation(String),
    #[error(transparent)]
    Device(#[from] DeviceError),
}

#[derive(Clone, Debug, Eq, PartialEq, Error)]
pub enum PipelineError {
    #[error("Linkage failed for stage {0:?}: {1}")]
    Linkage(wgt::ShaderStages, String),
    #[error("Entry point for stage {0:?} is invalid")]
    EntryPoint(naga::ShaderStage),
    #[error(transparent)]
    Device(#[from] DeviceError),
}

#[derive(Clone, Debug, Eq, PartialEq, Error)]
pub enum SurfaceError {
    #[error("Surface is lost")]
    Lost,
    #[error("Surface is outdated, needs to be re-created")]
    Outdated,
    #[error(transparent)]
    Device(#[from] DeviceError),
    #[error("Other reason: {0}")]
    Other(&'static str),
}

/// Error occurring while trying to create an instance, or create a surface from an instance;
/// typically relating to the state of the underlying graphics API or hardware.
#[derive(Clone, Debug, Error)]
#[error("{message}")]
pub struct InstanceError {
    /// These errors are very platform specific, so do not attempt to encode them as an enum.
    ///
    /// This message should describe the problem in sufficient detail to be useful for a
    /// user-to-developer “why won't this work on my machine” bug report, and otherwise follow
    /// <https://rust-lang.github.io/api-guidelines/interoperability.html#error-types-are-meaningful-and-well-behaved-c-good-err>.
    message: String,

    /// Underlying error value, if any is available.
    #[source]
    source: Option<Arc<dyn std::error::Error + Send + Sync + 'static>>,
}

impl InstanceError {
    #[allow(dead_code)] // may be unused on some platforms
    pub(crate) fn new(message: String) -> Self {
        Self {
            message,
            source: None,
        }
    }
    #[allow(dead_code)] // may be unused on some platforms
    pub(crate) fn with_source(
        message: String,
        source: impl std::error::Error + Send + Sync + 'static,
    ) -> Self {
        Self {
            message,
            source: Some(Arc::new(source)),
        }
    }
}

pub trait Api: Clone + fmt::Debug + Sized {
    type Instance: Instance<A = Self>;
    type Surface: Surface<A = Self>;
    type Adapter: Adapter<A = Self>;
    type Device: Device<A = Self>;

    type Queue: Queue<A = Self>;
    type CommandEncoder: CommandEncoder<A = Self>;

    /// This API's command buffer type.
    ///
    /// The only thing you can do with `CommandBuffer`s is build them
    /// with a [`CommandEncoder`] and then pass them to
    /// [`Queue::submit`] for execution, or destroy them by passing
    /// them to [`CommandEncoder::reset_all`].
    ///
    /// [`CommandEncoder`]: Api::CommandEncoder
    type CommandBuffer: WasmNotSendSync + fmt::Debug;

    type Buffer: fmt::Debug + WasmNotSendSync + 'static;
    type Texture: fmt::Debug + WasmNotSendSync + 'static;
    type SurfaceTexture: fmt::Debug + WasmNotSendSync + Borrow<Self::Texture>;
    type TextureView: fmt::Debug + WasmNotSendSync;
    type Sampler: fmt::Debug + WasmNotSendSync;
    type QuerySet: fmt::Debug + WasmNotSendSync;
    type Fence: fmt::Debug + WasmNotSendSync;

    type BindGroupLayout: fmt::Debug + WasmNotSendSync;
    type BindGroup: fmt::Debug + WasmNotSendSync;
    type PipelineLayout: fmt::Debug + WasmNotSendSync;
    type ShaderModule: fmt::Debug + WasmNotSendSync;
    type RenderPipeline: fmt::Debug + WasmNotSendSync;
    type ComputePipeline: fmt::Debug + WasmNotSendSync;

    type AccelerationStructure: fmt::Debug + WasmNotSendSync + 'static;
}

pub trait Instance: Sized + WasmNotSendSync {
    type A: Api;

    unsafe fn init(desc: &InstanceDescriptor) -> Result<Self, InstanceError>;
    unsafe fn create_surface(
        &self,
        display_handle: raw_window_handle::RawDisplayHandle,
        window_handle: raw_window_handle::RawWindowHandle,
    ) -> Result<<Self::A as Api>::Surface, InstanceError>;
    unsafe fn destroy_surface(&self, surface: <Self::A as Api>::Surface);
    unsafe fn enumerate_adapters(&self) -> Vec<ExposedAdapter<Self::A>>;
}

pub trait Surface: WasmNotSendSync {
    type A: Api;

    /// Configures the surface to use the given device.
    ///
    /// # Safety
    ///
    /// - All gpu work that uses the surface must have been completed.
    /// - All [`AcquiredSurfaceTexture`]s must have been destroyed.
    /// - All [`Api::TextureView`]s derived from the [`AcquiredSurfaceTexture`]s must have been destroyed.
    /// - All surfaces created using other devices must have been unconfigured before this call.
    unsafe fn configure(
        &self,
        device: &<Self::A as Api>::Device,
        config: &SurfaceConfiguration,
    ) -> Result<(), SurfaceError>;

    /// Unconfigures the surface on the given device.
    ///
    /// # Safety
    ///
    /// - All gpu work that uses the surface must have been completed.
    /// - All [`AcquiredSurfaceTexture`]s must have been destroyed.
    /// - All [`Api::TextureView`]s derived from the [`AcquiredSurfaceTexture`]s must have been destroyed.
    /// - The surface must have been configured on the given device.
    unsafe fn unconfigure(&self, device: &<Self::A as Api>::Device);

    /// Returns the next texture to be presented by the swapchain for drawing
    ///
    /// A `timeout` of `None` means to wait indefinitely, with no timeout.
    ///
    /// # Portability
    ///
    /// Some backends can't support a timeout when acquiring a texture and
    /// the timeout will be ignored.
    ///
    /// Returns `None` on timing out.
    unsafe fn acquire_texture(
        &self,
        timeout: Option<std::time::Duration>,
    ) -> Result<Option<AcquiredSurfaceTexture<Self::A>>, SurfaceError>;
    unsafe fn discard_texture(&self, texture: <Self::A as Api>::SurfaceTexture);
}

pub trait Adapter: WasmNotSendSync {
    type A: Api;

    unsafe fn open(
        &self,
        features: wgt::Features,
        limits: &wgt::Limits,
    ) -> Result<OpenDevice<Self::A>, DeviceError>;

    /// Return the set of supported capabilities for a texture format.
    unsafe fn texture_format_capabilities(
        &self,
        format: wgt::TextureFormat,
    ) -> TextureFormatCapabilities;

    /// Returns the capabilities of working with a specified surface.
    ///
    /// `None` means presentation is not supported for it.
    unsafe fn surface_capabilities(
        &self,
        surface: &<Self::A as Api>::Surface,
    ) -> Option<SurfaceCapabilities>;

    /// Creates a [`PresentationTimestamp`] using the adapter's WSI.
    ///
    /// [`PresentationTimestamp`]: wgt::PresentationTimestamp
    unsafe fn get_presentation_timestamp(&self) -> wgt::PresentationTimestamp;
}

pub trait Device: WasmNotSendSync {
    type A: Api;

    /// Exit connection to this logical device.
    unsafe fn exit(self, queue: <Self::A as Api>::Queue);
    /// Creates a new buffer.
    ///
    /// The initial usage is `BufferUses::empty()`.
    unsafe fn create_buffer(
        &self,
        desc: &BufferDescriptor,
    ) -> Result<<Self::A as Api>::Buffer, DeviceError>;
    unsafe fn destroy_buffer(&self, buffer: <Self::A as Api>::Buffer);
    //TODO: clarify if zero-sized mapping is allowed
    unsafe fn map_buffer(
        &self,
        buffer: &<Self::A as Api>::Buffer,
        range: MemoryRange,
    ) -> Result<BufferMapping, DeviceError>;
    unsafe fn unmap_buffer(&self, buffer: &<Self::A as Api>::Buffer) -> Result<(), DeviceError>;
    unsafe fn flush_mapped_ranges<I>(&self, buffer: &<Self::A as Api>::Buffer, ranges: I)
    where
        I: Iterator<Item = MemoryRange>;
    unsafe fn invalidate_mapped_ranges<I>(&self, buffer: &<Self::A as Api>::Buffer, ranges: I)
    where
        I: Iterator<Item = MemoryRange>;

    /// Creates a new texture.
    ///
    /// The initial usage for all subresources is `TextureUses::UNINITIALIZED`.
    unsafe fn create_texture(
        &self,
        desc: &TextureDescriptor,
    ) -> Result<<Self::A as Api>::Texture, DeviceError>;
    unsafe fn destroy_texture(&self, texture: <Self::A as Api>::Texture);
    unsafe fn create_texture_view(
        &self,
        texture: &<Self::A as Api>::Texture,
        desc: &TextureViewDescriptor,
    ) -> Result<<Self::A as Api>::TextureView, DeviceError>;
    unsafe fn destroy_texture_view(&self, view: <Self::A as Api>::TextureView);
    unsafe fn create_sampler(
        &self,
        desc: &SamplerDescriptor,
    ) -> Result<<Self::A as Api>::Sampler, DeviceError>;
    unsafe fn destroy_sampler(&self, sampler: <Self::A as Api>::Sampler);

    /// Create a fresh [`CommandEncoder`].
    ///
    /// The new `CommandEncoder` is in the "closed" state.
    unsafe fn create_command_encoder(
        &self,
        desc: &CommandEncoderDescriptor<Self::A>,
    ) -> Result<<Self::A as Api>::CommandEncoder, DeviceError>;
    unsafe fn destroy_command_encoder(&self, pool: <Self::A as Api>::CommandEncoder);

    /// Creates a bind group layout.
    unsafe fn create_bind_group_layout(
        &self,
        desc: &BindGroupLayoutDescriptor,
    ) -> Result<<Self::A as Api>::BindGroupLayout, DeviceError>;
    unsafe fn destroy_bind_group_layout(&self, bg_layout: <Self::A as Api>::BindGroupLayout);
    unsafe fn create_pipeline_layout(
        &self,
        desc: &PipelineLayoutDescriptor<Self::A>,
    ) -> Result<<Self::A as Api>::PipelineLayout, DeviceError>;
    unsafe fn destroy_pipeline_layout(&self, pipeline_layout: <Self::A as Api>::PipelineLayout);
    unsafe fn create_bind_group(
        &self,
        desc: &BindGroupDescriptor<Self::A>,
    ) -> Result<<Self::A as Api>::BindGroup, DeviceError>;
    unsafe fn destroy_bind_group(&self, group: <Self::A as Api>::BindGroup);

    unsafe fn create_shader_module(
        &self,
        desc: &ShaderModuleDescriptor,
        shader: ShaderInput,
    ) -> Result<<Self::A as Api>::ShaderModule, ShaderError>;
    unsafe fn destroy_shader_module(&self, module: <Self::A as Api>::ShaderModule);
    unsafe fn create_render_pipeline(
        &self,
        desc: &RenderPipelineDescriptor<Self::A>,
    ) -> Result<<Self::A as Api>::RenderPipeline, PipelineError>;
    unsafe fn destroy_render_pipeline(&self, pipeline: <Self::A as Api>::RenderPipeline);
    unsafe fn create_compute_pipeline(
        &self,
        desc: &ComputePipelineDescriptor<Self::A>,
    ) -> Result<<Self::A as Api>::ComputePipeline, PipelineError>;
    unsafe fn destroy_compute_pipeline(&self, pipeline: <Self::A as Api>::ComputePipeline);

    unsafe fn create_query_set(
        &self,
        desc: &wgt::QuerySetDescriptor<Label>,
    ) -> Result<<Self::A as Api>::QuerySet, DeviceError>;
    unsafe fn destroy_query_set(&self, set: <Self::A as Api>::QuerySet);
    unsafe fn create_fence(&self) -> Result<<Self::A as Api>::Fence, DeviceError>;
    unsafe fn destroy_fence(&self, fence: <Self::A as Api>::Fence);
    unsafe fn get_fence_value(
        &self,
        fence: &<Self::A as Api>::Fence,
    ) -> Result<FenceValue, DeviceError>;
    /// Calling wait with a lower value than the current fence value will immediately return.
    unsafe fn wait(
        &self,
        fence: &<Self::A as Api>::Fence,
        value: FenceValue,
        timeout_ms: u32,
    ) -> Result<bool, DeviceError>;

    unsafe fn start_capture(&self) -> bool;
    unsafe fn stop_capture(&self);

    unsafe fn create_acceleration_structure(
        &self,
        desc: &AccelerationStructureDescriptor,
    ) -> Result<<Self::A as Api>::AccelerationStructure, DeviceError>;
    unsafe fn get_acceleration_structure_build_sizes(
        &self,
        desc: &GetAccelerationStructureBuildSizesDescriptor<Self::A>,
    ) -> AccelerationStructureBuildSizes;
    unsafe fn get_acceleration_structure_device_address(
        &self,
        acceleration_structure: &<Self::A as Api>::AccelerationStructure,
    ) -> wgt::BufferAddress;
    unsafe fn destroy_acceleration_structure(
        &self,
        acceleration_structure: <Self::A as Api>::AccelerationStructure,
    );
}

pub trait Queue: WasmNotSendSync {
    type A: Api;

    /// Submits the command buffers for execution on GPU.
    ///
    /// Valid usage:
    ///
    /// - All of the [`CommandBuffer`][cb]s were created from
    ///   [`CommandEncoder`][ce]s that are associated with this queue.
    ///
    /// - All of those [`CommandBuffer`][cb]s must remain alive until
    ///   the submitted commands have finished execution. (Since
    ///   command buffers must not outlive their encoders, this
    ///   implies that the encoders must remain alive as well.)
    ///
    /// - All of the [`SurfaceTexture`][st]s that the command buffers
    ///   write to appear in the `surface_textures` argument.
    ///
    /// [cb]: Api::CommandBuffer
    /// [ce]: Api::CommandEncoder
    /// [st]: Api::SurfaceTexture
    unsafe fn submit(
        &self,
        command_buffers: &[&<Self::A as Api>::CommandBuffer],
        surface_textures: &[&<Self::A as Api>::SurfaceTexture],
        signal_fence: Option<(&mut <Self::A as Api>::Fence, FenceValue)>,
    ) -> Result<(), DeviceError>;
    unsafe fn present(
        &self,
        surface: &<Self::A as Api>::Surface,
        texture: <Self::A as Api>::SurfaceTexture,
    ) -> Result<(), SurfaceError>;
    unsafe fn get_timestamp_period(&self) -> f32;
}

/// Encoder and allocation pool for `CommandBuffer`s.
///
/// A `CommandEncoder` not only constructs `CommandBuffer`s but also
/// acts as the allocation pool that owns the buffers' underlying
/// storage. Thus, `CommandBuffer`s must not outlive the
/// `CommandEncoder` that created them.
///
/// The life cycle of a `CommandBuffer` is as follows:
///
/// - Call [`Device::create_command_encoder`] to create a new
///   `CommandEncoder`, in the "closed" state.
///
/// - Call `begin_encoding` on a closed `CommandEncoder` to begin
///   recording commands. This puts the `CommandEncoder` in the
///   "recording" state.
///
/// - Call methods like `copy_buffer_to_buffer`, `begin_render_pass`,
///   etc. on a "recording" `CommandEncoder` to add commands to the
///   list. (If an error occurs, you must call `discard_encoding`; see
///   below.)
///
/// - Call `end_encoding` on a recording `CommandEncoder` to close the
///   encoder and construct a fresh `CommandBuffer` consisting of the
///   list of commands recorded up to that point.
///
/// - Call `discard_encoding` on a recording `CommandEncoder` to drop
///   the commands recorded thus far and close the encoder. This is
///   the only safe thing to do on a `CommandEncoder` if an error has
///   occurred while recording commands.
///
/// - Call `reset_all` on a closed `CommandEncoder`, passing all the
///   live `CommandBuffers` built from it. All the `CommandBuffer`s
///   are destroyed, and their resources are freed.
///
/// # Safety
///
/// - The `CommandEncoder` must be in the states described above to
///   make the given calls.
///
/// - A `CommandBuffer` that has been submitted for execution on the
///   GPU must live until its execution is complete.
///
/// - A `CommandBuffer` must not outlive the `CommandEncoder` that
///   built it.
///
/// - A `CommandEncoder` must not outlive its `Device`.
///
/// It is the user's responsibility to meet this requirements. This
/// allows `CommandEncoder` implementations to keep their state
/// tracking to a minimum.
pub trait CommandEncoder: WasmNotSendSync + fmt::Debug {
    type A: Api;

    /// Begin encoding a new command buffer.
    ///
    /// This puts this `CommandEncoder` in the "recording" state.
    ///
    /// # Safety
    ///
    /// This `CommandEncoder` must be in the "closed" state.
    unsafe fn begin_encoding(&mut self, label: Label) -> Result<(), DeviceError>;

    /// Discard the command list under construction.
    ///
    /// If an error has occurred while recording commands, this
    /// is the only safe thing to do with the encoder.
    ///
    /// This puts this `CommandEncoder` in the "closed" state.
    ///
    /// # Safety
    ///
    /// This `CommandEncoder` must be in the "recording" state.
    ///
    /// Callers must not assume that implementations of this
    /// function are idempotent, and thus should not call it
    /// multiple times in a row.
    unsafe fn discard_encoding(&mut self);

    /// Return a fresh [`CommandBuffer`] holding the recorded commands.
    ///
    /// The returned [`CommandBuffer`] holds all the commands recorded
    /// on this `CommandEncoder` since the last call to
    /// [`begin_encoding`].
    ///
    /// This puts this `CommandEncoder` in the "closed" state.
    ///
    /// # Safety
    ///
    /// This `CommandEncoder` must be in the "recording" state.
    ///
    /// The returned [`CommandBuffer`] must not outlive this
    /// `CommandEncoder`. Implementations are allowed to build
    /// `CommandBuffer`s that depend on storage owned by this
    /// `CommandEncoder`.
    ///
    /// [`CommandBuffer`]: Api::CommandBuffer
    /// [`begin_encoding`]: CommandEncoder::begin_encoding
    unsafe fn end_encoding(&mut self) -> Result<<Self::A as Api>::CommandBuffer, DeviceError>;

    /// Reclaim all resources belonging to this `CommandEncoder`.
    ///
    /// # Safety
    ///
    /// This `CommandEncoder` must be in the "closed" state.
    ///
    /// The `command_buffers` iterator must produce all the live
    /// [`CommandBuffer`]s built using this `CommandEncoder` --- that
    /// is, every extant `CommandBuffer` returned from `end_encoding`.
    ///
    /// [`CommandBuffer`]: Api::CommandBuffer
    unsafe fn reset_all<I>(&mut self, command_buffers: I)
    where
        I: Iterator<Item = <Self::A as Api>::CommandBuffer>;

    unsafe fn transition_buffers<'a, T>(&mut self, barriers: T)
    where
        T: Iterator<Item = BufferBarrier<'a, Self::A>>;

    unsafe fn transition_textures<'a, T>(&mut self, barriers: T)
    where
        T: Iterator<Item = TextureBarrier<'a, Self::A>>;

    // copy operations

    unsafe fn clear_buffer(&mut self, buffer: &<Self::A as Api>::Buffer, range: MemoryRange);

    unsafe fn copy_buffer_to_buffer<T>(
        &mut self,
        src: &<Self::A as Api>::Buffer,
        dst: &<Self::A as Api>::Buffer,
        regions: T,
    ) where
        T: Iterator<Item = BufferCopy>;

    /// Copy from an external image to an internal texture.
    /// Works with a single array layer.
    /// Note: `dst` current usage has to be `TextureUses::COPY_DST`.
    /// Note: the copy extent is in physical size (rounded to the block size)
    #[cfg(webgl)]
    unsafe fn copy_external_image_to_texture<T>(
        &mut self,
        src: &wgt::ImageCopyExternalImage,
        dst: &<Self::A as Api>::Texture,
        dst_premultiplication: bool,
        regions: T,
    ) where
        T: Iterator<Item = TextureCopy>;

    /// Copy from one texture to another.
    /// Works with a single array layer.
    /// Note: `dst` current usage has to be `TextureUses::COPY_DST`.
    /// Note: the copy extent is in physical size (rounded to the block size)
    unsafe fn copy_texture_to_texture<T>(
        &mut self,
        src: &<Self::A as Api>::Texture,
        src_usage: TextureUses,
        dst: &<Self::A as Api>::Texture,
        regions: T,
    ) where
        T: Iterator<Item = TextureCopy>;

    /// Copy from buffer to texture.
    /// Works with a single array layer.
    /// Note: `dst` current usage has to be `TextureUses::COPY_DST`.
    /// Note: the copy extent is in physical size (rounded to the block size)
    unsafe fn copy_buffer_to_texture<T>(
        &mut self,
        src: &<Self::A as Api>::Buffer,
        dst: &<Self::A as Api>::Texture,
        regions: T,
    ) where
        T: Iterator<Item = BufferTextureCopy>;

    /// Copy from texture to buffer.
    /// Works with a single array layer.
    /// Note: the copy extent is in physical size (rounded to the block size)
    unsafe fn copy_texture_to_buffer<T>(
        &mut self,
        src: &<Self::A as Api>::Texture,
        src_usage: TextureUses,
        dst: &<Self::A as Api>::Buffer,
        regions: T,
    ) where
        T: Iterator<Item = BufferTextureCopy>;

    // pass common

    /// Sets the bind group at `index` to `group`, assuming the layout
    /// of all the preceding groups to be taken from `layout`.
    unsafe fn set_bind_group(
        &mut self,
        layout: &<Self::A as Api>::PipelineLayout,
        index: u32,
        group: &<Self::A as Api>::BindGroup,
        dynamic_offsets: &[wgt::DynamicOffset],
    );

    /// Sets a range in push constant data.
    ///
    /// IMPORTANT: while the data is passed as words, the offset is in bytes!
    ///
    /// # Safety
    ///
    /// - `offset_bytes` must be a multiple of 4.
    /// - The range of push constants written must be valid for the pipeline layout at draw time.
    unsafe fn set_push_constants(
        &mut self,
        layout: &<Self::A as Api>::PipelineLayout,
        stages: wgt::ShaderStages,
        offset_bytes: u32,
        data: &[u32],
    );

    unsafe fn insert_debug_marker(&mut self, label: &str);
    unsafe fn begin_debug_marker(&mut self, group_label: &str);
    unsafe fn end_debug_marker(&mut self);

    // queries

    /// # Safety:
    ///
    /// - If `set` is an occlusion query set, it must be the same one as used in the [`RenderPassDescriptor::occlusion_query_set`] parameter.
    unsafe fn begin_query(&mut self, set: &<Self::A as Api>::QuerySet, index: u32);
    /// # Safety:
    ///
    /// - If `set` is an occlusion query set, it must be the same one as used in the [`RenderPassDescriptor::occlusion_query_set`] parameter.
    unsafe fn end_query(&mut self, set: &<Self::A as Api>::QuerySet, index: u32);
    unsafe fn write_timestamp(&mut self, set: &<Self::A as Api>::QuerySet, index: u32);
    unsafe fn reset_queries(&mut self, set: &<Self::A as Api>::QuerySet, range: Range<u32>);
    unsafe fn copy_query_results(
        &mut self,
        set: &<Self::A as Api>::QuerySet,
        range: Range<u32>,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
        stride: wgt::BufferSize,
    );

    // render passes

    // Begins a render pass, clears all active bindings.
    unsafe fn begin_render_pass(&mut self, desc: &RenderPassDescriptor<Self::A>);
    unsafe fn end_render_pass(&mut self);

    unsafe fn set_render_pipeline(&mut self, pipeline: &<Self::A as Api>::RenderPipeline);

    unsafe fn set_index_buffer<'a>(
        &mut self,
        binding: BufferBinding<'a, Self::A>,
        format: wgt::IndexFormat,
    );
    unsafe fn set_vertex_buffer<'a>(&mut self, index: u32, binding: BufferBinding<'a, Self::A>);
    unsafe fn set_viewport(&mut self, rect: &Rect<f32>, depth_range: Range<f32>);
    unsafe fn set_scissor_rect(&mut self, rect: &Rect<u32>);
    unsafe fn set_stencil_reference(&mut self, value: u32);
    unsafe fn set_blend_constants(&mut self, color: &[f32; 4]);

    unsafe fn draw(
        &mut self,
        first_vertex: u32,
        vertex_count: u32,
        first_instance: u32,
        instance_count: u32,
    );
    unsafe fn draw_indexed(
        &mut self,
        first_index: u32,
        index_count: u32,
        base_vertex: i32,
        first_instance: u32,
        instance_count: u32,
    );
    unsafe fn draw_indirect(
        &mut self,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
        draw_count: u32,
    );
    unsafe fn draw_indexed_indirect(
        &mut self,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
        draw_count: u32,
    );
    unsafe fn draw_indirect_count(
        &mut self,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
        count_buffer: &<Self::A as Api>::Buffer,
        count_offset: wgt::BufferAddress,
        max_count: u32,
    );
    unsafe fn draw_indexed_indirect_count(
        &mut self,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
        count_buffer: &<Self::A as Api>::Buffer,
        count_offset: wgt::BufferAddress,
        max_count: u32,
    );

    // compute passes

    // Begins a compute pass, clears all active bindings.
    unsafe fn begin_compute_pass(&mut self, desc: &ComputePassDescriptor<Self::A>);
    unsafe fn end_compute_pass(&mut self);

    unsafe fn set_compute_pipeline(&mut self, pipeline: &<Self::A as Api>::ComputePipeline);

    unsafe fn dispatch(&mut self, count: [u32; 3]);
    unsafe fn dispatch_indirect(
        &mut self,
        buffer: &<Self::A as Api>::Buffer,
        offset: wgt::BufferAddress,
    );

    /// To get the required sizes for the buffer allocations use `get_acceleration_structure_build_sizes` per descriptor
    /// All buffers must be synchronized externally
    /// All buffer regions, which are written to may only be passed once per function call,
    /// with the exception of updates in the same descriptor.
    /// Consequences of this limitation:
    /// - scratch buffers need to be unique
    /// - a tlas can't be build in the same call with a blas it contains
    unsafe fn build_acceleration_structures<'a, T>(
        &mut self,
        descriptor_count: u32,
        descriptors: T,
    ) where
        Self::A: 'a,
        T: IntoIterator<Item = BuildAccelerationStructureDescriptor<'a, Self::A>>;

    unsafe fn place_acceleration_structure_barrier(
        &mut self,
        barrier: AccelerationStructureBarrier,
    );
}

bitflags!(
    /// Pipeline layout creation flags.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct PipelineLayoutFlags: u32 {
        /// Include support for `first_vertex` / `first_instance` drawing.
        const FIRST_VERTEX_INSTANCE = 1 << 0;
        /// Include support for num work groups builtin.
        const NUM_WORK_GROUPS = 1 << 1;
    }
);

bitflags!(
    /// Pipeline layout creation flags.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct BindGroupLayoutFlags: u32 {
        /// Allows for bind group binding arrays to be shorter than the array in the BGL.
        const PARTIALLY_BOUND = 1 << 0;
    }
);

bitflags!(
    /// Texture format capability flags.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct TextureFormatCapabilities: u32 {
        /// Format can be sampled.
        const SAMPLED = 1 << 0;
        /// Format can be sampled with a linear sampler.
        const SAMPLED_LINEAR = 1 << 1;
        /// Format can be sampled with a min/max reduction sampler.
        const SAMPLED_MINMAX = 1 << 2;

        /// Format can be used as storage with write-only access.
        const STORAGE = 1 << 3;
        /// Format can be used as storage with read and read/write access.
        const STORAGE_READ_WRITE = 1 << 4;
        /// Format can be used as storage with atomics.
        const STORAGE_ATOMIC = 1 << 5;

        /// Format can be used as color and input attachment.
        const COLOR_ATTACHMENT = 1 << 6;
        /// Format can be used as color (with blending) and input attachment.
        const COLOR_ATTACHMENT_BLEND = 1 << 7;
        /// Format can be used as depth-stencil and input attachment.
        const DEPTH_STENCIL_ATTACHMENT = 1 << 8;

        /// Format can be multisampled by x2.
        const MULTISAMPLE_X2   = 1 << 9;
        /// Format can be multisampled by x4.
        const MULTISAMPLE_X4   = 1 << 10;
        /// Format can be multisampled by x8.
        const MULTISAMPLE_X8   = 1 << 11;
        /// Format can be multisampled by x16.
        const MULTISAMPLE_X16  = 1 << 12;

        /// Format can be used for render pass resolve targets.
        const MULTISAMPLE_RESOLVE = 1 << 13;

        /// Format can be copied from.
        const COPY_SRC = 1 << 14;
        /// Format can be copied to.
        const COPY_DST = 1 << 15;
    }
);

bitflags!(
    /// Texture format capability flags.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct FormatAspects: u8 {
        const COLOR = 1 << 0;
        const DEPTH = 1 << 1;
        const STENCIL = 1 << 2;
        const PLANE_0 = 1 << 3;
        const PLANE_1 = 1 << 4;
        const PLANE_2 = 1 << 5;

        const DEPTH_STENCIL = Self::DEPTH.bits() | Self::STENCIL.bits();
    }
);

impl FormatAspects {
    pub fn new(format: wgt::TextureFormat, aspect: wgt::TextureAspect) -> Self {
        let aspect_mask = match aspect {
            wgt::TextureAspect::All => Self::all(),
            wgt::TextureAspect::DepthOnly => Self::DEPTH,
            wgt::TextureAspect::StencilOnly => Self::STENCIL,
            wgt::TextureAspect::Plane0 => Self::PLANE_0,
            wgt::TextureAspect::Plane1 => Self::PLANE_1,
            wgt::TextureAspect::Plane2 => Self::PLANE_2,
        };
        Self::from(format) & aspect_mask
    }

    /// Returns `true` if only one flag is set
    pub fn is_one(&self) -> bool {
        self.bits().count_ones() == 1
    }

    pub fn map(&self) -> wgt::TextureAspect {
        match *self {
            Self::COLOR => wgt::TextureAspect::All,
            Self::DEPTH => wgt::TextureAspect::DepthOnly,
            Self::STENCIL => wgt::TextureAspect::StencilOnly,
            Self::PLANE_0 => wgt::TextureAspect::Plane0,
            Self::PLANE_1 => wgt::TextureAspect::Plane1,
            Self::PLANE_2 => wgt::TextureAspect::Plane2,
            _ => unreachable!(),
        }
    }
}

impl From<wgt::TextureFormat> for FormatAspects {
    fn from(format: wgt::TextureFormat) -> Self {
        match format {
            wgt::TextureFormat::Stencil8 => Self::STENCIL,
            wgt::TextureFormat::Depth16Unorm
            | wgt::TextureFormat::Depth32Float
            | wgt::TextureFormat::Depth24Plus => Self::DEPTH,
            wgt::TextureFormat::Depth32FloatStencil8 | wgt::TextureFormat::Depth24PlusStencil8 => {
                Self::DEPTH_STENCIL
            }
            wgt::TextureFormat::NV12 => Self::PLANE_0 | Self::PLANE_1,
            _ => Self::COLOR,
        }
    }
}

bitflags!(
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct MemoryFlags: u32 {
        const TRANSIENT = 1 << 0;
        const PREFER_COHERENT = 1 << 1;
    }
);

//TODO: it's not intuitive for the backends to consider `LOAD` being optional.

bitflags!(
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct AttachmentOps: u8 {
        const LOAD = 1 << 0;
        const STORE = 1 << 1;
    }
);

bitflags::bitflags! {
    /// Similar to `wgt::BufferUsages` but for internal use.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct BufferUses: u16 {
        /// The argument to a read-only mapping.
        const MAP_READ = 1 << 0;
        /// The argument to a write-only mapping.
        const MAP_WRITE = 1 << 1;
        /// The source of a hardware copy.
        const COPY_SRC = 1 << 2;
        /// The destination of a hardware copy.
        const COPY_DST = 1 << 3;
        /// The index buffer used for drawing.
        const INDEX = 1 << 4;
        /// A vertex buffer used for drawing.
        const VERTEX = 1 << 5;
        /// A uniform buffer bound in a bind group.
        const UNIFORM = 1 << 6;
        /// A read-only storage buffer used in a bind group.
        const STORAGE_READ = 1 << 7;
        /// A read-write or write-only buffer used in a bind group.
        const STORAGE_READ_WRITE = 1 << 8;
        /// The indirect or count buffer in a indirect draw or dispatch.
        const INDIRECT = 1 << 9;
        /// A buffer used to store query results.
        const QUERY_RESOLVE = 1 << 10;
        const ACCELERATION_STRUCTURE_SCRATCH = 1 << 11;
        const BOTTOM_LEVEL_ACCELERATION_STRUCTURE_INPUT = 1 << 12;
        const TOP_LEVEL_ACCELERATION_STRUCTURE_INPUT = 1 << 13;
        /// The combination of states that a buffer may be in _at the same time_.
        const INCLUSIVE = Self::MAP_READ.bits() | Self::COPY_SRC.bits() |
            Self::INDEX.bits() | Self::VERTEX.bits() | Self::UNIFORM.bits() |
            Self::STORAGE_READ.bits() | Self::INDIRECT.bits() | Self::BOTTOM_LEVEL_ACCELERATION_STRUCTURE_INPUT.bits() | Self::TOP_LEVEL_ACCELERATION_STRUCTURE_INPUT.bits();
        /// The combination of states that a buffer must exclusively be in.
        const EXCLUSIVE = Self::MAP_WRITE.bits() | Self::COPY_DST.bits() | Self::STORAGE_READ_WRITE.bits() | Self::ACCELERATION_STRUCTURE_SCRATCH.bits();
        /// The combination of all usages that the are guaranteed to be be ordered by the hardware.
        /// If a usage is ordered, then if the buffer state doesn't change between draw calls, there
        /// are no barriers needed for synchronization.
        const ORDERED = Self::INCLUSIVE.bits() | Self::MAP_WRITE.bits();
    }
}

bitflags::bitflags! {
    /// Similar to `wgt::TextureUsages` but for internal use.
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub struct TextureUses: u16 {
        /// The texture is in unknown state.
        const UNINITIALIZED = 1 << 0;
        /// Ready to present image to the surface.
        const PRESENT = 1 << 1;
        /// The source of a hardware copy.
        const COPY_SRC = 1 << 2;
        /// The destination of a hardware copy.
        const COPY_DST = 1 << 3;
        /// Read-only sampled or fetched resource.
        const RESOURCE = 1 << 4;
        /// The color target of a renderpass.
        const COLOR_TARGET = 1 << 5;
        /// Read-only depth stencil usage.
        const DEPTH_STENCIL_READ = 1 << 6;
        /// Read-write depth stencil usage
        const DEPTH_STENCIL_WRITE = 1 << 7;
        /// Read-only storage buffer usage. Corresponds to a UAV in d3d, so is exclusive, despite being read only.
        const STORAGE_READ = 1 << 8;
        /// Read-write or write-only storage buffer usage.
        const STORAGE_READ_WRITE = 1 << 9;
        /// The combination of states that a texture may be in _at the same time_.
        const INCLUSIVE = Self::COPY_SRC.bits() | Self::RESOURCE.bits() | Self::DEPTH_STENCIL_READ.bits();
        /// The combination of states that a texture must exclusively be in.
        const EXCLUSIVE = Self::COPY_DST.bits() | Self::COLOR_TARGET.bits() | Self::DEPTH_STENCIL_WRITE.bits() | Self::STORAGE_READ.bits() | Self::STORAGE_READ_WRITE.bits() | Self::PRESENT.bits();
        /// The combination of all usages that the are guaranteed to be be ordered by the hardware.
        /// If a usage is ordered, then if the texture state doesn't change between draw calls, there
        /// are no barriers needed for synchronization.
        const ORDERED = Self::INCLUSIVE.bits() | Self::COLOR_TARGET.bits() | Self::DEPTH_STENCIL_WRITE.bits() | Self::STORAGE_READ.bits();

        /// Flag used by the wgpu-core texture tracker to say a texture is in different states for every sub-resource
        const COMPLEX = 1 << 10;
        /// Flag used by the wgpu-core texture tracker to say that the tracker does not know the state of the sub-resource.
        /// This is different from UNINITIALIZED as that says the tracker does know, but the texture has not been initialized.
        const UNKNOWN = 1 << 11;
    }
}

#[derive(Clone, Debug)]
pub struct InstanceDescriptor<'a> {
    pub name: &'a str,
    pub flags: wgt::InstanceFlags,
    pub dx12_shader_compiler: wgt::Dx12Compiler,
    pub gles_minor_version: wgt::Gles3MinorVersion,
}

#[derive(Clone, Debug)]
pub struct Alignments {
    /// The alignment of the start of the buffer used as a GPU copy source.
    pub buffer_copy_offset: wgt::BufferSize,
    /// The alignment of the row pitch of the texture data stored in a buffer that is
    /// used in a GPU copy operation.
    pub buffer_copy_pitch: wgt::BufferSize,
}

#[derive(Clone, Debug)]
pub struct Capabilities {
    pub limits: wgt::Limits,
    pub alignments: Alignments,
    pub downlevel: wgt::DownlevelCapabilities,
}

#[derive(Debug)]
pub struct ExposedAdapter<A: Api> {
    pub adapter: A::Adapter,
    pub info: wgt::AdapterInfo,
    pub features: wgt::Features,
    pub capabilities: Capabilities,
}

/// Describes information about what a `Surface`'s presentation capabilities are.
/// Fetch this with [Adapter::surface_capabilities].
#[derive(Debug, Clone)]
pub struct SurfaceCapabilities {
    /// List of supported texture formats.
    ///
    /// Must be at least one.
    pub formats: Vec<wgt::TextureFormat>,

    /// Range for the number of queued frames.
    ///
    /// This adjusts either the swapchain frame count to value + 1 - or sets SetMaximumFrameLatency to the value given,
    /// or uses a wait-for-present in the acquire method to limit rendering such that it acts like it's a value + 1 swapchain frame set.
    ///
    /// - `maximum_frame_latency.start` must be at least 1.
    /// - `maximum_frame_latency.end` must be larger or equal to `maximum_frame_latency.start`.
    pub maximum_frame_latency: RangeInclusive<u32>,

    /// Current extent of the surface, if known.
    pub current_extent: Option<wgt::Extent3d>,

    /// Supported texture usage flags.
    ///
    /// Must have at least `TextureUses::COLOR_TARGET`
    pub usage: TextureUses,

    /// List of supported V-sync modes.
    ///
    /// Must be at least one.
    pub present_modes: Vec<wgt::PresentMode>,

    /// List of supported alpha composition modes.
    ///
    /// Must be at least one.
    pub composite_alpha_modes: Vec<wgt::CompositeAlphaMode>,
}

#[derive(Debug)]
pub struct AcquiredSurfaceTexture<A: Api> {
    pub texture: A::SurfaceTexture,
    /// The presentation configuration no longer matches
    /// the surface properties exactly, but can still be used to present
    /// to the surface successfully.
    pub suboptimal: bool,
}

#[derive(Debug)]
pub struct OpenDevice<A: Api> {
    pub device: A::Device,
    pub queue: A::Queue,
}

#[derive(Clone, Debug)]
pub struct BufferMapping {
    pub ptr: NonNull<u8>,
    pub is_coherent: bool,
}

#[derive(Clone, Debug)]
pub struct BufferDescriptor<'a> {
    pub label: Label<'a>,
    pub size: wgt::BufferAddress,
    pub usage: BufferUses,
    pub memory_flags: MemoryFlags,
}

#[derive(Clone, Debug)]
pub struct TextureDescriptor<'a> {
    pub label: Label<'a>,
    pub size: wgt::Extent3d,
    pub mip_level_count: u32,
    pub sample_count: u32,
    pub dimension: wgt::TextureDimension,
    pub format: wgt::TextureFormat,
    pub usage: TextureUses,
    pub memory_flags: MemoryFlags,
    /// Allows views of this texture to have a different format
    /// than the texture does.
    pub view_formats: Vec<wgt::TextureFormat>,
}

impl TextureDescriptor<'_> {
    pub fn copy_extent(&self) -> CopyExtent {
        CopyExtent::map_extent_to_copy_size(&self.size, self.dimension)
    }

    pub fn is_cube_compatible(&self) -> bool {
        self.dimension == wgt::TextureDimension::D2
            && self.size.depth_or_array_layers % 6 == 0
            && self.sample_count == 1
            && self.size.width == self.size.height
    }

    pub fn array_layer_count(&self) -> u32 {
        match self.dimension {
            wgt::TextureDimension::D1 | wgt::TextureDimension::D3 => 1,
            wgt::TextureDimension::D2 => self.size.depth_or_array_layers,
        }
    }
}

/// TextureView descriptor.
///
/// Valid usage:
///. - `format` has to be the same as `TextureDescriptor::format`
///. - `dimension` has to be compatible with `TextureDescriptor::dimension`
///. - `usage` has to be a subset of `TextureDescriptor::usage`
///. - `range` has to be a subset of parent texture
#[derive(Clone, Debug)]
pub struct TextureViewDescriptor<'a> {
    pub label: Label<'a>,
    pub format: wgt::TextureFormat,
    pub dimension: wgt::TextureViewDimension,
    pub usage: TextureUses,
    pub range: wgt::ImageSubresourceRange,
}

#[derive(Clone, Debug)]
pub struct SamplerDescriptor<'a> {
    pub label: Label<'a>,
    pub address_modes: [wgt::AddressMode; 3],
    pub mag_filter: wgt::FilterMode,
    pub min_filter: wgt::FilterMode,
    pub mipmap_filter: wgt::FilterMode,
    pub lod_clamp: Range<f32>,
    pub compare: Option<wgt::CompareFunction>,
    // Must in the range [1, 16].
    //
    // Anisotropic filtering must be supported if this is not 1.
    pub anisotropy_clamp: u16,
    pub border_color: Option<wgt::SamplerBorderColor>,
}

/// BindGroupLayout descriptor.
///
/// Valid usage:
/// - `entries` are sorted by ascending `wgt::BindGroupLayoutEntry::binding`
#[derive(Clone, Debug)]
pub struct BindGroupLayoutDescriptor<'a> {
    pub label: Label<'a>,
    pub flags: BindGroupLayoutFlags,
    pub entries: &'a [wgt::BindGroupLayoutEntry],
}

#[derive(Clone, Debug)]
pub struct PipelineLayoutDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    pub flags: PipelineLayoutFlags,
    pub bind_group_layouts: &'a [&'a A::BindGroupLayout],
    pub push_constant_ranges: &'a [wgt::PushConstantRange],
}

#[derive(Debug)]
pub struct BufferBinding<'a, A: Api> {
    /// The buffer being bound.
    pub buffer: &'a A::Buffer,

    /// The offset at which the bound region starts.
    ///
    /// This must be less than the size of the buffer. Some back ends
    /// cannot tolerate zero-length regions; for example, see
    /// [VUID-VkDescriptorBufferInfo-offset-00340][340] and
    /// [VUID-VkDescriptorBufferInfo-range-00341][341], or the
    /// documentation for GLES's [glBindBufferRange][bbr].
    ///
    /// [340]: https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-VkDescriptorBufferInfo-offset-00340
    /// [341]: https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-VkDescriptorBufferInfo-range-00341
    /// [bbr]: https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glBindBufferRange.xhtml
    pub offset: wgt::BufferAddress,

    /// The size of the region bound, in bytes.
    ///
    /// If `None`, the region extends from `offset` to the end of the
    /// buffer. Given the restrictions on `offset`, this means that
    /// the size is always greater than zero.
    pub size: Option<wgt::BufferSize>,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for BufferBinding<'_, A> {
    fn clone(&self) -> Self {
        Self {
            buffer: self.buffer,
            offset: self.offset,
            size: self.size,
        }
    }
}

#[derive(Debug)]
pub struct TextureBinding<'a, A: Api> {
    pub view: &'a A::TextureView,
    pub usage: TextureUses,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for TextureBinding<'_, A> {
    fn clone(&self) -> Self {
        Self {
            view: self.view,
            usage: self.usage,
        }
    }
}

#[derive(Clone, Debug)]
pub struct BindGroupEntry {
    pub binding: u32,
    pub resource_index: u32,
    pub count: u32,
}

/// BindGroup descriptor.
///
/// Valid usage:
///. - `entries` has to be sorted by ascending `BindGroupEntry::binding`
///. - `entries` has to have the same set of `BindGroupEntry::binding` as `layout`
///. - each entry has to be compatible with the `layout`
///. - each entry's `BindGroupEntry::resource_index` is within range
///    of the corresponding resource array, selected by the relevant
///    `BindGroupLayoutEntry`.
#[derive(Clone, Debug)]
pub struct BindGroupDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    pub layout: &'a A::BindGroupLayout,
    pub buffers: &'a [BufferBinding<'a, A>],
    pub samplers: &'a [&'a A::Sampler],
    pub textures: &'a [TextureBinding<'a, A>],
    pub entries: &'a [BindGroupEntry],
    pub acceleration_structures: &'a [&'a A::AccelerationStructure],
}

#[derive(Clone, Debug)]
pub struct CommandEncoderDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    pub queue: &'a A::Queue,
}

/// Naga shader module.
pub struct NagaShader {
    /// Shader module IR.
    pub module: Cow<'static, naga::Module>,
    /// Analysis information of the module.
    pub info: naga::valid::ModuleInfo,
    /// Source codes for debug
    pub debug_source: Option<DebugSource>,
}

// Custom implementation avoids the need to generate Debug impl code
// for the whole Naga module and info.
impl fmt::Debug for NagaShader {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        write!(formatter, "Naga shader")
    }
}

/// Shader input.
#[allow(clippy::large_enum_variant)]
pub enum ShaderInput<'a> {
    Naga(NagaShader),
    SpirV(&'a [u32]),
}

pub struct ShaderModuleDescriptor<'a> {
    pub label: Label<'a>,
    pub runtime_checks: bool,
}

#[derive(Debug, Clone)]
pub struct DebugSource {
    pub file_name: Cow<'static, str>,
    pub source_code: Cow<'static, str>,
}

/// Describes a programmable pipeline stage.
#[derive(Debug)]
pub struct ProgrammableStage<'a, A: Api> {
    /// The compiled shader module for this stage.
    pub module: &'a A::ShaderModule,
    /// The name of the entry point in the compiled shader. There must be a function with this name
    ///  in the shader.
    pub entry_point: &'a str,
    /// Pipeline constants
    pub constants: &'a naga::back::PipelineConstants,
    /// Whether workgroup scoped memory will be initialized with zero values for this stage.
    ///
    /// This is required by the WebGPU spec, but may have overhead which can be avoided
    /// for cross-platform applications
    pub zero_initialize_workgroup_memory: bool,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for ProgrammableStage<'_, A> {
    fn clone(&self) -> Self {
        Self {
            module: self.module,
            entry_point: self.entry_point,
            constants: self.constants,
            zero_initialize_workgroup_memory: self.zero_initialize_workgroup_memory,
        }
    }
}

/// Describes a compute pipeline.
#[derive(Clone, Debug)]
pub struct ComputePipelineDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    /// The layout of bind groups for this pipeline.
    pub layout: &'a A::PipelineLayout,
    /// The compiled compute stage and its entry point.
    pub stage: ProgrammableStage<'a, A>,
}

/// Describes how the vertex buffer is interpreted.
#[derive(Clone, Debug)]
pub struct VertexBufferLayout<'a> {
    /// The stride, in bytes, between elements of this buffer.
    pub array_stride: wgt::BufferAddress,
    /// How often this vertex buffer is "stepped" forward.
    pub step_mode: wgt::VertexStepMode,
    /// The list of attributes which comprise a single vertex.
    pub attributes: &'a [wgt::VertexAttribute],
}

/// Describes a render (graphics) pipeline.
#[derive(Clone, Debug)]
pub struct RenderPipelineDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    /// The layout of bind groups for this pipeline.
    pub layout: &'a A::PipelineLayout,
    /// The format of any vertex buffers used with this pipeline.
    pub vertex_buffers: &'a [VertexBufferLayout<'a>],
    /// The vertex stage for this pipeline.
    pub vertex_stage: ProgrammableStage<'a, A>,
    /// The properties of the pipeline at the primitive assembly and rasterization level.
    pub primitive: wgt::PrimitiveState,
    /// The effect of draw calls on the depth and stencil aspects of the output target, if any.
    pub depth_stencil: Option<wgt::DepthStencilState>,
    /// The multi-sampling properties of the pipeline.
    pub multisample: wgt::MultisampleState,
    /// The fragment stage for this pipeline.
    pub fragment_stage: Option<ProgrammableStage<'a, A>>,
    /// The effect of draw calls on the color aspect of the output target.
    pub color_targets: &'a [Option<wgt::ColorTargetState>],
    /// If the pipeline will be used with a multiview render pass, this indicates how many array
    /// layers the attachments will have.
    pub multiview: Option<NonZeroU32>,
}

#[derive(Debug, Clone)]
pub struct SurfaceConfiguration {
    /// Maximum number of queued frames. Must be in
    /// `SurfaceCapabilities::maximum_frame_latency` range.
    pub maximum_frame_latency: u32,
    /// Vertical synchronization mode.
    pub present_mode: wgt::PresentMode,
    /// Alpha composition mode.
    pub composite_alpha_mode: wgt::CompositeAlphaMode,
    /// Format of the surface textures.
    pub format: wgt::TextureFormat,
    /// Requested texture extent. Must be in
    /// `SurfaceCapabilities::extents` range.
    pub extent: wgt::Extent3d,
    /// Allowed usage of surface textures,
    pub usage: TextureUses,
    /// Allows views of swapchain texture to have a different format
    /// than the texture does.
    pub view_formats: Vec<wgt::TextureFormat>,
}

#[derive(Debug, Clone)]
pub struct Rect<T> {
    pub x: T,
    pub y: T,
    pub w: T,
    pub h: T,
}

#[derive(Debug, Clone)]
pub struct BufferBarrier<'a, A: Api> {
    pub buffer: &'a A::Buffer,
    pub usage: Range<BufferUses>,
}

#[derive(Debug, Clone)]
pub struct TextureBarrier<'a, A: Api> {
    pub texture: &'a A::Texture,
    pub range: wgt::ImageSubresourceRange,
    pub usage: Range<TextureUses>,
}

#[derive(Clone, Copy, Debug)]
pub struct BufferCopy {
    pub src_offset: wgt::BufferAddress,
    pub dst_offset: wgt::BufferAddress,
    pub size: wgt::BufferSize,
}

#[derive(Clone, Debug)]
pub struct TextureCopyBase {
    pub mip_level: u32,
    pub array_layer: u32,
    /// Origin within a texture.
    /// Note: for 1D and 2D textures, Z must be 0.
    pub origin: wgt::Origin3d,
    pub aspect: FormatAspects,
}

#[derive(Clone, Copy, Debug)]
pub struct CopyExtent {
    pub width: u32,
    pub height: u32,
    pub depth: u32,
}

#[derive(Clone, Debug)]
pub struct TextureCopy {
    pub src_base: TextureCopyBase,
    pub dst_base: TextureCopyBase,
    pub size: CopyExtent,
}

#[derive(Clone, Debug)]
pub struct BufferTextureCopy {
    pub buffer_layout: wgt::ImageDataLayout,
    pub texture_base: TextureCopyBase,
    pub size: CopyExtent,
}

#[derive(Debug)]
pub struct Attachment<'a, A: Api> {
    pub view: &'a A::TextureView,
    /// Contains either a single mutating usage as a target,
    /// or a valid combination of read-only usages.
    pub usage: TextureUses,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for Attachment<'_, A> {
    fn clone(&self) -> Self {
        Self {
            view: self.view,
            usage: self.usage,
        }
    }
}

#[derive(Debug)]
pub struct ColorAttachment<'a, A: Api> {
    pub target: Attachment<'a, A>,
    pub resolve_target: Option<Attachment<'a, A>>,
    pub ops: AttachmentOps,
    pub clear_value: wgt::Color,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for ColorAttachment<'_, A> {
    fn clone(&self) -> Self {
        Self {
            target: self.target.clone(),
            resolve_target: self.resolve_target.clone(),
            ops: self.ops,
            clear_value: self.clear_value,
        }
    }
}

#[derive(Clone, Debug)]
pub struct DepthStencilAttachment<'a, A: Api> {
    pub target: Attachment<'a, A>,
    pub depth_ops: AttachmentOps,
    pub stencil_ops: AttachmentOps,
    pub clear_value: (f32, u32),
}

#[derive(Debug)]
pub struct RenderPassTimestampWrites<'a, A: Api> {
    pub query_set: &'a A::QuerySet,
    pub beginning_of_pass_write_index: Option<u32>,
    pub end_of_pass_write_index: Option<u32>,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for RenderPassTimestampWrites<'_, A> {
    fn clone(&self) -> Self {
        Self {
            query_set: self.query_set,
            beginning_of_pass_write_index: self.beginning_of_pass_write_index,
            end_of_pass_write_index: self.end_of_pass_write_index,
        }
    }
}

#[derive(Clone, Debug)]
pub struct RenderPassDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    pub extent: wgt::Extent3d,
    pub sample_count: u32,
    pub color_attachments: &'a [Option<ColorAttachment<'a, A>>],
    pub depth_stencil_attachment: Option<DepthStencilAttachment<'a, A>>,
    pub multiview: Option<NonZeroU32>,
    pub timestamp_writes: Option<RenderPassTimestampWrites<'a, A>>,
    pub occlusion_query_set: Option<&'a A::QuerySet>,
}

#[derive(Debug)]
pub struct ComputePassTimestampWrites<'a, A: Api> {
    pub query_set: &'a A::QuerySet,
    pub beginning_of_pass_write_index: Option<u32>,
    pub end_of_pass_write_index: Option<u32>,
}

// Rust gets confused about the impl requirements for `A`
impl<A: Api> Clone for ComputePassTimestampWrites<'_, A> {
    fn clone(&self) -> Self {
        Self {
            query_set: self.query_set,
            beginning_of_pass_write_index: self.beginning_of_pass_write_index,
            end_of_pass_write_index: self.end_of_pass_write_index,
        }
    }
}

#[derive(Clone, Debug)]
pub struct ComputePassDescriptor<'a, A: Api> {
    pub label: Label<'a>,
    pub timestamp_writes: Option<ComputePassTimestampWrites<'a, A>>,
}

/// Stores the text of any validation errors that have occurred since
/// the last call to `get_and_reset`.
///
/// Each value is a validation error and a message associated with it,
/// or `None` if the error has no message from the api.
///
/// This is used for internal wgpu testing only and _must not_ be used
/// as a way to check for errors.
///
/// This works as a static because `cargo nextest` runs all of our
/// tests in separate processes, so each test gets its own canary.
///
/// This prevents the issue of one validation error terminating the
/// entire process.
pub static VALIDATION_CANARY: ValidationCanary = ValidationCanary {
    inner: Mutex::new(Vec::new()),
};

/// Flag for internal testing.
pub struct ValidationCanary {
    inner: Mutex<Vec<String>>,
}

impl ValidationCanary {
    #[allow(dead_code)] // in some configurations this function is dead
    fn add(&self, msg: String) {
        self.inner.lock().push(msg);
    }

    /// Returns any API validation errors that have occurred in this process
    /// since the last call to this function.
    pub fn get_and_reset(&self) -> Vec<String> {
        self.inner.lock().drain(..).collect()
    }
}

#[test]
fn test_default_limits() {
    let limits = wgt::Limits::default();
    assert!(limits.max_bind_groups <= MAX_BIND_GROUPS as u32);
}

#[derive(Clone, Debug)]
pub struct AccelerationStructureDescriptor<'a> {
    pub label: Label<'a>,
    pub size: wgt::BufferAddress,
    pub format: AccelerationStructureFormat,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum AccelerationStructureFormat {
    TopLevel,
    BottomLevel,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum AccelerationStructureBuildMode {
    Build,
    Update,
}

/// Information of the required size for a corresponding entries struct (+ flags)
#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub struct AccelerationStructureBuildSizes {
    pub acceleration_structure_size: wgt::BufferAddress,
    pub update_scratch_size: wgt::BufferAddress,
    pub build_scratch_size: wgt::BufferAddress,
}

/// Updates use source_acceleration_structure if present, else the update will be performed in place.
/// For updates, only the data is allowed to change (not the meta data or sizes).
#[derive(Clone, Debug)]
pub struct BuildAccelerationStructureDescriptor<'a, A: Api> {
    pub entries: &'a AccelerationStructureEntries<'a, A>,
    pub mode: AccelerationStructureBuildMode,
    pub flags: AccelerationStructureBuildFlags,
    pub source_acceleration_structure: Option<&'a A::AccelerationStructure>,
    pub destination_acceleration_structure: &'a A::AccelerationStructure,
    pub scratch_buffer: &'a A::Buffer,
    pub scratch_buffer_offset: wgt::BufferAddress,
}

/// - All buffers, buffer addresses and offsets will be ignored.
/// - The build mode will be ignored.
/// - Reducing the amount of Instances, Triangle groups or AABB groups (or the number of Triangles/AABBs in corresponding groups),
/// may result in reduced size requirements.
/// - Any other change may result in a bigger or smaller size requirement.
#[derive(Clone, Debug)]
pub struct GetAccelerationStructureBuildSizesDescriptor<'a, A: Api> {
    pub entries: &'a AccelerationStructureEntries<'a, A>,
    pub flags: AccelerationStructureBuildFlags,
}

/// Entries for a single descriptor
/// * `Instances` - Multiple instances for a top level acceleration structure
/// * `Triangles` - Multiple triangle meshes for a bottom level acceleration structure
/// * `AABBs` - List of list of axis aligned bounding boxes for a bottom level acceleration structure
#[derive(Debug)]
pub enum AccelerationStructureEntries<'a, A: Api> {
    Instances(AccelerationStructureInstances<'a, A>),
    Triangles(Vec<AccelerationStructureTriangles<'a, A>>),
    AABBs(Vec<AccelerationStructureAABBs<'a, A>>),
}

/// * `first_vertex` - offset in the vertex buffer (as number of vertices)
/// * `indices` - optional index buffer with attributes
/// * `transform` - optional transform
#[derive(Clone, Debug)]
pub struct AccelerationStructureTriangles<'a, A: Api> {
    pub vertex_buffer: Option<&'a A::Buffer>,
    pub vertex_format: wgt::VertexFormat,
    pub first_vertex: u32,
    pub vertex_count: u32,
    pub vertex_stride: wgt::BufferAddress,
    pub indices: Option<AccelerationStructureTriangleIndices<'a, A>>,
    pub transform: Option<AccelerationStructureTriangleTransform<'a, A>>,
    pub flags: AccelerationStructureGeometryFlags,
}

/// * `offset` - offset in bytes
#[derive(Clone, Debug)]
pub struct AccelerationStructureAABBs<'a, A: Api> {
    pub buffer: Option<&'a A::Buffer>,
    pub offset: u32,
    pub count: u32,
    pub stride: wgt::BufferAddress,
    pub flags: AccelerationStructureGeometryFlags,
}

/// * `offset` - offset in bytes
#[derive(Clone, Debug)]
pub struct AccelerationStructureInstances<'a, A: Api> {
    pub buffer: Option<&'a A::Buffer>,
    pub offset: u32,
    pub count: u32,
}

/// * `offset` - offset in bytes
#[derive(Clone, Debug)]
pub struct AccelerationStructureTriangleIndices<'a, A: Api> {
    pub format: wgt::IndexFormat,
    pub buffer: Option<&'a A::Buffer>,
    pub offset: u32,
    pub count: u32,
}

/// * `offset` - offset in bytes
#[derive(Clone, Debug)]
pub struct AccelerationStructureTriangleTransform<'a, A: Api> {
    pub buffer: &'a A::Buffer,
    pub offset: u32,
}

pub use wgt::AccelerationStructureFlags as AccelerationStructureBuildFlags;
pub use wgt::AccelerationStructureGeometryFlags;

bitflags::bitflags! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct AccelerationStructureUses: u8 {
        // For blas used as input for tlas
        const BUILD_INPUT = 1 << 0;
        // Target for acceleration structure build
        const BUILD_OUTPUT = 1 << 1;
        // Tlas used in a shader
        const SHADER_INPUT = 1 << 2;
    }
}

#[derive(Debug, Clone)]
pub struct AccelerationStructureBarrier {
    pub usage: Range<AccelerationStructureUses>,
}
