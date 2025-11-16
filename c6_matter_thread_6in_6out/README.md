# ESP32-C6 Matter over Thread - 4 Inputs + 4 Outputs

Dispositivo Matter over Thread per **ESP32-C6** con **4 sensori di contatto** (ingressi) e **4 luci On/Off** (uscite) controllabili indipendentemente.

## Informazioni Dispositivo

- **Vendor**: VicinoDiCasaDigitale
- **Product**: Matter Thread 6in/6out
- **Chip**: ESP32-C6 (con radio Thread 802.15.4 nativa)
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

Modifica il file `main/app_main.cpp` alle **righe 189-190**:

```cpp
const char *vendor_name = "VicinoDiCasaDigitale";  // <- Cambia qui
const char *product_name = "Matter Thread 6in/6out"; // <- Cambia qui
```

**IMPORTANTE**: Questi valori vengono scritti in NVS **prima** dell'avvio di Matter. Se cambi i nomi dopo aver già fatto commissioning, potrebbe essere necessario fare un factory reset.

### Cambiare GPIO degli Ingressi

Modifica il file `main/app_main.cpp` alle **righe 37-40**:

```cpp
#define GPIO_INPUT_0  GPIO_NUM_0   // Input 1 <- Cambia qui
#define GPIO_INPUT_1  GPIO_NUM_1   // Input 2 <- Cambia qui
#define GPIO_INPUT_2  GPIO_NUM_2   // Input 3 <- Cambia qui
#define GPIO_INPUT_3  GPIO_NUM_21  // Input 4 <- Cambia qui
```

### Cambiare GPIO delle Uscite

Modifica il file `main/app_main.cpp` alle **righe 43-46**:

```cpp
#define GPIO_OUTPUT_0 GPIO_NUM_22  // Output 1 <- Cambia qui
#define GPIO_OUTPUT_1 GPIO_NUM_23  // Output 2 <- Cambia qui
#define GPIO_OUTPUT_2 GPIO_NUM_19  // Output 3 <- Cambia qui
#define GPIO_OUTPUT_3 GPIO_NUM_20  // Output 4 <- Cambia qui
```

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
   // Linee 54-57
   static uint16_t output_endpoint_ids[6] = {0, 0, 0, 0, 0, 0};  // Cambia 4 in 6
   static uint16_t input_endpoint_ids[6] = {0, 0, 0, 0, 0, 0};   // Cambia 4 in 6

   // Linee 60-61
   static const gpio_num_t input_pins[6] = {...};   // Aggiungi 2 GPIO
   static const gpio_num_t output_pins[6] = {...};  // Aggiungi 2 GPIO
   ```

2. **Modifica i loop** che iterano su ingressi/uscite:
   - `app_attribute_update_cb()` linea 107: `for (int i = 0; i < 6; i++)`
   - `gpio_input_task()` linea 139: `for (int i = 0; i < 6; i++)`
   - Creazione endpoint ingressi linea 242: `for (int i = 0; i < 6; i++)`
   - Creazione endpoint uscite linea 259: `for (int i = 0; i < 6; i++)`
   - Inizializzazione uscite linea 212: `for (int i = 0; i < 6; i++)`

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

## Factory Reset

Per resettare il dispositivo ai valori di fabbrica:

1. Tieni premuto il **pulsante BOOT (GPIO9)** per **5 secondi**
2. Il dispositivo si resetterà e cancellerà le credenziali Matter e Thread
3. Dovrai rifare il commissioning

## Struttura del Progetto

```
c6_matter_thread_6in_6out/
├── main/
│   ├── app_main.cpp              # Logica principale (GPIO, Matter endpoints)
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

**Soluzione**: Verifica in `main/app_main.cpp` linea 145 che ci sia:
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

### Vendor/Product Name ancora di default

**Sintomi**: Nell'app Matter il dispositivo appare come "TEST_VENDOR" / "TEST_PRODUCT".

**Causa**: I nomi vengono letti da NVS prima della scrittura personalizzata.

**Soluzione**:
1. Verifica che il codice di scrittura NVS (linee 188-197 in `app_main.cpp`) sia presente
2. Fai un **factory reset** per cancellare i vecchi valori NVS
3. Reflash il firmware
4. Ricommissiona il dispositivo

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

## Ottimizzazioni ESP32-C6

Questo progetto sfrutta le caratteristiche native dell'ESP32-C6:

- **Radio Thread 802.15.4 nativa**: nessun dongle esterno necessario
- **Hardware crypto acceleration**: AES, SHA, ECC accelerati via hardware
- **NimBLE stack**: stack Bluetooth ottimizzato per low memory
- **Partizioni ottimizzate**: layout flash per 4MB
- **FreeRTOS 1000 Hz**: polling GPIO responsive

## Riferimenti

- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [Matter Specification](https://csa-iot.org/developer-resource/specifications-download-request/)
- [Thread Group](https://www.threadgroup.org/)
- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)

## Licenza

Questo progetto utilizza:
- **ESP-IDF**: Apache 2.0
- **ESP-Matter**: Apache 2.0
- **Matter SDK (ConnectedHomeIP)**: Apache 2.0

## Supporto

Per issue e domande:
- ESP-IDF: [GitHub Issues](https://github.com/espressif/esp-idf/issues)
- ESP-Matter: [GitHub Issues](https://github.com/espressif/esp-matter/issues)
