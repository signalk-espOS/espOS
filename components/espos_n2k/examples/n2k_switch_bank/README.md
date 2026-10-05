# n2k_switch_bank

An NMEA 2000 switch bank any MFD on the bus can switch. The device joins the
bus as a node of its own (`espos_n2k::Node`): it claims an address, shows up
in the MFD's device list as "espOS switch bank", reports eight channels with
PGN 127501 (Binary Status Report) every 2 s and on every change, and switches
them on PGN 127502 (Switch Bank Control) addressed to its bank.

The bank instance is the device instance: the "NMEA 2000" page in the web UI
(`n2k.device_instance`), or the MFD's own instance setting, which writes the
same value back. Give every switch bank on the bus its own.

The candump server runs on the same bus, so signalk-server still reads every
frame through this device (port 2599, format `candump3`), the switch bank's
own included. Its clients cannot transmit with the switch bank's address.

## Building

```sh
idf.py set-target esp32p4      # or esp32s3, esp32c6, ...
idf.py build flash monitor
```

`sdkconfig.defaults` turns on `CONFIG_ESPOS_N2K_NODE`; without it
`espos_n2k/node.h` has no implementation to link.

## Hardware

* `kCanTx` / `kCanRx` in `main/main.cpp` are the Waveshare ESP32-P4 boards'
  CAN pins. Change them for your board.
* `kChannelPins` puts each channel on a GPIO (a relay module, or an LED with
  a resistor). `GPIO_NUM_NC`, the default, keeps a channel virtual: it
  switches and reports and drives nothing, which is enough to try it from an
  MFD.
* A CAN transceiver is not optional, and on a bench the bus needs 120 Ohm at
  each end. [`n2k_candump`](../n2k_candump/README.md) has the wiring notes and
  what `GET /api/v1/n2k` says when it is wrong.

## Trying it without an MFD

From a laptop on the candump port, with canboat's `analyzer`:

```sh
nc <device> 2599 | candump2analyzer | analyzer
```

shows the address claim, the product information and 127501 every 2 s.
Sending a 127502 for bank 0, channel 1 on, through the same socket:

```sh
printf '(0.0) can0 09F20E00#00FDFFFFFFFFFFFF\n' | nc <device> 2599
```

Source address 0 here stands in for an MFD; use one nothing else on your bus
holds. A frame with the switch bank's own address is refused.

The [N2K docs](../../../../docs/n2k.md#the-device-as-an-nmea-2000-node) cover
the node in full.
