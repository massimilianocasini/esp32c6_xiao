# Quick Start - Configurazione OpenHAB per SCD40

Guida rapida in 5 minuti per configurare OpenHAB con valori intuitivi (metri e °C).

## 1. Copia i File (2 minuti)

```bash
cd /path/to/c6_matter_thread_sd40_v1/openhab

# Copia tutti i file
sudo cp scd40.items /etc/openhab/items/
sudo cp scd40.rules /etc/openhab/rules/
sudo cp scd40.sitemap /etc/openhab/sitemaps/
sudo cp airquality.map /etc/openhab/transform/

# Imposta permessi
sudo chown -R openhab:openhab /etc/openhab/items/scd40.items
sudo chown -R openhab:openhab /etc/openhab/rules/scd40.rules
sudo chown -R openhab:openhab /etc/openhab/sitemaps/scd40.sitemap
sudo chown -R openhab:openhab /etc/openhab/transform/airquality.map
```

## 2. Trova i Channel ID (1 minuto)

1. Apri OpenHAB: `http://your-ip:8080`
2. Settings → Things → [SCD40 Device] → Channels
3. Copia i channel ID (es. `matter:device:bridge:74a3b12f_endpoint15_brightness`)

## 3. Configura gli Item (2 minuti)

Apri `/etc/openhab/items/scd40.items` e sostituisci **solo questi 2 righe** con i tuoi channel ID:

```javascript
// CERCA QUESTE RIGHE:
Dimmer SCD40Altitudine1_Raw "Altitudine Raw [%d]" (gSCD40)
    { channel="matter:XXXXX:XXXXX:endpoint15_brightness" }

Dimmer SCD40_TempOffset1_Raw "Offset Temperatura Raw [%d]" (gSCD40)
    { channel="matter:XXXXX:XXXXX:endpoint16_brightness" }

// SOSTITUISCI matter:XXXXX:XXXXX con il tuo channel ID reale
// Esempio:
Dimmer SCD40Altitudine1_Raw "Altitudine Raw [%d]" (gSCD40)
    { channel="matter:device:bridge:a1b2c3d4_endpoint15_brightness" }
```

**IMPORTANTE:** Lascia tutti gli altri item come sono! Modifichi solo questi 2.

## 4. Verifica (30 secondi)

Aspetta che OpenHAB carichi i nuovi file (max 30 secondi) oppure:

```bash
sudo systemctl restart openhab
```

Poi vai su: `http://your-ip:8080/basicui/app?sitemap=scd40`

## 5. Usa i Valori Intuitivi! 🎉

Adesso puoi:

### Impostare Altitudine in METRI (0-5000):
```javascript
SCD40Altitudine1.sendCommand(325)  // 325 metri
```

### Impostare Offset in GRADI (0-10):
```javascript
SCD40_TempOffset1.sendCommand(4.5)  // 4.5°C
```

---

## Verifica Rapida

Testa che funzioni dalla console Karaf:

```bash
ssh -p 8101 openhab@localhost  # password: habopen

# Verifica item proxy
openhab> items list | grep SCD40Altitudine1
openhab> items list | grep SCD40_TempOffset1

# Testa un comando
openhab> send SCD40Altitudine1 325
openhab> send SCD40_TempOffset1 4.5

# Verifica lo stato
openhab> items SCD40Altitudine1 state
openhab> items SCD40_TempOffset1 state
```

Se vedi i valori in metri e °C → **FUNZIONA!** ✅

---

## Se Non Funziona

1. **Controlla i log:**
   ```bash
   tail -f /var/log/openhab/openhab.log | grep SCD40
   ```

2. **Verifica che gli item Raw ricevano valori:**
   ```bash
   openhab> items SCD40Altitudine1_Raw state
   openhab> items SCD40_TempOffset1_Raw state
   ```
   Dovrebbero essere numeri 0-254, non NULL.

3. **Verifica le regole:**
   ```bash
   openhab> rules list | grep SCD40
   ```
   Dovrebbero esserci 6 regole attive.

---

## Tabella di Conversione Rapida

| **Metri** | **Dimmer** | **°C** | **Dimmer** |
|-----------|-----------|--------|-----------|
| 0         | 0         | 0.0    | 0         |
| 100       | 5         | 1.0    | 25        |
| 325       | 17        | 2.5    | 64        |
| 500       | 25        | 4.0    | 102       |
| 850       | 43        | 4.5    | 114       |
| 1000      | 51        | 5.0    | 127       |
| 2000      | 102       | 7.5    | 191       |
| 5000      | 254       | 10.0   | 254       |

---

**Fatto!** 🚀 Ora hai il controllo diretto in metri e gradi Celsius!

Per dettagli completi vedi: [README.md](README.md)
