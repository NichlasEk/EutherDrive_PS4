# Automatiserad PS4-benchmark 0.01

Separat app: **EutherDrive Auto Benchmark**, titel-ID `EDBM00001`.
Den vanliga spelaren (`EDRM00001`) behöver inte avinstalleras.
Paket: `dist/eutherdrive-auto-benchmark-0.01.pkg`.

## På konsolen

1. Installera paketet från USB. Stäng den gamla spelappen före testet.
2. Låt USB-stickan sitta kvar och starta **EutherDrive Auto Benchmark**.
3. Appen kör Black Belt, Alex Kidd, Sonic 1, Streets of Rage 2 och Zelda,
   två gånger vardera. Knappsekvenserna körs automatiskt. Tryck inte på
   spelknappar; skärmen visar status, inte interaktivt spel.
4. Vänta på `SUITE COMPLETE cases=10` och slutlig USB-export. Körningen kan
   ta många minuter, särskilt Zelda. Stäng via PS-knappen när exporten är klar.
5. Ta tillbaka stickan till datorn. Resultaten finns i **EutherDriveBench**.
   Behåll hela katalogen; den senaste `run-NNNN` är den sista exporten.

Varje USB-export skapar en ny mapp, aldrig över en gammal körning.
`export.txt` listar varje fil och om kopieringen lyckades. `complete.txt`
betyder att serien avslutats; kontrollera även felräknaren och `export.txt`.
En `.partial` är en avbruten/misslyckad kopia och ska inte räknas som komplett.
Startkontroll och export efter varje deltest bevarar information även om ett
senare test stannar. Om USB saknas eller inte är skrivbar går mätningen ändå.

Interna original: `/data/eutherdrive-bench/run-NNNN/`.
Native start-/runtime-/återställningslogg: `/data/eutherdrive-bench/native-probe.log`.
De kan hämtas via konsolens FTP-server. Vid krasch innan första USB-exporten
är native-loggen särskilt viktig. Den filen ersätts vid nästa appstart, så hämta
den via FTP före omstart om du behöver den senaste kraschens native-logg.
En omstart av appen startar en **ny hel serie**;
gamla interna körningar och USB-exporter skrivs inte över.

## Vad vi får

- `summary.log`: framsteg, tider, fel och referensjämförelse.
- `case-00-r1.log` osv: ROM/input-SHA256, pipeline/wall FPS, core/audio/copy-ms,
  p95, GC-räknare och fullständiga bild-/ljudhashar med sampleantal.
- `case-00-r1.ppm` osv: slutbild efter tidtagningen.
- `suite.tsv`: exakta ROMar, knappsekvensernas hashar, uppvärmning och referenshashar.
- `build-info.json`: spelarbygge, Mono, paketfiler och benchmarkkällornas hashar.
- `native-probe.log` i exporten: även native avslutning och rättighetsåterställning
  i den sista exporten efter Mono-cleanup.

Varje deltest mäter 300 bilder efter 1200 uppvärmningsbilder för SMS/Sonic,
1800 för Streets of Rage 2 och 3000 för Zelda. `reference=MATCH` jämför med
PS4-Mono i shadPS4. `DIFFER` sparas öppet: mätningen är genomförd men resultatet
är inte en verifierad identisk arbetslast. Zelda skiljer redan mellan desktop
Mono och PS4-Mono; se `PERFORMANCE-WORKFLOW.md`.

Detta mäter CPU-kärna, genererat ljud och bildbuffertkopiering. Löpande
Vulkan-presentation och fysisk ljudkö ingår **inte**. Statusvisning, loggning,
SHA256, slutbilder och USB-export sker utanför de uppmätta intervallen.
Kärnans redan befintliga diagnostik är oförändrad, liksom produktionens main.exe.
Hashning av varje bild/ljudbuffer ingår i wall FPS men inte pipeline FPS.

## Teknik och avgränsning

Den paketerade spelaren är oförändrad, SHA256
`ea9c9340a4c7ae20bc30da58216ef4b042d429fcdd2c35dcb23fc70b0ea333d9`.
Testerna använder separata Mono-domäner för att återställa statiskt chiptillstånd.
Att bara skapa en ny Emulator räckte inte: upprepade SMS-test fick olika ljudhashar.
PS4:s BCL saknar managed CreateDomain och unload-callbacken. Native-API skapar
istället tio domäner, som behålls fram till slutlig runtime-avslutning. Antalet
är avsiktligt begränsat till tio; ingen obegränsad testloop finns.

USB-koden är anpassad från ut99-orbis writable log exporter, med egna katalognamn
så att UT99:s loggar/mappningar lämnas ifred. Den provar faktisk skrivning,
använder skrivbar sandboxmappning när direkt USB-åtkomst saknas, kontrollerar
återställning av rättigheter och exporterar via `.partial`, fsync och rename.
Källursprung/licens finns i `probes/benchmark/usb/`. USB-mappning sker **före**
Mono-start; export under testet kräver inga nya rättighetsbyten. Den befintliga
JIT-transaktionen och firmware-9.60-gränsen är oförändrade.

FTP lämpar sig för att hämta de interna resultaten utan att flytta stickan.
Paketet är en vanlig app med `/app0`, Mono-moduler och beprövad startväg.
Ingen bin-loader-payload ingår eller har verifierats; eboot.bin ska inte skickas
som ett vanligt bin-loader-payload.

## Bygg och kontroller

```sh
python3 scripts/package-benchmark.py
python3 scripts/test-benchmark-usb.py
python3 scripts/test-benchmark-emulator.py
```

Bygget utgår från den validerade 0.20-spelarens stage och avvisar annat main.exe.
Det har egen build-katalog och ändrar inte spelarens `current-pkgroot`.
USB-testerna återanvänder ut99-orbis feltester med mockade kernel-anrop.
Emulatortestet behåller paketets managed filer men bygger en separat native host
utan fysisk credential-transaktion. Det monterar en privat katalog som USB och
kontrollerar alla tio resultat samt exporterade bytes. Det bevisar inte den
nya benchmarkappens funktion på fysisk PS4.

ROMar och runtimebinärer ligger endast i de lokala privata byggartefakterna.

## Verifierat 2026-09-30

- `build/benchmark-tests/emulator-20260930-132939/`: alla tio fall matchar
  PS4-Mono-referensen, noll fel/differenser, Mono-cleanup returnerar.
- Slutexport `usb/EutherDriveBench/run-0023/`: 25 filer, inga kopieringsfel,
  varje exporterad intern resultatfil byteidentisk. Inga `.partial` kvar.
- Desktop `build/benchmark-tests/desktop-isolated-results/run-0001/`: alla fem
  par reproducerbara; Sega matchar PS4-referensen, båda Zelda-fallen har den
  tidigare dokumenterade runtime-skillnaden.
- USB lagrings-/exporttester med ASan/UBSan passerar, inklusive felvägar.
- Releasepaketet är validerat av PkgTool och alla 76 extraherade filer matchar
  stage. `build/benchmark-tests/package-validation.json` har beviset.
- Slutpaketeringen lägger till cleanup om domän-API-symbolupplösning misslyckas.
  Benchmarkens managed IL är jämförd mot hela svittens testade assembly och
  är identisk bortsett från kompilatorns nya modul-GUID. Källhashar är samma.

Releasepaket SHA256:
`547f96aa6f0eda52a5bbe292a914308bfd2355f606048bb3b63e796a200976ff`.
Den fysiska konsolkörningen återstår. Dessa kontroller verifierar paket,
smoketest och simulerad USB-export, inte verkliga PS4-prestanda.

Leverans: paket, portabel SHA256-fil och EUTHERDRIVE-BENCHMARK-0.01.txt
kopierade till SCUMMVM_PS4. Samtliga tre filer lästes tillbaka och matchade
originalen. Stickan synkades och avmonterades med udisksctl.
