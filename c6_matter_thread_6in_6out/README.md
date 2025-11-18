# ESP32-C6 Matter over Thread - 4 Inputs + 4 Outputs + Antenna Control + LED Status

Dispositivo Matter over Thread per **ESP32-C6** (XIAO) con **4 sensori di contatto** (ingressi), **4 luci On/Off** (uscite), **controllo antenna RF** (interna/esterna) e **LED di stato Thread** - tutto controllabile indipendentemente via Matter.

## Informazioni Dispositivo

- **Vendor**: VicinoDiCasaDigitale
- **Product**: Matter Thread 6in/6out
- **Chip**: ESP32-C6 (XIAO - con radio Thread 802.15.4 nativa)
- **Protocollo**: Matter over Thread (Thread FTD - Full Thread Device)
- **Commissioning**: BLE (Bluetooth Low Energy)

## Configurazione Hardware

### GPIO Ingressi (Contact Sensors)

4 sensori di contatto indipendenti configurati con **pull-up interno**:

| Input | GPIO | Funzione Matter | Logica Fisica |
|-------|------|-----------------|---------------|
| Input 1 | GPIO 0 | Contact Sensor 1 | HIGH = Aperto, LOW = Chiuso |
| Input 2 | GPIO 1 | Contact Sensor 2 | HIGH = Aperto, LOW = Chiuso |
| Input 3 | GPIO 2 | Contact Sensor 3 | HIGH = Aperto, LOW = Chiuso |
| Input 4 | GPIO 21 | Contact Sensor 4 | HIGH = Aperto, LOW = Chiuso |

**Nota sulla logica**: Gli ingressi usano pull-up interno, quindi quando il contatto è **aperto** (circuito aperto), il pin legge **HIGH**. Quando il contatto è **chiuso** (circuito chiuso a massa), il pin legge **LOW**. Il firmware inverte automaticamente questa logica per Matter: aperto fisico = StateValue `true` (open), chiuso fisico = StateValue `false` (closed).

### GPIO Uscite (On/Off Lights)

4 uscite digitali indipendenti controllabili via Matter:

| Output | GPIO | Funzione Matter | Stato Iniziale |
|--------|------|-----------------|----------------|
| Output 1 | GPIO 22 | On/Off Light 1 | OFF (LOW) |
| Output 2 | GPIO 23 | On/Off Light 2 | OFF (LOW) |
| Output 3 | GPIO 19 | On/Off Light 3 | OFF (LOW) |
| Output 4 | GPIO 20 | On/Off Light 4 | OFF (LOW) |

**Nota**: Le uscite sono attive HIGH (1 = ON, 0 = OFF).

### Controllo Antenna RF (XIAO ESP32C6)

Il dispositivo include il controllo dello switch RF per selezionare tra antenna interna ed esterna:

| GPIO | Funzione | Valore | Effetto |
|------|----------|--------|---------|
| GPIO 3 | RF Switch Enable | LOW | Abilita il controllo dello switch RF |
| GPIO 14 | Antenna Selection | LOW | Antenna ceramica interna (default) |
| GPIO 14 | Antenna Selection | HIGH | Antenna esterna (connettore UFL) |

**Controllo via Matter**: È disponibile un **endpoint virtuale** (On/Off Light 5) che permette di switchare l'antenna remotamente:
- **ON** = Antenna Esterna (UFL connector)
- **OFF** = Antenna Interna (ceramica)

**Range tipico**:
- Antenna interna: ~80m (BLE/Thread)
- Antenna esterna: dipende dall'antenna UFL collegata (generalmente >150m)

### LED di Stato Thread (GPIO 15)

Il dispositivo include un **LED di stato** sul **GPIO 15** (USER LED della XIAO ESP32C6) che indica visivamente il ruolo del dispositivo nella mesh Thread:

| Ruolo Thread | Pattern LED | Descrizione Timing |
|--------------|-------------|-------------------|
| **Disconnesso** (DISABLED/DETACHED) | OFF | LED spento |
| **End Device** (CHILD) | Solid ON | Acceso fisso (dispositivo senza routing) |
| **Router** | Single Blink | 1000ms ON + 250ms OFF (routing abilitato) |
| **Leader** | Double Blink | Doppio lampeggio: 250ms OFF → 200ms ON → 250ms OFF → 1000ms ON |

**Vantaggi del LED di Stato**:
- Diagnosi rapida dello stato di rete senza seriale
- Identifica immediatamente il ruolo del dispositivo nella mesh
- Aggiornamento in tempo reale quando il ruolo cambia (max ritardo 1 secondo)

**Nota Tecnica**: Il LED viene gestito da un task FreeRTOS dedicato (`thread_status_led_task`) che polling il ruolo Thread ogni secondo tramite OpenThread API.

## Configurazione Thread Mesh

Il dispositivo è configurato come **Thread FTD (Full Thread Device)**, il che significa:
- Può agire come router nella mesh Thread
- Mantiene sempre la radio attiva
- Può instradare traffico per altri dispositivi
- Supporta dispositivi child (se configurato come parent)

### Parametri Thread (in `sdkconfig.defaults`)

```ini
CONFIG_OPENTHREAD_ENABLED=y
CONFIG_OPENTHREAD_SRP_CLIENT=y
CONFIG_OPENTHREAD_DNS_CLIENT=y
CONFIG_OPENTHREAD_LOG_LEVEL_NOTE=y
```

## Come Modificare i Parametri

### Cambiare Vendor Name e Product Name

Modifica il file `main/app_main.cpp` alle **righe 231-232** (circa):

```cpp
const char *vendor_name = "VicinoDiCasaDigitale";  // <- Cambia qui
const char *product_name = "Matter Thread 6in/6out"; // <- Cambia qui
```

**IMPORTANTE**: Questi valori vengono scritti in NVS **prima** dell'avvio di Matter. Se cambi i nomi dopo aver già fatto commissioning, è necessario fare un factory reset.

### Cambiare GPIO degli Ingressi

Modifica il file `main/app_main.cpp` alle **righe 37-40** (circa):

```cpp
#define GPIO_INPUT_0  GPIO_NUM_0   // Input 1 <- Cambia qui
#define GPIO_INPUT_1  GPIO_NUM_1   // Input 2 <- Cambia qui
#define GPIO_INPUT_2  GPIO_NUM_2   // Input 3 <- Cambia qui
#define GPIO_INPUT_3  GPIO_NUM_21  // Input 4 <- Cambia qui
```

### Cambiare GPIO delle Uscite

Modifica il file `main/app_main.cpp` alle **righe 43-46** (circa):

```cpp
#define GPIO_OUTPUT_0 GPIO_NUM_22  // Output 1 <- Cambia qui
#define GPIO_OUTPUT_1 GPIO_NUM_23  // Output 2 <- Cambia qui
#define GPIO_OUTPUT_2 GPIO_NUM_19  // Output 3 <- Cambia qui
#define GPIO_OUTPUT_3 GPIO_NUM_20  // Output 4 <- Cambia qui
```

### Cambiare Antenna di Default

Modifica il file `main/app_main.cpp` alla **linea 54** (circa):

```cpp
#define USE_EXTERNAL_ANTENNA  0  // 0 = antenna interna, 1 = antenna esterna
```

**Opzioni**:
- `0` = Antenna interna (ceramica) - **default**
- `1` = Antenna esterna (UFL connector)

**Nota**: Questa è solo l'impostazione iniziale all'avvio. L'antenna può essere cambiata dinamicamente via Matter dopo il boot.

### Cambiare GPIO di Controllo Antenna

Modifica il file `main/app_main.cpp` alle **righe 57-58** (circa):

```cpp
#define GPIO_WIFI_ENABLE      GPIO_NUM_3   // RF switch enable <- Cambia qui
#define GPIO_WIFI_ANT_CONFIG  GPIO_NUM_14  // Antenna selection <- Cambia qui
```

**Nota**: Secondo la documentazione ufficiale Seeed Studio, i GPIO corretti sono GPIO3 e GPIO14. Se il tuo hardware è diverso, modifica di conseguenza.

### Cambiare GPIO del LED di Stato Thread

Modifica il file `main/app_main.cpp` alla **linea 54** (circa):

```cpp
#define GPIO_STATUS_LED       GPIO_NUM_15  // USER LED - Thread role indicator <- Cambia qui
```

**Opzioni**:
- `GPIO_NUM_15` = USER LED sulla XIAO ESP32C6 (default)
- Qualsiasi GPIO disponibile con LED esterno collegato

**Nota**: Il LED è attivo HIGH (1 = acceso, 0 = spento).

### Cambiare Vendor ID e Product ID (Matter)

Modifica il file `sdkconfig.defaults` alle **righe 28-29**:

```ini
CONFIG_DEVICE_VENDOR_ID=0xFFF1    # <- Cambia qui (formato esadecimale)
CONFIG_DEVICE_PRODUCT_ID=0x8001   # <- Cambia qui (formato esadecimale)
```

**Nota**:
- `0xFFF1` è un Vendor ID di test. Per produzione, registra un Vendor ID ufficiale presso la CSA (Connectivity Standards Alliance).
- Il Product ID deve essere univoco per ciascun tipo di prodotto del tuo vendor.

### Modificare Numero di Ingressi/Uscite

Per cambiare il numero di ingressi o uscite (es. da 4 a 6):

1. **Modifica le dimensioni degli array** in `main/app_main.cpp`:
   ```cpp
   // Linee 67-70 (circa)
   static uint16_t output_endpoint_ids[6] = {0, 0, 0, 0, 0, 0};  // Cambia 4 in 6
   static uint16_t input_endpoint_ids[6] = {0, 0, 0, 0, 0, 0};   // Cambia 4 in 6

   // Linee 75-76 (circa)
   static const gpio_num_t input_pins[6] = {...};   // Aggiungi 2 GPIO
   static const gpio_num_t output_pins[6] = {...};  // Aggiungi 2 GPIO
   ```

2. **Modifica i loop** che iterano su ingressi/uscite:
   - `app_attribute_update_cb()`: `for (int i = 0; i < 6; i++)`
   - `gpio_input_task()`: `for (int i = 0; i < 6; i++)`
   - Creazione endpoint ingressi: `for (int i = 0; i < 6; i++)`
   - Creazione endpoint uscite: `for (int i = 0; i < 6; i++)`
   - Inizializzazione uscite: `for (int i = 0; i < 6; i++)`

3. **Aggiungi le definizioni GPIO** per i nuovi pin.

## Requisiti Software

- **ESP-IDF**: v5.1 o superiore
- **ESP-Matter SDK**: versione compatibile con IDF
- **Python**: 3.8 o superiore
- **Thread Border Router**: necessario per commissioning e operazioni

## Installazione Environment

```bash
# 1. Installa ESP-IDF (se non già installato)
cd ~/esp
git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c6
. ./export.sh

# 2. Installa ESP-Matter (se non già installato)
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
./install.sh
. ./export.sh

# 3. Naviga al progetto
cd /home/max/esp/project/project_c/c6_matter_thread_6in_6out
```

## Compilazione e Flash

### Prima Compilazione

```bash
# Configura il target ESP32-C6 (solo prima volta)
idf.py set-target esp32c6

# Compila il progetto
idf.py build
```

### Dopo Modifiche alla Configurazione

Se modifichi `sdkconfig.defaults`:

```bash
# Riconfigura e ricompila
idf.py reconfigure
idf.py build
```

Se modifichi solo il codice sorgente (`app_main.cpp`, ecc.):

```bash
# Compila direttamente
idf.py build
```

### Flash del Firmware

```bash
# Flash e monitor
idf.py -p /dev/ttyACM0 flash monitor

# Solo flash (senza monitor)
idf.py -p /dev/ttyACM0 flash

# Solo monitor (dopo flash)
idf.py -p /dev/ttyACM0 monitor
```

**Nota**: Su Windows usa `COM3` o la porta appropriata invece di `/dev/ttyACM0`.

## Commissioning del Dispositivo

### 1. Avvio e Codici di Commissioning

All'avvio, il dispositivo stamperà sulla seriale i codici di pairing:

```
====================================
   COMMISSIONING INFORMATION
====================================
Manual pairing code: 34970112332
QRCode: MT:Y.K9042C00KA0648G00
====================================
```

### 2. Aggiungere a Matter Ecosystem

Usa questi codici nell'app del tuo ecosistema Matter:
- **Google Home**: "Aggiungi dispositivo" → "Matter"
- **Apple Home**: "Aggiungi accessorio" → Scansiona QR code
- **Amazon Alexa**: "Dispositivi" → "+" → "Matter"

### 3. Thread Border Router

**IMPORTANTE**: È necessario un **Thread Border Router** attivo nella rete per il commissioning. Opzioni:
- Google Nest Hub (2nd gen o successivo)
- Apple HomePod mini
- Thread Border Router basato su Raspberry Pi

### 4. Verifica Stato Thread

Dopo il commissioning, verifica lo stato Thread nel log:

```
====================================
   THREAD NETWORK STATUS
====================================
Thread Provisioned: YES
Thread Enabled: YES
Thread Attached: YES
====================================
```

### 5. Dispositivi Visibili nell'App Matter

Dopo il commissioning, vedrai **9 dispositivi** totali:

**Input (Contact Sensors):**
1. Input 1 (GPIO 0)
2. Input 2 (GPIO 1)
3. Input 3 (GPIO 2)
4. Input 4 (GPIO 21)

**Output (On/Off Lights):**
5. Output 1 (GPIO 22)
6. Output 2 (GPIO 23)
7. Output 3 (GPIO 19)
8. Output 4 (GPIO 20)

**Controllo Antenna (Virtual Switch):**
9. **Antenna Control** (ON = Esterna, OFF = Interna)

## Funzionamento

### Monitoraggio Ingressi (Contact Sensors)

Gli ingressi vengono monitorati ogni **50ms** con debounce automatico. Quando uno stato cambia:

```
I (12345) app_main: Input 1 (GPIO0) changed to OPEN
I (12346) app_main: Input 2 (GPIO1) changed to CLOSED
```

Lo stato viene automaticamente sincronizzato via Matter usando il cluster **BooleanState** (StateValue attribute).

### Controllo Uscite (On/Off Lights)

Le uscite sono controllate via Matter usando il cluster **OnOff**. Quando un comando arriva:

```
I (23456) app_main: Output 1 (GPIO22) set to ON
I (23457) app_main: Output 2 (GPIO23) set to OFF
```

Il cambio di stato è **immediato** e sincronizzato con tutti i controller Matter.

### Controllo Antenna RF

L'antenna può essere controllata in **3 modi**:

#### 1. Via Matter (Remoto)

Dall'app Matter (Google Home, Apple Home, ecc.):
- **Accendi** "Antenna Control" → Switch su antenna **ESTERNA** (UFL)
- **Spegni** "Antenna Control" → Switch su antenna **INTERNA** (ceramica)

Log:
```
I (34567) app_main: Switched to EXTERNAL antenna (UFL)
```

#### 2. Via Codice (Compile-Time)

Modifica `USE_EXTERNAL_ANTENNA` in `main/app_main.cpp` e ricompila:
```cpp
#define USE_EXTERNAL_ANTENNA  1  // 1 = esterna
```

#### 3. Via Funzione (Runtime)

Chiama `switch_antenna()` nel codice:
```cpp
switch_antenna(true);   // Antenna esterna
switch_antenna(false);  // Antenna interna
```

**Automazioni Possibili**:
- "Quando esco di casa → Attiva antenna esterna"
- "Quando rientro → Attiva antenna interna"
- "Di notte → Antenna interna (risparmio energetico)"

### LED di Stato Thread

Il LED su GPIO15 fornisce feedback visivo immediato sullo stato della rete Thread:

#### Interpretazione Pattern LED

**LED Spento (OFF)**:
- Dispositivo non connesso alla rete Thread
- Stati: `DISABLED` o `DETACHED`
- **Azione**: Verifica il Thread Border Router e il commissioning

**LED Acceso Fisso (Solid ON)**:
- Dispositivo connesso come **End Device (Child)**
- Non esegue routing di pacchetti
- Consuma meno energia rispetto a Router/Leader
- **Tipico**: Dispositivi battery-powered o MTD (Minimal Thread Device)

**LED Lampeggio Singolo (1x al secondo)**:
- Dispositivo in modalità **Router**
- Pattern: 1000ms acceso + 250ms spento
- Esegue routing per altri dispositivi
- Mantiene la radio Thread sempre attiva
- **Tipico**: Dispositivi alimentati dalla rete in mesh grande

**LED Lampeggio Doppio (2x al secondo)**:
- Dispositivo in modalità **Leader**
- Pattern: doppio flash veloce + pausa
- Coordina la rete Thread (solo 1 Leader per rete)
- Include tutte le funzioni di Router + gestione rete
- **Tipico**: Dispositivo selezionato automaticamente dalla rete

#### Cambio di Ruolo Dinamico

Il ruolo Thread può cambiare automaticamente durante il funzionamento:
- **Router → Leader**: Se il Leader corrente diventa irraggiungibile
- **Leader → Router**: Se un dispositivo con priorità maggiore si unisce
- **Child → Router**: Se il dispositivo viene promosso dalla rete

Il LED si aggiorna entro **1 secondo** dal cambio di ruolo, fornendo feedback immediato.

#### Log di Cambio Ruolo

Ogni cambio di ruolo viene registrato sulla seriale:
```
I (12345) app_main: Thread role changed: ROUTER
I (23456) app_main: Thread role changed: LEADER (Router)
```

## Factory Reset

Per resettare il dispositivo ai valori di fabbrica:

1. Tieni premuto il **pulsante BOOT (GPIO9)** per **5 secondi**
2. Il dispositivo si resetterà e cancellerà le credenziali Matter e Thread
3. Dovrai rifare il commissioning

## Struttura del Progetto

```
c6_matter_thread_6in_6out/
├── main/
│   ├── app_main.cpp              # Logica principale (GPIO, Matter endpoints, antenna)
│   ├── app_reset.cpp             # Gestione factory reset
│   ├── include/
│   │   ├── app_priv.h            # Header privato
│   │   ├── app_reset.h           # Header reset
│   │   └── CHIPProjectConfig.h   # Configurazione CHIP
│   └── CMakeLists.txt            # Build configuration componente
├── CMakeLists.txt                # Build configuration progetto
├── partitions.csv                # Tabella partizioni flash
├── sdkconfig.defaults            # Configurazione default ESP-IDF
└── README.md                     # Questo file
```

## Troubleshooting

### Device non si commissiona

**Sintomi**: Il QR code o manual code non funzionano nell'app Matter.

**Soluzioni**:
1. Verifica che il Thread Border Router sia attivo e raggiungibile
2. Controlla che il BLE sia abilitato sul controller (smartphone/tablet)
3. Prova un factory reset del dispositivo
4. Verifica nel log che BLE sia attivo: `I (xxx) app_main: CHIPoBLE connection established`

### Ingressi mostrano stato invertito

**Sintomi**: Contatto aperto mostra "closed", contatto chiuso mostra "open".

**Causa**: Logica di inversione disabilitata o errata.

**Soluzione**: Verifica in `main/app_main.cpp` nella funzione `gpio_input_task()` che ci sia:
```cpp
inverted_state = !current_state;
```

### Uscite non rispondono ai comandi Matter

**Sintomi**: Comandi dall'app non cambiano lo stato delle uscite.

**Soluzioni**:
1. Verifica i log: `I (xxx) app_main: Output X (GPIOX) set to ON/OFF`
2. Controlla le connessioni hardware delle uscite
3. Verifica che l'endpoint ID sia corretto nei log di startup
4. Controlla che il callback `app_attribute_update_cb` venga chiamato

### Antenna non switcha

**Sintomi**: Cambiando lo switch "Antenna Control" nell'app, l'antenna non cambia.

**Soluzioni**:
1. Verifica nel log che appaia: `I (xxx) app_main: Switched to EXTERNAL/INTERNAL antenna`
2. Controlla che GPIO3 e GPIO14 siano configurabili (non usati da altre periferiche)
3. Prova a cambiare manualmente con `gpio_set_level(GPIO_NUM_14, 1)` per testare l'hardware
4. Se usi GPIO diversi da 14, verifica che `GPIO_WIFI_ANT_CONFIG` sia impostato correttamente

**Nota**: Se GPIO14 non è accessibile sul tuo XIAO, modifica `GPIO_WIFI_ANT_CONFIG` con il GPIO corretto (es. GPIO18, GPIO15, ecc.)

### Range antenna non migliora con antenna esterna

**Sintomi**: Anche con antenna esterna, il range è uguale.

**Soluzioni**:
1. Verifica che l'antenna esterna sia **effettivamente collegata** al connettore UFL
2. Controlla nel log che appaia: `I (xxx) app_main: Switched to EXTERNAL antenna (UFL)`
3. Misura il voltage su GPIO14: deve essere **~3.3V** per antenna esterna, **0V** per interna
4. Testa con un'antenna UFL di qualità nota (guadagno 2-5 dBi)

### Vendor/Product Name ancora di default

**Sintomi**: Nell'app Matter il dispositivo appare come "TEST_VENDOR" / "TEST_PRODUCT".

**Causa**: I nomi vengono letti da NVS prima della scrittura personalizzata, oppure sono già in cache nel controller.

**Soluzione**:
1. Verifica che il codice di scrittura NVS sia presente in `app_main.cpp`
2. Fai un **factory reset** del dispositivo per cancellare i vecchi valori NVS
3. **Rimuovi il dispositivo dall'app Matter** (importante!)
4. Reflash il firmware
5. **Ricommissiona** il dispositivo (il controller leggerà i nuovi valori)

### Errori di compilazione dopo modifica sdkconfig.defaults

**Sintomi**: Errori strani durante `idf.py build`.

**Soluzione**:
```bash
# Pulisci completamente
idf.py fullclean

# Riconfigura
idf.py set-target esp32c6
idf.py reconfigure

# Ricompila
idf.py build
```

### Thread Provisioned: NO

**Sintomi**: Il dispositivo non si connette alla rete Thread.

**Soluzioni**:
1. Verifica che il Thread Border Router sia nella stessa rete WiFi del controller
2. Riprova il commissioning tramite l'app Matter
3. Controlla i log OpenThread: cerca messaggi `chip[DL]: Thread connectivity changed`

### LED di Stato non Lampeggia

**Sintomi**: Il LED su GPIO15 rimane spento anche dopo il commissioning.

**Soluzioni**:
1. Verifica che il dispositivo sia connesso alla rete Thread (controlla log: `Thread Attached: YES`)
2. Controlla che GPIO15 sia configurato correttamente: `gpio_set_level(GPIO_STATUS_LED, ...)`
3. Verifica che il task LED sia stato avviato: cerca nel log `I (xxx) app_main: Thread status LED task started on GPIO15`
4. Testa manualmente il LED: aggiungi `gpio_set_level(GPIO_NUM_15, 1)` temporaneamente per verificare l'hardware
5. Se usi un LED esterno su GPIO diverso, verifica che `GPIO_STATUS_LED` sia impostato correttamente

### LED Lampeggia in Modo Diverso dal Previsto

**Sintomi**: Il pattern del LED non corrisponde alla documentazione.

**Possibili Cause**:
1. **Timing modificato nel codice**: Verifica `thread_status_led_task()` in `main/app_main.cpp` per i timing esatti
2. **Ruolo Thread non stabile**: Il dispositivo potrebbe cambiare ruolo frequentemente (controllare log)
3. **OpenThread instance non disponibile**: Verifica che `esp_openthread_get_instance()` ritorni un'istanza valida

**Debug**:
```
# Monitor log per vedere ruolo corrente
idf.py -p /dev/ttyACM0 monitor | grep "Thread role changed"
```

### LED Rimane Acceso Fisso ma Dovrebbe Essere Router

**Sintomi**: LED acceso fisso (Child) ma ti aspetti che sia Router.

**Spiegazione**:
Il ruolo Thread è assegnato **automaticamente dalla rete** in base a:
- Numero di dispositivi nella rete
- Capacità del dispositivo (FTD vs MTD)
- Configurazione OpenThread

**Verifica**:
1. Controlla che il dispositivo sia configurato come **FTD** (Full Thread Device)
2. Nel log, verifica: `I (xxx) app_main: Thread role changed: CHILD` o `ROUTER`
3. Una rete Thread piccola (2-3 dispositivi) potrebbe non richiedere Router aggiuntivi
4. Il Leader determina automaticamente quanti Router servono

**Se Vuoi Forzare Ruolo Router**:
Questo NON è raccomandato (la rete decide automaticamente), ma per testing puoi verificare la configurazione FTD in `sdkconfig.defaults`:
```ini
CONFIG_OPENTHREAD_FTD=y
CONFIG_OPENTHREAD_MTD=n
```

## Ottimizzazioni ESP32-C6

Questo progetto sfrutta le caratteristiche native dell'ESP32-C6 XIAO:

- **Radio Thread 802.15.4 nativa**: nessun dongle esterno necessario
- **Hardware crypto acceleration**: AES, SHA, ECC accelerati via hardware
- **NimBLE stack**: stack Bluetooth ottimizzato per low memory
- **Partizioni ottimizzate**: layout flash per 4MB
- **FreeRTOS 1000 Hz**: polling GPIO responsive
- **RF switch integrato**: selezione antenna interna/esterna senza componenti esterni
- **LED di stato Thread**: monitoring visivo real-time del ruolo nella mesh

## Funzionalità Avanzate

### Automazioni Matter con Antenna

Esempi di automazioni che puoi creare:

**Scenario 1: Range Esteso di Notte**
```
Quando: Ore 22:00
Azione: Attiva antenna esterna
Motivo: Migliore copertura per automazioni notturne
```

**Scenario 2: Risparmio Energetico**
```
Quando: Presenza rilevata in casa
Azione: Attiva antenna interna
Motivo: Sufficiente per casa, minor consumo
```

**Scenario 3: Integrazione con Altri Sensori**
```
Quando: Temperatura esterna < 0°C
Azione: Attiva antenna interna
Motivo: Evitare condensazione su antenna esterna
```

### Monitoraggio Stato Antenna

Per sapere quale antenna è attiva, controlla lo stato dell'endpoint "Antenna Control" nell'app Matter:
- **ON** (acceso) = Antenna Esterna attiva
- **OFF** (spento) = Antenna Interna attiva

### Integrazione con Home Assistant

Se usi Home Assistant con Matter:

```yaml
automation:
  - alias: "Switch Antenna Based on Presence"
    trigger:
      - platform: state
        entity_id: binary_sensor.presence_home
    action:
      - service: light.turn_{{ 'off' if trigger.to_state.state == 'home' else 'on' }}
        entity_id: light.antenna_control
```

## Riferimenti

- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [Matter Specification](https://csa-iot.org/developer-resource/specifications-download-request/)
- [Thread Group](https://www.threadgroup.org/)
- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [XIAO ESP32C6 Wiki](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)

## Licenza

Questo progetto utilizza:
- **ESP-IDF**: Apache 2.0
- **ESP-Matter**: Apache 2.0
- **Matter SDK (ConnectedHomeIP)**: Apache 2.0

## Supporto

Per issue e domande:
- ESP-IDF: [GitHub Issues](https://github.com/espressif/esp-idf/issues)
- ESP-Matter: [GitHub Issues](https://github.com/espressif/esp-matter/issues)
- XIAO ESP32C6: [Seeed Studio Forum](https://forum.seeedstudio.com/)

---

**Versione**: 1.2
**Ultima modifica**: 2025
**Autore**: VicinoDiCasaDigitale

---

## Changelog

### v1.2 (2025)
- Aggiunto LED di stato Thread su GPIO15 (USER LED)
- Pattern LED differenziati per Child/Router/Leader
- Monitoring real-time del ruolo Thread con aggiornamento < 1 secondo
- Log automatico dei cambi di ruolo Thread

### v1.1 (2025)
- Aggiunto controllo antenna RF (interna/esterna) via Matter
- Endpoint virtuale per switch antenna remoto
- Supporto antenna esterna UFL

### v1.0 (2025)
- Release iniziale
- 4 contact sensors + 4 on/off lights
- Matter over Thread su ESP32-C6
