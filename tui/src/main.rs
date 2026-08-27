mod app;
mod ffi;
mod format;
mod ui;

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers};
use ratatui::{try_init, try_restore, DefaultTerminal};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = ffi::find_engine_library().ok_or("找不到 cuda_pie_engine 动态库")?;
    let engine = ffi::Engine::load(&path)?;
    let mut terminal = try_init()?;
    let result = run(&mut terminal, engine);
    try_restore()?;
    result
}

fn run(terminal: &mut DefaultTerminal, engine: ffi::Engine) -> Result<(), Box<dyn std::error::Error>> {
    let mut app = app::App::new(engine);
    while app.running {
        terminal.draw(|frame| ui::draw(frame, &mut app))?;

        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    if key.code == KeyCode::Char('c') && key.modifiers.contains(KeyModifiers::CONTROL) {
                        app.quit();
                    } else {
                        app.handle_key(key.code);
                    }
                }
            }
        }

        app.refresh();
    }
    Ok(())
}
