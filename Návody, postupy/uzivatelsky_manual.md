# Laboratorní napájecí zdroj — Uživatelský manuál

---

## Obsah

1. [Přehled hardware](#1-přehled-hardware)
2. [Tlačítka a ovládací prvky](#2-tlačítka-a-ovládací-prvky)
3. [Displej a zobrazené hodnoty](#3-displej-a-zobrazené-hodnoty)
4. [Režimy provozu](#4-režimy-provozu)
5. [Ovládání výstupu](#5-ovládání-výstupu)
6. [Nastavení kroku enkodéru](#6-nastavení-kroku-enkodéru)
7. [Kalibrační menu](#7-kalibrační-menu)
8. [Ochrana a omezení výkonu](#8-ochrana-a-omezení-výkonu)
9. [IR dálkové ovládání](#9-ir-dálkové-ovládání)

---

## 1. Přehled hardware

| Komponenta | Popis |
|---|---|
| Raspberry Pi Pico (RP2040) | Hlavní řídicí jednotka |
| ST7735S 128×160 LCD | Barevný displej, landscape 160×128 px |
| MCP4728 DAC | 4kanálový D/A převodník, nastavení výstupů |
| MCP23S17 expandér | Vstupní tlačítka a indikační LED |
| Rotační enkodér | Nastavení hodnot, tlačítko = potvrzení |
| IR přijímač | Ovládání dálkovým ovladačem |
| Ventilátor | Chlazení, řízeno teplotou |
| Bzučák | Akustická signalizace |

---

## 2. Tlačítka a ovládací prvky

### Rotační enkodér

| Akce | Funkce |
|---|---|
| Otočení | Změna vybrané hodnoty (dle kroku) |
| Stisk | Potvrzení / přepnutí sloupce v menu |

### Tlačítka na předním panelu

| Tlačítko | Funkce |
|---|---|
| **OUT** | Zapnutí/vypnutí výstupu Source (Vset + Iset) |
| **tlačítko enkodéru** | Potvrzení v menu |
| **VOUT** | Výběr kanálu **Vset** |
| **100x** | Krok enkodéru ×100 (momentary) nebo reset na ×1 (toggle) |
| **IOUT** | Výběr kanálu **Iset** |
| **10x** | Krok enkodéru ×10 (momentary) nebo cyklus ×1→×10→×100 (toggle) |
| **ISINK** | Výběr kanálu **Isink** |
| **MENU** | Vstup do kalibračního menu / výstup z menu |
| **SINK** | Zapnutí/vypnutí výstupu Sink (Isink) |
| **MODE** | Přepnutí režimu zobrazení (Supply+Sink → Supply → Continuity) |


---

## 3. Displej a zobrazené hodnoty

### Barevné kódování

| Barva | Význam |
|---|---|
| Bílá | Normální hodnota |
| Žlutá | Aktivně vybraný kanál (enkodér edituje tuto hodnotu) |
| Zelená | Výstup zapnut / měřená hodnota v normě |
| Červená | Výstup vypnut / chyba |
| Oranžová | Varování / aktivní omezení výkonu |
| Šedá | Výstup nebo kanál je deaktivován |
| Azurová | Záhlaví / informační zpráva |

### Indikátory ve spodní liště

| Zobrazení | Popis |
|---|---|
| `source ON` / `source OFF` | Stav výstupu zdroje (zelená/červená) |
| `sink ON` / `sink OFF` | Stav výstupu zátěže (zelená/červená) |
| `step x1` / `x10` / `x100` | Aktuální krok enkodéru |
| `backlight XX%` | Aktuální jas podsvícení |

### Zprávy a upozornění

V dolní části displeje se zobrazují překryvné zprávy:

| Zpráva | Barva | Popis |
|---|---|---|
| `WARN: Pout limit active` | Oranžová | Překročen maximální výstupní výkon |
| `WARN: Psink limit active` | Oranžová | Překročen maximální výkon Sinku |
| `ERR: Vin too low` | Červená | Příliš nízké vstupní napětí |
| `CONTINUITY: closed` | Oranžová | Detekováno propojení (Continuity) |
| `IR: cmd=0xXX` | Azurová | Přijat kód z IR dálkového ovladače (3 s) |

---

## 4. Režimy provozu

Režimy se přepínají tlačítkem **MODE** — každým stiskem se přejde na další:

```
Supply + Sink  →  Supply  →  Continuity  →  Supply + Sink  →  ...
```

### 4.1 Režim Supply + Sink (výchozí)

Plné zobrazení všech hodnot:

| Levý sloupec (setpointy) | Pravý sloupec (měření) |
|---|---|
| Vset — nastavené napětí | Vout — měřené výstupní napětí |
| Iset — nastavený proud | Iout — měřený výstupní proud |
| Isink — nastavení proudového sinku | Pout — vypočítaný výstupní výkon |
| Tcore — teplota jádra RP2040 | Psink — výkon proudové zátěže |
| Tfan — teplota chladiče  | Isink — měřený proud zátěže |
| | Vin — měřené vstupní napětí |


### 4.2 Režim Supply

Zjednodušené zobrazení pouze pro napájecí zdroj:
- Vout a Iout zobrazeny **velkým písmem** (měřítko ×3)
- Vset, Iset zobrazeny malým písmem nahoře
- Spodní řádek: Pout, Tfan, Vin
- **Isink je při vstupu do tohoto režimu automaticky vypnut**

V tomto režimu jsou přístupné kanály **Vset** a **Iset**.

### 4.3 Režim Continuity (měřič spojitosti)

Určen pro testování propojení (odpory, kabely, pájené spoje):

| Levý sloupec | Pravý sloupec |
|---|---|
| Vset — testovací napětí | Vout — měřené napětí |
| Iset — testovací proud | Iout — měřený proud |
| Thr — práh detekce | |

Pokud naměřené napětí **klesne pod nastavený práh**, zobrazí se `CLOSED` (červeně) a bzučák vydá nepřerušovaný tón. Po překročení prahu se zobrazí `OPEN` (zeleně) a bzučák ztichne.

> **Vset, Iset a práh Thr** v režimu Continuity jsou **oddělené** od hodnot v režimu Supply+Sink — navzájem se neovlivňují a ukládají se do paměti flash nezávisle.

Výběr editovaného parametru:

| Tlačítko | Kanál |
|---|---|
| Vout | Vset |
| Iout | Iset |
| Isink | Thr (práh) |

---

Režim je vhodný pro testování součástek a kontrolu, zda kabel není přerušený. Na rozdíl od multimetru lze měřeným spojem prohnat i několik ampér, takže jde o poctivý test spoje. Lze jej použít i pro zkoušení diod a tranzistorů.

## 5. Ovládání výstupu

### Zapnutí/vypnutí zdroje (napájení)

Tlačítkem **OUT** se přepíná stav výstupu zdroje:
- Zapnuto → `source ON` (zeleně), LED svítí
- Vypnuto → `source OFF` (červeně), LED nesvítí


### Zapnutí/vypnutí zátěže

Tlačítkem **SINK** se přepíná stav zátěže:
- Zapnuto → `sink ON` (zeleně), LED svítí
- Vypnuto → `sink OFF` (červeně), LED nesvítí


## 6. Nastavení kroku enkodéru

Krok enkodéru určuje, o kolik DAC bitů se změní hodnota při jednom „kliknutí".

### Režim Momentary (výchozí)

Krok platí po dobu, kdy je tlačítko fyzicky stisknuto:

| Stav tlačítek | Krok |
|---|---|
| Žádné | ×1 |
| **10x** držen | ×10 |
| **100x** držen | ×100 |
| **100x** + **10x** oba drženy | ×1000 |

### Režim Toggle

Každý stisk tlačítka přepne krok a ten zůstane aktivní i po uvolnění:

| Akce | Výsledek |
|---|---|
| Stisk **10x** | Cyklus: ×1 → ×10 → ×100 → ×1 |
| Stisk **100x** | Reset na ×1 |

Přepínání mezi režimy se provádí v **kalibračním menu** (položka `Step mode`).
V budoucnu se to může změnit.

---

## 7. Kalibrační menu

### Vstup a výstup

- **MENU** — vstup do menu
- **MENU** znovu (uvolnit a znovu stisknout) — výstup **bez uložení**
- **tlačítko enkodéru** na položce `Exit` — výstup bez uložení (změny provedené v menu se zahodí a platí dřívější hodnoty)
- **tlačítko enkodéru** na položce `Save+Exit` — uložení do paměti flash a výstup z menu

### Navigace

| Akce | Funkce |
|---|---|
| Otočení enkodéru (levý sloupec) | Pohyb po položkách menu |
| tlačítko enkodéru | Přepnutí do pravého sloupce (editace hodnoty) |
| Otočení enkodéru (pravý sloupec) | Změna hodnoty |
| tlačítko enkodéru (v pravém sloupci) | Potvrzení a návrat do levého sloupce |

Menu má tři sloupce:
- **Název** — popis parametru
- **Hodnota** — aktuální hodnota (žlutě při editaci, oranžově při aktivní změně)
- **Výchozí** — tovární hodnota (šedě, pouze pro přehled)

### Přehled položek menu

| Položka | Popis | Rozsah |
|---|---|---|
| Exit | Výstup bez uložení | — |
| Save+Exit | Uložení do flash a výstup | — |
| **Auto cal** | Spustí kalibračního průvodce (viz níže) | — |
| **Plim on** | Povolení omezení výkonu | on / off |
| **Pout max W** | Maximální výstupní výkon | 0–200 W |
| **Psink max W** | Maximální výkon sinku | 0–200 W |
| **Vin min V** | Minimální vstupní napětí (mV krok 100) | 0–60 000 mV |
| **Beep** | Režim bzučáku | ALL / ALARM / MUTE |
| **Backlight** | Jas podsvícení | 1–100 % |
| **Step mode** | Režim tlačítek kroku | moment / toggle |
| Vout gain | Zisk pro měření výstupního napětí | 1–999999 |
| Vout offset | Offset pro měření výstupního napětí (mV) | ±9999 |
| Vin gain | Zisk pro měření vstupního napětí | 1–999999 |
| Vin offset | Offset pro měření vstupního napětí (mV) | ±9999 |
| HiI gain | Zisk pro měření proudu sinku | 1–999999 |
| HiI offset | Offset pro měření proudu sinku (mA) | ±9999 |
| Iout gain | Zisk pro měření výstupního proudu | 1–999999 |
| Iout offset | Offset pro měření výstupního proudu (mA) | ±9999 |
| Vset gain | Zisk pro zobrazení nastavení napětí | 1–999999 |
| Vset offset | Offset pro zobrazení nastavení napětí (mV) | ±9999 |
| Iset gain | Zisk pro zobrazení nastavení proudu | 1–999999 |
| Iset offset | Offset pro zobrazení nastavení proudu (mA) | ±9999 |
| Isink gain | Zisk pro zobrazení nastavení sinku | 1–999999 |
| Isink offset | Offset pro zobrazení nastavení sinku (mA) | ±9999 |

### Nastavení bzučáku

| Hodnota | Popis |
|---|---|
| ALL | Pípnutí při každém stisku tlačítka + alarmové události |
| ALARM | Pouze alarmové události (omezení výkonu, continuity) |
| MUTE | Tichý provoz (bzučák zcela vypnut) |

### Auto cal — kalibrační průvodce (doporučeno)

Položka `Auto cal` provede kalibraci krok za krokem: zdroj sám říká, na jaký rozsah přepnout multimetr a jakou hodnotu má ukázat. Zkalibruje se současně nastavení (Vset, Iset) i měření (Vout, Iout), volitelně i vstupní napětí Vin.

**Před spuštěním je nutný dostatečně silný napájecí zdroj (doporučeno ≥ 60 W).** Kompletní postup, rozsahy multimetru, bezpečnostní opatření a ruční kalibraci Isink najdete v [kalibrace.md](kalibrace.md).

### Ruční kalibrace (Gain a Offset) — pokročilé

Průvodce nahrazuje ruční ladění v naprosté většině případů. Ruční úpravu hodnot použijte jen tehdy, když potřebujete jednotlivou hodnotu doladit (například Isink).

Měřená hodnota se počítá takto:

```
zobrazená_hodnota = (raw_mV × gain / divisor) − offset
```

Od tohoto firmwaru je `divisor` (`div`) **10000** (dříve 1000), tedy `gain` je desetkrát větší pro stejné měřítko; například `gain = 110880` odpovídá zisku 11,088. Menu proto umožňuje zadat gain až do 999999. Průvodce vždy ukládá `div = 10000`; dříve uložené hodnoty s `div = 1000` zůstávají platné.

**Postup kalibrace výstupního napětí (Vout):**

1. Přiložte kalibrovaný voltmetr na výstupní svorky.
2. Zapněte Source (**OUT**), nastavte nenulové Vset.
3. Porovnejte zobrazení Vout na displeji s voltmetrem.
4. Vstupte do kalibračního menu (**MENU**).
5. Upravte `Vout gain` a `Vout offset` dokud se hodnoty neshodují.
6. Stejný postup pro ostatní měřené veličiny (Vin, HiI, Iout).
7. Uložte: `Save+Exit`.

> **Tip:** Začněte vždy od výchozích hodnot (výchozí sloupec v menu). Nejprve nastavte `gain` aby bylo měřítko správné, poté `offset` pro korekci nulového bodu.

---

## 8. Ochrana a omezení výkonu

Funkce omezení výkonu musí být povolena (`Plim on = on`).

### Omezení výstupního výkonu (Pout)

Pokud `Vout × Iout > Pout max`, zdroj automaticky snižuje **Vset** o 1 DAC krok za každou iteraci hlavní smyčky, dokud výkon neklesne pod limit. Po poklesu pod limit se Vset obnoví přirozeně — enkodérem.

Zobrazení: hodnota Vset se zobrazí **oranžově**, zpráva `WARN: Pout limit active`.

### Omezení výkonu zátěže (Psink)

Stejný mechanismus pro `Vout × Isink > Psink max` — snižuje se **Isink**.

Zobrazení: Isink setpoint oranžově, zpráva `WARN: Psink limit active`.

### Minimální vstupní napětí (Vin min)

Pokud `Vin < Vin min` (a hodnota je nenulová), dochází k automatickému snižování Vset.

Zobrazení: červená zpráva `ERR: Vin too low - reducing Vset`.

> **Poznámka:** Omezení výkonu jsou gradientní (plynulé snižování) — výstup **není okamžitě vypnut**. Pokud je potřeba okamžité vypnutí, použijte tlačítko **OUT**.

---

## 9. IR dálkové ovládání

Zdroj podporuje IR dálkový ovladač s protokolem NEC (běžné televizní ovladače).

Po přijetí signálu se na displeji zobrazí: `IR: cmd=0xXX` (azurově, 3 sekundy).

### Nastavení příkazů

Příkazy jsou definovány v souboru `peripherals.h` jako konstanty:

```c
#define IR_CMD_VI_TOGGLE   0xFF   /* zapnutí/vypnutí Source */
#define IR_CMD_IS_TOGGLE   0xFF   /* zapnutí/vypnutí Sink   */
#define IR_CMD_SEL_VSET    0xFF   /* výběr kanálu Vset      */
#define IR_CMD_SEL_ISET    0xFF   /* výběr kanálu Iset      */
#define IR_CMD_SEL_ISINK   0xFF   /* výběr kanálu Isink     */
#define IR_CMD_ENC_UP      0xFF   /* enkodér nahoru ×1      */
#define IR_CMD_ENC_DOWN    0xFF   /* enkodér dolů ×1        */
```

`0xFF` znamená deaktivovaný příkaz.

Ovládání zdroje přes IR zatím není implementováno, jde pouze o test příjmu.

---

## Rychlá reference — nejčastější úkony

| Úkon | Postup |
|---|---|
| Nastavit výstupní napětí | Vout (vybrat Vset) → otočit enkodér |
| Nastavit výstupní proud | Iout (vybrat Iset) → otočit enkodér |
| Zapnout výstup | OUT |
| Zapnout zátěž | SINK |
| Změnit krok ×10 (momentary) | Držet **10X** + otáčet enkodérem |
| Změnit krok ×10 (toggle) | Stisknout **10X** jednou |
| Přepnout režim | MODE |
| Vstoupit do kalibrace | MENU |
| Spustit kalibračního průvodce | MENU → položka `Auto cal` → tlačítko enkodéru |
| Uložit kalibraci | MENU → navigovat na `Save+Exit` → Tlačítko enkodéru |
| Opustit kalibraci bez uložení | MENU (znovu stisknout) |
