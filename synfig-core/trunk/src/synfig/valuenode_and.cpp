/* === S Y N F I G ========================================================= */
/*!	\file valuenode_and.cpp
**	\brief Implementation of the "And" valuenode conversion.
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2008 Chris Moore
**	Copyright (c) 2009 Nikita Kitaev
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

#include "valuenode_and.h"
#include "valuenode_const.h"
#include "general.h"

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;

/* === M A C R O S ========================================================= */

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

ValueNode_And::ValueNode_And(const ValueBase &x):
	LinkableValueNode(x.get_type())
{
	bool value(x.get(bool()));

	set_link("link1",        ValueNode_Const::create(bool(true)));
	set_link("link2",        ValueNode_Const::create(bool(false)));
	if (value)
		set_link("link2",ValueNode_Const::create(bool(true)));
}

ValueNode_And*
ValueNode_And::create(const ValueBase &x)
{
	return new ValueNode_And(x);
}

LinkableValueNode*
ValueNode_And::create_new()const
{
	return new ValueNode_And(get_type());
}

ValueNode_And::~ValueNode_And()
{
	unlink_all();
}

bool
ValueNode_And::set_link_vfunc(int i,ValueNode::Handle value)
{
	assert(i>=0 && i<link_count());

	switch(i)
	{
	case 0: CHECK_TYPE_AND_SET_VALUE(link1_,    ValueBase::TYPE_BOOL);
	case 1: CHECK_TYPE_AND_SET_VALUE(link2_,    ValueBase::TYPE_BOOL);
	}
	return false;
}

ValueNode::LooseHandle
ValueNode_And::get_link_vfunc(int i)const
{
	assert(i>=0 && i<link_count());

	if(i==0) return link1_;
	if(i==1) return link2_;
	return 0;
}

int
ValueNode_And::link_count()const
{
	return 2;
}

String
ValueNode_And::link_local_name(int i)const
{
	assert(i>=0 && i<link_count());

	if(i==0) return _("Link1");
	if(i==1) return _("Link2");
	return String();
}

String
ValueNode_And::link_name(int i)const
{
	assert(i>=0 && i<link_count());

	if(i==0) return "link1";
	if(i==1) return "link2";
	return String();
}

int
ValueNode_And::get_link_index_from_name(const String &name)const
{
	if(name=="link1")    return 0;
	if(name=="link2")    return 1;

	throw Exception::BadLinkName(name);
}

ValueBase
ValueNode_And::operator()(Time t)const
{
	if (getenv("SYNFIG_DEBUG_VALUENODE_OPERATORS"))
		printf("%s:%d operator()\n", __FILE__, __LINE__);

	bool link1     = (*link1_)   (t).get(bool());
	bool link2     = (*link2_)   (t).get(bool());

	return (link1 && link2);
}

String
ValueNode_And::get_name()const
{
	return "and";
}

String
ValueNode_And::get_local_name()const
{
	return _("AND");
}

bool
ValueNode_And::check_type(ValueBase::Type type)
{
	return type==ValueBase::TYPE_BOOL;
}
