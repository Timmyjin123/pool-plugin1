#pragma once
// Memoria — afterimage engine: звук оставляет послеобраз, который слоится, темнеет и распадается.
// Движок без зависимостей от JUCE.
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
    float chaos = 0;       // случайные события: warp, stutter, reverse, tape stop, dropout, shuffle
    float ruin = 0;        // шумы и разрушение памяти: шипение, треск, эрозия, понижение разрядности
    int harmony = 0;       // сдвиги слоёв: 0 OCT (всегда в тональности), 1 5TH, 2 SUS, 3 7TH, 4 9TH, 5 FREE
    float core = .5f;      // CORE: сила «подписи» режима (центр кольца)
    int organism = 1;      // ORGANISM: 0 OFF, 1 TAPE, 2 CONSOLE, 3 GLASS, 4 ASH
    float space = 0;       // большой реверберационный зал: громкость и длина хвоста (1.5 … 30 с)
    float shimmer = 0;     // хвост реверба поднимается на октаву (по HARMONY) и каскадом уходит вверх
    float prism = -1;      // переливы спектра; −1 — берётся из DRIFT (так в плагине)
    float tailLo = 40;     // фильтр хвостов: срез низа, Гц
    float tailHi = 16000;  // фильтр хвостов: срез верха, Гц
    float clarity = -1;    // автокоррекция середины и мягкие атаки; −1 — автоматически (от SPACE)
};

inline const char* chaosName (int t)
{
    static const char* n[7] = { "", "WARP", "STUTTER", "REVERSE", "WOW", "DROPOUT", "SHUFFLE" };
    return n[std::clamp (t, 0, 6)];
}

// Хаос: случайные события поверх мокрого сигнала (своя короткая память на 4.5 с)
struct ChaosStage
{
    std::vector<float> bl, br;
    int cap = 1, w = 0;
    double sr = 48000;
    float dBase = 200;
    int type = 0; long t = 0, dur = 0;
    float m = 0, mAtk = 0, mRel = 0;
    double start = 0, pos = 0;
    float stLen = 0, warpDepth = 0, warpPh = 0, warpRate = 0, dropDepth = 1, dropG = 1, dropRate = 0;
    float wowPh = 0, flPh = 0;
    int seedOffset = 0;
    long fired = 0;
    Rng rng { 0x51ed270bu };

    void prepare (double s)
    {
        sr = s;
        cap = (int) (4.5 * sr);
        bl.assign ((size_t) cap, 0.0f); br.assign ((size_t) cap, 0.0f);
        w = 0; type = 0; m = 0; dropG = 1;
        dBase = (float) (.006 * sr);
        mAtk = rate (.004, sr); mRel = rate (.0015, sr);
        dropRate = rate (.004, sr);
    }
    float rd (const std::vector<float>& b, double idx) const
    {
        while (idx < 0) idx += cap;
        while (idx >= cap) idx -= cap;
        const int i0 = (int) idx; const float fr = (float) (idx - i0);
        const int i1 = i0 + 1 >= cap ? 0 : i0 + 1;
        return b[(size_t) i0] * (1 - fr) + b[(size_t) i1] * fr;
    }
    void trigger()
    {
        // веса событий: warp и stutter чаще, shuffle реже
        static const int T[12] = { 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6 };
        type = T[rng.next() % 12u];
        t = 0; ++fired;
        const double now = (double) w - dBase;
        switch (type)
        {
            case 1: dur = (long) ((1.5 + 2.5 * rng.uni()) * sr); warpDepth = (float) ((.004 + .012 * rng.uni()) * sr);
                    warpRate = (float) (2 * 3.14159265358979 * (.3 + .8 * rng.uni()) / sr); warpPh = 0; break;
            case 2: stLen = (float) ((.08 + .32 * rng.uni()) * sr); dur = (long) ((.5 + 2.0 * rng.uni()) * sr);
                    start = now - stLen; pos = 0; break;
            case 3: dur = (long) ((.4 + .5 * rng.uni()) * sr); start = now; break;
            case 4: dur = (long) ((2.0 + 3.0 * rng.uni()) * sr); warpDepth = (float) ((.0008 + .0017 * rng.uni()) * sr);
                    warpRate = (float) (2 * 3.14159265358979 * (.5 + .6 * rng.uni()) / sr); warpPh = 0; break;   // плавающий строй
            case 5: dur = (long) ((.05 + .35 * rng.uni()) * sr); dropDepth = .05f + .35f * rng.uni(); break;
            default: dur = (long) (.3 * sr); seedOffset = 1 + (int) (rng.next() % 997u); break;
        }
    }
    void process (float& L, float& R, float chaos)
    {
        bl[(size_t) w] = L; br[(size_t) w] = R;
        wowPh += (float) (2 * 3.14159265358979 * .45 / sr); if (wowPh > 6.2831853f) wowPh -= 6.2831853f;
        flPh += (float) (2 * 3.14159265358979 * 6.3 / sr);  if (flPh > 6.2831853f) flPh -= 6.2831853f;
        const float wob = chaos * (float) (.0012 * sr) * (.8f * std::sin (wowPh) + .2f * std::sin (flPh));
        const double nd = (double) dBase + (double) wob;
        float oL = rd (bl, (double) w - nd), oR = rd (br, (double) w - nd);

        const float lam = chaos * chaos * .6f;                 // событий в секунду
        if (type == 0 && chaos > 1e-3f && rng.uni() < lam / (float) sr) trigger();

        float gain = 1;
        if (type != 0)
        {
            ++t;
            const bool on = t < dur;
            m += ((on ? 1.0f : 0.0f) - m) * (on ? mAtk : mRel);
            double idx = (double) w - nd;
            float win = 1;
            switch (type)
            {
                case 1: warpPh += warpRate; idx = (double) w - nd - warpDepth * (.5 - .5 * std::cos (warpPh)); break;
                case 2:
                {
                    pos += 1; if (pos >= stLen) pos -= stLen;
                    idx = start + pos;
                    const float f = (float) (.004 * sr);
                    win = std::min (1.0f, std::min ((float) pos / f, (stLen - (float) pos) / f));
                    break;
                }
                case 3: idx = start - (double) t; break;
                case 4:
                {
                    warpPh += warpRate;
                    const double fl = .15 * std::sin (warpPh * 11.3);   // лёгкое дрожание поверх качания
                    idx = (double) w - nd - warpDepth * (1.0 + std::sin (warpPh) + fl);
                    break;
                }
                case 5: dropG += ((on ? dropDepth : 1.0f) - dropG) * dropRate; gain = dropG; break;
                default: break;
            }
            if (type >= 1 && type <= 4)
            {
                const float eL = rd (bl, idx) * win, eR = rd (br, idx) * win;
                oL += m * (eL - oL); oR += m * (eR - oR);
            }
            if (! on && m < 1e-3f && (type != 5 || dropG > .999f)) { type = 0; m = 0; dropG = 1; }
        }
        L = oL * gain; R = oR * gain;
        if (++w >= cap) w = 0;
    }
};

// Характер слоя: полоса частот, сдвиг высоты, текстура
struct LayerChar { int band = 1; int semis = 0; int tex = 0; };
inline const char* bandName (int b) { static const char* n[4] = { "DARK", "MID", "BRIGHT", "AIR" }; return n[std::clamp (b, 0, 3)]; }
inline const char* texName (int t) { static const char* n[4] = { "CLEAN", "TAPE", "FOLD", "CRUSH" }; return n[std::clamp (t, 0, 3)]; }
inline const char* harmonyName (int h) { static const char* n[6] = { "OCT", "5TH", "SUS", "7TH", "9TH", "FREE" }; return n[std::clamp (h, 0, 5)]; }

// Интервалы для гранул и шиммера по выбранной гармонии (отношения частот)
inline int harmonyRatios (int h, float* out)
{
    static const float R[6][6] = { { .5f, 2.0f }, { .5f, 2.0f, 1.4983071f, .7491535f }, { .5f, 2.0f, 1.3348399f, 1.1224620f },
                                   { .5f, 2.0f, 1.7817974f, .8908987f }, { .5f, 2.0f, 1.1224620f, 2.2449241f },
                                   { .5f, 2.0f, 1.3348399f, 1.4983071f, .7491535f, 1.1892071f } };
    static const int N[6] = { 2, 4, 4, 4, 4, 6 };
    h = std::clamp (h, 0, 5);
    for (int i = 0; i < N[h]; ++i) out[i] = R[h][i];
    return N[h];
}
inline float shimmerRatio (int h)
{
    static const float r[6] = { 2.0f, 1.4983071f, 1.4983071f, 1.7817974f, 2.2449241f, 1.4983071f };
    return r[std::clamp (h, 0, 5)];
}

inline LayerChar layerChar (int seed, int k, int harmony = 5)
{
    harmony = std::clamp (harmony, 0, 5);
    static const LayerChar def[6][4] = { { { 0, -12, 1 }, { 1, 0, 0 }, { 2, 12, 0 }, { 3, 24, 1 } },
                                         { { 0, -12, 1 }, { 1, 0, 0 }, { 2, 12, 0 }, { 3, 19, 1 } },
                                         { { 0, -12, 1 }, { 1, 5, 0 }, { 2, 14, 0 }, { 3, 19, 1 } },
                                         { { 0, -12, 1 }, { 1, 10, 0 }, { 2, 12, 0 }, { 3, 19, 1 } },
                                         { { 0, -12, 1 }, { 1, 2, 0 }, { 2, 14, 0 }, { 3, 19, 1 } },
                                         { { 0, -12, 1 }, { 1, 0, 0 }, { 2, 12, 0 }, { 3, 7, 1 } } };
    if (seed <= 0) return def[harmony][k & 3];
    Rng r ((uint32_t) seed * 2654435761u + 12345u);
    for (int i = 0; i < 4; ++i) r.next();
    int perm[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; --i) std::swap (perm[i], perm[r.next() % (uint32_t) (i + 1)]);   // полосы всегда разные
    // высота зависит от полосы: тёмный слой держит низ, вверх уходят только яркие
    // OCT: только октавы — мелодия остаётся в своей тональности при любом наборе
    static const int OD[6] = { -12, -12, -24, 0, -12, 0 }, OM[6] = { 0, 0, 0, -12, 12, 0 },
                     OB[6] = { 0, 12, 12, 12, 24, 0 },     OA[6] = { 12, 24, 12, 24, 0, 12 };
    // 5TH: октавы + квинта (того же звукоряда класс высоты: +7, +19, −5, −17)
    static const int FD[6] = { -12, -12, -24, 0, -5, -17 }, FM[6] = { 0, 0, -5, 7, -12, 0 },
                     FB[6] = { 0, 12, 12, 7, 19, 12 },      FA[6] = { 12, 19, 24, 12, 19, 7 };
    // FREE: любые интервалы, включая кварты
    static const int PD[6] = { -12, -12, -24, 0, -5, -7 };
    static const int PM[6] = { 0, 0, -5, 5, 7, -12 };
    static const int PB[6] = { 0, 12, 12, 7, 19, 5 };
    static const int PA[6] = { 12, 7, 0, 12, 19, 24 };
    // SUS: секунда и кварта (+2, +5, +14, +17, −7, −10)
    static const int SD[6] = { -12, -12, -24, -7, -10, 0 }, SM[6] = { 0, 5, -7, 2, 0, 7 },
                     SB[6] = { 12, 14, 5, 17, 12, 7 },     SA[6] = { 12, 19, 17, 24, 14, 12 };
    // 7TH: малая септима (+10, −2, +22)
    static const int TD[6] = { -12, -14, -24, -2, 0, -12 }, TM[6] = { 0, 10, -2, 7, 0, -5 },
                     TB[6] = { 12, 10, 22, 7, 19, 12 },      TA[6] = { 22, 12, 19, 24, 10, 12 };
    // 9TH: нона (+14, +2, −10)
    static const int ND[6] = { -12, -10, -24, 0, -12, -5 }, NM[6] = { 0, 2, 7, -10, 14, 0 },
                     NB[6] = { 14, 12, 7, 19, 2, 12 },      NA[6] = { 14, 26, 19, 12, 24, 7 };
    const int* const sets[6][4] = { { OD, OM, OB, OA }, { FD, FM, FB, FA }, { SD, SM, SB, SA },
                                    { TD, TM, TB, TA }, { ND, NM, NB, NA }, { PD, PM, PB, PA } };
    // текстуры: чаще чистая и лента, складка и крош — редкая приправа
    static const int TX[8] = { 0, 0, 0, 1, 1, 1, 2, 3 };
    LayerChar c[4];
    for (int i = 0; i < 4; ++i)
    {
        c[i].band = perm[i];
        const int* set = sets[harmony][perm[i]];
        c[i].semis = set[r.next() % 6u];
        c[i].tex = TX[r.next() % 8u];
    }
    return c[k & 3];
}

// Сдвиг высоты (WSOLA): две головки задержки; при перезапуске головка ищет смещение,
// в котором она совпадает по фазе с соседней — тональный звук сдвигается без призвуков.
struct Shifter
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    int L = 2880, P = 768, N = 384;         // жизнь головки, поиск фазы (~66 Гц), окно сравнения
    float d[2] { 2, 2 };
    int age[2] { 0, 0 };
    void prepare (double sr, double life = .06, double search = .016, double cmp = .008)
    {
        L = (int) (life * sr); P = (int) (search * sr); N = (int) (cmp * sr);
        const int need = 3 * L + P + N + 16;   // запас под сдвиг до +24 (ratio 4)
        int n = 1; while (n < need) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
        age[0] = 0; age[1] = L / 2; d[0] = d[1] = 2;
    }
    float at (float delay) const
    {
        float pos = (float) w - delay;
        while (pos < 0) pos += (float) (mask + 1);
        const int i0 = (int) pos; const float fr = pos - (float) i0;
        return buf[(size_t) (i0 & mask)] * (1 - fr) + buf[(size_t) ((i0 + 1) & mask)] * fr;
    }
    void restart (int i, float ratio)
    {
        const float base = ratio > 1 ? 2.0f + (ratio - 1.0f) * (float) L : 2.0f;
        const float other = d[1 - i];
        int bestO = 0; double best = -1e30;
        for (int o = 0; o < P; o += 2)
        {
            double c = 0;
            const int da = (int) (base + (float) o), db = (int) other;
            for (int j = 0; j < N; j += 2)
                c += (double) buf[(size_t) ((w - da - j) & mask)] * buf[(size_t) ((w - db - j) & mask)];
            if (c > best) { best = c; bestO = o; }
        }
        d[i] = base + (float) bestO;
        age[i] = 0;
    }
    float process (float x, float ratio)
    {
        buf[(size_t) w] = x;
        float y = 0;
        for (int i = 0; i < 2; ++i)
        {
            if (age[i] >= L) restart (i, ratio);
            const float ph = (float) age[i] / (float) L;
            const float s = std::sin (3.14159265f * ph);
            y += at (d[i]) * s * s;
            d[i] += 1.0f - ratio;
            if (d[i] < 1.0f) d[i] = 1.0f;
            ++age[i];
        }
        w = (w + 1) & mask;
        return y;
    }
};

// ORGANISM: «живая кожа» звука. Mid/Side, тёплый лестничный фильтр с насыщением, полки, дыхание
// (пока играешь — открыто и уже, в хвосте — темнее, шире, плотнее), уступающие частоты и нарастание хвостов.
inline const char* organismName (int o) { static const char* n[5] = { "OFF", "TAPE", "CONSOLE", "GLASS", "ASH" }; return n[std::clamp (o, 0, 4)]; }
struct OrgChar
{
    float body, bodyHz, dip, dipHz, open, closed, res, drive, sideHp, air, wPlay, wTail, yield, swell;
};
inline const OrgChar& orgChar (int o)
{
    //                                 body bodyHz dip  dipHz  open   closed res  drive sideHp air   wPlay wTail yield swell
    static const OrgChar c[5] = { {     0,  150,   0,  1000, 20000, 20000,  0,  1.0f,  20,   0,   1.0f, 1.0f,  0,   0   },
                                  { 3.0f, 180, 1.5f, 1200, 14000,  3500, .15f, 1.4f, 220, 1.0f, 1.0f, 1.25f, .5f, .6f },
                                  { 1.5f, 120, 2.5f,  900, 18000,  6000, .05f, 1.8f, 150, 2.0f, 1.05f,1.15f, .4f, .3f },
                                  {   0,  150, 3.5f, 1400, 20000,  9000, .10f, 1.1f, 380, 4.5f, 1.15f,1.7f,  .7f, .9f },
                                  { 4.0f, 220, 4.0f,  800,  7000,  1600, .35f, 2.2f, 300, -3.0f,1.0f, 1.35f, .6f, 1.0f } };
    return c[std::clamp (o, 0, 4)];
}

struct Organism
{
    double sr = 48000;
    float breath = 1, mix = 0;              // дыхание 1 — открыт (играешь), 0 — выдох (хвост)
    float body = 0, dipLow = 0, dipBp = 0, dipIc1 = 0, dipIc2 = 0;
    float lad[4] {}, ladY = 0;
    float sHp1 = 0, sHp2 = 0, airLp = 0;
    float bLo[2] {}, bHi[2] {}, eD[3] {}, eW[3] {}, gY[3] { 1, 1, 1 };
    float drift = 0, driftT = 0; int driftCnt = 0;
    Rng rng { 0x0badf00du };
    float aIn = 0, aOut = 0, aEnv = 0, aEnvR = 0, aG = 0, aSm = 0;
    OrgChar cur = orgChar (0);
    // коэффициенты обновляются раз в 16 сэмплов
    int cCnt = 0;
    float cBody = 0, gBody = 0, dA1 = 0, dA2 = 0, dA3 = 0, dK = 0, dGain = 0, cLad = 0, cSide = 0, gAir = 0, wNow = 1;
    float c300 = 0, c2500 = 0, c6500 = 0;
    void prepare (double s)
    {
        sr = s; breath = 1; mix = 0;
        body = dipLow = dipBp = dipIc1 = dipIc2 = ladY = sHp1 = sHp2 = airLp = 0;
        for (auto& v : lad) v = 0;
        for (int i = 0; i < 3; ++i) { eD[i] = eW[i] = 0; gY[i] = 1; }
        bLo[0] = bLo[1] = bHi[0] = bHi[1] = 0;
        auto tc = [this] (double sec) { return (float) (1.0 - std::exp (-1.0 / (sec * sr))); };
        aIn = tc (.3); aOut = tc (2.5); aEnv = tc (.01); aEnvR = tc (.2); aG = tc (.05); aSm = tc (.15);
        c300 = op (300.0f); c2500 = op (2500.0f); c6500 = op (6500.0f); cCnt = 0;
    }
    float op (float fc) const { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * std::min ((double) fc, sr * .45) / sr)); }
    // play — 1 пока играешь, 0 в хвосте; dryMono — сухой сигнал для «уступающих частот»
    void process (float& L, float& R, int character, float play, float dryMono)
    {
        // плавная смена характера (без щелчков): параметры скользят к новому характеру
        const OrgChar& t = orgChar (character);
        const float* tp = &t.body; float* cp = &cur.body;
        for (int i = 0; i < 14; ++i) cp[i] += (tp[i] - cp[i]) * aSm * 16.0f / 16.0f;
        mix += ((character > 0 ? 1.0f : 0.0f) - mix) * aSm;
        if (mix < 1e-4f && character == 0) return;
        breath += (play - breath) * (play > breath ? aIn : aOut);
        if (--driftCnt <= 0) { driftCnt = 4800; driftT = rng.bi() * .03f; }   // разброс «деталей» ±3%
        drift += (driftT - drift) * .0005f;

        float m = .5f * (L + R), sd = .5f * (L - R);
        const float m0 = m, s0 = sd;
        const float b = breath, tail = 1 - b;
        if (--cCnt <= 0)
        {
            cCnt = 16;
            cBody = op (cur.bodyHz);
            gBody = std::pow (10.0f, cur.body * (.6f + .4f * tail) / 20.0f) - 1.0f;
            const float g = std::tan (3.14159265f * std::min (cur.dipHz, (float) sr * .45f) / (float) sr);
            dK = 1.2f; dA1 = 1 / (1 + g * (g + dK)); dA2 = g * dA1; dA3 = g * dA2;
            dGain = dK * (1.0f - std::pow (10.0f, -cur.dip / 20.0f));
            cLad = op ((cur.closed + (cur.open - cur.closed) * b * b) * (1.0f + drift));
            cSide = op (cur.sideHp);
            gAir = std::pow (10.0f, cur.air / 20.0f) - 1.0f;
            wNow = cur.wPlay + (cur.wTail - cur.wPlay) * tail;
        }
        // --- центр: тело внизу, провал «в носу», тёплый лестничный фильтр
        body += (m - body) * cBody;
        m += body * gBody;
        {   // провал: полосовой SVF вокруг dipHz
            const float v3 = m - dipIc2, v1 = dA1 * dipIc1 + dA2 * v3, v2 = dipIc2 + dA2 * dipIc1 + dA3 * v3;
            dipIc1 = 2 * v1 - dipIc1; dipIc2 = 2 * v2 - dipIc2;
            m -= v1 * dGain;
        }
        {   // лестничный фильтр: 4 ступени с насыщением, срез дышит
            const float g = cLad;
            const float dr = cur.drive;
            float x = m - cur.res * 3.2f * ladY;
            auto ft = [] (float v) { v = std::clamp (v, -3.0f, 3.0f); return v * (27.0f + v * v) / (27.0f + 9.0f * v * v); };   // быстрый tanh
            for (int i = 0; i < 4; ++i) { lad[i] += g * (ft (x * dr) / dr - lad[i]); x = lad[i]; }
            ladY = x;
            m = x * (1.0f + cur.res * .8f);
        }
        // --- бока: низ остаётся в центре, сверху воздух, ширина дышит
        sHp1 += (sd - sHp1) * cSide; float sh = sd - sHp1;
        sHp2 += (sh - sHp2) * cSide; sh -= sHp2;
        airLp += (sh - airLp) * c6500; const float airPart = sh - airLp;
        sh += airPart * gAir;
        sh *= wNow;
        // --- уступающие частоты: где играет новое, хвост отступает (3 полосы, до −6 дБ)
        if (cur.yield > 1e-3f)
        {
            float dB[3], wB[3];
            bLo[0] += (dryMono - bLo[0]) * c300; bHi[0] += (dryMono - bHi[0]) * c2500;
            dB[0] = bLo[0]; dB[1] = bHi[0] - bLo[0]; dB[2] = dryMono - bHi[0];
            bLo[1] += (m - bLo[1]) * c300; bHi[1] += (m - bHi[1]) * c2500;
            wB[0] = bLo[1]; wB[1] = bHi[1] - bLo[1]; wB[2] = m - bHi[1];
            float out = 0;
            for (int i = 0; i < 3; ++i)
            {
                const float ed = dB[i] * dB[i], ew = wB[i] * wB[i];
                eD[i] += (ed - eD[i]) * (ed > eD[i] ? aEnv : aEnvR);
                eW[i] += (ew - eW[i]) * (ew > eW[i] ? aEnv : aEnvR);
                const float gt = std::max (.5f, 1.0f / (1.0f + cur.yield * 1.5f * eD[i] / (eW[i] + 1e-7f)));
                gY[i] += (gt - gY[i]) * aG;
                out += wB[i] * gY[i];
            }
            m = out;
        }
        L = m0 + s0 + mix * ((m + sh) - (m0 + s0));
        R = m0 - s0 + mix * ((m - sh) - (m0 - s0));
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

// SPACE: плотный зал на 8 линиях (FDN) с модуляцией, двухполосным затуханием и шиммером
struct Reverb
{
    static constexpr int NL = 8;
    std::vector<float> line[NL];
    int len[NL] {}, cap[NL] {}, w[NL] {};
    float lenF[NL] {};   // фактическая длина: плавно скользит к len, без скачков
    float lpD[NL] {}, lpL[NL] {}, modPh[NL] {}, modInc[NL] {};
    float g[NL] {}, gLow[NL] {};
    std::vector<float> pre[2]; int preCap = 1, preW = 0;
    Allpass diff[2][4];
    Shifter shim;
    float shimLp = 0, shimFb = 0, shimPre1 = 0, shimPre2 = 0, shimLp2 = 0, aShimPre = 0;
    float envG = 0, guard = 1, aGuardAtk = 0, aGuardRel = 0;
    double sr = 48000;
    float aDamp = 0, aLow = 0, aShim = 0, modDepth = 0;
    float lastRt = -1, lastSize = -1;

    void prepare (double s)
    {
        sr = s;
        static const double base[NL] = { 41.3, 53.9, 61.7, 73.1, 84.7, 97.3, 109.9, 127.1 };
        for (int i = 0; i < NL; ++i)
        {
            cap[i] = (int) (base[i] * 2.3 * .001 * sr) + 64;
            line[i].assign ((size_t) cap[i], 0.0f);
            w[i] = 0; lpD[i] = lpL[i] = 0;
            modPh[i] = (float) i * .7f;
            modInc[i] = (float) (2 * 3.14159265358979 * (.07 + .045 * i) / sr);
            len[i] = (int) (base[i] * .001 * sr);
            lenF[i] = (float) len[i];
        }
        preCap = (int) (.16 * sr) + 4;
        for (auto& b : pre) b.assign ((size_t) preCap, 0.0f);
        preW = 0;
        static const double dms[4] = { 4.7, 6.3, 9.1, 12.7 };
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 4; ++j) diff[c][j].prepare ((int) (dms[j] * (c ? 1.13 : 1.0) * .001 * sr));
        shim.prepare (sr);
        shimLp = shimFb = 0;
        auto op = [this] (double fc) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * fc / sr)); };
        aLow = op (250); aShim = op (7000); aShimPre = op (4500);
        aGuardAtk = (float) (1.0 - std::exp (-1.0 / (.002 * sr))); aGuardRel = (float) (1.0 - std::exp (-1.0 / (.6 * sr)));
        envG = 0; guard = 1;
        shimPre1 = shimPre2 = shimLp2 = 0;
        modDepth = (float) (.0006 * sr);
        lastRt = lastSize = -1;
    }
    void configure (float rt, float size, float damp)
    {
        if (std::fabs (rt - lastRt) > .01f || std::fabs (size - lastSize) > .002f)
        {
            static const double base[NL] = { 41.3, 53.9, 61.7, 73.1, 84.7, 97.3, 109.9, 127.1 };
            for (int i = 0; i < NL; ++i)
            {
                len[i] = std::min (cap[i] - (int) modDepth - 8, (int) (base[i] * size * .001 * sr));
                const double t = (double) len[i] / sr;
                g[i] = (float) std::pow (10.0, -3.0 * t / rt);
                gLow[i] = (float) std::pow (10.0, -3.0 * t / (rt * 1.5));   // низ звучит в полтора раза дольше
            }
            if (lastRt < 0) for (int i = 0; i < NL; ++i) lenF[i] = (float) len[i];
            lastRt = rt; lastSize = size;
        }
        aDamp = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * damp / sr));
    }
    float rd (int i, float delay) const
    {
        float pos = (float) w[i] - delay;
        while (pos < 0) pos += (float) cap[i];
        const int i0 = (int) pos; const float fr = pos - (float) i0;
        const int i1 = i0 + 1 >= cap[i] ? 0 : i0 + 1;
        return line[i][(size_t) i0] * (1 - fr) + line[i][(size_t) i1] * fr;
    }
    // inL/inR — что попадает в зал; shimRatio — интервал шиммера; возвращает стерео хвост
    void process (float inL, float inR, float preDelay, float shimmer, float shimRatio, float& oL, float& oR)
    {
        pre[0][(size_t) preW] = inL; pre[1][(size_t) preW] = inR;
        int rp = preW - (int) preDelay; if (rp < 0) rp += preCap;
        float x[2] = { pre[0][(size_t) rp], pre[1][(size_t) rp] };
        if (++preW >= preCap) preW = 0;
        for (int c = 0; c < 2; ++c) for (auto& d : diff[c]) x[c] = d.process (x[c], .62f);

        float v[NL];
        for (int i = 0; i < NL; ++i)
        {
            modPh[i] += modInc[i]; if (modPh[i] > 6.2831853f) modPh[i] -= 6.2831853f;
            const float dl = (float) len[i] - lenF[i];
            lenF[i] += std::clamp (dl, -.03f, .03f);   // ≤ 3% скорости: смена размера зала звучит как мягкий проезд, без щелчка
            v[i] = rd (i, lenF[i] + modDepth * (1.0f + std::sin (modPh[i])));
        }
        // шиммер: сумма хвоста, сдвинутая вверх, снова попадает в зал
        float sh = 0;
        if (shimmer > 1e-4f)
        {
            float sum = 0; for (int i = 0; i < NL; ++i) sum += v[i];
            // до сдвига — только ниже ~4.5 кГц (2 полюса), после — ещё 2 полюса: каскад не уходит к пределу слышимого
            shimPre1 += (sum * .25f - shimPre1) * aShimPre; shimPre2 += (shimPre1 - shimPre2) * aShimPre;
            const float up = shim.process (shimPre2, shimRatio);
            shimLp += (up - shimLp) * aShim; shimLp2 += (shimLp - shimLp2) * aShim;
            sh = shimLp2 * shimmer * .32f;
        }
        // матрица Адамара 8×8 (без потерь энергии)
        // страховка от самовозбуждения: если энергия в линиях растёт выше нормы, обратная связь плавно слабеет
        float en = 0; for (int i = 0; i < NL; ++i) en += v[i] * v[i];
        en *= 1.0f / NL;
        envG += (en - envG) * (en > envG ? aGuardAtk : aGuardRel);
        const float gT = envG > .25f ? std::sqrt (.25f / envG) : 1.0f;
        guard += (gT - guard) * (gT < guard ? aGuardAtk : aGuardRel);
        const float shimDecay = 1.0f - .1f * shimmer;   // шиммер чуть укорачивает хвост: каскад не копится
        float h[NL];
        for (int i = 0; i < NL; ++i) h[i] = v[i];
        for (int len2 = 1; len2 < NL; len2 <<= 1)
            for (int i = 0; i < NL; i += len2 << 1)
                for (int j = i; j < i + len2; ++j) { const float a = h[j], b = h[j + len2]; h[j] = a + b; h[j + len2] = a - b; }
        const float nrm = .35355339f;
        for (int i = 0; i < NL; ++i)
        {
            float y = h[i] * nrm;
            lpL[i] += (y - lpL[i]) * aLow;                         // низ
            lpD[i] += (y - lpD[i]) * aDamp;                        // верх гаснет быстрее
            y = ((lpD[i] - lpL[i]) * g[i] + lpL[i] * gLow[i]) * guard * shimDecay;
            y += (i & 1 ? x[1] : x[0]) * .5f + sh * (i < 4 ? 1.0f : -1.0f) * .5f;
            if (y > 1.5f) y = 1.5f + std::tanh (y - 1.5f); else if (y < -1.5f) y = -1.5f + std::tanh (y + 1.5f);   // предохранитель
            line[i][(size_t) w[i]] = y;
            if (++w[i] >= cap[i]) w[i] = 0;
        }
        oL = (v[0] - v[1] + v[2] - v[3] + v[4] - v[5] + v[6] - v[7]) * .3f;
        oR = (v[0] + v[1] - v[2] - v[3] + v[4] + v[5] - v[6] - v[7]) * .3f;
    }
};

// PRISM: 5 резонансных полос, каждая медленно плывёт в своём диапазоне
struct Prism
{
    struct Band { float ic1[2] {}, ic2[2] {}; float lo = 100, hi = 300, ph = 0, inc = 0, f = 200; float g = 0, k = .3f, a1 = 0, a2 = 0, a3 = 0; };
    Band b[5];
    double sr = 48000;
    int cnt = 0;
    void prepare (double s)
    {
        sr = s;
        static const float lo[5] = { 90, 260, 700, 1900, 5000 }, hi[5] = { 260, 700, 1900, 5000, 11000 };
        static const float rateHz[5] = { .031f, .047f, .059f, .073f, .089f };
        for (int i = 0; i < 5; ++i)
        {
            b[i] = Band();
            b[i].lo = lo[i]; b[i].hi = hi[i]; b[i].ph = (float) i * 1.3f;
            b[i].inc = (float) (2 * 3.14159265358979 * rateHz[i] / sr * 16);
            b[i].k = 1.0f / (2.5f + (float) i * .6f);
        }
        cnt = 0;
    }
    void process (float& L, float& R, float amt, float wander)
    {
        if (cnt-- <= 0)
        {
            cnt = 15;
            for (auto& q : b)
            {
                q.ph += q.inc * (1 + wander);
                const float t = .5f + .5f * std::sin (q.ph) * (.6f + .4f * std::sin (q.ph * .37f + 1));
                q.f = q.lo * std::pow (q.hi / q.lo, t);
                q.g = std::tan (3.14159265f * std::min (q.f, (float) sr * .45f) / (float) sr);
                q.a1 = 1 / (1 + q.g * (q.g + q.k)); q.a2 = q.g * q.a1; q.a3 = q.g * q.a2;
            }
        }
        float in[2] = { L, R }, sum[2] = { 0, 0 };
        for (int i = 0; i < 5; ++i)
        {
            auto& q = b[i];
            for (int c = 0; c < 2; ++c)
            {
                const float v3 = in[c] - q.ic2[c];
                const float v1 = q.a1 * q.ic1[c] + q.a2 * v3;
                const float v2 = q.ic2[c] + q.a2 * q.ic1[c] + q.a3 * v3;
                q.ic1[c] = 2 * v1 - q.ic1[c]; q.ic2[c] = 2 * v2 - q.ic2[c];
                // полосы чуть по-разному в каналах: шире стерео
                const float pan = (i & 1) ? (c ? .75f : 1.0f) : (c ? 1.0f : .75f);
                sum[c] += v1 * q.k * pan;
            }
        }
        L = in[0] * (1 - .55f * amt) + sum[0] * 2.2f * amt;
        R = in[1] * (1 - .55f * amt) + sum[1] * 2.2f * amt;
    }
    float freq (int i) const { return b[i].f; }
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
        subShift.prepare (sr, .10, .026, .012);     // бас: длинное окно, поиск фазы до ~38 Гц
        subShift2.prepare (sr, .12, .030, .014);
        k.bass = op (400); k.sub2 = op (90); k.monoLo = op (120);
        k.cl300 = op (300); k.cl3000 = op (3000);
        auto tc = [this] (double sec) { return (float) (1.0 - std::exp (-1.0 / (sec * sr))); };
        k.clEnv = tc (.05); k.clAtk = tc (.02); k.clRel = tc (.3);
        k.trFast = tc (.001); k.trSlow = tc (.04); k.trAtk = tc (.0005); k.trRel = tc (.03);
        k.playRate = tc (1.2);
        k.swFast = tc (.005); k.swSlow = tc (.3); k.swDrop = tc (.012);
        k.tapeWow = (float) (2 * 3.14159265358979 * .63 / sr); k.tapeFl = (float) (2 * 3.14159265358979 * 6.7 / sr);
        k.tapeDepth = (float) (.004 * sr); k.tapeHf = op (5500);
        k.chFade = tc (.008);
        k.agcEnv = tc (2.0); k.agcDown = tc (1.5); k.agcUp = tc (3.0);
        agcEnv = 0; agcG = 1; chFade = 1; chPending = false;
        k.envAtkS = (float) (1.0 - std::exp (-1.0 / (.004 * sr))); k.envRelS = (float) (1.0 - std::exp (-1.0 / (.25 * sr)));
        k.envSlowS = (float) (1.0 - std::exp (-1.0 / (1.5 * sr)));
        bassLp[0] = bassLp[1] = 0; sub2Lp[0] = sub2Lp[1] = 0; monoLp[0] = monoLp[1] = 0; subEnv = subEnvSlow = 0;
        chaosSt.prepare (sr);
        reverb.prepare (sr);
        prism.prepare (sr);
        organism.prepare (sr);
        for (int j = 0; j < 4; ++j) { swF[j] = swS[j] = 0; swG[j] = 1; }
        hallSwF = hallSwS = 0; hallSwG = 1;
        for (int c = 0; c < 2; ++c) { pinkB[c][0] = pinkB[c][1] = pinkB[c][2] = 0; crackEnv[c] = 0; crackLp[c] = 0; }
        k.crackDecay = (float) std::exp (-1.0 / (.00025 * sr));
        k.crackLp = op (6000);
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
            // новый характер слоя не включается мгновенно: слои уходят в ноль (~30 мс), меняются и возвращаются
            const LayerChar nc = layerChar (chaosSt.seedOffset ? (p.layerSeed + chaosSt.seedOffset) % 1000 + 1 : p.layerSeed, j, p.harmony);
            if (nc.band != ch[j].band || nc.semis != ch[j].semis || nc.tex != ch[j].tex) { pendingCh[j] = nc; chPending = true; }
            if (chFade <= .01f && chPending) { ch[j] = pendingCh[j]; }
            deg[j].band = ch[j].band;
            ratio[j] = std::pow (2.0f, (float) ch[j].semis / 12.0f);
            static const float ppT[4] = { .31f, .47f, .67f, .89f };
            const float sc = curSize == 2 ? .55f : (curSize == 1 ? .8f : 1.0f);
            pong[j].len = std::min (pong[j].cap - 1, std::max (16, (int) (ppT[j] * sc * sr)));
        }
        if (chFade <= .01f && chPending) chPending = false;
        if (curMode == 0) deg[0].band = -1;   // у гранул одна петля: окраска слоёв только на выходе
        const float bfT = p.blur ? 1.0f : 0.0f;
        {
            const float sp = p.space;
            const float rt = 1.5f + sp * sp * 28.0f;
            const float size = 1.0f + 1.2f * sp;
            const float damp = 5500.0f + 11000.0f * std::clamp (p.tone, 0.0f, 1.0f);   // хвост зала светлый, верх гаснет мягко
            reverb.configure (rt, size, damp);
            preDelay = (float) ((.015 + .06 * sp) * sr);
            shimRatio = shimmerRatio (p.harmony);   // интервал шиммера по гармонии
            nGrainRatios = harmonyRatios (p.harmony, grainRatios);
            auto op = [this] (double fc) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * std::min (fc, sr * .45) / sr)); };
            tailLoBase = std::clamp (p.tailLo, 15.0f, 4000.0f);
            tailHiBase = std::clamp (p.tailHi, 300.0f, 20000.0f);
            tailLoOff = p.tailLo <= 15.5f;          // крайнее левое — срез низа выключен
            tailHiOff = p.tailHi >= 19500.0f;       // крайнее правое — срез верха выключен (но хвост всё равно мягко темнеет)
            clarityT = p.clarity >= 0 ? p.clarity : -1.0f;
            orgSel = std::clamp (p.organism, 0, 4);
            (void) op;
        }
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
            chFade += ((chPending ? 0.0f : 1.0f) - chFade) * k.chFade;
            sLay += (p.layers - sLay) * k.smooth;
            sPP += (p.pingpong - sPP) * k.smooth;
            sTone += (p.tone - sTone) * k.smooth;
            sDepth += (p.depth - sDepth) * k.smooth;
            sChaos += (p.chaos - sChaos) * k.smooth;
            sCore += (p.core - sCore) * k.smooth;
            sRuin += (p.ruin - sRuin) * k.smooth;
            sSpace += (p.space - sSpace) * k.smooth;
            sShim += (p.shimmer - sShim) * k.smooth;
            const float prT = p.prism >= 0 ? p.prism : std::max (0.0f, p.drift - .25f) * .8f;   // DRIFT двигает и спектр
            sPrism += (prT - sPrism) * k.smooth;
            // TAIL под капотом: срезы гуляют с DRIFT, а без игры хвост постепенно темнеет и истончается
            {
                const float pf = std::min (1.0f, std::max (envS[0], envS[1]) * 6.0f);
                playF += (pf - playF) * k.playRate;
                if (--tailCnt <= 0)
                {
                    tailCnt = 16;
                    const float wander = sDrift * .35f * (lfo[2].v * 2 - 1);
                    const float natural = (.4f + .6f * sSpace) * (1 - playF);
                    const float fLo = tailLoOff ? 0.0f : tailLoBase * std::pow (2.0f, wander + .4f * natural);
                    const float fHi = (tailHiOff ? 20000.0f : tailHiBase) * std::pow (2.0f, -wander * .7f - .9f * natural);
                    auto op1 = [this] (float fc) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * std::min ((double) fc, sr * .45) / sr)); };
                    tLoA = fLo <= 0 ? 0.0f : op1 (fLo);
                    tHiA = (tailHiOff && natural < .02f) ? 1.0f : op1 (std::min (fHi, 20000.0f));
                }
                sTLo += (tLoA - sTLo) * k.smooth * 4;
                sTHi += (tHiA - sTHi) * k.smooth * 4;
            }
            bf += (bfT - bf) * k.blur;
            lcE += (lcT - lcE) * k.enRate;
            hcE += (hcT - hcE) * k.enRate;
            for (int c = 0; c < 2; ++c) rg[c] += (rgT[c] - rg[c]) * k.recRate;

            // кривая обратной связи; не выше 0.995 — петля не раскачивается сама (бесконечно держит только FREEZE)
            const float dg = std::min (.995f, (2.0f - sDecay) * sDecay * 1.066f);
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
            // RUIN: шипение и треск записываются в память и стареют вместе с ней
            float hiss[2] = { 0, 0 };
            if (sRuin > 1e-4f)
            {
                const float r2 = sRuin * sRuin;
                for (int c = 0; c < 2; ++c)
                {
                    const float wn = ruinRng.bi();
                    float* b = pinkB[c];
                    b[0] = .99765f * b[0] + wn * .099f; b[1] = .963f * b[1] + wn * .2965f; b[2] = .57f * b[2] + wn * 1.0527f;
                    hiss[c] = (b[0] + b[1] + b[2] + wn * .1848f) * .08f;
                    if (ruinRng.uni() < sRuin * 22.0f / (float) sr)
                        crackAmp[c] = (.05f + .3f * ruinRng.uni()) * sRuin * (ruinRng.uni() < .5f ? -1.0f : 1.0f), crackEnv[c] = 1;
                    crackEnv[c] *= k.crackDecay;
                    crackLp[c] += (crackAmp[c] * crackEnv[c] - crackLp[c]) * k.crackLp;
                    g[c] += (r2 * .03f * hiss[c] + crackLp[c]) * std::max (rg[c], .3f);
                }
                if (ruinRng.uni() < r2 * 1.5f / (float) sr) erode();
            }

            float duck[2], fbg[2];
            for (int c = 0; c < 2; ++c)
            {
                const float swell = std::max (0.0f, (1 - envF[c] * 5) * envS[c]);
                const float dk = std::max (0.0f, (swell - envF[c]) * duckAmt + 1);
                const float fr = 1 - rg[c];                                // заморозка: петля без потерь
                duck[c] = dk + (1 - dk) * fr;
                fbg[c] = dg + (1 - dg) * fr;
            }

            ln = 0; sigL = sigR = 0;
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
            wl += sigL; wr += sigR; sigL = sigR = 0;
            blurStage (wl, wr);
            chaosSt.process (wl, wr, sChaos);   // всегда в цепи: включение CHAOS не сдвигает сигнал и не щёлкает
            if (sRuin > 1e-4f) { const float fl = sRuin * sRuin * .006f; wl += fl * hiss[0]; wr += fl * hiss[1]; }
            // SPACE + SHIMMER: большой зал поверх мокрого сигнала, PRISM — переливы спектра
            if (sSpace > 1e-4f || sShim > 1e-4f)
            {
                float rl, rr;
                // зал слышит и сам вход: облако раскрывается сразу, не дожидаясь первого круга памяти
                // нарастание зала: на новой ноте хвост зала стартует тише и вырастает
                {
                    const float a = .5f * (std::fabs (dryL) + std::fabs (dryR));
                    hallSwF += (a - hallSwF) * k.swFast; hallSwS += (a - hallSwS) * k.swSlow;
                    const float th = .8f * organism.cur.swell * organism.mix;
                    if (th > .05f && hallSwF > hallSwS * 1.35f + 1e-3f) hallSwG += (.2f - hallSwG) * k.swDrop;
                    else hallSwG += (1.0f - hallSwG) * (float) (1.0 - std::exp (-1.0 / (std::max (.05f, th) * sr)));
                }
                reverb.process ((wl + dryL * .55f) * hallSwG, (wr + dryR * .55f) * hallSwG, preDelay, sShim, shimRatio, rl, rr);
                const float send = std::min (1.0f, sSpace * 1.4f) + sShim * .3f;
                wl = wl * (1 - .35f * sSpace) + rl * send * 1.1f;
                wr = wr * (1 - .35f * sSpace) + rr * send * 1.1f;
                revLvl += (std::fabs (rl) + std::fabs (rr) - revLvl) * .0005f;
            }
            if (sPrism > 1e-4f) prism.process (wl, wr, sPrism, sDrift);
            clarity (wl, wr);
            organism.process (wl, wr, orgSel, playF, .5f * (dryL + dryR));
            depthTone (wl, wr, .5f * (dryL + dryR));
            // TAIL: фильтр хвостов, 24 дБ/окт снизу и сверху
            {
                float x[2] = { wl, wr };
                for (int c = 0; c < 2; ++c)
                {
                    float h = x[c];
                    for (int j = 0; j < 4; ++j) { tf[c][j] += (h - tf[c][j]) * sTLo; h -= tf[c][j]; }
                    for (int j = 4; j < 8; ++j) { tf[c][j] += (h - tf[c][j]) * sTHi; h = tf[c][j]; }
                    x[c] = h;
                }
                wl = x[0]; wr = x[1];
            }
            wl *= 2.0f; wr *= 2.0f;   // подъём уровня мокрого сигнала (выходной лимитер держит пики)
            // автогромкость под капотом: если мокрый сигнал долго громче нормы (≈ −15 дБ), он плавно уходит тише (до −6 дБ)
            {
                const float e2 = .5f * (wl * wl + wr * wr);
                agcEnv += (e2 - agcEnv) * k.agcEnv;
                const float rms = std::sqrt (agcEnv);
                const float tg = rms > .18f ? std::max (.5f, .18f / rms) : 1.0f;
                agcG += (tg - agcG) * (tg < agcG ? k.agcDown : k.agcUp);
                wl *= agcG; wr *= agcG;
            }

            float oL = wl * xf * sMix + dryL * (1 - sMix);
            float oR = wr * xf * sMix + dryR * (1 - sMix);
            monoAmt += ((p.mono ? 1.0f : 0.0f) - monoAmt) * k.smooth;
            if (monoAmt > 1e-4f) { const float m = (oL + oR) * .5f; oL += monoAmt * (m - oL); oR += monoAmt * (m - oR); }
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
        std::atomic<int> chaosType { 0 }, erodeCount { 0 };
        std::atomic<float> chaosEnv { 0 }, ruin { 0 }, chaos { 0 }, lastErode { -1 };
        std::atomic<float> space { 0 }, revLvl { 0 }, prism { 0 }, core { .5f }, breath { 1 };
        std::atomic<int> organism { 1 };
        std::array<std::atomic<float>, 4> lSwell {};
        std::array<std::atomic<float>, 5> prismF {};
        std::array<std::atomic<int>, 4> lBand {}, lSemis {}, lTex {};
        std::array<std::atomic<float>, 4> lLvl {};
    } viz;

private:
    struct LBand { float s1 = 0, s2 = 0, s3 = 0, held = 0; int hold = 0; };
    struct Deg
    {
        float lp1 = 0, lp2 = 0, env = 0, bloom = 1, lp3 = 0, dc = 0, held = 0;
        int hold = 0;
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
        const float la = sLay * live * chFade;
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
    // общее усиление петли не выше 0.998 (в заморозке — ровно 1): память не раскачивается сама
    static float loopCap (float gain, float frozen) { const float lim = .998f + .002f * frozen; return gain > lim ? lim / gain : 1.0f; }
    // «распускающийся» компрессор: тихое поднимает до ×1.8, громкое прижимает
    float bloomGain (Deg& d, float s, float amt)
    {
        const float t = std::max (0.0f, 1 + amt * (.8f - 1.9f * s * s));
        d.bloom += (t - d.bloom) * (t < d.bloom ? k.bloomDown : k.bloomUp);
        return d.bloom;
    }
    // запись в линию: лимитер, тепло, мягкое вхождение при переходах
    // RUIN на высоких значениях: петля теряет разрядность и частоту дискретизации
    float ruinCrush (Deg& d, float v)
    {
        const float rc = std::max (0.0f, (sRuin - .45f) / .55f);
        if (rc <= 1e-4f) return v;
        const int N = 1 + (int) (rc * 5.0f * (float) k.crushN / 4.0f);
        if (++d.hold >= N) { d.hold = 0; const float q = std::pow (2.0f, 11.0f - rc * 6.0f); d.held = std::round (v * q) / q; }
        return v + rc * .7f * (d.held - v);
    }
    // эрозия: случайный кусок памяти выгрызается с мягкими краями
    void erode()
    {
        if (lay.n <= 0) return;
        const int l = (int) (ruinRng.next() % (uint32_t) lay.n);
        const int len = lay.len[l];
        const int seg = std::min (len / 3, (int) ((.02 + .18 * ruinRng.uni()) * sr));
        if (seg < 8) return;
        const int p0 = (int) (ruinRng.next() % (uint32_t) len);
        const float gmin = .1f * ruinRng.uni();
        for (int j = 0; j < seg; ++j)
        {
            const float hw = .5f - .5f * std::cos (6.2831853f * (float) j / (float) seg);
            at (l, wrap ((p0 + j) % len, len)) *= 1.0f - (1.0f - gmin) * hw;
        }
        lastErode = (float) (lay.off[l] + p0);
        ++erodeCount;
    }

    void writeLine (int line, Deg& d, float v, float wd)
    {
        d.dc += (v - d.dc) * k.dc; v -= d.dc;   // постоянная составляющая не копится в петле
        v = ruinCrush (d, v);
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
        const float cap = loopCap (dk * fb * b, 1 - std::max (rg[0], rg[1]));
        writeLine (0, deg[0], b * (x * fb * cap + monoIn), wd);

        const float sp = sSpread, dr = sDrift * .9f;
        float sumL = grainSet (gL, len, [&] { return sp * rng.uni() + dr * lfo[1].v; });
        float sumR = grainSet (gR, len, [&] { return (rng.uni() - .5f) * sp + .5f + dr * lfo[0].v; });
        float sumS = grainSet (gS, len, [&] { return sp * rng.uni() + dr * lfo[2].v; });
        addLayer (sumL * .35f, 1, 0);
        addLayer (sumR * .35f, 0, 1);
        addLayer (sumS * .35f * .85f, 1, 1);
    }

    struct GrainSet { int P = 1000, ph = 0; int start[4] {}; float amp[4] {}; int idx[4] {}; float rate[4] { 1, 1, 1, 1 }; int rdir[4] { 1, 1, 1, 1 }; };
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
                // CORE: часть гранул звучит на другой высоте (по HARMONY) и задом наперёд
                const float pPitch = std::max (0.0f, sCore - .25f) * .85f;
                const float pRev = std::max (0.0f, sCore - .45f) * .55f;
                s.rate[v] = (rng.uni() < pPitch && nGrainRatios > 0) ? grainRatios[rng.next() % (uint32_t) nGrainRatios] : 1.0f;
                s.rdir[v] = rng.uni() < pRev ? -1 : 1;
                float u = pickU() * (.6f + .8f * sCore);
                u -= std::floor (u);
                const int minD = (int) ((float) s.P * (1.0f + std::max (0.0f, s.rate[v] - 1.0f))) + 4;
                const int D = minD + (int) (u * (float) std::max (1, len - minD - s.P));
                s.start[v] = wrap (((w[0] - dir * D) % len + len) % len, len);
            }
            const float fpos = (float) s.start[v] + (float) (dir * s.rdir[v]) * (float) vph * s.rate[v];
            const float a = 1.0f - std::fabs (2.0f * (float) vph / (float) s.P - 1.0f);
            float pw = fpos; const float L = (float) len; pw -= std::floor (pw / L) * L;
            s.amp[v] = a; s.idx[v] = (int) pw;
            sum += readFrac (0, pw) * a;
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
            const float apG = (.35f + .4f * sCore) * (1 - .5f * std::min (1.0f, fbg[c]));   // CORE: гуще облако
            const float fbStage = .91f * fbg[c] + (fbg[c] > .999f ? .09f : 0.0f);
            const float prev = dOut[(s + 3) & 3] * fbStage;
            const float x = degrade (deg[s], prev, rg[c]) * duck[c];
            const float b = bloomGain (deg[s], g[c] + x, bloomAmt);
            const float cap = loopCap (b * fbStage * duck[c], 1 - rg[c]);
            float v = b * (x * cap + g[c]);
            deg[s].dc += (v - deg[s].dc) * k.dc; v -= deg[s].dc;
            v = ruinCrush (deg[s], v);
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
        const float mk = .5f + sCore;
        const float m0 = std::min (k.modM0 * mk, (float) lay.len[0] * .25f), m1 = std::min (k.modM1 * mk, (float) lay.len[2] * .25f);
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
            const float cap = loopCap (b * fbg[ch] * duck[ch], 1 - rg[ch]);
            x[l] *= b;
            writeLine (l, deg[l], x[l] * fbg[ch] * cap + g[ch] * b, wd);
        }
        // CORE: россыпь отражений — отводы из глубины каждой линии, по очереди слева и справа
        {
            static const float fr[8] = { .23f, .61f, .37f, .83f, .29f, .71f, .47f, .91f };
            const float gT = std::max (0.0f, sCore - .2f) * .5f;
            if (gT > 1e-4f)
            {
                float tl = 0, tr = 0;
                for (int l = 0; l < 4; ++l)
                    for (int t2 = 0; t2 < 2; ++t2)
                    {
                        const float v = at (l, wrap (w[l] + (int) ((float) lay.len[l] * fr[l * 2 + t2]), lay.len[l]));
                        if (((l + t2) & 1) == 0) tl += v; else tr += v;
                    }
                // отражения темнее прямого звука: двойной однополюсный фильтр сглаживает треск и края эрозии
                tapLp[0] += (tl - tapLp[0]) * k.tapeHf; tapLp[1] += (tapLp[0] - tapLp[1]) * k.tapeHf;
                tapLp[2] += (tr - tapLp[2]) * k.tapeHf; tapLp[3] += (tapLp[2] - tapLp[3]) * k.tapeHf;
                sigL += tapLp[1] * gT; sigR += tapLp[3] * gT;
            }
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
        // CORE: плёнка плывёт (wow + flutter) и греется; качание копится от круга к кругу, как на старой ленте
        tapeWow += k.tapeWow; if (tapeWow > 6.2831853f) tapeWow -= 6.2831853f;
        tapeFl += k.tapeFl;   if (tapeFl > 6.2831853f) tapeFl -= 6.2831853f;
        const float depth = sCore * sCore * k.tapeDepth;
        const float off0 = 2.0f + depth * (1.0f + .75f * std::sin (tapeWow) + .25f * std::sin (tapeFl));
        const float off1 = 2.0f + depth * (1.0f + .75f * std::sin (tapeWow + 1.9f) + .25f * std::sin (tapeFl * 1.13f + .7f));
        float o0 = readFrac (0, (float) w[0] + (float) dir * off0), o1 = readFrac (1, (float) w[1] + (float) dir * off1);
        const float grit = std::max (0.0f, sCore - .3f) * .25f;
        o0 += grit * (o0 * std::fabs (o0) - .3f * o0 * o0); o1 += grit * (o1 * std::fabs (o1) - .3f * o1 * o1);
        tapeHf[0] += (o0 - tapeHf[0]) * k.tapeHf; tapeHf[1] += (o1 - tapeHf[1]) * k.tapeHf;
        const float hl = std::max (0.0f, sCore - .4f) * .6f;   // плёнка теряет верх
        o0 += hl * (tapeHf[0] - o0); o1 += hl * (tapeHf[1] - o1);
        const float c = sSpread * (rg[0] + rg[1]) * .5f, a = 1 - .49f * c, nn = 1.0f / std::sqrt (a * a + .2601f * c * c);
        float x[2] = { (a * o0 + .51f * c * o1) * nn, (a * o1 + .51f * c * o0) * nn };
        for (int l = 0; l < 2; ++l)
        {
            x[l] = degrade (deg[l], x[l], rg[l]) * duck[l];
            const float b = bloomGain (deg[l], g[l] + x[l], bloomAmt);
            const float cap = loopCap (b * fbg[l] * duck[l], 1 - rg[l]);
            x[l] *= b;
            writeLine (l, deg[l], x[l] * fbg[l] * cap + g[l] * b, wd);
        }
        // CORE: «головка эха» близко к записи — плывущее эхо слышно сразу, не через круг ленты
        const float hg = .5f * std::max (0.0f, sCore - .35f) / .65f;
        float h0 = 0, h1 = 0;
        if (hg > 1e-4f)
        {
            const float sr = (float) this->sr;
            const float dh = sr * (.42f + .35f * sCore) + 3 * depth * (1.0f + std::sin (tapeWow + .4f));
            h0 = readFrac (0, (float) w[0] - (float) dir * dh);
            h1 = readFrac (1, (float) w[1] - (float) dir * (dh * 1.31f));
            h0 += grit * (h0 * std::fabs (h0)); h1 += grit * (h1 * std::fabs (h1));
            tapeHh[0] += (h0 - tapeHh[0]) * k.tapeHf; tapeHh[1] += (h1 - tapeHh[1]) * k.tapeHf;
            h0 = hg * tapeHh[0]; h1 = hg * tapeHh[1];
        }
        const float d = sDrift, A = lfo[0].v, B = lfo[1].v;
        addLayer (x[0] + h0, 1 - A * d, A * d);
        addLayer (x[1] + h1, B * d, 1 - B * d);
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
        const float la = sLay * chFade, pp = sPP;
        const float fb = .3f + .5f * pp;
        for (int j = 0; j < ln; ++j)
        {
            float y = lx[j];
            const float sh = (ch[j].semis != 0 && la > 1e-4f) ? shift[j].process (y, ratio[j]) : y;   // сдвиг только там, где он нужен
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
            // нарастание: на новом материале слой проседает и плавно вырастает (время — по полосе и ORGANISM)
            {
                static const float swT[4] = { 1.2f, 0.0f, .6f, 1.6f };
                const float tsw = swT[ch[j].band] * organism.cur.swell * organism.mix;
                const float a = std::fabs (lx[j]);
                swF[j] += (a - swF[j]) * k.swFast; swS[j] += (a - swS[j]) * k.swSlow;
                if (tsw > .05f)
                {
                    if (swF[j] > swS[j] * 1.35f + 1e-3f) swG[j] += (.15f - swG[j]) * k.swDrop;
                    else swG[j] += (1.0f - swG[j]) * (float) (1.0 - std::exp (-1.0 / (tsw * sr)));
                }
                else swG[j] += (1.0f - swG[j]) * k.swDrop;
                y *= swG[j];
                swellT[j] = tsw;
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
    // Под капотом: если середина (300 Гц – 3 кГц) выпирает, она мягко прижимается (до −6 дБ),
    // а слишком резкие атаки мокрого сигнала сглаживаются — звук не бьёт «в лоб».
    void clarity (float& wl, float& wr)
    {
        // сила зависит от организма: OFF не трогает атаки и середину, ASH прижимает сильнее всех
        static const float orgCl[5] = { 0.0f, .6f, .45f, .8f, 1.0f };
        const float amt = clarityT >= 0 ? clarityT : orgCl[orgSel] * (.6f + .4f * sSpace);
        if (amt <= 1e-4f) return;
        float x[2] = { wl, wr }, mid[2];
        for (int c = 0; c < 2; ++c)
        {
            clHp[c] += (x[c] - clHp[c]) * k.cl300;  const float hp = x[c] - clHp[c];
            clLp[c] += (hp - clLp[c]) * k.cl3000;   mid[c] = clLp[c];
        }
        const float em = .5f * (mid[0] * mid[0] + mid[1] * mid[1]), ea = .5f * (x[0] * x[0] + x[1] * x[1]);
        eMid += (em - eMid) * k.clEnv; eAll += (ea - eAll) * k.clEnv;
        const float ratio = eAll > 1e-9f ? eMid / eAll : 0.0f;          // доля середины в энергии
        const float target = .35f;
        const float gT = ratio > target ? std::max (.5f, std::sqrt (target / ratio)) : 1.0f;
        gMid += (gT - gMid) * (gT < gMid ? k.clAtk : k.clRel);
        const float gm = 1.0f - amt * (1.0f - gMid * .92f);             // плюс постоянный лёгкий провал −0.7 дБ
        // мягкие атаки: быстрая огибающая против медленной
        const float a = std::max (std::fabs (x[0]), std::fabs (x[1]));
        tFast += (a - tFast) * k.trFast; tSlow += (a - tSlow) * k.trSlow;
        const float tg = tFast > tSlow * 1.5f + 1e-5f ? (tSlow * 1.5f + 1e-5f) / tFast : 1.0f;
        gTr += (tg - gTr) * (tg < gTr ? k.trAtk : k.trRel);
        const float gt = 1.0f - amt * .7f * (1.0f - gTr);
        for (int c = 0; c < 2; ++c) x[c] = (x[c] + (gm - 1.0f) * mid[c]) * gt;
        wl = x[0]; wr = x[1];
    }

    // DEPTH: саб −12 и −24 (только октавы — всегда в тональности), упругий: следует за игрой
    void depthTone (float& wl, float& wr, float dryMono)
    {
        float sub = 0;
        if (sDepth > 1e-4f)
        {
            // источник саба — низ памяти и входа (до 400 Гц): так бас сдвигается чисто
            const float src = .5f * (wl + wr) + .7f * dryMono;
            bassLp[0] += (src - bassLp[0]) * k.bass; bassLp[1] += (bassLp[0] - bassLp[1]) * k.bass;
            const float b = bassLp[1];
            const float s1 = subShift.process (b, .5f);
            subLp1 += (s1 - subLp1) * k.sub; subLp2 += (subLp1 - subLp2) * k.sub;
            const float g2 = std::max (0.0f, sDepth - .45f) / .55f;          // −24 вступает во второй половине ручки
            float s2v = 0;
            if (g2 > 1e-4f)
            {
                const float s2 = subShift2.process (b, .25f);
                sub2Lp[0] += (s2 - sub2Lp[0]) * k.sub2; sub2Lp[1] += (sub2Lp[0] - sub2Lp[1]) * k.sub2;
                s2v = sub2Lp[1];
            }
            // упругость: быстрая огибающая относительно медленной даёт толчок на каждой ноте
            const float a = std::fabs (b);
            subEnv += (a - subEnv) * (a > subEnv ? k.envAtkS : k.envRelS);
            subEnvSlow += (subEnv - subEnvSlow) * k.envSlowS;
            const float punch = std::min (1.6f, subEnv / (subEnvSlow + 1e-4f));
            const float gp = .55f + .55f * punch;
            const float raw = (subLp2 * 1.4f * sDepth + s2v * 1.1f * g2) * gp;
            sub = std::tanh (raw * 1.4f) / 1.4f;                               // лёгкая сатурация: саб слышен и на маленьких колонках
        }
        float x[2] = { wl, wr };
        const float t = (.5f - sTone) * 2.0f;                 // +1 тёмный … −1 яркий
        const float gLo = 1 + .45f * t, gHi = std::max (.2f, 1 - .55f * t);
        for (int c = 0; c < 2; ++c)
        {
            shelfLp[c] += (x[c] - shelfLp[c]) * k.shelf;
            x[c] += sDepth * .5f * shelfLp[c] + sub;
            tiltLp[c] += (x[c] - tiltLp[c]) * k.tilt;
            x[c] = tiltLp[c] * gLo + (x[c] - tiltLp[c]) * gHi;
        }
        // всё ниже 120 Гц — в моно: низ плотный и по центру
        for (int c = 0; c < 2; ++c) monoLp[c] += (x[c] - monoLp[c]) * k.monoLo;
        const float lowMid = .5f * (monoLp[0] + monoLp[1]);
        for (int c = 0; c < 2; ++c) x[c] += lowMid - monoLp[c];
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
        viz.chaosType.store (chaosSt.type); viz.chaosEnv.store (chaosSt.type == 5 ? 1.0f - chaosSt.dropG : chaosSt.m);
        viz.ruin.store (sRuin); viz.chaos.store (sChaos); viz.lastErode.store (lastErode); viz.erodeCount.store (erodeCount);
        viz.space.store (sSpace); viz.revLvl.store (revLvl); viz.prism.store (sPrism);
        viz.core.store (sCore); viz.breath.store (organism.breath); viz.organism.store (orgSel);
        for (int j = 0; j < 4; ++j) viz.lSwell[(size_t) j].store (swellT[j]);
        for (int j = 0; j < 5; ++j) viz.prismF[(size_t) j].store (prism.freq (j));
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
        float bass = 0, sub2 = 0, monoLo = 0, envAtkS = 0, envRelS = 0, envSlowS = 0;
        float cl300 = 0, cl3000 = 0, clEnv = 0, clAtk = 0, clRel = 0, trFast = 0, trSlow = 0, trAtk = 0, trRel = 0, playRate = 0;
        float chFade = 0, agcEnv = 0, agcDown = 0, agcUp = 0;
        float tapeWow = 0, tapeFl = 0, tapeDepth = 0, tapeHf = 0;
        float swFast = 0, swSlow = 0, swDrop = 0;
        float crackDecay = 0, crackLp = 0;
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
    float sLay = 0, sPP = 0, sTone = .48f, sDepth = .4f, sChaos = 0, sRuin = 0;
    ChaosStage chaosSt;
    Reverb reverb;
    Prism prism;
    float sSpace = 0, sShim = 0, sPrism = 0, preDelay = 1000, shimRatio = 2, revLvl = 0;
    float tLoA = 0, tHiA = 1, sTLo = 0, sTHi = 1, tf[2][8] {};
    float agcEnv = 0, agcG = 1, chFade = 1, monoAmt = 0;
    float sCore = .5f, sigL = 0, sigR = 0, tapeWow = 0, tapeFl = 0, tapeHf[2] {}, tapeHh[2] {}, tapLp[4] {};
    float grainRatios[6] { .5f, 2.0f }; int nGrainRatios = 2;
    Organism organism;
    int orgSel = 1;
    float swF[4] {}, swS[4] {}, swG[4] { 1, 1, 1, 1 }, swellT[4] {}, hallSwF = 0, hallSwS = 0, hallSwG = 1;
    bool chPending = false;
    LayerChar pendingCh[4];
    float tailLoBase = 40, tailHiBase = 16000, playF = 1, clarityT = -1;
    bool tailLoOff = false, tailHiOff = false;
    int tailCnt = 0;
    // автокоррекция середины и мягкие атаки
    float clLp[2] {}, clHp[2] {}, eMid = 0, eAll = 0, gMid = 1, tFast = 0, tSlow = 0, gTr = 1;
    Rng ruinRng { 0x7f4a7c15u };
    float pinkB[2][3] {}, crackEnv[2] {}, crackAmp[2] {}, crackLp[2] {};
    float lastErode = -1; int erodeCount = 0;
public:
    long chaosEvents() const { return chaosSt.fired; }
    int erosions() const { return erodeCount; }
private:
    Shifter subShift, subShift2;
    float bassLp[2] {}, sub2Lp[2] {}, monoLp[2] {}, subEnv = 0, subEnvSlow = 0;
    float subLp1 = 0, subLp2 = 0, shelfLp[2] {}, tiltLp[2] {}, outGain = 1, dcOut[2] {};

    float sDecay = .6f, sWarm = .2f, sDyn = .5f, sSpread = .3f, sDrift = .4f, sMix = .5f, sOut = 1;
    float lcE = 0, hcE = 0, lcA = 0, lcG = 1, hcB = 1, hcG = 1, hcS = 0;
    float gate[2] { 1, 1 }, rg[2] { 1, 1 }, envF[2] {}, envS[2] {};
    Tri lfo[4];
    float walk[2] {};
    Rng rng { 0x2545f491u };
};
} // namespace mem
