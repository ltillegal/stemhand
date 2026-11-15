# Stemhand

[Русский](#русский) · [English](#english)

Local stem separation and voice cleanup: four stems out of a finished mix, drum
parts, speech denoising — all on your own machine, nothing uploaded.
Target platform is macOS on Apple Silicon; development happens on Linux.

![Stemhand](docs/screenshot.png)

---

## Русский

Локальное приложение для разделения и очистки аудио: стемы из готового микса,
барабаны, голоса, денойзинг — всё на своей машине, без загрузок наружу.
Целевая платформа — macOS на Apple Silicon (M1, Sequoia); разработка идёт на Linux.

### Что работает

**Оболочка и доверие к моделям:**

- Хранилище `~/Library/Application Support/stemhand/models/<id>/<version>/`
  (Linux: `~/.local/share/stemhand/models`).
- Ed25519-подпись покрывает `manifest.json` байт-в-байт, манифест пинит SHA-256
  каждого файла. Одна подпись аутентифицирует весь каталог.
- Статусы `ok / unverified / corrupt / incompatible / broken`; подмена байта в
  весах ловится хешем, подмена манифеста — подписью.
- Волна из `ffmpeg`, CLI без дисплея: `stemhand --scan [--json]`.

**Денойзинг речи (GTCRN):**

- ONNX Runtime в C++, драйвер `gtcrn` (16 кГц, STFT 512/256, sqrt-Hann, WOLA).
- Модель: 523 КБ, 0.05M параметров, MIT включая веса.
- CLI: `stemhand --denoise <in> <out>`; в приложении — кнопка **Denoise voice**
  и клавиша `D`. Показывает прогресс и отменяется.

**Четыре стема (htdemucs):**

- `stemhand --separate <in> <outdir>` — drums / bass / other / vocals.
- Модель 289 МБ, ONNX, драйвер `htdemucs`.
- Граф принимает ровно 343 980 сэмплов (7.8 с при 44.1 кГц) — длина сегмента
  запечена в экспорт, поэтому файл режется на сегменты с перекрытием 25 %,
  а стемы пишутся на диск потоком, а не копятся в памяти.
- Кнопка **Separate 4 stems**; после разделения под волной появляется
  микс-панель: mute, solo, гейн (−24…+12 дБ), воспроизведение (Play/Pause,
  пробел), клик по волне для перехода по позиции, экспорт микса в WAV.
- Настройки микса сохраняются в `mix.json` рядом со стемами и восстанавливаются
  при повторном открытии файла.
- Долгую операцию можно прервать: кнопка становится **Cancel**, недоделанные
  стемы удаляются.
- Экспорт считается потоком, в два прохода: первый измеряет пик, второй пишет с
  одним общим коэффициентом. Громкий микс ужимается целиком, а не клиппится.
- Сверка с независимым Python-референсом: расхождение ровно 1 отсчёт PCM16
  (3·10⁻⁵), SI-SNR 48–81 дБ по стемам.

**Обновление моделей по сети:**

- `stemhand --update <index-url>` — проверяет индекс, ставит недостающее.
- В приложении — кнопка **Update models**; адрес реестра запоминается в
  `registry.url` внутри хранилища, а не в глобальном конфиге.
- Установка атомарная: скачивание в `.incoming/`, проверка подписи и хешей,
  потом `rename`. Провал проверки не оставляет модели.
- Версии сосуществуют; `--prefer <id>@<version>` фиксирует версию,
  `--release` снимает пиннинг, `--versions` показывает состояние.

### Сборка на macOS (Apple Silicon)

Нужен macOS 14+ и Apple Silicon. Всё остальное — одной командой:

```bash
brew install qt onnxruntime libsodium ffmpeg pkg-config cmake
git clone https://github.com/ltillegal/stemhand && cd stemhand
./scripts/doctor.sh                   # только проверяет окружение, ничего не ставит
./scripts/build-macos.sh
```

Если `doctor.sh` что-то не нашёл — он так и скажет, что именно и какой командой
ставить. Скрипт настраивает CMake, собирает `.app`, прогоняет `macdeployqt`, кладёт
`libonnxruntime.dylib` внутрь бандла, переписывает rpath и подписывает ad-hoc.
Вручную — то же самое:

```bash
./scripts/dev-keys.sh                 # ключ подписи моделей (создаётся один раз)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"
cmake --build build -j
open build/stemhand.app
```

Модели ставятся в каталог приложения (`~/Library/Application Support/stemhand/models`):

```bash
curl -L -o gtcrn_simple.onnx \
  https://huggingface.co/bitsydarel/gtcrn-onnx/resolve/main/gtcrn_simple.onnx
./scripts/install-gtcrn.sh gtcrn_simple.onnx

curl -L -o htdemucs.onnx \
  https://huggingface.co/MrCitron/demucs-v4-onnx/resolve/main/htdemucs.onnx
./scripts/install-demucs.sh htdemucs.onnx

open build/stemhand.app
```

**Сборка на Mac пока не проверена ни разу** — машины под рукой нет. Ожидаемые
грабли (PATH у `.app` из Finder, `macdeployqt`, dylib ONNX Runtime, подпись)
разобраны в [docs/MACOS.md](docs/MACOS.md); версии формул Homebrew сверены
2026-11-14: qt 6.12.0, onnxruntime 1.32.0, libsodium 1.0.22, ffmpeg 9.0.2.

### Сборка на Linux (разработка)

```bash
./scripts/fetch-onnxruntime.sh        # 35 МБ, кладёт ONNX Runtime в third_party/
./scripts/dev-keys.sh                 # ключевая пара для подписи моделей
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

ONNX Runtime необязателен при конфигурации: без него собирается всё, кроме
движка, а денойзинг честно сообщает, что движка нет.

### Скорость

Наблюдаемый диапазон на машине разработки (i5-3210M, 2 ядра / 4 потока, без
GPU) — **0.13–0.46× реального времени** для разделения (8 с аудио за 17–60 с).
Разброс не от кода: фоновая нагрузка хоста гуляет (load average 7–8 при двух
ядрах), и любой замер здесь смещён. Сравнивать числа можно только внутри одного
«тихого» окна; на M1 нужен отдельный замер.

Денойзинг речи на той же машине — 4.9–5.7× реального времени в один поток.
Больше потоков делает его медленнее: граф крошечный, синхронизация дороже
вычислений, поэтому по умолчанию один поток.

### Модели

```bash
./scripts/install-gtcrn.sh gtcrn_simple.onnx      # ставит в каталог приложения
```

```bash
./build/stemhand --denoise interview.wav clean.wav
./build/stemhand --separate song.mp3 stems/
./build/stemhand --mix stems/ mix.wav --solo vocals --gain vocals=3
./build/stemhand --mix stems/ mix.wav --mute drums
./build/stemhand --scan --json
```

Манифест модели: поле `driver` задаёт контракт рантайма (`gtcrn`, `htdemucs`),
`engine_abi` — совместимость с бинарём, `files[]` — путь, SHA-256 и размер.

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

Подпись: `stemhand-sign sign <model-dir> <private-key>` создаёт
`manifest.json.sig`. Публичный ключ зашивается в бинарь при сборке
(`keys/dev.pub` → `cmake/pinned_key.h.in`), приватный лежит в `.dev-keys/` и в
git не попадает. Ключ генерируется в каждой копии репозитория: коммитить чужой
публичный ключ бессмысленно, им никто не сможет подписать.

### Тесты

```bash
./scripts/doctor.sh                        # окружение
./scripts/build-macos.sh                   # сборка (macOS) или cmake --build build
cd build-macos && ctest --output-on-failure
cd .. && ./scripts/test.sh
```

`ctest` в чистой копии даёт **4 пройденных и 5 пропущенных**: тесты, которым нужны
реальные веса, реестр или готовые стемы, сами пропускаются, если этих файлов нет.
`scripts/test.sh` проверяет то, что не требует скачивания моделей: хранилище,
подписи, подмену байта, формат манифеста, детерминизм и обновления. Скрипт сам
находит бинарь в `build/` или `build-macos/`; если сборка лежит в другом месте —
`STEMHAND_BIN=/path/to/stemhand ./scripts/test.sh`.

### Лицензии моделей

См. [docs/MODELS.md](docs/MODELS.md). Коротко: при личном использовании
ограничений нет, но `DrumSep 5-stem`, `BS-RoFormer` и `Spleeter` **не
декларируют лицензию весов** — их нельзя перераспространять, только скачивать
у авторов. MUSDB18, на котором обучено почти всё, сам некоммерческий
(`CC BY-NC-SA`).

### Лицензия

Код — MIT (см. [LICENSE](LICENSE)). Веса моделей под неё не попадают: у каждого
своя лицензия, разбор в [docs/MODELS.md](docs/MODELS.md). В репозитории весов
нет — приложение скачивает их у авторов.

### Дальше

Проверить сборку на M1 и снять там первые честные секунды; подключить CoreML
execution provider (главный рычаг скорости на Apple Silicon); измерить качество
на реальной музыке, а не только на синтетике. План и обоснования —
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## English

A local application for audio separation and cleanup: stems from a finished mix,
drum parts, speech denoising — all on your own machine, nothing uploaded.
Target platform is macOS on Apple Silicon (M1, Sequoia); development happens on
Linux.

### What works

**Shell and model trust:**

- Store at `~/Library/Application Support/stemhand/models/<id>/<version>/`
  (Linux: `~/.local/share/stemhand/models`).
- An Ed25519 signature covers `manifest.json` byte for byte, and the manifest
  pins the SHA-256 of every file. One signature authenticates the whole
  directory.
- Statuses `ok / unverified / corrupt / incompatible / broken`; a flipped byte in
  the weights is caught by the hash, a rewritten manifest by the signature.
- Waveform via `ffmpeg`, headless CLI: `stemhand --scan [--json]`.

**Speech denoising (GTCRN):**

- ONNX Runtime in C++, `gtcrn` driver (16 kHz, STFT 512/256, sqrt-Hann, WOLA).
- Model: 523 KB, 0.05M parameters, MIT including the weights.
- CLI: `stemhand --denoise <in> <out>`; in the app — the **Denoise voice**
  button and the `D` key. Shows progress and can be cancelled.

**Four stems (htdemucs):**

- `stemhand --separate <in> <outdir>` — drums / bass / other / vocals.
- 289 MB ONNX model, `htdemucs` driver.
- The graph accepts exactly 343,980 samples (7.8 s at 44.1 kHz) — the segment
  length is baked into the export, so files are chunked with 25 % overlap and
  stems are streamed to disk instead of being held in memory.
- **Separate 4 stems** button; afterwards a mix panel appears under the
  waveform: mute, solo, gain (−24…+12 dB), playback (Play/Pause, Space),
  click-to-seek on the waveform, and WAV export of the mix.
- Mix settings (gain, mute, solo) are saved to `mix.json` next to the stems and
  restored when the file is reopened.
- A long job can be interrupted: the button becomes **Cancel**, partial stems
  are removed.
- Export is streamed in two passes: the first measures the peak, the second
  writes with one global factor. A hot mix is scaled down, not clipped per
  sample.
- Cross-checked against an independent Python reference: the difference is
  exactly 1 PCM16 sample (3·10⁻⁵), SI-SNR 48–81 dB per stem.

**Model updates over the network:**

- `stemhand --update <index-url>` — reads the index and installs what is
  missing.
- In the app — the **Update models** button; the registry URL is remembered in
  `registry.url` inside the store, not in a global config.
- Atomic install: download into `.incoming/`, verify signature and hashes, then
  `rename`. A failed check leaves no model behind.
- Versions coexist; `--prefer <id>@<version>` pins one, `--release` unpins,
  `--versions` lists what is installed and what is pinned.

### Building on macOS (Apple Silicon)

macOS 14+ and Apple Silicon are required. Everything else is one command:

```bash
brew install qt onnxruntime libsodium ffmpeg pkg-config cmake
git clone https://github.com/ltillegal/stemhand && cd stemhand
./scripts/doctor.sh                   # checks the environment, installs nothing
./scripts/build-macos.sh
```

If `doctor.sh` reports something missing, it names the formula to install. The
script configures CMake, builds the `.app`, runs `macdeployqt`, copies
`libonnxruntime.dylib` into the bundle, rewrites the rpath and signs ad-hoc.
By hand it is the same thing:

```bash
./scripts/dev-keys.sh                 # model signing key (created once)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"
cmake --build build -j
open build/stemhand.app
```

Models go into the application store
(`~/Library/Application Support/stemhand/models`):

```bash
curl -L -o gtcrn_simple.onnx \
  https://huggingface.co/bitsydarel/gtcrn-onnx/resolve/main/gtcrn_simple.onnx
./scripts/install-gtcrn.sh gtcrn_simple.onnx

curl -L -o htdemucs.onnx \
  https://huggingface.co/MrCitron/demucs-v4-onnx/resolve/main/htdemucs.onnx
./scripts/install-demucs.sh htdemucs.onnx

open build/stemhand.app
```

**The macOS build has never been verified** — there is no Mac in the
development loop. Expected pitfalls (the `.app` PATH from Finder,
`macdeployqt`, the ONNX Runtime dylib, signing) are covered in
[docs/MACOS.md](docs/MACOS.md). Homebrew formula versions were checked on
2026-11-14: qt 6.12.0, onnxruntime 1.32.0, libsodium 1.0.22, ffmpeg 9.0.2.

### Building on Linux (development)

```bash
./scripts/fetch-onnxruntime.sh        # 35 MB, unpacks ONNX Runtime into third_party/
./scripts/dev-keys.sh                 # model signing keypair
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

ONNX Runtime is optional at configure time: without it everything builds except
the engine, and the denoiser says plainly that the engine is missing.

### Speed

The observed range on the development machine (i5-3210M, 2 cores / 4 threads, no
GPU) is **0.13–0.46× realtime** for separation (8 s of audio in 17–60 s). The
spread is not the code: the host's background load moves between 7 and 8 on two
cores, so every measurement here is biased. Numbers are comparable only within
one quiet window; Apple Silicon needs its own measurement.

Speech denoising on the same machine runs at 4.9–5.7× realtime on one thread.
More threads make it slower: the graph is tiny and synchronisation costs more
than the arithmetic, so one thread is the default.

### Models

```bash
./scripts/install-gtcrn.sh gtcrn_simple.onnx      # installs into the app store
```

```bash
./build/stemhand --denoise interview.wav clean.wav
./build/stemhand --separate song.mp3 stems/
./build/stemhand --mix stems/ mix.wav --solo vocals --gain vocals=3
./build/stemhand --mix stems/ mix.wav --mute drums
./build/stemhand --scan --json
```

A model manifest: `driver` names the runtime contract (`gtcrn`, `htdemucs`),
`engine_abi` is compatibility with the binary, `files[]` carries path, SHA-256
and size.

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

Signing: `stemhand-sign sign <model-dir> <private-key>` writes
`manifest.json.sig`. The public key is compiled into the binary at build time
(`keys/dev.pub` → `cmake/pinned_key.h.in`); the private key lives in
`.dev-keys/` and never enters git. The key is generated per checkout: committing
someone else's public key would pin an anchor nobody can sign with.

### Tests

```bash
./scripts/doctor.sh                        # environment
./scripts/build-macos.sh                   # build (macOS) or cmake --build build
cd build-macos && ctest --output-on-failure
cd .. && ./scripts/test.sh
```

On a clean checkout `ctest` reports **4 passed and 5 skipped**: tests that need
real weights, a registry fixture or prepared stems skip themselves when those
files are absent. `scripts/test.sh` covers everything that does not require
downloading models: the store, signatures, a flipped byte, the manifest format,
determinism and updates. The script finds the binary in `build/` or
`build-macos/`; if your build lives elsewhere, use
`STEMHAND_BIN=/path/to/stemhand ./scripts/test.sh`.

### Model licences

See [docs/MODELS.md](docs/MODELS.md). In short: for personal use there are no
restrictions, but `DrumSep 5-stem`, `BS-RoFormer` and `Spleeter` **state no
licence for their weights** — they cannot be redistributed, only downloaded
from their authors. MUSDB18, which nearly everything is trained on, is itself
non-commercial (`CC BY-NC-SA`).

### Licence

The code is MIT (see [LICENSE](LICENSE)). Model weights are not covered: each
has its own licence, broken down in [docs/MODELS.md](docs/MODELS.md). No weights
are shipped here — the app downloads them from their authors.

### Next

Verify the build on M1 and take the first honest timings there; wire up the
CoreML execution provider (the main speed lever on Apple Silicon); measure
quality on real music rather than synthetic material. Plan and reasoning live in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
