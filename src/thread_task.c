/*
 * ..
 */

#include "src/thread_task.h"

void *dav1d_frame_task(void *const data) {
    Dav1dFrameContext *const f = data;

    for (;;) {
        pthread_mutex_lock(&f->frame_thread.td.lock);
        f->n_tile_data = 0;
        int did_signal = 0;
        while (!f->n_tile_data && !f->frame_thread.die) {
            if (!did_signal) {
                did_signal = 1;
                pthread_cond_signal(&f->frame_thread.td.cond);
            }
            pthread_cond_wait(&f->frame_thread.td.cond,
                              &f->frame_thread.td.lock);
        }
        if (f->frame_thread.die) {
            pthread_mutex_unlock(&f->frame_thread.td.lock);
            break;
        }
        pthread_mutex_unlock(&f->frame_thread.td.lock);

        decode_frame(f);
    }

    pthread_exit(NULL);
    return NULL;
}

void *dav1d_tile_task(void *const data) {
    Dav1dTileContext *const t = data;
    struct FrameTileThreadData *const fttd = t->tile_thread.fttd;
    const Dav1dFrameContext *const f = t->f;
    const int tile_thread_idx = t - f->tc;
    const uint64_t mask = 1ULL << tile_thread_idx;

    for (;;) {
        pthread_mutex_lock(&fttd->lock);
        fttd->available |= mask;
        int did_signal = 0;
        while (!fttd->tasks_left && !t->tile_thread.die) {
            if (!did_signal) {
                did_signal = 1;
                pthread_cond_signal(&fttd->icond);
            }
            pthread_cond_wait(&fttd->cond, &fttd->lock);
        }
        if (t->tile_thread.die) {
            pthread_mutex_unlock(&fttd->lock);
            break;
        }
        fttd->available &= ~mask;
        const int task_idx = fttd->num_tasks - fttd->tasks_left--;
        pthread_mutex_unlock(&fttd->lock);

        if (f->frame_thread.pass == 1 || f->n_tc >= f->frame_hdr.tiling.cols) {
            // we can (or in fact, if >, we need to) do full tile decoding.
            // loopfilter happens in the main thread
            Dav1dTileState *const ts = t->ts = &f->ts[task_idx];
            for (t->by = ts->tiling.row_start; t->by < ts->tiling.row_end;
                 t->by += f->sb_step)
            {
                decode_tile_sbrow(t);

                // signal progress
                pthread_mutex_lock(&ts->tile_thread.lock);
                atomic_store(&ts->progress, 1 + (t->by >> f->sb_shift));
                pthread_cond_signal(&ts->tile_thread.cond);
                pthread_mutex_unlock(&ts->tile_thread.lock);
            }
        } else {
            const int sby = f->tile_thread.task_idx_to_sby_and_tile_idx[task_idx][0];
            const int tile_idx = f->tile_thread.task_idx_to_sby_and_tile_idx[task_idx][1];
            Dav1dTileState *const ts = &f->ts[tile_idx];

            // the interleaved decoding can sometimes cause dependency issues
            // if one part of the frame decodes signifcantly faster than others.
            // Ideally, we'd "skip" tile_sbrows where dependencies are missing,
            // and resume them later as dependencies are met. This also would
            // solve the broadcast() below and allow us to use signal(). However,
            // for now, we use linear dependency tracking because it's simpler.
            if (atomic_load(&ts->progress) < sby) {
                pthread_mutex_lock(&ts->tile_thread.lock);
                while (atomic_load(&ts->progress) < sby)
                    pthread_cond_wait(&ts->tile_thread.cond,
                                      &ts->tile_thread.lock);
                pthread_mutex_unlock(&ts->tile_thread.lock);
            }

            // we need to interleave sbrow decoding for all tile cols in a
            // tile row, since otherwise subsequent threads will be blocked
            // waiting for the post-filter to complete
            t->ts = ts;
            t->by = sby << f->sb_shift;
            decode_tile_sbrow(t);

            // signal progress
            pthread_mutex_lock(&ts->tile_thread.lock);
            atomic_store(&ts->progress, 1 + sby);
            pthread_cond_broadcast(&ts->tile_thread.cond);
            pthread_mutex_unlock(&ts->tile_thread.lock);
        }
    }

    pthread_exit(NULL);
    return NULL;
}
