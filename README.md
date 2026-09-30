# EutherDrive PS4 – startplan

Aktuell testkandidat: **0.18 Vulkan med Doom3:s drivrutin**,
[Master System / Mega Drive / SNES](docs/CONSOLE-PLAYER.md).
0.13 fungerar på fysisk PS4 men går långsamt. Vulkan 0.16/0.17 startade inte.
0.18 korrigerar en trasig SELF-konvertering och använder Doom3:s låsta
Vulkan-bibliotek. Första GPU-frame/flip passerar i shadPS4; fysisk start och
hastighet återstår att testa. CPU 0.15 finns kvar som jämförelse.
Den nya appikonen är kvar.

Aktuell fortsättning: [status och handoff 2026-09-29](docs/HANDOFF-2026-09-29.md).

2026-09-29. Status: Mono 0.06 fysiskt verifierad på PS4 9.60/GoldHEN 2.4.
GB-spelversion 0.08 med Super Mario Land är nu bekräftad fungerande av användaren
på fysisk PS4. 0.09 lägger till ljudutmatning, GB/GBC-bibliotek och spelbyte.
Dess färgbild/ljuddata är skrivbordsverifierade; fysiskt 0.09-test återstår.
Se [GB-spelversionerna](docs/GB-PLAYER.md).

## Mål och riktning

En egen EutherDrive-frontend för PS4 med handkontroll, spelbibliotek och skins.
Återanvänd C#-kärnorna från `../EutherDrive_Android` och bevara känslan genom
bilder, färger, omslag, ljud och animationer. Avalonia behövs inte i PS4-versionen.
Använd OpenOrbis och våra befintliga PS4-lösningar; bygg de delar som saknas.

Första spelbara målet är **ett system, en ROM, bild, ljud och DualShock**.
Den första kärnan ska vara JIT-fri. Mega Drive, en avgränsad Game Gear-väg eller
en annan liten kärna är kandidater; välj efter beroende- och mognadsgranskningen.
Nuvarande Master System-adapter använder Mega Drive-kärnan som fallback och är
därför inte automatiskt det minsta portningsmålet.
Med JIT-fri kärna menas här att emulatorkoden inte genererar kod vid körning;
Mono-runtimen kan fortfarande JIT-kompilera vanlig IL om PS4-vägen fungerar.
Full systemlista och avancerade skins kommer stegvis.

## 0. Återanvänd den verifierade native PS4-baslinjen

Skapa inte ännu en generell hårdvaruprobe här. `../ut99-orbis` har Vulkan Probe
0.10 med fysiskt verifierad clear, draw, GPU-synkronisering och presentation.
`../ScummVM-PS4` har både den mindre video-/ljud-/kontrollproben och en fysiskt
körd Vulkan-applikation. Återanvänd låsta revisioner av dessa implementationer
och deras testresultat som baslinje.

ScummVM:s lilla statusprobe har komplett bild-, ljud- och kontrollverifiering i
shadPS4 men saknar fortfarande ett uttryckligt fysiskt ljudtest. Verifiera därför
EutherDrives faktiska ljudväg på konsolen när den kopplas in i stället för att
duplicera hela proben nu.

**Klart:** native bygg-/paketeringsväg och Vulkan-presentation har fysisk
PS4-evidens i referensprojekten. EutherDrive-specifik integration, ljud och
runtime förblir separata kommande testgrindar.

## 1. Avgör C#-vägen först

- Inventera vilka .NET-API:er en vald kärna faktiskt behöver. Kärnprojektet
  använder .NET 8 och drar i dag in flera andra kärnor och paket; skapa ett
  avgränsat testprojekt så att de inte följer med automatiskt.
- Undersök OpenOrbis/Mono som första managed-experiment. Befintliga exempel är ingen
  garanti för stöd för vår moderna .NET-kod. Granska runtime, laddning,
  firmwareberoenden och hur beroendena kan byggas och levereras.
- Börja inte med den befintliga `net8.0`-assemblyn. Fastställ först vilken
  framework-/BCL-profil PS4-runtimen faktiskt kan köra och bygg proben för den.
- Kör ett litet C#-test med arrayer/Span, generics, GC, undantag, filåtkomst,
  tidtagning och anrop över en enkel C-gränsyta. Testa trådar om kärnan kräver dem.
- Om Mono-vägen inte räcker: bedöm en anpassad runtime/AOT-lösning separat innan
  större frontendarbete. NativeAOT är inget färdigt PS4-byggmål; det löser inte
  automatiskt runtimeberoenden eller emulatorernas dynamiska kodgenerering.
- Första kärnan får inte kräva `System.Reflection.Emit`, `DynamicMethod` eller
  annan runtime-genererad kod. JIT-baserade N64-/arkadkärnor kommer senare.

**Klart när:** testet kör på PS4 och nödvändiga API:er fungerar. Emulatorresultat
och fysisk konsol dokumenteras var för sig.

Första proben finns i `probes/runtime` med reproducerbara bygg- och
paketeringsskript. Se `docs/MANAGED-RUNTIME-PROBE.md`. Den provar runtime och
native-anrop men innehåller ännu ingen emulatorkärna.

**Verifierat på fysisk PS4:** runtime-probe 0.06 passerar C#-testerna, Mono-
nedstängningen och verifierad återställning av processrättigheter på användarens
rapporterade firmware 9.60 / GoldHEN 2.4. Slutrad:
`RESULT PASS MONO=managed RESTORE=verified`. Detta är baslinjen inför kärnportning;
spel, bildflöde från kärnan, ljud och prestanda återstår.

## 2. Koppla in en emulator­kärna

Första avgränsade kärnbygget finns nu: [GB core probe 0.07](docs/GB-CORE-PROBE.md).
Desktop-testet passerar tre pixelkontroller med simulerad input. PS4-paketet är
byggt, men fysisk kärnkörning återstår. Det första testet använder DMG-läge.

Utgå från `../EutherDrive_Android/EutherDrive.Core/IEmulatorCore.cs`:
`LoadRom`, `RunFrame`, BGRA-bild, PCM-ljud och knapptillstånd.

- Bevara kärnlogiken och gör plattformsanpassningar vid dess gränser.
- Koppla bilden till texturvisning/VideoOut, ljudet till SceAudioOut och
  handkontrollen till ScePad. Återanvänd relevanta delar från ScummVM/UT99/Doom 3
  efter granskning av beroenden, licenser och faktiska testresultat.
- Låt ROM/BIOS ligga separat från paketet. Lägg skrivbara inställningar och
  sparfiler i en egen katalog, exempelvis `/data/eutherdrive`.
- Jämför deterministiska bild-/tillståndsresultat med samma kärna på datorn;
  mät sedan bildtider, ljudstabilitet och minnesanvändning på konsolen.

**Klart när:** ett valt spel går att styra med stabil bild och ljud på PS4.

## 3. Bygg en liten frontend med EutherDrive-känsla

- Granska befintliga skinresurser och skinformat i Android-/desktopprojektet.
  Skilj bilder, färger och layoutdata från Avalonia-specifika komponenter.
- Definiera en första stödd delmängd av skinformatet och en tydlig standardlayout
  för sådant som ännu inte stöds. Återanvänd resurser där det går.
- Implementera text, bildrutor, omslag, markering/fokus och handkontrollsnavigering
  med Orbis/native grafik. Första vyn: spellista, omslag, Start och Inställningar.
- Testa ett befintligt skin visuellt. Bygg sedan ut animationer och ljudeffekter.

**Klart när:** ett spel kan väljas och startas från en skinnad meny och man kan
återvända till menyn utan att tappa kontroll, ljud eller grafikresurser.

## 4. Sparning och fler system

Lägg till SRAM först, sedan savestates och verifierad återläsning. Nuvarande
savestate-kod använder reflection och måste granskas för vald runtime/AOT-väg.
Utöka en kärna i taget. N64 och Gauntlet kommer senare: de har dynamisk
kodgenerering och ytterligare prestanda-/grafikberoenden. Lova ingen bildfrekvens
innan den mätts på konsolen.

## Grafikväg: OpenOrbis, Vulkan och VideoOut

### Ansvarsfördelning

- **OpenOrbis** är utgångspunkten för att bygga och paketera native PS4-kod och
  ansluta till konsolens tjänster: VideoOut, ScePad, SceAudioOut och filåtkomst.
  C#-körmiljön är ett separat arbete enligt steg 1.
- **Vår Vulkan-stack** är en separat komponent ovanpå PS4:s GNM/GPU-väg.
  Utgå från arbetet i ScummVM, UT99 och Doom 3, med låsta källrevisioner och
  dokumenterade patchar. Räkna bara med funktioner som faktiskt är verifierade
  i den valda stacken; den är ingen garanti för full Vulkan-kompatibilitet.
- **VideoOut** sköter den sista presentationen på TV:n. Renderer och
  presentationskod ska ha en gemensam ägare för bildbuffertar och synkronisering.
- **OrbisGL** är en möjlig referens för C#-gränssnitt och input. Det är ett
  separat projekt från OpenOrbis och ska inte bli ett oavsiktligt krav för
  Vulkan-renderern. De resurser som behövs för skins kan ritas i vår egen frontend.

### Första renderern

Den planerade vägen för kärnor som redan producerar färdiga bildrutor är:

```text
C#-kärna: RunFrame → BGRA-bild + mått + stride
    → smal C-gränsyta → native stagingbuffer
    → Vulkan-textur → skalad bildruta + skin/text/meny
    → GPU-slutförande → VideoOut → TV
```

Börja med en fast 1280×720-utmatning, korrekt bildförhållande och valbar
heltalsskalning. Nearest-filter är första vägen för skarp pixelgrafik;
bilinjär skalning kan läggas till när format och sampling är kontrollerade.
Vulkan visar och komponerar här kärnans färdiga bild. Att flytta själva den
emulerade maskinens rendering till GPU:n är ett senare, separat projekt.

C-gränsytan ska dokumentera pixelformat, radlängd, buffertägande och livslängd.
Kopiera först bilden till native minne under ett kort anrop, så att GPU:n aldrig
läser en flyttad eller återanvänd C#-buffer. Hantera BGRA/RGBA uttryckligen.
Återanvänd staging-, textur- och kommandobuffertar först när föregående GPU-arbete
är klart. Optimera bort kopior efter mätning, inte i första prototypen.

Skins byggs av texturer, rektanglar, text via fontatlas och alfablendning.
Använd egna små shaders direkt i Vulkan. ScummVM:s GL-kompatibilitetslager
behöver inte följa med när frontenden inte använder OpenGL.

### Erfarenheter att ta med och verifiera igen

- Använd den nya clear-vägen med kompilerade shaders från PS4-arbetet som
  utgångspunkt. Testa clear innan någon annan draw har initierat grafikläget.
- GPU-kommandon måste ligga i korrekt GPU-åtkomligt minne. Ett lyckat submit-anrop
  räcker inte som bevis för att GPU:n slutfört arbetet eller att TV:n visar bilden.
- Bevara korrekt grafikläge mellan spelbild, menyer och skinritning, särskilt
  viewport, scissor, texturbindningar och blandning.
- Återanvändningen är ännu inte verifierad i EutherDrive. ScummVM:s fysiska
  framgång och Doom 3:s emulatorresultat är referenspunkter för nya tester.

### Första grafikproven

1. Native prov utan C#: kallstart med färgad clear, rörligt mönster och
   växlande presentation. Bekräfta resultatet på TV:n.
2. Kända BGRA-testbilder: kontrollera kanalordning, stride, orientering och
   skalning med pixeljämförelse där readback stöds.
3. Spelbild och halvtransparent meny med text: öppna/stäng upprepade gånger
   och kontrollera att varken bild, fokus eller resurser försvinner.
4. Samma bildflöde från C#-proben, därefter från en riktig emulator­kärna.

Bildvisningen för en enkel kärna ska provas före N64:s eller andra avancerade
GPU-backenders shader- och compute-krav. Att de också använder Vulkan innebär
inte att deras funktioner redan stöds av PS4-stacken.

## Arbetsplats och nästa konkreta steg

Arbeta här: `/home/nichlas/EutherDrive_PS4`.
Låt det befintliga Android-/Linux-arbetet ligga kvar i sin checkout; där finns
pågående lokala ändringar. Börja med en separat minimal runtime-probe här.
Välj därefter hur en bestämd kärnrevision ska delas mellan projekten.

Föreslagen struktur när implementationen börjar:

```text
docs/             beslut, beroenden och testresultat
probes/           små C#-/runtimeprov
platform/ps4/     native bild, ljud, input och filåtkomst
frontend/         meny, skinläsning och navigation
scripts/          reproducerbara bygg- och testkommandon
artifacts/        lokala loggar/bilder, ignoreras av Git
```

## Referenser för fortsatt granskning

- Kärna och skins: `../EutherDrive_Android`
- PS4-erfarenheter: `../ScummVM-PS4`, `../ut99-orbis`, `../Doom3-Ps4`
- Mono-startpunkt: https://github.com/marcussacana/PS4-OpenOrbis-Mono
- C#-frontend som referens: https://github.com/marcussacana/OrbisGL
- NativeAOT-begränsningar: https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/

Externa projekt är kandidater att granska, inte redan valda eller verifierade
beroenden för EutherDrive PS4.
