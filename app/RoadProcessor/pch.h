#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#define _CRT_SECURE_NO_WARNINGS 1

#define IMQS_ROADPROC_PCH_INCLUDED

#include <lib/pal/pal.h>
#include <lib/Video/Video.h>
#include <lib/gfx/gfx.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4503)
#endif
#include <nlohmann-json/json.hpp>

#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d.hpp>

#include <argparse/argparse.h>

#include <lensfun/lensfun.h>

#include <pdqsort/pdqsort.h>

//#include <glfw/deps/glad/glad.h>
//#include <glfw/include/GLFW/glfw3.h>

#include <EGL/egl.h>
//#include <GL/gl.h>

#include <glad/glad.h>
//#define GL_GLEXT_PROTOTYPES
//#include <GL/glext.h>

// proj4
#include <proj_api.h>

// intrinsics for SSE,AVX,etc
#include <x86intrin.h>

#include <cuda.h>
#include <cuda_runtime_api.h>

// Undefine ridiculous X11 macros
#undef Bool
#undef None
#undef Success

#include <torch/script.h>
#include <torch/csrc/api/include/torch/utils.h>

#include <avir/avir.h>
#include <colorspace/src/ColorSpace.h>
