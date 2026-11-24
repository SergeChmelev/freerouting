# Установка плагина «Route with freerouting-cpp» в KiCad

Плагин добавляет в редактор плат KiCad кнопку, которая разводит открытую плату
C++-портом freerouting. Плата выгружается в Specctra DSN, роутер `freerouting-cpp.exe`
разводит её, результат (SES) загружается обратно в плату.

Основной вариант — **нативный Windows-роутер** `freerouting-cpp.exe` рядом с плагином:
ни WSL, ни Linux не нужны. Запасной вариант — роутер внутри WSL (раздел 7).

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

1. Скопируйте папку плагина **целиком** (не её содержимое) в каталог плагинов:

   ```powershell
   Copy-Item -Recurse -Force C:\Users\HP\VibeCodes\freerouting\integrations\kicad\freerouting_cpp "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\"
   ```

   Пока ветка с плагином не слита в main, папка есть только в её worktree:
   `C:\Users\HP\VibeCodes\freerouting\.claude\worktrees\keen-chatterjee-c56a3f\integrations\kicad\freerouting_cpp`
   — подставьте этот путь.

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

Если exe не найден, плагин в режиме Auto запускает роутер через WSL (раздел 7).

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
   | Extra arguments | любые другие настройки роутера в виде `--router.<путь>=<значение>`, например `--router.automatic_neckdown=false` | пусто |
   | Run | Auto — exe, если найден, иначе WSL; Native (.exe) — только exe; WSL — только WSL | Auto |
   | Native executable | путь к `freerouting-cpp.exe`, если он не рядом с плагином | пусто |
   | WSL distribution, Router in WSL | только для режима WSL (раздел 7) | `Ubuntu`, `/usr/local/bin/freerouting-cpp` |

   Значения запоминаются в `freerouting_cpp\settings.json` рядом с плагином.
3. Нажмите **Route**. В окне хода работы видны этап (проход трассировки, сколько связей ещё
   не разведено, проход оптимизатора), полоса прогресса, время и лог роутера. Окно консоли
   роутера не появляется.
   **Cancel** останавливает роутер; плата при этом не меняется.
4. После успешной разводки результат загружается в плату, полигоны перезаливаются.
   Если остались неразведённые связи или нарушения зазоров, плагин об этом скажет —
   запустите DRC, чтобы их увидеть.

## 5. Обновление и удаление

- **Обновить плагин:** повторите шаг 1 раздела 2 (файлы перезапишутся, `bin\` и
  `settings.json` сохранятся), затем Refresh Plugins или перезапуск KiCad.
- **Обновить роутер:** повторите шаг 3 раздела 2. Пока идёт разводка, exe занят — сначала
  дождитесь конца или нажмите Cancel.
- **Удалить:** удалите папку
  `Документы\KiCad\10.0\scripting\plugins\freerouting_cpp` и обновите плагины.

## 6. Неполадки

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

## 7. Запасной вариант: роутер в WSL

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

## 8. Совместимость

Плагин использует SWIG-API `pcbnew` (`ExportSpecctraDSN` / `ImportSpecctraSES`), как и режим
DSN upstream-плагина freerouting. Этот API есть в KiCad 6–10. Разработчики KiCad планируют
убрать SWIG-плагины после версии 10; для следующих версий плагин нужно будет переписать
на IPC API.
