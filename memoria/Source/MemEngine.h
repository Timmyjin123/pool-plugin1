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

            float duck[2], fbg[2];
            for (int c = 0; c < 2; ++c)
            {
                const float swell = std::max (0.0f, (1 - envF[c] * 5) * envS[c]);
                const float dk = std::max (0.0f, (swell - envF[c]) * duckAmt + 1);
                const float fr = 1 - rg[c];                                // заморозка: петля без потерь
                duck[c] = dk + (1 - dk) * fr;
                fbg[c] = dg + (1 - dg) * fr;
            }

            float wl = 0, wr = 0;
            switch (curMode)
            {
                case 0: grains (g, duck, fbg, wd, bloomAmt, wl, wr); break;
                case 1: diffuse (g, duck, fbg, wd, bloomAmt, wl, wr); break;
                case 2: network (g, duck, fbg, wd, bloomAmt, wl, wr); break;
                default: tapes (g, duck, fbg, wd, bloomAmt, wl, wr); break;
            }
            for (int l = 0; l < lay.n; ++l) w[l] = wrap (w[l] + dir, lay.len[l]);

            float oL = wl * xf * sMix + dryL * (1 - sMix);
            float oR = wr * xf * sMix + dryR * (1 - sMix);
            if (p.mono) oL = oR = (oL + oR) * .5f;
            oL = softClip (warm (oL, wd)) * sOut;
            oR = softClip (warm (oR, wd)) * sOut;
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
        std::atomic<float> inLvl { 0 }, outLvl { 0 }, xf { 0 };
    } viz;

private:
    struct Deg
    {
        float lp1 = 0, lp2 = 0, env = 0, bloom = 1;
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
        return (1 - he) * x + he * (d.lp2 * hcG - d.lp2 * d.lp2 * hcS);
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
        v = limitStep (v, d.env, k.limAtk, k.limRel);
        v = warm (v, wd);
        float& cell = at (line, w[line]);
        cell = v * xf + cell * (1 - xf);
    }

    // ===== 0 GRAINS: одна моно-петля + 3 набора по 4 гранулы
    void grains (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt, float& wl, float& wr)
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
        wl = (sumL + .85f * sumS) * .35f;
        wr = (sumR + .85f * sumS) * .35f;
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
    void diffuse (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt, float& wl, float& wr)
    {
        float dOut[4];
        for (int s = 0; s < 4; ++s) dOut[s] = at (2 * s + 1, w[2 * s + 1]);
        wl = (dOut[0] + .7f * dOut[1] + .4f * dOut[3]) * .7f;
        wr = (dOut[2] + .5f * dOut[3] + .4f * dOut[1]) * .7f;

        for (int s = 0; s < 4; ++s)
        {
            const int c = s & 1;
            const float apG = .55f * (1 - .5f * std::min (1.0f, fbg[c]));
            const float fbStage = .91f * fbg[c] + (fbg[c] > .999f ? .09f : 0.0f);
            const float prev = dOut[(s + 3) & 3] * fbStage;
            const float x = degrade (deg[s], prev, rg[c]) * duck[c];
            const float b = bloomGain (deg[s], g[c] + x, bloomAmt);
            float v = limitStep (b * (x + g[c]), deg[s].env, k.limAtk, k.limRel);
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
    void network (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt, float& wl, float& wr)
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
        wl = (x[0] * (1 - A * d) + x[1] * B * d + x[2] * (.7f * (1 - d) + C * d) + x[3] * (.4f * (1 - d) + (1 - D) * d)) * .7f;
        wr = (x[1] * (1 - B * d) + x[0] * A * d + x[3] * (.7f * (1 - d) + D * d) + x[2] * (.4f * (1 - d) + (1 - C) * d)) * .7f;
    }

    // ===== 3 TAPES: две петли неравной длины с перекрёстной связью
    void tapes (const float* g, const float* duck, const float* fbg, float wd, float bloomAmt, float& wl, float& wr)
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
        wl = x[0] * (1 - A * d) + x[1] * B * d;
        wr = x[1] * (1 - B * d) + x[0] * A * d;
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
    }

    struct Coefs
    {
        float smooth = 0, fade = 0, recRate = 0, enRate = 0, limAtk = 0, limRel = 0, bloomDown = 0, bloomUp = 0;
        float envAtk = 0, envFastRel = 0, envSlowRel = 0, gateUp = 0, gateDown = 0, lfoScale = 1, walk = 0, modM0 = 0, modM1 = 0;
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

    float sDecay = .6f, sWarm = .2f, sDyn = .5f, sSpread = .3f, sDrift = .4f, sMix = .5f, sOut = 1;
    float lcE = 0, hcE = 0, lcA = 0, lcG = 1, hcB = 1, hcG = 1, hcS = 0;
    float gate[2] { 1, 1 }, rg[2] { 1, 1 }, envF[2] {}, envS[2] {};
    Tri lfo[4];
    float walk[2] {};
    Rng rng { 0x2545f491u };
};
} // namespace mem
