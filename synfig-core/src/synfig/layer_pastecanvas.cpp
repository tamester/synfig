/* === S Y N F I G ========================================================= */
/*!	\file layer_pastecanvas.cpp
**	\brief Implementation of the "Paste Canvas" layer
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**	Copyright (c) 2011-2013 Carlos López
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "layer_pastecanvas.h"
#include "string.h"
#include "time.h"
#include "context.h"
#include "paramdesc.h"
#include "renddesc.h"
#include "surface.h"
#include "value.h"
#include "valuenode.h"
#include "canvas.h"
#include "cairo_renddesc.h"


#endif

/* === U S I N G =========================================================== */

using namespace etl;
using namespace std;
using namespace synfig;

/* === M A C R O S ========================================================= */

#define MAX_DEPTH 10

// if this isn't defined, the 'dead heads' in examples/pirates.sifz don't render properly
#define SYNFIG_CLIP_PASTECANVAS

//#ifdef __APPLE__
//#undef SYNFIG_CLIP_PASTECANVAS
//#endif

/* === C L A S S E S ======================================================= */

class depth_counter	// Makes our recursive depth counter exception-safe
{
	int *depth;
public:
	depth_counter(int &x):depth(&x) { (*depth)++; }
	~depth_counter() { (*depth)--; }
};

/* === G L O B A L S ======================================================= */

SYNFIG_LAYER_INIT(Layer_PasteCanvas);
SYNFIG_LAYER_SET_NAME(Layer_PasteCanvas,"PasteCanvas"); // todo: use paste_canvas
SYNFIG_LAYER_SET_LOCAL_NAME(Layer_PasteCanvas,N_("Group"));
SYNFIG_LAYER_SET_CATEGORY(Layer_PasteCanvas,N_("Other"));
SYNFIG_LAYER_SET_VERSION(Layer_PasteCanvas,"0.2");
SYNFIG_LAYER_SET_CVS_ID(Layer_PasteCanvas,"$Id$");

/* === M E T H O D S ======================================================= */

Layer_PasteCanvas::Layer_PasteCanvas():
	param_origin(Point()),
	param_transformation(Transformation()),
	param_enable_transformation(ValueBase(true)),
	param_time_offset (Time(0)),
	depth(0),
	extra_reference(false)
{
	param_children_lock=ValueBase(bool(false));
	param_outline_grow=ValueBase(Real(0));
	param_curr_time=ValueBase(Time::begin());

	muck_with_time_=true;

	param_z_range=ValueBase(bool(false));
	param_z_range_position=ValueBase(Real(0.0));
	param_z_range_depth=ValueBase(Real(0.0));
	param_z_range_blur=ValueBase(Real(0.0));

	SET_INTERPOLATION_DEFAULTS();
	SET_STATIC_DEFAULTS();
}

Layer_PasteCanvas::~Layer_PasteCanvas()
{
/*	if(canvas)
		canvas->parent_set.erase(this);
*/

	//if(canvas)DEBUGINFO(strprintf("%d",canvas->count()));

	set_sub_canvas(0);

	//if(canvas && (canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=canvas->get_root()))
	//if(extra_reference)
	//	canvas->unref();
}

String
Layer_PasteCanvas::get_local_name()const
{
	if(!canvas)	return _("Group");
	if(canvas->is_inline()) return _("Group");
	if(canvas->get_root()==get_canvas()->get_root()) return '[' + canvas->get_id() + ']';

	return '[' + canvas->get_file_name() + ']';
}

Layer::Vocab
Layer_PasteCanvas::get_param_vocab()const
{
	Layer::Vocab ret(Layer_Composite::get_param_vocab());

	ret.push_back(ParamDesc("origin")
		.set_local_name(_("Origin"))
		.set_description(_("Position offset"))
	);
	
	ret.push_back(ParamDesc("transformation")
		.set_local_name(_("Transformation"))
		.set_description(_("Position, rotation, skew and scale"))
	);

	ret.push_back(ParamDesc("enable_transformation")
		.set_local_name(_("Enable Transformation"))
		.set_description(_("Enables or disables transformation"))
	);

	ret.push_back(ParamDesc("canvas")
		.set_local_name(_("Canvas"))
		.set_description(_("Group content"))
	);

	ret.push_back(ParamDesc("time_offset")
		.set_local_name(_("Time Offset"))
		.set_description(_("Time Offset to apply to the context"))
	);

	ret.push_back(ParamDesc("children_lock")
		.set_local_name(_("Children Lock"))
		.set_description(_("When checked prevents to select the children using the mouse click"))
		.set_static(true)
	);

	ret.push_back(ParamDesc("outline_grow")
		.set_local_name(_("Outline Grow"))
		.set_description(_("Exponential value to grow children Outline layers width"))
	);
	if(canvas && !(canvas->is_inline()))
	{
		ret.back().hidden();
	}

	ret.push_back(ParamDesc("z_range")
		.set_local_name(_("Z Range"))
		.set_description(_("When checked, only layers inside range are visible"))
		.set_static(true)
	);
	ret.push_back(ParamDesc("z_range_position")
		.set_local_name(_("Z Range Position"))
		.set_description(_("Starting position where layers are visible"))
	);
	ret.push_back(ParamDesc("z_range_depth")
		.set_local_name(_("Z Range Depth"))
		.set_description(_("Depth where layers are visible in range"))
	);
	ret.push_back(ParamDesc("z_range_blur")
		.set_local_name(_("Z Range Blur"))
		.set_description(_("Area where layers inside are partially visible"))
	);

	// optimize_layers() in canvas.cpp makes a new PasteCanvas layer
	// and copies over the parameters of the old layer.  the
	// 'curr_time' member wasn't being copied, so I've added it as a
	// hidden, non critical parameter, and now it will be.  this
	// allows a single exported subcanvas to be used more than once at
	// a time, with different time offets in each.  see bug #1896557.
	ret.push_back(ParamDesc("curr_time")
		.set_local_name(_("Current Time"))
		.not_critical()
		.hidden()
	);

	return ret;
}

bool
Layer_PasteCanvas::set_param(const String & param, const ValueBase &value)
{
	IMPORT_VALUE(param_origin);
	IMPORT_VALUE(param_transformation);
	IMPORT_VALUE(param_enable_transformation);

	// IMPORT(canvas);
	if(param=="canvas" && value.same_type_as(Canvas::Handle()))
	{
		set_sub_canvas(value.get(Canvas::Handle()));
		return true;
	}
	//! \todo this introduces bug 1844764 if enabled; it was introduced in r954.
	// http://synfig.org/images/3/3d/Moving-waypoints.sifz is an
	// example of an animation that has its waypoints displayed
	// incorrectly without this fix; select the outer layer and drag
	// the time slider.  The linear waypoints don't take effect until
	// 5s, but the time slider appears to pass the first one at 3s.
#if 0
	if (param=="time_offset" && value.same_type_as(time_offset))
	{
		if (time_offset != value.get(Time()))
		{
			value.put(&time_offset);
			// notify that the time_offset has changed so we can update the
			// waypoint positions in parent layers
			changed();
		}
		return true;
	}
#else
	IMPORT_VALUE(param_time_offset);
#endif

	IMPORT_VALUE(param_children_lock);
	IMPORT_VALUE(param_outline_grow);
	IMPORT_VALUE(param_curr_time);
	IMPORT_VALUE(param_z_range);
	IMPORT_VALUE(param_z_range_position);
	IMPORT_VALUE(param_z_range_depth);
	IMPORT_VALUE(param_z_range_blur);
	return Layer_Composite::set_param(param,value);
}

void
Layer_PasteCanvas::set_sub_canvas(etl::handle<synfig::Canvas> x)
{
	if(canvas && muck_with_time_)
		remove_child(canvas.get());

	// if(canvas && (canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=canvas->get_root()))
	if (extra_reference)
		canvas->unref();

	child_changed_connection.disconnect();

	if (canvas != x) signal_subcanvas_changed()();

	canvas=x;

	/*if(canvas)
		child_changed_connection=canvas->signal_changed().connect(
			sigc::mem_fun(
				*this,
				&Layer_PasteCanvas::changed
			)
		);
	*/

	if(canvas && muck_with_time_)
		add_child(canvas.get());

	if(canvas && (canvas->is_inline() || !get_canvas() || get_canvas()->get_root()!=canvas->get_root()))
	{
		canvas->ref();
		extra_reference = true;
	}
	else
		extra_reference = false;

	if(canvas)
		on_canvas_set();
}

// when a pastecanvas that contains another pastecanvas is copy/pasted
// from one document to another, only the outermost pastecanvas was
// getting its renddesc set to match that of its new parent.  this
// function is used to recurse through the pastecanvas copying its
// renddesc to any pastecanvases it contains (bug #2116947, svn r2200)
void
Layer_PasteCanvas::update_renddesc()
{
	if(!get_canvas() || !canvas || !canvas->is_inline()) return;

	canvas->rend_desc()=get_canvas()->rend_desc();
	for (IndependentContext iter = canvas->get_independent_context(); !iter->empty(); iter++)
	{
		etl::handle<Layer_PasteCanvas> paste = etl::handle<Layer_PasteCanvas>::cast_dynamic(*iter);
		if (paste) paste->update_renddesc();
	}
}

// This is called whenever the parent canvas gets set/changed
void
Layer_PasteCanvas::on_canvas_set()
{
	if(get_canvas() && canvas && canvas->is_inline() && canvas->parent()!=get_canvas())
	{
		canvas->set_inline(get_canvas());
	}
}

ValueBase
Layer_PasteCanvas::get_param(const String& param)const
{
	EXPORT_VALUE(param_origin);
	EXPORT_VALUE(param_transformation);
	EXPORT_VALUE(param_enable_transformation);
	if (param=="canvas")
	{
		synfig::ValueBase ret(canvas);
		return ret;
	}
	EXPORT_VALUE(param_time_offset);
	EXPORT_VALUE(param_children_lock);
	EXPORT_VALUE(param_curr_time);
	EXPORT_VALUE(param_outline_grow);
	EXPORT_VALUE(param_z_range);
	EXPORT_VALUE(param_z_range_position);
	EXPORT_VALUE(param_z_range_depth);
	EXPORT_VALUE(param_z_range_blur);

	EXPORT_NAME();
	EXPORT_VERSION();

	return Layer_Composite::get_param(param);
}

void
Layer_PasteCanvas::set_time(IndependentContext context, Time time)const
{
	Time time_offset=param_time_offset.get(Time());

	if(depth==MAX_DEPTH)return;depth_counter counter(depth);
	param_curr_time.set(time);

	context.set_time(time);
	if(canvas)
		canvas->set_time(time+time_offset);
}

synfig::Layer::Handle
Layer_PasteCanvas::hit_check(synfig::Context context, const synfig::Point &pos)const
{
	if(depth==MAX_DEPTH)return 0;depth_counter counter(depth);

	Transformation transformation(get_summary_transformation());

	bool children_lock=param_children_lock.get(bool(true));
	ContextParams cp(context.get_params());
	cp.z_range=param_z_range.get(bool());
	cp.z_range_position=param_z_range_position.get(Real());
	cp.z_range_depth=param_z_range_depth.get(Real());
	cp.z_range_blur=param_z_range_blur.get(Real());
	if (canvas) {
		Point target_pos = transformation.back_transform(pos);

		if(canvas && get_amount() && canvas->get_context(cp).get_color(target_pos).get_a()>=0.25)
		{
			if(!children_lock)
			{
				return canvas->get_context(cp).hit_check(target_pos);
			}
			return const_cast<Layer_PasteCanvas*>(this);
		}
	}
	return context.hit_check(pos);
}

Color
Layer_PasteCanvas::get_color(Context context, const Point &pos)const
{
	Transformation transformation(get_summary_transformation());

	ContextParams cp(context.get_params());
	cp.z_range=param_z_range.get(bool());
	cp.z_range_position=param_z_range_position.get(Real());
	cp.z_range_depth=param_z_range_depth.get(Real());
	cp.z_range_blur=param_z_range_blur.get(Real());
	if(!canvas || !get_amount())
		return context.get_color(pos);

	if(depth==MAX_DEPTH)return Color::alpha();depth_counter counter(depth);

	Point target_pos = transformation.back_transform(pos);

	return Color::blend(canvas->get_context(cp).get_color(target_pos),context.get_color(pos),get_amount(),get_blend_method());
}

Rect
Layer_PasteCanvas::get_bounding_rect_context_dependent(const ContextParams &context_params)const
{
	if (canvas)
	{
		ContextParams cp(context_params);
		cp.z_range=param_z_range.get(bool());
		cp.z_range_position=param_z_range_position.get(Real());
		cp.z_range_depth=param_z_range_depth.get(Real());
		cp.z_range_blur=param_z_range_blur.get(Real());

		return get_summary_transformation()
			.transform_bounds(
				canvas->get_context(cp).get_full_bounding_rect() );
	}
	return Rect::zero();
}

Rect
Layer_PasteCanvas::get_full_bounding_rect(Context context)const
{
	if(is_disabled() || Color::is_onto(get_blend_method()))
		return context.get_full_bounding_rect();

	return context.get_full_bounding_rect()|get_bounding_rect_context_dependent(context.get_params());
}

bool
Layer_PasteCanvas::accelerated_render(Context context,Surface *surface,int quality, const RendDesc &renddesc, ProgressCallback *cb)const
{
	Transformation transformation(get_summary_transformation());

	Real outline_grow=param_outline_grow.get(Real());
	Time time_offset=param_time_offset.get(Time());
	Time curr_time=param_curr_time.get(Time());

	if(cb && !cb->amount_complete(0,10000)) return false;

	if(depth==MAX_DEPTH)
		// if we are at the extent of our depth,
		// then we should just return whatever is under us.
		return context.accelerated_render(surface,quality,renddesc,cb);

	depth_counter counter(depth);

	if(!canvas || !get_amount())
		return context.accelerated_render(surface,quality,renddesc,cb);

	SuperCallback stageone(cb,0,4500,10000);
	SuperCallback stagetwo(cb,4500,9000,10000);
	SuperCallback stagethree(cb,9000,9999,10000);

	if (is_solid_color() || context->empty())
	{
		surface->set_wh(renddesc.get_w(),renddesc.get_h());
		surface->clear();
	}
	else if (!context.accelerated_render(surface,quality,renddesc,&stageone))
		return false;

	Real grow_value(get_parent_canvas_grow_value());
	canvas->set_grow_value(outline_grow+grow_value);

	if(muck_with_time_ && curr_time!=Time::begin() /*&& canvas->get_time()!=curr_time+time_offset*/)
		canvas->set_time(curr_time+time_offset);

	Color::BlendMethod blend_method(get_blend_method());
	const Rect full_bounding_rect(canvas->get_context(context).get_full_bounding_rect());

	bool blend_using_straight = false; // use 'straight' just for the central blit

	Rect inner_bounds(
	    full_bounding_rect.get_min(),
	    full_bounding_rect.get_max()
	);
	inner_bounds &= transformation.back_transform_bounds(renddesc.get_rect());
	Rect outer_bounds(transformation.transform_bounds(inner_bounds));
	outer_bounds &= renddesc.get_rect();
	
	// sometimes the user changes the parameters while we're
	// rendering, causing our pasted canvas' bounding box to shrink
	// and no longer overlap with our tile.  if that has happened,
	// let's just stop now - we'll be refreshing soon anyway
	//! \todo shouldn't a mutex ensure this isn't needed?
	// http://synfig.org/images/d/d2/Bbox-change.sifz is an example
	// that shows this happening - open the encapsulation, select the
	// 'shade', and toggle the 'invert' parameter quickly.
	// Occasionally you'll see:
	//   error: Context::accelerated_render(): Layer "shade" threw a bad_alloc exception!
	// where the shade layer tries to allocate itself a canvas of
	// negative proportions, due to changing bounding boxes.
	if (!inner_bounds.is_valid())
	{
		warning("%s:%d bounding box shrank while rendering?", __FILE__, __LINE__);
		return true;
	}

	// we have rendered what's under us, if necessary
	if(context->empty())
	{
		// if there's nothing under us, and we're blending 'onto', then we've finished
		if (Color::is_onto(blend_method)) return true;

		// there's nothing under us, so using straight blending is
		// faster than and equivalent to using composite, but we don't
		// want to blank the surrounding areas
		if (blend_method==Color::BLEND_COMPOSITE) blend_using_straight = true;
	}

	if (!etl::intersect(context.get_full_bounding_rect(),outer_bounds))
	{
		// if there's no intersection between the context and our
		// surface, and we're rendering 'onto', then we're done
		if (Color::is_onto(blend_method) && !Color::is_straight(blend_method))
			return true;

		/* 'straight' is faster than 'composite' and has the same
		 * effect if the affected area of the lower layer is
		 * transparent;  however, if we're not clipping the blit to
		 * just the bounding rectangle, the affected area is the whole
		 * tile, so we can't use this optimisation.  if we are
		 * clipping, then we can use 'straight' to blit the clipped
		 * rectangle, but we shouldn't set blend_method to 'straight',
		 * or the surrounding areas will be blanked, which we don't
		 * want.
		 */
#ifdef SYNFIG_CLIP_PASTECANVAS
		if (blend_method==Color::BLEND_COMPOSITE) blend_using_straight = true;
#endif	// SYNFIG_CLIP_PASTECANVAS
	}

	if (transformation.is_identity())
	{
		RendDesc intermediate_desc(renddesc);
		intermediate_desc.clear_flags();
		intermediate_desc.set_flags(0);
		Surface intermediate_surface;
		if(!canvas->get_context(context).accelerated_render(&intermediate_surface,quality,intermediate_desc,&stagetwo))
			return false;
		Surface::alpha_pen apen(surface->get_pen(0, 0));
		apen.set_alpha(get_amount());
		apen.set_blend_method(blend_using_straight ? Color::BLEND_STRAIGHT : blend_method);
		intermediate_surface.blit_to(apen);
	}
	else
	{
		Vector width_vector(
			transformation.transform(
				Vector(inner_bounds.maxx - inner_bounds.minx, 0.0), false ));
		Vector pixels_width_vector(
			width_vector[0]/renddesc.get_pw(),
			width_vector[1]/renddesc.get_ph() );
		int inner_width_pixels = (int)ceil(pixels_width_vector.mag());

		Vector ortho_axis_x(width_vector.norm());
		Vector ortho_axis_y(-ortho_axis_x.perp());

		Vector height_vector(
			transformation.transform(
				Vector(0.0, inner_bounds.maxy - inner_bounds.miny), false ));
		Vector ortho_height_vector(
			ortho_axis_y * (height_vector*ortho_axis_y) );
		Vector pixels_height_vector(
			ortho_height_vector[0]/renddesc.get_pw(),
			ortho_height_vector[1]/renddesc.get_ph() );
		int inner_height_pixels = (int)ceil(pixels_height_vector.mag());

		// make 8 pixels border for bicubic resampling
		float intermediate_pw = (inner_bounds.maxx-inner_bounds.minx)/(float)inner_width_pixels;
		float intermediate_ph = (inner_bounds.maxy-inner_bounds.miny)/(float)inner_height_pixels;
		inner_bounds.maxx += 8.f*intermediate_pw;
		inner_bounds.minx -= 8.f*intermediate_pw;
		inner_bounds.maxy += 8.f*intermediate_ph;
		inner_bounds.miny -= 8.f*intermediate_ph;
		inner_width_pixels += 16;
		inner_height_pixels += 16;

		RendDesc intermediate_desc(renddesc);
		intermediate_desc.clear_flags();
		intermediate_desc.set_tl(Vector(inner_bounds.minx,inner_bounds.maxy));
		intermediate_desc.set_br(Vector(inner_bounds.maxx,inner_bounds.miny));
		intermediate_desc.set_flags(0);
		intermediate_desc.set_w(inner_width_pixels);
		intermediate_desc.set_h(inner_height_pixels);

		Surface intermediate_surface;
		if(!canvas->get_context(context).accelerated_render(&intermediate_surface,quality,intermediate_desc,&stagetwo))
			return false;

		Rect pixels_outer_bounds(
			Vector((outer_bounds.minx-renddesc.get_tl()[0])/renddesc.get_pw(),
				   (outer_bounds.miny-renddesc.get_tl()[1])/renddesc.get_ph()),
			Vector((outer_bounds.maxx-renddesc.get_tl()[0])/renddesc.get_pw(),
				   (outer_bounds.maxy-renddesc.get_tl()[1])/renddesc.get_ph())
		);

		int left   = (int)floor(pixels_outer_bounds.minx);
		int top    = (int)floor(pixels_outer_bounds.miny);
		int right  = (int)ceil (pixels_outer_bounds.maxx);
		int bottom = (int)ceil (pixels_outer_bounds.maxy);

		int w = min(surface->get_w(), renddesc.get_w());
		int h = min(surface->get_h(), renddesc.get_h());

		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right > w) right = w;
		if (bottom > h) bottom = h;

		int decx = right - left;
		if (top < bottom && left < right) {
			Vector initial_outer_pos(left*renddesc.get_pw(), top*renddesc.get_ph());
			initial_outer_pos += renddesc.get_tl();
			Vector initial_inner_pos = transformation.back_transform(initial_outer_pos);
			Vector initial_inner_surface_pos(initial_inner_pos - intermediate_desc.get_tl());
			initial_inner_surface_pos[0] /= intermediate_desc.get_pw();
			initial_inner_surface_pos[1] /= intermediate_desc.get_ph();

			Vector initial_outer_pos01((left+1)*renddesc.get_pw(), top*renddesc.get_ph());
			initial_outer_pos01 += renddesc.get_tl();
			Vector initial_inner_pos01 = transformation.back_transform(initial_outer_pos01);
			Vector initial_inner_surface_pos01(initial_inner_pos01 - intermediate_desc.get_tl());
			initial_inner_surface_pos01[0] /= intermediate_desc.get_pw();
			initial_inner_surface_pos01[1] /= intermediate_desc.get_ph();

			Vector initial_outer_pos10(left*renddesc.get_pw(), (top+1)*renddesc.get_ph());
			initial_outer_pos10 += renddesc.get_tl();
			Vector initial_inner_pos10 = transformation.back_transform(initial_outer_pos10);
			Vector initial_inner_surface_pos10(initial_inner_pos10 - intermediate_desc.get_tl());
			initial_inner_surface_pos10[0] /= intermediate_desc.get_pw();
			initial_inner_surface_pos10[1] /= intermediate_desc.get_ph();

			Vector dx(initial_inner_surface_pos01 - initial_inner_surface_pos);
			Vector dy(initial_inner_surface_pos10 - initial_inner_surface_pos);

			Vector row_inner_surface_pos(initial_inner_surface_pos);
			Vector inner_surface_pos;

			Surface::alpha_pen apen(surface->get_pen(left, top));
			apen.set_alpha(get_amount());
			apen.set_blend_method(blend_using_straight ? Color::BLEND_STRAIGHT : blend_method);
			for(int y = top; y < bottom; y++) {
				inner_surface_pos = row_inner_surface_pos;
				for(int x = left; x < right; x++) {
					apen.put_value( intermediate_surface.cubic_sample(inner_surface_pos[0], inner_surface_pos[1]) );
					apen.inc_x();
					inner_surface_pos += dx;
				}
				apen.dec_x(decx);
				apen.inc_y();
				row_inner_surface_pos += dy;
			}
		}
	}

	if(cb && !cb->amount_complete(10000,10000)) return false;

	return true;
}

///////

bool
Layer_PasteCanvas::accelerated_cairorender(Context context,cairo_t *cr, int quality, const RendDesc &renddesc, ProgressCallback *cb)const
{
	Transformation transformation(get_summary_transformation());

	Real outline_grow=param_outline_grow.get(Real());
	Time time_offset=param_time_offset.get(Time());
	Time curr_time=param_curr_time.get(Time());

	if(cb && !cb->amount_complete(0,10000)) return false;

	if(depth==MAX_DEPTH)
		// if we are at the extent of our depth,
		// then we should just return whatever is under us.
		return context.accelerated_cairorender(cr,quality,renddesc,cb);

	depth_counter counter(depth);

	if(!canvas || !get_amount())
		return context.accelerated_cairorender(cr,quality,renddesc,cb);

	SuperCallback stageone(cb,0,4500,10000);
	SuperCallback stagetwo(cb,4500,9000,10000);
	SuperCallback stagethree(cb,9000,9999,10000);


	Real grow_value(get_parent_canvas_grow_value());
	canvas->set_grow_value(outline_grow+grow_value);

	if(muck_with_time_ && curr_time!=Time::begin() /*&& canvas->get_time()!=curr_time+time_offset*/)
		canvas->set_time(curr_time+time_offset);

	bool ret;
	RendDesc workdesc(renddesc);

	// Render the background
	ret=context.accelerated_cairorender(cr, quality, renddesc, &stagethree);
	if(!ret)
		return false;


	// render the canvas to be pasted onto pastesurface
	cairo_surface_t* pastesurface=cairo_surface_create_similar_image(cairo_get_target(cr), CAIRO_FORMAT_ARGB32, workdesc.get_w(), workdesc.get_h());
	cairo_t* subcr=cairo_create(pastesurface);
	// apply the transformations form the current context
	cairo_matrix_t matrix;
	cairo_get_matrix(cr, &matrix);

	// apply the transformations form the (paste canvas) group layer
	cairo_set_matrix(subcr, &matrix);

	cairo_matrix_t cairo_transformation_matrix;
	Matrix transformation_matrix(transformation.get_matrix());
	cairo_matrix_init(
		&cairo_transformation_matrix,
		transformation_matrix.m00,
		transformation_matrix.m01,
		transformation_matrix.m10,
		transformation_matrix.m11,
		transformation_matrix.m20,
		transformation_matrix.m21 );

	cairo_transform(subcr, &cairo_transformation_matrix);

	// Effectively render the canvas content
	ret=canvas->get_context(context).accelerated_cairorender(subcr, quality, workdesc, &stagetwo);
	// we are done apply the result to the source
	cairo_destroy(subcr);

	if(!ret)
		return false;
	// Let's paint the result with its alpha
	cairo_save(cr);

	cairo_status_t status;
	status=cairo_matrix_invert(&matrix);
	if(status) // doh! the matrix can't be inverted!
	{
		synfig::error("Can't invert current Cairo matrix!");
		return false;
	}
	// apply the inverse of the transformation of the current context to
	// compensate the pending transformations form cr to be applied.
	cairo_transform(cr, &matrix);
	cairo_set_source_surface(cr, pastesurface, 0, 0);
	cairo_paint_with_alpha_operator(cr, get_amount(), get_blend_method());

	cairo_restore(cr);
	cairo_surface_destroy(pastesurface);

	if(cb && !cb->amount_complete(10000,10000)) return false;

	return true;
}
///////


void Layer_PasteCanvas::get_times_vfunc(Node::time_set &set) const
{
	Time time_offset=param_time_offset.get(Time());

	Node::time_set tset;
	if(canvas) tset = canvas->get_times();

	Node::time_set::iterator i = tset.begin(), end = tset.end();

	//Make sure we offset the time...
	//! \todo: SOMETHING STILL HAS TO BE DONE WITH THE OTHER DIRECTION
	//		   (recursing down the tree needs to take this into account too...)
	for(; i != end; ++i)
		set.insert(*i
#ifdef ADJUST_WAYPOINTS_FOR_TIME_OFFSET // see node.h
				   - time_offset
#endif
			);

	Layer::get_times_vfunc(set);
}


void
Layer_PasteCanvas::set_render_method(Context context, RenderMethod x)
{
	if(canvas) // if there is a canvas pass down to it
		canvas->get_context(context).set_render_method(x);

	// in any case pass it down
	context.set_render_method(x);
}

