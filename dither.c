#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include <sndfile.h>

#if defined(__arm__)
#include <arm_neon.h>
#endif

#if !defined(M_PI)
#define M_PI 3.14159265358979323846264338
#endif


#if defined(__arm__)
static inline
void convert16d2(int16_t *out, float const *in, size_t count)
{
    static const uint16_t atab[4] = {
        65184, 64545, 64455, 64860,
    };
    static uint16_t xstore[4] = { 1, 2, 3, 4 };
    static uint16_t cstore[4] = { 0 };
    uint16x4_t x = vld1_u16(xstore), c = vld1_u16(cstore);
    uint16x4_t a;
    size_t i;

    a = vld1_u16(atab);

    for (i = 0; i < count; i += 4)
    {
        float32x4_t inp = vld1q_f32(in + i);

        uint32x4_t t;
        uint16x4_t old = x;

        t = vmull_u16(x, a);
        t = vaddw_u16(t, c);
        uint16x4x2_t uz = vuzp_u16(vreinterpret_u16_u32(vget_low_u32(t)), vreinterpret_u16_u32(vget_high_u32(t)));
        x = uz.val[0];
        c = uz.val[1];

        old = vext_u16(old, x, 3);

        uint32x4_t dither = vsubl_u16(x, old);

        int32x4_t s = vcvtq_n_s32_f32(inp, 31);
        s = vqaddq_s32(s, vreinterpretq_s32_u32(dither));

        int16x4_t outp;
        outp = vqrshrn_n_s32(s, 16);

        vst1_s16(out + i, outp);
    }
    vst1_u16(xstore, x);
    vst1_u16(cstore, c);
}
#endif

static float coeff[16] = { 1.0, 0.0 };

static int32_t filter(int32_t *ptr, int type)
{
    switch (type)
    {
    default: break;
    case 1: return ptr[0];
    case 2: return ptr[0] - ptr[1];
    case 3: return ptr[0] + ((ptr[2] - ptr[1]) >> 1);
    case 4: return rint(ptr[0] *  2.033
                      + ptr[1] * -2.165
                      + ptr[2] *  1.959
                      + ptr[3] * -1.590
                      + ptr[4] *  0.6149);
    case 5: return ptr[1] - ptr[0];
    case 6: return ptr[1] - ptr[0] - ptr[2];
    case 7: return ptr[1] - ptr[0] + ((ptr[3] - ptr[2]) >> 1);
    case 8: return rint(ptr[0] * -1.0
                      + ptr[1] *  2.033
                      + ptr[2] * -2.165
                      + ptr[3] *  1.959
                      + ptr[4] * -1.590
                      + ptr[5] *  0.6149);
    case 255: return rint(ptr[0] * coeff[0]
                        + ptr[1] * coeff[1]
                        + ptr[2] * coeff[2]
                        + ptr[3] * coeff[3]
                        + ptr[4] * coeff[4]
                        + ptr[5] * coeff[5]
                        + ptr[6] * coeff[6]
                        + ptr[7] * coeff[7]
                        + ptr[8] * coeff[8]
                        + ptr[9] * coeff[9]
                        + ptr[10] * coeff[10]
                        + ptr[11] * coeff[11]
                        + ptr[12] * coeff[12]
                        + ptr[13] * coeff[13]
                        + ptr[14] * coeff[14]
                        + ptr[15] * coeff[15]);
    }
    return 0;
}

#define RANDBITS() ((rand() ^ (rand() << 10) ^ (rand() >> 10)) & 0xffff)
void convert_c(int16_t *out, float const *in, size_t count, int fixbits, int dtype, int stype)
{
    static int32_t randwin[32];
    static int32_t errwin[32];
    static int iwin = 0;
    size_t i;
    float scale = (1ull << (15 + fixbits));
    int fixrnd = 1 << (fixbits - 1);
    int rtype = dtype >> 8;

    dtype &= 255;

    for (i = 0; i < count; i++)
    {
        int dither = filter(randwin + iwin, dtype);
        double s = in[i] * scale + filter(errwin + iwin, stype);

        if (s < LONG_MIN) s = LONG_MIN;
        if (s > LONG_MAX) s = LONG_MAX;

        long si = ((long)s + dither + fixrnd) >> fixbits;

        iwin = (iwin - 1) & 15;
        randwin[iwin] = randwin[iwin+16] = RANDBITS() - (rtype ? RANDBITS() : 0x8000);
        errwin[iwin] = errwin[iwin+16] = (long)s - (si << fixbits);

        if (si < SHRT_MIN) si = SHRT_MIN;
        if (si > SHRT_MAX) si = SHRT_MAX;

        out[i] = si;
    }
}

void convert(int16_t *out, float const *in, size_t count, int fixbits, int dtype, int stype, int c)
{
#if defined(__arm__)
    if (c == 0 && dtype == 2 && stype == 0 && fixbits == 16)
        convert16d2(out, in, count);
    else
#endif
        convert_c(out, in, count, fixbits, dtype, stype);
}

static void mksine(float *out, size_t count, double w0, double w1, double t_s, float ampl)
{
    static uint64_t off = 0;
    double w1_0 = (w1 - w0) * 0.5;
    uint64_t o = off;
    size_t i;

    for (i = 0; i < count; i++)
    {
        double t = o++ * t_s;
        out[i] = ampl * sin(w0 * t + w1_0 * t * t);
    }

    off = o;
}

int main(int argc, char *argv[])
{
    char const *inname = NULL;
    char const *outname = "sine.wav";
    uint32_t rate = 44100;
    uint32_t duration = 15;
    double w0 = 500 * M_PI * 2.0;
    double sin_ramp = 1000 * M_PI * 2.0;
    double sin_amp = pow(10.0, -90.0 * 0.05);
    double w1;
    double t_s;
    int lapbits = 0;
    int dtype = 2;
    int stype = 0;
    int c = 0;
    int floatout = 0;
    uint32_t samples;
    int fixbits;
    float in[4096];
    int16_t out[4096];
    SNDFILE *infile = NULL;
    SNDFILE *outfile;
    SF_INFO sfinfo;
    int opt;
    int i;

    while ((opt = getopt(argc, argv, "i:o:r:t:f:m:a:b:d:s:P:cF")) != -1)
        switch (opt)
        {
        case 'i': inname = optarg;                          break;
        case 'o': outname = optarg;                         break;
        case 'r': rate = atoi(optarg);                      break;
        case 't': duration = atoi(optarg);                  break;
        case 'f': w0 = atoi(optarg) * M_PI * 2.0;           break;
        case 'm': sin_ramp = atof(optarg) * M_PI * 2.0;     break;
        case 'a': sin_amp = pow(10.0, atof(optarg) * 0.05); break;
        case 'b': lapbits = atoi(optarg);                   break;
        case 'd': dtype = atoi(optarg);                     break;
        case 's': stype = atoi(optarg);                     break;
        case 'P':
                for (i = 0; i < 16; i++)
                    if (optarg != NULL)
                    {
                        coeff[i] = atof(optarg);
                        optarg = strchr(optarg, ',');
                        if (optarg != NULL) optarg++;
                    }
                    else coeff[i] = 0.0f;
                break;
        case 'c': c = 1;                                    break;
        case 'F': floatout = 1;                             break;
        default:
            fprintf(stderr, "unknown option '%c'\n", opt);
            exit(EXIT_FAILURE);
        }

    if (inname)
    {
        if ((infile = sf_open(inname, SFM_READ, &sfinfo)) == NULL)
        {
            fprintf(stderr, "couldn't open input outfile '%s'\n", inname);
            exit(EXIT_FAILURE);
        }
        if (sfinfo.channels != 1)
        {
            fprintf(stderr, "%d channel files not supported.\n", sfinfo.channels);
            sf_close(infile);
            exit(EXIT_FAILURE);
        }
        rate = sfinfo.samplerate;
        samples = sfinfo.frames;
    }
    else
    {
        samples = rate * duration;
        infile = NULL;
        sfinfo.samplerate = rate;
        sfinfo.frames = samples;
        sfinfo.channels = 1;
    }

    if (floatout)
        sfinfo.format = (SF_FORMAT_WAV | SF_FORMAT_FLOAT);
    else
        sfinfo.format = (SF_FORMAT_WAV | SF_FORMAT_PCM_16);

    if ((outfile = sf_open(outname, SFM_WRITE, &sfinfo)) == NULL)
    {
        fprintf(stderr, "couldn't open output outfile '%s'\n", outname);
        sf_close(infile);
        exit(EXIT_FAILURE);
    }

    srand(100);

    fixbits = 16 - lapbits;
    w1 = w0 + sin_ramp;
    t_s = 1.0f / rate;

    do
    {
        if (infile == NULL)
        {
            i = samples;
            if (i > 4096) i = 4096;
            mksine(in, i, w0, w1, t_s, sin_amp);
        }
        else
            i = sf_read_float(infile, in, 4096);

        samples -= i;

        if (floatout)
            (void)sf_write_float(outfile, in, i);
        else
        {
            convert(out, in, i, fixbits, dtype, stype, c);

            (void)sf_write_short(outfile, out, i);
        }
    } while (i >= 4096);

    if (infile != NULL) sf_close(infile);
    sf_close(outfile);

    return EXIT_SUCCESS;
}
