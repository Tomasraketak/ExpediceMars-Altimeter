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
| Tlačítko SW1 | **D1 (GPIO1)** ↔ GND, R1 10 kΩ pull-up na 3V3 |
| LED | GPIO15 (na desce, svítí v L) |
| Baterie | BAT+ / BAT− (1S LiPo) |

**Proč D1 pro tlačítko:** GPIO0–GPIO7 jsou na ESP32-C6 v LP (RTC) doméně, takže
dokážou probudit čip z deep sleepu. GPIO1 na C6 není strapping pin, takže držení
tlačítka při zapnutí nerozhodí bootování. Pin jde změnit v `config.h`, ale pro buzení
z deep sleepu musí zůstat v rozsahu GPIO0–GPIO7.

Na desce je externí pull-up R1 10 kΩ z D1 na 3V3 (navíc se zapíná i vnitřní pull-up).
Při stisku teče přes R1 ~0,33 mA — jen po dobu držení, v klidu nic.

Adresu BMP580 řešit nemusíš — knihovna sama zkusí 0x46 i 0x47.

> **Baterie:** XIAO nemá ochranu proti hlubokému vybití. Použij článek s ochrannou
> destičkou (většina „modelářských“ LiPo článků ji má), jinak ho můžeš při dlouhém
> zapomenutí v režimu ARMED zničit.

### Volitelně

V `config.h` jde zapnout:

- `PIN_BUZZER` — aktivní piezo bzučák. Pípá souběžně s LED, takže raketu v trávě najdeš
  podle vyblikaného/vypípaného apogea.
- `PIN_VBAT` — měření napětí baterie přes vlastní odporový dělič (XIAO ESP32-C6 nemá
  dělič k BAT vyvedený na žádný pin). Např. 2× 220 kΩ mezi BAT+ a GND, střed na A2 (GPIO2)
  (A1/GPIO1 už je tlačítko).
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


### Nejde nahrát nový kód („přístup odepřen“, exit status 2)

ESP32-C6 má USB přímo v čipu. Starší verze firmwaru čip uspávala i s připojeným USB,
port se pak odpojoval a Windows hlásily *přístup odepřen*. Od této verze se při
připojeném USB nespí. Na desce se starým firmwarem (nebo když je v deep sleepu) jednou
použij **ruční bootloader**:

1. Zavři Sériový monitor / Plotter a jiné programy, které můžou držet COM port.
2. Odpoj USB, **drž tlačítko B (BOOT)** na XIAO, připoj USB a BOOT pusť
   (nebo: drž BOOT, krátce stiskni R (RESET), pusť BOOT).
3. V *Nástroje → Port* vyber port, který se teď objevil (může mít jiné číslo COM).
4. Nahraj. Po nahrání stiskni **RESET**.

---

## Ovládání tlačítkem

| Akce | Co to udělá |
|---|---|
| **krátký stisk** | probudí výškoměr / vybliká poslední apogeum |
| **dvojklik** | zapne nebo vypne WiFi s webem (volba se pamatuje, výchozí = zapnuto) |
| **držet 3 s** | uloží aktuální výšku jako startovní a **ozbrojí** (po 3 s LED cvrnkne — pak pusť) |
| **držet 8 s** | vypne (deep sleep); probudí se zase stiskem. Od ~3,3 s LED bliká a **čím dál rychleji**, jak se blíží vypnutí; v 8 s svítí trvale — pak pusť |

Držení delší než 3 s a kratší než 8 s se vyhodnotí **až při puštění**, takže se nedá
omylem přeskočit — LED ti v 3 s cvrnkne a pak zrychlujícím blikáním ukazuje, jak blízko
je vypnutí. Pustíš-li tlačítko během blikání (3–8 s), výškoměr se ozbrojí.

### Testovací let

Ve webu je tlačítko **Testovací let**. Nastaví současnou výšku jako startovní a **hned**
spustí záznam — práh startu (+10 m) se přeskočí. Prvních **10 s** je podmínka konce
záznamu zablokovaná, potom se nevyžaduje apogeum: záznam skončí, jakmile je výška
< 25 m a mění se pomaleji než 1 m/s po dobu 1 s. Na stole tedy skončí za ~11 s,
s výškoměrem v ruce nebo ve výtahu tak dlouho, dokud se hýbeš. Let je ve webu označený
štítkem **„test“** (v hlavičce souboru příznak `0x04`). Během záznamu web neodpovídá.

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

WiFi AP je **po zapnutí defaultně zapnuté** a zůstane zapnuté, dokud ho nevypneš
(dvojklikem tlačítka nebo tlačítkem *Vypnout WiFi* ve webu). Volba se pamatuje i přes
vypnutí a reset; dvojklikem se zase zapne.

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

**AP se samo nevypíná.** Pozor na spotřebu: se zapnutou WiFi bere výškoměr ~80–120 mA,
takže 300mAh článek vydrží zhruba 3 h a výškoměr se sám neuspí. Když ho necháváš
dlouho na rampě, WiFi vypni. (Automatické vypnutí jde vrátit v `config.h`:
`AP_TIMEOUT_MS`, a výchozí stav `DEF_WIFI_ON`.)

Ve webu je v patičce **verze firmwaru a datum, kdy byl přeložen** (např.
`fw 1.1.0 (2026-11-08 14:05)`). **Napětí baterie** se zobrazí jen s osazeným děličem
(viz `PIN_VBAT`), jinak je tam „neměřeno“.

---

## Jak to funguje

```
IDLE ──(3 s / web: Nulovat+ARM)──► ARMED ──(výška > +10 m)──► ASCENT
 ▲                                   ▲                          │
 │                                   │              (pokles > 10 m pod maximum)
 │                                   │                          ▼
 └──(po 5 min)── LANDED ◄──(< 25 m a < 1 m/s po 1 s)── DESCENT ◄┘
```

1. **Startovní výška.** Držením tlačítka 3 s (nebo z webu) se zprůměruje 20 vzorků tlaku
   (~2 s) a uloží se do NVS jako referenční tlak startovní plochy. Reference přežije
   odpojení baterie. Výška se pak počítá barometricky **vůči této referenci**, takže se
   neřeší tlak přepočtený na hladinu moře ani jeho změny během dne.
2. **ARMED.** Měří se 10× za sekundu a drží se **kruhový buffer posledních 5 vzorků**
   (0,5 s historie). Mezi vzorky čip spí (light sleep).
3. **Start letu.** Jakmile jsou **dva vzorky po sobě nad +10 m**, začne se zapisovat —
   výška **každých 100 ms** do flash. Do souboru se nejdřív vysype celý kruhový buffer,
   takže záznam obsahuje i první okamžiky letu **před** překročením prahu.
4. **Apogeum.** Detekuje se, když aktuální výška klesne o **víc než 10 m** pod dosud
   nejvyšší naměřenou hodnotu tohoto letu. Samotné apogeum je to zaznamenané maximum,
   ne hodnota v okamžiku detekce.
5. **Konec záznamu.** Jen **po apogeu**: výška je **pod 25 m** a zároveň se mění
   (klesá i stoupá) **pomaleji než 1 m/s**, a to **nepřetržitě aspoň 1 s**. Pak se soubor
   uzavře, dopíše se hlavička se souhrnem a výškoměr se odzbrojí.
   Tvrdý strop je 10 minut záznamu.
6. **Web.** Každý let jde ve webu zobrazit v grafu (výška v čase) a stáhnout jako CSV
   (`Export CSV`, nebo `CSV pro Excel` s desetinnou čárkou).
7. **LANDED.** 5 minut vybliká apogeum a drží se dostupný pro web, pak přejde do IDLE
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
| apogeum + stav letu | NVS | **co 250 ms**, když maximum vyroste o ≥ 0,1 m; při detekci apogea vždy | ~0,1 m / ~350 ms |
| poslední apogeum (po přistání) | NVS | při uzavření letu | nic |
| stav „ozbrojeno“ + reference | NVS | při nastavení | nic |

**Po resetu (brownout, vybitá baterka, cuknutí konektorem) se stane tohle:**

1. Při startu se zjistí, že v NVS je rozepsaný let → záznam **pokračuje ve stejném souboru**.
2. Soubor se projde a zjistí se, co v něm doopravdy je (poslední čas, maximum, max. rychlost).
   Pokud apogeum ještě nebylo detekované, vezme se jako maximum **to lepší** z NVS a ze
   souboru. Pokud už bylo, platí hodnota z NVS — při detekci apogea se tam zapisuje vždy
   a od té chvíle se maximum **zmrazí** (nic naměřeného po apogeu ho už nepřepíše).
   Kdyby soubor letu chyběl nebo byl poškozený, let se nezahodí — pokračuje se jen
   s apogeem z NVS (bez profilu).
3. Časová osa se napojí **za poslední zapsaný vzorek**, takže nikdy neskočí zpátky.
   Do grafu se vloží značka přerušení (v CSV řádek `# --- preruseni zaznamu (reset) ---`).
   Délku výpadku nemá výškoměr jak zjistit (nemá RTC), počítá se s odhadem 300 ms.
4. Let se normálně dokončí, detekuje se apogeum i přistání a soubor se uzavře.
5. Let je ve webu označený štítkem **„obnoveno po resetu“**.

Když se výškoměr **resetuje na rampě** (ozbrojený, let ještě nezačal), naběhne zase
ozbrojený a se stejnou referencí — nemusíš nic mačkat.

Když **nedojde k obnově hned** (při dopadu se vypojí baterka a zapneš to až doma), let se
uzavře zhruba 1 s po zapnutí (pokud je výška vůči startovišti pod 25 m, jinak až po 10min limitu) a apogeum zůstane to naměřené před výpadkem. Díky zmrazení
maxima po apogeu ho nepřepíše ani „falešná výška“ způsobená jinou nadmořskou výškou
nebo změnou počasí doma.

**Nepřečtené apogeum:** dokud apogeum někdo neviděl (krátký stisk tlačítka nebo nové
ozbrojení), výškoměr ho po každém zapnutí / resetu rovnou bliká (režim LANDED, 5 min).
Reset po přistání ani vypojená baterka tak o výsledek nepřipraví.

---

## Spotřeba

Kde se šetří:

- **light sleep mezi vzorky** — v IDLE i ARMED čip mezi měřeními spí a budí ho časovač
  nebo tlačítko. Tohle je zdaleka největší úspora.
- **WiFi** je defaultně zapnutá (dá se vypnout), vysílací výkon snížený na 11 dBm.
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
| `DEF_LAUNCH_ALT_M` | 10 m | práh startu nad startovní plochou |
| `DEF_APOGEE_DROP_M` | 10 m | pokles pod maximum pro detekci apogea |
| `PRELOG_SAMPLES` | 5 | kruhový buffer před startem |
| `SAMPLE_PERIOD_MS` | 100 | perioda vzorkování za letu |
| `LAND_ALT_M` / `LAND_SPEED_MS` / `LAND_STABLE_MS` | 25 m / 1 m/s / 1 s | konec záznamu (jen po apogeu) |
| `TEST_LAND_BLOCK_MS` | 10 s | testovací let: jak dlouho je konec zablokovaný |
| `MAX_FLIGHT_MS` | 10 min | tvrdý strop délky záznamu |
| `LOG_FLUSH_MS` | 1000 | jak často do flash |
| `PROGRESS_SAVE_MS` | 250 | jak často záloha apogea do NVS |
| `IDLE_TO_SLEEP_MS` | 3 min | nečinnost → deep sleep |
| `AP_TIMEOUT_MS` | 0 (nikdy) | nečinnost webu → vypnout AP |
| `DEF_WIFI_ON` | 1 | WiFi po zapnutí zapnutá, dokud ji uživatel nevypne |
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

Příznaky: `0x01` = let řádně uzavřen, `0x02` = záznam pokračoval po resetu, `0x04` = testovací let.
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
> Pozn.: ověření níže proběhlo na původní verzi (práh 25 m, buffer 15 vzorků, přistání
> ±2 m / 5 s). Aktuální verze (10 m, 5 vzorků, konec < 25 m a < 1 m/s po 1 s) zatím
> přeložená ani simulovaná nebyla.

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
