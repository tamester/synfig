/* === S Y N F I G ========================================================= */
/*!	\file workarea.cpp
**	\brief Template Header
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2006 Yue Shi Lai
**	Copyright (c) 2007, 2008 Chris Moore
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

#include <sigc++/adaptors/hide.h>

#include "workarea.h"
#include "canvasview.h"
#include "app.h"
#include <gtkmm/window.h>
#include <gtkmm/image.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/ruler.h>
#include <gtkmm/arrow.h>
#include <gtkmm/image.h>
#include <gtkmm/scrollbar.h>
#include <cmath>
#include <sigc++/retype_return.h>
#include <sigc++/retype.h>
#include <sigc++/hide.h>
#include <ETL/misc>

#include <synfig/target_scanline.h>
#include <synfig/target_tile.h>
#include <synfig/surface.h>
#include <synfig/valuenode_composite.h>
#include <synfigapp/canvasinterface.h>
#include "event_mouse.h"
#include "event_layerclick.h"
#include "widget_color.h"
#include <synfig/distance.h>
#include "workarearenderer.h"

#include "renderer_canvas.h"
#include "renderer_grid.h"
#include "renderer_guides.h"
#include "renderer_timecode.h"
#include "renderer_ducks.h"
#include "renderer_dragbox.h"
#include "renderer_bbox.h"
#include "asyncrenderer.h"
#include <gtkmm/frame.h>

#include <synfig/mutex.h>

#include "general.h"

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;
using namespace studio;

/* === M A C R O S ========================================================= */

#ifndef stratof
#define stratof(X) (atof((X).c_str()))
#define stratoi(X) (atoi((X).c_str()))
#endif


/* === G L O B A L S ======================================================= */

/* === C L A S S E S ======================================================= */

class studio::WorkAreaTarget : public synfig::Target_Tile
{
public:
	WorkArea *workarea;
	bool low_res;
	int w,h;
	int real_tile_w,real_tile_h;
	//std::vector<Glib::RefPtr<Gdk::Pixbuf> >::iterator tile_iter;

	int twindow_start, twindow_width, twindow_height, twindow_pad;
	int refresh_id;

	bool onionskin;
	bool onion_first_tile;
	int onion_layers;

	std::list<synfig::Time> onion_skin_queue;

	synfig::Mutex mutex;

	void set_onion_skin(bool x, int *onions)
	{
		onionskin=x;

		Time time(rend_desc().get_time_start());

		if(!onionskin)
			return;
		onion_skin_queue.push_back(time);

		try
		{
		Time thistime=time;
		for(int i=0; i<onions[0]; i++)
			{
				Time keytime=get_canvas()->keyframe_list().find_prev(thistime)->get_time();
				onion_skin_queue.push_back(keytime);
				thistime=keytime;
			}
		}
		catch(...)
		{  }

		try
		{
		Time thistime=time;
		for(int i=0; i<onions[1]; i++)
			{
				Time keytime=get_canvas()->keyframe_list().find_next(thistime)->get_time();
				onion_skin_queue.push_back(keytime);
				thistime=keytime;
			}
		}
		catch(...)
		{  }

		onion_layers=onion_skin_queue.size();

		onion_first_tile=false;
	}
public:

	WorkAreaTarget(WorkArea *workarea,int w, int h):
		workarea(workarea),
		low_res(workarea->get_low_resolution_flag()),
		w(w),
		h(h),
		real_tile_w(workarea->tile_w),
		real_tile_h(workarea->tile_h),
		refresh_id(workarea->refreshes),
		onionskin(false),
		onion_layers(0)
	{
		//set_remove_alpha();
		//set_avoid_time_sync();
		set_clipping(true);
		if(low_res)
		{
			int div = workarea->get_low_res_pixel_size();
			set_tile_w(workarea->tile_w/div);
			set_tile_h(workarea->tile_h/div);
		}
		else
		{
			set_tile_w(workarea->tile_w);
			set_tile_h(workarea->tile_h);
		}
		set_canvas(workarea->get_canvas());
		set_quality(workarea->get_quality());
	}

	~WorkAreaTarget()
	{
		workarea->queue_draw();
	}

	virtual bool set_rend_desc(synfig::RendDesc *newdesc)
	{
		assert(workarea);
		newdesc->set_flags(RendDesc::PX_ASPECT|RendDesc::IM_SPAN);
		if(low_res) {
			int div = workarea->get_low_res_pixel_size();
			newdesc->set_wh(w/div,h/div);
		}
		else
			newdesc->set_wh(w,h);

		if(
			 	workarea->get_w()!=w
			|| 	workarea->get_h()!=h
		) workarea->set_wh(w,h,4);

		workarea->full_frame=false;

		desc=*newdesc;
		return true;
	}

	virtual int total_tiles()const
	{
		int tw(rend_desc().get_w()/get_tile_w());
		int th(rend_desc().get_h()/get_tile_h());
		if(rend_desc().get_w()%get_tile_w()!=0)tw++;
		if(rend_desc().get_h()%get_tile_h()!=0)th++;
		return tw*th;
	}

	virtual int next_frame(Time& time)
	{
		synfig::Mutex::Lock lock(mutex);

		if(!onionskin)
			return synfig::Target_Tile::next_frame(time);

		onion_first_tile=(onion_layers==(signed)onion_skin_queue.size());

		if(!onion_skin_queue.empty())
		{
			time=onion_skin_queue.front();
			onion_skin_queue.pop_front();
		}
		else
			return 0;

		return onion_skin_queue.size()+1;
	}

	virtual int next_tile(int& x, int& y)
	{
		synfig::Mutex::Lock lock(mutex);
		//if(workarea->tile_queue.empty()) return 0;

		//int curr_tile(workarea->tile_queue.front());
		//workarea->tile_queue.pop_front();
		int curr_tile(workarea->next_unrendered_tile(refresh_id-onion_skin_queue.size()));
		if(curr_tile<0)
			return 0;

		// Width of the image(in tiles)
		int tw(rend_desc().get_w()/get_tile_w());
		if(rend_desc().get_w()%get_tile_w()!=0)tw++;

		y=(curr_tile/tw)*get_tile_w();
		x=(curr_tile%tw)*get_tile_h();

		// Mark this tile as "up-to-date"
		if(onionskin)
			workarea->tile_book[curr_tile].second=refresh_id-onion_skin_queue.size();
		else
			workarea->tile_book[curr_tile].second=refresh_id;

		return total_tiles()-curr_tile+1;
	}


	virtual bool start_frame(synfig::ProgressCallback */*cb*/)
	{
		synfig::Mutex::Lock lock(mutex);

		int tw(rend_desc().get_w()/get_tile_w());
		if(rend_desc().get_w()%get_tile_w()!=0)tw++;
		int th(rend_desc().get_h()/get_tile_h());
		if(rend_desc().get_h()%get_tile_h()!=0)th++;

		twindow_start=0;
		twindow_width=tw;
		twindow_height=th;
		twindow_pad=0;

		workarea->tile_book.resize(total_tiles());
		//tile_iter=workarea->tile_book.begin()+twindow_start;
		return true;
	}

	static void free_buff(const guint8 *x) { free(const_cast<guint8*>(x)); }

	virtual bool add_tile(const synfig::Surface &surface, int x, int y)
	{
		synfig::Mutex::Lock lock(mutex);
		assert(surface);

		PixelFormat pf(PF_RGB);

		const int total_bytes(get_tile_w()*get_tile_h()*synfig::channels(pf));

		unsigned char *buffer((unsigned char*)malloc(total_bytes));

		if(!surface || !buffer)
			return false;
		{
			unsigned char *dest(buffer);
			const Color *src(surface[0]);
			int w(get_tile_w());
			int h(get_tile_h());
			int x(surface.get_w()*surface.get_h());
			//if(low_res) {
			//	int div = workarea->get_low_res_pixel_size();
			//	w/=div,h/=div;
			//}
			Color dark(0.6,0.6,0.6);
			Color lite(0.8,0.8,0.8);
			for(int i=0;i<x;i++)
				dest=Color2PixelFormat(
					Color::blend(
						(*(src++)),
						((i/surface.get_w())*8/h+(i%surface.get_w())*8/w)&1?dark:lite,
						1.0f
					).clamped(),
					pf,dest,App::gamma
				);
		}

		x/=get_tile_w();
		y/=get_tile_h();
		int tw(rend_desc().get_w()/get_tile_w());
		if(rend_desc().get_w()%get_tile_w()!=0)tw++;
		unsigned int index=y*tw+x;

		// Sanity check
		if(index>workarea->tile_book.size())
			return false;

		Glib::RefPtr<Gdk::Pixbuf> pixbuf;

		pixbuf=Gdk::Pixbuf::create_from_data(
			buffer,	// pointer to the data
			Gdk::COLORSPACE_RGB, // the colorspace
			((pf&PF_A)==PF_A), // has alpha?
			8, // bits per sample
			surface.get_w(),	// width
			surface.get_h(),	// height
			surface.get_w()*synfig::channels(pf), // stride (pitch)
			sigc::ptr_fun(&WorkAreaTarget::free_buff)
		);

		if(low_res)
		{
			// We need to scale up
			int div = workarea->get_low_res_pixel_size();
			pixbuf=pixbuf->scale_simple(
				surface.get_w()*div,
				surface.get_h()*div,
				Gdk::INTERP_NEAREST
			);
		}

		if(!onionskin || onion_first_tile || !workarea->tile_book[index].first)
		{
			workarea->tile_book[index].first=pixbuf;
		}
		else
		{
			pixbuf->composite(
				workarea->tile_book[index].first, // Dest
				0,//int dest_x
				0,//int dest_y
				pixbuf->get_width(), // dest width
				pixbuf->get_height(), // dest_height,
				0, // double offset_x
				0, // double offset_y
				1, // double scale_x
				1, // double scale_y
				Gdk::INTERP_NEAREST, // interp
				255/(onion_layers-onion_skin_queue.size()+1) //int overall_alpha
			);
		}

		//if(index%2)
			workarea->queue_draw();
		assert(workarea->tile_book[index].first);
		return true;
	}

	virtual void end_frame()
	{
		//workarea->queue_draw();
	}
};


class studio::WorkAreaTarget_Full : public synfig::Target_Scanline
{
public:
	WorkArea *workarea;
	bool low_res;
	int w,h;
	int real_tile_w,real_tile_h;
	//std::vector<Glib::RefPtr<Gdk::Pixbuf> >::iterator tile_iter;

	int twindow_start, twindow_width, twindow_height, twindow_pad;
	int refresh_id;

	bool onionskin;
	bool onion_first_tile;
	int onion_layers;

	Surface surface;

	std::list<synfig::Time> onion_skin_queue;

	void set_onion_skin(bool x, int *onions)
	{
		onionskin=x;

		Time time(rend_desc().get_time_start());

		if(!onionskin)
			return;
		onion_skin_queue.push_back(time);
		//onion_skin_queue.push_back(time-1);
		//onion_skin_queue.push_back(time+1);
		try
		{
		Time thistime=time;
		for(int i=0; i<onions[0]; i++)
			{
				Time keytime=get_canvas()->keyframe_list().find_prev(thistime)->get_time();
				onion_skin_queue.push_back(keytime);
				thistime=keytime;
			}
		}
		catch(...)
		{  }

		try
		{
		Time thistime=time;
		for(int i=0; i<onions[1]; i++)
			{
				Time keytime=get_canvas()->keyframe_list().find_next(thistime)->get_time();
				onion_skin_queue.push_back(keytime);
				thistime=keytime;
			}
		}
		catch(...)
		{  }

		onion_layers=onion_skin_queue.size();

		onion_first_tile=false;
	}
public:

	WorkAreaTarget_Full(WorkArea *workarea,int w, int h):
		workarea(workarea),
		low_res(workarea->get_low_resolution_flag()),
		w(w),
		h(h),
		refresh_id(workarea->refreshes),
		onionskin(false),
		onion_layers(0)
	{
		set_canvas(workarea->get_canvas());
		set_quality(workarea->get_quality());
	}

	~WorkAreaTarget_Full()
	{
	}

	virtual bool set_rend_desc(synfig::RendDesc *newdesc)
	{
		assert(workarea);
		newdesc->set_flags(RendDesc::PX_ASPECT|RendDesc::IM_SPAN);
		if(low_res)
		{
			int div = workarea->get_low_res_pixel_size();
			newdesc->set_wh(w/div,h/div);
		}
		else
			newdesc->set_wh(w,h);

		if(
			 	workarea->get_w()!=w
			|| 	workarea->get_h()!=h
		) workarea->set_wh(w,h,4);

		surface.set_wh(newdesc->get_w(),newdesc->get_h());

		desc=*newdesc;
		workarea->full_frame=true;
		workarea->tile_book.resize(1);
		return true;
	}

	virtual int next_frame(Time& time)
	{
		// Mark this tile as "up-to-date"
		if(onionskin)
			workarea->tile_book[0].second=refresh_id-onion_skin_queue.size();
		else
			workarea->tile_book[0].second=refresh_id;

		if(!onionskin)
			return synfig::Target_Scanline::next_frame(time);

		onion_first_tile=(onion_layers==(signed)onion_skin_queue.size());

		if(!onion_skin_queue.empty())
		{
			time=onion_skin_queue.front();
			onion_skin_queue.pop_front();
		}
		else
			return 0;
		return onion_skin_queue.size()+1;
	}


	virtual bool start_frame(synfig::ProgressCallback */*cb*/)
	{
		return true;
	}

	virtual Color * start_scanline(int scanline)
	{
		return surface[scanline];
	}

	virtual bool end_scanline()
	{
		return true;
	}

	static void free_buff(const guint8 *x) { free(const_cast<guint8*>(x)); }

	virtual void end_frame()
	{
		assert(surface);

		PixelFormat pf(PF_RGB);

		const int total_bytes(surface.get_w()*surface.get_h()*synfig::channels(pf));

		unsigned char *buffer((unsigned char*)malloc(total_bytes));

		if(!surface || !buffer)
			return;
		{
			unsigned char *dest(buffer);
			const Color *src(surface[0]);
			int w(surface.get_w());
			//int h(surface.get_h());
			int x(surface.get_w()*surface.get_h());
			//if(low_res) {
			//	int div = workarea->get_low_res_pixel_size();
			//	w/=div,h/=div;
			//}
			Color dark(0.6,0.6,0.6);
			Color lite(0.8,0.8,0.8);
			int tw=workarea->tile_w;
			int th=workarea->tile_h;
			if(low_res)
			{
				int div = workarea->get_low_res_pixel_size();
				tw/=div;
				th/=div;
			}
			for(int i=0;i<x;i++)
				dest=Color2PixelFormat(
					Color::blend(
						(*(src++)),
						((i/w)*8/th+(i%w)*8/tw)&1?dark:lite,
						1.0f
					).clamped(),
					pf,
					dest,
					App::gamma
				);
		}

		Glib::RefPtr<Gdk::Pixbuf> pixbuf;

		pixbuf=Gdk::Pixbuf::create_from_data(
			buffer,	// pointer to the data
			Gdk::COLORSPACE_RGB, // the colorspace
			((pf&PF_A)==PF_A), // has alpha?
			8, // bits per sample
			surface.get_w(),	// width
			surface.get_h(),	// height
			surface.get_w()*synfig::channels(pf), // stride (pitch)
			sigc::ptr_fun(&WorkAreaTarget::free_buff)
		);

		if(low_res)
		{
			// We need to scale up
			int div = workarea->get_low_res_pixel_size();
			pixbuf=pixbuf->scale_simple(
				surface.get_w()*div,
				surface.get_h()*div,
				Gdk::INTERP_NEAREST
			);
		}

		int index=0;

		if(!onionskin || onion_first_tile || !workarea->tile_book[index].first)
		{
			workarea->tile_book[index].first=pixbuf;
		}
		else
		{
			pixbuf->composite(
				workarea->tile_book[index].first, // Dest
				0,//int dest_x
				0,//int dest_y
				pixbuf->get_width(), // dest width
				pixbuf->get_height(), // dest_height,
				0, // double offset_x
				0, // double offset_y
				1, // double scale_x
				1, // double scale_y
				Gdk::INTERP_NEAREST, // interp
				255/(onion_layers-onion_skin_queue.size()+1) //int overall_alpha
			);
		}

		workarea->queue_draw();
		assert(workarea->tile_book[index].first);
	}
};




/* === M E T H O D S ======================================================= */


WorkArea::WorkArea(etl::loose_handle<synfigapp::CanvasInterface> canvas_interface):
	Gtk::Table(3, 3, false), /* 3 columns by 3 rows*/
	canvas_interface(canvas_interface),
	canvas(canvas_interface->get_canvas()),
	scrollx_adjustment(0,-4,4,0.01,0.1),
	scrolly_adjustment(0,-4,4,0.01,0.1),
	w(TILE_SIZE),
	h(TILE_SIZE),
	last_event_time(0),
	progresscallback(0),
	dragging(DRAG_NONE),
	show_grid(false),
	tile_w(TILE_SIZE),
	tile_h(TILE_SIZE),
	timecode_width(0),
	timecode_height(0)
{
	show_guides=true;
	curr_input_device=0;
	full_frame=false;
	allow_duck_clicks=true;
	allow_layer_clicks=true;
	render_idle_func_id=0;
	zoom=prev_zoom=1.0;
	quality=10;
	low_res_pixel_size=2;
	rendering=false;
	canceled_=false;
	low_resolution=true;
	pw=0.001;
	ph=0.001;
	last_focus_point=Point(0,0);
	onion_skin=false;
	onion_skins[0]=0;
	onion_skins[1]=0;
	queued=false;
	dirty_trap_enabled=false;
	solid_lines=true;

	dirty_trap_queued=0;

	meta_data_lock=false;

	insert_renderer(new Renderer_Canvas,	000);
	insert_renderer(new Renderer_Grid,		100);
	insert_renderer(new Renderer_Guides,	200);
	insert_renderer(new Renderer_Ducks,		300);
	insert_renderer(new Renderer_BBox,		399);
	insert_renderer(new Renderer_Dragbox,	400);
	insert_renderer(new Renderer_Timecode,	500);

	signal_duck_selection_changed().connect(sigc::mem_fun(*this,&studio::WorkArea::queue_draw));
	signal_strokes_changed().connect(sigc::mem_fun(*this,&studio::WorkArea::queue_draw));
	signal_grid_changed().connect(sigc::mem_fun(*this,&studio::WorkArea::queue_draw));
	signal_grid_changed().connect(sigc::mem_fun(*this,&studio::WorkArea::save_meta_data));
	signal_sketch_saved().connect(sigc::mem_fun(*this,&studio::WorkArea::save_meta_data));

	// Not that it really makes a difference... (setting this to zero, that is)
	refreshes=0;

  	drawing_area=manage(new class Gtk::DrawingArea());
	drawing_area->show();
	drawing_area->set_extension_events(Gdk::EXTENSION_EVENTS_ALL);

	drawing_frame=manage(new Gtk::Frame);
	drawing_frame->add(*drawing_area);
	//drawing_frame->set_shadow_type(Gtk::SHADOW_NONE);
	//drawing_frame->property_border_width()=5;
	//drawing_frame->modify_fg(Gtk::STATE_NORMAL,Gdk::Color("#00ffff"));
	//drawing_frame->modify_base(Gtk::STATE_NORMAL,Gdk::Color("#ff00ff"));
	/*drawing_frame->modify_fg(Gtk::STATE_ACTIVE,Gdk::Color("#00ffff"));
	drawing_frame->modify_base(Gtk::STATE_ACTIVE,Gdk::Color("#ff00ff"));
	drawing_frame->modify_bg(Gtk::STATE_ACTIVE,Gdk::Color("#00ff00"));
	drawing_frame->modify_fg(Gtk::STATE_INSENSITIVE,Gdk::Color("#00ffff"));
	drawing_frame->modify_base(Gtk::STATE_INSENSITIVE,Gdk::Color("#ff00ff"));
	drawing_frame->modify_bg(Gtk::STATE_INSENSITIVE,Gdk::Color("#00ff00"));
	drawing_frame->modify_fg(Gtk::STATE_SELECTED,Gdk::Color("#00ffff"));
	drawing_frame->modify_base(Gtk::STATE_SELECTED,Gdk::Color("#ff00ff"));
	drawing_frame->modify_bg(Gtk::STATE_SELECTED,Gdk::Color("#00ff00"));
	*/
	//drawing_frame->set_state(Gtk::STATE_NORMAL);

	drawing_frame->show();

	attach(*drawing_frame, 1, 2, 1, 2, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0, 0);

	Gtk::IconSize iconsize=Gtk::IconSize::from_name("synfig-small_icon");


	// Create the vertical and horizontal rulers
	vruler = manage(new class Gtk::VRuler());
	hruler = manage(new class Gtk::HRuler());
	vruler->set_metric(Gtk::PIXELS);
	hruler->set_metric(Gtk::PIXELS);
	Pango::FontDescription fd(hruler->get_style()->get_font());
	fd.set_size(Pango::SCALE*8);
	vruler->modify_font(fd);
	hruler->modify_font(fd);
	vruler->show();
	hruler->show();
	attach(*vruler, 0, 1, 1, 2, Gtk::SHRINK|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0, 0);
	attach(*hruler, 1, 2, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK|Gtk::FILL, 0, 0);
	hruler->signal_event().connect(sigc::mem_fun(*this, &WorkArea::on_hruler_event));
	vruler->signal_event().connect(sigc::mem_fun(*this, &WorkArea::on_vruler_event));
	hruler->add_events(Gdk::BUTTON1_MOTION_MASK | Gdk::BUTTON2_MOTION_MASK |Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK);
	vruler->add_events(Gdk::BUTTON1_MOTION_MASK | Gdk::BUTTON2_MOTION_MASK |Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK);

	// Create the menu button
	menubutton=manage(new class Gtk::Button());
	Gtk::Arrow *arrow1 = manage(new class Gtk::Arrow(Gtk::ARROW_RIGHT, Gtk::SHADOW_OUT));
	arrow1->set_size_request(10,10);
	menubutton->add(*arrow1);
	menubutton->show_all();
	menubutton->signal_pressed().connect(sigc::mem_fun(*this, &WorkArea::popup_menu));
	attach(*menubutton, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 0, 0);

	Gtk::HBox *hbox = manage(new class Gtk::HBox(false, 0));

	Gtk::VScrollbar *vscrollbar1 = manage(new class Gtk::VScrollbar(*get_scrolly_adjustment()));
	Gtk::HScrollbar *hscrollbar1 = manage(new class Gtk::HScrollbar(*get_scrollx_adjustment()));
	vscrollbar1->show();
	attach(*vscrollbar1, 2, 3, 1, 2, Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0, 0);

	ZoomDial *zoomdial=manage(new class ZoomDial(iconsize));
	zoomdial->signal_zoom_in().connect(sigc::mem_fun(*this, &studio::WorkArea::zoom_in));
	zoomdial->signal_zoom_out().connect(sigc::mem_fun(*this, &studio::WorkArea::zoom_out));
	zoomdial->signal_zoom_fit().connect(sigc::mem_fun(*this, &studio::WorkArea::zoom_fit));
	zoomdial->signal_zoom_norm().connect(sigc::mem_fun(*this, &studio::WorkArea::zoom_norm));

	hbox->pack_end(*hscrollbar1, Gtk::PACK_EXPAND_WIDGET,0);
	hscrollbar1->show();
	hbox->pack_start(*zoomdial, Gtk::PACK_SHRINK,0);
	zoomdial->show();

	attach(*hbox, 0, 2, 2, 3, Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK|Gtk::FILL, 0, 0);
	hbox->show();

	drawing_area->add_events(Gdk::KEY_PRESS_MASK | Gdk::KEY_RELEASE_MASK);
	add_events(Gdk::KEY_PRESS_MASK);
	drawing_area->add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK);
	drawing_area->add_events(Gdk::BUTTON1_MOTION_MASK | Gdk::BUTTON2_MOTION_MASK |Gdk::POINTER_MOTION_MASK);

	// ----------------- Attach signals

	drawing_area->signal_expose_event().connect(sigc::mem_fun(*this, &WorkArea::refresh));
	drawing_area->signal_event().connect(sigc::mem_fun(*this, &WorkArea::on_drawing_area_event));
	drawing_area->signal_size_allocate().connect(sigc::hide(sigc::mem_fun(*this, &WorkArea::refresh_dimension_info)));



	canvas_interface->signal_rend_desc_changed().connect(sigc::mem_fun(*this, &WorkArea::refresh_dimension_info));
	// When either of the scrolling adjustments change, then redraw.
	get_scrollx_adjustment()->signal_value_changed().connect(sigc::mem_fun(*this, &WorkArea::queue_scroll));
	get_scrolly_adjustment()->signal_value_changed().connect(sigc::mem_fun(*this, &WorkArea::queue_scroll));
	get_scrollx_adjustment()->signal_value_changed().connect(sigc::mem_fun(*this, &WorkArea::refresh_dimension_info));
	get_scrolly_adjustment()->signal_value_changed().connect(sigc::mem_fun(*this, &WorkArea::refresh_dimension_info));

	get_canvas()->signal_meta_data_changed("grid_size").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("grid_snap").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("grid_show").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("guide_show").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("guide_x").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("guide_y").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("onion_skin").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("guide_snap").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("sketch").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));
	get_canvas()->signal_meta_data_changed("solid_lines").connect(sigc::mem_fun(*this,&WorkArea::load_meta_data));

	queued=false;
	meta_data_lock=false;
	set_focus_point(Point(0,0));


	load_meta_data();
	// Load sketch
	{
		String data(canvas->get_meta_data("sketch"));
		if(!data.empty())
		{
			if(!load_sketch(data))
				load_sketch(dirname(canvas->get_file_name())+ETL_DIRECTORY_SEPARATOR+basename(data));
		}
	}

	hruler->property_max_size()=double(10.0);
	vruler->property_max_size()=double(10.0);

	drawing_area->set_flags(drawing_area->get_flags()|Gtk::CAN_FOCUS);
}

WorkArea::~WorkArea()
{
//	delete [] buffer;

	// don't leave the render function queued if we are about to vanish;
	// that causes crashes
	if(render_idle_func_id)
		render_idle_func_id=0;
}

#ifdef SINGLE_THREADED
bool
WorkArea::get_updating()const
{
	return App::single_threaded && async_renderer && async_renderer->updating;
}
#endif

#ifdef SINGLE_THREADED
void
WorkArea::stop_updating(bool cancel)
{
	async_renderer->stop();
	if (cancel) canceled_=true;
}
#endif

void
WorkArea::save_meta_data()
{
	if(meta_data_lock)
		return;
	meta_data_lock=true;

	Vector s(get_grid_size());
	canvas_interface->set_meta_data("grid_size",strprintf("%f %f",s[0],s[1]));
	canvas_interface->set_meta_data("grid_snap",get_grid_snap()?"1":"0");
	canvas_interface->set_meta_data("guide_snap",get_guide_snap()?"1":"0");
	canvas_interface->set_meta_data("guide_show",get_show_guides()?"1":"0");
	canvas_interface->set_meta_data("grid_show",show_grid?"1":"0");
	canvas_interface->set_meta_data("onion_skin",onion_skin?"1":"0");
	{
		String data;
		GuideList::const_iterator iter;
		for(iter=get_guide_list_x().begin();iter!=get_guide_list_x().end();++iter)
		{
			if(!data.empty())
				data+=' ';
			data+=strprintf("%f",*iter);
		}
		if(!data.empty())
			canvas_interface->set_meta_data("guide_x",data);

		data.clear();
		for(iter=get_guide_list_y().begin();iter!=get_guide_list_y().end();++iter)
		{
			if(!data.empty())
				data+=' ';
			data+=strprintf("%f",*iter);
		}
		if(!data.empty())
			canvas_interface->set_meta_data("guide_y",data);
	}

	if(get_sketch_filename().size())
	{
		if(dirname(canvas->get_file_name())==dirname(get_sketch_filename()))
			canvas_interface->set_meta_data("sketch",basename(get_sketch_filename()));
		else
			canvas_interface->set_meta_data("sketch",get_sketch_filename());
	}

	meta_data_lock=false;
}

void
WorkArea::load_meta_data()
{
	if(meta_data_lock)
		return;
	meta_data_lock=true;

	String data;

	data=canvas->get_meta_data("grid_size");
	if(!data.empty())
	{
		float gx(get_grid_size()[0]),gy(get_grid_size()[1]);

		String::iterator iter(find(data.begin(),data.end(),' '));
		String tmp(data.begin(),iter);

		if(!tmp.empty())
			gx=stratof(tmp);
		else
			synfig::error("WorkArea::load_meta_data(): Unable to parse data for \"grid_size\", which was \"%s\"",data.c_str());

		if(iter==data.end())
			tmp.clear();
		else
			tmp=String(iter+1,data.end());

		if(!tmp.empty())
			gy=stratof(tmp);
		else
			synfig::error("WorkArea::load_meta_data(): Unable to parse data for \"grid_size\", which was \"%s\"",data.c_str());

		set_grid_size(Vector(gx,gy));
	}

	data=canvas->get_meta_data("grid_show");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		show_grid=true;
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		show_grid=false;

	data=canvas->get_meta_data("solid_lines");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		solid_lines=true;
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		solid_lines=false;

	data=canvas->get_meta_data("guide_show");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		show_guides=true;
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		show_guides=false;

	data=canvas->get_meta_data("grid_snap");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		set_grid_snap(true);
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		set_grid_snap(false);

	data=canvas->get_meta_data("guide_snap");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		set_guide_snap(true);
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		set_guide_snap(false);

	data=canvas->get_meta_data("onion_skin");
	if(data.size() && (data=="1" || data[0]=='t' || data[0]=='T'))
		set_onion_skin(true);
	if(data.size() && (data=="0" || data[0]=='f' || data[0]=='F'))
		set_onion_skin(false);

	data=canvas->get_meta_data("guide_x");
	get_guide_list_x().clear();
	while(!data.empty())
	{
		String::iterator iter(find(data.begin(),data.end(),' '));
		String guide(data.begin(),iter);

		if(!guide.empty())
			get_guide_list_x().push_back(stratof(guide));

		if(iter==data.end())
			data.clear();
		else
			data=String(iter+1,data.end());
	}
	//sort(get_guide_list_x());

	data=canvas->get_meta_data("guide_y");
	get_guide_list_y().clear();
	while(!data.empty())
	{
		String::iterator iter(find(data.begin(),data.end(),' '));
		String guide(data.begin(),iter);

		if(!guide.empty())
			get_guide_list_y().push_back(stratof(guide));

		if(iter==data.end())
			data.clear();
		else
			data=String(iter+1,data.end());
	}
	//sort(get_guide_list_y());

	meta_data_lock=false;
	queue_draw();
}

void
WorkArea::set_onion_skin(bool x)
{
	if(onion_skin==x)
		return;
	onion_skin=x;
	save_meta_data();
	queue_render_preview();
	signal_onion_skin_changed()();
}

bool
WorkArea::get_onion_skin()const
{
	return onion_skin;
}

void WorkArea::set_onion_skins(int *onions)
{
	onion_skins[0]=onions[0];
	onion_skins[1]=onions[1];
	if(onion_skin)
		queue_render_preview();
}

void
WorkArea::enable_grid()
{
	show_grid=true;
	save_meta_data();
	queue_draw();
}

void
WorkArea::disable_grid()
{
	show_grid=false;
	save_meta_data();
	queue_draw();
}

void
WorkArea::set_show_guides(bool x)
{
	show_guides=x;
	save_meta_data();
	queue_draw();
}

void
WorkArea::toggle_grid()
{
	show_grid=!show_grid;
	save_meta_data();
	queue_draw();
}

void
WorkArea::set_low_resolution_flag(bool x)
{
	if(x!=low_resolution)
	{
		low_resolution=x;
		queue_render_preview();
	}
}

void
WorkArea::toggle_low_resolution_flag()
{
	set_low_resolution_flag(!get_low_resolution_flag());
}

void
WorkArea::popup_menu()
{
	signal_popup_menu()();
}

void
WorkArea::set_grid_size(const synfig::Vector &s)
{
	Duckmatic::set_grid_size(s);
	save_meta_data();
	queue_draw();
}

void
WorkArea::set_focus_point(const synfig::Point &point)
{
	// These next three lines try to ensure that we place the
	// focus on a pixel boundary
	/*Point adjusted(point[0]/abs(get_pw()),point[1]/abs(get_ph()));
	adjusted[0]=(abs(adjusted[0]-floor(adjusted[0]))<0.5)?floor(adjusted[0])*abs(get_pw()):ceil(adjusted[0])*abs(get_ph());
	adjusted[1]=(abs(adjusted[1]-floor(adjusted[1]))<0.5)?floor(adjusted[1])*abs(get_ph()):ceil(adjusted[1])*abs(get_ph());
	*/
	const synfig::Point& adjusted(point);

	synfig::RendDesc &rend_desc(get_canvas()->rend_desc());
	Real x_factor=(rend_desc.get_br()[0]-rend_desc.get_tl()[0]>0)?-1:1;
	Real y_factor=(rend_desc.get_br()[1]-rend_desc.get_tl()[1]>0)?-1:1;

	get_scrollx_adjustment()->set_value(adjusted[0]*x_factor);
	get_scrolly_adjustment()->set_value(adjusted[1]*y_factor);
}

synfig::Point
WorkArea::get_focus_point()const
{
	synfig::RendDesc &rend_desc(get_canvas()->rend_desc());
	Real x_factor=(rend_desc.get_br()[0]-rend_desc.get_tl()[0]>0)?-1:1;
	Real y_factor=(rend_desc.get_br()[1]-rend_desc.get_tl()[1]>0)?-1:1;

	return synfig::Point(get_scrollx_adjustment()->get_value()*x_factor, get_scrolly_adjustment()->get_value()*y_factor);
}

bool
WorkArea::set_wh(int W, int H,int CHAN)
{
	// If our size is already set, don't set it again
	if(W==w && H==h && CHAN==bpp)
	{
		return true;
	}
	if(W<=0 || H<=0 || CHAN<=0)
		return false;

	assert(W>0);
	assert(H>0);
	assert(CHAN>0);

	// Set all of the parameters
	w=W;
	h=H;
	bpp=CHAN;

	refresh_dimension_info();

	tile_book.clear();

	return true;
}

bool
WorkArea::on_key_press_event(GdkEventKey* event)
{
	if(get_selected_ducks().empty())
		return false;

	Real multiplier(1.0);

	if(Gdk::ModifierType(event->state)&GDK_SHIFT_MASK)
		multiplier=10.0;

	Vector nudge;
	switch(event->keyval)
	{
		case GDK_Left:
			nudge=Vector(-pw,0);
			break;
		case GDK_Right:
			nudge=Vector(pw,0);
			break;
		case GDK_Up:
			nudge=Vector(0,-ph);
			break;
		case GDK_Down:
			nudge=Vector(0,ph);
			break;
		default:
			return false;
			break;
	}

	synfigapp::Action::PassiveGrouper grouper(instance.get(),_("Nudge"));

	// Grid snap does not apply to nudging
	bool grid_snap_holder(get_grid_snap());
	bool guide_snap_holder(get_guide_snap());
	set_grid_snap(false);

	try {
		start_duck_drag(get_selected_duck()->get_trans_point());
		translate_selected_ducks(get_selected_duck()->get_trans_point()+nudge*multiplier);
		end_duck_drag();
	}
	catch(String)
	{
		canvas_view->duck_refresh_flag=true;
		canvas_view->queue_rebuild_ducks();
	}

	set_grid_snap(grid_snap_holder);
	set_guide_snap(guide_snap_holder);

	return true;
}

bool
WorkArea::on_drawing_area_event(GdkEvent *event)
{
	synfig::Point mouse_pos;
    float bezier_click_pos;
	const float radius((abs(pw)+abs(ph))*4);
	int button_pressed(0);
	float pressure(0);
	bool is_mouse(false);
	Gdk::ModifierType modifier(Gdk::ModifierType(0));

	// Handle input stuff
	if(
		event->any.type==GDK_MOTION_NOTIFY ||
		event->any.type==GDK_BUTTON_PRESS ||
		event->any.type==GDK_2BUTTON_PRESS ||
		event->any.type==GDK_3BUTTON_PRESS ||
		event->any.type==GDK_BUTTON_RELEASE
	)
	{
		GdkDevice *device;
		if(event->any.type==GDK_MOTION_NOTIFY)
		{
			device=event->motion.device;
			modifier=Gdk::ModifierType(event->motion.state);
		}
		else
		{
			device=event->button.device;
			modifier=Gdk::ModifierType(event->button.state);
			drawing_area->grab_focus();
		}

		// Make sure we recognize the device
		if(curr_input_device)
		{
			if(curr_input_device!=device)
			{
				assert(device);
				curr_input_device=device;
				signal_input_device_changed()(curr_input_device);
			}
		}
		else if(device)
		{
			curr_input_device=device;
			signal_input_device_changed()(curr_input_device);
		}

		assert(curr_input_device);

		// Calculate the position of the
		// input device in canvas coordinates
		// and the buttons
		if(!event->button.axes)
		{
			mouse_pos=synfig::Point(screen_to_comp_coords(synfig::Point(event->button.x,event->button.y)));
			button_pressed=event->button.button;
			pressure=1.0f;
			is_mouse=true;
			if(isnan(event->button.x) || isnan(event->button.y))
				return false;
		}
		else
		{
			double x(event->button.axes[0]);
			double y(event->button.axes[1]);
			if(isnan(x) || isnan(y))
				return false;

			pressure=event->button.axes[2];
			//synfig::info("pressure=%f",pressure);
			pressure-=0.04f;
			pressure/=1.0f-0.04f;


			assert(!isnan(pressure));

			mouse_pos=synfig::Point(screen_to_comp_coords(synfig::Point(x,y)));

			button_pressed=event->button.button;

			if(button_pressed==1 && pressure<0 && (event->any.type!=GDK_BUTTON_RELEASE && event->any.type!=GDK_BUTTON_PRESS))
				button_pressed=0;
			if(pressure<0)
				pressure=0;

			//if(event->any.type==GDK_BUTTON_PRESS && button_pressed)
			//	synfig::info("Button pressed on input device = %d",event->button.button);

			//if(event->button.axes[2]>0.1)
			//	button_pressed=1;
			//else
			//	button_pressed=0;
		}
	}
	// GDK mouse scrolling events
	else if(event->any.type==GDK_SCROLL)
	{
		// GDK information needed to properly interpret mouse
		// scrolling events are: scroll.state, scroll.x/scroll.y, and
		// scroll.direction. The value of scroll.direction will be
		// obtained later.

		modifier=Gdk::ModifierType(event->scroll.state);
		mouse_pos=synfig::Point(screen_to_comp_coords(synfig::Point(event->scroll.x,event->scroll.y)));
	}

	// Handle the renderables
	{
		std::set<etl::handle<WorkAreaRenderer> >::iterator iter;
		for(iter=renderer_set_.begin();iter!=renderer_set_.end();++iter)
		{
			if((*iter)->get_enabled())
				if((*iter)->event_vfunc(event))
				{
					// Event handled. Return true.
					return true;
				}
		}
	}

	// Event hasn't been handled, pass it down
	switch(event->type)
    {
	case GDK_BUTTON_PRESS:
		{
		switch(button_pressed)
		{
		case 1:	// Attempt to click on a duck
		{
			etl::handle<Duck> duck;
			dragging=DRAG_NONE;

			if(allow_duck_clicks)
			{
				duck=find_duck(mouse_pos,radius);

				if(duck)
				{
					// make a note of whether the duck we click on was selected or not
					if(duck_is_selected(duck))
						clicked_duck=duck;
					else
					{
						clicked_duck=0;
						// if CTRL isn't pressed, clicking an unselected duck will unselect all other ducks
						if(!(modifier&GDK_CONTROL_MASK))
							clear_selected_ducks();
						select_duck(duck);
					}
				}
			}
			//else
			//	clear_selected_ducks();



			selected_bezier=find_bezier(mouse_pos,radius,&bezier_click_pos);
			if(duck)
			{
				if (!duck->get_editable())
					return true;

				//get_selected_duck()->signal_user_click(0)();
				//if(clicked_duck)clicked_duck->signal_user_click(0)();

				// if the user is holding shift while clicking on a tangent duck, consider splitting the tangent
				if (event->motion.state&GDK_SHIFT_MASK && duck->get_type() == Duck::TYPE_TANGENT)
				{
					synfigapp::ValueDesc value_desc = duck->get_value_desc();

					// we have the tangent, but need the vertex - that's the parent
					if (value_desc.parent_is_value_node()) {
						ValueNode_Composite::Handle parent_value_node = value_desc.get_parent_value_node();

						// if the tangent isn't split, then split it
						if (!((*(parent_value_node->get_link("split")))(get_time()).get(bool())))
						{
							if (get_canvas_view()->canvas_interface()->
								change_value(synfigapp::ValueDesc(parent_value_node,
																  parent_value_node->get_link_index_from_name("split")),
											 true))
							{
								// rebuild the ducks from scratch, so the tangents ducks aren't connected
								get_canvas_view()->rebuild_ducks();

								// reprocess the mouse click
								return on_drawing_area_event(event);
							}
							else
								return true;
						}
					} else {
						// I don't know how to access the vertex from the tangent duck when originally drawing the bline in the bline tool

						// synfig::ValueNode::Handle vn = value_desc.get_value_node();
						synfig::info("parent isn't value node?  shift-drag-tangent doesn't work in bline tool yet...");
					}
				}

				dragging=DRAG_DUCK;
				drag_point=mouse_pos;
				//drawing_area->queue_draw();
				start_duck_drag(mouse_pos);
				get_canvas_view()->reset_cancel_status();
				return true;
			}
// I commented out this section because
// it was causing issues when rotoscoping.
// At the moment, we don't need it, so
// this was the easiest way to fix the problem.
/*
			else
			if(selected_bezier)
			{
				selected_duck=0;
				selected_bezier->signal_user_click(0)(bezier_click_pos);
			}
*/
			else
			{
				//clear_selected_ducks();
				selected_bezier=0;
				if(canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DOWN,BUTTON_LEFT,mouse_pos,pressure,modifier))==Smach::RESULT_OK)
				{
					// Check for a guide click
					GuideList::iterator iter;

					iter=find_guide_x(mouse_pos,radius);
					if(iter==get_guide_list_x().end())
					{
						curr_guide_is_x=false;
						iter=find_guide_y(mouse_pos,radius);
					}
					else
						curr_guide_is_x=true;
					if(iter!=get_guide_list_x().end() && iter!=get_guide_list_y().end())
					{
						dragging=DRAG_GUIDE;
						curr_guide=iter;
						return true;
					}


					// All else fails, try making a selection box
					dragging=DRAG_BOX;
					curr_point=drag_point=mouse_pos;
					return true;
				}
			}
			break;
		}
		case 2:	// Attempt to drag and move the window
		{
			etl::handle<Duck> duck=find_duck(mouse_pos,radius);
			etl::handle<Bezier> bezier=find_bezier(mouse_pos,radius,&bezier_click_pos);
			if(duck)
				duck->signal_user_click(1)();
			else
			if(bezier)
				bezier->signal_user_click(1)(bezier_click_pos);

			if(canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DOWN,BUTTON_MIDDLE,mouse_pos,pressure,modifier))==Smach::RESULT_OK)
			if(is_mouse)
			{
				dragging=DRAG_WINDOW;
				drag_point=mouse_pos;
				signal_user_click(1)(mouse_pos);
			}
			break;
		}
		case 3:	// Attempt to either get info on a duck, or open the menu
		{
			etl::handle<Duck> duck=find_duck(mouse_pos,radius);
			etl::handle<Bezier> bezier=find_bezier(mouse_pos,radius,&bezier_click_pos);

			Layer::Handle layer(get_canvas()->find_layer(mouse_pos));
			if(duck)
			{
				if(get_selected_ducks().size()<=1)
					duck->signal_user_click(2)();
				else
					canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MULTIPLE_DUCKS_CLICKED,BUTTON_RIGHT,mouse_pos,pressure,modifier));
				return true;
			}
			else if(bezier)
			{
				bezier->signal_user_click(2)(bezier_click_pos);
				return true;
			}
			else if (layer)
			{
				if(canvas_view->get_smach().process_event(EventLayerClick(layer,BUTTON_RIGHT,mouse_pos))==Smach::RESULT_OK)
					return false;
				return true;
			}
			else
				canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DOWN,BUTTON_RIGHT,mouse_pos,pressure,modifier));
			/*
			if(canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DOWN,BUTTON_RIGHT,mouse_pos,pressure,modifier))==Smach::RESULT_OK)
			{
				//popup_menu();
				return true;
			}
			*/
			break;
		}
		case 4:
			signal_user_click(3)(mouse_pos);
			break;
		case 5:
			signal_user_click(4)(mouse_pos);
			break;
		default:
			break;
		}
		}
		break;
	case GDK_MOTION_NOTIFY:
		curr_point=mouse_pos;

		if(event->motion.time-last_event_time<25)
			return true;
		else
			last_event_time=event->motion.time;

		signal_cursor_moved_();

		// Guide/Duck highlights on hover
		if(dragging==DRAG_NONE)
		{
			GuideList::iterator iter;

			iter=find_guide_x(mouse_pos,radius);
			if(iter==get_guide_list_x().end())
				iter=find_guide_y(mouse_pos,radius);

			if(iter!=curr_guide)
			{
				curr_guide=iter;
				drawing_area->queue_draw();
			}

			etl::handle<Duck> duck;
			duck=find_duck(mouse_pos,radius);
			if(duck!=hover_duck)
			{
				hover_duck=duck;
				drawing_area->queue_draw();
			}
		}


		if(dragging==DRAG_DUCK)
		{
			if(canvas_view->get_cancel_status())
			{
				dragging=DRAG_NONE;
				canvas_view->queue_rebuild_ducks();
				return true;
			}
			/*
			Point point((mouse_pos-selected_duck->get_origin())/selected_duck->get_scalar());
			if(get_grid_snap())
			{
				point[0]=floor(point[0]/grid_size[0]+0.5)*grid_size[0];
				point[1]=floor(point[1]/grid_size[1]+0.5)*grid_size[1];
			}
			selected_duck->set_point(point);
			*/

			//Point p(mouse_pos);

			set_axis_lock(event->motion.state&GDK_SHIFT_MASK);

			translate_selected_ducks(mouse_pos);

			drawing_area->queue_draw();
		}

		if(dragging==DRAG_BOX)
		{
			curr_point=mouse_pos;
			drawing_area->queue_draw();
		}

		if(dragging==DRAG_GUIDE)
		{
			if(curr_guide_is_x)
				*curr_guide=mouse_pos[0];
			else
				*curr_guide=mouse_pos[1];
			drawing_area->queue_draw();
		}

		if(dragging!=DRAG_WINDOW)
		{	// Update those triangle things on the rulers
			const synfig::Point point(mouse_pos);
			hruler->property_position()=Distance(point[0],Distance::SYSTEM_UNITS).get(App::distance_system,get_canvas()->rend_desc());
			vruler->property_position()=Distance(point[1],Distance::SYSTEM_UNITS).get(App::distance_system,get_canvas()->rend_desc());
		}

		if(dragging == DRAG_WINDOW)
			set_focus_point(get_focus_point() + mouse_pos-drag_point);
		else if (event->motion.state & GDK_BUTTON1_MASK &&
				canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DRAG, BUTTON_LEFT,
																  mouse_pos,pressure,modifier)) == Smach::RESULT_ACCEPT)
			return true;
		else if (event->motion.state & GDK_BUTTON2_MASK &&
				 canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DRAG, BUTTON_MIDDLE,
																   mouse_pos, pressure, modifier)) == Smach::RESULT_ACCEPT)
			return true;
		else if (event->motion.state & GDK_BUTTON3_MASK &&
				 canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_DRAG, BUTTON_RIGHT,
																   mouse_pos, pressure, modifier)) == Smach::RESULT_ACCEPT)
			return true;
		else if(canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_MOTION, BUTTON_NONE,
																  mouse_pos, pressure,modifier)) == Smach::RESULT_ACCEPT)
			return true;

		break;

	case GDK_BUTTON_RELEASE:
	{
		bool ret(false);

		if(dragging==DRAG_GUIDE)
		{
			dragging=DRAG_NONE;
			save_meta_data();
			return true;
		}
		else
		if(dragging==DRAG_DUCK)
		{
			synfigapp::Action::PassiveGrouper grouper(instance.get(),_("Move"));
			dragging=DRAG_NONE;
			//translate_selected_ducks(mouse_pos);
			set_axis_lock(false);

			try{
			get_canvas_view()->duck_refresh_flag=false;
			get_canvas_view()->duck_refresh_needed=false;
			const bool drag_did_anything(end_duck_drag());
			get_canvas_view()->duck_refresh_flag=true;
			if(!drag_did_anything)
			{
				// if we originally clicked on a selected duck ...
				if(clicked_duck)
				{
					// ... and CTRL is pressed, then just toggle the clicked duck
					//     otherwise make the clicked duck the only selected duck
					if(modifier&GDK_CONTROL_MASK)
						unselect_duck(clicked_duck);
					else
					{
						clear_selected_ducks();
						select_duck(clicked_duck);
					}
					clicked_duck->signal_user_click(0)();
				}
			}
			else
			{
				if(canvas_view->duck_refresh_needed)
					canvas_view->queue_rebuild_ducks();
				return true;
			}
			}catch(String)
			{
				canvas_view->duck_refresh_flag=true;
				canvas_view->queue_rebuild_ducks();
				return true;
			}
			//queue_draw();
			clicked_duck=0;

			ret=true;
		}

		if(dragging==DRAG_BOX)
		{
			dragging=DRAG_NONE;
			if((drag_point-mouse_pos).mag()>radius/2.0f)
			{
				if(canvas_view->get_smach().process_event(EventBox(drag_point,mouse_pos,MouseButton(event->button.button),modifier))==Smach::RESULT_ACCEPT)
					return true;

				// when dragging a box around some ducks:
				// SHIFT selects; CTRL toggles; SHIFT+CTRL unselects; <none> clears all then selects
				if(modifier&GDK_SHIFT_MASK)
					select_ducks_in_box(drag_point,mouse_pos);

				if(modifier&GDK_CONTROL_MASK)
					toggle_select_ducks_in_box(drag_point,mouse_pos);
				else if(!(modifier&GDK_SHIFT_MASK))
				{
					clear_selected_ducks();
					select_ducks_in_box(drag_point,mouse_pos);
				}
				ret=true;
			}
			else
			{
				if(allow_layer_clicks)
				{
					Layer::Handle layer(get_canvas()->find_layer(drag_point));
					//if(layer)
					{
						if(canvas_view->get_smach().process_event(EventLayerClick(layer,BUTTON_LEFT,mouse_pos,modifier))==Smach::RESULT_OK)
							signal_layer_selected_(layer);
						ret=true;
					}
				}
				else
				{
					signal_user_click(0)(mouse_pos);
				}
			}
		}

		dragging=DRAG_NONE;

		if(canvas_view->get_smach().process_event(EventMouse(EVENT_WORKAREA_MOUSE_BUTTON_UP,MouseButton(event->button.button),mouse_pos,pressure,modifier))==Smach::RESULT_ACCEPT)
			ret=true;

		return ret;
	}
		break;
	case GDK_SCROLL:
	{
		// Handle a mouse scrolling event like Xara Xtreme and
		// Inkscape:

		// Scroll up/down: scroll up/down
		// Shift + scroll up/down: scroll left/right
		// Control + scroll up/down: zoom in/out

		if(modifier&GDK_CONTROL_MASK)
		{

			// The zoom is performed while preserving the pointer
			// position as a fixed point (similarly to Xara Xtreme and
			// Inkscape).

			// The strategy used below is to scroll to the updated
			// position, then zoom. This is easy to implement within
			// the present architecture, but has the disadvantage of
			// triggering multiple visible refreshes. Note: 1.25 is
			// the hard wired ratio in zoom_in()/zoom_out(). The
			// variable "drift" compensates additional inaccuracies in
			// the zoom. There is also an additional minus sign for
			// the inverted y coordinates.

			// FIXME: One might want to figure out where in the code
			// this empirical drift is been introduced.

			const synfig::Point scroll_point(get_scrollx_adjustment()->get_value(),get_scrolly_adjustment()->get_value());
			const double drift = 0.052;

			switch(event->scroll.direction)
			{
				case GDK_SCROLL_UP:
				case GDK_SCROLL_RIGHT:
					get_scrollx_adjustment()->set_value(scroll_point[0]+(mouse_pos[0]-scroll_point[0])*(1.25-(1+drift)));
					get_scrolly_adjustment()->set_value(scroll_point[1]-(mouse_pos[1]+scroll_point[1])*(1.25-(1+drift)));
					zoom_in();
					break;
				case GDK_SCROLL_DOWN:
				case GDK_SCROLL_LEFT:
					get_scrollx_adjustment()->set_value(scroll_point[0]+(mouse_pos[0]-scroll_point[0])*(1/1.25-(1+drift)));
					get_scrolly_adjustment()->set_value(scroll_point[1]-(mouse_pos[1]+scroll_point[1])*(1/1.25-(1+drift)));
					zoom_out();
					break;
				default:
					break;
			}
		}
		else if(modifier&GDK_SHIFT_MASK)
		{
			// Scroll in either direction by 20 pixels. Ideally, the
			// amount of pixels per scrolling event should be
			// configurable. Xara Xtreme currently uses an (hard
			// wired) amount 20 pixel, Inkscape defaults to 40 pixels.

			const int scroll_pixel = 20;

			switch(event->scroll.direction)
			{
				case GDK_SCROLL_UP:
					get_scrollx_adjustment()->set_value(get_scrollx_adjustment()->get_value()-scroll_pixel*pw);
					break;
				case GDK_SCROLL_DOWN:
					get_scrollx_adjustment()->set_value(get_scrollx_adjustment()->get_value()+scroll_pixel*pw);
					break;
				case GDK_SCROLL_LEFT:
					get_scrolly_adjustment()->set_value(get_scrolly_adjustment()->get_value()+scroll_pixel*ph);
					break;
				case GDK_SCROLL_RIGHT:
					get_scrolly_adjustment()->set_value(get_scrolly_adjustment()->get_value()-scroll_pixel*ph);
					break;
				default:
					break;
			}
		}
		else
		{
			// Scroll in either direction by 20 pixels. Ideally, the
			// amount of pixels per scrolling event should be
			// configurable. Xara Xtreme currently uses an (hard
			// wired) amount 20 pixel, Inkscape defaults to 40 pixels.

			const int scroll_pixel = 20;

			switch(event->scroll.direction)
			{
				case GDK_SCROLL_UP:
					get_scrolly_adjustment()->set_value(get_scrolly_adjustment()->get_value()+scroll_pixel*ph);
					break;
				case GDK_SCROLL_DOWN:
					get_scrolly_adjustment()->set_value(get_scrolly_adjustment()->get_value()-scroll_pixel*ph);
					break;
				case GDK_SCROLL_LEFT:
					get_scrollx_adjustment()->set_value(get_scrollx_adjustment()->get_value()-scroll_pixel*pw);
					break;
				case GDK_SCROLL_RIGHT:
					get_scrollx_adjustment()->set_value(get_scrollx_adjustment()->get_value()+scroll_pixel*pw);
					break;
				default:
					break;
			}
		}
	}
		break;
	default:
		break;
	}
		return false;
}

bool
WorkArea::on_hruler_event(GdkEvent */*event*/)
{
/*
	switch(event->type)
    {
	case GDK_BUTTON_PRESS:
		if(dragging==DRAG_NONE)
		{
			dragging=DRAG_GUIDE;
			curr_guide=get_guide_list_y().insert(get_guide_list_y().begin());
			curr_guide_is_x=false;
		}
		return true;
		break;

	case GDK_MOTION_NOTIFY:
		// Guide movement
		if(dragging==DRAG_GUIDE && curr_guide_is_x==false)
		{
			double y,x;
			if(event->button.axes)
			{
				x=(event->button.axes[0]);
				y=(event->button.axes[1]);
			}
			else
			{
				x=event->button.x;
				y=event->button.y;
			}

			if(isnan(y) || isnan(x))
				return false;

			*curr_guide=synfig::Point(screen_to_comp_coords(synfig::Point(x,y)))[1];

			queue_draw();
		}
		return true;
		break;

	case GDK_BUTTON_RELEASE:
		if(dragging==DRAG_GUIDE && curr_guide_is_x==false)
		{
			dragging=DRAG_NONE;
			get_guide_list_y().erase(curr_guide);
		}
		break;
		return true;
	default:
		break;
	}
*/
	return false;
}

bool
WorkArea::on_vruler_event(GdkEvent */*event*/)
{
/*
	switch(event->type)
    {
	case GDK_BUTTON_PRESS:
		if(dragging==DRAG_NONE)
		{
			dragging=DRAG_GUIDE;
			curr_guide=get_guide_list_x().insert(get_guide_list_x().begin());
			curr_guide_is_x=true;
		}
		return true;
		break;
	case GDK_BUTTON_RELEASE:
		if(dragging==DRAG_GUIDE && curr_guide_is_x==true)
		{
			dragging=DRAG_NONE;
			get_guide_list_x().erase(curr_guide);
		}
		break;
		return true;
	default:
		break;
	}
*/
	return false;
}


void
WorkArea::refresh_dimension_info()
{
	synfig::RendDesc &rend_desc(get_canvas()->rend_desc());

	canvaswidth=rend_desc.get_br()[0]-rend_desc.get_tl()[0];
	canvasheight=rend_desc.get_br()[1]-rend_desc.get_tl()[1];

	pw=canvaswidth/w;
	ph=canvasheight/h;

	scrollx_adjustment.set_page_increment(abs(get_grid_size()[0]));
	scrollx_adjustment.set_step_increment(abs(pw));
	scrollx_adjustment.set_lower(-abs(canvaswidth));
	scrollx_adjustment.set_upper(abs(canvaswidth));
	scrolly_adjustment.set_lower(-abs(canvasheight));
	scrolly_adjustment.set_upper(abs(canvasheight));
	scrolly_adjustment.set_step_increment(abs(ph));
	scrolly_adjustment.set_page_increment(abs(get_grid_size()[1]));



	if(drawing_area->get_width()<=0 || drawing_area->get_height()<=0 || w==0 || h==0)
		return;

	const synfig::Point focus_point(get_focus_point());
	const synfig::Real x(focus_point[0]/pw+drawing_area->get_width()/2-w/2);
	const synfig::Real y(focus_point[1]/ph+drawing_area->get_height()/2-h/2);

	window_tl[0]=rend_desc.get_tl()[0]-pw*x;
	window_br[0]=rend_desc.get_br()[0]+pw*(drawing_area->get_width()-x-w);

	window_tl[1]=rend_desc.get_tl()[1]-ph*y;
	window_br[1]=rend_desc.get_br()[1]+ph*(drawing_area->get_height()-y-h);

	hruler->property_lower()=Distance(window_tl[0],Distance::SYSTEM_UNITS).get(App::distance_system,rend_desc);
	hruler->property_upper()=Distance(window_br[0],Distance::SYSTEM_UNITS).get(App::distance_system,rend_desc);
	vruler->property_lower()=Distance(window_tl[1],Distance::SYSTEM_UNITS).get(App::distance_system,rend_desc);
	vruler->property_upper()=Distance(window_br[1],Distance::SYSTEM_UNITS).get(App::distance_system,rend_desc);

	view_window_changed();
}


synfig::Point
WorkArea::screen_to_comp_coords(synfig::Point pos)const
{
	synfig::RendDesc &rend_desc(get_canvas()->rend_desc());
	//synfig::Vector::value_type canvaswidth=rend_desc.get_br()[0]-rend_desc.get_tl()[0];
	//synfig::Vector::value_type canvasheight=rend_desc.get_br()[1]-rend_desc.get_tl()[1];
	//synfig::Vector::value_type pw=canvaswidth/w;
	//synfig::Vector::value_type ph=canvasheight/h;
	Vector focus_point=get_focus_point();
	synfig::Vector::value_type x=focus_point[0]/pw+drawing_area->get_width()/2-w/2;
	synfig::Vector::value_type y=focus_point[1]/ph+drawing_area->get_height()/2-h/2;

	return rend_desc.get_tl()-synfig::Point(pw*x,ph*y)+synfig::Point(pw*pos[0],ph*pos[1]);
}

synfig::Point
WorkArea::comp_to_screen_coords(synfig::Point /*pos*/)const
{
	synfig::warning("WorkArea::comp_to_screen_coords: Not yet implemented");
	return synfig::Point();
}

int
WorkArea::next_unrendered_tile(int refreshes)const
{
	//assert(!tile_book.empty());
	if(tile_book.empty())
		return -1;

	//const synfig::RendDesc &rend_desc(get_canvas()->rend_desc());

	const synfig::Vector focus_point(get_focus_point());

	// Calculate the window coordinates of the top-left
	// corner of the canvas.
	const synfig::Vector::value_type
		x(focus_point[0]/pw+drawing_area->get_width()/2-w/2),
		y(focus_point[1]/ph+drawing_area->get_height()/2-h/2);

	int div = low_res_pixel_size;
	const int width_in_tiles(w/tile_w+((low_resolution?((w/div)%(tile_w/div)):(w%tile_w))?1:0));
	const int height_in_tiles(h/tile_h+(h%tile_h?1:0));

	int
		u(0),v(0),
		u1(int(-x/tile_w)),
		v1(int(-y/tile_h)),
		u2(int((-x+drawing_area->get_width())/tile_w+1)),
		v2(int((-y+drawing_area->get_height())/tile_h+1));

	if(u2>width_in_tiles)u2=width_in_tiles;
	if(v2>height_in_tiles)v2=height_in_tiles;
	if(u1<0)u1=0;
	if(v1<0)v1=0;

	int last_good_tile(-1);

	for(v=v1;v<v2;v++)
		for(u=u1;u<u2;u++)
		{
			int index(v*width_in_tiles+u);
			if(tile_book[index].second<refreshes)
			{
				last_good_tile=index;
				if(rand()%8==0)
					return index;
			}
		}
	return last_good_tile;
}

/*
template <typename F, typename T=WorkAreaRenderer, typename R=typename F::result_type>
class handle2ptr_t : public std::unary_function<typename etl::handle<T>,R>
{
private:
	F func;
public:
	handle2ptr_t(const F &func):func(func) { };

	R operator()(typename etl::handle<T> x) { return func(*x); }
};

template <typename F>
handle2ptr_t<F>
handle2ptr(F func)
{
	return handle2ptr_t<F>(func);
}
	for_each(
		renderer_set_.begin(),
		renderer_set_.end(),
		handle2ptr(
			sigc::bind(
				sigc::bind(
					sigc::mem_fun(
						&WorkAreaRenderer::render_vfunc
					),
					Gdk::Rectangle(event->area)
				),
				drawing_area->get_window()
			)
		)
	);
*/

bool
WorkArea::refresh(GdkEventExpose*event)
{
	assert(get_canvas());

	drawing_area->get_window()->clear();

	//const synfig::RendDesc &rend_desc(get_canvas()->rend_desc());

	const synfig::Vector focus_point(get_focus_point());

	// Update the old focus point
	last_focus_point=focus_point;

	// Draw out the renderables
	{
		std::set<etl::handle<WorkAreaRenderer> >::iterator iter;
		for(iter=renderer_set_.begin();iter!=renderer_set_.end();++iter)
		{
			if((*iter)->get_enabled())
				(*iter)->render_vfunc(
					drawing_area->get_window(),
					Gdk::Rectangle(&event->area)
				);
		}
	}

	// Calculate the window coordinates of the top-left
	// corner of the canvas.
	//const synfig::Vector::value_type
	//	x(focus_point[0]/pw+drawing_area->get_width()/2-w/2),
	//	y(focus_point[1]/ph+drawing_area->get_height()/2-h/2);

	//const synfig::Vector::value_type window_startx(window_tl[0]);
	//const synfig::Vector::value_type window_endx(window_br[0]);
	//const synfig::Vector::value_type window_starty(window_tl[1]);
	//const synfig::Vector::value_type window_endy(window_br[1]);

	Glib::RefPtr<Gdk::GC> gc=Gdk::GC::create(drawing_area->get_window());

	// If we are in animate mode, draw a red border around the screen
	if(canvas_interface->get_mode()&synfigapp::MODE_ANIMATE)
	{
// #define USE_FRAME_BACKGROUND_TO_SHOW_EDIT_MODE
#ifdef USE_FRAME_BACKGROUND_TO_SHOW_EDIT_MODE
		// This method of drawing the red border doesn't work on any
		// Gtk theme which uses the crux-engine, hcengine, industrial,
		// mist, or ubuntulooks engine, such as the default ubuntu
		// 'Human' theme.
		drawing_frame->modify_bg(Gtk::STATE_NORMAL,Gdk::Color("#FF0000"));
#else
		// So let's do it in a more primitive fashion.
		gc->set_rgb_fg_color(Gdk::Color("#FF0000"));
		gc->set_line_attributes(1,Gdk::LINE_SOLID,Gdk::CAP_BUTT,Gdk::JOIN_MITER);
		drawing_area->get_window()->draw_rectangle(
			gc,
			false,	// Fill?
			0,0,	// x,y
			drawing_area->get_width()-1,drawing_area->get_height()-1); // w,h
#endif
	}
#ifdef USE_FRAME_BACKGROUND_TO_SHOW_EDIT_MODE
	else
		drawing_frame->unset_bg(Gtk::STATE_NORMAL);
#endif

	return true;
}

void
WorkArea::done_rendering()
{
/*
	assert(buffer);
	assert(w>0);
	assert(h>0);
	pix_buf=Gdk::Pixbuf::create_from_data(
		buffer,	// pointer to the data
		Gdk::COLORSPACE_RGB, // the colorspace
		true, // has alpha?
		8, // bits per sample
		w,	// width
		h,	// height
		w*bpp);	// stride (pitch)
	assert(pix_buf);
*/
}


void
WorkArea::set_quality(int x)
{
	if(x==quality)
		return;
	quality=x;
	queue_render_preview();
}

void
WorkArea::set_low_res_pixel_size(int x)
{
	if(x==low_res_pixel_size)
		return;
	low_res_pixel_size=x;
	queue_render_preview();
}

namespace studio
{
class WorkAreaProgress : public synfig::ProgressCallback
{
	WorkArea *work_area;
	ProgressCallback *cb;

public:

	WorkAreaProgress(WorkArea *work_area,ProgressCallback *cb):
		work_area(work_area),cb(cb)
	{
		assert(cb);
	}

	virtual bool
	task(const std::string &str)
	{
		if(work_area->dirty)
			return false;
		return cb->task(str);
	}

	virtual bool
	error(const std::string &err)
	{
		if(work_area->dirty)
			return false;
		return cb->error(err);
	}

	virtual bool
	amount_complete(int current, int total)
	{
		if(work_area->dirty)
			return false;
		return cb->amount_complete(current,total);
	}
};
}

bool
studio::WorkArea::async_update_preview()
{
#ifdef SINGLE_THREADED
	if (get_updating())
	{
		stop_updating();
		queue_render_preview();
		return false;
	}
#endif

	async_renderer=0;

	queued=false;
	canceled_=false;
	get_canvas_view()->reset_cancel_status();

	// This object will mark us as busy until
	// we are done.
	//studio::App::Busy busy;

	//WorkAreaProgress callback(this,get_canvas_view()->get_ui_interface().get());
	//synfig::ProgressCallback *cb=&callback;

	if(!is_visible())return false;

	/*
	// If we are queued to render the scene at the next idle
	// go ahead and de-queue it.
	if(render_idle_func_id)
	{
		g_source_remove(render_idle_func_id);
		//queued=false;
		render_idle_func_id=0;
	}
	*/

	dirty=false;
	get_canvas_view()->reset_cancel_status();

	//bool ret=false;
	RendDesc desc=get_canvas()->rend_desc();

	int w=(int)(desc.get_w()*zoom);
	int h=(int)(desc.get_h()*zoom);

	// ensure that the size we draw is at least one pixel in each dimension
	int min_size = low_resolution ? low_res_pixel_size : 1;
	if (w < min_size) w = min_size;
	if (h < min_size) h = min_size;

	// Setup the description parameters
	desc.set_antialias(1);
	desc.set_time(cur_time);

	set_rend_desc(desc);

	// Create the render target
	handle<Target> target;

	// if we have lots of pixels to render and the tile renderer isn't disabled, use it
	int div;
	div = low_resolution ? low_res_pixel_size : 1;
	if ((w*h > 240*div*135*div && !getenv("SYNFIG_DISABLE_TILE_RENDER")) || getenv("SYNFIG_FORCE_TILE_RENDER"))
	{
		// do a tile render
		handle<WorkAreaTarget> trgt(new class WorkAreaTarget(this,w,h));

		trgt->set_rend_desc(&desc);
		trgt->set_onion_skin(get_onion_skin(), onion_skins);
		target=trgt;
	}
	else
	{
		// do a scanline render
		handle<WorkAreaTarget_Full> trgt(new class WorkAreaTarget_Full(this,w,h));

		trgt->set_rend_desc(&desc);
		trgt->set_onion_skin(get_onion_skin(), onion_skins);
		target=trgt;
	}

	// We can rest assured that our time has already
	// been set, so there is no need to have to
	// recalculate that over again.
	// UPDATE: This is kind of needless with
	// the way that time is handled now in SYNFIG.
	//target->set_avoid_time_sync(true);
	async_renderer=new AsyncRenderer(target);
	async_renderer->signal_finished().connect(
		sigc::mem_fun(this,&WorkArea::async_update_finished)
	);

	rendering=true;
	async_renderer->start();

	synfig::ProgressCallback *cb=get_canvas_view()->get_ui_interface().get();

	rendering=true;
	cb->task(_("Rendering..."));
	rendering=true;

	return true;
}

void
studio::WorkArea::async_update_finished()
{
	synfig::ProgressCallback *cb=get_canvas_view()->get_ui_interface().get();

	rendering=false;

	if(!async_renderer)
		return;

	// If we completed successfully, then
	// we aren't dirty anymore
	if(async_renderer->has_success())
	{
		dirty=false;
		//queued=false;
		cb->task(_("Idle"));
	}
	else
	{
		dirty=true;
		cb->task(_("Render Failed"));
	}
	//get_canvas_view()->reset_cancel_status();
	done_rendering();
}

bool
studio::WorkArea::sync_update_preview()
{
	//	const Time &time(cur_time);

	canceled_=false;
	get_canvas_view()->reset_cancel_status();

	async_renderer=0;

again:
	// This object will mark us as busy until
	// we are done.
	studio::App::Busy busy;

	WorkAreaProgress callback(this,get_canvas_view()->get_ui_interface().get());
	synfig::ProgressCallback *cb=&callback;

	// We don't want to render if we are already rendering
	if(rendering)
	{
		dirty=true;
		return false;
	}

	if(!is_visible())return false;
	get_canvas()->set_time(get_time());
	get_canvas_view()->get_smach().process_event(EVENT_REFRESH_DUCKS);
	signal_rendering()();

	// If we are queued to render the scene at the next idle
	// go ahead and de-queue it.
	if(render_idle_func_id)
	{
		g_source_remove(render_idle_func_id);
		//queued=false;
		render_idle_func_id=0;
	}
	// Start rendering
	rendering=true;

	dirty=false;
	get_canvas_view()->reset_cancel_status();

	RendDesc desc=get_canvas()->rend_desc();
	//newdesc->set_flags(RendDesc::PX_ASPECT|RendDesc::IM_SPAN);

	int w=(int)(desc.get_w()*zoom);
	int h=(int)(desc.get_h()*zoom);

	// Setup the description parameters
	desc.set_antialias(1);
	desc.set_time(cur_time);
	//desc.set_wh(w,h);

	set_rend_desc(desc);

	// Create the render target
	handle<WorkAreaTarget> target(new class WorkAreaTarget(this,w,h));

	target->set_rend_desc(&desc);

	// We can rest assured that our time has already
	// been set, so there is no need to have to
	// recalculate that over again.
	target->set_avoid_time_sync(true);

	if(cb)
		cb->task(strprintf(_("Rendering canvas %s..."),get_canvas()->get_name().c_str()));

	bool ret = target->render(cb);

	if(!ret && !get_canvas_view()->get_cancel_status() && dirty)
	{
		rendering=false;
		//canceled_=true;
		goto again;
	}
	if(get_canvas_view()->get_cancel_status())
		canceled_=true;

	if(cb)
	{
		if(ret)
			cb->task(_("Idle"));
		else
			cb->task(_("Render Failed"));
		cb->amount_complete(0,1);
	}

	// Refresh the work area to make sure that
	// it is being displayed correctly
	drawing_area->queue_draw();

	// If we completed successfully, then
	// we aren't dirty anymore
	if(ret)
	{
		dirty=false;
		//queued=false;
	}
	else dirty=true;
	rendering=false;
	//get_canvas_view()->reset_cancel_status();
	done_rendering();
	return ret;
}

void
studio::WorkArea::async_render_preview(synfig::Time time)
{
	cur_time=time;
	//tile_book.clear();

	refreshes+=5;
	if(!is_visible())return;

	get_canvas()->set_time(get_time());
	get_canvas_view()->get_smach().process_event(EVENT_REFRESH_DUCKS);
	signal_rendering()();

	async_update_preview();
}
void
WorkArea::async_render_preview()
{
	return async_render_preview(get_canvas_view()->get_time());
}

bool
studio::WorkArea::sync_render_preview(synfig::Time time)
{
	cur_time=time;
	//tile_book.clear();
	refreshes+=5;
	if(!is_visible())return false;
	return sync_update_preview();
}

bool
WorkArea::sync_render_preview()
{
	return sync_render_preview(get_canvas_view()->get_time());
}

void
WorkArea::sync_render_preview_hook()
{
	sync_render_preview(get_canvas_view()->get_time());
}

void
WorkArea::queue_scroll()
{
//	const synfig::RendDesc &rend_desc(get_canvas()->rend_desc());

	const synfig::Point focus_point(get_focus_point());

	const synfig::Real
		new_x(focus_point[0]/pw+drawing_area->get_width()/2-w/2),
		new_y(focus_point[1]/ph+drawing_area->get_height()/2-h/2);

	const synfig::Real
		old_x(last_focus_point[0]/pw+drawing_area->get_width()/2-w/2),
		old_y(last_focus_point[1]/ph+drawing_area->get_height()/2-h/2);

	// If the coordinates didn't change, we shouldn't queue a draw
	if(old_x==new_x && old_y==new_y)
		return;

	const int
		dx(round_to_int(old_x)-round_to_int(new_x)),
		dy(round_to_int(old_y)-round_to_int(new_y));

	drawing_area->get_window()->scroll(-dx,-dy);

	if (timecode_width && timecode_height)
	{
		drawing_area->queue_draw_area(4,       4,    4+timecode_width,    4+timecode_height);
		drawing_area->queue_draw_area(4-dx, 4-dy, 4-dx+timecode_width, 4-dy+timecode_height);
	}

#ifndef USE_FRAME_BACKGROUND_TO_SHOW_EDIT_MODE
	if(canvas_interface->get_mode()&synfigapp::MODE_ANIMATE)
	{
		int maxx = drawing_area->get_width()-1;
		int maxy = drawing_area->get_height()-1;

		if (dx > 0)
		{
			drawing_area->queue_draw_area(      0, 0,       1, maxy);
			drawing_area->queue_draw_area(maxx-dx, 0, maxx-dx, maxy);
		}
		else if (dx < 0)
		{
			drawing_area->queue_draw_area(   maxx, 0,    maxx, maxy);
			drawing_area->queue_draw_area(    -dx, 0,     -dx, maxy);
		}
		if (dy > 0)
		{
			drawing_area->queue_draw_area(0,       0, maxx,       1);
			drawing_area->queue_draw_area(0, maxy-dy, maxx, maxy-dy);
		}
		else if (dy < 0)
		{
			drawing_area->queue_draw_area(0,    maxy, maxx,    maxy);
			drawing_area->queue_draw_area(0,     -dy, maxx,     -dy);
		}
	}
#endif // USE_FRAME_BACKGROUND_TO_SHOW_EDIT_MODE

	last_focus_point=focus_point;
}

void
studio::WorkArea::zoom_in()
{
	set_zoom(zoom*1.25);
}

void
studio::WorkArea::zoom_out()
{
	set_zoom(zoom/1.25);
}

void
studio::WorkArea::zoom_fit()
{
	float new_zoom(min(drawing_area->get_width() * zoom / w,
					   drawing_area->get_height() * zoom / h) * 0.995);
	if (zoom / new_zoom > 0.995 && new_zoom / zoom > 0.995)
	{
		set_zoom(prev_zoom);
		return set_focus_point(previous_focus);
	}
	previous_focus = get_focus_point();
	prev_zoom = zoom;
	set_zoom(new_zoom);
	set_focus_point(Point(0,0));
}

void
studio::WorkArea::zoom_norm()
{
	if (zoom == 1.0) return set_zoom(prev_zoom);
	prev_zoom = zoom;
	set_zoom(1.0f);
}

gboolean
studio::WorkArea::__render_preview(gpointer data)
{
	WorkArea *work_area(static_cast<WorkArea*>(data));

	// there's no point anyone trying to cancel the timer now - it's gone off already
	work_area->render_idle_func_id = 0;

	work_area->queued=false;
	work_area->async_render_preview(work_area->get_canvas_view()->get_time());

	return 0;
}

void
studio::WorkArea::queue_render_preview()
{
	//synfig::info("queue_render_preview(): called for %s", get_canvas_view()->get_time().get_string().c_str());

	if(queued==true)
	{
		return;
		//synfig::info("queue_render_preview(): already queued, unqueuing");
/*		if(render_idle_func_id)
			g_source_remove(render_idle_func_id);
		render_idle_func_id=0;
		queued=false;
*/
		//async_renderer=0;
	}

	if(dirty_trap_enabled)
	{
		dirty_trap_queued++;
		return;
	}

	int queue_time=50;

	if(rendering)
		queue_time+=250;


	if(queued==false)
	{
		//synfig::info("queue_render_preview(): (re)queuing...");
		//render_idle_func_id=g_idle_add_full(G_PRIORITY_DEFAULT,__render_preview,this,NULL);
		render_idle_func_id=g_timeout_add_full(
			G_PRIORITY_DEFAULT,	// priority -
			queue_time,			// interval - the time between calls to the function, in milliseconds (1/1000ths of a second)
			__render_preview,	// function - function to call
			this,				// data     - data to pass to function
			NULL);				// notify   - function to call when the idle is removed, or NULL
		queued=true;
	}
/*	else if(rendering)
	{
		refreshes+=5;
		dirty=true;
		queue_draw();
	}
*/
}

DirtyTrap::DirtyTrap(WorkArea *work_area):work_area(work_area)
{
	work_area->dirty_trap_enabled=true;

	work_area->dirty_trap_queued=0;
}

DirtyTrap::~DirtyTrap()
{
	work_area->dirty_trap_enabled=false;
	if(work_area->dirty_trap_queued)
		work_area->queue_render_preview();
}

void
studio::WorkArea::queue_draw_preview()
{
	drawing_area->queue_draw();
}

void
studio::WorkArea::set_cursor(const Gdk::Cursor& x)
{
	drawing_area->get_window()->set_cursor(x);
}
void
studio::WorkArea::set_cursor(Gdk::CursorType x)
{
	drawing_area->get_window()->set_cursor(Gdk::Cursor(x));
}

//#include "iconcontroller.h"
void
studio::WorkArea::refresh_cursor()
{
//	set_cursor(IconController::get_tool_cursor(canvas_view->get_smach().get_state_name(),drawing_area->get_window()));
}

void
studio::WorkArea::reset_cursor()
{
	drawing_area->get_window()->set_cursor(Gdk::Cursor(Gdk::TOP_LEFT_ARROW));
//	set_cursor(Gdk::TOP_LEFT_ARROW);
}

void
studio::WorkArea::set_zoom(float z)
{
	z=max(1.0f/128.0f,min(128.0f,z));
	if(z==zoom)
		return;
	zoom = z;
	refresh_dimension_info();
	/*if(async_renderer)
	{
		async_renderer->stop();
		async_renderer=0;
	}*/
	refreshes+=5;
	async_update_preview();
	//queue_render_preview();
}

void
WorkArea::set_selected_value_node(etl::loose_handle<synfig::ValueNode> x)
{
	if(x!=selected_value_node_)
	{
		selected_value_node_=x;
		queue_draw();
	}
}

void
WorkArea::insert_renderer(const etl::handle<WorkAreaRenderer> &x)
{
	renderer_set_.insert(x);
	x->set_work_area(this);
	queue_draw();
}

void
WorkArea::insert_renderer(const etl::handle<WorkAreaRenderer> &x, int priority)
{
	x->set_priority(priority);
	insert_renderer(x);
}

void
WorkArea::erase_renderer(const etl::handle<WorkAreaRenderer> &x)
{
	x->set_work_area(0);
	renderer_set_.erase(x);
	queue_draw();
}

void
WorkArea::resort_render_set()
{
	std::set<etl::handle<WorkAreaRenderer> > tmp(
		renderer_set_.begin(),
		renderer_set_.end()
	);
	renderer_set_.swap(tmp);
	queue_draw();
}