# Установка плагина «Route with freerouting-cpp» в KiCad

Плагин добавляет в редактор плат KiCad кнопку, которая разводит открытую плату
C++-портом freerouting. Плата выгружается в Specctra DSN, роутер `freerouting-cpp.exe`
разводит её, результат (SES) загружается обратно в плату.

Основной вариант — **нативный Windows-роутер** `freerouting-cpp.exe` рядом с плагином:
ни WSL, ни Linux не нужны. Запасной вариант — роутер внутри WSL (раздел 8).

Инструкция для Windows и KiCad 10. Плагин работает и в KiCad 6–9: отличается только номер
версии в пути к каталогу плагинов.

## 1. Что нужно

| Что | Где взять / как проверить |
|---|---|
| KiCad 6–10 (проверено на 10.0.8) | KiCad → Справка → О программе |
| Папка плагина `freerouting_cpp` | в репозитории: `integrations\kicad\freerouting_cpp` |
| Роутер `freerouting-cpp.exe` | сборка C++-порта под Windows (один статический файл, без DLL) |

## 2. Установка

Каталог плагинов KiCad 10:
`Документы\KiCad\10.0\scripting\plugins` (`%USERPROFILE%\Documents\KiCad\10.0\scripting\plugins`).
Если у вас он другой, откройте его из KiCad: в редакторе плат
**Инструменты → Внешние плагины → Открыть каталог плагинов**
(Tools → External Plugins → Open Plugin Directory).

1. Скопируйте папку плагина **целиком** (не её содержимое) в каталог плагинов. Команда
   выполняется из корня репозитория:

   ```powershell
   Copy-Item -Recurse -Force integrations\kicad\freerouting_cpp "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\"
   ```

2. Создайте в ней подпапку `bin`:

   ```powershell
   New-Item -ItemType Directory -Force "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\bin"
   ```

3. Положите туда роутер (вместо `<папка с exe>` — где лежит собранный `freerouting-cpp.exe`):

   ```powershell
   Copy-Item -Force <папка с exe>\freerouting-cpp.exe "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\bin\"
   ```

В итоге должно получиться:

```
Документы\KiCad\10.0\scripting\plugins\
└── freerouting_cpp\
    ├── __init__.py
    ├── plugin.py
    ├── runner.py
    ├── icon.png
    └── bin\
        └── freerouting-cpp.exe
```

Плагин ищет роутер в таком порядке:
1. путь из поля «Native executable» в настройках, если оно заполнено;
2. `freerouting_cpp\bin\freerouting-cpp.exe`;
3. `freerouting_cpp\freerouting-cpp.exe`.

Если exe не найден, плагин в режиме Auto запускает роутер через WSL (раздел 8).

## 3. Включение в KiCad

1. Откройте плату в редакторе плат (PCB Editor).
2. **Инструменты → Внешние плагины → Обновить плагины** (Tools → External Plugins →
   Refresh Plugins) или просто перезапустите KiCad.
3. В меню **Инструменты → Внешние плагины** появится пункт **Route with freerouting-cpp**,
   а на верхней панели — кнопка с зелёно-красными дорожками.
4. Если кнопки на панели нет: **Настройки → Параметры → Редактор плат → Плагины действий**
   (Preferences → PCB Editor → Action Plugins) и отметьте «Показывать кнопку» для плагина.

## 4. Первый запуск

1. **Сохраните плату.** Импорт результата заменяет все дорожки и переходы платы, и отменить
   его одним шагом может не получиться.
2. Нажмите кнопку плагина. Откроется окно настроек. Внизу, в рамке «Router», написано, какой
   роутер будет запущен («Will run: …\bin\freerouting-cpp.exe»). Если там
   «Native executable not found», значит exe лежит не там (см. раздел 2).

   | Поле | Что делает | По умолчанию |
   |---|---|---|
   | Threads | потоки оптимизатора (`-mt`) | 4 |
   | Via cost | стоимость перехода (`--router.scoring.via_costs`); больше — меньше переходов | 0 = по умолчанию |
   | Max passes | наибольшее число проходов трассировщика (`-mp`) | 0 = по умолчанию |
   | Timeout, min | ограничение времени (`--timeout PT<N>M`) | 0 = без ограничения |
   | Skip the optimizer | без оптимизации: быстрее, но дорожки длиннее | выкл. |
   | Skip the SMD fanout stage | без этапа fanout (`--router.fanout.enabled=false`) | выкл. |
   | Extra arguments | любые другие настройки роутера в виде `--router.<путь>=<значение>`, например `--router.automatic_neckdown=false`; идут последними и перекрывают всё остальное | пусто |
   | Pass the board's design rules | передать ограничения платы ключами `--router.*` (раздел 5) | вкл. |
   | Give the router the project files too | передать роутеру ещё `.kicad_pro`, `.kicad_dru`, `.kicad_pcb` (раздел 5) | вкл. |
   | LUT / single-layer profile | профиль для ЛУТ и однослойных плат (раздел 5) | выкл. |
   | Via cost in this profile | стоимость перехода в профиле ЛУТ | 1000 |
   | Run | Auto — exe, если найден, иначе WSL; Native (.exe) — только exe; WSL — только WSL | Auto |
   | Native executable | путь к `freerouting-cpp.exe`, если он не рядом с плагином | пусто |
   | WSL distribution, Router in WSL | только для режима WSL (раздел 8) | `Ubuntu`, `/usr/local/bin/freerouting-cpp` |

   Значения запоминаются в `freerouting_cpp\settings.json` рядом с плагином.
3. Нажмите **Route**. В окне хода работы видны этап (проход трассировки, сколько связей ещё
   не разведено, проход оптимизатора), полоса прогресса, время и лог роутера. Окно консоли
   роутера не появляется.
   **Cancel** останавливает роутер; плата при этом не меняется.
4. После успешной разводки результат загружается в плату, полигоны перезаливаются.
   Если остались неразведённые связи или нарушения зазоров, плагин об этом скажет —
   запустите DRC, чтобы их увидеть.

## 5. Что плагин берёт из проекта

Плагин ничего не дописывает в DSN: всё, что он берёт из проекта сверх DSN, передаётся
роутеру ключами командной строки. В начале окна хода работы и в `router.log` перечислено,
что именно взято.

**Ограничения платы** (галочка «Pass the board's design rules», включена по умолчанию).
Берутся из **Параметры платы → Правила проектирования → Ограничения** (Board Setup →
Design Rules → Constraints):

| Ограничение KiCad | Ключ роутера |
|---|---|
| Минимальная ширина дорожки и минимальная ширина соединения (большая из двух) | `--router.neck_width_um` |
| Зазор от меди до края платы | `--router.copper_to_edge_clearance_um` |
| Зазор от меди до отверстия | `--router.hole_clearance_um` |
| Зазор между отверстиями | `--router.hole_to_hole_um` |

Значения в микрометрах; ограничение, равное 0, не передаётся. **Минимальный зазор** не
передаётся: такого ключа у роутера нет, а зазоры классов цепей и так попадают к нему через
DSN. `neck_width_um` включает сужение: связь, которая не разводится шириной своего класса,
пробуется ещё раз этой шириной.

**Файлы проекта** (галочка «Give the router the project files too», включена). Роутер получит
`-de плата.dsn+плата.kicad_pro+плата.kicad_dru+плата.kicad_pcb` — из файлов, которые есть рядом
с платой. `.kicad_pcb` берётся с диска, поэтому плату надо сохранить. Роутер читает их так же,
как Java freerouting: правила зазоров из `.kicad_dru`, минимальную ширину дорожки из
`.kicad_pro`, зазоры зон и подключение площадок из `.kicad_pcb`.

**Профиль «ЛУТ / однослойная»** (галочка «LUT / single-layer profile»). Для самодельных плат:
лазерно-утюжная технология, один слой меди, верхний слой — только перемычки. Одна галочка
добавляет

```
--router.fanout.enabled=false --router.automatic_neckdown=false
--router.autorouter.fallback_strategies=none --router.scoring.via_costs=1000
```

Стоимость перехода задаётся полем «Via cost in this profile». Каждый переход — это перемычка,
поэтому высокая стоимость делает их как можно меньше. Полноценный режим «верхний слой =
перемычки» будет позже. Ширина дорожек и зазоры берутся из классов цепей KiCad
(**Параметры платы → Классы цепей**). Если у какого-то класса дорожки уже 0,5 мм, плагин
перед разводкой спросит, продолжать ли: для ЛУТ это тонко.

## 6. Обновление и удаление

- **Обновить плагин:** повторите шаг 1 раздела 2 (файлы перезапишутся, `bin\` и
  `settings.json` сохранятся), затем Refresh Plugins или перезапуск KiCad.
- **Обновить роутер:** повторите шаг 3 раздела 2. Пока идёт разводка, exe занят — сначала
  дождитесь конца или нажмите Cancel.
- **Удалить:** удалите папку
  `Документы\KiCad\10.0\scripting\plugins\freerouting_cpp` и обновите плагины.

## 7. Неполадки

**Кнопки нет и пункта в меню нет.** Проверьте, что `__init__.py` лежит именно в
`plugins\freerouting_cpp\`. Ошибки загрузки Python-плагинов KiCad показывает в
Настройки → Параметры → Редактор плат → Плагины действий (кнопка с предупреждением,
если какой-то плагин не загрузился). Можно также открыть **Инструменты → Консоль
скриптов** и выполнить `import freerouting_cpp` — будет видна ошибка импорта.

**«Native executable not found» / «cannot start the router».** Exe не там, где его ищет плагин:
проверьте путь `freerouting_cpp\bin\freerouting-cpp.exe` или укажите полный путь в поле
«Native executable». Антивирус может блокировать или удалить новый exe — проверьте его
карантин.

**«Specctra DSN export failed».** KiCad не смог выгрузить плату — обычно из-за
посадочного места без контура или неверного контура платы (Edge.Cuts). Попробуйте вручную
**Файл → Экспорт → Specctra DSN**: KiCad покажет причину.

**«The router failed (exit code …)».** Окно остаётся открытым с логом. Полный лог —
в `%TEMP%\kicad-freerouting-cpp\<имя платы>\router.log` (первая строка — точная команда
запуска; её можно повторить в PowerShell).

**DRC после разводки сообщает `track_width`.** Роутер сужает дорожки у выводов
(neckdown). Чтобы отключить, впишите в Extra arguments
`--router.automatic_neckdown=false`.

**Где временные файлы.** `%TEMP%\kicad-freerouting-cpp\<имя платы>\`: выгруженный
`<плата>.dsn`, результат `<плата>.ses` и `router.log`. Они перезаписываются при следующем
запуске для той же платы.

## 8. Запасной вариант: роутер в WSL

Если exe нет, в режиме Auto (или WSL) плагин запускает Linux-сборку роутера:
`wsl -d Ubuntu -e /usr/local/bin/freerouting-cpp ...`, пути переводятся
в `/mnt/c/...`. Для этого нужны:

- WSL с дистрибутивом `Ubuntu` (`wsl -l -v`);
- роутер по пути `/usr/local/bin/freerouting-cpp` (или свой путь в поле
  «Router in WSL»); проверка:

  ```powershell
  wsl -d Ubuntu -e /usr/local/bin/freerouting-cpp --help
  ```

- библиотека `libgeos_c`; если роутер пишет `error while loading shared libraries: libgeos_c`:

  ```powershell
  wsl -d Ubuntu -e sudo apt install -y libgeos-c1t64
  ```

  (так пакет называется в Ubuntu 24.04 и новее; в более старых — `libgeos-c1v5`).

## 9. Совместимость

Плагин использует SWIG-API `pcbnew` (`ExportSpecctraDSN` / `ImportSpecctraSES`), как и режим
DSN upstream-плагина freerouting. Этот API есть в KiCad 6–10. Разработчики KiCad планируют
убрать SWIG-плагины после версии 10; для следующих версий плагин нужно будет переписать
на IPC API.
