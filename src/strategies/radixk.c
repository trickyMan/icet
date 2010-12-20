/* -*- c -*- *******************************************************/
/*
 * Copyright (C) 2010 Sandia Corporation
 * Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
 * the U.S. Government retains certain rights in this software.
 *
 * This source code is released under the New BSD License.
 */

/* The Radix-k algorithm was designed by Tom Peterka at Argonne National
   Laboratory.

   Copyright (c) University of Chicago
   Permission is hereby granted to use, reproduce, prepare derivative works, and
   to redistribute to others.

   The Radix-k algorithm was ported to IceT by Wesley Kendall from University
   of Tennessee at Knoxville.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <string.h>

#include <IceT.h>

#include <IceTDevCommunication.h>
#include <IceTDevDiagnostics.h>
#include <IceTDevImage.h>

#define RADIXK_SWAP_IMAGE_TAG_START     2200
#define SWAP_IMAGE_DATA 21
#define SWAP_DEPTH_DATA 22

#define MAGIC_K 8 /* K value that is attempted to be used each round */

#define RADIXK_RECEIVE_BUFFER                   ICET_SI_STRATEGY_BUFFER_0
#define RADIXK_SEND_BUFFER                      ICET_SI_STRATEGY_BUFFER_1
#define RADIXK_PARTITION_INDICES_BUFFER         ICET_SI_STRATEGY_BUFFER_2
#define RADIXK_PARTITION_INFO_BUFFER            ICET_SI_STRATEGY_BUFFER_3
#define RADIXK_RECEIVE_REQUEST_BUFFER           ICET_SI_STRATEGY_BUFFER_4
#define RADIXK_SEND_REQUEST_BUFFER              ICET_SI_STRATEGY_BUFFER_5
#define RADIXK_SIZES_BUFFER                     ICET_SI_STRATEGY_BUFFER_6
#define RADIXK_OFFSETS_BUFFER                   ICET_SI_STRATEGY_BUFFER_7
#define RADIXK_FACTORS_ARRAY                    ICET_SI_STRATEGY_BUFFER_8

typedef struct {
    int rank; /* Rank of partner. */
    IceTSizeType offset; /* Offset of partner's partition in image. */
    IceTSizeType size; /* Size of partner's partition. */
    IceTVoid *receiveBuffer; /* A buffer for receiving data from partner. */
    IceTVoid *sendBuffer; /* A buffer to hold data being sent to partner. */
    IceTBoolean hasArrived; /* True when message arrives. */
    IceTBoolean isComposited; /* True when received image is composited. */
} radixkPartnerInfo;

static int* radixkGetK(int world_size, int* num_rounds_p)
{
    /* Divide the world size into groups that are closest to the magic k
       value. */
    int* k_array;
    int max_num_k;
    int num_groups = 0;
    int next_divide = world_size;

    /* The maximum number of factors possible is the floor of log base 2. */
    max_num_k = (int)(floor(log10(world_size)/log10(2)));
    k_array = icetGetStateBuffer(RADIXK_FACTORS_ARRAY, sizeof(int) * max_num_k);

    while (next_divide > 1) {
        int next_k = -1;

        /* If the magic k value is perfectly divisible by the next_divide
           size, we are good to go */
        if ((next_divide % MAGIC_K) == 0) {
            next_k = MAGIC_K;
        }

        /* If that does not work, look for a factor near the magic_k. */
        if (next_k == -1) {
            int magic_k_distance;
            for (magic_k_distance = 1;
                 magic_k_distance <= MAGIC_K-2;
                 magic_k_distance++) {
                if ((next_divide % (MAGIC_K-magic_k_distance)) == 0) {
                    next_k = MAGIC_K-magic_k_distance;
                    break;
                }
                if ((next_divide % (MAGIC_K+magic_k_distance)) == 0) {
                    next_k = MAGIC_K+magic_k_distance;
                    break;
                }
            }
        }

        /* If you STILL don't have a good factor, progress upwards to find the
           best match. */
        if (next_k == -1) {
            int try_k;
            int max_k;

            /* The largest possible smallest factor (other than next_divide
               itself) is the square root of next_divide.  We don't have to
               check the values between the square root and next_divide. */
            max_k = (int)floor(sqrt(next_divide));

            /* It would be better to just visit prime numbers, but other than
               having a huge table, how would you do that?  Hopefully this is an
               uncommon use case. */
            for (try_k = MAGIC_K+1; try_k < max_k; try_k++) {
                if ((next_divide % try_k) == 0) {
                    next_k = try_k;
                    break;
                }
            }
        }

        /* If we STILL don't have a factor, then next_division must be a large
           prime.  Basically give up by using next_divide as the next k. */
        if (next_k == -1) {
            next_k = next_divide;
        }

        /* Set the k value in the array. */
        k_array[num_groups] = next_k;
        next_divide /= next_k;
        num_groups++;

        if (num_groups > max_num_k) {
            icetRaiseError("Somehow we got more factors than possible.",
                           ICET_SANITY_CHECK_FAIL);
        }
    }

    /* Sanity check to make sure that the k's actually multiply to the number
     * of processes. */
    {
        int product = k_array[0];
        int i;
        for (i = 1; i < num_groups; ++i) {
            product *= k_array[i];
        }
        if (product != world_size) {
            icetRaiseError("Product of k's not equal to number of processes.",
                           ICET_SANITY_CHECK_FAIL);
        }
    }

    *num_rounds_p = num_groups;
    return k_array;
}

/* radixkGetPartitionIndices

   my position in each round forms an num_rounds-dimensional vector
   [round 0 pos, round 1 pos, ... round num_rounds-1 pos]
   where pos is my position in the group of partners within that round

   inputs:
     num_rounds: number of rounds
     k_array: vector of k values
     group_rank: my rank in composite order (compose_group in icetRadixkCompose)

   outputs:
     partition_indices: index of my partition for each round.
*/
static void radixkGetPartitionIndices(int num_rounds,
                                      const int *k_array,
                                      int group_rank,
                                      int *partition_indices)
{

    int step; /* step size in rank for a lattice direction */
    int i;

    step = 1;
    for (i = 0; i < num_rounds; i++) {
        partition_indices[i] = (group_rank / step) % k_array[i];
        step *= k_array[i];
    }

}

/* radixkGetPartners
   gets the ranks of my trading partners

   inputs:
    k_array: vector of k values
    current_round: current round number (0 to num_rounds - 1)
    partition_index: image partition to collect (0 to k[current_round] - 1)
    compose_group: array of world ranks representing the group of processes
        participating in compositing (passed into icetRadixkCompose)
    group_rank: Index in compose_group that represents me
    start_offset: Start of partition that is being divided in current_round
    start_size: Size of partition that is being divided in current_round

   output:
    partners: Array of radixkPartnerInfo describing all the processes
        participating in this round.
*/
static radixkPartnerInfo *radixkGetPartners(const int *k_array,
                                            int current_round,
                                            int partition_index,
                                            const int *compose_group,
                                            int group_rank,
                                            IceTSizeType start_offset,
                                            IceTSizeType start_size)
{
    int current_k = k_array[current_round];
    radixkPartnerInfo *partners
        = icetGetStateBuffer(RADIXK_PARTITION_INFO_BUFFER,
                             sizeof(radixkPartnerInfo) * current_k);
    int step; /* ranks jump by this much in the current round */
    IceTVoid *recv_buf_pool;
    IceTVoid *send_buf_pool;
    IceTSizeType sparse_image_size;
    int first_partner_group_rank;
    IceTSizeType next_offset;
    int i;

    step = 1;
    for (i = 0; i < current_round; i++) {
        step *= k_array[i];
    }

    /* Allocate arrays that can be used as send/receive buffers. */
    sparse_image_size = icetSparseImageBufferSize(start_size/current_k + 1, 1);
    recv_buf_pool = icetGetStateBuffer(RADIXK_RECEIVE_BUFFER,
                                       sparse_image_size * current_k);
    send_buf_pool = icetGetStateBuffer(RADIXK_SEND_BUFFER,
                                       sparse_image_size * current_k);

#ifdef DEBUG
    /* Defensive */
    memset(partners, 0xDC, sizeof(radixkPartnerInfo) * current_k);
    memset(recv_buf_pool, 0xDC, sparse_image_size * current_k);
    memset(send_buf_pool, 0xDC, sparse_image_size * current_k);
#endif

    first_partner_group_rank = group_rank - partition_index * step;
    next_offset = start_offset;
    for (i = 0; i < current_k; i++) {
        radixkPartnerInfo *p = &partners[i];
        int partner_group_rank = first_partner_group_rank + i*step;
        int remain;

        p->rank = compose_group[partner_group_rank];

        /* Distribute any remainder 1 element at a time from 0..(rem-1) */
        remain = (i < (start_size % current_k)) ? 1 : 0;
        p->offset = next_offset;
        p->size = (start_size/current_k) + remain;
        next_offset += p->size;

        p->receiveBuffer = ((IceTByte*)recv_buf_pool + i*sparse_image_size);
        p->sendBuffer = ((IceTByte*)send_buf_pool + i*sparse_image_size);

        p->hasArrived = ICET_FALSE;
        p->isComposited = ICET_FALSE;
    }

    return partners;
}

/* As applicable, posts an asynchronous receive for each process from which
   we are receiving an image piece. */
static IceTCommRequest *radixkPostReceives(radixkPartnerInfo *partners,
                                           int current_k,
                                           int current_round,
                                           int current_partition_index)
{
    IceTCommRequest *receive_requests;
    radixkPartnerInfo *me;
    IceTSizeType sparse_image_size;
    int tag;
    int i;

    me = &partners[current_partition_index];

    receive_requests = icetGetStateBuffer(RADIXK_RECEIVE_REQUEST_BUFFER,
                                          current_k * sizeof(IceTCommRequest));

    sparse_image_size = icetSparseImageBufferSize(me->size, 1);

    tag = RADIXK_SWAP_IMAGE_TAG_START + current_round;

    for (i = 0; i < current_k; i++) {
        radixkPartnerInfo *p = &partners[i];
        if (i != current_partition_index) {
            receive_requests[i] = icetCommIrecv(p->receiveBuffer,
                                                sparse_image_size,
                                                ICET_BYTE,
                                                p->rank,
                                                tag);
            p->hasArrived = ICET_FALSE;
            p->isComposited = ICET_FALSE;
        } else {
            /* No need to send to myself. */
            receive_requests[i] = ICET_COMM_REQUEST_NULL;
            p->hasArrived = ICET_TRUE;
            p->isComposited = ICET_TRUE;
        }
    }

    return receive_requests;
}

/* As applicable, posts an asynchronous send for each process to which we are
   sending an image piece. */
static IceTCommRequest *radixkPostSends(radixkPartnerInfo *partners,
                                        int current_k,
                                        int current_round,
                                        int current_partition_index,
                                        const IceTImage image)
{
    IceTCommRequest *send_requests;
    radixkPartnerInfo *me;
    int tag;
    int i;

    me = &partners[current_partition_index];

    send_requests = icetGetStateBuffer(RADIXK_SEND_REQUEST_BUFFER,
                                       current_k * sizeof(IceTCommRequest));


    tag = RADIXK_SWAP_IMAGE_TAG_START + current_round;

    /* TODO: post these in a different order so that images that can be
       composited first will show up first on each process. */
    for (i = 0; i < current_k; i++) {
        radixkPartnerInfo *p = &partners[i];
        if (i != current_partition_index) {
            IceTSparseImage compressed_image;
            IceTVoid *package_buffer;
            IceTSizeType package_size;

            compressed_image = icetSparseImageAssignBuffer(p->sendBuffer,
                                                           p->size, 1);
            icetCompressSubImage(image, p->offset, p->size, compressed_image);
            icetSparseImagePackageForSend(compressed_image,
                                          &package_buffer, &package_size);

            send_requests[i] = icetCommIsend(package_buffer,
                                             package_size,
                                             ICET_BYTE,
                                             p->rank,
                                             tag);
        } else {
            /* No need to send to myself. */
            send_requests[i] = ICET_COMM_REQUEST_NULL;
        }
    }

    return send_requests;
}

static void radixkCompositeIncomingImages(radixkPartnerInfo *partners,
                                          IceTCommRequest *receive_requests,
                                          int current_k,
                                          int current_partition_index,
                                          IceTImage image)
{
    radixkPartnerInfo *me = &partners[current_partition_index];
    IceTBoolean ordered_composite = icetIsEnabled(ICET_ORDERED_COMPOSITE);
    IceTBoolean done = ICET_FALSE;

    while (!done) {
        int receive_idx;
        int partner_idx;

        /* Wait for an image to come in. */
        receive_idx = icetCommWaitany(current_k, receive_requests);
        partners[receive_idx].hasArrived = ICET_TRUE;

        /* Check all images to see if anything is ready for compositing.  When
           doing an ordered composite, we can only composite images adjacent
           our rank or adjacent to one already composited. */
        done = ICET_TRUE;
        for (partner_idx = current_partition_index - 1;
             partner_idx >= 0;
             partner_idx--) {
            radixkPartnerInfo *p = &partners[partner_idx];
            if (p->hasArrived && !p->isComposited) {
                IceTSparseImage inSparseImage
                    = icetSparseImageUnpackageFromReceive(p->receiveBuffer);
                if (icetSparseImageGetNumPixels(inSparseImage) != me->size) {
                    icetRaiseError("Radix-k received image with wrong size.",
                                   ICET_SANITY_CHECK_FAIL);
                }
                icetCompressedSubComposite(image,
                                           me->offset,
                                           inSparseImage,
                                           ICET_SRC_ON_TOP);
                p->isComposited = ICET_TRUE;
            }
            if (!p->hasArrived || !p->isComposited) {
                done = ICET_FALSE;
                if (ordered_composite) {
                    break;
                }
            }
        }
        for (partner_idx = current_partition_index + 1;
             partner_idx < current_k;
             partner_idx++) {
            radixkPartnerInfo *p = &partners[partner_idx];
            if (p->hasArrived && !p->isComposited) {
                IceTSparseImage inSparseImage
                    = icetSparseImageUnpackageFromReceive(p->receiveBuffer);
                if (icetSparseImageGetNumPixels(inSparseImage) != me->size) {
                    icetRaiseError("Radix-k received image with wrong size.",
                                   ICET_SANITY_CHECK_FAIL);
                }
                icetCompressedSubComposite(image,
                                           me->offset,
                                           inSparseImage,
                                           ICET_DEST_ON_TOP);
                p->isComposited = ICET_TRUE;
            }
            if (!p->hasArrived || !p->isComposited) {
                done = ICET_FALSE;
                if (ordered_composite) {
                    break;
                }
            }
        }
    }
}

static void radixkGatherFinalImage(IceTInt* compose_group, IceTInt group_rank,
                                   IceTInt group_size, IceTInt image_dest,
                                   IceTSizeType offset, IceTSizeType size,
                                   IceTImage image)
{
    int i;
    IceTEnum color_format;
    IceTEnum depth_format;
    IceTCommRequest *requests;
    int *all_sizes;
    int* all_offsets;

    icetRaiseDebug("Collecting image data.");
    /* Adjust image for output as some buffers, such as depth, might be
       dropped. */
    icetImageAdjustForOutput(image);

    color_format = icetImageGetColorFormat(image);
    depth_format = icetImageGetDepthFormat(image);
    requests = icetGetStateBuffer(RADIXK_RECEIVE_REQUEST_BUFFER,
                                  group_size * sizeof(IceTCommRequest));

    /* TODO: Compute the sizes instead of communicate them. */ 
    /* Find out the sizes of each process. */
    all_sizes = icetGetStateBuffer(RADIXK_SIZES_BUFFER,
                                   sizeof(int) * group_size);
    if (group_rank == image_dest) {
        all_sizes[group_rank] = (int)size;
        for (i = 0; i < group_size; i++) {
            if (i != group_rank) {
                requests[i] = icetCommIrecv(&(all_sizes[i]), 1, ICET_INT,
                                            compose_group[i], SWAP_IMAGE_DATA);
            } else {
                requests[i] = ICET_COMM_REQUEST_NULL;
            }
        }
        for (i = 0; i < group_size; i++) {
            icetCommWait(requests + i);
        }
    } else {
        icetCommSend(&size, 1, ICET_INT, compose_group[image_dest],
                     SWAP_IMAGE_DATA);
    }

    /* Compute all the offsets. */
    all_offsets = icetGetStateBuffer(RADIXK_OFFSETS_BUFFER,
                                     sizeof(int) * group_size);
    all_offsets[0] = 0;
    for (i = 1; i < group_size; i++) {
        all_offsets[i] = all_offsets[i - 1] + all_sizes[i - 1];
    }

    /* Exchange color and depth data. */
    if (color_format != ICET_IMAGE_COLOR_NONE) {
        IceTSizeType pixel_size;
        IceTVoid* color_buf = icetImageGetColorVoid(image, &pixel_size);
        if (group_rank == image_dest) {
            for (i = 0; i < group_size; i++) {
                if (i != group_rank) {
                    requests[i] = icetCommIrecv((IceTByte*)color_buf
                                                + pixel_size * all_offsets[i],
                                                pixel_size * all_sizes[i],
                                                ICET_BYTE,
                                                compose_group[i],
                                                SWAP_IMAGE_DATA);
                } else {
                    requests[i] = ICET_COMM_REQUEST_NULL;
                }
            }
            for (i = 0; i < group_size; i++) {
                icetCommWait(requests + i);
            }
        } else {
            icetCommSend((IceTByte*)color_buf + pixel_size * offset,
                         pixel_size * size,
                         ICET_BYTE,
                         compose_group[image_dest],
                         SWAP_IMAGE_DATA);
        }
    }
    if (depth_format != ICET_IMAGE_DEPTH_NONE) {
        IceTSizeType pixel_size;
        IceTVoid* depth_buf = icetImageGetDepthVoid(image, &pixel_size);
        if (group_rank == image_dest) {
            for (i = 0; i < group_size; i++) {
                if (i != group_rank) {
                    requests[i] = icetCommIrecv((IceTByte*)depth_buf
                                                + pixel_size * all_offsets[i],
                                                pixel_size * all_sizes[i],
                                                ICET_BYTE,
                                                compose_group[i],
                                                SWAP_IMAGE_DATA);
                } else {
                    requests[i] = ICET_COMM_REQUEST_NULL;
                }
            }
            for (i = 0; i < group_size; i++) {
                icetCommWait(requests + i);
            }
        } else {
            icetCommSend((IceTByte*)depth_buf + pixel_size * offset,
                         pixel_size * size,
                         ICET_BYTE,
                         compose_group[image_dest],
                         SWAP_IMAGE_DATA);
        }
    }

    /* This will not work for multi-tile compositing most likely because IceT
       does not create separate communicators for each compositing group. */
    /* TODO use Gatherv since processes might not contain equal portions. */
#if 0
    if (color_format != ICET_IMAGE_COLOR_NONE) {
        IceTSizeType pixel_size;
        IceTVoid* color_buf = icetImageGetColorVoid(image, &pixel_size);
        if (group_rank == image_dest) {
            icetCommGather(MPI_IN_PLACE, size * pixel_size,
                           ICET_BYTE, color_buf + pixel_size * offset,
                           compose_group[image_dest]);
        } else {
            icetCommGather(color_buf + pixel_size * offset, size * pixel_size,
                           ICET_BYTE, color_buf + pixel_size * offset,
                           compose_group[image_dest]);
        }
    }
    if (depth_format != ICET_IMAGE_DEPTH_NONE) {
        IceTSizeType pixel_size;
        IceTVoid* depth_buf = icetImageGetDepthVoid(image, &pixel_size);
        if (group_rank == image_dest) {
            icetCommGather(MPI_IN_PLACE, size * pixel_size,
                           ICET_BYTE, depth_buf + pixel_size * offset,
                           compose_group[image_dest]);
        } else {
            icetCommGather(depth_buf + pixel_size * offset, size * pixel_size,
                           ICET_BYTE, depth_buf + pixel_size * offset,
                           compose_group[image_dest]);
        }
    }
#endif
}

void icetRadixkCompose(IceTInt *compose_group, IceTInt group_size,
                       IceTInt image_dest, IceTImage image) {
    IceTSizeType size = icetImageGetNumPixels(image);
    int num_rounds;
    int* k_array;

    IceTSizeType my_offset; /* Offset of my subimage */
    IceTSizeType my_size; /* Size of my subimage */
    int *partition_indices; /* My round vector [round0 pos, round1 pos, ...] */
    int current_round;

    /* Find your rank in your group. */
    IceTInt group_rank = icetFindMyRankInGroup(compose_group, group_size);
    if (group_rank < 0) {
        icetRaiseError("Local process not in compose_group?",
                       ICET_SANITY_CHECK_FAIL);
        return;
    }

    if (group_size == 1) {
        /* I am the only process in the group.  No compositing to be done.
         * Just return and the image will be complete. */
        return;
    }

    k_array = radixkGetK(group_size, &num_rounds);

    /* num_rounds > 0 is assumed several places throughout this function */
    if (num_rounds <= 0) {
        icetRaiseError("Radix-k has no rounds?", ICET_SANITY_CHECK_FAIL);
    }

    /* initialize size, my round vector, my offset */
    partition_indices = icetGetStateBuffer(RADIXK_PARTITION_INDICES_BUFFER,
                                           sizeof(int) * num_rounds);
    radixkGetPartitionIndices(num_rounds,
                              k_array,
                              group_rank,
                              partition_indices);

    /* Any peer we communicate with in round i starts that round with a block of
       the same size as ours prior to splitting for sends/recvs.  So we can
       calculate the current round's peer sizes based on our current size and
       the k_array[i] info. */
    my_offset = 0;
    my_size = size;

    for (current_round = 0; current_round < num_rounds; current_round++) {
        int current_k = k_array[current_round];
        int current_partition_index = partition_indices[current_round];
        radixkPartnerInfo *partners
            = radixkGetPartners(k_array,
                                current_round,
                                current_partition_index,
                                compose_group,
                                group_rank,
                                my_offset,
                                my_size);
        IceTCommRequest *receive_requests;
        IceTCommRequest *send_requests;

        my_offset = partners[current_partition_index].offset;
        my_size = partners[current_partition_index].size;

        receive_requests = radixkPostReceives(partners,
                                              current_k,
                                              current_round,
                                              current_partition_index);

        send_requests = radixkPostSends(partners,
                                        current_k,
                                        current_round,
                                        current_partition_index,
                                        image);

        radixkCompositeIncomingImages(partners,
                                      receive_requests,
                                      current_k,
                                      current_partition_index,
                                      image);

        icetCommWaitall(current_k, send_requests);
    } /* for all rounds */

    radixkGatherFinalImage(compose_group, group_rank, group_size, image_dest,
                           my_offset, my_size, image);

    return;
}
