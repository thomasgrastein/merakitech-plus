# CoffeeSense NFC in the original Meraki firmware

CoffeeSense tags carry a recipe and a bean card for the original Meraki display. This reference covers the tag
format, the image codes and how to write a compatible tag. The original firmware behaviour described here comes
from the existing static analysis of `demo_v9` 1.1.1-69. File and test evidence is listed under
[Verification and open questions](#verification-and-open-questions).

## CoffeeSense format

CoffeeSense stores the recipe and display fields on a MIFARE Classic 1K tag.
Meraki Dial-In is a private application used to build and test compatible files. Its export behaviour is cited
where it provides evidence about the format. The writing instructions use a publicly accessible generator.

### Payload layers and key material

1. Serialize the 17 fields as compact UTF-8 JSON. The example uses Dial-In’s field order. The analysed stock
   parser looks up properties by name.
2. Encrypt the JSON with AES-128-CBC. The Dial-In encoder uses PKCS#7 padding and passes the same 128-bit key
   bytes as the initialization vector (IV). The stock analysis identifies AES-128-CBC with a fixed key and IV.
3. Encode the ciphertext as standard Base64 ASCII. The card stores the Base64 characters as bytes. An `.mct`
   dump represents those bytes in hex.
4. Place the bytes in successive MIFARE Classic data blocks starting at absolute block 4, skipping sector trailers.

The key and IV values are intentionally omitted. The MIFARE sector Key A used to authenticate block reads is separate
from the payload encryption. Fixed-key, fixed-IV CBC is deterministic and the format has no authentication tag.
Successfully decoding a card provides no proof of its writer or integrity.

### JSON fields and encoding

The stock parser expects all 17 properties. A missing property reaches the `json error` path.
The table describes the types verified in Dial-In’s plaintext JSON. Its four recipe values are numbers.
The other 13 fields are strings, including the numeric-looking codes. In the saved Paperbunker JavaScript,
all 17 values are sent to its server as strings, along with a separate `format` option. The request body
shows the form inputs. The server’s serialized tag payload remains encrypted in the saved exports.

| Key | JSON type | Values emitted or accepted by Dial-In | Meaning and stock use |
|---|---|---|---|
| `gs` | number | See generator ranges below. | Recommended grind-dial setting. Static analysis indicates display-only use. |
| `pw` | number | See generator ranges below. | Ground-coffee dose. Stock uses it as the automatic grinding dose. |
| `bt` | number | See generator ranges below. | Brew temperature in °C, applied by stock. |
| `bw` | number | See generator ranges below. | Target beverage yield. Stock uses it as the automatic brew target. |
| `n1` | string | Trimmed text or `""` when unset. | First card name line. Dial-In uses the coffee name. |
| `n2` | string | Trimmed text or `""` when unset. | Second card name line. Dial-In uses the roaster name. |
| `el` | string | Integer metres as decimal text, for example `"1800"`, or `""` when unset. | Elevation, without a unit suffix in Dial-In. |
| `pr` | string | `"1"`–`"7"` or `"00"` for unspecified. | Processing method code. See the mapping below. |
| `ct` | string | `"0"` = Blend, `"1"` = Single-Origin. | Coffee type in the Dial-In mapping. |
| `rl` | string | `"1"`–`"7"`. An empty value fails the dump self-check. | Roast level on a seven-step scale. See the mapping below. |
| `bgn` | string | Existing catalog basename matching `^\d+(\.\d+){1,2}$`. | Background identifier. Stock loads `S:imgs/nfc/<bgn>.png`. |
| `o1`, `o2`, `o3` | string each | Trimmed text or `""` for an unused line. | Up to three display lines for origin. |
| `f1`, `f2`, `f3` | string each | Trimmed text or `""` for an unused line. | Up to three flavour notes. |

Dial-In validates the inclusive export ranges below. The Paperbunker column records the saved HTML input
attributes. Paperbunker’s server-side validation was unavailable for inspection.

| Recipe field | Dial-In export validation | Paperbunker form (saved page) |
|---|---|---|
| `gs` | 0–100, editor step 0.5 | 0–100, step 1 |
| `pw` | 5–30 g, editor step 0.1 g | 0–50 g, step 0.1 g |
| `bt` | 85–100 °C, editor step 0.5 °C | 80–105 °C, step 1 °C |
| `bw` | 10–80 g, editor step 0.1 g | 0–100, step 1, labelled volume in ml |

These limits come from the generator code or form. The valid range in the stock NFC reader remains unverified for
each field. In particular, **0–100 is a generator input range for `gs`**. The stock preset format documents its own
0–40 grind setting. The stock NFC values accepted at either end of the scale still need a tag test.
[Stock preset capture](presets-spec.md)
[Paperbunker NFC Tools](https://nfc-tools.paperbunker.cz/)

Name, origin and flavour text all count toward the [stock payload budget](#tag-layout-dump-format-and-stock-capacity).
Stock display clipping and character support remain unverified.

Static analysis identifies `"00"` as an empty name-line marker. Dial-In trims the labels it exports and can emit
empty strings for optional text. Equivalent handling of both spellings across all stock fields has not been verified.
Dial-In falls back to `"1"` for an unrecognised coffee type. This is an exporter policy.

| Code string | Processing (`pr`) | Roast (`rl`) |
|---|---|---|
| `"00"` | Unspecified, emitted by Dial-In for empty processing input | — |
| `"1"` | Natural | Very Light |
| `"2"` | Pulped Natural | Light |
| `"3"` | Washed | Medium-Light |
| `"4"` | Semi-Washed | Medium |
| `"5"` | Honey | Medium-Dark |
| `"6"` | Wet-Panting (label in the current mapping) | Dark |
| `"7"` | Special | Very Dark |

The saved Paperbunker form offers the same `ct` values and `1`–`7` mappings for `pr` and `rl`. The
`pr="00"` fallback comes from Dial-In. The spelling “Wet-Panting” appears in both generators. The exact
stock display labels still need a tag test.

#### CoffeeSense background catalog

The `bgn` value is a dotted numeric PNG basename with two or three components. Write the code without `.png`,
a path or a URL. The stock analysis records the path pattern `S:imgs/nfc/<bgn>.png`, such as `S:imgs/nfc/1.1.1.png`. A syntactically
valid code does not establish that its PNG exists on a particular firmware version.

The table documents the 47 PNGs reviewed for this catalog.
Background availability can vary by firmware version.

| Code | Filename | Image subject |
|---|---|---|
| `1.1` | `1.1.png` | A steaming glass cup of amber tea with a tea bag. |
| `1.1.1` | `1.1.1.png` | White daisy-like flowers with yellow centres. |
| `1.1.2` | `1.1.2.png` | A close-up of a deep red rose. |
| `1.1.3` | `1.1.3.png` | A cluster of white blossoms and buds among green leaves. |
| `2.1.1` | `2.1.1.png` | A pile of blackberries. |
| `2.1.2` | `2.1.2.png` | A single red raspberry against a black background. |
| `2.1.3` | `2.1.3.png` | Blueberries covered in water droplets. |
| `2.1.4` | `2.1.4.png` | A close-up of red strawberries. |
| `2.2.1` | `2.2.1.png` | A mix of golden and dark raisins. |
| `2.2.2` | `2.2.2.png` | A dried plum beside a dried apricot. |
| `2.3.1` | `2.3.1.png` | Half a coconut showing its white flesh. |
| `2.3.2` | `2.3.2.png` | Red cherries with stems and water droplets. |
| `2.3.3` | `2.3.3.png` | Cut pomegranates showing their red seeds. |
| `2.3.4` | `2.3.4.png` | Pineapple wedges with the rind on. |
| `2.3.5` | `2.3.5.png` | A bunch of green grapes hanging on the vine. |
| `2.3.6` | `2.3.6.png` | A red apple with a stem and water droplets. |
| `2.3.7` | `2.3.7.png` | A peach hanging from a leafy branch. |
| `2.3.8` | `2.3.8.png` | A green pear with water droplets next to a sliced pear. |
| `2.4.1` | `2.4.1.png` | Half a pink grapefruit on a wooden surface. |
| `2.4.2` | `2.4.2.png` | A halved orange surrounded by orange wedges. |
| `2.4.3` | `2.4.3.png` | A yellow lemon submerged in water, with bubbles against a dark background. |
| `2.4.4` | `2.4.4.png` | Whole limes and a cut lime against a blue-green background. |
| `3.2.1` | `3.2.1.png` | A wine bottle and two stemmed wine glasses in low light. |
| `3.2.2` | `3.2.2.png` | A tumbler of amber liquor with ice cubes against a golden background. |
| `6.1` | `6.1.png` | Loose shredded brown tobacco on a light surface. |
| `6.2` | `6.2.png` | Three brown cigars on beige fabric. |
| `6.3.3` | `6.3.3.png` | Swirls of pale smoke against a dark background. |
| `6.4.2` | `6.4.2.png` | A close-up of small, glossy brown grains or seeds. |
| `7.1` | `7.1.png` | A red chilli pepper covered with small bubbles against a black background. |
| `7.2` | `7.2.png` | A close-up of black peppercorns. |
| `7.3.1` | `7.3.1.png` | Whole star anise pods on a dark surface. |
| `7.3.2` | `7.3.2.png` | Whole nutmegs with orange-brown strands of mace around the dark seeds. |
| `7.3.3` | `7.3.3.png` | Cinnamon sticks in a white bowl. |
| `7.3.4` | `7.3.4.png` | A pile of whole dried cloves. |
| `8.1.1` | `8.1.1.png` | A pile of shelled peanuts, many split into halves. |
| `8.1.2` | `8.1.2.png` | A close-up of peeled hazelnuts. |
| `8.1.3` | `8.1.3.png` | A close-up of whole almonds with their brown skins. |
| `8.2` | `8.2.png` | A pile of whole cacao beans. |
| `8.2.1` | `8.2.1.png` | Broken pieces of milk chocolate with visible nut fragments. |
| `8.2.2` | `8.2.2.png` | Stacked pieces of dark chocolate with chocolate shavings against a black background. |
| `9.1` | `9.1.png` | A close-up of coarse brown sugar crystals. |
| `9.1.1` | `9.1.1.png` | A lattice of glossy dark syrup on a light surface. |
| `9.1.2` | `9.1.2.png` | Golden syrup dripping from a spoon into a white dish. |
| `9.1.3` | `9.1.3.png` | A stream of thick caramel sauce pouring from a glass vessel. |
| `9.1.4` | `9.1.4.png` | A piece of honeycomb with honey pooling beneath it. |
| `9.2` | `9.2.png` | Vanilla pods over a shallow bowl of speckled granules. |
| `9.4` | `9.4.png` | Gummy bears and sugar-coated rainbow candy strips on a pale pink background. |

Paperbunker's **Background Image** dropdown and **Prev/Next** controls let you browse the backgrounds
offered by its CoffeeSense NFC Tag Creator.
[Paperbunker NFC Tools](https://nfc-tools.paperbunker.cz/)

Synthetic plaintext example with all 17 properties in Dial-In’s export order. It is 192 UTF-8 bytes and
encodes to 280 Base64 bytes with the documented padding.

```json
{"gs":12,"pw":18,"bt":93,"bw":36,"n1":"Example Coffee","n2":"Example Roaster","el":"1200","pr":"3","ct":"1","rl":"3","bgn":"1.1.1","o1":"Colombia","o2":"","o3":"","f1":"Cocoa","f2":"","f3":""}
```

On a successful decode, the analysed stock path applies the dose, brew temperature and target yield immediately.
It normally saves the decoded JSON to `/customer/nfc.json` and shows the bean card. The grind setting is a
recommendation. [Original firmware analysis](nfc-plan.md#24-stock-ui-flow-on-tap)

### Tag layout, dump format and stock capacity

| Item | Layout |
|---|---|
| MIFARE Classic 1K | 16 sectors of four 16-byte blocks. The last block of each sector is its trailer. |
| `.mct` representation | `+Sector: N` followed by four lines of 32 hex characters each. A full dump has 16 headers and 64 block lines. |
| Sector 0 | Reserved from the payload. Current Dial-In code uses a non-zero manufacturer-block template. One older Dial-In export has a zero-filled block 0. Leave sector 0 of the target tag as it is. |
| Payload blocks read by stock | Absolute blocks `4–6, 8–10, 12–14, 16–18, 20–22, 24–26, 28–29`. Block 30 is outside the read window even though it is a data block in sector 7. |
| Padding in data blocks | The inspected dumps have zero-filled bytes in the payload area after the Base64 text. For inspection, concatenate data blocks in order and trim trailing NUL bytes before decoding Base64. A partially filled final block is normal. This padding is separate from PKCS#7 inside the ciphertext. |

**The analysed stock build reads 320 data bytes.** Base64 text or trailing zero bytes can occupy that window.
The Dial-In dump builder can place up to 720 bytes in the
45 data blocks of sectors 1–15. Its parse/decrypt check can therefore pass on a dump containing more data than
stock reads. For compatibility with the analysed stock build, keep the encoded payload within 320 bytes.
Standard Base64 and PKCS#7 imply a maximum of 240 ciphertext bytes, or 239 bytes of compact UTF-8 JSON before
padding. Count bytes, including multi-byte text. This is a calculated upper bound. A full 320-byte payload
fills the window with no trailing NUL inside it. Acceptance at that exact boundary still needs a stock tag test.
A local test reproduced the boundary. With the same encoding, 239 bytes of JSON produced 320 Base64 bytes
and 240 bytes of JSON produced 344 Base64 bytes. A saved Paperbunker export also contained 344 Base64 bytes.
Its payload extends into block 30 and later, past the read window of the analysed firmware.

### Generate an MCT file and write it to an existing tag

Use an Android phone whose NFC hardware supports MIFARE Classic and an existing writable MIFARE Classic 1K
tag that you own or have permission to modify. CoffeeSense writes replace the previous recipe data in the target
blocks. Save a dump of the existing tag before editing it.
[MIFARE Classic Tool for Android](https://play.google.com/store/apps/details?id=de.syss.MifareClassicTool).

1. **Generate the file.** Open Paperbunker's CoffeeSense NFC Tag Creator, enter the recipe and card details,
   choose a background and select **Generate MCT File**. Check the last non-zero byte of the Base64 payload.
   It must fall at or before absolute block 29, counting only data blocks as described in
   [Tag layout, dump format and stock capacity](#tag-layout-dump-format-and-stock-capacity).
   [Paperbunker NFC Tools](https://nfc-tools.paperbunker.cz/).
2. **Import and back up.** In MCT, use **Import/Export/Convert Files** to import the `.mct` file as a dump.
   Use **Read Tag** on the target tag and save the result separately. Use the target tag's sector keys to
   authenticate. Proceed only if you can read the target data blocks and have permission to write them.
3. **Write only the recipe data.** Open the imported dump and copy each intended 16-byte data block as 32 hex
   characters. In MCT, use **Write Tag → Write Block** and enter the sector and its local block index.
   Use blocks 0–2 in sectors 1–6 and blocks 0–1 in sector 7. Leave sector 0, all sector trailers (local block 3),
   and all blocks outside the stock read window untouched. Include the dump's zero-filled blocks within the
   read window so remnants of a previous, longer recipe are cleared.
4. **Read back and test.** Use **Read Tag** again and compare all 20 payload blocks with the generated dump.
   With the machine idle, tap the tag against the original Meraki and check the displayed recipe before brewing.

[MCT user manual](https://github.com/ikarus23/MifareClassicTool/blob/master/Mifare%20Classic%20Tool/app/src/main/assets/help/help.html).

**Firmware key requirement.** A newly written tag needs a payload generated with the original firmware's
CoffeeSense AES key for the display to decode it. A compatible generator uses that key before export.
MCT writes the already-encrypted bytes and authenticates to the tag with the separate MIFARE sector keys.
The firmware key is intentionally omitted from this documentation.

## Verification and open questions

| Evidence | Result |
|---|---|
| 18 saved Paperbunker `.mct` exports | Each had 16 sector headers and 64 blocks. Base64 began at block 4, skipped the trailers and was followed by zero bytes. The payloads were 280, 320 or 344 bytes long. |
| 3 saved Paperbunker Flipper `.nfc` exports | Each contained 64 blocks. One had exactly the same Base64 payload as the corresponding `.mct` export. |
| 4 saved Meraki Dial-In `.mct` exports | The same layout held, with 256- or 300-byte Base64 payloads. An additional database export had a 320-byte dump whose Base64 matched the stored payload byte for byte. |
| Dial-In plaintext fixture and current exporter | The plaintext fixture confirmed the 17 names, types and order. All 61 regression tests passed. An independent decrypt of a new test-key export recovered that JSON and the encoder test confirmed the 239/240-byte boundary. |
| Saved Paperbunker form and JavaScript | HTML attributes and code choices were checked in the form. The script constructs a JSON request with the 17 fields as strings and a separate format option. |
| Existing static analysis of stock `demo_v9` 1.1.1-69 | The reader's 20-block window, the required property names and the application of the three recipe values come from the analysed firmware. |
| Earlier physical test reported by the Dial-In contributor | Self-generated `.mct` files were used successfully on a physical tag with the original Meraki. The tested tag variant and firmware build were not recorded. |

The saved exports were inspected without decrypting their payloads. The byte layout, Base64 and lengths are
verified from the files. The 17 plaintext fields are verified in Dial-In’s fixture and fresh test export. The
Paperbunker server’s plaintext, a stock-generated `nfc.json`, the exact stock display labels and acceptance
at the 320-byte boundary remain unverified.
