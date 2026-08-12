# CLAUDE.md

Guida per Claude Code quando lavora su questo repository.

## Obiettivo del progetto

Firmware per **Seeed Studio XIAO ESP32-C6** che espone un dispositivo Matter
completo su rete **Thread**, basato su ESP-IDF v5.2.1 ed ESP-Matter. Il
dispositivo combina I/O digitali, un sensore SCD40 (CO2/temperatura/umidità)
e un display OLED SSD1306, tutti raggiungibili tramite endpoint Matter.

Il progetto (struttura, mappa GPIO, elenco endpoint) **rimane così com'è**:
il README in questa cartella è la fonte di verità su pin e funzionalità
esistenti e non va riprogettato. Nuovo lavoro = estendere/correggere questo
main, non ripensare l'uso dei GPIO già assegnati.

Riferimenti:
- Scheda: [XIAO ESP32-C6 getting started](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- Framework: [esp-idf v5.2.1](https://github.com/espressif/esp-idf/tree/v5.2.1), [esp-matter](https://github.com/espressif/esp-matter)

## Setup ambiente (obbligatorio prima di ogni build)

```bash
source $IDF_PATH/export.sh
source $ESP_MATTER_PATH/export.sh
```

`CMakeLists.txt` fallisce con `FATAL_ERROR` se `ESP_MATTER_PATH` non è
definita. Target chip: `esp32c6` (imposta `IDF_TARGET=esp32c6` se il progetto
non è già configurato — verificare `sdkconfig` prima di un `idf.py set-target`,
che invalida la build esistente).

## Comandi principali

Usare **sempre `./build.sh`**, non `idf.py` a mano, salvo necessità specifiche
(mantiene coerenza con i colleghi e stampa controlli su `IDF_PATH`/`ESP_MATTER_PATH`):

```bash
./build.sh menuconfig   # apre il menu di configurazione (Kconfig)
./build.sh build        # compila
./build.sh flash        # flash (porta di default /dev/ttyUSB0, override con PORT=...)
./build.sh monitor      # monitor seriale
./build.sh all          # build + flash + monitor
./build.sh clean        # pulisce build/
./build.sh fullclean    # pulisce build/ e managed_components/
./build.sh size         # dimensioni firmware/componenti
```

Su XIAO ESP32-C6 la porta seriale è spesso `/dev/ttyACM0`, non `ttyUSB0`:
`PORT=/dev/ttyACM0 ./build.sh flash`.

## Struttura del repository

```
c6_matter_thread_sd40_v1_oled/
├── CMakeLists.txt              # boilerplate esp-matter, target device HAL, project()
├── build.sh / flash.sh         # wrapper idf.py
├── partitions.csv              # tabella partizioni (4MB flash, OTA)
├── sdkconfig.defaults          # default SDK (Thread/Matter abilitati)
├── main/
│   ├── app_main.cpp            # entry point, GPIO, I2C, SCD40 driver, endpoint Matter
│   ├── app_reset.cpp/.h        # factory reset via bottone BOOT
│   ├── app_priv.h              # dichiarazioni condivise tra app_main e app_reset
│   ├── Kconfig.projbuild        # opzioni di progetto esposte in menuconfig
│   └── include/CHIPProjectConfig.h
└── components/
    └── oled_display/           # driver SSD1306 + font 8x8 (componente IDF locale)
```

**Non esiste una directory `components/` per driver GPIO dedicati**: gli
input/output sono gestiti direttamente in `app_main.cpp` (funzioni statiche +
task FreeRTOS `gpio_input`), non come componente separato. Estendere questo
stesso file per modifiche alla logica GPIO/endpoint, sul modello del codice
esistente; estrarre in `components/` (come `oled_display`) solo se emerge
duplicazione reale da eliminare, non preventivamente.

## Convenzioni di stile osservate nel codice esistente

- **C++17** per `main/` (vedi flag `-std=gnu++17` in CMakeLists.txt), **C** per
  i componenti driver (`components/oled_display` è puro C con header C).
- Namespace usati apertamente in app_main.cpp: `using namespace esp_matter;`,
  `esp_matter::attribute`, `esp_matter::endpoint`, `chip::app::Clusters`.
- Tag di logging per modulo: `static const char *TAG = "app_main";` — un TAG
  per file/componente, mai riusato tra moduli diversi.
- GPIO ed endpoint dichiarati come `#define` in cima al file con commento a
  fianco (es. `#define GPIO_INPUT_0 GPIO_NUM_0 // Input 1`), non come enum.
- ID endpoint Matter salvati in variabili `static uint16_t ..._endpoint_id`
  globali al file, popolate durante l'init e riferite dai task/callback.
- Configurazioni "tunabili" (GPIO, indirizzi I2C, intervalli) esposte via
  `Kconfig.projbuild` con default coerenti col valore hardcoded nel .cpp;
  quando si aggiunge un nuovo pin o parametro, aggiungere sempre la voce
  Kconfig corrispondente, non solo il `#define`.
- Task FreeRTOS con nome descrittivo, stack 4096, priorità 5 (vedi tabella
  "Background Tasks" nel README) — pattern da seguire per nuovi task.
- Commenti e log misti italiano/inglese: i log tecnici (`ESP_LOGI`/`ESP_LOGE`)
  sono in inglese, i commenti "narrativi" e le stringhe utente (es. display,
  messaggi NTP) possono essere in italiano. Seguire questo mix per continuità
  col resto del file, non forzare la traduzione di codice esistente.
- Gli header dei driver (`app_priv.h`) usano `#pragma once` ed espongono solo
  le funzioni necessarie tra moduli (niente struct interne dei driver).

## Note specifiche Matter/Thread

- Il device è multi-endpoint (17 endpoint totali): endpoint 1-N mappano
  funzioni fisiche in ordine di creazione nel codice — vedi tabella "Matter
  Endpoints" nel README, che è la fonte di verità aggiornata e va tenuta
  sincronizzata con `app_main.cpp` ad ogni modifica.
- Setup code/discriminator di commissioning sono hardcoded
  (`20202021` / `0xF00`) e documentati nel README: non vanno cambiati senza
  un motivo esplicito (romperebbero la commissioning già documentata per
  questo device).
- Il factory reset (bottone GPIO9/BOOT, 5s hold) deve invalidare anche
  eventuale stato persistito in NVS relativo a nuove funzionalità aggiunte
  (pattern: `nvs_erase_scd40_config()` in `app_priv.h`/`app_reset.cpp`).
- Thread è abilitato via sdkconfig (`CHIP_DEVICE_CONFIG_ENABLE_THREAD`); il
  codice deve restare compilabile anche quando questa opzione è assente,
  quindi ogni sezione Thread-specific in `app_main.cpp` è avvolta in
  `#if CHIP_DEVICE_CONFIG_ENABLE_THREAD`.

## GPIO — mappa attuale (invariata, non riassegnare)

Questa è la mappa **definitiva** per `c6_matter_thread_sd40_v1_oled`, come da
README del progetto. Qualunque nuovo sviluppo deve rispettarla: nessun pin va
riassegnato o reinterpretato.

| GPIO | Uso |
|------|-----|
| 0 | Input 1 — Contact Sensor (Matter endpoint 1) |
| 1 | Input 2 — Contact Sensor (Matter endpoint 2) |
| 2 | Input 3 — Contact Sensor (Matter endpoint 3) |
| 21 | Input 4 — Contact Sensor (Matter endpoint 4) |
| 19 | Output 1 — On/Off Light (Matter endpoint 5) |
| 20 | Output 2 — On/Off Light (Matter endpoint 6) |
| 22 | I2C SDA (SCD40 + OLED) |
| 23 | I2C SCL (SCD40 + OLED) |
| 9 | Bottone reset di fabbrica (BOOT) |
| 15 | LED stato Thread |
| 3 | Controllo antenna RF — enable (deve restare LOW) |
| 14 | Controllo antenna RF — select interno/esterno |

Nota storica: GPIO0 e GPIO1 sono **input**, non output — qualunque richiesta
generica di "controllare GPIO0" va quindi intesa nel contesto di questo
progetto come lettura/gestione del relativo Contact Sensor endpoint, non come
riassegnazione a output.

## Verifica dopo ogni modifica

Dopo ogni modifica a codice o configurazione:
1. `./build.sh build` e controllare che non ci siano warning nuovi legati ai
   file toccati (in particolare su GPIO non configurati o cluster Matter).
2. Se si tocca `Kconfig.projbuild`, rilanciare `./build.sh menuconfig` (anche
   in modalità non interattiva `idf.py menuconfig` fallisce silenziosamente
   su opzioni malformate: controllare che `sdkconfig` venga rigenerato con
   la nuova voce).
3. Se si tocca la logica GPIO/Matter, non esiste ancora un target di unit
   test host-side in questo repo: la verifica minima è la build pulita
   (`./build.sh fullclean && ./build.sh build`) più ispezione manuale del log
   endpoint in `./build.sh monitor` dopo flash su hardware reale (non c'è
   CI/hardware-in-the-loop configurato).
4. Aggiornare il README del progetto toccato (tabella GPIO, tabella endpoint,
   sezione "Project Structure") in modo che resti sincronizzato col codice —
   è già successo che il README documenti pin/endpoint in modo dettagliato:
   mantenere quello stile per coerenza.

## Cose da NON fare

- Non introdurre `idf.py` invocato direttamente nei task di build se esiste
  già un target equivalente in `build.sh`: estendere lo script piuttosto che
  bypassarlo.
- Non hardcodare setup code/discriminator diversi da quelli documentati senza
  aggiornare il README (rompe la commissioning già documentata).
- Non rimuovere i controlli `#if CHIP_DEVICE_CONFIG_ENABLE_THREAD` — il
  codice deve restare portabile anche a build Wi-Fi/BLE-only.
- Non committare `sdkconfig` generato da una configurazione locale se diverge
  dai default di progetto senza discuterne (il repo tiene sia `sdkconfig` sia
  `sdkconfig.defaults`: verificare quale dei due si intende aggiornare prima
  di fare `git add`).
