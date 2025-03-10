/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// The common infrastructure for ps_quad_* shaders.
///
/// # Memory layout
///
/// The diagram below shows the the various pieces of data fectched in the vertex shader:
///
///```ascii
///                                       (int gpu buffer)
///                                       +---------------+    (sGpuCache)
///  (instance-step vertex attr)          |  Int header   |   +-----------+
/// +-----------------------------+       |               |   | Transform |
/// |    Quad instance (uvec4)    |  +--> | transform id +--> +-----------+
/// |                             |  |    | z id          |
/// | x: int prim address        +---+    +---------------+   (float gpu buffer)
/// | y: float prim address      +--------------------------> +-----------+--------------+-+-+
/// | z: quad flags               |      (sGpuCache)          | Quad Prim | Quad Segment | | |
/// |    edge flags               |   +--------------------+  |           |              | | |
/// |    part index               |   |     Picture task   |  | bounds    | rect         | | |
/// |    segment index            |   |                    |  | clip      | uv rect      | | |
/// | w: picture task address    +--> | task rect          |  | color     |              | | |
/// +-----------------------------+   | device pixel scale |  +-----------+--------------+-+-+
///                                   | content origin     |
///                                   +--------------------+
///
/// To use the quad infrastructure, a shader must define the following entry
/// points in the corresponding shader stages:
/// - void pattern_vertex(PrimitiveInfo prim)
/// - vec4 pattern_fragment(vec4 base_color)
///```

#define WR_FEATURE_TEXTURE_2D

#include shared,rect,transform,render_task,gpu_buffer

flat varying mediump vec4 v_color;
// w: has edge flags
// x,y,z are avaible for patterns to use.
flat varying lowp ivec4 v_flags;

#ifndef SWGL_ANTIALIAS
varying highp vec2 vLocalPos;
#endif

#ifdef WR_VERTEX_SHADER

#define EDGE_AA_LEFT    1
#define EDGE_AA_TOP     2
#define EDGE_AA_RIGHT   4
#define EDGE_AA_BOTTOM  8

#define PART_CENTER     0
#define PART_LEFT       1
#define PART_TOP        2
#define PART_RIGHT      3
#define PART_BOTTOM     4
#define PART_ALL        5

#define QF_IS_OPAQUE            1
#define QF_APPLY_DEVICE_CLIP    2
#define QF_IGNORE_DEVICE_SCALE  4
#define QF_USE_AA_SEGMENTS      8
#define QF_SAMPLE_AS_MASK       16

#define INVALID_SEGMENT_INDEX   0xff

#define AA_PIXEL_RADIUS 2.0

PER_INSTANCE in ivec4 aData;

struct QuadSegment {
    RectWithEndpoint rect;
    RectWithEndpoint uv_rect;
};

struct PrimitiveInfo {
    vec2 local_pos;

    RectWithEndpoint local_prim_rect;
    RectWithEndpoint local_clip_rect;

    QuadSegment segment;

    int edge_flags;
    int quad_flags;
    ivec2 pattern_input;
};

struct QuadPrimitive {
    RectWithEndpoint bounds;
    RectWithEndpoint clip;
    vec4 color;
};

QuadSegment fetch_segment(int base, int index) {
    QuadSegment seg;

    vec4 texels[2] = fetch_from_gpu_buffer_2f(base + 3 + index * 2);

    seg.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    seg.uv_rect = RectWithEndpoint(texels[1].xy, texels[1].zw);

    return seg;
}

QuadPrimitive fetch_primitive(int index) {
    QuadPrimitive prim;

    vec4 texels[3] = fetch_from_gpu_buffer_3f(index);

    prim.bounds = RectWithEndpoint(texels[0].xy, texels[0].zw);
    prim.clip = RectWithEndpoint(texels[1].xy, texels[1].zw);
    prim.color = texels[2];

    return prim;
}

struct QuadHeader {
    int transform_id;
    int z_id;
    ivec2 pattern_input;
};

QuadHeader fetch_header(int address) {
    ivec4 header = fetch_from_gpu_buffer_1i(address);

    QuadHeader qh = QuadHeader(
        header.x,
        header.y,
        header.zw
    );

    return qh;
}

struct QuadInstance {
    // x
    int prim_address_i;

    // y
    int prim_address_f;

    // z
    int quad_flags;
    int edge_flags;
    int part_index;
    int segment_index;

    // w
    int picture_task_address;
};

QuadInstance decode_instance() {
    QuadInstance qi = QuadInstance(
        aData.x,

        aData.y,

        (aData.z >> 24) & 0xff,
        (aData.z >> 16) & 0xff,
        (aData.z >>  8) & 0xff,
        (aData.z >>  0) & 0xff,

        aData.w
    );

    return qi;
}

struct VertexInfo {
    vec2 local_pos;
};

VertexInfo write_vertex(vec2 local_pos,
                        float z,
                        Transform transform,
                        vec2 content_origin,
                        RectWithEndpoint task_rect,
                        float device_pixel_scale,
                        int quad_flags) {
    VertexInfo vi;

    // Transform the current vertex to world space.
    vec4 world_pos = transform.m * vec4(local_pos, 0.0, 1.0);

    // Convert the world positions to device pixel space.
    vec2 device_pos = world_pos.xy * device_pixel_scale;

    if ((quad_flags & QF_APPLY_DEVICE_CLIP) != 0) {
        RectWithEndpoint device_clip_rect = RectWithEndpoint(
            content_origin,
            content_origin + task_rect.p1 - task_rect.p0
        );

        // Clip to task rect
        device_pos = rect_clamp(device_clip_rect, device_pos);

        vi.local_pos = (transform.inv_m * vec4(device_pos / device_pixel_scale, 0.0, 1.0)).xy;
    } else {
        vi.local_pos = local_pos;
    }

    // Apply offsets for the render task to get correct screen location.
    vec2 final_offset = -content_origin + task_rect.p0;

    gl_Position = uTransform * vec4(device_pos + final_offset * world_pos.w, z * world_pos.w, world_pos.w);

    return vi;
}

float edge_aa_offset(int edge, int flags) {
    return ((flags & edge) != 0) ? AA_PIXEL_RADIUS : 0.0;
}

#ifdef WR_VERTEX_SHADER
void pattern_vertex(PrimitiveInfo prim);
#endif

PrimitiveInfo quad_primive_info(void) {
    QuadInstance qi = decode_instance();

    QuadHeader qh = fetch_header(qi.prim_address_i);
    Transform transform = fetch_transform(qh.transform_id);
    PictureTask task = fetch_picture_task(qi.picture_task_address);
    QuadPrimitive prim = fetch_primitive(qi.prim_address_f);
    float z = float(qh.z_id);

    QuadSegment seg;
    if (qi.segment_index == INVALID_SEGMENT_INDEX) {
        seg.rect = prim.bounds;
        seg.uv_rect = RectWithEndpoint(vec2(0.0), vec2(0.0));
    } else {
        seg = fetch_segment(qi.prim_address_f, qi.segment_index);
    }

    // The local space rect that we will draw, which is effectively:
    //  - The tile within the primitive we will draw
    //  - Intersected with any local-space clip rect(s)
    //  - Expanded for AA edges where appropriate
    RectWithEndpoint local_coverage_rect = seg.rect;

    // Apply local clip rect
    local_coverage_rect.p0 = max(local_coverage_rect.p0, prim.clip.p0);
    local_coverage_rect.p1 = min(local_coverage_rect.p1, prim.clip.p1);
    local_coverage_rect.p1 = max(local_coverage_rect.p0, local_coverage_rect.p1);

    switch (qi.part_index) {
        case PART_LEFT:
            local_coverage_rect.p1.x = local_coverage_rect.p0.x + AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_LEFT);
#else
            local_coverage_rect.p0.x -= AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y -= AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y += AA_PIXEL_RADIUS;
#endif
            break;
        case PART_TOP:
            local_coverage_rect.p0.x = local_coverage_rect.p0.x + AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x = local_coverage_rect.p1.x - AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y = local_coverage_rect.p0.y + AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_TOP);
#else
            local_coverage_rect.p0.y -= AA_PIXEL_RADIUS;
#endif
            break;
        case PART_RIGHT:
            local_coverage_rect.p0.x = local_coverage_rect.p1.x - AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_RIGHT);
#else
            local_coverage_rect.p1.x += AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y -= AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y += AA_PIXEL_RADIUS;
#endif
            break;
        case PART_BOTTOM:
            local_coverage_rect.p0.x = local_coverage_rect.p0.x + AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x = local_coverage_rect.p1.x - AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y = local_coverage_rect.p1.y - AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_BOTTOM);
#else
            local_coverage_rect.p1.y += AA_PIXEL_RADIUS;
#endif
            break;
        case PART_CENTER:
            local_coverage_rect.p0.x += edge_aa_offset(EDGE_AA_LEFT, qi.edge_flags);
            local_coverage_rect.p1.x -= edge_aa_offset(EDGE_AA_RIGHT, qi.edge_flags);
            local_coverage_rect.p0.y += edge_aa_offset(EDGE_AA_TOP, qi.edge_flags);
            local_coverage_rect.p1.y -= edge_aa_offset(EDGE_AA_BOTTOM, qi.edge_flags);
            break;
        case PART_ALL:
        default:
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(qi.edge_flags);
#else
            local_coverage_rect.p0.x -= edge_aa_offset(EDGE_AA_LEFT, qi.edge_flags);
            local_coverage_rect.p1.x += edge_aa_offset(EDGE_AA_RIGHT, qi.edge_flags);
            local_coverage_rect.p0.y -= edge_aa_offset(EDGE_AA_TOP, qi.edge_flags);
            local_coverage_rect.p1.y += edge_aa_offset(EDGE_AA_BOTTOM, qi.edge_flags);
#endif
            break;
    }

    vec2 local_pos = mix(local_coverage_rect.p0, local_coverage_rect.p1, aPosition);

    float device_pixel_scale = task.device_pixel_scale;
    if ((qi.quad_flags & QF_IGNORE_DEVICE_SCALE) != 0) {
        device_pixel_scale = 1.0f;
    }

    VertexInfo vi = write_vertex(
        local_pos,
        z,
        transform,
        task.content_origin,
        task.task_rect,
        device_pixel_scale,
        qi.quad_flags
    );

    v_color = prim.color;

    return PrimitiveInfo(
        vi.local_pos,
        prim.bounds,
        prim.clip,
        seg,
        qi.edge_flags,
        qi.quad_flags,
        qh.pattern_input
    );
}

void antialiasing_vertex(PrimitiveInfo prim) {
#ifndef SWGL_ANTIALIAS
    // This does the setup that is required for init_tranform_vs.
    RectWithEndpoint xf_bounds = RectWithEndpoint(
        max(prim.local_prim_rect.p0, prim.local_clip_rect.p0),
        min(prim.local_prim_rect.p1, prim.local_clip_rect.p1)
    );
    vTransformBounds = vec4(xf_bounds.p0, xf_bounds.p1);

    vLocalPos = prim.local_pos;

    if (prim.edge_flags == 0) {
        v_flags.w = 0;
    } else {
        v_flags.w = 1;
    }
#endif
}

void main() {
    PrimitiveInfo prim = quad_primive_info();
    antialiasing_vertex(prim);
    pattern_vertex(prim);
}
#endif

#ifdef WR_FRAGMENT_SHADER
vec4 pattern_fragment(vec4 base_color);

float antialiasing_fragment() {
    float alpha = 1.0;
#ifndef SWGL_ANTIALIAS
    if (v_flags.w != 0) {
        alpha = init_transform_fs(vLocalPos);
    }
#endif
    return alpha;
}

void main() {
    vec4 base_color = v_color;
    base_color *= antialiasing_fragment();
    oFragColor = pattern_fragment(base_color);
}

#endif
