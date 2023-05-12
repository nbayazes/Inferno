#include "pch.h"
#include "OpenSimplex2.h"

// Port of OpenSimplex2
// https://github.com/KdotJPG/OpenSimplex2/blob/master/csharp/OpenSimplex2.cs

namespace OpenSimplex2 {
    using int64 = long long;

    constexpr auto PRIME_X = 0x5205402B9270C86FL;
    constexpr auto PRIME_Y = 0x598CD327003817B5L;
    constexpr auto PRIME_Z = 0x5BCC226E9FA0BACBL;
    constexpr auto PRIME_W = 0x56CC5227E58F554BL;
    constexpr auto HASH_MULTIPLIER = 0x53A3F72DEEC546F5L;
    constexpr auto SEED_FLIP_3D = -0x52D547B2E96ED629L;
    constexpr auto SEED_OFFSET_4D = 0xE83DC3E0DA7164DL;

    constexpr auto ROOT2OVER2 = 0.7071067811865476;
    constexpr auto SKEW_2D = 0.366025403784439;
    constexpr auto UNSKEW_2D = -0.21132486540518713;

    constexpr auto ROOT3OVER3 = 0.577350269189626;
    constexpr auto FALLBACK_ROTATE_3D = 2.0 / 3.0;
    constexpr auto ROTATE_3D_ORTHOGONALIZER = UNSKEW_2D;

    constexpr auto SKEW_4D = -0.138196601125011f;
    constexpr auto UNSKEW_4D = 0.309016994374947f;
    constexpr auto LATTICE_STEP_4D = 0.2f;

    constexpr auto N_GRADS_2D_EXPONENT = 7;
    constexpr auto N_GRADS_3D_EXPONENT = 8;
    constexpr auto N_GRADS_4D_EXPONENT = 9;
    constexpr auto N_GRADS_2D = 1 << N_GRADS_2D_EXPONENT;
    constexpr auto N_GRADS_3D = 1 << N_GRADS_3D_EXPONENT;
    constexpr auto N_GRADS_4D = 1 << N_GRADS_4D_EXPONENT;

    constexpr auto NORMALIZER_2D = 0.01001634121365712;
    constexpr auto NORMALIZER_3D = 0.07969837668935331;
    constexpr auto NORMALIZER_4D = 0.0220065933241897;

    constexpr auto RSQUARED_2D = 0.5f;
    constexpr auto RSQUARED_3D = 0.6f;
    constexpr auto RSQUARED_4D = 0.6f;

    /*
     * Gradients
     */

    std::array<float, N_GRADS_2D * 2> GRADIENTS_2D;

    std::array grad2 = {
         0.38268343236509f,   0.923879532511287f,
         0.923879532511287f,  0.38268343236509f,
         0.923879532511287f, -0.38268343236509f,
         0.38268343236509f,  -0.923879532511287f,
        -0.38268343236509f,  -0.923879532511287f,
        -0.923879532511287f, -0.38268343236509f,
        -0.923879532511287f,  0.38268343236509f,
        -0.38268343236509f,   0.923879532511287f,
        //-------------------------------------//
         0.130526192220052f,  0.99144486137381f,
         0.608761429008721f,  0.793353340291235f,
         0.793353340291235f,  0.608761429008721f,
         0.99144486137381f,   0.130526192220051f,
         0.99144486137381f,  -0.130526192220051f,
         0.793353340291235f, -0.60876142900872f,
         0.608761429008721f, -0.793353340291235f,
         0.130526192220052f, -0.99144486137381f,
        -0.130526192220052f, -0.99144486137381f,
        -0.608761429008721f, -0.793353340291235f,
        -0.793353340291235f, -0.608761429008721f,
        -0.99144486137381f,  -0.130526192220052f,
        -0.99144486137381f,   0.130526192220051f,
        -0.793353340291235f,  0.608761429008721f,
        -0.608761429008721f,  0.793353340291235f,
        -0.130526192220052f,  0.99144486137381f,
    };


    std::array<float, N_GRADS_3D * 4> GRADIENTS_3D;

    std::array grad3 = {
         2.22474487139f,       2.22474487139f,      -1.0f,                 0.0f,
         2.22474487139f,       2.22474487139f,       1.0f,                 0.0f,
         3.0862664687972017f,  1.1721513422464978f,  0.0f,                 0.0f,
         1.1721513422464978f,  3.0862664687972017f,  0.0f,                 0.0f,
        -2.22474487139f,       2.22474487139f,      -1.0f,                 0.0f,
        -2.22474487139f,       2.22474487139f,       1.0f,                 0.0f,
        -1.1721513422464978f,  3.0862664687972017f,  0.0f,                 0.0f,
        -3.0862664687972017f,  1.1721513422464978f,  0.0f,                 0.0f,
        -1.0f,                -2.22474487139f,      -2.22474487139f,       0.0f,
         1.0f,                -2.22474487139f,      -2.22474487139f,       0.0f,
         0.0f,                -3.0862664687972017f, -1.1721513422464978f,  0.0f,
         0.0f,                -1.1721513422464978f, -3.0862664687972017f,  0.0f,
        -1.0f,                -2.22474487139f,       2.22474487139f,       0.0f,
         1.0f,                -2.22474487139f,       2.22474487139f,       0.0f,
         0.0f,                -1.1721513422464978f,  3.0862664687972017f,  0.0f,
         0.0f,                -3.0862664687972017f,  1.1721513422464978f,  0.0f,
         //--------------------------------------------------------------------//
         -2.22474487139f,      -2.22474487139f,      -1.0f,                 0.0f,
         -2.22474487139f,      -2.22474487139f,       1.0f,                 0.0f,
         -3.0862664687972017f, -1.1721513422464978f,  0.0f,                 0.0f,
         -1.1721513422464978f, -3.0862664687972017f,  0.0f,                 0.0f,
         -2.22474487139f,      -1.0f,                -2.22474487139f,       0.0f,
         -2.22474487139f,       1.0f,                -2.22474487139f,       0.0f,
         -1.1721513422464978f,  0.0f,                -3.0862664687972017f,  0.0f,
         -3.0862664687972017f,  0.0f,                -1.1721513422464978f,  0.0f,
         -2.22474487139f,      -1.0f,                 2.22474487139f,       0.0f,
         -2.22474487139f,       1.0f,                 2.22474487139f,       0.0f,
         -3.0862664687972017f,  0.0f,                 1.1721513422464978f,  0.0f,
         -1.1721513422464978f,  0.0f,                 3.0862664687972017f,  0.0f,
         -1.0f,                 2.22474487139f,      -2.22474487139f,       0.0f,
          1.0f,                 2.22474487139f,      -2.22474487139f,       0.0f,
          0.0f,                 1.1721513422464978f, -3.0862664687972017f,  0.0f,
          0.0f,                 3.0862664687972017f, -1.1721513422464978f,  0.0f,
         -1.0f,                 2.22474487139f,       2.22474487139f,       0.0f,
          1.0f,                 2.22474487139f,       2.22474487139f,       0.0f,
          0.0f,                 3.0862664687972017f,  1.1721513422464978f,  0.0f,
          0.0f,                 1.1721513422464978f,  3.0862664687972017f,  0.0f,
          2.22474487139f,      -2.22474487139f,      -1.0f,                 0.0f,
          2.22474487139f,      -2.22474487139f,       1.0f,                 0.0f,
          1.1721513422464978f, -3.0862664687972017f,  0.0f,                 0.0f,
          3.0862664687972017f, -1.1721513422464978f,  0.0f,                 0.0f,
          2.22474487139f,      -1.0f,                -2.22474487139f,       0.0f,
          2.22474487139f,       1.0f,                -2.22474487139f,       0.0f,
          3.0862664687972017f,  0.0f,                -1.1721513422464978f,  0.0f,
          1.1721513422464978f,  0.0f,                -3.0862664687972017f,  0.0f,
          2.22474487139f,      -1.0f,                 2.22474487139f,       0.0f,
          2.22474487139f,       1.0f,                 2.22474487139f,       0.0f,
          1.1721513422464978f,  0.0f,                 3.0862664687972017f,  0.0f,
          3.0862664687972017f,  0.0f,                 1.1721513422464978f,  0.0f,
    };

    void Init() {
        for (int i = 0; i < grad2.size(); i++) {
            grad2[i] = (float)(grad2[i] / NORMALIZER_2D);
        }

        for (int i = 0, j = 0; i < GRADIENTS_2D.size(); i++, j++) {
            if (j == grad2.size()) j = 0;
            GRADIENTS_2D[i] = grad2[j];
        }

        for (int i = 0; i < grad3.size(); i++) {
            grad3[i] = (float)(grad3[i] / NORMALIZER_3D);
        }
        for (int i = 0, j = 0; i < GRADIENTS_3D.size(); i++, j++) {
            if (j == grad3.size()) j = 0;
            GRADIENTS_3D[i] = grad3[j];
        }
    }

    /*
     * Utility
     */

    float Grad(int64 seed, int64 xsvp, int64 ysvp, float dx, float dy) {
        int64 hash = seed ^ xsvp ^ ysvp;
        hash *= HASH_MULTIPLIER;
        hash ^= hash >> (64 - N_GRADS_2D_EXPONENT + 1);
        int gi = (int)hash & ((N_GRADS_2D - 1) << 1);
        return GRADIENTS_2D[gi | 0] * dx + GRADIENTS_2D[gi | 1] * dy;
    }

    float Grad(int64 seed, int64 xrvp, int64 yrvp, int64 zrvp, float dx, float dy, float dz) {
        int64 hash = (seed ^ xrvp) ^ (yrvp ^ zrvp);
        hash *= HASH_MULTIPLIER;
        hash ^= hash >> (64 - N_GRADS_3D_EXPONENT + 2);
        int gi = (int)hash & ((N_GRADS_3D - 1) << 2);
        return GRADIENTS_3D[gi | 0] * dx + GRADIENTS_3D[gi | 1] * dy + GRADIENTS_3D[gi | 2] * dz;
    }

    //float Grad(long seed, long xsvp, long ysvp, long zsvp, long wsvp, float dx, float dy, float dz, float dw) {
    //    long hash = seed ^ (xsvp ^ ysvp) ^ (zsvp ^ wsvp);
    //    hash *= HASH_MULTIPLIER;
    //    hash ^= hash >> (64 - N_GRADS_4D_EXPONENT + 2);
    //    int gi = (int)hash & ((N_GRADS_4D - 1) << 2);
    //    return (GRADIENTS_4D[gi | 0] * dx + GRADIENTS_4D[gi | 1] * dy) + (GRADIENTS_4D[gi | 2] * dz + GRADIENTS_4D[gi | 3] * dw);
    //}

    constexpr int FastFloor(double x) {
        int xi = (int)x;
        return x < xi ? xi - 1 : xi;
    }

    constexpr int FastRound(double x) {
        return x < 0 ? (int)(x - 0.5) : (int)(x + 0.5);
    }

    /**
     * 2D Simplex noise base.
     */
    float Noise2_UnskewedBase(int64 seed, double xs, double ys) {
        // Get base points and offsets.
        int64 xsb = FastFloor(xs), ysb = FastFloor(ys);
        auto xi = (float)(xs - xsb), yi = (float)(ys - ysb);

        // Prime pre-multiplication for hash.
        int64 xsbp = xsb * PRIME_X, ysbp = ysb * PRIME_Y;

        // Unskew.
        float t = (xi + yi) * (float)UNSKEW_2D;
        float dx0 = xi + t, dy0 = yi + t;

        // First vertex.
        float value = 0;
        float a0 = RSQUARED_2D - dx0 * dx0 - dy0 * dy0;
        if (a0 > 0) {
            value = (a0 * a0) * (a0 * a0) * Grad(seed, xsbp, ysbp, dx0, dy0);
        }

        // Second vertex.
        float a1 = (float)(2 * (1 + 2 * UNSKEW_2D) * (1 / UNSKEW_2D + 2)) * t + ((float)(-2 * (1 + 2 * UNSKEW_2D) * (1 + 2 * UNSKEW_2D)) + a0);
        if (a1 > 0) {
            float dx1 = dx0 - (float)(1 + 2 * UNSKEW_2D);
            float dy1 = dy0 - (float)(1 + 2 * UNSKEW_2D);
            value += (a1 * a1) * (a1 * a1) * Grad(seed, xsbp + PRIME_X, ysbp + PRIME_Y, dx1, dy1);
        }

        // Third vertex.
        if (dy0 > dx0) {
            float dx2 = dx0 - (float)UNSKEW_2D;
            float dy2 = dy0 - (float)(UNSKEW_2D + 1);
            float a2 = RSQUARED_2D - dx2 * dx2 - dy2 * dy2;
            if (a2 > 0) {
                value += (a2 * a2) * (a2 * a2) * Grad(seed, xsbp, ysbp + PRIME_Y, dx2, dy2);
            }
        }
        else {
            float dx2 = dx0 - (float)(UNSKEW_2D + 1);
            float dy2 = dy0 - (float)UNSKEW_2D;
            float a2 = RSQUARED_2D - dx2 * dx2 - dy2 * dy2;
            if (a2 > 0) {
                value += (a2 * a2) * (a2 * a2) * Grad(seed, xsbp + PRIME_X, ysbp, dx2, dy2);
            }
        }

        return value;
    }

    /*
    * Noise Evaluators
    */

    // 2D Simplex noise, standard lattice orientation.
    float Noise2(int64 seed, double x, double y) {
        // Get points for A2* lattice
        double s = SKEW_2D * (x + y);
        double xs = x + s, ys = y + s;
        return Noise2_UnskewedBase(seed, xs, ys);
    }

    /**
     * 2D Simplex noise, with Y pointing down the main diagonal.
     * Might be better for a 2D sandbox style game, where Y is vertical.
     * Probably slightly less optimal for heightmaps or continent maps,
     * unless your map is centered around an equator. It's a subtle
     * difference, but the option is here to make it an easy choice.
     */
    float Noise2_ImproveX(int64 seed, double x, double y) {
        // Skew transform and rotation baked into one.
        double xx = x * ROOT2OVER2;
        double yy = y * (ROOT2OVER2 * (1 + 2 * SKEW_2D));
        return Noise2_UnskewedBase(seed, yy + xx, yy - xx);
    }

    // Generate overlapping cubic lattices for 3D OpenSimplex2 noise.
    float Noise3_UnrotatedBase(int64 seed, double xr, double yr, double zr) {
        // Get base points and offsets.
        int xrb = FastRound(xr), yrb = FastRound(yr), zrb = FastRound(zr);
        auto xri = (float)(xr - xrb), yri = (float)(yr - yrb), zri = (float)(zr - zrb);

        // -1 if positive, 1 if negative.
        int xNSign = (int)(-1.0f - xri) | 1, yNSign = (int)(-1.0f - yri) | 1, zNSign = (int)(-1.0f - zri) | 1;

        // Compute absolute values, using the above as a shortcut. This was faster in my tests for some reason.
        float ax0 = xNSign * -xri, ay0 = yNSign * -yri, az0 = zNSign * -zri;

        // Prime pre-multiplication for hash.
        int64 xrbp = xrb * PRIME_X, yrbp = yrb * PRIME_Y, zrbp = zrb * PRIME_Z;

        // Loop: Pick an edge on each lattice copy.
        float value = 0;
        float a = (RSQUARED_3D - xri * xri) - (yri * yri + zri * zri);
        for (int l = 0; ; l++) {

            // Closest point on cube.
            if (a > 0) {
                value += (a * a) * (a * a) * Grad(seed, xrbp, yrbp, zrbp, xri, yri, zri);
            }

            // Second-closest point.
            if (ax0 >= ay0 && ax0 >= az0) {
                float b = a + ax0 + ax0;
                if (b > 1) {
                    b -= 1;
                    value += (b * b) * (b * b) * Grad(seed, xrbp - xNSign * PRIME_X, yrbp, zrbp, xri + xNSign, yri, zri);
                }
            }
            else if (ay0 > ax0 && ay0 >= az0) {
                float b = a + ay0 + ay0;
                if (b > 1) {
                    b -= 1;
                    value += (b * b) * (b * b) * Grad(seed, xrbp, yrbp - yNSign * PRIME_Y, zrbp, xri, yri + yNSign, zri);
                }
            }
            else {
                float b = a + az0 + az0;
                if (b > 1) {
                    b -= 1;
                    value += (b * b) * (b * b) * Grad(seed, xrbp, yrbp, zrbp - zNSign * PRIME_Z, xri, yri, zri + zNSign);
                }
            }

            // Break from loop if we're done, skipping updates below.
            if (l == 1) break;

            // Update absolute value.
            ax0 = 0.5f - ax0;
            ay0 = 0.5f - ay0;
            az0 = 0.5f - az0;

            // Update relative coordinate.
            xri = xNSign * ax0;
            yri = yNSign * ay0;
            zri = zNSign * az0;

            // Update falloff.
            a += (0.75f - ax0) - (ay0 + az0);

            // Update prime for hash.
            xrbp += (xNSign >> 1) & PRIME_X;
            yrbp += (yNSign >> 1) & PRIME_Y;
            zrbp += (zNSign >> 1) & PRIME_Z;

            // Update the reverse sign indicators.
            xNSign = -xNSign;
            yNSign = -yNSign;
            zNSign = -zNSign;

            // And finally update the seed for the other lattice copy.
            seed ^= SEED_FLIP_3D;
        }

        return value;
    }

    /**
     * 3D OpenSimplex2 noise, with better visual isotropy in (X, Y).
     * Recommended for 3D terrain and time-varied animations.
     * The Z coordinate should always be the "different" coordinate in whatever your use case is.
     * If Y is vertical in world coordinates, call Noise3_ImproveXZ(x, z, Y) or use noise3_XZBeforeY.
     * If Z is vertical in world coordinates, call Noise3_ImproveXZ(x, y, Z).
     * For a time varied animation, call Noise3_ImproveXY(x, y, T).
     */
    float Noise3_ImproveXY(int64 seed, double x, double y, double z) {
        // Re-orient the cubic lattices without skewing, so Z points up the main lattice diagonal,
        // and the planes formed by XY are moved far out of alignment with the cube faces.
        // Orthonormal rotation. Not a skew transform.
        double xy = x + y;
        double s2 = xy * ROTATE_3D_ORTHOGONALIZER;
        double zz = z * ROOT3OVER3;
        double xr = x + s2 + zz;
        double yr = y + s2 + zz;
        double zr = xy * -ROOT3OVER3 + zz;

        // Evaluate both lattices to form a BCC lattice.
        return Noise3_UnrotatedBase(seed, xr, yr, zr);
    }

    /**
     * 3D OpenSimplex2 noise, with better visual isotropy in (X, Z).
     * Recommended for 3D terrain and time-varied animations.
     * The Y coordinate should always be the "different" coordinate in whatever your use case is.
     * If Y is vertical in world coordinates, call Noise3_ImproveXZ(x, Y, z).
     * If Z is vertical in world coordinates, call Noise3_ImproveXZ(x, Z, y) or use Noise3_ImproveXY.
     * For a time varied animation, call Noise3_ImproveXZ(x, T, y) or use Noise3_ImproveXY.
     */
    float Noise3_ImproveXZ(int64 seed, double x, double y, double z) {
        // Re-orient the cubic lattices without skewing, so Y points up the main lattice diagonal,
        // and the planes formed by XZ are moved far out of alignment with the cube faces.
        // Orthonormal rotation. Not a skew transform.
        double xz = x + z;
        double s2 = xz * ROTATE_3D_ORTHOGONALIZER;
        double yy = y * ROOT3OVER3;
        double xr = x + s2 + yy;
        double zr = z + s2 + yy;
        double yr = xz * -ROOT3OVER3 + yy;

        // Evaluate both lattices to form a BCC lattice.
        return Noise3_UnrotatedBase(seed, xr, yr, zr);
    }

    /**
     * 3D OpenSimplex2 noise, fallback rotation option
     * Use Noise3_ImproveXY or Noise3_ImproveXZ instead, wherever appropriate.
     * They have less diagonal bias. This function's best use is as a fallback.
     */
    float Noise3_Fallback(int64 seed, double x, double y, double z) {
        // Re-orient the cubic lattices via rotation, to produce a familiar look.
        // Orthonormal rotation. Not a skew transform.
        double r = FALLBACK_ROTATE_3D * (x + y + z);
        double xr = r - x, yr = r - y, zr = r - z;

        // Evaluate both lattices to form a BCC lattice.
        return Noise3_UnrotatedBase(seed, xr, yr, zr);
    }
}
