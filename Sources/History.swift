import Foundation

struct Frame: Identifiable {
    var id = UUID()
    let time: Double
    let timecode: String
    let fps: Double
    let dropFrame: Bool
    let reverse: Bool
}
struct Gap: Identifiable {
    var id = UUID()
    let start: Double
    var end: Double?
    let initial: Bool
}

/// All mutations are on the main thread. Monotonic time, never the LTC clock,
/// defines the history window, so seeks/reverse/midnight do not lose history.
struct History {
    var frames: [Frame] = []
    var gaps: [Gap] = []
    var last: Frame?
    var start: Double = 0
    var now: Double = 0
    var threshold: Double = 0.12
    var missing = false
    var cutoff: Double { max(start, now - 10) }
    var measuredFPS: Double? {
        guard !missing else { return nil }
        let recent = frames.filter { $0.time > now - 1 }.map(\.fps).sorted()
        guard !recent.isEmpty else { return nil }
        return recent[recent.count / 2]
    }
    mutating func reset(at time: Double, threshold: Double) {
        self = History(start: time, now: time, threshold: threshold)
    }
    mutating func ingest(_ frame: Frame) {
        guard frame.time >= start, frame.time >= (last?.time ?? start) else { return }
        detectGap(at: frame.time)
        if missing {
            if let index = gaps.indices.last, gaps[index].end == nil { gaps[index].end = frame.time }
            missing = false
        }
        frames.append(frame)
        last = frame
        now = max(now, frame.time)
    }
    private mutating func detectGap(at time: Double) {
        let reference = last?.time ?? start
        if !missing && time - reference >= threshold {
            // Detection waits threshold; the estimated absent interval starts
            // when the next frame was due. Hardware/input latency still applies.
            let expected = last.map { 1 / $0.fps } ?? 0
            gaps.append(Gap(start: reference + expected, initial: last == nil))
            missing = true
        }
    }
    mutating func tick(at time: Double) {
        now = max(now, time)
        detectGap(at: now)
        let limit = cutoff
        frames.removeAll { $0.time < limit }
        gaps.removeAll { ($0.end ?? .infinity) < limit }
    }
    var csv: String {
        var lines = ["type,session_seconds,age_seconds,timecode,measured_fps,drop_frame,reverse,duration_seconds,open_at_snapshot,initial_no_signal"]
        for f in frames {
            lines.append(String(format: "frame,%.6f,%.6f,%@,%.6f,%d,%d,,,", f.time - start, now - f.time, f.timecode, f.fps, f.dropFrame ? 1 : 0, f.reverse ? 1 : 0))
        }
        for gap in gaps {
            let a = max(cutoff, gap.start), b = min(now, gap.end ?? now)
            lines.append(String(format: "gap,%.6f,%.6f,,,,,%.6f,%d,%d", a - start, now - a, max(0, b - a), gap.end == nil ? 1 : 0, gap.initial ? 1 : 0))
        }
        return lines.joined(separator: "\n") + "\n"
    }
}
