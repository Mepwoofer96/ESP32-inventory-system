# ESP32-inventory-system

This is an inventory system for Light it Up LLC.
The system references this repo to stay up to date with current changes.

Main inventory system code is stored in `/main/inventory`.

Local reference link: <http://inventory.local>
Global reference link: <http://inventory.local>

## What it does

**Index page:**
- Includes all parts currently saved to the system
- Links to individual data sheets for each part
- Links to every part

**Admin page:**
Allows control of the following:
- Creating new parts
- Removing existing parts
- Switching to WiFi mode
- Switching to AP mode
- Getting a PDF printout of all parts on the system
- Changing admin password and username (**currently only persists per session**)
- Checking for updates from this repo (**must be in WiFi mode to check**)
- Shows current version #

**Part page:**
Displays:
- Part name
- Part stock #
- Part image
- Part data sheet
- Ability to change the current stock # of parts

## Explanation

**WiFi mode** allows the inventory system to connect to a WiFi network that has internet access. You can access it by connecting to the specified WiFi network and either scanning the given NFC tag or typing in <http://inventory.local>.

**AP mode** makes the inventory system create its own WiFi network. You can connect to this WiFi with the given NFC tag, or by going into your settings, finding the "ESP_Inventory" network, and connecting manually. This WiFi will prompt you with a sign-in page; after signing in, it should take you to the Index page. If you need to go to the Admin page, exit the sign-in page and either scan the given NFC tag or type in <http://inventory.local> (make sure no VPN is on).

**Creating a new part** will take you to an NFC page. If the page says it's unable to write the NFC tag from your phone, place the NFC tag onto the writer connected to the inventory system and press **"Write from Onboard NFC."**
