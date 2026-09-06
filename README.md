# SMPTE History — macOS

Natywny monitor wejściowego LTC/SMPTE do pracy obok Resolume Arena. Pokazuje timecode, mierzone FPS oraz przerwy w odbiorze poprawnych ramek z ostatnich 10 sekund. Ręczny Start/Stop, przegląd każdej ramki i eksport CSV.

## Uruchomienie

1. Pobierz plik **SMPTE-History-1.0.2.dmg** z [Releases](https://github.com/in4you-jc/SMPTE-History/releases/latest), otwórz go i przeciągnij **SMPTE History.app** na skrót **Applications**. Następnie uruchom aplikację z folderu Aplikacje. Nie potrzebujesz kodu źródłowego, Pythona ani dodatkowych bibliotek. Wymaga macOS 13 lub nowszego; zawiera wersje Apple Silicon i Intel. Alternatywnie w archiwum ZIP znajdziesz aplikację w `dist/SMPTE History.app`.
2. W Arenie sprawdź **Preferences → Audio → SMPTE**: urządzenie i numer kanału używanego przez SMPTE 1 lub SMPTE 2.
3. W monitorze wybierz **to samo urządzenie oraz ten sam kanał**. Kanały są numerowane od 1. „Odśwież” odczytuje ponownie listę urządzeń.
4. Pozostaw próg zaniku **120 ms** lub wybierz 80/200/500 ms. Kliknij **Start · nowy zapis**. Przy pierwszym uruchomieniu macOS poprosi o dostęp do mikrofonu — to uprawnienie obejmuje również wejścia interfejsów audio.
5. Po zdarzeniu kliknij **Stop · zachowaj historię**. Odbiór audio i timer odświeżania zostaną wyłączone. Możesz przejrzeć ramki i nacisnąć **Zapisz CSV**.

Start czyści poprzednią historię. Eksport podczas nasłuchu zapisuje migawkę z chwili naciśnięcia przycisku. Monitor nie może odzyskać danych sprzed włączenia. Zamknięcie okna kończy aplikację i zwalnia wejście audio.

Przycisk **Pokaż demo** pokazuje oznaczoną symulację 25 FPS z przerwą; nie otwiera wejścia audio.

## Co dokładnie mierzy

- **Timecode**: etykieta ostatniej poprawnie zdekodowanej ramki, `HH:MM:SS:FF`; średnik przed FF oznacza flagę drop-frame. Przy zaniku ostatnia wartość pozostaje na ekranie, ale status zmienia się na BRAK LTC.
- **FPS · pomiar**: mediana pomiarów z ostatniej sekundy. Pojedyncza ramka jest mierzona jako częstotliwość próbkowania / liczba próbek w ramce LTC. Tabela zawiera surowe pomiary poszczególnych ramek.
- **FPS nie jest odczytem ustawienia Areny ani gwarantowanym rozpoznaniem nominalnego formatu.** LTC nie przesyła pełnej informacji pozwalającej rozróżnić wszystkie nominalne częstotliwości. Przy zmienionej prędkości odtwarzania pomiar także się zmienia. Pierwsza ramka po uzyskaniu synchronizacji może mieć mniej dokładny pomiar. Zegar interfejsu i kształt przebiegu wpływają na wynik. Obsługiwane typowe formaty: 23,976 / 24 / 25 / 29,97 / 30; filtr przyjmuje pomiary 15–40 FPS i etykiety klatek 0–29.
- **Zanik**: przez wybrany próg nie otrzymano poprawnej ramki. Nie dowodzi to fizycznej ciszy: może oznaczać również zakłócony sygnał, niewłaściwy kanał lub brak dostępu do urządzenia. Czas początku przerwy jest szacowany od chwili oczekiwanej następnej ramki; wykrycie następuje dopiero po przekroczeniu progu. Krótsze przerwy nie są raportowane jako zaniki.
- **Zaniki w oknie**: liczba przerw po odebraniu LTC, które przecinają widoczne 10 sekund. Początkowe oczekiwanie bez LTC jest opisane osobno i nie powiększa tego licznika. Długość przerwy na liście/CSV jest przycięta do widocznego okna; „otwarty” oznacza brak powrotu sygnału przed migawką.
- Bufor używa zegara monotonicznego komputera. Skok, cofnięcie lub przejście timecode przez północ nie zmienia długości okna. Strzałka przy DF/NDF oznacza odebrany LTC odtwarzany wstecz.

## Współpraca z Resolume

To **osobna aplikacja towarzysząca, nie wtyczka FFGL ani panel wewnątrz Areny**. Dekoduje wejście LTC niezależnie. Nie mierzy wewnętrznego stanu synchronizacji, opóźnienia/offsetu ani ustawionych FPS Areny. Wynik może więc różnić się od jej panelu SMPTE.

Dokumentacja REST API dołączona do Areny **7.23.2** (`rest/docs/swagger.yaml`) nie zawiera SMPTE/timecode. Wybrana integracja audio nie zależy od numeru wersji REST API. Aplikacja nie jest wtyczką do Areny.

Urządzenie/sterownik musi pozwalać Arenie i monitorowi na jednoczesny odbiór. Aplikacja nie zmienia systemowego wejścia domyślnego ani nominalnej częstotliwości urządzenia. Gdy sterownik nie pozwala współdzielić wejścia, potrzebne będzie udostępnienie sygnału na drugim wejściu lub istniejący routing audio. Jednocześnie monitorowany jest jeden wybrany kanał.

Brak dźwięku w głośnikach jest prawidłowy: monitor nie tworzy wyjścia audio. Używaj bezpośredniego kanału LTC, bez redukcji szumu, trybu izolacji głosu i innych procesorów. Sam napis „mikrofon” w uprawnieniach macOS nie oznacza, że należy używać mikrofonu laptopa.

## Obciążenie i dane

Dekoder libltc jest wykonywany lokalnie w C, interfejs odświeża się 10 razy/s. Audio nie jest zapisywane na dysk ani wysyłane do sieci. W pamięci jest bieżące okno i ograniczona kolejka odbioru; przepełnienie jest zgłaszane. Po Stop nie ma aktywnego nasłuchu. Rzeczywiste obciążenie należy sprawdzić na docelowym interfejsie podczas próby — brak deklaracji konkretnego procentu CPU.

## CSV

Pierwsza kolumna: `frame` albo `gap`. Pozostałe kolumny:

| Kolumna | Znaczenie |
|---|---|
| session_seconds | Czas od uruchomienia nasłuchu |
| age_seconds | Wiek względem chwili migawki |
| timecode | Zdekodowana etykieta SMPTE |
| measured_fps | Surowy pomiar FPS danej ramki |
| drop_frame, reverse | Flagi 0/1 |
| duration_seconds | Długość przerwy w obrębie okna |
| open_at_snapshot | 1, gdy przerwa nie zakończyła się przed migawką |
| initial_no_signal | 1, gdy chodzi o brak sygnału przed pierwszą ramką |

Wiersze ramek są chronologiczne; wiersze przerw znajdują się po nich. Separator to przecinek, znak dziesiętny to kropka, kodowanie UTF-8.

## Budowanie i testy

Wymagane Apple Command Line Tools / Xcode. Zależność libltc 1.3.2 jest dołączona w źródłach, więc kompilacja nie wymaga pobierania pakietów.

```sh
bash build.sh
bash test.sh
bash package-dmg.sh
```

`build.sh` tworzy uniwersalną aplikację i podpisuje ją lokalnie ad hoc. Nie jest to podpis Developer ID ani notaryzacja Apple. Gdy przeniesiona na inny Mac aplikacja zostanie zablokowana, użyj systemowej opcji „Otwórz mimo to” w Prywatność i ochrona albo zbuduj aplikację ze źródeł. Nie trzeba wyłączać Gatekeepera.

Testy automatyczne obejmują dekodowanie faktycznego przebiegu LTC generowanego przez libltc, wiele FPS, 44,1/48 kHz, odwróconą polaryzację, wybór kanału stereo, ciszę, powrót sygnału, szum oraz logikę historii i CSV. To testy programowe; nie zastępują testu fizycznego wejścia wraz z Areną. Demo interfejsu jest symulacją, a nie testem urządzenia.

## Źródła i licencje

Kod aplikacji w `Sources/` i testy: MIT (plik `LICENSE`). Biblioteka **libltc 1.3.2**: LGPL-3.0-or-later, pełne źródła i licencja w `vendor/libltc`, commit `bf84b01097a1789c0296cc5fcfc3bf4608407930`. Aplikacja linkuje dynamicznie do `Contents/Frameworks/libltc.dylib`; skrypt pozwala przebudować i ponownie podpisać aplikację po zmianie biblioteki.

- [Resolume: SMPTE](https://resolume.com/support/en/smpte)
- [Resolume: REST API](https://resolume.com/support/en/restapi)
- [libltc — kod źródłowy](https://github.com/x42/libltc/tree/v1.3.2)
- [libltc — dokumentacja](https://x42.github.io/libltc/)
- [Apple: dostęp do wejść audio](https://support.apple.com/en-us/102071)
