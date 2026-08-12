# Roadmap

## Bug fix

- [ ] **LED Router — pattern errato** (`thread_status_led_task`)
  Il codice produce 1000ms ON + 250ms OFF, indistinguibile dall'End Device (solid ON).
  Correggere a 250ms ON + 750ms OFF come da documentazione.

- [ ] **Dead code in `app_attribute_update_cb`**
  Il controllo `if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id)`
  appare due volte di seguito. Unificare in un unico blocco.

## Miglioramenti

- [ ] **Debounce ingressi GPIO**
  Il `gpio_input_task` rileva il cambio di stato ma non ha debounce. Un contatto rimbalzante
  può generare più eventi Matter per una singola chiusura fisica. Aggiungere almeno
  2-3 letture stabili consecutive prima di notificare.

- [ ] **OLED indipendente dal ciclo SCD40**
  L'aggiornamento del display è agganciato alla lettura del sensore (ogni 5s).
  Se SCD40 è scollegato, l'OLED si congela. Separare in un task dedicato.

- [ ] **Backoff su recovery I2C fallita**
  Quando la reinizializzazione del bus I2C fallisce, il contatore errori viene comunque
  azzerato. Questo causa un nuovo tentativo ogni 3 letture invece di attendere più a lungo.
  Introdurre un backoff progressivo.

## Refactoring

- [ ] **Helper NVS generico**
  `nvs_save_altitude` e `nvs_save_temp_offset` hanno struttura identica.
  Estrarre un helper `nvs_save_value()` per ridurre la duplicazione.

## Funzionalità future

- [ ] **Configurare il server NTP locale da menuconfig**
  Attualmente l'indirizzo IPv6 del server NTP è un `#define` nel codice.
  Spostarlo in `Kconfig.projbuild` per renderlo configurabile da `menuconfig`
  senza modificare il sorgente.

- [ ] **Selezione antenna via Matter (runtime)**
  Attualmente la selezione antenna (interna/esterna) è un `#define` a compile-time.
  Esporre un endpoint Matter On/Off per cambiarlo a runtime e salvare la preferenza in NVS.

- [ ] **Visualizzazione commissioning QR sull'OLED**
  Il codice chiama già `oled_show_commissioning_info()` ma la funzione mostra solo
  il codice numerico. Valutare se aggiungere il QR code grafico (richiede font bitmap).
