# Cell sourcing — SquashSensor, Canada, 2026-09-26

A search for the one cell this design needs, bought the way somebody reproducing it in
Canada would have to buy it: in ones and twos, shipped to a Canadian address. It closes
[`BOM-REVIEW.md`](BOM-REVIEW.md) B1 and two of its §5 open items.

**The requirement.** One rechargeable cell, 70–150 mAh, ≤4.0 mm thick for carrier B or
≤5.5 mm for carrier A, ≤4.6 g, solderable tabs or fitted leads, discharge rated −20 to
+60 °C, standard charge ≤0.5 C, 15 mA continuous with 2 s pulses to 50 mA. LiFePO4
preferred. Qualifying also meant paperwork: a maker's datasheet, a UN 38.3 summary,
IEC 62133 or UL 1642/2054 evidence, a stable part number, and quantity 1–10 shipped to
Canada.

**Method.** Every price, stock level and restriction below was read off a live product
page on 2026-09-26, in a browser set to Canada; prices are CAD unless marked. Every cell
figure comes from the maker's datasheet or test report, named where it is used. Nothing is
taken from a search-result snippet. Stock and prices move; the part numbers and the
paperwork are what this document is for.

---

## 1. The verdict

**No LiFePO4 cell qualifies — not in Canada, and not anywhere with a datasheet at this
size.** The fallback is a lithium-cobalt pouch, and the best-documented one a Canadian can
actually order is **PKCELL LP402025**, sold as Adafruit 1317: 150 mAh, in stock at PiShop.ca
for $7.95, with a maker's datasheet and a published UN 38.3 report. It misses the brief only
by hairs: 4.0 ± 0.3 mm against 4.0, and 4.65 g against 4.6.

For carrier B's 4 mm the best part found is **Cornell Dubilier Knowles (Illinois Capacitor)
RJD2430C1ST1**, a 3.55 mm steel-can coin cell with tabs and a published UL 1642 file — which
no distributor will sell into Canada.

**The failure this search was set up to catch is systemic, not a part.** DigiKey.ca and
Mouser.ca sell no lithium cell of any kind into Canada, rechargeable or primary.

## 2. DigiKey.ca and Mouser.ca block every lithium cell

- **DigiKey hides them.** The same filter — lithium polymer, lithium-ion and lithium-ion
  polymer, rechargeable — returns **296 parts on digikey.com and 93 on digikey.ca**. All
  but six of the 93 are Marketplace listings marked "Unavailable in your selected
  currency", and none of the six is inside 70–150 mAh. Parts in stock on digikey.com read
  **"This product is no longer available at DigiKey"** on digikey.ca: Jauch
  LP402025JU+PCM+2 WIRES 50MM (478 in stock on .com, US$9.93), SparkFun 13853, TinyCircuits
  ASR00011, RJD2430C1ST1, GlobTek BL0105F2635161S1PCAT. The .com page says why: "Air
  shipments of this product are unavailable at this time."
- **Primaries too.** Tabbed lithium primaries: 63 on digikey.com, 20 of them in stock
  (Panasonic CR-2032/F2N, 22,630 at US$1.32); 20 on digikey.ca, of which 17 are
  Marketplace and three are Panasonic parts at zero stock with minimums of 900–2,160.
- **Mouser refuses them.** Every lithium cell checked on mouser.ca is "Shipping
  Restricted", and its product page says **"Mouser does not presently sell this product in
  your region"**: all 29 Renata ICP cells, RJD2430C1ST1, all 57 rechargeable coin-cell
  listings, and Panasonic's CR-2032/F2N. The only unrestricted lithium in its battery-pack
  category is 1500 mAh and up. It does not list Varta CoinPower at all.

**"In stock at DigiKey" says nothing about whether a Canadian can buy it.** Open the .ca
product page.

## 3. LiFePO4

**The smallest on either distributor is several times too big, and unsellable here.**
DigiKey.ca lists 16 LFP parts, all Marketplace; the smallest are Dantona LIFEO4-14430 and
Ultralast UL14430SL-2P, **400 mAh cylinders, 12.7–15.2 × 43.2 mm**, "Unavailable in your
selected currency". The ZEUS PCIFR18650-1500 that BOM-REVIEW B1.1 cited no longer returns a
result. Mouser.ca's LFP category holds 136 parts; the smallest are Power-Sonic's
PSL-FP-IFR18650PC and -EC, **1.1 and 1.5 Ah 18650s**, not sold in the region. Newark Canada
has no LFP cell at or below 250 mAh, and PiShop.ca's are 8 and 12 Ah.

**Worldwide, three cells come close and none qualifies.**

| Cell | Size | What stops it |
|---|---|---|
| Benergy BHG302423, 90 mAh | 3.3 × 24 × 26 mm | A spec table, no datasheet; sold through Alibaba and Made-in-China |
| Renata IFR2045, 67 mAh coin | Ø20 × 4.5 mm | A 2016 "preliminary" flyer; Renata no longer lists it |
| EEMB LP472040F, 200 mAh | 5.0 × 20.5 × 41 mm | Documented, but too large, rated only to −10 °C, and sold by quotation |

No quotation request was sent; no maker was found that lists one with a datasheet.

## 4. What a Canadian can order

| Cell | mAh | T × W × L | Mass | Paperwork | Where, CAD, stock seen |
|---|---|---|---|---|---|
| **PKCELL LP402025** (Adafruit 1317) | 150 (142 min) | 4.0 ± 0.3 × 20 × 25 mm | 4.65 g | Datasheet QA.S.0228; UN 38.3 report NCT18052772B1-1; IEC 62133 claimed on PKCELL's site, no certificate | PiShop.ca $7.95, in stock; Abra $11.45, in stock; Newark $8.11, 4-week lead |
| Data Power DTP401525 (SparkFun PRT-13853) | 110 | 4.2 × 15.5 × 27 mm | "<8 g" | Datasheet; no UN 38.3 summary published | Abra $13.84, in stock |
| EEMB LP401730, with protection | 150 (140) | 4.3 × 17.5 × 31 mm | 2.8 g | Datasheet; UN 38.3 claimed; EEMB's UL file is company-wide (MH20555) | Amazon.ca, sold and shipped by Amazon US, $51.35 per 4-pack, in stock |
| PKCELL LP401230 (Adafruit 1570) | 105 (95) | 4.0 ± 0.3 × 12 × 30 mm | 3.0 g | Datasheet and UN 38.3 report, both published by Adafruit | Newark $8.11, **20-week** lead, non-cancellable |

**The LP402025 datasheet** (PKCELL QA.S.0228, edition A, 2014) gives standard charge 0.2 C,
quick charge 1 C, 1 C maximum continuous discharge, charge 0–45 °C, **discharge −20 to
+60 °C**, storage **−5 to 35 °C for a month and −20 to 45 °C for six**, a Seiko S-8261
protection circuit, and no mass — and says PKCELL will not announce spec changes. The
UN 38.3 report (NCT Testing, 2019-01-16) covers T1–T8 and lists 26.0 × 20.0 × 4.0 mm,
0.56 Wh, 75 mA standard charge and 225 mA maximum discharge. The EEMB and LP401230 cells
carry the same −20 to +60 °C discharge rating; EEMB allows a month of storage at up to
60 °C.

**Retailers' own-brand cells are disqualified**, because none names a maker or ships a
datasheet: Abra's BAT-LIPO-3.7-120-S ($7.45, in stock — at **2.88 mm and 3.4 g** the best
mechanical fit found anywhere), its 4.06 mm BAT-LIPO-3.7-120 and its unbranded 100 and
150 mAh cells; Canada Robotix 0939 ($5.99, 26 left), whose own-letterhead datasheet gives
5.5 ± 0.3 mm, 10 g and discharge −10 to +50 °C against the product page's −20 to +60; and
BC Robotics' "Generic / Unlisted" 150 mAh, out of stock.

**Two notes on the shops.** Elmwood Electronics has merged into PiShop.ca and no longer
takes orders. BC Robotics caps lithium cells at four per order.

## 5. Documented, but not sold into Canada

| Cell | mAh | Size | Mass | Paperwork | Blocked by |
|---|---|---|---|---|---|
| **RJD2430C1ST1**, steel coin, tabs | 110 (104) | Ø24.5 × **3.55 mm** | 4.5 g | Catalogue (February 2025): UL 1642 file **MH28281**; 0.5 C charge, 2 C maximum; −20 to +60 °C | DigiKey.ca "no longer available"; Mouser.ca restricted. digikey.com: 217 at US$15.70 |
| GlobTek BL0105F2635161S1PCAT | 105 | **2.80** × 16.5 × 37 mm | "20 gr" as printed | Rev K1 (2020): cells UL 1642, pack IEC 62133 with CB certificate, UN 38.3 | DigiKey.ca "no longer available". digikey.com: 1,257 at US$9.97 |
| Renata ICP401230UPR | 130 (125) | 4.5 × 12.7 × 31 mm | 3.5 g | Datasheet; UN 38.3 summary published | Mouser.ca "does not presently sell" |
| Jauch LP402025JU+PCM | 140 | 4.2 (4.5 cycled) × 20.5 × 27 mm | ~5 g | Datasheet: UL 1642 yes, **IEC 62133 no** | DigiKey.ca "no longer available" |
| Varta CP1654 A4X, tabbed | 145 | Ø16.1 × 5.4 mm | 3.1 g | Full datasheet, UN 38.3, UL MH13654 | **End of life** — last delivery 2025-01-31 |
| EEMB LP302030HA, bare | 110 (90) | **3.5** × 20.5 × 31 mm | 2.2 g | Datasheet; no protection circuit | Not found for sale in Canada |

**The RJD2430C1ST1 is the best cell in this document for carrier B.** It is the only
documented part under 4.0 mm with its tabs on that also meets the mass limit; the steel can
does not swell — its catalogue gives 3.3 mm bare after 500 cycles — and its heat behaviour
is characterised rather than asserted: >70% retention and >85% recovery after 20 days at
60 °C on full charge, >40% and >60% after 60, and no leakage after 4 h at 90 °C. The same
catalogue says not to leave it "in heated car by sunshine". A Canadian gets one through a
US parcel address: Transport Canada's Special Provision 34 exempts road transport of
UN 38.3-tested cells under 20 Wh, and this one is 0.41 Wh.

The GlobTek is the best-certified cell found, and at 2.8 mm the thinnest, but it is rated to
discharge only down to −10 °C, prints a mass no 105 mAh pouch can have, ends in a Hirose
DF65, and its terms say it "may not be used by consumers directly" — a problem for a kit.

## 6. Primary cells

**They can be bought and cannot carry the load.** Newark Canada stocks tabbed and pinned coin
cells: Panasonic CR2032-1F2 (solder tabs, $3.37, 14 in stock), CR2450-G1A (PCB pins, $6.89,
81) and BR2032-1HE (PCB pins, $3.82, 59). But Panasonic's CR2450 datasheet (February 2026)
rates **continuous drain at 0.2 mA**, its capacity-versus-load curve is near 100 mAh by
~10 mA, and it weighs 6.2 g; Energizer's CR2450 rates 620 mAh at 0.39 mA and tests pulses
only to 9 mA. This device draws 15 mA continuous with 50 mA pulses. The README's conclusion
that a primary does not survive the duty cycle stands, now on datasheet numbers.

The one primary that could — a thin lithium-manganese pouch such as GM Battery's CP403030,
4.0 × 30 × 30 mm, 550 mAh, 300 mA continuous, −40 to +75 °C (PowerStream's listing) — was
not checked for mass or for Canadian availability.

## 7. Why it works this way

- **Cells shipped alone (UN3480) fly only as fully regulated dangerous goods.** IATA's 2026
  guidance: cargo aircraft only, at ≤30% state of charge; the lighter "Section II" route for
  loose cells ended on 31 March 2022. DigiKey's staff have said it does not ship lithium by
  air, and its Canadian site hides the parts rather than offering a ground service.
- **Cells inside a product (UN3481) fly normally.** That is how finished products arrive.
- **Canada Post takes loose cells by domestic surface only** — not by air, and not to or
  from the US (*The ABCs of mailing*, 10 November 2025, pp. 26–27). Canadian retailers
  import in bulk by ground and ship domestically, which is also why BC Robotics limits a
  parcel to four cells.
- **The UN 38.3 summary is a reseller's obligation too.** 49 CFR 173.185(a)(3) requires
  "each manufacturer and subsequent distributor" to make it available, and IATA's guidance
  says anyone in the supply chain may ask. The requirement is satisfiable from any
  legitimate seller, on request.

Every cell that could be traced in a shipping product is a catalogue pouch from a Shenzhen or
Guangzhou maker: Watchy's is a Guangzhou Markyn GMB042030 (180 mAh, 4.2 mm, 4.5 g),
Bangle.js 2's an XK302627 (175 mAh, 3 mm), Adafruit's are PKCELL and SparkFun's Data Power.

## 8. Other chemistries

None meets the ≈0.32 Wh the design needs inside 5.5 mm.

- **NiMH button** (Varta V150HT): the only rechargeable found rated to +85 °C, and not
  dangerous goods — but 0.18 Wh at 5.85 mm and 6.0 g.
- **Lithium titanate** (Nichicon SLB12400L151): 0.36 Wh, in Ø12.5 × 40 mm and 9.0 g.
- **Rechargeable lithium coins** (Maxell ML2032, Panasonic VL3032): rated near 0.2 mA; the
  ML2032 lasts 30 cycles at full depth.
- **Thin-film solid state** (TDK CeraCharge, Ilika M250): 100–250 µAh.
- **Sodium-ion**: nothing smaller than an 18650 was found, and it ships as UN3551 under the
  same restrictions.

## 9. What was verified where

**Live pages, 2026-09-26:** every DigiKey.ca and .com, Mouser.ca, Newark Canada, PiShop.ca,
Abra, Canada Robotix, BC Robotics and Amazon.ca figure above. **Read in full:** PKCELL's
LP402025 specification and its UN 38.3 report, the RJD catalogue, GlobTek's BL0105 drawing,
Panasonic's and Energizer's CR2450 sheets, and Canada Robotix's 0939 sheet. **Read in the
same search, sources below:** the Renata, Jauch, Varta, EEMB and Data Power datasheets,
Varta's end-of-life notice, the LiFePO4 near-misses, the shipping rules, the product cells
and the other chemistries.

**Not verified:** stock counts at PiShop.ca, Abra and Amazon.ca, which show "in stock" only;
whether Newark completes a lithium order to Canada (listed, not ordered); IEC 62133 for
PKCELL's cells, which is a claim; the SparkFun cell's mass; the maker of Abra's own-brand
cells; the CP403030's mass and availability.

## Sources

Cells:
- PKCELL LP402025, QA.S.0228 — https://cdn-shop.adafruit.com/product-files/1317/C1515_-_Li-Polymer_402025_150mAh_3.7V_with_PCM.pdf
- LP402025 UN 38.3 report NCT18052772B1-1 — https://cdn-shop.adafruit.com/product-files/1317/1317_LP402025+UN38.3.pdf
- PKCELL LP401230 and its UN 38.3 report — https://cdn-shop.adafruit.com/product-files/1570/1570datasheet.pdf, https://cdn-shop.adafruit.com/product-files/1570/1570_LP401230+UN38.3.pdf
- PKCELL certification claims — https://www.pkcell.com/product/lp401230-105mah-3-7v/, https://www.batterypkcell.com/certificates/
- Cornell Dubilier Knowles RJD catalogue — https://www.cde.com/resources/catalogs/RJD.pdf
- GlobTek BL0105F2635161S1PCAT, Rev K1 — https://www.globtek.com/pdf/manual-datasheets/BL0105F2635161S1PCAT.pdf
- Renata ICP401230UPR datasheet and UN 38.3 summary — https://www.renata.com/en-us/downloads/?product=icp401230upr&fileid=c3ffa3e27d8b9b28a9de2a7c18, https://www.renata.com/en/downloadfile/icp401230upr/?fileid=589f3a29c0321a889ad1db6114
- Jauch LP402025JU — https://www.jauch.com/downloadfile/677e3528e2de71444db6cca90654e9171/matd_246483_20200507_140mah_-_lp402025ju_1s1p_2_wire_50mm.pdf
- Varta CoinPower end-of-life notice — https://www.anglia.com/registration/pcn_ptn/docs/ptn/PTNVAR20240326.pdf
- EEMB LP302030HA and LP401730 — https://eemb.oss-accelerate.aliyuncs.com/uploads/20230902/a285ffd9e33c2a69d63713d6bf75641b.pdf, https://eemb.oss-accelerate.aliyuncs.com//uploads/20230309/202fb8205636483684d73d1caac2e13b.pdf
- Data Power DTP401525 (SparkFun 13853) — https://cdn.sparkfun.com/datasheets/Prototyping/spe-00-DTP401525-110mah-en-1.0ver.pdf
- Panasonic CR2450 — https://energy.panasonic.com/dam/master/pdf/en/datasheet/lithium/CR2450_Datasheet_EN_240701.pdf
- Energizer CR2450 — https://data.energizer.com/pdfs/cr2450.pdf

Product pages read live:
- PiShop.ca, Adafruit 1317 — https://www.pishop.ca/product/lithium-ion-polymer-battery-3-7v-150mah/
- Abra, SparkFun PRT-13853 — https://abra-electronics.com/batteries-holders/batteries-polymer-lithium-ion/prt-13853-110mah-polymer-lithium-ion-battery.html
- Abra, BAT-LIPO-3.7-120-S — https://abra-electronics.com/batteries-holders/batteries-polymer-lithium-ion/bat-lipo-3-7-120-s-3-7v-120mah-lithium-ion-polymer-battery.html
- Newark Canada, Adafruit 1570 — https://canada.newark.com/adafruit/1570/lithium-ion-polymer-battery-3/dp/66AH9261
- Amazon.ca, EEMB LP401730 4-pack — https://www.amazon.ca/dp/B08HH2VLZ7
- Canada Robotix 0939 — https://www.canadarobotix.com/products/939
- BC Robotics 150 mAh — https://bc-robotics.com/shop/lithium-ion-polymer-battery-150mah/
- DigiKey.ca, Jauch LP402025JU — https://www.digikey.ca/en/products/detail/jauch-quartz/LP402025JU-PCM-2-WIRES-50MM/9560974
- DigiKey.ca, Dantona LIFEO4-14430 — https://www.digikey.ca/en/products/detail/dantona-industries/LIFEO4-14430/13692678
- Mouser.ca, Renata ICP401230UPR — https://www.mouser.ca/en/ProductDetail/Renata/ICP401230UPR?qs=WtvvTbtNXq9vjpvRLNtg1g%3D%3D
- Mouser.ca, Power-Sonic PSL-FP-IFR18650PC — https://www.mouser.ca/en/ProductDetail/Power-Sonic/PSL-FP-IFR18650PC?qs=HP5BdaG8bCpeItCW9Mw1vg%3D%3D

Shipping rules:
- IATA lithium battery guidance, 2026 — https://www.iata.org/contentassets/05e6d8742b0047259bf3a700bc9d42b9/lithium-battery-guidance-document.pdf
- End of Section II for loose cells — https://www.thecompliancecenter.com/help-center/articles/big-change-coming-in-2022-for-lithium-batteries/
- DigiKey on lithium by air — https://forum.digikey.com/t/lithium-battery-shipping/8153
- Canada Post, *The ABCs of mailing* — https://www.canadapost-postescanada.ca/cpc/doc/en/support/abcs-of-mailing.pdf
- Transport Canada, transporting batteries — https://tc.canada.ca/en/dangerous-goods/safety-awareness-materials-faq/industry/transporting-batteries
- 49 CFR 173.185 — https://www.law.cornell.edu/cfr/text/49/173.185

Products and other chemistries:
- Watchy hardware and its cell — https://watchy.sqfmi.com/docs/hardware/, https://www.powerstream.com/lip/GMB042030.pdf
- Bangle.js 2 — https://www.espruino.com/Bangle.js2+Technical
- Varta NiMH button cells — https://www.varta-ag.com/fileadmin/varta_microbattery/downloads/service/battery-documentation/nickel-metal-hydride/Sales-Literature-FOLDER_Nickel_Metal_Hydride_Button_en.pdf
- Nichicon SLB12400L151 — https://www.nichicon.com/en-us/products/lithium-titanate-rechargeable-batteries/products/slb12400l151/
- Maxell ML2032 — https://biz.maxell.com/en/rechargeable_batteries/data_sheet/ML2032_Data_sheet_e.pdf
- Thin lithium primaries — https://www.powerstream.com/thin-primary-lithium.htm
