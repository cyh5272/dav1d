/*
 * ..
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "output/muxer.h"

typedef struct MuxerPriv {
    FILE *f;
} Y4m2OutputContext;

static int y4m2_open(Y4m2OutputContext *const c, const char *const file,
                     const Dav1dPictureParameters *p, const unsigned fps[2])
{
    if (!strcmp(file, "-")) {
        c->f = stdin;
    } else if (!(c->f = fopen(file, "w"))) {
        fprintf(stderr, "Failed to open %s: %s\n", file, strerror(errno));
        return -1;
    }

    static const char *const ss_name[][2] = {
        [DAV1D_PIXEL_LAYOUT_I400] = { "mono", "mono10" },
        [DAV1D_PIXEL_LAYOUT_I420] = { "420jpeg", "420p10" },
        [DAV1D_PIXEL_LAYOUT_I422] = { "422", "422p10" },
        [DAV1D_PIXEL_LAYOUT_I444] = { "444", "444p10" }
    };
    fprintf(c->f, "YUV4MPEG2 W%d H%d C%s Ip F%d:%d\n",
            p->w, p->h, ss_name[p->layout][p->bpc > 8], fps[0], fps[1]);

    return 0;
}

static int y4m2_write(Y4m2OutputContext *const c, Dav1dPicture *const p) {
    fprintf(c->f, "FRAME\n");

    uint8_t *ptr;
    const int hbd = p->p.bpc > 8;

    ptr = p->data[0];
    for (int y = 0; y < p->p.h; y++) {
        if (fwrite(ptr, p->p.w << hbd, 1, c->f) != 1)
            goto error;
        ptr += p->stride[0];
    }

    if (p->p.layout != DAV1D_PIXEL_LAYOUT_I400) {
        // u/v
        const int ss_ver = p->p.layout == DAV1D_PIXEL_LAYOUT_I420;
        const int ss_hor = p->p.layout != DAV1D_PIXEL_LAYOUT_I444;
        const int cw = (p->p.w + ss_hor) >> ss_hor;
        const int ch = (p->p.h + ss_ver) >> ss_ver;
        for (int pl = 1; pl <= 2; pl++) {
            ptr = p->data[pl];
            for (int y = 0; y < ch; y++) {
                if (fwrite(ptr, cw << hbd, 1, c->f) != 1)
                    goto error;
                ptr += p->stride[1];
            }
        }
    }

    dav1d_picture_unref(p);
    return 0;

error:
    dav1d_picture_unref(p);
    fprintf(stderr, "Failed to write frame data: %s\n", strerror(errno));
    return -1;
}

static void y4m2_close(Y4m2OutputContext *const c) {
    if (c->f != stdin)
        fclose(c->f);
}

const Muxer y4m2_muxer = {
    .priv_data_size = sizeof(Y4m2OutputContext),
    .name = "yuv4mpeg2",
    .extension = "y4m",
    .write_header = y4m2_open,
    .write_picture = y4m2_write,
    .write_trailer = y4m2_close,
};
