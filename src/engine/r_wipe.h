// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1997 Midway Home Entertainment, Inc
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

#ifndef D3DF_WIPE_H
#define D3DF_WIPE_H

void WIPE_FadeScreen(int fadetics);
void WIPE_MeltScreen(void);

// The screen the next wipe starts from, taken from the back buffer before the swap (it reads it back after,
// otherwise: by then what's there is the driver's business, black under ANGLE's vulkan)
void WIPE_CaptureScreen(void);
// The same, drawing the screen with this drawer first (outside the frame loop's frames)
void WIPE_CaptureDrawer(void (*draw)(void));
// The captured screen goes unused (no wipe came of it)
void WIPE_DiscardCapture(void);

#endif
