#pragma once
// Memoria — «станция дрейфующей памяти». Движок без зависимостей от JUCE.
//
// Идея: одна общая память (пул сэмплов), на которой режим раскладывает свои кольцевые линии.
// Каждый проход по петле сигнал проходит цепочку деградации (срез низа, срез верха с чётной
// сатурацией, лимитер, тепло), поэтому старый материал постепенно темнеет и «испаряется».
// Смена режима не стирает память: новый режим читает то, что осталось от прежнего.
//
// Режимы: 0 Grains — гранулы над петлёй; 1 Diffuse — кольцо из длинных allpass с дрейфом;
//         2 Network — 4 перекрёстно связанные линии; 3 Tapes — две петли неравной длины.
// Размер: 0 Long, 1 Mid, 2 Short.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mem
{
constexpr double kRef = 46875.0;     // частота, под которую подобраны покадровые коэффициенты
constexpr int kMaxLines = 8;
constexpr double kPoolSeconds = 46.0;
constexpr int kGrainViz = 12;

// Коэффициент однополюсного сглаживания, заданный при kRef, пересчитанный на другую частоту.
inline float rate (double c, double sr) { return (float) (1.0 - std::pow (1.0 - c, kRef / sr)); }

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                             // [-1,1)
};

struct Params
{
    int mode = 0, size = 0;
    float decay = .6f;     // 0..1, обратная связь памяти
    float warmth = .2f;    // 0..1, сатурация
    float dynamics = .5f;  // 0..1: <.5 дакинг/наплыв, >.5 «распускающийся» компрессор, .5 — выкл
    float spread = .3f;    // разброс гранул / связь линий
    float drift = .4f;     // глубина медленных LFO
    float mix = .5f;       // сухой / мокрый
    float out = 1.0f;      // выходное усиление (линейное)
    int lowcut = 0;        // 0 выкл, 1..3 сила
    int highcut = 1;       // 0 выкл, 1..3 сила (3 — темнее всего)
    int input = 0;         // 0 оба канала, 1 только L, 2 только R
    bool freeze = false, reverse = false, mono = false;
    float layers = 0;      // 0..1: насколько слои расходятся по характеру (полоса, высота, текстура, панорама)
    int layerSeed = 0;     // 0 — базовый набор характеров, иначе случайный по номеру
    float pingpong = 0;    // 0..1: пинг-понг задержки на каждый слой
    bool blur = false;     // размыть всё в облако
    float tone = .48f;     // 0 тёмный … .5 нейтрально … 1 яркий (наклон вокруг 900 Гц)
    float depth = .4f;     // саб-слой на октаву ниже + подъём низа
};

// Характер слоя: полоса частот, сдвиг высоты, текстура
struct LayerChar { int band = 1; int semis = 0; int tex = 0; };
inline const char* bandName (int b) { static const char* n[4] = { "DARK", "MID", "BRIGHT", "AIR" }; return n[std::clamp (b, 0, 3)]; }
inline const char* texName (int t) { static const char* n[4] = { "CLEAN", "TAPE", "FOLD", "CRUSH" }; return n[std::clamp (t, 0, 3)]; }
inline LayerChar layerChar (int seed, int k)
{
    static const LayerChar def[4] = { { 0, -12, 1 }, { 1, 0, 0 }, { 2, 12, 0 }, { 3, 7, 1 } };
    if (seed <= 0) return def[k & 3];
    Rng r ((uint32_t) seed * 2654435761u + 12345u);
    for (int i = 0; i < 4; ++i) r.next();
    int perm[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; --i) std::swap (perm[i], perm[r.next() % (uint32_t) (i + 1)]);   // полосы всегда разные
    // высота зависит от полосы: тёмный слой держит низ, вверх уходят только яркие
    static const int PD[6] = { -12, -12, -24, 0, -5, -7 };
    static const int PM[6] = { 0, 0, -5, 5, 7, -12 };
    static const int PB[6] = { 0, 12, 12, 7, 19, 5 };
    static const int PA[6] = { 12, 7, 0, 12, 19, 24 };
    // текстуры: чаще чистая и лента, складка и крош — редкая приправа
    static const int TX[8] = { 0, 0, 0, 1, 1, 1, 2, 3 };
    LayerChar c[4];
    for (int i = 0; i < 4; ++i)
    {
        c[i].band = perm[i];
        const int* set = perm[i] == 0 ? PD : perm[i] == 1 ? PM : perm[i] == 2 ? PB : PA;
        c[i].semis = set[r.next() % 6u];
        c[i].tex = TX[r.next() % 8u];
    }
    return c[k & 3];
}

// Сдвиг высоты: две головки задержки со сменным окном
struct Shifter
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    float ph = 0, W = 1000;
    void prepare (double sr)
    {
        W = (float) (.07 * sr);
        int n = 1; while (n < (int) W + 16) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; ph = 0;
    }
    float process (float x, float ratio)
    {
        buf[(size_t) w] = x;
        ph += (1.0f - ratio) / W;
        ph -= std::floor (ph);
        float y = 0;
        for (float off : { 0.0f, .5f })
        {
            float p = ph + off; if (p >= 1) p -= 1;
            const float d = p * W + 2.0f;
            float pos = (float) w - d;
            while (pos < 0) pos += (float) (mask + 1);
            const int i0 = (int) pos; const float fr = pos - (float) i0;
            const float v = buf[(size_t) (i0 & mask)] * (1 - fr) + buf[(size_t) ((i0 + 1) & mask)] * fr;
            const float s = std::sin (3.14159265f * p);
            y += v * s * s;
        }
        w = (w + 1) & mask;
        return y;
    }
};

// Пинг-понг задержка: эхо прыгает L ↔ R
struct PingPong
{
    std::vector<float> l, r;
    int cap = 1, len = 1, w = 0;
    float lpL = 0, lpR = 0;
    void prepare (double sr) { cap = (int) (1.0 * sr) + 4; l.assign ((size_t) cap, 0.0f); r.assign ((size_t) cap, 0.0f); w = 0; lpL = lpR = 0; }
    void process (float in, float fb, float lpA, bool startRight, float& oL, float& oR)
    {
        if (w >= len) w = 0;
        const float a = l[(size_t) w], b = r[(size_t) w];
        lpL += (a - lpL) * lpA; lpR += (b - lpR) * lpA;
        l[(size_t) w] = (startRight ? 0.0f : in) + fb * lpR;
        r[(size_t) w] = (startRight ? in : 0.0f) + fb * lpL;
        if (++w >= len) w = 0;
        oL = a; oR = b;
    }
};

// Диффузор для BLUR: цепочка allpass с хвостом
struct Allpass
{
    std::vector<float> b; int w = 0;
    void prepare (int n) { b.assign ((size_t) std::max (2, n), 0.0f); w = 0; }
    float process (float x, float g)
    {
        const float d = b[(size_t) w];
        const float v = x + g * d;
        b[(size_t) w] = v;
        if (++w >= (int) b.size()) w = 0;
        return d - g * v;
    }
};

inline const char* modeName (int m)
{
    static const char* n[4] = { "GRAINS", "DIFFUSE", "NETWORK", "TAPES" };
    return n[std::clamp (m, 0, 3)];
}

struct Layout
{
    int n = 0;
    int off[kMaxLines] {};
    int len[kMaxLines] {};
    int total() const { int t = 0; for (int i = 0; i < n; ++i) t += len[i]; return t; }
};

inline bool isPrime (int n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; (long long) d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}
inline int primeNear (double x)
{
    int n = std::max (3, (int) std::lround (x));
    if (n % 2 == 0) ++n;
    while (! isPrime (n)) n += 2;
    return n;
}

// Длины линий в секундах. Взаимно некратные, чтобы линии не сходились по фазе.
inline Layout makeLayout (int mode, int size, double sr, int poolLen)
{
    static const double grains[3] = { 7.13, 5.0, 1.6 };
    static const double diff[8] = { 7.32, 4.33, 3.13, 4.73, 6.47, 4.66, 7.07, 4.98 };   // ap0 d0 ap1 d1 ap2 d2 ap3 d3
    static const double diffScale[3] = { 1.0, .6, .22 };
    static const double net[4] = { 12.3, 9.1, 7.0, 16.3 };
    static const double netScale[3] = { 1.0, .55, .2 };
    static const double tapes[3][2] = { { 21.33, 23.4 }, { 10.0, 9.2 }, { 2.13, 2.63 } };
    size = std::clamp (size, 0, 2);
    double secs[kMaxLines] {};
    int n = 0;
    switch (std::clamp (mode, 0, 3))
    {
        case 0: secs[n++] = grains[size]; break;
        case 1: for (double d : diff) secs[n++] = d * diffScale[size]; break;
        case 2: for (double d : net) secs[n++] = d * netScale[size]; break;
        default: secs[n++] = tapes[size][0]; secs[n++] = tapes[size][1]; break;
    }
    Layout L;
    L.n = n;
    int off = 0;
    for (int i = 0; i < n; ++i)
    {
        int len = primeNear (secs[i] * sr);
        len = std::max (256, std::min (len, poolLen - off - 1));
        L.off[i] = off;
        L.len[i] = len;
        off += len;
    }
    return L;
}

struct Tri   // треугольный LFO 0..1
{
    float v = 0, d = 1;
    void step (float inc)
    {
        v += d * inc;
        if (v >= 1) { v = 1; d = -1; }
        else if (v <= 0) { v = 0; d = 1; }
    }
};

inline float limitStep (float x, float& env, float atk, float rel)
{
    const float a = std::fabs (x);
    float over = 0;
    if (a >= .8f) over = a < 1.2f ? 1.25f * (a - .8f) * (a - .8f) : a - 1.0f;
    env += (over - env) * (over > env ? atk : rel);
    if (env > 1) env = 1;
    return x * (1.0f - env);
}

inline float warm (float x, float d)
{
    if (d <= 1e-6f) return x;
    const float lim = (1 + d) / (2 * d);          // вершина параболы: дальше кривая загнулась бы обратно
    x = std::clamp (x, -lim, lim);
    return ((1 + d) * x - d * x * std::fabs (x)) * (1 - .3f * d);
}

inline float softClip (float x)
{
    if (x > .9f) { x = std::min (x, 1.0984f); x -= 2.52f * (x - .9f) * (x - .9f); }
    else if (x < -.9f) { x = std::max (x, -1.0984f); x += 2.52f * (x + .9f) * (x + .9f); }
    return x;
}

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        poolLen = (int) (kPoolSeconds * sr) + 64;
        pool.assign ((size_t) poolLen, 0.0f);
        k.smooth = rate (.001, sr);
        k.fade = (float) (.001 * kRef / sr);
        k.recRate = rate (.002, sr);
        k.enRate = rate (5e-5, sr);
        k.limAtk = rate (.1, sr); k.limRel = rate (.001, sr);
        k.bloomDown = rate (.01, sr); k.bloomUp = rate (3e-5, sr);
        k.envAtk = rate (.1, sr); k.envFastRel = rate (.00015, sr); k.envSlowRel = rate (4e-5, sr);
        k.gateUp = rate (.1, sr); k.gateDown = rate (1e-5, sr);
        k.lfoScale = (float) (kRef / sr);
        k.walk = (float) (1.6e-4 * std::sqrt (kRef / sr));
        k.modM0 = (float) (8300.0 / kRef * sr);
        k.modM1 = (float) (11100.0 / kRef * sr);
        curMode = curSize = -1;
        xf = 0;
        dir = 1;
        pendingErase = false;
        for (int c = 0; c < 2; ++c) { gate[c] = 1; rg[c] = 1; envF[c] = envS[c] = 0; }
        lfo[0].v = .13f; lfo[1].v = .58f; lfo[2].v = .31f; lfo[3].v = .86f;
        walk[0] = walk[1] = 0;
        for (auto& sh : shift) sh.prepare (sr);
        for (auto& pp : pong) pp.prepare (sr);
        static const double apMs[6] = { 11.3, 19.7, 29.9, 43.1, 61.3, 83.9 };
        for (int j = 0; j < 6; ++j) { blurAp[0][j].prepare ((int) (apMs[j] * .001 * sr)); blurAp[1][j].prepare ((int) (apMs[j] * 1.07 * .001 * sr)); }
        auto op = [this] (double fc) { return (float) (1.0 - std::exp (-2.0 * 3.141592653589793 * fc / sr)); };
        k.b700 = op (700); k.b150 = op (150); k.b2500 = op (2500); k.b1500 = op (1500); k.b4000 = op (4000);
        k.t1500 = op (1500); k.t400 = op (400); k.t1000 = op (1000); k.pp = op (2800); k.tank = op (2500);
        k.b100 = op (100); k.b3000 = op (3000); k.b7000 = op (7000); k.b11000 = op (11000); k.up = op (5500);
        k.sub = op (180); k.shelf = op (200); k.tilt = op (900); k.dc = op (8); k.dcOut = op (16);
        dcOut[0] = dcOut[1] = 0;
        k.limAtkOut = rate (.35, sr); k.limRelOut = (float) (1.0 - std::exp (-1.0 / (.25 * sr)));
        subShift.prepare (sr);
        subLp1 = subLp2 = 0; shelfLp[0] = shelfLp[1] = 0; tiltLp[0] = tiltLp[1] = 0; outGain = 1;
        k.blur = (float) (1.0 - std::exp (-1.0 / (.4 * sr)));
        k.crushN = std::max (1, (int) std::lround (sr / 12000.0));
        bf = 0; blurPrev[0] = blurPrev[1] = 0; tankLp[0] = tankLp[1] = 0;
        for (auto& lb : lband) lb = LBand();
        resetStates();
    }

    void requestErase() { eraseReq.store (true); }

    // Обработка блока. inR может быть nullptr (моно-вход).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p)
    {
        const bool wantLayout = p.mode != curMode || p.size != curSize;
        if (eraseReq.exchange (false)) pendingErase = true;
        const bool wantDir = (p.reverse ? -1 : 1) != dir;
        bool want = wantLayout || wantDir || pendingErase;

        const int lc = std::clamp (p.lowcut, 0, 3), hc = std::clamp (p.highcut, 0, 3);
        static const float LC_A[3] = { .035f, .06f, .1f }, LC_G[3] = { 1.035f, 1.04f, 1.063f };
        static const float HC_B[3] = { .5f, .15f, .08f }, HC_G[3] = { 1.002f, 1.01f, 1.033f }, HC_S[3] = { .001f, .025f, .03f };
        if (lc > 0) { lcA = rate (LC_A[lc - 1], sr); lcG = LC_G[lc - 1]; }
        if (hc > 0) { hcB = rate (HC_B[hc - 1], sr); hcG = HC_G[hc - 1]; hcS = HC_S[hc - 1]; }
        const float lcT = lc > 0 ? 1.0f : 0.0f, hcT = hc > 0 ? 1.0f : 0.0f;

        const float rgT[2] = { (! p.freeze && (p.input == 0 || p.input == 1)) ? 1.0f : 0.0f,
                               (! p.freeze && (p.input == 0 || p.input == 2)) ? 1.0f : 0.0f };

        if (curMode < 0) { rg[0] = rgT[0]; rg[1] = rgT[1]; }
        for (int j = 0; j < 4; ++j)
        {
            ch[j] = layerChar (p.layerSeed, j);
            deg[j].band = ch[j].band;
            ratio[j] = std::pow (2.0f, (float) ch[j].semis / 12.0f);
            static const float ppT[4] = { .31f, .47f, .67f, .89f };
            const float sc = curSize == 2 ? .55f : (curSize == 1 ? .8f : 1.0f);
            pong[j].len = std::min (pong[j].cap - 1, std::max (16, (int) (ppT[j] * sc * sr)));
        }
        if (curMode == 0) deg[0].band = -1;   // у гранул одна петля: окраска слоёв только на выходе
        const float bfT = p.blur ? 1.0f : 0.0f;
        float inPk = 0, outPk = 0;
        for (int i = 0; i < n; ++i)
        {
            // ---- переходы: смена режима, размера, направления, стирание
            if (want)
            {
                xf -= k.fade;
                if (xf <= 0)
                {
                    xf = 0;
                    if (curMode < 0 || p.mode != curMode || p.size != curSize) applyLayout (p.mode, p.size);
                    if (pendingErase) { std::fill (pool.begin(), pool.end(), 0.0f); resetStates(); pendingErase = false; }
                    dir = p.reverse ? -1 : 1;
                    want = false;
                }
            }
            else if (xf < 1) xf = std::min (1.0f, xf + k.fade);

            // ---- сглаживание ручек
            sDecay += (p.decay - sDecay) * k.smooth;
            sWarm += (p.warmth - sWarm) * k.smooth;
            sDyn += (p.dynamics - sDyn) * k.smooth;
            sSpread += (p.spread - sSpread) * k.smooth;
            sDrift += (p.drift - sDrift) * k.smooth;
            sMix += (p.mix - sMix) * k.smooth;
            sOut += (p.out - sOut) * k.smooth;
            sLay += (p.layers - sLay) * k.smooth;
            sPP += (p.pingpong - sPP) * k.smooth;
            sTone += (p.tone - sTone) * k.smooth;
            sDepth += (p.depth - sDepth) * k.smooth;
            bf += (bfT - bf) * k.blur;
            lcE += (lcT - lcE) * k.enRate;
            hcE += (hcT - hcE) * k.enRate;
            for (int c = 0; c < 2; ++c) rg[c] += (rgT[c] - rg[c]) * k.recRate;

            const float dg = (2.0f - sDecay) * sDecay * 1.066f;            // кривая обратной связи
            const float wd = sWarm * sWarm * sWarm;
            const float duckAmt = sDyn < .5f ? (.5f - sDyn) * 2 * (.5f - sDyn) * 2 : 0.0f;
            const float bloomAmt = sDyn > .5f ? (sDyn - .5f) * 2 * (sDyn - .5f) * 2 : 0.0f;

            // ---- медленные LFO со случайным темпом
            walk[0] += rng.bi() * k.walk;
            walk[1] += rng.bi() * k.walk * 2;
            for (float& wv : walk) { if (wv >= 1) wv = -.5f; else if (wv < -.5f) wv = 1; }
            lfo[0].step (3.1151e-6f * k.lfoScale * (1 + walk[0]));
            lfo[1].step (3.7667e-6f * k.lfoScale * (1 + walk[0]));
            lfo[2].step (2.3159e-6f * k.lfoScale * (1 + walk[1]));
            lfo[3].step (2.9663e-6f * k.lfoScale * (1 + walk[1]));

            // ---- вход: гейт на пути записи, огибающие для дакинга
            const float dryL = inL[i], dryR = inR ? inR[i] : inL[i];
            float g[2] = { dryL, dryR };
            for (int c = 0; c < 2; ++c)
            {
                const float a = std::fabs (g[c]);
                if (a > 0.0015f) gate[c] += (1 - gate[c]) * k.gateUp;
                else gate[c] -= gate[c] * k.gateDown;
                g[c] *= gate[c] * rg[c];
                const float e = std::fabs (g[c] * 4);
                envF[c] += (e - envF[c]) * (e > envF[c] ? k.envAtk : k.envFastRel);
                envS[c] += (e - envS[c]) * (e > envS[c] ? k.envAtk : k.envSlowRel);
            }
            inPk = std::max (inPk, std::max (std::fabs (dryL), std::fabs (dryR)));
            // размытое облако понемногу дописывается обратно в память
            if (bf > 1e-4f) for (int c = 0; c < 2; ++c) g[c] += bf * .15f * blurPrev[c] * rg[c];

            float duck[2], fbg[2];
            for (int c = 0; c < 2; ++c)
            {
                const float swell = std::max (0.0f, (1 - envF[c] * 5) * envS[c]);
                const float dk = std::max (0.0f, (swell - envF[c]) * duckAmt + 1);
                const float fr = 1 - rg[c];                                // заморозка: петля без потерь
                duck[c] = dk + (1 - dk) * fr;
                fbg[c] = dg + (1 - dg) * fr;
            }

            ln = 0;
            switch (curMode)
            {
                case 0: grains (g, duck, fbg, wd, bloomAmt); break;
                case 1: diffuse (g, duck, fbg, wd, bloomAmt); break;
                case 2: network (g, duck, fbg, wd, bloomAmt); break;
                default: tapes (g, duck, fbg, wd, bloomAmt); break;
            }
            for (int l = 0; l < lay.n; ++l) w[l] = wrap (w[l] + dir, lay.len[l]);

            float wl = 0, wr = 0;
            layerStage (wl, wr);
            blurStage (wl, wr);
            depthTone (wl, wr);
            wl *= 2.0f; wr *= 2.0f;   // подъём уровня мокрого сигнала (выходной лимитер держит пики)

            float oL = wl * xf * sMix + dryL * (1 - sMix);
            float oR = wr * xf * sMix + dryR * (1 - sMix);
            if (p.mono) oL = oR = (oL + oR) * .5f;
            oL = warm (oL, wd) * sOut;
            oR = warm (oR, wd) * sOut;
            // плавный лимитер: пики уменьшают громкость, а не искажаются
            const float pk = std::max (std::fabs (oL), std::fabs (oR));
            const float tgt = pk > .84f ? .84f / pk : 1.0f;
            outGain += (tgt - outGain) * (tgt < outGain ? k.limAtkOut : k.limRelOut);
            oL = softClip (oL * outGain);
            oR = softClip (oR * outGain);
            outL[i] = oL;
            outR[i] = oR;
            outPk = std::max (outPk, std::max (std::fabs (oL), std::fabs (oR)));
        }
        publishViz (inPk, outPk);
    }

    // ---- для интерфейса (читается из потока интерфейса)
    const float* poolData() const { return pool.data(); }
    int getPoolLen() const { return poolLen; }
    struct VizState
    {
        std::atomic<int> mode { 0 }, size { 0 }, n { 0 }, dir { 1 };
        std::array<std::atomic<int>, kMaxLines> off {}, len {}, w {};
        std::array<std::atomic<float>, kGrainViz> gPos {}, gAmp {};   // gPos: индекс в пуле, -1 — нет
        std::array<std::atomic<float>, 4> lfo {};
        std::atomic<float> inLvl { 0 }, outLvl { 0 }, xf { 0 }, blur { 0 }, layers { 0 };
        std::atomic<int> nLayers { 0 };
        std::array<std::atomic<int>, 4> lBand {}, lSemis {}, lTex {};
        std::array<std::atomic<float>, 4> lLvl {};
    } viz;

private:
    struct LBand { float s1 = 0, s2 = 0, s3 = 0, held = 0; int hold = 0; };
    struct Deg
    {
        float lp1 = 0, lp2 = 0, env = 0, bloom = 1, lp3 = 0, dc = 0;
        int band = -1;   // окраска линии внутри петли по характеру слоя
    };

    static int wrap (int i, int len) { return i < 0 ? i + len : (i >= len ? i - len : i); }
    float& at (int line, int idx) { return pool[(size_t) (lay.off[line] + idx)]; }
    float readFrac (int line, float pos)
    {
        const int len = lay.len[line];
        while (pos < 0) pos += (float) len;
        while (pos >= (float) len) pos -= (float) len;
        const int i0 = (int) pos;
        const float fr = pos - (float) i0;
        const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
        return at (line, i0) * (1 - fr) + at (line, i1) * fr;
    }

    // цепочка деградации в петле: срез низа + срез верха с чётной сатурацией
    // live — 1 при обычной записи, 0 в заморозке: замороженная память держится без потерь
    float degrade (Deg& d, float x, float live)
    {
        const float le = lcE * live, he = hcE * live;
        d.lp1 += (x - d.lp1) * lcA;
        x = (1 - le) * x + le * (x - d.lp1) * lcG;
        d.lp2 += (x - d.lp2) * hcB;
        x = (1 - he) * x + he * (d.lp2 * hcG - d.lp2 * d.lp2 * hcS);
        // слой со своим характером темнеет или светлеет с каждым кругом
        const float la = sLay * live;
        if (d.band >= 0 && la > 1e-4f)
        {
            switch (d.band)
            {
                case 0: d.lp3 += (x - d.lp3) * k.t1500; x += la * .6f * (d.lp3 - x); break;
                case 2: d.lp3 += (x - d.lp3) * k.t400;  x -= la * .25f * d.lp3; break;
                case 3: d.lp3 += (x - d.lp3) * k.t1000; x -= la * .3f * d.lp3; break;
                default: break;
            }
        }
        return x;
    }
    // «распускающийся» компрессор: тихое поднимает до ×1.8, громкое прижимает
    float bloomGain (Deg& d, float s, float amt)
    {
        const float t = std::max (0.0f, 1 + amt * (.8f - 1.9f * s * s));
        d.bloom += (t - d.bloom) * (t < d.bloom ? k.bloomDown : k.bloomUp);
        return d.bloom;
    }
    // запись в линию: лимитер, тепло, мягкое вхождение при переходах
    void writeLine (int line, Deg& d, float v, float wd)
    {
        d.dc += (v - d.dc) * k.dc; v -= d.dc;   // постоянная составляющая не копится в петле
        v = limitStep (v, d.env, k.limAtk, k.limRel);
        v = warm (v, wd);
        float& cell = at (line, w[line]);
        cell = v * xf + cell * (1 - xf);
    }

    // ===== 0 GRAINS: одна моно-петля + 3 набора по 4 гранулы
    void grains (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt)
    {
        const int len = lay.len[0];
        const float k1 = 1.0f / std::max (1.0f, rg[0] + rg[1]);
        const float monoIn = (g[0] + g[1]) * k1;
        const float dk = (duck[0] + duck[1]) * .5f, fb = std::max (fbg[0], fbg[1]);
        const float old = at (0, w[0]);
        const float x = degrade (deg[0], old, std::max (rg[0], rg[1])) * dk;
        const float b = bloomGain (deg[0], monoIn + x, bloomAmt);
        writeLine (0, deg[0], b * (x * fb + monoIn), wd);

        const float sp = sSpread, dr = sDrift * .9f;
        float sumL = grainSet (gL, len, [&] { return sp * rng.uni() + dr * lfo[1].v; });
        float sumR = grainSet (gR, len, [&] { return (rng.uni() - .5f) * sp + .5f + dr * lfo[0].v; });
        float sumS = grainSet (gS, len, [&] { return sp * rng.uni() + dr * lfo[2].v; });
        addLayer (sumL * .35f, 1, 0);
        addLayer (sumR * .35f, 0, 1);
        addLayer (sumS * .35f * .85f, 1, 1);
    }

    struct GrainSet { int P = 1000, ph = 0; int start[4] {}; float amp[4] {}; int idx[4] {}; };
    template <typename F>
    float grainSet (GrainSet& s, int len, F pickU)
    {
        float sum = 0;
        for (int v = 0; v < 4; ++v)
        {
            int vph = s.ph + v * s.P / 4;
            if (vph >= s.P) vph -= s.P;
            if (vph == 0)
            {
                float u = pickU();
                u -= std::floor (u);
                const int D = s.P + (int) (u * (float) std::max (1, len - 2 * s.P));
                s.start[v] = wrap (((w[0] - dir * D) % len + len) % len, len);
            }
            const int idx = ((s.start[v] + dir * vph) % len + len) % len;
            const float a = 1.0f - std::fabs (2.0f * (float) vph / (float) s.P - 1.0f);
            s.amp[v] = a; s.idx[v] = idx;
            sum += at (0, idx) * a;
        }
        if (++s.ph >= s.P) s.ph = 0;
        return sum;
    }

    // ===== 1 DIFFUSE: 4 ступени allpass → задержка по кругу
    void diffuse (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt)
    {
        float dOut[4];
        for (int s = 0; s < 4; ++s) dOut[s] = at (2 * s + 1, w[2 * s + 1]);
        addLayer (dOut[0], .7f, 0);
        addLayer (dOut[1], .49f, .28f);
        addLayer (dOut[2], 0, .7f);
        addLayer (dOut[3], .28f, .35f);

        for (int s = 0; s < 4; ++s)
        {
            const int c = s & 1;
            const float apG = .55f * (1 - .5f * std::min (1.0f, fbg[c]));
            const float fbStage = .91f * fbg[c] + (fbg[c] > .999f ? .09f : 0.0f);
            const float prev = dOut[(s + 3) & 3] * fbStage;
            const float x = degrade (deg[s], prev, rg[c]) * duck[c];
            const float b = bloomGain (deg[s], g[c] + x, bloomAmt);
            float v = b * (x + g[c]);
            deg[s].dc += (v - deg[s].dc) * k.dc; v -= deg[s].dc;
            v = limitStep (v, deg[s].env, k.limAtk, k.limRel);
            v = warm (v, wd);

            const int ap = 2 * s, dl = 2 * s + 1;
            float apOut;
            if (s < 2)
            {
                const float m = s == 0 ? mod[0] : mod[1];
                apOut = readFrac (ap, (float) w[ap] + (float) dir * m);
            }
            else apOut = at (ap, w[ap]);
            float& apCell = at (ap, w[ap]);
            apCell = (v + apOut * apG) * xf + apCell * (1 - xf);
            float& dCell = at (dl, w[dl]);
            dCell = (apOut - v * apG) * xf + dCell * (1 - xf);
        }
        // дрейф длины двух первых allpass: ±0.2% высоты, проход за минуту-две
        const float m0 = std::min (k.modM0, (float) lay.len[0] * .25f), m1 = std::min (k.modM1, (float) lay.len[2] * .25f);
        mod[0] += modInc[0]; if (mod[0] >= m0) { mod[0] = m0; modInc[0] = -.002112f; } else if (mod[0] <= 0) { mod[0] = 0; modInc[0] = .002112f; }
        mod[1] += modInc[1]; if (mod[1] >= m1) { mod[1] = m1; modInc[1] = -.0018099f; } else if (mod[1] <= 0) { mod[1] = 0; modInc[1] = .0018099f; }
    }

    // ===== 2 NETWORK: 4 линии с перекрёстной связью, панорама плывёт по LFO
    void network (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt)
    {
        float o[4];
        for (int l = 0; l < 4; ++l) o[l] = at (l, w[l]);
        const float c = sSpread * (rg[0] + rg[1]) * .5f, a = 1 - .46f * c;   // в заморозке линии не смешиваются
        const float n6 = 1.0f / std::sqrt (a * a + .36f * c * c), n5 = 1.0f / std::sqrt (a * a + .3025f * c * c);
        float x[4] = { (a * o[0] - .6f * c * o[3]) * n6, (a * o[1] + .6f * c * o[2]) * n6,
                       (a * o[2] + .55f * c * o[0]) * n5, (a * o[3] + .55f * c * o[1]) * n5 };
        for (int l = 0; l < 4; ++l)
        {
            const int ch = l & 1;
            x[l] = degrade (deg[l], x[l], rg[ch]) * duck[ch];
            const float b = bloomGain (deg[l], g[ch] + x[l], bloomAmt);
            x[l] *= b;
            writeLine (l, deg[l], x[l] * fbg[ch] + g[ch] * b, wd);
        }
        const float d = sDrift;
        const float A = lfo[0].v, B = lfo[1].v, C = lfo[2].v, D = lfo[3].v;
        addLayer (x[0], (1 - A * d) * .7f, A * d * .7f);
        addLayer (x[1], B * d * .7f, (1 - B * d) * .7f);
        addLayer (x[2], (.7f * (1 - d) + C * d) * .7f, (.4f * (1 - d) + (1 - C) * d) * .7f);
        addLayer (x[3], (.4f * (1 - d) + (1 - D) * d) * .7f, (.7f * (1 - d) + D * d) * .7f);
    }

    // ===== 3 TAPES: две петли неравной длины с перекрёстной связью
    void tapes (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt)
    {
        const float o0 = at (0, w[0]), o1 = at (1, w[1]);
        const float c = sSpread * (rg[0] + rg[1]) * .5f, a = 1 - .49f * c, nn = 1.0f / std::sqrt (a * a + .2601f * c * c);
        float x[2] = { (a * o0 + .51f * c * o1) * nn, (a * o1 + .51f * c * o0) * nn };
        for (int l = 0; l < 2; ++l)
        {
            x[l] = degrade (deg[l], x[l], rg[l]) * duck[l];
            const float b = bloomGain (deg[l], g[l] + x[l], bloomAmt);
            x[l] *= b;
            writeLine (l, deg[l], x[l] * fbg[l] + g[l] * b, wd);
        }
        const float d = sDrift, A = lfo[0].v, B = lfo[1].v;
        addLayer (x[0], 1 - A * d, A * d);
        addLayer (x[1], B * d, 1 - B * d);
        // ещё две головки чтения посреди лент — появляются вместе со слоями
        const float t0 = at (0, wrap (w[0] + lay.len[0] / 2, lay.len[0]));
        const float t1 = at (1, wrap (w[1] + (int) ((float) lay.len[1] * .37f), lay.len[1]));
        addLayer (t0, .55f * sLay, .25f * sLay);
        addLayer (t1, .25f * sLay, .55f * sLay);
    }

    void addLayer (float x, float gl, float gr)
    {
        if (ln >= 4) return;
        lx[ln] = x; lgl[ln] = gl; lgr[ln] = gr; ++ln;
    }

    float bandFilter (LBand& b, int band, float x)
    {
        switch (band)
        {
            case 0: b.s1 += (x - b.s1) * k.b700; return b.s1 * 1.1f;
            case 1: { b.s1 += (x - b.s1) * k.b100; const float h = x - b.s1; b.s2 += (h - b.s2) * k.b3000; return b.s2 * .9f; }
            case 2: { b.s1 += (x - b.s1) * k.b1500; const float h = x - b.s1; b.s2 += (h - b.s2) * k.b7000; return b.s2 * .95f; }
            default: { b.s1 += (x - b.s1) * k.b4000; const float h = x - b.s1; b.s2 += (h - b.s2) * k.b11000; return b.s2 * 1.1f; }
        }
    }
    float texture (LBand& b, int tex, float x)
    {
        switch (tex)
        {
            case 1: return warm (x * 1.3f, .8f);
            case 2: return std::sin (1.7f * x) * (1.1f / 1.7f);
            case 3:
                if (++b.hold >= k.crushN) { b.hold = 0; b.held = std::round (x * 64.0f) / 64.0f; }
                return .5f * (x + b.held);
            default: return x;
        }
    }

    // общий выход слоёв: характер (высота, полоса, текстура), раскладка по стерео, пинг-понг
    void layerStage (float& wl, float& wr)
    {
        static const float pos3[3] = { -.8f, .8f, 0.0f }, pos4[4] = { -.85f, .85f, -.4f, .4f };
        const float la = sLay, pp = sPP;
        const float fb = .3f + .5f * pp;
        for (int j = 0; j < ln; ++j)
        {
            float y = lx[j];
            const float sh = shift[j].process (y, ratio[j]);
            if (la > 1e-4f)
            {
                if (ch[j].semis != 0)
                {
                    float shv = sh;
                    if (ch[j].semis > 0) { lband[j].s3 += (sh - lband[j].s3) * k.up; shv = lband[j].s3; }   // сдвиг вверх без жёсткого верха
                    y += la * (shv - y);
                }
                y += la * (bandFilter (lband[j], ch[j].band, y) - y);
                y += la * (texture (lband[j], ch[j].tex, y) - y);
            }
            const float pos = ln == 3 ? pos3[j] : pos4[j];
            const float th = (pos + 1) * .25f * 3.14159265f;
            const float e = std::max (.5f, std::sqrt (lgl[j] * lgl[j] + lgr[j] * lgr[j]));
            const float gl = lgl[j] + la * .7f * (std::cos (th) * e - lgl[j]);
            const float gr = lgr[j] + la * .7f * (std::sin (th) * e - lgr[j]);
            wl += y * gl; wr += y * gr;
            lvl[j] += (std::fabs (y) - lvl[j]) * .001f;
            if (pp > 1e-4f)
            {
                float oL, oR;
                pong[j].process (y * pp, fb, k.pp, pos > 0, oL, oR);
                wl += oL * pp * .9f; wr += oR * pp * .9f;
            }
        }
    }

    // DEPTH (саб на октаву ниже + подъём низа) и TONE (наклон вокруг 900 Гц)
    void depthTone (float& wl, float& wr)
    {
        const float mono = .5f * (wl + wr);
        const float sh = subShift.process (mono, .5f);
        subLp1 += (sh - subLp1) * k.sub; subLp2 += (subLp1 - subLp2) * k.sub;
        float x[2] = { wl, wr };
        const float t = (.5f - sTone) * 2.0f;                 // +1 тёмный … −1 яркий
        const float gLo = 1 + .45f * t, gHi = std::max (.2f, 1 - .55f * t);
        for (int c = 0; c < 2; ++c)
        {
            shelfLp[c] += (x[c] - shelfLp[c]) * k.shelf;
            x[c] += sDepth * .7f * shelfLp[c] + sDepth * 1.3f * subLp2;
            tiltLp[c] += (x[c] - tiltLp[c]) * k.tilt;
            x[c] = tiltLp[c] * gLo + (x[c] - tiltLp[c]) * gHi;
        }
        for (int c = 0; c < 2; ++c) { dcOut[c] += (x[c] - dcOut[c]) * k.dcOut; x[c] -= dcOut[c]; }
        wl = x[0]; wr = x[1];
    }

    // BLUR: стерео-диффузор с хвостом, размывает всё в облако
    void blurStage (float& wl, float& wr)
    {
        if (bf < 1e-4f) { blurPrev[0] = blurPrev[1] = 0; return; }
        const float in[2] = { wl, wr };
        float o[2];
        for (int c = 0; c < 2; ++c)
        {
            float x = in[c] + .55f * tankLp[1 - c];
            for (auto& ap : blurAp[c]) x = ap.process (x, .7f);
            o[c] = x;
        }
        for (int c = 0; c < 2; ++c) { tankLp[c] += (o[c] - tankLp[c]) * k.tank; blurPrev[c] = o[c]; }
        wl += bf * (o[0] * .8f - wl);
        wr += bf * (o[1] * .8f - wr);
    }

    void applyLayout (int mode, int size)
    {
        lay = makeLayout (mode, size, sr, poolLen);
        curMode = std::clamp (mode, 0, 3);
        curSize = std::clamp (size, 0, 2);
        // сгладить стыки новых колец, чтобы старый материал не щёлкал на границе
        for (int l = 0; l < lay.n; ++l)
        {
            const int F = std::min (120, lay.len[l] / 2);
            for (int j = 0; j < F; ++j)
            {
                const float gg = (float) j / (float) F;
                at (l, j) *= gg;
                at (l, lay.len[l] - 1 - j) *= gg;
            }
        }
        static const double gl[3][2] = { { .58, .27 }, { .43, .21 }, { .2, .1 } };
        gL.P = gR.P = std::max (64, (int) (gl[curSize][0] * sr));
        gS.P = std::max (64, (int) (gl[curSize][1] * sr));
        resetStates();
    }

    void resetStates()
    {
        for (auto& d : deg) d = Deg();
        for (int l = 0; l < kMaxLines; ++l) w[l] = 0;
        for (auto* s : { &gL, &gR, &gS }) { s->ph = 0; for (int v = 0; v < 4; ++v) { s->start[v] = 0; s->amp[v] = 0; s->idx[v] = 0; } }
        gR.ph = gR.P / 8;   // правый набор со сдвигом, чтобы каналы не совпадали
        mod[0] = mod[1] = 0;
        modInc[0] = .002112f; modInc[1] = .0018099f;
    }

    void publishViz (float inPk, float outPk)
    {
        viz.mode.store (curMode); viz.size.store (curSize); viz.n.store (lay.n); viz.dir.store (dir);
        for (int l = 0; l < kMaxLines; ++l)
        {
            viz.off[(size_t) l].store (l < lay.n ? lay.off[l] : 0);
            viz.len[(size_t) l].store (l < lay.n ? lay.len[l] : 0);
            viz.w[(size_t) l].store (l < lay.n ? w[l] : 0);
        }
        int gi = 0;
        if (curMode == 0)
            for (auto* s : { &gL, &gR, &gS })
                for (int v = 0; v < 4; ++v, ++gi)
                {
                    viz.gPos[(size_t) gi].store ((float) (lay.off[0] + s->idx[v]));
                    viz.gAmp[(size_t) gi].store (s->amp[v]);
                }
        for (; gi < kGrainViz; ++gi) { viz.gPos[(size_t) gi].store (-1); viz.gAmp[(size_t) gi].store (0); }
        for (int j = 0; j < 4; ++j) viz.lfo[(size_t) j].store (lfo[j].v);
        viz.inLvl.store (inPk); viz.outLvl.store (outPk); viz.xf.store (xf);
        viz.blur.store (bf); viz.layers.store (sLay); viz.nLayers.store (ln);
        for (int j = 0; j < 4; ++j)
        {
            viz.lBand[(size_t) j].store (ch[j].band); viz.lSemis[(size_t) j].store (ch[j].semis);
            viz.lTex[(size_t) j].store (ch[j].tex); viz.lLvl[(size_t) j].store (lvl[j]);
        }
    }

    struct Coefs
    {
        float smooth = 0, fade = 0, recRate = 0, enRate = 0, limAtk = 0, limRel = 0, bloomDown = 0, bloomUp = 0;
        float envAtk = 0, envFastRel = 0, envSlowRel = 0, gateUp = 0, gateDown = 0, lfoScale = 1, walk = 0, modM0 = 0, modM1 = 0;
        float b700 = 0, b150 = 0, b2500 = 0, b1500 = 0, b4000 = 0, t1500 = 0, t400 = 0, t1000 = 0, pp = 0, tank = 0, blur = 0;
        float b100 = 0, b3000 = 0, b7000 = 0, b11000 = 0, up = 0, sub = 0, shelf = 0, tilt = 0, limAtkOut = 0, limRelOut = 0, dc = 0, dcOut = 0;
        int crushN = 6;
    } k;

    double sr = 48000;
    int poolLen = 0;
    std::vector<float> pool;
    Layout lay;
    int curMode = -1, curSize = -1, dir = 1;
    int w[kMaxLines] {};
    Deg deg[kMaxLines];
    GrainSet gL, gR, gS;
    float mod[2] {}, modInc[2] { .002112f, .0018099f };
    float xf = 0;
    bool pendingErase = false;
    std::atomic<bool> eraseReq { false };

    // слои
    int ln = 0;
    float lx[4] {}, lgl[4] {}, lgr[4] {}, ratio[4] { 1, 1, 1, 1 }, lvl[4] {};
    LayerChar ch[4];
    Shifter shift[4];
    PingPong pong[4];
    LBand lband[4];
    Allpass blurAp[2][6];
    float bf = 0, blurPrev[2] {}, tankLp[2] {};
    float sLay = 0, sPP = 0, sTone = .48f, sDepth = .4f;
    Shifter subShift;
    float subLp1 = 0, subLp2 = 0, shelfLp[2] {}, tiltLp[2] {}, outGain = 1, dcOut[2] {};

    float sDecay = .6f, sWarm = .2f, sDyn = .5f, sSpread = .3f, sDrift = .4f, sMix = .5f, sOut = 1;
    float lcE = 0, hcE = 0, lcA = 0, lcG = 1, hcB = 1, hcG = 1, hcS = 0;
    float gate[2] { 1, 1 }, rg[2] { 1, 1 }, envF[2] {}, envS[2] {};
    Tri lfo[4];
    float walk[2] {};
    Rng rng { 0x2545f491u };
};
} // namespace mem
