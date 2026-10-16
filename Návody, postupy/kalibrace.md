# Kalibrace

Kalibrace určuje, jak firmware převádí surové hodnoty z ADC na napětí a proud a jak zobrazuje nastavené hodnoty (Vset, Iset). Existují dva způsoby:

1. **Kalibrační průvodce `Auto cal`** (doporučeno) — zdroj vede uživatele krok za krokem.
2. **Ruční úprava gain/offset** v kalibračním menu (pokročilé; nutné pro Isink).

---

## 1. Co budete potřebovat

- Multimetr s rozsahy DC V (až 200 V) a DC A/mA (nejlépe i 20 A).
- Silný napájecí zdroj pro vstup zdroje (viz níže).
- Krátké a dostatečně tlusté kabely pro měření proudu.

### Dostatečně silný vstupní zdroj

**Kalibrujte jen se zdrojem či adaptérem, který zvládne plný výkon** — doporučeno **≥ 60 W**, USB-C PD, nebo XT60 z akumulátoru či laboratorního zdroje. Slabý adaptér způsobí pokles vstupního napětí pod zátěží, výstup poklesne a kalibrace bude špatná, hlavně v bodech MAX.

Průvodce to hlídá:

- na úvodní obrazovce zobrazí aktuální Vin a při **Vin < 12 V** nedovolí pokračovat,
- během každého kroku sleduje Vin; pokud klesne o více než 10 % proti hodnotě na začátku kroku nebo pod minimum nastavené v menu (`Vin min V`), krok se zruší, výstup se vypne, zobrazí se „Input supply too weak" a bod se nezapočítá. Krok pak můžete opakovat.

---

## 2. Kalibrační průvodce (`Auto cal`)

Spuštění: **MENU** → položka `Auto cal` → tlačítko enkodéru.

> **Pokud se zdroj během kalibrace vypne, nic se neuloží. Po zapnutí platí původní kalibrace a průvodce je potřeba spustit znovu.**

Do paměti flash se zapisuje **jednou, až po závěrečném potvrzení** „Save? ENC=yes". Žádné mezivýsledky se průběžně neukládají. Riziko zůstává jen během samotného zápisu (desítky milisekund); při výpadku v této chvíli nesedí kontrolní součet a po zapnutí se načtou tovární hodnoty.

### Ovládání v průvodci

| Tlačítko | Akce |
|---|---|
| otáčení enkodéru | krok ADJUST: mění nastavení výstupu; krok MAX: mění zadávané číslo |
| **10x** / **100x** | násobič kroku (stejně jako v běžném provozu) |
| tlačítko enkodéru | potvrdit krok |
| **MODE** | přeskočit krok (bod se do výpočtu nezapočte) |
| **OUT** | nouzově vypnout/zapnout výstup (pozastavení kroku) |
| **MENU** | zrušit celý průvodce; původní kalibrace i nastavení zůstanou |

### Úvodní upozornění

```
! WARNING !
Use a strong input supply
(XT60 / USB-C PD, >=60 W).
Weak supply = wrong cal!
Do not power off during
calibration.
ENC=continue MENU=quit
```

Pokračujte tlačítkem enkodéru, **MENU** průvodce ukončí.

### Dva typy kroků

- **ADJUST** — točíte enkodérem, dokud multimetr neukáže cílovou hodnotu, a potvrdíte tlačítkem enkodéru. Skutečná hodnota se rovná cíli.
- **MAX** — zdroj nastaví maximum a vy enkodérem zadáte číslo, které ukazuje multimetr. Tak se využije celý rozsah zdroje.

V každém bodě se zároveň kalibruje **nastavení** (Vset / Iset) i **měření** (Vout / Iout).

### Část 1 — Napětí

Svorky nechte naprázdno, multimetr (DC V) připojte paralelně na výstupní svorky. Proudový limit Iset nastaví průvodce na bezpečných cca 20 mA (mírný nabíjecí proud výstupních kondenzátorů).

| Krok | Typ | Multimetr | Cíl |
|---|---|---|---|
| V1 | ADJUST | 2 V | 1,800 V |
| V2 | ADJUST | 20 V | 18,00 V |
| V3 | MAX | 200 V | Vset na maximu (cca 35 V); zadáte naměřenou hodnotu (krok 10 mV) — časově omezený krok, viz níže |

### Část 2 — Proud

Svorky zkratujte přes ampérmetr (zdroj tím přejde do omezení proudu). Napětí Vset nastaví průvodce na pevných cca 3 V. Před každým přepojením multimetru na jinou zdířku je výstup vypnutý a průvodce vás vyzve k přepojení.

| Krok | Typ | Multimetr | Cíl |
|---|---|---|---|
| I1 | ADJUST | 20 mA | 18,0 mA |
| I2 | ADJUST | 200 mA | 180 mA |
| I3 | ADJUST | 2 A (nemá-li multimetr, 20 A) | 1,800 A |
| I4 | MAX | 20 A | Iset na maximu; zadáte naměřenou hodnotu (krok 1 mA) |

Před **každým krokem** průvodce zobrazí výzvu **„CHANGE METER RANGE"** s požadovaným rozsahem multimetru (výstup je během toho vypnutý) a pípne.

Kroky **V3 (maximální napětí)** a **I4 (maximální proud)** jsou časově omezené, aby zdroj neběžel dlouho na maximu:

1. Po stisku tlačítka enkodéru se výstup zapne (u V3 se napětí plynule zvyšuje z nuly během asi 1,5 s).
2. Asi 2 s se čeká na ustálení, pak se asi 1 s průměruje ADC.
3. Zobrazí se **„READ METER NOW!"** a odpočet; **výstup se vypne nejpozději po 5 s** (20A rozsahy multimetrů bývají časově omezené a lineární stupeň se hřeje).
4. Naměřenou hodnotu zadáváte až po vypnutí výstupu.

Omezení výkonu (Plim) zůstává aktivní. Pokud zasáhne, krok se zruší a zobrazí se upozornění „Power limit hit!".

### Část 3 — Vin (volitelná)

Změřte multimetrem vstupní napětí na vstupu (XT60 / USB-C) a zadejte ho. Upraví se jen gain Vin (offset zůstane). Část lze přeskočit tlačítkem **MODE**.

### Výpočet

Pro každý kanál (Vout + Vset, Iout + Iset) se z naměřených bodů vypočte přímka *skutečná = a · x + b* metodou **váženého nejmenších čtverců s váhou 1/skutečná²**. Minimalizuje se tedy relativní chyba: nízké body určují offset, vysoké gain. Surová hodnota ADC se průměruje z 256 vzorků.

- S jedním platným bodem se upraví jen gain, offset zůstane.
- Bez bodů zůstane kanál beze změny.
- Výsledek se převede na `div = 10000`, `gain = round(a · 10000)`, `offset = round(−b)`. Pokud vyjde neplatný (gain ≤ 0, gain/div ≥ 100, |offset| ≥ 9999), zobrazí se chyba a kanál se nezmění.

### Výsledky a uložení

Po posledním kroku se pro každý kanál zobrazí starý a nový gain/offset a **zbytková chyba v každém bodě v %**. Mezi stránkami listujete otáčením enkodéru nebo tlačítkem enkodéru. Poslední stránka nabízí **„Save? ENC=yes MENU=no"**.

Po skončení (uložení i zrušení) se vrátí původní nastavení výstupu a výstup zůstane vypnutý.

---

## 3. Ruční kalibrace

Gain a offset jednotlivých kanálů lze měnit v kalibračním menu (**MENU**). Měřená hodnota:

```
hodnota = raw_mV × gain / div − offset            (div = 10000)
```

Nastavení (Vset, Iset, Isink) se zobrazuje jako `cnt × 2048 / 4095 × gain / div − offset`, kde `cnt` je hodnota DAC (0–4095, reference 2,048 V).

Postup viz [uživatelský manuál](uzivatelsky_manual.md#7-kalibrační-menu). Hodnoty v menu platí do restartu, dokud nezvolíte `Save+Exit`; `Exit` nebo **MENU** změny zahodí.

### Ruční kalibrace Isink

Průvodce Isink **nekalibruje**, protože potřebuje vnější zdroj proudu.

1. Na svorky připojte vnější zdroj (např. laboratorní zdroj s proudovým omezením) **přes ampérmetr**. Polaritu a napětí volte tak, aby výkon zátěže zůstal pod limitem `Psink max W`.
2. Zapněte zátěž tlačítkem **SINK**, vyberte kanál **ISINK** a nastavte několik hodnot (např. 0,2 A, 1 A, 3 A).
3. U každé hodnoty porovnejte nastavený a měřený proud (`Isink` na displeji) s ampérmetrem.
4. V menu upravte `HiI gain` / `HiI offset` (měření proudu zátěže) a `Isink gain` / `Isink offset` (zobrazení nastavené hodnoty), dokud se hodnoty neshodují. Nejprve gain (při vyšším proudu), pak offset (při nízkém).
5. Uložte přes `Save+Exit`.
