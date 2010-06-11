/* -*- c -*- *****************************************************************
** Copyright (C) 2003 Sandia Corporation
** Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
** license for use of this work by or on behalf of the U.S. Government.
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that this Notice and any statement
** of authorship are reproduced on all copies.
**
** This test checks to make sure the size of compressed images never
** exceeds the advertised maximum buffer size.
*****************************************************************************/

/*TODO: Eventually we should probably remove OpenGL dependence in this test.*/
#include <IceTGL.h>
#include "test_codes.h"
#include "test-util.h"

#include <image.h>
#include <state.h>

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static void draw(void)
{
    printf("In draw\n");
  /*I really don't care what Ice-T set the projection to.*/
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
    glVertex3f(-1.0, -1.0, 0.0);
    glVertex3f(1.0, -1.0, 0.0);
    glVertex3f(1.0, 1.0, 0.0);
    glVertex3f(-1.0, 1.0, 0.0);
    glEnd();
}

static void InitPathologicalImage(IceTImage image)
{
  /* Create a worst case possible for image with respect to compression.
     Every other pixel is active so the run lengths are all 1. */
    IceTEnum format;
    IceTSizeType num_pixels;

    num_pixels = icetImageGetSize(image);

    format = icetImageGetColorFormat(image);
    if (format == ICET_IMAGE_COLOR_RGBA_UBYTE) {
        IceTUByte *buffer = icetImageGetColorUByte(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[4*i + 0] = 255*(IceTUByte)(i%2);
            buffer[4*i + 1] = 255*(IceTUByte)(i%2);
            buffer[4*i + 2] = 255*(IceTUByte)(i%2);
            buffer[4*i + 3] = 255*(IceTUByte)(i%2);
        }
    } else if (format == ICET_IMAGE_COLOR_RGBA_FLOAT) {
        IceTFloat *buffer = icetImageGetColorFloat(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[4*i + 0] = (IceTFloat)(i%2);
            buffer[4*i + 1] = (IceTFloat)(i%2);
            buffer[4*i + 2] = (IceTFloat)(i%2);
            buffer[4*i + 3] = (IceTFloat)(i%2);
        }
    } else if (format != ICET_IMAGE_COLOR_NONE) {
        printf("*** Unknown color format? ***\n");
    }

    format = icetImageGetDepthFormat(image);
    if (format == ICET_IMAGE_DEPTH_FLOAT) {
        IceTFloat *buffer = icetImageGetDepthFloat(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[i] = (IceTFloat)(i%2);
        }
    } else if (format != ICET_IMAGE_DEPTH_NONE) {
        printf("*** Unknown depth format? ***\n");
    }
}

static void InitActiveImage(IceTImage image)
{
  /* Create a worst case possible for image with respect to compression.
     All the pixels are active, so no data can be removed. */
    IceTEnum format;
    IceTSizeType num_pixels;
    int seed;

    seed = time(NULL);
    srand(seed);

    num_pixels = icetImageGetSize(image);

    format = icetImageGetColorFormat(image);
    if (format == ICET_IMAGE_COLOR_RGBA_UBYTE) {
        IceTUByte *buffer = icetImageGetColorUByte(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[4*i + 0] = (IceTUByte)(rand()%255 + 1);
            buffer[4*i + 1] = (IceTUByte)(rand()%255 + 1);
            buffer[4*i + 2] = (IceTUByte)(rand()%255 + 1);
            buffer[4*i + 3] = (IceTUByte)(rand()%255 + 1);
        }
    } else if (format == ICET_IMAGE_COLOR_RGBA_FLOAT) {
        IceTFloat *buffer = icetImageGetColorFloat(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[4*i + 0] = ((IceTFloat)(rand()%255 + 1))/255;
            buffer[4*i + 1] = ((IceTFloat)(rand()%255 + 1))/255;
            buffer[4*i + 2] = ((IceTFloat)(rand()%255 + 1))/255;
            buffer[4*i + 3] = ((IceTFloat)(rand()%255 + 1))/255;
        }
    } else if (format != ICET_IMAGE_COLOR_NONE) {
        printf("*** Unknown color format? ***\n");
    }

    format = icetImageGetDepthFormat(image);
    if (format == ICET_IMAGE_DEPTH_FLOAT) {
        IceTFloat *buffer = icetImageGetDepthFloat(image);
        IceTSizeType i;
        for (i = 0; i < num_pixels; i++) {
            buffer[i] = ((IceTFloat)(rand()%255))/255;
        }
    } else if (format != ICET_IMAGE_DEPTH_NONE) {
        printf("*** Unknown depth format? ***\n");
    }
}

static int DoCompressionTest(IceTEnum color_format, IceTEnum depth_format,
                             IceTEnum composite_mode)
{
    GLint viewport[4];
    int pixels;
    IceTImage image;
    IceTVoid *imagebuffer;
    IceTSizeType imagesize;
    IceTSparseImage compressedimage;
    IceTVoid *compressedbuffer;
    IceTSizeType compressedsize;
    IceTSizeType color_pixel_size;
    IceTSizeType depth_pixel_size;
    IceTSizeType pixel_size;
    IceTSizeType size;
    int result;

    result = TEST_PASSED;

    printf("Using color format of 0x%x\n", (int)color_format);
    printf("Using depth format of 0x%x\n", (int)depth_format);
    printf("Using composite mode of 0x%x\n", (int)composite_mode);

    icetCompositeMode(composite_mode);

    glGetIntegerv(GL_VIEWPORT, viewport);
    pixels = viewport[2]*viewport[3];

    printf("Allocating memory for %d pixel image.\n", pixels);
    imagesize = icetImageBufferSize(color_format, depth_format, pixels);
    if (imagesize > icetImageMaxBufferSize(pixels)) {
         printf("Current image format larger than max size!!!!\n");
         result = TEST_FAILED;
    }
    imagebuffer = malloc(imagesize);
    image = icetImageInitialize(imagebuffer,color_format,depth_format,pixels);

    compressedsize
        = icetSparseImageBufferSize(color_format, depth_format, pixels);
    if (compressedsize > icetSparseImageMaxBufferSize(pixels)) {
        printf("Current sparse image format larger than max size!!!!!\n");
        result = TEST_FAILED;
    }
    compressedbuffer = malloc(compressedsize);

  /* Get the number of bytes per pixel.  This is used in checking the
     size of compressed images. */
    icetImageGetColorVoid(image, &color_pixel_size);
    icetImageGetDepthVoid(image, &depth_pixel_size);
    pixel_size = color_pixel_size + depth_pixel_size;

    printf("\nCreating worst possible image (with respect to compression).\n");
    InitPathologicalImage(image);

    printf("Compressing image.\n");
    compressedimage = icetCompressImage(image, compressedbuffer);
    size = icetSparseImageGetCompressedBufferSize(compressedimage);
    printf("Expected size: %d.  Actual size: %d\n",
           (int)(pixel_size*(pixels/2)), (int)size);
    if (   (size > compressedsize)
        || (size < pixel_size*(pixels/2)) ) {
        printf("Size differs from expected size!\n");
        result = TEST_FAILED;
    }

    printf("\nCreating a different worst possible image.\n");
    InitActiveImage(image);
    printf("Compressing image.\n");
    compressedimage = icetCompressImage(image, compressedbuffer);
    size = icetSparseImageGetCompressedBufferSize(compressedimage);
    printf("Expected size: %d.  Actual size: %d\n",
           (int)imagesize, (int)size);
    if ((size > compressedsize) || (size < pixel_size*pixels)) {
        printf("Size differs from expected size!\n");
        result = TEST_FAILED;
    }

    printf("\nCompressing zero size image.\n");
    image = icetImageInitialize(imagebuffer, color_format, depth_format, 0);
    compressedimage = icetCompressImage(image, compressedbuffer);
    size = icetSparseImageGetCompressedBufferSize(compressedimage);
    printf("Expected size: %d.  Actual size: %d\n",
           (int)icetSparseImageBufferSize(color_format, depth_format, 0),
           (int)size);
    if (size > icetSparseImageBufferSize(color_format,depth_format,0)) {
        printf("Size differs from expected size!\n");
        result = TEST_FAILED;
    }

  /* This test can be a little volatile.  The icetGetCompressedTileImage
   * expects certain things to be set correctly by the icetDrawFrame
   * function.  Since we want to call icetGetCompressedTileImage directly,
   * we try to set up these parameters by hand.  It is possible for this
   * test to incorrectly fail if the two functions are mutually changed and
   * this scaffolding is not updated correctly. */
    printf("\nSetup for actual render.\n");
    icetResetTiles();
    icetAddTile(viewport[0], viewport[1], viewport[2], viewport[3], 0);
    icetDrawFunc(draw);
    icetGLSetColorFormat(color_format);
    icetGLSetDepthFormat(depth_format);
  /* Do a perfunctory draw to set other state variables. */
    icetDrawFrame();
    icetStateSetIntegerv(ICET_CONTAINED_VIEWPORT, 4, viewport);
    printf("Now render and get compressed image.\n");
    compressedimage = icetGetCompressedTileImage(0, compressedbuffer);
    size = icetSparseImageGetCompressedBufferSize(compressedimage);
    printf("Expected size: %d.  Actual size: %d\n",
           (int)compressedsize, (int)size);
    if ((size > compressedsize) || (size < pixel_size*pixels)) {
        printf("Size differs from expected size!\n");
        result = TEST_FAILED;
    }

    printf("Cleaning up.\n");
    free(imagebuffer);
    free(compressedbuffer);
    return result;
}

static int CompressionSizeRun()
{
    int result;

    icetStrategy(ICET_STRATEGY_REDUCE);

    printf("Compress depth only.\n");
    result = DoCompressionTest(ICET_IMAGE_COLOR_NONE, ICET_IMAGE_DEPTH_FLOAT,
                               ICET_COMPOSITE_MODE_Z_BUFFER);

    printf("\n\nCompress 8-bit color only.\n");
    if (result == TEST_PASSED) {
        result = DoCompressionTest(ICET_IMAGE_COLOR_RGBA_UBYTE,
                                   ICET_IMAGE_DEPTH_NONE,
                                   ICET_COMPOSITE_MODE_BLEND);
    } else {
        DoCompressionTest(ICET_IMAGE_COLOR_RGBA_UBYTE,
                          ICET_IMAGE_DEPTH_NONE,
                          ICET_COMPOSITE_MODE_BLEND);
    }

    printf("\n\nCompress 32-bit color only.\n");
    if (result == TEST_PASSED) {
        result = DoCompressionTest(ICET_IMAGE_COLOR_RGBA_FLOAT,
                                   ICET_IMAGE_DEPTH_NONE,
                                   ICET_COMPOSITE_MODE_BLEND);
    } else {
        DoCompressionTest(ICET_IMAGE_COLOR_RGBA_FLOAT,
                          ICET_IMAGE_DEPTH_NONE,
                          ICET_COMPOSITE_MODE_BLEND);
    }

    printf("\n\nCompress depth and 8-bit color.\n");
    if (result == TEST_PASSED) {
        result = DoCompressionTest(ICET_IMAGE_COLOR_RGBA_UBYTE,
                                   ICET_IMAGE_DEPTH_FLOAT,
                                   ICET_COMPOSITE_MODE_Z_BUFFER);
    } else {
        DoCompressionTest(ICET_IMAGE_COLOR_RGBA_UBYTE,
                          ICET_IMAGE_DEPTH_FLOAT,
                          ICET_COMPOSITE_MODE_Z_BUFFER);
    }

    printf("\n\nCompress depth and 32-bit color.\n");
    if (result == TEST_PASSED) {
        result = DoCompressionTest(ICET_IMAGE_COLOR_RGBA_FLOAT,
                                   ICET_IMAGE_DEPTH_FLOAT,
                                   ICET_COMPOSITE_MODE_Z_BUFFER);
    } else {
        DoCompressionTest(ICET_IMAGE_COLOR_RGBA_FLOAT,
                          ICET_IMAGE_DEPTH_FLOAT,
                          ICET_COMPOSITE_MODE_Z_BUFFER);
    }

    return result;
}

int CompressionSize(int argc, char *argv[])
{
    /* To remove warning */
    (void)argc;
    (void)argv;

    return run_test(CompressionSizeRun);
}
