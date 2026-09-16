// lut-parity — quantifies the divergence between the two LUT paths (W11 decision).
//
//   g++ -O2 -o /tmp/lut-parity tools/analysis/lut-parity.cpp
//   /tmp/lut-parity tests/golden/media/lut_example.cube
//
// The native effect (src/effects/ColorMap.cpp) resamples any LUT larger than 17^3
// down to 17^3 for L1 cache friendliness, then applies trilinear. The WASM path the
// web front end runs (image-processing-lib/src/ColorGrading/LutApply.cpp) keeps the
// LUT at its native cube size and applies trilinear or tetrahedral, chosen by the
// caller. This walks the 8-bit cube and reports how far apart they land.
//
// Standalone on purpose: no OpenCV, no Skia, no build-system entanglement, so it
// still runs after either implementation moves. resampleLut3D is copied verbatim
// from ColorGradingCore.cpp — keep it that way or the measurement stops meaning
// anything.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ── verbatim from ColorGradingCore.cpp ──────────────────────────────────────
static void resampleLut3D(const float* srcLut, int srcSize, float* dstLut, int dstSize) {
    const float srcSizeM1 = (float)(srcSize - 1);
    const float stepVal = 1.0f / (float)(dstSize - 1);
    for (int ib = 0; ib < dstSize; ib++)
    for (int ig = 0; ig < dstSize; ig++)
    for (int ir = 0; ir < dstSize; ir++) {
        float rf = (float)ir * stepVal * srcSizeM1;
        float gf = (float)ig * stepVal * srcSizeM1;
        float bf = (float)ib * stepVal * srcSizeM1;
        int r0 = (int)rf, r1 = std::min(r0 + 1, srcSize - 1);
        int g0 = (int)gf, g1 = std::min(g0 + 1, srcSize - 1);
        int b0 = (int)bf, b1 = std::min(b0 + 1, srcSize - 1);
        float dr = rf - r0, idr = 1.0f - dr;
        float dg = gf - g0, idg = 1.0f - dg;
        float db = bf - b0, idb = 1.0f - db;
        const float* p000 = srcLut + ((b0*srcSize+g0)*srcSize+r0)*3;
        const float* p100 = srcLut + ((b0*srcSize+g0)*srcSize+r1)*3;
        const float* p010 = srcLut + ((b0*srcSize+g1)*srcSize+r0)*3;
        const float* p110 = srcLut + ((b0*srcSize+g1)*srcSize+r1)*3;
        const float* p001 = srcLut + ((b1*srcSize+g0)*srcSize+r0)*3;
        const float* p101 = srcLut + ((b1*srcSize+g0)*srcSize+r1)*3;
        const float* p011 = srcLut + ((b1*srcSize+g1)*srcSize+r0)*3;
        const float* p111 = srcLut + ((b1*srcSize+g1)*srcSize+r1)*3;
        int dstIdx = (ib*dstSize*dstSize + ig*dstSize + ir) * 3;
        for (int ch = 0; ch < 3; ch++) {
            float c00 = p000[ch]*idr + p100[ch]*dr;
            float c01 = p001[ch]*idr + p101[ch]*dr;
            float c10 = p010[ch]*idr + p110[ch]*dr;
            float c11 = p011[ch]*idr + p111[ch]*dr;
            float c0  = c00*idg + c10*dg;
            float c1  = c01*idg + c11*dg;
            dstLut[dstIdx + ch] = c0*idb + c1*db;
        }
    }
}

struct Lut { int n = 0; std::vector<float> d; };

static Lut parseCube(const char* path) {
    Lut l; std::ifstream f(path); std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line); std::string tok; is >> tok;
        if (tok == "LUT_3D_SIZE") { is >> l.n; continue; }
        if (!tok.empty() && (std::isalpha((unsigned char)tok[0]) || tok[0] == '"')) continue;
        try {
            float r = std::stof(tok), g, b;
            if (is >> g >> b) { l.d.push_back(r); l.d.push_back(g); l.d.push_back(b); }
        } catch (...) {}
    }
    return l;
}

static void trilinear(const float* lut, int N, float r, float g, float b, float* out) {
    float rf = r*(N-1), gf = g*(N-1), bf = b*(N-1);
    int r0 = (int)rf, r1 = std::min(r0+1, N-1);
    int g0 = (int)gf, g1 = std::min(g0+1, N-1);
    int b0 = (int)bf, b1 = std::min(b0+1, N-1);
    float dr = rf-r0, idr = 1-dr, dg = gf-g0, idg = 1-dg, db = bf-b0, idb = 1-db;
    auto P = [&](int bi,int gi,int ri){ return lut + ((bi*N+gi)*N+ri)*3; };
    const float *p000=P(b0,g0,r0),*p100=P(b0,g0,r1),*p010=P(b0,g1,r0),*p110=P(b0,g1,r1);
    const float *p001=P(b1,g0,r0),*p101=P(b1,g0,r1),*p011=P(b1,g1,r0),*p111=P(b1,g1,r1);
    for (int c = 0; c < 3; c++) {
        float c00=p000[c]*idr+p100[c]*dr, c01=p001[c]*idr+p101[c]*dr;
        float c10=p010[c]*idr+p110[c]*dr, c11=p011[c]*idr+p111[c]*dr;
        out[c] = (c00*idg+c10*dg)*idb + (c01*idg+c11*dg)*db;
    }
}

// Tetrahedral, as LutApply.cpp's applyTetrahedralScalar does it.
static void tetrahedral(const float* lut, int N, float r, float g, float b, float* out) {
    float rf = r*(N-1), gf = g*(N-1), bf = b*(N-1);
    int r0 = (int)rf, r1 = std::min(r0+1, N-1);
    int g0 = (int)gf, g1 = std::min(g0+1, N-1);
    int b0 = (int)bf, b1 = std::min(b0+1, N-1);
    float dr = rf-r0, dg = gf-g0, db = bf-b0;
    auto P = [&](int bi,int gi,int ri){ return lut + ((bi*N+gi)*N+ri)*3; };
    const float* c000 = P(b0,g0,r0); const float* c111 = P(b1,g1,r1);
    const float *v1, *v2; float w1, w2, w3;
    if (dr >= dg) {
        if (dg >= db)      { v1=P(b0,g0,r1); v2=P(b0,g1,r1); w1=dr; w2=dg; w3=db; }
        else if (dr >= db) { v1=P(b0,g0,r1); v2=P(b1,g0,r1); w1=dr; w2=db; w3=dg; }
        else               { v1=P(b1,g0,r0); v2=P(b1,g0,r1); w1=db; w2=dr; w3=dg; }
    } else {
        if (db > dg)       { v1=P(b1,g0,r0); v2=P(b1,g1,r0); w1=db; w2=dg; w3=dr; }
        else if (db > dr)  { v1=P(b0,g1,r0); v2=P(b1,g1,r0); w1=dg; w2=db; w3=dr; }
        else               { v1=P(b0,g1,r0); v2=P(b0,g1,r1); w1=dg; w2=dr; w3=db; }
    }
    for (int c = 0; c < 3; c++)
        out[c] = c000[c]*(1-w1) + v1[c]*(w1-w2) + v2[c]*(w2-w3) + c111[c]*w3;
}

struct Stat { double maxd = 0, sum = 0; long n = 0, over1 = 0, over2 = 0; };
static void acc(Stat& s, const float* a, const float* b) {
    for (int c = 0; c < 3; c++) {
        double d = std::fabs((double)a[c] - b[c]) * 255.0;
        s.maxd = std::max(s.maxd, d); s.sum += d; s.n++;
        if (d > 1.0) s.over1++;
        if (d > 2.0) s.over2++;
    }
}
static void report(const char* name, const Stat& s) {
    printf("  %-40s max=%7.3f LSB  mean=%6.3f  >1LSB=%5.2f%%  >2LSB=%5.2f%%\n",
           name, s.maxd, s.sum/s.n, 100.0*s.over1/s.n, 100.0*s.over2/s.n);
}

int main(int argc, char** argv) {
    Lut src = parseCube(argv[1]);
    int N = src.n;
    printf("%s: LUT_3D_SIZE %d (%zu entries)\n", argv[1], N, src.d.size()/3);
    if ((int)src.d.size() != N*N*N*3) { printf("  parse mismatch\n"); return 1; }

    std::vector<float> l17(17*17*17*3);
    resampleLut3D(src.d.data(), N, l17.data(), 17);

    Stat s_coarse, s_tetra, s_ct;
    const int step = 3;                      // every 3rd 8-bit level per channel
    float a[3], b[3], c[3];
    long count = 0;
    for (int r = 0; r < 256; r += step)
    for (int g = 0; g < 256; g += step)
    for (int bl = 0; bl < 256; bl += step) {
        float rf = r/255.0f, gf = g/255.0f, bf = bl/255.0f;
        trilinear(src.d.data(), N, rf, gf, bf, a);   // front end: trilinear @ native
        trilinear(l17.data(), 17, rf, gf, bf, b);    // ColorMap.cpp: trilinear @ 17
        tetrahedral(src.d.data(), N, rf, gf, bf, c); // front end: tetrahedral @ native
        acc(s_coarse, b, a); acc(s_tetra, c, a); acc(s_ct, b, c);
        count++;
    }
    printf("  sampled %ld colours (every %dth 8-bit level per channel)\n", count, step);
    report("ColorMap 17^3   vs  front end 33^3 tri", s_coarse);
    report("tetrahedral     vs  trilinear (both 33^3)", s_tetra);
    report("ColorMap 17^3   vs  front end tetrahedral", s_ct);
    return 0;
}
