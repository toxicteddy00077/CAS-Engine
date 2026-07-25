#include "../include/types.h"
#include <lmdb.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <liburing.h>

//ring init
cas_io_ring* cas_io_ring_init(const uint32_t depth);
//ring close
void cas_io_ring_close();
//async read/write req
int cas_io_ring_read_write(cas_io_ring* ring, int fd, void* buffer, uint32_t len, off_t offset, int rw_flags);
//submit req
int cas_io_ring_submit(cas_io_ring* ring);
//completions
int cas_io_ring_get_completions(cas_io_ring* ring, void **output, uint32_t count);


cas_io_ring* cas_io_ring_init_(const uint32_t depth){
    cas_io_ring *ring = malloc(sizeof(cas_io_ring));

    if(io_uring_queue_init(depth, &ring->ring, 0) != 0) return NULL;

    ring->q_depth = depth;
    ring->pending_sqe = 0;
    return ring;
}


void cas_io_ring_close(cas_io_ring* ring){
    io_uring_queue_exit(&ring->ring);
    free(ring);
}

int cas_io_ring_read_write(cas_io_ring* ring, int fd, void* buffer, uint32_t len, off_t offset, int rw_flags){
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring->ring);
    if (!sqe) return -1;

    switch(rw_flags){
        case 0:
            io_uring_prep_read(sqe, fd, buffer, len, offset);
            break;
        case 1:
            io_uring_prep_write(sqe, fd, buffer, len, offset);
            break;
        default:
            return -1;
    }

    ring->pending_sqe++;
    return 0;
}


int cas_io_ring_submit(cas_io_ring* ring){
    int res = io_uring_submit(&ring->ring);
    if (res < 0) goto cleanup;
    ring->pending_sqe -= res;
    return 0;

cleanup:
    cas_io_ring_close(ring);
    return -1;
}

int cas_io_ring_get_completions(cas_io_ring* ring, void **output, uint32_t count){
    struct io_uring_cqe *cqe;
    int res = io_uring_wait_cqe_nr(&ring->ring, &cqe, count);
    if (res < 0) goto cleanup;
    for (int i = 0; i < res; i++) {
        output[i] = cqe[i].user_data;
    }
    return 0;

cleanup:
    cas_io_ring_close(ring);
    return -1;
}
