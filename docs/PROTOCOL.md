# Supported air interface and fields

This plugin implements classic OGNTP v1 / OGN1 M-band, not every protocol
transmitted in the same band.

| Property | Value |
| --- | --- |
| Channels | M1 868.2 MHz; M2 868.4 MHz |
| Modulation | 2-GFSK, 100 kchip/s, nominal deviation ±50 kHz, BT 0.5 |
| Line code | Manchester IEEE 802.3 |
| Decoded sync | `0x0AF3656C`, 32 bits |
| Codeword | 26 bytes: 4 header, 16 whitened payload, 6 parity |
| FEC | OGN custom (208,160) LDPC, 48 parity equations |
| Whitening | Two 64-bit blocks, zero-key TEA with eight cycles |

The receive order is frame extraction, LDPC validation/correction on the
still-whitened codeword, even parity of header bits 0–27, TEA inverse on bytes
4–19, and report-specific field parsing. Words are explicitly little-endian;
the header and parity bytes are not dewhitened. This public reversible
whitening is separate from private encrypted payload formats.

## Reports

- **Position:** address/type, second, fix quality and dimensions, coordinates,
  GNSS altitude above geoid, standard-pressure altitude where available,
  ground speed, track, climb, turn, DOP, aircraft type and header flags.
- **Status:** available battery, sensor, GNSS, RF, version and hardware fields.
- **Information:** checked seven-bit fields such as model or registration.
- **Wind:** measurement location, altitude, wind speed/direction and climb.
- **Unknown, custom or encrypted:** validated header/type only.

OGN's nonlinear unsigned and signed variable-resolution encodings are decoded
before unit conversion. DOP includes its 1.0 offset. Track follows reference
rounding. Invalid fixes do not create displayed coordinates; unavailable
barometric altitude, climb, turn and sensors remain unavailable. The transmitted
second alone cannot reconstruct a complete UTC timestamp.

## Integrity limits

The default two-bit correction searches for a unique one/two-bit syndrome.
The OGN matrix has ambiguous two-bit syndromes, which are rejected. Configured
limits of three to six use experimental min-sum recovery from hard bits.
Some higher-error patterns can map to a different valid codeword. Passing LDPC
and header parity therefore does not prove that all data is correct or that
the sender is authentic. Setting corrections to zero accepts only codewords
that already satisfy parity. Sync tolerance is a separate control.

The old framing-only Mode Builder random payload generator does not create
valid OGN parity and whitening. Such IQ can reveal sync in a workbench while
being correctly rejected by this plugin. This project's generator creates
fully encoded synthetic OGN packets.

## Sources and provenance

- [OGN Tracking Protocol](https://wiki.glidernet.org/ogn-tracking-protocol),
  including the public reference codeword used in tests.
- [glidernet/ogn-tracker](https://github.com/glidernet/ogn-tracker/tree/f14ff85796903e890ac3d3dfc0e7b46c831c4d7c),
  pinned reference for the air interface, TEA and parity-check constants.
- [esp32-ogn-tracker](https://github.com/pjalocha/esp32-ogn-tracker/tree/42e0e366d05bfc866ec5e3d0fba8058f9da3a3dc),
  pinned reference for modern OGN1 report layouts and scaling.

The decoder implementation is independently written. Only numeric constants
defining the parity-check graph are transcribed from the tracker reference.
See NOTICE.txt for precise bundled-source licensing and provenance.
