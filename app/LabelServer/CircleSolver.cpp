#include "pch.h"
#include "CircleSolver.h"

using namespace std;

namespace imqs {
namespace label {

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Circle Solver
// Summary:
// The only thing that seems to train reliably with backprop is the 4x4 matrix, without perspective divide.
// When we add in the perspective divide, the loss gets worse.
// I tried training camera positions instead of the raw 4x4 matrix, but that doesn't work (although
// I didn't try training it without the perspective divide).
// I also tried training the theta positions, after training the 4x4 matrix, but that only seems to work
// in the most trivial test case. In the real world cases, it just explodes the loss.
// I'd love to know why these things happen. It may simply be that the learning rate is too large,
// but I think there's more to it than that.
// It may be that the system is underdetermined.
// It may be local optimimums.
// It may be imbalanced variables, i.e. their magnitudes are so high that they destroy the gradient signal
// for the other variables.

//const float AssumedCircleZ = 0.0f;

const bool Use3x3      = false;
const auto TorchDevice = torch::kCPU;

at::Tensor MatTranslate(at::Tensor m, at::Tensor trans) {
	auto tm  = torch::eye(4).to(TorchDevice);
	tm[0][3] = trans[0];
	tm[1][3] = trans[1];
	tm[2][3] = trans[2];
	return at::matmul(m, tm);
}

at::Tensor MatLookAt(at::Tensor eyeV, at::Tensor centerV, at::Tensor upV) {
	// Make rotation matrix

	// Z vector
	auto z   = eyeV - centerV;
	auto mag = at::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	if (mag.item().toFloat() != 0) {
		z = z.clone() / mag;
	}

	// X vector = Up cross Z
	auto x = torch::zeros({3}).to(TorchDevice);
	x[0]   = upV[1] * z[2] - upV[2] * z[1];
	x[1]   = -upV[0] * z[2] + upV[2] * z[0];
	x[2]   = upV[0] * z[1] - upV[1] * z[0];

	// Y = Z cross X
	auto y = torch::zeros({3}).to(TorchDevice);
	y[0]   = z[1] * x[2] - z[2] * x[1];
	y[1]   = -z[0] * x[2] + z[2] * x[0];
	y[2]   = z[0] * x[1] - z[1] * x[0];

	//cout << "x:\n" << x << endl;
	//cout << "y:\n" << y << endl;

	// mpichler, 19950515
	// cross product gives area of parallelogram, which is < 1.0 for
	//  non-perpendicular unit-length vectors; so normalize x, y here

	mag = at::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	if (mag.item().toFloat() != 0) {
		x = x.clone() / mag;
	}

	mag = at::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	if (mag.item().toFloat() != 0) {
		y = y.clone() / mag;
	}

	auto m = torch::zeros({4, 4}).to(TorchDevice);
	//auto ma = m.accessor<float, 2>();
	//cout << "m:\n" << m << endl;
	//cout << "slice part level 1:\n" << m.slice(0, 0, 1) << endl;
	//cout << "slice part level 2:\n" << m.slice(0, 0, 1).slice(1, 0, 3) << endl;
	m.slice(0, 0, 1).slice(1, 0, 3) = x;
	m.slice(0, 1, 2).slice(1, 0, 3) = y;
	m.slice(0, 2, 3).slice(1, 0, 3) = z;

	m[3][3] = 1.0f;

	//cout << "m:\n" << m << endl;

	// Translate Eye to Origin
	//imat.Translate(-eyex, -eyey, -eyez);
	m = MatTranslate(m, -eyeV);

	//cout << "m:\n" << m << endl;

	return m;
}

at::Tensor MatFrustum(at::Tensor left, at::Tensor right, at::Tensor bottom, at::Tensor top, at::Tensor zNear, at::Tensor zFar) {
	auto A = (right + left) / (right - left);
	auto B = (top + bottom) / (top - bottom);
	auto C = -1 * (zFar + zNear) / (zFar - zNear);
	auto D = -1 * (2 * zFar * zNear) / (zFar - zNear);
	auto E = (2 * zNear) / (right - left);
	auto F = (2 * zNear) / (top - bottom);
	auto m = torch::zeros({4, 4}).to(TorchDevice);
	//cout << "A:\n" << A << endl;
	//cout << "m[0]:\n" << m[0] << endl;
	//cout << "m[0][0]:\n" << m[0][0] << endl;
	m.slice(0, 0, 1).slice(1, 2, 3) = A;
	m.slice(0, 1, 2).slice(1, 2, 3) = B;
	m.slice(0, 2, 3).slice(1, 2, 3) = C;
	m.slice(0, 3, 4).slice(1, 2, 3) = -1;
	m.slice(0, 2, 3).slice(1, 3, 4) = D;
	m.slice(0, 0, 1).slice(1, 0, 1) = E;
	m.slice(0, 1, 2).slice(1, 1, 2) = F;
	//m[0][2] = A;
	//m[1][2] = B;
	//m[2][2] = C;
	//m[3][2] = -1;
	//m[2][3] = D;
	//m[0][0] = E;
	//m[1][1] = F;
	return m;
}

at::Tensor MatPerspective(at::Tensor fovDegrees, at::Tensor aspect, at::Tensor zNear, at::Tensor zFar) {
	auto top    = zNear * at::tan(fovDegrees * IMQS_PI / 360.0);
	auto bottom = -top;
	auto left   = bottom * aspect;
	auto right  = top * aspect;
	return MatFrustum(left, right, bottom, top, zNear, zFar);
}

struct World2ViewNet : torch::nn::Module {
	torch::Tensor EyeV;
	torch::Tensor LookAtV;
	torch::Tensor UpV;
	torch::Tensor CameraFOV;
	torch::Tensor CameraAspect;
	torch::Tensor ZNear;
	torch::Tensor ZFar;

	torch::Tensor M3; // 3x3 matrix

	torch::Tensor M4;    // 4x4 matrix
	torch::Tensor Theta; // Every row[i] records the angle of sample point[i]

	bool Camera      = false;
	bool Perspective = false;

	World2ViewNet(size_t nPts) {
		//EyeV = register_parameter("EyeV", torch::tensor({0.f, 0.f, AssumedCircleZ - 30.0f}));
		//LookAtV = register_parameter("LookAtV", torch::tensor({0.f, 0.f, AssumedCircleZ}));
		EyeV         = register_parameter("EyeV", torch::tensor({-300.f, -200.f, AssumedCircleZ - 30.0f}));
		LookAtV      = register_parameter("LookAtV", torch::tensor({-300.f, -200.f, AssumedCircleZ}));
		UpV          = register_parameter("UpV", torch::tensor({0.f, 1.f, 0.f}));
		CameraFOV    = register_parameter("CameraFOV", torch::tensor({90.0f}));
		CameraAspect = register_parameter("CameraAspect", torch::tensor({1.0f}));
		ZNear        = register_parameter("ZNear", torch::tensor({1.0f}));
		ZFar         = register_parameter("ZFar", torch::tensor({1000.0f}));

		M3 = register_parameter("M3", torch::eye(3));
		M4 = register_parameter("M4", torch::eye(4));

		vector<float> angles;
		for (size_t i = 0; i < nPts; i++) {
			angles.push_back((float) i * (float) IMQS_PI * 2.0f / (float) nPts);
		}
		Theta = register_parameter("T", torch::tensor(angles));
		//Theta = torch::tensor(angles);
		//cout << Theta << endl;
	}
	//torch::Tensor forward(int iSample, torch::Tensor input) {
	torch::Tensor forward(int iSample) {
		if (Use3x3) {
			auto input = torch::empty({3}).to(TorchDevice);
			input[0]   = at::cos(Theta[iSample]);
			input[1]   = at::sin(Theta[iSample]);
			input[2]   = 1.0f;
			auto p     = at::matmul(M3, input);
			//p          = p.clone() / p[2];
			//cout << "M3\n"
			//     << M3 << endl;
			//cout << "p:\n"
			//     << p << endl;
			return p;
		}

		// build up the 4x4 transformation matrix
		if (Camera) {
			auto modelView  = MatLookAt(EyeV, LookAtV, UpV);
			auto projection = MatPerspective(CameraFOV, CameraAspect, ZNear, ZFar);
			modelView       = modelView.to(TorchDevice);
			projection      = projection.to(TorchDevice);
			auto mvProj     = at::matmul(modelView, projection);
			//auto mvProj = at::matmul(projection, modelView);
			//auto mvProj = modelView;
			M4 = mvProj;
		}
		//cout << "modelView\n" << modelView << endl;
		//cout << "projection\n" << projection << endl;
		//cout << "mvProj\n" << mvProj << endl;
		//exit(0);

		//cout << M << endl;
		//cout << input << endl;
		//auto p = M * input;
		//cout << p << endl;
		//auto input = torch::tensor({at::cos(Theta[iSample]), at::sin(Theta[iSample]), 0.0f, 1.0f});
		//auto input = torch::empty({4,1}).to(TorchDevice);
		auto input = torch::empty({4}).to(TorchDevice);
		input[0]   = at::cos(Theta[iSample]);
		input[1]   = at::sin(Theta[iSample]);
		input[2]   = AssumedCircleZ;
		input[3]   = 1.0f;
		//cout << input << endl;
		//auto p = at::matmul(M, input);
		auto p = at::matmul(M4, input);
		//cout << "input:\n" << input << endl;
		//cout << "ph:\n" << p << endl;
		//p /= p[3];
		if (Perspective) {
			//cout << "ph:\n"
			//     << p << endl;
			p = p.clone() / p[3];
			//cout << "p:\n"
			//     << p << endl;
			//exit(1);
		}
		//cout << "p:\n" << p << endl;
		return p;
	}
};

Error SolveCircle(std::vector<gfx::Vec2f> circlePts, std::vector<float> circlePtAngles, gfx::Mat4d& _world2View) {
	auto model = World2ViewNet(circlePts.size());
	model.to(TorchDevice);
	auto bestModelM4    = torch::zeros({1});
	auto bestModelM3    = torch::zeros({1});
	auto bestModelTheta = torch::zeros({1});
	//float             lr             = 8.0f;
	float             lr        = 0.2f;
	float             initialLR = lr;
	float             bestLoss  = 1e9f;
	torch::optim::SGD optim(model.parameters(), torch::optim::SGDOptions(lr));
	//torch::optim::Adam optim(model.parameters(), torch::optim::AdamOptions(lr));
	for (int cycle = 0; cycle < 2; cycle++) {
		if (cycle == 1) {
			model.Perspective = true;
			optim.options.learning_rate(initialLR * 1e-6f);
			//optim.options.learning_rate(initialLR * 0.01f);
		}
		for (int epoch = 0; epoch < 40; epoch++) {
			model.zero_grad();
			//model.EyeV.set_requires_grad(false);
			//model.LookAtV.set_requires_grad(false);
			model.UpV.set_requires_grad(false);
			model.CameraAspect.set_requires_grad(false);
			model.CameraFOV.set_requires_grad(false);
			model.ZNear.set_requires_grad(false);
			model.ZFar.set_requires_grad(false);
			//model.M4.set_requires_grad(cycle == 0);
			model.Theta.set_requires_grad(false);
			//model.Theta.set_requires_grad(cycle == 1);
			//auto loss = torch::Scalar(0.0f);
			auto loss = torch::zeros({1}).to(TorchDevice);
			for (size_t i = 0; i < circlePts.size(); i++) {
				auto viewPt = circlePts[i];
				//float th = circlePtAngles[i];
				//float x = cos(th);
				//float y = sin(th);
				//auto inp = torch::tensor({pt.x, pt.y, 0.f, 1.f});
				//auto inp = torch::tensor({x, y, 0.f, 1.f});
				//inp                           = inp.to(TorchDevice);
				auto out    = model.forward((int) i);
				auto dx     = out[0] - viewPt.x;
				auto dy     = out[1] - viewPt.y;
				auto distSQ = dx * dx + dy * dy;
				loss += distSQ;
				//if (model.Perspective)
				//	tsf::print("%3d: %6.3f %6.3f -> %6.3f %6.3f (%6.3f)\n", i, viewPt.x, viewPt.y, out[0].item().toDouble(), out[1].item().toDouble(), distSQ.item().toDouble());
				//tsf::print("%3d %6.3f %6.3f\n", i, dx.item().toDouble(), dx.item().toDouble());
				// force two left-most parameters on the bottom row to zero
				//loss += at::abs(model.M[3][0]);
				//loss += at::abs(model.M[3][1]);
				//auto len                      = at::sqrt(out[0] * out[0] + out[1] * out[1]);
				//auto distanceFromUnitCircleSQ = at::pow(len - 1, 2);
				//tsf::print("%3d %3d\n");
				//cout << "  distance: " << distanceFromUnitCircleSQ << endl;
				//loss += distanceFromUnitCircleSQ;
				//loss = out[0];
				//cout << "  loss: " << loss << endl;
				//break;
			}
			loss = loss.clone() / (float) circlePts.size();
			//cout << "loss:\n"
			//     << loss << endl;
			//cout << "matrix:\n"
			//     << model.M << endl;
			float lossVal = loss[0].item().toFloat();
			if (lossVal < bestLoss) {
				bestLoss = lossVal;
				if (Use3x3)
					bestModelM3 = model.M3.to(torch::kCPU);
				else
					bestModelM4 = model.M4.to(torch::kCPU);
				bestModelTheta = model.Theta.to(torch::kCPU);
			}
			//if (epoch % 5 == 0)
			//	tsf::print("%3d, loss: %.6f\n", epoch, lossVal);
			loss.backward();
			optim.step();
			//exit(1);
			//if (iter != 0 && iter % 10 == 0)
			//	lr *= 0.1f;
			if (lossVal > bestLoss && lr != initialLR) {
				//lr *= 0.1f;
				//optim.options.learning_rate(lr);
			}
		}
	}

	if (Use3x3) {
		cout << "M3:\n"
		     << bestModelM3 << endl;
	}

	// We need libmkl_avx2.so for inverse()
	//auto inv = at::inverse(model.M);
	//auto inv = model.M.inverse();
	//gfx::Mat4d view2World;
	gfx::Mat4d world2View;
	auto       ac = bestModelM4.accessor<float, 2>();
	for (int r = 0; r < 4; r++) {
		for (int c = 0; c < 4; c++) {
			//view2World.row[r][c] = ac[r][c];
			world2View.row[r][c] = ac[r][c];
		}
	}
	//auto world2View = view2World.Inverted();
	auto view2World = world2View.Inverted();

	//cout << "eye:\n"
	//     << model.EyeV << endl;
	//cout << "lookat:\n"
	//     << model.LookAtV << endl;
	//cout << "fov:\n"
	//     << model.CameraFOV << endl;

	//tsf::print("World2View:\n");
	//for (int i = 0; i < 4; i++)
	//	tsf::print("  %7.4f %7.4f %7.4f %7.4f\n", world2View.row[i][0], world2View.row[i][1], world2View.row[i][2], world2View.row[i][3]);

	//tsf::print("View2World:\n");
	//for (int i = 0; i < 4; i++)
	//	tsf::print("  %7.4f %7.4f %7.4f %7.4f\n", view2World.row[i][0], view2World.row[i][1], view2World.row[i][2], view2World.row[i][3]);

	//tsf::print("Sample points view -> world:\n");
	//for (auto pt : circlePts) {
	//	auto s = view2World * gfx::Vec4d(pt.x, pt.y, 0, 1);
	//	s = s * (1.0 / s.w);
	//	double len = gfx::Vec2d(s.x, s.y).size();
	//	tsf::print("  %7.4f %7.4f -> %7.4f %7.4f %7.4f (%.4f)\n", pt.x, pt.y, s.x, s.y, s.z, len);
	//}

	//tsf::print("Sample points world -> view:\n");
	//for (size_t i = 0; i < circlePts.size(); i++) {
	//	auto pt = circlePts[i];
	//	auto s = world2View * gfx::Vec4d(pt.x, pt.y, 0, 1);
	//	s = s * (1.0 / s.w);
	//	double len = gfx::Vec2d(s.x, s.y).size();
	//	tsf::print("  %7.4f %7.4f %7.4f -> %7.4f %7.4f %7.4f (%.4f)\n", pt.x, pt.y, s.x, s.y, s.z, len);
	//}

	_world2View = world2View;
	return Error();
}

} // namespace label
} // namespace imqs