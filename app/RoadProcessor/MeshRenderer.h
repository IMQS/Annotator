#pragma once

#include "Mesh.h"

namespace imqs {
namespace roadproc {

// GPU based mesh renderer
// This thing takes an unprojected image, with a distortion mesh, and renders that mesh
// onto the gigantic flat earth canvas.
// I've tested creating a framebuffer up to 8192x8192 on a Geforce 1080.
class MeshRenderer {
public:
	int FBWidth  = 0; // Framebuffer width
	int FBHeight = 0; // Framebuffer height

	~MeshRenderer();

	// Create a GPU rendering context with the given width and height
	Error Initialize(int fbWidth, int fbHeight);
	void  Destroy(); // Called by destructor

	void Clear(gfx::Color8 color);
	void CopyImageToDevice(const gfx::Image& img);
	void CopyDeviceToImage(gfx::Image& img);
	void DrawMesh(const Mesh& m, const gfx::Image& img);

	Error DrawHelloWorldTriangle();

private:
	bool        IsInitialized = false;
	GLFWwindow* Window        = nullptr;
	GLuint      FBO           = -1;
	GLuint      FBTex         = -1;
	GLuint      CopyShader    = -1;

	Error CompileShader(std::string vertexSrc, std::string fragSrc, GLuint& shader);
};

} // namespace roadproc
} // namespace imqs