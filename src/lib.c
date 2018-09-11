/*
 * ..
 */

#include <errno.h>
#include <string.h>

#include "dav1d.h"

#include "include/version.h"

#include "common/mem.h"
#include "common/validate.h"

#include "src/data.h"
#include "src/internal.h"
#include "src/obu.h"
#include "src/qm.h"
#include "src/ref.h"
#include "src/thread_task.h"
#include "src/wedge.h"

void dav1d_init(void) {
    av1_init_wedge_masks();
    av1_init_interintra_masks();
    av1_init_qm_tables();
}

const char *dav1d_version(void) {
    return DAV1D_VERSION;
}

void dav1d_default_settings(Dav1dSettings *const s) {
    s->n_frame_threads = 1;
    s->n_tile_threads = 1;
}

int dav1d_open(Dav1dContext **const c_out,
               const Dav1dSettings *const s)
{
    validate_input_or_ret(c_out != NULL, -EINVAL);
    validate_input_or_ret(s != NULL, -EINVAL);
    validate_input_or_ret(s->n_tile_threads >= 1 &&
                          s->n_tile_threads <= 64, -EINVAL);
    validate_input_or_ret(s->n_frame_threads >= 1 &&
                          s->n_frame_threads <= 256, -EINVAL);

    Dav1dContext *const c = *c_out = dav1d_alloc_aligned(sizeof(*c), 32);
    if (!c) goto error;
    memset(c, 0, sizeof(*c));

    c->n_fc = s->n_frame_threads;
    c->fc = dav1d_alloc_aligned(sizeof(*c->fc) * s->n_frame_threads, 32);
    if (!c->fc) goto error;
    memset(c->fc, 0, sizeof(*c->fc) * s->n_frame_threads);
    if (c->n_fc > 1) {
        c->frame_thread.out_delayed =
            malloc(sizeof(*c->frame_thread.out_delayed) * c->n_fc);
        memset(c->frame_thread.out_delayed, 0,
               sizeof(*c->frame_thread.out_delayed) * c->n_fc);
    }
    for (int n = 0; n < s->n_frame_threads; n++) {
        Dav1dFrameContext *const f = &c->fc[n];
        f->c = c;
        f->lf.last_sharpness = -1;
        f->n_tc = s->n_tile_threads;
        f->tc = dav1d_alloc_aligned(sizeof(*f->tc) * s->n_tile_threads, 32);
        if (!f->tc) goto error;
        memset(f->tc, 0, sizeof(*f->tc) * s->n_tile_threads);
        if (f->n_tc > 1) {
            pthread_mutex_init(&f->tile_thread.lock, NULL);
            pthread_cond_init(&f->tile_thread.cond, NULL);
            pthread_cond_init(&f->tile_thread.icond, NULL);
        }
        for (int m = 0; m < s->n_tile_threads; m++) {
            Dav1dTileContext *const t = &f->tc[m];
            t->f = f;
            t->cf = dav1d_alloc_aligned(32 * 32 * sizeof(int32_t), 32);
            if (!t->cf) goto error;
            t->scratch.mem = dav1d_alloc_aligned(128 * 128 * 4, 32);
            if (!t->scratch.mem) goto error;
            memset(t->cf, 0, 32 * 32 * sizeof(int32_t));
            t->emu_edge =
                dav1d_alloc_aligned(160 * (128 + 7) * sizeof(uint16_t), 32);
            if (!t->emu_edge) goto error;
            if (f->n_tc > 1) {
                pthread_mutex_init(&t->tile_thread.td.lock, NULL);
                pthread_cond_init(&t->tile_thread.td.cond, NULL);
                t->tile_thread.fttd = &f->tile_thread;
                pthread_create(&t->tile_thread.td.thread, NULL, dav1d_tile_task, t);
            }
        }
        f->libaom_cm = av1_alloc_ref_mv_common();
        if (c->n_fc > 1) {
            pthread_mutex_init(&f->frame_thread.td.lock, NULL);
            pthread_cond_init(&f->frame_thread.td.cond, NULL);
            pthread_create(&f->frame_thread.td.thread, NULL, dav1d_frame_task, f);
        }
    }

    // intra edge tree
    c->intra_edge.root[BL_128X128] = &c->intra_edge.branch_sb128[0].node;
    init_mode_tree(c->intra_edge.root[BL_128X128], c->intra_edge.tip_sb128, 1);
    c->intra_edge.root[BL_64X64] = &c->intra_edge.branch_sb64[0].node;
    init_mode_tree(c->intra_edge.root[BL_64X64], c->intra_edge.tip_sb64, 0);

    return 0;

error:
    if (c) {
        if (c->fc) {
            for (int n = 0; n < c->n_fc; n++)
                if (c->fc[n].tc)
                    free(c->fc[n].tc);
            free(c->fc);
        }
        free(c);
    }
    fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
    return -ENOMEM;
}

int dav1d_decode(Dav1dContext *const c,
                 Dav1dData *const in, Dav1dPicture *const out)
{
    int res;

    validate_input_or_ret(c != NULL, -EINVAL);
    validate_input_or_ret(out != NULL, -EINVAL);

    while (!in) {
        if (c->n_fc == 1) return -EAGAIN;

        // flush
        const unsigned next = c->frame_thread.next;
        Dav1dFrameContext *const f = &c->fc[next];

        pthread_mutex_lock(&f->frame_thread.td.lock);
        while (f->n_tile_data > 0)
            pthread_cond_wait(&f->frame_thread.td.cond,
                              &f->frame_thread.td.lock);
        pthread_mutex_unlock(&f->frame_thread.td.lock);
        Dav1dThreadPicture *const out_delayed =
            &c->frame_thread.out_delayed[next];
        if (out_delayed->p.data[0]) {
            if (++c->frame_thread.next == c->n_fc)
                c->frame_thread.next = 0;
            if (out_delayed->visible) {
                dav1d_picture_ref(out, &out_delayed->p);
            }
            dav1d_thread_picture_unref(out_delayed);
            if (out->data[0]) {
                return 0;
            }
            // else continue
        } else {
            return -EAGAIN;
        }
    }

    while (in->sz > 0) {
        if ((res = parse_obus(c, in)) < 0)
            return res;

        assert(res <= in->sz);
        in->sz -= res;
        in->data += res;
        if (c->out.data[0]) {
            if (!in->sz) dav1d_data_unref(in);
            dav1d_picture_ref(out, &c->out);
            dav1d_picture_unref(&c->out);
            return 0;
        }
    }

    if (c->out.data[0]) {
        dav1d_picture_ref(out, &c->out);
        dav1d_picture_unref(&c->out);
        return 0;
    }

    return -EAGAIN;
}

void dav1d_close(Dav1dContext *const c) {
    validate_input(c != NULL);

    for (int n = 0; n < c->n_fc; n++) {
        Dav1dFrameContext *const f = &c->fc[n];

        // clean-up threading stuff
        if (c->n_fc > 1) {
            pthread_mutex_lock(&f->frame_thread.td.lock);
            f->frame_thread.die = 1;
            pthread_cond_signal(&f->frame_thread.td.cond);
            pthread_mutex_unlock(&f->frame_thread.td.lock);
            pthread_join(f->frame_thread.td.thread, NULL);
            freep(&f->frame_thread.b);
            freep(&f->frame_thread.pal_idx);
            freep(&f->frame_thread.cf);
            freep(&f->frame_thread.tile_start_off);
            freep(&f->frame_thread.pal);
            freep(&f->frame_thread.cbi);
            pthread_mutex_destroy(&f->frame_thread.td.lock);
            pthread_cond_destroy(&f->frame_thread.td.cond);
        }
        if (f->n_tc > 1) {
            pthread_mutex_lock(&f->tile_thread.lock);
            for (int m = 0; m < f->n_tc; m++) {
                Dav1dTileContext *const t = &f->tc[m];
                t->tile_thread.die = 1;
            }
            pthread_cond_broadcast(&f->tile_thread.cond);
            while (f->tile_thread.available != (1 << f->n_tc) - 1)
                pthread_cond_wait(&f->tile_thread.icond,
                                  &f->tile_thread.lock);
            pthread_mutex_unlock(&f->tile_thread.lock);
            for (int m = 0; m < f->n_tc; m++) {
                Dav1dTileContext *const t = &f->tc[m];
                if (f->n_tc > 1) {
                    pthread_join(t->tile_thread.td.thread, NULL);
                    pthread_mutex_destroy(&t->tile_thread.td.lock);
                    pthread_cond_destroy(&t->tile_thread.td.cond);
                }
            }
            pthread_mutex_destroy(&f->tile_thread.lock);
            pthread_cond_destroy(&f->tile_thread.cond);
            pthread_cond_destroy(&f->tile_thread.icond);
        }
        for (int m = 0; m < f->n_tc; m++) {
            Dav1dTileContext *const t = &f->tc[m];
            free(t->cf);
            free(t->scratch.mem);
            free(t->emu_edge);
        }
        for (int m = 0; m < f->n_ts; m++) {
            Dav1dTileState *const ts = &f->ts[m];
            pthread_cond_destroy(&ts->tile_thread.cond);
            pthread_mutex_destroy(&ts->tile_thread.lock);
        }
        free(f->ts);
        free(f->tc);
        free(f->ipred_edge[0]);
        free(f->a);
        free(f->lf.mask);
        free(f->lf.level);
        free(f->lf.tx_lpf_right_edge[0]);
        av1_free_ref_mv_common(f->libaom_cm);
        free(f->lf.cdef_line);
        free(f->lf.lr_lpf_line);
    }
    free(c->fc);
    if (c->n_fc > 1) {
        for (int n = 0; n < c->n_fc; n++)
            if (c->frame_thread.out_delayed[n].p.data[0])
                dav1d_thread_picture_unref(&c->frame_thread.out_delayed[n]);
        free(c->frame_thread.out_delayed);
    }
    for (int n = 0; n < c->n_tile_data; n++)
        dav1d_data_unref(&c->tile[n].data);
    for (int n = 0; n < 8; n++) {
        if (c->cdf[n].cdf)
            cdf_thread_unref(&c->cdf[n]);
        if (c->refs[n].p.p.data[0])
            dav1d_thread_picture_unref(&c->refs[n].p);
        if (c->refs[n].refmvs)
            dav1d_ref_dec(c->refs[n].refmvs);
        if (c->refs[n].segmap)
            dav1d_ref_dec(c->refs[n].segmap);
    }
    free(c);
}
