# EutherDrive Consoles — CPU 0.15 / Vulkan 0.18

**Vulkan-test finns nu:** `dist/eutherdrive-vulkan-player-0.18.pkg`.
CPU-jämförelse: `dist/eutherdrive-console-player-0.15.pkg`. Båda paketen använder
exakt samma managed assembly, ROM:ar och kontroller; den native grafikvägen skiljer.
0.16 och 0.17 startade inte på fysisk PS4 enligt användaren. 0.18 byter till
Doom3-Ps4:s Vulkan/OpenGNM/PSBC-arkiv. ICD-hashen matchar Doom3 bundled 0.07:s
build-info; arkiven låses av `scripts/doom3-vulkan.sha256`.
SDK:s create-fself producerade dubbla RW LOAD-segment och trunkerade
initialiserad data i 0.17. Samma ELF konverterad med Doom3:s source-built
create-fself (upstream f2da6229684ce320f673c06fceac9c218f621788) saknar båda
felen. `scripts/verify-oelf.py` stoppar nu sådana artefakter efter konvertering.
Det är ett konstaterat binärfel; fysisk start med korrigeringen återstår.

0.18:s första frame kontrolleras före Mono och driverloggen hålls öppen tills
första submit/fence/flip lyckats. shadPS4 passerar detta, sedan stoppar dess
befintliga libkernel/rättighetsbegränsning Mono-vägen. Desktop GPU-kontroll av
färger, skalning och HUD passerar också. Detta bevisar inte fysisk PS4-start.

0.15/0.18 visar FPS och core/audio/video-ms direkt under spelet, uppdaterat var
annan sekund. Mätningen är genomsnittet sedan spelstart. Fota raden efter cirka
30 sekunder i samma spel på båda versionerna. Stäng appen före paketbyte.
Om core dominerar behöver själva C#-kärnan optimeras; GPU-skalning löser den
separata bildkostnaden. Video inkluderar väntan på frame pacing och vsync.

0.14-fotot visar `Controller read failed`, inte FPS. NativeInput reserverar
negativa tal för fel, men knappfältet är uint32: extra höga bitar kunde skapa
ett falskt negativt returvärde. SDK:ns lågbitarsknappar och touchpad filtreras
nu ut före retur. Test med bit 31 satt och riktiga knappar kvar PASS. Riktigt
DeviceNotConnected/SendAgain ger neutrala knappar och återhämtning; andra
läsfel stoppas med exakt felkod. Bilden bevisar inte vilket av dessa fall
som inträffade på konsolen.

## Vulkan-väg

Återanvänder låsta OpenGNM/vulkan-ps4/PSBC-arkiv från Doom3-Ps4. Arkivhashar
sparas i paketet och byggkatalogen; källstacken ändras inte här. All grafik
går genom Vulkan-API till OpenGNM/GNM; inga Piglet-/Shacc-beroenden.
Vulkan startas före Mono, med ensam VideoOut-ägare från början, som i
ScummVM-PS4:s ps4gl-main.c. CPU-buffertarna är vanliga uploadkällor. Spelbilden laddas upp i sin ursprungliga storlek som RGBA-textur med BGRA-källdata; shadern byter
röd/blå kanal eftersom Doom3:s linjära bildväg kräver RGBA.
Nearest-filtershadern skalar den med samma heltalsskala. Mätfältet är en
separat liten textur. CPU:n skapar fortfarande emulatorernas spelbilder.

GPU acquire/render-fence väntar högst två
sekunder. En lokal wrap av OpenGNM:s flip-helper använder också begränsad
statuspollning och fångar returnerade flip-fel som ICD:n annars ignorerar.
Resurser behålls till processavslut vid GPU-fel. Allocator-lås aktiveras före
native/PSBC-trådar; Mono-binär och rättighetsväg är oförändrade.

Desktop offscreen Vulkan/NVIDIA + Khronos validation: åtta pass med full
921600-pixeljämförelse av sex bildmått, kanalordning, kanter, HUD och byte
tillbaka från HUD. Inga validation-fel. Det testar shader-/texturlogik med en
annan ICD; det bevisar inte PS4-ICD/presentation. Native flip-fel/timeout-tester
och befintliga UI/ljud/credential-tester PASS. GLSL/SPIR-V valideras separat.

```sh
ED_CONSOLE_PLAYER=1 ED_VULKAN_PLAYER=1 \
  ED_JBC_DIR=/home/nichlas/ut99-orbis/build/ps4-usb \
  ED_CONSOLE_LIBRARY="$PWD/build/console-library-0.11.json" \
  scripts/package-runtime-probe.sh
```

`ED_VULKAN_STACK` kan välja en annan redan byggd stack. För snabb native-
jämförelse kan `ED_CONSOLE_VALIDATED_MAIN_SHA256` låsa en redan ROM-testad
assembly; skriptet kräver samma hash och fem PASS-rader. Vanliga byggen
kompilerar och ROM-testar alltid om managed-koden. Shaders återskapas med
`python3 scripts/build-vulkan-shaders.py`.

## Tidigare prestandasteg 0.14

0.13 är den fysiskt fungerande baslinjen, commit `b094d84`. 0.14 minskar
Mono-loggnivån från debug till warning, undviker per-rad fsync och extra
diagnostik-flips när frontenden är aktiv. Loggar och fel finns kvar;
slutrapporten synkas efter normal nedstängning. Bildskalningen expanderar en
källrad och kopierar upprepade rader i stället för division per utpixel.
Pixel-för-pixel-kontroll av hela 1280x720-bilden passerar för SMS/MD/SNES och
640x480, inklusive kanter och bakgrund.

Tre alternerade desktopkörningar av 1000 native-renderingar gav median
2,408 s före och 0,525 s efter (4,58x för den avgränsade renderingsvägen).
Mockad VideoOut/audio används: resultatet är **inte** fysisk PS4-FPS eller
kärnans hastighet. Ingen frame-skipping eller ändrad emuleringstiming införs.

Spela cirka 30 sekunder per system och återgå med L1+R1. Bibliotekets statusrad
visar genomsnittlig FPS och core/audio/video ms per bild. Core inkluderar
adapterns RunFrame; audio inkluderar PCM-hämtning/kopiering och kömatning;
video inkluderar framebufferkonvertering, skalning och pacing/vsync-väntan.
Återstående flaskhals och verklig hastighetsvinst kräver fysisk återkoppling.

På fysisk PS4 startar 0.12:s UI, men ROM-start stoppas med ett fel som börjar
`Method EutherDrive.Core.MdTracerCore.md_m68k:ini...`. Resten klipptes av UI:t.
`initialize2` var en enda metod med 47 383 opcode-registreringar och 2 179 371
IL-byte. Den levererade PS4-Mono-binären innehåller feltexten
`Method %s is too complex.`; detta är en stark kandidat, inte ett fullständigt
avläst konsolfel.

0.13 delar samma registreringar i 186 metoder med högst 256 anrop vardera,
utan ändrat innehåll eller ordning. `NoInlining` hindrar sammanslagning.
Största metodkroppen är nu 11 798 IL-byte. Fel visas på en separat, sidindelad
skärm med undantagstyp och hela meddelandet: X byter sida, cirkel återgår.
2026-09-30 bekräftar användaren att 0.13 fungerar på fysisk PS4, men mycket
långsamt. Exakta spel, FPS och ljudkvalitet är inte angivna.

Privat PS4-testversion: Master System, Mega Drive och första SNES-integrationen.
Den använder hela `MdTracerAdapter`/`MdTracerCore` från EutherDrive_Android,
inklusive 68000, Z80, VDP och YM2612/PSG. Master System går genom samma
MD-adapter som Android, inte den fristående SMS/GG-seed-proven.
Den gemensamma managed-spelaren finns i `probes/gb/Player.cs`;
`CONSOLE_PLAYER` väljer adapter-/bildmåtts-/kontrollvägen för konsolerna.
SNES använder hela den lokala KSNES-kärnan och `SnesAdapter`; första testet är
Zelda: A Link to the Past. Sega CD/32X-kod följer med som gemensamma beroenden
men erbjuds inte som spelbara system i denna version.

Källrevision: `7771ae7f736caef7f20399a6315d3140217bfbd2`, 308 pinnade
käll-/resursfiler. Android-checkouten ändras inte. Källhashar och de exakt
valda managed-beroendenas hashvärden finns under `build/console-player/`.
32X-resurserna måste finnas även när ett vanligt MD-spel identifieras.

## Bygg

```sh
ED_CONSOLE_PLAYER=1 \
  ED_CONSOLE_LIBRARY="$PWD/build/console-library-0.11.json" \
  scripts/package-runtime-probe.sh
```

JSON-listan innehåller absoluta lokala ROM-sökvägar. `ED_CONSOLE_ROM` stöder
ett enda spel. Tillåtna format: `.sms`, `.md`, `.gen`, `.smd`, `.sfc`, `.smc`.
ZIP-/specialchip-/BIOS-spel är inte verifierade genom dessa tester.
Ett tidigare byggt libjbc kan användas med
`ED_JBC_DIR=/home/nichlas/ut99-orbis/build/ps4-usb`.

Utdata för CPU: `dist/eutherdrive-console-player-0.15.pkg`, samma title ID
`EDRM00001` som tidigare tester. Installation ersätter den installerade
GB-testappen. De äldre PKG-filerna bevaras; GB-regressionen förblir körbar.
Paketet innehåller användarens ROM:ar och är inte en offentlig release.
KSNES/runtime/licensproveniens är inte färdiggranskad för publicering.

## Kontroller och test på PS4

- Bibliotek: upp/ned väljer, X startar, triangel mute, cirkel avslutar.
- Alla spel: L1+R1 återgår till biblioteket. Spelet startas om nästa gång.
- Master System: styrkors, X=knapp 1, cirkel=knapp 2, Options=paus.
  Starta SMS-spelet med knapp 1; Options är inte spelets startknapp.
- Mega Drive: X=A, cirkel=B, fyrkant=C, Options=Start.
  Triangel=X, L2=Y, R2=Z för sexknappskontroll.
- SNES: cirkel=A, X=B, triangel=X, fyrkant=Y, L1=L, R1=R,
  Options=Start och touchpad-knapp=Select.

Mute ändras i biblioteket: triangel är en spelknapp i MD/SNES.
Börja med Black Belt/Alex Kidd, därefter Sonic och Streets of Rage 2.
Kontrollera hastighet, musik/effekter och upprepade spelbyten. Prova därefter
Zeldas filval och spelstart. Cirkel i biblioteket går igenom normal Mono-
cleanup och rättighetsåterställning. Firmware-spärren till 9.60 är oförändrad.

**Batterisparning och savestates erbjuds inte ännu.** Snapshotens automatiska
SMS/MD-save-sökvägar och SNES-save-timers är avstängda. Cartridge RAM-emulering
finns kvar i minnet. Inga original-ROM:ar eller deras saves skrivs över/importeras.
Hosttester använder separata ROM-kopior i byggkatalogen.

## Plattform och UI

Den befintliga VideoOut-/AudioOut-/Mono-vägen återanvänds. Managed bildanrop
anger mått och period per system; native kontrollerar gränser, centrerar bilden
med största heltalsskala som ryms i 1280x720 och väntar på flip innan återbruk.
PCM är stereo 44,1 kHz och omsamplas till 48 kHz genom den befintliga ljudkön.
Förhandsbilder kopieras under ett synkront anrop och håller inga Mono-pekare.

Biblioteket tar Androids standardpalett (`skins/default.apa`): mörka paneler,
turkosa fokusmarkeringar, dämpade etiketter och en separat förhandsbildspanel.
Bilderna är riktiga ramar från samma ROM-test, inte påhittade omslag.
`.preview` innehåller little-endian bredd/höjd och A8R8G8B8-pixlar, max 320x240.
Detta är en första delmängd av stilen, inte en komplett `.apa`-loader:
Inter-fontatlas, rundade paneler, omslag, animationer och skinval återstår.
Granskad native host-rendering: `build/ui-preview/library.png`.

Framework-anpassningarna gäller Array/Math, BitOperations, Span-I/O,
string-slicing/split, diagnostik och reflektionens äldre API. JSON-metadata och
KSNES desktop-DI/JSON-manager används inte av PS4-spelaren. Inga CPU-, VDP-,
APU- eller ljudfilteralgoritmer ersätts. Linux-loggvägar riktas till
`/data/eutherdrive-ps4/logs` på konsolen, respektive hosttestets egna katalog.

Runtime-closure är explicit: pinnad PS4-BCL samt NLayer 1.16.0,
SharpCompress 0.36.0, ZstdSharp.Port 0.7.4 och Mono-forwarding-facades.
Skrivbordet använder sin egen matchande corlib; PS4-paketet använder runtime-
versionens pinnade corlib. Host-Mono är inte ett bevis för PS4-runtimens API-stöd.

## Verifieringsnivåer

För varje paketerad ROM körs 1200 ramar med skriptad input, pixelhashar,
färgkontroll och WAV-utmatning. Tyst ljud, fel samplingsmängd eller utebliven
bildförändring stoppar paketbygget. Att en titelskärm passerar är inte ett
fullständigt spelbarhetstest. Fysisk PS4-körning/ljud/hastighet återstår.

Native tester täcker GB/SMS/MD-bildmått, heltalsskala, PAL-period,
buffertväxling, förhandsbildens gränser/ägande, kontroller och ljudkö/livscykel.
UI och ljud passerar ASan/UBSan; LeakSanitizer måste vara avstängd i sandlådan
som använder ptrace. Shellcheck och det äldre GB-pixeloraklet passerar.

Slutpaketets SHA-256 och resultat per spel finns i [handoff](HANDOFF-2026-09-29.md).
0.11 är byggd och paketvaliderad. Vid dess leveranskontroll var stickan inte
ansluten. Den återkom under 0.12-arbetet: 0.12 ligger nu på stickan, med
verifierad SHA-256-readback. Stickan lämnades monterad.

## Egen appikon i 0.12

`assets/icons/icon0.png` är en 512x512 RGBA PNG som paketeras som
`sce_sys/icon0.png`. Originalbild och exakt imagegen-prompt ligger i samma
katalog. 0.12 är en ny paketrevision för appikonen; managed/native-spelarens
interna 0.11-etiketter beskriver kärnimplementationen. 0.11-paketet bevaras.
