# Memoria — станция дрейфующей памяти (VST3 / AU)

Эффект для Ableton Live: всё, что звучит на дорожке, попадает в общую память и кружит в ней.
Каждый круг материал немного темнеет и истончается, будто испаряется.
Пока играешь, память может приглушаться, а когда перестаёшь — наплывать.
Сделан по мотивам разбора идей Cosmos; код написан с нуля.

## Как получить
GitHub собирает плагин сам: **Actions → memoria →** верхний запуск → **Artifacts → Memoria-macOS** (или Memoria-Windows).

## Установка на Mac
1. Распакуйте архив, положите `Memoria.vst3` в `/Library/Audio/Plug-Ins/VST3/`.
2. В Терминале, каждую команду отдельно:
```
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Memoria.vst3
sudo codesign --force --deep -s - /Library/Audio/Plug-Ins/VST3/Memoria.vst3
```
3. Ableton → Настройки → Plug-ins → **Rescan** с зажатым Option.

## Управление
- **Режим:** GRAINS — гранулы над петлёй; DIFFUSE — кольцо из длинных allpass с плёночным дрейфом;
  NETWORK — 4 перекрёстно связанные линии (7–16 с); TAPES — две петли неравной длины (до 23 с).
- **Размер:** LONG / MID / SHORT — длина линий.
- **DECAY** — сколько памяти остаётся на каждом круге (вверху чуть больше 1, лимитер держит петлю).
- **WARMTH** — сатурация в петле и на выходе.
- **DYNAMICS** — влево: память приглушается, пока играешь, и наплывает после; вправо: тихие хвосты распускаются в дрон.
- **SPREAD** — разброс гранул / сколько звука перетекает между линиями.
- **DRIFT** — глубина медленных непериодичных LFO.
- **MIX**, **OUTPUT**.
- **LOW-CUT / HIGH-CUT** — сила деградации в петле (клик — следующая степень).
- **INPUT** — писать в память L+R, только L или только R.
- **FREEZE** — вход не пишется, память кружит без потерь. **REVERSE** — всё в обратную сторону.
  **MONO** — моно-выход. **ERASE** — стереть память.
- Смена режима не стирает память: новый режим играет по старому материалу.

## Разработка
- Тест движка без JUCE: `g++ -O2 -std=c++17 memoria/tests/engine_test.cpp -o /tmp/mt && /tmp/mt`
- Снимок окна (Linux): `cmake -S memoria -B build -DMEMORIA_TOOLS=ON && cmake --build build --target memoria_snapshot`,
  затем `xvfb-run build/memoria_snapshot_artefacts/Release/memoria_snapshot out.png 0 9`.
