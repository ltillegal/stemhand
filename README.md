# Photone

Локальное приложение для разделения и очистки аудио: стемы из готового микса,
барабаны, голоса, денойзинг — всё на своей машине, без загрузок наружу.
Целевая платформа — macOS на Apple Silicon (M1, Sequoia); разработка идёт на Linux.

![Photone](docs/screenshot.png)

## Что уже есть (M0)

Это **оболочка с настоящей подсистемой доверия к моделям**, а не готовый продукт.
Инференса пока нет — следующий шаг (M1).

- **Хранилище моделей** `~/Library/Application Support/photone/models/<id>/<version>/`
  (на Linux — `~/.local/share/photone/models`).
- **Манифест + подпись.** Ed25519-подпись покрывает `manifest.json` байт-в-байт,
  манифест пинит SHA-256 каждого файла весов. Одна подпись аутентифицирует весь
  каталог, подмена весов ловится хешем.
- **Статусы:** `ok` (подпись и хеши сходятся), `unverified` (хеши сходятся, подписи
  нет), `corrupt` (файл изменён/отсутствует), `incompatible` (другой engine ABI),
  `broken` (нет или битый манифест).
- **Просмотр волны** через `ffmpeg`/`ffprobe`: метаданные + пиковый конверт на 8 кГц.
- **CLI без дисплея:** `photone --scan [--json]` — для тестов и агентов.

Проверено на этой машине: сборка, сканирование трёх подписанных моделей,
детект подмены байта (`sha256 mismatch`), детект отсутствия подписи (`unverified`),
код возврата 1 при повреждённой модели.

## Сборка

Linux (разработка):

```bash
./scripts/dev-keys.sh                 # один раз: ключевая пара для подписи моделей
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

macOS (цель), после `brew install qt ffmpeg libsodium cmake`:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
open build/photone.app
```

Зависимости: Qt 6.5+ (Core, Gui, Quick, QuickControls2, Network), libsodium,
ffmpeg в PATH. ONNX Runtime появится на M1.

## Запуск

```bash
./build/photone                                  # пустое окно, каталог моделей по умолчанию
./build/photone --root build/demo-models         # демо-хранилище
./build/photone --scan --json --root build/demo-models
./scripts/demo-models.sh                         # три фейковые подписанные модели
```

Горячие клавиши: `O` открыть файл, `R` пересканировать модели, `I` импортировать
папку модели, `Q` выход. Файл можно перетащить в окно.

## Формат модели

```json
{
  "schema": 1,
  "id": "denoise/deepfilternet3",
  "version": "3.0.0",
  "name": "DeepFilterNet 3",
  "kind": "denoise",
  "engine_abi": 1,
  "sample_rate": 48000,
  "channels": 2,
  "stems": ["voice"],
  "eps": ["cpu", "coreml"],
  "license": { "code": "MIT", "weights": "MIT", "commercial": true },
  "files": [
    { "path": "deepfilternet3.onnx", "sha256": "...", "size": 1234567 }
  ]
}
```

Подпись: `photone-sign sign <model-dir> <private-key>` создаёт `manifest.sig`
(Ed25519 по точным байтам `manifest.json`). Проверка: `photone-sign verify`.

Публичный ключ зашит в бинарь (`keys/dev.pub` → `cmake/pinned_key.h.in`).
Приватный лежит вне репозитория (`.dev-keys/`, в git не попадает).

## Лицензии моделей

См. [docs/MODELS.md](docs/MODELS.md). Коротко: при личном использовании ограничений
нет, но `DrumSep 5-stem`, `BS-RoFormer` и `Spleeter` **не декларируют лицензию весов** —
их нельзя перераспространять, только скачивать у авторов. MUSDB18, на котором обучено
почти всё, сам некоммерческий (`CC BY-NC-SA`).

## Дальше

M1: ONNX Runtime + DeepFilterNet3 (официальный ONNX-экспорт, 2.17M параметров) —
первый реальный звук. Затем htdemucs (MIT) для 4 стемов и RoFormer для качества.
План и обоснования — [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
