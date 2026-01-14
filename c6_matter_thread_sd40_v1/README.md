# ESP32-C6 Matter over Thread - 4 Inputs + 2 Outputs + SCD40 Sensor

Dispositivo Matter over Thread per **ESP32-C6** (XIAO) con **4 sensori di contatto** (ingressi), **2 luci On/Off** (uscite), **sensore SCD40 CO2/Temperatura/Umidità**, **controllo antenna RF** (interna/esterna) e **LED di stato Thread** - tutto controllabile indipendentemente via Matter.

## Informazioni Dispositivo

- **Vendor**: VicinoDiCasaDigitale
- **Product**: Matter Thread 4in/2out+SCD40
- **Chip**: ESP32-C6 (XIAO - con radio Thread 802.15.4 nativa)
- **Protocollo**: Matter over Thread (Thread FTD - Full Thread Device)
- **Commissioning**: BLE (Bluetooth Low Energy)
- **Sensore Ambientale**: Sensirion SCD40 (CO2, Temperatura, Umidità)

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

2 uscite digitali indipendenti controllabili via Matter:

| Output    | GPIO     | Funzione Matter | Stato Iniziale |
|-----------|----------|-----------------|----------------|
| Output 1  | GPIO 19  | On/Off Light 1  | OFF (LOW)      |
| Output 2  | GPIO 20  | On/Off Light 2  | OFF (LOW)      |

**Nota**: Le uscite sono attive HIGH (1 = ON, 0 = OFF).

### I2C - Sensore SCD40 (Sensirion CO2/Temperatura/Umidità)

Il progetto include il sensore ambientale **Sensirion SCD40** collegato via I2C:

| Segnale | GPIO | Funzione |
|---------|------|----------|
| SDA | GPIO 22 | I2C Data |
| SCL | GPIO 23 | I2C Clock |

**Indirizzo I2C**: `0x62` (fisso hardware)

**Misurazioni disponibili**:
- **CO2**: 0-40'000 ppm (accuratezza ±50 ppm + 5% per SCD40)
- **Temperatura**: -10°C a +60°C (accuratezza ±0.8°C)
- **Umidità Relativa**: 0-100% RH (accuratezza ±6% RH)

**Modalità operative**:
1. **Periodic Measurement** (default): letture ogni 5 secondi
2. **Low Power Periodic**: letture ogni 30 secondi (riduce consumo)
3. **Single Shot** (solo SCD41): misurazioni on-demand

**Endpoint Matter creati dal sensore**:
1. **Air Quality Sensor** - Qualità dell'aria (enum calcolata da CO2)
   - Good (< 800 ppm)
   - Fair (800-1000 ppm)
   - Moderate (1000-1500 ppm)
   - Poor (1500-2000 ppm)
   - Very Poor (2000-5000 ppm)
   - Extremely Poor (> 5000 ppm)
2. **CO2 Concentration** - Valore CO2 in PPM (0-5000)
   - ✅ Utilizza il cluster **Carbon Dioxide Concentration Measurement (0x040D)** standard Matter
   - ⚠️ **COMPATIBILITÀ OPENHAB**: Richiede OpenHAB 5.2+ (PR #19897 merged 27/12/2025)
   - Su OpenHAB 5.1: Il canale mostrerà NULL (non supportato)
   - Su OpenHAB 5.2+: Funzionerà automaticamente senza riflash
   - **Fallback**: Usa Air Quality enum (endpoint 7) su versioni precedenti
   - Endpoint separato dall'Air Quality per massima compatibilità
3. **Temperature Sensor** - Temperatura in °C × 100
4. **Humidity Sensor** - Umidità in %RH × 100

### Controllo Antenna RF (XIAO ESP32C6)

Il dispositivo include il controllo dello switch RF per selezionare tra antenna interna ed esterna:

| GPIO | Funzione | Valore | Effetto |
|------|----------|--------|---------|
| GPIO 3 | RF Switch Enable | LOW | Abilita il controllo dello switch RF |
| GPIO 14 | Antenna Selection | LOW | Antenna ceramica interna (default) |
| GPIO 14 | Antenna Selection | HIGH | Antenna esterna (connettore UFL) |

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

## Comandi SCD40 Implementati

Il firmware include **tutti i 20 comandi I2C** del sensore SCD40 secondo il datasheet ufficiale Sensirion (Version 1 - January 2021).

### Comandi Base

1. **start_periodic_measurement** - Avvia misurazioni periodiche (5s)
2. **read_measurement** - Legge CO2, temperatura, umidità
3. **stop_periodic_measurement** - Ferma misurazioni periodiche

### Compensazione Segnale On-Chip

4. **set_temperature_offset(float °C)** - Imposta offset temperatura per accuratezza RH/T
5. **get_temperature_offset(float *°C)** - Legge offset temperatura configurato
6. **set_sensor_altitude(uint16_t meters)** - Imposta altitudine per compensazione pressione
7. **get_sensor_altitude(uint16_t *meters)** - Legge altitudine configurata
8. **set_ambient_pressure(uint32_t Pascal)** - Imposta pressione ambiente (override altitudine)

### Calibrazione sul Campo

9. **perform_forced_recalibration(uint16_t target_ppm, int16_t *correction)** - Calibrazione forzata con riferimento noto
10. **set_automatic_self_calibration_enabled(bool enabled)** - Abilita/disabilita ASC
11. **get_automatic_self_calibration_enabled(bool *enabled)** - Legge stato ASC

### Modalità Basso Consumo

12. **start_low_power_periodic_measurement()** - Avvia misurazioni a 30s (basso consumo)
13. **get_data_ready_status(bool *ready)** - Controlla se i dati sono pronti

### Funzioni Avanzate

14. **persist_settings()** - Salva configurazioni in EEPROM (max 2000 scritture)
15. **get_serial_number(uint64_t *serial)** - Legge numero seriale 48-bit del sensore
16. **perform_self_test(bool *passed)** - Esegue autodiagnosi (~5.5 secondi)
17. **perform_factory_reset()** - Reset di fabbrica (cancella tutte le impostazioni)
18. **reinit()** - Reinizializza sensore ricaricando impostazioni da EEPROM

### Single Shot (Solo SCD41)

19. **measure_single_shot()** - Misura on-demand CO2/T/RH (~1.35 secondi)
20. **measure_single_shot_rht_only()** - Misura solo T/RH (~50 ms)

## Come Utilizzare i Comandi SCD40

### Metodo 1: Nel Codice Firmware (Raccomandato)

Puoi chiamare i comandi direttamente nel codice. Esempio in `app_main.cpp`:

```cpp
// Esempio: Configurare il sensore all'avvio
void configure_scd40_sensor(void)
{
    // 1. Fermare misurazioni periodiche
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Impostare offset temperatura (se il sensore si scalda)
    scd40_set_temperature_offset(4.5f);  // Offset di 4.5°C

    // 3. Impostare altitudine (se non a livello del mare)
    scd40_set_sensor_altitude(350);  // 350 metri s.l.m.

    // 4. Abilitare calibrazione automatica (default: già abilitata)
    scd40_set_automatic_self_calibration_enabled(true);

    // 5. Salvare configurazioni in EEPROM (opzionale)
    scd40_persist_settings();

    // 6. Riavviare misurazioni
    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

    ESP_LOGI(TAG, "SCD40 configured and ready");
}
```

**Dove aggiungere il codice**: In `app_main.cpp`, dopo `scd40_init()` nella funzione `app_main()`.

### Metodo 2: Tramite Task Periodico

Crea un task che esegue operazioni periodiche (es. calibrazione settimanale):

```cpp
void scd40_maintenance_task(void *arg)
{
    while (1) {
        // Attendi 7 giorni
        vTaskDelay(pdMS_TO_TICKS(7 * 24 * 60 * 60 * 1000));

        // Esegui calibrazione forzata (se esposto ad aria esterna)
        int16_t correction;
        esp_err_t err = scd40_perform_forced_recalibration(400, &correction);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Weekly calibration completed. Correction: %d ppm", correction);
        }
    }
}

// Avviare il task in app_main():
xTaskCreate(scd40_maintenance_task, "scd40_maint", 4096, NULL, 4, NULL);
```

### Metodo 3: Tramite Trigger da Ingressi Matter

Puoi utilizzare i sensori di contatto per trigger comandi SCD40:

```cpp
// Esempio: Input 4 trigger calibrazione quando chiuso
if (endpoint_id == input_endpoint_ids[3] && inverted_state == false) {
    ESP_LOGI(TAG, "Input 4 closed - triggering SCD40 forced recalibration");

    // Ferma misurazioni
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Calibra (assumendo aria esterna = 400 ppm)
    int16_t correction;
    scd40_perform_forced_recalibration(400, &correction);

    // Riavvia misurazioni
    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
}
```

### Metodo 4: Test e Debug Interattivo

Per testare i comandi durante lo sviluppo:

```cpp
// Aggiungere temporaneamente in scd40_init():
void scd40_init_with_test(void)
{
    ESP_LOGI(TAG, "=== SCD40 DIAGNOSTIC MODE ===");

    // Test 1: Leggi numero seriale
    uint64_t serial;
    if (scd40_get_serial_number(&serial) == ESP_OK) {
        ESP_LOGI(TAG, "Serial: 0x%012llX", serial);
    }

    // Test 2: Leggi configurazione attuale
    float temp_offset;
    if (scd40_get_temperature_offset(&temp_offset) == ESP_OK) {
        ESP_LOGI(TAG, "Temperature offset: %.2f°C", temp_offset);
    }

    uint16_t altitude;
    if (scd40_get_sensor_altitude(&altitude) == ESP_OK) {
        ESP_LOGI(TAG, "Sensor altitude: %u m", altitude);
    }

    bool asc_enabled;
    if (scd40_get_automatic_self_calibration_enabled(&asc_enabled) == ESP_OK) {
        ESP_LOGI(TAG, "ASC: %s", asc_enabled ? "ENABLED" : "DISABLED");
    }

    // Test 3: Self-test
    bool test_passed;
    if (scd40_perform_self_test(&test_passed) == ESP_OK) {
        ESP_LOGI(TAG, "Self-test: %s", test_passed ? "PASSED" : "FAILED");
    }

    ESP_LOGI(TAG, "=== END DIAGNOSTIC ===");
}
```

### Esempi Pratici di Utilizzo

#### Esempio 1: Compensazione Altitudine per Accuratezza CO2

```cpp
// Per un dispositivo installato a 1200 metri s.l.m. a Milano
void setup_altitude_compensation(void)
{
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    scd40_set_sensor_altitude(1200);  // 1200 metri
    scd40_persist_settings();  // Salva in EEPROM

    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
}
```

#### Esempio 2: Calibrazione Manuale Mensile

```cpp
// Da eseguire portando il sensore all'aria aperta per 3+ minuti
void monthly_calibration(void)
{
    ESP_LOGI(TAG, "Starting monthly calibration - sensor must be in fresh air!");

    // Assicurati che il sensore sia in aria esterna per almeno 3 minuti
    vTaskDelay(pdMS_TO_TICKS(3 * 60 * 1000));

    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    int16_t correction;
    esp_err_t err = scd40_perform_forced_recalibration(400, &correction);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration successful! Correction: %d ppm", correction);
        scd40_persist_settings();
    } else {
        ESP_LOGE(TAG, "Calibration FAILED!");
    }

    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);
}
```

#### Esempio 3: Modalità Basso Consumo

```cpp
// Switchare a modalità low power (utile per batteria)
void enable_low_power_mode(void)
{
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Passa a 30 secondi invece di 5 secondi
    scd40_start_low_power_periodic_measurement();

    ESP_LOGI(TAG, "SCD40 switched to low power mode (30s interval)");
}
```

#### Esempio 4: Controllo Data Ready

```cpp
// Polling intelligente per evitare letture vuote
void smart_read_scd40(void)
{
    bool data_ready = false;
    scd40_get_data_ready_status(&data_ready);

    if (data_ready) {
        uint16_t co2;
        float temperature, humidity;
        scd40_read_measurement(&co2, &temperature, &humidity);
        ESP_LOGI(TAG, "CO2: %u ppm, T: %.1f°C, RH: %.1f%%", co2, temperature, humidity);
    } else {
        ESP_LOGD(TAG, "Data not ready yet, skipping read");
    }
}
```

## Sequenza di Configurazione Raccomandata

Per configurare il sensore SCD40 all'avvio del dispositivo, segui questa sequenza:

```cpp
// In app_main(), dopo scd40_init()
void configure_scd40_for_production(void)
{
    ESP_LOGI(TAG, "Configuring SCD40 for optimal performance...");

    // 1. Ferma misurazioni
    scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Configura offset temperatura (misura la differenza tra SCD40 e termometro di riferimento)
    //    Default è 4°C. Modifica se necessario.
    scd40_set_temperature_offset(4.0f);

    // 3. Configura altitudine (in metri s.l.m.)
    scd40_set_sensor_altitude(0);  // Livello del mare (modifica se necessario)

    // 4. Abilita auto-calibrazione (raccomandato se esposto ad aria fresca settimanalmente)
    scd40_set_automatic_self_calibration_enabled(true);

    // 5. Salva configurazioni in EEPROM (opzionale ma raccomandato)
    scd40_persist_settings();

    // 6. Riavvia misurazioni periodiche
    scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

    ESP_LOGI(TAG, "SCD40 configuration complete");
}
```

## Note Importanti sull'Uso dei Comandi

### ⚠️ Sequenza Corretta per Comandi di Configurazione

**IMPORTANTE**: Alcuni comandi richiedono che il sensore sia in **idle mode** (misurazioni fermate):

```cpp
// CORRETTO ✓
scd40_send_command(SCD40_CMD_STOP_PERIODIC_MEASUREMENT);
vTaskDelay(pdMS_TO_TICKS(500));  // Attendi 500ms
// Ora puoi eseguire comandi di configurazione
scd40_set_temperature_offset(5.0f);
scd40_set_sensor_altitude(1000);
// ...
scd40_send_command(SCD40_CMD_START_PERIODIC_MEASUREMENT);

// ERRATO ✗
scd40_set_temperature_offset(5.0f);  // ERRORE: sensore in misurazione!
```

**Comandi che richiedono idle mode**:
- `set_temperature_offset`
- `get_temperature_offset`
- `set_sensor_altitude`
- `get_sensor_altitude`
- `perform_forced_recalibration`
- `set_automatic_self_calibration_enabled`
- `get_automatic_self_calibration_enabled`
- `persist_settings`
- `get_serial_number`
- `perform_self_test`
- `perform_factory_reset`
- `reinit`

**Comandi che funzionano durante misurazioni**:
- `read_measurement`
- `set_ambient_pressure` (unico che può essere chiamato durante periodic measurement)
- `get_data_ready_status`

### ⚠️ Limite Scritture EEPROM

`persist_settings()` scrive su EEPROM con **limite di 2000 cicli**:

```cpp
// NON FARE ✗
while (1) {
    scd40_persist_settings();  // ERRORE: troppi cicli!
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// CORRETTO ✓
// Chiama persist_settings() solo quando cambi configurazioni permanenti
scd40_set_temperature_offset(4.5f);
scd40_persist_settings();  // OK: chiamato una volta
```

### ⚠️ Timing dei Comandi

Rispetta i tempi di esecuzione indicati nel datasheet:

| Comando | Tempo Esecuzione |
|---------|------------------|
| `stop_periodic_measurement` | 500 ms |
| `perform_forced_recalibration` | 400 ms |
| `persist_settings` | 800 ms |
| `perform_self_test` | 5500 ms |
| `perform_factory_reset` | 1200 ms |
| `reinit` | 20 ms |
| `measure_single_shot` | 1350 ms |
| `measure_single_shot_rht_only` | 50 ms |
| Altri comandi (read/write) | 1 ms |

## Configurazione Endpoint Matter

Il firmware è configurato per supportare **fino a 20 endpoint dinamici** tramite la configurazione:

```
CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=20
```

Questa impostazione in `sdkconfig.defaults` permette la creazione di tutti i 17 endpoint attualmente utilizzati (0-16 + endpoint 17 per temperature offset), lasciando margine per future espansioni.

**Importante**: Se modifichi il numero di endpoint nel firmware, assicurati che `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` sia sufficientemente alto, altrimenti la creazione degli endpoint fallirà con errore `Dynamic endpoint count cannot be greater than CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT`.

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
cd c6_matter_thread_sd40_v1
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
I (1811) app_main:    COMMISSIONING INFORMATION
I (1811) app_main: ====================================
I (1811) chip[SVR]: SetupQRCode: [MT:-24J042C00KA0648G00]
I (1811) chip[SVR]: Copy/paste the below URL in a browser to see the QR Code:
I (1812) chip[SVR]: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A-24J042C00KA0648G00
I (1812) chip[SVR]: Manual pairing code: [34970112332]
I (1812) app_main: ====================================
```

### 2. Aggiungere a Matter Ecosystem

Usa questi codici nell'app del tuo ecosistema Matter:
- **Google Home**: "Aggiungi dispositivo" → "Matter"
- **Apple Home**: "Aggiungi accessorio" → Scansiona QR code
- **Amazon Alexa**: "Dispositivi" → "+" → "Matter"
- **Home Assistant**: Matter integration → "Add device"

### 3. Thread Border Router

**IMPORTANTE**: È necessario un **Thread Border Router** attivo nella rete per il commissioning. Opzioni:
- Google Nest Hub (2nd gen o successivo)
- Apple HomePod mini
- Thread Border Router basato su Raspberry Pi
- OpenThread Border Router (OTBR)

### 4. Dispositivi Visibili nell'App Matter

Dopo il commissioning, vedrai **17 endpoint** totali:

**Input (Contact Sensors):**
1. Input 1 (GPIO 0)
2. Input 2 (GPIO 1)
3. Input 3 (GPIO 2)
4. Input 4 (GPIO 21)

**Output (On/Off Lights):**
5. Output 1 (GPIO 19)
6. Output 2 (GPIO 20)

**Sensori Ambientali (SCD40):**
7. **Air Quality Sensor** - Qualità aria (enum 0-6 basato su CO2 ppm)
8. **CO2 Sensor** - Concentrazione CO2 in ppm (cluster 0x040D - richiede OpenHAB 5.2+)
9. **Temperature Sensor** - Temperatura in °C
10. **Humidity Sensor** - Umidità relativa in %RH

**Controlli SCD40 (Virtual Switches per OpenHAB/Matter):**
11. **SCD40 Calibrate** - Switch momentaneo per calibrazione forzata (400 ppm)
12. **SCD40 ASC Enable** - Switch persistente per abilitare/disabilitare auto-calibrazione
13. **SCD40 Low Power** - Switch persistente per modalità basso consumo (30s invece di 5s)
14. **SCD40 Persist** - Switch momentaneo per salvare impostazioni in EEPROM
15. **SCD40 Self Test** - Switch momentaneo per eseguire autodiagnosi (~5.5s)
16. **SCD40 Altitude** - Dimmer per impostare altitudine (0-254 = 0-5000 metri)
17. **SCD40 Temp Offset** - Dimmer per impostare offset temperatura (0-254 = 0-10°C)

## Sistema di Debouncing per Controlli SCD40

Il firmware implementa un **sistema di debouncing intelligente a due stadi** per i controlli altitude e temperature offset, per proteggere l'EEPROM del sensore (limitata a 2000 cicli di scrittura) e ottimizzare il traffico Matter:

**Stage 1 - Debouncing comandi (10 secondi):**
- Quando OpenHAB invia un comando (es. slider altitude spostato), il valore viene salvato come "pending"
- Ogni nuovo comando resetta il timer
- Dopo 10 secondi di inattività, il valore finale viene inviato al device ESP32

**Stage 2 - Auto-persist EEPROM (10 secondi):**
- Dopo l'invio al device, parte un secondo timer di 10 secondi
- Se non ci sono nuovi invii, il comando `persist_settings()` viene automaticamente eseguito
- Questo salva permanentemente la configurazione nell'EEPROM del sensore SCD40

**Vantaggi:**
- ✅ Evita invii multipli durante lo spostamento degli slider
- ✅ Protegge l'EEPROM da wear eccessivo
- ✅ Riduce il traffico sulla rete Matter/Thread
- ✅ User experience fluida (nessun lag percepibile)

**Esempio timeline:**
```
T+0s   → Utente sposta slider da 0m a 500m (valore pending: 500m)
T+2s   → Utente corregge a 450m (valore pending: 450m, timer resettato)
T+5s   → Utente corregge a 480m (valore pending: 480m, timer resettato)
T+15s  → Nessuna modifica per 10s → Invio al device: 480m
T+25s  → Nessuna modifica per altri 10s → Salvataggio EEPROM
```

**Logging pulito:**
Le regole OpenHAB sono configurate per mostrare solo i messaggi essenziali:
- `✓ Altitudine inviata: Xm` (quando il valore viene inviato al device)
- `✓ Offset temperatura inviato: X°C` (quando il valore viene inviato al device)
- `✓ Auto-salvataggio in EEPROM` (quando la configurazione viene salvata)

Tutti i messaggi di debug intermedi sono stati rimossi per evitare log eccessivamente verbosi.

## Controllo SCD40 da OpenHAB

Il dispositivo espone 7 endpoint virtuali che permettono di controllare il sensore SCD40 direttamente da OpenHAB tramite Matter over Thread.

### Configurazione Items OpenHAB

Dopo aver aggiunto il dispositivo Matter in OpenHAB, crea un file `.items` con le seguenti definizioni:

```
// Switch momentanei (si resettano automaticamente a OFF)
Switch SCD40_Calibrate       "Calibrazione Forzata (400 ppm)"  <calibrate>    { channel="matter:..." }
Switch SCD40_Persist         "Salva Impostazioni in EEPROM"    <save>         { channel="matter:..." }
Switch SCD40_SelfTest        "Autodiagnosi Sensore (~5.5s)"    <test>         { channel="matter:..." }

// Switch persistenti
Switch SCD40_ASC             "Calibrazione Automatica (ASC)"   <calibration>  { channel="matter:..." }
Switch SCD40_LowPower        "Modalità Low Power"              <battery>      { channel="matter:..." }

// Dimmer per parametri numerici
Dimmer SCD40_Altitude        "Altitudine [%.0f m]"             <altitude>     { channel="matter:..." }
Dimmer SCD40_TempOffset      "Offset Temperatura [%.2f °C]"    <temperature>  { channel="matter:..." }
```

### Mappatura Valori Dimmer

I dimmer mappano valori 0-254 (Matter Level Control) a range specifici:

**SCD40_Altitude** (0-254 → 0-5000 metri):
```
valore_dimmer = (altitudine_metri * 254) / 5000
altitudine_metri = (valore_dimmer * 5000) / 254

Esempi:
  0   → 0 metri (livello mare)
  25  → 492 metri
  127 → 2500 metri
  254 → 5000 metri
```

**SCD40_TempOffset** (0-254 → 0-10°C):
```
valore_dimmer = (offset_celsius * 254) / 10.0
offset_celsius = (valore_dimmer * 10.0) / 254

Esempi:
  0   → 0.00°C (nessun offset)
  25  → 0.98°C
  127 → 5.00°C
  254 → 10.00°C
```

### Utilizzo degli Switch

#### 1. Calibrazione Forzata (Forced Recalibration)

Usa questo switch quando esponi il sensore ad aria fresca (400 ppm CO2):

```
SCD40_Calibrate ON
```

**Cosa succede**:
1. Il sensore ferma le misurazioni
2. Esegue FRC a 400 ppm
3. Riprende le misurazioni
4. Lo switch torna automaticamente OFF

**Quando usarlo**:
- All'aperto in ambiente non inquinato
- Dopo aver arieggiato completamente una stanza
- Quando ASC è disabilitato e noti drift nei valori

**Attenzione**: Non usare in ambienti chiusi o con CO2 > 400 ppm!

#### 2. Calibrazione Automatica (ASC)

Abilita/disabilita la calibrazione automatica settimanale:

```
SCD40_ASC ON   // Abilita ASC (consigliato)
SCD40_ASC OFF  // Disabilita ASC
```

**Cosa succede**:
- **ON**: Il sensore si calibra automaticamente assumendo che venga esposto ad aria fresca (400 ppm) almeno una volta a settimana
- **OFF**: Nessuna calibrazione automatica, necessaria FRC manuale

**Raccomandazioni**:
- Lascia ASC **ON** in ambienti abitativi normali
- Imposta ASC **OFF** solo in ambienti con CO2 sempre elevata (serre, laboratori)

#### 3. Modalità Low Power

Riduce consumo energetico (misurazioni ogni 30s invece di 5s):

```
SCD40_LowPower ON   // Attiva low power
SCD40_LowPower OFF  // Torna a modalità normale
```

**Differenze**:
- **Normale**: 15 mA @ 3.3V, misurazione ogni 5s
- **Low Power**: 1 mA @ 3.3V, misurazione ogni 30s

**Quando usare**:
- Dispositivi alimentati a batteria
- Quando non servono aggiornamenti frequenti

#### 4. Salva Impostazioni (Persist)

Salva configurazione corrente (ASC, altitude, temp offset) in EEPROM del sensore:

```
SCD40_Persist ON
```

**Cosa succede**:
1. Ferma misurazioni
2. Scrive configurazione in EEPROM (800ms)
3. Riprende misurazioni
4. Switch torna automaticamente OFF

**Importante**:
- EEPROM ha limite di 2000 scritture
- Usa solo quando vuoi rendere permanente una configurazione testata
- Non usare in automazioni ripetitive!

#### 5. Autodiagnosi (Self Test)

Esegue test completo del sensore (~5.5 secondi):

```
SCD40_SelfTest ON
```

**Cosa succede**:
1. Ferma misurazioni
2. Esegue test hardware interno
3. Riprende misurazioni
4. Switch torna automaticamente OFF

**Controllo risultato**: Verifica i log ESP32:
```
I (12345) app_main: SCD40 self test: PASSED
I (12345) app_main: SCD40 self test: FAILED
```

**Quando usare**:
- Dopo l'installazione iniziale
- Se sospetti malfunzionamenti
- Per diagnostica periodica

### Utilizzo dei Dimmer

#### 1. Impostazione Altitudine

Configura l'altitudine della tua posizione per compensazione pressione barometrica:

**Da OpenHAB UI/sitemap**:
```
Slider item=SCD40_Altitude minValue=0 maxValue=254 step=1
```

**Da regola OpenHAB**:
```javascript
rule "Imposta altitudine 500 metri"
when
    System started
then
    val altitude_meters = 500
    val dimmer_value = (altitude_meters * 254 / 5000) as Number
    SCD40_Altitude.sendCommand(dimmer_value)
end
```

**Valori tipici**:
- Milano (120m): `dimmer = 6`
- Roma (20m): `dimmer = 1`
- Torino (240m): `dimmer = 12`
- Trento (200m): `dimmer = 10`

#### 2. Impostazione Offset Temperatura

Compensa il riscaldamento dovuto all'elettronica:

**Da regola OpenHAB**:
```javascript
rule "Imposta offset temperatura +3.5°C"
when
    System started
then
    val offset_celsius = 3.5
    val dimmer_value = (offset_celsius * 254 / 10.0) as Number
    SCD40_TempOffset.sendCommand(dimmer_value)
end
```

**Come determinare l'offset**:
1. Leggi temperatura SCD40
2. Confronta con termometro di riferimento
3. Calcola differenza: `offset = temp_scd40 - temp_reference`
4. Imposta l'offset calcolato

**Esempio**:
- SCD40 legge 25.3°C
- Termometro di riferimento: 22.0°C
- Offset necessario: 25.3 - 22.0 = 3.3°C
- Valore dimmer: `(3.3 * 254) / 10.0 = 84`

### Esempi di Automazione OpenHAB

#### Calibrazione automatica settimanale all'aperto

```javascript
rule "Calibrazione SCD40 ogni domenica mattina"
when
    Time cron "0 0 8 ? * SUN"  // Domenica ore 8:00
then
    // Assicurati di essere all'aperto o con finestre aperte!
    SCD40_Calibrate.sendCommand(ON)
    logInfo("SCD40", "Calibrazione forzata eseguita")
end
```

#### Salvataggio configurazione dopo modifica

```javascript
rule "Salva configurazione SCD40"
when
    Item SCD40_ASC changed or
    Item SCD40_Altitude changed or
    Item SCD40_TempOffset changed
then
    // Attendi che il comando sia processato
    Thread::sleep(2000)

    // Salva in EEPROM
    SCD40_Persist.sendCommand(ON)
    logInfo("SCD40", "Configurazione salvata in EEPROM")
end
```

#### Attivazione low power di notte

```javascript
rule "Low power notturno"
when
    Time cron "0 0 23 * * ?"  // 23:00
then
    SCD40_LowPower.sendCommand(ON)
    logInfo("SCD40", "Modalità low power attivata")
end

rule "Normal power diurno"
when
    Time cron "0 0 7 * * ?"  // 07:00
then
    SCD40_LowPower.sendCommand(OFF)
    logInfo("SCD40", "Modalità normale attivata")
end
```

#### Self-test settimanale automatico

```javascript
rule "Self-test SCD40 settimanale"
when
    Time cron "0 0 2 ? * SAT"  // Sabato ore 2:00
then
    SCD40_SelfTest.sendCommand(ON)
    logInfo("SCD40", "Self-test avviato")

    // Monitora i log ESP32 per il risultato
end
```

### Best Practices

1. **Primo avvio**:
   - Imposta altitudine della tua zona
   - Imposta offset temperatura dopo confronto con termometro
   - Abilita ASC (consigliato)
   - Salva configurazione con Persist

2. **Manutenzione periodica**:
   - Calibrazione forzata ogni 1-2 mesi (se ASC disabilitato)
   - Self-test ogni 6 mesi
   - Verifica offset temperatura stagionalmente

3. **Limiti EEPROM**:
   - Non usare Persist in loop o automazioni frequenti
   - Massimo 2000 scritture lifetime
   - Usa Persist solo per configurazioni finali

4. **Timing**:
   - Attendi 5 secondi dopo start_periodic_measurement
   - Self-test richiede ~5.5 secondi
   - Calibrazione forzata richiede ~2 secondi
   - Persist richiede ~800 ms

## Monitoraggio Valori SCD40

I valori del sensore SCD40 vengono automaticamente pubblicati via Matter:

```
I (12345) app_main: SCD40: CO2=650 ppm, Temp=22.50°C, Humidity=45.30%RH
I (12346) app_main: Air Quality updated to: 1 (CO2: 650 ppm)
```

**Mappatura Air Quality**:
- `1` = Good (< 800 ppm)
- `2` = Fair (800-1000 ppm)
- `3` = Moderate (1000-1500 ppm)
- `4` = Poor (1500-2000 ppm)
- `5` = Very Poor (2000-5000 ppm)
- `6` = Extremely Poor (> 5000 ppm)

## Troubleshooting SCD40

### Sensore non rilevato (I2C error)

**Sintomi**: Log mostra `I2C initialization failed` o `SCD40 sensor initialization failed`.

**Soluzioni**:
1. Verifica connessioni I2C:
   - SDA → GPIO 22
   - SCL → GPIO 23
   - VDD → 3.3V
   - GND → GND
2. Controlla indirizzo I2C: deve essere `0x62` (fisso hardware)
3. Usa un I2C scanner per verificare che il dispositivo risponda
4. Verifica che i pin GPIO 22 e 23 non siano usati per altre funzioni

### Letture CO2 sempre a 0 o 400 ppm

**Sintomi**: Il valore CO2 non cambia mai.

**Soluzioni**:
1. Verifica che `start_periodic_measurement` sia stato chiamato
2. Attendi almeno 5 secondi dopo l'avvio (prima misurazione)
3. Controlla nel log: `SCD40 periodic measurement started`
4. Verifica CRC: se hai errori CRC, c'è un problema di comunicazione I2C

### Temperatura/Umidità non accurate

**Sintomi**: I valori T/RH non corrispondono a un termometro di riferimento.

**Soluzioni**:
1. Configura il temperature offset:
   ```cpp
   float offset = temp_scd40 - temp_reference;
   scd40_set_temperature_offset(offset);
   ```
2. Verifica che il sensore non sia troppo vicino a fonti di calore
3. Attendi l'equilibrio termico (sensore acceso da almeno 10 minuti)

### CO2 deriva nel tempo (drift)

**Sintomi**: Le letture CO2 aumentano/diminuiscono lentamente nel tempo.

**Soluzioni**:
1. Abilita automatic self-calibration (ASC):
   ```cpp
   scd40_set_automatic_self_calibration_enabled(true);
   ```
2. Esponi il sensore ad aria fresca (400 ppm) almeno 1 volta a settimana
3. Esegui forced recalibration manuale se necessario:
   ```cpp
   int16_t correction;
   scd40_perform_forced_recalibration(400, &correction);
   ```

### Self-test fallisce

**Sintomi**: `scd40_perform_self_test()` ritorna `test_passed = false`.

**Soluzioni**:
1. Verifica alimentazione stabile (3.3V ±10%)
2. Controlla che non ci siano disturbi elettromagnetici
3. Prova un factory reset del sensore:
   ```cpp
   scd40_perform_factory_reset();
   ```
4. Se il problema persiste, il sensore potrebbe essere difettoso

### Errore "Cluster cannot be NULL"

**Sintomi**: Log mostra `E (XXXXX) data_model: Cluster cannot be NULL.`

**Causa**: Questo errore può avere due origini:

1. **EEPROM sync durante boot**: Il firmware tenta di sincronizzare valori EEPROM su endpoint non ancora creati
2. **On/Off cluster auto-update**: Matter SDK tenta di impostare On/Off a ON quando Level Control cambia (dimmer virtuali)

**Soluzioni implementate nel firmware:**

1. **Validazione endpoint prima di EEPROM sync** (app_main.cpp:1485-1515):
   ```cpp
   if (scd40_temp_offset_endpoint_id != 0 && scd40_get_temperature_offset(&saved_offset) == ESP_OK) {
       // Solo se endpoint creato con successo
   }
   ```

2. **Handler On/Off per dimmer virtuali** (app_main.cpp:863):
   ```cpp
   // Accetta silenziosamente cambi On/Off per dimmer virtuali
   if (cluster_id == OnOff::Id && endpoint è altitude/temp_offset) {
       return ESP_OK;
   }
   ```

**Nota**: Se vedi ancora questo errore, verifica che `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` in `sdkconfig.defaults` sia >= 20 per permettere la creazione di tutti gli endpoint necessari.

## Ottimizzazioni ESP32-C6

Questo progetto sfrutta le caratteristiche native dell'ESP32-C6 XIAO:

- **Radio Thread 802.15.4 nativa**: nessun dongle esterno necessario
- **Hardware crypto acceleration**: AES, SHA, ECC accelerati via hardware
- **I2C hardware controller**: comunicazione affidabile con SCD40
- **NimBLE stack**: stack Bluetooth ottimizzato per low memory
- **Partizioni ottimizzate**: layout flash per 4MB
- **FreeRTOS 1000 Hz**: polling GPIO responsive e letture sensore precise
- **RF switch integrato**: selezione antenna interna/esterna senza componenti esterni
- **LED di stato Thread**: monitoring visivo real-time del ruolo nella mesh

## Riferimenti

- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [Matter Specification](https://csa-iot.org/developer-resource/specifications-download-request/)
- [Thread Group](https://www.threadgroup.org/)
- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [XIAO ESP32C6 Wiki](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- [Sensirion SCD40 Datasheet](https://sensirion.com/products/catalog/SCD40/)

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

**Versione**: 2.2.4
**Ultima modifica**: 2025-12-31
**Autore**: VicinoDiCasaDigitale

---

## Changelog

### v2.2.4 (2025-12-31)
- **FIX CRITICO**: Tipo di dato corretto per attribute update
  - **Problema**: `attribute::update()` falliva con `ESP_ERR_INVALID_ARG`
  - **Errore**: `Different value type : Expected Type : 136 Attempted Type: 8`
  - **Causa**: Usavo `ESP_MATTER_VAL_TYPE_UINT8` invece di `ESP_MATTER_VAL_TYPE_NULLABLE_UINT8`
  - **Soluzione**: Cambiato tipo a `NULLABLE_UINT8` per CurrentLevel attribute
  - **Risultato**: Gli update ora vengono accettati e inviati a OpenHAB
  - Log attesi dopo 10s dal boot:
    ```
    I (12810) app_main: Syncing SCD40 configuration to OpenHAB...
    I (12810) app_main: → OpenHAB: Synced altitude 40m (level 3) ✓
    I (12811) app_main: → OpenHAB: Synced temp offset 4.00°C (level 103) ✓
    I (12811) app_main: ✓ SCD40 configuration synced to OpenHAB
    ```

### v2.2.3 (2025-12-31)
- **FIX**: Sincronizzazione OpenHAB con delay post-boot per stabilità connessione
  - **Problema**: L'update immediato post `esp_matter::start()` arrivava troppo presto, OpenHAB non riceveva i valori
  - **Soluzione**: Creato task dedicato `scd40_config_sync_task` con delay di 10 secondi
  - Il task:
    1. Aspetta 10 secondi per stabilizzazione connessione Matter/Thread
    2. Invia `attribute::update()` per altitude e temp offset
    3. Si auto-elimina dopo completamento
  - **Risultato**: OpenHAB riceve correttamente i valori dall'EEPROM al boot
  - Visibile nei log dopo ~13 secondi dal boot:
    ```
    I (13xxx) app_main: SCD40 config sync task started - waiting 10s...
    I (23xxx) app_main: → OpenHAB: Synced altitude 40m (level 3)
    I (23xxx) app_main: → OpenHAB: Synced temp offset 4.00°C (level 103)
    I (23xxx) app_main: ✓ SCD40 configuration synced to OpenHAB
    ```

### v2.2.2 (2025-12-31)
- **FIX**: Sincronizzazione automatica valori EEPROM → OpenHAB all'avvio
  - **Problema**: OpenHAB mostrava valori vecchi/cached (es. 0.1°C) invece dei valori corretti dall'EEPROM (es. 4.0°C)
  - **Causa**: L'ESP32 caricava correttamente i valori dall'EEPROM ma non notificava OpenHAB
  - **Soluzione**: Aggiunta sincronizzazione esplicita dopo `esp_matter::start()`
  - Il firmware ora:
    1. Carica `altitude` e `temp_offset` dall'EEPROM SCD40
    2. Avvia Matter e crea gli endpoint
    3. Forza un `attribute::update()` per notificare OpenHAB
    4. OpenHAB riceve i valori corretti e aggiorna la UI
  - **Risultato**: OpenHAB mostra sempre i valori corretti dall'EEPROM al riavvio
  - Visibile nei log:
    ```
    I (xxxx) app_main: → OpenHAB: Synced altitude 40m (level XX)
    I (xxxx) app_main: → OpenHAB: Synced temp offset 4.00°C (level XX)
    I (xxxx) app_main: ✓ SCD40 configuration synced to OpenHAB
    ```

### v2.2.1 (2025-12-31)
- **FIX**: Protezione contro sovrascrittura valori durante inizializzazione Matter
  - **Problema**: Dopo aver letto i valori dall'EEPROM, Matter SDK li sovrascriveva con valori di default durante la creazione degli endpoint
  - **Soluzione**: Aggiunto flag `device_initialization_complete` che ignora attribute updates durante il boot
  - Il firmware ora:
    1. Crea tutti gli endpoint con i valori corretti dall'EEPROM
    2. Ignora i callback POST_UPDATE durante l'inizializzazione
    3. Abilita gli update solo DOPO il completamento del boot
  - **Risultato**: I valori salvati rimangono intatti e non vengono più resettati a 0
  - Visibile nei log: `"Device initialization complete - ready for operation"`

### v2.2 (2025-12-31)
- **FIX CRITICO**: Ripristino automatico valori da EEPROM SCD40 all'avvio
  - **Problema risolto**: All'avvio, i valori salvati nell'EEPROM del SCD40 venivano sovrascritti con quelli di NVS
  - **Soluzione**: Implementata lettura prioritaria dall'EEPROM SCD40 all'avvio
  - **Gerarchia nuova**: EEPROM SCD40 > NVS ESP32 > Valori default
  - Al boot, il firmware ora:
    1. Legge `altitude` e `temp_offset` dall'EEPROM del SCD40
    2. Se trovati, li usa e sincronizza con NVS ESP32
    3. Altrimenti usa NVS come fallback
    4. Infine applica i valori al sensore
  - **Vantaggio**: I valori salvati con `persist_settings()` sopravvivono a power-off, reflash firmware, e factory reset ESP32
  - **Log chiari**: All'avvio mostra se i valori provengono da EEPROM o NVS
  - **Backward compatible**: Se l'EEPROM è vuota (primo avvio), usa NVS o default senza errori

### v2.1 (2025-12-30)
- **FIX**: Aumentato `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` da 17 a 20
  - Risolto errore creazione endpoint 17 (temperature offset)
  - Permette espansioni future senza problemi
- **FIX**: Risolto errore "Cluster cannot be NULL" con due fix:
  - Validazione endpoint ID prima di EEPROM sync durante boot
  - Handler On/Off cluster per dimmer virtuali (altitude/temp offset)
- **ENHANCEMENT**: Sistema di debouncing intelligente a due stadi per OpenHAB
  - Stage 1: 10s di attesa prima di inviare al device (evita invii multipli)
  - Stage 2: 10s di attesa prima di salvare in EEPROM (protegge da wear)
  - Implementato nelle regole OpenHAB per altitude e temperature offset
- **ENHANCEMENT**: Logging pulito nelle regole OpenHAB
  - Rimossi tutti i messaggi di debug verbosi
  - Mantenuti solo messaggi essenziali: invio, salvataggio, errori
- **DOC**: Aggiornata documentazione con tutte le fix e i miglioramenti

### v2.0 (2025-01)
- **MAJOR**: Aggiunto sensore Sensirion SCD40 (CO2/Temperatura/Umidità)
- Implementati tutti i 20 comandi I2C del SCD40 (API completa)
- Ridotte uscite da 4 a 2 (GPIO 22/23 ora usati per I2C)
- Endpoint Matter per Air Quality, CO2, Temperature, Humidity
- Supporto calibrazione automatica (ASC) e forzata (FRC)
- Modalità low power e single shot (SCD41)
- Persist settings in EEPROM
- Self-test e diagnostica sensore

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
