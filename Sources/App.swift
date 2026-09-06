import SwiftUI
import AppKit
import AVFoundation
import UniformTypeIdentifiers

struct InputDevice: Identifiable {
    let id: UInt32
    let name: String
    let uid: String
    let channels: Int
    let rate: Double
}

final class Monitor: ObservableObject {
    @Published var devices: [InputDevice] = []
    @Published var deviceID: UInt32 = 0
    @Published var channel = 1
    @Published var threshold = 120
    @Published var running = false
    @Published var requesting = false
    @Published var history = History()
    @Published var message = "Wybierz to samo wejście audio i kanał LTC co w Arenie."
    @Published var error = ""
    @Published var demo = false
    private var capture: OpaquePointer?
    private var timer: Timer?
    private var activity: NSObjectProtocol?
    var selected: InputDevice? { devices.first { $0.id == deviceID } }

    init() {
        refreshDevices()
        if CommandLine.arguments.contains("--demo") { loadDemo() }
    }
    func refreshDevices() {
        guard !running else { return }
        var raw = [SHDevice](repeating: SHDevice(), count: 128)
        let count = sh_devices(&raw, Int32(raw.count))
        devices = raw.prefix(Int(count)).map { d in
            var name = d.name, uid = d.uid
            let n = withUnsafePointer(to: &name) { ptr in ptr.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) } }
            let u = withUnsafePointer(to: &uid) { ptr in ptr.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) } }
            return InputDevice(id: d.id, name: n, uid: u, channels: Int(d.channels), rate: d.sample_rate)
        }
        let saved = UserDefaults.standard.string(forKey: "deviceUID")
        if !devices.contains(where: { $0.id == deviceID }) {
            deviceID = devices.first(where: { $0.uid == saved })?.id ?? devices.first?.id ?? 0
        }
        channel = min(max(1, channel), selected?.channels ?? 1)
    }
    func start() {
        guard !running && !requesting else { return }
        guard let requested = selected else { error = "Brak wybranego wejścia audio. Podłącz interfejs i kliknij Odśwież."; return }
        let requestedChannel = channel
        requesting = true
        AVCaptureDevice.requestAccess(for: .audio) { [weak self] allowed in
            DispatchQueue.main.async {
                guard let self else { return }
                self.requesting = false
                if allowed { self.beginCapture(uid: requested.uid, channel: requestedChannel) }
                else { self.error = "Zezwól SMPTE History na dostęp do mikrofonu w Ustawieniach systemowych → Prywatność i ochrona → Mikrofon. Uprawnienie obejmuje również wejścia interfejsu audio." }
            }
        }
    }
    private func beginCapture(uid: String, channel requestedChannel: Int) {
        refreshDevices()
        guard let device = devices.first(where: { $0.uid == uid }), requestedChannel <= device.channels else {
            error = "Wybrane wejście zostało odłączone lub zmieniła się liczba kanałów. Wybierz wejście ponownie."; return
        }
        deviceID = device.id; channel = requestedChannel
        guard let c = sh_create(device.rate, Int32(channel - 1), Int32(device.channels)) else {
            error = "Nie udało się utworzyć dekodera LTC."; return
        }
        let result = device.uid.withCString { sh_start(c, $0) }
        guard result == 0 else {
            sh_destroy(c)
            error = "Nie można otworzyć wejścia (CoreAudio: \(result)). Sprawdź podłączenie, uprawnienie mikrofonu i dostępność interfejsu dla kilku aplikacji."
            return
        }
        capture = c
        history.reset(at: sh_now(), threshold: Double(threshold) / 1000)
        demo = false; error = ""; running = true
        UserDefaults.standard.set(device.uid, forKey: "deviceUID")
        message = "\(device.name) · kanał \(channel) · \(Int(device.rate)) Hz · odbiór LTC"
        activity = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .idleSystemSleepDisabled], reason: "Monitorowanie wejścia LTC")
        let t = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in self?.poll() }
        timer = t
        RunLoop.main.add(t, forMode: .common)
    }
    private func drain() {
        guard let c = capture else { return }
        var raw = [SHFrame](repeating: SHFrame(), count: 2048)
        let count = sh_read(c, &raw, Int32(raw.count))
        for f in raw.prefix(Int(count)) {
            let separator = f.drop_frame != 0 ? ";" : ":"
            let tc = String(format: "%02d:%02d:%02d%@%02d", f.hours, f.minutes, f.seconds, separator, f.frames)
            history.ingest(Frame(time: f.time, timecode: tc, fps: f.measured_fps, dropFrame: f.drop_frame != 0, reverse: f.reverse != 0))
        }
        history.tick(at: sh_now())
    }
    private func poll() {
        guard let c = capture else { return }
        drain()
        if sh_overflows(c) > 0 { error = "Przepełnienie bufora odbioru: \(sh_overflows(c)) ramek. Ta historia jest niepełna." }
        if sh_error(c) != 0 {
            error = "Błąd odbioru CoreAudio: \(sh_error(c)). Nasłuch został zatrzymany."; stop(); return
        }
        let lastAudio = sh_last_audio(c)
        if history.now - max(lastAudio, history.start) > 2 {
            error = "Brak danych z interfejsu audio przez ponad 2 s. Sprawdź urządzenie i uruchom nasłuch ponownie."; stop()
        }
    }
    func stop() {
        timer?.invalidate(); timer = nil
        if let c = capture {
            sh_stop(c)
            drain()
            sh_destroy(c)
        }
        capture = nil; running = false
        if let activity { ProcessInfo.processInfo.endActivity(activity) }
        activity = nil
        message = "Historia zatrzymana. Wejście audio zwolnione. Start rozpocznie nowy zapis."
    }
    func exportCSV() {
        // Take the snapshot before the save panel opens; acquisition may continue.
        let contents = history.csv
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        let date = DateFormatter(); date.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        panel.nameFieldStringValue = "SMPTE-\(demo ? "DEMO-" : "")\(date.string(from: Date())).csv"
        panel.begin { response in
            guard response == .OK, let url = panel.url else { return }
            do { try contents.write(to: url, atomically: true, encoding: .utf8) }
            catch { self.error = "Nie udało się zapisać CSV: \(error.localizedDescription)" }
        }
    }
    func loadDemo() {
        if running { stop() }
        demo = true; error = ""
        history.reset(at: 0, threshold: 0.12)
        for n in 1...300 {
            if (170...187).contains(n) { continue }
            history.ingest(Frame(time: Double(n) / 25,
                timecode: String(format: "01:00:%02d:%02d", n / 25, n % 25),
                fps: 25, dropFrame: false, reverse: false))
        }
        history.tick(at: 12)
        message = "DEMO — przykładowe dane 25 FPS z przerwą. Wejście audio nie jest używane."
    }
    deinit {
        timer?.invalidate()
        if let capture { sh_destroy(capture) }
        if let activity { ProcessInfo.processInfo.endActivity(activity) }
    }
}

private let ink = Color(red: 0.055, green: 0.07, blue: 0.10)
private let card = Color(red: 0.095, green: 0.12, blue: 0.16)
private let mint = Color(red: 0.28, green: 0.92, blue: 0.70)

struct ContentView: View {
    @ObservedObject var model: Monitor
    @State private var selectedFrame: UUID?
    var status: String {
        if model.demo { return "DEMO" }
        if !model.running { return "ZATRZYMANO" }
        if model.history.missing { return "BRAK LTC" }
        return model.history.last == nil ? "OCZEKIWANIE" : "ODBIÓR LTC"
    }
    var statusColor: Color { model.history.missing && model.running ? .red : mint }
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            header
            inputControls
            metrics
            timeline
            actions
            messages
            records
            footnote
        }
        .padding(24).frame(minWidth: 860, minHeight: 660).background(ink)
        .preferredColorScheme(.dark)
    }
    @ViewBuilder private var header: some View {
            HStack {
                VStack(alignment: .leading, spacing: 5) {
                    Text("SMPTE HISTORY").font(.system(size: 13, weight: .bold, design: .monospaced)).tracking(3).foregroundColor(mint)
                    Text("Ostatnie 10 sekund").font(.system(size: 27, weight: .semibold))
                }
                Spacer()
                Circle().fill(statusColor).frame(width: 8, height: 8)
                Text(status).font(.system(size: 12, weight: .semibold, design: .monospaced)).foregroundColor(statusColor)
            }
    }

    @ViewBuilder private var inputControls: some View {
            HStack(alignment: .bottom, spacing: 14) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("WEJŚCIE AUDIO").font(.caption).foregroundColor(.secondary)
                    Picker("Wejście audio", selection: $model.deviceID) {
                        if model.devices.isEmpty { Text("Brak wejść").tag(UInt32(0)) }
                        ForEach(model.devices) { d in Text(d.name).tag(d.id) }
                    }.labelsHidden().frame(minWidth: 200)
                }
                VStack(alignment: .leading, spacing: 6) {
                    Text("KANAŁ LTC").font(.caption).foregroundColor(.secondary)
                    Picker("Kanał LTC", selection: $model.channel) {
                        ForEach(1...max(1, model.selected?.channels ?? 1), id: \.self) { Text("\($0)").tag($0) }
                    }.labelsHidden().frame(width: 85)
                }
                VStack(alignment: .leading, spacing: 6) {
                    Text("PRÓG ZANIKU").font(.caption).foregroundColor(.secondary)
                    Picker("Próg zaniku", selection: $model.threshold) {
                        ForEach([80, 120, 200, 500], id: \.self) { Text("\($0) ms").tag($0) }
                    }.labelsHidden().frame(width: 100)
                }
                Button("Odśwież") { model.refreshDevices() }
            }.disabled(model.running || model.requesting)
            .onChange(of: model.deviceID) { _ in model.channel = 1 }
    }

    @ViewBuilder private var metrics: some View {
            HStack(spacing: 12) {
                metric("OSTATNI TIMECODE", value: model.history.last?.timecode ?? "--:--:--:--", width: 310)
                metric("FPS · POMIAR", value: model.history.measuredFPS.map { String(format: "%.3f", $0) } ?? "—", width: 170)
                metric("ZANIKI W OKNIE", value: "\(model.history.gaps.filter { !$0.initial }.count)", width: 150)
            }
    }

    @ViewBuilder private var timeline: some View {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("HISTORIA ODBIORU").font(.system(size: 11, weight: .semibold)).foregroundColor(.secondary)
                    Spacer()
                    Text("zielony: LTC    czerwony: brak poprawnych ramek").font(.caption).foregroundColor(.secondary)
                }
                Timeline(history: model.history).frame(height: 43)
                HStack {
                    Text("−10 s"); Spacer(); Text("−5 s"); Spacer(); Text(model.running ? "teraz" : "chwila zatrzymania")
                }.font(.system(size: 11, design: .monospaced)).foregroundColor(.secondary)
            }
    }

    @ViewBuilder private var actions: some View {
            HStack(spacing: 10) {
                Button { model.running ? model.stop() : model.start() } label: {
                    Label(model.running ? "Stop · zachowaj historię" : "Start · nowy zapis", systemImage: model.running ? "stop.fill" : "play.fill")
                        .frame(minWidth: 190)
                        .padding(.horizontal, 12).padding(.vertical, 8)
                        .foregroundColor(ink).background(mint).cornerRadius(7)
                }.buttonStyle(.plain)
                    .disabled(model.requesting || (!model.running && model.selected == nil))
                Button("Zapisz CSV") { model.exportCSV() }.disabled(model.history.frames.isEmpty && model.history.gaps.isEmpty)
                Spacer()
                Button("Pokaż demo") { model.loadDemo() }.disabled(model.running || model.requesting)
            }
    }

    @ViewBuilder private var messages: some View {
            Text(model.message).font(.caption).foregroundColor(model.demo ? .orange : .secondary).lineLimit(2)
            if !model.error.isEmpty {
                Text(model.error).font(.caption).foregroundColor(.orange).textSelection(.enabled)
            }
    }

    @ViewBuilder private var records: some View {
            HStack(alignment: .top, spacing: 18) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("RAMKI LTC").font(.system(size: 11, weight: .semibold)).foregroundColor(.secondary)
                    Table(Array(model.history.frames.reversed()), selection: $selectedFrame) {
                        TableColumn("Wiek [s]") { frame in Text(String(format: "%.3f", max(0, model.history.now - frame.time))).monospacedDigit() }.width(70)
                        TableColumn("Timecode") { frame in Text(frame.timecode).font(.system(.body, design: .monospaced)) }.width(130)
                        TableColumn("FPS") { frame in Text(String(format: "%.3f", frame.fps)).monospacedDigit() }.width(65)
                        TableColumn("Format") { frame in Text("\(frame.dropFrame ? "DF" : "NDF")\(frame.reverse ? " ←" : "")") }.width(60)
                    }.frame(minHeight: 170)
                }
                VStack(alignment: .leading, spacing: 8) {
                    Text("PRZERWY W ODBIORZE").font(.system(size: 11, weight: .semibold)).foregroundColor(.secondary)
                    ScrollView {
                        VStack(alignment: .leading, spacing: 12) {
                            if model.history.gaps.isEmpty {
                                Text("Brak wykrytych przerw").foregroundColor(.secondary).font(.callout)
                            }
                            ForEach(model.history.gaps.reversed()) { gap in
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(gap.initial ? "Brak LTC na początku" : "Zanik LTC").foregroundColor(.red).fontWeight(.medium)
                                    Text(String(format: "%.2f s temu · %.0f ms%@", max(0, model.history.now - max(gap.start, model.history.cutoff)), max(0, min(gap.end ?? model.history.now, model.history.now) - max(gap.start, model.history.cutoff)) * 1000, gap.end == nil ? " · otwarty" : ""))
                                        .font(.system(size: 11, design: .monospaced)).foregroundColor(.secondary)
                                }
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading)
                    }
                }.frame(width: 230)
            }
    }

    @ViewBuilder private var footnote: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Pomiar niezależny od dekodera Areny. FPS z długości ramek audio; DF/NDF z sygnału. Zanik = brak poprawnej ramki przez wybrany próg. Czas przerwy jest przybliżony.")
                .font(.system(size: 11)).foregroundColor(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("Developed by Black Light Design")
                .font(.system(size: 11, weight: .medium)).foregroundColor(.secondary)
        }
    }

    func metric(_ label: String, value: String, width: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(label).font(.system(size: 11, weight: .semibold)).foregroundColor(.secondary)
            Text(value).font(.system(size: 29, weight: .medium, design: .monospaced)).foregroundColor(.white).lineLimit(1)
        }.frame(minWidth: width, maxWidth: .infinity, alignment: .leading).padding(16)
            .background(card).cornerRadius(10)
    }
}

struct Timeline: View {
    let history: History
    var body: some View {
        Canvas { context, size in
            let left = history.now - 10
            func rect(_ a: Double, _ b: Double) -> CGRect {
                let x = max(0, min(10, a - left)) / 10 * size.width
                let end = max(0, min(10, b - left)) / 10 * size.width
                return CGRect(x: x, y: 0, width: max(0, end - x), height: size.height)
            }
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(card))
            // Paint only received frame intervals: no invented green history.
            for f in history.frames {
                context.fill(Path(rect(f.time - 1 / f.fps, f.time)), with: .color(mint.opacity(0.8)))
            }
            for g in history.gaps {
                context.fill(Path(rect(max(history.cutoff, g.start), g.end ?? history.now)), with: .color(.red.opacity(0.85)))
            }
            for i in 1..<10 {
                var line = Path(); let x = Double(i) / 10 * size.width
                line.move(to: CGPoint(x: x, y: 0)); line.addLine(to: CGPoint(x: x, y: size.height))
                context.stroke(line, with: .color(ink.opacity(0.4)), lineWidth: 1)
            }
        }.cornerRadius(6)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var monitor: Monitor?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { monitor?.stop() }
}
@main
struct SMPTEHistoryApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var monitor = Monitor()
    var body: some Scene {
        Window("SMPTE History", id: "main") {
            ContentView(model: monitor).onAppear { delegate.monitor = monitor }
        }.defaultSize(width: 940, height: 780)
        .commands {
            CommandGroup(replacing: .newItem) { }
        }
    }
}
