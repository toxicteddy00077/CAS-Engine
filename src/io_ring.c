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
void cas_io_ring_close(cas_io_ring* ring);
//async read/write req
int cas_io_ring_read_write(cas_io_ring* ring, int fd, void* buffer, uint32_t len, off_t offset, int rw_flags, void *user_data);
//submit req
int cas_io_ring_submit(cas_io_ring* ring);
//completions
int cas_io_ring_get_completions(cas_io_ring* ring, void **output, uint32_t count);


cas_io_ring* cas_io_ring_init(const uint32_t depth){
    cas_io_ring *ring = malloc(sizeof(cas_io_ring));
    if(!ring) return NULL;

    if(io_uring_queue_init(depth, &ring->ring, 0) != 0){
        free(ring);
        return NULL;
    }

    ring->q_depth = depth;
    ring->pending_sqe = 0;
    return ring;
}


void cas_io_ring_close(cas_io_ring* ring){
    if (!ring) return;
    io_uring_queue_exit(&ring->ring);
    free(ring);
}

int cas_io_ring_read_write(cas_io_ring* ring, int fd, void* buffer, uint32_t len, off_t offset, int rw_flags, void *user_data) {
    if (!ring) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring->ring);
    if (!sqe) return -1; // ring full

    switch(rw_flags) {
        case 0:
            io_uring_prep_read(sqe, fd, buffer, len, offset);
            break;
        case 1:
            io_uring_prep_write(sqe, fd, buffer, len, offset);
            break;
        default:
            return -1;
    }

    io_uring_sqe_set_data(sqe, user_data);

    ring->pending_sqe++;
    return 0;
}

int cas_io_ring_submit(cas_io_ring* ring) {
    if (!ring) return -1;

    int res = io_uring_submit(&ring->ring);
    if (res < 0) {
        return res;
    }

    ring->pending_sqe -= res;
    return res; // Returns the number of submitted SQEs
}

int cas_io_ring_get_completions(cas_io_ring* ring, void **output, uint32_t count) {
    if (!ring || !output || count == 0) return -1;

    struct io_uring_cqe *cqe;

    // Wait until at least 1 completion is ready (or up to 'count')
    int res = io_uring_wait_cqe_nr(&ring->ring, &cqe, 1);
    if (res < 0) return res;

    uint32_t completed = 0;
    unsigned head;

    // Properly iterate through available CQEs in the completion queue
    io_uring_for_each_cqe(&ring->ring, head, cqe) {
        if (completed >= count) break;

        // Extract user_data payload pointer
        output[completed] = io_uring_cqe_get_data(cqe);
        completed++;
    }

    // Mark harvested CQEs as seen so liburing can reuse those slots!
    io_uring_cq_advance(&ring->ring, completed);

    return (int)completed; // Return how many completions were harvested
}
