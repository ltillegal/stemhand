# Stemhand

Локальное приложение для разделения и очистки аудио: стемы из готового микса,
барабаны, голоса, денойзинг — всё на своей машине, без загрузок наружу.
Целевая платформа — macOS на Apple Silicon (M1, Sequoia); разработка идёт на Linux.

![Stemhand](docs/screenshot.png)

## Что работает

**M0 — оболочка и доверие к моделям:**

- Хранилище `~/Library/Application Support/stemhand/models/<id>/<version>/`
  (Linux: `~/.local/share/stemhand/models`).
- Ed25519-подпись покрывает `manifest.json` байт-в-байт, манифест пинит SHA-256
  каждого файла. Одна подпись аутентифицирует весь каталог.
- Статусы `ok / unverified / corrupt / incompatible / broken`; подмена байта в
  весах ловится хешем, подмена манифеста — подписью.
- Волна из `ffmpeg`, CLI без дисплея: `stemhand --scan [--json]`.

**M1 — первый настоящий звук:**

- ONNX Runtime в C++, драйвер `gtcrn` (16 кГц, STFT 512/256, sqrt-Hann, WOLA).
- Модель GTCRN: 523 КБ, 0.05M параметров, MIT включая веса.
- CLI: `stemhand --denoise <in> <out>`; в приложении — кнопка **Denoise voice**
  и клавиша `D`.

**M3 — четыре стема:**

- `stemhand --separate <in> <outdir>` — drums / bass / other / vocals.
- Модель htdemucs (289 МБ, ONNX), драйвер `htdemucs`.
- Граф принимает ровно 343 980 сэмплов (7.8 с при 44.1 кГц) — длина сегмента
  запечена в экспорт, поэтому файл режется на сегменты с перекрытием 25 %,
  а стемы пишутся на диск потоком, а не копятся в памяти.
- Долгую операцию можно прервать: пока идёт разделение, кнопка становится
  **Cancel**; недоделанные стемы удаляются, а не остаются половиной.
- В приложении — кнопка **Separate 4 stems**; после разделения под волной
  появляется микс-панель: четыре стема с mute, solo, гейном (−24…+12 дБ) и
  экспортом сведённого микса в WAV.
- Экспорт считается потоком, в два прохода: первый измеряет пик, второй пишет
  с одним общим коэффициентом. Громкий микс ужимается целиком, а не клиппится
  по сэмплам.
- Сверка с независимым Python-референсом: расхождение ровно 1 отсчёт PCM16
  (3·10⁻⁵), SI-SNR 48–81 дБ по стемам.

Скорость на этой машине (2 потока, свободная машина): 8 с аудио за 17.3 с —
0.46× реального времени. То есть 4-минутный трек — около 9 минут. На M1 будет
быстрее, но замеров оттуда пока нет.

**M2 — обновление моделей по сети:**

- `stemhand --update <index-url>` — проверяет индекс, ставит недостающее.
- В приложении — кнопка **Update models**; адрес реестра запоминается в
  `registry.url` внутри хранилища, а не в глобальном конфиге.
- Установка атомарная: скачивание в `.incoming/`, проверка подписи и хешей,
  потом `rename`. Провал проверки не оставляет модели.
- Версии сосуществуют: после обновления 1.0.0 остаётся на диске.
- Пиннинг: `--prefer <id>@<version>` фиксирует версию, `--release` снимает
  пиннинг, `--versions` показывает, что установлено и что закреплено.

Замеры на этой машине (i5-3210M, 2 ядра / 4 потока, без GPU):

| Потоки | 9.77 с речи | Реальное время |
|---|---|---|
| 1 | 1.7–2.1 с | **4.9–5.7×** |
| 2 | 2.1 с | 4.6× |
| 4 | 3.6 с | 2.7× |

Разброс в первой строке — не шум измерения, а чужая нагрузка на машине
(load average 6.9 при двух ядрах). Под ней тот же прогон даёт 3.5 с вместо
1.7 с. Замеры стоит повторять на незагруженной системе.

Больше потоков — медленнее: граф крошечный, синхронизация дороже вычислений.
Поэтому по умолчанию один поток.

Сверка реализаций: C++ против независимого Python-референса — SI-SNR 70 дБ,
максимальное расхождение 3·10⁻⁵ (это разница float32 против float64 в
накоплении), корреляция 1.000000. Python-референс против **авторского**
`enh.wav` — 77 дБ и 1.0000, то есть предобработка восстановлена верно, а не
подогнана.

## Сборка

Linux (разработка):

```bash
./scripts/dev-keys.sh                 # один раз: ключевая пара для подписи моделей
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

macOS (цель) — либо `./scripts/build-macos.sh`, либо вручную, после
`brew install qt ffmpeg libsodium cmake pkg-config onnxruntime`:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"
cmake --build build -j
open build/stemhand.app
```

Подробно про бандл, `macdeployqt`, dylib и грабли — [docs/MACOS.md](docs/MACOS.md).
ONNX Runtime необязателен при конфигурации: без него собирается всё, кроме
движка, а денойзинг честно сообщает, что движка нет.

## Обновление моделей

Реестр — это каталог или HTTP-префикс с `index.json` и по одному каталогу на
версию модели:

```json
{
  "schema": 1,
  "models": [
    { "id": "denoise/gtcrn", "version": "1.1.0",
      "manifest": "denoise/gtcrn/1.1.0/manifest.json" }
  ]
}
```

```bash
./scripts/make-registry.sh 1.1.0            # локальный реестр для тестов
./build/stemhand --update "file://$PWD/build/registry/index.json" --root build/upg
# update   denoise/gtcrn 1.1.0 (from 1.0.0)
# done     build/upg/denoise/gtcrn/1.1.0
```

Индекс не является источником доверия: он умеет только указывать, а манифест
каждой модели подписан и пинит хеши весов. Поэтому подмена индекса даёт отказ
проверки, а не подмену модели.

```bash
./build/stemhand --update https://example.org/stemhand/index.json --check  # только план
./build/stemhand --versions                                  # что установлено
./build/stemhand --prefer denoise/gtcrn@1.0.0                # закрепить версию
./build/stemhand --release denoise/gtcrn                     # снять закрепление
```

## Модели

```bash
# GTCRN, 16 кГц денойзер речи (523 КБ)
curl -L -o gtcrn_simple.onnx \
  https://huggingface.co/bitsydarel/gtcrn-onnx/resolve/main/gtcrn_simple.onnx
./scripts/install-gtcrn.sh gtcrn_simple.onnx ~/.local/share/stemhand/models
```

```bash
./build/stemhand --denoise interview.wav clean.wav --root ~/.local/share/stemhand/models
./build/stemhand --separate song.mp3 stems/ --root ~/.local/share/stemhand/models
./build/stemhand --mix stems/ mix.wav --solo vocals --gain vocals=3
./build/stemhand --mix stems/ mix.wav --mute drums
./build/stemhand --scan --json
```

Формат манифеста — см. ниже; поле `driver` задаёт контракт рантайма
(`gtcrn` сейчас, `htdemucs` и `roformer` дальше), `engine_abi` — совместимость
с бинарём.

```json
{
  "schema": 1,
  "id": "denoise/gtcrn",
  "version": "1.0.0",
  "name": "GTCRN",
  "kind": "denoise",
  "driver": "gtcrn",
  "engine_abi": 1,
  "sample_rate": 16000,
  "channels": 1,
  "stems": ["voice"],
  "license": { "code": "MIT", "weights": "MIT", "commercial": true },
  "files": [
    { "path": "gtcrn_simple.onnx", "sha256": "...", "size": 535638 }
  ]
}
```

Подпись: `stemhand-sign sign <model-dir> <private-key>` создаёт `manifest.json.sig`.
Публичный ключ зашит в бинарь (`keys/dev.pub` → `cmake/pinned_key.h.in`),
приватный лежит в `.dev-keys/` и в git не попадает.

## Тесты

```bash
./scripts/test.sh        # 24 проверки: доверие, подмена, денойзинг, стемы, микс, обновления
cd build && ctest        # асинхронный путь через движок (Qt Test)
```

## Лицензии моделей

См. [docs/MODELS.md](docs/MODELS.md). Коротко: при личном использовании
ограничений нет, но `DrumSep 5-stem`, `BS-RoFormer` и `Spleeter` **не
декларируют лицензию весов** — их нельзя перераспространять, только скачивать
у авторов. MUSDB18, на котором обучено почти всё, сам некоммерческий
(`CC BY-NC-SA`).

## Дальше

M2 — реестр обновлений: скачивание моделей по HTTP-индексу, докачка, откат,
пиннинг версии в проекте. M3 — htdemucs и 4 стема. План и обоснования —
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
