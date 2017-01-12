/**
 * @file swscale.c  Video filter for scaling and pixel conversion
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libswscale/swscale.h>


static struct vidsz sz_dst = {0, 0};

struct swscale_enc {
	struct vidfilt_enc_st vf;   /**< Inheritance           */

	struct SwsContext *sws;
	struct vidframe *frame;
};

static enum vidfmt swscale_format = VID_FMT_YUV420P;  /* XXX: configurable */


static enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
	case VID_FMT_NV12:    return AV_PIX_FMT_NV12;
	case VID_FMT_NV21:    return AV_PIX_FMT_NV21;
	default:              return AV_PIX_FMT_NONE;
	}
}


static void encode_destructor(void *arg)
{
	struct swscale_enc *st = arg;

	list_unlink(&st->vf.le);

	mem_deref(st->frame);
	sws_freeContext(st->sws);
}


static int encode_update(struct vidfilt_enc_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct swscale_enc *st;
	int err = 0;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_enc_st *)st;

	return err;
}


static int encode_process(struct vidfilt_enc_st *st, struct vidframe *frame)
{
	struct swscale_enc *enc = (struct swscale_enc *)st;
	enum AVPixelFormat avpixfmt, avpixfmt_dst;
	const uint8_t *srcSlice[4];
	uint8_t *dst[4];
	int srcStride[4], dstStride[4];
	int i, h;
	int err = 0;
	struct vidsz sz_src;

	if (!st)
		return EINVAL;

	if (!frame)
		return 0;

	sz_src.w = frame->size.w;
	sz_src.h = frame->size.h;

	if (sz_dst.w <= 0) {
		sz_dst.w = sz_src.w;
	}
	if (sz_dst.h <= 0) {
		sz_dst.h = sz_src.h;
	}

	avpixfmt = vidfmt_to_avpixfmt(frame->fmt);
	if (avpixfmt == AV_PIX_FMT_NONE) {
		warning("swscale: unknown pixel-format (%s)\n",
			vidfmt_name(frame->fmt));
		return EINVAL;
	}

	avpixfmt_dst = vidfmt_to_avpixfmt(swscale_format);
	if (avpixfmt_dst == AV_PIX_FMT_NONE) {
		warning("swscale: unknown pixel-format (%s)\n",
			vidfmt_name(swscale_format));
		return EINVAL;
	}

	if (!enc->sws) {

		struct SwsContext *sws;
		int flags = 0;

		sws = sws_getContext(sz_src.w, sz_src.h, avpixfmt,
				     sz_dst.w, sz_dst.h, avpixfmt_dst,
				     flags, NULL, NULL, NULL);
		if (!sws) {
			warning("swscale: sws_getContext error\n");
			return ENOMEM;
		}

		enc->sws = sws;

		info("swscale: created SwsContext: `%s' --> `%s'\n",
		     vidfmt_name(frame->fmt),
		     vidfmt_name(swscale_format));
	}

	if (!enc->frame) {

		err = vidframe_alloc(&enc->frame, swscale_format,
				     &frame->size);
		if (err) {
			warning("swscale: vidframe_alloc error (%m)\n", err);
			return err;
		}
	}

	for (i=0; i<4; i++) {
		srcSlice[i]  = frame->data[i];
		srcStride[i] = frame->linesize[i];
		dst[i]       = enc->frame->data[i];
		dstStride[i] = enc->frame->linesize[i];
	}

	h = sws_scale(enc->sws, srcSlice, srcStride,
		      0, sz_src.h, dst, dstStride);
	if (h <= 0) {
		warning("swscale: sws_scale error (%d)\n", h);
		return EPROTO;
	}

	/* Copy the converted frame back to the input frame */
	for (i=0; i<4; i++) {
		frame->data[i]     = enc->frame->data[i];
		frame->linesize[i] = enc->frame->linesize[i];
	}
	frame->size = enc->frame->size;
	frame->fmt = enc->frame->fmt;

	return 0;
}


static struct vidfilt vf_swscale = {
	LE_INIT, "swscale", encode_update, encode_process, NULL, NULL
};


static int module_init(void)
{
	(void)conf_get_vidsz(conf_cur(), "video_size", &sz_dst);
	vidfilt_register(&vf_swscale);
	return 0;
}


static int module_close(void)
{
	vidfilt_unregister(&vf_swscale);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(swscale) = {
	"swscale",
	"vidfilt",
	module_init,
	module_close
};
