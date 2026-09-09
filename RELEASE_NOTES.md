# SMPTE History 1.1.0

Ciągły odbiór LTC i przekazywanie wybranego kanału na wyjścia tej samej karty dźwiękowej.

- Automatyczne odczytanie liczby kanałów wyjściowych CoreAudio; osobny włącznik i poziom 0–100% dla każdego OUT.
- Wycisz wszystkie bez zatrzymywania nasłuchu.
- Zamroź historię i eksportuj migawkę bez przerywania przekazywania LTC.
- Wspólny zegar wejścia i wyjść w AUHAL; zatrzymanie przy błędzie lub zmianie konfiguracji karty.
- Zachowane: 10-sekundowa historia, diagnostyka FPS/zaników, ikona i podpis Developed by Black Light Design.

Instalacja: zamknij poprzednią wersję i uruchom **SMPTE-History-1.1.0.pkg**. Alternatywnie otwórz DMG i przeciągnij aplikację do Applications. Wymagany macOS 13+, Apple Silicon lub Intel. Pakiety nie mają podpisu Developer ID ani notaryzacji Apple.

Obsługa: wybierz kartę i wejście LTC, w zakładce **Wyjścia karty** włącz przekazywanie oraz wybrane OUT, ustaw poziomy i naciśnij Start. 100% = poziom wejściowy. Domyślnie kanały są wyciszone. „Stop · wyłącz audio” zatrzymuje wejście i wyjścia; „Zamroź historię” zachowuje działający tor.

Przekazywany jest oryginalny dźwięk, bez regeneracji LTC/freewheel. Obsługiwane są wejścia i wyjścia jednego urządzenia CoreAudio. Nie podłączaj wyjścia z powrotem do wejścia LTC. Rzeczywiste opóźnienie i poziom elektryczny zależą od karty. Sterownik musi pozwalać na współdzielenie z Areną.

Weryfikacja: testy dekodera, historii i routingu (128 kanałów, niezależne poziomy, mute, ponowne dekodowanie sygnału wyjściowego). Sprawdzono również uruchomienie i zatrzymanie AUHAL na VB-Cable z wyciszonymi wyjściami, regulację kanałów w UI oraz zamrożenie historii bez zatrzymania audio. Fizyczny tor z kartą i odbiornikami LTC wymaga próby przed użyciem podczas wydarzenia.
