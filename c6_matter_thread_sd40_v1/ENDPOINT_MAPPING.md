# Matter Endpoint Mapping

Questo documento descrive tutti gli endpoint Matter creati dal dispositivo e come sono visualizzati in OpenHAB.

## Configurazione Endpoint

Il dispositivo crea **18 endpoint totali** (endpoint 0 root + 17 endpoint dinamici).

La configurazione in `sdkconfig.defaults` è:
```
CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=20
```

Questo valore permette la creazione di tutti i 17 endpoint attualmente utilizzati (endpoint 1-17), lasciando margine per future espansioni.

**Importante**: Se vedi errori come `Dynamic endpoint count cannot be greater than CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT`, aumenta questo valore in `sdkconfig.defaults`, elimina il file `sdkconfig`, e riesegui `idf.py set-target esp32c6` seguito da `idf.py build`.

## Endpoint Principali

### Input GPIO (Pulsanti)
- **Endpoint 1**: Input 1 (GPIO0) - Generic Switch
- **Endpoint 2**: Input 2 (GPIO1) - Generic Switch
- **Endpoint 3**: Input 3 (GPIO2) - Generic Switch
- **Endpoint 4**: Input 4 (GPIO21) - Generic Switch

### Output GPIO (Relè)
- **Endpoint 5**: Output 1 (GPIO19) - On/Off Light
- **Endpoint 6**: Output 2 (GPIO20) - On/Off Light

## Sensori SCD40

### Sensori Ambientali
- **Endpoint 7**: Air Quality Sensor
  - Cluster: Air Quality (0x005B)
  - Cluster: CO2 Concentration Measurement (0x040D)
  - OpenHAB: Sensore di qualità dell'aria + CO2 (ppm)

- **Endpoint 8**: Temperature Sensor (0x0402)
  - OpenHAB: Number (temperatura in °C × 100, es. 2550 = 25.50°C)

- **Endpoint 9**: Humidity Sensor (0x0405)
  - OpenHAB: Number (umidità in % × 100, es. 4820 = 48.20%RH)

## Controlli Virtuali SCD40 per OpenHAB

### Switch di Comando (Momentary/Latching)
- **Endpoint 10**: SCD40 Calibrate - On/Off Switch (momentary)
  - **Funzione**: Esegue calibrazione FRC a 400 ppm
  - **Tipo**: Momentary (si spegne automaticamente dopo l'azione)

- **Endpoint 11**: SCD40 ASC Enable - On/Off Switch (latching)
  - **Funzione**: Abilita/disabilita Automatic Self-Calibration
  - **Tipo**: Latching (mantiene lo stato ON/OFF)
  - **Default**: ON

- **Endpoint 12**: SCD40 Low Power - On/Off Switch (latching)
  - **Funzione**: Modalità low power (misurazione ogni 30s invece di 5s)
  - **Tipo**: Latching (mantiene lo stato ON/OFF)
  - **Default**: OFF

- **Endpoint 13**: SCD40 Persist Settings - On/Off Switch (momentary)
  - **Funzione**: Salva impostazioni in EEPROM del sensore
  - **Tipo**: Momentary (si spegne automaticamente dopo l'azione)

- **Endpoint 14**: SCD40 Self Test - On/Off Switch (momentary)
  - **Funzione**: Esegue self-test del sensore (5.5 secondi)
  - **Tipo**: Momentary (si spegne automaticamente dopo l'azione)

### Controlli Numerici (Dimmer/Level Control)
- **Endpoint 16**: SCD40 Altitude Control
  - **Cluster**: Level Control (0x0008)
  - **Attributo**: CurrentLevel (0-254)
  - **Range**: 0-254 → 0-5000 metri (formula: metri = level × 5000 / 254)
  - **OpenHAB**: Dimmer item (convertito in metri tramite regole)
  - **Default**: 0 (livello del mare)

- **Endpoint 17**: SCD40 Temperature Offset Control
  - **Cluster**: Level Control (0x0008)
  - **Attributo**: CurrentLevel (0-254)
  - **Range**: 0-254 → 0-10°C (formula: °C = level × 10 / 254)
  - **OpenHAB**: Dimmer item (convertito in °C tramite regole)
  - **Default**: ~102 (≈4.0°C)

**Nota**: Gli endpoint 16 e 17 usano il cluster Level Control (dimmer) invece di Number items. Le regole OpenHAB (`scd40.rules`) si occupano della conversione bidirezionale tra valori raw (0-254) e valori intuitivi (metri e gradi Celsius).

## Come configurare in OpenHAB

### Switch (Endpoint 10-14)
In OpenHAB appariranno come **Switch items**. Rinominali in:
- Endpoint 10: "SCD40 Calibrate"
- Endpoint 11: "SCD40 ASC Enable"
- Endpoint 12: "SCD40 Low Power"
- Endpoint 13: "SCD40 Persist Settings"
- Endpoint 14: "SCD40 Self Test"

### Dimmer Items (Endpoint 16-17)
In OpenHAB appariranno come **Dimmer items**. Sono necessari due set di items:

**Items RAW (collegati ai channel Matter - NON MODIFICARE DIRETTAMENTE):**
```
Dimmer SCD40Altitudine1_Raw "Altitudine RAW [%d]" { channel="matter:..." }
Dimmer SCD40_TempOffset1_Raw "Offset RAW [%d]" { channel="matter:..." }
```

**Items PROXY (da usare nelle UI e automazioni):**
```
Number:Length SCD40Altitudine1 "Altitudine [%d m]"
Number:Temperature SCD40_TempOffset1 "Offset Temperatura [%.2f °C]"
```

**Le regole in `scd40.rules` gestiscono automaticamente:**
- Conversione da valori intuitivi (metri/°C) a dimmer (0-254)
- Debouncing intelligente (10s prima di inviare al device)
- Auto-persist (10s dopo invio → salvataggio EEPROM)
- Conversione bidirezionale (device → UI)

### Esempio Sitemap OpenHAB
```
Switch item=SCD40_Calibrate1 label="Calibra SCD40"
Switch item=SCD40_ASC1 label="ASC Automatico"
Switch item=SCD40_LowPower1 label="Modalità Risparmio"
Switch item=SCD40_Persist1 label="Salva Impostazioni"
Switch item=SCD40_SelfTest1 label="Autotest"
Setpoint item=SCD40Altitudine1 label="Altitudine [%d m]" minValue=0 maxValue=5000 step=10
Slider item=SCD40_TempOffset1 label="Offset Temp [%.2f °C]" minValue=0 maxValue=10 step=0.1
```

**Nota**: Usa gli item PROXY (senza `_Raw`), non quelli raw. Le conversioni sono automatiche.

## Note Importanti

1. **Persistenza Multi-Livello (v2.2.4+)**:
   - **EEPROM SCD40** (priorità massima): I valori sono salvati nell'EEPROM interna del sensore SCD40
   - **NVS ESP32** (sincronizzato): Copia automaticamente sincronizzata con EEPROM SCD40
   - **All'avvio**: Il firmware legge PRIMA dall'EEPROM SCD40, poi sincronizza NVS
   - **Sync automatico con OpenHAB**: Dopo 10 secondi dal boot, i valori vengono inviati a OpenHAB
   - **Persistenza garantita**: I valori sopravvivono a power-off, reflash firmware, e factory reset ESP32
   - Log di sync (dopo ~10s dal boot):
     ```
     I (12810) app_main: → OpenHAB: Synced altitude Xm (level Y)
     I (12811) app_main: → OpenHAB: Synced temp offset X°C (level Y)
     ```

2. **Momenti di applicazione**: Quando si modificano altitude o temperature offset:
   - Il sensore viene fermato temporaneamente
   - Le nuove impostazioni vengono applicate
   - Il sensore viene riavviato
   - La procedura richiede circa 500ms

3. **Self-Test**: Durante il self-test (5.5 secondi):
   - Il sensore non fornisce letture
   - Potrebbero comparire errori CRC nei log (normale)
   - Al termine il sensore riprende automaticamente

4. **Persist Settings**:
   - **Automatico (v2.1+)**: Le regole OpenHAB salvano automaticamente in EEPROM dopo 20s (10s debounce + 10s auto-persist)
   - **Manuale**: Usa lo switch "Persist Settings" per salvare immediatamente
   - **Limite**: L'EEPROM SCD40 ha un limite di 2000 scritture lifetime (non abusare del persist manuale)
