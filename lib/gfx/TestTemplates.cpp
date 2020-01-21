#include "pch.h"
#include "Vec2.h"
#include "Vec3.h"
#include "Vec4.h"

// This file exists for testing template compilation issues

namespace imqs {
namespace gfx {

static void TestTemplates() {
	{
		Vec2d a(1, 2);
		Vec2d b = 5.0 * a;
	}
}

} // namespace gfx
} // namespace imqs