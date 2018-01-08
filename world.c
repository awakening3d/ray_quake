/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// world.c -- world query functions

#include "quakedef.h"
#include "tracing.h"
/*

entities never clip against themselves, or their owner

line of sight checks trace->crosscontent, but bullets don't

*/




int SV_HullPointContents (hull_t *hull, int num, vec3_t p);

/*
===============================================================================

HULL BOXES

===============================================================================
*/

static struct {
	hull_t		box_hull;
	dclipnode_t	box_clipnodes[6];
	mplane_t	box_planes[6];
} boxhulls[MAX_TRACE_THREAD_NUM];


/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
void SV_InitBoxHull (void)
{
	int		i;
	int		side;

	for (int thread = 0; thread < MAX_TRACE_THREAD_NUM; thread++) {
		boxhulls[thread].box_hull.clipnodes = boxhulls[thread].box_clipnodes;
		boxhulls[thread].box_hull.planes = boxhulls[thread].box_planes;
		boxhulls[thread].box_hull.firstclipnode = 0;
		boxhulls[thread].box_hull.lastclipnode = 5;

		for (i = 0; i < 6; i++)
		{
			boxhulls[thread].box_clipnodes[i].planenum = i;

			side = i & 1;

			boxhulls[thread].box_clipnodes[i].children[side] = CONTENTS_EMPTY;
			if (i != 5)
				boxhulls[thread].box_clipnodes[i].children[side ^ 1] = i + 1;
			else
				boxhulls[thread].box_clipnodes[i].children[side ^ 1] = CONTENTS_SOLID;

			boxhulls[thread].box_planes[i].type = i >> 1;
			boxhulls[thread].box_planes[i].normal[i >> 1] = 1;
		}
	}
	
}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
hull_t	*SV_HullForBox (int thread, vec3_t mins, vec3_t maxs)
{
	boxhulls[thread].box_planes[0].dist = maxs[0];
	boxhulls[thread].box_planes[1].dist = mins[0];
	boxhulls[thread].box_planes[2].dist = maxs[1];
	boxhulls[thread].box_planes[3].dist = mins[1];
	boxhulls[thread].box_planes[4].dist = maxs[2];
	boxhulls[thread].box_planes[5].dist = mins[2];

	return &boxhulls[thread].box_hull;
}



/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
hull_t *SV_HullForEntity (int thread, edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset)
{
	model_t		*model;
	vec3_t		size;
	vec3_t		hullmins, hullmaxs;
	hull_t		*hull;

// decide which clipping hull to use, based on the size
	if (ent->v.solid == SOLID_BSP)
	{	// explicit hulls in the BSP model
		if (ent->v.movetype != MOVETYPE_PUSH)
			Sys_Error ("SOLID_BSP without MOVETYPE_PUSH");

		model = sv.models[ (int)ent->v.modelindex ];

		if (!model || model->type != mod_brush)
			Sys_Error ("MOVETYPE_PUSH with a non bsp model");

		VectorSubtract (maxs, mins, size);
		if (size[0] < 3)
			hull = &model->hulls[0];
		else if (size[0] <= 32)
			hull = &model->hulls[1];
		else
			hull = &model->hulls[2];

// calculate an offset value to center the origin
		VectorSubtract (hull->clip_mins, mins, offset);
		VectorAdd (offset, ent->v.origin, offset);
	}
	else
	{	// create a temp hull from bounding box sizes

		VectorSubtract (ent->v.mins, maxs, hullmins);
		VectorSubtract (ent->v.maxs, mins, hullmaxs);
		hull = SV_HullForBox (thread,hullmins, hullmaxs);
		
		VectorCopy (ent->v.origin, offset);
	}


	return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/



areanode_t	sv_areanodes[AREA_NODES];
static	int			sv_numareanodes;

/*
===============
SV_CreateAreaNode

===============
*/
areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1, mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes];
	sv_numareanodes++;

	ClearLink (&anode->trigger_edicts);
	ClearLink (&anode->solid_edicts);
	
	if (depth == AREA_DEPTH)
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}
	
	VectorSubtract (maxs, mins, size);
	if (size[0] > size[1])
		anode->axis = 0;
	else
		anode->axis = 1;
	
	anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
	VectorCopy (mins, mins1);	
	VectorCopy (mins, mins2);	
	VectorCopy (maxs, maxs1);	
	VectorCopy (maxs, maxs2);	
	
	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;
	
	anode->children[0] = SV_CreateAreaNode (depth+1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode (depth+1, mins1, maxs1);

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld (void)
{
	SV_InitBoxHull ();
	
	memset (sv_areanodes, 0, sizeof(sv_areanodes));
	sv_numareanodes = 0;
	SV_CreateAreaNode (0, sv.worldmodel->mins, sv.worldmodel->maxs);
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict (edict_t *ent)
{
	if (!ent->area.prev)
		return;		// not linked in anywhere
	RemoveLink (&ent->area);
	ent->area.prev = ent->area.next = NULL;
}


/*
====================
SV_TouchLinks
====================
*/
void SV_TouchLinks ( edict_t *ent, areanode_t *node )
{
	link_t		*l, *next;
	edict_t		*touch;
	int			old_self, old_other;

// touch linked edicts
	for (l = node->trigger_edicts.next ; l != &node->trigger_edicts ; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch == ent)
			continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
			continue;
		if (ent->v.absmin[0] > touch->v.absmax[0]
		|| ent->v.absmin[1] > touch->v.absmax[1]
		|| ent->v.absmin[2] > touch->v.absmax[2]
		|| ent->v.absmax[0] < touch->v.absmin[0]
		|| ent->v.absmax[1] < touch->v.absmin[1]
		|| ent->v.absmax[2] < touch->v.absmin[2] )
			continue;
		old_self = pr_global_struct->self;
		old_other = pr_global_struct->other;

		pr_global_struct->self = EDICT_TO_PROG(touch);
		pr_global_struct->other = EDICT_TO_PROG(ent);
		pr_global_struct->time = sv.time;
		PR_ExecuteProgram (touch->v.touch);

		pr_global_struct->self = old_self;
		pr_global_struct->other = old_other;
	}
	
// recurse down both sides
	if (node->axis == -1)
		return;
	
	if ( ent->v.absmax[node->axis] > node->dist )
		SV_TouchLinks ( ent, node->children[0] );
	if ( ent->v.absmin[node->axis] < node->dist )
		SV_TouchLinks ( ent, node->children[1] );
}


/*
===============
SV_FindTouchedLeafs

===============
*/
void SV_FindTouchedLeafs (edict_t *ent, mnode_t *node)
{
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;
	int			leafnum;

	if (node->contents == CONTENTS_SOLID)
		return;
	
// add an efrag if the node is a leaf

	if ( node->contents < 0)
	{
		if (ent->num_leafs == MAX_ENT_LEAFS)
			return;

		leaf = (mleaf_t *)node;
		leafnum = leaf - sv.worldmodel->leafs - 1;

		ent->leafnums[ent->num_leafs] = leafnum;
		ent->num_leafs++;			
		return;
	}
	
// NODE_MIXED

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);
	
// recurse down the contacted sides
	if (sides & 1)
		SV_FindTouchedLeafs (ent, node->children[0]);
		
	if (sides & 2)
		SV_FindTouchedLeafs (ent, node->children[1]);
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent, qboolean touch_triggers)
{
	areanode_t	*node;

	if (ent->area.prev)
		SV_UnlinkEdict (ent);	// unlink from old position
		
	if (ent == sv.edicts)
		return;		// don't add the world

	if (ent->free)
		return;

// set the abs box

#ifdef QUAKE2
	if (ent->v.solid == SOLID_BSP && 
	(ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
	{	// expand for rotation
		float		max, v;
		int			i;

		max = 0;
		for (i=0 ; i<3 ; i++)
		{
			v =fabs( ent->v.mins[i]);
			if (v > max)
				max = v;
			v =fabs( ent->v.maxs[i]);
			if (v > max)
				max = v;
		}
		for (i=0 ; i<3 ; i++)
		{
			ent->v.absmin[i] = ent->v.origin[i] - max;
			ent->v.absmax[i] = ent->v.origin[i] + max;
		}
	}
	else
#endif
	{
		VectorAdd (ent->v.origin, ent->v.mins, ent->v.absmin);	
		VectorAdd (ent->v.origin, ent->v.maxs, ent->v.absmax);
	}

//
// to make items easier to pick up and allow them to be grabbed off
// of shelves, the abs sizes are expanded
//
	if ((int)ent->v.flags & FL_ITEM)
	{
		ent->v.absmin[0] -= 15;
		ent->v.absmin[1] -= 15;
		ent->v.absmax[0] += 15;
		ent->v.absmax[1] += 15;
	}
	else
	{	// because movement is clipped an epsilon away from an actual edge,
		// we must fully check even when bounding boxes don't quite touch
		ent->v.absmin[0] -= 1;
		ent->v.absmin[1] -= 1;
		ent->v.absmin[2] -= 1;
		ent->v.absmax[0] += 1;
		ent->v.absmax[1] += 1;
		ent->v.absmax[2] += 1;
	}
	
// link to PVS leafs
	ent->num_leafs = 0;
	if (ent->v.modelindex)
		SV_FindTouchedLeafs (ent, sv.worldmodel->nodes);

	if (ent->v.solid == SOLID_NOT)
		return;

// find the first node that the ent's box crosses
	node = sv_areanodes;
	while (1)
	{
		if (node->axis == -1)
			break;
		if (ent->v.absmin[node->axis] > node->dist)
			node = node->children[0];
		else if (ent->v.absmax[node->axis] < node->dist)
			node = node->children[1];
		else
			break;		// crosses the node
	}
	
// link it in	

	if (ent->v.solid == SOLID_TRIGGER)
		InsertLinkBefore (&ent->area, &node->trigger_edicts);
	else
		InsertLinkBefore (&ent->area, &node->solid_edicts);
	
// if touch_triggers, touch all entities at this node and decend for more
	if (touch_triggers)
		SV_TouchLinks ( ent, sv_areanodes );
}



/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

#if	!id386

/*
==================
SV_HullPointContents

==================
*/
int SV_HullPointContents (hull_t *hull, int num, vec3_t p)
{
	float		d;
	dclipnode_t	*node;
	mplane_t	*plane;

	while (num >= 0)
	{
		if (num < hull->firstclipnode || num > hull->lastclipnode)
			Sys_Error ("SV_HullPointContents: bad node number");
	
		node = hull->clipnodes + num;
		plane = hull->planes + node->planenum;
		
		if (plane->type < 3)
			d = p[plane->type] - plane->dist; // axis-aligned plane
		else
			d = DotProduct (plane->normal, p) - plane->dist; // distance to plane
		if (d < 0)
			num = node->children[1]; // in back of plane
		else
			num = node->children[0]; // in front of plane
	}
	
	return num;
}

#endif	// !id386


/*
==================
SV_PointContents

==================
*/
int SV_PointContents (vec3_t p)
{
	int		cont;

	cont = SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
	if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN)
		cont = CONTENTS_WATER;
	return cont;
}

int SV_TruePointContents (vec3_t p)
{
	return SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
}

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
edict_t	*SV_TestEntityPosition (edict_t *ent)
{
	trace_t	trace;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);
	
	if (trace.startsolid)
		return sv.edicts;
		
	return NULL;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125)



typedef struct {
	int num;
	float p1f, p2f;
	vec3_t p1, p2;

	float		t1, t2;
	float		frac;
	vec3_t		mid;
	int			side;
	float		midf;

	dclipnode_t	*node;
	mplane_t	*plane;

	int addr; // address of return
} param_t;


typedef struct
{
	const dclipnode_t* clipnodes;
	const mplane_t* planes;

	int d;
	param_t p[512];
} world_t;

static world_t  sworld;


static void push(world_t* pworld, const param_t* param)
{
	param_t* p = pworld->p;
	p[pworld->d].num = param->num;
	p[pworld->d].p1f = param->p1f;
	p[pworld->d].p2f = param->p2f;
	VectorCopy(param->p1, p[pworld->d].p1);
	VectorCopy(param->p2, p[pworld->d].p2);
	p[pworld->d].t1 = param->t1;
	p[pworld->d].t2 = param->t2;
	p[pworld->d].frac = param->frac;
	VectorCopy(param->mid, p[pworld->d].mid);
	p[pworld->d].side = param->side;
	p[pworld->d].midf = param->midf;
	p[pworld->d].node = param->node;
	p[pworld->d].plane = param->plane;
	p[pworld->d].addr = param->addr;
	pworld->d++;
}

static void pop(world_t* pworld, param_t* param)
{
	pworld->d--;

	const param_t* p = pworld->p;
	param->num = p[pworld->d].num;
	param->p1f = p[pworld->d].p1f;
	param->p2f = p[pworld->d].p2f;
	VectorCopy(p[pworld->d].p1, param->p1);
	VectorCopy(p[pworld->d].p2, param->p2);
	param->t1 = p[pworld->d].t1;
	param->t2 = p[pworld->d].t2;
	param->frac = p[pworld->d].frac;
	VectorCopy(p[pworld->d].mid, param->mid);
	param->side = p[pworld->d].side;
	param->midf = p[pworld->d].midf;
	param->node = p[pworld->d].node;
	param->plane = p[pworld->d].plane;
	param->addr = p[pworld->d].addr;
}

/*
==================
SV_RecursiveHullCheck

==================
*/

qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	dclipnode_t	*node;
	mplane_t	*plane;
	float		t1, t2;
	float		frac;
	int			i;
	vec3_t		mid;
	int			side;
	float		midf;

// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;

		trace->content = num;
/*		
		// extra check for sky
		if (cl.worldmodel && trace->content >= hull->firstclipnode && trace->content <= hull->lastclipnode) {
			mnode_t* mnode = cl.worldmodel->nodes + trace->content;
			if (-CONTENTS_SKY == mnode->contents) {
				//if ( check_sky_content(p1, p2, trace->endpos) )
					trace->content = CONTENTS_SKY;
			}
		}
		if (trace->content != CONTENTS_SKY) trace->content = num;
*/		
		return true;		// empty
	}

	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error ("SV_RecursiveHullCheck: bad node number");

	//trace->content = num; // use 'content' to record parent node
//
// find the point distances
//
	node = hull->clipnodes + num;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProduct (plane->normal, p1) - plane->dist;
		t2 = DotProduct (plane->normal, p2) - plane->dist;
	}
	
#if 1
	if (t1 >= 0 && t2 >= 0)
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0 && t2 < 0)
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#else
	if ( (t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0) )
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if ( (t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0) )
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#endif

// put the crosspoint DIST_EPSILON pixels on the near side
	if (t1 < 0)
		frac = (t1 + DIST_EPSILON)/(t1-t2);
	else
		frac = (t1 - DIST_EPSILON)/(t1-t2);
	if (frac < 0)
		frac = 0;
	if (frac > 1)
		frac = 1;
		
	midf = p1f + (p2f - p1f)*frac;
	for (i=0 ; i<3 ; i++)
		mid[i] = p1[i] + frac*(p2[i] - p1[i]);

	side = (t1 < 0);

// move up to the node
	if (!SV_RecursiveHullCheck (hull, node->children[side], p1f, midf, p1, mid, trace) )
		return false;

#ifdef PARANOID
	if (SV_HullPointContents (sv_hullmodel, mid, node->children[side])
	== CONTENTS_SOLID)
	{
		Con_Printf ("mid PointInHullSolid\n");
		return false;
	}
#endif
	
	if (SV_HullPointContents (hull, node->children[side^1], mid)
	!= CONTENTS_SOLID)
// go past the node
		return SV_RecursiveHullCheck (hull, node->children[side^1], midf, p2f, mid, p2, trace);
	
	if (trace->allsolid)
		return false;		// never got out of the solid area
		
//==================
// the other side of the node is solid, this is the impact point
//==================
	if (!side)
	{
		VectorCopy (plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		VectorSubtract (vec3_origin, plane->normal, trace->plane.normal);
		trace->plane.dist = -plane->dist;
	}
	
	while (SV_HullPointContents (hull, hull->firstclipnode, mid)
	== CONTENTS_SOLID)
	{ // shouldn't really happen, but does occasionally
		frac -= 0.1;
		if (frac < 0)
		{
			trace->fraction = midf;
			VectorCopy (mid, trace->endpos);
			Con_DPrintf ("backup past 0\n");
			return false;
		}
		midf = p1f + (p2f - p1f)*frac;
		for (i=0 ; i<3 ; i++)
			mid[i] = p1[i] + frac*(p2[i] - p1[i]);
	}
	
	trace->fraction = midf;
	VectorCopy (mid, trace->endpos);

	return false;
}



qboolean GO_SV_RecursiveHullCheck(hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	world_t* pworld = &sworld;
	pworld->clipnodes = hull->clipnodes;
	pworld->planes = hull->planes;
	pworld->d = 0;

	param_t p = { num, p1f, p2f }; VectorCopy(p1, p.p1); VectorCopy(p2, p.p2);

	qboolean result;

begincall:

	// check for empty
	if (p.num < 0) {
		if (p.num != CONTENTS_SOLID) {
			trace->allsolid = false;
			if (p.num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;

		trace->content = p.num;

		//return true;
		result = true; goto endcall;
	}

	if (p.num < hull->firstclipnode || p.num > hull->lastclipnode)
		Sys_Error("SV_RecursiveHullCheck: bad node number");

	// find the point distances
	p.node = pworld->clipnodes + p.num;
	p.plane = pworld->planes + p.node->planenum;

	if (p.plane->type < 3) {
		p.t1 = p.p1[p.plane->type] - p.plane->dist;
		p.t2 = p.p2[p.plane->type] - p.plane->dist;
	}
	else {
		p.t1 = DotProduct(p.plane->normal, p.p1) - p.plane->dist;
		p.t2 = DotProduct(p.plane->normal, p.p2) - p.plane->dist;
	}


	if (p.t1 >= 0 && p.t2 >= 0) {
		//return RecursiveHullCheck (pworld, node->children[0], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 1;
		push(pworld, &p);
		p.num = p.node->children[0];
		goto begincall;
	returncall1:
		goto endcall;
	}
	if (p.t1 < 0 && p.t2 < 0) {
		//return RecursiveHullCheck (pworld, node->children[1], p.p1f, p.p2f, p.p1, p.p2, trace);
		p.addr = 2;
		push(pworld, &p);
		p.num = p.node->children[1];
		goto begincall;
	returncall2:
		goto endcall;
	}

	// put the crosspoint DIST_EPSILON pixels on the near side
	if (p.t1 < 0)
		p.frac = (p.t1 + DIST_EPSILON) / (p.t1 - p.t2);
	else
		p.frac = (p.t1 - DIST_EPSILON) / (p.t1 - p.t2);
	if (p.frac < 0)
		p.frac = 0;
	if (p.frac > 1)
		p.frac = 1;

	p.midf = p.p1f + (p.p2f - p.p1f)*p.frac;
	for (int i = 0; i<3; i++)
		p.mid[i] = p.p1[i] + p.frac*(p.p2[i] - p.p1[i]);

	p.side = (p.t1 < 0);

	// move up to the node
	//	if (!RecursiveHullCheck (pworld, node->children[p.side], p.p1f, p.midf, p.p1, p.mid, trace) )
	//		return false;
	p.addr = 3;
	push(pworld, &p);
	p.num = p.node->children[p.side];
	p.p2f = p.midf;
	VectorCopy(p.mid, p.p2);
	goto begincall;

returncall3:
	if (!result) {
		result = false; goto endcall;
	}

	if (SV_HullPointContents(hull, p.node->children[p.side ^ 1], p.mid) != CONTENTS_SOLID) {
		//return RecursiveHullCheck (pworld, node->children[p.side^1], p.midf, p.p2f, p.mid, p.p2, trace); // go past the node
		p.addr = 4;
		push(pworld, &p);
		p.num = p.node->children[p.side ^ 1];
		p.p1f = p.midf;
		VectorCopy(p.mid, p.p1);
		goto begincall;
	returncall4:
		goto endcall;
	}

	if (trace->allsolid) {
		//return false;		// never got out of the solid area
		result = false; goto endcall;
	}

	//==================
	// the other side of the node is solid, this is the impact point
	//==================
	if (!p.side) {
		VectorCopy(p.plane->normal, trace->plane.normal);
		trace->plane.dist = p.plane->dist;
	}
	else {
		VectorScale(p.plane->normal, -1, trace->plane.normal);
		trace->plane.dist = -p.plane->dist;
	}

	trace->fraction = p.midf;
	VectorCopy(p.mid, trace->endpos);

	//return false;
	result = false; goto endcall;


endcall: // exit function
	if (0 == pworld->d) return result;
	pop(pworld, &p);
	switch (p.addr) {
	case 1: goto returncall1;
	case 2: goto returncall2;
	case 3: goto returncall3;
	case 4: goto returncall4;
	}

}



qboolean WHILE__SV_RecursiveHullCheck(hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	world_t* pworld = &sworld;
	pworld->clipnodes = hull->clipnodes;
	pworld->planes = hull->planes;
	pworld->d = 0;

	param_t p = { num, p1f, p2f }; VectorCopy(p1, p.p1); VectorCopy(p2, p.p2);

	qboolean result;

	while (true) {

		// check for empty
		if (p.num < 0) {
			if (p.num != CONTENTS_SOLID) {
				trace->allsolid = false;
				if (p.num == CONTENTS_EMPTY)
					trace->inopen = true;
				else
					trace->inwater = true;
			}
			else
				trace->startsolid = true;

			trace->content = p.num;

			//return true;
			result = true;

			qboolean b = false;
			while (0 != pworld->d) { // endcall
				pop(pworld, &p);
				if (1 == p.addr) {
				} else if (2 == p.addr) {
				} else if (3 == p.addr) {
					if (!result) {
						result = false; continue;
					}

					if (SV_HullPointContents(hull, p.node->children[p.side ^ 1], p.mid) != CONTENTS_SOLID) {
						//return RecursiveHullCheck (pworld, node->children[p.side^1], p.midf, p.p2f, p.mid, p.p2, trace); // go past the node
						p.addr = 4;
						push(pworld, &p);
						p.num = p.node->children[p.side ^ 1];
						p.p1f = p.midf;
						VectorCopy(p.mid, p.p1);
						b = true;
						break;
					}

					if (trace->allsolid) {
						//return false;		// never got out of the solid area
						result = false; continue;
					}

					//==================
					// the other side of the node is solid, this is the impact point
					//==================
					if (!p.side) {
						VectorCopy(p.plane->normal, trace->plane.normal);
						trace->plane.dist = p.plane->dist;
					}
					else {
						VectorScale(p.plane->normal, -1, trace->plane.normal);
						trace->plane.dist = -p.plane->dist;
					}

					trace->fraction = p.midf;
					VectorCopy(p.mid, trace->endpos);

					//return false;
					result = false; continue;
				} else if (4 == p.addr) {
				}

			} // of while (0 != pworld->d)
			if (b) continue; else break;

		} // of if (p.num < 0)


		if (p.num < hull->firstclipnode || p.num > hull->lastclipnode)
			Sys_Error("SV_RecursiveHullCheck: bad node number");

		// find the point distances
		p.node = pworld->clipnodes + p.num;
		p.plane = pworld->planes + p.node->planenum;

		if (p.plane->type < 3) {
			p.t1 = p.p1[p.plane->type] - p.plane->dist;
			p.t2 = p.p2[p.plane->type] - p.plane->dist;
		}
		else {
			p.t1 = DotProduct(p.plane->normal, p.p1) - p.plane->dist;
			p.t2 = DotProduct(p.plane->normal, p.p2) - p.plane->dist;
		}


		if (p.t1 >= 0 && p.t2 >= 0) {
			//return RecursiveHullCheck (pworld, node->children[0], p.p1f, p.p2f, p.p1, p.p2, trace);
			p.addr = 1;
			push(pworld, &p);
			p.num = p.node->children[0];
			continue;
		}
		if (p.t1 < 0 && p.t2 < 0) {
			//return RecursiveHullCheck (pworld, node->children[1], p.p1f, p.p2f, p.p1, p.p2, trace);
			p.addr = 2;
			push(pworld, &p);
			p.num = p.node->children[1];
			continue;
		}

		// put the crosspoint DIST_EPSILON pixels on the near side
		if (p.t1 < 0)
			p.frac = (p.t1 + DIST_EPSILON) / (p.t1 - p.t2);
		else
			p.frac = (p.t1 - DIST_EPSILON) / (p.t1 - p.t2);
		if (p.frac < 0)
			p.frac = 0;
		if (p.frac > 1)
			p.frac = 1;

		p.midf = p.p1f + (p.p2f - p.p1f)*p.frac;
		for (int i = 0; i<3; i++)
			p.mid[i] = p.p1[i] + p.frac*(p.p2[i] - p.p1[i]);

		p.side = (p.t1 < 0);

		// move up to the node
		//	if (!RecursiveHullCheck (pworld, node->children[p.side], p.p1f, p.midf, p.p1, p.mid, trace) )
		//		return false;
		p.addr = 3;
		push(pworld, &p);
		p.num = p.node->children[p.side];
		p.p2f = p.midf;
		VectorCopy(p.mid, p.p2);
		continue;


	} // of while (true)

	return result;
}




/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
trace_t SV_ClipMoveToEntity (int thread, edict_t *ent, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t		trace;
	vec3_t		offset;
	vec3_t		start_l, end_l;
	hull_t		*hull;

// fill in a default trace
	memset (&trace, 0, sizeof(trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy (end, trace.endpos);

// get the clipping hull
	hull = SV_HullForEntity (thread, ent, mins, maxs, offset);

	VectorSubtract (start, offset, start_l);
	VectorSubtract (end, offset, end_l);

#ifdef QUAKE2
	// rotate start and end into the models frame of reference
	if (ent->v.solid == SOLID_BSP && 
	(ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
	{
		vec3_t	a;
		vec3_t	forward, right, up;
		vec3_t	temp;

		AngleVectors (ent->v.angles, forward, right, up);

		VectorCopy (start_l, temp);
		start_l[0] = DotProduct (temp, forward);
		start_l[1] = -DotProduct (temp, right);
		start_l[2] = DotProduct (temp, up);

		VectorCopy (end_l, temp);
		end_l[0] = DotProduct (temp, forward);
		end_l[1] = -DotProduct (temp, right);
		end_l[2] = DotProduct (temp, up);
	}
#endif

// trace a line through the apropriate clipping hull
	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

#ifdef QUAKE2
	// rotate endpos back to world frame of reference
	if (ent->v.solid == SOLID_BSP && 
	(ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
	{
		vec3_t	a;
		vec3_t	forward, right, up;
		vec3_t	temp;

		if (trace.fraction != 1)
		{
			VectorSubtract (vec3_origin, ent->v.angles, a);
			AngleVectors (a, forward, right, up);

			VectorCopy (trace.endpos, temp);
			trace.endpos[0] = DotProduct (temp, forward);
			trace.endpos[1] = -DotProduct (temp, right);
			trace.endpos[2] = DotProduct (temp, up);

			VectorCopy (trace.plane.normal, temp);
			trace.plane.normal[0] = DotProduct (temp, forward);
			trace.plane.normal[1] = -DotProduct (temp, right);
			trace.plane.normal[2] = DotProduct (temp, up);
		}
	}
#endif

// fix trace up by the offset
	if (trace.fraction != 1)
		VectorAdd (trace.endpos, offset, trace.endpos);

// did we clip the move?
	if (trace.fraction < 1 || trace.startsolid  )
		trace.ent = ent;

	return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
void SV_ClipToLinks (int thread, areanode_t *node, moveclip_t *clip )
{
	link_t		*l, *next;
	edict_t		*touch;
	trace_t		trace;

	if (!node || !node->solid_edicts.next) return;

// touch linked edicts
	for (l = node->solid_edicts.next ; l != &node->solid_edicts ; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch->v.solid == SOLID_NOT)
			continue;
		if (touch == clip->passedict)
			continue;
		if (touch->v.solid == SOLID_TRIGGER)
			Sys_Error ("Trigger in clipping list");

		if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP)
			continue;

		if (clip->boxmins[0] > touch->v.absmax[0]
		|| clip->boxmins[1] > touch->v.absmax[1]
		|| clip->boxmins[2] > touch->v.absmax[2]
		|| clip->boxmaxs[0] < touch->v.absmin[0]
		|| clip->boxmaxs[1] < touch->v.absmin[1]
		|| clip->boxmaxs[2] < touch->v.absmin[2] )
			continue;

		if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0])
			continue;	// points never interact

	// might intersect, so do an exact clip
		if (clip->trace.allsolid)
			return;
		if (clip->passedict)
		{
		 	if (PROG_TO_EDICT(touch->v.owner) == clip->passedict)
				continue;	// don't clip against own missiles
			if (PROG_TO_EDICT(clip->passedict->v.owner) == touch)
				continue;	// don't clip against owner
		}

		if ((int)touch->v.flags & FL_MONSTER)
			trace = SV_ClipMoveToEntity (thread, touch, clip->start, clip->mins2, clip->maxs2, clip->end);
		else
			trace = SV_ClipMoveToEntity (thread, touch, clip->start, clip->mins, clip->maxs, clip->end);
		if (trace.allsolid || trace.startsolid ||
		trace.fraction < clip->trace.fraction)
		{
			trace.ent = touch;
		 	if (clip->trace.startsolid)
			{
				clip->trace = trace;
				clip->trace.startsolid = true;
			}
			else
				clip->trace = trace;
		}
		else if (trace.startsolid)
			clip->trace.startsolid = true;
	}
	
// recurse down both sides
	if (node->axis == -1)
		return;

	if ( clip->boxmaxs[node->axis] > node->dist )
		SV_ClipToLinks (thread, node->children[0], clip );
	if ( clip->boxmins[node->axis] < node->dist )
		SV_ClipToLinks (thread, node->children[1], clip );
}


/*
==================
SV_MoveBounds
==================
*/
void SV_MoveBounds (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
#if 0
// debug to test against everything
boxmins[0] = boxmins[1] = boxmins[2] = -9999;
boxmaxs[0] = boxmaxs[1] = boxmaxs[2] = 9999;
#else
	int		i;
	
	for (i=0 ; i<3 ; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
#endif
}



/*
==================
SV_Move
==================
*/
trace_t SV_Move (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type, edict_t *passedict)
{
	moveclip_t	clip;
	int			i;

	memset ( &clip, 0, sizeof ( moveclip_t ) );

// clip to world
	clip.trace = SV_ClipMoveToEntity (0, sv.edicts, start, mins, maxs, end );

	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.type = type;
	clip.passedict = passedict;

	if (type == MOVE_MISSILE)
	{
		for (i=0 ; i<3 ; i++)
		{
			clip.mins2[i] = -15;
			clip.maxs2[i] = 15;
		}
	}
	else
	{
		VectorCopy (mins, clip.mins2);
		VectorCopy (maxs, clip.maxs2);
	}
	
// create the bounding box of the entire move
	SV_MoveBounds ( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

// clip to entities
	SV_ClipToLinks ( 0, sv_areanodes, &clip );

	return clip.trace;
}


