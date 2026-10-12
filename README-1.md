# Smart Access Control System — Wokwi starter files

## Board and behavior
- MCU: STM32 Blue Pill (STM32F103C8)
- Authentication: authorized RFID UID **01:02:03:04** followed by PIN **2580**
- Keypad: `#` submits, `*` clears the typed PIN
- Servo: 0° = locked, 90° = unlocked; automatically relocks after 5 seconds
- Three incorrect PINs trigger a temporary 10-second lockout

## Wokwi setup
1. Create a new Wokwi project using **STM32 Blue Pill**.
2. Replace the default sketch with `sketch.ino`.
3. Replace `diagram.json` with the included wiring diagram.
4. Add the lines in `libraries.txt` through Wokwi's Library Manager if they are not detected automatically.
5. Start the simulation. Click the RFID reader, select the **Blue Card**, and use its **Tap** control. Then click the keypad and type `2580`, followed by `#`.
6. To test denial, select a different RFID card or enter the wrong 4-digit PIN. Three wrong PIN attempts cause a 10-second lockout.

## Pin mapping
- RC522: SS/SDA=PA4, SCK=PA5, MISO=PA6, MOSI=PA7, RST=PB1, power=3.3 V
- I2C LCD: SCL=PB6, SDA=PB7, address 0x27
- Keypad rows R1-R4=PB12-PB15; columns C1-C4=PB8-PB11
- Servo signal=PA8; buzzer=PB0; red LED=PA0; green LED=PA1

## Important hardware note
This starter project is a simulation. For a physical circuit, power the servo from a suitable regulated 5 V supply and connect its ground to STM32 ground; do not power the servo from a GPIO pin. The RC522 must use 3.3 V. If the I2C LCD backpack is powered at 5 V, check its SDA/SCL pull-ups and use proper level shifting if needed. A real door latch/solenoid requires a suitable driver stage and a separate power supply; do not connect it directly to a microcontroller pin. This demo is not a production-grade lock.
