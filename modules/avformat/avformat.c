/**
 * @file avformat.c  libavformat media-source
 *
 * Copyright (C) 2010 - 2020 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "mod_avformat.h"


/*
 * TODO:
 *
 * ok - Test looping
 * ok - check audio input/out samplerate/channels
 * ok - add locking of shared resources
 * ok - note: one single thread (shared) for audio+video
 * ok - add audio resampler -> (not yet)
 *    - test seeking fwd/back
 *    - proper timestamp sync
 *    - media-context, use device name or not?
 */


/**
 * @defgroup avformat avformat
 *
 * Video source using FFmpeg libavformat
 *
 *
 * Example config:
 \verbatim
  audio_source            avformat,/tmp/testfile.mp4
  video_source            avformat,/tmp/testfile.mp4
 \endverbatim
 */


static struct ausrc *ausrc;
static struct vidsrc *mod_avf;


static void shared_destructor(void *arg)
{
	struct shared *st = arg;

	info(".... avformat: ** shared context destroyed **\n");

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->au.ctx) {
		avcodec_close(st->au.ctx);
		avcodec_free_context(&st->au.ctx);
	}

	if (st->vid.ctx) {
		avcodec_close(st->vid.ctx);
		avcodec_free_context(&st->vid.ctx);
	}

	if (st->ic)
		avformat_close_input(&st->ic);

	mem_deref(st->lock);
}


static void *read_thread(void *data)
{
	struct shared *st = data;
	uint64_t now, offset = tmr_jiffies();
	double auts = 0, vidts = 0;

	while (st->run) {

		AVPacket pkt;
		int ret;

		sys_msleep(4);

		now = tmr_jiffies();

		for (;;) {
			double xts;

			if (st->au.idx >=0 && st->vid.idx >=0)
				xts = min(auts, vidts);
			else if (st->au.idx >=0)
				xts = auts;
			else if (st->au.idx >=0)
				xts = vidts;
			else
				break;

			if (now < (offset + xts))
				break;

			av_init_packet(&pkt);

			ret = av_read_frame(st->ic, &pkt);
			if (ret == (int)AVERROR_EOF) {

				debug("avformat: rewind stream\n");

				sys_msleep(1000);

				ret = av_seek_frame(st->ic, -1, 0,
						    AVSEEK_FLAG_BACKWARD);
				if (ret < 0) {
					info("avformat: seek error (%d)\n",
					     ret);
					goto out;
				}

				offset = tmr_jiffies();
				break;
			}
			else if (ret < 0) {
				debug("avformat: read error (%d)\n", ret);
				goto out;
			}

			if (pkt.stream_index == st->au.idx) {

				if (pkt.pts == AV_NOPTS_VALUE) {
					warning("no audio pts\n");
				}

				auts = 1000 * pkt.pts *
					av_q2d(st->au.time_base);

				avformat_audio_decode(st, &pkt);
			}
			else if (pkt.stream_index == st->vid.idx) {

				if (pkt.pts == AV_NOPTS_VALUE) {
					warning("no video pts\n");
				}

				vidts = 1000 * pkt.pts *
					av_q2d(st->vid.time_base);

				avformat_video_decode(st, &pkt);
			}

			av_packet_unref(&pkt);
		}
	}

 out:
	return NULL;
}


static int open_codec(struct stream *s, const struct AVStream *strm, int i,
		      AVCodecContext *ctx)
{
	AVCodec *codec;
	int ret;

	if (s->idx >= 0 || s->ctx)
		return 0;

	codec = avcodec_find_decoder(ctx->codec_id);
	if (!codec) {
		info("avformat: can't find codec %i\n",
		     ctx->codec_id);
		return ENOENT;
	}

	ret = avcodec_open2(ctx, codec, NULL);
	if (ret < 0) {
		warning("avformat: error opening codec (%i)\n",
			ret);
		return ENOMEM;
	}

	s->time_base = strm->time_base;
	s->ctx = ctx;
	s->idx = i;

	debug("avformat: '%s' using decoder '%s' (%s)\n",
	      av_get_media_type_string(ctx->codec_type),
	      codec->name, codec->long_name);

	return 0;
}


int avformat_shared_alloc(struct shared **shp, const char *dev)
{
	struct shared *st;
	unsigned i;
	int err;
	int ret;

	info(".... avformat: shared state (%s)\n", dev);

	st = mem_zalloc(sizeof(*st), shared_destructor);
	if (!st)
		return ENOMEM;

	st->id = "avformat";

	st->au.idx  = -1;
	st->vid.idx = -1;

	err = lock_alloc(&st->lock);
	if (err)
		goto out;

	ret = avformat_open_input(&st->ic, dev, NULL, NULL);
	if (ret < 0) {
		warning("avformat: avformat_open_input(%s) failed (ret=%s)\n",
			dev, av_err2str(ret));
		err = ENOENT;
		goto out;
	}

#if 0
	av_dump_format(st->ic, 0, "", 0);
#endif

	info("nb_streams: %d\n", st->ic->nb_streams);

	for (i=0; i<st->ic->nb_streams; i++) {

		const struct AVStream *strm = st->ic->streams[i];
		AVCodecContext *ctx;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)

		ctx = avcodec_alloc_context3(NULL);
		if (!ctx) {
			err = ENOMEM;
			goto out;
		}

		ret = avcodec_parameters_to_context(ctx, strm->codecpar);
		if (ret < 0) {
			warning("avformat: avcodec_parameters_to_context\n");
			err = EPROTO;
			goto out;
		}
#else
		ctx = strm->codec;
#endif

		switch (ctx->codec_type) {

		case AVMEDIA_TYPE_AUDIO:
			err = open_codec(&st->au, strm, i, ctx);
			if (err)
				goto out;
			break;

		case AVMEDIA_TYPE_VIDEO:
			err = open_codec(&st->vid, strm, i, ctx);
			if (err)
				goto out;
			break;

		default:
			break;
		}
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:

	if (err)
		mem_deref(st);
	else
		*shp = st;

	return err;
}


void avformat_shared_set_audio(struct shared *sh, struct ausrc_st *st)
{
	if (!sh)
		return;

	lock_write_get(sh->lock);
	sh->ausrc_st = st;
	lock_rel(sh->lock);
}


void avformat_shared_set_video(struct shared *sh, struct vidsrc_st *st)
{
	if (!sh)
		return;

	lock_write_get(sh->lock);
	sh->vidsrc_st = st;
	lock_rel(sh->lock);
}


static int module_init(void)
{
	int err;

	avformat_network_init();

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "avformat", avformat_audio_alloc);

	err |= vidsrc_register(&mod_avf, baresip_vidsrcl(),
			       "avformat", avformat_video_alloc, NULL);

	return err;
}


static int module_close(void)
{
	mod_avf = mem_deref(mod_avf);
	ausrc = mem_deref(ausrc);

	avformat_network_deinit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avformat) = {
	"avformat",
	"avsrc",
	module_init,
	module_close
};
