# SMPTE History 1.0.0

Pierwsze wydanie niezależnego monitora LTC/SMPTE dla macOS, do pracy obok Resolume Arena.

- Przesuwające się okno ostatnich 10 sekund: timecode, mierzone FPS i przerwy w odbiorze poprawnych ramek.
- Wybór urządzenia audio i kanału, ręczny Start/Stop, eksport CSV.
- Tryb demonstracyjny bez dostępu do wejścia audio.
- macOS 13+, Apple Silicon i Intel (Universal Binary).

Pobierz archiwum `SMPTE-History-macOS.zip`, rozpakuj je i otwórz `SMPTE-History/dist/SMPTE History.app`. Pełna instrukcja i kod źródłowy znajdują się w archiwum oraz repozytorium.

Aplikacja jest podpisana lokalnie ad hoc, bez notaryzacji Apple. Jest osobnym monitorem sygnału audio, nie wtyczką FFGL. Mierzone FPS nie są odczytem ustawienia Areny. Przed użyciem podczas wydarzenia sprawdź współdzielenie wejścia audio i odbiór LTC na docelowym sprzęcie.

Weryfikacja: testy programowe dekodera i historii, kompilacja dla obu architektur, sprawdzenie podpisu oraz interfejsu demonstracyjnego. Nie przeprowadzono testu z fizycznym źródłem LTC i Areną.
