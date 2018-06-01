#include "pch.h"
#include "Decode.h"

namespace imqs {
namespace video {

StaticError VideoFile::ErrNeedMoreData("Codec needs more data");

double VideoStreamInfo::DurationSeconds() const {
	return (double) Duration / (double) AV_TIME_BASE;
}

double VideoStreamInfo::FrameRateSeconds() const {
	return av_q2d(FrameRate);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VideoFile::Initialize() {
	av_register_all();
}

VideoFile::VideoFile() {
}

VideoFile::~VideoFile() {
	Close();
}

void VideoFile::Close() {
	FlushCachedFrames();

	sws_freeContext(SwsCtx);
	avcodec_free_context(&VideoDecCtx);
	avformat_close_input(&FmtCtx);
	av_frame_free(&Frame);
	FmtCtx         = nullptr;
	VideoDecCtx    = nullptr;
	VideoStream    = nullptr;
	VideoStreamIdx = -1;
	Frame          = nullptr;
	SwsCtx         = nullptr;
	SwsDstW        = 0;
	SwsDstH        = 0;
	Filename       = "";
}

Error VideoFile::OpenFile(std::string filename) {
	Close();

	int r = avformat_open_input(&FmtCtx, filename.c_str(), NULL, NULL);
	if (r < 0)
		return TranslateErr(r, tsf::fmt("Could not open video file %v", filename).c_str());

	r = avformat_find_stream_info(FmtCtx, NULL);
	if (r < 0) {
		Close();
		return TranslateErr(r, "Could not find stream information");
	}

	auto err = OpenCodecContext(FmtCtx, AVMEDIA_TYPE_VIDEO, VideoStreamIdx, VideoDecCtx);
	if (!err.OK()) {
		Close();
		return err;
	}

	VideoStream = FmtCtx->streams[VideoStreamIdx];

	//av_dump_format(fmt_ctx, 0, src_filename, 0);

	Frame = av_frame_alloc();
	if (!Frame) {
		Close();
		return Error("Out of memory allocating frame");
	}

	Filename = filename;
	return Error();
}

VideoStreamInfo VideoFile::GetVideoStreamInfo() {
	VideoStreamInfo inf;
	// frame rate: 119.880116
	// duration: 4:31
	//int64_t duration = FmtCtx->duration;
	//int64_t tbase = VideoDecCtx->
	//av_rescale()
	inf.Duration  = FmtCtx->duration;
	inf.NumFrames = VideoStream->nb_frames;
	inf.FrameRate = VideoStream->r_frame_rate;
	Dimensions(inf.Width, inf.Height);
	return inf;
}

Error VideoFile::SeekToFrame(int64_t frame) {
	double ts = av_q2d(VideoStream->time_base);
	double t  = frame / ts;
	int    r  = av_seek_frame(FmtCtx, VideoStreamIdx, (int64_t) t, 0);
	return Error();
}

Error VideoFile::SeekToFraction(double fraction_0_to_1) {
	double seconds = fraction_0_to_1 * GetVideoStreamInfo().DurationSeconds();
	return SeekToMicrosecond((int64_t)(seconds * 1000000.0));
}

Error VideoFile::SeekToMicrosecond(int64_t microsecond) {
	double ts = av_q2d(VideoStream->time_base);
	double t  = ((double) microsecond / 1000000.0) / ts;
	int    r  = av_seek_frame(FmtCtx, VideoStreamIdx, (int64_t) t, 0);
	return Error();
}

double VideoFile::LastFrameTimeSeconds() const {
	return av_q2d(av_mul_q({(int) LastFramePTS, 1}, VideoStream->time_base));
}

int64_t VideoFile::LastFrameTimeMicrosecond() const {
	return (int64_t)(LastFrameTimeSeconds() * 1000000.0);
}

/* This turns out to be useless, because the FFMPeg rationals are stored as
32-bit num/dem, so with a denominator of 1000000 you quickly hit 32-bit limits.
int64_t VideoFile::LastFrameAVTime() const {
	// change the time unit to AV_TIME_BASE
	auto v1 = av_make_q(VideoStream->time_base.num, VideoStream->time_base.den);
	auto v2 = av_make_q(AV_TIME_BASE, 1);
	auto scale = av_mul_q(v1, v2);
	auto a = av_mul_q({(int) LastFramePTS, 1}, scale);
	return a.num;
}
*/

Error VideoFile::DecodeFrameRGBA(int width, int height, void* buf, int stride) {
	bool haveFrame = false;
	while (!haveFrame) {
		int r = avcodec_receive_frame(VideoDecCtx, Frame);
		switch (r) {
		case 0:
			haveFrame = true;
			break;
		case AVERROR_EOF:
			return ErrEOF;
		case AVERROR(EAGAIN): {
			// need more data
			AVPacket pkt;
			av_init_packet(&pkt);
			pkt.data = nullptr;
			pkt.size = 0;
			r        = av_read_frame(FmtCtx, &pkt);
			if (r != 0)
				return TranslateErr(r, "av_read_frame");
			r = avcodec_send_packet(VideoDecCtx, &pkt);
			av_packet_unref(&pkt);
			if (r == AVERROR_INVALIDDATA) {
				// skip over invalid data, and keep trying
			} else if (r != 0) {
				return TranslateErr(r, "avcodec_send_packet");
			}
			break;
		}
		default:
			return TranslateErr(r, "avcodec_receive_frame");
		}
	}
	IMQS_ASSERT(haveFrame);

	LastFramePTS = Frame->pts;

	if (SwsCtx && (SwsDstW != width) || (SwsDstH != height)) {
		sws_freeContext(SwsCtx);
		SwsCtx = nullptr;
	}

	if (!SwsCtx) {
		SwsCtx = sws_getContext(Frame->width, Frame->height, (AVPixelFormat) Frame->format, width, height, AVPixelFormat::AV_PIX_FMT_RGBA, 0, nullptr, nullptr, nullptr);
		if (!SwsCtx)
			return Error("Unable to create libswscale scaling context");
		SwsDstH = height;
		SwsDstW = width;
	}

	uint8_t* buf8         = (uint8_t*) buf;
	uint8_t* dst[4]       = {buf8 + 0, buf8 + 1, buf8 + 2, buf8 + 3};
	int      dstStride[4] = {stride, stride, stride, stride};

	sws_scale(SwsCtx, Frame->data, Frame->linesize, 0, Frame->height, dst, dstStride);

	return Error();
}

Error VideoFile::RecvFrame() {
	int ret = avcodec_receive_frame(VideoDecCtx, Frame);
	if (ret == 0)
		return Error();
	return TranslateErr(ret, "avcodec_receive_frame");
}

Error VideoFile::TranslateErr(int ret, const char* whileBusyWith) {
	char errBuf[AV_ERROR_MAX_STRING_SIZE + 1];

	switch (ret) {
	case AVERROR_EOF: return ErrEOF;
	case AVERROR(EAGAIN): return ErrNeedMoreData;
	default:
		av_strerror(ret, errBuf, sizeof(errBuf));
		if (whileBusyWith)
			return Error::Fmt("%v: %v", whileBusyWith, errBuf);
		else
			return Error::Fmt("AVERROR %v", errBuf);
	}
}

Error VideoFile::OpenCodecContext(AVFormatContext* fmt_ctx, AVMediaType type, int& stream_idx, AVCodecContext*& dec_ctx) {
	int           ret;
	AVStream*     st;
	AVCodec*      dec  = NULL;
	AVDictionary* opts = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0)
		return Error::Fmt("Could not find %s stream", av_get_media_type_string(type));

	stream_idx = ret;
	st         = fmt_ctx->streams[stream_idx];

	// find decoder for the stream
	dec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!dec)
		return Error::Fmt("Failed to find %s codec", av_get_media_type_string(type));

	// Allocate a codec context for the decoder
	dec_ctx = avcodec_alloc_context3(dec);
	if (!dec_ctx)
		return Error::Fmt("Failed to allocate %v codec context", av_get_media_type_string(type));

	// Copy codec parameters from input stream to output codec context
	ret = avcodec_parameters_to_context(dec_ctx, st->codecpar);
	if (ret < 0)
		return Error::Fmt("Failed to copy %s codec parameters to decoder context. Error %v", av_get_media_type_string(type), ret);

	// Init the video decoder
	//av_dict_set(&opts, "refcounted_frames", "1", 0); // not necessary, since avcodec_receive_frame() always uses refcounted frames
	//av_dict_set(&opts, "flags2", "+export_mvs", 0); // motion vectors
	ret = avcodec_open2(dec_ctx, dec, &opts);
	if (ret < 0)
		return Error::Fmt("Failed to open %s codec", av_get_media_type_string(type));

	return Error();
}

void VideoFile::FlushCachedFrames() {
	// flush cached frames in the codec's buffers
	if (!VideoDecCtx)
		return;

	// send an empty packet which instructs the codec to start flushing
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	avcodec_send_packet(VideoDecCtx, &pkt);

	// drain the codec
	while (true) {
		int r = avcodec_receive_frame(VideoDecCtx, Frame);
		if (r != 0)
			break;
	}
}
} // namespace video
} // namespace imqs