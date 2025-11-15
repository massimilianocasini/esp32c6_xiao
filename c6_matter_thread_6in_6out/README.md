# ESP32C6 XIAO Matter over Thread Device

Implementazione completa di un dispositivo Matter over Thread per XIAO ESP32C6 con controllo GPIO.

## Caratteristiche

- **GPIO0 (D0)**: Uscita digitale ON/OFF controllabile via Matter
- **GPIO1 (D1)**: Ingresso digitale per controllo locale (con pull-up interno)
- **GPIO9 (BOOT)**: Pulsante per reset factory (tieni premuto 5 secondi)
- **Protocollo**: Matter over Thread
- **Network**: Thread mesh network
- **Commissioning**: Supporto QR code e manual pairing code

## Requisiti

### Software
- ESP-IDF v5.1 o superiore
- ESP-Matter SDK
- Python 3.8+

### Hardware
- XIAO ESP32C6
- (Opzionale) LED su GPIO0
- (Opzionale) Pulsante su GPIO1

## Installazione Environment

```bash
# 1. Installa ESP-IDF
cd ~/esp
git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c6
. ./export.sh

# 2. Installa ESP-Matter
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
./install.sh
. ./export.sh

# 3. Configura il target
cd ~/esp/matter_light_switch
idf.py set-target esp32c6
```

## Compilazione

```bash
# Pulisci build precedenti
idf.py fullclean

# Configura il progetto
idf.py menuconfig

# Compila
idf.py build
```

## Flash del Firmware

```bash
# Flash del firmware
idf.py -p /dev/ttyACM0 flash

# Monitora l'output seriale
idf.py -p /dev/ttyACM0 monitor
```

Nota: Su Windows usa `COM3` o la porta appropriata invece di `/dev/ttyACM0`

## Utilizzo

### Commissioning

All'avvio, il dispositivo stamperà sulla seriale i codici di commissioning:

```
╔════════════════════════════════════════════════════════════╗
║                  COMMISSIONING CODES MATTER                 ║
╠════════════════════════════════════════════════════════════╣
║ Manual Pairing Code:                                        ║
║ 34970112332                                                 ║
╠════════════════════════════════════════════════════════════╣
║ QR Code:                                                    ║
║ MT:Y.K9042C00KA0648G00                                      ║
╚════════════════════════════════════════════════════════════╝
```

Usa questi codici nell'app del tuo ecosistema Matter (Google Home, Apple Home, Amazon Alexa, etc.)

### Controllo Locale

- **GPIO1**: Premi il pulsante per toggleare lo stato ON/OFF localmente
- Lo stato viene sincronizzato automaticamente con Matter

### Reset Factory

- Tieni premuto il pulsante BOOT (GPIO9) per 5 secondi
- Il dispositivo si resetterà ai valori di fabbrica
- Dovrai rifare il commissioning

## Configurazione Thread Network

Il dispositivo usa questi parametri di default (modificabili in `sdkconfig.defaults`):

- **Network Name**: MatterThread
- **Channel**: 15
- **PAN ID**: 0x1234

## GPIO Mapping XIAO ESP32C6

| Funzione | GPIO | Pin XIAO |
|----------|------|----------|
| Output   | 0    | D0       |
| Input    | 1    | D1       |
| Reset    | 9    | BOOT     |
| UART TX  | 16   | TX       |
| UART RX  | 17   | RX       |

## Debugging

### Log Level
Modifica in `sdkconfig.defaults`:
```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_LOG_DEFAULT_LEVEL=4
```

### Monitor Thread Status
```bash
# Nella console seriale
> thread state
> thread channel
> thread networkname
```

### Test GPIO
```bash
# Output test
> gpio set 0 1  # ON
> gpio set 0 0  # OFF

# Input test  
> gpio get 1
```

## Struttura del Progetto

```
matter_light_switch/
├── main/
│   ├── app_main.cpp       # Applicazione principale
│   ├── app_reset.cpp      # Gestione reset
│   ├── app_reset.h        # Header reset
│   ├── app_priv.h         # Header privato
│   └── CMakeLists.txt     # Build configuration
├── CMakeLists.txt         # Project configuration
├── partitions.csv         # Partition table
├── sdkconfig.defaults     # Default configuration
└── README.md             # Questo file
```

## Ottimizzazioni per ESP32C6

- Radio Thread nativa (802.15.4)
- Hardware acceleration per crypto (AES, SHA, ECC)
- Partizioni ottimizzate per 4MB flash
- Low power mode ready (configurabile)

## Troubleshooting

### Errore "No Thread dataset"
- Il dispositivo deve essere commissionato tramite un Border Router Thread
- Assicurati di avere un Thread Border Router nella rete

### GPIO non risponde
- Verifica i collegamenti hardware
- Controlla i log per errori di configurazione GPIO

### Commissioning fallisce
- Verifica che il Thread Border Router sia attivo
- Controlla che il canale Thread sia corretto
- Prova un reset factory

## Licenza

Questo progetto usa le librerie ESP-IDF e ESP-Matter sotto le rispettive licenze Apache 2.0.

## Supporto

Per problemi specifici del XIAO ESP32C6, consulta:
- [Documentazione Seeed Studio](https://wiki.seeedstudio.com/XIAO_ESP32C6_Getting_Started/)
- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [Thread Group](https://www.threadgroup.org/)
