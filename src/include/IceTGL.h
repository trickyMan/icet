/* -*- c -*- *******************************************************/
/*
 * Copyright (C) 2010 Sandia Corporation
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 */

#ifndef _ICET_GL_H_
#define _ICET_GL_H_

#include <IceT.h>

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

ICET_GL_EXPORT void icetGLInitialize(void);

ICET_GL_EXPORT void icetGLSetReadBuffer(GLenum mode);

ICET_GL_EXPORT IceTImage icetGLDrawFrame(void);

#define ICET_GL_STATE_START (IceTEnum)0x00000140

#define ICET_GL_INITIALIZED     (ICET_GL_STATE_START | (IceTEnum)0x0001)

#define ICET_GL_READ_BUFFER     (ICET_GL_STATE_START | (IceTEnum)0x0010)

#define ICET_GL_STATE_ENABLE_START (ICET_STATE_ENABLE_START | (IceTEnum)0x0030)

/* These should go to GL layer. */
#define ICET_GL_DISPLAY         (ICET_GL_STATE_ENABLE_START | (IceTEnum)0x0000)
#define ICET_GL_DISPLAY_COLORED_BACKGROUND (ICET_GL_STATE_ENABLE_START | (IceTEnum)0x0001)
#define ICET_GL_DISPLAY_INFLATE (ICET_GL_STATE_ENABLE_START | (IceTEnum)0x0002)
#define ICET_GL_DISPLAY_INFLATE_WITH_HARDWARE (ICET_GL_STATE_ENABLE_START | (IceTEnum)0x0003)


#ifdef __cplusplus
}
#endif

#endif /* _ICET_GL_H_ */
