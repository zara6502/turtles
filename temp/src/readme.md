Подготовил обновление на основе твоего `temp/src`.

Сборка и тесты пройдены

**Файлы:**

- Полный пакет ZIP
- Обновлённый maze_automata.cpp
- Пакетный runner Python
- Инструкция

Что изменено:

- BFS больше не запускается даже при `--algo all`.
- Память парного автомата сокращена примерно с 44 до 8 байт на пару.
- Уменьшено потребление памяти таблицей посещённых состояний A\*.
- Добавлен пакетный запуск по `manifest.csv`.
- Проверяется совпадение DAFds с результатами исходного benchmark.
- Для незавершённого A\* сохраняется достигнутая нижняя граница.

### **Первый запуск: 60 лабиринтов**

Распакуй ZIP с сохранением структуры `temp/src`, собери:

```
g++ -std=c++17 -O3 -DNDEBUG -march=native -Wall -Wextra -Wpedantic temp/src/maze_automata.cpp -o maze_automata.exe
```

Затем:

```
python temp/src/maze_automata_sweep.py \
  --manifest temp/dafds/manifest.csv \
  --maze-root PATH_TO_LOCAL_SWEEP \
  --reference-runs temp/dafds/runs.csv \
  --exe ./maze_automata.exe \
  --limit 5 \
  --seed-from 51 --seed-to 55 \
  --max-states 1000000 \
  --time-limit 120 \
  --max-pair-states 70000000 \
  --output-dir temp/astar_pilot
```

`PATH_TO_LOCAL_SWEEP` — каталог, внутри которого находятся файлы `raw/mazes`, указанные в manifest. В опубликованном репозитории самих лабиринтов нет.

После пилота можно убрать `--seed-from` и `--seed-to`, чтобы запустить все 600 лабиринтов.

Результаты появятся в `temp/astar_pilot/`: `astar_runs.csv` и `summary.txt`.

**Пока не будем менять алгоритм поиска.** Сначала посмотрим, насколько хорошо A\* справляется с реальными лабиринтами 51×51, 101×101 и 151×151.
