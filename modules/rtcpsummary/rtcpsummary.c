/**
 * @file rtcpsummary.c RTCP summary module
 * Output RTCP stats at the end of a call if there are any
 *
 *  Copyright (C) 2010 - 2018 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


static rtcpsummary_callback_h *m_callback = NULL;


static void printRtcpSummaryLine(const struct ua *ua, const struct stream *s)
{
	const struct rtcp_stats *rtcp;
	rtcp = stream_rtcp_stats(s);

	if (rtcp && (rtcp->tx.sent || rtcp->rx.sent)) {
		info("\n");
		/*
		 * Add a stats line to make it easier to parse result
		 * from script. Use a similar format used for the
		 * XRTP message in audio.c
		 */
		info(
			"EX=BareSip;"  /* Reporter Identifier */
			"CS=%d;"       /* Call Setup in ms */
			"CD=%d;"       /* Call Duration in sec */
			"PR=%u;"       /* Packets RX */
			"PS=%u;"       /* Packets TX */
			"PL=%d,%d;"    /* Packets Lost RX, TX */
			"PD=%d,%d;"    /* Packets Discarded, RX,TX */
			"JI=%.1f,%.1f;"/* Jitter RX, TX in ms */
			"DL=%.1f;"     /* RTT in ms */
			"IP=%J,%J;"    /* Local, Remote IPs */
			 "\n"
			,
			 call_setup_duration(stream_call(s)) * 1000,
			 call_duration(stream_call(s)),
			 rtcp->rx.sent,
			 rtcp->tx.sent,
			 rtcp->rx.lost,
			 rtcp->tx.lost,
			 stream_metric_get_rx_n_err(s),
			 stream_metric_get_tx_n_err(s),
			 1.0 * rtcp->rx.jit/1000,
			 1.0 * rtcp->tx.jit/1000,
			 1.0 * rtcp->rtt/1000,
			 sdp_media_laddr(stream_sdp(s)),
			 sdp_media_raddr(stream_sdp(s)));

		if (m_callback) {
			m_callback(
				ua,
				call_setup_duration(stream_call(s)) * 1000,
				call_duration(stream_call(s)),
				rtcp->rx.sent,
				rtcp->tx.sent,
				rtcp->rx.lost,
				rtcp->tx.lost,
				stream_metric_get_rx_n_err(s),
				stream_metric_get_tx_n_err(s),
				1.0 * rtcp->rx.jit / 1000,
				1.0 * rtcp->tx.jit / 1000,
				1.0 * rtcp->rtt / 1000,
				sdp_media_laddr(stream_sdp(s)),
				sdp_media_raddr(stream_sdp(s)));
		}
	}
	else {
		/*
			* put a line showing how
			* RTCP stats were NOT collected
			*/
		info("\n");
		info("EX=BareSip;ERROR=No RTCP stats collected;\n");
	}
}


static void ua_event_handler(struct ua *ua,
			     enum ua_event ev,
			     struct call *call,
			     const char *prm,
			     void *arg )
{
	const struct stream *s;
	struct le *le;
	(void)call;

	switch (ev) {

	case UA_EVENT_CALL_CLOSED:
		for (le = call_streaml(call)->head;
		     le;
		     le = le->next) {
			s = le->data;
			printRtcpSummaryLine(ua, s);
		}
		break;

	default:
		break;
	}
}


void rtcpsummary_set_callback(rtcpsummary_callback_h *callback)
{
	m_callback = callback;
}


static int module_init(void)
{
	int err = uag_event_register(ua_event_handler, NULL);
	if (err) {
		info("Error loading rtcpsummary module: %d", err);
		return err;
	}
	return 0;
}


static int module_close(void)
{
	debug("rtcpsummary: module closing..\n");
	uag_event_unregister(ua_event_handler);
	return 0;
}


const struct mod_export DECL_EXPORTS(rtcpsummary) = {
	"rtcpsummary",
	"application",
	module_init,
	module_close
};
