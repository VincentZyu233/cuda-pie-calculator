use libloading::Library;
use std::ffi::{c_char, c_void, CStr};
use std::path::{Path, PathBuf};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PieGpuInfo {
    pub available: i32,
    pub device_index: i32,
    pub name: [c_char; 256],
    pub reason: [c_char; 256],
    pub total_memory_bytes: u64,
    pub compute_major: i32,
    pub compute_minor: i32,
    pub multiprocessor_count: i32,
    pub core_clock_khz: i32,
    pub memory_clock_khz: i32,
    pub memory_bus_width_bits: i32,
    pub driver_version: i32,
    pub runtime_version: i32,
}

impl Default for PieGpuInfo {
    fn default() -> Self {
        Self {
            available: 0,
            device_index: -1,
            name: [0; 256],
            reason: [0; 256],
            total_memory_bytes: 0,
            compute_major: 0,
            compute_minor: 0,
            multiprocessor_count: 0,
            core_clock_khz: 0,
            memory_clock_khz: 0,
            memory_bus_width_bits: 0,
            driver_version: 0,
            runtime_version: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PieJobSnapshot {
    pub state: i32,
    pub mode: i32,
    pub requested_digits: u32,
    pub completed_steps: u32,
    pub total_steps: u32,
    pub gpu_milliseconds: f64,
    pub sample_target: u64,
    pub samples_completed: u64,
    pub hits_inside_circle: u64,
    pub monte_carlo_estimate: f64,
    pub monte_carlo_confidence95: f64,
    pub samples_per_second: f64,
    pub phase: [c_char; 256],
    pub message: [c_char; 512],
}

impl Default for PieJobSnapshot {
    fn default() -> Self {
        Self {
            state: 1,
            mode: 0,
            requested_digits: 0,
            completed_steps: 0,
            total_steps: 0,
            gpu_milliseconds: 0.0,
            sample_target: 0,
            samples_completed: 0,
            hits_inside_circle: 0,
            monte_carlo_estimate: 0.0,
            monte_carlo_confidence95: 0.0,
            samples_per_second: 0.0,
            phase: [0; 256],
            message: [0; 512],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PieResourceStats {
    pub cpu_available: i32,
    pub cpu_percent: f64,
    pub memory_used_bytes: u64,
    pub memory_total_bytes: u64,
    pub gpu_details_available: i32,
    pub gpu_utilization_percent: u32,
    pub gpu_memory_used_bytes: u64,
    pub gpu_memory_total_bytes: u64,
    pub gpu_temperature_celsius: u32,
}

impl Default for PieResourceStats {
    fn default() -> Self {
        Self {
            cpu_available: 0,
            cpu_percent: 0.0,
            memory_used_bytes: 0,
            memory_total_bytes: 0,
            gpu_details_available: 0,
            gpu_utilization_percent: 0,
            gpu_memory_used_bytes: 0,
            gpu_memory_total_bytes: 0,
            gpu_temperature_celsius: 0,
        }
    }
}

pub fn text_from_c(value: &[c_char]) -> String {
    if value.is_empty() {
        return String::new();
    }
    unsafe {
        let ptr = value.as_ptr();
        let text = CStr::from_ptr(ptr);
        text.to_string_lossy().into_owned()
    }
}

type EngineCreate = unsafe extern "C" fn() -> *mut c_void;
type EngineDestroy = unsafe extern "C" fn(*mut c_void);
type DeviceCountFn = unsafe extern "C" fn(*const c_void) -> i32;
type SelectedSlotFn = unsafe extern "C" fn(*const c_void) -> i32;
type SelectDeviceFn = unsafe extern "C" fn(*mut c_void, i32) -> i32;
type GetDeviceFn = unsafe extern "C" fn(*const c_void, i32, *mut PieGpuInfo);
type GetDeviceSnapshotFn = unsafe extern "C" fn(*const c_void, i32, *mut PieJobSnapshot);
type GetSnapshotFn = unsafe extern "C" fn(*const c_void, *mut PieJobSnapshot);
type CopyResultFn = unsafe extern "C" fn(*const c_void, *mut c_char, usize) -> usize;
type StartExactFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type StartMonteCarloFn = unsafe extern "C" fn(*mut c_void, u64) -> i32;
type TogglePauseFn = unsafe extern "C" fn(*mut c_void);
type CancelFn = unsafe extern "C" fn(*mut c_void);
type StopFn = unsafe extern "C" fn(*mut c_void);

type ResourceCreate = unsafe extern "C" fn(*const PieGpuInfo) -> *mut c_void;
type ResourceDestroy = unsafe extern "C" fn(*mut c_void);
type ResourceSample = unsafe extern "C" fn(*mut c_void, *mut PieResourceStats);

pub struct Engine {
    library: Library,
    handle: *mut c_void,
    device_count_fn: DeviceCountFn,
    selected_slot_fn: SelectedSlotFn,
    select_device_fn: SelectDeviceFn,
    get_device_fn: GetDeviceFn,
    get_device_snapshot_fn: GetDeviceSnapshotFn,
    get_snapshot_fn: GetSnapshotFn,
    copy_result_fn: CopyResultFn,
    start_exact_fn: StartExactFn,
    start_monte_carlo_fn: StartMonteCarloFn,
    toggle_pause_fn: TogglePauseFn,
    cancel_fn: CancelFn,
    stop_fn: StopFn,
    destroy_fn: EngineDestroy,
}

impl Engine {
    pub fn load(path: &Path) -> Result<Self, String> {
        unsafe {
            let library = Library::new(path).map_err(|e| e.to_string())?;
            let create: EngineCreate =
                *library.get::<EngineCreate>(b"pie_engine_create\0").map_err(|e| e.to_string())?;
            let destroy_fn: EngineDestroy =
                *library.get::<EngineDestroy>(b"pie_engine_destroy\0").map_err(|e| e.to_string())?;
            let device_count_fn: DeviceCountFn =
                *library.get::<DeviceCountFn>(b"pie_engine_device_count\0").map_err(|e| e.to_string())?;
            let selected_slot_fn: SelectedSlotFn =
                *library.get::<SelectedSlotFn>(b"pie_engine_selected_slot\0").map_err(|e| e.to_string())?;
            let select_device_fn: SelectDeviceFn =
                *library.get::<SelectDeviceFn>(b"pie_engine_select_device\0").map_err(|e| e.to_string())?;
            let get_device_fn: GetDeviceFn =
                *library.get::<GetDeviceFn>(b"pie_engine_get_device\0").map_err(|e| e.to_string())?;
            let get_device_snapshot_fn: GetDeviceSnapshotFn =
                *library.get::<GetDeviceSnapshotFn>(b"pie_engine_get_device_snapshot\0")
                    .map_err(|e| e.to_string())?;
            let get_snapshot_fn: GetSnapshotFn =
                *library.get::<GetSnapshotFn>(b"pie_engine_get_snapshot\0").map_err(|e| e.to_string())?;
            let copy_result_fn: CopyResultFn =
                *library.get::<CopyResultFn>(b"pie_engine_copy_result\0").map_err(|e| e.to_string())?;
            let start_exact_fn: StartExactFn =
                *library.get::<StartExactFn>(b"pie_engine_start_exact\0").map_err(|e| e.to_string())?;
            let start_monte_carlo_fn: StartMonteCarloFn =
                *library.get::<StartMonteCarloFn>(b"pie_engine_start_monte_carlo\0")
                    .map_err(|e| e.to_string())?;
            let toggle_pause_fn: TogglePauseFn =
                *library.get::<TogglePauseFn>(b"pie_engine_toggle_pause\0").map_err(|e| e.to_string())?;
            let cancel_fn: CancelFn = *library.get::<CancelFn>(b"pie_engine_cancel\0").map_err(|e| e.to_string())?;
            let stop_fn: StopFn = *library.get::<StopFn>(b"pie_engine_stop\0").map_err(|e| e.to_string())?;

            let handle = create();
            if handle.is_null() {
                return Err("创建 CUDA 引擎失败：没有可用 GPU 或驱动？".to_string());
            }

            Ok(Self {
                library,
                handle,
                device_count_fn,
                selected_slot_fn,
                select_device_fn,
                get_device_fn,
                get_device_snapshot_fn,
                get_snapshot_fn,
                copy_result_fn,
                start_exact_fn,
                start_monte_carlo_fn,
                toggle_pause_fn,
                cancel_fn,
                stop_fn,
                destroy_fn,
            })
        }
    }

    pub fn device_count(&self) -> usize {
        unsafe { (self.device_count_fn)(self.handle) as usize }
    }

    pub fn selected_slot(&self) -> usize {
        unsafe { (self.selected_slot_fn)(self.handle) as usize }
    }

    pub fn select_device(&self, slot: usize) -> bool {
        unsafe { (self.select_device_fn)(self.handle, slot as i32) != 0 }
    }

    pub fn get_device(&self, slot: usize) -> PieGpuInfo {
        let mut info = PieGpuInfo::default();
        unsafe {
            (self.get_device_fn)(self.handle, slot as i32, &mut info);
        }
        info
    }

    pub fn get_device_snapshot(&self, slot: usize) -> PieJobSnapshot {
        let mut snapshot = PieJobSnapshot::default();
        unsafe {
            (self.get_device_snapshot_fn)(self.handle, slot as i32, &mut snapshot);
        }
        snapshot
    }

    pub fn snapshot(&self) -> PieJobSnapshot {
        let mut snapshot = PieJobSnapshot::default();
        unsafe {
            (self.get_snapshot_fn)(self.handle, &mut snapshot);
        }
        snapshot
    }

    pub fn copy_result(&self) -> String {
        unsafe {
            let length = (self.copy_result_fn)(self.handle, std::ptr::null_mut(), 0);
            if length == 0 {
                return String::new();
            }
            let mut buffer = vec![0u8; length + 1];
            let written = (self.copy_result_fn)(self.handle, buffer.as_mut_ptr() as *mut c_char, length + 1);
            let count = written.min(buffer.len());
            if let Ok(text) = std::str::from_utf8(&buffer[..count]) {
                text.to_string()
            } else {
                String::new()
            }
        }
    }

    pub fn start_exact(&self, digits: u32) -> bool {
        unsafe { (self.start_exact_fn)(self.handle, digits) != 0 }
    }

    pub fn start_monte_carlo(&self, samples: u64) -> bool {
        unsafe { (self.start_monte_carlo_fn)(self.handle, samples) != 0 }
    }

    pub fn toggle_pause(&self) {
        unsafe { (self.toggle_pause_fn)(self.handle) }
    }

    pub fn cancel(&self) {
        unsafe { (self.cancel_fn)(self.handle) }
    }

    pub fn stop(&self) {
        unsafe { (self.stop_fn)(self.handle) }
    }

    pub fn create_resource(&self, gpu: &PieGpuInfo) -> Result<Resource, String> {
        Resource::new(&self.library, gpu)
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe {
            (self.stop_fn)(self.handle);
            (self.destroy_fn)(self.handle);
        }
    }
}

pub struct Resource {
    handle: *mut c_void,
    destroy_fn: ResourceDestroy,
    sample_fn: ResourceSample,
}

impl Resource {
    fn new(library: &Library, gpu: &PieGpuInfo) -> Result<Self, String> {
        unsafe {
            let create: ResourceCreate =
                *library.get::<ResourceCreate>(b"pie_resource_create\0").map_err(|e| e.to_string())?;
            let destroy_fn: ResourceDestroy =
                *library.get::<ResourceDestroy>(b"pie_resource_destroy\0").map_err(|e| e.to_string())?;
            let sample_fn: ResourceSample =
                *library.get::<ResourceSample>(b"pie_resource_sample\0").map_err(|e| e.to_string())?;
            let handle = create(gpu);
            if handle.is_null() {
                return Err("创建资源监控失败".to_string());
            }
            Ok(Self {
                handle,
                destroy_fn,
                sample_fn,
            })
        }
    }

    pub fn sample(&self) -> PieResourceStats {
        let mut stats = PieResourceStats::default();
        unsafe {
            (self.sample_fn)(self.handle, &mut stats);
        }
        stats
    }
}

impl Drop for Resource {
    fn drop(&mut self) {
        unsafe {
            (self.destroy_fn)(self.handle);
        }
    }
}

pub fn find_engine_library() -> Option<PathBuf> {
    let name = if cfg!(target_os = "windows") {
        "cuda_pie_engine.dll"
    } else {
        "libcuda_pie_engine.so"
    };

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let candidate = dir.join(name);
            if candidate.exists() {
                return Some(candidate);
            }
        }
    }

    if let Ok(cwd) = std::env::current_dir() {
        let candidate = cwd.join(name);
        if candidate.exists() {
            return Some(candidate);
        }
    }

    None
}