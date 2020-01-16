#pragma once

namespace imqs {
namespace label {

static const float AssumedCircleZ = 0.0f;

Error SolveCircle(std::vector<gfx::Vec2f> circlePts, std::vector<float> circlePtAngles, gfx::Mat4d& world2View);

} // namespace label
} // namespace imqs