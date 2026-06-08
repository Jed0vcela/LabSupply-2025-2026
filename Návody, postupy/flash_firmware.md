# Nahrávání firmwaru

## Požadavky

- Micro USB kabel
- PC (Windows, macOS nebo Linux)
- Soubor `firmware.uf2` — stáhnout z [GitHub – Zkompilované programy](https://github.com/Jed0vcela/LabSupply-2025-2026/tree/main/Kompilovan%C3%A9%20programy)

---

## Postup nahrání firmwaru

**1.** Odpojte zdroj od napájení.

**2.** Odstraňte přední kryt zdroje.

**3.** Stiskněte a **držte** tlačítko **BOOTSEL** na Raspberry Pi Pico.

<!-- přidám obrázek -->

**4.** Při stále stisknutém tlačítku připojte micro USB kabel do PC.  
Zdroj se zobrazí jako USB flash disk (mass storage) s názvem `RPI-RP2`.

**5.** Tlačítko BOOTSEL uvolněte.

**6.** Stáhněte nejnovější `firmware.uf2` z [GitHub – Zkompilované programy](https://github.com/Jed0vcela/LabSupply-2025-2026/tree/main/Kompilovan%C3%A9%20programy).

**7.** Přetáhněte soubor `firmware.uf2` na disk `RPI-RP2`.

<!-- přidám obrázek -->

**8.** Zařízení se automaticky odpojí od PC, resetuje a spustí nový firmware.

---

## Vymazání paměti flash (reset kalibrace)

Hodnoty uložené v paměti FLASH (kalibrace, nastavení) **přežijí nahrání nového firmwaru**. Pokud jste v kalibračním menu uložili nesmyslné hodnoty a zdroj se chová nesprávně, je nutné paměť zcela vymazat.

> **Soubor ke stažení:** [`flash_nuke.uf2`](https://datasheets.raspberrypi.com/soft/flash_nuke.uf2) — oficiální nástroj od Raspberry Pi Foundation

### Postup

**1.** Odpojte zdroj od napájení.

**2.** Nahrajte `flash_nuke.uf2` stejným postupem jako firmware výše.  
   Zařízení se po nahrání samo resetuje — disk `RPI-RP2` zmizí.

**3.** Po pár sekundách se raspberry znova připojí.

**4.** Nahrajte "čistý" firmware dle postupu víše.

> Po vymazání flash se při prvním spuštění načtou **tovární výchozí hodnoty** kalibrace.
