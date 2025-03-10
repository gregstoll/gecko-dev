/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


use api::{units::*, PremultipliedColorF, ClipMode};
use api::{ColorF, ImageFormat, LineOrientation, BorderStyle};
use crate::batch::{AlphaBatchBuilder, AlphaBatchContainer, BatchTextures};
use crate::batch::{ClipBatcher, BatchBuilder, INVALID_SEGMENT_INDEX, ClipMaskInstanceList};
use crate::command_buffer::{CommandBufferList, QuadFlags};
use crate::pattern::{PatternKind, PatternShaderInput};
use crate::segment::EdgeAaSegmentMask;
use crate::spatial_tree::SpatialTree;
use crate::clip::{ClipStore, ClipItemKind};
use crate::frame_builder::{FrameGlobalResources};
use crate::gpu_cache::{GpuCache, GpuCacheAddress};
use crate::gpu_types::{BorderInstance, SvgFilterInstance, BlurDirection, BlurInstance, PrimitiveHeaders, ScalingInstance};
use crate::gpu_types::{TransformPalette, ZBufferIdGenerator, MaskInstance, ClipSpace};
use crate::gpu_types::{ZBufferId, QuadSegment, PrimitiveInstanceData, TransformPaletteId};
use crate::internal_types::{FastHashMap, TextureSource, CacheTextureId};
use crate::picture::{SliceId, SurfaceInfo, ResolvedSurfaceTexture, TileCacheInstance};
use crate::quad;
use crate::prim_store::{PrimitiveInstance, PrimitiveStore, PrimitiveScratchBuffer};
use crate::prim_store::gradient::{
    FastLinearGradientInstance, LinearGradientInstance, RadialGradientInstance,
    ConicGradientInstance,
};
use crate::renderer::{GpuBufferAddress, GpuBufferBuilder};
use crate::render_backend::DataStores;
use crate::render_task::{RenderTaskKind, RenderTaskAddress, SubPass};
use crate::render_task::{RenderTask, ScalingTask, SvgFilterInfo, MaskSubPass};
use crate::render_task_graph::{RenderTaskGraph, RenderTaskId};
use crate::resource_cache::ResourceCache;
use crate::spatial_tree::{SpatialNodeIndex};


const STYLE_SOLID: i32 = ((BorderStyle::Solid as i32) << 8) | ((BorderStyle::Solid as i32) << 16);
const STYLE_MASK: i32 = 0x00FF_FF00;

/// A tag used to identify the output format of a `RenderTarget`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum RenderTargetKind {
    Color, // RGBA8
    Alpha, // R8
}

/// Identifies a given `RenderTarget` in a `RenderTargetList`.
#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTargetIndex(pub usize);

pub struct RenderTargetContext<'a, 'rc> {
    pub global_device_pixel_scale: DevicePixelScale,
    pub prim_store: &'a PrimitiveStore,
    pub clip_store: &'a ClipStore,
    pub resource_cache: &'rc mut ResourceCache,
    pub use_dual_source_blending: bool,
    pub use_advanced_blending: bool,
    pub break_advanced_blend_batches: bool,
    pub batch_lookback_count: usize,
    pub spatial_tree: &'a SpatialTree,
    pub data_stores: &'a DataStores,
    pub surfaces: &'a [SurfaceInfo],
    pub scratch: &'a PrimitiveScratchBuffer,
    pub screen_world_rect: WorldRect,
    pub globals: &'a FrameGlobalResources,
    pub tile_caches: &'a FastHashMap<SliceId, Box<TileCacheInstance>>,
    pub root_spatial_node_index: SpatialNodeIndex,
}

/// Represents a number of rendering operations on a surface.
///
/// In graphics parlance, a "render target" usually means "a surface (texture or
/// framebuffer) bound to the output of a shader". This trait has a slightly
/// different meaning, in that it represents the operations on that surface
/// _before_ it's actually bound and rendered. So a `RenderTarget` is built by
/// the `RenderBackend` by inserting tasks, and then shipped over to the
/// `Renderer` where a device surface is resolved and the tasks are transformed
/// into draw commands on that surface.
///
/// We express this as a trait to generalize over color and alpha surfaces.
/// a given `RenderTask` will draw to one or the other, depending on its type
/// and sometimes on its parameters. See `RenderTask::target_kind`.
pub trait RenderTarget {
    /// Creates a new RenderTarget of the given type.
    fn new(
        texture_id: CacheTextureId,
        screen_size: DeviceIntSize,
        gpu_supports_fast_clears: bool,
        used_rect: DeviceIntRect,
    ) -> Self;

    /// Optional hook to provide additional processing for the target at the
    /// end of the build phase.
    fn build(
        &mut self,
        _ctx: &mut RenderTargetContext,
        _gpu_cache: &mut GpuCache,
        _render_tasks: &RenderTaskGraph,
        _prim_headers: &mut PrimitiveHeaders,
        _transforms: &mut TransformPalette,
        _z_generator: &mut ZBufferIdGenerator,
        _prim_instances: &[PrimitiveInstance],
        _cmd_buffers: &CommandBufferList,
        _gpu_buffer_builder: &mut GpuBufferBuilder,
    ) {
    }

    /// Associates a `RenderTask` with this target. That task must be assigned
    /// to a region returned by invoking `allocate()` on this target.
    ///
    /// TODO(gw): It's a bit odd that we need the deferred resolves and mutable
    /// GPU cache here. They are typically used by the build step above. They
    /// are used for the blit jobs to allow resolve_image to be called. It's a
    /// bit of extra overhead to store the image key here and the resolve them
    /// in the build step separately.  BUT: if/when we add more texture cache
    /// target jobs, we might want to tidy this up.
    fn add_task(
        &mut self,
        task_id: RenderTaskId,
        ctx: &RenderTargetContext,
        gpu_cache: &mut GpuCache,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        render_tasks: &RenderTaskGraph,
        clip_store: &ClipStore,
        transforms: &mut TransformPalette,
    );

    fn needs_depth(&self) -> bool;
    fn texture_id(&self) -> CacheTextureId;
}

/// A series of `RenderTarget` instances, serving as the high-level container
/// into which `RenderTasks` are assigned.
///
/// During the build phase, we iterate over the tasks in each `RenderPass`. For
/// each task, we invoke `allocate()` on the `RenderTargetList`, which in turn
/// attempts to allocate an output region in the last `RenderTarget` in the
/// list. If allocation fails (or if the list is empty), a new `RenderTarget` is
/// created and appended to the list. The build phase then assign the task into
/// the target associated with the final allocation.
///
/// The result is that each `RenderPass` is associated with one or two
/// `RenderTargetLists`, depending on whether we have all our tasks have the
/// same `RenderTargetKind`. The lists are then shipped to the `Renderer`, which
/// allocates a device texture array, with one slice per render target in the
/// list.
///
/// The upshot of this scheme is that it maximizes batching. In a given pass,
/// we need to do a separate batch for each individual render target. But with
/// the texture array, we can expose the entirety of the previous pass to each
/// task in the current pass in a single batch, which generally allows each
/// task to be drawn in a single batch regardless of how many results from the
/// previous pass it depends on.
///
/// Note that in some cases (like drop-shadows), we can depend on the output of
/// a pass earlier than the immediately-preceding pass.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTargetList<T> {
    pub format: ImageFormat,
    pub targets: Vec<T>,
}

impl<T: RenderTarget> RenderTargetList<T> {
    pub fn new(
        format: ImageFormat,
    ) -> Self {
        RenderTargetList {
            format,
            targets: Vec::new(),
        }
    }

    pub fn build(
        &mut self,
        ctx: &mut RenderTargetContext,
        gpu_cache: &mut GpuCache,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        cmd_buffers: &CommandBufferList,
        gpu_buffer_builder: &mut GpuBufferBuilder,
    ) {
        if self.targets.is_empty() {
            return;
        }

        for target in &mut self.targets {
            target.build(
                ctx,
                gpu_cache,
                render_tasks,
                prim_headers,
                transforms,
                z_generator,
                prim_instances,
                cmd_buffers,
                gpu_buffer_builder,
            );
        }
    }

    pub fn needs_depth(&self) -> bool {
        self.targets.iter().any(|target| target.needs_depth())
    }
}

const NUM_PATTERNS: usize = crate::pattern::NUM_PATTERNS as usize;

/// Contains the work (in the form of instance arrays) needed to fill a color
/// color output surface (RGBA8).
///
/// See `RenderTarget`.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ColorRenderTarget {
    pub alpha_batch_containers: Vec<AlphaBatchContainer>,
    // List of blur operations to apply for this render target.
    pub vertical_blurs: FastHashMap<TextureSource, Vec<BlurInstance>>,
    pub horizontal_blurs: FastHashMap<TextureSource, Vec<BlurInstance>>,
    pub scalings: FastHashMap<TextureSource, Vec<ScalingInstance>>,
    pub svg_filters: Vec<(BatchTextures, Vec<SvgFilterInstance>)>,
    pub blits: Vec<BlitJob>,
    alpha_tasks: Vec<RenderTaskId>,
    screen_size: DeviceIntSize,
    pub texture_id: CacheTextureId,
    // Track the used rect of the render target, so that
    // we can set a scissor rect and only clear to the
    // used portion of the target as an optimization.
    pub used_rect: DeviceIntRect,
    pub resolve_ops: Vec<ResolveOp>,
    pub clear_color: Option<ColorF>,

    pub prim_instances: [Vec<PrimitiveInstanceData>; NUM_PATTERNS],
    pub prim_instances_with_scissor: FastHashMap<(DeviceIntRect, PatternKind), Vec<PrimitiveInstanceData>>,
    
    pub clip_masks: ClipMaskInstanceList,
}

impl RenderTarget for ColorRenderTarget {
    fn new(
        texture_id: CacheTextureId,
        screen_size: DeviceIntSize,
        _: bool,
        used_rect: DeviceIntRect,
    ) -> Self {
        ColorRenderTarget {
            alpha_batch_containers: Vec::new(),
            vertical_blurs: FastHashMap::default(),
            horizontal_blurs: FastHashMap::default(),
            scalings: FastHashMap::default(),
            svg_filters: Vec::new(),
            blits: Vec::new(),
            alpha_tasks: Vec::new(),
            screen_size,
            texture_id,
            used_rect,
            resolve_ops: Vec::new(),
            clear_color: Some(ColorF::TRANSPARENT),
            prim_instances: [Vec::new(), Vec::new(), Vec::new(), Vec::new()],
            prim_instances_with_scissor: FastHashMap::default(),
            clip_masks: ClipMaskInstanceList::new(),
        }
    }

    fn build(
        &mut self,
        ctx: &mut RenderTargetContext,
        gpu_cache: &mut GpuCache,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        cmd_buffers: &CommandBufferList,
        gpu_buffer_builder: &mut GpuBufferBuilder,
    ) {
        profile_scope!("build");
        let mut merged_batches = AlphaBatchContainer::new(None);

        for task_id in &self.alpha_tasks {
            profile_scope!("alpha_task");
            let task = &render_tasks[*task_id];

            match task.kind {
                RenderTaskKind::Picture(ref pic_task) => {
                    let target_rect = task.get_target_rect();

                    let scissor_rect = if pic_task.can_merge {
                        None
                    } else {
                        Some(target_rect)
                    };

                    if !pic_task.can_use_shared_surface {
                        self.clear_color = pic_task.clear_color;
                    }

                    // TODO(gw): The type names of AlphaBatchBuilder and BatchBuilder
                    //           are still confusing. Once more of the picture caching
                    //           improvement code lands, the AlphaBatchBuilder and
                    //           AlphaBatchList types will be collapsed into one, which
                    //           should simplify coming up with better type names.
                    let alpha_batch_builder = AlphaBatchBuilder::new(
                        self.screen_size,
                        ctx.break_advanced_blend_batches,
                        ctx.batch_lookback_count,
                        *task_id,
                        (*task_id).into(),
                    );

                    let mut batch_builder = BatchBuilder::new(alpha_batch_builder);
                    let cmd_buffer = cmd_buffers.get(pic_task.cmd_buffer_index);

                    cmd_buffer.iter_prims(&mut |cmd, spatial_node_index, segments| {
                        batch_builder.add_prim_to_batch(
                            cmd,
                            spatial_node_index,
                            ctx,
                            gpu_cache,
                            render_tasks,
                            prim_headers,
                            transforms,
                            pic_task.raster_spatial_node_index,
                            pic_task.surface_spatial_node_index,
                            z_generator,
                            prim_instances,
                            gpu_buffer_builder,
                            segments,
                        );
                    });

                    let alpha_batch_builder = batch_builder.finalize();

                    alpha_batch_builder.build(
                        &mut self.alpha_batch_containers,
                        &mut merged_batches,
                        target_rect,
                        scissor_rect,
                    );
                }
                _ => {
                    unreachable!();
                }
            }
        }

        if !merged_batches.is_empty() {
            self.alpha_batch_containers.push(merged_batches);
        }
    }

    fn texture_id(&self) -> CacheTextureId {
        self.texture_id
    }

    fn add_task(
        &mut self,
        task_id: RenderTaskId,
        ctx: &RenderTargetContext,
        gpu_cache: &mut GpuCache,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        render_tasks: &RenderTaskGraph,
        _: &ClipStore,
        transforms: &mut TransformPalette,
    ) {
        profile_scope!("add_task");
        let task = &render_tasks[task_id];

        match task.kind {
            RenderTaskKind::Prim(ref info) => {
                let render_task_address = task_id.into();
                let target_rect = task.get_target_rect();

                quad::add_to_batch(
                    info.pattern,
                    info.pattern_input,
                    render_task_address,
                    info.transform_id,
                    info.prim_address_f,
                    info.quad_flags,
                    info.edge_flags,
                    INVALID_SEGMENT_INDEX as u8,
                    RenderTaskId::INVALID,
                    ZBufferId(0),
                    render_tasks,
                    gpu_buffer_builder,
                    |_, instance| {
                        if info.prim_needs_scissor_rect {
                            self.prim_instances_with_scissor
                                .entry((target_rect, info.pattern))
                                .or_insert(Vec::new())
                                .push(instance);
                        } else {
                            self.prim_instances[info.pattern as usize].push(instance);
                        }
                    }
                );
            }
            RenderTaskKind::VerticalBlur(..) => {
                add_blur_instances(
                    &mut self.vertical_blurs,
                    BlurDirection::Vertical,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                );
            }
            RenderTaskKind::HorizontalBlur(..) => {
                add_blur_instances(
                    &mut self.horizontal_blurs,
                    BlurDirection::Horizontal,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                );
            }
            RenderTaskKind::Picture(ref pic_task) => {
                if let Some(ref resolve_op) = pic_task.resolve_op {
                    self.resolve_ops.push(resolve_op.clone());
                }
                self.alpha_tasks.push(task_id);
            }
            RenderTaskKind::SvgFilter(ref task_info) => {
                add_svg_filter_instances(
                    &mut self.svg_filters,
                    render_tasks,
                    &task_info.info,
                    task_id,
                    task.children.get(0).cloned(),
                    task.children.get(1).cloned(),
                    task_info.extra_gpu_cache_handle.map(|handle| gpu_cache.get_address(&handle)),
                )
            }
            RenderTaskKind::Image(..) |
            RenderTaskKind::Cached(..) |
            RenderTaskKind::ClipRegion(..) |
            RenderTaskKind::Border(..) |
            RenderTaskKind::CacheMask(..) |
            RenderTaskKind::FastLinearGradient(..) |
            RenderTaskKind::LinearGradient(..) |
            RenderTaskKind::RadialGradient(..) |
            RenderTaskKind::ConicGradient(..) |
            RenderTaskKind::TileComposite(..) |
            RenderTaskKind::Empty(..) |
            RenderTaskKind::LineDecoration(..) => {
                panic!("Should not be added to color target!");
            }
            RenderTaskKind::Readback(..) => {}
            RenderTaskKind::Scaling(ref info) => {
                add_scaling_instances(
                    info,
                    &mut self.scalings,
                    task,
                    task.children.first().map(|&child| &render_tasks[child]),
                );
            }
            RenderTaskKind::Blit(ref task_info) => {
                let target_rect = task.get_target_rect();
                self.blits.push(BlitJob {
                    source: task_info.source,
                    target_rect,
                });
            }
            #[cfg(test)]
            RenderTaskKind::Test(..) => {}
        }

        build_sub_pass(
            task_id,
            task,
            gpu_buffer_builder,
            render_tasks,
            transforms,
            ctx,
            &mut self.clip_masks,
        );
    }

    fn needs_depth(&self) -> bool {
        self.alpha_batch_containers.iter().any(|ab| {
            !ab.opaque_batches.is_empty()
        })
    }
}

/// Contains the work (in the form of instance arrays) needed to fill an alpha
/// output surface (R8).
///
/// See `RenderTarget`.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct AlphaRenderTarget {
    pub clip_batcher: ClipBatcher,
    // List of blur operations to apply for this render target.
    pub vertical_blurs: FastHashMap<TextureSource, Vec<BlurInstance>>,
    pub horizontal_blurs: FastHashMap<TextureSource, Vec<BlurInstance>>,
    pub scalings: FastHashMap<TextureSource, Vec<ScalingInstance>>,
    pub zero_clears: Vec<RenderTaskId>,
    pub one_clears: Vec<RenderTaskId>,
    pub texture_id: CacheTextureId,
    pub clip_masks: ClipMaskInstanceList,
}

impl RenderTarget for AlphaRenderTarget {
    fn new(
        texture_id: CacheTextureId,
        _: DeviceIntSize,
        gpu_supports_fast_clears: bool,
        _: DeviceIntRect,
    ) -> Self {
        AlphaRenderTarget {
            clip_batcher: ClipBatcher::new(gpu_supports_fast_clears),
            vertical_blurs: FastHashMap::default(),
            horizontal_blurs: FastHashMap::default(),
            scalings: FastHashMap::default(),
            zero_clears: Vec::new(),
            one_clears: Vec::new(),
            texture_id,
            clip_masks: ClipMaskInstanceList::new(),
        }
    }

    fn texture_id(&self) -> CacheTextureId {
        self.texture_id
    }

    fn add_task(
        &mut self,
        task_id: RenderTaskId,
        ctx: &RenderTargetContext,
        gpu_cache: &mut GpuCache,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        render_tasks: &RenderTaskGraph,
        clip_store: &ClipStore,
        transforms: &mut TransformPalette,
    ) {
        profile_scope!("add_task");
        let task = &render_tasks[task_id];
        let target_rect = task.get_target_rect();

        match task.kind {
            RenderTaskKind::Image(..) |
            RenderTaskKind::Cached(..) |
            RenderTaskKind::Readback(..) |
            RenderTaskKind::Picture(..) |
            RenderTaskKind::Blit(..) |
            RenderTaskKind::Border(..) |
            RenderTaskKind::LineDecoration(..) |
            RenderTaskKind::FastLinearGradient(..) |
            RenderTaskKind::LinearGradient(..) |
            RenderTaskKind::RadialGradient(..) |
            RenderTaskKind::ConicGradient(..) |
            RenderTaskKind::TileComposite(..) |
            RenderTaskKind::Prim(..) |
            RenderTaskKind::SvgFilter(..) => {
                panic!("BUG: should not be added to alpha target!");
            }
            RenderTaskKind::Empty(..) => {
                // TODO(gw): Could likely be more efficient by choosing to clear to 0 or 1
                //           based on the clip chain, or even skipping clear and masking the
                //           prim region with blend disabled.
                self.one_clears.push(task_id);
            }
            RenderTaskKind::VerticalBlur(..) => {
                self.zero_clears.push(task_id);
                add_blur_instances(
                    &mut self.vertical_blurs,
                    BlurDirection::Vertical,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                );
            }
            RenderTaskKind::HorizontalBlur(..) => {
                self.zero_clears.push(task_id);
                add_blur_instances(
                    &mut self.horizontal_blurs,
                    BlurDirection::Horizontal,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                );
            }
            RenderTaskKind::CacheMask(ref task_info) => {
                let clear_to_one = self.clip_batcher.add(
                    task_info.clip_node_range,
                    task_info.root_spatial_node_index,
                    render_tasks,
                    gpu_cache,
                    clip_store,
                    transforms,
                    task_info.actual_rect,
                    task_info.device_pixel_scale,
                    target_rect.min.to_f32(),
                    task_info.actual_rect.min,
                    ctx,
                );
                if task_info.clear_to_one || clear_to_one {
                    self.one_clears.push(task_id);
                }
            }
            RenderTaskKind::ClipRegion(ref region_task) => {
                if region_task.clear_to_one {
                    self.one_clears.push(task_id);
                }
                let device_rect = DeviceRect::from_size(
                    target_rect.size().to_f32(),
                );
                self.clip_batcher.add_clip_region(
                    region_task.local_pos,
                    device_rect,
                    region_task.clip_data.clone(),
                    target_rect.min.to_f32(),
                    DevicePoint::zero(),
                    region_task.device_pixel_scale.0,
                );
            }
            RenderTaskKind::Scaling(ref info) => {
                add_scaling_instances(
                    info,
                    &mut self.scalings,
                    task,
                    task.children.first().map(|&child| &render_tasks[child]),
                );
            }
            #[cfg(test)]
            RenderTaskKind::Test(..) => {}
        }

        build_sub_pass(
            task_id,
            task,
            gpu_buffer_builder,
            render_tasks,
            transforms,
            ctx,
            &mut self.clip_masks,
        );
    }

    fn needs_depth(&self) -> bool {
        false
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, PartialEq, Clone)]
pub struct ResolveOp {
    pub src_task_ids: Vec<RenderTaskId>,
    pub dest_task_id: RenderTaskId,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum PictureCacheTargetKind {
    Draw {
        alpha_batch_container: AlphaBatchContainer,
    },
    Blit {
        task_id: RenderTaskId,
        sub_rect_offset: DeviceIntVector2D,
    },
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PictureCacheTarget {
    pub surface: ResolvedSurfaceTexture,
    pub kind: PictureCacheTargetKind,
    pub clear_color: Option<ColorF>,
    pub dirty_rect: DeviceIntRect,
    pub valid_rect: DeviceIntRect,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TextureCacheRenderTarget {
    pub target_kind: RenderTargetKind,
    pub horizontal_blurs: FastHashMap<TextureSource, Vec<BlurInstance>>,
    pub blits: Vec<BlitJob>,
    pub border_segments_complex: Vec<BorderInstance>,
    pub border_segments_solid: Vec<BorderInstance>,
    pub clears: Vec<DeviceIntRect>,
    pub line_decorations: Vec<LineDecorationJob>,
    pub fast_linear_gradients: Vec<FastLinearGradientInstance>,
    pub linear_gradients: Vec<LinearGradientInstance>,
    pub radial_gradients: Vec<RadialGradientInstance>,
    pub conic_gradients: Vec<ConicGradientInstance>,
}

impl TextureCacheRenderTarget {
    pub fn new(target_kind: RenderTargetKind) -> Self {
        TextureCacheRenderTarget {
            target_kind,
            horizontal_blurs: FastHashMap::default(),
            blits: vec![],
            border_segments_complex: vec![],
            border_segments_solid: vec![],
            clears: vec![],
            line_decorations: vec![],
            fast_linear_gradients: vec![],
            linear_gradients: vec![],
            radial_gradients: vec![],
            conic_gradients: vec![],
        }
    }

    pub fn add_task(
        &mut self,
        task_id: RenderTaskId,
        render_tasks: &RenderTaskGraph,
    ) {
        profile_scope!("add_task");
        let task_address = task_id.into();

        let task = &render_tasks[task_id];
        let target_rect = task.get_target_rect();

        match task.kind {
            RenderTaskKind::LineDecoration(ref info) => {
                self.clears.push(target_rect);

                self.line_decorations.push(LineDecorationJob {
                    task_rect: target_rect.to_f32(),
                    local_size: info.local_size,
                    style: info.style as i32,
                    axis_select: match info.orientation {
                        LineOrientation::Horizontal => 0.0,
                        LineOrientation::Vertical => 1.0,
                    },
                    wavy_line_thickness: info.wavy_line_thickness,
                });
            }
            RenderTaskKind::HorizontalBlur(..) => {
                add_blur_instances(
                    &mut self.horizontal_blurs,
                    BlurDirection::Horizontal,
                    task_address,
                    task.children[0],
                    render_tasks,
                );
            }
            RenderTaskKind::Blit(ref task_info) => {
                // Add a blit job to copy from an existing render
                // task to this target.
                self.blits.push(BlitJob {
                    source: task_info.source,
                    target_rect,
                });
            }
            RenderTaskKind::Border(ref task_info) => {
                self.clears.push(target_rect);

                let task_origin = target_rect.min.to_f32();
                // TODO(gw): Clone here instead of a move of this vec, since the frame
                //           graph is immutable by this point. It's rare that borders
                //           are drawn since they are persisted in the texture cache,
                //           but perhaps this could be improved in future.
                let instances = task_info.instances.clone();
                for mut instance in instances {
                    // TODO(gw): It may be better to store the task origin in
                    //           the render task data instead of per instance.
                    instance.task_origin = task_origin;
                    if instance.flags & STYLE_MASK == STYLE_SOLID {
                        self.border_segments_solid.push(instance);
                    } else {
                        self.border_segments_complex.push(instance);
                    }
                }
            }
            RenderTaskKind::FastLinearGradient(ref task_info) => {
                self.fast_linear_gradients.push(task_info.to_instance(&target_rect));
            }
            RenderTaskKind::LinearGradient(ref task_info) => {
                self.linear_gradients.push(task_info.to_instance(&target_rect));
            }
            RenderTaskKind::RadialGradient(ref task_info) => {
                self.radial_gradients.push(task_info.to_instance(&target_rect));
            }
            RenderTaskKind::ConicGradient(ref task_info) => {
                self.conic_gradients.push(task_info.to_instance(&target_rect));
            }
            RenderTaskKind::Prim(..) |
            RenderTaskKind::Image(..) |
            RenderTaskKind::Cached(..) |
            RenderTaskKind::VerticalBlur(..) |
            RenderTaskKind::Picture(..) |
            RenderTaskKind::ClipRegion(..) |
            RenderTaskKind::CacheMask(..) |
            RenderTaskKind::Readback(..) |
            RenderTaskKind::Scaling(..) |
            RenderTaskKind::TileComposite(..) |
            RenderTaskKind::Empty(..) |
            RenderTaskKind::SvgFilter(..) => {
                panic!("BUG: unexpected task kind for texture cache target");
            }
            #[cfg(test)]
            RenderTaskKind::Test(..) => {}
        }
    }
}

fn add_blur_instances(
    instances: &mut FastHashMap<TextureSource, Vec<BlurInstance>>,
    blur_direction: BlurDirection,
    task_address: RenderTaskAddress,
    src_task_id: RenderTaskId,
    render_tasks: &RenderTaskGraph,
) {
    let source = render_tasks[src_task_id].get_texture_source();

    let instance = BlurInstance {
        task_address,
        src_task_address: src_task_id.into(),
        blur_direction,
    };

    instances
        .entry(source)
        .or_insert(Vec::new())
        .push(instance);
}

fn add_scaling_instances(
    task: &ScalingTask,
    instances: &mut FastHashMap<TextureSource, Vec<ScalingInstance>>,
    target_task: &RenderTask,
    source_task: Option<&RenderTask>,
) {
    let target_rect = target_task
        .get_target_rect()
        .inner_box(task.padding)
        .to_f32();

    let source = source_task.unwrap().get_texture_source();

    let source_rect = source_task.unwrap().get_target_rect().to_f32();

    instances
        .entry(source)
        .or_insert(Vec::new())
        .push(ScalingInstance {
            target_rect,
            source_rect,
        });
}

fn add_svg_filter_instances(
    instances: &mut Vec<(BatchTextures, Vec<SvgFilterInstance>)>,
    render_tasks: &RenderTaskGraph,
    filter: &SvgFilterInfo,
    task_id: RenderTaskId,
    input_1_task: Option<RenderTaskId>,
    input_2_task: Option<RenderTaskId>,
    extra_data_address: Option<GpuCacheAddress>,
) {
    let mut textures = BatchTextures::empty();

    if let Some(id) = input_1_task {
        textures.input.colors[0] = render_tasks[id].get_texture_source();
    }

    if let Some(id) = input_2_task {
        textures.input.colors[1] = render_tasks[id].get_texture_source();
    }

    let kind = match filter {
        SvgFilterInfo::Blend(..) => 0,
        SvgFilterInfo::Flood(..) => 1,
        SvgFilterInfo::LinearToSrgb => 2,
        SvgFilterInfo::SrgbToLinear => 3,
        SvgFilterInfo::Opacity(..) => 4,
        SvgFilterInfo::ColorMatrix(..) => 5,
        SvgFilterInfo::DropShadow(..) => 6,
        SvgFilterInfo::Offset(..) => 7,
        SvgFilterInfo::ComponentTransfer(..) => 8,
        SvgFilterInfo::Identity => 9,
        SvgFilterInfo::Composite(..) => 10,
    };

    let input_count = match filter {
        SvgFilterInfo::Flood(..) => 0,

        SvgFilterInfo::LinearToSrgb |
        SvgFilterInfo::SrgbToLinear |
        SvgFilterInfo::Opacity(..) |
        SvgFilterInfo::ColorMatrix(..) |
        SvgFilterInfo::Offset(..) |
        SvgFilterInfo::ComponentTransfer(..) |
        SvgFilterInfo::Identity => 1,

        // Not techincally a 2 input filter, but we have 2 inputs here: original content & blurred content.
        SvgFilterInfo::DropShadow(..) |
        SvgFilterInfo::Blend(..) |
        SvgFilterInfo::Composite(..) => 2,
    };

    let generic_int = match filter {
        SvgFilterInfo::Blend(mode) => *mode as u16,
        SvgFilterInfo::ComponentTransfer(data) =>
            (data.r_func.to_int() << 12 |
             data.g_func.to_int() << 8 |
             data.b_func.to_int() << 4 |
             data.a_func.to_int()) as u16,
        SvgFilterInfo::Composite(operator) =>
            operator.as_int() as u16,
        SvgFilterInfo::LinearToSrgb |
        SvgFilterInfo::SrgbToLinear |
        SvgFilterInfo::Flood(..) |
        SvgFilterInfo::Opacity(..) |
        SvgFilterInfo::ColorMatrix(..) |
        SvgFilterInfo::DropShadow(..) |
        SvgFilterInfo::Offset(..) |
        SvgFilterInfo::Identity => 0,
    };

    let instance = SvgFilterInstance {
        task_address: task_id.into(),
        input_1_task_address: input_1_task.map(|id| id.into()).unwrap_or(RenderTaskAddress(0)),
        input_2_task_address: input_2_task.map(|id| id.into()).unwrap_or(RenderTaskAddress(0)),
        kind,
        input_count,
        generic_int,
        padding: 0,
        extra_data_address: extra_data_address.unwrap_or(GpuCacheAddress::INVALID),
    };

    for (ref mut batch_textures, ref mut batch) in instances.iter_mut() {
        if let Some(combined_textures) = batch_textures.combine_textures(textures) {
            batch.push(instance);
            // Update the batch textures to the newly combined batch textures
            *batch_textures = combined_textures;
            return;
        }
    }

    instances.push((textures, vec![instance]));
}

// Information required to do a blit from a source to a target.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BlitJob {
    pub source: RenderTaskId,
    pub target_rect: DeviceIntRect,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Clone, Debug)]
pub struct LineDecorationJob {
    pub task_rect: DeviceRect,
    pub local_size: LayoutSize,
    pub wavy_line_thickness: f32,
    pub style: i32,
    pub axis_select: f32,
}

fn build_mask_tasks(
    info: &MaskSubPass,
    render_task_address: RenderTaskAddress,
    task_world_rect: WorldRect,
    target_rect: DeviceIntRect,
    main_prim_address: GpuBufferAddress,
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    clip_store: &ClipStore,
    data_stores: &DataStores,
    spatial_tree: &SpatialTree,
    gpu_buffer_builder: &mut GpuBufferBuilder,
    transforms: &mut TransformPalette,
    render_tasks: &RenderTaskGraph,
    results: &mut ClipMaskInstanceList,
) {
    for i in 0 .. info.clip_node_range.count {
        let clip_instance = clip_store.get_instance_from_range(&info.clip_node_range, i);
        let clip_node = &data_stores.clip[clip_instance.handle];

        let (clip_address, fast_path) = match clip_node.item.kind {
            ClipItemKind::RoundedRectangle { rect, radius, mode } => {
                let (fast_path, clip_address) = if radius.is_uniform().is_some() {
                    let mut writer = gpu_buffer_builder.f32.write_blocks(3);
                    writer.push_one(rect);
                    writer.push_one([radius.top_left.width, 0.0, 0.0, 0.0]);
                    writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);
                    let clip_address = writer.finish();

                    (true, clip_address)
                } else {
                    let mut writer = gpu_buffer_builder.f32.write_blocks(4);
                    writer.push_one(rect);
                    writer.push_one([
                        radius.top_left.width,
                        radius.top_left.height,
                        radius.top_right.width,
                        radius.top_right.height,
                    ]);
                    writer.push_one([
                        radius.bottom_left.width,
                        radius.bottom_left.height,
                        radius.bottom_right.width,
                        radius.bottom_right.height,
                    ]);
                    writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);
                    let clip_address = writer.finish();

                    (false, clip_address)
                };

                (clip_address, fast_path)
            }
            ClipItemKind::Rectangle { rect, mode, .. } => {
                assert_eq!(mode, ClipMode::Clip);

                let mut writer = gpu_buffer_builder.f32.write_blocks(3);
                writer.push_one(rect);
                writer.push_one([0.0, 0.0, 0.0, 0.0]);
                writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);
                let clip_address = writer.finish();

                (clip_address, true)
            }
            ClipItemKind::BoxShadow { .. } => {
                panic!("bug: box-shadow clips not expected on non-legacy rect/quads");
            }
            ClipItemKind::Image { rect, .. } => {
                let clip_transform_id = transforms.get_id(
                    clip_node.item.spatial_node_index,
                    raster_spatial_node_index,
                    spatial_tree,
                );

                let is_same_coord_system = spatial_tree.is_matching_coord_system(
                    prim_spatial_node_index,
                    raster_spatial_node_index,
                );

                let clip_needs_scissor_rect = !is_same_coord_system;
                let mut quad_flags = QuadFlags::SAMPLE_AS_MASK;

                if is_same_coord_system {
                    quad_flags |= QuadFlags::APPLY_DEVICE_CLIP;
                }

                for tile in clip_store.visible_mask_tiles(&clip_instance) {
                    let clip_prim_address = quad::write_prim_blocks(
                        &mut gpu_buffer_builder.f32,
                        rect,
                        rect,
                        PremultipliedColorF::WHITE,
                        &[QuadSegment {
                            rect: tile.tile_rect,
                            task_id: tile.task_id,
                        }]
                    );

                    let texture = render_tasks
                        .resolve_texture(tile.task_id)
                        .expect("bug: texture not found for tile");

                    quad::add_to_batch(
                        PatternKind::ColorOrTexture,
                        PatternShaderInput::default(),
                        render_task_address,
                        clip_transform_id,
                        clip_prim_address,
                        quad_flags,
                        EdgeAaSegmentMask::empty(),
                        0,
                        tile.task_id,
                        ZBufferId(0),
                        render_tasks,
                        gpu_buffer_builder,
                        |_, prim| {
                            if clip_needs_scissor_rect {
                                results
                                    .image_mask_instances_with_scissor
                                    .entry((target_rect, texture))
                                    .or_insert(Vec::new())
                                    .push(prim);
                            } else {
                                results
                                    .image_mask_instances
                                    .entry(texture)
                                    .or_insert(Vec::new())
                                    .push(prim);
                            }
                        }
                    );
                }

                // TODO(gw): For now, we skip the main mask prim below for image masks. Perhaps
                //           we can better merge the logic together?
                // TODO(gw): How to efficiently handle if the image-mask rect doesn't cover local prim rect?
                continue;
            }
        };

        let prim_spatial_node = spatial_tree.get_spatial_node(prim_spatial_node_index);
        let clip_spatial_node = spatial_tree.get_spatial_node(clip_node.item.spatial_node_index);
        let raster_spatial_node = spatial_tree.get_spatial_node(raster_spatial_node_index);
        let raster_clip = raster_spatial_node.coordinate_system_id == clip_spatial_node.coordinate_system_id;

        let (clip_space, clip_transform_id, main_prim_address, prim_transform_id, is_same_coord_system) = if raster_clip {
            let prim_transform_id = TransformPaletteId::IDENTITY;

            let clip_transform_id = transforms.get_id(
                raster_spatial_node_index,
                clip_node.item.spatial_node_index,
                spatial_tree,
            );

            let main_prim_address = quad::write_prim_blocks(
                &mut gpu_buffer_builder.f32,
                task_world_rect.cast_unit(),
                task_world_rect.cast_unit(),
                PremultipliedColorF::WHITE,
                &[],
            );

            (ClipSpace::Raster, clip_transform_id, main_prim_address, prim_transform_id, true)
        } else {
            let prim_transform_id = transforms.get_id(
                prim_spatial_node_index,
                raster_spatial_node_index,
                spatial_tree,
            );

            let clip_transform_id = if prim_spatial_node.coordinate_system_id < clip_spatial_node.coordinate_system_id {
                transforms.get_id(
                    clip_node.item.spatial_node_index,
                    prim_spatial_node_index,
                    spatial_tree,
                )
            } else {
                transforms.get_id(
                    prim_spatial_node_index,
                    clip_node.item.spatial_node_index,
                    spatial_tree,
                )
            };

            let is_same_coord_system = spatial_tree.is_matching_coord_system(
                prim_spatial_node_index,
                raster_spatial_node_index,
            );

            (ClipSpace::Primitive, clip_transform_id, main_prim_address, prim_transform_id, is_same_coord_system)
        };

        let clip_needs_scissor_rect = !is_same_coord_system;

        let quad_flags = if is_same_coord_system {
            QuadFlags::APPLY_DEVICE_CLIP
        } else {
            QuadFlags::empty()
        };

        quad::add_to_batch(
            PatternKind::Mask,
            PatternShaderInput::default(),
            render_task_address,
            prim_transform_id,
            main_prim_address,
            quad_flags,
            EdgeAaSegmentMask::all(),
            INVALID_SEGMENT_INDEX as u8,
            RenderTaskId::INVALID,
            ZBufferId(0),
            render_tasks,
            gpu_buffer_builder,
            |_, prim| {
                let instance = MaskInstance {
                    prim,
                    clip_transform_id,
                    clip_address: clip_address.as_int(),
                    clip_space,
                    unused: 0,
                };

                if clip_needs_scissor_rect {
                    if fast_path {
                        results.mask_instances_fast_with_scissor
                               .entry(target_rect)
                               .or_insert(Vec::new())
                               .push(instance);
                    } else {
                        results.mask_instances_slow_with_scissor
                               .entry(target_rect)
                               .or_insert(Vec::new())
                               .push(instance);
                    }
                } else {
                    if fast_path {
                        results.mask_instances_fast.push(instance);
                    } else {
                        results.mask_instances_slow.push(instance);
                    }
                }
            }
        );
    }
}

fn build_sub_pass(
    task_id: RenderTaskId,
    task: &RenderTask,
    gpu_buffer_builder: &mut GpuBufferBuilder,
    render_tasks: &RenderTaskGraph,
    transforms: &mut TransformPalette,
    ctx: &RenderTargetContext,
    output: &mut ClipMaskInstanceList,
) {
    if let Some(ref sub_pass) = task.sub_pass {
        match sub_pass {
            SubPass::Masks { ref masks } => {
                let render_task_address = task_id.into();
                let target_rect = task.get_target_rect();

                let (device_pixel_scale, content_origin, raster_spatial_node_index) = match task.kind {
                    RenderTaskKind::Picture(ref info) => {
                        (info.device_pixel_scale, info.content_origin, info.raster_spatial_node_index)
                    }
                    RenderTaskKind::Empty(ref info) => {
                        (info.device_pixel_scale, info.content_origin, info.raster_spatial_node_index)
                    }
                    RenderTaskKind::Prim(ref info) => {
                        (info.device_pixel_scale, info.content_origin, info.raster_spatial_node_index)
                    }
                    _ => panic!("unexpected: {}", task.kind.as_str()),
                };

                let content_rect = DeviceRect::new(
                    content_origin,
                    content_origin + target_rect.size().to_f32(),
                );

                build_mask_tasks(
                    masks,
                    render_task_address,
                    content_rect / device_pixel_scale,
                    target_rect,
                    masks.prim_address_f,
                    masks.prim_spatial_node_index,
                    raster_spatial_node_index,
                    ctx.clip_store,
                    ctx.data_stores,
                    ctx.spatial_tree,
                    gpu_buffer_builder,
                    transforms,
                    render_tasks,
                    output,
                );
            }
        }
    }
}
