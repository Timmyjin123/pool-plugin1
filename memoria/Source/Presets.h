#pragma once
// Пресеты Memoria. Без зависимостей от JUCE: таблицу читает и плагин, и офлайн-тесты.
#include <vector>
#include "MemEngine.h"

namespace memoria
{
struct Preset
{
    const char* name;
    int mode, size;
    float decay, warmth, dynamics, spread, drift, mix;
    int lowcut, highcut;
    float layers; int seed; float pingpong; bool blur;
    float tone, depth; bool reverse;
};

inline const std::vector<Preset>& presets()
{
    //  name                                  mode size decay warm dyn spread drift mix lc hc layers seed pp  blur  tone depth reverse
    static const std::vector<Preset> p = {
        { "Дрон · Бесконечный", 2, 1, .98f, .30f, .95f, .50f, .60f, .70f, 1, 1, .80f, 0, .30f, true, .58f, .60f, false },
        { "Дрон · Глубокий космос",              1, 0, .97f, .15f, .90f, .25f, .90f, .70f, 1, 2, .70f,  42, .30f, true , .40f, .70f, false },
        { "Дрон · Орган из пыли",                3, 1, .96f, .35f, .90f, .60f, .40f, .75f, 0, 2, .90f, 128, .10f, true , .44f, .55f, false },
        { "Дрон · Северное сияние",              2, 0, .93f, .20f, .75f, .70f, 1.0f, .65f, 1, 1, .90f, 101, .60f, true , .55f, .40f, false },
        { "Дрон · Медленное таяние",             1, 0, .99f, .25f, .65f, .30f, .30f, .60f, 1, 2, .30f,   0, .10f, true , .45f, .50f, false },
        { "Дрон · Ковёр из памяти", 1, 2, .95f, .30f, .85f, .50f, .60f, .70f, 1, 1, .70f, 0, .50f, true, .58f, .45f, false },
        { "Дрон · Низкое солнце",                3, 0, .97f, .45f, .90f, .30f, .50f, .70f, 1, 3, .60f,   7, .10f, true , .35f, .80f, false },
        { "Дрон · Хор призраков", 1, 1, .90f, .30f, .20f, .60f, .80f, .60f, 1, 2, 1.0f, 0, .40f, true, .52f, .40f, false },
        { "Простор · Собор",                     1, 0, .90f, .15f, .70f, .30f, .60f, .50f, 1, 1, .40f,   0, .20f, false, .52f, .45f, false },
        { "Простор · Туман над водой",           1, 0, .92f, .20f, .85f, .40f, .70f, .65f, 2, 2, .60f,  12, .20f, true , .50f, .40f, false },
        { "Простор · Созвездие", 2, 0, .85f, .20f, .80f, .60f, .90f, .60f, 1, 1, .70f, 93, .40f, false, .44f, .42f, false },
        { "Простор · Воздух",                    0, 1, .80f, .10f, .70f, .50f, .70f, .55f, 2, 0, .70f, 404, .50f, true , .65f, .25f, false },
        { "Простор · Колокола вдали",            2, 1, .88f, .10f, .80f, .55f, .70f, .60f, 3, 1, .80f,  31, .70f, true , .58f, .35f, false },
        { "Простор · Стеклянные слои",           0, 1, .80f, .15f, .60f, .55f, .60f, .60f, 2, 1, .90f, 314, .40f, false, .60f, .30f, false },
        { "Эхо · Пинг-понг галактика", 2, 2, .75f, .25f, .55f, .40f, .60f, .55f, 1, 1, .50f, 150, 1.0f, false, .42f, .45f, false },
        { "Эхо · Сеть",                          2, 2, .70f, .35f, .40f, .30f, .50f, .45f, 1, 2, .30f,  51, .70f, false, .48f, .40f, false },
        { "Эхо · Хрупкое",                       0, 2, .65f, .45f, .25f, .70f, .50f, .50f, 3, 1, .70f, 777, .80f, false, .55f, .30f, false },
        { "Эхо · Пульс в тумане", 3, 2, .78f, .35f, .30f, .40f, .60f, .50f, 2, 2, .60f, 20, .90f, true, .52f, .22f, false },
        { "Эхо · Короткие петли",                3, 2, .80f, .40f, .30f, .50f, .70f, .50f, 2, 3, .50f,  88, .50f, false, .46f, .40f, false },
        { "Эхо · Ступени", 1, 2, .72f, .20f, .50f, .30f, .40f, .50f, 1, 1, .50f, 187, 1.0f, false, .55f, .10f, false },
        { "Текстура · Облако пыли",              0, 2, .60f, .50f, .50f, .80f, .80f, .60f, 0, 1, .60f,  23, .30f, true , .50f, .40f, false },
        { "Текстура · Зерно и пыль",             0, 2, .70f, .60f, .70f, .90f, .90f, .60f, 1, 3, 1.0f, 250, .60f, false, .52f, .35f, false },
        { "Текстура · Ржавые петли", 3, 2, .85f, .85f, .50f, .60f, .50f, .55f, 0, 3, .80f, 666, .50f, false, .55f, .15f, false },
        { "Текстура · Тёплая лента",             3, 1, .88f, .70f, .40f, .30f, .40f, .50f, 1, 3, .40f,   0, .20f, false, .50f, .45f, false },
        { "Текстура · Старое радио",             2, 2, .80f, .75f, .40f, .50f, .50f, .60f, 3, 3, .70f,  13, .30f, false, .60f, .10f, false },
        { "Текстура · Испарение",                0, 0, .75f, .30f, .30f, .35f, .50f, .55f, 1, 2, .30f,   0, .15f, false, .50f, .40f, false },
        { "Текстура · Шёпот",                    0, 0, .85f, .25f, .10f, .30f, .40f, .45f, 2, 3, .50f,   5, .30f, true , .50f, .35f, false },
        { "Распад · Обратный прилив",            1, 1, .88f, .25f, .60f, .40f, .70f, .60f, 1, 2, .60f,  77, .30f, true , .48f, .50f, true  },
        { "Распад · Плёнка назад",               3, 1, .90f, .40f, .50f, .40f, .60f, .55f, 1, 2, .50f,   0, .20f, false, .47f, .45f, true  },
        { "Распад · Две ленты",                  3, 0, .95f, .20f, .60f, .20f, .50f, .50f, 1, 2, .20f,   0, .20f, false, .50f, .40f, false },
        { "Распад · Медленный хор",              1, 1, .80f, .25f, .20f, .50f, .40f, .45f, 2, 2, .50f,   7, .10f, false, .50f, .45f, false },
        { "Распад · Гранулы назад",              0, 1, .80f, .30f, .40f, .70f, .80f, .60f, 1, 2, .70f, 333, .40f, true , .50f, .40f, true  },
        { "Глубина · Подводный",                 2, 0, .94f, .30f, .85f, .40f, .50f, .70f, 0, 3, .70f,   3, .20f, true , .30f, .90f, false },
        { "Глубина · Тёмная материя",            1, 0, .97f, .40f, .90f, .30f, .60f, .75f, 0, 3, .80f,  64, .10f, true , .25f, 1.0f, false },
        { "Глубина · Гул земли",                 3, 0, .96f, .50f, .95f, .30f, .30f, .70f, 0, 3, .50f,   0, .00f, true , .30f, .95f, false },
        { "Глубина · Басовые призраки",          0, 0, .85f, .35f, .70f, .40f, .60f, .60f, 0, 2, .90f, 512, .20f, false, .38f, .85f, false },
        { "Чисто · Лёгкая память",               0, 0, .55f, .10f, .50f, .25f, .30f, .30f, 1, 1, .20f,   0, .10f, false, .50f, .30f, false },
        { "Чисто · Тень",                        2, 1, .60f, .10f, .30f, .30f, .40f, .35f, 1, 1, .30f,   0, .20f, false, .50f, .35f, false },
        { "Чисто · Отражения",                   1, 2, .60f, .10f, .50f, .30f, .50f, .35f, 1, 1, .20f,   0, .40f, false, .52f, .30f, false },
        { "Чисто · Широкое эхо",                 3, 2, .65f, .10f, .50f, .20f, .50f, .40f, 1, 1, .30f,   0, .60f, false, .52f, .30f, false },
    };
    return p;
}

inline mem::Params toParams (const Preset& p)
{
    mem::Params q;
    q.mode = p.mode; q.size = p.size; q.decay = p.decay; q.warmth = p.warmth; q.dynamics = p.dynamics;
    q.spread = p.spread; q.drift = p.drift; q.mix = p.mix; q.lowcut = p.lowcut; q.highcut = p.highcut;
    q.layers = p.layers; q.layerSeed = p.seed; q.pingpong = p.pingpong; q.blur = p.blur;
    q.tone = p.tone; q.depth = p.depth; q.reverse = p.reverse;
    return q;
}
}
