/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// cl_fx.c -- entity effects parsing and management

#include "client.h"

void CL_LogoutEffect (const vec3_t org, int type);
void CL_ItemRespawnParticles (const vec3_t org);

static vec3_t avelocities [NUMVERTEXNORMALS];

extern	struct model_s	*cl_mod_smoke;
extern	struct model_s	*cl_mod_flash;

/*
==============================================================

LIGHT STYLE MANAGEMENT

==============================================================
*/

typedef struct
{
	int		length;
	float	value[3];
	float	map[MAX_QPATH];
} clightstyle_t;

static clightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
static int	lastofs;

/*
================
CL_ClearLightStyles
================
*/
void CL_ClearLightStyles (void)
{
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	lastofs = -1;
}

/*
================
CL_RunLightStyles
================
*/
void CL_RunLightStyles (void)
{
	int		ofs;
	int		i;
	clightstyle_t	*ls;

	ofs = cl.time / 100;
	if (ofs == lastofs)
		return;
	lastofs = ofs;

	for (i=0,ls=cl_lightstyle ; i<MAX_LIGHTSTYLES ; i++, ls++)
	{
		if (!ls->length)
		{
			ls->value[0] = ls->value[1] = ls->value[2] = 1.0f;
			continue;
		}
		if (ls->length == 1)
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[0];
		else
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[ofs%ls->length];
	}
}


void CL_SetLightstyle (int index)
{
	char	*s;
	int		length, i;
	clightstyle_t	*dest;

	s = cl.configstrings[index + CS_LIGHTS];

	length = strlen(s);
	if (length >= MAX_QPATH)
		Com_Error (ERR_DROP, "svc_lightstyle length=%i", length);

	dest = &cl_lightstyle[index];
	dest->length = length;

	dest->map[0] = 1.0f;
	for (i = 0; i < length; i++) {
		dest->map[i] = ( float )( s[i] - 'a' ) / ( float )( 'm' - 'a' );
	}
}

/*
================
CL_AddLightStyles
================
*/
void CL_AddLightStyles (void)
{
	int		i;
	clightstyle_t	*ls;

	for (i = 0, ls = cl_lightstyle; i < MAX_LIGHTSTYLES; i++, ls++)
	{
		if ( Cvar_VariableValue("gl_timebasedfx") )
		{
			time_t			ctime;
			struct tm		*ltime;
			char			hour_s[5];
			int				hour_i, am, pmarray[12], j, k;
			float			brightness;

			ctime = time(NULL);
			ltime = localtime(&ctime);
			strftime(hour_s, sizeof(hour_s), "%H", ltime);
			if (hour_s[0] == '0') // trim zero off, otherwise int val. == 2
			{
				hour_s[0] = hour_s[1];
				hour_s[1] = 0;
			}
			hour_i = atoi(hour_s);

			// convert to 12-hour clock
			if ( hour_i <= 11 )		// AM
			{
				am = 1;
				if (hour_i == 0) // 0 = 12AM midnight
					hour_i = 12;
			}
			else					// PM
			{
				am = 0;
				if (hour_i > 12) // leave 12PM noon alone
					hour_i -= 12;

				// PM uses the 'reverse hour' of AM
				for (j = 1, k = 11; j < 12; j++, k--)
					pmarray[j] = k;
			}

			if ( am )
			{
				if (hour_i == 12) // midnight
					brightness = 0.15f;
				else
					brightness = ( ( (float) hour_i / 12.0f ) * 0.75f + 0.25f );
			}
			else
			{
				if (hour_i == 12) // noon
					brightness = 1.00f;
				else
					brightness = ( ( (float) pmarray[hour_i] / 12.0f ) * 0.75f + 0.25f );
			}
			
			ls->value[0] = brightness;
			ls->value[1] = brightness;
			ls->value[2] = brightness;
		}
		V_AddLightStyle (i, ls->value);
	}
}

/*
==============================================================

DLIGHT MANAGEMENT

==============================================================
*/

static cdlight_t	cl_dlights[MAX_DLIGHTS];

/*
================
CL_ClearDlights
================
*/
void CL_ClearDlights (void)
{
	memset (cl_dlights, 0, sizeof(cl_dlights));
}

/*
===============
CL_AllocDlight

===============
*/
cdlight_t *CL_AllocDlight (int key)
{
	int		i;
	cdlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				//memset (dl, 0, sizeof(*dl));
				dl->key = key;
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			//memset (dl, 0, sizeof(*dl));
			dl->key = key;
			return dl;
		}
	}

	dl = &cl_dlights[0];
	//memset (dl, 0, sizeof(*dl));
	dl->key = key;
	return dl;
}


/*
===============
CL_RunDLights

===============
*/
void CL_RunDLights (void)
{
	int			i;
	cdlight_t	*dl;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (!dl->radius)
			continue;
		
		if (dl->die < cl.time) {
			dl->radius = 0;
			return;
		}
		//dl->radius -= cls.frametime*dl->decay;
		if (dl->radius < 0.0f)
			dl->radius = 0.0f;
	}
}

/*
==============
CL_ParseMuzzleFlash
==============
*/
void CL_ParseMuzzleFlash (sizebuf_t *msg)
{
	vec3_t		fv, rv;
	cdlight_t	*dl;
	int			i, weapon, silenced;
	centity_t	*pl;
	float		volume;
	char		soundname[64];

	i = MSG_ReadShort (msg);
	if (i < 1 || i >= MAX_EDICTS)
		Com_Error (ERR_DROP, "CL_ParseMuzzleFlash: bad entity");

	weapon = MSG_ReadByte (msg);
	silenced = weapon & MZ_SILENCED;
	weapon &= ~MZ_SILENCED;

	pl = &cl_entities[i];

	dl = CL_AllocDlight (i);
	VectorCopy (pl->current.origin,  dl->origin);
	AngleVectors (pl->current.angles, fv, rv, NULL);
	VectorMA (dl->origin, 18, fv, dl->origin);
	VectorMA (dl->origin, 16, rv, dl->origin);
	if (silenced)
		dl->radius = 100 + (rand()&31);
	else
		dl->radius = 200 + (rand()&31);
	//dl->minlight = 32;
	dl->die = cl.time + 250;
	VectorClear(dl->color);
	//dl->decay = 0;

	if (silenced)
		volume = 0.2f;
	else
		volume = 1;

	switch (weapon)
	{
	case MZ_BLASTER:
		VectorSet(dl->color, 1, 0.5, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_BLUEHYPERBLASTER:
		VectorSet(dl->color, 0, 0, 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_HYPERBLASTER:
		VectorSet(dl->color, 1, 0.5, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_MACHINEGUN:
		VectorSet(dl->color, 1, 0.5, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
		break;
	case MZ_SHOTGUN:
		VectorSet(dl->color, 1, 0.5, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/shotgf1b.wav"), volume, ATTN_NORM, 0);
		S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/shotgr1b.wav"), volume, ATTN_NORM, 0.1f);
		break;
	case MZ_SSHOTGUN:
		VectorSet(dl->color, 1, 0.5, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/sshotf1b.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_CHAINGUN1:
		dl->radius = 200 + (rand()&31);
		VectorSet(dl->color, 1, 0.5, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
		break;
	case MZ_CHAINGUN2:
		dl->radius = 225 + (rand()&31);
		VectorSet(dl->color, 1, 0.5, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.05f);
		break;
	case MZ_CHAINGUN3:
		dl->radius = 250 + (rand()&31);
		VectorSet(dl->color, 1, 0.5, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.033f);
		Com_sprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.066f);
		break;
	case MZ_RAILGUN:
		VectorSet(dl->color, 0.3f, 0.5f, 1.0f);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/railgf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_ROCKET:
		VectorSet(dl->color, 1, 0.5f, 0.2f);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/rocklf1a.wav"), volume, ATTN_NORM, 0);
		S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/rocklr1b.wav"), volume, ATTN_NORM, 0.1f);
		break;
	case MZ_GRENADE:
		VectorSet(dl->color, 0.7, 0.5f, 0.3);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), volume, ATTN_NORM, 0);
		S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/grenlr1b.wav"), volume, ATTN_NORM, 0.1f);
		break;
	case MZ_BFG:
		//VectorSet(dl->color, 0, 1, 0);
		dl->radius = 0;  // muzzleflash happens long before effect shows up
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/bfg__f1y.wav"), volume, ATTN_NORM, 0);
		break;

	case MZ_LOGIN:
		VectorSet(dl->color, 0, 1, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	case MZ_LOGOUT:
		VectorSet(dl->color, 1, 0, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	case MZ_RESPAWN:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	// RAFAEL
	case MZ_PHALANX:
		VectorSet(dl->color, 1, 0.5f, 0.5f);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/plasshot.wav"), volume, ATTN_NORM, 0);
		break;
	// RAFAEL
	case MZ_IONRIPPER:
		VectorSet(dl->color, 1, 0.5f, 0.5f);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/rippfire.wav"), volume, ATTN_NORM, 0);
		break;

// ======================
// PGM
	case MZ_ETF_RIFLE:
		VectorSet(dl->color, 0.9f, 0.7f, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/nail1.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_SHOTGUN2:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/shotg2.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_HEATBEAM:
		VectorSet(dl->color, 1, 1, 0);
//		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/bfg__l1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_BLASTER2:
		VectorSet(dl->color, 0, 1, 0);
		// FIXME - different sound for blaster2 ??
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case MZ_TRACKER:
		// negative flashes handled the same in gl/soft until CL_AddDLights
		VectorSet(dl->color, -1, -1, -1);
		S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/disint2.wav"), volume, ATTN_NORM, 0);
		break;		
	case MZ_NUKE1:
		VectorSet(dl->color, 1, 0, 0);
		break;
	case MZ_NUKE2:
		VectorSet(dl->color, 1, 1, 0);
		break;
	case MZ_NUKE4:
		VectorSet(dl->color, 0, 0, 1);
		break;
	case MZ_NUKE8:
		VectorSet(dl->color, 0, 1, 1);
		break;
// PGM
// ======================
	}
}


/*
==============
CL_ParseMuzzleFlash2
==============
*/
void CL_ParseMuzzleFlash2 (sizebuf_t *msg) 
{
	int			ent, flash_number;
	cdlight_t	*dl;
	vec3_t		origin, forward, right;
	char		soundname[64];

	ent = MSG_ReadShort (msg);
	if (ent < 1 || ent >= MAX_EDICTS)
		Com_Error (ERR_DROP, "CL_ParseMuzzleFlash2: bad entity");

	flash_number = MSG_ReadByte (msg);
	if( flash_number == -1 ) {
		Com_Error (ERR_DROP, "CL_ParseMuzzleFlash2: read past end of message");
	}

	// locate the origin
	AngleVectors (cl_entities[ent].current.angles, forward, right, NULL);
	origin[0] = cl_entities[ent].current.origin[0] + forward[0] * monster_flash_offset[flash_number][0] + right[0] * monster_flash_offset[flash_number][1];
	origin[1] = cl_entities[ent].current.origin[1] + forward[1] * monster_flash_offset[flash_number][0] + right[1] * monster_flash_offset[flash_number][1];
	origin[2] = cl_entities[ent].current.origin[2] + forward[2] * monster_flash_offset[flash_number][0] + right[2] * monster_flash_offset[flash_number][1] + monster_flash_offset[flash_number][2];

	dl = CL_AllocDlight (ent);
	VectorCopy (origin,  dl->origin);
	VectorClear(dl->color);
	dl->radius = 200 + (rand()&31);
	//dl->minlight = 32;
	dl->die = cl.time + 250;	// + 0.1;
	//dl->decay = 0;

	switch (flash_number)
	{
	case MZ2_INFANTRY_MACHINEGUN_1:
	case MZ2_INFANTRY_MACHINEGUN_2:
	case MZ2_INFANTRY_MACHINEGUN_3:
	case MZ2_INFANTRY_MACHINEGUN_4:
	case MZ2_INFANTRY_MACHINEGUN_5:
	case MZ2_INFANTRY_MACHINEGUN_6:
	case MZ2_INFANTRY_MACHINEGUN_7:
	case MZ2_INFANTRY_MACHINEGUN_8:
	case MZ2_INFANTRY_MACHINEGUN_9:
	case MZ2_INFANTRY_MACHINEGUN_10:
	case MZ2_INFANTRY_MACHINEGUN_11:
	case MZ2_INFANTRY_MACHINEGUN_12:
	case MZ2_INFANTRY_MACHINEGUN_13:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_SOLDIER_MACHINEGUN_1:
	case MZ2_SOLDIER_MACHINEGUN_2:
	case MZ2_SOLDIER_MACHINEGUN_3:
	case MZ2_SOLDIER_MACHINEGUN_4:
	case MZ2_SOLDIER_MACHINEGUN_5:
	case MZ2_SOLDIER_MACHINEGUN_6:
	case MZ2_SOLDIER_MACHINEGUN_7:
	case MZ2_SOLDIER_MACHINEGUN_8:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_GUNNER_MACHINEGUN_1:
	case MZ2_GUNNER_MACHINEGUN_2:
	case MZ2_GUNNER_MACHINEGUN_3:
	case MZ2_GUNNER_MACHINEGUN_4:
	case MZ2_GUNNER_MACHINEGUN_5:
	case MZ2_GUNNER_MACHINEGUN_6:
	case MZ2_GUNNER_MACHINEGUN_7:
	case MZ2_GUNNER_MACHINEGUN_8:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("gunner/gunatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_ACTOR_MACHINEGUN_1:
	case MZ2_SUPERTANK_MACHINEGUN_1:
	case MZ2_SUPERTANK_MACHINEGUN_2:
	case MZ2_SUPERTANK_MACHINEGUN_3:
	case MZ2_SUPERTANK_MACHINEGUN_4:
	case MZ2_SUPERTANK_MACHINEGUN_5:
	case MZ2_SUPERTANK_MACHINEGUN_6:
	case MZ2_TURRET_MACHINEGUN:			// PGM
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_BOSS2_MACHINEGUN_L1:
	case MZ2_BOSS2_MACHINEGUN_L2:
	case MZ2_BOSS2_MACHINEGUN_L3:
	case MZ2_BOSS2_MACHINEGUN_L4:
	case MZ2_BOSS2_MACHINEGUN_L5:
	case MZ2_CARRIER_MACHINEGUN_L1:		// PMM
	case MZ2_CARRIER_MACHINEGUN_L2:		// PMM
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NONE, 0);
		break;

	case MZ2_SOLDIER_BLASTER_1:
	case MZ2_SOLDIER_BLASTER_2:
	case MZ2_SOLDIER_BLASTER_3:
	case MZ2_SOLDIER_BLASTER_4:
	case MZ2_SOLDIER_BLASTER_5:
	case MZ2_SOLDIER_BLASTER_6:
	case MZ2_SOLDIER_BLASTER_7:
	case MZ2_SOLDIER_BLASTER_8:
	case MZ2_TURRET_BLASTER:			// PGM
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_FLYER_BLASTER_1:
	case MZ2_FLYER_BLASTER_2:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("flyer/flyatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_MEDIC_BLASTER_1:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("medic/medatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_HOVER_BLASTER_1:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("hover/hovatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_FLOAT_BLASTER_1:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("floater/fltatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_SOLDIER_SHOTGUN_1:
	case MZ2_SOLDIER_SHOTGUN_2:
	case MZ2_SOLDIER_SHOTGUN_3:
	case MZ2_SOLDIER_SHOTGUN_4:
	case MZ2_SOLDIER_SHOTGUN_5:
	case MZ2_SOLDIER_SHOTGUN_6:
	case MZ2_SOLDIER_SHOTGUN_7:
	case MZ2_SOLDIER_SHOTGUN_8:
		VectorSet(dl->color, 1, 1, 0);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_TANK_BLASTER_1:
	case MZ2_TANK_BLASTER_2:
	case MZ2_TANK_BLASTER_3:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_TANK_MACHINEGUN_1:
	case MZ2_TANK_MACHINEGUN_2:
	case MZ2_TANK_MACHINEGUN_3:
	case MZ2_TANK_MACHINEGUN_4:
	case MZ2_TANK_MACHINEGUN_5:
	case MZ2_TANK_MACHINEGUN_6:
	case MZ2_TANK_MACHINEGUN_7:
	case MZ2_TANK_MACHINEGUN_8:
	case MZ2_TANK_MACHINEGUN_9:
	case MZ2_TANK_MACHINEGUN_10:
	case MZ2_TANK_MACHINEGUN_11:
	case MZ2_TANK_MACHINEGUN_12:
	case MZ2_TANK_MACHINEGUN_13:
	case MZ2_TANK_MACHINEGUN_14:
	case MZ2_TANK_MACHINEGUN_15:
	case MZ2_TANK_MACHINEGUN_16:
	case MZ2_TANK_MACHINEGUN_17:
	case MZ2_TANK_MACHINEGUN_18:
	case MZ2_TANK_MACHINEGUN_19:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Com_sprintf(soundname, sizeof(soundname), "tank/tnkatk2%c.wav", 'a' + rand() % 5);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound(soundname), 1, ATTN_NORM, 0);
		break;

	case MZ2_CHICK_ROCKET_1:
	case MZ2_TURRET_ROCKET:			// PGM
		VectorSet(dl->color, 1, 0.5f, 0.2f);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("chick/chkatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_TANK_ROCKET_1:
	case MZ2_TANK_ROCKET_2:
	case MZ2_TANK_ROCKET_3:
		VectorSet(dl->color, 1, 0.5f, 0.2f);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_SUPERTANK_ROCKET_1:
	case MZ2_SUPERTANK_ROCKET_2:
	case MZ2_SUPERTANK_ROCKET_3:
	case MZ2_BOSS2_ROCKET_1:
	case MZ2_BOSS2_ROCKET_2:
	case MZ2_BOSS2_ROCKET_3:
	case MZ2_BOSS2_ROCKET_4:
	case MZ2_CARRIER_ROCKET_1:
//	case MZ2_CARRIER_ROCKET_2:
//	case MZ2_CARRIER_ROCKET_3:
//	case MZ2_CARRIER_ROCKET_4:
		VectorSet(dl->color, 1, 0.5f, 0.2f);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/rocket.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_GUNNER_GRENADE_1:
	case MZ2_GUNNER_GRENADE_2:
	case MZ2_GUNNER_GRENADE_3:
	case MZ2_GUNNER_GRENADE_4:
		VectorSet(dl->color, 1, 0.5f, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("gunner/gunatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_GLADIATOR_RAILGUN_1:
	// PMM
	case MZ2_CARRIER_RAILGUN:
	case MZ2_WIDOW_RAIL:
	// pmm
		VectorSet(dl->color, 0.5f, 0.5f, 1.0f);
		break;

// --- Xian's shit starts ---
	case MZ2_MAKRON_BFG:
		VectorSet(dl->color, 0.5f, 1, 0.5f);
		//S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("makron/bfg_fire.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_MAKRON_BLASTER_1:
	case MZ2_MAKRON_BLASTER_2:
	case MZ2_MAKRON_BLASTER_3:
	case MZ2_MAKRON_BLASTER_4:
	case MZ2_MAKRON_BLASTER_5:
	case MZ2_MAKRON_BLASTER_6:
	case MZ2_MAKRON_BLASTER_7:
	case MZ2_MAKRON_BLASTER_8:
	case MZ2_MAKRON_BLASTER_9:
	case MZ2_MAKRON_BLASTER_10:
	case MZ2_MAKRON_BLASTER_11:
	case MZ2_MAKRON_BLASTER_12:
	case MZ2_MAKRON_BLASTER_13:
	case MZ2_MAKRON_BLASTER_14:
	case MZ2_MAKRON_BLASTER_15:
	case MZ2_MAKRON_BLASTER_16:
	case MZ2_MAKRON_BLASTER_17:
		VectorSet(dl->color, 1, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("makron/blaster.wav"), 1, ATTN_NORM, 0);
		break;
	
	case MZ2_JORG_MACHINEGUN_L1:
	case MZ2_JORG_MACHINEGUN_L2:
	case MZ2_JORG_MACHINEGUN_L3:
	case MZ2_JORG_MACHINEGUN_L4:
	case MZ2_JORG_MACHINEGUN_L5:
	case MZ2_JORG_MACHINEGUN_L6:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("boss3/xfire.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_JORG_MACHINEGUN_R1:
	case MZ2_JORG_MACHINEGUN_R2:
	case MZ2_JORG_MACHINEGUN_R3:
	case MZ2_JORG_MACHINEGUN_R4:
	case MZ2_JORG_MACHINEGUN_R5:
	case MZ2_JORG_MACHINEGUN_R6:
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		break;

	case MZ2_JORG_BFG_1:
		VectorSet(dl->color, 0.5f, 1, 0.5f);
		break;

	case MZ2_BOSS2_MACHINEGUN_R1:
	case MZ2_BOSS2_MACHINEGUN_R2:
	case MZ2_BOSS2_MACHINEGUN_R3:
	case MZ2_BOSS2_MACHINEGUN_R4:
	case MZ2_BOSS2_MACHINEGUN_R5:
	case MZ2_CARRIER_MACHINEGUN_R1:			// PMM
	case MZ2_CARRIER_MACHINEGUN_R2:			// PMM
		VectorSet(dl->color, 1, 1, 0);
		CL_ParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		break;

// ======
// ROGUE
	case MZ2_STALKER_BLASTER:
	case MZ2_DAEDALUS_BLASTER:
	case MZ2_MEDIC_BLASTER_2:
	case MZ2_WIDOW_BLASTER:
	case MZ2_WIDOW_BLASTER_SWEEP1:
	case MZ2_WIDOW_BLASTER_SWEEP2:
	case MZ2_WIDOW_BLASTER_SWEEP3:
	case MZ2_WIDOW_BLASTER_SWEEP4:
	case MZ2_WIDOW_BLASTER_SWEEP5:
	case MZ2_WIDOW_BLASTER_SWEEP6:
	case MZ2_WIDOW_BLASTER_SWEEP7:
	case MZ2_WIDOW_BLASTER_SWEEP8:
	case MZ2_WIDOW_BLASTER_SWEEP9:
	case MZ2_WIDOW_BLASTER_100:
	case MZ2_WIDOW_BLASTER_90:
	case MZ2_WIDOW_BLASTER_80:
	case MZ2_WIDOW_BLASTER_70:
	case MZ2_WIDOW_BLASTER_60:
	case MZ2_WIDOW_BLASTER_50:
	case MZ2_WIDOW_BLASTER_40:
	case MZ2_WIDOW_BLASTER_30:
	case MZ2_WIDOW_BLASTER_20:
	case MZ2_WIDOW_BLASTER_10:
	case MZ2_WIDOW_BLASTER_0:
	case MZ2_WIDOW_BLASTER_10L:
	case MZ2_WIDOW_BLASTER_20L:
	case MZ2_WIDOW_BLASTER_30L:
	case MZ2_WIDOW_BLASTER_40L:
	case MZ2_WIDOW_BLASTER_50L:
	case MZ2_WIDOW_BLASTER_60L:
	case MZ2_WIDOW_BLASTER_70L:
	case MZ2_WIDOW_RUN_1:
	case MZ2_WIDOW_RUN_2:
	case MZ2_WIDOW_RUN_3:
	case MZ2_WIDOW_RUN_4:
	case MZ2_WIDOW_RUN_5:
	case MZ2_WIDOW_RUN_6:
	case MZ2_WIDOW_RUN_7:
	case MZ2_WIDOW_RUN_8:
		VectorSet(dl->color, 0, 1, 0);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_WIDOW_DISRUPTOR:
		VectorSet(dl->color, -1, -1, -1);
		S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("weapons/disint2.wav"), 1, ATTN_NORM, 0);
		break;

	case MZ2_WIDOW_PLASMABEAM:
	case MZ2_WIDOW2_BEAMER_1:
	case MZ2_WIDOW2_BEAMER_2:
	case MZ2_WIDOW2_BEAMER_3:
	case MZ2_WIDOW2_BEAMER_4:
	case MZ2_WIDOW2_BEAMER_5:
	case MZ2_WIDOW2_BEAM_SWEEP_1:
	case MZ2_WIDOW2_BEAM_SWEEP_2:
	case MZ2_WIDOW2_BEAM_SWEEP_3:
	case MZ2_WIDOW2_BEAM_SWEEP_4:
	case MZ2_WIDOW2_BEAM_SWEEP_5:
	case MZ2_WIDOW2_BEAM_SWEEP_6:
	case MZ2_WIDOW2_BEAM_SWEEP_7:
	case MZ2_WIDOW2_BEAM_SWEEP_8:
	case MZ2_WIDOW2_BEAM_SWEEP_9:
	case MZ2_WIDOW2_BEAM_SWEEP_10:
	case MZ2_WIDOW2_BEAM_SWEEP_11:
		dl->radius = 300 + (rand()&100);
		VectorSet(dl->color, 1, 1, 0);
		dl->die = cl.time + 200;
		break;
// ROGUE
// ======

// --- Xian's shit ends ---

	}
}


/*
===============
CL_AddDLights

===============
*/
void CL_AddDLights (void)
{
	int			i;
	cdlight_t	*dl;

	dl = cl_dlights;

//=====
//PGM
#ifdef GL_QUAKE
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (!dl->radius)
			continue;
		V_AddLight (dl->origin, dl->radius,	dl->color[0], dl->color[1], dl->color[2]);
	}
#else
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (!dl->radius)
			continue;

		// negative light in software. only black allowed
		if ((dl->color[0] < 0) || (dl->color[1] < 0) || (dl->color[2] < 0))
		{
			dl->radius = -(dl->radius);
			dl->color[0] = dl->color[1] = dl->color[2] = 1;
		}
		V_AddLight (dl->origin, dl->radius,	dl->color[0], dl->color[1], dl->color[2]);
	}
#endif
}



/*
==============================================================

PARTICLE MANAGEMENT

==============================================================
*/

cparticle_t	*active_particles, *free_particles;

cparticle_t	particles[MAX_PARTICLES];
int			cl_numparticles = MAX_PARTICLES;


/*
===============
CL_ClearParticles
===============
*/
static void CL_ClearParticles (void)
{
	int		i;
	
	free_particles = &particles[0];
	active_particles = NULL;

	for (i = 0; i < cl_numparticles-1; i++)
		particles[i].next = &particles[i+1];

	particles[cl_numparticles-1].next = NULL;
}


/*
===============
CL_ParticleEffect

Wall impact puffs
===============
*/
void CL_ParticleEffect (const vec3_t org, const vec3_t dir, int color, int count)
{
	int			i;
	cparticle_t	*p;
	float		d;

	for (i = 0; i < count; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = color + (rand()&7);

		d = rand()&31;
		p->org[0] = org[0] + ((rand()&7)-4) + d*dir[0];
		p->org[1] = org[1] + ((rand()&7)-4) + d*dir[1];
		p->org[2] = org[2] + ((rand()&7)-4) + d*dir[2];
		p->vel[0] = crand()*20;
		p->vel[1] = crand()*20;
		p->vel[2] = crand()*20;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (0.5f + frand()*0.3f);
	}
}


/*
===============
CL_ParticleEffect2
===============
*/
void CL_ParticleEffect2 (const vec3_t org, const vec3_t dir, int color, int count)
{
	int			i;
	cparticle_t	*p;
	float		d;

	for (i=0 ; i<count ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = color;

		d = rand()&7;
		p->org[0] = org[0] + ((rand()&7)-4) + d*dir[0];
		p->org[1] = org[1] + ((rand()&7)-4) + d*dir[1];
		p->org[2] = org[2] + ((rand()&7)-4) + d*dir[2];
		p->vel[0] = crand()*20;
		p->vel[1] = crand()*20;
		p->vel[2] = crand()*20;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (0.5f + frand()*0.3f);
	}
}


// RAFAEL
/*
===============
CL_ParticleEffect3
===============
*/
void CL_ParticleEffect3 (const vec3_t org, const vec3_t dir, int color, int count)
{
	int			i;
	cparticle_t	*p;
	float		d;

	for (i=0 ; i<count ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = color;

		d = rand()&7;
		p->org[0] = org[0] + ((rand()&7)-4) + d*dir[0];
		p->org[1] = org[1] + ((rand()&7)-4) + d*dir[1];
		p->org[2] = org[2] + ((rand()&7)-4) + d*dir[2];
		p->vel[0] = crand()*20;
		p->vel[1] = crand()*20;
		p->vel[2] = crand()*20;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (0.5f + frand()*0.3f);
	}
}

/*
===============
CL_TeleporterParticles
===============
*/
void CL_TeleporterParticles (const entity_state_t *ent)
{
	int			i;
	cparticle_t	*p;

	for (i=0 ; i<8 ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = 0xdb;

		p->org[0] = ent->origin[0] - 16 + (rand()&31);
		p->org[1] = ent->origin[1] - 16 + (rand()&31);
		p->vel[0] = crand()*14;
		p->vel[1] = crand()*14;

		p->org[2] = ent->origin[2] - 8.0f + (rand()&7);
		p->vel[2] = 80.0f + (rand()&7);

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -0.5f;
	}
}


/*
===============
CL_LogoutEffect

===============
*/
void CL_LogoutEffect (const vec3_t org, int type)
{
	int			i;
	cparticle_t	*p;

	for (i = 0; i < 500; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;

		if (type == MZ_LOGIN)
			p->color = 0xd0 + (rand()&7);	// green
		else if (type == MZ_LOGOUT)
			p->color = 0x40 + (rand()&7);	// red
		else
			p->color = 0xe0 + (rand()&7);	// yellow

		p->org[0] = org[0] - 16 + frand()*32;
		p->org[1] = org[1] - 16 + frand()*32;
		p->org[2] = org[2] - 24 + frand()*56;
		p->vel[0] = crand()*20;
		p->vel[1] = crand()*20;
		p->vel[2] = crand()*20;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (1.0f + frand()*0.3f);
	}
}


/*
===============
CL_ItemRespawnParticles

===============
*/
void CL_ItemRespawnParticles (const vec3_t org)
{
	int			i;
	cparticle_t	*p;
	cdlight_t *dl;

	dl = CL_AllocDlight(0);
	VectorCopy(org, dl->origin);
	dl->radius = 84;
	dl->die = cl.time + 150;
	VectorSet(dl->color, 0.2, 0.9, 0.3);

	for (i = 0; i < 32; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;

		p->color = 0xd4 + (rand()&3);	// green

		p->org[0] = org[0] + crand()*8;
		p->org[1] = org[1] + crand()*8;
		p->org[2] = org[2] + crand()*8;
		p->vel[0] = crand()*8;
		p->vel[1] = crand()*8;
		p->vel[2] = crand()*8;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY*0.2;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (1.0f + frand()*0.3f);
	}
}


/*
===============
CL_ExplosionParticles
===============
*/
void CL_ExplosionParticles (const vec3_t org)
{
	int			i;
	cparticle_t	*p;

	for (i = 0; i < 128; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = 0xe0 + (rand()&7);

		p->org[0] = org[0] + ((rand()%32)-16);
		p->org[1] = org[1] + ((rand()%32)-16);
		p->org[2] = org[2] + ((rand()%32)-16);
		p->vel[0] = (rand()%384)-192;
		p->vel[1] = (rand()%384)-192;
		p->vel[2] = (rand()%384)-192;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -0.8f / (0.5f + frand()*0.3f);
	}
}


/*
===============
CL_BigTeleportParticles
===============
*/
void CL_BigTeleportParticles (const vec3_t org)
{
	int			i;
	cparticle_t	*p;
	float		dist, s, c;
	static int colortable[4] = {2*8,13*8,21*8,18*8};

	for (i=0 ; i<4096 ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;

		p->color = colortable[rand()&3];

		Q_sincos(M_TWOPI*(rand()&1023)/1023.0f, &s, &c);

		dist = rand()&31;
		p->org[0] = org[0] + c*dist;
		p->vel[0] = c*(70+(rand()&63));
		p->accel[0] = -c*100;

		p->org[1] = org[1] + s*dist;
		p->vel[1] = s*(70+(rand()&63));
		p->accel[1] = -s*100;

		p->org[2] = org[2] + 8 + (rand()%90);
		p->vel[2] = -100.0f + (rand()&31);
		p->accel[2] = PARTICLE_GRAVITY*4;
		p->alpha = 1.0f;

		p->alphavel = -0.3f / (0.5f + frand()*0.3f);
	}
}


/*
===============
CL_BlasterParticles

Wall impact puffs
===============
*/
void CL_BlasterParticles (const vec3_t org, const vec3_t dir)
{
	int			i, count = 40;
	cparticle_t	*p;
	float		d;

	for (i=0 ; i<count ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = 0xe0 + (rand()&7);

		d = rand()&15;
		p->org[0] = org[0] + ((rand()&7)-4) + d*dir[0];
		p->org[1] = org[1] + ((rand()&7)-4) + d*dir[1];
		p->org[2] = org[2] + ((rand()&7)-4) + d*dir[2];
		p->vel[0] = dir[0] * 30 + crand()*40;
		p->vel[1] = dir[1] * 30 + crand()*40;
		p->vel[2] = dir[2] * 30 + crand()*40;

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (0.5f + frand()*0.3f);
	}
}


/*
===============
CL_BlasterTrail

===============
*/
void CL_BlasterTrail (const vec3_t start, const vec3_t end)
{
	vec3_t		move, vec;
	float		len;
	cparticle_t	*p;
	int			dec = 5;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	// FIXME: this is a really silly way to have a loop
	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		VectorClear (p->accel);
		
		p->time = cl.time;

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (0.3f+frand()*0.2f);
		p->color = 0xe0;

		p->org[0] = move[0] + crand();
		p->org[1] = move[1] + crand();
		p->org[2] = move[2] + crand();
		p->vel[0] = crand()*5;
		p->vel[1] = crand()*5;
		p->vel[2] = crand()*5;

		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_QuadTrail

===============
*/
void CL_QuadTrail (const vec3_t start, const vec3_t end)
{
	vec3_t		move, vec;
	float		len;
	cparticle_t	*p;
	int			dec = 5;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		VectorClear (p->accel);
		
		p->time = cl.time;

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (0.8f+frand()*0.2f);
		p->color = 115;

		p->org[0] = move[0] + crand()*16;
		p->org[1] = move[1] + crand()*16;
		p->org[2] = move[2] + crand()*16;
		p->vel[0] = crand()*5;
		p->vel[1] = crand()*5;
		p->vel[2] = crand()*5;

		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_FlagTrail

===============
*/
void CL_FlagTrail (const vec3_t start, const vec3_t end, float color)
{
	vec3_t		move, vec;
	float		len;
	cparticle_t	*p;
	int			dec = 5;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		VectorClear (p->accel);
		
		p->time = cl.time;

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (0.8f+frand()*0.2f);
		p->color = color;

		p->org[0] = move[0] + crand()*16;
		p->org[1] = move[1] + crand()*16;
		p->org[2] = move[2] + crand()*16;
		p->vel[0] = crand()*5;
		p->vel[1] = crand()*5;
		p->vel[2] = crand()*5;

		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_DiminishingTrail

===============
*/
void CL_DiminishingTrail (const vec3_t start, const vec3_t end, centity_t *old, int flags)
{
	vec3_t		move, vec;
	float		len;
	cparticle_t	*p;
	float		dec = 0.5f;
	float		orgscale, velscale;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	if (old->trailcount > 900)
	{
		orgscale = 4;
		velscale = 15;
	}
	else if (old->trailcount > 800)
	{
		orgscale = 2;
		velscale = 10;
	}
	else
	{
		orgscale = 1;
		velscale = 5;
	}

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;

		// drop less particles as it flies
		if ((rand()&1023) < old->trailcount)
		{
			p = free_particles;
			free_particles = p->next;
			p->next = active_particles;
			active_particles = p;
			VectorClear (p->accel);
		
			p->time = cl.time;

			if (flags & EF_GIB)
			{
				p->alpha = 1.0f;
				p->alphavel = -1.0f / (1.0f+frand()*0.4f);
				p->color = 0xe8 + (rand()&7);

				p->org[0] = move[0] + crand()*orgscale;
				p->org[1] = move[1] + crand()*orgscale;
				p->org[2] = move[2] + crand()*orgscale;
				p->vel[0] = crand()*velscale;
				p->vel[1] = crand()*velscale;
				p->vel[2] = crand()*velscale - PARTICLE_GRAVITY;
			}
			else if (flags & EF_GREENGIB)
			{
				p->alpha = 1.0f;
				p->alphavel = -1.0f / (1.0f+frand()*0.4f);
				p->color = 0xdb + (rand()&7);

				p->org[0] = move[0] + crand()*orgscale;
				p->org[1] = move[1] + crand()*orgscale;
				p->org[2] = move[2] + crand()*orgscale;
				p->vel[0] = crand()*velscale;
				p->vel[1] = crand()*velscale;
				p->vel[2] = crand()*velscale - PARTICLE_GRAVITY;
			}
			else
			{
				p->alpha = 1.0f;
				p->alphavel = -1.0f / (1.0f+frand()*0.2f);
				p->color = 4 + (rand()&7);

				p->org[0] = move[0] + crand()*orgscale;
				p->org[1] = move[1] + crand()*orgscale;
				p->org[2] = move[2] + crand()*orgscale;
				p->vel[0] = crand()*velscale;
				p->vel[1] = crand()*velscale;
				p->vel[2] = crand()*velscale;
				p->accel[2] = 20;
			}
		}

		old->trailcount -= 5;
		if (old->trailcount < 100)
			old->trailcount = 100;
		VectorAdd (move, vec, move);
	}
}


/*
===============
CL_RocketTrail

===============
*/
void CL_RocketTrail (const vec3_t start, const vec3_t end, centity_t *old)
{
	vec3_t		move, vec;
	float		len;
	cparticle_t	*p;
	float		dec = 1;

	// smoke
	CL_DiminishingTrail (start, end, old, EF_ROCKET);

	// fire
	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	//VectorScale (vec, dec, vec);

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;

		if ( (rand()&7) == 0)
		{
			p = free_particles;
			free_particles = p->next;
			p->next = active_particles;
			active_particles = p;
			
			VectorClear (p->accel);
			p->time = cl.time;

			p->alpha = 1.0f;
			p->alphavel = -1.0f / (1.0f+frand()*0.2f);
			p->color = 227 + (rand()&3);

			p->org[0] = move[0] + crand()*5;
			p->org[1] = move[1] + crand()*5;
			p->org[2] = move[2] + crand()*5;
			p->vel[0] = crand()*20;
			p->vel[1] = crand()*20;
			p->vel[2] = crand()*20;

			p->accel[2] = -PARTICLE_GRAVITY;
		}
		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_RailTrail

===============
*/
void CL_RailTrail (const vec3_t start, const vec3_t end)
{
	vec3_t		move, vec, right, up, dir;
	float		len, dec, c, s;
	cparticle_t	*p;
	int			i;
	byte		clr = 0x74;
	cdlight_t *dl;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	// add a light at the dest
	dl = CL_AllocDlight(0);
	VectorCopy(end, dl->origin);
	dl->radius = 100;
	dl->die = cl.time + 200;
	VectorSet(dl->color, 0.3, 0.5, 1.0);

	MakeNormalVectors (vec, right, up);

	for (i=0 ; i<len ; i++)
	{

		if(i % 3 == 0){
			VectorAdd(move, vec, move);
			continue;
		}

		if (!free_particles)
			return;

		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		
		p->time = cl.time;
		VectorClear (p->accel);

		Q_sincos(i * 0.1f, &s, &c);

		VectorScale (right, c, dir);
		VectorMA (dir, s, up, dir);

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (1.0f+frand()*0.2f);
		p->color = clr + (rand()&7);

		p->org[0] = move[0] + dir[0]*3;
		p->org[1] = move[1] + dir[1]*3;
		p->org[2] = move[2] + dir[2]*3;
		p->vel[0] = dir[0]*6;
		p->vel[1] = dir[1]*6;
		p->vel[2] = dir[2]*6;

		VectorAdd (move, vec, move);
	}

	dec = 2.0f;
	VectorScale (vec, dec, vec);
	VectorCopy (start, move);

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		VectorClear (p->accel);

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (0.6f+frand()*0.2f);
		p->color = 0x0 + (rand()&15);

		p->org[0] = move[0] + crand()*3;
		p->org[1] = move[1] + crand()*3;
		p->org[2] = move[2] + crand()*3;
		p->vel[0] = crand()*3;
		p->vel[1] = crand()*3;
		p->vel[2] = crand()*3;

		VectorAdd (move, vec, move);
	}
}

// RAFAEL
/*
===============
CL_IonripperTrail
===============
*/
void CL_IonripperTrail (const vec3_t start, const vec3_t ent)
{
	vec3_t	move, vec;
	float	len;
	cparticle_t *p;
	int		dec = 5;
	int     left = 0;

	VectorCopy (start, move);
	VectorSubtract (ent, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		VectorClear (p->accel);

		p->time = cl.time;
		p->alpha = 0.5f;
		p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
		p->color = 0xe4 + (rand()&3);

		VectorCopy(move, p->org);

		if (left)
		{
			left = 0;
			p->vel[0] = 10;
		}
		else 
		{
			left = 1;
			p->vel[0] = -10;
		}

		p->vel[1] = p->vel[2] = 0;

		VectorAdd (move, vec, move);
	}
}


/*
===============
CL_BubbleTrail

===============
*/
void CL_BubbleTrail (const vec3_t start, const vec3_t end)
{
	vec3_t		move, vec;
	float		len;
	int			i;
	cparticle_t	*p;
	int			dec = 32;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	for (i=0 ; i<len ; i+=dec)
	{
		if (!free_particles)
			return;

		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		VectorClear (p->accel);
		p->time = cl.time;

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (1.0f+frand()*0.2f);
		p->color = 4 + (rand()&7);

		p->org[0] = move[0] + crand()*2;
		p->org[1] = move[1] + crand()*2;
		p->org[2] = move[2] + crand()*2;
		p->vel[0] = crand()*5;
		p->vel[1] = crand()*5;
		p->vel[2] = crand()*5;

		p->vel[2] += 6;

		VectorAdd (move, vec, move);
	}
}


/*
===============
CL_FlyParticles
===============
*/

#define	BEAMLENGTH			16
void CL_FlyParticles (const vec3_t origin, int count)
{
	int			i;
	cparticle_t	*p;
	float		sp, sy, cp, cy;
	vec3_t		forward;
	float		dist = 64;
	float		ltime;


	if (count > NUMVERTEXNORMALS)
		count = NUMVERTEXNORMALS;

	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++) {
			avelocities[i][0] = (rand()&255) * 0.01f;
			avelocities[i][1] = (rand()&255) * 0.01f;
			avelocities[i][2] = (rand()&255) * 0.01f;
		}
	}


	ltime = (float)cl.time / 1000.0f;
	for (i=0 ; i<count ; i+=2)
	{
		if (!free_particles)
			return;

		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		Q_sincos(ltime * avelocities[i][0], &sy, &cy);
		Q_sincos(ltime * avelocities[i][1], &sp, &cp);
		//Q_sincos(ltime * avelocities[i][2], &sr, &cr);
	
		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;

		p->time = cl.time;

		dist = (float)sin(ltime + i)*64.0f;
		p->org[0] = origin[0] + bytedirs[i][0]*dist + forward[0]*BEAMLENGTH;
		p->org[1] = origin[1] + bytedirs[i][1]*dist + forward[1]*BEAMLENGTH;
		p->org[2] = origin[2] + bytedirs[i][2]*dist + forward[2]*BEAMLENGTH;

		VectorClear (p->vel);
		VectorClear (p->accel);

		p->color = 0;
		//p->colorvel = 0;

		p->alpha = 1.0f;
		p->alphavel = -100;
	}
}

void CL_FlyEffect (centity_t *ent, const vec3_t origin)
{
	int n, count, starttime;

	if (ent->fly_stoptime < cl.time)
	{
		starttime = cl.time;
		ent->fly_stoptime = cl.time + 60000;
	}
	else
	{
		starttime = ent->fly_stoptime - 60000;
	}

	n = cl.time - starttime;
	if (n < 20000)
		count = n * 162 / 20000;
	else
	{
		n = ent->fly_stoptime - cl.time;
		if (n < 20000)
			count = n * 162 / 20000;
		else
			count = 162;
	}

	CL_FlyParticles (origin, count);
}


/*
===============
CL_BfgParticles
===============
*/

#define	BEAMLENGTH			16
void CL_BfgParticles (const entity_t *ent)
{
	int			i;
	cparticle_t	*p;
	float		sp, sy, cp, cy;
	vec3_t		forward;
	float		dist = 64;
	float		ltime;
	
	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++) {
			avelocities[i][0] = (rand()&255) * 0.01f;
			avelocities[i][1] = (rand()&255) * 0.01f;
			avelocities[i][2] = (rand()&255) * 0.01f;
		}
	}

	ltime = (float)cl.time / 1000.0f;
	for (i=0 ; i<NUMVERTEXNORMALS ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		Q_sincos(ltime * avelocities[i][0], &sy, &cy);
		Q_sincos(ltime * avelocities[i][1], &sp, &cp);
		//Q_sincos(ltime * avelocities[i][2], &sr, &cr);
	
		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;

		p->time = cl.time;

		dist = (float)sin(ltime + i)*64.0f;
		p->org[0] = ent->origin[0] + bytedirs[i][0]*dist + forward[0]*BEAMLENGTH;
		p->org[1] = ent->origin[1] + bytedirs[i][1]*dist + forward[1]*BEAMLENGTH;
		p->org[2] = ent->origin[2] + bytedirs[i][2]*dist + forward[2]*BEAMLENGTH;

		VectorClear (p->vel);
		VectorClear (p->accel);

		dist = (float)Distance (p->org, ent->origin) / 90.0f;
		p->color = (int)floor(0xd0 + dist * 7);
		//p->colorvel = 0;

		p->alpha = 1.0f - dist;
		p->alphavel = -100;
	}
}


/*
===============
CL_TrapParticles
===============
*/
// RAFAEL
void CL_TrapParticles (entity_t *ent)
{
	vec3_t		move, vec;
	vec3_t		start, end;
	float		len;
	int			j, i, k;
	cparticle_t	*p;
	int			dec = 5;
	float		vel;
	vec3_t		dir, org;

	ent->origin[2]-=14;
	VectorCopy (ent->origin, start);
	VectorCopy (ent->origin, end);
	end[2]+=64;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, dec, vec);

	// FIXME: this is a really silly way to have a loop
	while (len > 0)
	{
		len -= dec;

		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		VectorClear (p->accel);
		
		p->time = cl.time;

		p->alpha = 1.0f;
		p->alphavel = -1.0f / (0.3f+frand()*0.2f);
		p->color = 0xe0;
		for (j=0 ; j<3 ; j++)
		{
			p->org[j] = move[j] + crand();
			p->vel[j] = crand()*15;
		}
		p->accel[2] = PARTICLE_GRAVITY;

		VectorAdd (move, vec, move);
	}

	ent->origin[2]+=14;
	VectorCopy (ent->origin, org);
	for (i=-2 ; i<=2 ; i+=4) {
		for (j=-2 ; j<=2 ; j+=4) {
			for (k=-2 ; k<=4 ; k+=4)
			{
				if (!free_particles)
					return;
				p = free_particles;
				free_particles = p->next;
				p->next = active_particles;
				active_particles = p;

				p->time = cl.time;
				p->color = 0xe0 + (rand()&3);

				p->alpha = 1.0f;
				p->alphavel = -1.0f / (0.3f + (rand()&7) * 0.02f);
				
				p->org[0] = org[0] + i + ((rand()&23) * crand());
				p->org[1] = org[1] + j + ((rand()&23) * crand());
				p->org[2] = org[2] + k + ((rand()&23) * crand());
	
				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;
	
				VectorNormalize (dir);						
				vel = 50 + (rand()&63);
				VectorScale (dir, vel, p->vel);

				p->accel[0] = p->accel[1] = 0;
				p->accel[2] = -PARTICLE_GRAVITY;
			}
		}
	}
}


/*
===============
CL_BFGExplosionParticles
===============
*/
//FIXME combined with CL_ExplosionParticles
void CL_BFGExplosionParticles (const vec3_t org)
{
	int			i, j;
	cparticle_t	*p;

	for (i=0 ; i<256 ; i++)
	{
		if (!free_particles)
			return;
		p = free_particles;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;

		p->time = cl.time;
		p->color = 0xd0 + (rand()&7);

		for (j=0 ; j<3 ; j++)
		{
			p->org[j] = org[j] + ((rand()%32)-16);
			p->vel[j] = (rand()%384)-192;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0f;

		p->alphavel = -0.8f / (0.5f + frand()*0.3f);
	}
}


/*
===============
CL_TeleportParticles

===============
*/
void CL_TeleportParticles (const vec3_t org)
{
	int			i, j, k;
	cparticle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i=-16 ; i<=16 ; i+=4)
		for (j=-16 ; j<=16 ; j+=4)
			for (k=-16 ; k<=32 ; k+=4)
			{
				if (!free_particles)
					return;
				p = free_particles;
				free_particles = p->next;
				p->next = active_particles;
				active_particles = p;

				p->time = cl.time;
				p->color = 7 + (rand()&7);

				p->alpha = 1.0f;
				p->alphavel = -1.0f / (0.3f + (rand()&7) * 0.02f);
				
				p->org[0] = org[0] + i + (rand()&3);
				p->org[1] = org[1] + j + (rand()&3);
				p->org[2] = org[2] + k + (rand()&3);
	
				dir[0] = j*8;
				dir[1] = i*8;
				dir[2] = k*8;
	
				VectorNormalize (dir);						
				vel = 50 + (rand()&63);
				VectorScale (dir, vel, p->vel);

				p->accel[0] = p->accel[1] = 0;
				p->accel[2] = -PARTICLE_GRAVITY;
			}
}


/*
===============
CL_AddParticles
===============
*/
void CL_AddParticles (void)
{
	cparticle_t		*p, *next;
	//float			alpha;
	float			cltime;
	float			time = 0, time2 = 0;
	cparticle_t		*active = NULL, *tail = NULL;
	particle_t		part;

	cltime = (float)cl.time;

	for (p=active_particles ; p ; p=next)
	{
		next = p->next;

		// PMM - added INSTANT_PARTICLE handling for heat beam
		if (p->alphavel != INSTANT_PARTICLE)
		{
			time = (cltime - p->time)*0.001f;
			part.alpha = p->alpha + time*p->alphavel;
			if (part.alpha <= 0.0f)
			{	// faded out
				p->next = free_particles;
				free_particles = p;
				continue;
			}
			else if(part.alpha <= 0.3f && p->color == 0xe8)
			{
#if 0
				if(rand() & 4) {
					trace_t		tr;
					time2 = time*time;
					part.origin[0] = p->org[0] + p->vel[0]*time + p->accel[0]*time2;
					part.origin[1] = p->org[1] + p->vel[1]*time + p->accel[1]*time2;
					part.origin[2] = p->org[2] + p->vel[2]*time + p->accel[2]*time2;
					tr = CM_BoxTrace(p->org, part.origin, vec3_origin, vec3_origin, 0, MASK_SOLID);
					if (tr.fraction != 1.0f) {
						R_AddDecal(tr.endpos, tr.plane.normal, 0, 0, 0, 1.0f, 2 + ((rand()%21*0.05) - 0.5), 1, 0, rand()%361);
					}
				}
#endif
 				R_AddStain(p->org, 0, 18);
 			}
		}
		else
		{
			part.alpha = p->alpha;
			p->alphavel = p->alpha = 0.0f;
		}

		p->next = NULL;
		if (!tail) {
			active = tail = p;
		}
		else {
			tail->next = p;
			tail = p;
		}

		if (part.alpha > 1.0f)
			part.alpha = 1.0f;

		time2 = time*time;

		part.origin[0] = p->org[0] + p->vel[0]*time + p->accel[0]*time2;
		part.origin[1] = p->org[1] + p->vel[1]*time + p->accel[1]*time2;
		part.origin[2] = p->org[2] + p->vel[2]*time + p->accel[2]*time2;
		part.color = p->color;

		V_AddParticle (&part);

		// PMM
		//if (p->alphavel == INSTANT_PARTICLE)
		//	p->alphavel = p->alpha = 0.0;
	}

	active_particles = active;
}


/*
==============
CL_EntityEvent

An entity has just been parsed that has an event value

the female events are there for backwards compatability
==============
*/
extern struct sfx_s	*cl_sfx_footsteps[4];

void CL_EntityEvent (const entity_state_t *ent)
{
	switch (ent->event)
	{
	case EV_ITEM_RESPAWN:
		S_StartSound (NULL, ent->number, CHAN_WEAPON, S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
		CL_ItemRespawnParticles (ent->origin);
		break;
	case EV_PLAYER_TELEPORT:
		S_StartSound (NULL, ent->number, CHAN_WEAPON, S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
		CL_TeleportParticles (ent->origin);
		break;
	case EV_FOOTSTEP:
		if (cl_footsteps->integer)
			S_StartSound (NULL, ent->number, CHAN_BODY, cl_sfx_footsteps[rand()&3], 1, ATTN_NORM, 0);
		break;
	case EV_FALLSHORT:
		S_StartSound (NULL, ent->number, CHAN_AUTO, S_RegisterSound ("player/land1.wav"), 1, ATTN_NORM, 0);
		break;
	case EV_FALL:
		S_StartSound (NULL, ent->number, CHAN_AUTO, S_RegisterSound ("*fall2.wav"), 1, ATTN_NORM, 0);
		break;
	case EV_FALLFAR:
		S_StartSound (NULL, ent->number, CHAN_AUTO, S_RegisterSound ("*fall1.wav"), 1, ATTN_NORM, 0);
		break;
	}
}


/*
==============
CL_ClearEffects

==============
*/
void CL_ClearEffects (void)
{
	CL_ClearParticles ();
	CL_ClearDlights ();
	CL_ClearLightStyles ();
}
