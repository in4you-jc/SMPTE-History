import Foundation

func frame(_ time: Double, _ code: String = "01:00:00:00") -> Frame {
    Frame(time: time, timecode: code, fps: 25, dropFrame: false, reverse: false)
}
@main struct HistoryTests {
    static func main() {
        var h = History()
        h.reset(at: 100, threshold: 0.12)
        h.ingest(frame(100.04)); h.ingest(frame(100.08)); h.tick(at: 100.15)
        assert(h.gaps.isEmpty && !h.missing)
        h.tick(at: 100.21)
        assert(h.missing && h.gaps.count == 1 && h.measuredFPS == nil)
        assert(abs(h.gaps[0].start - 100.12) < 1e-6)
        h.tick(at: 100.3); assert(h.gaps.count == 1)
        h.ingest(frame(100.4)); assert(!h.missing && h.gaps[0].end == 100.4)
        // A complete dropout between UI polls is detected when frames drain.
        h.ingest(frame(101)); assert(h.gaps.count == 2 && h.gaps[1].end == 101)
        // Ten seconds are based on acquisition time, even if LTC seeks.
        for i in 1...400 { h.ingest(frame(101 + Double(i)/25, "00:00:00:00")); h.tick(at: 101 + Double(i)/25) }
        assert(h.frames.count <= 251 && h.frames.count >= 250)
        assert(h.frames.first!.time >= h.now - 10 && h.gaps.isEmpty)
        let snapshot = h.csv
        assert(snapshot.components(separatedBy: "\n").count == h.frames.count + 2)
        assert(snapshot.contains("25.000000"))
        h.tick(at: 140)
        assert(h.frames.isEmpty && h.missing && h.gaps.count == 1)
        assert(h.csv.contains("gap,")) // A long open gap still spans the visible window.
        h.reset(at: 200, threshold: 0.12)
        h.tick(at: 201)
        assert(h.gaps.count == 1 && h.gaps[0].initial)
        h.ingest(frame(201.04)); assert(h.gaps[0].end == 201.04)
        let count = h.frames.count
        h.ingest(frame(199)); assert(h.frames.count == count)
        h.reset(at: 500, threshold: 0.2)
        assert(h.frames.isEmpty && h.gaps.isEmpty && h.last == nil && !h.missing)
        print("PASS history: timeout, recovery, dropout between polls, bounded 10s window, LTC seeks, long gap, CSV, initial absence, reset")
    }
}
