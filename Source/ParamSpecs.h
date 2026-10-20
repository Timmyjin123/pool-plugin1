#pragma once
// Описание всех параметров плагина и пресетов. Без зависимостей от JUCE.
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace pool
{
enum ParamId
{
    // Основной звук
    P_ipitch, P_dw,
    // Облако зёрен
    P_size, P_dens, P_pos, P_pj, P_pitch, P_ps, P_harm, P_rev, P_wid, P_glitch,
    // Пэд и дрон
    P_pad, P_pdr, P_pwid, P_ppitch, P_pdark, P_pwarm,
    // Далёкий голос
    P_far, P_freg, P_fcl, P_fsw, P_fsp, P_fdist,
    // Текстура
    P_drive, P_crush, P_hp, P_lp,
    // Пространство
    P_dt, P_df, P_dm, P_rvm,
    // Живость
    P_dr, P_ds, P_vol,
    P_COUNT
};

struct ParamSpec
{
    const char* id;     // ASCII-идентификатор для хоста и сохранения
    const char* name;   // полное имя (UTF-8), видно в Live
    const char* label;  // короткая подпись на ручке (UTF-8)
    float mn, mx;
    bool logScale;
    const char* unit;   // UTF-8
    bool noDrift;       // true: дрейф параметров на него не действует
    float def;
    int group;
};

inline const char* const* groupNames()
{
    static const char* const g[7] = { "Основной звук", "Облако зёрен", "Пэд и дрон", "Далёкий голос",
                                      "Текстура", "Пространство", "Живость" };
    return g;
}
constexpr int kNumGroups = 7;

inline const ParamSpec* paramSpecs()
{
    static const ParamSpec s[P_COUNT] = {
        { "ipitch", "Высота основного звука", "Высота", -24, 24, false, " пт", true, 0, 0 },
        { "dw", "Сухой звук / облако", "Сухой / облако", 0, 1, false, "", false, .8f, 0 },

        { "size", "Размер зерна", "Размер зерна", 8, 1200, true, " мс", false, 300, 1 },
        { "dens", "Плотность зёрен", "Плотность", 2, 100, true, " /с", false, 20, 1 },
        { "pos", "Позиция в записи", "Позиция", 0, 1, false, "", false, .5f, 1 },
        { "pj", "Разброс позиции", "Разброс", 0, 1, false, "", false, .3f, 1 },
        { "pitch", "Высота зёрен", "Высота зёрен", -24, 24, false, " пт", false, 0, 1 },
        { "ps", "Разброс высоты зёрен", "Разброс высоты", 0, 12, false, " пт", false, 0, 1 },
        { "harm", "Октавы и квинты", "Октавы, квинты", 0, 1, false, "", false, 0, 1 },
        { "rev", "Обратное чтение", "Обратно", 0, 1, false, "", false, 0, 1 },
        { "wid", "Ширина зёрен", "Ширина", 0, 1, false, "", false, .8f, 1 },
        { "glitch", "Глитч-клики", "Глитч", 0, 1, false, "", false, 0, 1 },

        { "pad", "Пэд, громкость", "Пэд", 0, 1, false, "", false, .55f, 2 },
        { "pdr", "Дрон: октавы и квинта", "Дрон", 0, 1, false, "", false, .35f, 2 },
        { "pwid", "Ширина пэда", "Ширина", 0, 1, false, "", false, .9f, 2 },
        { "ppitch", "Высота пэда", "Высота", -24, 24, false, " пт", true, 0, 2 },
        { "pdark", "Тембр пэда", "Тембр", 500, 12000, true, " Гц", false, 4500, 2 },
        { "pwarm", "Тепло пэда (насыщение)", "Тепло", 0, 1, false, "", false, .25f, 2 },

        { "far", "Далёкий голос, громкость", "Голос вдали", 0, 1, false, "", false, .5f, 3 },
        { "freg", "Регистр далёкого голоса", "Регистр", -24, 36, false, " пт", true, 12, 3 },
        { "fcl", "Чистота тона", "Чистота", 0, 1, false, "", false, .6f, 3 },
        { "fsw", "Появление и исчезание", "Исчезание", 0, 1, false, "", true, .6f, 3 },
        { "fsp", "Скорость появления", "Скорость", 0, 1, false, "", true, .4f, 3 },
        { "fdist", "Даль", "Даль", 0, 1, false, "", false, .5f, 3 },

        { "drive", "Перегруз", "Перегруз", 1, 20, true, "", false, 1.5f, 4 },
        { "crush", "Биткраш", "Биткраш", 0, 1, false, "", false, 0, 4 },
        { "hp", "Срез низов", "Срез низов", 20, 2000, true, " Гц", false, 60, 4 },
        { "lp", "Срез верхов", "Срез верхов", 300, 16000, true, " Гц", false, 12000, 4 },

        { "dt", "Дилей, время", "Время", .05f, 1.2f, true, " с", false, .4f, 5 },
        { "df", "Дилей, повторы", "Повторы", 0, .9f, false, "", false, .4f, 5 },
        { "dm", "Дилей, микс", "Дилей", 0, 1, false, "", false, .3f, 5 },
        { "rvm", "Реверб", "Реверб", 0, 1, false, "", false, .5f, 5 },

        { "dr", "Дрейф параметров", "Дрейф", 0, 1, false, "", true, .3f, 6 },
        { "ds", "Скорость дрейфа", "Скорость", 0, 1, false, "", true, .3f, 6 },
        { "vol", "Громкость", "Громкость", 0, 1, false, "", true, .7f, 6 },
    };
    return s;
}

inline float paramToValue(const ParamSpec& s, float x)
{
    x = std::min(1.0f, std::max(0.0f, x));
    return s.logScale ? s.mn * std::pow(s.mx / s.mn, x) : s.mn + (s.mx - s.mn) * x;
}

inline float paramFromValue(const ParamSpec& s, float v)
{
    v = std::min(s.mx, std::max(s.mn, v));
    return s.logScale ? std::log(v / s.mn) / std::log(s.mx / s.mn) : (v - s.mn) / (s.mx - s.mn);
}

struct PresetDef
{
    const char* name;
    bool freeze;
    std::vector<std::pair<int, float>> v; // параметры, не указанные здесь, берутся по умолчанию
};

inline const std::vector<PresetDef>& presetDefs()
{
    static const std::vector<PresetDef> p = {
        { "Хор вдали", false, { { P_size, 700 }, { P_dens, 25 }, { P_pos, .5f }, { P_pj, .25f }, { P_ps, .3f }, { P_harm, .4f }, { P_wid, 1 }, { P_dw, .35f }, { P_drive, 1.4f }, { P_hp, 80 }, { P_lp, 6000 }, { P_dt, .55f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .75f }, { P_pad, .45f }, { P_pdr, .4f }, { P_pwid, 1 }, { P_pdark, 3000 }, { P_pwarm, .2f }, { P_far, .85f }, { P_fcl, .5f }, { P_fdist, .6f }, { P_dr, .3f }, { P_ds, .2f } } },
        { "Скрипка вдали", false, { { P_size, 500 }, { P_dens, 20 }, { P_pos, .45f }, { P_pj, .15f }, { P_ps, .2f }, { P_harm, .3f }, { P_wid, 1 }, { P_dw, .3f }, { P_drive, 1.3f }, { P_hp, 80 }, { P_lp, 7000 }, { P_dt, .6f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .8f }, { P_pad, .3f }, { P_pdr, .3f }, { P_pwid, 1 }, { P_pdark, 3500 }, { P_pwarm, .15f }, { P_far, .95f }, { P_fcl, .85f }, { P_fdist, .7f }, { P_dr, .25f }, { P_ds, .2f } } },
        { "Низкий гул вдали", false, { { P_size, 800 }, { P_dens, 30 }, { P_pos, .5f }, { P_pj, .2f }, { P_ps, .2f }, { P_harm, .3f }, { P_wid, 1 }, { P_dw, .3f }, { P_drive, 1.3f }, { P_hp, 60 }, { P_lp, 5000 }, { P_dt, .6f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .8f }, { P_pad, .35f }, { P_pdr, .4f }, { P_pwid, 1 }, { P_pdark, 2500 }, { P_pwarm, .3f }, { P_far, .9f }, { P_fcl, .7f }, { P_fdist, .5f }, { P_dr, .25f }, { P_ds, .2f } } },
        { "Тёплый пэд", false, { { P_size, 700 }, { P_dens, 30 }, { P_pos, .5f }, { P_pj, .2f }, { P_ps, .3f }, { P_harm, .4f }, { P_wid, 1 }, { P_dw, .4f }, { P_drive, 1.4f }, { P_hp, 80 }, { P_lp, 6000 }, { P_dt, .55f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .7f }, { P_pad, .9f }, { P_pdr, .55f }, { P_pwid, 1 }, { P_pdark, 3500 }, { P_pwarm, .35f }, { P_dr, .4f }, { P_ds, .2f } } },
        { "Дрон", true, { { P_size, 900 }, { P_dens, 40 }, { P_pos, .6f }, { P_pj, .15f }, { P_ps, .2f }, { P_harm, .5f }, { P_wid, 1 }, { P_dw, .2f }, { P_drive, 1.2f }, { P_hp, 60 }, { P_lp, 5000 }, { P_dt, .7f }, { P_df, .6f }, { P_dm, .35f }, { P_rvm, .85f }, { P_pad, 1 }, { P_pdr, .9f }, { P_pwid, 1 }, { P_pdark, 2200 }, { P_pwarm, .5f }, { P_dr, .3f }, { P_ds, .15f } } },
        { "Бесконечное лето", false, { { P_size, 450 }, { P_dens, 28 }, { P_pos, .55f }, { P_pj, .35f }, { P_ps, .4f }, { P_harm, .35f }, { P_rev, .1f }, { P_wid, .9f }, { P_glitch, .05f }, { P_dw, .85f }, { P_drive, 2.5f }, { P_hp, 120 }, { P_lp, 6500 }, { P_dt, .45f }, { P_df, .45f }, { P_dm, .35f }, { P_rvm, .6f }, { P_dr, .5f }, { P_ds, .25f }, { P_pad, .5f }, { P_pdr, .4f }, { P_pwid, 1 }, { P_pdark, 5000 }, { P_pwarm, .3f } } },
        { "Туман", false, { { P_size, 900 }, { P_dens, 60 }, { P_pos, .7f }, { P_pj, .5f }, { P_pitch, 12 }, { P_ps, .8f }, { P_harm, .6f }, { P_rev, .2f }, { P_wid, 1 }, { P_dw, 1 }, { P_drive, 1.3f }, { P_hp, 200 }, { P_lp, 5000 }, { P_dt, .8f }, { P_df, .6f }, { P_dm, .4f }, { P_rvm, .85f }, { P_dr, .6f }, { P_ds, .2f }, { P_pad, .6f }, { P_pdr, .6f }, { P_pwid, 1 }, { P_pdark, 3000 }, { P_pwarm, .2f } } },
        { "Цифровая пыль", false, { { P_size, 25 }, { P_dens, 70 }, { P_pos, .3f }, { P_pj, .8f }, { P_ps, 3 }, { P_harm, .1f }, { P_glitch, .5f }, { P_dw, .9f }, { P_drive, 6 }, { P_crush, .35f }, { P_hp, 300 }, { P_lp, 9000 }, { P_dt, .12f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .3f }, { P_dr, .5f }, { P_ds, .8f }, { P_pad, .25f }, { P_pdr, .2f }, { P_pwid, .8f }, { P_pdark, 7000 }, { P_pwarm, .6f } } },
        { "Сломанная лента", false, { { P_size, 150 }, { P_dens, 14 }, { P_pos, .4f }, { P_pj, .45f }, { P_pitch, -7 }, { P_ps, 2 }, { P_harm, .2f }, { P_rev, .5f }, { P_wid, .8f }, { P_dw, .9f }, { P_drive, 8 }, { P_crush, .2f }, { P_hp, 80 }, { P_lp, 3500 }, { P_dt, .31f }, { P_df, .65f }, { P_dm, .5f }, { P_rvm, .5f }, { P_dr, .55f }, { P_ds, .4f }, { P_pad, .3f }, { P_pdr, .3f }, { P_pwid, .9f }, { P_pdark, 2500 }, { P_pwarm, .7f } } },
        { "Застывший хор", true, { { P_size, 700 }, { P_dens, 45 }, { P_pos, .5f }, { P_pj, .3f }, { P_ps, .5f }, { P_harm, .5f }, { P_wid, 1 }, { P_dw, 1 }, { P_drive, 2 }, { P_hp, 100 }, { P_lp, 7000 }, { P_dt, .5f }, { P_df, .5f }, { P_dm, .3f }, { P_rvm, .75f }, { P_dr, .7f }, { P_ds, .3f }, { P_pad, .8f }, { P_pdr, .6f }, { P_pwid, 1 }, { P_pdark, 3500 }, { P_pwarm, .3f } } },
    };
    return p;
}

// Значение параметра в пресете (в реальных единицах) или значение по умолчанию.
inline float presetValue(const PresetDef& pr, int paramIdx)
{
    for (auto& kv : pr.v)
        if (kv.first == paramIdx)
            return kv.second;
    return paramSpecs()[paramIdx].def;
}
} // namespace pool
