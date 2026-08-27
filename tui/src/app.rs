use crate::ffi::{self, Engine, PieJobSnapshot, PieResourceStats, Resource};
use ratatui::crossterm::event::KeyCode;

pub const DEFAULT_DIGITS: u32 = 10_000;
pub const MIN_DIGITS: u32 = 10;
pub const MAX_DIGITS: u32 = 10_000;
pub const DEFAULT_SAMPLES: u64 = 100_000_000;
pub const MIN_SAMPLES: u64 = 1_000_000;
pub const MAX_SAMPLES: u64 = 4_000_000_000;
pub const STEP_DIGITS: u32 = 100;
pub const STEP_SAMPLES: u64 = 10_000_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JobState {
    GpuUnavailable = 0,
    Idle = 1,
    Preparing = 2,
    Running = 3,
    Paused = 4,
    Cancelling = 5,
    Finished = 6,
    Cancelled = 7,
    Failed = 8,
}

impl JobState {
    pub fn from_i32(value: i32) -> Self {
        match value {
            0 => Self::GpuUnavailable,
            1 => Self::Idle,
            2 => Self::Preparing,
            3 => Self::Running,
            4 => Self::Paused,
            5 => Self::Cancelling,
            6 => Self::Finished,
            7 => Self::Cancelled,
            _ => Self::Failed,
        }
    }

    pub fn is_active(self) -> bool {
        matches!(
            self,
            Self::Preparing | Self::Running | Self::Paused | Self::Cancelling
        )
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::GpuUnavailable => "GPU 不可用",
            Self::Idle => "空闲",
            Self::Preparing => "准备中",
            Self::Running => "运行中",
            Self::Paused => "已暂停",
            Self::Cancelling => "取消中",
            Self::Finished => "已完成",
            Self::Cancelled => "已取消",
            Self::Failed => "失败",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    ExactDigits,
    MonteCarlo,
}

impl Mode {
    pub fn toggle(self) -> Self {
        match self {
            Self::ExactDigits => Self::MonteCarlo,
            Self::MonteCarlo => Self::ExactDigits,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::ExactDigits => "精确位数",
            Self::MonteCarlo => "蒙特卡洛",
        }
    }
}

pub struct App {
    // Declare the resource before the engine so it is dropped first, keeping the
    // loaded dynamic library alive until the resource handle is released.
    pub resource: Option<Resource>,
    pub engine: Engine,
    pub running: bool,
    pub selected: usize,
    pub gpu_names: Vec<String>,
    pub gpu_reasons: Vec<String>,
    pub device_states: Vec<JobState>,
    pub job: PieJobSnapshot,
    pub resources: PieResourceStats,
    pub result: String,
    pub mode: Mode,
    pub digits: u32,
    pub samples: u64,
    pub scroll: usize,
    pub throughput: Vec<u64>,
}

impl App {
    pub fn new(engine: Engine) -> Self {
        let count = engine.device_count();
        let mut gpu_names = Vec::with_capacity(count);
        let mut gpu_reasons = Vec::with_capacity(count);
        for index in 0..count {
            let info = engine.get_device(index);
            gpu_names.push(ffi::text_from_c(&info.name));
            gpu_reasons.push(ffi::text_from_c(&info.reason));
        }
        if gpu_names.is_empty() {
            gpu_names.push("(无 CUDA 设备)".to_string());
            gpu_reasons.push(String::new());
        }

        let selected = engine.selected_slot().min(count.saturating_sub(1));
        let gpu = engine.get_device(selected);
        let resource = engine.create_resource(&gpu).ok();

        let mut app = Self {
            resource,
            engine,
            running: true,
            selected,
            gpu_names,
            gpu_reasons,
            device_states: Vec::with_capacity(count),
            job: PieJobSnapshot::default(),
            resources: PieResourceStats::default(),
            result: String::new(),
            mode: Mode::ExactDigits,
            digits: DEFAULT_DIGITS,
            samples: DEFAULT_SAMPLES,
            scroll: 0,
            throughput: Vec::new(),
        };
        app.refresh();
        app
    }

    pub fn refresh(&mut self) {
        let count = self.engine.device_count();
        if count == 0 {
            self.job = PieJobSnapshot::default();
            self.result = String::new();
            self.device_states = Vec::new();
            return;
        }

        self.job = self.engine.snapshot();
        self.result = self.engine.copy_result();
        self.resources = self
            .resource
            .as_ref()
            .map(|resource| resource.sample())
            .unwrap_or_default();

        self.device_states = (0..count)
            .map(|index| JobState::from_i32(self.engine.get_device_snapshot(index).state))
            .collect();

        if self.job.mode == 1 && JobState::from_i32(self.job.state) == JobState::Running {
            let sample_rate = self.job.samples_per_second.max(0.0) as u64;
            self.throughput.push(sample_rate);
            if self.throughput.len() > 60 {
                self.throughput.remove(0);
            }
        }
    }

    pub fn handle_key(&mut self, key: KeyCode) {
        match key {
            KeyCode::Char('q') | KeyCode::Esc => {
                self.quit();
            }
            KeyCode::Char('s') => self.start(),
            KeyCode::Char('p') | KeyCode::Char(' ') => self.engine.toggle_pause(),
            KeyCode::Char('c') => self.engine.cancel(),
            KeyCode::Char('m') => self.toggle_mode(),
            KeyCode::Char('+') | KeyCode::Char('=') => self.increase(),
            KeyCode::Char('-') | KeyCode::Char('_') => self.decrease(),
            KeyCode::Up | KeyCode::Char('k') => self.previous_device(),
            KeyCode::Down | KeyCode::Char('j') => self.next_device(),
            KeyCode::PageUp | KeyCode::Char('[') => self.scroll_result(false),
            KeyCode::PageDown | KeyCode::Char(']') => self.scroll_result(true),
            _ => {}
        }
    }

    pub fn quit(&mut self) {
        self.engine.stop();
        self.running = false;
    }

    fn start(&mut self) {
        if JobState::from_i32(self.job.state).is_active() {
            return;
        }
        let started = match self.mode {
            Mode::ExactDigits => self.engine.start_exact(self.digits),
            Mode::MonteCarlo => self.engine.start_monte_carlo(self.samples),
        };
        if started {
            self.scroll = 0;
            self.throughput.clear();
        }
    }

    fn toggle_mode(&mut self) {
        if JobState::from_i32(self.job.state).is_active() {
            return;
        }
        self.mode = self.mode.toggle();
        self.scroll = 0;
    }

    fn increase(&mut self) {
        if JobState::from_i32(self.job.state).is_active() {
            return;
        }
        match self.mode {
            Mode::ExactDigits => {
                self.digits = self.digits.saturating_add(STEP_DIGITS).min(MAX_DIGITS);
            }
            Mode::MonteCarlo => {
                self.samples = self.samples.saturating_add(STEP_SAMPLES).min(MAX_SAMPLES);
            }
        }
        self.scroll = 0;
    }

    fn decrease(&mut self) {
        if JobState::from_i32(self.job.state).is_active() {
            return;
        }
        match self.mode {
            Mode::ExactDigits => {
                self.digits = self
                    .digits
                    .saturating_sub(STEP_DIGITS)
                    .max(MIN_DIGITS);
            }
            Mode::MonteCarlo => {
                self.samples = self.samples.saturating_sub(STEP_SAMPLES).max(MIN_SAMPLES);
            }
        }
        self.scroll = 0;
    }

    fn previous_device(&mut self) {
        self.select_relative(true);
    }

    fn next_device(&mut self) {
        self.select_relative(false);
    }

    fn select_relative(&mut self, previous: bool) {
        if JobState::from_i32(self.job.state).is_active() {
            return;
        }
        let count = self.engine.device_count();
        if count <= 1 {
            return;
        }
        let next = if previous {
            if self.selected == 0 {
                count - 1
            } else {
                self.selected - 1
            }
        } else {
            (self.selected + 1) % count
        };
        if self.engine.select_device(next) {
            self.selected = next;
            self.scroll = 0;
            self.recreate_resource();
        }
    }

    fn recreate_resource(&mut self) {
        let gpu = self.engine.get_device(self.selected);
        self.resource = self.engine.create_resource(&gpu).ok();
    }

    fn scroll_result(&mut self, down: bool) {
        if down {
            self.scroll = self.scroll.saturating_add(1);
        } else {
            self.scroll = self.scroll.saturating_sub(1);
        }
    }
}
