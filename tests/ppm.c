/* -*- c -*- *******************************************************/
/*
 * Copyright (C) 2003 Sandia Corporation
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that this Notice and any statement
 * of authorship are reproduced on all copies.
 */

#include "test-util.h"

#include <stdlib.h>
#include <stdio.h>

void write_ppm(const char *filename,
	       const GLubyte *image,
	       int width, int height)
{
    FILE *fd;
    int x, y;
    const unsigned char *color;
    GLint color_format;

    icetGetIntegerv(ICET_COLOR_FORMAT, &color_format);

    fd = fopen(filename, "wb");

    fprintf(fd, "P6\n");
    fprintf(fd, "# %s generated by ICE-T test suite.\n", filename);
    fprintf(fd, "%d %d\n", width, height);
    fprintf(fd, "255\n");

    for (y = height-1; y >= 0; y--) {
	color = image + y*width*4;
	for (x = 0; x < width; x++) {
	    switch (color_format) {
	      case GL_RGBA:
		  fwrite(color, 1, 3, fd);
		  break;
#ifdef GL_BGRA_EXT
	      case GL_BGRA_EXT:
		  fwrite(color+2, 1, 1, fd);
		  fwrite(color+1, 1, 1, fd);
		  fwrite(color+0, 1, 1, fd);
		  break;
#endif
	      default:
		  printf("Bad color format.\n");
		  return;
	    }
	    color += 4;
	}
    }

    fclose(fd);
}
