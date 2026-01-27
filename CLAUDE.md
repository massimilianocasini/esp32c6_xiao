# CLAUDE.md - Istruzioni Globali

## Lingua

**Parla sempre in italiano** nelle risposte e nelle spiegazioni.
I commenti nel codice e i messaggi di log possono rimanere in inglese per compatibilità.

## Progetti in questa directory

### c6_matter_thread_sd40_v1_oled
Dispositivo Matter/Thread con sensore SCD40 CO2 e display OLED per ESP32-C6.
Vedi `c6_matter_thread_sd40_v1_oled/CLAUDE.md` per dettagli specifici.

## Ambiente di sviluppo

```bash
# Attivare l'ambiente ESP-IDF e ESP-Matter
source /opt/esp/idf/export.sh
source /workspace/esp-matter/export.sh
```

## Convenzioni generali

- **Lingua interfaccia utente**: Italiano (display OLED, messaggi utente)
- **Lingua codice**: Inglese (variabili, funzioni, commenti tecnici)
- **Lingua log**: Inglese (per compatibilità con strumenti di debug)
- **Documentazione README**: Inglese (per la community)
- **Comunicazione con Claude**: Italiano
