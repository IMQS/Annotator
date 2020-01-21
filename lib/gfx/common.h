#pragma once

// These are includes that are common to gfx compilation, as well as anything that imports gfx

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 28020)
#endif
#include <cmath>
#include <limits>
#include <lib/pal/pal.h>
#include <turbojpeg.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <agg/include/agg_basics.h>
#include <agg/include/agg_conv_stroke.h>
#include <agg/include/agg_conv_curve.h>
#include <agg/include/agg_conv_clip_polyline.h>
#include <agg/include/agg_conv_clip_polygon.h>
#include <agg/include/agg_ellipse.h>
#include <agg/include/agg_path_storage.h>
#include <agg/include/agg_pixfmt_rgba.h>
#include <agg/include/agg_rasterizer_scanline_aa.h>
#include <agg/include/agg_renderer_scanline.h>
#include <agg/include/agg_rendering_buffer.h>
#include <agg/include/agg_scanline_u.h>
#include <agg/include/agg_scanline_p.h>
#include <agg/svg/agg_svg_parser.h>
#include <agg/svg/agg_svg_path_renderer.h>
