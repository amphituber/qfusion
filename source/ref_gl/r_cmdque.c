/*
Copyright (C) 2016 Victor Luchits

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

#include "r_local.h"
#include "r_cmdque.h"

// producers and handlers for
// frame commands buffer and reliable inter-frame commands queue

/*
=============================================================

FRAME COMMANDS BUFFER

=============================================================
*/

typedef struct
{
	int             id;
	float           cameraSeparation;
	bool            forceClear;
	bool            forceVsync;
} refCmdBeginFrame_t;

typedef struct
{
	int             id;
} refCmdEndFrame_t;

typedef struct
{
	int             id;
	int             x, y, w, h;
	float           s1, t1, s2, t2;
	float           angle;
	vec4_t          color;
	void            *shader;
} refCmdDrawStretchPic_t;

typedef struct
{
	int             id;
	unsigned        length;
	float           x_offset, y_offset;
	poly_t          poly;
} refCmdDrawStretchOrScenePoly_t;

typedef struct
{
	int             id;
} refCmdClearScene_t;

typedef struct
{
	int             id;
	unsigned        length;
	entity_t        entity;
	int             numBoneposes;
	bonepose_t      *boneposes;
	bonepose_t      *oldboneposes;
} refCmdAddEntityToScene_t;

typedef struct
{
	int             id;
	vec3_t          origin;
	float           intensity;
	float           r, g, b;
} refCmdAddLightToScene_t;

typedef struct
{
	int             id;
	int             style;
	float           r, g, b;
} refCmdAddLightStyleToScene_t;

typedef struct
{
	int             id;
	unsigned        length;
	int             registrationSequence;
	int             worldModelSequence;
	refdef_t        refdef;
	uint8_t         *areabits;
} refCmdRenderScene_t;

typedef struct
{
	int             id;
	int             x, y, w, h;
} refCmdSetScissor_t;

typedef struct
{
	int             id;
} refCmdResetScissor_t;

typedef struct
{
	int             id;
} refCmdSync_t;

typedef struct
{
	int             id;
	int             x, y, w, h;
	float           s1, t1, s2, t2;
} refCmdDrawStretchRaw_t;

typedef unsigned (*refCmdHandler_t)( const void * );

static unsigned R_HandleBeginFrameCmd( uint8_t *cmdbuf );
static unsigned R_HandleEndFrameCmd( uint8_t *cmdbuf );
static unsigned R_HandleDrawStretchPicCmd( uint8_t *cmdbuf );
static unsigned R_HandleDrawStretchPolyCmd( uint8_t *cmdbuf );
static unsigned R_HandleClearSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleAddEntityToSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleAddLightToSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleAddPolyToSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleAddLightStyleToSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleRenderSceneCmd( uint8_t *cmdbuf );
static unsigned R_HandleSetScissorCmd( uint8_t *cmdbuf );
static unsigned R_HandleResetScissorCmd( uint8_t *cmdbuf );
static unsigned R_HandleDrawStretchRawCmd( uint8_t *cmdbuf );
static unsigned R_HandleDrawStretchRawYUVCmd( uint8_t *cmdbuf );

static unsigned R_HandleBeginFrameCmd( uint8_t *pcmd )
{
	refCmdBeginFrame_t *cmd = (void *)pcmd;
	R_BeginFrame( cmd->cameraSeparation, cmd->forceClear, cmd->forceVsync );
	return sizeof( *cmd );
}

static unsigned R_HandleEndFrameCmd( uint8_t *pcmd )
{
	refCmdEndFrame_t *cmd = (void *)pcmd;
	R_EndFrame();
	return sizeof( *cmd );
}

static unsigned R_HandleDrawStretchPicCmd( uint8_t *pcmd )
{
	refCmdDrawStretchPic_t *cmd = (void *)pcmd;
	R_DrawRotatedStretchPic( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, cmd->s2, cmd->t2,
		cmd->angle, cmd->color, cmd->shader );
	return sizeof( *cmd );
}

static unsigned R_HandleDrawStretchPolyCmd( uint8_t *pcmd )
{
	refCmdDrawStretchOrScenePoly_t *cmd = (void *)pcmd;
	R_DrawStretchPoly( &cmd->poly, cmd->x_offset, cmd->y_offset );
	return cmd->length;
}

static unsigned R_HandleClearSceneCmd( uint8_t *pcmd )
{
	refCmdClearScene_t *cmd = (void *)pcmd;
	R_ClearScene();
	return sizeof( *cmd );
}

static unsigned R_HandleAddEntityToSceneCmd( uint8_t *pcmd )
{
	refCmdAddEntityToScene_t *cmd = (void *)pcmd;
	R_AddEntityToScene( &cmd->entity );
	return cmd->length;
}

static unsigned R_HandleAddLightToSceneCmd( uint8_t *pcmd )
{
	refCmdAddLightToScene_t *cmd = (void *)pcmd;
	R_AddLightToScene( cmd->origin, cmd->intensity, cmd->r, cmd->g, cmd->b );
	return sizeof( *cmd );
}

static unsigned R_HandleAddPolyToSceneCmd( uint8_t *pcmd )
{
	refCmdDrawStretchOrScenePoly_t *cmd = (void *)pcmd;
	R_AddPolyToScene( &cmd->poly );
	return cmd->length;
}

static unsigned R_HandleAddLightStyleToSceneCmd( uint8_t *pcmd )
{
	refCmdAddLightStyleToScene_t *cmd = (void *)pcmd;
	R_AddLightStyleToScene( cmd->style, cmd->r, cmd->g, cmd->b );
	return sizeof( *cmd );
}

static unsigned R_HandleRenderSceneCmd( uint8_t *pcmd )
{
	refCmdRenderScene_t *cmd = (void *)pcmd;

	// ignore scene render calls issued during registration
	if( cmd->registrationSequence != rsh.registrationSequence ) {
		return cmd->length;
	}
	if( !( cmd->refdef.rdflags & RDF_NOWORLDMODEL ) && ( cmd->worldModelSequence != rsh.worldModelSequence ) ) {
		return cmd->length;
	}

	R_RenderScene( &cmd->refdef );
	return cmd->length;
}

static unsigned R_HandleSetScissorCmd( uint8_t *pcmd )
{
	refCmdSetScissor_t *cmd = (void *)pcmd;
	R_Scissor( cmd->x, cmd->y, cmd->w, cmd->h );
	return sizeof( *cmd );
}

static unsigned R_HandleResetScissorCmd( uint8_t *pcmd )
{
	refCmdResetScissor_t *cmd = (void *)pcmd;
	R_ResetScissor();
	return sizeof( *cmd );
}

static unsigned R_HandleDrawStretchRawCmd( uint8_t *pcmd )
{
	refCmdDrawStretchRaw_t *cmd = (void *)pcmd;
	R_DrawStretchRaw( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, cmd->s2, cmd->t2 );
	return sizeof( *cmd );
}

static unsigned R_HandleDrawStretchRawYUVCmd( uint8_t *pcmd )
{
	refCmdDrawStretchRaw_t *cmd = (void *)pcmd;
	R_DrawStretchRawYUV( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, cmd->s2, cmd->t2 );
	return sizeof( *cmd );
}

// ============================================================================

static void RF_IssueBeginFrameCmd( ref_cmdbuf_t *cmdbuf, float cameraSeparation, bool forceClear, bool forceVsync )
{
	refCmdBeginFrame_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_BEGIN_FRAME;
	cmd.cameraSeparation = cameraSeparation;
	cmd.forceClear = forceClear;
	cmd.forceVsync = forceVsync;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueEndFrameCmd( ref_cmdbuf_t *cmdbuf )
{
	refCmdEndFrame_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_END_FRAME;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueDrawRotatedStretchPicCmd( ref_cmdbuf_t *cmdbuf, int x, int y, int w, int h,
	float s1, float t1, float s2, float t2, float angle, const vec4_t color, const shader_t *shader )
{
	refCmdDrawStretchPic_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_DRAW_STRETCH_PIC;
	cmd.x = x;
	cmd.y = y;
	cmd.w = w;
	cmd.h = h;
	cmd.s1 = s1;
	cmd.t1 = t1;
	cmd.s2 = s2;
	cmd.t2 = t2;
	cmd.angle = angle;
	cmd.shader = (void *)shader;
	Vector4Copy( color, cmd.color );

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueDrawStretchPolyOrAddPolyToSceneCmd( ref_cmdbuf_t *cmdbuf, int id, const poly_t *poly,
	float x_offset, float y_offset )
{
	refCmdDrawStretchOrScenePoly_t cmd;
	size_t cmd_len = sizeof( cmd );
	int numverts;
	uint8_t *pcmd;

	numverts = poly->numverts;
	if( !numverts || !poly->shader )
		return;

	cmd.id = id;
	cmd.poly = *poly;
	cmd.x_offset = x_offset;
	cmd.y_offset = y_offset;

	if( poly->verts )
		cmd_len += numverts * sizeof( vec4_t );
	if( poly->stcoords )
		cmd_len += numverts * sizeof( vec2_t );
	if( poly->normals )
		cmd_len += numverts * sizeof( vec4_t );
	if( poly->colors )
		cmd_len += numverts * sizeof( byte_vec4_t );
	if( poly->elems )
		cmd_len += poly->numelems * sizeof( elem_t );
	cmd_len = ALIGN( cmd_len, sizeof( float ) );

	cmd.length = cmd_len;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;

	pcmd = cmdbuf->buf + cmdbuf->len;
	pcmd += sizeof( cmd );

	if( poly->verts ) {
		cmd.poly.verts = (void *)pcmd;
		memcpy( pcmd, poly->verts, numverts * sizeof( vec4_t ) );
		pcmd += numverts * sizeof( vec4_t );
	}
	if( poly->stcoords ) {
		cmd.poly.stcoords = (void *)pcmd;
		memcpy( pcmd, poly->stcoords, numverts * sizeof( vec2_t ) );
		pcmd += numverts * sizeof( vec2_t );
	}
	if( poly->normals ) {
		cmd.poly.normals = (void *)pcmd;
		memcpy( pcmd, poly->normals, numverts * sizeof( vec4_t ) );
		pcmd += numverts * sizeof( vec4_t );
	}
	if( poly->colors ) {
		cmd.poly.colors = (void *)pcmd;
		memcpy( pcmd, poly->colors, numverts * sizeof( byte_vec4_t ) );
		pcmd += numverts * sizeof( byte_vec4_t );
	}
	if( poly->elems ) {
		cmd.poly.elems = (void *)pcmd;
		memcpy( pcmd, poly->elems, poly->numelems * sizeof( elem_t ) );
		pcmd += poly->numelems * sizeof( elem_t );
	}

	pcmd = cmdbuf->buf + cmdbuf->len;
	memcpy( pcmd, &cmd, sizeof( cmd ) );

	cmdbuf->len += cmd_len;
}

static void RF_IssueDrawStretchPolyCmd( ref_cmdbuf_t *cmdbuf, const poly_t *poly, float x_offset, float y_offset )
{
	RF_IssueDrawStretchPolyOrAddPolyToSceneCmd( cmdbuf, REF_CMD_DRAW_STRETCH_POLY, poly, x_offset, y_offset );
}

static void RF_IssueClearSceneCmd( ref_cmdbuf_t *cmdbuf )
{
	refCmdClearScene_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_CLEAR_SCENE;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueAddEntityToSceneCmd( ref_cmdbuf_t *cmdbuf, const entity_t *ent )
{
	refCmdAddEntityToScene_t cmd;
	size_t cmd_len = sizeof( cmd );
	uint8_t *pcmd;
	size_t bones_len = 0;

	cmd.id = REF_CMD_ADD_ENTITY_TO_SCENE;
	cmd.entity = *ent;
	cmd.numBoneposes = R_SkeletalGetNumBones( ent->model, NULL );

	bones_len = cmd.numBoneposes * sizeof( bonepose_t );
	if( cmd.numBoneposes && ent->boneposes ) {
		cmd_len += bones_len;
	}
	if( cmd.numBoneposes && ent->oldboneposes ) {
		cmd_len += bones_len;
	}
	cmd.length = cmd_len;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;

	pcmd = cmdbuf->buf + cmdbuf->len;
	pcmd += sizeof( cmd );

	if( cmd.numBoneposes && ent->boneposes ) {
		cmd.entity.boneposes = (void *)pcmd;
		memcpy( pcmd, ent->boneposes, bones_len );
		pcmd += bones_len;
	}

	if( cmd.numBoneposes && ent->oldboneposes ) {
		cmd.entity.oldboneposes = (void *)pcmd;
		memcpy( pcmd, ent->oldboneposes, bones_len );
		pcmd += bones_len;
	}

	pcmd = cmdbuf->buf + cmdbuf->len;
	memcpy( pcmd, &cmd, sizeof( cmd ) );

	cmdbuf->len += cmd_len;
}

static void RF_IssueAddLightToSceneCmd( ref_cmdbuf_t *cmdbuf, const vec3_t org, float intensity, float r, float g, float b )
{
	refCmdAddLightToScene_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_ADD_LIGHT_TO_SCENE;
	VectorCopy( org, cmd.origin );
	cmd.intensity = intensity;
	cmd.r = r;
	cmd.g = g;
	cmd.b = b;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueAddPolyToSceneCmd( ref_cmdbuf_t *cmdbuf, const poly_t *poly )
{
	RF_IssueDrawStretchPolyOrAddPolyToSceneCmd( cmdbuf, REF_CMD_ADD_POLY_TO_SCENE, poly, 0.0f, 0.0f );
}

static void RF_IssueAddLightStyleToSceneCmd( ref_cmdbuf_t *cmdbuf, int style, float r, float g, float b )
{
	refCmdAddLightStyleToScene_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_ADD_LIGHT_STYLE_TO_SCENE;
	cmd.style = style;
	cmd.r = r;
	cmd.g = g;
	cmd.b = b;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueRenderSceneCmd( ref_cmdbuf_t *cmdbuf, const refdef_t *fd )
{
	refCmdRenderScene_t cmd;
	size_t cmd_len = sizeof( cmd );
	uint8_t *pcmd;
	unsigned areabytes = 0;

	cmd.id = REF_CMD_RENDER_SCENE;
	cmd.refdef = *fd;
	cmd.registrationSequence = rsh.registrationSequence;
	cmd.worldModelSequence = rsh.worldModelSequence;

	if( fd->areabits && rsh.worldBrushModel ) {
		areabytes = ((rsh.worldBrushModel->numareas+7)/8);
#ifdef AREAPORTALS_MATRIX
		areabytes *= rsh.worldBrushModel->numareas;
#endif
		cmd_len = ALIGN( cmd_len + areabytes, sizeof( float ) );
	}

	cmd.length = cmd_len;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;

	pcmd = cmdbuf->buf + cmdbuf->len;
	pcmd += sizeof( cmd );

	if( areabytes > 0 ) {
		cmd.refdef.areabits = (void*)pcmd;
		memcpy( pcmd, fd->areabits, areabytes );
	}

	pcmd = cmdbuf->buf + cmdbuf->len;
	memcpy( pcmd, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueSetScissorCmd( ref_cmdbuf_t *cmdbuf, int x, int y, int w, int h )
{
	refCmdSetScissor_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_SET_SCISSOR;
	cmd.x = x;
	cmd.y = y;
	cmd.w = w;
	cmd.h = h;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueResetScissorCmd( ref_cmdbuf_t *cmdbuf )
{
	refCmdResetScissor_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = REF_CMD_RESET_SCISSOR;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueDrawStretchRawOrRawYUVCmd( ref_cmdbuf_t *cmdbuf, int id, int x, int y, int w, int h, float s1, float t1, float s2, float t2 )
{
	refCmdDrawStretchRaw_t cmd;
	size_t cmd_len = sizeof( cmd );

	cmd.id = id;
	cmd.x = x;
	cmd.y = y;
	cmd.w = w;
	cmd.h = h;
	cmd.s1 = s1;
	cmd.t1 = t1;
	cmd.s2 = s2;
	cmd.t2 = t2;

	if( cmdbuf->len + cmd_len > sizeof( cmdbuf->buf ) )
		return;
	memcpy( cmdbuf->buf + cmdbuf->len, &cmd, sizeof( cmd ) );
	cmdbuf->len += cmd_len;
}

static void RF_IssueDrawStretchRawCmd( ref_cmdbuf_t *cmdbuf, int x, int y, int w, int h, float s1, float t1, float s2, float t2 )
{
	RF_IssueDrawStretchRawOrRawYUVCmd( cmdbuf, REF_CMD_DRAW_STRETCH_RAW, x, y, w, h, s1, t1, s2, t2 );
}

static void RF_IssueDrawStretchRawYUVCmd( ref_cmdbuf_t *cmdbuf, int x, int y, int w, int h, float s1, float t1, float s2, float t2 )
{
	RF_IssueDrawStretchRawOrRawYUVCmd( cmdbuf, REF_CMD_DRAW_STRETCH_RAW_YUV, x, y, w, h, s1, t1, s2, t2 );
}

// ============================================================================

static void RF_RunCmdBufProc( ref_cmdbuf_t *cmdbuf )
{
	// must match the corresponding REF_CMD_ enums!
	static const refCmdHandler_t refCmdHandlers[NUM_REF_CMDS] =
	{
		(refCmdHandler_t)R_HandleBeginFrameCmd,
		(refCmdHandler_t)R_HandleEndFrameCmd,
		(refCmdHandler_t)R_HandleDrawStretchPicCmd,
		(refCmdHandler_t)R_HandleDrawStretchPolyCmd,
		(refCmdHandler_t)R_HandleClearSceneCmd,
		(refCmdHandler_t)R_HandleAddEntityToSceneCmd,
		(refCmdHandler_t)R_HandleAddLightToSceneCmd,
		(refCmdHandler_t)R_HandleAddPolyToSceneCmd,
		(refCmdHandler_t)R_HandleAddLightStyleToSceneCmd,
		(refCmdHandler_t)R_HandleRenderSceneCmd,
		(refCmdHandler_t)R_HandleSetScissorCmd,
		(refCmdHandler_t)R_HandleResetScissorCmd,
		(refCmdHandler_t)R_HandleDrawStretchRawCmd,
		(refCmdHandler_t)R_HandleDrawStretchRawYUVCmd,
	};
	size_t t;

	for( t = 0; t < cmdbuf->len; ) {
		uint8_t *cmd = cmdbuf->buf + t;
		int id = *(int *)cmd;
			
		if( id < 0 || id >= NUM_REF_CMDS )
			break;
			
		size_t len = refCmdHandlers[id]( cmd );
		
		if( len == 0 )
			break;
			
		t += len;
	}
}

static void RF_ClearCmdBuf( ref_cmdbuf_t *cmdbuf )
{
	cmdbuf->len = 0;
}

static void RF_SetCmdBufFrameId( ref_cmdbuf_t *cmdbuf, unsigned frameId )
{
	cmdbuf->frameId = frameId;
}

static unsigned RF_GetCmdBufFrameId( ref_cmdbuf_t *cmdbuf )
{
	return cmdbuf->frameId;
}

ref_cmdbuf_t *RF_CreateCmdBuf( void )
{
	ref_cmdbuf_t *cmdbuf;

	cmdbuf = R_Malloc( sizeof( *cmdbuf ) );
	cmdbuf->BeginFrame = &RF_IssueBeginFrameCmd;
	cmdbuf->EndFrame = &RF_IssueEndFrameCmd;
	cmdbuf->DrawRotatedStretchPic = &RF_IssueDrawRotatedStretchPicCmd;
	cmdbuf->DrawStretchPoly = &RF_IssueDrawStretchPolyCmd;
	cmdbuf->ClearScene = &RF_IssueClearSceneCmd;
	cmdbuf->AddEntityToScene = &RF_IssueAddEntityToSceneCmd;
	cmdbuf->AddLightToScene = &RF_IssueAddLightToSceneCmd;
	cmdbuf->AddPolyToScene = &RF_IssueAddPolyToSceneCmd;
	cmdbuf->AddLightStyleToScene = &RF_IssueAddLightStyleToSceneCmd;
	cmdbuf->RenderScene = &RF_IssueRenderSceneCmd;
	cmdbuf->SetScissor = &RF_IssueSetScissorCmd;
	cmdbuf->ResetScissor = &RF_IssueResetScissorCmd;
	cmdbuf->DrawStretchRaw = &RF_IssueDrawStretchRawCmd;
	cmdbuf->DrawStretchRawYUV = &RF_IssueDrawStretchRawYUVCmd;

	cmdbuf->Clear = &RF_ClearCmdBuf;
	cmdbuf->SetFrameId = &RF_SetCmdBufFrameId;
	cmdbuf->GetFrameId = &RF_GetCmdBufFrameId;
	cmdbuf->RunCmds = &RF_RunCmdBufProc;

	return cmdbuf;
}

void RF_DestroyCmdBuf( ref_cmdbuf_t **pcmdbuf )
{
	if( !pcmdbuf || !*pcmdbuf )
		return;
	R_Free( *pcmdbuf );
	*pcmdbuf = NULL;
}

/*
=============================================================

INTER-FRAME COMMANDS PIPE

=============================================================
*/

typedef struct
{
	int             id;
} refReliableCmdInitShutdown_t;

typedef struct
{
	int             id;
} refReliableCmdSurfaceChange_t;

typedef struct
{
	int             id;
	unsigned        pixels;
	bool            silent;
	bool            media;
	int             x, y, w, h;
	char			fmtstring[64];
	char            path[512];
	char            name[512];
} refReliableCmdScreenShot_t;

typedef struct
{
	int             id;
} refReliableCmdBeginEndRegistration_t;

typedef struct
{
	int             id;
	int             num;
	int             r, g, b;
} refReliableCmdSetCustomColor_t;

typedef struct
{
	int				id;
	vec3_t			wall, floor;
} refReliableCmdSetWallFloorColors_t;

typedef struct
{
	int             id;
	char			drawbuffer[32];
} refReliableCmdSetDrawBuffer_t;

typedef struct
{
	int             id;
	char			texturemode[32];
} refReliableCmdSetTextureMode_t;

typedef struct
{
	int             id;
	char			filter;
} refReliableCmdSetTextureFilter_t;

typedef struct
{
	int             id;
	float			gamma;
} refReliableCmdSetGamma_t;

static unsigned R_HandleInitReliableCmd( void *pcmd );
static unsigned R_HandleShutdownReliableCmd( void *pcmd );
static unsigned R_HandleSurfaceChangeReliableCmd( void *pcmd );
static unsigned R_HandleScreenShotReliableCmd( void *pcmd );
static unsigned R_HandleEnvShotReliableCmd( void *pcmd );
static unsigned R_HandleBeginRegistrationReliableCmd( void *pcmd );
static unsigned R_HandleEndRegistrationReliableCmd( void *pcmd );
static unsigned R_HandleSetCustomColorReliableCmd( void *pcmd );
static unsigned R_HandleSetWallFloorColorsReliableCmd( void *pcmd );
static unsigned R_HandleSetDrawBufferReliableCmd( void *pcmd );
static unsigned R_HandleSetTextureModeReliableCmd( void *pcmd );
static unsigned R_HandleSetTextureFilterReliableCmd( void *pcmd );
static unsigned R_HandleSetGammaReliableCmd( void *pcmd );

refPipeCmdHandler_t refPipeCmdHandlers[NUM_REF_PIPE_CMDS] =
{
	(refPipeCmdHandler_t)R_HandleInitReliableCmd,
	(refPipeCmdHandler_t)R_HandleShutdownReliableCmd,
	(refPipeCmdHandler_t)R_HandleSurfaceChangeReliableCmd,
	(refPipeCmdHandler_t)R_HandleScreenShotReliableCmd,
	(refPipeCmdHandler_t)R_HandleEnvShotReliableCmd,
	(refPipeCmdHandler_t)R_HandleBeginRegistrationReliableCmd,
	(refPipeCmdHandler_t)R_HandleEndRegistrationReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetCustomColorReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetWallFloorColorsReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetDrawBufferReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetTextureModeReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetTextureFilterReliableCmd,
	(refPipeCmdHandler_t)R_HandleSetGammaReliableCmd,
};

static unsigned R_HandleInitReliableCmd( void *pcmd )
{
	refReliableCmdInitShutdown_t *cmd = pcmd;

	RB_Init();

	RFB_Init();

	R_InitBuiltinScreenImages();

	R_BindFrameBufferObject( 0 );

	return sizeof( *cmd );
}

static unsigned R_HandleShutdownReliableCmd( void *pcmd )
{
	refReliableCmdInitShutdown_t *cmd = pcmd;

	R_ReleaseBuiltinScreenImages();

	RB_Shutdown();

	RFB_Shutdown();

	return sizeof( *cmd );
}

static unsigned R_HandleSurfaceChangeReliableCmd( void *pcmd )
{
	refReliableCmdSurfaceChange_t *cmd = pcmd;

	GLimp_UpdatePendingWindowSurface();

	return sizeof( *cmd );
}

static unsigned R_HandleScreenShotReliableCmd( void *pcmd )
{
	refReliableCmdScreenShot_t *cmd = pcmd;

	R_TakeScreenShot( cmd->path, cmd->name, cmd->fmtstring, cmd->x, cmd->y, cmd->w, cmd->h, cmd->silent, cmd->media );

	return sizeof( *cmd );
}

static unsigned R_HandleEnvShotReliableCmd( void *pcmd )
{
	refReliableCmdScreenShot_t *cmd = pcmd;

	R_TakeEnvShot( cmd->path, cmd->name, cmd->pixels );

	return sizeof( *cmd );
}

static unsigned R_HandleBeginRegistrationReliableCmd( void *pcmd )
{
	refReliableCmdBeginEndRegistration_t *cmd = pcmd;

	RB_BeginRegistration();

	return sizeof( *cmd );
}

static unsigned R_HandleEndRegistrationReliableCmd( void *pcmd )
{
	refReliableCmdBeginEndRegistration_t *cmd = pcmd;

	RB_EndRegistration();

	RFB_FreeUnusedObjects();

	return sizeof( *cmd );
}

static unsigned R_HandleSetCustomColorReliableCmd( void *pcmd )
{
	refReliableCmdSetCustomColor_t *cmd = pcmd;

	R_SetCustomColor( cmd->num, cmd->r, cmd->g, cmd->b );

	return sizeof( *cmd );
}

static unsigned R_HandleSetWallFloorColorsReliableCmd( void *pcmd )
{
	refReliableCmdSetWallFloorColors_t *cmd = pcmd;
	
	R_SetWallFloorColors( cmd->wall, cmd->floor );
	
	return sizeof( *cmd );
}

static unsigned R_HandleSetDrawBufferReliableCmd( void *pcmd )
{
	refReliableCmdSetDrawBuffer_t *cmd = pcmd;
	
	R_SetDrawBuffer( cmd->drawbuffer );

	return sizeof( *cmd );
}

static unsigned R_HandleSetTextureModeReliableCmd( void *pcmd )
{
	refReliableCmdSetTextureMode_t *cmd = pcmd;
	
	R_TextureMode( cmd->texturemode );
	
	return sizeof( *cmd );
}

static unsigned R_HandleSetTextureFilterReliableCmd( void *pcmd )
{
	refReliableCmdSetTextureFilter_t *cmd = pcmd;

	R_AnisotropicFilter( cmd->filter );

	return sizeof( *cmd );
}

static unsigned R_HandleSetGammaReliableCmd( void *pcmd )
{
	refReliableCmdSetGamma_t *cmd = pcmd;

	R_SetGamma( cmd->gamma );

	return sizeof( *cmd );
}

// ============================================================================

void RF_IssueInitReliableCmd( qbufPipe_t *pipe )
{
	refReliableCmdInitShutdown_t cmd = { REF_PIPE_CMD_INIT };
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueShutdownReliableCmd( qbufPipe_t *pipe )
{
	refReliableCmdInitShutdown_t cmd = { REF_PIPE_CMD_SHUTDOWN };
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSurfaceChangeReliableCmd( qbufPipe_t *pipe )
{
	refReliableCmdSurfaceChange_t cmd = { REF_PIPE_CMD_SURFACE_CHANGE };
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

static void RF_IssueEnvScreenShotReliableCmd( qbufPipe_t *pipe, int id, const char *path, const char *name,
	const char *fmtstring, int x, int y, int w, int h, unsigned pixels, bool silent, bool media )
{
	refReliableCmdScreenShot_t cmd = { 0 };

	cmd.id = id;
	cmd.x = x;
	cmd.y = y;
	cmd.w = w;
	cmd.h = h;
	cmd.pixels = pixels;
	cmd.silent = silent;
	cmd.media = media;
	Q_strncpyz( cmd.path, path, sizeof( cmd.path ) );
	Q_strncpyz( cmd.name, name, sizeof( cmd.name ) );
	Q_strncpyz( cmd.fmtstring, fmtstring, sizeof( cmd.fmtstring ) );

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueScreenShotReliableCmd( qbufPipe_t *pipe, const char *path, const char *name, const char *fmtstring, bool silent )
{
	RF_IssueEnvScreenShotReliableCmd( pipe, REF_PIPE_CMD_SCREEN_SHOT, path, name, fmtstring, 0, 0, glConfig.width, glConfig.height, 0, silent, true );
}

void RF_IssueEnvShotReliableCmd( qbufPipe_t *pipe, const char *path, const char *name, unsigned pixels )
{
	RF_IssueEnvScreenShotReliableCmd( pipe, REF_PIPE_CMD_ENV_SHOT, path, name, "", 0, 0, glConfig.width, glConfig.height, pixels, false, false );
}

void RF_IssueAviShotReliableCmd( qbufPipe_t *pipe, const char *path, const char *name, int x, int y, int w, int h )
{
	RF_IssueEnvScreenShotReliableCmd( pipe, REF_PIPE_CMD_SCREEN_SHOT, path, name, "", x, y, w, h, 0, true, false );
}

void RF_IssueBeginRegistrationReliableCmd( qbufPipe_t *pipe )
{
	refReliableCmdBeginEndRegistration_t cmd = { REF_PIPE_CMD_BEGIN_REGISTRATION };
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueEndRegistrationReliableCmd( qbufPipe_t *pipe )
{
	refReliableCmdBeginEndRegistration_t cmd = { REF_PIPE_CMD_END_REGISTRATION };
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetCustomColorReliableCmd( qbufPipe_t *pipe, int num, int r, int g, int b )
{
	refReliableCmdSetCustomColor_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_CUSTOM_COLOR;
	cmd.num = num;
	cmd.r = r;
	cmd.g = g;
	cmd.b = b;
	
	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetWallFloorColorsReliableCmd( qbufPipe_t *pipe, const vec3_t wallColor, const vec3_t floorColor )
{
	refReliableCmdSetWallFloorColors_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_WALL_FLOOR_COLORS;
	VectorCopy( wallColor, cmd.wall );
	VectorCopy( floorColor, cmd.floor );

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetDrawBufferReliableCmd( qbufPipe_t *pipe, const char *drawbuffer )
{
	refReliableCmdSetDrawBuffer_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_DRAWBUFFER;
	Q_strncpyz( cmd.drawbuffer, drawbuffer, sizeof( cmd.drawbuffer ) );

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetTextureModeReliableCmd( qbufPipe_t *pipe, const char *texturemode )
{
	refReliableCmdSetTextureMode_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_TEXTURE_MODE;
	Q_strncpyz( cmd.texturemode, texturemode, sizeof( cmd.texturemode ) );

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetTextureFilterReliableCmd( qbufPipe_t *pipe, int filter )
{
	refReliableCmdSetTextureFilter_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_TEXTURE_FILTER;
	cmd.filter = filter;

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}

void RF_IssueSetGammaReliableCmd( qbufPipe_t *pipe, float gamma )
{
	refReliableCmdSetGamma_t cmd;
	
	cmd.id = REF_PIPE_CMD_SET_GAMMA;
	cmd.gamma = gamma;

	ri.BufPipe_WriteCmd( pipe, &cmd, sizeof( cmd ) );
}
