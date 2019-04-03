#pragma once

#include "OpticalFlow.h"
#include "Perspective.h"
#include "MeshRenderer.h"

namespace imqs {
namespace roadproc {

// VideoStitcher consumes video frames, and for each frame, it outputs either
// information about the vehicle's velocity, or a mesh that can be used to stitch
// the frames into one large mosaic. It operates exclusively on adjacent pairs
// of video frames.
// When VideoStitcher operates on a series of videos, it assumes that the videos
// are all part of the same recording session, and that the files are split up
// because of file size limitations (eg 4GB .MOV files). We assume that the very
// last frame of one file and the very first frame of the next file are the
// same time apart as any other pair of frames within a single video file. This
// assumption is valid for a Fuji X-T2.
// Right now only the CPU perspective removal supports lens correction, but
// I've commented out the CPU path in here, and hardcoded it to use the GPU.
// It shouldn't be too hard to make the GPU also do the lens correction, but
// it's just not a massive priority right now.
class VideoStitcher {
public:
	// Running state
	int               VideoWidth  = 0;
	int               VideoHeight = 0;
	gfx::Image        Frame;
	gfx::Image        Flat;
	gfx::Image        FlatPrev;
	gfx::Image        Splat;
	gfx::Image        FullFlat;           // If EnableFullFlatOutput is true, then FullFlat contains the entire frustum, with perspective removed
	gfx::Image        BrightnessAdjuster; // This is dynamically adjusted
	gfx::Image        VignetteAdjust;     // This is computed during initialization, and then held constant
	gfx::Image        DistortionAdjust;   // This is computed during initialization, and then held constant
	video::VideoFile  Video;
	video::NVVideo    NVVid;
	video::IVideo*    ActiveVideo = nullptr;
	FlattenParams     FP;
	OpticalFlow       Flow;
	roadproc::Frustum Frustum;
	double            BlackenPercentage = 0;   // If non-zero, then blacken left/right edges, so they don't make it into the stitched footage
	int               FlatWidth         = 0;   // computed according to perspective params and video size
	int               FlatHeight        = 550; // Only perform matching on this window, aligned to the bottom of the flattened frame
	int               MatchHeight       = 150; // Only perform matching from the bottom matchHeight pixels of the 'next' flattened frame
	int               PixelsPerMeshCell = 60;  // Stride between each matching grid cell
	time::Duration    RemainingTime;
	double            FrameTime                   = 0; // Absolute video time in seconds, of most recently decoded frame
	size_t            FrameNumber                 = 0;
	double            StartVideoAt                = 0;     // Seeks first frame of video to X seconds of first video.
	bool              EnableFullFlatOutput        = false; // If true, then FullFlat contains the full flat image output
	bool              EnableCPUPerspectiveRemoval = false; // CPU path supports lens correction, but it's slower
	bool              EnableBrightnessAdjuster    = true;
	bool              EnableNVVideo               = true;

	// Output
	bool                                       EnableDebugPrint  = false;
	double                                     TotalVideoSeconds = 0;  // total length of all video files
	time::Time                                 FirstVideoCreationTime; // Metadata extract from first video
	std::vector<std::pair<double, gfx::Vec2f>> Velocities;             // Velocities for every frame as [time,velocity]. Velocity of frame zero is copied from frame 1. Velocity is in flattened pixels.
	roadproc::Mesh                             Mesh;                   // The most recently stitched mesh

	Error       Start(std::vector<std::string> videoFiles, FlattenParams fp);
	Error       Rewind();               // Rewind to StartVideoAt of first video
	Error       Next();                 // Process the next frame
	gfx::Rect32 CropRectFromFullFlat(); // Returns the crop rectangle (out of the full flattened frustum image) that is used for alignment.
	void        PrintRemainingTime();

private:
	std::vector<std::string> VideoFiles;
	MeshRenderer             Rend;
	double                   VideoTimeOffset = 0; // Accumulating time counter, so that we can merge multiple videos into one timelime
	time::Time               ProcessingStartTime;
	size_t                   CurrentVideo = -1;

	// Tracking parameters
	float                   AngleDeadAhead         = -90; // -90 is when the camera is facing straight forward from the car, and driving straight
	float                   ExpectedAngleRange     = 10;  // expect camera to be pointing at most ExpectedAngleRange degrees left or right, from dead ahead
	float                   AvgImgDiff             = 0;
	float                   AbsDivergenceThreshold = 30; // When running velocity differs from absolute by more than this, we invoke a restart
	gfx::Vec2f              FlowBias;
	gfx::Vec2f              AvgDir;
	std::vector<gfx::Vec2f> AbsFlowBias;
	const int               AbsRestartCheckInterval = 100;
	int                     AbsRestart              = AbsRestartCheckInterval;
	bool                    NeedResync              = false; // If true, then we're trying to regain lock from a series of good absolute locks
	std::vector<float>      BrightnessDelta;

	Error       LoadNextFrame();
	void        ComputeTimeRemaining();
	void        RemovePerspective();
	Error       ComputeStitch();
	void        CheckSyncRestart(FlowResult& absFlowResult, bool& didReset);
	void        ComputeBrightnessAdjustment(gfx::Vec2f disp);
	void        SetupMesh(roadproc::Mesh& m);
	static void SetupMesh(int srcWidth, int srcHeight, int matchHeight, int pixelsPerMeshCell, int flowMatchRadius, roadproc::Mesh& m);
};

} // namespace roadproc
} // namespace imqs