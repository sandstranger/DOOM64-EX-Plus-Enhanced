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
//
// DESCRIPTION:
//        Game completion, final screen animation.
//
//-----------------------------------------------------------------------------

#include <ctype.h>
#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "d_englsh.h"
#include "sounds.h"
#include "doomstat.h"
#include "t_bsp.h"
#include "g_local.h"
#include "info.h"
#include "r_local.h"
#include "st_stuff.h"
#include "r_wipe.h"
#include "gl_draw.h"

CVAR_EXTERNAL(m_reworkedzombieman);
CVAR_EXTERNAL(m_reworkedzombieshotgun);
CVAR_EXTERNAL(m_reworkedimp);
CVAR_EXTERNAL(m_reworkedpinkyandspectre);
CVAR_EXTERNAL(m_reworkedBaronofHell);
CVAR_EXTERNAL(m_reworkedHellKnight);

static int          castrotation = 0;
static int          castnum;
static int     casttics;
static state_t* caststate;
static boolean     castdeath;
static boolean     castdying;
static int          castframes;
static int          castonmelee;
static boolean     castattacking;
static dPalette_t   finalePal;

typedef struct {
	char* name;
	mobjtype_t type;
} castinfo_t;

castinfo_t originalorder[] = {
	{   CC_ZOMBIE,  MT_POSSESSED1   },
	{   CC_SHOTGUN, MT_POSSESSED2   },
	{   CC_IMP,     MT_IMP1         },
	{   CC_IMP2,    MT_IMP2         },
	{   CC_DEMON,   MT_DEMON1       },
	{   CC_DEMON2,  MT_DEMON2       },
	{   CC_LOST,    MT_SKULL        },
	{   CC_CACO,    MT_CACODEMON    },
	{   CC_HELL,    MT_BRUISER2     },
	{   CC_BARON,   MT_BRUISER1     },
	{   CC_ARACH,   MT_BABY         },
	{   CC_PAIN,    MT_PAIN         },
	{   CC_MANCU,   MT_MANCUBUS     },
	{   CC_CYBER,   MT_CYBORG       },
	{   CC_HERO,    MT_PLAYER       },
	{   NULL,       0               }
};

castinfo_t extendedorder[] = {
	{   CC_ZOMBIE,  MT_POSSESSED1   },
	{   CC_SHOTGUN, MT_POSSESSED2   },
	{   CC_HEAVY,   MT_CHAINGUY     },
	{   CC_IMP,     MT_IMP1         },
	{   CC_IMP2,    MT_IMP2         },
	{   CC_DEMON,   MT_DEMON1       },
	{   CC_DEMON2,  MT_DEMON2       },
	{   CC_LOST,    MT_SKULL        },
	{   CC_CACO,    MT_CACODEMON    },
	{   CC_HELL,    MT_BRUISER2     },
	{   CC_BARON,   MT_BRUISER1     },
	{   CC_ARACH,   MT_BABY         },
	{   CC_PAIN,    MT_PAIN         },
	{   CC_REVEN,   MT_UNDEAD       },
	{   CC_MANCU,   MT_MANCUBUS     },
	{   CC_HELLHOUND,   MT_HELLHOUND     },
	{   CC_ARCH,    MT_VILE         },
	{   CC_SPIDER,  MT_SPIDER       },
	{   CC_CYBER,   MT_CYBORG       },
	{   CC_ANNIHILATOR,   MT_ANNIHILATOR       },
	{   CC_MOTH,    MT_RESURRECTOR  },
	{   CC_HERO,    MT_PLAYER       },
	{   NULL,       0               }
};

castinfo_t *castorder;

CVAR(m_extendedcast, 0);

//
// F_Start
//

void F_Start(void) {
	castorder = m_extendedcast.value > 0 ? extendedorder : originalorder;

	gameaction = ga_nothing;
	automapactive = false;

	castnum = 0;
	caststate = &states[mobjinfo[castorder[castnum].type].seestate];
	casttics = caststate->info_tics;
	castdeath = false;
	castframes = 0;
	castonmelee = 0;
	castattacking = false;
	castdying = false;
	
	finalePal.a = 255;
	finalePal.r = 0;
	finalePal.g = 0;
	finalePal.b = 0;

	// hack - force-play seesound from first cast
	S_StartSound(NULL, mobjinfo[castorder[castnum].type].seesound);
}

//
// F_Stop
//

void F_Stop(void) {
	S_StopMusic();
	//gameaction = ga_nothing;
	WIPE_FadeScreen(6);
}

//
// F_Ticker
//

int F_Ticker(void) {
	int st;
	playercontrols_t* pc = &Controls;

	if (!castdeath) {
		if (pc->key[PCKEY_LEFT]) {
			castrotation = (castrotation + 1) & 7;
			pc->key[PCKEY_LEFT] = 0;
		}
		else if (pc->key[PCKEY_RIGHT]) {
			castrotation = (castrotation - 1) & 7;
			pc->key[PCKEY_RIGHT] = 0;
		}
		else if (players[consoleplayer].cmd.buttons) {
			castdying = true;
		}
	}

	finalePal.r += 2;
	finalePal.g += 2;
	finalePal.b += 2;
	finalePal.r = MIN(finalePal.r, 250);
	finalePal.g = MIN(finalePal.g, 250);
	finalePal.b = MIN(finalePal.b, 250);

	if (!castdeath && castdying) {
		S_StartSound(NULL, sfx_shotgun);
		S_StartSound(NULL, mobjinfo[castorder[castnum].type].deathsound);
		caststate = &states[mobjinfo[castorder[castnum].type].deathstate];
		casttics = caststate->info_tics;
		castframes = 0;
		castattacking = false;
		castdying = false;
		castdeath = true;
	}
	
	// advance state
	if (--casttics > 0) {
		return 0;    // not time to change state yet
	}

	if (caststate->info_tics == -1 || caststate->nextstate == S_NULL) {
		// switch from deathstate to next monster

		castnum++;
		castdeath = false;
		castrotation = 0;
		if (castorder[castnum].name == NULL) {
			castnum = 0;
		}

		finalePal.a = 255;
		finalePal.r = 0;
		finalePal.g = 0;
		finalePal.b = 0;

		if (mobjinfo[castorder[castnum].type].seesound) {
			S_StartSound(NULL, mobjinfo[castorder[castnum].type].seesound);
		}

		caststate = &states[mobjinfo[castorder[castnum].type].seestate];
		castframes = 0; 
	}
	else {
		// just advance to next state in animation

		if (caststate == &states[S_PLAY_ATK2]) { // gross hack..
			goto stopattack;
		}

		st = caststate->nextstate;
		caststate = &states[st];
		castframes++;

		// sound hacks
		{
			int sound = 0;

			switch (st) {
			case S_PLAY_ATK2:                           // player
				sound = sfx_sht2fire;
				break;
			case S_SARG_ATK2:                           // demon
			case S_SARG2_ATK2:                          // spectre
				sound = sfx_sargatk;
				break;
			case S_FATT_ATK2:                           // mancubus
			case S_FATT_ATK4:                           // mancubus
			case S_FATT_ATK6:                           // mancubus
			case S_TROO_ATK2:                           // imp
			case S_TROO2_ATK2:                          // imp nightmare
			case S_HEAD_ATK3:                           // cacodemon
			case S_BOSS1_ATK3:                          // hell knight
			case S_BOSS2_ATK3:                          // baron
			case S_GECH_ATK3:                           // hellhound
				sound = sfx_bdmissile;
				break;
			case S_TROO_MELEE3:                         // imp scratch
			case S_TROO2_MELEE3:                        // imp nightmare scratch
				sound = sfx_scratch;
				break;
			case S_POSS1_ATK2:                          // former human
			case S_CPOS_ATK2:							// chaingun guy
			case S_CPOS_ATK3:
			case S_CPOS_ATK4:
			case S_SPID_ATK2:							// spider demon
			case S_SPID_ATK4:
			case S_SPID_ATK6:
				sound = sfx_pistol;
				break;
			case S_POSS2_ATK2:                          // shotgun guy
				sound = sfx_shotgun;
				break;
			case S_SKUL_ATK2:                           // skull
			case S_PAIN_ATK4:                           // pain
				sound = sfx_skullatk;
				break;
			case S_BSPI_ATK2:                           // arachnotron
				sound = sfx_plasma;
				break;
			case S_CYBR_ATK2:                           // cyberdemon
			case S_CYBR_ATK4:
			case S_CYBR_ATK6:
			case S_A64A_ATK2:                           // annihilator
			case S_A64A_ATK4:
			case S_A64A_ATK6:
				sound = sfx_missile;
				break;
			case S_SKEL_FIST2:
				sound = sfx_dart;
				break;
			case S_SKEL_FIST4:
				sound = sfx_dartshoot;
				break;
			case S_SKEL_MISS3:
			case S_RECT_ATK3:
				sound = sfx_tracer;
				break;
			case S_VILE_ATK2:
				sound = sfx_vilatk;
				break;
			case S_GECH_MELEE3:                         // hellhound melee
				sound = sfx_hellhoundatk;
				break;
			default:
				sound = 0;
				break;
			}

			if (sound) {
				S_StartSound(NULL, sound);
			}
		}
	}

	if (castframes == 12) {
		// go into attack frame
		castattacking = true;
		if (castonmelee) {
			caststate = &states[mobjinfo[castorder[castnum].type].meleestate];
		}
		else {
			caststate = &states[mobjinfo[castorder[castnum].type].missilestate];
		}
		castonmelee ^= 1;

		if (caststate == &states[S_NULL]) {
			if (castonmelee) {
				caststate = &states[mobjinfo[castorder[castnum].type].meleestate];
			}
			else {
				caststate = &states[mobjinfo[castorder[castnum].type].missilestate];
			}
		}
	}

	if (castattacking) {
		if (castframes == 24 ||
			caststate == &states[mobjinfo[castorder[castnum].type].seestate]) {
		stopattack:
			castattacking = false;
			castframes = 0;
			caststate = &states[mobjinfo[castorder[castnum].type].seestate];
		}
	}

	casttics = caststate->info_tics;
	if (casttics == -1) {
		casttics = TICRATE;
	}
	
	return 0;
}

//
// F_Drawer
//

void F_Drawer(void) {
	GL_ClearView(0xFF000000);
	Draw_GfxImageInter(64, 30, "EVIL", D_RGBA(255, 255, 255, 0xff), false);
	Draw_BigText(-1, 240 - 32, D_RGBA(255, 0, 0, 0xff), castorder[castnum].name);
	Draw_Sprite2D(
		caststate->sprite,
		castrotation,
		(caststate->info_frame & FF_FRAMEMASK),
		160,
		180,
		1.0f,
		mobjinfo[castorder[castnum].type].palette,
		D_RGBA(finalePal.r, finalePal.g, finalePal.b, finalePal.a)
	);

	// Styd: option to change some vanilla monster sprites to reworked vanilla monster sprites now supports final cast
	if (caststate == &states[S_POSS1_RUN1] || caststate == &states[S_POSS1_RUN2] || caststate == &states[S_POSS1_RUN3] || caststate == &states[S_POSS1_RUN4] || caststate == &states[S_POSS1_RUN5] || caststate == &states[S_POSS1_RUN6] || caststate == &states[S_POSS1_RUN7] || caststate == &states[S_POSS1_RUN8] || caststate == &states[S_POSS1_ATK1] || caststate == &states[S_POSS1_ATK2] || caststate == &states[S_POSS1_ATK3] || caststate == &states[S_POSS1_DIE1] || caststate == &states[S_POSS1_DIE2] || caststate == &states[S_POSS1_DIE3] || caststate == &states[S_POSS1_DIE4] || caststate == &states[S_POSS1_DIE5]) {
		if (m_reworkedzombieman.value == 1) {
			// reworked vanilla monsters sprites
		    caststate->sprite = SPR_POS1;
		}
		else if (m_reworkedzombieman.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_POSS;
		}
	}

	if (caststate == &states[S_POSS2_RUN1] || caststate == &states[S_POSS2_RUN2] || caststate == &states[S_POSS2_RUN3] || caststate == &states[S_POSS2_RUN4] || caststate == &states[S_POSS2_RUN5] || caststate == &states[S_POSS2_RUN6] || caststate == &states[S_POSS2_RUN7] || caststate == &states[S_POSS2_RUN8] || caststate == &states[S_POSS2_ATK1] || caststate == &states[S_POSS2_ATK2] || caststate == &states[S_POSS2_ATK3] || caststate == &states[S_POSS2_DIE1] || caststate == &states[S_POSS2_DIE2] || caststate == &states[S_POSS2_DIE3] || caststate == &states[S_POSS2_DIE4] || caststate == &states[S_POSS2_DIE5]) {
		if (m_reworkedzombieshotgun.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_POS2;
			mobjinfo[castorder[castnum].type].palette = 0;
		}
		else if (m_reworkedzombieshotgun.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_POSS;
			mobjinfo[castorder[castnum].type].palette = 1;
		}
	}

	if (caststate == &states[S_TROO_RUN1] || caststate == &states[S_TROO_RUN2] || caststate == &states[S_TROO_RUN3] || caststate == &states[S_TROO_RUN4] || caststate == &states[S_TROO_RUN5] || caststate == &states[S_TROO_RUN6] || caststate == &states[S_TROO_RUN7] || caststate == &states[S_TROO_RUN8] || caststate == &states[S_TROO_MELEE1] || caststate == &states[S_TROO_MELEE2] || caststate == &states[S_TROO_MELEE3] || caststate == &states[S_TROO_ATK1] || caststate == &states[S_TROO_ATK2] || caststate == &states[S_TROO_ATK3] || caststate == &states[S_TROO_DIE1] || caststate == &states[S_TROO_DIE2] || caststate == &states[S_TROO_DIE3] || caststate == &states[S_TROO_DIE4] || caststate == &states[S_TROO_DIE5]) {
		if (m_reworkedimp.value == 2) {
			// reworked vanilla monsters sprites
			// imp with a mouth
			caststate->sprite = SPR_TROM;
		}
		else if (m_reworkedimp.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_TRO1;
		}
		else if (m_reworkedimp.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_TROO;
		}
	}

	if (caststate == &states[S_TROO2_RUN1] || caststate == &states[S_TROO2_RUN2] || caststate == &states[S_TROO2_RUN3] || caststate == &states[S_TROO2_RUN4] || caststate == &states[S_TROO2_RUN5] || caststate == &states[S_TROO2_RUN6] || caststate == &states[S_TROO2_RUN7] || caststate == &states[S_TROO2_RUN8] || caststate == &states[S_TROO2_MELEE1] || caststate == &states[S_TROO2_MELEE2] || caststate == &states[S_TROO2_MELEE3] || caststate == &states[S_TROO2_ATK1] || caststate == &states[S_TROO2_ATK2] || caststate == &states[S_TROO2_ATK3] || caststate == &states[S_TROO2_DIE1] || caststate == &states[S_TROO2_DIE2] || caststate == &states[S_TROO2_DIE3] || caststate == &states[S_TROO2_DIE4] || caststate == &states[S_TROO2_DIE5]) {
		if (m_reworkedimp.value == 2) {
			// reworked vanilla monsters sprites
			// imp with a mouth
			caststate->sprite = SPR_TROM;
			mobjinfo[castorder[castnum].type].palette = 1;
		}
		else if (m_reworkedimp.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_TRO2;
			mobjinfo[castorder[castnum].type].palette = 0;
		}
		else if (m_reworkedimp.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_TROO;
			mobjinfo[castorder[castnum].type].palette = 1;
		}
	}

	if (caststate == &states[S_SARG_RUN1] || caststate == &states[S_SARG_RUN2] || caststate == &states[S_SARG_RUN3] || caststate == &states[S_SARG_RUN4] || caststate == &states[S_SARG_RUN5] || caststate == &states[S_SARG_RUN6] || caststate == &states[S_SARG_RUN7] || caststate == &states[S_SARG_RUN8] || caststate == &states[S_SARG_ATK1] || caststate == &states[S_SARG_ATK2] || caststate == &states[S_SARG_ATK3] || caststate == &states[S_SARG_DIE1] || caststate == &states[S_SARG_DIE2] || caststate == &states[S_SARG_DIE3] || caststate == &states[S_SARG_DIE4] || caststate == &states[S_SARG_DIE5] || caststate == &states[S_SARG_DIE6]) {
		if (m_reworkedpinkyandspectre.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_SAR1;
		}
		else if (m_reworkedpinkyandspectre.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_SARG;
		}
	}

	if (caststate == &states[S_SARG2_RUN0] || caststate == &states[S_SARG2_RUN1] || caststate == &states[S_SARG2_RUN2] || caststate == &states[S_SARG2_RUN3] || caststate == &states[S_SARG2_RUN4] || caststate == &states[S_SARG2_RUN5] || caststate == &states[S_SARG2_RUN6] || caststate == &states[S_SARG2_RUN7] || caststate == &states[S_SARG2_RUN8] || caststate == &states[S_SARG2_ATK0] || caststate == &states[S_SARG2_ATK1] || caststate == &states[S_SARG2_ATK2] || caststate == &states[S_SARG2_ATK3] || caststate == &states[S_SARG2_DIE0] || caststate == &states[S_SARG2_DIE1] || caststate == &states[S_SARG2_DIE2] || caststate == &states[S_SARG2_DIE3] || caststate == &states[S_SARG2_DIE4] || caststate == &states[S_SARG2_DIE5] || caststate == &states[S_SARG2_DIE6]) {
		if (m_reworkedpinkyandspectre.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_SAR2;
			mobjinfo[castorder[castnum].type].palette = 0;
		}
		else if (m_reworkedpinkyandspectre.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_SARG;
			mobjinfo[castorder[castnum].type].palette = 1;
		}
	}

	if (caststate == &states[S_BOSS1_RUN1] || caststate == &states[S_BOSS1_RUN2] || caststate == &states[S_BOSS1_RUN3] || caststate == &states[S_BOSS1_RUN4] || caststate == &states[S_BOSS1_RUN5] || caststate == &states[S_BOSS1_RUN6] || caststate == &states[S_BOSS1_RUN7] || caststate == &states[S_BOSS1_RUN8] || caststate == &states[S_BOSS1_ATK1] || caststate == &states[S_BOSS1_ATK2] || caststate == &states[S_BOSS1_ATK3] || caststate == &states[S_BOSS1_DIE1] || caststate == &states[S_BOSS1_DIE2] || caststate == &states[S_BOSS1_DIE3] || caststate == &states[S_BOSS1_DIE4] || caststate == &states[S_BOSS1_DIE5] || caststate == &states[S_BOSS1_DIE6]) {
		if (m_reworkedBaronofHell.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_BOS1;
			mobjinfo[castorder[castnum].type].palette = 0;
		}
		else if (m_reworkedBaronofHell.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_BOSS;
			mobjinfo[castorder[castnum].type].palette = 1;
		}
	}

	if (caststate == &states[S_BOSS2_RUN1] || caststate == &states[S_BOSS2_RUN2] || caststate == &states[S_BOSS2_RUN3] || caststate == &states[S_BOSS2_RUN4] || caststate == &states[S_BOSS2_RUN5] || caststate == &states[S_BOSS2_RUN6] || caststate == &states[S_BOSS2_RUN7] || caststate == &states[S_BOSS2_RUN8] || caststate == &states[S_BOSS2_ATK1] || caststate == &states[S_BOSS2_ATK2] || caststate == &states[S_BOSS2_ATK3] || caststate == &states[S_BOSS2_DIE1] || caststate == &states[S_BOSS2_DIE2] || caststate == &states[S_BOSS2_DIE3] || caststate == &states[S_BOSS2_DIE4] || caststate == &states[S_BOSS2_DIE5] || caststate == &states[S_BOSS2_DIE6]) {
		if (m_reworkedHellKnight.value == 1) {
			// reworked vanilla monsters sprites
			caststate->sprite = SPR_BOS2;
		}
		else if (m_reworkedHellKnight.value == 0) {
			// vanilla monsters sprites
			caststate->sprite = SPR_BOSS;
		}
	}
}
