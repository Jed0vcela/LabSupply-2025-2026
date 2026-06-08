# Laboratorní napájecí zdroj + elektronická zátěž

Kombinovaný zdroj a elektronická zátěž na společných výstupních svorkách — vhodný pro nabíjení i vybíjení baterií.

## Napájení

- **USB-C** nebo **XT60** konektor (12–30 V, např. z akumulátorů)
- Výstupní napětí může být vyšší i nižší než vstupní (výstup **není galvanicky oddělen** od vstupu)

## Parametry

| Parametr | Hodnota |
|---|---|
| Výstupní napětí | 0 – 35 V |
| Výstupní proud (zdroj) | 0 – 4,5 A |
| Proudová zátěž | 0 – 3,8 A (max. 20 W) |

## Funkce

- **Zdroj** — nastavitelné napětí a proud (Vset, Iset)
- **Elektronická zátěž** — nastavitelný proudový sink (Isink), souběžně se zdrojem
- **Měřič kontinuity** — nastavitelný práh napětí, akustická signalizace
- **Ochrana výkonu** — konfigurovatelné limity Pout a Psink, ochrana při poklesu Vin
- **Kalibrace** — nastavení gain/offset pro každý měřený kanál, uložení do interní flash
- **Ukládání nastavení** — hodnoty Vset, Iset a Isink se ukládají do flash
- **IR dálkové ovládání** — NEC protokol, zatím není implementováno
- **LCD displej** — barevný 160×128 px, tři zobrazovací režimy

## Hardware

Raspberry Pi Pico (RP2040) · MCP4728 DAC · MCP23S17 · ST7735S LCD · rotační enkodér · NTC teplotní senzory · ventilátor · bzučák

## Dokumentace

Podrobný uživatelský manuál: [`manual_napajeci_zdroj.md`](manual_napajeci_zdroj.md)

<img width="4624" height="3472" alt="IMG_20260608_140253" src="https://github.com/user-attachments/assets/2df4201b-ee5e-44f5-9043-8d2f1a5b26d9" />

<img width="3472" height="4624" alt="IMG_20260608_140136" src="https://github.com/user-attachments/assets/06a2c057-860f-4f5b-88a8-9de93c431756" />

<img width="1724" height="938" alt="LabSupply 2025-2026" src="https://github.com/user-attachments/assets/e68a7a44-ff1c-4edc-9360-bf4c87f79170" />
