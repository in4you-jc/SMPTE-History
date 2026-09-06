# SMPTE History 1.0.2

Dodano ikonę aplikacji na podstawie grafiki timecode: zielone cyfry na czarnym tle, dostosowane do kwadratowego formatu. Podpis „Developed by Black Light Design” pozostaje w stopce.

- Przesuwające się okno ostatnich 10 sekund: timecode, mierzone FPS i przerwy w odbiorze poprawnych ramek.
- Wybór urządzenia audio i kanału, ręczny Start/Stop, eksport CSV.
- Tryb demonstracyjny bez dostępu do wejścia audio.
- macOS 13+, Apple Silicon i Intel (Universal Binary).

**Najprostsza instalacja:** pobierz `SMPTE-History-1.0.2.dmg`, otwórz go i przeciągnij `SMPTE History.app` na skrót `Applications`. Uruchom aplikację z folderu Aplikacje. Wszystkie potrzebne biblioteki są już w środku.

**Instalator PKG:** pobierz `SMPTE-History-1.0.2.pkg` i przejdź przez instalator macOS. Aplikacja trafi do `/Applications/SMPTE History.app`. Przed aktualizacją zamknij działającą aplikację. Pakiet wymaga macOS 13+ i może poprosić o hasło administratora; nie ma podpisu Developer ID Installer ani notaryzacji Apple. Plik `.pkg.sha256` zawiera sumę kontrolną instalatora.

Alternatywnie pobierz archiwum `SMPTE-History-macOS.zip`, rozpakuj je i otwórz `SMPTE-History/dist/SMPTE History.app`. Pełna instrukcja i kod źródłowy znajdują się w archiwum oraz repozytorium. Plik `.dmg.sha256` zawiera sumę kontrolną obrazu DMG.

Aplikacja jest podpisana lokalnie ad hoc, bez notaryzacji Apple. Jest osobnym monitorem sygnału audio, nie wtyczką FFGL. Mierzone FPS nie są odczytem ustawienia Areny. Przed użyciem podczas wydarzenia sprawdź współdzielenie wejścia audio i odbiór LTC na docelowym sprzęcie.

Weryfikacja: testy programowe dekodera i historii, kompilacja dla obu architektur, sprawdzenie podpisu oraz interfejsu demonstracyjnego. Nie przeprowadzono testu z fizycznym źródłem LTC i Areną.
