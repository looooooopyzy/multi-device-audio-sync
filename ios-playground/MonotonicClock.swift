import Darwin

enum MonotonicClock {
    static let timebase: mach_timebase_info_data_t = {
        var info = mach_timebase_info_data_t()
        mach_timebase_info(&info)
        return info
    }()
    static func nowNs() -> Int64 {
        let ticks = mach_absolute_time()
        let numer = UInt64(timebase.numer), denom = UInt64(timebase.denom)
        let value = (ticks / denom) * numer + ((ticks % denom) * numer) / denom
        return Int64(value)
    }
}
