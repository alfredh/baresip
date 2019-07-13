/**
 * @file opus/decode.c Opus Decode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <opus/opus.h>
#include "opus.h"


struct audec_state {
	OpusDecoder *dec;
	unsigned ch;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	if (ads->dec)
		opus_decoder_destroy(ads->dec);
}


int opus_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp)
{
	struct audec_state *ads;
	int opuserr, err = 0;
	(void)fmtp;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->ch = ac->ch;

	ads->dec = opus_decoder_create(ac->srate, ac->ch, &opuserr);
	if (!ads->dec) {
		warning("opus: decoder create: %s\n", opus_strerror(opuserr));
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int opus_decode_frm(struct audec_state *ads,
		    int fmt, void *sampv, size_t *sampc,
		    const uint8_t *buf, size_t len)
{
	int n;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	switch (fmt) {

	case AUFMT_S16LE:
		n = opus_decode(ads->dec, buf, (opus_int32)len,
				sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0) {
			warning("opus: decode error: %s\n", opus_strerror(n));
			return EPROTO;
		}
		break;

	case AUFMT_FLOAT:
		n = opus_decode_float(ads->dec, buf, (opus_int32)len,
				      sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0) {
			warning("opus: float decode error: %s\n",
				opus_strerror(n));
			return EPROTO;
		}
		break;

	default:
		return ENOTSUP;
	}

	*sampc = n * ads->ch;

	return 0;
}


int opus_decode_pkloss(struct audec_state *ads,
		       int fmt, void *sampv, size_t *sampc,
		       const uint8_t *buf, size_t len)
{
	int16_t *s16 = sampv;
	int n;
	opus_int32 frame_size;
	size_t nsamp = 0;

	if (!ads || !sampv || !sampc)
		return EINVAL;

	opus_decoder_ctl(ads->dec, OPUS_GET_LAST_PACKET_DURATION(&frame_size));

	n = opus_decode(ads->dec, buf, (opus_int32)len,
			sampv, (int)(frame_size), 1);
	if (n < 0) {
		warning("opus: decode error: %s\n", opus_strerror(n));
		return EPROTO;
	}

	nsamp = n * ads->ch;
	s16 += nsamp;

	n = opus_decode(ads->dec, buf, (opus_int32)len,
			s16, (int)(frame_size), 0);
	if (n < 0)
		return EPROTO;

	nsamp += (n * ads->ch);

	if (nsamp > *sampc) {
		warning("opus: pkloss: buffer too small.\n");
		return ENOMEM;
	}

	*sampc = nsamp;

	re_printf("opus: pkloss: %zu samples\n", *sampc);

	return 0;
}
