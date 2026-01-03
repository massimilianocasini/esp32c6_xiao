# Configurazione OpenHAB per SCD40 Matter Device

Questa guida spiega come configurare OpenHAB per utilizzare il dispositivo SCD40 Matter con **valori intuitivi** (metri e gradi Celsius) invece dei valori raw del dimmer (0-254).

## ⚠️ IMPORTANTE: Cluster CO2 e Compatibilità OpenHAB

Il dispositivo utilizza il cluster standard Matter **Carbon Dioxide Concentration Measurement (0x040D)** per i valori CO2 in ppm.

**Compatibilità OpenHAB:**
- ✅ **OpenHAB 5.2+**: Supporto completo cluster CO2 (PR #19897 merged 27/12/2025)
- ❌ **OpenHAB 5.1 e precedenti**: Cluster CO2 **non supportato** (canale mostrerà NULL)

**Se usi OpenHAB 5.1:**
- Il canale CO2 ppm non funzionerà (mostrerà NULL)
- **Soluzione temporanea**: Usa il canale Air Quality (endpoint 7) che mostra valori 0-6
- Quando aggiorni a OpenHAB 5.2+, il canale CO2 funzionerà automaticamente senza riflash

## Indice

1. [Installazione File](#installazione-file)
2. [Configurazione Channel ID](#configurazione-channel-id)
3. [Come Funziona](#come-funziona)
4. [Utilizzo](#utilizzo)
5. [Troubleshooting](#troubleshooting)

---

## Installazione File

### 1. Copia i file di configurazione

Copia i file dalla directory `openhab/` nella tua installazione OpenHAB:

```bash
# Da macOS/Linux
sudo cp scd40.items /etc/openhab/items/
sudo cp scd40.rules /etc/openhab/rules/
sudo cp scd40.sitemap /etc/openhab/sitemaps/
sudo cp airquality.map /etc/openhab/transform/

# Imposta i permessi corretti
sudo chown openhab:openhab /etc/openhab/items/scd40.items
sudo chown openhab:openhab /etc/openhab/rules/scd40.rules
sudo chown openhab:openhab /etc/openhab/sitemaps/scd40.sitemap
sudo chown openhab:openhab /etc/openhab/transform/airquality.map
```

### 2. Riavvia OpenHAB (opzionale)

```bash
sudo systemctl restart openhab
```

O attendi che OpenHAB rilevi automaticamente i nuovi file.

---

## Configurazione Channel ID

### Trova i Channel ID del tuo dispositivo

1. Apri OpenHAB UI: `http://your-openhab-ip:8080`
2. Vai su **Settings** → **Things**
3. Clicca sul tuo dispositivo Matter SCD40
4. Vai sulla tab **Channels**
5. Copia i Channel ID

### Sostituisci i placeholder nel file items

Apri `/etc/openhab/items/scd40.items` e sostituisci i placeholder `matter:XXXXX:XXXXX:endpointX_XXX` con i tuoi channel ID reali.

**Esempio:**

Prima (placeholder):
```javascript
Number:Dimensionless SCD40_CO2_1 "CO2 [%.0f ppm]" <gas> (gSCD40)
    { channel="matter:XXXXX:XXXXX:endpoint8_co2" }
```

Dopo (con channel ID reale):
```javascript
Number:Dimensionless SCD40_CO2_1 "CO2 [%.0f ppm]" <gas> (gSCD40)
    { channel="matter:device:bridge:74a3b12f_endpoint8_carbondioxide" }
```

⚠️ **Nota OpenHAB 5.1**: Se il canale non esiste o mostra NULL, la tua versione OpenHAB non supporta ancora il cluster CO2. Usa il canale Air Quality (endpoint 7) come fallback o aggiorna a OpenHAB 5.2+.

### Mapping Endpoint → Channel

| Endpoint | Tipo | Channel Type | Esempio Channel ID | Compatibilità |
|----------|------|--------------|-------------------|---------------|
| 7 | Air Quality | `airquality` | `endpoint7_airquality` | Tutte le versioni |
| 8 | CO2 ppm | `carbondioxide` | `endpoint8_carbondioxide` | ⚠️ Solo OpenHAB 5.2+ |
| 9 | Temperature | `temperature` | `endpoint9_temperature` | Tutte le versioni |
| 10 | Humidity | `humidity` | `endpoint10_humidity` | Tutte le versioni |
| 11 | Calibrate Switch | `switch` | `endpoint11_switch` | Tutte le versioni |
| 12 | ASC Switch | `switch` | `endpoint12_switch` | Tutte le versioni |
| 13 | Low Power Switch | `switch` | `endpoint13_switch` | Tutte le versioni |
| 14 | Persist Switch | `switch` | `endpoint14_switch` | Tutte le versioni |
| 15 | Self Test Switch | `switch` | `endpoint15_switch` | Tutte le versioni |
| 16 | Altitude Dimmer | `brightness` | `endpoint16_brightness` | Tutte le versioni |
| 17 | Temp Offset Dimmer | `brightness` | `endpoint17_brightness` | Tutte le versioni |

---

## Come Funziona

### Sistema di Trasformazione Automatica con Debouncing a Due Stadi

Il sistema usa un **pattern proxy con debouncing intelligente a due stadi** per evitare invii multipli durante lo spostamento degli slider e proteggere l'EEPROM del sensore SCD40 (limitata a 2000 cicli di scrittura):

```
┌─────────────────┐         ┌──────────────────┐         ┌────────────┐
│  UI OpenHAB     │ ──────> │  Item Proxy      │ ──────> │ Pending    │
│  (Metri/°C)     │         │  (Conversione)   │         │ (attesa)   │
└─────────────────┘         └──────────────────┘         └────────────┘
                                                                │
                                                         Timer 10s
                                                                │
                                                                ▼
                                                          ┌────────────┐
                                                          │ Item Raw   │
                                                          │ (0-254)    │
                                                          └────────────┘
                                                                │
                                                         Timer 10s
                                                                │
                                                                ▼
                                                          ┌────────────┐
                                                          │   EEPROM   │
                                                          │   Persist  │
                                                          └────────────┘
```

**Timeline:**
- **T+0s**: Utente modifica valore → Salvato come pending
- **T+10s**: Nessuna nuova modifica → Inviato al device ESP32
- **T+20s**: Nessuna nuova modifica → Salvato in EEPROM

**Vantaggi:**
- ✅ Evita invii multipli durante lo spostamento slider
- ✅ Riduce wear dell'EEPROM (max 2000 cicli)
- ✅ Performance migliorate (meno traffico Matter)
- ✅ Logging pulito (solo messaggi essenziali)

**Logging pulito:**
Le regole sono configurate per mostrare SOLO i messaggi essenziali:
- `✓ Altitudine inviata: Xm` - quando il valore viene inviato al device
- `✓ Offset temperatura inviato: X°C` - quando il valore viene inviato al device
- `✓ Auto-salvataggio in EEPROM` - quando la configurazione viene salvata
- `✓ Auto-salvataggio switch in EEPROM` - quando gli switch vengono salvati
- `⚠️ CO2 elevato: X ppm` - warning per valori CO2 sopra soglie

**Messaggi rimossi** (per evitare log verbosi):
- ❌ "Comando altitudine/offset ricevuto"
- ❌ "Pending aggiornato a X"
- ❌ "Timer scaduto - controllo pending values"
- ❌ "Altitudine/Offset aggiornato: X"
- ❌ "Timestamp invio aggiornato"
- ❌ Tutti i messaggi di debug intermedi

**Risultato**: Durante lo spostamento di uno slider vedrai solo il messaggio finale quando il valore viene effettivamente inviato al device, invece di decine di messaggi di debug.

### Formule di Conversione

**Altitudine:**
```javascript
// Da metri a dimmer
level = (metri * 254) / 5000

// Da dimmer a metri
metri = (level * 5000) / 254
```

**Offset Temperatura:**
```javascript
// Da °C a dimmer
level = (°C * 254) / 10

// Da dimmer a °C
°C = (level * 10) / 254
```

### Item Utilizzati

**Item RAW (nascosti - non toccare):**
- `SCD40Altitudine1_Raw` - Dimmer Matter (0-254)
- `SCD40_TempOffset1_Raw` - Dimmer Matter (0-254)

**Item PROXY (da usare nelle UI):**
- `SCD40Altitudine1` - Number:Length (0-5000 metri)
- `SCD40_TempOffset1` - Number:Temperature (0-10 °C)

**Item INTERNI (gestione automatica - non toccare):**
- `SCD40_AltitudePending` - Valore altitudine in attesa di invio
- `SCD40_TempOffsetPending` - Valore offset in attesa di invio
- `SCD40_LastChangeMillis` - Timestamp ultima modifica
- `SCD40_SendTimestamp` - Timestamp ultimo invio al device

**Flusso di gestione:**
1. **UI → Proxy** (comando utente)
2. **Proxy → Pending** (salvataggio temporaneo)
3. **Timer 10s** → **Pending → Raw** (invio al device)
4. **Timer 10s** → **Persist** (salvataggio EEPROM)

---

## Utilizzo

### Dalla UI OpenHAB

1. Apri la sitemap: `http://your-openhab-ip:8080/basicui/app?sitemap=scd40`

2. **Imposta Altitudine:**
   - Usa il setpoint per impostare i metri direttamente (es. 325 m)
   - Range: 0-5000 metri
   - Step: 10 metri

3. **Imposta Offset Temperatura:**
   - Usa lo slider per impostare i gradi direttamente (es. 4.5 °C)
   - Range: 0-10 °C
   - Step: 0.1 °C

### Da Regole/Script

```javascript
// Imposta altitudine a 325 metri
SCD40Altitudine1.sendCommand(325)

// Imposta offset temperatura a 4.5°C
SCD40_TempOffset1.sendCommand(4.5)

// Leggi i valori correnti
val altitude = (SCD40Altitudine1.state as Number).intValue
val offset = (SCD40_TempOffset1.state as Number).floatValue

logInfo("SCD40", "Altitudine: {}m, Offset: {:.2f}°C", altitude, offset)
```

### Da REST API

```bash
# Imposta altitudine a 325 metri
curl -X POST \
  http://openhab:8080/rest/items/SCD40Altitudine1 \
  -H 'Content-Type: text/plain' \
  -d '325'

# Imposta offset temperatura a 4.5°C
curl -X POST \
  http://openhab:8080/rest/items/SCD40_TempOffset1 \
  -H 'Content-Type: text/plain' \
  -d '4.5'

# Leggi i valori
curl http://openhab:8080/rest/items/SCD40Altitudine1/state
curl http://openhab:8080/rest/items/SCD40_TempOffset1/state
```

### Esempi Pratici

**Scenario 1: Casa al mare (0 metri)**
```javascript
SCD40Altitudine1.sendCommand(0)
SCD40_TempOffset1.sendCommand(3.5)  // Offset +3.5°C
SCD40_Persist1.sendCommand(ON)      // Salva in EEPROM
```

**Scenario 2: Appartamento in montagna (850 metri)**
```javascript
SCD40Altitudine1.sendCommand(850)
SCD40_TempOffset1.sendCommand(4.0)
SCD40_Persist1.sendCommand(ON)
```

**Scenario 3: Calibrazione**
```javascript
// Esponi il sensore all'aria esterna (circa 400 ppm CO2)
createTimer(now.plusMinutes(5), [ |  // Attendi 5 minuti
    SCD40_Calibrate1.sendCommand(ON)  // Calibra a 400 ppm
    createTimer(now.plusSeconds(1), [ |
        SCD40_Persist1.sendCommand(ON)  // Salva calibrazione
    ])
])
```

---

## Troubleshooting

### I valori proxy non si aggiornano

**Causa:** Le regole non sono attive.

**Soluzione:**
```bash
# Controlla i log delle regole
tail -f /var/log/openhab/openhab.log | grep SCD40

# Riavvia OpenHAB
sudo systemctl restart openhab
```

### Valori fuori range

**Causa:** Limiti violati.

**Soluzione:** Le regole hanno protezioni automatiche:
- Altitudine: limitata a 0-5000 m
- Offset: limitato a 0-10 °C

Controlla i log per warning:
```bash
grep "SCD40.*warn" /var/log/openhab/openhab.log
```

### Item proxy rimane NULL

**Causa:** Channel ID non configurati correttamente.

**Soluzione:**
1. Verifica i channel ID in OpenHAB UI
2. Controlla che gli item RAW ricevano valori
3. Attiva le regole di inizializzazione (riavvia OpenHAB)

### Auto-persist non funziona

**Causa:** Regola utility disabilitata.

**Soluzione:**
Puoi attivare manualmente il persist:
```javascript
SCD40_Persist1.sendCommand(ON)
```

O modificare la regola "Auto-persist settings after changes" in `scd40.rules`.

---

## Feature Avanzate

### Debouncing e Persistenza Automatica

**Sistema a due stadi:**
1. **Debouncing (10s)**: Aspetta 10 secondi dall'ultima modifica prima di inviare al device
   - Evita invii multipli durante lo spostamento slider
   - Ogni nuova modifica resetta il timer

2. **Auto-persist (10s)**: Aspetta ulteriori 10 secondi dopo l'invio prima di salvare in EEPROM
   - Protegge EEPROM da scritture eccessive (max 2000 cicli)
   - Solo dopo che i valori sono stabilizzati

**Per gli switch (ASC, Low Power):**
- Salvataggio immediato dopo 2 secondi (non necessitano debouncing)

**Per disabilitare:**
- Commenta le regole `Auto-persist` in `scd40.rules`

### Alert CO2

La regola `Log CO2 warnings` logga automaticamente quando CO2 > 800 ppm.

Puoi estenderla per inviare notifiche:

```javascript
rule "SCD40: CO2 Alert Notification"
when
    Item SCD40_CO2_1 changed
then
    val co2 = (SCD40_CO2_1.state as Number).intValue

    if (co2 > 1500) {
        sendBroadcastNotification("⚠️ CO2 MOLTO ALTO: " + co2 + " ppm!")
    } else if (co2 > 1000) {
        sendNotification("myemail@example.com", "CO2 elevato: " + co2 + " ppm")
    }
end
```

### Grafana Integration

Configura persistence per grafici storici:

```javascript
// persistence/influxdb.persist
SCD40_CO2_1, SCD40_Temperature1, SCD40_Humidity1 : strategy = everyChange
SCD40Altitudine1, SCD40_TempOffset1 : strategy = everyUpdate
```

---

## Riferimenti

- [OpenHAB Documentation](https://www.openhab.org/docs/)
- [Matter Device Integration](https://www.openhab.org/addons/bindings/matter/)
- [DSL Rules](https://www.openhab.org/docs/configuration/rules-dsl.html)
- [Sitemaps](https://www.openhab.org/docs/ui/sitemaps.html)

---

## Supporto

Per problemi o domande:
1. Controlla i log: `/var/log/openhab/openhab.log`
2. Verifica la configurazione Matter: Settings → Things
3. Testa i channel raw prima dei proxy
4. Verifica che le regole siano attive: Settings → Rules

---

## Dettagli Tecnici: Cluster CO2 e Roadmap OpenHAB

### Implementazione Corretta

Il dispositivo utilizza il cluster **Carbon Dioxide Concentration Measurement (0x040D)** come specificato nello standard Matter per sensori CO2.

**Vantaggi:**
- ✅ Conforme allo standard Matter
- ✅ Semanticamente corretto (CO2, non CO)
- ✅ Pronto per OpenHAB 5.2+ senza modifiche firmware
- ✅ Nessun workaround o hack

### Supporto OpenHAB

**Timeline del supporto:**
- ❌ **OpenHAB 5.1 e precedenti**: Cluster 0x040D **non supportato**
- ✅ **OpenHAB 5.2+**: Supporto completo cluster CO2 (PR #19897 merged 27/12/2025)

**Su OpenHAB 5.1:**
- Il canale CO2 ppm (endpoint 8) mostrerà **NULL** o non esisterà
- **Fallback funzionante**: Canale Air Quality (endpoint 7) con valori 0-6

**Dopo upgrade a OpenHAB 5.2+:**
1. Il canale CO2 ppm apparirà automaticamente
2. **Non serve riflash** del firmware ESP32
3. Basta ricommissionare il device o riavviare OpenHAB
4. Il canale mostrerà i valori 400-5000 ppm correttamente

### Vantaggi di Questa Scelta

**Invece di un workaround "sporco":**
- ❌ Non usiamo cluster sbagliati (CO invece di CO2)
- ❌ Non confondiamo la semantica dei dati
- ❌ Non creiamo debito tecnico

**Approccio "future-proof":**
- ✅ Codice pulito e standard-compliant
- ✅ Upgrade trasparente a OpenHAB 5.2
- ✅ Nessuna breaking change futura
- ✅ Compatibilità con altri controller Matter (Google Home, Apple Home, ecc.)

### Riferimenti

- [OpenHAB Matter Binding PR #19897](https://github.com/openhab/openhab-addons/pull/19897) - Supporto cluster CO2
- [OpenHAB Matter Binding Documentation](https://www.openhab.org/addons/bindings/matter/)
- [Matter Specification - Cluster 0x040D (CO2)](https://csa-iot.org/developer-resource/specifications-download-request/)

---

**Versione:** 2.1.1
**Data:** 2025-12-30
**Compatibilità:** OpenHAB 4.x/5.x
**Funzionalità:**
- Debouncing intelligente a due stadi (10s + 10s) per protezione EEPROM
- Logging pulito (solo messaggi essenziali)
- Auto-persist automatico per switch e dimmer
**Cluster CO2:** 0x040D (standard Matter - richiede OpenHAB 5.2+)
**Fix:** Risolti errori "Cluster cannot be NULL" nel firmware ESP32
