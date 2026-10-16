# Vývoj

## Build (Pico SDK + CMake)

Primární build je **Pico SDK + CMake**. Potřebujete `cmake`, `arm-none-eabi-gcc`, překladač C/C++ pro hostitele a Pico SDK (včetně submodulů, kvůli USB):

```bash
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk ~/pico-sdk
cd Code
mkdir -p build && cd build
cmake .. -DPICO_SDK_PATH=$HOME/pico-sdk     # nebo: export PICO_SDK_PATH=...
make -j
```

Výsledkem je `lab_supply.uf2` (a `.bin`, `.hex`, `.elf`). Složka `Code/build/` se nesleduje v gitu (je v `.gitignore`).

> `Code/platformio.ini` používá `framework = picosdk`, který oficiální platforma `raspberrypi` nepodporuje. Pro build slouží CMake.

## Struktura kódu

| Cesta | Obsah |
|---|---|
| `Code/src/` | zdrojové kódy (C/C++) |
| `Code/include/` | hlavičky |
| `Code/test/` | testy na hostiteli (běžný g++) |
| `Kompilované programy/` | hotové `.uf2` pro uživatele |
| `KiCad soubory/` | hardware (Power board, Control board, OPamp_boost1) |

| Soubor | Účel |
|---|---|
| `main.cpp` | inicializace, stavový automat režimů, hlavní smyčka; objekt `mcp4728 dac` je lokální v `main()` |
| `psu_control.cpp/.h` | `g_dac_cnt[]`, `dac_update()`, omezení výkonu, tlačítka, násobič kroku, `cnt_to_mv()`, `setpt_disp()` |
| `cal_menu.cpp/.h` | blokující kalibrační menu (tabulka `g_cal_items`, `CAL_ITEMS_MAX = 28`) |
| `cal_wizard.cpp/.h` | kalibrační průvodce (`Auto cal`), tabulka kroků `STEPS[]` |
| `cal_fit.cpp/.h` | matematika fitu bez závislosti na Pico SDK (testovaná na hostiteli) |
| `flash_store.cpp/.h` | `cal_flash_t g_cal`, `CAL_DEFAULTS`, `flash_load/save()`, `cal_apply()`; poslední sektor flash, CRC32 + `CAL_MAGIC` |
| `display.cpp/.h` | kreslení režimů, `field_t`, `fmt_fp()`, `msg_post()`, `mav_*`, `draw_splash()` |
| `peripherals.cpp/.h` | enkodér, IR, ventilátor, bzučák, podsvícení |
| `ADC_measurement.c/.h` | ADC přes multiplexer, `raw_mv[]`, `calibrated[]`, `adc_set_calibration()` |
| `mcp4728.cpp`, `mcp23s17.c`, `st7735.c` | drivery DAC / IO expandér / LCD |
| `version.h` | `FW_VERSION` |

### Kalibrační model

- Měření: `value = raw_mV * gain / div - off` (int64 uvnitř).
- Nastavení: `setpt_disp(cnt) = cnt_to_mv(cnt) * gain / div - off`, `cnt_to_mv(cnt) = cnt * 2048 / 4095`.
  - `2048 * gain` musí být menší než 2^31, tedy při `div = 10000` musí být `gain < ~1 000 000`.
- Kanál **Vin** se kalibruje v jednotkách **10 mV** (stejně jako `vin_min_mV` a zobrazení Vin), ne v mV; proto je výchozí `vin_gain` 10950/10000.
- DAC: 12 bit, Vref 2,048 V; kanál C = Vset, D = Iset, A+B = Isink.
- **Neměnit layout `cal_flash_t` ani `CAL_MAGIC`** — uživatelům by se smazala kalibrace.

### Tlačítka — popisek na panelu ↔ pin MCP23S17

Tlačítka jsou active-low, stisk se detekuje sestupnou hranou (`prev & ~now`).

| Panel | Pin | Funkce v normálním režimu |
|---|---|---|
| OUT | GPA0 | zap/vyp výstup zdroje |
| tlačítko enkodéru | GPA1 | potvrzení v menu |
| VOUT | GPB0 | výběr kanálu Vset |
| 100x | GPB1 | krok ×100 (momentary) / reset na ×1 (toggle) |
| IOUT | GPB2 | výběr kanálu Iset |
| 10x | GPB3 | krok ×10 (momentary) / cyklus (toggle) |
| ISINK | GPB4 | výběr kanálu Isink |
| MENU | GPB5 | vstup/výstup kalibračního menu |
| SINK | GPB6 | zap/vyp zátěže |
| MODE | GPB7 | přepnutí režimu zobrazení |

Piny Pico: IR přijímač GP1, ventilátor GP22, bzučák GP14.

## Testy

Matematika kalibračního fitu se testuje na hostiteli:

```bash
cd Code/test
g++ -Wall -Wextra -I../include test_cal_fit.cpp ../src/cal_fit.cpp -o /tmp/test_cal_fit && /tmp/test_cal_fit
```

Test pokrývá přesná i zašuměná data, váhy, 0 a 1 bod, degenerované body, záporný gain a přetečení gainu i offsetu. Chování na hardwaru je třeba ověřit ručně.

## Jak vydat `.uf2`

1. Zvyšte `FW_VERSION` v `Code/include/version.h`.
2. Přeložte (viz výše).
3. Zkopírujte `Code/build/lab_supply.uf2` do `Kompilované programy/firmwareN.uf2` (`N` = číslo verze).
4. Aktualizujte odkazy na soubor v [nahrani_firmwaru.md](nahrani_firmwaru.md).
5. Ideálně přiložte soubor i k vydání na GitHub Releases.
