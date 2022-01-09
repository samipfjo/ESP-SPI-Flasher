# ESP SPI Flasher
 A simple system to turn an ESP8266 or ESP32 into an SPI memory flasher

&nbsp;

### Directions:

#### Getting the ESP side running
1) Make sure your device has at least 4MB of memory!
2) Wire your flash chip to your device via the SPI pins ([example](docs/SPI-connection.png))
3) Make sure your board's USB-to-serial drivers are installed
4) Install the [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html) (or their [VSCode extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html#installation))
5) Configure `./src/SPI-Flasher/platformio.ini` if you don't have a nodemcuv2 compatible device
6) Open `./src/SPI-Flasher/` in your shell and run `pio run --target upload`

&nbsp;

#### Getting the host side running
1) Install [Python 3.6+](https://www.python.org/downloads/)
2) Open a shell in `./src/read_server/`
3) `pip install -r requirements.txt`

&nbsp;

##### Refer to "Flashing a BIOS chip" below now if you are doing so

&nbsp;

#### Flashing the image to the chip
`python spi_flasher.py -port [PORT] -baud 921600 -file bios.rom --erase --write`

NOTE 1: If you get a bunch of "Hash mismatch" messages, press "ctrl + C" and lower the baud rate

NOTE 2: Erasing is mandatory prior to writes on (most) flash chips that have already been written

&nbsp;

#### Flashing a BIOS chip
- UEFI BIOSes  
	1) Check the last 512 bytes of the file in a hex editor ([HxD is a good one for Windows](https://mh-nexus.de/en/downloads.php?product=HxD20))
		1) All `0xFF` means it is probably a bad image
	2) Open the BIOS file with [UEFITool](https://github.com/LongSoft/UEFITool/releases)
		1) Click the drop-down before `(...) capsule` and ensure it says either "Intel Image" or "UEFI Image"
		2) Extract the image from the capsule (Action > Capsule > Extract Body)
	3) Make sure the size of the image and the chip match exactly
	4) Proceed with flashing

&nbsp;

#### Author's note

This project was created to fix the BIOS chip of an ASUS M32AD that was bricked by a faulty automatic update, which it succeeded in doing!
