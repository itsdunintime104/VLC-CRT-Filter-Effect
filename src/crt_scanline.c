/*****************************************************************************
 * crt_scanline.c : CRT Scanline video filter for VLC
 *****************************************************************************
 * Original scanline filter: Copyright (C) 2026 Jules Lazaro
 * Phosphor glow, NTSC color bleed, black scanline mode: community contributors
 * Barrel distortion & vignette inspired by sprash3's Shadertoy shader XdtfzX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include <string.h>
#include <stdlib.h>

/* stb_image: public domain PNG/JPG/BMP decoder (Sean Barrett) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include <vlc_configuration.h>
#include "filter_picture.h"

static int  Create  ( vlc_object_t * );
static void Destroy ( vlc_object_t * );
static picture_t *Filter( filter_t *, picture_t * );

#define FILTER_PREFIX "crtemulator-"

#define DARKNESS_TEXT N_("Scanline darkness")
#define DARKNESS_LONGTEXT N_( \
    "How dark the scanlines are at 1080p. " \
    "Scales down for lower resolutions. 0 = off. Default: 35" )

#define SPACING_TEXT N_("Line spacing (pixels at 480p)")
#define SPACING_LONGTEXT N_( \
    "Base scanline spacing at 480p. Scales with resolution. Default: 2" )

#define BLEND_TEXT N_("Smooth blending")
#define BLEND_LONGTEXT N_( \
    "Smooth sine-wave vs hard lines. Default: on" )

#define PHOSPHOR_TEXT N_("Phosphor glow intensity")
#define PHOSPHOR_LONGTEXT N_( \
    "Simulates CRT phosphor brightness. Higher values brighten lit pixels " \
    "and add glow between scanlines. 0 = off. Default: 0" )

#define NTSC_TEXT N_("NTSC color bleed")
#define NTSC_LONGTEXT N_( \
    "Simulates analog NTSC chroma smearing. " \
    "0=off, 1=light (S-Video), 2=medium (Composite), 3=heavy (RF). Default: 0" )

#define BLACKLINE_TEXT N_("Black scanline mode")
#define BLACKLINE_LONGTEXT N_( \
    "Full black scanlines instead of darkened. " \
    "Overrides darkness for scanline rows. Default: off" )

#define BARREL_TEXT N_("Barrel distortion (CRT curvature)")
#define BARREL_LONGTEXT N_( \
    "Simulates the curved glass of a CRT screen. " \
    "0 = flat, 100 = maximum curvature. Default: 0" )

#define VIGNETTE_TEXT N_("Vignette (edge darkening)")
#define VIGNETTE_LONGTEXT N_( \
    "Darkens the edges and corners like a CRT bezel shadow. " \
    "0 = off, 100 = heavy vignette. Default: 0" )

#define NOISE_TEXT N_("RF noise")
#define NOISE_LONGTEXT N_( \
    "Adds analog static/grain to the image. " \
    "Independent of signal type. 0 = off, 100 = heavy static. Default: 0" )

#define BEZEL_TEXT N_("CRT bezel frame")
#define BEZEL_LONGTEXT N_( \
    "Draws a rounded TV frame border around the video. " \
    "0 = off, higher = thicker frame. Default: 0" )

#define REFLECT_TEXT N_("Glass reflection")
#define REFLECT_LONGTEXT N_( \
    "Adds a diagonal light reflection across the CRT glass. " \
    "0 = off, 100 = bright shine. Default: 0" )

#define OVERLAY_TEXT N_("TV overlay image path")
#define OVERLAY_LONGTEXT N_( \
    "Path to a PNG image of a TV/monitor frame. " \
    "The dark screen area becomes transparent, showing the video through it. " \
    "Use overlays from Soqueroeu TV Backgrounds or any PNG with a dark screen area." )

#define OVERLAY_ZOOM_TEXT N_("Overlay screen zoom")
#define OVERLAY_ZOOM_LONGTEXT N_( \
    "How much of the frame the video fills (percentage). " \
    "Lower = smaller video inside the TV. 100 = full frame. Default: 65" )

#define OVERLAY_X_TEXT N_("Overlay video X offset")
#define OVERLAY_X_LONGTEXT N_("Horizontal offset of video in overlay. 0=center. Default: 0")

#define OVERLAY_Y_TEXT N_("Overlay video Y offset")
#define OVERLAY_Y_LONGTEXT N_("Vertical offset of video in overlay. 0=center. Default: 0")

#define CONTRAST_TEXT N_("Contrast")
#define CONTRAST_LONGTEXT N_( \
    "Scales luma contrast around midpoint. " \
    "100 = no change, <100 = washed out, >100 = punchier. Default: 100" )

#define SATURATION_TEXT N_("Saturation (color intensity)")
#define SATURATION_LONGTEXT N_( \
    "Scales chroma around neutral. " \
    "0 = monochrome, 100 = no change, 200 = oversaturated. Default: 100" )

vlc_module_begin ()
    set_description( N_("CRT Emulator video filter") )
    set_shortname( N_("CRT Emulator") )
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    set_capability( "video filter", 0 )

    add_integer_with_range( FILTER_PREFIX "darkness", 35, 0, 100,
                            DARKNESS_TEXT, DARKNESS_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "spacing", 2, 1, 20,
                            SPACING_TEXT, SPACING_LONGTEXT, false )
        change_safe()
    add_bool( FILTER_PREFIX "blend", true,
              BLEND_TEXT, BLEND_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "phosphor", 0, 0, 100,
                            PHOSPHOR_TEXT, PHOSPHOR_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "ntsc", 0, 0, 3,
                            NTSC_TEXT, NTSC_LONGTEXT, false )
        change_safe()
    add_bool( FILTER_PREFIX "blackline", false,
              BLACKLINE_TEXT, BLACKLINE_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "barrel", 0, 0, 100,
                            BARREL_TEXT, BARREL_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "vignette", 0, 0, 100,
                            VIGNETTE_TEXT, VIGNETTE_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "bezel", 0, 0, 100,
                            BEZEL_TEXT, BEZEL_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "reflect", 0, 0, 100,
                            REFLECT_TEXT, REFLECT_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "noise", 0, 0, 100,
                            NOISE_TEXT, NOISE_LONGTEXT, false )
        change_safe()
    add_string( FILTER_PREFIX "overlay", "",
                OVERLAY_TEXT, OVERLAY_LONGTEXT, false )
        change_safe()
    add_string( FILTER_PREFIX "custom-presets", "",
                N_("Custom presets"), N_("Serialized custom preset data"), true )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "overlay-zoom", 65, 20, 100,
                            OVERLAY_ZOOM_TEXT, OVERLAY_ZOOM_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "overlay-x", 0, -50, 50,
                            OVERLAY_X_TEXT, OVERLAY_X_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "overlay-y", -12, -50, 50,
                            OVERLAY_Y_TEXT, OVERLAY_Y_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "contrast", 100, 50, 150,
                            CONTRAST_TEXT, CONTRAST_LONGTEXT, false )
        change_safe()
    add_integer_with_range( FILTER_PREFIX "saturation", 100, 0, 200,
                            SATURATION_TEXT, SATURATION_LONGTEXT, false )
        change_safe()

    add_shortcut( "crtemulator" )
    set_callbacks( Create, Destroy )
vlc_module_end ()

static const char *const ppsz_filter_options[] = {
    "darkness", "spacing", "blend", "phosphor", "ntsc", "blackline",
    "barrel", "vignette", "bezel", "reflect", "noise",
    "overlay", "custom-presets", "overlay-zoom", "overlay-x", "overlay-y",
    "contrast", "saturation",
    NULL
};

/*****************************************************************************
 * Phosphor lookup table
 *****************************************************************************/
#define PHOSPHOR_LUT_SIZE 256
static uint8_t phosphor_lut[PHOSPHOR_LUT_SIZE];
static int     phosphor_lut_intensity = -1;

static void build_phosphor_lut( int intensity )
{
    if( intensity == phosphor_lut_intensity )
        return;

    double gamma = 1.0 - (double)intensity * 0.004;
    if( gamma < 0.5 ) gamma = 0.5;
    double boost = 1.0 + (double)intensity * 0.005;

    for( int i = 0; i < 256; i++ )
    {
        double v = pow( (double)i / 255.0, gamma ) * boost;
        int out = (int)( v * 255.0 + 0.5 );
        phosphor_lut[i] = (uint8_t)( out > 255 ? 255 : out );
    }
    phosphor_lut_intensity = intensity;
}

/*****************************************************************************
 * NTSC chroma processing — IIR lowpass + optional Y/C crosstalk
 *
 * Replaces the old symmetric box blur with a causal IIR lowpass filter
 * that naturally produces the rightward color smear of real NTSC video.
 *
 * IIR formula: out[x] = out[x-1] + alpha * (in[x] - out[x-1])
 *   - Smaller alpha = heavier bleed (wider impulse response)
 *   - The filter is causal (left-to-right), producing the characteristic
 *     rightward smear of analog video
 *
 * For composite mode, we also add Y/C crosstalk:
 *   - Cross-chrominance: high-freq luma detail leaks into chroma
 *     (causes rainbow artifacts on fine detail)
 *
 * Alpha values tuned to approximate real signal bandwidths:
 *   S-Video:   alpha=0.35 (I channel ~1.5 MHz, moderate smear)
 *   Composite: alpha=0.20 (heavier smear from Y/C separation filter)
 *   RF:        alpha=0.12 (worst signal path, maximum bleed)
 *****************************************************************************/

/* IIR lowpass coefficients per mode */
static const double ntsc_alpha[] = { 0.0, 0.35, 0.20, 0.12 };

/* Cross-chrominance strength: how much luma detail leaks into chroma.
 * S-Video has separate Y/C so no crosstalk. Composite and RF have it. */
static const double ntsc_crosstalk[] = { 0.0, 0.0, 0.15, 0.25 };

static void ntsc_chroma_process( uint8_t *p_dst, const uint8_t *p_src,
                                 const uint8_t *p_luma,
                                 int i_chroma_width, int i_chroma_lines,
                                 int i_src_pitch, int i_dst_pitch,
                                 int i_luma_pitch,
                                 int i_ntsc_mode, int i_frame_count )
{
    double alpha = ntsc_alpha[i_ntsc_mode];
    double crosstalk = ntsc_crosstalk[i_ntsc_mode];

    for( int y = 0; y < i_chroma_lines; y++ )
    {
        const uint8_t *in  = &p_src[y * i_src_pitch];
        uint8_t       *out = &p_dst[y * i_dst_pitch];

        /* Luma row for cross-chrominance (each chroma row maps to 2 luma
         * rows in YUV420; we use the first of the pair). */
        const uint8_t *luma_row = NULL;
        if( crosstalk > 0.0 && p_luma != NULL )
        {
            int luma_y = y * 2;
            luma_row = &p_luma[luma_y * i_luma_pitch];
        }

        /* Forward IIR pass (left to right — causal, rightward smear) */
        double acc = (double)in[0];

        for( int x = 0; x < i_chroma_width; x++ )
        {
            double sample = (double)in[x];

            /* Cross-chrominance: inject high-freq luma detail into chroma.
             * High-freq = difference between adjacent luma pixels.
             * We modulate by a subcarrier pattern to simulate the
             * 3.58 MHz encoding artifact. */
            if( luma_row != NULL )
            {
                int lx = x * 2; /* chroma→luma coordinate */
                if( lx + 1 < i_chroma_width * 2 )
                {
                    /* High-frequency luma energy (edge detector) */
                    double luma_hf = (double)luma_row[lx + 1]
                                   - (double)luma_row[lx];

                    /* Modulate by subcarrier pattern:
                     * alternates sign per pixel and per line to simulate
                     * the 3.58 MHz carrier phase. */
                    int phase = (x + y + i_frame_count) & 1;
                    double modulated = luma_hf * ( phase ? 1.0 : -1.0 );

                    sample += modulated * crosstalk;
                }
            }

            /* IIR lowpass: acc tracks the filtered signal */
            acc = acc + alpha * ( sample - acc );

            /* Clamp to valid chroma range */
            int val = (int)( acc + 0.5 );
            out[x] = (uint8_t)( val < 0 ? 0 : val > 255 ? 255 : val );
        }

        /* Optional reverse pass for less extreme directionality.
         * Blend forward (70%) and backward (30%) for a more natural look.
         * S-Video: no reverse (cleaner signal). Composite/RF: yes.
         *
         * BUG FIX: Save forward-filtered row before reverse pass.
         * Without this, the reverse IIR reads already-overwritten values,
         * causing compounding feedback instead of a clean backward sweep. */
        if( i_ntsc_mode >= 2 )
        {
            /* Save forward-filtered values to in[] (which we're done
             * reading from — safe to reuse as scratch). If in == out
             * (shouldn't happen, but be safe), use a stack buffer. */
            uint8_t fwd_buf[4096]; /* stack scratch for small rows */
            uint8_t *fwd = ( i_chroma_width <= 4096 ) ? fwd_buf : out;
            if( fwd != out )
                memcpy( fwd, out, i_chroma_width );

            double racc = (double)fwd[i_chroma_width - 1];
            double blend_fwd = 0.7;
            double blend_rev = 0.3;

            for( int x = i_chroma_width - 1; x >= 0; x-- )
            {
                racc = racc + alpha * ( (double)fwd[x] - racc );
                int blended = (int)( (double)fwd[x] * blend_fwd
                                    + racc * blend_rev + 0.5 );
                out[x] = (uint8_t)( blended < 0 ? 0
                                  : blended > 255 ? 255 : blended );
            }
        }
    }
}

/*****************************************************************************
 * NTSC luma artifacts — dot crawl + cross-luminance
 *
 * For composite/RF modes, add subtle checkerboard-modulated chroma
 * energy into the luma plane. This simulates the 3.58 MHz subcarrier
 * remnant that the decoder's lowpass filter fails to remove.
 *
 * The pattern alternates per frame (dot crawl animation) and per
 * scanline (spatial frequency of the subcarrier).
 *****************************************************************************/
static void ntsc_luma_artifacts( uint8_t *p_luma_dst,
                                 const uint8_t *p_luma_src,
                                 const uint8_t *p_chroma_u,
                                 const uint8_t *p_chroma_v,
                                 int i_width, int i_height,
                                 int i_luma_src_pitch, int i_luma_dst_pitch,
                                 int i_chroma_pitch,
                                 int i_ntsc_mode, int i_frame_count )
{
    /* Dot crawl strength: 0 for S-Video, moderate for composite, heavy for RF */
    double strength;
    if( i_ntsc_mode <= 1 ) return; /* S-Video: no dot crawl */
    else if( i_ntsc_mode == 2 ) strength = 6.0;  /* Composite */
    else strength = 10.0; /* RF */

    for( int y = 0; y < i_height; y++ )
    {
        const uint8_t *in  = &p_luma_src[y * i_luma_src_pitch];
        uint8_t       *out = &p_luma_dst[y * i_luma_dst_pitch];

        /* Chroma row (each luma row pair shares one chroma row) */
        int cy = y / 2;
        const uint8_t *u_row = &p_chroma_u[cy * i_chroma_pitch];
        const uint8_t *v_row = &p_chroma_v[cy * i_chroma_pitch];

        for( int x = 0; x < i_width; x++ )
        {
            int val = in[x];

            /* Subcarrier pattern: checkerboard that shifts per frame.
             * Real NTSC uses ~227.5 cycles/line (3.58 MHz / 15.734 kHz),
             * but for visual effect at video resolutions a simple
             * alternating pattern works well. */
            int phase = ( x + y + i_frame_count ) & 1;
            int sign = phase ? 1 : -1;

            /* Chroma energy at this position (how far from neutral gray) */
            int cx = x / 2;
            int u_energy = (int)u_row[cx] - 128; /* neutral = 128 */
            int v_energy = (int)v_row[cx] - 128;

            /* Combined chroma energy magnitude (fast approximation, avoids
             * sqrt in hot loop — ~2M calls/frame at 1080p).
             * max(|u|,|v|) + 0.4*min(|u|,|v|) approximates Euclidean length
             * within ~8%, which is more than adequate for a visual effect. */
            int au = u_energy < 0 ? -u_energy : u_energy;
            int av = v_energy < 0 ? -v_energy : v_energy;
            double chroma_mag = (double)( au > av ? au + (av * 2 / 5)
                                                  : av + (au * 2 / 5) );

            /* Inject as modulated pattern into luma */
            double artifact = (double)sign * chroma_mag * strength / 128.0;

            val += (int)artifact;
            out[x] = (uint8_t)( val < 0 ? 0 : val > 255 ? 255 : val );
        }
    }
}

/* RF noise */
static inline uint8_t rf_noise( int x, int y, int frame_seed )
{
    unsigned h = (unsigned)( x * 374761393u + y * 668265263u +
                             (unsigned)frame_seed * 1274126177u );
    h = (h ^ (h >> 13)) * 1103515245u;
    return (uint8_t)( (h >> 16) & 0x0F );
}

/*****************************************************************************
 * Barrel distortion — CRT screen curvature
 *
 * Maps output pixel (ox, oy) back to source pixel (sx, sy) using the
 * barrel distortion formula from sprash3's CRT shader:
 *   uv' = r * uv / sqrt(r*r - dot(uv,uv))
 *
 * where uv is in [-1,1] normalized coordinates and r controls curvature.
 * Larger r = less curvature (flatter). We map user's 0-100 to r = inf..2.0.
 *
 * For pixels that map outside the source frame, we output black (Y=16 for
 * broadcast black in YUV, or 0 for full range).
 *****************************************************************************/
static inline void barrel_map( int ox, int oy, int width, int height,
                               double f_barrel_r,
                               int *sx, int *sy )
{
    /* Normalize to [-1, 1] using pixel-center coordinates */
    double u = ( ( (double)ox + 0.5 ) / (double)width  ) * 2.0 - 1.0;
    double v = ( ( (double)oy + 0.5 ) / (double)height ) * 2.0 - 1.0;

    /* Apply barrel distortion: uv' = r * uv / sqrt(r^2 - |uv|^2) */
    double d2 = u * u + v * v;
    double r2 = f_barrel_r * f_barrel_r;

    if( d2 >= r2 )
    {
        /* Outside the sphere — maps to black */
        *sx = -1;
        *sy = -1;
        return;
    }

    double scale = f_barrel_r / sqrt( r2 - d2 );
    double su = u * scale;
    double sv = v * scale;

    /* Back to pixel coordinates (no rounding bias — prevents edge blackout) */
    *sx = (int)( ( su + 1.0 ) * 0.5 * (double)width );
    *sy = (int)( ( sv + 1.0 ) * 0.5 * (double)height );

    /* Clamp to frame bounds */
    if( *sx < 0 || *sx >= width || *sy < 0 || *sy >= height )
    {
        *sx = -1;
        *sy = -1;
    }
}

/*****************************************************************************
 * Vignette — edge/corner darkening
 *
 * Computes a darkening factor based on distance from center.
 * Uses smooth falloff: factor = 1.0 - strength * (dist_from_center^2)
 * Returns a scale value 0-256 (like scanline scaling).
 *****************************************************************************/
static inline int vignette_scale( int x, int y, int width, int height,
                                  int i_vignette )
{
    /* Normalize to [-1, 1] */
    double u = ( (double)x / (double)width  ) * 2.0 - 1.0;
    double v = ( (double)y / (double)height ) * 2.0 - 1.0;

    /* Distance squared from center, scaled so corners = 1.0 */
    double d2 = ( u * u + v * v ) * 0.5;

    /* Strength: user 0-100 maps to 0.0 - 1.0 */
    double strength = (double)i_vignette / 100.0;

    /* Smooth vignette with power curve for natural falloff */
    double factor = 1.0 - strength * d2 * d2; /* quartic falloff */
    if( factor < 0.0 ) factor = 0.0;

    return (int)( factor * 256.0 );
}

/*****************************************************************************
 * Bezel — Pre-computed CRT TV frame mask
 *
 * Instead of computing the frame per-pixel per-frame, we generate a mask
 * once (at init or when resolution changes) and apply it every frame.
 *
 * The mask stores two values per pixel:
 *   - luma value for the bezel (0-255)
 *   - alpha (0 = transparent/screen, 255 = fully opaque bezel)
 *
 * Packed as: mask[y * width + x] = (alpha << 8) | luma
 * Using uint16_t array for compact storage.
 *
 * Inspired by cool-retro-term's TerminalFrame shader by Filippo Scognamiglio.
 *****************************************************************************/

static void build_bezel_mask( uint16_t *mask, int width, int height,
                              int i_bezel )
{
    /* Border thickness: user 1-100 maps to 3%-12% of smaller dimension */
    double min_dim = (double)( width < height ? width : height );
    double thickness = ( 0.03 + (double)i_bezel * 0.0009 ) * min_dim;
    if( thickness < 6.0 ) thickness = 6.0;

    /* Corner radius */
    double corner_r = thickness * 1.2;
    if( corner_r < 10.0 ) corner_r = 10.0;

    double half_w = (double)width * 0.5;
    double half_h = (double)height * 0.5;

    /* Inner screen rect (centered) */
    double scr_hw = half_w - thickness; /* screen half-width */
    double scr_hh = half_h - thickness; /* screen half-height */

    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            /* Center-relative coordinates */
            double px = (double)x - half_w + 0.5;
            double py = (double)y - half_h + 0.5;
            double ax = px < 0 ? -px : px; /* abs */
            double ay = py < 0 ? -py : py;

            /* Signed distance to rounded rect (standard SDF):
             * q = abs(p) - rect_half_size
             * dist = length(max(q - corner, 0)) + min(max(q.x-corner, q.y-corner), 0) - corner
             */
            double qx = ax - scr_hw + corner_r;
            double qy = ay - scr_hh + corner_r;
            double ox = qx > 0.0 ? qx : 0.0;
            double oy = qy > 0.0 ? qy : 0.0;
            double inside = qx > qy ? qx : qy;
            if( inside > 0.0 ) inside = 0.0;
            double dist = sqrt( ox * ox + oy * oy ) + inside - corner_r;

            /* dist < 0 = inside screen, dist > 0 = in bezel */
            if( dist < -1.5 )
            {
                mask[y * width + x] = 0;
                continue;
            }

            /* Alpha: smooth anti-aliased edge over 1.5px */
            double alpha;
            if( dist < 0.0 )
                alpha = 0.5 + dist / 3.0;
            else
                alpha = 1.0;
            if( alpha <= 0.0 ) { mask[y * width + x] = 0; continue; }
            if( alpha > 1.0 ) alpha = 1.0;

            /* How deep into the bezel (0 = at screen edge, 1 = outer edge) */
            double depth = dist / thickness;
            if( depth < 0.0 ) depth = 0.0;
            if( depth > 1.0 ) depth = 1.0;

            /* === Bezel luma shading === */

            /* Base: very dark (luma 6) */
            double luma = 6.0;

            /* Inner rim highlight — bright edge where glass meets frame.
             * This is THE defining visual feature of a CRT bezel. */
            if( depth < 0.08 )
                luma += 40.0 * ( 1.0 - depth / 0.08 );
            else if( depth < 0.15 )
                luma += 15.0 * ( 1.0 - ( depth - 0.08 ) / 0.07 );

            /* 3D bevel: light from top-left.
             * Use pixel position to determine which face is lit. */
            double nx = px / half_w; /* -1..1 normalized position */
            double ny = py / half_h;

            /* Top face = ny < 0 (above center), bottom = ny > 0 */
            double top_face = ny < 0.0 ? -ny : 0.0;
            double bot_face = ny > 0.0 ?  ny : 0.0;
            double lft_face = nx < 0.0 ? -nx : 0.0;
            double rgt_face = nx > 0.0 ?  nx : 0.0;

            /* Lit faces (top, left) get a subtle brightness boost */
            double bevel = ( top_face * 12.0 + lft_face * 10.0 ) * ( 0.3 + 0.7 * ( 1.0 - depth ) );
            /* Shadow faces (bottom, right) stay darker */
            bevel += ( bot_face * 3.0 + rgt_face * 3.0 ) * depth;

            luma += bevel;

            /* Outer edge: slight ambient catch */
            if( depth > 0.92 )
                luma += 5.0 * ( ( depth - 0.92 ) / 0.08 );

            if( luma > 55.0 ) luma = 55.0;
            if( luma < 3.0 ) luma = 3.0;

            int a = (int)( alpha * 255.0 );
            int l = (int)( luma + 0.5 );
            mask[y * width + x] = (uint16_t)( ( a << 8 ) | l );
        }
    }
}

/*****************************************************************************
 * Glass reflection — diagonal light streak across the CRT screen
 *
 * Simulates overhead light reflecting off the curved CRT glass.
 * Returns an additive brightness value (0-255) to add to luma.
 * The reflection is a smooth diagonal band from top-left to center.
 *****************************************************************************/
static inline int reflect_value( int x, int y, int width, int height,
                                 int i_reflect )
{
    /* Normalize to [-1, 1] from center */
    double u = ( (double)x / (double)width  ) * 2.0 - 1.0;
    double v = ( (double)y / (double)height ) * 2.0 - 1.0;

    /* Real CRT glass reflection: a broad, soft glow in the upper-left
     * quadrant — like an overhead room light reflecting off curved glass.
     * Based on reference images from Soqueroeu TV Backgrounds. */

    /* Primary reflection: soft glow centered at upper-left (-0.4, -0.5) */
    double dx1 = u + 0.4;
    double dy1 = v + 0.5;
    double d1 = dx1 * dx1 * 1.2 + dy1 * dy1 * 1.8; /* elliptical */
    double glow1 = exp( -d1 * 2.0 ); /* broad, soft falloff */

    /* Secondary highlight: smaller, brighter spot at upper-left edge
     * (where glass curvature catches the most light) */
    double dx2 = u + 0.55;
    double dy2 = v + 0.6;
    double d2 = dx2 * dx2 + dy2 * dy2;
    double glow2 = exp( -d2 * 6.0 ) * 0.5; /* tighter, dimmer */

    /* Combine: broad ambient + focused highlight */
    double combined = glow1 + glow2;

    /* Fade toward edges (reflection shouldn't reach screen border) */
    double edge_x = 1.0 - u * u;
    double edge_y = 1.0 - v * v;
    double edge_fade = edge_x * edge_y;
    if( edge_fade < 0.0 ) edge_fade = 0.0;

    double intensity = combined * edge_fade * (double)i_reflect / 100.0;

    /* Scale: at reflect=100, max ~80 luma units added.
     * This needs to be strong enough to visibly brighten a dark TV frame
     * (luma 10-30) so the reflection is noticeable on the bezel. */
    int result = (int)( intensity * 80.0 );
    return result > 80 ? 80 : result;
}

/***************************************************************************
 * Overlay — load PNG TV background, scale to video, composite via
 * zoom-rectangle positioning (PASS 5).
 ***************************************************************************/
typedef struct
{
    uint8_t *p_y;    /* scaled overlay Y plane */
    uint8_t *p_u;    /* scaled overlay U plane (half-res) */
    uint8_t *p_v;    /* scaled overlay V plane (half-res) */
    int      i_width;
    int      i_height;
    char    *psz_path;
} overlay_t;

/* Load a PNG from disk, scale to target dimensions, convert RGB to YUV. */
static void overlay_load( overlay_t *ov, const char *path,
                          int tgt_w, int tgt_h )
{
    /* Free previous */
    free( ov->p_y ); ov->p_y = NULL;
    free( ov->p_u ); ov->p_u = NULL;
    free( ov->p_v ); ov->p_v = NULL;
    free( ov->psz_path ); ov->psz_path = NULL;
    ov->i_width = 0;
    ov->i_height = 0;

    if( !path || !path[0] )
        return;

    /* Load PNG via stb_image (RGBA) */
    int img_w, img_h, img_ch;
    unsigned char *rgba = stbi_load( path, &img_w, &img_h, &img_ch, 4 );
    if( !rgba )
        return;

    /* Allocate scaled planes */
    int n = tgt_w * tgt_h;
    int n_chroma = (tgt_w / 2) * (tgt_h / 2);
    ov->p_y = (uint8_t *)malloc( n );
    ov->p_u = (uint8_t *)malloc( n_chroma );
    ov->p_v = (uint8_t *)malloc( n_chroma );
    if( !ov->p_y || !ov->p_u || !ov->p_v )
    {
        stbi_image_free( rgba );
        free( ov->p_y ); free( ov->p_u );
        free( ov->p_v );
        ov->p_y = ov->p_u = ov->p_v = NULL;
        return;
    }

    /* Scale + convert RGB to YUV420.
     *
     * ASPECT RATIO: Center-crop the overlay to match the video's AR.
     * For a 16:9 overlay with 4:3 video: crop sides, keeping TV centered.
     * For same AR: no crop. This preserves the TV's correct proportions.
     */
    double ov_ar = (double)img_w / (double)img_h;
    double tgt_ar = (double)tgt_w / (double)tgt_h;

    int crop_x = 0, crop_y = 0, crop_w = img_w, crop_h = img_h;

    if( ov_ar > tgt_ar + 0.01 )
    {
        /* Overlay wider than video: crop sides */
        crop_w = (int)( (double)img_h * tgt_ar + 0.5 );
        crop_x = ( img_w - crop_w ) / 2;
    }
    else if( ov_ar < tgt_ar - 0.01 )
    {
        /* Overlay taller than video: crop top/bottom */
        crop_h = (int)( (double)img_w / tgt_ar + 0.5 );
        crop_y = ( img_h - crop_h ) / 2;
    }

    for( int y = 0; y < tgt_h; y++ )
    {
        int src_y = crop_y + y * crop_h / tgt_h;
        if( src_y >= img_h ) src_y = img_h - 1;

        for( int x = 0; x < tgt_w; x++ )
        {
            int src_x = crop_x + x * crop_w / tgt_w;
            if( src_x >= img_w ) src_x = img_w - 1;

            const unsigned char *px = &rgba[ ( src_y * img_w + src_x ) * 4 ];
            int r = px[0], g = px[1], b = px[2];

            /* RGB to Y */
            int yy = ( 66 * r + 129 * g + 25 * b + 128 ) / 256 + 16;

            ov->p_y[ y * tgt_w + x ] = (uint8_t)( yy > 235 ? 235 : yy < 16 ? 16 : yy );

            /* Chroma: subsample 2x2 (only on even x,y) */
            if( ( x & 1 ) == 0 && ( y & 1 ) == 0 )
            {
                int uu = ( -38 * r - 74 * g + 112 * b + 128 ) / 256 + 128;
                int vv = ( 112 * r - 94 * g - 18 * b + 128 ) / 256 + 128;
                int ci = ( y / 2 ) * ( tgt_w / 2 ) + ( x / 2 );
                ov->p_u[ci] = (uint8_t)( uu > 240 ? 240 : uu < 16 ? 16 : uu );
                ov->p_v[ci] = (uint8_t)( vv > 240 ? 240 : vv < 16 ? 16 : vv );
            }
        }
    }

    stbi_image_free( rgba );

    ov->i_width = tgt_w;
    ov->i_height = tgt_h;
    ov->psz_path = strdup( path );
}

static void overlay_free( overlay_t *ov )
{
    free( ov->p_y );
    free( ov->p_u );
    free( ov->p_v );
    free( ov->psz_path );
    memset( ov, 0, sizeof(*ov) );
}

struct filter_sys_t
{
    int i_frame_count;
    uint8_t  *p_barrel_tmp;
    int       i_barrel_tmp_size;
    uint16_t *p_bezel_mask;
    int       i_bezel_mask_w;
    int       i_bezel_mask_h;
    int       i_bezel_param;
    overlay_t overlay;
};

static int Create( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;

    if( p_filter->fmt_in.video.i_chroma != p_filter->fmt_out.video.i_chroma )
    {
        msg_Err( p_filter, "Input and output chromas don't match" );
        return VLC_EGENERIC;
    }

    switch( p_filter->fmt_in.video.i_chroma )
    {
        CASE_PLANAR_YUV
            break;
        default:
            msg_Dbg( p_filter,
                     "Unsupported chroma (%4.4s), need planar YUV. "
                     "Try disabling hardware decoding.",
                     (char *)&p_filter->fmt_in.video.i_chroma );
            return VLC_EGENERIC;
    }

    filter_sys_t *p_sys = malloc( sizeof( *p_sys ) );
    if( !p_sys )
        return VLC_ENOMEM;
    p_sys->i_frame_count = 0;
    p_sys->p_barrel_tmp = NULL;
    p_sys->i_barrel_tmp_size = 0;
    p_sys->p_bezel_mask = NULL;
    p_sys->i_bezel_mask_w = 0;
    p_sys->i_bezel_mask_h = 0;
    p_sys->i_bezel_param = -1;
    memset( &p_sys->overlay, 0, sizeof(p_sys->overlay) );
    p_filter->p_sys = p_sys;

    config_ChainParse( p_filter, FILTER_PREFIX, ppsz_filter_options,
                       p_filter->p_cfg );

    p_filter->pf_video_filter = Filter;

    msg_Info( p_filter,
              "CRT filter initialized (scanlines+phosphor+NTSC+barrel+vignette)" );

    return VLC_SUCCESS;
}

static void Destroy( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    filter_sys_t *p_sys = p_filter->p_sys;
    free( p_sys->p_barrel_tmp );
    free( p_sys->p_bezel_mask );
    overlay_free( &p_sys->overlay );
    free( p_sys );
}

/*****************************************************************************
 * Filter: apply CRT scanline + phosphor + NTSC + barrel + vignette
 *****************************************************************************/
static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    picture_t *p_outpic;

    if( !p_pic )
        return NULL;

    p_outpic = filter_NewPicture( p_filter );
    if( !p_outpic )
    {
        msg_Warn( p_filter, "can't get output picture" );
        picture_Release( p_pic );
        return NULL;
    }

    p_sys->i_frame_count++;

    /* Read parameters from global config — live adjustable */
    int i_base_darkness = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "darkness" );
    int i_base_spacing  = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "spacing" );
    int b_blend         = config_GetInt( p_filter,
                                         FILTER_PREFIX "blend" ) != 0;
    int i_phosphor      = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "phosphor" );
    int i_ntsc          = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "ntsc" );
    int b_blackline     = config_GetInt( p_filter,
                                         FILTER_PREFIX "blackline" ) != 0;
    int i_barrel        = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "barrel" );
    int i_vignette      = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "vignette" );
    int i_bezel         = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "bezel" );
    int i_reflect       = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "reflect" );
    int i_noise         = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "noise" );
    int i_contrast      = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "contrast" );
    int i_saturation    = (int)config_GetInt( p_filter,
                                              FILTER_PREFIX "saturation" );

    /* Clamp */
    if( i_base_darkness < 0 )   i_base_darkness = 0;
    if( i_base_darkness > 100 ) i_base_darkness = 100;
    if( i_base_spacing < 1 )    i_base_spacing = 1;
    if( i_base_spacing > 20 )   i_base_spacing = 20;
    if( i_phosphor < 0 )        i_phosphor = 0;
    if( i_phosphor > 100 )      i_phosphor = 100;
    if( i_ntsc < 0 )            i_ntsc = 0;
    if( i_ntsc > 3 )            i_ntsc = 3;
    if( i_noise < 0 )           i_noise = 0;
    if( i_noise > 100 )         i_noise = 100;
    if( i_bezel < 0 )           i_bezel = 0;
    if( i_bezel > 100 )         i_bezel = 100;
    if( i_reflect < 0 )         i_reflect = 0;
    if( i_reflect > 100 )       i_reflect = 100;
    if( i_barrel < 0 )          i_barrel = 0;
    if( i_barrel > 100 )        i_barrel = 100;
    if( i_vignette < 0 )        i_vignette = 0;
    if( i_vignette > 100 )      i_vignette = 100;
    if( i_contrast < 50 )       i_contrast = 50;
    if( i_contrast > 150 )      i_contrast = 150;
    if( i_saturation < 0 )      i_saturation = 0;
    if( i_saturation > 200 )    i_saturation = 200;

    /* Short-circuit: everything off */
    if( i_base_darkness == 0 && !b_blackline && i_phosphor == 0 && i_ntsc == 0
        && i_barrel == 0 && i_vignette == 0 && i_bezel == 0 && i_reflect == 0
        && i_noise == 0 && i_contrast == 100 && i_saturation == 100 )
    {
        picture_Copy( p_outpic, p_pic );
        return CopyInfoAndRelease( p_outpic, p_pic );
    }

    /* Build phosphor LUT if needed */
    if( i_phosphor > 0 )
        build_phosphor_lut( i_phosphor );

    /* Barrel distortion radius: user 1-100 maps to r = 10.0 .. 1.5
     * (smaller r = more curvature). 0 = disabled. */
    double f_barrel_r = 0.0;
    if( i_barrel > 0 )
        f_barrel_r = 10.0 - (double)i_barrel * 0.085; /* 1→9.9, 50→5.75, 100→1.5 */

    const int i_height = p_pic->p[Y_PLANE].i_visible_lines;

    /* Scale spacing (ref: 480p) */
    double f_spacing = (double)i_base_spacing * (double)i_height / 480.0;
    if( f_spacing < 1.5 )
        f_spacing = 1.5;

    /* Scale darkness (ref: 1080p) */
    double f_dark_ratio = (double)i_height / 1080.0;
    if( f_dark_ratio > 1.0 )  f_dark_ratio = 1.0;
    if( f_dark_ratio < 0.15 ) f_dark_ratio = 0.15;

    int i_eff_darkness = (int)( (double)i_base_darkness * f_dark_ratio + 0.5 );

    const int i_bright = 256;
    const int i_dark   = 256 - ( ( i_eff_darkness * 256 ) / 100 );

    const double f_two_pi = 6.28318530717958647692;

    /* =========================================================
     * PASS 1: Apply barrel distortion to all planes (if enabled)
     *
     * BUG FIX: Use luma dimensions for the barrel coordinate space
     * on ALL planes. Chroma planes in YUV420 are half-width/half-height,
     * so we pass the luma width/height to barrel_map for consistent
     * curvature, then scale the returned coordinates for chroma.
     * ========================================================= */
    const int i_luma_w = p_pic->p[Y_PLANE].i_visible_pitch;
    const int i_luma_h = p_pic->p[Y_PLANE].i_visible_lines;

    for( int i_plane = 0; i_plane < p_pic->i_planes; i_plane++ )
    {
        const plane_t *p_src = &p_pic->p[i_plane];
        plane_t *p_dst = &p_outpic->p[i_plane];

        const int i_lines = p_src->i_visible_lines;
        const int i_visible_pitch = p_src->i_visible_pitch;

        if( i_barrel > 0 )
        {
            uint8_t black_val = ( i_plane == Y_PLANE ) ? 0 : 128;

            /* Scale factors: chroma dimensions relative to luma */
            int x_scale = ( i_luma_w > 0 && i_visible_pitch > 0 )
                        ? i_luma_w / i_visible_pitch : 1;
            int y_scale = ( i_luma_h > 0 && i_lines > 0 )
                        ? i_luma_h / i_lines : 1;
            if( x_scale < 1 ) x_scale = 1;
            if( y_scale < 1 ) y_scale = 1;

            for( int y = 0; y < i_lines; y++ )
            {
                uint8_t *out = &p_dst->p_pixels[y * p_dst->i_pitch];
                for( int x = 0; x < i_visible_pitch; x++ )
                {
                    int sx, sy;
                    /* Map in luma coordinate space */
                    barrel_map( x * x_scale, y * y_scale,
                                i_luma_w, i_luma_h,
                                f_barrel_r, &sx, &sy );
                    if( sx < 0 )
                    {
                        out[x] = black_val;
                    }
                    else
                    {
                        /* Scale back to this plane's coordinates */
                        int px = sx / x_scale;
                        int py = sy / y_scale;
                        if( px >= i_visible_pitch ) px = i_visible_pitch - 1;
                        if( py >= i_lines ) py = i_lines - 1;
                        out[x] = p_src->p_pixels[py * p_src->i_pitch + px];
                    }
                }
            }
        }
        else
        {
            for( int y = 0; y < i_lines; y++ )
            {
                memcpy( &p_dst->p_pixels[y * p_dst->i_pitch],
                        &p_src->p_pixels[y * p_src->i_pitch],
                        i_visible_pitch );
            }
        }
    }

    /* At this point, p_outpic has the barrel-distorted (or copied) frame.
     * All subsequent effects operate on p_outpic in-place or use it as
     * the source for further processing. */

    /* =========================================================
     * PASS 2: NTSC luma artifacts (dot crawl / cross-luminance)
     * Needs chroma data to compute, writes to luma plane.
     * Must happen BEFORE scanlines darken the luma.
     * ========================================================= */
    if( i_ntsc >= 2 && p_outpic->i_planes >= 3 )
    {
        /* We need a temp copy of luma since ntsc_luma_artifacts reads
         * and writes the same plane */
        const plane_t *p_y = &p_outpic->p[Y_PLANE];
        int luma_size = p_y->i_visible_lines * p_y->i_pitch;
        int needed = luma_size;
        if( needed > p_sys->i_barrel_tmp_size )
        {
            free( p_sys->p_barrel_tmp );
            p_sys->p_barrel_tmp = malloc( needed );
            p_sys->i_barrel_tmp_size = needed;
        }
        if( p_sys->p_barrel_tmp )
        {
            /* Copy current luma to temp */
            memcpy( p_sys->p_barrel_tmp, p_y->p_pixels, luma_size );

            ntsc_luma_artifacts( p_y->p_pixels,
                                 p_sys->p_barrel_tmp,
                                 p_outpic->p[U_PLANE].p_pixels,
                                 p_outpic->p[V_PLANE].p_pixels,
                                 p_y->i_visible_pitch,
                                 p_y->i_visible_lines,
                                 p_y->i_pitch, p_y->i_pitch,
                                 p_outpic->p[U_PLANE].i_pitch,
                                 i_ntsc, p_sys->i_frame_count );
        }
    }

    /* =========================================================
     * PASS 3: NTSC chroma processing (IIR lowpass + crosstalk)
     * ========================================================= */
    if( i_ntsc > 0 )
    {
        for( int i_plane = 1; i_plane < p_outpic->i_planes; i_plane++ )
        {
            plane_t *p_chroma = &p_outpic->p[i_plane];
            int chroma_size = p_chroma->i_visible_lines * p_chroma->i_pitch;
            int needed = chroma_size;
            if( needed > p_sys->i_barrel_tmp_size )
            {
                free( p_sys->p_barrel_tmp );
                p_sys->p_barrel_tmp = malloc( needed );
                p_sys->i_barrel_tmp_size = needed;
            }
            if( p_sys->p_barrel_tmp )
            {
                /* Copy chroma to temp, process from temp to output */
                memcpy( p_sys->p_barrel_tmp, p_chroma->p_pixels, chroma_size );

                ntsc_chroma_process( p_chroma->p_pixels,
                                     p_sys->p_barrel_tmp,
                                     p_outpic->p[Y_PLANE].p_pixels,
                                     p_chroma->i_visible_pitch,
                                     p_chroma->i_visible_lines,
                                     p_chroma->i_pitch,
                                     p_chroma->i_pitch,
                                     p_outpic->p[Y_PLANE].i_pitch,
                                     i_ntsc, p_sys->i_frame_count );
            }
        }
    }

    /* =========================================================
     * PASS 3.5: Saturation — scale U/V around neutral 128
     * Applied AFTER NTSC so bleed operates on original colors.
     * ========================================================= */
    if( i_saturation != 100 && p_outpic->i_planes >= 3 )
    {
        for( int i_plane = 1; i_plane < p_outpic->i_planes; i_plane++ )
        {
            plane_t *p_chroma = &p_outpic->p[i_plane];
            const int i_lines_c = p_chroma->i_visible_lines;
            const int i_vis_c   = p_chroma->i_visible_pitch;

            for( int y = 0; y < i_lines_c; y++ )
            {
                uint8_t *row = &p_chroma->p_pixels[y * p_chroma->i_pitch];
                for( int x = 0; x < i_vis_c; x++ )
                {
                    int val = 128 + ( ( (int)row[x] - 128 ) * i_saturation ) / 100;
                    row[x] = (uint8_t)( val < 0 ? 0 : val > 255 ? 255 : val );
                }
            }
        }
    }

    /* =========================================================
     * PASS 3.75: Build/apply bezel mask
     * The mask is pre-computed once and cached. Rebuilt only when
     * resolution or bezel parameter changes.
     * ========================================================= */
    if( i_bezel > 0 )
    {
        const int bw = p_outpic->p[Y_PLANE].i_visible_pitch;
        const int bh = p_outpic->p[Y_PLANE].i_visible_lines;

        /* Rebuild mask if needed */
        if( p_sys->i_bezel_mask_w != bw || p_sys->i_bezel_mask_h != bh
            || p_sys->i_bezel_param != i_bezel )
        {
            free( p_sys->p_bezel_mask );
            p_sys->p_bezel_mask = malloc( bw * bh * sizeof(uint16_t) );
            if( p_sys->p_bezel_mask )
            {
                build_bezel_mask( p_sys->p_bezel_mask, bw, bh, i_bezel );
                p_sys->i_bezel_mask_w = bw;
                p_sys->i_bezel_mask_h = bh;
                p_sys->i_bezel_param = i_bezel;
            }
        }

        /* Apply mask to chroma planes (set bezel area to neutral) */
        if( p_sys->p_bezel_mask && p_outpic->i_planes >= 3 )
        {
            for( int i_plane = 1; i_plane < p_outpic->i_planes; i_plane++ )
            {
                plane_t *p_chroma = &p_outpic->p[i_plane];
                const int cw = p_chroma->i_visible_pitch;
                const int ch = p_chroma->i_visible_lines;
                int x_sc = bw / cw; if( x_sc < 1 ) x_sc = 1;
                int y_sc = bh / ch; if( y_sc < 1 ) y_sc = 1;

                for( int y = 0; y < ch; y++ )
                {
                    uint8_t *row = &p_chroma->p_pixels[y * p_chroma->i_pitch];
                    for( int x = 0; x < cw; x++ )
                    {
                        uint16_t m = p_sys->p_bezel_mask[
                            (y * y_sc) * bw + (x * x_sc) ];
                        int a = m >> 8;
                        if( a > 0 )
                        {
                            /* Blend toward neutral 128 */
                            int orig = (int)row[x];
                            row[x] = (uint8_t)(
                                ( orig * (255 - a) + 128 * a ) / 255 );
                        }
                    }
                }
            }
        }
    }

    /* =========================================================
     * PASS 4: Luma effects (scanlines + phosphor + vignette + noise)
     * ========================================================= */
    {
        const plane_t *p_y_src = &p_outpic->p[Y_PLANE];
        const int i_lines = p_y_src->i_visible_lines;
        const int i_visible_pitch = p_y_src->i_visible_pitch;

        /* We need to process luma in-place. Copy to temp first. */
        int luma_size = i_lines * p_y_src->i_pitch;
        int needed = luma_size;
        if( needed > p_sys->i_barrel_tmp_size )
        {
            free( p_sys->p_barrel_tmp );
            p_sys->p_barrel_tmp = malloc( needed );
            p_sys->i_barrel_tmp_size = needed;
        }

        if( p_sys->p_barrel_tmp &&
            ( i_base_darkness > 0 || b_blackline || i_phosphor > 0
              || i_vignette > 0 || i_bezel > 0 || i_reflect > 0
              || i_noise > 0 || i_contrast != 100 ) )
        {
            memcpy( p_sys->p_barrel_tmp, p_y_src->p_pixels, luma_size );

        for( int y = 0; y < i_lines; y++ )
        {
            const uint8_t *p_in  = &p_sys->p_barrel_tmp[y * p_y_src->i_pitch];
            uint8_t       *p_out = &p_outpic->p[Y_PLANE].p_pixels[y * p_y_src->i_pitch];

            /* Compute scanline scale for this row */
            int i_scale;

            if( b_blackline )
            {
                int i_sp = (int)( f_spacing + 0.5 );
                if( i_sp < 2 ) i_sp = 2;
                i_scale = ( ( y % i_sp ) < ( i_sp / 2 ) ) ? i_bright : 0;
            }
            else if( i_base_darkness == 0 )
            {
                i_scale = i_bright;
            }
            else if( b_blend )
            {
                double f_phase = cos( (double)y * f_two_pi / f_spacing );
                int i_mid   = ( i_bright + i_dark ) / 2;
                int i_range = ( i_bright - i_dark ) / 2;
                i_scale = i_mid + (int)( f_phase * (double)i_range );
            }
            else
            {
                int i_sp = (int)( f_spacing + 0.5 );
                if( i_sp < 2 ) i_sp = 2;
                i_scale = ( ( y % i_sp ) < ( i_sp / 2 ) ) ? i_bright : i_dark;
            }

            /* Apply all effects per pixel */
            int need_per_pixel = ( i_scale < i_bright ) || b_blackline
                                 || ( i_phosphor > 0 ) || ( i_noise > 0 )
                                 || ( i_vignette > 0 ) || ( i_bezel > 0 )
                                 || ( i_reflect > 0 ) || ( i_contrast != 100 );

            if( !need_per_pixel )
            {
                memcpy( p_out, p_in, i_visible_pitch );
            }
            else
            {
                for( int x = 0; x < i_visible_pitch; x++ )
                {
                    unsigned val = p_in[x];

                    /* Contrast: scale luma around midpoint 128 */
                    if( i_contrast != 100 )
                    {
                        int c = 128 + ( ( (int)val - 128 ) * i_contrast ) / 100;
                        val = (unsigned)( c < 0 ? 0 : c > 255 ? 255 : c );
                    }

                    /* Phosphor glow */
                    if( i_phosphor > 0 )
                        val = phosphor_lut[val];

                    /* Scanline darkening */
                    if( i_scale < i_bright )
                        val = ( val * (unsigned)i_scale ) >> 8;

                    /* Vignette */
                    if( i_vignette > 0 )
                    {
                        int v_scale = vignette_scale( x, y,
                                                      i_visible_pitch, i_lines,
                                                      i_vignette );
                        val = ( val * (unsigned)v_scale ) >> 8;
                    }

                    /* Analog noise */
                    if( i_noise > 0 )
                    {
                        int raw = (int)rf_noise( x, y,
                                                 p_sys->i_frame_count );
                        int n = ( raw * i_noise ) / 100 - ( i_noise * 8 / 100 );
                        int result = (int)val + n;
                        val = (unsigned)( result < 0 ? 0 :
                                          result > 255 ? 255 : result );
                    }

                    /* Glass reflection moved to PASS 6 (after overlay) */

                    /* Bezel frame — blend with pre-computed mask */
                    if( i_bezel > 0 && p_sys->p_bezel_mask
                        && x < p_sys->i_bezel_mask_w
                        && y < p_sys->i_bezel_mask_h )
                    {
                        uint16_t m = p_sys->p_bezel_mask[
                            y * p_sys->i_bezel_mask_w + x ];
                        int a = m >> 8;    /* alpha 0-255 */
                        int l = m & 0xFF;  /* bezel luma */
                        if( a > 0 )
                            val = (unsigned)(
                                ( (int)val * (255 - a) + l * a ) / 255 );
                    }

                    p_out[x] = (uint8_t)val;
                }
            }
        }
        } /* end if luma effects needed */
    } /* end PASS 4 block */

    /* =========================================================
     * PASS 5: TV overlay — scale video into screen region, overlay frame
     *
     * Instead of drawing the overlay ON TOP of the full-frame video,
     * we SHRINK the video to fit inside the TV's screen area and fill
     * everything else with the overlay image (TV frame, desk, wall).
     * ========================================================= */
    {
        char *psz_overlay = config_GetPsz( p_filter, FILTER_PREFIX "overlay" );

        if( psz_overlay && psz_overlay[0] )
        {
            const int ov_w = p_outpic->p[Y_PLANE].i_visible_pitch;
            const int ov_h = p_outpic->p[Y_PLANE].i_visible_lines;
            overlay_t *ov = &p_sys->overlay;

            /* Reload if path or resolution changed */
            if( !ov->p_y
                || ov->i_width != ov_w || ov->i_height != ov_h
                || !ov->psz_path || strcmp( ov->psz_path, psz_overlay ) != 0 )
            {
                overlay_load( ov, psz_overlay, ov_w, ov_h );
                msg_Info( p_filter,
                    "CRT overlay loaded: %dx%d frame", ov_w, ov_h );
            }

            if( ov->p_y )
            {
                /* We need a copy of the current video to sample from
                 * while we overwrite the output. Use barrel_tmp. */
                int luma_size = ov_h * p_outpic->p[Y_PLANE].i_pitch;
                int needed = luma_size;
                if( needed > p_sys->i_barrel_tmp_size )
                {
                    free( p_sys->p_barrel_tmp );
                    p_sys->p_barrel_tmp = malloc( needed );
                    p_sys->i_barrel_tmp_size = needed;
                }

                if( p_sys->p_barrel_tmp )
                {
                    plane_t *p_y = &p_outpic->p[Y_PLANE];
                    memcpy( p_sys->p_barrel_tmp, p_y->p_pixels, luma_size );

                    /* Read overlay positioning parameters */
                    int i_zoom = (int)config_GetInt( p_filter,
                                     FILTER_PREFIX "overlay-zoom" );
                    int i_off_x = (int)config_GetInt( p_filter,
                                      FILTER_PREFIX "overlay-x" );
                    int i_off_y = (int)config_GetInt( p_filter,
                                      FILTER_PREFIX "overlay-y" );
                    if( i_zoom < 20 ) i_zoom = 20;
                    if( i_zoom > 100 ) i_zoom = 100;
                    if( i_off_x < -50 ) i_off_x = -50;
                    if( i_off_x > 50 ) i_off_x = 50;
                    if( i_off_y < -50 ) i_off_y = -50;
                    if( i_off_y > 50 ) i_off_y = 50;

                    /* Screen region: video area within the overlay frame.
                     * Zoom controls size, X/Y offsets shift position. */
                    int scr_w = ov_w * i_zoom / 100;
                    int scr_h = ov_h * i_zoom / 100;
                    int scr_off_x = ( ov_w - scr_w ) / 2
                                  + i_off_x * ov_w / 200;
                    int scr_off_y = ( ov_h - scr_h ) / 2
                                  + i_off_y * ov_h / 200;
                    /* Clamp so screen stays within frame */
                    if( scr_off_x < 0 ) scr_off_x = 0;
                    if( scr_off_y < 0 ) scr_off_y = 0;
                    if( scr_off_x + scr_w > ov_w )
                        scr_off_x = ov_w - scr_w;
                    if( scr_off_y + scr_h > ov_h )
                        scr_off_y = ov_h - scr_h;

                    for( int y = 0; y < ov_h; y++ )
                    {
                        uint8_t *out = &p_y->p_pixels[y * p_y->i_pitch];
                        const uint8_t *ovl_y = &ov->p_y[y * ov_w];

                        /* Anti-alias blend zone width (pixels) */
                        int aa = 3;

                        for( int x = 0; x < ov_w; x++ )
                        {
                            int rx = x - scr_off_x;
                            int ry = y - scr_off_y;

                            /* Signed distance to zoom rectangle edge
                             * (negative = inside, positive = outside) */
                            int dl = -rx;          /* dist to left edge */
                            int dr = rx - scr_w + 1; /* dist to right edge */
                            int dt = -ry;          /* dist to top edge */
                            int db = ry - scr_h + 1; /* dist to bottom edge */
                            int edge_dist = dl;
                            if( dr > edge_dist ) edge_dist = dr;
                            if( dt > edge_dist ) edge_dist = dt;
                            if( db > edge_dist ) edge_dist = db;

                            if( edge_dist >= aa )
                            {
                                /* Fully outside zoom: show overlay */
                                out[x] = ovl_y[x];
                            }
                            else if( edge_dist <= -aa )
                            {
                                /* Fully inside zoom: show video */
                                int vx = rx * ov_w / scr_w;
                                int vy = ry * ov_h / scr_h;
                                if( vx >= ov_w ) vx = ov_w - 1;
                                if( vy >= ov_h ) vy = ov_h - 1;
                                out[x] = p_sys->p_barrel_tmp[
                                    vy * p_y->i_pitch + vx ];
                            }
                            else
                            {
                                /* Anti-alias blend zone */
                                int vx = rx > 0 ? rx * ov_w / scr_w : 0;
                                int vy = ry > 0 ? ry * ov_h / scr_h : 0;
                                if( vx >= ov_w ) vx = ov_w - 1;
                                if( vy >= ov_h ) vy = ov_h - 1;
                                uint8_t vid = p_sys->p_barrel_tmp[
                                    vy * p_y->i_pitch + vx ];

                                /* Blend: -aa = all video, +aa = all overlay */
                                int blend_a = ( edge_dist + aa ) * 255 / ( aa * 2 );
                                if( blend_a < 0 ) blend_a = 0;
                                if( blend_a > 255 ) blend_a = 255;
                                out[x] = (uint8_t)(
                                    ( (int)vid * (255 - blend_a)
                                    + (int)ovl_y[x] * blend_a ) / 255 );
                            }
                        }
                    }

                    /* Same for U/V chroma planes */
                    if( ov->p_u && ov->p_v && p_outpic->i_planes >= 3 )
                    {
                        int chroma_w = ov_w / 2;
                        int chroma_h = ov_h / 2;

                        for( int ip = 1; ip <= 2; ip++ )
                        {
                            plane_t *p_c = &p_outpic->p[ip];
                            const uint8_t *ov_c = (ip == 1) ? ov->p_u : ov->p_v;

                            /* Save current chroma */
                            int c_size = chroma_h * p_c->i_pitch;
                            /* Reuse barrel_tmp if big enough, else skip scaling */
                            uint8_t *c_save = NULL;
                            if( c_size <= p_sys->i_barrel_tmp_size )
                            {
                                c_save = p_sys->p_barrel_tmp;
                                memcpy( c_save, p_c->p_pixels, c_size );
                            }

                            for( int y = 0; y < chroma_h && y < p_c->i_visible_lines; y++ )
                            {
                                uint8_t *out = &p_c->p_pixels[y * p_c->i_pitch];

                                int c_aa = 2; /* chroma AA (half of luma) */
                                int csw = scr_w / 2;
                                int csh = scr_h / 2;
                                int csx = scr_off_x / 2;
                                int csy = scr_off_y / 2;

                                for( int x = 0; x < chroma_w && x < p_c->i_visible_pitch; x++ )
                                {
                                    int crx = x - csx;
                                    int cry = y - csy;

                                    int cdl = -crx, cdr = crx - csw + 1;
                                    int cdt = -cry, cdb = cry - csh + 1;
                                    int ced = cdl;
                                    if( cdr > ced ) ced = cdr;
                                    if( cdt > ced ) ced = cdt;
                                    if( cdb > ced ) ced = cdb;

                                    if( ced >= c_aa )
                                    {
                                        out[x] = ov_c[y * chroma_w + x];
                                    }
                                    else if( ced <= -c_aa && c_save )
                                    {
                                        int cvx = crx * chroma_w / csw;
                                        int cvy = cry * chroma_h / csh;
                                        if( cvx >= chroma_w ) cvx = chroma_w - 1;
                                        if( cvy >= chroma_h ) cvy = chroma_h - 1;
                                        out[x] = c_save[ cvy * p_c->i_pitch + cvx ];
                                    }
                                    else if( c_save )
                                    {
                                        int cvx = crx > 0 ? crx * chroma_w / csw : 0;
                                        int cvy = cry > 0 ? cry * chroma_h / csh : 0;
                                        if( cvx >= chroma_w ) cvx = chroma_w - 1;
                                        if( cvy >= chroma_h ) cvy = chroma_h - 1;
                                        uint8_t cv = c_save[ cvy * p_c->i_pitch + cvx ];
                                        int ca = ( ced + c_aa ) * 255 / ( c_aa * 2 );
                                        if( ca < 0 ) ca = 0;
                                        if( ca > 255 ) ca = 255;
                                        out[x] = (uint8_t)(
                                            ( (int)cv * (255 - ca)
                                            + (int)ov_c[y*chroma_w+x] * ca ) / 255 );
                                    }
                                    else
                                    {
                                        out[x] = ov_c[y * chroma_w + x];
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        free( psz_overlay );
    }

    /* =========================================================
     * PASS 6: Glass reflection — applied AFTER overlay compositing
     * so it illuminates BOTH the video AND the TV frame/bezel.
     *
     * This is the Mega Bezel approach: the reflection is an additive
     * light layer on top of the fully composited output. It brightens
     * the TV frame edges where screen light would reflect off the glass
     * and plastic bezel.
     * ========================================================= */
    if( i_reflect > 0 )
    {
        const int rw = p_outpic->p[Y_PLANE].i_visible_pitch;
        const int rh = p_outpic->p[Y_PLANE].i_visible_lines;
        plane_t *p_y = &p_outpic->p[Y_PLANE];

        for( int y = 0; y < rh; y++ )
        {
            uint8_t *row = &p_y->p_pixels[y * p_y->i_pitch];
            for( int x = 0; x < rw; x++ )
            {
                int r = reflect_value( x, y, rw, rh, i_reflect );
                if( r > 0 )
                {
                    int result = (int)row[x] + r;
                    row[x] = (uint8_t)( result > 255 ? 255 : result );
                }
            }
        }
    }

    return CopyInfoAndRelease( p_outpic, p_pic );
}
