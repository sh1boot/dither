#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include <arm_neon.h>
#include <sndfile.h>

#if !defined(M_PI)
#define M_PI 3.14159265358979323846264338
#endif

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

#define RANDBITS() ((rand() ^ (rand() << 10) ^ (rand() >> 10)) & 0xffff)
void convert_c(int16_t *out, float const *in, size_t count, int fixbits, int dtype)
{
    static int r1 = 0;
    int r0 = 0;
    size_t i;
    float scale = (1ull << (15 + fixbits));
    int fixrnd = 1 << (fixbits - 1);

    for (i = 0; i < count; i++)
    {
        double s = in[i];

        s *= scale;
        if (s < LONG_MIN) s = LONG_MIN;
        if (s > LONG_MAX) s = LONG_MAX;

        long si = (long)s;

        int dither;
        r0 = r1;
        r1 = RANDBITS();
        switch (dtype)
        {
        default: dither = 0;            break;
        case 1:
            r0 = RANDBITS();
            /*@FALLTHROUGH@*/
        case 2: dither = r1 - r0;       break;
        }

        si = si + dither;

        si = (si + fixrnd) >> fixbits;

        if (si < SHRT_MIN) si = SHRT_MIN;
        if (si > SHRT_MAX) si = SHRT_MAX;

        out[i] = si;
    }
}

void convert(int16_t *out, float const *in, size_t count, int fixbits, int dtype, int c)
{
    if (c == 0 && dtype == 2 && fixbits == 16)
        convert16d2(out, in, count);
    else
        convert_c(out, in, count, fixbits, dtype);
}

static void mksine(float *out, size_t count, int freq, int rate, float ampl)
{
    static int off = 0;
    int o = off;
    size_t i;

    for (i = 0; i < count; i++)
        out[i] = ampl * sin(o++ * M_PI * 2.0 * freq / rate);

    off = o % freq;
}

int main(int argc, char *argv[])
{
    char const *inname = NULL;
    char const *outname = "sine.wav";
    uint32_t rate = 44100;
    uint32_t samples = rate * 15;
    double freq = 4000;
    double sin_amp = pow(10.0, -90.0 * 0.05);
    int lapbits = 0;
    int dtype = 2;
    int c = 0;
    int fixbits;
    float in[4096];
    int16_t out[4096];
    SNDFILE *infile = NULL;
    SNDFILE *outfile;
    SF_INFO sfinfo;
    int opt;
    int i;

    while ((opt = getopt(argc, argv, "i:o:r:t:f:a:b:d:c")) != -1)
        switch (opt)
        {
        case 'i': inname = optarg;                          break;
        case 'o': outname = optarg;                         break;
        case 'r': rate = atoi(optarg);                      break;
        case 't': samples = atoi(optarg) * rate;            break;
        case 'f': freq = atoi(optarg);                      break;
        case 'a': sin_amp = pow(10.0, atof(optarg) * 0.05); break;
        case 'b': lapbits = atoi(optarg);                   break;
        case 'd': dtype = atoi(optarg);                     break;
        case 'c': c = 1;                                    break;
        default:
            fprintf(stderr, "unknown option '%c'\n", opt);
            exit(EXIT_FAILURE);
        }

    if (inname)
    {
        if ((infile = sf_open(inname, SFM_WRITE, &sfinfo)) == NULL)
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
        infile = NULL;
        sfinfo.samplerate = rate;
        sfinfo.frames = samples;
        sfinfo.channels = 1;
    }

    sfinfo.format = (SF_FORMAT_WAV | SF_FORMAT_PCM_16);

    if ((outfile = sf_open(outname, SFM_WRITE, &sfinfo)) == NULL)
    {
        fprintf(stderr, "couldn't open output outfile '%s'\n", outname);
        sf_close(infile);
        exit(EXIT_FAILURE);
    }

    srand(100);

    fixbits = 16 - lapbits;

    do
    {
        if (infile == NULL)
        {
            i = samples;
            if (i > 4096) i = 4096;
            mksine(in, i, freq, rate, sin_amp);
            samples -= i;
        }
        else
            i = sf_read_float(infile, in, 4096);

        convert(out, in, i, fixbits, dtype, c);

        (void)sf_write_short(outfile, out, i);
    } while (i >= 4096);

    if (infile != NULL) sf_close(infile);
    sf_close(outfile);

    return EXIT_SUCCESS;
}
