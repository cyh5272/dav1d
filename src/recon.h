/*
 * ..
 */

#ifndef __DAV1D_SRC_RECON_H__
#define __DAV1D_SRC_RECON_H__

#include "src/internal.h"
#include "src/levels.h"

#define DEBUG_BLOCK_INFO 0 && \
        f->frame_hdr.frame_offset == 2 && t->by >= 0 && t->by < 4 && \
        t->bx >= 8 && t->bx < 12
#define DEBUG_B_PIXELS 0

#define decl_recon_b_intra_fn(name) \
void (name)(Dav1dTileContext *t, enum BlockSize bs, \
            enum EdgeFlags intra_edge_flags, const Av1Block *b)
typedef decl_recon_b_intra_fn(*recon_b_intra_fn);

#define decl_recon_b_inter_fn(name) \
void (name)(Dav1dTileContext *t, enum BlockSize bs, const Av1Block *b)
typedef decl_recon_b_inter_fn(*recon_b_inter_fn);

#define decl_filter_sbrow_fn(name) \
void (name)(Dav1dFrameContext *f, int sby)
typedef decl_filter_sbrow_fn(*filter_sbrow_fn);

#define decl_backup_ipred_edge_fn(name) \
void (name)(Dav1dTileContext *t)
typedef decl_backup_ipred_edge_fn(*backup_ipred_edge_fn);

#define decl_read_coef_blocks_fn(name) \
void (name)(Dav1dTileContext *t, enum BlockSize bs, const Av1Block *b)
typedef decl_read_coef_blocks_fn(*read_coef_blocks_fn);

decl_recon_b_intra_fn(recon_b_intra_8bpc);
decl_recon_b_intra_fn(recon_b_intra_16bpc);

decl_recon_b_inter_fn(recon_b_inter_8bpc);
decl_recon_b_inter_fn(recon_b_inter_16bpc);

decl_filter_sbrow_fn(filter_sbrow_8bpc);
decl_filter_sbrow_fn(filter_sbrow_16bpc);

decl_backup_ipred_edge_fn(backup_ipred_edge_8bpc);
decl_backup_ipred_edge_fn(backup_ipred_edge_16bpc);

decl_read_coef_blocks_fn(read_coef_blocks_8bpc);
decl_read_coef_blocks_fn(read_coef_blocks_16bpc);

#endif /* __DAV1D_SRC_RECON_H__ */
