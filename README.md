# ExpediceMars Altimeter

Výškoměr pro modely raket na **Seeed XIAO ESP32-C6** + **BMP580** (I2C), jedno tlačítko,
napájení z 1S LiPo přímo na BAT piny XIAO.

Zaznamenává **apogeum** a celý **průběh výšky v čase**, data drží v non-volatile paměti
(přežijí reset i odpojení baterie) a nabízí **webové rozhraní** na prohlížení,
pojmenování a export letů. Kód je psaný na co nejnižší spotřebu.

---

## Obsah

- [Zapojení](#zapojení)
- [Instalace v Arduino IDE 2](#instalace-v-arduino-ide-2)
- [Ovládání tlačítkem](#ovládání-tlačítkem)
- [Webové rozhraní](#webové-rozhraní)
- [Jak to funguje](#jak-to-funguje)
- [Odolnost proti resetu](#odolnost-proti-resetu)
- [Spotřeba](#spotřeba)
- [Nastavitelné parametry](#nastavitelné-parametry)
- [Formát dat a API](#formát-dat-a-api)
- [Ověření](#ověření)

---

## Zapojení

| BMP580 | XIAO ESP32-C6 | pozn. |
|---|---|---|
| VCC | 3V3 | **ne 5 V** |
| GND | GND | |
| SDA | D4 (GPIO22) | I2C, 400 kHz |
| SCL | D5 (GPIO23) | |

| Další | XIAO ESP32-C6 |
|---|---|
| Tlačítko | **D0 (GPIO0)** ↔ GND |
| LED | GPIO15 (na desce, svítí v L) |
| Baterie | BAT+ / BAT− (1S LiPo) |

**Proč zrovna D0 pro tlačítko:** GPIO0–GPIO7 jsou na ESP32-C6 v LP (RTC) doméně, takže
dokážou probudit čip z deep sleepu. Zároveň GPIO0 na C6 není strapping pin, takže držení
tlačítka při zapnutí nerozhodí bootování. Pin jde změnit v `config.h`, ale pro buzení
z deep sleepu musí zůstat v rozsahu GPIO0–GPIO7.

Tlačítko nepotřebuje žádné externí součástky (používá se vnitřní pull-up). Pro úplnou
jistotu při deep sleepu můžeš přidat externí **100 kΩ z D0 na 3V3**.

Adresu BMP580 řešit nemusíš — knihovna sama zkusí 0x46 i 0x47.

> **Baterie:** XIAO nemá ochranu proti hlubokému vybití. Použij článek s ochrannou
> destičkou (většina „modelářských“ LiPo článků ji má), jinak ho můžeš při dlouhém
> zapomenutí v režimu ARMED zničit.

### Volitelně

V `config.h` jde zapnout:

- `PIN_BUZZER` — aktivní piezo bzučák. Pípá souběžně s LED, takže raketu v trávě najdeš
  podle vyblikaného/vypípaného apogea.
- `PIN_VBAT` — měření napětí baterie přes vlastní odporový dělič (XIAO ESP32-C6 nemá
  dělič k BAT vyvedený na žádný pin). Např. 2× 220 kΩ mezi BAT+ a GND, střed na A1 (GPIO1).
  **GPIO3 a GPIO14 nepoužívej** — ovládají na XIAO VF přepínač antény.

---

## Instalace v Arduino IDE 2

1. **Deska:** *Soubor → Vlastnosti → Správce dalších desek* přidej
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`,
   pak ve *Správci desek* nainstaluj **esp32 by Espressif Systems** (testováno na 3.3.11).
2. **Knihovna BMP580:** stáhni [Tomasraketak/BMP580-library](https://github.com/Tomasraketak/BMP580-library)
   jako ZIP a dej *Sketch → Přidat knihovnu → Přidat .ZIP knihovnu*.
3. **Vyber desku** `XIAO_ESP32C6`.
4. ⚠️ **Nastav `Tools → Partition Scheme` na „No OTA (2MB APP/2MB SPIFFS)“.**
   Výchozí schéma má jen 1,2 MB na program a sketch se do něj **nevejde**
   (potřebuje ~1,33 MB). Zároveň tím dostaneš ~2 MB na záznamy letů.
5. Nahraj sketch. Souborový systém se při prvním startu založí sám, nic dalšího
   nahrávat nemusíš.

Ostatní volby (CPU frekvence, USB CDC) nech výchozí — frekvenci si nastavuje program sám.

---

## Ovládání tlačítkem

| Akce | Co to udělá |
|---|---|
| **krátký stisk** | probudí výškoměr / vybliká poslední apogeum |
| **dvojklik** | zapne nebo vypne WiFi s webem |
| **držet 3 s** | uloží aktuální výšku jako startovní a **ozbrojí** (po 3 s LED cvrnkne — pak pusť) |
| **držet 8 s** | vypne (deep sleep); probudí se zase stiskem |

Držení delší než 3 s a kratší než 8 s se vyhodnotí **až při puštění**, takže se nedá
omylem přeskočit — LED ti v 3 s a v 8 s dá vědět, kde jsi.

### Řeč LED

| Vzor | Význam |
|---|---|
| krátké bliknutí co 4 s | odzbrojeno (IDLE) |
| dvě bliknutí co 3 s | **ozbrojeno**, čeká na start |
| rychlé blikání | probíhá let |
| velmi rychlé blikání | kalibruje se startovní výška |
| 2× krátce po sobě | akce potvrzena |
| dlouhé–krátké–dlouhé | chyba (např. senzor neodpovídá) |
| bliká číslice po číslici | vyblikává apogeum posledního letu v metrech |

Vyblikání apogea: každá číslice = odpovídající počet bliknutí, nula = jedno dlouhé
bliknutí, mezi číslicemi delší pauza. Např. **137 m** = `•` … `•••` … `•••••••`.

---

## Webové rozhraní

Dvojklikem na tlačítko se zapne WiFi AP:

- **SSID:** `ExpediceMars-XXXX` (XXXX = konec MAC adresy), heslo `raketa1234`
- adresa **http://192.168.4.1** nebo **http://vyskomer.local**
- funguje i captive portál — telefon nabídne přihlášení a stránka se otevře sama

Co web umí:

- živá výška, rychlost, tlak, teplota, stav paměti
- **nastavit startovní výšku + ozbrojit**, odzbrojit, uspat
- seznam letů s **apogeem**, datem, délkou a počtem vzorků
- detail letu: graf výšky v čase s vyznačeným apogeem, odečet hodnot tažením prstu,
  max. rychlost, čas apogea
- **pojmenování letu**, smazání, **export CSV** (i ve variantě pro český Excel)
- nastavení prahů, nadmořské výšky startoviště a přihlašovacích údajů WiFi

Stránka je celá uložená v čipu (AP nemá internet, takže se nic nestahuje z CDN).
Výškoměr nemá hodiny reálného času — čas si bere z prohlížeče při otevření stránky,
takže lety zaznamenané po připojení telefonu mají skutečné datum a čas.

**AP se sám vypne po 5 minutách** bez požadavku, aby zbytečně nežral baterii.

---

## Jak to funguje

```
IDLE ──(3 s / web: Nulovat+ARM)──► ARMED ──(výška > +25 m)──► ASCENT
 ▲                                   ▲                          │
 │                                   │              (pokles > 10 m pod maximum)
 │                                   │                          ▼
 └──(po 5 min)── LANDED ◄──(výška stabilní 5 s)── DESCENT ◄─────┘
```

1. **Startovní výška.** Držením tlačítka 3 s (nebo z webu) se zprůměruje 20 vzorků tlaku
   (~2 s) a uloží se do NVS jako referenční tlak startovní plochy. Reference přežije
   odpojení baterie. Výška se pak počítá barometricky **vůči této referenci**, takže se
   neřeší tlak přepočtený na hladinu moře ani jeho změny během dne.
2. **ARMED.** Měří se 10× za sekundu a drží se **kruhový buffer posledních 15 vzorků**
   (1,5 s historie). Mezi vzorky čip spí (light sleep).
3. **Start letu.** Jakmile jsou **dva vzorky po sobě nad +25 m**, začne se zapisovat.
   Do souboru se nejdřív vysype celý kruhový buffer, takže záznam obsahuje i chvíli
   **před** startem a celý rozjezd.
4. **Apogeum.** Detekuje se, když aktuální výška klesne o **víc než 10 m** pod dosud
   nejvyšší naměřenou hodnotu tohoto letu. Samotné apogeum je to zaznamenané maximum,
   ne hodnota v okamžiku detekce.
5. **Přistání.** Po apogeu se sleduje, jestli se výška udrží v pásmu ±2 m po dobu 5 s.
   Pak se soubor uzavře, dopíše se hlavička se souhrnem a výškoměr se odzbrojí.
   Tvrdý strop je 10 minut záznamu.
6. **LANDED.** 5 minut vybliká apogeum a drží se dostupný pro web, pak přejde do IDLE
   a po dalších 3 minutách nečinnosti usne do deep sleepu.

Výška se filtruje lehkým EMA filtrem (α = 0,4) a v senzoru je zapnutý mírný hardwarový
IIR filtr. Vzorkování senzoru běží na 20 Hz — schválně dvakrát rychleji než odečty,
protože knihovna BMP580 cachuje poslední měření po dobu 1/ODR a při stejné frekvenci
by občas vrátila starý vzorek.

---

## Odolnost proti resetu

Tohle byl hlavní požadavek, takže je to řešené na třech úrovních:

| Co | Kam | Jak často | Nejhorší možná ztráta |
|---|---|---|---|
| vzorky profilu | LittleFS (flash) | splachuje se do flash **každou 1 s** | ~1 s profilu |
| apogeum + stav letu | NVS | **co 250 ms**, když maximum vyroste o ≥ 1 m | ~1 m |
| stav „ozbrojeno“ + reference | NVS | při nastavení | nic |

**Po resetu (brownout, vybitá baterka, cuknutí konektorem) se stane tohle:**

1. Při startu se zjistí, že v NVS je rozepsaný let → záznam **pokračuje ve stejném souboru**.
2. Soubor se projde a zjistí se, co v něm doopravdy je (poslední čas, maximum, max. rychlost).
   Jako maximum se vezme **to lepší** z NVS a ze souboru — NVS má i to, co se nestihlo
   zapsat, soubor zase to, co se do NVS nestihlo promítnout.
3. Časová osa se napojí **za poslední zapsaný vzorek**, takže nikdy neskočí zpátky.
   Do grafu se vloží značka přerušení (v CSV řádek `# --- preruseni zaznamu (reset) ---`).
   Délku výpadku nemá výškoměr jak zjistit (nemá RTC), počítá se s odhadem 300 ms.
4. Let se normálně dokončí, detekuje se apogeum i přistání a soubor se uzavře.
5. Let je ve webu označený štítkem **„obnoveno po resetu“**.

Když se výškoměr **resetuje na rampě** (ozbrojený, let ještě nezačal), naběhne zase
ozbrojený a se stejnou referencí — nemusíš nic mačkat.

Když **nedojde k obnově vůbec** (třeba se vybije baterka a zapneš to až doma), let se
uzavře během ~5 s po zapnutí a apogeum zůstane to poslední naměřené před výpadkem.

---

## Spotřeba

Kde se šetří:

- **light sleep mezi vzorky** — v IDLE i ARMED čip mezi měřeními spí a budí ho časovač
  nebo tlačítko. Tohle je zdaleka největší úspora.
- **WiFi jen na vyžádání** a s automatickým vypnutím, vysílací výkon snížený na 11 dBm.
- **VF přepínač antény** (GPIO3) je napájený jen když jede WiFi.
- **Bluetooth vypnutý**, CPU na 80 MHz místo 160 MHz.
- **BMP580** běží na 20 Hz s OSR ×8/×1, před deep sleepem jde do STANDBY.
- Vzor blikání LED je navržený s krátkými pulzy a light sleep se nikdy neuspí přes
  okamžik, kdy má LED přepnout — vzor tak zůstane přesný.

Odhad spotřeby (**katalogové hodnoty, ne změřené — ověř si měřením na svém kusu**):

| Stav | Odhad | 300mAh 1S LiPo |
|---|---|---|
| deep sleep („vypnuto“) | desítky µA (Seeed uvádí ~15 µA) | měsíce |
| ARMED / čeká na rampě | ~0,7–1 mA | ~2 týdny |
| let (plný běh) | ~25 mA | (trvá minuty) |
| zapnuté WiFi AP | ~80–120 mA | ~3 h |

Při zapojeném USB je odběr výrazně vyšší a light sleep nemusí být tak hluboký — měř
na baterii.

---

## Nastavitelné parametry

Všechno podstatné je v `config.h`:

| Parametr | Výchozí | Co dělá |
|---|---|---|
| `DEF_LAUNCH_ALT_M` | 25 m | práh startu nad startovní plochou |
| `DEF_APOGEE_DROP_M` | 10 m | pokles pod maximum pro detekci apogea |
| `PRELOG_SAMPLES` | 15 | kruhový buffer před startem |
| `SAMPLE_PERIOD_MS` | 100 | perioda vzorkování za letu |
| `LAND_BAND_M` / `LAND_STABLE_MS` | 2 m / 5 s | kdy je to přistání |
| `MAX_FLIGHT_MS` | 10 min | tvrdý strop délky záznamu |
| `LOG_FLUSH_MS` | 1000 | jak často do flash |
| `PROGRESS_SAVE_MS` | 250 | jak často záloha apogea do NVS |
| `IDLE_TO_SLEEP_MS` | 3 min | nečinnost → deep sleep |
| `AP_TIMEOUT_MS` | 5 min | nečinnost webu → vypnout AP |
| `ARMED_TIMEOUT_MS` | 0 | 0 = v ARMED nikdy sám neusne |
| `ALT_EMA_ALPHA` | 0,40 | filtrace výšky (1,0 = žádná) |
| `USE_TEMP_COMPENSATION` | 0 | přepočet podle teploty vzduchu |

První tři a nadmořskou výšku startoviště jde měnit i **z webu za běhu** (ukládají se do NVS).

---

## Formát dat a API

Každý let = jeden soubor `/fNNNN.dat` na LittleFS:

```
[hlavička 44 B][vzorek 8 B][vzorek 8 B]...

hlavička: magic "EAM1" (u32), verze (u8), příznaky (u8), id (u16), unixový čas (u32),
          referenční tlak (f32), n.m. startoviště (f32), apogeum (f32), čas apogea (u32),
          délka (u32), max. rychlost (f32), počet vzorků (u32), rezerva (u32)
vzorek:   čas od začátku letu v ms (u32), výška nad startem v m (f32)
```

Příznaky: `0x01` = let řádně uzavřen, `0x02` = záznam pokračoval po resetu.
Vzorek s výškou `NaN` je značka přerušení záznamu. Případný název letu je vedle
v `/fNNNN.nam`, takže přejmenování nikdy nesáhne na naměřená data.

REST API (hodí se na vlastní skripty):

| Endpoint | |
|---|---|
| `GET /api/status` | živý stav |
| `GET /api/flights` | seznam letů |
| `GET /api/flight?id=N&max=800` | profil (plochá řada `[t0,a0,t1,a1,…]`, `null` = přerušení) |
| `GET /api/csv?id=N[&dec=,]` | export CSV |
| `POST /api/name?id=N` | tělo = nový název |
| `POST /api/zero` · `/api/arm?on=0\|1` · `/api/sleep` | akce |
| `POST /api/settings?padMsl=&launchM=&apoDrop=&ssid=&pass=` | nastavení |
| `POST /api/delete?id=N` · `/api/deleteall?confirm=1` | mazání |

Stažení všech letů z počítače připojeného k AP:

```bash
for id in $(curl -s http://192.168.4.1/api/flights | grep -o '"id":[0-9]*' | cut -d: -f2); do
  curl -s -o "let$id.csv" "http://192.168.4.1/api/csv?id=$id"
done
```

---

## Ověření

Co je hotové a ověřené:

- **Překlad** pro `XIAO_ESP32C6`, esp32 core 3.3.11, `-Wall -Wextra` bez jediného warningu.
  Zabere 1 330 kB programu (63 % z 2 MB) a 48 kB RAM.
- **Letová logika** byla prohnaná simulátorem na PC, který pouští **skutečný stavový
  automat** z `ExpediceMarsAltimeter.ino` proti simulované trajektorii (motor 0,8 s,
  apogeum 234,4 m, výsadek 5 m/s). Ověřeno:
  - normální let — apogeum sedí na 0,1 m, záznam začíná 15 vzorky z kruhového bufferu
    (z toho 7 ještě na zemi), průměrná perioda 99 ms, přistání detekováno 5 s po dosednutí;
  - **reset ve stoupání** i **reset těsně po apogeu** — záznam pokračuje ve stejném
    souboru, apogeum vyjde v obou případech přesně 234,4 m, časová osa nikdy neskočí
    zpátky, v záznamu je právě jedna značka přerušení;
  - **reset na rampě** — naběhne zase ozbrojený a let se zaznamená kompletní;
  - **výpadek baterie a zapnutí až za 5 minut** — let se uzavře do ~5 s a apogeum
    zůstane to poslední naměřené.
- **Web** — JSON odpovědi zvalidované, stránka se parsuje bez chyb, všech 41 ID použitých
  v JS existuje v HTML, graf otestován i na hraničních datech (přerušení, samé nuly,
  prázdný záznam, záporné výšky).

Co **není** ověřené a chce to reálný test: skutečná spotřeba, chování BMP580 při
vibracích a chování v opravdové raketě. Doporučuju první let udělat „nasucho“ —
ozbrojit, vynést výškoměr na balkon nebo vyjet výtahem a zkontrolovat, že se let
zaznamenal a graf sedí.
