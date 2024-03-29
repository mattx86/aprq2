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

#include "server.h"

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

/*
====================
SV_SetMaster_f

Specify a list of master servers
====================
*/
static void SV_SetMaster_f (void)
{
	int		i, slot;

	// only dedicated servers send heartbeats
	if (!dedicated->integer)
	{
		Com_Printf ("Only dedicated servers use masters.\n");
		return;
	}

	// make sure the server is listed public
	Cvar_Set ("sv_public", "1");

	memset (&master_adr, 0, sizeof(master_adr));

	slot = 1;		// slot 0 will always contain the id master
	for (i=1 ; i<Cmd_Argc() ; i++)
	{
		if (slot == MAX_MASTERS)
			break;

		if (!NET_StringToAdr (Cmd_Argv(i), &master_adr[i]))
		{
			Com_Printf ("Bad address: %s\n", Cmd_Argv(i));
			continue;
		}
		if (master_adr[slot].port == 0)
			master_adr[slot].port = BigShort (PORT_MASTER);

		Com_Printf ("Master server at %s\n", NET_AdrToString (&master_adr[slot]));

		if (svs.initialized)
		{
			Com_Printf ("Sending a ping.\n");

			Netchan_OutOfBandPrint (NS_SERVER, &master_adr[slot], "ping");
		}

		slot++;
	}

	svs.last_heartbeat = -9999999;
}



/*
==================
SV_SetPlayer

Sets sv_client and sv_player to the player with idnum Cmd_Argv(1)
==================
*/
qboolean SV_SetPlayer (void)
{
	client_t	*cl;
	int			i;
	int			idnum;
	char		*s;

	if (Cmd_Argc() < 2)
		return false;

	s = Cmd_Argv(1);

	// numeric values are just slot numbers
	if (s[0] >= '0' && s[0] <= '9')
	{
		idnum = atoi(Cmd_Argv(1));
		if (idnum < 0 || idnum >= maxclients->integer)
		{
			Com_Printf ("Bad client slot: %i\n", idnum);
			return false;
		}

		sv_client = &svs.clients[idnum];
		sv_player = sv_client->edict;
		if (!sv_client->state)
		{
			Com_Printf ("Client %i is not active\n", idnum);
			return false;
		}
		return true;
	}

	// check for a name match
	for (i=0,cl=svs.clients ; i<maxclients->integer; i++,cl++)
	{
		if (!cl->state)
			continue;
		if (!strcmp(cl->name, s))
		{
			sv_client = cl;
			sv_player = sv_client->edict;
			return true;
		}
	}

	Com_Printf ("Userid %s is not on the server\n", s);
	return false;
}


/*
===============================================================================

SAVEGAME FILES

===============================================================================
*/

/*
=====================
SV_WipeSavegame

Delete save/<XXX>/
=====================
*/
static void SV_WipeSavegame (const char *savename)
{
	char	name[MAX_OSPATH];
	char	*s;

	Com_DPrintf("SV_WipeSaveGame(%s)\n", savename);

	Com_sprintf (name, sizeof(name), "%s/save/%s/server.ssv", FS_Gamedir (), savename);
	FS_RemoveFile(name);
	Com_sprintf (name, sizeof(name), "%s/save/%s/game.ssv", FS_Gamedir (), savename);
	FS_RemoveFile(name);

	Com_sprintf (name, sizeof(name), "%s/save/%s/*.sav", FS_Gamedir (), savename);
	s = Sys_FindFirst( name, 0, 0 );
	while (s)
	{
		FS_RemoveFile(s);
		s = Sys_FindNext( 0, 0 );
	}
	Sys_FindClose ();
	Com_sprintf (name, sizeof(name), "%s/save/%s/*.sv2", FS_Gamedir (), savename);
	s = Sys_FindFirst(name, 0, 0 );
	while (s)
	{
		FS_RemoveFile(s);
		s = Sys_FindNext( 0, 0 );
	}
	Sys_FindClose ();
}

/*
================
SV_CopySaveGame
================
*/
static void SV_CopySaveGame (const char *src, const char *dst)
{
	char	name[MAX_OSPATH], name2[MAX_OSPATH];
	int		l, len;
	char	*found;

	Com_DPrintf("SV_CopySaveGame(%s, %s)\n", src, dst);

	SV_WipeSavegame(dst);

	// copy the savegame over
	Com_sprintf (name, sizeof(name), "%s/save/%s/server.ssv", FS_Gamedir(), src);
	Com_sprintf (name2, sizeof(name2), "%s/save/%s/server.ssv", FS_Gamedir(), dst);
	FS_CopyFile(name, name2);

	Com_sprintf (name, sizeof(name), "%s/save/%s/game.ssv", FS_Gamedir(), src);
	Com_sprintf (name2, sizeof(name2), "%s/save/%s/game.ssv", FS_Gamedir(), dst);
	FS_CopyFile(name, name2);

	Com_sprintf (name, sizeof(name), "%s/save/%s/", FS_Gamedir(), src);
	len = strlen(name);
	Com_sprintf (name, sizeof(name), "%s/save/%s/*.sav", FS_Gamedir(), src);
	found = Sys_FindFirst(name, 0, 0 );
	while (found)
	{
		strcpy (name+len, found+len);

		Com_sprintf (name2, sizeof(name2), "%s/save/%s/%s", FS_Gamedir(), dst, found+len);
		FS_CopyFile(name, name2);

		// change sav to sv2
		l = strlen(name);
		strcpy (name+l-3, "sv2");
		l = strlen(name2);
		strcpy (name2+l-3, "sv2");
		FS_CopyFile(name, name2);

		found = Sys_FindNext( 0, 0 );
	}
	Sys_FindClose ();
}


/*
==============
SV_WriteLevelFile

==============
*/
static void SV_WriteLevelFile (void)
{
	char	name[MAX_OSPATH];
	fileHandle_t f;

	Com_DPrintf("SV_WriteLevelFile()\n");

	Com_sprintf (name, sizeof(name), "save/current/%s.sv2", sv.name);
	FS_FOpenFile( name, &f, FS_MODE_WRITE );
	if (!f) {
		Com_Printf ("Failed to open %s\n", name);
		return;
	}

	FS_Write(sv.configstrings, sizeof(sv.configstrings), f);
	CM_WritePortalState (f);
	FS_FCloseFile(f);

	Com_sprintf (name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->WriteLevel (name);
}

/*
==============
SV_ReadLevelFile

==============
*/
void SV_ReadLevelFile (void)
{
	char	name[MAX_OSPATH];
	fileHandle_t f;

	Com_DPrintf("SV_ReadLevelFile()\n");

	Com_sprintf (name, sizeof(name), "save/current/%s.sv2", sv.name);
	FS_FOpenFile( name, &f, FS_MODE_READ|FS_TYPE_REAL );
	if (!f) {
		Com_Printf ("Failed to open %s\n", name);
		return;
	}
	FS_Read (sv.configstrings, sizeof(sv.configstrings), f);
	*( ( byte * )sv.configstrings + sizeof( sv.configstrings ) - 1 ) = 0;

	CM_ReadPortalState (f);
	FS_FCloseFile(f);

	Com_sprintf (name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->ReadLevel (name);
}

/*
==============
SV_WriteServerFile

==============
*/
static void SV_WriteServerFile (qboolean autosave)
{
	fileHandle_t f;
	cvar_t	*var;
	char	name[MAX_OSPATH], string[128];
	char	comment[MAX_QPATH];
	time_t	aclock;
	struct tm	*newtime;

	Com_DPrintf("SV_WriteServerFile(%s)\n", autosave ? "true" : "false");

	strcpy(name, "save/current/server.ssv");
	FS_FOpenFile( name, &f, FS_MODE_WRITE );
	if (!f) {
		Com_Printf ("Couldn't write %s\n", name);
		return;
	}
	// write the comment field
	memset (comment, 0, sizeof(comment));

	if (!autosave)
	{
		time (&aclock);
		newtime = localtime (&aclock);
		Com_sprintf (comment,sizeof(comment), "%2i:%i%i %2i/%2i  ", newtime->tm_hour
			, newtime->tm_min/10, newtime->tm_min%10,
			newtime->tm_mon+1, newtime->tm_mday);
		Q_strncatz (comment, sv.configstrings[CS_NAME], sizeof(comment));
	}
	else
	{	// autosaved
		Com_sprintf (comment, sizeof(comment), "ENTERING %s", sv.configstrings[CS_NAME]);
	}

	FS_Write(comment, 32, f);

	// write the mapcmd
	FS_Write(svs.mapcmd, sizeof(svs.mapcmd), f);

	// write all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	for (var = cvar_vars ; var ; var=var->next)
	{
		if (!(var->flags & CVAR_LATCH))
			continue;
		if (strlen(var->name) >= sizeof(name)-1
			|| strlen(var->string) >= sizeof(string)-1)
		{
			Com_Printf ("Cvar too long: %s = %s\n", var->name, var->string);
			continue;
		}
		memset (name, 0, sizeof(name));
		memset (string, 0, sizeof(string));
		strcpy (name, var->name);
		strcpy (string, var->string);
		FS_Write(name, sizeof(name), f);
		FS_Write(string, sizeof(string), f);
	}

	FS_FCloseFile(f);

	// write game state
	Com_sprintf (name, sizeof(name), "%s/save/current/game.ssv", FS_Gamedir());
	ge->WriteGame (name, autosave);
}

/*
==============
SV_ReadServerFile

==============
*/
static void SV_ReadServerFile (void)
{
	fileHandle_t f;
	char	name[MAX_OSPATH], string[128];
	char	comment[32];
	char	mapcmd[MAX_TOKEN_CHARS];

	Com_DPrintf("SV_ReadServerFile()\n");

	strcpy(name, "save/current/server.ssv");
	FS_FOpenFile( name, &f, FS_MODE_READ|FS_TYPE_REAL|FS_PATH_GAME );
	if (!f) {
		Com_Printf ("Couldn't read %s\n", name);
		return;
	}
	// read the comment field
	FS_Read(comment, sizeof(comment), f);

	// read the mapcmd
	FS_Read(mapcmd, sizeof(mapcmd), f);

	// read all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	while (1)
	{
		if (!FS_Read(name, sizeof(name), f))
			break;
		FS_Read(string, sizeof(string), f);
		Com_DPrintf ("Set %s = %s\n", name, string);
		Cvar_Set(name, string);
	}

	FS_FCloseFile(f);

	// start a new game fresh with new cvars
	SV_InitGame ();

	strcpy (svs.mapcmd, mapcmd);

	// read game state
	Com_sprintf (name, sizeof(name), "%s/save/current/game.ssv", FS_Gamedir());
	ge->ReadGame (name);
}


//=========================================================




/*
==================
SV_DemoMap_f

Puts the server in demo mode on a specific map/cinematic
==================
*/
static void SV_DemoMap_f (void)
{
	char	demoName[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("USAGE: demomap <demoname>\n");
		return;
	}

	if(strlen(Cmd_Argv(1)) >= sizeof(demoName)-1)
	{
		Com_Printf("Demomap: Demo name is too long\n");
		return;
	}

	Q_strncpyz(demoName, Cmd_Argv(1), sizeof(demoName));
	COM_DefaultExtension(demoName, sizeof(demoName), ".dm2");

	SV_Map (true, demoName, false );
}

/*
==================
SV_GameMap_f

Saves the state of the map just being exited and goes to a new map.

If the initial character of the map string is '*', the next map is
in a new unit, so the current savegame directory is cleared of
map files.

Example:

*inter.cin+jail

Clears the archived maps, plays the inter.cin cinematic, then
goes to map jail.bsp.
==================
*/
static void SV_GameMap_f (void)
{
	char		*map, expanded[MAX_QPATH], tMap[MAX_QPATH];
	int			i;
	client_t	*cl;
	//qboolean	*savedInuse;

	if (Cmd_Argc() != 2) {
		Com_Printf ("USAGE: gamemap <map>\n");
		return;
	}

	Q_strncpyz(tMap, Cmd_Argv(1), sizeof(tMap));
	map = tMap;
	Com_DPrintf("SV_GameMap(%s)\n", map);

	FS_CreatePath (va("%s/save/current/", FS_Gamedir()));

	// check for clearing the current savegame
	if (map[0] == '*')
	{
		// wipe all the *.sav files
		SV_WipeSavegame("current");
	}
	else
	{	// save the map just exited

		if (!strchr(map, '.') && !strchr(map, '$'))
		{
			Com_sprintf (expanded, sizeof(expanded), "maps/%s.bsp", map);
			if (FS_LoadFile (expanded, NULL) == -1)
			{
				Com_Printf ("Can't find %s\n", expanded);
				return;
			}
		}
		if (sv.state == ss_game)
		{
			qboolean	savedInuse[MAX_CLIENTS];
			// clear all the client inuse flags before saving so that
			// when the level is re-entered, the clients will spawn
			// at spawn points instead of occupying body shells
			//savedInuse = malloc(maxclients->integer * sizeof(qboolean));
			for (i=0,cl=svs.clients ; i<maxclients->integer; i++,cl++)
			{
				savedInuse[i] = cl->edict->inuse;
				cl->edict->inuse = false;
			}

			SV_WriteLevelFile ();

			// we must restore these for clients to transfer over correctly
			for (i=0,cl=svs.clients ; i<maxclients->integer; i++,cl++)
				cl->edict->inuse = savedInuse[i];
			//free (savedInuse);
		}
	}

	// start up the next map
	SV_Map (false, map, false );

	// archive server state
	Q_strncpyz (svs.mapcmd, map, sizeof(svs.mapcmd));

	// copy off the level to the autosave slot
	if (!dedicated->integer && !Cvar_VariableIntValue("deathmatch"))
	{
		SV_WriteServerFile(true);
		SV_CopySaveGame("current", "save0");
	}
}

/*
==================
SV_Map_f

Goes directly to a given map without any savegame archiving.
For development work
==================
*/
static void SV_Map_f (void)
{
	char	*map;
	char	expanded[MAX_QPATH];

	if (Cmd_Argc() != 2) {
		Com_Printf ("USAGE: map <mapname>\n");
		return;
	}
	// if not a pcx, demo, or cinematic, check to make sure the level exists
	map = Cmd_Argv(1);
	if (!strchr(map, '.') && !strchr(map, '$'))
	{
		Com_sprintf (expanded, sizeof(expanded), "maps/%s.bsp", map);
		if (FS_LoadFile (expanded, NULL) == -1)
		{
			Com_Printf ("Can't find %s\n", expanded);
			return;
		}
	}

	sv.state = ss_dead;		// don't save current level when changing
	SV_WipeSavegame("current");
	SV_GameMap_f ();
}

/*
=====================================================================

  SAVEGAMES

=====================================================================
*/


/*
==============
SV_Loadgame_f

==============
*/
static void SV_Loadgame_f (void)
{
	char	name[MAX_OSPATH];
	char	*dir;

	if (Cmd_Argc() != 2) {
		Com_Printf ("USAGE: loadgame <directory>\n");
		return;
	}

	dir = Cmd_Argv(1);
	if (strstr(dir, "..") || strchr(dir, '/') || strchr(dir, '\\')) {
		Com_Printf ("Bad savedir.\n");
		return;
	}

	// make sure the server.ssv file exists
	Com_sprintf (name, sizeof(name), "save/%s/server.ssv", Cmd_Argv(1));
	if (FS_LoadFileEx( name, NULL, FS_TYPE_REAL|FS_PATH_GAME ) == -1 ) {
		Com_Printf ("No such savegame: %s\n", name);
		return;
	}

	Com_Printf ("Loading game...\n");
	SV_CopySaveGame(Cmd_Argv(1), "current");

	SV_ReadServerFile ();

	// go to the map
	sv.state = ss_dead;		// don't save current level when changing
	SV_Map (false, svs.mapcmd, true);
}



/*
==============
SV_Savegame_f

==============
*/
static void SV_Savegame_f (void)
{
	char	*dir;

	if (sv.state != ss_game)
	{
		Com_Printf ("You must be in a game to save.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("USAGE: savegame <directory>\n");
		return;
	}

	if (Cvar_VariableIntValue("deathmatch"))
	{
		Com_Printf ("Can't savegame in a deathmatch\n");
		return;
	}

	if (!strcmp (Cmd_Argv(1), "current"))
	{
		Com_Printf ("Can't save to 'current'\n");
		return;
	}

	if (maxclients->integer == 1 && svs.clients[0].edict->client->ps.stats[STAT_HEALTH] <= 0)
	{
		Com_Printf ("\nCan't savegame while dead!\n");
		return;
	}

	dir = Cmd_Argv(1);
	if (strstr(dir, "..") || strchr(dir, '/') || strchr(dir, '\\'))
	{
		Com_Printf ("Bad savedir.\n");
		return;
	}

	Com_Printf ("Saving game...\n");

	// archive current level, including all client edicts.
	// when the level is reloaded, they will be shells awaiting
	// a connecting client
	SV_WriteLevelFile ();

	// save server state
	SV_WriteServerFile (false);

	// copy it off
	SV_CopySaveGame("current", dir);

	Com_Printf ("Done.\n");
}

//===============================================================

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
static void SV_Kick_f (void)
{
	if (!svs.initialized)
	{
		Com_Printf ("No server running.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: kick <userid>\n");
		return;
	}

	if (!SV_SetPlayer ())
		return;

	//r1: ignore kick message on connecting players (and those with no name)
	if (sv_client->state == cs_spawned && sv_client->name[0])
		SV_BroadcastPrintf (PRINT_HIGH, "%s was kicked\n", sv_client->name);
	// print directly, because the dropped client won't get the
	// SV_BroadcastPrintf message
	SV_ClientPrintf (sv_client, PRINT_HIGH, "You were kicked from the game\n");
	SV_DropClient (sv_client);
	sv_client->lastmessage = svs.realtime;	// min case there is a funny zombie
}


/*
================
SV_Status_f
================
*/
static void SV_Status_f (void)
{
	int			i;
	client_t	*cl;
	char		*s;
	int			ping;
	if (!svs.clients)
	{
		Com_Printf ("No server running.\n");
		return;
	}
	Com_Printf ("map              : %s\n", sv.name);

	Com_Printf ("num score ping name            lastmsg address               qport \n");
	Com_Printf ("--- ----- ---- --------------- ------- --------------------- ------\n");
	for (i=0,cl=svs.clients ; i<maxclients->integer; i++,cl++)
	{
		if (!cl->state)
			continue;
		Com_Printf ("%3i ", i);
		Com_Printf ("%5i ", cl->edict->client->ps.stats[STAT_FRAGS]);

		if (cl->state == cs_connected)
			Com_Printf ("CNCT ");
		else if (cl->state == cs_zombie)
			Com_Printf ("ZMBI ");
		else
		{
			ping = cl->ping < 9999 ? cl->ping : 9999;
			Com_Printf ("%4i ", ping);
		}

		Com_Printf ("%-15s %7i ", cl->name, svs.realtime - cl->lastmessage);
		Com_Printf ("%7i ", svs.realtime - cl->lastmessage );

		s = NET_AdrToString ( &cl->netchan.remote_address);
		Com_Printf ("%-21s %5i\n", s, cl->netchan.qport);
	}
	Com_Printf ("\n");
}

/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f(void)
{
	client_t *client;
	int		j;
	char	*p;
	char	text[1024];

	if (Cmd_Argc () < 2)
		return;

	if (!svs.initialized) {
		Com_Printf ("No server running.\n");
		return;
	}

	strcpy (text, "console: ");
	p = Cmd_Args();

	if (*p == '"')
	{
		p++;
		p[strlen(p)-1] = 0;
	}

	strncat(text, p, 1014);

	for (j = 0, client = svs.clients; j < maxclients->integer; j++, client++)
	{
		if (client->state != cs_spawned)
			continue;
		SV_ClientPrintf(client, PRINT_CHAT, "%s\n", text);
	}
}


/*
==================
SV_Heartbeat_f
==================
*/
static void SV_Heartbeat_f (void)
{
	svs.last_heartbeat = -9999999;
}


/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
static void SV_Serverinfo_f (void)
{
	Com_Printf ("Server info settings:\n");
	Info_Print (Cvar_Serverinfo());
}


/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
static void SV_DumpUser_f (void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: info <userid>\n");
		return;
	}

	if (!SV_SetPlayer ())
		return;

	Com_Printf ("userinfo\n");
	Com_Printf ("--------\n");
	Info_Print (sv_client->userinfo);

}


/*
==============
SV_ServerRecord_f

Begins server demo recording.  Every entity and every message will be
recorded, but no playerinfo will be stored.  Primarily for demo merging.
==============
*/
static void SV_ServerRecord_f (void)
{
	char	name[MAX_OSPATH];
	byte	buf_data[32768];
	sizebuf_t	buf;
	int		len;
	int		i;
	size_t	size;

	if (Cmd_Argc() != 2) {
		Com_Printf ("serverrecord <demoname>\n");
		return;
	}

	if (svs.demofile) {
		Com_Printf ("Already recording.\n");
		return;
	}

	if (sv.state != ss_game) {
		Com_Printf ("You must be in a level to record.\n");
		return;
	}

	//
	// open the demo file
	//
	Com_sprintf (name, sizeof(name), "%s/demos/%s.dm2", FS_Gamedir(), Cmd_Argv(1));

	FS_CreatePath(name);
	svs.demofile = fopen (name, "wb");
	if (!svs.demofile) {
		Com_Printf ("ERROR: couldn't open.\n");
		return;
	}

	Com_Printf ("recording to %s.\n", name);

	// setup a buffer to catch all multicasts
	SZ_Init (&svs.demo_multicast, svs.demo_multicast_buf, sizeof(svs.demo_multicast_buf));

	//
	// write a single giant fake message with all the startup info
	//
	SZ_Init (&buf, buf_data, sizeof(buf_data));

	//
	// serverdata needs to go over for all types of servers
	// to make sure the protocol is right, and to set the gamedir
	//
	// send the serverdata
	MSG_WriteByte (&buf, svc_serverdata);
	MSG_WriteLong (&buf, PROTOCOL_VERSION_DEFAULT);
	MSG_WriteLong (&buf, svs.spawncount);
	// 2 means server demo
	MSG_WriteByte (&buf, 2);	// demos are always attract loops
	MSG_WriteString (&buf, Cvar_VariableString ("gamedir"));
	MSG_WriteShort (&buf, -1);
	// send full levelname
	MSG_WriteString (&buf, sv.configstrings[CS_NAME]);

	for (i=0 ; i<MAX_CONFIGSTRINGS ; i++)
		if (sv.configstrings[i][0])
		{
			MSG_WriteByte (&buf, svc_configstring);
			MSG_WriteShort (&buf, i);
			MSG_WriteString (&buf, sv.configstrings[i]);
		}

	// write it to the demo file
	Com_DPrintf ("signon message length: %i\n", buf.cursize);
	len = LittleLong (buf.cursize);
	size = fwrite (&len, 4, 1, svs.demofile);
	size = fwrite (buf.data, buf.cursize, 1, svs.demofile);

	// the rest of the demo file will be individual frames
}


/*
==============
SV_ServerStop_f

Ends server demo recording
==============
*/
static void SV_ServerStop_f (void)
{
	if (!svs.demofile)
	{
		Com_Printf ("Not doing a serverrecord.\n");
		return;
	}
	fclose (svs.demofile);
	svs.demofile = NULL;
	Com_Printf ("Recording completed.\n");
}


/*
===============
SV_KillServer_f

Kick everyone off, possibly in preparation for a new game

===============
*/
static void SV_KillServer_f (void)
{
	if (!svs.initialized)
		return;
	SV_Shutdown ("Server was killed.\n", false);
	NET_Config ( NET_NONE );	// close network sockets
}

/*
===============
SV_ServerCommand_f

Let the game dll handle a command
===============
*/
static void SV_ServerCommand_f (void)
{
	if (!ge)
	{
		Com_Printf ("No game loaded.\n");
		return;
	}

	ge->ServerCommand();
}

//===========================================================

/*
==================
SV_InitOperatorCommands
==================
*/
void SV_InitOperatorCommands (void)
{
	Cmd_AddCommand ("heartbeat", SV_Heartbeat_f);
	Cmd_AddCommand ("kick", SV_Kick_f);
	Cmd_AddCommand ("status", SV_Status_f);
	Cmd_AddCommand ("serverinfo", SV_Serverinfo_f);
	Cmd_AddCommand ("dumpuser", SV_DumpUser_f);

	Cmd_AddCommand ("map", SV_Map_f);
	Cmd_AddCommand ("demomap", SV_DemoMap_f);
	Cmd_AddCommand ("gamemap", SV_GameMap_f);
	Cmd_AddCommand ("setmaster", SV_SetMaster_f);

	if ( dedicated->integer )
		Cmd_AddCommand ("say", SV_ConSay_f);

	Cmd_AddCommand ("serverrecord", SV_ServerRecord_f);
	Cmd_AddCommand ("serverstop", SV_ServerStop_f);

	Cmd_AddCommand ("save", SV_Savegame_f);
	Cmd_AddCommand ("load", SV_Loadgame_f);

	Cmd_AddCommand ("killserver", SV_KillServer_f);

	Cmd_AddCommand ("sv", SV_ServerCommand_f);
}
