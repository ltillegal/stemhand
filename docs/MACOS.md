# Сборка на macOS (Apple Silicon)

Ни одной строки из этого документа я не проверял на Mac — машины под рукой нет.
Всё, что здесь есть, выведено из трёх фактов, которые проверяемы удалённо:
версии формул Homebrew (проверены 2026-11-14) и требования самого кода
(`CMAKE_OSX_DEPLOYMENT_TARGET 14.0`, arm64, Qt 6.5+).

## Зависимости

```bash
brew install qt onnxruntime libsodium ffmpeg pkg-config cmake
```

Проверенные версии в Homebrew на 2026-11-14 — ровно те же, что на Linux-машине:

| Формула | Версия |
|---|---|
| qt | 6.12.0 |
| onnxruntime | 1.32.0 |
| libsodium | 1.0.22 |
| ffmpeg | 9.0.2 |

`ffmpeg` нужен не для линковки, а как внешний процесс: приложение вызывает
`ffmpeg`/`ffprobe` из `PATH`. Если запускать `.app` из Finder, `PATH` будет
минимальным — см. «Грабли» ниже.

## Сборка

```bash
./scripts/build-macos.sh
```

Скрипт делает то же, что вручную:

```bash
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"
cmake --build build-macos -j
```

CMake найдёт `onnxruntime_cxx_api.h` в `$(brew --prefix onnxruntime)/include`,
а библиотеку — в `.../lib`. Если не найдёт, `ONNX Runtime not found` будет
предупреждением, а денойзинг и разделение честно скажут, что движка нет.

## Бандл

`macdeployqt` копирует внутрь `.app` плагины Qt, но не сторонние dylib.
`libonnxruntime.dylib` нужно положить в `Contents/Frameworks` и переписать
ссылку, иначе приложение не запустится на машине без Homebrew (на своей
запустится, потому что dylib лежит в `/opt/homebrew/lib`). Скрипт это делает:

1. `macdeployqt stemhand.app -always-overwrite`
2. копирует `libonnxruntime*.dylib` в `Contents/Frameworks`
3. `install_name_tool -change <brew path> @rpath/<name>` для бинаря
4. `install_name_tool -add_rpath @executable_path/../Frameworks`
5. `codesign --force --deep --sign -` (ad-hoc; для личного использования
   достаточно, для распространения нужен Developer ID и нотаризация)

## Проверка после сборки

```bash
open build-macos/stemhand.app
# или из терминала, чтобы видеть stderr:
./build-macos/stemhand.app/Contents/MacOS/stemhand --scan
```

Ожидаемый порядок проверки:

0. `./scripts/doctor.sh` — окружение; `cd build-macos && ctest` — 4 теста
   проходят, 5 пропускаются (нет весов и фикстур); `./scripts/test.sh` — все
   проверки хранилища и подписей.
1. `--scan` → каталог `~/Library/Application Support/stemhand/models` и
   «no models installed».
2. `scripts/install-gtcrn.sh gtcrn_simple.onnx` → модель `denoise/gtcrn`
   со статусом `ok` (подпись и хеши проверяются одинаково на обеих платформах).
3. `--denoise` на 10-секундном файле → первое настоящее число: сколько секунд
   на секунду аудио даёт M1 одним потоком.
4. `--separate` на 3-секундном фрагменте → четыре стема.

## Грабли, которых стоит ждать

| Симптом | Причина | Что делать |
|---|---|---|
| «ffmpeg and ffprobe are required but were not found in PATH» | Finder запускает `.app` с урезанным `PATH` | запускать из терминала или добавить `/opt/homebrew/bin` в окружение LaunchAgent |
| `dyld: Library not loaded: libonnxruntime...` | dylib не забандлен | шаг 2–4 выше |
| `Qt platform plugin "cocoa" not found` | `macdeployqt` не прогнан | прогнать |
| Медленнее, чем ожидалось | используется CPU-провайдер, CoreML ещё не включён | это ожидаемо: `CoreML` EP — следующий шаг, сейчас в коде только CPU |
| Модель 289 МБ не скачивается | нет сети/редиректа | скачать вручную и положить рядом, затем `--update file://...` |

## Что не проверено

- Сборка целиком: ни одного запуска на Apple Silicon не было.
- `CMAKE_OSX_ARCHITECTURES=arm64` жёстко задан в `CMakeLists.txt`; на Intel-Mac
  сборка не соберётся (и не должна — целевая платформа M1+).
- CoreML execution provider не подключён: в коде `Ort::SessionOptions` без
  `AppendExecutionProvider`. Для M1 это главный рычаг скорости, и включать его
  надо после того, как базовая сборка заработает, а не одновременно.
