#include "simd_blitters.h"

#if defined(HAVE_IMMINTRIN_H) && !defined(SDL_DISABLE_IMMINTRIN_H)
#include <immintrin.h>
#endif /* defined(HAVE_IMMINTRIN_H) && !defined(SDL_DISABLE_IMMINTRIN_H) */

#define BAD_AVX2_FUNCTION_CALL                                               \
    printf(                                                                  \
        "Fatal Error: Attempted calling an AVX2 function when both compile " \
        "time and runtime support is missing. If you are seeing this "       \
        "message, you have stumbled across a pygame bug, please report it "  \
        "to the devs!");                                                     \
    PG_EXIT(1)

/* helper function that does a runtime check for AVX2. It has the added
 * functionality of also returning 0 if compile time support is missing */
int
pg_has_avx2()
{
#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
    return SDL_HasAVX2();
#else
    return 0;
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */
}

/* This returns 1 when avx2 is available at runtime but support for it isn't
 * compiled in, 0 in all other cases */
int
pg_avx2_at_runtime_but_uncompiled()
{
    if (SDL_HasAVX2()) {
#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
        return 0;
#else
        return 1;
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */
    }
    return 0;
}

/* just prints the first/lower 128 bits, in two chunks */
// static void
_debug_print256_num(__m256i var, const char *msg)
{
    printf("%s\n", msg);
    Uint64 *z = (Uint64 *)&var;
    printf("l: %llX\n", *z);
    printf("h: %llX\n", *(z + 1));
}

#define __AVX2__ 1

#define SETUP_AVX2_BLITTER                                                    \
    int n;                                                                    \
    int width = info->width;                                                  \
    int height = info->height;                                                \
                                                                              \
    Uint32 *srcp = (Uint32 *)info->s_pixels;                                  \
    int srcskip = info->s_skip >> 2;                                          \
    int srcpxskip = info->s_pxskip >> 2;                                      \
                                                                              \
    Uint32 *dstp = (Uint32 *)info->d_pixels;                                  \
    int dstskip = info->d_skip >> 2;                                          \
    int dstpxskip = info->d_pxskip >> 2;                                      \
                                                                              \
    int pre_8_width = width % 8;                                              \
    int post_8_width = width / 8;                                             \
                                                                              \
    __m256i *srcp256 = (__m256i *)info->s_pixels;                             \
    __m256i *dstp256 = (__m256i *)info->d_pixels;                             \
                                                                              \
    __m256i shuff_out_A =                                                     \
        _mm256_set_epi8(0x80, 23, 0x80, 22, 0x80, 21, 0x80, 20, 0x80, 19,     \
                        0x80, 18, 0x80, 17, 0x80, 16, 0x80, 7, 0x80, 6, 0x80, \
                        5, 0x80, 4, 0x80, 3, 0x80, 2, 0x80, 1, 0x80, 0);      \
                                                                              \
    __m256i shuff_out_B = _mm256_set_epi8(                                    \
        0x80, 31, 0x80, 30, 0x80, 29, 0x80, 28, 0x80, 27, 0x80, 26, 0x80, 25, \
        0x80, 24, 0x80, 15, 0x80, 14, 0x80, 13, 0x80, 12, 0x80, 11, 0x80, 10, \
        0x80, 9, 0x80, 8);                                                    \
                                                                              \
    /* Alpha offset (in 16 bit space). Once a pixel has been extracted */     \
    /* into the AVX registers, it looks like [0][A][0][R][0][G][0][B] */      \
    /* -- except the location of the alpha is not guaranteed to be the */     \
    /* same on different pixelformats, so we need to find the offset */       \
    /* to further extract the alpha */                                        \
    /* src->Ashift is in bits, so Ashift / 8 * 2 (because of 16 bit space) */ \
    /* derives to Ashift >> 2 */                                              \
    int a_off = info->src->Ashift >> 2;                                       \
                                                                              \
    __m256i shuff_out_alpha = _mm256_set_epi8(                                \
        0x80, 8 + a_off, 0x80, 8 + a_off, 0x80, 8 + a_off, 0x80, 8 + a_off,   \
        0x80, 0 + a_off, 0x80, 0 + a_off, 0x80, 0 + a_off, 0x80, 0 + a_off,   \
        0x80, 8 + a_off, 0x80, 8 + a_off, 0x80, 8 + a_off, 0x80, 8 + a_off,   \
        0x80, 0 + a_off, 0x80, 0 + a_off, 0x80, 0 + a_off, 0x80, 0 + a_off);  \
                                                                              \
    __m256i mm256_mask =                                                      \
        _mm256_set_epi32(0x00, (pre_8_width > 6) ? 0x80000000 : 0x00,         \
                         (pre_8_width > 5) ? 0x80000000 : 0x00,               \
                         (pre_8_width > 4) ? 0x80000000 : 0x00,               \
                         (pre_8_width > 3) ? 0x80000000 : 0x00,               \
                         (pre_8_width > 2) ? 0x80000000 : 0x00,               \
                         (pre_8_width > 1) ? 0x80000000 : 0x00,               \
                         (pre_8_width > 0) ? 0x80000000 : 0x00);              \
                                                                              \
    __m256i mm256_src, mm256_dst;

#define RUN_AVX2_BLITTER(BLITTER_CODE)                                  \
    while (height--) {                                                  \
        for (int i = post_8_width; i > 0; i--) {                        \
            /* ==== load 8 pixels into AVX registers ==== */            \
            mm256_src = _mm256_loadu_si256(srcp256);                    \
            mm256_dst = _mm256_loadu_si256(dstp256);                    \
                                                                        \
            {BLITTER_CODE}                                              \
                                                                        \
            /* ==== store 8 pixels from AVX registers ==== */           \
            _mm256_storeu_si256(dstp256, mm256_dst);                    \
                                                                        \
            srcp256++;                                                  \
            dstp256++;                                                  \
        }                                                               \
        srcp = (Uint32 *)srcp256;                                       \
        dstp = (Uint32 *)dstp256;                                       \
        if (pre_8_width > 0) {                                          \
            mm256_src = _mm256_maskload_epi32((int *)srcp, mm256_mask); \
            mm256_dst = _mm256_maskload_epi32((int *)dstp, mm256_mask); \
                                                                        \
            {BLITTER_CODE}                                              \
                                                                        \
            /* ==== store 1-7 pixels from AVX registers ==== */         \
            _mm256_maskstore_epi32((int *)dstp, mm256_mask, mm256_dst); \
                                                                        \
            srcp += srcpxskip * pre_8_width;                            \
            dstp += dstpxskip * pre_8_width;                            \
        }                                                               \
                                                                        \
        srcp256 = (__m256i *)(srcp + srcskip);                          \
        dstp256 = (__m256i *)(dstp + dstskip);                          \
    }

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
alphablit_alpha_avx2_argb_no_surf_alpha_opaque_dst(SDL_BlitInfo *info)
{
    SETUP_AVX2_BLITTER

    __m256i mm256_srcA, mm256_srcB, mm256_dstA, mm256_dstB, mm256_srcAlpha,
        temp;

    /* Original 'Straight Alpha' blending equation:
        --------------------------------------------
        dstRGB = (srcRGB * srcA) + (dstRGB * (1-srcA))
            dstA = srcA + (dstA * (1-srcA))

        We use something slightly different to simulate
        SDL1, as follows:
        dstRGB = (((dstRGB << 8) + (srcRGB - dstRGB) * srcA + srcRGB) >> 8)
            dstA = srcA + dstA - ((srcA * dstA) / 255);
    */

    /* Alpha blend procedure in this blitter:
        --------------------------------------
        srcRGBA = 16 bit interspersed source pixels
            i.e. [0][A][0][R][0][G][0][B]
        srcA = 16 bit interspersed alpha channel
            i.e. [0][A][0][A][0][A][0][A]
        As you can see, each pixel is moved out into 64 bits of register
        for processing. This blitter loads in 8 pixels at once, but
        processes 4 at a time (4x64=256 bits).

        * In this blitter, the interspersed RGBA layout is not determined,
        because the only thing that matters is alpha location, and a mask
        is provided to select alpha out of the registers. This way the blitter
        can easily support different pixel arrangements
        (RGBA, BGRA, ARGB, ABGR)

        Order of operations:
            temp = srcRGBA - dstRGBA
            temp *= srcA
            dstRGBA <<= 8
            dstRGBA += temp
            dstRGBA += srcRGBA
            dstRGBA >>= 8
        */

    RUN_AVX2_BLITTER(
        /* ==== shuffle pixels out into two registers each, src
         * and dst set up for 16 bit math, like 0A0R0G0B ==== */
        mm256_srcA = _mm256_shuffle_epi8(mm256_src, shuff_out_A);
        mm256_srcB = _mm256_shuffle_epi8(mm256_src, shuff_out_B);
        mm256_dstA = _mm256_shuffle_epi8(mm256_dst, shuff_out_A);
        mm256_dstB = _mm256_shuffle_epi8(mm256_dst, shuff_out_B);

        /* ==== alpha blend opaque dst on A pixels ==== */
        mm256_srcAlpha = _mm256_shuffle_epi8(mm256_srcA, shuff_out_alpha);
        temp = _mm256_sub_epi16(mm256_srcA, mm256_dstA);
        temp = _mm256_mullo_epi16(temp, mm256_srcAlpha);
        mm256_dstA = _mm256_slli_epi16(mm256_dstA, 8);
        mm256_dstA = _mm256_add_epi16(mm256_dstA, temp);
        mm256_dstA = _mm256_add_epi16(mm256_dstA, mm256_srcA);
        mm256_dstA = _mm256_srli_epi16(mm256_dstA, 8);

        /* ==== alpha blend opaque dst on B pixels ==== */
        mm256_srcAlpha = _mm256_shuffle_epi8(mm256_srcB, shuff_out_alpha);
        temp = _mm256_sub_epi16(mm256_srcB, mm256_dstB);
        temp = _mm256_mullo_epi16(temp, mm256_srcAlpha);
        mm256_dstB = _mm256_slli_epi16(mm256_dstB, 8);
        mm256_dstB = _mm256_add_epi16(mm256_dstB, temp);
        mm256_dstB = _mm256_add_epi16(mm256_dstB, mm256_srcB);
        mm256_dstB = _mm256_srli_epi16(mm256_dstB, 8);

        /* ==== recombine A and B pixels ==== */
        mm256_dst = _mm256_packus_epi16(mm256_dstA, mm256_dstB);)
}
#else
void
alphablit_alpha_avx2_argb_no_surf_alpha_opaque_dst(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
alphablit_alpha_avx2_argb_no_surf_alpha(SDL_BlitInfo *info)
{
    SETUP_AVX2_BLITTER

    // Used to choose which byte to use when pulling from the RGBX buffer and
    // the dst alpha buffer. Chooses the RGBX buffer most of the time, chooses
    // A when on the low byte of the X 16 bit region.
    // RGB buffer looks like: [0][R][0][G][0][B][0][X] 
    // Dst alpha looks like:  [0][A][0][A][0][A][0][A]
    // We want:               [0][R][0][G][0][B][0][A]
    // *ofc real memory layout differs based on pixelformat.
    __m256i combine_rgba_mask = _mm256_set1_epi64x(1LL << (((info->src->Ashift)*2) + 7));

    __m256i mm256_srcA, mm256_srcB, mm256_dstA, mm256_dstB, mm256_srcAlpha,
        temp, mm256_dstAlpha, mm256_newDstAlpha;

    /*
     dstRGB = (((dstRGB << 8) + (srcRGB - dstRGB) * srcA + srcRGB) >> 8)
     dstA = srcA + dstA - ((srcA * dstA) / 255);
     */

    RUN_AVX2_BLITTER(
        /* ==== shuffle pixels out into two registers each, src
         * and dst set up for 16 bit math, like 0A0R0G0B ==== */
        mm256_srcA = _mm256_shuffle_epi8(mm256_src, shuff_out_A);
        mm256_srcB = _mm256_shuffle_epi8(mm256_src, shuff_out_B);
        mm256_dstA = _mm256_shuffle_epi8(mm256_dst, shuff_out_A);
        mm256_dstB = _mm256_shuffle_epi8(mm256_dst, shuff_out_B);

        /* ==== alpha blend on A pixels ==== */
        mm256_srcAlpha = _mm256_shuffle_epi8(mm256_srcA, shuff_out_alpha);

        // figure out alpha
        mm256_dstAlpha = _mm256_shuffle_epi8(mm256_dstA, shuff_out_alpha);
        temp = _mm256_mullo_epi16(mm256_srcAlpha, mm256_dstAlpha);
        temp = _mm256_srli_epi16(
            _mm256_mulhi_epu16(temp, _mm256_set1_epi16((short)0x8081)), 7);
        mm256_newDstAlpha = _mm256_sub_epi16(mm256_dstAlpha, temp);
        mm256_newDstAlpha =
            _mm256_add_epi16(mm256_srcAlpha, mm256_newDstAlpha);

        // if preexisting dst alpha is 0, src alpha should be set to 255
        // enforces that dest alpha 0 means "copy source RGB"
        // happens after real src alpha values used to calculate dst alpha
        // compares each 16 bit block to zeroes, yielding 0xFFFF or 0x0000--
        // shifts out bottom 8 bits to get to 0x00FF or 0x0000.
        mm256_dstAlpha =
            _mm256_cmpeq_epi16(mm256_dstAlpha, _mm256_setzero_si256());
        mm256_dstAlpha = _mm256_srli_epi16(mm256_dstAlpha, 8);
        mm256_srcAlpha = _mm256_max_epu8(mm256_dstAlpha, mm256_srcAlpha);

        // figure out RGB
        temp = _mm256_sub_epi16(mm256_srcA, mm256_dstA);
        temp = _mm256_mullo_epi16(temp, mm256_srcAlpha);
        temp = _mm256_add_epi16(temp, mm256_srcA);
        mm256_dstA = _mm256_slli_epi16(mm256_dstA, 8);
        mm256_dstA = _mm256_add_epi16(mm256_dstA, temp);
        mm256_dstA = _mm256_srli_epi16(mm256_dstA, 8);

        // blend together dstRGB and dstA
        mm256_dstA = _mm256_blendv_epi8(mm256_dstA, mm256_newDstAlpha, combine_rgba_mask);

        /* ==== alpha blend on B pixels ==== */
        mm256_srcAlpha = _mm256_shuffle_epi8(mm256_srcB, shuff_out_alpha);

        // figure out alpha
        mm256_dstAlpha = _mm256_shuffle_epi8(mm256_dstB, shuff_out_alpha);
        temp = _mm256_mullo_epi16(mm256_srcAlpha, mm256_dstAlpha);
        temp = _mm256_srli_epi16(
            _mm256_mulhi_epu16(temp, _mm256_set1_epi16((short)0x8081)), 7);
        mm256_newDstAlpha = _mm256_sub_epi16(mm256_dstAlpha, temp);
        mm256_newDstAlpha =
            _mm256_add_epi16(mm256_srcAlpha, mm256_newDstAlpha);

        // if preexisting dst alpha is 0, src alpha should be set to 255
        // enforces that dest alpha 0 means "copy source RGB"
        // happens after real src alpha values used to calculate dst alpha
        // compares each 16 bit block to zeroes, yielding 0xFFFF or 0x0000--
        // shifts out bottom 8 bits to get to 0x00FF or 0x0000.
        mm256_dstAlpha =
            _mm256_cmpeq_epi16(mm256_dstAlpha, _mm256_setzero_si256());
        mm256_dstAlpha = _mm256_srli_epi16(mm256_dstAlpha, 8);
        mm256_srcAlpha = _mm256_max_epu8(mm256_dstAlpha, mm256_srcAlpha);

        // figure out RGB
        temp = _mm256_sub_epi16(mm256_srcB, mm256_dstB);
        temp = _mm256_mullo_epi16(temp, mm256_srcAlpha);
        temp = _mm256_add_epi16(temp, mm256_srcB);
        mm256_dstB = _mm256_slli_epi16(mm256_dstB, 8);
        mm256_dstB = _mm256_add_epi16(mm256_dstB, temp);
        mm256_dstB = _mm256_srli_epi16(mm256_dstB, 8);

        // blend together dstRGB and dstA
        mm256_dstB = _mm256_blendv_epi8(mm256_dstB, mm256_newDstAlpha, combine_rgba_mask);

        /* ==== recombine A and B pixels ==== */
        mm256_dst = _mm256_packus_epi16(mm256_dstA, mm256_dstB);)
}
#else
void
alphablit_alpha_avx2_argb_no_surf_alpha(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgba_mul_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_zero, mm_two_five_fives;
    __m256i mm256_src, mm256_srcA, mm256_srcB, mm256_dst, mm256_dstA,
        mm256_dstB, mm256_shuff_mask_A, mm256_shuff_mask_B,
        mm256_two_five_fives;

    mm256_shuff_mask_A =
        _mm256_set_epi8(0x80, 23, 0x80, 22, 0x80, 21, 0x80, 20, 0x80, 19, 0x80,
                        18, 0x80, 17, 0x80, 16, 0x80, 7, 0x80, 6, 0x80, 5,
                        0x80, 4, 0x80, 3, 0x80, 2, 0x80, 1, 0x80, 0);
    mm256_shuff_mask_B =
        _mm256_set_epi8(0x80, 31, 0x80, 30, 0x80, 29, 0x80, 28, 0x80, 27, 0x80,
                        26, 0x80, 25, 0x80, 24, 0x80, 15, 0x80, 14, 0x80, 13,
                        0x80, 12, 0x80, 11, 0x80, 10, 0x80, 9, 0x80, 8);

    mm_zero = _mm_setzero_si128();
    mm_two_five_fives = _mm_set_epi64x(0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF);

    mm256_two_five_fives = _mm256_set_epi8(
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    /*mm_src = 0x000000000000000000000000AARRGGBB*/
                    mm_src = _mm_unpacklo_epi8(mm_src, mm_zero);
                    /*mm_src = 0x000000000000000000AA00RR00GG00BB*/
                    mm_dst = _mm_cvtsi32_si128(*dstp);
                    /*mm_dst = 0x000000000000000000000000AARRGGBB*/
                    mm_dst = _mm_unpacklo_epi8(mm_dst, mm_zero);
                    /*mm_dst = 0x000000000000000000AA00RR00GG00BB*/

                    mm_dst = _mm_mullo_epi16(mm_src, mm_dst);
                    /*mm_dst = 0x0000000000000000AAAARRRRGGGGBBBB*/
                    mm_dst = _mm_add_epi16(mm_dst, mm_two_five_fives);
                    /*mm_dst = 0x0000000000000000AAAARRRRGGGGBBBB*/
                    mm_dst = _mm_srli_epi16(mm_dst, 8);
                    /*mm_dst = 0x000000000000000000AA00RR00GG00BB*/
                    mm_dst = _mm_packus_epi16(mm_dst, mm_dst);
                    /*mm_dst = 0x00000000AARRGGBB00000000AARRGGBB*/
                    *dstp = _mm_cvtsi128_si32(mm_dst);
                    /*dstp = 0xAARRGGBB*/
                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_srcA =
                        _mm256_shuffle_epi8(mm256_src, mm256_shuff_mask_A);
                    mm256_srcB =
                        _mm256_shuffle_epi8(mm256_src, mm256_shuff_mask_B);

                    mm256_dstA =
                        _mm256_shuffle_epi8(mm256_dst, mm256_shuff_mask_A);
                    mm256_dstB =
                        _mm256_shuffle_epi8(mm256_dst, mm256_shuff_mask_B);

                    mm256_dstA = _mm256_mullo_epi16(mm256_srcA, mm256_dstA);
                    mm256_dstA =
                        _mm256_add_epi16(mm256_dstA, mm256_two_five_fives);
                    mm256_dstA = _mm256_srli_epi16(mm256_dstA, 8);

                    mm256_dstB = _mm256_mullo_epi16(mm256_srcB, mm256_dstB);
                    mm256_dstB =
                        _mm256_add_epi16(mm256_dstB, mm256_two_five_fives);
                    mm256_dstB = _mm256_srli_epi16(mm256_dstB, 8);

                    mm256_dst = _mm256_packus_epi16(mm256_dstA, mm256_dstB);
                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgba_mul_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgb_mul_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    /* if either surface has a non-zero alpha mask use that as our mask */
    Uint32 amask = info->src->Amask | info->dst->Amask;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_zero, mm_two_five_fives, mm_alpha_mask;
    __m256i mm256_src, mm256_srcA, mm256_srcB, mm256_dst, mm256_dstA,
        mm256_dstB, mm256_shuff_mask_A, mm256_shuff_mask_B,
        mm256_two_five_fives, mm256_alpha_mask;

    mm256_shuff_mask_A =
        _mm256_set_epi8(0x80, 23, 0x80, 22, 0x80, 21, 0x80, 20, 0x80, 19, 0x80,
                        18, 0x80, 17, 0x80, 16, 0x80, 7, 0x80, 6, 0x80, 5,
                        0x80, 4, 0x80, 3, 0x80, 2, 0x80, 1, 0x80, 0);
    mm256_shuff_mask_B =
        _mm256_set_epi8(0x80, 31, 0x80, 30, 0x80, 29, 0x80, 28, 0x80, 27, 0x80,
                        26, 0x80, 25, 0x80, 24, 0x80, 15, 0x80, 14, 0x80, 13,
                        0x80, 12, 0x80, 11, 0x80, 10, 0x80, 9, 0x80, 8);

    mm_zero = _mm_setzero_si128();
    mm_two_five_fives = _mm_set_epi64x(0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF);
    mm_alpha_mask = _mm_cvtsi32_si128(amask);

    mm256_two_five_fives = _mm256_set_epi8(
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF);

    mm256_alpha_mask = _mm256_set_epi32(amask, amask, amask, amask, amask,
                                        amask, amask, amask);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    /*mm_src = 0x000000000000000000000000AARRGGBB*/
                    mm_src = _mm_or_si128(mm_src, mm_alpha_mask);
                    /* ensure source alpha is 255 */
                    mm_src = _mm_unpacklo_epi8(mm_src, mm_zero);
                    /*mm_src = 0x000000000000000000AA00RR00GG00BB*/
                    mm_dst = _mm_cvtsi32_si128(*dstp);
                    /*mm_dst = 0x000000000000000000000000AARRGGBB*/
                    mm_dst = _mm_unpacklo_epi8(mm_dst, mm_zero);
                    /*mm_dst = 0x000000000000000000AA00RR00GG00BB*/

                    mm_dst = _mm_mullo_epi16(mm_src, mm_dst);
                    /*mm_dst = 0x0000000000000000AAAARRRRGGGGBBBB*/
                    mm_dst = _mm_add_epi16(mm_dst, mm_two_five_fives);
                    /*mm_dst = 0x0000000000000000AAAARRRRGGGGBBBB*/
                    mm_dst = _mm_srli_epi16(mm_dst, 8);
                    /*mm_dst = 0x000000000000000000AA00RR00GG00BB*/
                    mm_dst = _mm_packus_epi16(mm_dst, mm_dst);
                    /*mm_dst = 0x00000000AARRGGBB00000000AARRGGBB*/
                    *dstp = _mm_cvtsi128_si32(mm_dst);
                    /*dstp = 0xAARRGGBB*/
                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_src = _mm256_or_si256(mm256_src, mm256_alpha_mask);
                    /* ensure source alpha is 255 */
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_srcA =
                        _mm256_shuffle_epi8(mm256_src, mm256_shuff_mask_A);
                    mm256_srcB =
                        _mm256_shuffle_epi8(mm256_src, mm256_shuff_mask_B);

                    mm256_dstA =
                        _mm256_shuffle_epi8(mm256_dst, mm256_shuff_mask_A);
                    mm256_dstB =
                        _mm256_shuffle_epi8(mm256_dst, mm256_shuff_mask_B);

                    mm256_dstA = _mm256_mullo_epi16(mm256_srcA, mm256_dstA);
                    mm256_dstA =
                        _mm256_add_epi16(mm256_dstA, mm256_two_five_fives);
                    mm256_dstA = _mm256_srli_epi16(mm256_dstA, 8);

                    mm256_dstB = _mm256_mullo_epi16(mm256_srcB, mm256_dstB);
                    mm256_dstB =
                        _mm256_add_epi16(mm256_dstB, mm256_two_five_fives);
                    mm256_dstB = _mm256_srli_epi16(mm256_dstB, 8);

                    mm256_dst = _mm256_packus_epi16(mm256_dstA, mm256_dstB);
                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgb_mul_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgba_add_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst;
    __m256i mm256_src, mm256_dst;

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_dst = _mm_adds_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_dst = _mm256_adds_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgba_add_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgb_add_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    /* if either surface has a non-zero alpha mask use that as our mask */
    Uint32 amask = info->src->Amask | info->dst->Amask;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_alpha_mask;
    __m256i mm256_src, mm256_dst, mm256_alpha_mask;

    mm_alpha_mask = _mm_cvtsi32_si128(amask);
    mm256_alpha_mask = _mm256_set_epi32(amask, amask, amask, amask, amask,
                                        amask, amask, amask);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_src = _mm_subs_epu8(mm_src, mm_alpha_mask);
                    mm_dst = _mm_adds_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_src = _mm256_subs_epu8(mm256_src, mm256_alpha_mask);
                    mm256_dst = _mm256_adds_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgb_add_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgba_sub_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst;
    __m256i mm256_src, mm256_dst;

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_dst = _mm_subs_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_dst = _mm256_subs_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgba_sub_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgb_sub_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    /* if either surface has a non-zero alpha mask use that as our mask */
    Uint32 amask = info->src->Amask | info->dst->Amask;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_alpha_mask;
    __m256i mm256_src, mm256_dst, mm256_alpha_mask;

    mm_alpha_mask = _mm_cvtsi32_si128(amask);
    mm256_alpha_mask = _mm256_set_epi32(amask, amask, amask, amask, amask,
                                        amask, amask, amask);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_src = _mm_subs_epu8(mm_src, mm_alpha_mask);
                    mm_dst = _mm_subs_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_src = _mm256_subs_epu8(mm256_src, mm256_alpha_mask);
                    mm256_dst = _mm256_subs_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgb_sub_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgba_max_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst;
    __m256i mm256_src, mm256_dst;

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_dst = _mm_max_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_dst = _mm256_max_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgba_max_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgb_max_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    /* if either surface has a non-zero alpha mask use that as our mask */
    Uint32 amask = info->src->Amask | info->dst->Amask;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_alpha_mask;
    __m256i mm256_src, mm256_dst, mm256_alpha_mask;

    mm_alpha_mask = _mm_cvtsi32_si128(amask);
    mm256_alpha_mask = _mm256_set_epi32(amask, amask, amask, amask, amask,
                                        amask, amask, amask);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_src = _mm_subs_epu8(mm_src, mm_alpha_mask);
                    mm_dst = _mm_max_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_src = _mm256_subs_epu8(mm256_src, mm256_alpha_mask);
                    mm256_dst = _mm256_max_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgb_max_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgba_min_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst;
    __m256i mm256_src, mm256_dst;

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_dst = _mm_min_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_dst = _mm256_min_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgba_min_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */

#if defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
    !defined(SDL_DISABLE_IMMINTRIN_H)
void
blit_blend_rgb_min_avx2(SDL_BlitInfo *info)
{
    int n;
    int width = info->width;
    int height = info->height;

    Uint32 *srcp = (Uint32 *)info->s_pixels;
    int srcskip = info->s_skip >> 2;
    int srcpxskip = info->s_pxskip >> 2;

    Uint32 *dstp = (Uint32 *)info->d_pixels;
    int dstskip = info->d_skip >> 2;
    int dstpxskip = info->d_pxskip >> 2;

    int pre_8_width = width % 8;
    int post_8_width = (width - pre_8_width) / 8;

    /* if either surface has a non-zero alpha mask use that as our mask */
    Uint32 amask = info->src->Amask | info->dst->Amask;

    __m256i *srcp256 = (__m256i *)info->s_pixels;
    __m256i *dstp256 = (__m256i *)info->d_pixels;

    __m128i mm_src, mm_dst, mm_alpha_mask;
    __m256i mm256_src, mm256_dst, mm256_alpha_mask;

    mm_alpha_mask = _mm_cvtsi32_si128(amask);
    mm256_alpha_mask = _mm256_set_epi32(amask, amask, amask, amask, amask,
                                        amask, amask, amask);

    while (height--) {
        if (pre_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm_src = _mm_cvtsi32_si128(*srcp);
                    mm_dst = _mm_cvtsi32_si128(*dstp);

                    mm_src = _mm_adds_epu8(mm_src, mm_alpha_mask);
                    mm_dst = _mm_min_epu8(mm_dst, mm_src);

                    *dstp = _mm_cvtsi128_si32(mm_dst);

                    srcp += srcpxskip;
                    dstp += dstpxskip;
                },
                n, pre_8_width);
        }
        srcp256 = (__m256i *)srcp;
        dstp256 = (__m256i *)dstp;
        if (post_8_width > 0) {
            LOOP_UNROLLED4(
                {
                    mm256_src = _mm256_loadu_si256(srcp256);
                    mm256_dst = _mm256_loadu_si256(dstp256);

                    mm256_src = _mm256_adds_epu8(mm256_src, mm256_alpha_mask);
                    mm256_dst = _mm256_min_epu8(mm256_dst, mm256_src);

                    _mm256_storeu_si256(dstp256, mm256_dst);

                    srcp256++;
                    dstp256++;
                },
                n, post_8_width);
        }
        srcp = (Uint32 *)srcp256 + srcskip;
        dstp = (Uint32 *)dstp256 + dstskip;
    }
}
#else
void
blit_blend_rgb_min_avx2(SDL_BlitInfo *info)
{
    BAD_AVX2_FUNCTION_CALL;
}
#endif /* defined(__AVX2__) && defined(HAVE_IMMINTRIN_H) && \
          !defined(SDL_DISABLE_IMMINTRIN_H) */
