use unicode_width::UnicodeWidthStr;

pub fn format_bytes(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} B")
    } else {
        format!("{value:.1} {}", UNITS[unit])
    }
}

pub fn format_count(value: u64) -> String {
    if value >= 100_000_000 {
        format!("{:.2} 亿", value as f64 / 100_000_000.0)
    } else if value >= 10_000 {
        format!("{:.2} 万", value as f64 / 10_000.0)
    } else {
        value.to_string()
    }
}

pub fn format_rate(samples_per_second: f64) -> String {
    if samples_per_second >= 1_000_000_000.0 {
        format!("{:.2} G/s", samples_per_second / 1_000_000_000.0)
    } else if samples_per_second >= 1_000_000.0 {
        format!("{:.2} M/s", samples_per_second / 1_000_000.0)
    } else if samples_per_second >= 1_000.0 {
        format!("{:.2} K/s", samples_per_second / 1_000.0)
    } else {
        format!("{samples_per_second:.0} /s")
    }
}

pub fn wrapped_line_count(text: &str, width: usize) -> usize {
    if width == 0 {
        return text.lines().count();
    }
    let mut total = 0;
    for line in text.split('\n') {
        let line_width = UnicodeWidthStr::width(line);
        if line_width == 0 {
            total += 1;
        } else {
            total += (line_width + width - 1) / width;
        }
    }
    total
}