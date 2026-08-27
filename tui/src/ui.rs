use crate::app::{App, JobState, Mode};
use crate::ffi::text_from_c;
use crate::format;
use ratatui::layout::{Alignment, Constraint, Direction, Layout};
use ratatui::prelude::{Line, Rect, Span, Text};
use ratatui::style::{Color, Modifier, Style};
use ratatui::widgets::{Block, Borders, Gauge, List, ListItem, ListState, Paragraph, Sparkline, Wrap};
use ratatui::Frame;

pub fn draw(frame: &mut Frame, app: &mut App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Length(4),
            Constraint::Min(7),
            Constraint::Length(7),
            Constraint::Length(4),
            Constraint::Length(2),
        ])
        .split(frame.area());

    render_header(frame, chunks[0], app);
    render_devices(frame, chunks[1], app);
    render_result(frame, chunks[2], app);
    render_progress(frame, chunks[3], app);
    render_system(frame, chunks[4], app);
    render_footer(frame, chunks[5]);
}

fn render_header(frame: &mut Frame, area: Rect, app: &App) {
    let param = match app.mode {
        Mode::ExactDigits => format!("目标：{} 位", app.digits),
        Mode::MonteCarlo => format!("目标：{} 样本", format::format_count(app.samples)),
    };
    let line = Line::from(vec![
        Span::styled(" π 计算器 ", Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)),
        Span::raw("   "),
        Span::styled(format!("模式：{}", app.mode.label()), Style::default().fg(Color::Yellow)),
        Span::raw("    "),
        Span::styled(
            format!("设备 {}/{}", app.selected + 1, app.gpu_names.len()),
            Style::default().fg(Color::Magenta),
        ),
        Span::raw("    "),
        Span::styled(param, Style::default().fg(Color::Green)),
    ]);
    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(Color::Cyan))
        .title(Span::styled(" 控制面板 ", Style::default().fg(Color::Cyan)));
    frame.render_widget(Paragraph::new(Text::from(line)).block(block), area);
}

fn render_devices(frame: &mut Frame, area: Rect, app: &App) {
    let mut list_state = ListState::default();
    list_state.select(Some(app.selected));

    let items: Vec<ListItem> = (0..app.gpu_names.len())
        .map(|index| {
            let state = app.device_states.get(index).copied().unwrap_or(JobState::GpuUnavailable);
            let color = state_color(state);
            let state_text = if state == JobState::GpuUnavailable {
                app.gpu_reasons
                    .get(index)
                    .filter(|reason| !reason.is_empty())
                    .map(|reason| reason.as_str())
                    .unwrap_or("不可用")
            } else {
                state.label()
            };
            let prefix = if index == app.selected { "►" } else { " " };
            ListItem::new(Line::from(vec![
                Span::styled(format!(" {prefix} "), Style::default().fg(Color::Cyan)),
                Span::raw("  "),
                Span::styled(
                    app.gpu_names.get(index).cloned().unwrap_or_default(),
                    Style::default().fg(Color::White),
                ),
                Span::raw("  "),
                Span::styled(state_text, Style::default().fg(color)),
            ]))
        })
        .collect();

    let list = List::new(items)
        .block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(Color::Magenta))
                .title(Span::styled(" CUDA 设备 ", Style::default().fg(Color::Magenta))),
        )
        .highlight_style(
            Style::default()
                .bg(Color::DarkGray)
                .fg(Color::White)
                .add_modifier(Modifier::BOLD),
        );

    frame.render_stateful_widget(list, area, &mut list_state);
}

fn render_result(frame: &mut Frame, area: Rect, app: &mut App) {
    let result_text = if app.result.is_empty() {
        "(暂无结果，按 s 开始计算)".to_string()
    } else {
        app.result.clone()
    };

    let inner_width = area.width.saturating_sub(2).max(1) as usize;
    let visible_lines = area.height.saturating_sub(2).max(1) as usize;
    let total_lines = format::wrapped_line_count(&result_text, inner_width);
    let max_scroll = total_lines.saturating_sub(visible_lines);
    if app.scroll > max_scroll {
        app.scroll = max_scroll;
    }

    let paragraph = Paragraph::new(result_text)
        .block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(Color::Green))
                .title(Span::styled(" 计算结果 ", Style::default().fg(Color::Green))),
        )
        .wrap(Wrap { trim: false })
        .scroll((app.scroll as u16, 0));

    frame.render_widget(paragraph, area);
}

fn render_progress(frame: &mut Frame, area: Rect, app: &App) {
    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(Color::Yellow))
        .title(Span::styled(" 计算进度 ", Style::default().fg(Color::Yellow)));
    let inner = block.inner(area);
    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(1), Constraint::Length(2), Constraint::Length(2)])
        .split(inner);

    let phase = text_from_c(&app.job.phase);
    let state_label = JobState::from_i32(app.job.state).label();
    let status = if phase.is_empty() {
        state_label.to_string()
    } else {
        format!(" {state_label}  ·  {phase}")
    };
    frame.render_widget(
        Paragraph::new(Text::from(status)).style(Style::default().fg(Color::White)),
        rows[0],
    );

    let (percent, label) = progress_values(app);
    let gauge = Gauge::default()
        .gauge_style(
            Style::default()
                .fg(Color::Cyan)
                .bg(Color::Black)
                .add_modifier(Modifier::BOLD),
        )
        .percent(percent)
        .label(Span::styled(label, Style::default().fg(Color::Yellow)));
    frame.render_widget(gauge, rows[1]);

    let max_rate = app.throughput.iter().copied().max().unwrap_or(1).max(1);
    let sparkline = Sparkline::default()
        .block(Block::default().borders(Borders::TOP).title(Span::styled(
            format!(" 吞吐：{}", format::format_rate(app.job.samples_per_second)),
            Style::default().fg(Color::Cyan),
        )))
        .data(&app.throughput)
        .max(max_rate);
    frame.render_widget(sparkline, rows[2]);
}

fn progress_values(app: &App) -> (u16, String) {
    let active = JobState::from_i32(app.job.state).is_active();
    let job_mode = if active {
        app.job.mode
    } else if app.mode == Mode::MonteCarlo {
        1
    } else {
        0
    };

    if job_mode == 1 {
        if app.job.sample_target > 0 {
            let percent = ((app.job.samples_completed as f64 / app.job.sample_target as f64) * 100.0)
                .clamp(0.0, 100.0) as u16;
            let label = format!(
                "{} / {}",
                format::format_count(app.job.samples_completed),
                format::format_count(app.job.sample_target)
            );
            (percent, label)
        } else {
            (0, "等待蒙特卡洛任务".to_string())
        }
    } else if app.job.total_steps > 0 {
        let percent = ((app.job.completed_steps as f64 / app.job.total_steps as f64) * 100.0)
            .clamp(0.0, 100.0) as u16;
        let label = format!("{}/{}", app.job.completed_steps, app.job.total_steps);
        (percent, label)
    } else {
        (0, "等待精确位数任务".to_string())
    }
}

fn render_system(frame: &mut Frame, area: Rect, app: &App) {
    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(Color::Red))
        .title(Span::styled(" 系统资源 ", Style::default().fg(Color::Red)));
    let inner = block.inner(area);
    let columns = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage(34),
            Constraint::Percentage(33),
            Constraint::Percentage(33),
        ])
        .split(inner);
    render_cpu(frame, columns[0], app);
    render_memory(frame, columns[1], app);
    render_gpu(frame, columns[2], app);
}

fn render_cpu(frame: &mut Frame, area: Rect, app: &App) {
    let available = app.resources.cpu_available != 0;
    let value = if available { app.resources.cpu_percent.clamp(0.0, 100.0) } else { 0.0 };
    let label = if available {
        format!(" CPU {value:.1}% ")
    } else {
        " CPU N/A ".to_string()
    };
    let gauge = Gauge::default()
        .gauge_style(Style::default().fg(Color::Blue))
        .percent(value as u16)
        .label(Span::styled(label, Style::default().fg(Color::White)));
    frame.render_widget(gauge, area);
}

fn render_memory(frame: &mut Frame, area: Rect, app: &App) {
    let total = app.resources.memory_total_bytes;
    let used = app.resources.memory_used_bytes;
    let percent = if total > 0 {
        ((used as f64 / total as f64) * 100.0).clamp(0.0, 100.0) as u16
    } else {
        0
    };
    let label = format!(
        " 内存 {}/{} ",
        format::format_bytes(used),
        format::format_bytes(total)
    );
    let gauge = Gauge::default()
        .gauge_style(Style::default().fg(Color::Green))
        .percent(percent)
        .label(Span::styled(label, Style::default().fg(Color::White)));
    frame.render_widget(gauge, area);
}

fn render_gpu(frame: &mut Frame, area: Rect, app: &App) {
    let available = app.resources.gpu_details_available != 0;
    let value = if available {
        app.resources.gpu_utilization_percent.min(100)
    } else {
        0
    };
    let label = if available {
        format!(
            " GPU {value}%  {}°C ",
            app.resources.gpu_temperature_celsius
        )
    } else {
        " GPU N/A ".to_string()
    };
    let gauge = Gauge::default()
        .gauge_style(Style::default().fg(Color::Magenta))
        .percent(value)
        .label(Span::styled(label, Style::default().fg(Color::White)));
    frame.render_widget(gauge, area);
}

fn render_footer(frame: &mut Frame, area: Rect) {
    let line = Line::from(vec![
        Span::styled(" s 开始 ", Style::default().fg(Color::Cyan)),
        Span::raw("  "),
        Span::styled(" p 暂停 ", Style::default().fg(Color::Yellow)),
        Span::raw("  "),
        Span::styled(" c 取消 ", Style::default().fg(Color::Red)),
        Span::raw("  "),
        Span::styled(" m 切换模式 ", Style::default().fg(Color::Magenta)),
        Span::raw("  "),
        Span::styled(" +/- 调整 ", Style::default().fg(Color::Green)),
        Span::raw("  "),
        Span::styled(" 上下键 设备 ", Style::default().fg(Color::Blue)),
        Span::raw("  "),
        Span::styled(" 翻页/[] 滚动 ", Style::default().fg(Color::Cyan)),
        Span::raw("  "),
        Span::styled(" q 退出 ", Style::default().fg(Color::Red)),
    ]);
    frame.render_widget(
        Paragraph::new(Text::from(line)).alignment(Alignment::Center),
        area,
    );
}

fn state_color(state: JobState) -> Color {
    match state {
        JobState::Running => Color::Cyan,
        JobState::Paused => Color::Yellow,
        JobState::Finished => Color::Green,
        JobState::Failed => Color::Red,
        JobState::Cancelled => Color::Red,
        JobState::Preparing => Color::Blue,
        JobState::Cancelling => Color::Yellow,
        JobState::GpuUnavailable => Color::DarkGray,
        JobState::Idle => Color::Gray,
    }
}
