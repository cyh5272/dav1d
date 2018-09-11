/*
 * ..
 */

/*
 * Changes made compared to libaom version:
 * - we disable TMV and enable MV_COMPRESS so that the
 *   input array for prev_frames can be at 4x4 instead of
 *   8x8 resolution, and therefore shared between cur_frame
 *   and prev_frame. To make enc/dec behave consistent, we
 *   also make this change around line 2580:
#if 0
                AOMMIN(((mi_row >> 1) << 1) + 1 + (((xd->n8_h - 1) >> 1) << 1),
                       mi_row_end - 1) *
                    prev_frame_mvs_stride +
                AOMMIN(((mi_col >> 1) << 1) + 1 + (((xd->n8_w - 1) >> 1) << 1),
                       mi_col_end - 1)
#else
                (((mi_row >> 1) << 1) + 1) * prev_frame_mvs_stride +
                (((mi_col >> 1) << 1) + 1)
#endif
 *   and the same change (swap mi_cols from prev_frame.mv_stride) on line 2407
 * - we disable rect-block overhanging edge inclusion (see
 *   line 2642):
  if (num_8x8_blocks_wide == num_8x8_blocks_high || 1) {
    mv_ref_search[5].row = -1;
    mv_ref_search[5].col = 0;
    mv_ref_search[6].row = 0;
    mv_ref_search[6].col = -1;
  } else {
    mv_ref_search[5].row = -1;
    mv_ref_search[5].col = num_8x8_blocks_wide;
    mv_ref_search[6].row = num_8x8_blocks_high;
    mv_ref_search[6].col = -1;
  }
 *   Note that this is a bitstream change and needs the same
 *   change on the decoder side also.
 * - we change xd->mi to be a pointer instead of a double ptr.
 */
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/intops.h"

#define av1_zero(a) memset(a, 0, sizeof(a))

#define ATTRIBUTE_PACKED
#define INLINE inline
#define IMPLIES(a, b) (!(a) || (b))  //  Logical 'a implies b' (or 'a -> b')

#define ROUND_POWER_OF_TWO(value, n) (((value) + (((1 << (n)) >> 1))) >> (n))
#define ROUND_POWER_OF_TWO_SIGNED(value, n)           \
  (((value) < 0) ? -ROUND_POWER_OF_TWO(-(value), (n)) \
                 : ROUND_POWER_OF_TWO((value), (n)))
#define NELEMENTS(x) (int)(sizeof(x) / sizeof(x[0]))

#define MAX_MV_REF_CANDIDATES 2

#define MAX_REF_MV_STACK_SIZE 8
#define REF_CAT_LEVEL 640

#define FRAME_OFFSET_BITS 5
#define MAX_FRAME_DISTANCE ((1 << FRAME_OFFSET_BITS) - 1)
#define INVALID_MV 0x80008000

#define COMP_NEWMV_CTXS 5
#define REFMV_OFFSET 4
#define REFMV_CTX_MASK ((1 << (8 - REFMV_OFFSET)) - 1)

#define MV_IN_USE_BITS 14
#define MV_UPP (1 << MV_IN_USE_BITS)
#define MV_LOW (-(1 << MV_IN_USE_BITS))

typedef struct MV {
    int16_t row;
    int16_t col;
} MV;
typedef union int_mv {
    uint32_t as_int;
    MV as_mv;
} int_mv;
typedef int8_t MV_REFERENCE_FRAME;
#define MFMV_STACK_SIZE 3
typedef struct {
  int_mv mfmv0;
  uint8_t ref_frame_offset;
} TPL_MV_REF;
typedef struct {
    int_mv mv[2];
    MV_REFERENCE_FRAME ref_frame[2];
    int8_t mode, sb_type;
} MV_REF;
#define MB_MODE_INFO MV_REF

#define AOMMAX(a,b) ((a)>(b)?(a):(b))
#define AOMMIN(a,b) ((a)<(b)?(a):(b))

typedef struct candidate_mv {
    int_mv this_mv;
    int_mv comp_mv;
    int weight;
} CANDIDATE_MV;
#define NONE_FRAME -1
#define INTRA_FRAME 0
#define LAST_FRAME 1

#define LAST2_FRAME 2
#define LAST3_FRAME 3
#define GOLDEN_FRAME 4
#define BWDREF_FRAME 5
#define ALTREF2_FRAME 6
#define ALTREF_FRAME 7
#define LAST_REF_FRAMES (LAST3_FRAME - LAST_FRAME + 1)

#define INTER_REFS_PER_FRAME (ALTREF_FRAME - LAST_FRAME + 1)
#define TOTAL_REFS_PER_FRAME (ALTREF_FRAME - INTRA_FRAME + 1)

#define FWD_REFS (GOLDEN_FRAME - LAST_FRAME + 1)
#define FWD_RF_OFFSET(ref) (ref - LAST_FRAME)
#define BWD_REFS (ALTREF_FRAME - BWDREF_FRAME + 1)
#define BWD_RF_OFFSET(ref) (ref - BWDREF_FRAME)
#define FWD_REFS (GOLDEN_FRAME - LAST_FRAME + 1)
#define SINGLE_REFS (FWD_REFS + BWD_REFS)
typedef enum ATTRIBUTE_PACKED {
  LAST_LAST2_FRAMES,      // { LAST_FRAME, LAST2_FRAME }
  LAST_LAST3_FRAMES,      // { LAST_FRAME, LAST3_FRAME }
  LAST_GOLDEN_FRAMES,     // { LAST_FRAME, GOLDEN_FRAME }
  BWDREF_ALTREF_FRAMES,   // { BWDREF_FRAME, ALTREF_FRAME }
  LAST2_LAST3_FRAMES,     // { LAST2_FRAME, LAST3_FRAME }
  LAST2_GOLDEN_FRAMES,    // { LAST2_FRAME, GOLDEN_FRAME }
  LAST3_GOLDEN_FRAMES,    // { LAST3_FRAME, GOLDEN_FRAME }
  BWDREF_ALTREF2_FRAMES,  // { BWDREF_FRAME, ALTREF2_FRAME }
  ALTREF2_ALTREF_FRAMES,  // { ALTREF2_FRAME, ALTREF_FRAME }
  TOTAL_UNIDIR_COMP_REFS,
  // NOTE: UNIDIR_COMP_REFS is the number of uni-directional reference pairs
  //       that are explicitly signaled.
  UNIDIR_COMP_REFS = BWDREF_ALTREF_FRAMES + 1,
} UNIDIR_COMP_REF;
#define TOTAL_COMP_REFS (FWD_REFS * BWD_REFS + TOTAL_UNIDIR_COMP_REFS)
#define MODE_CTX_REF_FRAMES (TOTAL_REFS_PER_FRAME + TOTAL_COMP_REFS)

#define GLOBALMV_OFFSET 3
#define NEWMV_CTX_MASK ((1 << GLOBALMV_OFFSET) - 1)
#define GLOBALMV_CTX_MASK ((1 << (REFMV_OFFSET - GLOBALMV_OFFSET)) - 1)
#define MI_SIZE_LOG2 2
#define MI_SIZE (1 << MI_SIZE_LOG2)
#define MAX_SB_SIZE_LOG2 7
#define MAX_MIB_SIZE_LOG2 (MAX_SB_SIZE_LOG2 - MI_SIZE_LOG2)
#define MIN_MIB_SIZE_LOG2 (MIN_SB_SIZE_LOG2 - MI_SIZE_LOG2)
#define MAX_MIB_SIZE (1 << MAX_MIB_SIZE_LOG2)
#define MI_SIZE_64X64 (64 >> MI_SIZE_LOG2)
#define MI_SIZE_128X128 (128 >> MI_SIZE_LOG2)
#define REFMV_OFFSET 4

typedef enum ATTRIBUTE_PACKED {
  BLOCK_4X4,
  BLOCK_4X8,
  BLOCK_8X4,
  BLOCK_8X8,
  BLOCK_8X16,
  BLOCK_16X8,
  BLOCK_16X16,
  BLOCK_16X32,
  BLOCK_32X16,
  BLOCK_32X32,
  BLOCK_32X64,
  BLOCK_64X32,
  BLOCK_64X64,
  BLOCK_64X128,
  BLOCK_128X64,
  BLOCK_128X128,
  BLOCK_4X16,
  BLOCK_16X4,
  BLOCK_8X32,
  BLOCK_32X8,
  BLOCK_16X64,
  BLOCK_64X16,
  BLOCK_32X128,
  BLOCK_128X32,
  BLOCK_SIZES_ALL,
  BLOCK_SIZES = BLOCK_4X16,
  BLOCK_INVALID = 255,
  BLOCK_LARGEST = (BLOCK_SIZES - 1)
} BLOCK_SIZE;

typedef enum ATTRIBUTE_PACKED {
  PARTITION_NONE,
  PARTITION_HORZ,
  PARTITION_VERT,
  PARTITION_SPLIT,
  PARTITION_HORZ_A,  // HORZ split and the top partition is split again
  PARTITION_HORZ_B,  // HORZ split and the bottom partition is split again
  PARTITION_VERT_A,  // VERT split and the left partition is split again
  PARTITION_VERT_B,  // VERT split and the right partition is split again
  PARTITION_HORZ_4,  // 4:1 horizontal partition
  PARTITION_VERT_4,  // 4:1 vertical partition
  EXT_PARTITION_TYPES,
  PARTITION_TYPES = PARTITION_SPLIT + 1,
  PARTITION_INVALID = 255
} PARTITION_TYPE;
typedef struct CUR_MODE_INFO {
  PARTITION_TYPE partition;
} CUR_MODE_INFO ;

typedef enum ATTRIBUTE_PACKED {
  DC_PRED,        // Average of above and left pixels
  V_PRED,         // Vertical
  H_PRED,         // Horizontal
  D45_PRED,       // Directional 45  deg = round(arctan(1/1) * 180/pi)
  D135_PRED,      // Directional 135 deg = 180 - 45
  D117_PRED,      // Directional 117 deg = 180 - 63
  D153_PRED,      // Directional 153 deg = 180 - 27
  D207_PRED,      // Directional 207 deg = 180 + 27
  D63_PRED,       // Directional 63  deg = round(arctan(2/1) * 180/pi)
  SMOOTH_PRED,    // Combination of horizontal and vertical interpolation
  SMOOTH_V_PRED,  // Vertical interpolation
  SMOOTH_H_PRED,  // Horizontal interpolation
  PAETH_PRED,     // Predict from the direction of smallest gradient
  NEARESTMV,
  NEARMV,
  GLOBALMV,
  NEWMV,
  // Compound ref compound modes
  NEAREST_NEARESTMV,
  NEAR_NEARMV,
  NEAREST_NEWMV,
  NEW_NEARESTMV,
  NEAR_NEWMV,
  NEW_NEARMV,
  GLOBAL_GLOBALMV,
  NEW_NEWMV,
  MB_MODE_COUNT,
  INTRA_MODES = PAETH_PRED + 1,  // PAETH_PRED has to be the last intra mode.
  INTRA_INVALID = MB_MODE_COUNT  // For uv_mode in inter blocks
} PREDICTION_MODE;
typedef enum {
  IDENTITY = 0,      // identity transformation, 0-parameter
  TRANSLATION = 1,   // translational motion 2-parameter
  ROTZOOM = 2,       // simplified affine with rotation + zoom only, 4-parameter
  AFFINE = 3,        // affine, 6-parameter
  TRANS_TYPES,
} TransformationType;
#if 0
typedef enum {
  KEY_FRAME = 0,
  INTER_FRAME = 1,
#if CONFIG_OBU
  INTRA_ONLY_FRAME = 2,  // replaces intra-only
  S_FRAME = 3,
#endif
  FRAME_TYPES,
} FRAME_TYPE;
#endif

#define LEAST_SQUARES_SAMPLES_MAX_BITS 3
#define LEAST_SQUARES_SAMPLES_MAX (1 << LEAST_SQUARES_SAMPLES_MAX_BITS)
#define SAMPLES_ARRAY_SIZE (LEAST_SQUARES_SAMPLES_MAX * 2)

static const uint8_t mi_size_wide[BLOCK_SIZES_ALL] = {
  1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16, 16,
  16, 32, 32,  1, 4, 2, 8, 4, 16, 8, 32
};
static const uint8_t mi_size_high[BLOCK_SIZES_ALL] = {
  1, 2, 1, 2, 4, 2, 4, 8, 4, 8, 16, 8, 16,
  32, 16, 32,  4, 1, 8, 2, 16, 4, 32, 8
};

static const uint8_t block_size_wide[BLOCK_SIZES_ALL] = {
  4,  4,
  8,  8,
  8,  16,
  16, 16,
  32, 32,
  32, 64,
  64, 64, 128, 128, 4,
  16, 8,
  32, 16,
  64, 32, 128
};

static const uint8_t block_size_high[BLOCK_SIZES_ALL] = {
  4,  8,
  4,  8,
  16, 8,
  16, 32,
  16, 32,
  64, 32,
  64, 128, 64, 128, 16,
  4,  32,
  8,  64,
  16, 128, 32
};

static const uint8_t num_8x8_blocks_wide_lookup[BLOCK_SIZES_ALL] = {
  1, 1,
  1, 1,
  1, 2,
  2, 2,
  4, 4,
  4, 8,
  8, 8, 16, 16, 1,
  2, 1,
  4, 2,
  8, 4, 16
};
static const uint8_t num_8x8_blocks_high_lookup[BLOCK_SIZES_ALL] = {
  1, 1,
  1, 1,
  2, 1,
  2, 4,
  2, 4,
  8, 4,
  8, 16, 8, 16, 2,
  1, 4,
  1, 8,
  2, 16, 4
};

static INLINE int is_global_mv_block(const MB_MODE_INFO *const mbmi,
                                     TransformationType type) {
  const PREDICTION_MODE mode = mbmi->mode;
  const BLOCK_SIZE bsize = mbmi->sb_type;
  const int block_size_allowed =
      AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8;
  return (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) && type > TRANSLATION &&
         block_size_allowed;
}

typedef struct {
  TransformationType wmtype;
  int32_t wmmat[6];
  int16_t alpha, beta, gamma, delta;
} WarpedMotionParams;

#define WARPEDMODEL_PREC_BITS 16
static const WarpedMotionParams default_warp_params = {
  IDENTITY,
  { 0, 0, (1 << WARPEDMODEL_PREC_BITS), 0, 0, (1 << WARPEDMODEL_PREC_BITS) },
  0, 0, 0, 0,
};

#define REF_FRAMES_LOG2 3
#define REF_FRAMES (1 << REF_FRAMES_LOG2)
#define FRAME_BUFFERS (REF_FRAMES + 7)
typedef struct {
#if 0
  int ref_count;
#endif

  unsigned int cur_frame_offset;
  unsigned int ref_frame_offset[INTER_REFS_PER_FRAME];

  MV_REF *mvs;
  ptrdiff_t mv_stride;
#if 0
#if CONFIG_SEGMENT_PRED_LAST
  uint8_t *seg_map;
#endif
#endif
  int mi_rows;
  int mi_cols;
#if 0
  // Width and height give the size of the buffer (before any upscaling, unlike
  // the sizes that can be derived from the buf structure)
  int width;
  int height;
  WarpedMotionParams global_motion[TOTAL_REFS_PER_FRAME];
#if CONFIG_FILM_GRAIN_SHOWEX
  int showable_frame;  // frame can be used as show existing frame in future
#endif
#if CONFIG_FILM_GRAIN
  int film_grain_params_present;
  aom_film_grain_t film_grain_params;
#endif
  aom_codec_frame_buffer_t raw_frame_buffer;
  YV12_BUFFER_CONFIG buf;
#if CONFIG_HASH_ME
  hash_table hash_table;
#endif
#endif
  uint8_t intra_only;
#if 0
  FRAME_TYPE frame_type;
  // The Following variables will only be used in frame parallel decode.

  // frame_worker_owner indicates which FrameWorker owns this buffer. NULL means
  // that no FrameWorker owns, or is decoding, this buffer.
  AVxWorker *frame_worker_owner;

  // row and col indicate which position frame has been decoded to in real
  // pixel unit. They are reset to -1 when decoding begins and set to INT_MAX
  // when the frame is fully decoded.
  int row;
  int col;
#endif
} RefCntBuffer;

#define INVALID_IDX -1  // Invalid buffer index.
typedef struct TileInfo {
  int mi_row_start, mi_row_end;
  int mi_col_start, mi_col_end;
  int tg_horz_boundary;
} TileInfo;
typedef struct macroblockd {
#if 0
  struct macroblockd_plane plane[MAX_MB_PLANE];
  uint8_t bmode_blocks_wl;
  uint8_t bmode_blocks_hl;

  FRAME_COUNTS *counts;
#endif
  TileInfo tile;
  int mi_stride;

  CUR_MODE_INFO cur_mi;
  MB_MODE_INFO *mi;
#if 0
  MODE_INFO *left_mi;
  MODE_INFO *above_mi;
  MB_MODE_INFO *left_mbmi;
  MB_MODE_INFO *above_mbmi;
  MB_MODE_INFO *chroma_left_mbmi;
  MB_MODE_INFO *chroma_above_mbmi;
#endif
  int up_available;
  int left_available;
#if 0
  int chroma_up_available;
  int chroma_left_available;
#endif
  /* Distance of MB away from frame edges in subpixels (1/8th pixel)  */
  int mb_to_left_edge;
  int mb_to_right_edge;
  int mb_to_top_edge;
  int mb_to_bottom_edge;
#if 0
  FRAME_CONTEXT *fc;

  /* pointers to reference frames */
  const RefBuffer *block_refs[2];

  /* pointer to current frame */
  const YV12_BUFFER_CONFIG *cur_buf;

  ENTROPY_CONTEXT *above_context[MAX_MB_PLANE];
  ENTROPY_CONTEXT left_context[MAX_MB_PLANE][2 * MAX_MIB_SIZE];

  PARTITION_CONTEXT *above_seg_context;
  PARTITION_CONTEXT left_seg_context[MAX_MIB_SIZE];

  TXFM_CONTEXT *above_txfm_context;
  TXFM_CONTEXT *left_txfm_context;
  TXFM_CONTEXT left_txfm_context_buffer[2 * MAX_MIB_SIZE];

#if CONFIG_LOOP_RESTORATION
  WienerInfo wiener_info[MAX_MB_PLANE];
  SgrprojInfo sgrproj_info[MAX_MB_PLANE];
#endif  // CONFIG_LOOP_RESTORATION
#endif
  // block dimension in the unit of mode_info.
  uint8_t n8_w, n8_h;
#if 0
  uint8_t ref_mv_count[MODE_CTX_REF_FRAMES];
  CANDIDATE_MV ref_mv_stack[MODE_CTX_REF_FRAMES][MAX_REF_MV_STACK_SIZE];
#endif
  uint8_t is_sec_rect;

#if 0
  // Counts of each reference frame in the above and left neighboring blocks.
  // NOTE: Take into account both single and comp references.
  uint8_t neighbors_ref_counts[TOTAL_REFS_PER_FRAME];

  FRAME_CONTEXT *tile_ctx;
  /* Bit depth: 8, 10, 12 */
  int bd;

  int qindex[MAX_SEGMENTS];
  int lossless[MAX_SEGMENTS];
  int corrupted;
  int cur_frame_force_integer_mv;
// same with that in AV1_COMMON
  struct aom_internal_error_info *error_info;
  const WarpedMotionParams *global_motion;
  int prev_qindex;
  int delta_qindex;
  int current_qindex;
#if CONFIG_EXT_DELTA_Q
  // Since actual frame level loop filtering level value is not available
  // at the beginning of the tile (only available during actual filtering)
  // at encoder side.we record the delta_lf (against the frame level loop
  // filtering level) and code the delta between previous superblock's delta
  // lf and current delta lf. It is equivalent to the delta between previous
  // superblock's actual lf and current lf.
  int prev_delta_lf_from_base;
  int current_delta_lf_from_base;
  // For this experiment, we have four frame filter levels for different plane
  // and direction. So, to support the per superblock update, we need to add
  // a few more params as below.
  // 0: delta loop filter level for y plane vertical
  // 1: delta loop filter level for y plane horizontal
  // 2: delta loop filter level for u plane
  // 3: delta loop filter level for v plane
  // To make it consistent with the reference to each filter level in segment,
  // we need to -1, since
  // SEG_LVL_ALT_LF_Y_V = 1;
  // SEG_LVL_ALT_LF_Y_H = 2;
  // SEG_LVL_ALT_LF_U   = 3;
  // SEG_LVL_ALT_LF_V   = 4;
  int prev_delta_lf[FRAME_LF_COUNT];
  int curr_delta_lf[FRAME_LF_COUNT];
#endif

  DECLARE_ALIGNED(16, uint8_t, seg_mask[2 * MAX_SB_SQUARE]);

  CFL_CTX cfl;

  JNT_COMP_PARAMS jcp_param;

  int all_one_sided_refs;
#endif
} MACROBLOCKD;
typedef struct RefBuffer {
  int idx;  // frame buf idx
#if 0
  int map_idx;  // frame map idx
  YV12_BUFFER_CONFIG *buf;
  struct scale_factors sf;
#endif
} RefBuffer;
typedef struct BufferPool {
#if 0
// Protect BufferPool from being accessed by several FrameWorkers at
// the same time during frame parallel decode.
// TODO(hkuang): Try to use atomic variable instead of locking the whole pool.
#if CONFIG_MULTITHREAD
  pthread_mutex_t pool_mutex;
#endif

  // Private data associated with the frame buffer callbacks.
  void *cb_priv;

  aom_get_frame_buffer_cb_fn_t get_fb_cb;
  aom_release_frame_buffer_cb_fn_t release_fb_cb;
#endif
  RefCntBuffer frame_bufs[FRAME_BUFFERS];
#if 0
  // Frame buffers allocated internally by the codec.
  InternalFrameBufferList int_frame_buffers;
#endif
} BufferPool;
typedef struct AV1Common {
#if 0
  struct aom_internal_error_info error;
  aom_color_primaries_t color_primaries;
  aom_transfer_characteristics_t transfer_characteristics;
  aom_matrix_coefficients_t matrix_coefficients;
  int color_range;
  int width;
  int height;
  int render_width;
  int render_height;
  int last_width;
  int last_height;
  int timing_info_present;
  uint32_t num_units_in_tick;
  uint32_t time_scale;
  int equal_picture_interval;
  uint32_t num_ticks_per_picture;

  // TODO(jkoleszar): this implies chroma ss right now, but could vary per
  // plane. Revisit as part of the future change to YV12_BUFFER_CONFIG to
  // support additional planes.
  int subsampling_x;
  int subsampling_y;

  int largest_tile_id;
  size_t largest_tile_size;

  // Scale of the current frame with respect to itself.
  struct scale_factors sf_identity;

  // Marks if we need to use 16bit frame buffers (1: yes, 0: no).
  int use_highbitdepth;
  YV12_BUFFER_CONFIG *frame_to_show;
#endif

  // TODO(hkuang): Combine this with cur_buf in macroblockd.
  RefCntBuffer cur_frame;
#if 0
  int ref_frame_map[REF_FRAMES]; /* maps fb_idx to reference slot */

  // Prepare ref_frame_map for the next frame.
  // Only used in frame parallel decode.
  int next_ref_frame_map[REF_FRAMES];

  // TODO(jkoleszar): could expand active_ref_idx to 4, with 0 as intra, and
  // roll new_fb_idx into it.
#endif

  // Each Inter frame can reference INTER_REFS_PER_FRAME buffers
  RefBuffer frame_refs[INTER_REFS_PER_FRAME];

#if 0
  int is_skip_mode_allowed;
  int skip_mode_flag;
  int ref_frame_idx_0;
  int ref_frame_idx_1;

  int new_fb_idx;

  FRAME_TYPE last_frame_type; /* last frame's frame type for motion search.*/
  FRAME_TYPE frame_type;

  int show_frame;
#if CONFIG_FILM_GRAIN_SHOWEX
  int showable_frame;  // frame can be used as show existing frame in future
#endif
  int last_show_frame;
  int show_existing_frame;
  // Flag for a frame used as a reference - not written to the bitstream
  int is_reference_frame;

#if CONFIG_FWD_KF
  int reset_decoder_state;
#endif  // CONFIG_FWD_KF

  // Flag signaling that the frame is encoded using only INTRA modes.
  uint8_t intra_only;
  uint8_t last_intra_only;

#if CONFIG_CDF_UPDATE_MODE
  uint8_t disable_cdf_update;
#endif  // CONFIG_CDF_UPDATE_MODE
#endif
  int allow_high_precision_mv;
  int cur_frame_force_integer_mv;  // 0 the default in AOM, 1 only integer
#if 0
  int disable_intra_edge_filter;  // 1 - disable corner/edge/upsampling
  int allow_screen_content_tools;
  int allow_intrabc;
  int allow_interintra_compound;
  int allow_masked_compound;

#if !CONFIG_NO_FRAME_CONTEXT_SIGNALING
  // Flag signaling which frame contexts should be reset to default values.
  RESET_FRAME_CONTEXT_MODE reset_frame_context;
#endif

  // MBs, mb_rows/cols is in 16-pixel units; mi_rows/cols is in
  // MODE_INFO (8-pixel) units.
  int MBs;
  int mb_rows, mi_rows;
  int mb_cols, mi_cols;
#endif
  int mi_rows;
  int mi_cols;
  int mi_stride;

#if 0
  /* profile settings */
  TX_MODE tx_mode;

  int base_qindex;
  int y_dc_delta_q;
  int u_dc_delta_q;
  int v_dc_delta_q;
  int u_ac_delta_q;
  int v_ac_delta_q;

  int separate_uv_delta_q;

  // The dequantizers below are true dequntizers used only in the
  // dequantization process.  They have the same coefficient
  // shift/scale as TX.
  int16_t y_dequant_QTX[MAX_SEGMENTS][2];
  int16_t u_dequant_QTX[MAX_SEGMENTS][2];
  int16_t v_dequant_QTX[MAX_SEGMENTS][2];

  // Global quant matrix tables
  const qm_val_t *giqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
  const qm_val_t *gqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];

  // Local quant matrix tables for each frame
  const qm_val_t *y_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  const qm_val_t *u_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  const qm_val_t *v_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];

  // Encoder
  int using_qmatrix;
#if CONFIG_AOM_QM_EXT
  int qm_y;
  int qm_u;
  int qm_v;
#endif  // CONFIG_AOM_QM_EXT
  int min_qmlevel;
  int max_qmlevel;

  /* We allocate a MODE_INFO struct for each macroblock, together with
     an extra row on top and column on the left to simplify prediction. */
  int mi_alloc_size;
  MODE_INFO *mip; /* Base of allocated array */
  MODE_INFO *mi;  /* Corresponds to upper left visible macroblock */

  // TODO(agrange): Move prev_mi into encoder structure.
  // prev_mip and prev_mi will only be allocated in encoder.
  MODE_INFO *prev_mip; /* MODE_INFO array 'mip' from last decoded frame */
  MODE_INFO *prev_mi;  /* 'mi' from last frame (points into prev_mip) */

  // Separate mi functions between encoder and decoder.
  int (*alloc_mi)(struct AV1Common *cm, int mi_size);
  void (*free_mi)(struct AV1Common *cm);
  void (*setup_mi)(struct AV1Common *cm);

  // Grid of pointers to 8x8 MODE_INFO structs.  Any 8x8 not in the visible
  // area will be NULL.
  MODE_INFO **mi_grid_base;
  MODE_INFO **mi_grid_visible;
  MODE_INFO **prev_mi_grid_base;
  MODE_INFO **prev_mi_grid_visible;
#endif
  // Whether to use previous frame's motion vectors for prediction.
  int allow_ref_frame_mvs;

#if 0
#if !CONFIG_SEGMENT_PRED_LAST
  // Persistent mb segment id map used in prediction.
  int seg_map_idx;
  int prev_seg_map_idx;

  uint8_t *seg_map_array[NUM_PING_PONG_BUFFERS];
#endif
  uint8_t *last_frame_seg_map;
  uint8_t *current_frame_seg_map;
  int seg_map_alloc_size;

  InterpFilter interp_filter;

  int switchable_motion_mode;

  loop_filter_info_n lf_info;
  // The denominator of the superres scale; the numerator is fixed.
  uint8_t superres_scale_denominator;
  int superres_upscaled_width;
  int superres_upscaled_height;
  RestorationInfo rst_info[MAX_MB_PLANE];

  // rst_end_stripe[i] is one more than the index of the bottom stripe
  // for tile row i.
  int rst_end_stripe[MAX_TILE_ROWS];

  // Pointer to a scratch buffer used by self-guided restoration
  int32_t *rst_tmpbuf;

  // Flag signaling how frame contexts should be updated at the end of
  // a frame decode
  REFRESH_FRAME_CONTEXT_MODE refresh_frame_context;
#endif
  int ref_frame_sign_bias[TOTAL_REFS_PER_FRAME]; /* Two state 0, 1 */
#if 0
  struct loopfilter lf;
  struct segmentation seg;
  int all_lossless;
#endif
  int frame_parallel_decode;  // frame-based threading.
#if 0
  int reduced_tx_set_used;

  // Context probabilities for reference frame prediction
  MV_REFERENCE_FRAME comp_fwd_ref[FWD_REFS];
  MV_REFERENCE_FRAME comp_bwd_ref[BWD_REFS];
  REFERENCE_MODE reference_mode;

  FRAME_CONTEXT *fc;              /* this frame entropy */
  FRAME_CONTEXT *frame_contexts;  // FRAME_CONTEXTS
  FRAME_CONTEXT *pre_fc;          // Context referenced in this frame
  unsigned int frame_context_idx; /* Context to use/update */
#if CONFIG_NO_FRAME_CONTEXT_SIGNALING
  int fb_of_context_type[REF_FRAMES];
  int primary_ref_frame;
#endif
  FRAME_COUNTS counts;
#endif

  unsigned int frame_offset;

#if 0
  unsigned int current_video_frame;
  BITSTREAM_PROFILE profile;

  // AOM_BITS_8 in profile 0 or 1, AOM_BITS_10 or AOM_BITS_12 in profile 2 or 3.
  aom_bit_depth_t bit_depth;
  aom_bit_depth_t dequant_bit_depth;  // bit_depth of current dequantizer

  int error_resilient_mode;

  int tile_cols, tile_rows;
  int last_tile_cols, last_tile_rows;

  BOUNDARY_TYPE *boundary_info;
  int boundary_info_alloc_size;

#if CONFIG_MAX_TILE
  int min_log2_tile_cols;
  int max_log2_tile_cols;
  int max_log2_tile_rows;
  int min_log2_tile_rows;
  int min_log2_tiles;
  int max_tile_width_sb;
  int max_tile_height_sb;
  int uniform_tile_spacing_flag;
  int log2_tile_cols;                        // only valid for uniform tiles
  int log2_tile_rows;                        // only valid for uniform tiles
  int tile_col_start_sb[MAX_TILE_COLS + 1];  // valid for 0 <= i <= tile_cols
  int tile_row_start_sb[MAX_TILE_ROWS + 1];  // valid for 0 <= i <= tile_rows
#if CONFIG_DEPENDENT_HORZTILES
  int tile_row_independent[MAX_TILE_ROWS];  // valid for 0 <= i <  tile_rows
#endif
  int tile_width, tile_height;  // In MI units
#else
  int log2_tile_cols, log2_tile_rows;  // Used in non-large_scale_tile_coding.
  int tile_width, tile_height;         // In MI units
#endif  // CONFIG_MAX_TILE

#if CONFIG_EXT_TILE
  unsigned int large_scale_tile;
  unsigned int single_tile_decoding;
#endif  // CONFIG_EXT_TILE

#if CONFIG_DEPENDENT_HORZTILES
  int dependent_horz_tiles;
  int tile_group_start_row[MAX_TILE_ROWS][MAX_TILE_COLS];
  int tile_group_start_col[MAX_TILE_ROWS][MAX_TILE_COLS];
#endif
#if CONFIG_LOOPFILTERING_ACROSS_TILES
#if CONFIG_LOOPFILTERING_ACROSS_TILES_EXT
  int loop_filter_across_tiles_v_enabled;
  int loop_filter_across_tiles_h_enabled;
#else
  int loop_filter_across_tiles_enabled;
#endif  // CONFIG_LOOPFILTERING_ACROSS_TILES_EXT
#endif  // CONFIG_LOOPFILTERING_ACROSS_TILES

  int byte_alignment;
  int skip_loop_filter;

  // Private data associated with the frame buffer callbacks.
  void *cb_priv;
  aom_get_frame_buffer_cb_fn_t get_fb_cb;
  aom_release_frame_buffer_cb_fn_t release_fb_cb;

  // Handles memory for the codec.
  InternalFrameBufferList int_frame_buffers;
#endif
  // External BufferPool passed from outside.
  BufferPool buffer_pool;
#if 0
  PARTITION_CONTEXT *above_seg_context;
  ENTROPY_CONTEXT *above_context[MAX_MB_PLANE];
  TXFM_CONTEXT *above_txfm_context;
  TXFM_CONTEXT *top_txfm_context[MAX_MB_PLANE];
  TXFM_CONTEXT left_txfm_context[MAX_MB_PLANE][2 * MAX_MIB_SIZE];
  int above_context_alloc_cols;
#endif

  WarpedMotionParams global_motion[TOTAL_REFS_PER_FRAME];
#if 0
#if CONFIG_FILM_GRAIN
  int film_grain_params_present;
  aom_film_grain_t film_grain_params;
#endif
  int cdef_pri_damping;
  int cdef_sec_damping;
  int nb_cdef_strengths;
  int cdef_strengths[CDEF_MAX_STRENGTHS];
  int cdef_uv_strengths[CDEF_MAX_STRENGTHS];
  int cdef_bits;
  int cdef_preset[4];

  int delta_q_present_flag;
  // Resolution of delta quant
  int delta_q_res;
#if CONFIG_EXT_DELTA_Q
  int delta_lf_present_flag;
  // Resolution of delta lf level
  int delta_lf_res;
  // This is a flag for number of deltas of loop filter level
  // 0: use 1 delta, for y_vertical, y_horizontal, u, and v
  // 1: use separate deltas for each filter level
  int delta_lf_multi;
#endif
  int num_tg;
#endif
  struct {
    BLOCK_SIZE sb_size;
    int enable_order_hint;
    int order_hint_bits_minus1;
  } seq_params;
#if 0
  SequenceHeader seq_params;
  int current_frame_id;
  int ref_frame_id[REF_FRAMES];
  int valid_for_referencing[REF_FRAMES];
  int refresh_mask;
  int invalid_delta_frame_id_minus1;
  LV_MAP_CTX_TABLE coeff_ctx_table;
#endif
  TPL_MV_REF *tpl_mvs;
#if 0
  int tpl_mvs_mem_size;
#endif
  // TODO(jingning): This can be combined with sign_bias later.
  int8_t ref_frame_side[TOTAL_REFS_PER_FRAME];

#if 0
  int frame_refs_short_signaling;

#if CONFIG_SCALABILITY
  int temporal_layer_id;
  int enhancement_layer_id;
  int enhancement_layers_cnt;
#endif
#if TXCOEFF_TIMER
  int64_t cum_txcoeff_timer;
  int64_t txcoeff_timer;
  int txb_count;
#endif

#if TXCOEFF_COST_TIMER
  int64_t cum_txcoeff_cost_timer;
  int64_t txcoeff_cost_timer;
  int64_t txcoeff_cost_count;
#endif
  const cfg_options_t *options;
#endif

    int ref_buf_idx[INTER_REFS_PER_FRAME];
    int ref_order_hint[INTER_REFS_PER_FRAME];
} AV1_COMMON;

static INLINE void integer_mv_precision(MV *mv) {
  int mod = (mv->row % 8);
  if (mod != 0) {
    mv->row -= mod;
    if (abs(mod) > 4) {
      if (mod > 0) {
        mv->row += 8;
      } else {
        mv->row -= 8;
      }
    }
  }

  mod = (mv->col % 8);
  if (mod != 0) {
    mv->col -= mod;
    if (abs(mod) > 4) {
      if (mod > 0) {
        mv->col += 8;
      } else {
        mv->col -= 8;
      }
    }
  }
}

static INLINE int clamp(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

static INLINE void clamp_mv(MV *mv, int min_col, int max_col, int min_row,
                            int max_row) {
  mv->col = clamp(mv->col, min_col, max_col);
  mv->row = clamp(mv->row, min_row, max_row);
}

#if 0
static INLINE int frame_is_intra_only(const AV1_COMMON *const cm) {
  return cm->frame_type == KEY_FRAME || cm->intra_only;
}
#endif

static INLINE int is_intrabc_block(const MB_MODE_INFO *mbmi) {
  return mbmi->ref_frame[0] == INTRA_FRAME && mbmi->mv[0].as_mv.row != -0x8000;
  //return mbmi->use_intrabc;
}

static INLINE int is_inter_block(const MB_MODE_INFO *mbmi) {
  if (is_intrabc_block(mbmi)) return 1;
  return mbmi->ref_frame[0] > INTRA_FRAME;
}

static INLINE int has_second_ref(const MB_MODE_INFO *mbmi) {
  return mbmi->ref_frame[1] > INTRA_FRAME;
}

static INLINE MV_REFERENCE_FRAME comp_ref0(int ref_idx) {
  static const MV_REFERENCE_FRAME lut[] = {
    LAST_FRAME,     // LAST_LAST2_FRAMES,
    LAST_FRAME,     // LAST_LAST3_FRAMES,
    LAST_FRAME,     // LAST_GOLDEN_FRAMES,
    BWDREF_FRAME,   // BWDREF_ALTREF_FRAMES,
    LAST2_FRAME,    // LAST2_LAST3_FRAMES
    LAST2_FRAME,    // LAST2_GOLDEN_FRAMES,
    LAST3_FRAME,    // LAST3_GOLDEN_FRAMES,
    BWDREF_FRAME,   // BWDREF_ALTREF2_FRAMES,
    ALTREF2_FRAME,  // ALTREF2_ALTREF_FRAMES,
  };
  assert(NELEMENTS(lut) == TOTAL_UNIDIR_COMP_REFS);
  return lut[ref_idx];
}

static INLINE MV_REFERENCE_FRAME comp_ref1(int ref_idx) {
  static const MV_REFERENCE_FRAME lut[] = {
    LAST2_FRAME,    // LAST_LAST2_FRAMES,
    LAST3_FRAME,    // LAST_LAST3_FRAMES,
    GOLDEN_FRAME,   // LAST_GOLDEN_FRAMES,
    ALTREF_FRAME,   // BWDREF_ALTREF_FRAMES,
    LAST3_FRAME,    // LAST2_LAST3_FRAMES
    GOLDEN_FRAME,   // LAST2_GOLDEN_FRAMES,
    GOLDEN_FRAME,   // LAST3_GOLDEN_FRAMES,
    ALTREF2_FRAME,  // BWDREF_ALTREF2_FRAMES,
    ALTREF_FRAME,   // ALTREF2_ALTREF_FRAMES,
  };
  assert(NELEMENTS(lut) == TOTAL_UNIDIR_COMP_REFS);
  return lut[ref_idx];
}

#define WARPEDMODEL_PREC_BITS 16
#define GM_TRANS_ONLY_PREC_DIFF (WARPEDMODEL_PREC_BITS - 3)
#define WARPEDMODEL_ROW3HOMO_PREC_BITS 16

static INLINE int convert_to_trans_prec(int allow_hp, int coor) {
  if (allow_hp)
    return ROUND_POWER_OF_TWO_SIGNED(coor, WARPEDMODEL_PREC_BITS - 3);
  else
    return ROUND_POWER_OF_TWO_SIGNED(coor, WARPEDMODEL_PREC_BITS - 2) * 2;
}

static INLINE int block_center_x(int mi_col, BLOCK_SIZE bs) {
  const int bw = block_size_wide[bs];
  return mi_col * MI_SIZE + bw / 2 - 1;
}

static INLINE int block_center_y(int mi_row, BLOCK_SIZE bs) {
  const int bh = block_size_high[bs];
  return mi_row * MI_SIZE + bh / 2 - 1;
}

#if 0
static INLINE MV_REFERENCE_FRAME comp_ref0(int ref_idx) {
  static const MV_REFERENCE_FRAME lut[] = {
    LAST_FRAME,    // LAST_LAST2_FRAMES,
    LAST_FRAME,    // LAST_LAST3_FRAMES,
    LAST_FRAME,    // LAST_GOLDEN_FRAMES,
    BWDREF_FRAME,  // BWDREF_ALTREF_FRAMES,
  };
  assert(NELEMENTS(lut) == UNIDIR_COMP_REFS);
  return lut[ref_idx];
}

static INLINE MV_REFERENCE_FRAME comp_ref1(int ref_idx) {
  static const MV_REFERENCE_FRAME lut[] = {
    LAST2_FRAME,   // LAST_LAST2_FRAMES,
    LAST3_FRAME,   // LAST_LAST3_FRAMES,
    GOLDEN_FRAME,  // LAST_GOLDEN_FRAMES,
    ALTREF_FRAME,  // BWDREF_ALTREF_FRAMES,
  };
  assert(NELEMENTS(lut) == UNIDIR_COMP_REFS);
  return lut[ref_idx];
}
#endif

// Convert a global motion vector into a motion vector at the centre of the
// given block.
//
// The resulting motion vector will have three fractional bits of precision. If
// allow_hp is zero, the bottom bit will always be zero. If CONFIG_AMVR and
// is_integer is true, the bottom three bits will be zero (so the motion vector
// represents an integer)
static INLINE int_mv gm_get_motion_vector(const WarpedMotionParams *gm,
                                          int allow_hp, BLOCK_SIZE bsize,
                                          int mi_col, int mi_row,
                                          int is_integer) {
  int_mv res;
  const int32_t *mat = gm->wmmat;
  int x, y, tx, ty;

  if (gm->wmtype == TRANSLATION) {
    // All global motion vectors are stored with WARPEDMODEL_PREC_BITS (16)
    // bits of fractional precision. The offset for a translation is stored in
    // entries 0 and 1. For translations, all but the top three (two if
    // cm->allow_high_precision_mv is false) fractional bits are always zero.
    //
    // After the right shifts, there are 3 fractional bits of precision. If
    // allow_hp is false, the bottom bit is always zero (so we don't need a
    // call to convert_to_trans_prec here)
    res.as_mv.row = gm->wmmat[0] >> GM_TRANS_ONLY_PREC_DIFF;
    res.as_mv.col = gm->wmmat[1] >> GM_TRANS_ONLY_PREC_DIFF;
    assert(IMPLIES(1 & (res.as_mv.row | res.as_mv.col), allow_hp));
    if (is_integer) {
      integer_mv_precision(&res.as_mv);
    }
    return res;
  }

  x = block_center_x(mi_col, bsize);
  y = block_center_y(mi_row, bsize);

  if (gm->wmtype == ROTZOOM) {
    assert(gm->wmmat[5] == gm->wmmat[2]);
    assert(gm->wmmat[4] == -gm->wmmat[3]);
  }
  if (gm->wmtype > AFFINE) {
    int xc = (int)((int64_t)mat[2] * x + (int64_t)mat[3] * y + mat[0]);
    int yc = (int)((int64_t)mat[4] * x + (int64_t)mat[5] * y + mat[1]);
    const int Z = (int)((int64_t)mat[6] * x + (int64_t)mat[7] * y +
                        (1 << WARPEDMODEL_ROW3HOMO_PREC_BITS));
    xc *= 1 << (WARPEDMODEL_ROW3HOMO_PREC_BITS - WARPEDMODEL_PREC_BITS);
    yc *= 1 << (WARPEDMODEL_ROW3HOMO_PREC_BITS - WARPEDMODEL_PREC_BITS);
    xc = (int)(xc > 0 ? ((int64_t)xc + Z / 2) / Z : ((int64_t)xc - Z / 2) / Z);
    yc = (int)(yc > 0 ? ((int64_t)yc + Z / 2) / Z : ((int64_t)yc - Z / 2) / Z);
    tx = convert_to_trans_prec(allow_hp, xc) - (x << 3);
    ty = convert_to_trans_prec(allow_hp, yc) - (y << 3);
  } else {
    const int xc =
        (mat[2] - (1 << WARPEDMODEL_PREC_BITS)) * x + mat[3] * y + mat[0];
    const int yc =
        mat[4] * x + (mat[5] - (1 << WARPEDMODEL_PREC_BITS)) * y + mat[1];
    tx = convert_to_trans_prec(allow_hp, xc);
    ty = convert_to_trans_prec(allow_hp, yc);
  }

  res.as_mv.row = ty;
  res.as_mv.col = tx;

  if (is_integer) {
    integer_mv_precision(&res.as_mv);
  }
  return res;
}

static INLINE int have_newmv_in_inter_mode(PREDICTION_MODE mode) {
  return (mode == NEWMV || mode == NEW_NEWMV || mode == NEAREST_NEWMV ||
          mode == NEW_NEARESTMV || mode == NEAR_NEWMV || mode == NEW_NEARMV);
}

/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */
#ifndef AV1_COMMON_MVREF_COMMON_H_
#define AV1_COMMON_MVREF_COMMON_H_

//#include "av1/common/onyxc_int.h"
//#include "av1/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MVREF_ROW_COLS 3

// Set the upper limit of the motion vector component magnitude.
// This would make a motion vector fit in 26 bits. Plus 3 bits for the
// reference frame index. A tuple of motion vector can hence be stored within
// 32 bit range for efficient load/store operations.
#define REFMVS_LIMIT ((1 << 12) - 1)

typedef struct position {
  int row;
  int col;
} POSITION;

// clamp_mv_ref
#define MV_BORDER (16 << 3)  // Allow 16 pels in 1/8th pel units

static INLINE int get_relative_dist(const AV1_COMMON *cm, int a, int b) {
  if (!cm->seq_params.enable_order_hint) return 0;
 
  const int bits = cm->seq_params.order_hint_bits_minus1 + 1;
 
  assert(bits >= 1);
  assert(a >= 0 && a < (1 << bits));
  assert(b >= 0 && b < (1 << bits));

  int diff = a - b;
  int m = 1 << (bits - 1);
  diff = (diff & (m - 1)) - (diff & m);
  return diff;
}

static INLINE void clamp_mv_ref(MV *mv, int bw, int bh, const MACROBLOCKD *xd) {
  clamp_mv(mv, xd->mb_to_left_edge - bw * 8 - MV_BORDER,
           xd->mb_to_right_edge + bw * 8 + MV_BORDER,
           xd->mb_to_top_edge - bh * 8 - MV_BORDER,
           xd->mb_to_bottom_edge + bh * 8 + MV_BORDER);
}

// This function returns either the appropriate sub block or block's mv
// on whether the block_size < 8x8 and we have check_sub_blocks set.
static INLINE int_mv get_sub_block_mv(const MB_MODE_INFO *candidate,
                                      int which_mv, int search_col) {
  (void)search_col;
  return candidate->mv[which_mv];
}

static INLINE int_mv get_sub_block_pred_mv(const MB_MODE_INFO *candidate,
                                           int which_mv, int search_col) {
  (void)search_col;
  return candidate->mv[which_mv];
}

// Performs mv sign inversion if indicated by the reference frame combination.
static INLINE int_mv scale_mv(const MB_MODE_INFO *mbmi, int ref,
                              const MV_REFERENCE_FRAME this_ref_frame,
                              const int *ref_sign_bias) {
  int_mv mv = mbmi->mv[ref];
  if (ref_sign_bias[mbmi->ref_frame[ref]] != ref_sign_bias[this_ref_frame]) {
    mv.as_mv.row *= -1;
    mv.as_mv.col *= -1;
  }
  return mv;
}

// Checks that the given mi_row, mi_col and search point
// are inside the borders of the tile.
static INLINE int is_inside(const TileInfo *const tile, int mi_col, int mi_row,
                            int mi_rows, const POSITION *mi_pos) {
  const int dependent_horz_tile_flag = 0;
  if (dependent_horz_tile_flag && !tile->tg_horz_boundary) {
    return !(mi_row + mi_pos->row < 0 ||
             mi_col + mi_pos->col < tile->mi_col_start ||
             mi_row + mi_pos->row >= mi_rows ||
             mi_col + mi_pos->col >= tile->mi_col_end);
  } else {
    return !(mi_row + mi_pos->row < tile->mi_row_start ||
             mi_col + mi_pos->col < tile->mi_col_start ||
             mi_row + mi_pos->row >= tile->mi_row_end ||
             mi_col + mi_pos->col >= tile->mi_col_end);
  }
}

static INLINE int find_valid_row_offset(const TileInfo *const tile, int mi_row,
                                        int mi_rows, int row_offset) {
  const int dependent_horz_tile_flag = 0;
  if (dependent_horz_tile_flag && !tile->tg_horz_boundary)
    return clamp(row_offset, -mi_row, mi_rows - mi_row - 1);
  else
    return clamp(row_offset, tile->mi_row_start - mi_row,
                 tile->mi_row_end - mi_row - 1);
}

static INLINE int find_valid_col_offset(const TileInfo *const tile, int mi_col,
                                        int col_offset) {
  return clamp(col_offset, tile->mi_col_start - mi_col,
               tile->mi_col_end - mi_col - 1);
}

static INLINE void lower_mv_precision(MV *mv, int allow_hp,
                                      int is_integer) {
  if (is_integer) {
    integer_mv_precision(mv);
  } else {
    if (!allow_hp) {
      if (mv->row & 1) mv->row += (mv->row > 0 ? -1 : 1);
      if (mv->col & 1) mv->col += (mv->col > 0 ? -1 : 1);
    }
  }
}

static INLINE int8_t get_uni_comp_ref_idx(const MV_REFERENCE_FRAME *const rf) {
  // Single ref pred
  if (rf[1] <= INTRA_FRAME) return -1;

  // Bi-directional comp ref pred
  if ((rf[0] < BWDREF_FRAME) && (rf[1] >= BWDREF_FRAME)) return -1;

  for (int8_t ref_idx = 0; ref_idx < TOTAL_UNIDIR_COMP_REFS; ++ref_idx) {
    if (rf[0] == comp_ref0(ref_idx) && rf[1] == comp_ref1(ref_idx))
      return ref_idx;
  }
  return -1;
}

static INLINE int8_t av1_ref_frame_type(const MV_REFERENCE_FRAME *const rf) {
  if (rf[1] > INTRA_FRAME) {
    const int8_t uni_comp_ref_idx = get_uni_comp_ref_idx(rf);
    if (uni_comp_ref_idx >= 0) {
      assert((REF_FRAMES + FWD_REFS * BWD_REFS + uni_comp_ref_idx) <
             MODE_CTX_REF_FRAMES);
      return REF_FRAMES + FWD_REFS * BWD_REFS + uni_comp_ref_idx;
    } else {
      return REF_FRAMES + FWD_RF_OFFSET(rf[0]) +
             BWD_RF_OFFSET(rf[1]) * FWD_REFS;
    }
  }

  return rf[0];
}

// clang-format off
static MV_REFERENCE_FRAME ref_frame_map[TOTAL_COMP_REFS][2] = {
  { LAST_FRAME, BWDREF_FRAME },  { LAST2_FRAME, BWDREF_FRAME },
  { LAST3_FRAME, BWDREF_FRAME }, { GOLDEN_FRAME, BWDREF_FRAME },

  { LAST_FRAME, ALTREF2_FRAME },  { LAST2_FRAME, ALTREF2_FRAME },
  { LAST3_FRAME, ALTREF2_FRAME }, { GOLDEN_FRAME, ALTREF2_FRAME },

  { LAST_FRAME, ALTREF_FRAME },  { LAST2_FRAME, ALTREF_FRAME },
  { LAST3_FRAME, ALTREF_FRAME }, { GOLDEN_FRAME, ALTREF_FRAME },

  { LAST_FRAME, LAST2_FRAME }, { LAST_FRAME, LAST3_FRAME },
  { LAST_FRAME, GOLDEN_FRAME }, { BWDREF_FRAME, ALTREF_FRAME },

  // NOTE: Following reference frame pairs are not supported to be explicitly
  //       signalled, but they are possibly chosen by the use of skip_mode,
  //       which may use the most recent one-sided reference frame pair.
  { LAST2_FRAME, LAST3_FRAME }, { LAST2_FRAME, GOLDEN_FRAME },
  { LAST3_FRAME, GOLDEN_FRAME }, {BWDREF_FRAME, ALTREF2_FRAME},
  { ALTREF2_FRAME, ALTREF_FRAME }
};
// clang-format on

static INLINE void av1_set_ref_frame(MV_REFERENCE_FRAME *rf,
                                     int8_t ref_frame_type) {
  if (ref_frame_type >= REF_FRAMES) {
    rf[0] = ref_frame_map[ref_frame_type - REF_FRAMES][0];
    rf[1] = ref_frame_map[ref_frame_type - REF_FRAMES][1];
  } else {
    rf[0] = ref_frame_type;
    rf[1] = NONE_FRAME;
    assert(ref_frame_type > NONE_FRAME);
  }
}

static uint16_t compound_mode_ctx_map[3][COMP_NEWMV_CTXS] = {
  { 0, 1, 1, 1, 1 },
  { 1, 2, 3, 4, 4 },
  { 4, 4, 5, 6, 7 },
};

static INLINE int16_t av1_mode_context_analyzer(
    const int16_t *const mode_context, const MV_REFERENCE_FRAME *const rf) {
  const int8_t ref_frame = av1_ref_frame_type(rf);

  if (rf[1] <= INTRA_FRAME) return mode_context[ref_frame];

  const int16_t newmv_ctx = mode_context[ref_frame] & NEWMV_CTX_MASK;
  const int16_t refmv_ctx =
      (mode_context[ref_frame] >> REFMV_OFFSET) & REFMV_CTX_MASK;

  const int16_t comp_ctx = compound_mode_ctx_map[refmv_ctx >> 1][AOMMIN(
      newmv_ctx, COMP_NEWMV_CTXS - 1)];
  return comp_ctx;
}

static INLINE uint8_t av1_drl_ctx(const CANDIDATE_MV *ref_mv_stack,
                                  int ref_idx) {
  if (ref_mv_stack[ref_idx].weight >= REF_CAT_LEVEL &&
      ref_mv_stack[ref_idx + 1].weight >= REF_CAT_LEVEL)
    return 0;

  if (ref_mv_stack[ref_idx].weight >= REF_CAT_LEVEL &&
      ref_mv_stack[ref_idx + 1].weight < REF_CAT_LEVEL)
    return 1;

  if (ref_mv_stack[ref_idx].weight < REF_CAT_LEVEL &&
      ref_mv_stack[ref_idx + 1].weight < REF_CAT_LEVEL)
    return 2;

  return 0;
}

void av1_setup_frame_buf_refs(AV1_COMMON *cm);
void av1_setup_frame_sign_bias(AV1_COMMON *cm);
void av1_setup_skip_mode_allowed(AV1_COMMON *cm);

#if 0
void av1_setup_motion_field(AV1_COMMON *cm);
void av1_set_frame_refs(AV1_COMMON *const cm, int lst_map_idx, int gld_map_idx);
#endif  // CONFIG_FRAME_REFS_SIGNALING

#if 0
static INLINE void av1_collect_neighbors_ref_counts(MACROBLOCKD *const xd) {
  av1_zero(xd->neighbors_ref_counts);

  uint8_t *const ref_counts = xd->neighbors_ref_counts;

  const MB_MODE_INFO *const above_mbmi = xd->above_mbmi;
  const MB_MODE_INFO *const left_mbmi = xd->left_mbmi;
  const int above_in_image = xd->up_available;
  const int left_in_image = xd->left_available;

  // Above neighbor
  if (above_in_image && is_inter_block(above_mbmi)) {
    ref_counts[above_mbmi->ref_frame[0]]++;
    if (has_second_ref(above_mbmi)) {
      ref_counts[above_mbmi->ref_frame[1]]++;
    }
  }

  // Left neighbor
  if (left_in_image && is_inter_block(left_mbmi)) {
    ref_counts[left_mbmi->ref_frame[0]]++;
    if (has_second_ref(left_mbmi)) {
      ref_counts[left_mbmi->ref_frame[1]]++;
    }
  }
}
#endif

void av1_copy_frame_mvs(const AV1_COMMON *const cm, MB_MODE_INFO *mi,
                        int mi_row, int mi_col, int x_mis, int y_mis);

void av1_find_mv_refs(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                      MB_MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                      uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
                      CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
                      int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES],
                      int_mv *global_mvs, int mi_row, int mi_col,
                      int16_t *mode_context);

// check a list of motion vectors by sad score using a number rows of pixels
// above and a number cols of pixels in the left to select the one with best
// score to use as ref motion vector
void av1_find_best_ref_mvs(int allow_hp, int_mv *mvlist, int_mv *nearest_mv,
                           int_mv *near_mv, int is_integer);

int selectSamples(MV *mv, int *pts, int *pts_inref, int len, BLOCK_SIZE bsize);
int findSamples(const AV1_COMMON *cm, MACROBLOCKD *xd, int mi_row, int mi_col,
                int *pts, int *pts_inref);

#define INTRABC_DELAY_PIXELS 256  //  Delay of 256 pixels
#define INTRABC_DELAY_SB64 (INTRABC_DELAY_PIXELS / 64)
#define USE_WAVE_FRONT 1  // Use only top left area of frame for reference.

static INLINE void av1_find_ref_dv(int_mv *ref_dv, const TileInfo *const tile,
                                   int mib_size, int mi_row, int mi_col) {
  (void)mi_col;
  if (mi_row - mib_size < tile->mi_row_start) {
    ref_dv->as_mv.row = 0;
    ref_dv->as_mv.col = -MI_SIZE * mib_size - INTRABC_DELAY_PIXELS;
  } else {
    ref_dv->as_mv.row = -MI_SIZE * mib_size;
    ref_dv->as_mv.col = 0;
  }
  ref_dv->as_mv.row *= 8;
  ref_dv->as_mv.col *= 8;
}

static INLINE int av1_is_dv_valid(const MV dv, const AV1_COMMON *cm,
                                  const MACROBLOCKD *xd, int mi_row, int mi_col,
                                  BLOCK_SIZE bsize, int mib_size_log2) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int SCALE_PX_TO_MV = 8;
  // Disallow subpixel for now
  // SUBPEL_MASK is not the correct scale
  if (((dv.row & (SCALE_PX_TO_MV - 1)) || (dv.col & (SCALE_PX_TO_MV - 1))))
    return 0;

  const TileInfo *const tile = &xd->tile;
  // Is the source top-left inside the current tile?
  const int src_top_edge = mi_row * MI_SIZE * SCALE_PX_TO_MV + dv.row;
  const int tile_top_edge = tile->mi_row_start * MI_SIZE * SCALE_PX_TO_MV;
  if (src_top_edge < tile_top_edge) return 0;
  const int src_left_edge = mi_col * MI_SIZE * SCALE_PX_TO_MV + dv.col;
  const int tile_left_edge = tile->mi_col_start * MI_SIZE * SCALE_PX_TO_MV;
  if (src_left_edge < tile_left_edge) return 0;
  // Is the bottom right inside the current tile?
  const int src_bottom_edge = (mi_row * MI_SIZE + bh) * SCALE_PX_TO_MV + dv.row;
  const int tile_bottom_edge = tile->mi_row_end * MI_SIZE * SCALE_PX_TO_MV;
  if (src_bottom_edge > tile_bottom_edge) return 0;
  const int src_right_edge = (mi_col * MI_SIZE + bw) * SCALE_PX_TO_MV + dv.col;
  const int tile_right_edge = tile->mi_col_end * MI_SIZE * SCALE_PX_TO_MV;
  if (src_right_edge > tile_right_edge) return 0;

#if 0
  // Special case for sub 8x8 chroma cases, to prevent referring to chroma
  // pixels outside current tile.
  for (int plane = 1; plane < av1_num_planes(cm); ++plane) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    if (is_chroma_reference(mi_row, mi_col, bsize, pd->subsampling_x,
                            pd->subsampling_y)) {
      if (bw < 8 && pd->subsampling_x)
        if (src_left_edge < tile_left_edge + 4 * SCALE_PX_TO_MV) return 0;
      if (bh < 8 && pd->subsampling_y)
        if (src_top_edge < tile_top_edge + 4 * SCALE_PX_TO_MV) return 0;
    }
  }
#endif

  // Is the bottom right within an already coded SB? Also consider additional
  // constraints to facilitate HW decoder.
  const int max_mib_size = 1 << mib_size_log2;
  const int active_sb_row = mi_row >> mib_size_log2;
  const int active_sb64_col = (mi_col * MI_SIZE) >> 6;
  const int sb_size = max_mib_size * MI_SIZE;
  const int src_sb_row = ((src_bottom_edge >> 3) - 1) / sb_size;
  const int src_sb64_col = ((src_right_edge >> 3) - 1) >> 6;
  const int total_sb64_per_row =
      ((tile->mi_col_end - tile->mi_col_start - 1) >> 4) + 1;
  const int active_sb64 = active_sb_row * total_sb64_per_row + active_sb64_col;
  const int src_sb64 = src_sb_row * total_sb64_per_row + src_sb64_col;
  if (src_sb64 >= active_sb64 - INTRABC_DELAY_SB64) return 0;

#if USE_WAVE_FRONT
  const int gradient = 1 + INTRABC_DELAY_SB64 + (sb_size > 64);
  const int wf_offset = gradient * (active_sb_row - src_sb_row);
  if (src_sb_row > active_sb_row ||
      src_sb64_col >= active_sb64_col - INTRABC_DELAY_SB64 + wf_offset)
    return 0;
#endif

  return 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_COMMON_MVREF_COMMON_H_

/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <stdlib.h>

//#include "av1/common/mvref_common.h"
//#include "av1/common/warped_motion.h"

// Although we assign 32 bit integers, all the values are strictly under 14
// bits.
static int div_mult[32] = { 0,    16384, 8192, 5461, 4096, 3276, 2730, 2340,
                            2048, 1820,  1638, 1489, 1365, 1260, 1170, 1092,
                            1024, 963,   910,  862,  819,  780,  744,  712,
                            682,  655,   630,  606,  585,  564,  546,  528 };

// TODO(jingning): Consider the use of lookup table for (num / den)
// altogether.
static void get_mv_projection(MV *output, MV ref, int num, int den) {
  den = AOMMIN(den, MAX_FRAME_DISTANCE);
  num = num > 0 ? AOMMIN(num, MAX_FRAME_DISTANCE)
                : AOMMAX(num, -MAX_FRAME_DISTANCE);
  int mv_row = ROUND_POWER_OF_TWO_SIGNED(ref.row * num * div_mult[den], 14);
  int mv_col = ROUND_POWER_OF_TWO_SIGNED(ref.col * num * div_mult[den], 14);
  const int clamp_max = MV_UPP - 1;
  const int clamp_min = MV_LOW + 1;
  output->row = (int16_t)clamp(mv_row, clamp_min, clamp_max);
  output->col = (int16_t)clamp(mv_col, clamp_min, clamp_max);
}

#if 0
void av1_copy_frame_mvs(const AV1_COMMON *const cm, MB_MODE_INFO *mi,
                        int mi_row, int mi_col, int x_mis, int y_mis) {
  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_cols, 1);
  MV_REF *frame_mvs =
      cm->cur_frame.mvs + (mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_mis = ROUND_POWER_OF_TWO(x_mis, 1);
  y_mis = ROUND_POWER_OF_TWO(y_mis, 1);
  int w, h;

  for (h = 0; h < y_mis; h++) {
    MV_REF *mv = frame_mvs;
    for (w = 0; w < x_mis; w++) {
      mv->ref_frame = NONE_FRAME;
      mv->mv.as_int = 0;

      for (int idx = 0; idx < 2; ++idx) {
        MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
        if (ref_frame > INTRA_FRAME) {
          int8_t ref_idx = cm->ref_frame_side[ref_frame];
          if (ref_idx) continue;
          if ((abs(mi->mv[idx].as_mv.row) > REFMVS_LIMIT) ||
              (abs(mi->mv[idx].as_mv.col) > REFMVS_LIMIT))
            continue;
          mv->ref_frame = ref_frame;
          mv->mv.as_int = mi->mv[idx].as_int;
        }
      }
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}
#endif

static void add_ref_mv_candidate(
    const MB_MODE_INFO *const candidate, const MV_REFERENCE_FRAME rf[2],
    uint8_t *refmv_count, uint8_t *ref_match_count, uint8_t *newmv_count,
    CANDIDATE_MV *ref_mv_stack, int_mv *gm_mv_candidates,
    const WarpedMotionParams *gm_params, int col, int weight) {
  if (!is_inter_block(candidate)) return;  // for intrabc
  int index = 0, ref;
  assert(weight % 2 == 0);

  if (rf[1] == NONE_FRAME) {
    // single reference frame
    for (ref = 0; ref < 2; ++ref) {
      if (candidate->ref_frame[ref] == rf[0]) {
        int_mv this_refmv;
        if (is_global_mv_block(candidate, gm_params[rf[0]].wmtype))
          this_refmv = gm_mv_candidates[0];
        else
          this_refmv = get_sub_block_mv(candidate, ref, col);

        for (index = 0; index < *refmv_count; ++index)
          if (ref_mv_stack[index].this_mv.as_int == this_refmv.as_int) break;

        if (index < *refmv_count) ref_mv_stack[index].weight += weight;

        // Add a new item to the list.
        if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
          ref_mv_stack[index].this_mv = this_refmv;
          ref_mv_stack[index].weight = weight;
          ++(*refmv_count);
        }
        if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
        ++*ref_match_count;
      }
    }
  } else {
    // compound reference frame
    if (candidate->ref_frame[0] == rf[0] && candidate->ref_frame[1] == rf[1]) {
      int_mv this_refmv[2];

      for (ref = 0; ref < 2; ++ref) {
        if (is_global_mv_block(candidate, gm_params[rf[ref]].wmtype))
          this_refmv[ref] = gm_mv_candidates[ref];
        else
          this_refmv[ref] = get_sub_block_mv(candidate, ref, col);
      }

      for (index = 0; index < *refmv_count; ++index)
        if ((ref_mv_stack[index].this_mv.as_int == this_refmv[0].as_int) &&
            (ref_mv_stack[index].comp_mv.as_int == this_refmv[1].as_int))
          break;

      if (index < *refmv_count) ref_mv_stack[index].weight += weight;

      // Add a new item to the list.
      if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[index].this_mv = this_refmv[0];
        ref_mv_stack[index].comp_mv = this_refmv[1];
        ref_mv_stack[index].weight = weight;
        ++(*refmv_count);
      }
      if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
      ++*ref_match_count;
    }
  }
}

static void scan_row_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          int mi_row, int mi_col,
                          const MV_REFERENCE_FRAME rf[2], int row_offset,
                          CANDIDATE_MV *ref_mv_stack, uint8_t *refmv_count,
                          uint8_t *ref_match_count, uint8_t *newmv_count,
                          int_mv *gm_mv_candidates, int max_row_offset,
                          int *processed_rows) {
  int end_mi = AOMMIN(xd->n8_w, cm->mi_cols - mi_col);
  end_mi = AOMMIN(end_mi, mi_size_wide[BLOCK_64X64]);
  const int n8_w_8 = mi_size_wide[BLOCK_8X8];
  const int n8_w_16 = mi_size_wide[BLOCK_16X16];
  int i;
  int col_offset = 0;
  const int shift = 0;
  // TODO(jingning): Revisit this part after cb4x4 is stable.
  if (abs(row_offset) > 1) {
    col_offset = 1;
    if ((mi_col & 0x01) && xd->n8_w < n8_w_8) --col_offset;
  }
  const int use_step_16 = (xd->n8_w >= 16);
  MB_MODE_INFO *const candidate_mi0 = xd->mi + row_offset * xd->mi_stride;
  (void)mi_row;

  for (i = 0; i < end_mi;) {
    const MB_MODE_INFO *const candidate = &candidate_mi0[col_offset + i];
    const int candidate_bsize = candidate->sb_type;
    const int n8_w = mi_size_wide[candidate_bsize];
    int len = AOMMIN(xd->n8_w, n8_w);
    if (use_step_16)
      len = AOMMAX(n8_w_16, len);
    else if (abs(row_offset) > 1)
      len = AOMMAX(len, n8_w_8);

    int weight = 2;
    if (xd->n8_w >= n8_w_8 && xd->n8_w <= n8_w) {
      int inc = AOMMIN(-max_row_offset + row_offset + 1,
                       mi_size_high[candidate_bsize]);
      // Obtain range used in weight calculation.
      weight = AOMMAX(weight, (inc << shift));
      // Update processed rows.
      *processed_rows = inc - row_offset - 1;
    }

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, gm_mv_candidates,
                         cm->global_motion, col_offset + i, len * weight);

    i += len;
  }
}

static void scan_col_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          int mi_row, int mi_col,
                          const MV_REFERENCE_FRAME rf[2], int col_offset,
                          CANDIDATE_MV *ref_mv_stack, uint8_t *refmv_count,
                          uint8_t *ref_match_count, uint8_t *newmv_count,
                          int_mv *gm_mv_candidates, int max_col_offset,
                          int *processed_cols) {
  int end_mi = AOMMIN(xd->n8_h, cm->mi_rows - mi_row);
  end_mi = AOMMIN(end_mi, mi_size_high[BLOCK_64X64]);
  const int n8_h_8 = mi_size_high[BLOCK_8X8];
  const int n8_h_16 = mi_size_high[BLOCK_16X16];
  int i;
  int row_offset = 0;
  const int shift = 0;
  if (abs(col_offset) > 1) {
    row_offset = 1;
    if ((mi_row & 0x01) && xd->n8_h < n8_h_8) --row_offset;
  }
  const int use_step_16 = (xd->n8_h >= 16);
  (void)mi_col;

  for (i = 0; i < end_mi;) {
    const MB_MODE_INFO *const candidate =
        &xd->mi[(row_offset + i) * xd->mi_stride + col_offset];
    const int candidate_bsize = candidate->sb_type;
    const int n8_h = mi_size_high[candidate_bsize];
    int len = AOMMIN(xd->n8_h, n8_h);
    if (use_step_16)
      len = AOMMAX(n8_h_16, len);
    else if (abs(col_offset) > 1)
      len = AOMMAX(len, n8_h_8);

    int weight = 2;
    if (xd->n8_h >= n8_h_8 && xd->n8_h <= n8_h) {
      int inc = AOMMIN(-max_col_offset + col_offset + 1,
                       mi_size_wide[candidate_bsize]);
      // Obtain range used in weight calculation.
      weight = AOMMAX(weight, (inc << shift));
      // Update processed cols.
      *processed_cols = inc - col_offset - 1;
    }

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, gm_mv_candidates,
                         cm->global_motion, col_offset, len * weight);

    i += len;
  }
}

static void scan_blk_mbmi(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          const int mi_row, const int mi_col,
                          const MV_REFERENCE_FRAME rf[2], int row_offset,
                          int col_offset, CANDIDATE_MV *ref_mv_stack,
                          uint8_t *ref_match_count, uint8_t *newmv_count,
                          int_mv *gm_mv_candidates,
                          uint8_t refmv_count[MODE_CTX_REF_FRAMES]) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;

  mi_pos.row = row_offset;
  mi_pos.col = col_offset;

  if (is_inside(tile, mi_col, mi_row, cm->mi_rows, &mi_pos)) {
    const MB_MODE_INFO *const candidate =
        &xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    const int len = mi_size_wide[BLOCK_8X8];

    add_ref_mv_candidate(candidate, rf, refmv_count, ref_match_count,
                         newmv_count, ref_mv_stack, gm_mv_candidates,
                         cm->global_motion, mi_pos.col, 2 * len);
  }  // Analyze a single 8x8 block motion information.
}

static int has_top_right(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                         int mi_row, int mi_col, int bs) {
  const int sb_mi_size = mi_size_wide[cm->seq_params.sb_size];
  const int mask_row = mi_row & (sb_mi_size - 1);
  const int mask_col = mi_col & (sb_mi_size - 1);

  if (bs > mi_size_wide[BLOCK_64X64]) return 0;

  // In a split partition all apart from the bottom right has a top right
  int has_tr = !((mask_row & bs) && (mask_col & bs));

  // bs > 0 and bs is a power of 2
  assert(bs > 0 && !(bs & (bs - 1)));

  // For each 4x4 group of blocks, when the bottom right is decoded the blocks
  // to the right have not been decoded therefore the bottom right does
  // not have a top right
  while (bs < sb_mi_size) {
    if (mask_col & bs) {
      if ((mask_col & (2 * bs)) && (mask_row & (2 * bs))) {
        has_tr = 0;
        break;
      }
    } else {
      break;
    }
    bs <<= 1;
  }

  // The left hand of two vertical rectangles always has a top right (as the
  // block above will have been decoded)
  if (xd->n8_w < xd->n8_h)
    if (!xd->is_sec_rect) has_tr = 1;

  // The bottom of two horizontal rectangles never has a top right (as the block
  // to the right won't have been decoded)
  if (xd->n8_w > xd->n8_h)
    if (xd->is_sec_rect) has_tr = 0;

  // The bottom left square of a Vertical A (in the old format) does
  // not have a top right as it is decoded before the right hand
  // rectangle of the partition
  if (xd->cur_mi.partition == PARTITION_VERT_A) {
    if (xd->n8_w == xd->n8_h)
      if (mask_row & bs) has_tr = 0;
  }

  return has_tr;
}

static int check_sb_border(const int mi_row, const int mi_col,
                           const int row_offset, const int col_offset) {
  const int sb_mi_size = mi_size_wide[BLOCK_64X64];
  const int row = mi_row & (sb_mi_size - 1);
  const int col = mi_col & (sb_mi_size - 1);

  if (row + row_offset < 0 || row + row_offset >= sb_mi_size ||
      col + col_offset < 0 || col + col_offset >= sb_mi_size)
    return 0;

  return 1;
}

static int add_tpl_ref_mv(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          int mi_row, int mi_col, MV_REFERENCE_FRAME ref_frame,
                          int blk_row, int blk_col, int_mv *gm_mv_candidates,
                          uint8_t refmv_count[MODE_CTX_REF_FRAMES],
                          CANDIDATE_MV ref_mv_stacks[][MAX_REF_MV_STACK_SIZE],
                          int16_t *mode_context) {
  POSITION mi_pos;
  int idx;
  const int weight_unit = 1;  // mi_size_wide[BLOCK_8X8];

  mi_pos.row = (mi_row & 0x01) ? blk_row : blk_row + 1;
  mi_pos.col = (mi_col & 0x01) ? blk_col : blk_col + 1;

  if (!is_inside(&xd->tile, mi_col, mi_row, cm->mi_rows, &mi_pos)) return 0;

  const TPL_MV_REF *prev_frame_mvs =
      cm->tpl_mvs + ((mi_row + mi_pos.row) >> 1) * (cm->mi_stride >> 1) +
      ((mi_col + mi_pos.col) >> 1);

  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame);

  if (rf[1] == NONE_FRAME) {
    int cur_frame_index = cm->cur_frame.cur_frame_offset;
    int buf_idx_0 = cm->frame_refs[FWD_RF_OFFSET(rf[0])].idx;
    int frame0_index = cm->buffer_pool.frame_bufs[buf_idx_0].cur_frame_offset;
    int cur_offset_0 = get_relative_dist(cm, cur_frame_index, frame0_index);
    CANDIDATE_MV *ref_mv_stack = ref_mv_stacks[rf[0]];

    if (prev_frame_mvs->mfmv0.as_int != INVALID_MV) {
      int_mv this_refmv;

      get_mv_projection(&this_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                        cur_offset_0, prev_frame_mvs->ref_frame_offset);
      lower_mv_precision(&this_refmv.as_mv, cm->allow_high_precision_mv,
                         cm->cur_frame_force_integer_mv);

      if (blk_row == 0 && blk_col == 0)
        if (abs(this_refmv.as_mv.row - gm_mv_candidates[0].as_mv.row) >= 16 ||
            abs(this_refmv.as_mv.col - gm_mv_candidates[0].as_mv.col) >= 16)
          mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);

      for (idx = 0; idx < refmv_count[rf[0]]; ++idx)
        if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int) break;

      if (idx < refmv_count[rf[0]]) ref_mv_stack[idx].weight += 2 * weight_unit;

      if (idx == refmv_count[rf[0]] &&
          refmv_count[rf[0]] < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[idx].weight = 2 * weight_unit;
        ++(refmv_count[rf[0]]);
      }

      return 1;
    }
  } else {
    // Process compound inter mode
    int cur_frame_index = cm->cur_frame.cur_frame_offset;
    int buf_idx_0 = cm->frame_refs[FWD_RF_OFFSET(rf[0])].idx;
    int frame0_index = cm->buffer_pool.frame_bufs[buf_idx_0].cur_frame_offset;

    int cur_offset_0 = get_relative_dist(cm, cur_frame_index, frame0_index);
    int buf_idx_1 = cm->frame_refs[FWD_RF_OFFSET(rf[1])].idx;
    int frame1_index = cm->buffer_pool.frame_bufs[buf_idx_1].cur_frame_offset;
    int cur_offset_1 = get_relative_dist(cm, cur_frame_index, frame1_index);
    CANDIDATE_MV *ref_mv_stack = ref_mv_stacks[ref_frame];

    if (prev_frame_mvs->mfmv0.as_int != INVALID_MV) {
      int_mv this_refmv;
      int_mv comp_refmv;
      get_mv_projection(&this_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                        cur_offset_0, prev_frame_mvs->ref_frame_offset);
      get_mv_projection(&comp_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                        cur_offset_1, prev_frame_mvs->ref_frame_offset);

      lower_mv_precision(&this_refmv.as_mv, cm->allow_high_precision_mv,
                         cm->cur_frame_force_integer_mv);
      lower_mv_precision(&comp_refmv.as_mv, cm->allow_high_precision_mv,
                         cm->cur_frame_force_integer_mv);

      if (blk_row == 0 && blk_col == 0)
        if (abs(this_refmv.as_mv.row - gm_mv_candidates[0].as_mv.row) >= 16 ||
            abs(this_refmv.as_mv.col - gm_mv_candidates[0].as_mv.col) >= 16 ||
            abs(comp_refmv.as_mv.row - gm_mv_candidates[1].as_mv.row) >= 16 ||
            abs(comp_refmv.as_mv.col - gm_mv_candidates[1].as_mv.col) >= 16)
          mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);

      for (idx = 0; idx < refmv_count[ref_frame]; ++idx)
        if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int &&
            comp_refmv.as_int == ref_mv_stack[idx].comp_mv.as_int)
          break;

      if (idx < refmv_count[ref_frame])
        ref_mv_stack[idx].weight += 2 * weight_unit;

      if (idx == refmv_count[ref_frame] &&
          refmv_count[ref_frame] < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[idx].comp_mv.as_int = comp_refmv.as_int;
        ref_mv_stack[idx].weight = 2 * weight_unit;
        ++(refmv_count[ref_frame]);
      }
      return 1;
    }
  }
  return 0;
}

static void setup_ref_mv_list(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref_frame,
    uint8_t refmv_count[MODE_CTX_REF_FRAMES],
    CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
    int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES], int_mv *gm_mv_candidates,
    int mi_row, int mi_col, int16_t *mode_context) {
  const int bs = AOMMAX(xd->n8_w, xd->n8_h);
  const int has_tr = has_top_right(cm, xd, mi_row, mi_col, bs);
  MV_REFERENCE_FRAME rf[2];

  const TileInfo *const tile = &xd->tile;
  int max_row_offset = 0, max_col_offset = 0;
  const int row_adj = (xd->n8_h < mi_size_high[BLOCK_8X8]) && (mi_row & 0x01);
  const int col_adj = (xd->n8_w < mi_size_wide[BLOCK_8X8]) && (mi_col & 0x01);
  int processed_rows = 0;
  int processed_cols = 0;

  av1_set_ref_frame(rf, ref_frame);
  mode_context[ref_frame] = 0;
  refmv_count[ref_frame] = 0;

  // Find valid maximum row/col offset.
  if (xd->up_available) {
    max_row_offset = -(MVREF_ROW_COLS << 1) + row_adj;

    if (xd->n8_h < mi_size_high[BLOCK_8X8])
      max_row_offset = -(2 << 1) + row_adj;

    max_row_offset =
        find_valid_row_offset(tile, mi_row, cm->mi_rows, max_row_offset);
  }

  if (xd->left_available) {
    max_col_offset = -(MVREF_ROW_COLS << 1) + col_adj;

    if (xd->n8_w < mi_size_wide[BLOCK_8X8])
      max_col_offset = -(2 << 1) + col_adj;

    max_col_offset = find_valid_col_offset(tile, mi_col, max_col_offset);
  }

  uint8_t col_match_count = 0;
  uint8_t row_match_count = 0;
  uint8_t newmv_count = 0;

  // Scan the first above row mode info. row_offset = -1;
  if (abs(max_row_offset) >= 1)
    scan_row_mbmi(cm, xd, mi_row, mi_col, rf, -1, ref_mv_stack[ref_frame],
                  &refmv_count[ref_frame], &row_match_count, &newmv_count,
                  gm_mv_candidates, max_row_offset, &processed_rows);
  // Scan the first left column mode info. col_offset = -1;
  if (abs(max_col_offset) >= 1)
    scan_col_mbmi(cm, xd, mi_row, mi_col, rf, -1, ref_mv_stack[ref_frame],
                  &refmv_count[ref_frame], &col_match_count, &newmv_count,
                  gm_mv_candidates, max_col_offset, &processed_cols);
  // Check top-right boundary
  if (has_tr)
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, -1, xd->n8_w,
                  ref_mv_stack[ref_frame], &row_match_count, &newmv_count,
                  gm_mv_candidates, &refmv_count[ref_frame]);

  uint8_t nearest_match = (row_match_count > 0) + (col_match_count > 0);
  uint8_t nearest_refmv_count = refmv_count[ref_frame];

  // TODO(yunqing): for comp_search, do it for all 3 cases.
  for (int idx = 0; idx < nearest_refmv_count; ++idx)
    ref_mv_stack[ref_frame][idx].weight += REF_CAT_LEVEL;

  if (cm->allow_ref_frame_mvs) {
    int is_available = 0;
    const int voffset = AOMMAX(mi_size_high[BLOCK_8X8], xd->n8_h);
    const int hoffset = AOMMAX(mi_size_wide[BLOCK_8X8], xd->n8_w);
    const int blk_row_end = AOMMIN(xd->n8_h, mi_size_high[BLOCK_64X64]);
    const int blk_col_end = AOMMIN(xd->n8_w, mi_size_wide[BLOCK_64X64]);

    const int tpl_sample_pos[3][2] = {
      { voffset, -2 },
      { voffset, hoffset },
      { voffset - 2, hoffset },
    };
    const int allow_extension = (xd->n8_h >= mi_size_high[BLOCK_8X8]) &&
                                (xd->n8_h < mi_size_high[BLOCK_64X64]) &&
                                (xd->n8_w >= mi_size_wide[BLOCK_8X8]) &&
                                (xd->n8_w < mi_size_wide[BLOCK_64X64]);

    int step_h = (xd->n8_h >= mi_size_high[BLOCK_64X64])
                     ? mi_size_high[BLOCK_16X16]
                     : mi_size_high[BLOCK_8X8];
    int step_w = (xd->n8_w >= mi_size_wide[BLOCK_64X64])
                     ? mi_size_wide[BLOCK_16X16]
                     : mi_size_wide[BLOCK_8X8];

    for (int blk_row = 0; blk_row < blk_row_end; blk_row += step_h) {
      for (int blk_col = 0; blk_col < blk_col_end; blk_col += step_w) {
        int ret = add_tpl_ref_mv(cm, xd, mi_row, mi_col, ref_frame, blk_row,
                                 blk_col, gm_mv_candidates, refmv_count,
                                 ref_mv_stack, mode_context);
        if (blk_row == 0 && blk_col == 0) is_available = ret;
      }
    }

    if (is_available == 0) mode_context[ref_frame] |= (1 << GLOBALMV_OFFSET);

    for (int i = 0; i < 3 && allow_extension; ++i) {
      const int blk_row = tpl_sample_pos[i][0];
      const int blk_col = tpl_sample_pos[i][1];

      if (!check_sb_border(mi_row, mi_col, blk_row, blk_col)) continue;
      add_tpl_ref_mv(cm, xd, mi_row, mi_col, ref_frame, blk_row, blk_col,
                     gm_mv_candidates, refmv_count, ref_mv_stack, mode_context);
    }
  }

  uint8_t dummy_newmv_count = 0;

  // Scan the second outer area.
  scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, -1, -1, ref_mv_stack[ref_frame],
                &row_match_count, &dummy_newmv_count, gm_mv_candidates,
                &refmv_count[ref_frame]);

  for (int idx = 2; idx <= MVREF_ROW_COLS; ++idx) {
    const int row_offset = -(idx << 1) + 1 + row_adj;
    const int col_offset = -(idx << 1) + 1 + col_adj;

    if (abs(row_offset) <= abs(max_row_offset) &&
        abs(row_offset) > processed_rows)
      scan_row_mbmi(cm, xd, mi_row, mi_col, rf, row_offset,
                    ref_mv_stack[ref_frame], &refmv_count[ref_frame],
                    &row_match_count, &dummy_newmv_count, gm_mv_candidates,
                    max_row_offset, &processed_rows);

    if (abs(col_offset) <= abs(max_col_offset) &&
        abs(col_offset) > processed_cols)
      scan_col_mbmi(cm, xd, mi_row, mi_col, rf, col_offset,
                    ref_mv_stack[ref_frame], &refmv_count[ref_frame],
                    &col_match_count, &dummy_newmv_count, gm_mv_candidates,
                    max_col_offset, &processed_cols);
  }

  uint8_t ref_match_count = (row_match_count > 0) + (col_match_count > 0);

  switch (nearest_match) {
    case 0:
      mode_context[ref_frame] |= 0;
      if (ref_match_count >= 1) mode_context[ref_frame] |= 1;
      if (ref_match_count == 1)
        mode_context[ref_frame] |= (1 << REFMV_OFFSET);
      else if (ref_match_count >= 2)
        mode_context[ref_frame] |= (2 << REFMV_OFFSET);
      break;
    case 1:
      mode_context[ref_frame] |= (newmv_count > 0) ? 2 : 3;
      if (ref_match_count == 1)
        mode_context[ref_frame] |= (3 << REFMV_OFFSET);
      else if (ref_match_count >= 2)
        mode_context[ref_frame] |= (4 << REFMV_OFFSET);
      break;
    case 2:
    default:
      if (newmv_count >= 1)
        mode_context[ref_frame] |= 4;
      else
        mode_context[ref_frame] |= 5;

      mode_context[ref_frame] |= (5 << REFMV_OFFSET);
      break;
  }

  // Rank the likelihood and assign nearest and near mvs.
  int len = nearest_refmv_count;
  while (len > 0) {
    int nr_len = 0;
    for (int idx = 1; idx < len; ++idx) {
      if (ref_mv_stack[ref_frame][idx - 1].weight <
          ref_mv_stack[ref_frame][idx].weight) {
        CANDIDATE_MV tmp_mv = ref_mv_stack[ref_frame][idx - 1];
        ref_mv_stack[ref_frame][idx - 1] = ref_mv_stack[ref_frame][idx];
        ref_mv_stack[ref_frame][idx] = tmp_mv;
        nr_len = idx;
      }
    }
    len = nr_len;
  }

  len = refmv_count[ref_frame];
  while (len > nearest_refmv_count) {
    int nr_len = nearest_refmv_count;
    for (int idx = nearest_refmv_count + 1; idx < len; ++idx) {
      if (ref_mv_stack[ref_frame][idx - 1].weight <
          ref_mv_stack[ref_frame][idx].weight) {
        CANDIDATE_MV tmp_mv = ref_mv_stack[ref_frame][idx - 1];
        ref_mv_stack[ref_frame][idx - 1] = ref_mv_stack[ref_frame][idx];
        ref_mv_stack[ref_frame][idx] = tmp_mv;
        nr_len = idx;
      }
    }
    len = nr_len;
  }

  if (rf[1] > NONE_FRAME) {
    // TODO(jingning, yunqing): Refactor and consolidate the compound and
    // single reference frame modes. Reduce unnecessary redundancy.
    if (refmv_count[ref_frame] < MAX_MV_REF_CANDIDATES) {
      int_mv ref_id[2][2], ref_diff[2][2];
      int ref_id_count[2] = { 0 }, ref_diff_count[2] = { 0 };

      int mi_width = AOMMIN(mi_size_wide[BLOCK_64X64], xd->n8_w);
      mi_width = AOMMIN(mi_width, cm->mi_cols - mi_col);
      int mi_height = AOMMIN(mi_size_high[BLOCK_64X64], xd->n8_h);
      mi_height = AOMMIN(mi_height, cm->mi_rows - mi_row);
      int mi_size = AOMMIN(mi_width, mi_height);

      for (int idx = 0; abs(max_row_offset) >= 1 && idx < mi_size;) {
        const MB_MODE_INFO *const candidate = &xd->mi[-xd->mi_stride + idx];
        const int candidate_bsize = candidate->sb_type;

        for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
          MV_REFERENCE_FRAME can_rf = candidate->ref_frame[rf_idx];

          for (int cmp_idx = 0; cmp_idx < 2; ++cmp_idx) {
            if (can_rf == rf[cmp_idx] && ref_id_count[cmp_idx] < 2) {
              ref_id[cmp_idx][ref_id_count[cmp_idx]] = candidate->mv[rf_idx];
              ++ref_id_count[cmp_idx];
            } else if (can_rf > INTRA_FRAME && ref_diff_count[cmp_idx] < 2) {
              int_mv this_mv = candidate->mv[rf_idx];
              if (cm->ref_frame_sign_bias[can_rf] !=
                  cm->ref_frame_sign_bias[rf[cmp_idx]]) {
                this_mv.as_mv.row = -this_mv.as_mv.row;
                this_mv.as_mv.col = -this_mv.as_mv.col;
              }
              ref_diff[cmp_idx][ref_diff_count[cmp_idx]] = this_mv;
              ++ref_diff_count[cmp_idx];
            }
          }
        }
        idx += mi_size_wide[candidate_bsize];
      }

      for (int idx = 0; abs(max_col_offset) >= 1 && idx < mi_size;) {
        const MB_MODE_INFO *const candidate = &xd->mi[idx * xd->mi_stride - 1];
        const int candidate_bsize = candidate->sb_type;

        for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
          MV_REFERENCE_FRAME can_rf = candidate->ref_frame[rf_idx];

          for (int cmp_idx = 0; cmp_idx < 2; ++cmp_idx) {
            if (can_rf == rf[cmp_idx] && ref_id_count[cmp_idx] < 2) {
              ref_id[cmp_idx][ref_id_count[cmp_idx]] = candidate->mv[rf_idx];
              ++ref_id_count[cmp_idx];
            } else if (can_rf > INTRA_FRAME && ref_diff_count[cmp_idx] < 2) {
              int_mv this_mv = candidate->mv[rf_idx];
              if (cm->ref_frame_sign_bias[can_rf] !=
                  cm->ref_frame_sign_bias[rf[cmp_idx]]) {
                this_mv.as_mv.row = -this_mv.as_mv.row;
                this_mv.as_mv.col = -this_mv.as_mv.col;
              }
              ref_diff[cmp_idx][ref_diff_count[cmp_idx]] = this_mv;
              ++ref_diff_count[cmp_idx];
            }
          }
        }
        idx += mi_size_high[candidate_bsize];
      }

      // Build up the compound mv predictor
      int_mv comp_list[3][2];

      for (int idx = 0; idx < 2; ++idx) {
        int comp_idx = 0;
        for (int list_idx = 0; list_idx < ref_id_count[idx] && comp_idx < 2;
             ++list_idx, ++comp_idx)
          comp_list[comp_idx][idx] = ref_id[idx][list_idx];
        for (int list_idx = 0; list_idx < ref_diff_count[idx] && comp_idx < 2;
             ++list_idx, ++comp_idx)
          comp_list[comp_idx][idx] = ref_diff[idx][list_idx];
        for (; comp_idx < 3; ++comp_idx)
          comp_list[comp_idx][idx] = gm_mv_candidates[idx];
      }

      if (refmv_count[ref_frame]) {
        assert(refmv_count[ref_frame] == 1);
        if (comp_list[0][0].as_int ==
                ref_mv_stack[ref_frame][0].this_mv.as_int &&
            comp_list[0][1].as_int ==
                ref_mv_stack[ref_frame][0].comp_mv.as_int) {
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].this_mv =
              comp_list[1][0];
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].comp_mv =
              comp_list[1][1];
        } else {
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].this_mv =
              comp_list[0][0];
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].comp_mv =
              comp_list[0][1];
        }
        ref_mv_stack[ref_frame][refmv_count[ref_frame]].weight = 2;
        ++refmv_count[ref_frame];
      } else {
        for (int idx = 0; idx < MAX_MV_REF_CANDIDATES; ++idx) {
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].this_mv =
              comp_list[idx][0];
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].comp_mv =
              comp_list[idx][1];
          ref_mv_stack[ref_frame][refmv_count[ref_frame]].weight = 2;
          ++refmv_count[ref_frame];
        }
      }
    }

    assert(refmv_count[ref_frame] >= 2);

    for (int idx = 0; idx < refmv_count[ref_frame]; ++idx) {
      clamp_mv_ref(&ref_mv_stack[ref_frame][idx].this_mv.as_mv,
                   xd->n8_w << MI_SIZE_LOG2, xd->n8_h << MI_SIZE_LOG2, xd);
      clamp_mv_ref(&ref_mv_stack[ref_frame][idx].comp_mv.as_mv,
                   xd->n8_w << MI_SIZE_LOG2, xd->n8_h << MI_SIZE_LOG2, xd);
    }
  } else {
    // Handle single reference frame extension
    int mi_width = AOMMIN(mi_size_wide[BLOCK_64X64], xd->n8_w);
    mi_width = AOMMIN(mi_width, cm->mi_cols - mi_col);
    int mi_height = AOMMIN(mi_size_high[BLOCK_64X64], xd->n8_h);
    mi_height = AOMMIN(mi_height, cm->mi_rows - mi_row);
    int mi_size = AOMMIN(mi_width, mi_height);

    for (int idx = 0; abs(max_row_offset) >= 1 && idx < mi_size &&
                      refmv_count[ref_frame] < MAX_MV_REF_CANDIDATES;) {
      const MB_MODE_INFO *const candidate = &xd->mi[-xd->mi_stride + idx];
      const int candidate_bsize = candidate->sb_type;

      // TODO(jingning): Refactor the following code.
      for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
        if (candidate->ref_frame[rf_idx] > INTRA_FRAME) {
          int_mv this_mv = candidate->mv[rf_idx];
          if (cm->ref_frame_sign_bias[candidate->ref_frame[rf_idx]] !=
              cm->ref_frame_sign_bias[ref_frame]) {
            this_mv.as_mv.row = -this_mv.as_mv.row;
            this_mv.as_mv.col = -this_mv.as_mv.col;
          }
          int stack_idx;
          for (stack_idx = 0; stack_idx < refmv_count[ref_frame]; ++stack_idx) {
            int_mv stack_mv = ref_mv_stack[ref_frame][stack_idx].this_mv;
            if (this_mv.as_int == stack_mv.as_int) break;
          }

          if (stack_idx == refmv_count[ref_frame]) {
            ref_mv_stack[ref_frame][stack_idx].this_mv = this_mv;

            // TODO(jingning): Set an arbitrary small number here. The weight
            // doesn't matter as long as it is properly initialized.
            ref_mv_stack[ref_frame][stack_idx].weight = 2;
            ++refmv_count[ref_frame];
          }
        }
      }
      idx += mi_size_wide[candidate_bsize];
    }

    for (int idx = 0; abs(max_col_offset) >= 1 && idx < mi_size &&
                      refmv_count[ref_frame] < MAX_MV_REF_CANDIDATES;) {
      const MB_MODE_INFO *const candidate = &xd->mi[idx * xd->mi_stride - 1];
      const int candidate_bsize = candidate->sb_type;

      // TODO(jingning): Refactor the following code.
      for (int rf_idx = 0; rf_idx < 2; ++rf_idx) {
        if (candidate->ref_frame[rf_idx] > INTRA_FRAME) {
          int_mv this_mv = candidate->mv[rf_idx];
          if (cm->ref_frame_sign_bias[candidate->ref_frame[rf_idx]] !=
              cm->ref_frame_sign_bias[ref_frame]) {
            this_mv.as_mv.row = -this_mv.as_mv.row;
            this_mv.as_mv.col = -this_mv.as_mv.col;
          }
          int stack_idx;
          for (stack_idx = 0; stack_idx < refmv_count[ref_frame]; ++stack_idx) {
            int_mv stack_mv = ref_mv_stack[ref_frame][stack_idx].this_mv;
            if (this_mv.as_int == stack_mv.as_int) break;
          }

          if (stack_idx == refmv_count[ref_frame]) {
            ref_mv_stack[ref_frame][stack_idx].this_mv = this_mv;

            // TODO(jingning): Set an arbitrary small number here. The weight
            // doesn't matter as long as it is properly initialized.
            ref_mv_stack[ref_frame][stack_idx].weight = 2;
            ++refmv_count[ref_frame];
          }
        }
      }
      idx += mi_size_high[candidate_bsize];
    }

    for (int idx = 0; idx < refmv_count[ref_frame]; ++idx) {
      clamp_mv_ref(&ref_mv_stack[ref_frame][idx].this_mv.as_mv,
                   xd->n8_w << MI_SIZE_LOG2, xd->n8_h << MI_SIZE_LOG2, xd);
    }

    if (mv_ref_list != NULL) {
      for (int idx = refmv_count[ref_frame]; idx < MAX_MV_REF_CANDIDATES; ++idx)
        mv_ref_list[rf[0]][idx].as_int = gm_mv_candidates[0].as_int;

      for (int idx = 0;
           idx < AOMMIN(MAX_MV_REF_CANDIDATES, refmv_count[ref_frame]); ++idx) {
        mv_ref_list[rf[0]][idx].as_int =
            ref_mv_stack[ref_frame][idx].this_mv.as_int;
      }
    }
  }
}

void av1_find_mv_refs(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                      MB_MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                      uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
                      CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
                      int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES],
                      int_mv *global_mvs, int mi_row, int mi_col,
                      int16_t *mode_context) {
  int_mv zeromv[2];
  BLOCK_SIZE bsize = mi->sb_type;
  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame);

  if (ref_frame < REF_FRAMES) {
    if (ref_frame != INTRA_FRAME) {
      global_mvs[ref_frame] = gm_get_motion_vector(
          &cm->global_motion[ref_frame], cm->allow_high_precision_mv, bsize,
          mi_col, mi_row, cm->cur_frame_force_integer_mv);
    } else {
      global_mvs[ref_frame].as_int = INVALID_MV;
    }
  }

  if (ref_frame != INTRA_FRAME) {
    zeromv[0].as_int =
        gm_get_motion_vector(&cm->global_motion[rf[0]],
                             cm->allow_high_precision_mv, bsize, mi_col, mi_row,
                             cm->cur_frame_force_integer_mv)
            .as_int;
    zeromv[1].as_int =
        (rf[1] != NONE_FRAME)
            ? gm_get_motion_vector(&cm->global_motion[rf[1]],
                                   cm->allow_high_precision_mv, bsize, mi_col,
                                   mi_row, cm->cur_frame_force_integer_mv)
                  .as_int
            : 0;
  } else {
    zeromv[0].as_int = zeromv[1].as_int = 0;
  }

  setup_ref_mv_list(cm, xd, ref_frame, ref_mv_count, ref_mv_stack, mv_ref_list,
                    zeromv, mi_row, mi_col, mode_context);
}

void av1_find_best_ref_mvs(int allow_hp, int_mv *mvlist, int_mv *nearest_mv,
                           int_mv *near_mv, int is_integer) {
  int i;
  // Make sure all the candidates are properly clamped etc
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
    lower_mv_precision(&mvlist[i].as_mv, allow_hp, is_integer);
  }
  *nearest_mv = mvlist[0];
  *near_mv = mvlist[1];
}

void av1_setup_frame_buf_refs(AV1_COMMON *cm) {
  cm->cur_frame.cur_frame_offset = cm->frame_offset;

  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    const int buf_idx = cm->frame_refs[ref_frame - LAST_FRAME].idx;
    if (buf_idx >= 0)
      cm->cur_frame.ref_frame_offset[ref_frame - LAST_FRAME] =
          cm->buffer_pool.frame_bufs[buf_idx].cur_frame_offset;
  }
}

#if 0
void av1_setup_frame_sign_bias(AV1_COMMON *cm) {
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    const int buf_idx = cm->frame_refs[ref_frame - LAST_FRAME].idx;
    if (cm->seq_params.enable_order_hint && buf_idx != INVALID_IDX) {
      const int ref_frame_offset =
          cm->buffer_pool->frame_bufs[buf_idx].cur_frame_offset;
      cm->ref_frame_sign_bias[ref_frame] =
          (get_relative_dist(cm, ref_frame_offset, (int)cm->frame_offset) <= 0)
              ? 0
              : 1;
    } else {
      cm->ref_frame_sign_bias[ref_frame] = 0;
    }
  }
}
#endif

#define MAX_OFFSET_WIDTH 64
#define MAX_OFFSET_HEIGHT 0

static int get_block_position(AV1_COMMON *cm, int *mi_r, int *mi_c, int blk_row,
                              int blk_col, MV mv, int sign_bias) {
  const int base_blk_row = (blk_row >> 3) << 3;
  const int base_blk_col = (blk_col >> 3) << 3;

  const int row_offset = (mv.row >= 0) ? (mv.row >> (4 + MI_SIZE_LOG2))
                                       : -((-mv.row) >> (4 + MI_SIZE_LOG2));

  const int col_offset = (mv.col >= 0) ? (mv.col >> (4 + MI_SIZE_LOG2))
                                       : -((-mv.col) >> (4 + MI_SIZE_LOG2));

  int row = (sign_bias == 1) ? blk_row - row_offset : blk_row + row_offset;
  int col = (sign_bias == 1) ? blk_col - col_offset : blk_col + col_offset;

  if (row < 0 || row >= (cm->mi_rows >> 1) || col < 0 ||
      col >= (cm->mi_cols >> 1))
    return 0;

  if (row < base_blk_row - (MAX_OFFSET_HEIGHT >> 3) ||
      row >= base_blk_row + 8 + (MAX_OFFSET_HEIGHT >> 3) ||
      col < base_blk_col - (MAX_OFFSET_WIDTH >> 3) ||
      col >= base_blk_col + 8 + (MAX_OFFSET_WIDTH >> 3))
    return 0;

  *mi_r = row;
  *mi_c = col;

  return 1;
}

static int motion_field_projection(AV1_COMMON *cm, MV_REFERENCE_FRAME ref_frame,
                                   int dir,
                                   const int from_x4, const int to_x4,
                                   const int from_y4, const int to_y4) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  int ref_offset[TOTAL_REFS_PER_FRAME] = { 0 };

  (void)dir;

  int ref_frame_idx = cm->frame_refs[FWD_RF_OFFSET(ref_frame)].idx;
  if (ref_frame_idx < 0) return 0;

  if (cm->buffer_pool.frame_bufs[ref_frame_idx].intra_only) return 0;

  if (cm->buffer_pool.frame_bufs[ref_frame_idx].mi_rows != cm->mi_rows ||
      cm->buffer_pool.frame_bufs[ref_frame_idx].mi_cols != cm->mi_cols)
    return 0;

  int ref_frame_index =
      cm->buffer_pool.frame_bufs[ref_frame_idx].cur_frame_offset;
  unsigned int *ref_rf_idx =
      &cm->buffer_pool.frame_bufs[ref_frame_idx].ref_frame_offset[0];
   int cur_frame_index = cm->cur_frame.cur_frame_offset;
  int ref_to_cur = get_relative_dist(cm, ref_frame_index, cur_frame_index);

  for (MV_REFERENCE_FRAME rf = LAST_FRAME; rf <= INTER_REFS_PER_FRAME; ++rf) {
    ref_offset[rf] =
        get_relative_dist(cm, ref_frame_index, ref_rf_idx[rf - LAST_FRAME]);
  }

  if (dir == 2) ref_to_cur = -ref_to_cur;

  MV_REF *mv_ref_base = cm->buffer_pool.frame_bufs[ref_frame_idx].mvs;
  const ptrdiff_t mv_stride =
    cm->buffer_pool.frame_bufs[ref_frame_idx].mv_stride;
  const int mvs_rows = (cm->mi_rows + 1) >> 1;
  const int mvs_cols = (cm->mi_cols + 1) >> 1;

  assert(from_y4 >= 0);
  const int row_start8 = from_y4 >> 1;
  const int row_end8 = imin(to_y4 >> 1, mvs_rows);
  const int col_start8 = imax((from_x4 - (MAX_OFFSET_WIDTH >> 2)) >> 1, 0);
  const int col_end8 = imin((to_x4 + (MAX_OFFSET_WIDTH >> 2)) >> 1, mvs_cols);
  for (int blk_row = row_start8; blk_row < row_end8; ++blk_row) {
    for (int blk_col = col_start8; blk_col < col_end8; ++blk_col) {
      MV_REF *mv_ref = &mv_ref_base[((blk_row << 1) + 1) * mv_stride +
                                     (blk_col << 1) + 1];
      int diridx;
      const int ref0 = mv_ref->ref_frame[0], ref1 = mv_ref->ref_frame[1];
      if (ref1 > 0 && ref_offset[ref1] > 0 &&
          abs(mv_ref->mv[1].as_mv.row) < (1 << 12) &&
          abs(mv_ref->mv[1].as_mv.col) < (1 << 12))
      {
        diridx = 1;
      } else if (ref0 > 0 && ref_offset[ref0] > 0 &&
                 abs(mv_ref->mv[0].as_mv.row) < (1 << 12) &&
                 abs(mv_ref->mv[0].as_mv.col) < (1 << 12))
      {
        diridx = 0;
      } else {
        continue;
      }
      MV fwd_mv = mv_ref->mv[diridx].as_mv;

      if (mv_ref->ref_frame[diridx] > INTRA_FRAME) {
        int_mv this_mv;
        int mi_r, mi_c;
        const int ref_frame_offset = ref_offset[mv_ref->ref_frame[diridx]];

        int pos_valid = abs(ref_frame_offset) <= MAX_FRAME_DISTANCE &&
                        ref_frame_offset > 0 &&
                        abs(ref_to_cur) <= MAX_FRAME_DISTANCE;

        if (pos_valid) {
          get_mv_projection(&this_mv.as_mv, fwd_mv, ref_to_cur,
                            ref_frame_offset);
          pos_valid = get_block_position(cm, &mi_r, &mi_c, blk_row, blk_col,
                                         this_mv.as_mv, dir >> 1);
        }

        if (pos_valid && mi_c >= (from_x4 >> 1) && mi_c < (to_x4 >> 1)) {
          int mi_offset = mi_r * (cm->mi_stride >> 1) + mi_c;

          tpl_mvs_base[mi_offset].mfmv0.as_mv.row = fwd_mv.row;
          tpl_mvs_base[mi_offset].mfmv0.as_mv.col = fwd_mv.col;
          tpl_mvs_base[mi_offset].ref_frame_offset = ref_frame_offset;
        }
      }
    }
  }

  return 1;
}

#if 0
void av1_setup_motion_field(AV1_COMMON *cm) {
  memset(cm->ref_frame_side, 0, sizeof(cm->ref_frame_side));
  if (!cm->seq_params.enable_order_hint) return;

  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  int size = ((cm->mi_rows + MAX_MIB_SIZE) >> 1) * (cm->mi_stride >> 1);
  for (int idx = 0; idx < size; ++idx) {
    tpl_mvs_base[idx].mfmv0.as_int = INVALID_MV;
    tpl_mvs_base[idx].ref_frame_offset = 0;
  }

  const int cur_order_hint = cm->cur_frame.cur_frame_offset;
  RefCntBuffer *const frame_bufs = cm->buffer_pool->frame_bufs;

  int ref_buf_idx[INTER_REFS_PER_FRAME];
  int ref_order_hint[INTER_REFS_PER_FRAME];

  for (int ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    const int ref_idx = ref_frame - LAST_FRAME;
    const int buf_idx = cm->frame_refs[ref_idx].idx;
    int order_hint = 0;

    if (buf_idx >= 0) order_hint = frame_bufs[buf_idx].cur_frame_offset;

    ref_buf_idx[ref_idx] = buf_idx;
    ref_order_hint[ref_idx] = order_hint;

    if (get_relative_dist(cm, order_hint, cur_order_hint) > 0)
      cm->ref_frame_side[ref_frame] = 1;
    else if (order_hint == cur_order_hint)
      cm->ref_frame_side[ref_frame] = -1;
  }

  int ref_stamp = MFMV_STACK_SIZE - 1;

  if (ref_buf_idx[LAST_FRAME - LAST_FRAME] >= 0) {
    const int alt_of_lst_order_hint =
        frame_bufs[ref_buf_idx[LAST_FRAME - LAST_FRAME]]
            .ref_frame_offset[ALTREF_FRAME - LAST_FRAME];

    const int is_lst_overlay =
        (alt_of_lst_order_hint == ref_order_hint[GOLDEN_FRAME - LAST_FRAME]);
    if (!is_lst_overlay) motion_field_projection(cm, LAST_FRAME, 2);
    --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[BWDREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
    if (motion_field_projection(cm, BWDREF_FRAME, 0)) --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[ALTREF2_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
    if (motion_field_projection(cm, ALTREF2_FRAME, 0)) --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[ALTREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0 &&
      ref_stamp >= 0)
    if (motion_field_projection(cm, ALTREF_FRAME, 0)) --ref_stamp;

  if (ref_stamp >= 0 && ref_buf_idx[LAST2_FRAME - LAST_FRAME] >= 0)
    if (motion_field_projection(cm, LAST2_FRAME, 2)) --ref_stamp;
}
#endif

void av1_setup_motion_field(AV1_COMMON *cm) {
  if (!cm->seq_params.enable_order_hint) return;

  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  int size = (((cm->mi_rows + 31) & ~31) >> 1) * (cm->mi_stride >> 1);
  for (int idx = 0; idx < size; ++idx) {
    tpl_mvs_base[idx].mfmv0.as_int = INVALID_MV;
    tpl_mvs_base[idx].ref_frame_offset = 0;
  }

  memset(cm->ref_frame_side, 0, sizeof(cm->ref_frame_side));
  RefCntBuffer *const frame_bufs = cm->buffer_pool.frame_bufs;

  const int cur_order_hint = cm->cur_frame.cur_frame_offset;
  int *const ref_buf_idx = cm->ref_buf_idx;
  int *const ref_order_hint = cm->ref_order_hint;

  for (int ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    const int ref_idx = ref_frame - LAST_FRAME;
    const int buf_idx = cm->frame_refs[ref_idx].idx;
    int order_hint = 0;

    if (buf_idx >= 0) order_hint = frame_bufs[buf_idx].cur_frame_offset;

    ref_buf_idx[ref_idx] = buf_idx;
    ref_order_hint[ref_idx] = order_hint;

    if (get_relative_dist(cm, order_hint, cur_order_hint) > 0)
      cm->ref_frame_side[ref_frame] = 1;
    else if (order_hint == cur_order_hint)
      cm->ref_frame_side[ref_frame] = -1;
  }
}

void av1_fill_motion_field(AV1_COMMON *cm,
                           const int tile_col_start4, const int tile_col_end4,
                           const int row_start4, int row_end4)
{
  RefCntBuffer *const frame_bufs = cm->buffer_pool.frame_bufs;
  const int cur_order_hint = cm->cur_frame.cur_frame_offset;
  int *const ref_buf_idx = cm->ref_buf_idx;
  int *const ref_order_hint = cm->ref_order_hint;

  int ref_stamp = MFMV_STACK_SIZE - 1;

  if (ref_buf_idx[LAST_FRAME - LAST_FRAME] >= 0) {
    const int alt_of_lst_order_hint =
        frame_bufs[ref_buf_idx[LAST_FRAME - LAST_FRAME]]
            .ref_frame_offset[ALTREF_FRAME - LAST_FRAME];

    const int is_lst_overlay =
        (alt_of_lst_order_hint == ref_order_hint[GOLDEN_FRAME - LAST_FRAME]);
      if (!is_lst_overlay) motion_field_projection(cm, LAST_FRAME, 2,
                                                   tile_col_start4, tile_col_end4,
                                                   row_start4, row_end4);
    --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[BWDREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
      if (motion_field_projection(cm, BWDREF_FRAME, 0,
                                  tile_col_start4, tile_col_end4,
                                  row_start4, row_end4)) --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[ALTREF2_FRAME - LAST_FRAME],
                        cur_order_hint) > 0) {
      if (motion_field_projection(cm, ALTREF2_FRAME, 0,
                                  tile_col_start4, tile_col_end4,
                                  row_start4, row_end4)) --ref_stamp;
  }

  if (get_relative_dist(cm, ref_order_hint[ALTREF_FRAME - LAST_FRAME],
                        cur_order_hint) > 0 &&
      ref_stamp >= 0)
      if (motion_field_projection(cm, ALTREF_FRAME, 0,
                                  tile_col_start4, tile_col_end4,
                                  row_start4, row_end4)) --ref_stamp;

  if (ref_stamp >= 0 && ref_buf_idx[LAST2_FRAME - LAST_FRAME] >= 0)
      if (motion_field_projection(cm, LAST2_FRAME, 2,
                                  tile_col_start4, tile_col_end4,
                                  row_start4, row_end4)) --ref_stamp;
}

#if 0
static INLINE void record_samples(MB_MODE_INFO *mbmi, int *pts, int *pts_inref,
                                  int row_offset, int sign_r, int col_offset,
                                  int sign_c) {
  int bw = block_size_wide[mbmi->sb_type];
  int bh = block_size_high[mbmi->sb_type];
  int x = col_offset * MI_SIZE + sign_c * AOMMAX(bw, MI_SIZE) / 2 - 1;
  int y = row_offset * MI_SIZE + sign_r * AOMMAX(bh, MI_SIZE) / 2 - 1;

  pts[0] = (x * 8);
  pts[1] = (y * 8);
  pts_inref[0] = (x * 8) + mbmi->mv[0].as_mv.col;
  pts_inref[1] = (y * 8) + mbmi->mv[0].as_mv.row;
}

// Select samples according to the motion vector difference.
int selectSamples(MV *mv, int *pts, int *pts_inref, int len, BLOCK_SIZE bsize) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int thresh = clamp(AOMMAX(bw, bh), 16, 112);
  int pts_mvd[SAMPLES_ARRAY_SIZE] = { 0 };
  int i, j, k, l = len;
  int ret = 0;
  assert(len <= LEAST_SQUARES_SAMPLES_MAX);

  // Obtain the motion vector difference.
  for (i = 0; i < len; ++i) {
    pts_mvd[i] = abs(pts_inref[2 * i] - pts[2 * i] - mv->col) +
                 abs(pts_inref[2 * i + 1] - pts[2 * i + 1] - mv->row);

    if (pts_mvd[i] > thresh)
      pts_mvd[i] = -1;
    else
      ret++;
  }

  // Keep at least 1 sample.
  if (!ret) return 1;

  i = 0;
  j = l - 1;
  for (k = 0; k < l - ret; k++) {
    while (pts_mvd[i] != -1) i++;
    while (pts_mvd[j] == -1) j--;
    assert(i != j);
    if (i > j) break;

    // Replace the discarded samples;
    pts_mvd[i] = pts_mvd[j];
    pts[2 * i] = pts[2 * j];
    pts[2 * i + 1] = pts[2 * j + 1];
    pts_inref[2 * i] = pts_inref[2 * j];
    pts_inref[2 * i + 1] = pts_inref[2 * j + 1];
    i++;
    j--;
  }

  return ret;
}

// Note: Samples returned are at 1/8-pel precision
// Sample are the neighbor block center point's coordinates relative to the
// left-top pixel of current block.
int findSamples(const AV1_COMMON *cm, MACROBLOCKD *xd, int mi_row, int mi_col,
                int *pts, int *pts_inref) {
  MB_MODE_INFO *const mbmi0 = xd->mi[0];
  int ref_frame = mbmi0->ref_frame[0];
  int up_available = xd->up_available;
  int left_available = xd->left_available;
  int i, mi_step = 1, np = 0;

  const TileInfo *const tile = &xd->tile;
  int do_tl = 1;
  int do_tr = 1;

  // scan the nearest above rows
  if (up_available) {
    int mi_row_offset = -1;
    MB_MODE_INFO *mbmi = xd->mi[mi_row_offset * xd->mi_stride];
    uint8_t n8_w = mi_size_wide[mbmi->sb_type];

    if (xd->n8_w <= n8_w) {
      // Handle "current block width <= above block width" case.
      int col_offset = -mi_col % n8_w;

      if (col_offset < 0) do_tl = 0;
      if (col_offset + n8_w > xd->n8_w) do_tr = 0;

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, 0, -1, col_offset, 1);
        pts += 2;
        pts_inref += 2;
        np++;
        if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    } else {
      // Handle "current block width > above block width" case.
      for (i = 0; i < AOMMIN(xd->n8_w, cm->mi_cols - mi_col); i += mi_step) {
        int mi_col_offset = i;
        mi = xd->mi[mi_col_offset + mi_row_offset * xd->mi_stride];
        mbmi = &mi->mbmi;
        n8_w = mi_size_wide[mbmi->sb_type];
        mi_step = AOMMIN(xd->n8_w, n8_w);

        if (mbmi->ref_frame[0] == ref_frame &&
            mbmi->ref_frame[1] == NONE_FRAME) {
          record_samples(mbmi, pts, pts_inref, 0, -1, i, 1);
          pts += 2;
          pts_inref += 2;
          np++;
          if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // scan the nearest left columns
  if (left_available) {
    int mi_col_offset = -1;

    MB_MODE_INFO *mi = xd->mi[mi_col_offset];
    uint8_t n8_h = mi_size_high[mbmi->sb_type];

    if (xd->n8_h <= n8_h) {
      // Handle "current block height <= above block height" case.
      int row_offset = -mi_row % n8_h;

      if (row_offset < 0) do_tl = 0;

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, row_offset, 1, 0, -1);
        pts += 2;
        pts_inref += 2;
        np++;
        if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    } else {
      // Handle "current block height > above block height" case.
      for (i = 0; i < AOMMIN(xd->n8_h, cm->mi_rows - mi_row); i += mi_step) {
        int mi_row_offset = i;
        mbmi = xd->mi[mi_col_offset + mi_row_offset * xd->mi_stride];
        n8_h = mi_size_high[mbmi->sb_type];
        mi_step = AOMMIN(xd->n8_h, n8_h);

        if (mbmi->ref_frame[0] == ref_frame &&
            mbmi->ref_frame[1] == NONE_FRAME) {
          record_samples(mbmi, pts, pts_inref, i, 1, 0, -1);
          pts += 2;
          pts_inref += 2;
          np++;
          if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-left block
  if (do_tl && left_available && up_available) {
    int mi_row_offset = -1;
    int mi_col_offset = -1;

    MB_MODE_INFO *mbmi = xd->mi[mi_col_offset + mi_row_offset * xd->mi_stride];

    if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
      record_samples(mbmi, pts, pts_inref, 0, -1, 0, -1);
      pts += 2;
      pts_inref += 2;
      np++;
      if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-right block
  if (do_tr &&
      has_top_right(cm, xd, mi_row, mi_col, AOMMAX(xd->n8_w, xd->n8_h))) {
    POSITION trb_pos = { -1, xd->n8_w };

    if (is_inside(tile, mi_col, mi_row, cm->mi_rows, &trb_pos)) {
      int mi_row_offset = -1;
      int mi_col_offset = xd->n8_w;

      MB_MODE_INFO *mbmi =
          xd->mi[mi_col_offset + mi_row_offset * xd->mi_stride];

      if (mbmi->ref_frame[0] == ref_frame && mbmi->ref_frame[1] == NONE_FRAME) {
        record_samples(mbmi, pts, pts_inref, 0, -1, xd->n8_w, 1);
        np++;
        if (np >= LEAST_SQUARES_SAMPLES_MAX) return LEAST_SQUARES_SAMPLES_MAX;
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  return np;
}

void av1_setup_skip_mode_allowed(AV1_COMMON *cm) {
  cm->is_skip_mode_allowed = 0;
  cm->ref_frame_idx_0 = cm->ref_frame_idx_1 = INVALID_IDX;

  if (!cm->seq_params.enable_order_hint || frame_is_intra_only(cm) ||
      cm->reference_mode == SINGLE_REFERENCE)
    return;

  RefCntBuffer *const frame_bufs = cm->buffer_pool->frame_bufs;
  const int cur_frame_offset = cm->frame_offset;
  int ref_frame_offset[2] = { -1, INT_MAX };
  int ref_idx[2] = { INVALID_IDX, INVALID_IDX };

  // Identify the nearest forward and backward references.
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    const int buf_idx = cm->frame_refs[i].idx;
    if (buf_idx == INVALID_IDX) continue;

    const int ref_offset = frame_bufs[buf_idx].cur_frame_offset;
    if (get_relative_dist(cm, ref_offset, cur_frame_offset) < 0) {
      // Forward reference
      if (ref_frame_offset[0] == -1 ||
          get_relative_dist(cm, ref_offset, ref_frame_offset[0]) > 0) {
        ref_frame_offset[0] = ref_offset;
        ref_idx[0] = i;
      }
    } else if (get_relative_dist(cm, ref_offset, cur_frame_offset) > 0) {
      // Backward reference
      if (ref_frame_offset[1] == INT_MAX ||
          get_relative_dist(cm, ref_offset, ref_frame_offset[1]) < 0) {
        ref_frame_offset[1] = ref_offset;
        ref_idx[1] = i;
      }
    }
  }

  if (ref_idx[0] != INVALID_IDX && ref_idx[1] != INVALID_IDX) {
    // == Bi-directional prediction ==
    cm->is_skip_mode_allowed = 1;
    cm->ref_frame_idx_0 = AOMMIN(ref_idx[0], ref_idx[1]);
    cm->ref_frame_idx_1 = AOMMAX(ref_idx[0], ref_idx[1]);
  } else if (ref_idx[0] != INVALID_IDX && ref_idx[1] == INVALID_IDX) {
    // == Forward prediction only ==
    // Identify the second nearest forward reference.
    ref_frame_offset[1] = -1;
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      const int buf_idx = cm->frame_refs[i].idx;
      if (buf_idx == INVALID_IDX) continue;

      const int ref_offset = frame_bufs[buf_idx].cur_frame_offset;
      if ((ref_frame_offset[0] != -1 &&
           get_relative_dist(cm, ref_offset, ref_frame_offset[0]) < 0) &&
          (ref_frame_offset[1] == -1 ||
           get_relative_dist(cm, ref_offset, ref_frame_offset[1]) > 0)) {
        // Second closest forward reference
        ref_frame_offset[1] = ref_offset;
        ref_idx[1] = i;
      }
    }
    if (ref_frame_offset[1] != -1) {
      cm->is_skip_mode_allowed = 1;
      cm->ref_frame_idx_0 = AOMMIN(ref_idx[0], ref_idx[1]);
      cm->ref_frame_idx_1 = AOMMAX(ref_idx[0], ref_idx[1]);
    }
  }
}

typedef struct {
  int map_idx;   // frame map index
  int buf_idx;   // frame buffer index
  int sort_idx;  // index based on the offset to be used for sorting
} REF_FRAME_INFO;

static int compare_ref_frame_info(const void *arg_a, const void *arg_b) {
  const REF_FRAME_INFO *info_a = (REF_FRAME_INFO *)arg_a;
  const REF_FRAME_INFO *info_b = (REF_FRAME_INFO *)arg_b;

  if (info_a->sort_idx < info_b->sort_idx) return -1;
  if (info_a->sort_idx > info_b->sort_idx) return 1;
  return (info_a->map_idx < info_b->map_idx)
             ? -1
             : ((info_a->map_idx > info_b->map_idx) ? 1 : 0);
}

static void set_ref_frame_info(AV1_COMMON *const cm, int frame_idx,
                               REF_FRAME_INFO *ref_info) {
  assert(frame_idx >= 0 && frame_idx <= INTER_REFS_PER_FRAME);

  const int buf_idx = ref_info->buf_idx;

  cm->frame_refs[frame_idx].idx = buf_idx;
  cm->frame_refs[frame_idx].buf = &cm->buffer_pool->frame_bufs[buf_idx].buf;
  cm->frame_refs[frame_idx].map_idx = ref_info->map_idx;
}

void av1_set_frame_refs(AV1_COMMON *const cm, int lst_map_idx,
                        int gld_map_idx) {
  BufferPool *const pool = cm->buffer_pool;
  RefCntBuffer *const frame_bufs = pool->frame_bufs;

  assert(cm->seq_params.enable_order_hint);
  assert(cm->seq_params.order_hint_bits_minus_1 >= 0);
  const int cur_frame_offset = (int)cm->frame_offset;
  const int cur_frame_sort_idx = 1 << cm->seq_params.order_hint_bits_minus_1;

  REF_FRAME_INFO ref_frame_info[REF_FRAMES];
  int ref_flag_list[INTER_REFS_PER_FRAME] = { 0, 0, 0, 0, 0, 0, 0 };

  for (int i = 0; i < REF_FRAMES; ++i) {
    const int map_idx = i;

    ref_frame_info[i].map_idx = map_idx;
    ref_frame_info[i].sort_idx = -1;

    const int buf_idx = cm->ref_frame_map[map_idx];
    ref_frame_info[i].buf_idx = buf_idx;

    if (buf_idx < 0 || buf_idx >= FRAME_BUFFERS) continue;
    // TODO(zoeliu@google.com): To verify the checking on ref_count.
    if (frame_bufs[buf_idx].ref_count <= 0) continue;

    const int offset = (int)frame_bufs[buf_idx].cur_frame_offset;
    ref_frame_info[i].sort_idx =
        (offset == -1) ? -1
                       : cur_frame_sort_idx +
                             get_relative_dist(cm, offset, cur_frame_offset);
    assert(ref_frame_info[i].sort_idx >= -1);

    if (map_idx == lst_map_idx) lst_frame_sort_idx = ref_frame_info[i].sort_idx;
    if (map_idx == gld_map_idx) gld_frame_sort_idx = ref_frame_info[i].sort_idx;
  }

  // Confirm both LAST_FRAME and GOLDEN_FRAME are valid forward reference
  // frames.
  if (lst_frame_sort_idx == -1 || lst_frame_sort_idx >= cur_frame_sort_idx) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Inter frame requests a look-ahead frame as LAST");
  }
  if (gld_frame_sort_idx == -1 || gld_frame_sort_idx >= cur_frame_sort_idx) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Inter frame requests a look-ahead frame as GOLDEN");
  }

  // Sort ref frames based on their frame_offset values.
  qsort(ref_frame_info, REF_FRAMES, sizeof(REF_FRAME_INFO),
        compare_ref_frame_info);

  // Identify forward and backward reference frames.
  // Forward  reference: offset < cur_frame_offset
  // Backward reference: offset >= cur_frame_offset
  int fwd_start_idx = 0, fwd_end_idx = REF_FRAMES - 1;

  for (int i = 0; i < REF_FRAMES; i++) {
    if (ref_frame_info[i].sort_idx == -1) {
      fwd_start_idx++;
      continue;
    }

    if (ref_frame_info[i].sort_idx >= cur_frame_sort_idx) {
      fwd_end_idx = i - 1;
      break;
    }
  }

  int bwd_start_idx = fwd_end_idx + 1;
  int bwd_end_idx = REF_FRAMES - 1;

  // === Backward Reference Frames ===

  // == ALTREF_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(cm, ALTREF_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_end_idx]);
    ref_flag_list[ALTREF_FRAME - LAST_FRAME] = 1;
    bwd_end_idx--;
  }

  // == BWDREF_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(cm, BWDREF_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_start_idx]);
    ref_flag_list[BWDREF_FRAME - LAST_FRAME] = 1;
    bwd_start_idx++;
  }

  // == ALTREF2_FRAME ==
  if (bwd_start_idx <= bwd_end_idx) {
    set_ref_frame_info(cm, ALTREF2_FRAME - LAST_FRAME,
                       &ref_frame_info[bwd_start_idx]);
    ref_flag_list[ALTREF2_FRAME - LAST_FRAME] = 1;
  }

  // === Forward Reference Frames ===

  for (int i = fwd_start_idx; i <= fwd_end_idx; ++i) {
    // == LAST_FRAME ==
    if (ref_frame_info[i].map_idx == lst_map_idx) {
      set_ref_frame_info(cm, LAST_FRAME - LAST_FRAME, &ref_frame_info[i]);
      ref_flag_list[LAST_FRAME - LAST_FRAME] = 1;
    }

    // == GOLDEN_FRAME ==
    if (ref_frame_info[i].map_idx == gld_map_idx) {
      set_ref_frame_info(cm, GOLDEN_FRAME - LAST_FRAME, &ref_frame_info[i]);
      ref_flag_list[GOLDEN_FRAME - LAST_FRAME] = 1;
    }
  }

  assert(ref_flag_list[LAST_FRAME - LAST_FRAME] == 1 &&
         ref_flag_list[GOLDEN_FRAME - LAST_FRAME] == 1);

  // == LAST2_FRAME ==
  // == LAST3_FRAME ==
  // == BWDREF_FRAME ==
  // == ALTREF2_FRAME ==
  // == ALTREF_FRAME ==

  // Set up the reference frames in the anti-chronological order.
  static const MV_REFERENCE_FRAME ref_frame_list[INTER_REFS_PER_FRAME - 2] = {
    LAST2_FRAME, LAST3_FRAME, BWDREF_FRAME, ALTREF2_FRAME, ALTREF_FRAME
  };

  int ref_idx;
  for (ref_idx = 0; ref_idx < (INTER_REFS_PER_FRAME - 2); ref_idx++) {
    const MV_REFERENCE_FRAME ref_frame = ref_frame_list[ref_idx];

    if (ref_flag_list[ref_frame - LAST_FRAME] == 1) continue;

    while (fwd_start_idx <= fwd_end_idx &&
           (ref_frame_info[fwd_end_idx].map_idx == lst_map_idx ||
            ref_frame_info[fwd_end_idx].map_idx == gld_map_idx)) {
      fwd_end_idx--;
    }
    if (fwd_start_idx > fwd_end_idx) break;

    set_ref_frame_info(cm, ref_frame - LAST_FRAME,
                       &ref_frame_info[fwd_end_idx]);
    ref_flag_list[ref_frame - LAST_FRAME] = 1;

    fwd_end_idx--;
  }

  // Assign all the remaining frame(s), if any, to the earliest reference frame.
  for (; ref_idx < (INTER_REFS_PER_FRAME - 2); ref_idx++) {
    const MV_REFERENCE_FRAME ref_frame = ref_frame_list[ref_idx];
    if (ref_flag_list[ref_frame - LAST_FRAME] == 1) continue;
    set_ref_frame_info(cm, ref_frame - LAST_FRAME,
                       &ref_frame_info[fwd_start_idx]);
    ref_flag_list[ref_frame - LAST_FRAME] = 1;
  }

  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    assert(ref_flag_list[i] == 1);
  }
}
#endif

enum BlockSize {
    BS_128x128,
    BS_128x64,
    BS_64x128,
    BS_64x64,
    BS_64x32,
    BS_64x16,
    BS_32x64,
    BS_32x32,
    BS_32x16,
    BS_32x8,
    BS_16x64,
    BS_16x32,
    BS_16x16,
    BS_16x8,
    BS_16x4,
    BS_8x32,
    BS_8x16,
    BS_8x8,
    BS_8x4,
    BS_4x16,
    BS_4x8,
    BS_4x4,
    N_BS_SIZES,
};
extern const uint8_t av1_block_dimensions[N_BS_SIZES][4];
const uint8_t bs_to_sbtype[N_BS_SIZES] = {
    [BS_128x128] = BLOCK_128X128,
    [BS_128x64] = BLOCK_128X64,
    [BS_64x128] = BLOCK_64X128,
    [BS_64x64] = BLOCK_64X64,
    [BS_64x32] = BLOCK_64X32,
    [BS_64x16] = BLOCK_64X16,
    [BS_32x64] = BLOCK_32X64,
    [BS_32x32] = BLOCK_32X32,
    [BS_32x16] = BLOCK_32X16,
    [BS_32x8] = BLOCK_32X8,
    [BS_16x64] = BLOCK_16X64,
    [BS_16x32] = BLOCK_16X32,
    [BS_16x16] = BLOCK_16X16,
    [BS_16x8] = BLOCK_16X8,
    [BS_16x4] = BLOCK_16X4,
    [BS_8x32] = BLOCK_8X32,
    [BS_8x16] = BLOCK_8X16,
    [BS_8x8] = BLOCK_8X8,
    [BS_8x4] = BLOCK_8X4,
    [BS_4x16] = BLOCK_4X16,
    [BS_4x8] = BLOCK_4X8,
    [BS_4x4] = BLOCK_4X4,
};
const uint8_t sbtype_to_bs[BLOCK_SIZES_ALL] = {
    [BLOCK_128X128] = BS_128x128,
    [BLOCK_128X64] = BS_128x64,
    [BLOCK_64X128] = BS_64x128,
    [BLOCK_64X64] = BS_64x64,
    [BLOCK_64X32] = BS_64x32,
    [BLOCK_64X16] = BS_64x16,
    [BLOCK_32X64] = BS_32x64,
    [BLOCK_32X32] = BS_32x32,
    [BLOCK_32X16] = BS_32x16,
    [BLOCK_32X8] = BS_32x8,
    [BLOCK_16X64] = BS_16x64,
    [BLOCK_16X32] = BS_16x32,
    [BLOCK_16X16] = BS_16x16,
    [BLOCK_16X8] = BS_16x8,
    [BLOCK_16X4] = BS_16x4,
    [BLOCK_8X32] = BS_8x32,
    [BLOCK_8X16] = BS_8x16,
    [BLOCK_8X8] = BS_8x8,
    [BLOCK_8X4] = BS_8x4,
    [BLOCK_4X16] = BS_4x16,
    [BLOCK_4X8] = BS_4x8,
    [BLOCK_4X4] = BS_4x4,
};

static inline struct MV av1_clamp_mv(const struct MV mv,
                                    const int bx4, const int by4,
                                    const int bw4, const int bh4,
                                    const int iw4, const int ih4)
{
    const int left = -(bx4 + bw4 + 4) * 4 * 8;
    const int right = (iw4 - bx4 + 0 * bw4 + 4) * 4 * 8;
    const int top = -(by4 + bh4 + 4) * 4 * 8;
    const int bottom = (ih4 - by4 + 0 * bh4 + 4) * 4 * 8;

    return (struct MV) { .col = iclip(mv.col, left, right),
                         .row = iclip(mv.row, top, bottom) };
}

#include <stdio.h>

void av1_find_ref_mvs(CANDIDATE_MV *mvstack, int *cnt, int_mv (*mvlist)[2],
                      int *ctx, int refidx_dav1d[2],
                      int w4, int h4, int bs, int bp, int by4, int bx4,
                      int tile_col_start4, int tile_col_end4,
                      int tile_row_start4, int tile_row_end4,
                      AV1_COMMON *cm)
{
    const int bw4 = av1_block_dimensions[bs][0];
    const int bh4 = av1_block_dimensions[bs][1];
    int stride = cm->cur_frame.mv_stride;
    MACROBLOCKD xd = (MACROBLOCKD) {
        .n8_w = bw4,
        .n8_h = bh4,
        .mi_stride = stride,
        .up_available = by4 > tile_row_start4,
        .left_available = bx4 > tile_col_start4,
        .tile = {
            .mi_col_end = AOMMIN(w4, tile_col_end4),
            .mi_row_end = AOMMIN(h4, tile_row_end4),
            .tg_horz_boundary = 0,
            .mi_row_start = tile_row_start4,
            .mi_col_start = tile_col_start4,
        },
        .mi = (MB_MODE_INFO *) &cm->cur_frame.mvs[by4 * stride + bx4],
        .mb_to_bottom_edge = (h4 - bh4 - by4) * 32,
        .mb_to_left_edge = -bx4 * 32,
        .mb_to_right_edge = (w4 - bw4 - bx4) * 32,
        .mb_to_top_edge = -by4 * 32,
        .is_sec_rect = 0,
        .cur_mi = {
            .partition = bp,
        },
    };
    xd.mi->sb_type = bs_to_sbtype[bs];
    if (xd.n8_w < xd.n8_h) {
        // Only mark is_sec_rect as 1 for the last block.
        // For PARTITION_VERT_4, it would be (0, 0, 0, 1);
        // For other partitions, it would be (0, 1).
        if (!((bx4 + xd.n8_w) & (xd.n8_h - 1))) xd.is_sec_rect = 1;
    }

    if (xd.n8_w > xd.n8_h)
        if (by4 & (xd.n8_w - 1)) xd.is_sec_rect = 1;

    MV_REFERENCE_FRAME rf[2] = { refidx_dav1d[0] + 1, refidx_dav1d[1] + 1 };
    const int refidx = av1_ref_frame_type(rf);
    int16_t single_context[MODE_CTX_REF_FRAMES];
    uint8_t mv_cnt[MODE_CTX_REF_FRAMES] = { 0 };
    CANDIDATE_MV mv_stack[MODE_CTX_REF_FRAMES][MAX_REF_MV_STACK_SIZE];
    memset(mv_stack, 0, sizeof(mv_stack));
    int_mv mv_list[MODE_CTX_REF_FRAMES][MAX_MV_REF_CANDIDATES] = { { { 0 } } };
    int_mv gmvs[MODE_CTX_REF_FRAMES];
#if 0
    void av1_find_mv_refs(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                          MB_MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                          uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
                          CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
                          int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES],
                          int_mv *global_mvs, int mi_row, int mi_col,
                          int16_t *mode_context)
#endif
    av1_find_mv_refs(cm, &xd, xd.mi, refidx, mv_cnt,
                     mv_stack, mv_list, gmvs, by4, bx4,
                     single_context);
#if !defined(NDEBUG)
    if (refidx_dav1d[1] == -1 && mv_cnt[refidx] >= 1) {
        int_mv tmpa = { .as_int = mv_stack[refidx][0].this_mv.as_int };
        clamp_mv_ref(&tmpa.as_mv, bw4 * 4, bh4 * 4, &xd);
        int_mv tmp1 = { .as_mv =
                 av1_clamp_mv(mv_stack[refidx][0].this_mv.as_mv,
                              bx4, by4, bw4, bh4, w4, h4) };
        assert(tmpa.as_int == tmp1.as_int);
        assert(tmp1.as_int == mv_list[refidx][0].as_int);
        if (mv_cnt[refidx] >= 2) {
            int_mv tmpb = { .as_int = mv_stack[refidx][1].this_mv.as_int };
            clamp_mv_ref(&tmpb.as_mv, bw4 * 4, bh4 * 4, &xd);
            int_mv tmp2 = { .as_mv =
                     av1_clamp_mv(mv_stack[refidx][1].this_mv.as_mv,
                                  bx4, by4, bw4, bh4, w4, h4) };
            assert(tmp2.as_int == tmpb.as_int);
            assert(tmp2.as_int == mv_list[refidx][1].as_int);
        }
    }
#endif
    for (int i = 0; i < mv_cnt[refidx]; i++)
        mvstack[i] = mv_stack[refidx][i];
    *cnt = mv_cnt[refidx];

    mvlist[0][0] = mv_list[refidx_dav1d[0] + 1][0];
    mvlist[0][1] = mv_list[refidx_dav1d[0] + 1][1];
    if (refidx_dav1d[1] != -1) {
        mvlist[1][0] = mv_list[refidx_dav1d[1] + 1][0];
        mvlist[1][1] = mv_list[refidx_dav1d[1] + 1][1];
    }

    if (ctx) {
        if (refidx_dav1d[1] == -1)
            *ctx = single_context[refidx_dav1d[0] + 1];
        else
            *ctx = av1_mode_context_analyzer(single_context, rf);
    }

    if (0 && bx4 == 38 && by4 == 15 && cm->frame_offset == 3 &&
        refidx_dav1d[1] == -1 && refidx_dav1d[0] == 4 &&
        bw4 == 1 && bh4 == 1 && bp == 3)
    {
        MV_REF *l = bx4 ? &cm->cur_frame.mvs[by4*stride+bx4-1] : NULL;
        MV_REF *a = by4 ? &cm->cur_frame.mvs[by4*stride+bx4-stride] : NULL;
        printf("Input: left=[0]y:%d,x:%d,r:%d,[1]y:%d,x:%d,r:%d,mode=%d, "
               "above=[0]y:%d,x:%d,r:%d,[1]y:%d,x:%d,r:%d,mode=%d, "
               "temp=y:%d,x:%d,r:%d [use_ref=%d]\n",
               l ? l->mv[0].as_mv.row : -1,
               l ? l->mv[0].as_mv.col : -1,
               l ? l->ref_frame[0]: -1,
               l ? l->mv[1].as_mv.row : -1,
               l ? l->mv[1].as_mv.col : -1,
               l ? l->ref_frame[1]: -1,
               l ? l->mode : -1,
               a ? a->mv[0].as_mv.row: -1,
               a ? a->mv[0].as_mv.col : -1,
               a ? a->ref_frame[0] : -1,
               a ? a->mv[1].as_mv.row: -1,
               a ? a->mv[1].as_mv.col : -1,
               a ? a->ref_frame[1] : -1,
               a ? a->mode : -1,
               cm->tpl_mvs[(by4 >> 1) * (cm->mi_stride >> 1) + (bx4 >> 1)].mfmv0.as_mv.row,
               cm->tpl_mvs[(by4 >> 1) * (cm->mi_stride >> 1) + (bx4 >> 1)].mfmv0.as_mv.col,
               cm->tpl_mvs[(by4 >> 1) * (cm->mi_stride >> 1) +
                           (bx4 >> 1)].ref_frame_offset,
               cm->allow_ref_frame_mvs);
        printf("Edges: l=%d,t=%d,r=%d,b=%d,w=%d,h=%d,border=%d\n",
               xd.mb_to_left_edge,
               xd.mb_to_top_edge,
               xd.mb_to_right_edge,
               xd.mb_to_bottom_edge,
               xd.n8_w << MI_SIZE_LOG2,
               xd.n8_h << MI_SIZE_LOG2,
               MV_BORDER);
        printf("bp=%d, x=%d, y=%d, refs=%d/%d, n_mvs: %d, "
               "first mv: y=%d,x=%d | y=%d,x=%d, "
               "first comp mv: y=%d,x=%d | y=%d,x=%d, "
               "second mv: y=%d, x=%d | y=%d, x=%d, "
               "second comp mv: y=%d, x=%d | y=%d, x=%d, "
               "third mv: y=%d, x=%d, "
               "ctx=%d\n",
               bp, bx4, by4, refidx_dav1d[0], refidx_dav1d[1], mv_cnt[refidx],
               mv_stack[refidx][0].this_mv.as_mv.row,
               mv_stack[refidx][0].this_mv.as_mv.col,
               mv_list[refidx_dav1d[0] + 1][0].as_mv.row,
               mv_list[refidx_dav1d[0] + 1][0].as_mv.col,
               mv_stack[refidx][0].comp_mv.as_mv.row,
               mv_stack[refidx][0].comp_mv.as_mv.col,
               mv_list[refidx_dav1d[1] + 1][0].as_mv.row,
               mv_list[refidx_dav1d[1] + 1][0].as_mv.col,
               mv_stack[refidx][1].this_mv.as_mv.row,
               mv_stack[refidx][1].this_mv.as_mv.col,
               mv_list[refidx_dav1d[0] + 1][1].as_mv.row,
               mv_list[refidx_dav1d[0] + 1][1].as_mv.col,
               mv_stack[refidx][1].comp_mv.as_mv.row,
               mv_stack[refidx][1].comp_mv.as_mv.col,
               mv_list[refidx_dav1d[1] + 1][1].as_mv.row,
               mv_list[refidx_dav1d[1] + 1][1].as_mv.col,
               mv_stack[refidx][2].this_mv.as_mv.row,
               mv_stack[refidx][2].this_mv.as_mv.col,
               *ctx);
    }
}

void av1_init_ref_mv_common(AV1_COMMON *cm,
                            const int w8, const int h8,
                            const ptrdiff_t stride,
                            const int allow_sb128,
                            MV_REF *cur,
                            MV_REF *ref_mvs[7],
                            const unsigned cur_poc,
                            const unsigned ref_poc[7],
                            const unsigned ref_ref_poc[7][7],
                            const WarpedMotionParams gmv[7],
                            const int allow_hp,
                            const int force_int_mv,
                            const int allow_ref_frame_mvs,
                            const int order_hint)
{
    if (cm->mi_cols != (w8 << 1) || cm->mi_rows != (h8 << 1)) {
        const int align_h = (h8 + 15) & ~15;
        if (cm->tpl_mvs) free(cm->tpl_mvs);
        cm->tpl_mvs = malloc(sizeof(*cm->tpl_mvs) * (stride >> 1) * align_h);
        for (int i = 0; i < 7; i++)
            cm->frame_refs[i].idx = i;
        cm->mi_cols = w8 << 1;
        cm->mi_rows = h8 << 1;
        cm->mi_stride = stride;
        for (int i = 0; i < 7; i++) {
            cm->buffer_pool.frame_bufs[i].mi_rows = cm->mi_rows;
            cm->buffer_pool.frame_bufs[i].mi_cols = cm->mi_cols;
            cm->buffer_pool.frame_bufs[i].mv_stride = stride;
        }
        cm->cur_frame.mv_stride = stride;
    }

    cm->allow_high_precision_mv = allow_hp;
    cm->seq_params.sb_size = allow_sb128 ? BLOCK_128X128 : BLOCK_64X64;

    cm->seq_params.enable_order_hint = !!order_hint;
    cm->seq_params.order_hint_bits_minus1 = order_hint - 1;
    // FIXME get these from the sequence/frame headers instead of hardcoding
    cm->frame_parallel_decode = 0;
    cm->cur_frame_force_integer_mv = force_int_mv;

    memcpy(&cm->global_motion[1], gmv, sizeof(*gmv) * 7);

    cm->frame_offset = cur_poc;
    cm->allow_ref_frame_mvs = allow_ref_frame_mvs;
    cm->cur_frame.mvs = cur;
    for (int i = 0; i < 7; i++) {
        cm->buffer_pool.frame_bufs[i].mvs = ref_mvs[i];
        cm->buffer_pool.frame_bufs[i].intra_only = ref_mvs[i] == NULL;
        cm->buffer_pool.frame_bufs[i].cur_frame_offset = ref_poc[i];
        for (int j = 0; j < 7; j++)
            cm->buffer_pool.frame_bufs[i].ref_frame_offset[j] =
                ref_ref_poc[i][j];
    }
    av1_setup_frame_buf_refs(cm);
    for (int i = 0; i < 7; i++) {
        const int ref_poc = cm->buffer_pool.frame_bufs[i].cur_frame_offset;
        cm->ref_frame_sign_bias[1 + i] = get_relative_dist(cm, ref_poc, cur_poc) > 0;
    }
    av1_setup_motion_field(cm);
}

void av1_init_ref_mv_tile_row(AV1_COMMON *cm,
                              int tile_col_start4, int tile_col_end4,
                              int row_start4, int row_end4)
{
    av1_fill_motion_field(cm, tile_col_start4, tile_col_end4,
                          row_start4, row_end4);
}

AV1_COMMON *av1_alloc_ref_mv_common(void) {
    AV1_COMMON *cm = malloc(sizeof(*cm));
    memset(cm, 0, sizeof(*cm));
    return cm;
}

void av1_free_ref_mv_common(AV1_COMMON *cm) {
    if (cm->tpl_mvs) free(cm->tpl_mvs);
    free(cm);
}
