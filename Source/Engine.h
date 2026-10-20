#pragma once
// DSP-движок Pool. Чистый C++17, без зависимостей от JUCE (можно тестировать отдельно).
// Порт из веб-прототипа: зёрна, спектральный пэд и далёкий голос (в фоновом потоке),
// фазовый вокодер для сдвига высоты, дилей, реверб, компрессор.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace pool
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

// ---------------------------------------------------------------- утилиты
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    inline float bi() { return uni() * 2.0f - 1.0f; }                           // [-1,1)
};

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

// ---------------------------------------------------------------- БПФ
class FFT
{
public:
    explicit FFT (int n) : N (n)
    {
        int lg = 0;
        while ((1 << lg) < n) ++lg;
        tc.resize ((size_t) n / 2);
        ts.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            tc[(size_t) i] = std::cos (kTwoPi * i / n);
            ts[(size_t) i] = std::sin (kTwoPi * i / n);
        }
        br.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int x = i, r = 0;
            for (int b = 0; b < lg; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
            br[(size_t) i] = r;
        }
    }

    // inverse == true: обратное преобразование без нормировки
    void run (double* re, double* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = br[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        const double sg = inverse ? 1.0 : -1.0;
        const int HB = N >> 1;
        for (int len = 2, lh = 0; len <= N; len <<= 1, ++lh)
        {
            const int half = len >> 1, mk = half - 1, step = N / len;
            for (int q = 0; q < HB; ++q)
            {
                const int k = q & mk;
                const int a = ((q >> lh) << (lh + 1)) + k;
                const int b = a + half;
                const int ti = k * step;
                const double wr = tc[(size_t) ti], wi = sg * ts[(size_t) ti];
                const double xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
            }
        }
    }

private:
    int N;
    std::vector<double> tc, ts;
    std::vector<int> br;
};

// ---------------------------------------------------------------- фильтр
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void set (double fs, double f, double q, bool highpass)
    {
        f = std::min (std::max (f, 10.0), fs * 0.45);
        const double w0 = kTwoPi * f / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + al;
        if (highpass) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
        else          { b0 = (1 - c) / 2; b1 = 1 - c;    b2 = (1 - c) / 2; }
        a1 = -2 * c; a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }

    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// ---------------------------------------------------------------- зёрна
struct GrainParams
{
    float size = 300, dens = 20, pos = .5f, pj = .3f, pitch = 0, ps = 0, harm = 0, rev = 0, wid = .8f, glitch = 0, crush = 0;
    bool freeze = false;
};

class GrainCloud
{
public:
    static constexpr int kMax = 64;
    struct VizGrain { float back, pan, semis, life, size; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        L = (int) (sr * 6.0);
        buf.assign ((size_t) L, 0.0f);
        w = 0;
        for (auto& g : G) g.on = false;
        acc = 0; cnt = 0; sl = sr2 = 0;
        win.resize (1025);
        for (int i = 0; i <= 1024; ++i) win[(size_t) i] = (float) (.5 - .5 * std::cos (kTwoPi * i / 1024.0));
        rng = Rng (0xC0FFEE11u);
    }

    const float* data() const { return buf.data(); }
    int length() const { return L; }
    int head() const { return w; }

    void process (const float* in, float* oL, float* oR, int n, const GrainParams& P)
    {
        const double norm = 1.4 / std::sqrt (std::max (1.0, (double) P.dens * P.size / 1000.0));
        acc += (double) P.dens * n / sr;
        while (acc >= 1.0) { acc -= 1.0; spawn (P, (int) (rng.uni() * n)); }
        const int hold = 1 + (int) std::floor (P.crush * P.crush * 30.0f);
        const float q = std::pow (2.0f, std::max (2.0f, 15.0f - P.crush * 12.0f));
        for (int i = 0; i < n; ++i)
        {
            if (! P.freeze) { buf[(size_t) w] = in[i]; if (++w >= L) w = 0; }
            float l = 0, r = 0;
            for (auto& g : G)
            {
                if (! g.on) continue;
                if (g.d > 0) { --g.d; continue; }
                const float wv = win[(size_t) ((double) g.age / g.len * 1024.0)];
                double p = g.p;
                const int i0 = (int) p;
                const double f = p - i0;
                const int i1 = i0 + 1 >= L ? 0 : i0 + 1;
                const float s = (float) ((buf[(size_t) i0] * (1.0 - f) + buf[(size_t) i1] * f) * wv);
                l += s * g.gl; r += s * g.gr;
                p += g.dir * g.inc;
                if (p >= L) p -= L; else if (p < 0) p += L;
                g.p = p;
                if (++g.age >= g.len) g.on = false;
            }
            l *= (float) norm; r *= (float) norm;
            if (P.crush > .01f)
            {
                if (cnt == 0) { sl = std::round (l * q) / q; sr2 = std::round (r * q) / q; }
                cnt = (cnt + 1) % hold;
                l = sl; r = sr2;
            }
            oL[i] = l; oR[i] = r;
        }
    }

    int snapshot (VizGrain* out, int maxN) const
    {
        int c = 0;
        for (auto& g : G)
        {
            if (! g.on || g.d > 0 || c >= maxN) continue;
            out[c++] = { g.backN, g.pan, g.semis, (float) g.age / (float) g.len, (float) g.len / (float) sr };
        }
        return c;
    }

private:
    struct Grain
    {
        bool on = false;
        int d = 0, dir = 1, age = 0, len = 1;
        double p = 0, inc = 1;
        float gl = .7f, gr = .7f, backN = 0, pan = .5f, semis = 0;
    };

    void spawn (const GrainParams& P, int delay)
    {
        static const float IV[6] = { -12, -5, 0, 7, 12, 19 };
        Grain* g = nullptr;
        for (auto& x : G) if (! x.on) { g = &x; break; }
        if (! g) return;
        double len = P.size * sr / 1000.0 * (.75 + rng.uni() * .5);
        float semis = P.pitch + rng.bi() * P.ps;
        if (rng.uni() < P.harm) semis = P.pitch + IV[(int) (rng.uni() * 6.0f) % 6];
        float pj = P.pj;
        if (P.glitch > 0 && rng.uni() < P.glitch * .6f)
        {
            len = sr * (.003 + rng.uni() * .03);
            semis += (rng.uni() < .5f ? 0.0f : rng.bi() * 12.0f);
            pj = std::max (pj, .6f);
        }
        const double inc = std::pow (2.0, semis / 12.0);
        len = std::max (48.0, std::min (len, (L - 200) / (2 * inc + 2)));
        const int ilen = (int) len;
        const int dir = rng.uni() < P.rev ? -1 : 1;
        const double mn = dir > 0 ? ilen * inc + 64 : 64, mx = L - ilen * (inc + 1) - 8;
        double back = (P.pos + rng.bi() * pj) * .9 * L;
        back = std::min (mx, std::max (mn, back));
        double p = w - back;
        if (p < 0) p += L;
        const float pan = .5f + (rng.uni() - .5f) * P.wid;
        g->on = true; g->d = delay; g->p = p; g->inc = inc; g->dir = dir; g->age = 0; g->len = ilen;
        g->gl = (float) std::cos (pan * kPi / 2); g->gr = (float) std::sin (pan * kPi / 2);
        g->backN = (float) (back / (.9 * L)); g->pan = pan; g->semis = semis;
    }

    double sr = 48000;
    int L = 288000, w = 0, cnt = 0;
    std::vector<float> buf, win;
    std::array<Grain, kMax> G;
    double acc = 0;
    float sl = 0, sr2 = 0;
    Rng rng;
};

// ---------------------------------------------------------------- сдвиг высоты (фазовый вокодер)
class PitchShifter
{
public:
    static constexpr int N = 4096, K = N / 2, HOP = N / 4;

    PitchShifter() : fft (N), win (N), re (N), im (N), vin (N), vob (N), aM (K + 1), aF (K + 1), oM (K + 1), oF (K + 1), ph (K + 1), sp (K + 1)
    {
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sin (kPi * (i + .5) / N);
        reset();
    }

    void setSampleRate (double sr) { smCoef = (float) (1.0 - std::exp (-1.0 / (0.0105 * sr))); }

    void reset()
    {
        std::fill (vin.begin(), vin.end(), 0.0f);
        std::fill (vob.begin(), vob.end(), 0.0f);
        std::fill (ph.begin(), ph.end(), 0.0);
        std::fill (sp.begin(), sp.end(), 0.0);
        vip = vop = vcnt = 0;
        sm = 0;
    }

    void process (const float* in, float* out, int n, float semis)
    {
        const float tgt = std::fabs (semis) > .01f ? 1.0f : 0.0f;
        const double rr = std::pow (2.0, semis / 12.0);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            float xi = x;
            vin[(size_t) vip] = x;
            vip = (vip + 1) & (N - 1);
            const bool act = sm > .0005f || tgt > 0;
            if (act && ++vcnt >= HOP) { vcnt = 0; frame (rr); }
            const float pv = vob[(size_t) vop];
            vob[(size_t) vop] = 0;
            vop = (vop + 1) & (N - 1);
            if (act) xi = x * (1 - sm) + pv * sm;
            sm += (tgt - sm) * smCoef;
            out[i] = xi;
        }
    }

private:
    void frame (double r)
    {
        const double TW = kTwoPi;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = vin[(size_t) ((vip + i) & (N - 1))] * win[(size_t) i]; im[(size_t) i] = 0; }
        fft.run (re.data(), im.data(), false);
        for (int k = 0; k <= K; ++k)
        {
            const double a = re[(size_t) k], b = im[(size_t) k], phase = std::atan2 (b, a);
            double d = phase - ph[(size_t) k] - TW * k * HOP / N;
            d -= TW * std::floor (d / TW + .5);
            aM[(size_t) k] = std::sqrt (a * a + b * b);
            aF[(size_t) k] = TW * k / N + d / HOP;
            ph[(size_t) k] = phase;
        }
        std::fill (oM.begin(), oM.end(), 0.0);
        std::fill (oF.begin(), oF.end(), 0.0);
        for (int k = 0; k <= K; ++k)
        {
            const int j = (int) std::floor (k * r + .5);
            if (j > K) break;
            if (aM[(size_t) k] > oM[(size_t) j]) { oM[(size_t) j] = aM[(size_t) k]; oF[(size_t) j] = aF[(size_t) k] * r; }
        }
        std::fill (re.begin(), re.end(), 0.0);
        std::fill (im.begin(), im.end(), 0.0);
        for (int j = 0; j <= K; ++j)
        {
            const double m = oM[(size_t) j];
            if (m <= 0) continue;
            sp[(size_t) j] += HOP * oF[(size_t) j];
            const double c = m * std::cos (sp[(size_t) j]), s = m * std::sin (sp[(size_t) j]);
            re[(size_t) j] = c; im[(size_t) j] = s;
            if (j > 0 && j < K) { re[(size_t) (N - j)] = c; im[(size_t) (N - j)] = -s; }
        }
        fft.run (re.data(), im.data(), true);
        for (int i = 0; i < N; ++i) vob[(size_t) ((vop + i) & (N - 1))] += (float) (re[(size_t) i] / N * win[(size_t) i] * .5);
    }

    FFT fft;
    std::vector<float> win;
    std::vector<double> re, im;
    std::vector<float> vin, vob;
    std::vector<double> aM, aF, oM, oF, ph, sp;
    int vip = 0, vop = 0, vcnt = 0;
    float sm = 0, smCoef = .002f;
};

// ---------------------------------------------------------------- пэд и далёкий голос (фоновый поток)
struct SpecParams
{
    float pos = .5f, pj = 0, pdr = 0, pwid = 1, ppitch = 0, freg = 12, fcl = 0;
    bool doPad = false, doFar = false;
    int head = 0;
};

class SpectralLayers
{
public:
    static constexpr int N = 32768, H = N / 2, HP = N / 4;

    SpectralLayers() : fft (N) {}
    ~SpectralLayers() { stop(); }

    // buffer — кольцевой буфер зёрен (читается только из рабочего потока)
    void start (const float* buffer, int length, uint32_t seed)
    {
        stop();
        buf = buffer; L = length;
        rng = Rng (seed);
        re.assign (N, 0); im.assign (N, 0); mag.assign (H + 1, 0);
        xlr.assign (H, 0); xli.assign (H, 0); xrr.assign (H, 0); xri.assign (H, 0);
        accL.assign (N, 0); accR.assign (N, 0); accFL.assign (N, 0); accFR.assign (N, 0);
        pwin.resize (N);
        for (int i = 0; i < N; ++i) pwin[(size_t) i] = (float) std::sin (kPi * (i + .5) / N);
        TC.resize (65536); TS.resize (65536);
        for (int i = 0; i < 65536; ++i) { const double a = rng.uni() * kTwoPi; TC[(size_t) i] = (float) std::cos (a); TS[(size_t) i] = (float) std::sin (a); }
        for (auto* s : { &segA, &segB }) { s->l.assign (HP, 0); s->r.assign (HP, 0); s->fl.assign (HP, 0); s->fr.assign (HP, 0); }
        cur = &segA; nxt = &segB;
        pp = 0; under = 0; state.store (0);
        quit.store (false);
        worker = std::thread ([this] { run(); });
    }

    void stop()
    {
        if (worker.joinable()) { quit.store (true); worker.join(); }
    }

    void setOffline (bool b) { offline.store (b); }
    int underruns() const { return under.load(); }

    // Первое включение слоёв после паузы
    void activate (const SpecParams& req)
    {
        for (auto* v : { &cur->l, &cur->r, &cur->fl, &cur->fr }) std::fill (v->begin(), v->end(), 0.0f);
        pp = 0;
        const int st = state.load (std::memory_order_acquire);
        if (st != 1) { state.store (0); request (req); }
    }

    // Один отсчёт воспроизведения (вызывается только из аудио-потока)
    inline void next (float& pl, float& pr, float& fl, float& fr, const SpecParams& req)
    {
        pl = cur->l[(size_t) pp]; pr = cur->r[(size_t) pp];
        fl = cur->fl[(size_t) pp]; fr = cur->fr[(size_t) pp];
        if (++pp >= HP) { pp = 0; advance (req); }
    }

private:
    struct Seg { std::vector<float> l, r, fl, fr; };

    void request (const SpecParams& r) { reqParams = r; state.store (1, std::memory_order_release); }

    void advance (const SpecParams& req)
    {
        int st = state.load (std::memory_order_acquire);
        if (st == 1 && offline.load())
        {
            while ((st = state.load (std::memory_order_acquire)) != 2)
                std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
        if (st == 2) { std::swap (cur, nxt); request (req); }
        else if (st == 1) { under.fetch_add (1); }
        else request (req);
    }

    void run()
    {
        while (! quit.load())
        {
            if (state.load (std::memory_order_acquire) == 1)
            {
                const SpecParams p = reqParams;
                compute (p);
                state.store (2, std::memory_order_release);
            }
            else
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    void compute (const SpecParams& P)
    {
        static const double RT[4] = { 1, .5, 1.5, 2 }, FV[4] = { 1, 1.0072, .993, 2 }, FG[4] = { 1, .8, .8, .4 };
        const float* win = pwin.data();
        double* R = re.data(); double* I = im.data();
        double back = P.pos * .9 * L + rng.bi() * P.pj * .25 * L;
        back = std::min ((double) (L - N - 16), std::max (0.0, back));
        int st = P.head - (int) back - N;
        while (st < 0) st += L;
        double ein = 0;
        for (int i = 0; i < N; ++i)
        {
            const double x = buf[st] * win[i];
            R[i] = x; I[i] = 0; ein += x * x;
            if (++st >= L) st = 0;
        }
        fft.run (R, I, false);
        double mx = 1e-12;
        for (int k = 0; k <= H; ++k)
        {
            const double m = std::sqrt (R[k] * R[k] + I[k] * I[k]);
            mag[(size_t) k] = m;
            if (m > mx) mx = m;
        }
        const float* tc = TC.data(); const float* ts = TS.data();

        if (P.doPad)
        {
            std::fill (xlr.begin(), xlr.end(), 0.0); std::fill (xli.begin(), xli.end(), 0.0);
            std::fill (xrr.begin(), xrr.end(), 0.0); std::fill (xri.begin(), xri.end(), 0.0);
            const double dr = P.pdr, GN[4] = { 1, dr, dr * .7, dr * .45 };
            const double wd = P.pwid, pr = std::pow (2.0, P.ppitch / 12.0);
            for (int v = 0; v < 4; ++v)
            {
                const double g = GN[v];
                if (g <= .001) continue;
                const double r = RT[v] * pr;
                const int oL = (int) (rng.uni() * 65536.0f), oR = (int) (rng.uni() * 65536.0f);
                for (int j = 1; j < H; ++j)
                {
                    const double s = j / r; const int k = (int) s;
                    if (k >= H - 1) break;
                    const double f = s - k, m = (mag[(size_t) k] * (1 - f) + mag[(size_t) k + 1] * f) * g;
                    const int a = (j + oL) & 65535, b = (j + oR) & 65535;
                    const double ur = tc[a], ui = ts[a];
                    xlr[(size_t) j] += m * ur; xli[(size_t) j] += m * ui;
                    xrr[(size_t) j] += m * (ur * (1 - wd) + tc[b] * wd); xri[(size_t) j] += m * (ui * (1 - wd) + ts[b] * wd);
                }
            }
            assembleAndOverlap (accL, accR, ein, win);
        }
        if (P.doFar)
        {
            std::fill (xlr.begin(), xlr.end(), 0.0); std::fill (xli.begin(), xli.end(), 0.0);
            std::fill (xrr.begin(), xrr.end(), 0.0); std::fill (xri.begin(), xri.end(), 0.0);
            const double Rg = std::pow (2.0, P.freg / 12.0), sh = P.fcl, wd = P.pwid;
            for (int v = 0; v < 4; ++v)
            {
                const double g = FG[v], r = FV[v] * Rg;
                const int oL = (int) (rng.uni() * 65536.0f), oR = (int) (rng.uni() * 65536.0f);
                for (int j = 1; j < H; ++j)
                {
                    const double s = j / r; const int k = (int) s;
                    if (k >= H - 1) break;
                    const double f = s - k, q = mag[(size_t) k] * (1 - f) + mag[(size_t) k + 1] * f, u = q / mx;
                    const double m = q * ((1 - sh) + sh * u * u) * g;
                    const int a = (j + oL) & 65535, b = (j + oR) & 65535;
                    const double ur = tc[a], ui = ts[a];
                    xlr[(size_t) j] += m * ur; xli[(size_t) j] += m * ui;
                    xrr[(size_t) j] += m * (ur * (1 - wd) + tc[b] * wd); xri[(size_t) j] += m * (ui * (1 - wd) + ts[b] * wd);
                }
            }
            assembleAndOverlap (accFL, accFR, ein, win);
        }
        // готовим сегмент воспроизведения и сдвигаем накопители
        std::copy (accL.begin(), accL.begin() + HP, nxt->l.begin());
        std::copy (accR.begin(), accR.begin() + HP, nxt->r.begin());
        std::copy (accFL.begin(), accFL.begin() + HP, nxt->fl.begin());
        std::copy (accFR.begin(), accFR.begin() + HP, nxt->fr.begin());
        for (auto* a : { &accL, &accR, &accFL, &accFR })
        {
            std::copy (a->begin() + HP, a->end(), a->begin());
            std::fill (a->begin() + (N - HP), a->end(), 0.0);
        }
    }

    // Собирает спектр из xlr..xri, делает обратное БПФ, нормирует по энергии и накладывает в аккумулятор
    void assembleAndOverlap (std::vector<double>& aL, std::vector<double>& aR, double ein, const float* win)
    {
        double* R = re.data(); double* I = im.data();
        std::fill (re.begin(), re.end(), 0.0); std::fill (im.begin(), im.end(), 0.0);
        for (int j = 1; j < H; ++j)
        {
            const double a = xlr[(size_t) j], b = xli[(size_t) j], c = xrr[(size_t) j], d = xri[(size_t) j];
            R[j] = a - d; I[j] = b + c; R[N - j] = a + d; I[N - j] = c - b;
        }
        fft.run (R, I, true);
        double eo = 0;
        for (int i = 0; i < N; ++i) eo += R[i] * R[i] + I[i] * I[i];
        const double gn = (eo > 1e-20 && ein > 1e-10) ? std::sqrt (2 * ein / eo) * .7 : 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double w = win[i] * gn;
            aL[(size_t) i] += R[i] * w; aR[(size_t) i] += I[i] * w;
        }
    }

    FFT fft;
    const float* buf = nullptr;
    int L = 0;
    Rng rng;
    std::vector<double> re, im, mag, xlr, xli, xrr, xri, accL, accR, accFL, accFR;
    std::vector<float> pwin, TC, TS;
    Seg segA, segB;
    Seg* cur = nullptr;
    Seg* nxt = nullptr;
    int pp = 0;
    SpecParams reqParams;
    std::atomic<int> state { 0 }; // 0 — свободно, 1 — запрос/расчёт, 2 — готово
    std::atomic<bool> quit { false }, offline { false };
    std::atomic<int> under { 0 };
    std::thread worker;
};

// ---------------------------------------------------------------- эффекты
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        size = nextPow2 (maxSamples + 4);
        mask = size - 1;
        b.assign ((size_t) size, 0.0f);
        wp = 0;
    }
    inline void write (float x) { b[(size_t) wp] = x; wp = (wp + 1) & mask; }
    // d — задержка в отсчётах (дробная); читать до write
    inline float read (float d) const
    {
        float rp = (float) wp - 1.0f - d;
        while (rp < 0) rp += (float) size;
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        return b[(size_t) (i0 & mask)] * (1 - f) + b[(size_t) ((i0 + 1) & mask)] * f;
    }
private:
    std::vector<float> b;
    int size = 8, mask = 7, wp = 0;
};

class Allpass
{
public:
    void prepare (int len, float gain) { n = len; g = gain; b.assign ((size_t) len, 0.0f); i = 0; }
    inline float process (float x)
    {
        const float d = b[(size_t) i];
        const float y = -g * x + d;
        b[(size_t) i] = x + g * y;
        if (++i >= n) i = 0;
        return y;
    }
private:
    std::vector<float> b;
    int n = 1, i = 0;
    float g = .6f;
};

// Реверб на сети линий задержки (8 линий, матрица Хаусхолдера)
class Reverb
{
public:
    void prepare (double sr)
    {
        static const double base[8] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 79.1 };
        const double t60 = 4.8;
        for (int i = 0; i < 8; ++i)
        {
            len[i] = (int) (base[i] * 0.001 * sr);
            line[i].assign ((size_t) nextPow2 (len[i] + 64), 0.0f);
            mask[i] = (int) line[i].size() - 1;
            g[i] = (float) std::pow (10.0, -3.0 * len[i] / (t60 * sr));
            lp[i] = 0; ph[i] = (float) i * .7f; rate[i] = (float) (kTwoPi * (.25 + .07 * i) / sr);
        }
        wp = 0;
        damp = (float) std::exp (-kTwoPi * 5500.0 / sr);
        const double k = sr / 44100.0;
        apl[0].prepare ((int) (142 * k), .6f); apl[1].prepare ((int) (107 * k), .6f);
        apr[0].prepare ((int) (151 * k), .6f); apr[1].prepare ((int) (113 * k), .6f);
    }

    inline void process (float inL, float inR, float& outL, float& outR)
    {
        const float xl = apl[1].process (apl[0].process (inL)) * .35f;
        const float xr = apr[1].process (apr[0].process (inR)) * .35f;
        float y[8], sum = 0;
        for (int i = 0; i < 8; ++i)
        {
            ph[i] += rate[i];
            if (ph[i] > (float) kTwoPi) ph[i] -= (float) kTwoPi;
            const float d = (float) len[i] + 6.0f * std::sin (ph[i]);
            float rp = (float) wp - d;
            while (rp < 0) rp += (float) line[i].size();
            const int i0 = (int) rp; const float f = rp - (float) i0;
            const float v = line[i][(size_t) (i0 & mask[i])] * (1 - f) + line[i][(size_t) ((i0 + 1) & mask[i])] * f;
            lp[i] = v * (1 - damp) + lp[i] * damp;
            y[i] = lp[i] * g[i];
            sum += y[i];
        }
        const float h = sum * .25f;
        float oL = 0, oR = 0;
        for (int i = 0; i < 8; ++i)
        {
            line[i][(size_t) (wp & mask[i])] = y[i] - h + ((i & 1) ? xr : xl);
            if (i & 1) oR += y[i]; else oL += y[i];
        }
        ++wp;
        outL = oL * .5f; outR = oR * .5f;
    }

private:
    std::vector<float> line[8];
    int len[8] {}, mask[8] {};
    float g[8] {}, lp[8] {}, ph[8] {}, rate[8] {};
    float damp = .5f;
    int wp = 0;
    Allpass apl[2], apr[2];
};

class Compressor
{
public:
    void prepare (double sr)
    {
        atk = (float) std::exp (-1.0 / (0.010 * sr));
        rel = (float) std::exp (-1.0 / (0.300 * sr));
        gdb = 0;
    }
    inline void process (float& l, float& r)
    {
        const float lv = std::max (std::fabs (l), std::fabs (r));
        const float db = 20.0f * std::log10 (lv + 1e-9f);
        const float thr = -16.0f, knee = 20.0f, ratio = 5.0f;
        const float over = db - thr;
        float outdb;
        if (2 * over < -knee) outdb = db;
        else if (2 * std::fabs (over) <= knee) outdb = db + (1.0f / ratio - 1.0f) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        else outdb = thr + over / ratio;
        const float target = outdb - db; // <= 0
        gdb = target < gdb ? atk * gdb + (1 - atk) * target : rel * gdb + (1 - rel) * target;
        const float lin = std::exp (gdb * 0.11512925f) * 2.0f; // +6 дБ компенсации
        l *= lin; r *= lin;
    }
private:
    float atk = .99f, rel = .999f, gdb = 0;
};

inline float shapeTanh (float x)
{
    static const float inv = 1.0f / std::tanh (3.0f);
    x = clampf (x, -1.0f, 1.0f);
    return std::tanh (3.0f * x) * inv;
}

inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= .8f) return x;
    const float y = .8f + .2f * std::tanh ((a - .8f) / .2f);
    return x < 0 ? -y : y;
}

// ---------------------------------------------------------------- параметры движка (реальные единицы)
struct EngineParams
{
    float ipitch = 0, dw = .8f;
    float size = 300, dens = 20, pos = .5f, pj = .3f, pitch = 0, ps = 0, harm = 0, rev = 0, wid = .8f, glitch = 0, crush = 0;
    bool freeze = false;
    float pad = .55f, pdr = .35f, pwid = .9f, ppitch = 0, pdark = 4500, pwarm = .25f;
    float farLevel = .5f, freg = 12, fcl = .6f, fdist = .5f, fenv = 1;
    float drive = 1.5f, hp = 60, lp = 12000, dt = .4f, df = .4f, dm = .3f, rvm = .5f, vol = .7f;
};

// ---------------------------------------------------------------- движок
class Engine
{
public:
    static constexpr int kChunk = 256;
    static constexpr int kVizMax = GrainCloud::kMax;

    Engine() = default;
    ~Engine() { spec.stop(); }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        grains.prepare (sr);
        pitch.setSampleRate (sr);
        pitch.reset();
        spec.start (grains.data(), grains.length(), 0xBADC0DEu);
        wasAct = false;
        for (int c = 0; c < 2; ++c)
        {
            for (auto* f : { &hpF[c], &lpF[c], &padLp[c], &farLp[c], &fbLp[c] }) f->reset();
            dly[c].prepare ((int) (sr * 2.2));
        }
        rev.prepare (sr);
        comp.prepare (sr);
        const float c = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        smCoef = c;
        // начальные значения сглаживателей
        gDry = gWet = 0; gDrive = 1; gPost = .4f; gPadG = 0; gPadD = 1; gPadC = .33f; gFar = 0; gDm = 0; gRvm = 0; gMaster = 0; gFs = 0;
        fHp = 60; fLp = 12000; fPad = 4500; fFar = 11000; dtS = (float) (.4 * sr);
        vizIdx.store (0);
    }

    void setOffline (bool b) { spec.setOffline (b); }
    int underruns() const { return spec.underruns(); }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const EngineParams& P)
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min (kChunk, n - done);
            chunk (inL + done, inR ? inR + done : nullptr, outL + done, outR + done, m, P);
            done += m;
        }
        // снимок зёрен для визуализации
        const int wi = 1 - vizIdx.load (std::memory_order_relaxed);
        vizCount[wi] = grains.snapshot (viz[wi].data(), kVizMax);
        vizIdx.store (wi, std::memory_order_release);
    }

    // для интерфейса
    int getGrainViz (GrainCloud::VizGrain* out, int maxN) const
    {
        const int wi = vizIdx.load (std::memory_order_acquire);
        const int c = std::min (vizCount[wi], maxN);
        for (int i = 0; i < c; ++i) out[i] = viz[wi][(size_t) i];
        return c;
    }
    float levelGrain() const { return lvlGrain.load(); }
    float levelPad() const { return lvlPad.load(); }
    float levelFar() const { return lvlFar.load(); }
    float levelOut() const { return lvlOut.load(); }

private:
    inline float sm (float& v, float t) { v += (t - v) * smCoef; return v; }

    void chunk (const float* inL, const float* inR, float* outL, float* outR, int m, const EngineParams& P)
    {
        float mono[kChunk], xi[kChunk], gL[kChunk], gR[kChunk], pL[kChunk], pR[kChunk], fL[kChunk], fR[kChunk];
        for (int i = 0; i < m; ++i) mono[i] = inR ? .5f * (inL[i] + inR[i]) : inL[i];

        pitch.process (mono, xi, m, P.ipitch);

        GrainParams gp;
        gp.size = P.size; gp.dens = P.dens; gp.pos = P.pos; gp.pj = P.pj; gp.pitch = P.pitch; gp.ps = P.ps; gp.harm = P.harm;
        gp.rev = P.rev; gp.wid = P.wid; gp.glitch = P.glitch; gp.crush = P.crush; gp.freeze = P.freeze;
        grains.process (xi, gL, gR, m, gp);

        SpecParams sp;
        sp.pos = P.pos; sp.pj = P.pj; sp.pdr = P.pdr; sp.pwid = P.pwid; sp.ppitch = P.ppitch; sp.freg = P.freg; sp.fcl = P.fcl;
        sp.doPad = P.pad > .01f; sp.doFar = P.farLevel > .01f; sp.head = grains.head();
        const bool act = sp.doPad || sp.doFar;
        if (act)
        {
            if (! wasAct) spec.activate (sp);
            for (int i = 0; i < m; ++i) spec.next (pL[i], pR[i], fL[i], fR[i], sp);
        }
        else
            for (int i = 0; i < m; ++i) pL[i] = pR[i] = fL[i] = fR[i] = 0;
        wasAct = act;

        // --- цели сглаживателей (раз на блок)
        const float tDry = (float) std::cos (P.dw * kPi / 2), tWet = (float) std::sin (P.dw * kPi / 2);
        const float tDrive = P.drive, tPost = .4f / (1.0f + (P.drive - 1.0f) * .08f);
        const float tPadG = P.pad * P.pad * 1.6f, tPadD = 1.0f + P.pwarm * 7.0f, tPadC = .33f / (1.0f + P.pwarm * 2.5f);
        const float tFar = P.farLevel * P.farLevel * 1.8f * P.fenv;
        const float tDm = P.dm, tRvm = P.rvm, tMaster = P.vol * P.vol * 1.4f;
        const float tFs = .15f + P.fdist * 1.1f;
        const float cb = (float) (1.0 - std::exp (-(double) m / (0.03 * sr)));
        fHp += (P.hp - fHp) * cb; fLp += (P.lp - fLp) * cb; fPad += (P.pdark - fPad) * cb;
        const float tFarF = 1800.0f * std::pow (11000.0f / 1800.0f, 1.0f - P.fdist);
        fFar += (tFarF - fFar) * cb;
        for (int c = 0; c < 2; ++c)
        {
            hpF[c].set (sr, fHp, .7071, true); lpF[c].set (sr, fLp, .7071, false);
            padLp[c].set (sr, fPad, .7071, false); farLp[c].set (sr, fFar, .7071, false);
            fbLp[c].set (sr, 3200.0, .7071, false);
        }
        const float tDtS = (float) (P.dt * sr);
        const float df = P.df;

        double sqG = 0, sqP = 0, sqF = 0, sqO = 0;
        for (int i = 0; i < m; ++i)
        {
            const float dg = sm (gDry, tDry), wg = sm (gWet, tWet);
            const float drv = sm (gDrive, tDrive), post = sm (gPost, tPost);
            const float d = xi[i];
            float pl = shapeTanh ((d * dg + gL[i] * wg) * drv) * post;
            float pr = shapeTanh ((d * dg + gR[i] * wg) * drv) * post;

            const float pgn = sm (gPadG, tPadG), pdn = sm (gPadD, tPadD), pcn = sm (gPadC, tPadC);
            float ql = padLp[0].process (shapeTanh (pL[i] * pgn * pdn) * pcn);
            float qr = padLp[1].process (shapeTanh (pR[i] * pgn * pdn) * pcn);

            float al = lpF[0].process (hpF[0].process (pl + ql));
            float ar = lpF[1].process (hpF[1].process (pr + qr));

            const float fg = sm (gFar, tFar);
            const float fl = farLp[0].process (fL[i] * fg), fr = farLp[1].process (fR[i] * fg);

            const float inDL = al + fl, inDR = ar + fr;
            // стерео-дилей с перекрёстной обратной связью
            dtS += (tDtS - dtS) * smCoef;
            const float dmv = sm (gDm, tDm);
            const float yL = dly[0].read (dtS), yR = dly[1].read (std::min (dtS * 1.37f, (float) (1.99 * sr)));
            const float bL = fbLp[0].process (yL) * df, bR = fbLp[1].process (yR) * df;
            dly[0].write (inDL + bR);
            dly[1].write (inDR + bL);
            const float s2l = inDL * (1.0f - dmv * .4f) + yL * dmv;
            const float s2r = inDR * (1.0f - dmv * .4f) + yR * dmv;

            const float fsn = sm (gFs, tFs);
            float rl, rr;
            rev.process (s2l + fl * fsn, s2r + fr * fsn, rl, rr);
            const float rv = sm (gRvm, tRvm);
            float ol = rl * rv + s2l * (1.0f - rv * .3f);
            float orr = rr * rv + s2r * (1.0f - rv * .3f);
            const float mg = sm (gMaster, tMaster);
            ol *= mg; orr *= mg;
            comp.process (ol, orr);
            ol = softLimit (ol); orr = softLimit (orr);
            outL[i] = ol; outR[i] = orr;

            sqG += gL[i] * gL[i] + gR[i] * gR[i];
            sqP += ql * ql + qr * qr;
            sqF += fl * fl + fr * fr;
            sqO += ol * ol + orr * orr;
        }
        const double inv = 1.0 / (2.0 * m);
        lvlGrain.store ((float) std::sqrt (sqG * inv)); lvlPad.store ((float) std::sqrt (sqP * inv));
        lvlFar.store ((float) std::sqrt (sqF * inv)); lvlOut.store ((float) std::sqrt (sqO * inv));
    }

    double sr = 48000;
    GrainCloud grains;
    PitchShifter pitch;
    SpectralLayers spec;
    bool wasAct = false;
    Biquad hpF[2], lpF[2], padLp[2], farLp[2], fbLp[2];
    DelayLine dly[2];
    Reverb rev;
    Compressor comp;
    float smCoef = .001f;
    float gDry = 0, gWet = 0, gDrive = 1, gPost = .4f, gPadG = 0, gPadD = 1, gPadC = .33f, gFar = 0, gDm = 0, gRvm = 0, gMaster = 0, gFs = 0;
    float fHp = 60, fLp = 12000, fPad = 4500, fFar = 11000, dtS = 19200;

    std::array<std::array<GrainCloud::VizGrain, kVizMax>, 2> viz {};
    int vizCount[2] { 0, 0 };
    std::atomic<int> vizIdx { 0 };
    std::atomic<float> lvlGrain { 0 }, lvlPad { 0 }, lvlFar { 0 }, lvlOut { 0 };
};
} // namespace pool
