// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1997 Id Software, Inc.
// Copyright(C) 2007-2012 Samuel Villarreal
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    Teleportation.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "s_sound.h"
#include "p_local.h"
#include "sounds.h"
#include "tables.h"
#include "doomstat.h"

//
// TELEPORTATION
//

//
// P_Telefrag
//

static void P_Telefrag(mobj_t* thing, fixed_t x, fixed_t y) {
	int     delta;
	int     size;
	mobj_t* m;

	for (m = mobjhead.next; m != &mobjhead; m = m->next) {
		if (!(m->flags & MF_SHOOTABLE)) {
			continue;
		}

		size = m->radius + thing->radius + 4 * FRACUNIT;

		delta = m->x - x;
		if (delta < -size || delta > size) {
			continue;
		}

		delta = m->y - y;
		if (delta < -size || delta > size) {
			continue;
		}

		P_DamageMobj(m, thing, thing, 10000);
		m->flags &= ~(MF_SOLID | MF_SHOOTABLE);
	}
}

//
// EV_Teleport
//

int EV_Teleport(line_t* line, mobj_t* thing) {
	int		    tag;
	boolean		flag;
	mobj_t* m, * fog;
	unsigned	an;
	fixed_t		oldx, oldy, oldz;
	int		    side;

	side = !P_PointOnLineSide(thing->x, thing->y, line);

	if (thing->flags & MF_MISSILE)
		return 0;		/* don't teleport missiles */

	if (side == 1)		/* don't teleport if hit back of line, */
		return 0;		/* so you can get out of teleporter */

	tag = line->tag;
	for (m = mobjhead.next; m != &mobjhead; m = m->next)
	{
		if (m->type != MT_DEST_TELEPORT)
			continue;		/* not a teleportman */

		if ((tag != m->tid))
			continue;   /* not matching the tid */

		oldx = thing->x;
		oldy = thing->y;
		oldz = thing->z;
		thing->flags |= MF_TELEPORT;
		numthingspec = 0;

		if (thing->player)
			P_Telefrag(thing, m->x, m->y);

		flag = P_TryMove(thing, m->x, m->y);
		thing->flags &= ~MF_TELEPORT;
		if (!flag)
			return 0;	/* move is blocked */
		thing->z = thing->floorz;

		/* spawn teleport fog at source and destination */
		fog = P_SpawnMobj(oldx, oldy, oldz + (thing->info->height >> 1), MT_TELEPORTFOG);
		S_StartSound(fog, sfx_telept);
		an = m->angle >> ANGLETOFINESHIFT;
		fog = P_SpawnMobj(m->x + 20 * finecosine[an], m->y + 20 * finesine[an]
			, thing->z + (thing->info->height >> 1), MT_TELEPORTFOG);
		S_StartSound(fog, sfx_telept);
		if (thing->player)
			thing->reactiontime = 9;	/* don't move for a bit */ //[psx] changed to 9
		thing->angle = m->angle;
		thing->momx = thing->momy = thing->momz = 0;
		return 1;
	}

	return 0;
}

//
// EV_SilentTeleport
//

int EV_SilentTeleport(line_t* line, mobj_t* thing) {
	int		    tag;
	mobj_t* m;
	unsigned	an;

	tag = line->tag;
	for (m = mobjhead.next; m != &mobjhead; m = m->next)
	{
		if (m->type != MT_DEST_TELEPORT)
			continue;		/* not a teleportman */

		if ((tag != m->tid))
			continue;   /* not matching the tid */

		thing->flags |= MF_TELEPORT;
		numthingspec = 0;

		if (thing->player)
			P_Telefrag(thing, m->x, m->y);

		P_TryMove(thing, m->x, m->y);
		thing->flags &= ~MF_TELEPORT;
		thing->z = m->z;
		thing->angle = m->angle;
		thing->momx = thing->momy = thing->momz = 0;
		return 1;
	}

	return 0;
}
