// Как мокрый сигнал ляжет в микс: наклон спектра, лишняя середина, «шумность», ровность громкости.
//   g++ -O2 -std=c++17 memoria/tests/mix_report.cpp -o /tmp/mx && /tmp/mx [фильтр-пресетов] [abl]
// Вход — аккорды с чистыми гармониками (без шума), поэтому весь шум на выходе сделан плагином.
// Признаки (по мокрому сигналу, MIX = 1, с 4-й по 20-ю секунду):
//   наклон  — дБ/окт по 1/3-октавным полосам 80 Гц…8 кГц (у «глубокого» микса около −4…−5);
//   середина— насколько полосы 800 Гц…4 кГц выше прямой наклона (дБ, плюс = выпирает);
//   шум     — спектральная плоскость в 1…6 кГц (0 = только тоны, 1 = белый шум);
//   LRA     — разброс краткосрочной громкости (дБ, 10…95 перцентиль): меньше = собраннее;
//   глубина — доля энергии ниже 250 Гц (дБ относительно всего).
// С аргументом abl для каждого пресета выключает по очереди слои, хаос, руины, зал, шиммер, призму, пинг-понг.
#include "../Source/Presets.h"
#include <algorithm>
#include <complex>
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

using namespace mem;

static float progression (double t)
{
    static const double ch[4][4] = { { 130.81, 164.81, 196.0, 246.94 }, { 110.0, 164.81, 196.0, 246.94 },
                                     { 87.31, 130.81, 164.81, 220.0 },  { 98.0, 146.83, 196.0, 329.63 } };
    t = std::fmod (t, 12.0);
    const int c = (int) (t / 3.0); const double tt = std::fmod (t, 3.0);
    double y = 0;
    for (int n = 0; n < 4; ++n)
    {
        const double on = n * .06; if (tt < on) continue;
        const double e = std::min (1.0, (tt - on) / .005) * std::exp (-(tt - on) * 1.1) * std::min (1.0, (3.0 - tt) / .03);
        for (int h = 1; ch[c][n] * h < 12000; ++h) y += std::sin (2 * M_PI * ch[c][n] * h * (tt - on) + h * h * .7) * e * std::exp (-(tt - on) * .25 * h) / (std::pow ((double) h, .75) * 1.4);
    }
    return (float) (.05 * y);
}

static const std::vector<float>& inputBuf()
{
    static std::vector<float> b;
    if (b.empty()) { b.resize ((size_t) (12 * 48000)); for (size_t i = 0; i < b.size(); ++i) b[i] = progression ((double) i / 48000); }
    return b;
}

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

struct M { double tilt, mid, noise, lra, low; };
static std::vector<double> dryDb; static std::vector<double> lastDb;

static M measure (Params p, bool dry = false)
{
    const double sr = 48000; const int B = 256;
    p.mix = 1; p.out = 1;
    Engine e; e.prepare (sr);
    std::vector<float> il (B), ol (B), orr (B), L, R;
    for (long n = 0; n < (long) (20 * sr); n += B)
    {
        for (int i = 0; i < B; ++i) il[(size_t) i] = inputBuf()[(size_t) (n + i) % inputBuf().size()];
        if (dry) { ol = il; orr = il; } else e.process (il.data(), il.data(), ol.data(), orr.data(), B, p);
        L.insert (L.end(), ol.begin(), ol.end()); R.insert (R.end(), orr.begin(), orr.end());
    }
    const size_t N = 8192; std::vector<std::complex<double>> a (N);
    std::vector<double> pw (N / 2, 0.0);
    double flat = 0; int nf = 0;
    for (size_t s = (size_t) (4 * sr); s + N <= L.size(); s += N / 2)
    {
        for (size_t i = 0; i < N; ++i) a[i] = .5 * (L[s + i] + R[s + i]) * (.5 - .5 * std::cos (2 * M_PI * i / N));
        fft (a);
        double lg = 0, ar = 0; int k0 = 0;
        for (size_t k = 1; k < N / 2; ++k)
        {
            const double m = std::norm (a[k]); pw[k] += m;
            const double f = (double) k * sr / N;
            if (f >= 1000 && f < 6000) { lg += std::log (m + 1e-20); ar += m; ++k0; }
        }
        flat += std::exp (lg / k0) / (ar / k0 + 1e-20); ++nf;
    }
    // 1/3-октавы
    std::vector<double> fc, db;
    for (double f = 80; f <= 8000; f *= std::pow (2.0, 1.0 / 3))
    {
        double s = 0; const double lo = f / std::pow (2.0, 1.0 / 6), hi = f * std::pow (2.0, 1.0 / 6);
        for (size_t k = 1; k < N / 2; ++k) { const double ff = (double) k * sr / N; if (ff >= lo && ff < hi) s += pw[k]; }
        fc.push_back (std::log2 (f / 1000)); db.push_back (10 * std::log10 (s + 1e-20));
    }
    lastDb = db; if (! dryDb.empty()) for (size_t i = 0; i < db.size(); ++i) db[i] -= dryDb[i];
    if (getenv ("CURVE") && ! dryDb.empty())
    {
        std::string o = "   кривая:"; char b[32];
        for (size_t i = 0; i < db.size(); i += 2) { std::snprintf (b, sizeof b, " %.0f:%+.0f", 1000 * std::pow (2.0, fc[i]), db[i] - db[db.size() / 2]); o += b; }
        std::puts (o.c_str());
    }
    double mx = 0, my = 0; for (size_t i = 0; i < fc.size(); ++i) { mx += fc[i]; my += db[i]; } mx /= fc.size(); my /= fc.size();
    double sxy = 0, sxx = 0; for (size_t i = 0; i < fc.size(); ++i) { sxy += (fc[i] - mx) * (db[i] - my); sxx += (fc[i] - mx) * (fc[i] - mx); }
    const double slope = sxy / sxx;
    double mid = 0; int nm = 0;
    for (size_t i = 0; i < fc.size(); ++i) { const double f = 1000 * std::pow (2.0, fc[i]); if (f >= 800 && f <= 4000) { mid += db[i] - (my + slope * (fc[i] - mx)); ++nm; } }
    double tot = 0, low = 0;
    for (size_t k = 1; k < N / 2; ++k) { tot += pw[k]; if ((double) k * sr / N < 250) low += pw[k]; }
    // краткосрочная громкость (400 мс, шаг 100 мс)
    std::vector<double> st;
    for (size_t s = (size_t) (4 * sr); s + 19200 <= L.size(); s += 4800)
    { double q = 0; for (size_t i = s; i < s + 19200; ++i) q += .5 * ((double) L[i] * L[i] + (double) R[i] * R[i]); st.push_back (10 * std::log10 (q / 19200 + 1e-12)); }
    std::sort (st.begin(), st.end());
    const double lra = st[(size_t) (st.size() * .95)] - st[(size_t) (st.size() * .10)];
    return { slope, mid / nm, flat / nf, lra, 10 * std::log10 (low / tot + 1e-12) };
}

static void row (const char* name, const M& m)
{
    std::printf ("%-36s наклон %5.1f  середина %+5.1f  шум %.3f  LRA %4.1f  низ %5.1f\n", name, m.tilt, m.mid, m.noise, m.lra, m.low);
}

int main (int argc, char** argv)
{
    const std::string filt = argc > 1 ? argv[1] : "";
    const bool abl = argc > 2 && std::string (argv[2]) == "abl";
    row ("ВХОД (сухой, абсолютно)", measure (Params(), true)); dryDb = lastDb;
    std::printf ("ниже — относительно входа: наклон = насколько темнее/светлее входа, середина = горб передачи 0.8–4 кГц\n");
    double st = 0, sm = 0, sn = 0, sl = 0, slo = 0; int n = 0;
    const auto& P = memoria::presets();
    std::vector<std::string> out (P.size());
    std::vector<M> res (P.size()); std::vector<bool> use (P.size(), false);
    std::atomic<size_t> next { 0 };
    auto worker = [&]
    {
        for (size_t i; (i = next++) < P.size();)
        {
            const auto& pr = P[i];
            if (! filt.empty() && filt != "*" && std::string (pr.name).find (filt) == std::string::npos) continue;
            use[i] = true;
            const Params p = memoria::toParams (pr);
            char buf[512]; std::string o;
            auto add = [&] (const char* name, const M& m) { std::snprintf (buf, sizeof buf, "%-36s наклон %5.1f  середина %+5.1f  шум %.3f  LRA %4.1f  низ %5.1f\n", name, m.tilt, m.mid, m.noise, m.lra, m.low); o += buf; };
            res[i] = measure (p); add (pr.name, res[i]);
            if (abl)
            {
                struct A { const char* n; void (*f) (Params&); };
                const A as[] = { { "  − слои", [] (Params& q) { q.layers = 0; } }, { "  − хаос", [] (Params& q) { q.chaos = 0; } },
                                 { "  − руины", [] (Params& q) { q.ruin = 0; } }, { "  − зал", [] (Params& q) { q.space = 0; } },
                                 { "  − шиммер", [] (Params& q) { q.shimmer = 0; } }, { "  − призма", [] (Params& q) { q.prism = 0; } },
                                 { "  − пинг-понг", [] (Params& q) { q.pingpong = 0; } }, { "  − организм", [] (Params& q) { q.organism = 0; } },
                                 { "  − саб", [] (Params& q) { q.depth = 0; } }, { "  − warmth", [] (Params& q) { q.warmth = 0; } },
                                 { "  − блюр", [] (Params& q) { q.blur = false; } }, { "  − clarity", [] (Params& q) { q.clarity = 0; } },
                                 { "  ГОЛОЕ ЯДРО", [] (Params& q) { q.layers = 0; q.space = 0; q.organism = 0; q.prism = 0; q.depth = 0; q.clarity = 0; q.pingpong = 0; q.blur = false; q.shimmer = 0; q.chaos = 0; q.ruin = 0; q.tone = .5f; } },
                                 { "  ядро без hc/lc", [] (Params& q) { q.layers = 0; q.space = 0; q.organism = 0; q.prism = 0; q.depth = 0; q.clarity = 0; q.pingpong = 0; q.blur = false; q.shimmer = 0; q.chaos = 0; q.ruin = 0; q.tone = .5f; q.highcut = 0; q.lowcut = 0; q.warmth = 0; } } };
                for (auto& x : as) { Params q = p; x.f (q); add (x.n, measure (q)); }
            }
            out[i] = o;
        }
    };
    inputBuf();
    std::vector<std::thread> th; const unsigned nt = std::max (1u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < nt; ++t) th.emplace_back (worker);
    for (auto& t : th) t.join();
    for (size_t i = 0; i < P.size(); ++i) if (use[i])
    {
        std::fputs (out[i].c_str(), stdout);
        const M& m = res[i]; st += m.tilt; sm += m.mid; sn += m.noise; sl += m.lra; slo += m.low; ++n;
    }
    if (n) std::printf ("\nСРЕДНЕЕ (%d): наклон %.2f  середина %+.2f  шум %.3f  LRA %.2f  низ %.2f\n", n, st / n, sm / n, sn / n, sl / n, slo / n);
    return 0;
}
