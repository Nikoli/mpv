/*
 * VA API output module
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <stdarg.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <va/va_x11.h>

#include <libavutil/common.h>
#include <libavcodec/vaapi.h>

#include "config.h"
#include "mpvcore/mp_msg.h"
#include "video/out/vo.h"
#include "video/memcpy_pic.h"
#include "sub/sub.h"
#include "sub/img_convert.h"
#include "x11_common.h"

#include "video/vfcap.h"
#include "video/mp_image.h"
#include "video/vaapi.h"
#include "video/decode/dec_video.h"

struct vaapi_osd_image {
    int            w, h;
    VAImage        image;
    VASubpictureID subpic_id;
    bool           is_used;
};

struct vaapi_subpic {
    VASubpictureID id;
    int src_x, src_y, src_w, src_h;
    int dst_x, dst_y, dst_w, dst_h;
};

struct vaapi_osd_part {
    bool active;
    int bitmap_pos_id;
    struct vaapi_osd_image image;
    struct vaapi_subpic subpic;
    struct osd_conv_cache *conv_cache;
};

#define MAX_OUTPUT_SURFACES 2

struct priv {
    struct mp_log           *log;
    struct vo               *vo;
    VADisplay                display;
    struct mp_vaapi_ctx     *mpvaapi;

    struct mp_image_params   image_params;
    struct mp_rect           src_rect;
    struct mp_rect           dst_rect;
    struct mp_osd_res        screen_osd_res;

    struct mp_image         *output_surfaces[MAX_OUTPUT_SURFACES];
    struct mp_image         *swdec_surfaces[MAX_OUTPUT_SURFACES];

    int                      output_surface;
    int                      visible_surface;
    int                      scaling;
    int                      force_scaled_osd;
    // with old libva versions only
    int                      deint;
    int                      deint_type;

    VAImageFormat            osd_format; // corresponds to OSD_VA_FORMAT
    struct vaapi_osd_part    osd_parts[MAX_OSD_PARTS];
    bool                     osd_screen;

    struct va_surface_pool  *pool;
    struct va_image_formats *va_image_formats;
    VAImageFormat           *va_subpic_formats;
    unsigned int            *va_subpic_flags;
    int                      va_num_subpic_formats;
    VADisplayAttribute      *va_display_attrs;
    int                      va_num_display_attrs;
};

#define OSD_VA_FORMAT VA_FOURCC_BGRA

static const bool osd_formats[SUBBITMAP_COUNT] = {
    // Actually BGRA, but only on little endian.
    // This will break on big endian, I think.
    [SUBBITMAP_RGBA] = true,
};

static void flush_output_surfaces(struct priv *p)
{
    for (int n = 0; n < MAX_OUTPUT_SURFACES; n++)
        mp_image_unrefp(&p->output_surfaces[n]);
    p->output_surface = 0;
    p->visible_surface = 0;
}

// See flush_surfaces() remarks - the same applies.
static void free_video_specific(struct priv *p)
{
    flush_output_surfaces(p);

    for (int n = 0; n < MAX_OUTPUT_SURFACES; n++)
        mp_image_unrefp(&p->swdec_surfaces[n]);
}

static bool alloc_swdec_surfaces(struct priv *p, int w, int h, int imgfmt)
{
    free_video_specific(p);
    for (int i = 0; i < MAX_OUTPUT_SURFACES; i++) {
        p->swdec_surfaces[i] =
            va_surface_pool_get_wrapped(p->pool, p->va_image_formats, imgfmt, w, h);
        if (!p->swdec_surfaces[i])
            return false;
    }
    return true;
}

static void resize(struct priv *p)
{
    vo_get_src_dst_rects(p->vo, &p->src_rect, &p->dst_rect, &p->screen_osd_res);

    // It's not clear whether this is needed; maybe not.
    //vo_x11_clearwindow(p->vo, p->vo->x11->window);

    p->vo->want_redraw = true;
}

static int reconfig(struct vo *vo, struct mp_image_params *params, int flags)
{
    struct priv *p = vo->priv;

    vo_x11_config_vo_window(vo, NULL, vo->dx, vo->dy, vo->dwidth, vo->dheight,
                            flags, "vaapi");

    if (!IMGFMT_IS_VAAPI(params->imgfmt)) {
        if (!alloc_swdec_surfaces(p, params->w, params->h, params->imgfmt))
            return -1;
    }

    p->image_params = *params;
    resize(p);
    return 0;
}

static int query_format(struct vo *vo, uint32_t imgfmt)
{
    struct priv *p = vo->priv;
    if (IMGFMT_IS_VAAPI(imgfmt) || va_image_format_from_imgfmt(p->va_image_formats, imgfmt))
        return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW;

    return 0;
}

static bool put_surface(struct priv *p, VASurfaceID surface, int flags)
{
    VAStatus status;
    status = vaPutSurface(p->display,
                          surface,
                          p->vo->x11->window,
                          p->src_rect.x0,
                          p->src_rect.y0,
                          p->src_rect.x1 - p->src_rect.x0,
                          p->src_rect.y1 - p->src_rect.y0,
                          p->dst_rect.x0,
                          p->dst_rect.y0,
                          p->dst_rect.x1 - p->dst_rect.x0,
                          p->dst_rect.y1 - p->dst_rect.y0,
                          NULL, 0,
                          flags);
    return check_va_status(status, "vaPutSurface()");
}

static bool render_to_screen(struct priv *p, struct mp_image *mpi)
{
    bool res = true;
    VAStatus status;

    VASurfaceID surface = va_surface_id_in_mp_image(mpi);
    if (surface == VA_INVALID_ID)
        return false;

    for (int n = 0; n < MAX_OSD_PARTS; n++) {
        struct vaapi_osd_part *part = &p->osd_parts[n];
        if (part->active) {
            struct vaapi_subpic *sp = &part->subpic;
            int flags = 0;
            if (p->osd_screen)
                flags |= VA_SUBPICTURE_DESTINATION_IS_SCREEN_COORD;
            status = vaAssociateSubpicture2(p->display,
                                            sp->id, &surface, 1,
                                            sp->src_x, sp->src_y,
                                            sp->src_w, sp->src_h,
                                            sp->dst_x, sp->dst_y,
                                            sp->dst_w, sp->dst_h,
                                            flags);
            check_va_status(status, "vaAssociateSubpicture()");
        }
    }

    unsigned int flags =
        (va_get_colorspace_flag(p->image_params.colorspace) | p->scaling);

    if (!p->deint) {
        put_surface(p, surface, flags);
    } else {
        // Deinterlacing with old libva versions
        for (int i = 0; i <= !!(p->deint > 1); i++) {
            int img_flags = p->deint && (mpi->fields & MP_IMGFIELD_INTERLACED) ?
                            (((!!(mpi->fields & MP_IMGFIELD_TOP_FIRST)) ^ i) == 0 ?
                            VA_BOTTOM_FIELD : VA_TOP_FIELD) : VA_FRAME_PICTURE;
            put_surface(p, surface, flags | img_flags);
        }
    }

    for (int n = 0; n < MAX_OSD_PARTS; n++) {
        struct vaapi_osd_part *part = &p->osd_parts[n];
        if (part->active) {
            struct vaapi_subpic *sp = &part->subpic;
            status = vaDeassociateSubpicture(p->display, sp->id,
                                             &surface, 1);
            check_va_status(status, "vaDeassociateSubpicture()");
        }
    }

    return res;
}

static void flip_page(struct vo *vo)
{
    struct priv *p = vo->priv;

    p->visible_surface = p->output_surface;
    render_to_screen(p, p->output_surfaces[p->output_surface]);
    p->output_surface = (p->output_surface + 1) % MAX_OUTPUT_SURFACES;
}

static void draw_image(struct vo *vo, struct mp_image *mpi)
{
    struct priv *p = vo->priv;

    if (!IMGFMT_IS_VAAPI(mpi->imgfmt)) {
        struct mp_image *wrapper = p->swdec_surfaces[p->output_surface];
        struct va_surface *surface = va_surface_in_mp_image(wrapper);
        if (!surface)
            return;
        if (!va_surface_upload(surface, mpi))
            return;
        mp_image_copy_attributes(wrapper, mpi);
        mpi = wrapper;
    }

    mp_image_setrefp(&p->output_surfaces[p->output_surface], mpi);
}

static struct mp_image *get_screenshot(struct priv *p)
{
    struct va_surface *surface =
        va_surface_in_mp_image(p->output_surfaces[p->visible_surface]);
    if (!surface)
        return NULL;
    struct mp_image *img = va_surface_download(surface, p->va_image_formats);
    if (!img)
        return NULL;
    struct mp_image_params params = p->image_params;
    params.imgfmt = img->imgfmt;
    mp_image_params_guess_csp(&params); // ensure colorspace consistency
    mp_image_set_params(img, &params);
    return img;
}

static bool redraw_frame(struct priv *p)
{
    p->output_surface = p->visible_surface;
    return render_to_screen(p, p->output_surfaces[p->output_surface]);
}

static void free_subpicture(struct priv *p, struct vaapi_osd_image *img)
{
    if (img->image.image_id != VA_INVALID_ID)
        vaDestroyImage(p->display, img->image.image_id);
    if (img->subpic_id != VA_INVALID_ID)
        vaDestroySubpicture(p->display, img->subpic_id);
    img->image.image_id = VA_INVALID_ID;
    img->subpic_id = VA_INVALID_ID;
}

static int new_subpicture(struct priv *p, int w, int h,
                          struct vaapi_osd_image *out)
{
    VAStatus status;

    free_subpicture(p, out);

    struct vaapi_osd_image m = {
        .image = {.image_id = VA_INVALID_ID, .buf = VA_INVALID_ID},
        .subpic_id = VA_INVALID_ID,
        .w = w,
        .h = h,
    };

    status = vaCreateImage(p->display, &p->osd_format, w, h, &m.image);
    if (!check_va_status(status, "vaCreateImage()"))
        goto error;
    status = vaCreateSubpicture(p->display, m.image.image_id, &m.subpic_id);
    if (!check_va_status(status, "vaCreateSubpicture()"))
        goto error;

    *out = m;
    return 0;

error:
    free_subpicture(p, &m);
    MP_ERR(p, "failed to allocate OSD sub-picture of size %dx%d.\n", w, h);
    return -1;
}

static void draw_osd_cb(void *pctx, struct sub_bitmaps *imgs)
{
    struct priv *p = pctx;

    struct vaapi_osd_part *part = &p->osd_parts[imgs->render_index];
    if (imgs->bitmap_pos_id != part->bitmap_pos_id) {
        part->bitmap_pos_id = imgs->bitmap_pos_id;

        osd_scale_rgba(part->conv_cache, imgs);

        struct mp_rect bb;
        if (!mp_sub_bitmaps_bb(imgs, &bb))
            goto error;

        // Prevent filtering artifacts on borders
        int pad = 2;

        int w = bb.x1 - bb.x0;
        int h = bb.y1 - bb.y0;
        if (part->image.w < w + pad || part->image.h < h + pad) {
            int sw = MP_ALIGN_UP(w + pad, 64);
            int sh = MP_ALIGN_UP(h + pad, 64);
            if (new_subpicture(p, sw, sh, &part->image) < 0)
                goto error;
        }

        struct vaapi_osd_image *img = &part->image;
        struct mp_image vaimg;
        if (va_image_map(p->display, &img->image, &vaimg) < 0)
            goto error;

        // Clear borders and regions uncovered by sub-bitmaps
        mp_image_clear(&vaimg, 0, 0, w + pad, h + pad);

        for (int n = 0; n < imgs->num_parts; n++) {
            struct sub_bitmap *sub = &imgs->parts[n];

            // Note: nothing guarantees that the sub-bitmaps don't overlap.
            //       But in all currently existing cases, they don't.
            //       We simply hope that this won't change, and nobody will
            //       ever notice our little shortcut here.

            size_t dst = (sub->y - bb.y0) * vaimg.stride[0] +
                         (sub->x - bb.x0) * 4;

            memcpy_pic(vaimg.planes[0] + dst, sub->bitmap, sub->w * 4, sub->h,
                       vaimg.stride[0], sub->stride);
        }

        if (va_image_unmap(p->display, &img->image) < 0)
            goto error;

        part->subpic = (struct vaapi_subpic) {
            .id = img->subpic_id,
            .src_x = 0,     .src_y = 0,
            .src_w = w,     .src_h = h,
            .dst_x = bb.x0, .dst_y = bb.y0,
            .dst_w = w,     .dst_h = h,
        };
    }

    part->active = true;

error:
    ;
}

static void draw_osd(struct vo *vo, struct osd_state *osd)
{
    struct priv *p = vo->priv;

    if (!p->osd_format.fourcc)
        return;

    struct mp_osd_res vid_res = {
        .w = p->image_params.w,
        .h = p->image_params.h,
        .display_par = 1.0 / vo->aspdat.par,
        .video_par = vo->aspdat.par,
    };

    struct mp_osd_res *res;
    if (p->osd_screen) {
        res = &p->screen_osd_res;
    } else {
        res = &vid_res;
    }

    for (int n = 0; n < MAX_OSD_PARTS; n++)
        p->osd_parts[n].active = false;
    osd_draw(osd, *res, osd->vo_pts, 0, osd_formats, draw_osd_cb, p);
}

static int get_displayattribtype(const char *name)
{
    if (!strcasecmp(name, "brightness"))
        return VADisplayAttribBrightness;
    else if (!strcasecmp(name, "contrast"))
        return VADisplayAttribContrast;
    else if (!strcasecmp(name, "saturation"))
        return VADisplayAttribSaturation;
    else if (!strcasecmp(name, "hue"))
        return VADisplayAttribHue;
    return -1;
}

static VADisplayAttribute *get_display_attribute(struct priv *p,
                                                 const char *name)
{
    int type = get_displayattribtype(name);
    for (int n = 0; n < p->va_num_display_attrs; n++) {
        VADisplayAttribute *attr = &p->va_display_attrs[n];
        if (attr->type == type)
            return attr;
    }
    return NULL;
}

static int get_equalizer(struct priv *p, const char *name, int *value)
{
    VADisplayAttribute * const attr = get_display_attribute(p, name);

    if (!attr || !(attr->flags & VA_DISPLAY_ATTRIB_GETTABLE))
        return VO_NOTIMPL;

    /* normalize to -100 .. 100 range */
    int r = attr->max_value - attr->min_value;
    if (r == 0)
        return VO_NOTIMPL;
    *value = ((attr->value - attr->min_value) * 200) / r - 100;
    return VO_TRUE;
}

static int set_equalizer(struct priv *p, const char *name, int value)
{
    VADisplayAttribute * const attr = get_display_attribute(p, name);
    VAStatus status;

    if (!attr || !(attr->flags & VA_DISPLAY_ATTRIB_SETTABLE))
        return VO_NOTIMPL;

    /* normalize to attribute value range */
    int r = attr->max_value - attr->min_value;
    if (r == 0)
        return VO_NOTIMPL;
    attr->value = ((value + 100) * r) / 200 + attr->min_value;

    status = vaSetDisplayAttributes(p->display, attr, 1);
    if (!check_va_status(status, "vaSetDisplayAttributes()"))
        return VO_FALSE;
    return VO_TRUE;
}

static int control(struct vo *vo, uint32_t request, void *data)
{
    struct priv *p = vo->priv;

    switch (request) {
    case VOCTRL_GET_DEINTERLACE:
        if (!p->deint_type)
            break;
        *(int*)data = !!p->deint;
        return VO_TRUE;
    case VOCTRL_SET_DEINTERLACE:
        if (!p->deint_type)
            break;
        p->deint = *(int*)data ? p->deint_type : 0;
        return VO_TRUE;
    case VOCTRL_GET_HWDEC_INFO: {
        struct mp_hwdec_info *arg = data;
        arg->vaapi_ctx = p->mpvaapi;
        return true;
    }
    case VOCTRL_SET_EQUALIZER: {
        struct voctrl_set_equalizer_args *eq = data;
        return set_equalizer(p, eq->name, eq->value);
    }
    case VOCTRL_GET_EQUALIZER: {
        struct voctrl_get_equalizer_args *eq = data;
        return get_equalizer(p, eq->name, eq->valueptr);
    }
    case VOCTRL_REDRAW_FRAME:
        return redraw_frame(p);
    case VOCTRL_SCREENSHOT: {
        struct voctrl_screenshot_args *args = data;
        args->out_image = get_screenshot(p);
        return true;
    }
    case VOCTRL_GET_PANSCAN:
        return VO_TRUE;
    case VOCTRL_SET_PANSCAN:
        resize(p);
        return VO_TRUE;
    }

    int events = 0;
    int r = vo_x11_control(vo, &events, request, data);
    if (events & VO_EVENT_RESIZE)
        resize(p);
    if (events & VO_EVENT_EXPOSE)
        vo->want_redraw = true;
    return r;
}

static void uninit(struct vo *vo)
{
    struct priv *p = vo->priv;

    free_video_specific(p);
    va_surface_pool_release(p->pool);

    for (int n = 0; n < MAX_OSD_PARTS; n++) {
        struct vaapi_osd_part *part = &p->osd_parts[n];
        free_subpicture(p, &part->image);
    }

    va_destroy(p->mpvaapi);

    vo_x11_uninit(vo);
}

static int preinit(struct vo *vo)
{
    struct priv *p = vo->priv;
    p->vo = vo;
    p->log = vo->log;

    VAStatus status;

    if (!vo_x11_init(vo))
        return -1;

    p->display = vaGetDisplay(vo->x11->display);
    if (!p->display)
        return -1;

    p->mpvaapi = va_initialize(p->display);
    if (!p->mpvaapi) {
        vaTerminate(p->display);
        return -1;
    }

    p->pool = va_surface_pool_alloc(p->display, VA_RT_FORMAT_YUV420);
    p->va_image_formats = p->mpvaapi->image_formats;

    int max_subpic_formats = vaMaxNumSubpictureFormats(p->display);
    p->va_subpic_formats = talloc_array(vo, VAImageFormat, max_subpic_formats);
    p->va_subpic_flags = talloc_array(vo, unsigned int, max_subpic_formats);
    status = vaQuerySubpictureFormats(p->display,
                                      p->va_subpic_formats,
                                      p->va_subpic_flags,
                                      &p->va_num_subpic_formats);
    if (!check_va_status(status, "vaQuerySubpictureFormats()"))
        p->va_num_subpic_formats = 0;
    MP_VERBOSE(vo, "%d subpicture formats available:\n",
               p->va_num_subpic_formats);

    for (int i = 0; i < p->va_num_subpic_formats; i++) {
        MP_VERBOSE(vo, "  %s, flags 0x%x\n",
                   VA_STR_FOURCC(p->va_subpic_formats[i].fourcc),
                   p->va_subpic_flags[i]);
        if (p->va_subpic_formats[i].fourcc == OSD_VA_FORMAT) {
            p->osd_format = p->va_subpic_formats[i];
            if (!p->force_scaled_osd) {
                p->osd_screen =
                    p->va_subpic_flags[i] & VA_SUBPICTURE_DESTINATION_IS_SCREEN_COORD;
            }
        }
    }

    if (!p->osd_format.fourcc)
        MP_ERR(vo, "OSD format not supported. Disabling OSD.\n");

    for (int n = 0; n < MAX_OSD_PARTS; n++) {
        struct vaapi_osd_part *part = &p->osd_parts[n];
        part->image.image.image_id = VA_INVALID_ID;
        part->image.subpic_id = VA_INVALID_ID;
        part->conv_cache = talloc_steal(vo, osd_conv_cache_new());
    }

    int max_display_attrs = vaMaxNumDisplayAttributes(p->display);
    p->va_display_attrs = talloc_array(vo, VADisplayAttribute, max_display_attrs);
    if (p->va_display_attrs) {
        status = vaQueryDisplayAttributes(p->display, p->va_display_attrs,
                                          &p->va_num_display_attrs);
        if (!check_va_status(status, "vaQueryDisplayAttributes()"))
            p->va_num_display_attrs = 0;
    }
    return 0;
}

#define OPT_BASE_STRUCT struct priv

const struct vo_driver video_out_vaapi = {
    .info = &(const vo_info_t) {
        "VA API with X11",
        "vaapi",
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com> and others",
        ""
    },
    .preinit = preinit,
    .query_format = query_format,
    .reconfig = reconfig,
    .control = control,
    .draw_image = draw_image,
    .draw_osd = draw_osd,
    .flip_page = flip_page,
    .uninit = uninit,
    .priv_size = sizeof(struct priv),
    .priv_defaults = &(const struct priv) {
        .scaling = VA_FILTER_SCALING_DEFAULT,
        .deint = 0,
#if !CONFIG_VAAPI_VPP
        .deint_type = 2,
#endif
    },
    .options = (const struct m_option[]) {
#if USE_VAAPI_SCALING
        OPT_CHOICE("scaling", scaling, 0,
                   ({"default", VA_FILTER_SCALING_DEFAULT},
                    {"fast", VA_FILTER_SCALING_FAST},
                    {"hq", VA_FILTER_SCALING_HQ},
                    {"nla", VA_FILTER_SCALING_NL_ANAMORPHIC})),
#endif
        OPT_CHOICE("deint", deint_type, 0,
                   ({"no", 0},
                    {"first-field", 1},
                    {"bob", 2})),
        OPT_FLAG("scaled-osd", force_scaled_osd, 0),
        {0}
    },
};
