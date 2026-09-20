use anyhow::{anyhow, Context, Result};
use clap::Parser;
use ffmpeg::{codec, format, frame, media, software};
use ffmpeg_next as ffmpeg;
use sdl3::{
    event::Event,
    gpu::{
        ColorTargetDescription, ColorTargetInfo, Device, FillMode, Filter,
        GraphicsPipelineTargetInfo, LoadOp, PrimitiveType, RasterizerState, SampleCount,
        SamplerAddressMode, SamplerCreateInfo, SamplerMipmapMode, ShaderFormat, ShaderStage,
        StoreOp, Texture, TextureCreateInfo, TextureFormat, TextureRegion, TextureSamplerBinding,
        TextureTransferInfo, TextureType, TextureUsage, TransferBuffer, TransferBufferUsage,
    },
    keyboard::Keycode,
    pixels::Color,
};
use serde::Serialize;
use std::{
    fs,
    path::{Path, PathBuf},
    time::{Duration, Instant},
};

const VIDEO_VERTEX_SHADER: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/video.vert.spv"));
const VIDEO_FRAGMENT_SHADER: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/video.frag.spv"));

#[derive(Parser, Debug)]
#[command(name = "creative-suite-rust-prototype")]
#[command(about = "Minimal comparative Rust video and GPU preview prototype")]
struct Options {
    #[arg(long)]
    input: PathBuf,
    #[arg(long)]
    benchmark: bool,
    #[arg(long, default_value_t = 300)]
    frames: usize,
    #[arg(long)]
    effect: Option<String>,
    #[arg(long)]
    output: Option<PathBuf>,
}

#[derive(Clone)]
struct VideoFrame {
    width: u32,
    height: u32,
    index: i64,
    rgba: Vec<u8>,
}

struct VideoDecoder {
    input: format::context::Input,
    decoder: ffmpeg::decoder::Video,
    scaler: software::scaling::context::Context,
    decoded: frame::Video,
    rgba: frame::Video,
    stream_index: usize,
    fps: f64,
    total_frames: i64,
    next_index: i64,
    flushing: bool,
}

impl VideoDecoder {
    fn open(path: &Path) -> Result<Self> {
        ffmpeg::init().context("initializing FFmpeg")?;
        let input =
            format::input(path).with_context(|| format!("opening input {}", path.display()))?;
        let stream = input
            .streams()
            .best(media::Type::Video)
            .ok_or_else(|| anyhow!("no video stream found"))?;
        let stream_index = stream.index();
        let time_base = stream.time_base();
        let context = codec::context::Context::from_parameters(stream.parameters())
            .context("creating decoder context")?;
        let decoder = context.decoder().video().context("opening video decoder")?;
        let rate = stream.avg_frame_rate();
        let fallback_rate = stream.rate();
        let rate = if rate.0 > 0 && rate.1 > 0 {
            rate
        } else {
            fallback_rate
        };
        let fps = if rate.0 > 0 && rate.1 > 0 {
            f64::from(rate.0) / f64::from(rate.1)
        } else {
            30.0
        };
        let total_frames = if stream.frames() > 0 {
            stream.frames()
        } else if stream.duration() > 0 && time_base.0 != 0 {
            ((stream.duration() as f64 * f64::from(time_base.0) / f64::from(time_base.1)) * fps)
                as i64
        } else {
            0
        };
        let scaler = software::scaling::context::Context::get(
            decoder.format(),
            decoder.width(),
            decoder.height(),
            format::Pixel::RGBA,
            decoder.width(),
            decoder.height(),
            software::scaling::flag::Flags::BILINEAR,
        )
        .context("creating RGBA conversion context")?;
        let rgba = frame::Video::new(format::Pixel::RGBA, decoder.width(), decoder.height());

        Ok(Self {
            input,
            decoder,
            scaler,
            decoded: frame::Video::empty(),
            rgba,
            stream_index,
            fps,
            total_frames,
            next_index: 0,
            flushing: false,
        })
    }

    fn read(&mut self) -> Result<Option<VideoFrame>> {
        loop {
            match self.decoder.receive_frame(&mut self.decoded) {
                Ok(()) => {
                    self.scaler
                        .run(&self.decoded, &mut self.rgba)
                        .context("converting decoded frame to RGBA")?;
                    let width = self.rgba.width();
                    let height = self.rgba.height();
                    let stride = self.rgba.stride(0);
                    let row_size = width as usize * 4;
                    let mut rgba = vec![0_u8; row_size * height as usize];
                    for row in 0..height as usize {
                        let source_start = row * stride;
                        let destination_start = row * row_size;
                        rgba[destination_start..destination_start + row_size].copy_from_slice(
                            &self.rgba.data(0)[source_start..source_start + row_size],
                        );
                    }
                    let result = VideoFrame {
                        width,
                        height,
                        index: self.next_index,
                        rgba,
                    };
                    self.next_index += 1;
                    return Ok(Some(result));
                }
                Err(ffmpeg::Error::Other { errno }) if errno == ffmpeg::error::EAGAIN => {}
                Err(ffmpeg::Error::Eof) => return Ok(None),
                Err(error) => return Err(anyhow!(error)).context("receiving decoded frame"),
            }

            if self.flushing {
                return Ok(None);
            }
            match self.input.packets().next() {
                Some((stream, packet)) => {
                    if stream.index() == self.stream_index {
                        self.decoder
                            .send_packet(&packet)
                            .context("sending packet to decoder")?;
                    }
                }
                None => {
                    self.decoder.send_eof().context("flushing decoder")?;
                    self.flushing = true;
                }
            }
        }
    }

    fn seek_frame(&mut self, target: i64) -> Result<Option<VideoFrame>> {
        let target = target.max(0);
        let timestamp = ((target as f64 / self.fps) * 1_000_000.0) as i64;
        self.input
            .seek(timestamp, ..timestamp)
            .context("seeking video")?;
        self.decoder.flush();
        self.flushing = false;
        self.next_index = 0;
        while let Some(frame) = self.read()? {
            if frame.index >= target {
                return Ok(Some(frame));
            }
        }
        Ok(None)
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RectData {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct EffectData {
    grayscale: u32,
    padding: [f32; 3],
    tint: [f32; 4],
}

struct GpuPreview {
    window: sdl3::video::Window,
    gpu: Device,
    video_texture: Texture<'static>,
    white_texture: Texture<'static>,
    transfer_buffer: TransferBuffer,
    sampler: sdl3::gpu::Sampler,
    pipeline: sdl3::gpu::GraphicsPipeline,
}

impl GpuPreview {
    fn new(video: &sdl3::VideoSubsystem, width: u32, height: u32, hidden: bool) -> Result<Self> {
        let mut builder = video.window("Creative Suite Prototype - Rust", 1280, 720);
        builder.position_centered().resizable();
        if hidden {
            builder.hidden();
        }
        let window = builder
            .build()
            .map_err(|error| anyhow!(error.to_string()))?;
        let gpu = Device::new(ShaderFormat::SPIRV, true)
            .context("creating SDL3 GPU device")?
            .with_window(&window)
            .context("claiming window for SDL3 GPU device")?;

        let vertex_shader = gpu
            .create_shader()
            .with_code(
                ShaderFormat::SPIRV,
                VIDEO_VERTEX_SHADER,
                ShaderStage::Vertex,
            )
            .with_uniform_buffers(1)
            .with_entrypoint(c"main")
            .build()
            .context("creating vertex shader")?;
        let fragment_shader = gpu
            .create_shader()
            .with_code(
                ShaderFormat::SPIRV,
                VIDEO_FRAGMENT_SHADER,
                ShaderStage::Fragment,
            )
            .with_samplers(1)
            .with_uniform_buffers(1)
            .with_entrypoint(c"main")
            .build()
            .context("creating fragment shader")?;
        let swapchain_format = gpu.get_swapchain_texture_format(&window);
        let pipeline = gpu
            .create_graphics_pipeline()
            .with_primitive_type(PrimitiveType::TriangleStrip)
            .with_vertex_shader(&vertex_shader)
            .with_fragment_shader(&fragment_shader)
            .with_rasterizer_state(
                RasterizerState::new()
                    .with_fill_mode(FillMode::Fill)
                    .with_cull_mode(sdl3::gpu::CullMode::None),
            )
            .with_target_info(
                GraphicsPipelineTargetInfo::new().with_color_target_descriptions(&[
                    ColorTargetDescription::new().with_format(swapchain_format),
                ]),
            )
            .build()
            .context("creating graphics pipeline")?;

        let texture_info = |texture_width, texture_height| {
            TextureCreateInfo::new()
                .with_type(TextureType::_2D)
                .with_format(TextureFormat::R8g8b8a8Unorm)
                .with_usage(TextureUsage::SAMPLER)
                .with_width(texture_width)
                .with_height(texture_height)
                .with_layer_count_or_depth(1)
                .with_num_levels(1)
                .with_sample_count(SampleCount::NoMultiSampling)
        };
        let video_texture = gpu
            .create_texture(texture_info(width, height))
            .context("creating video texture")?;
        let white_texture = gpu
            .create_texture(texture_info(1, 1))
            .context("creating timeline texture")?;
        let transfer_buffer = gpu
            .create_transfer_buffer()
            .with_size((width as usize * height as usize * 4) as u32)
            .with_usage(TransferBufferUsage::UPLOAD)
            .build()
            .context("creating GPU transfer buffer")?;
        let sampler = gpu
            .create_sampler(
                SamplerCreateInfo::new()
                    .with_min_filter(Filter::Linear)
                    .with_mag_filter(Filter::Linear)
                    .with_mipmap_mode(SamplerMipmapMode::Nearest)
                    .with_address_mode_u(SamplerAddressMode::ClampToEdge)
                    .with_address_mode_v(SamplerAddressMode::ClampToEdge)
                    .with_address_mode_w(SamplerAddressMode::ClampToEdge),
            )
            .context("creating GPU sampler")?;

        let result = Self {
            window,
            gpu,
            video_texture,
            white_texture,
            transfer_buffer,
            sampler,
            pipeline,
        };
        result.upload_texture(&[255, 255, 255, 255], 1, 1)?;
        drop(vertex_shader);
        drop(fragment_shader);
        Ok(result)
    }

    fn upload_texture(&self, bytes: &[u8], width: u32, height: u32) -> Result<()> {
        let mut mapped = self.transfer_buffer.map::<u8>(&self.gpu, true);
        mapped.mem_mut()[..bytes.len()].copy_from_slice(bytes);
        mapped.unmap();

        let command = self
            .gpu
            .acquire_command_buffer()
            .context("acquiring upload command buffer")?;
        let copy_pass = self
            .gpu
            .begin_copy_pass(&command)
            .context("beginning upload copy pass")?;
        let texture = if width == 1 && height == 1 {
            &self.white_texture
        } else {
            &self.video_texture
        };
        copy_pass.upload_to_gpu_texture(
            TextureTransferInfo::new()
                .with_transfer_buffer(&self.transfer_buffer)
                .with_pixels_per_row(width)
                .with_rows_per_layer(height),
            TextureRegion::new()
                .with_texture(texture)
                .with_width(width)
                .with_height(height)
                .with_depth(1),
            true,
        );
        self.gpu.end_copy_pass(copy_pass);
        command
            .submit()
            .context("submitting upload command buffer")?;
        let idle = unsafe { sdl3::sys::gpu::SDL_WaitForGPUIdle(self.gpu.raw()) };
        if !idle {
            return Err(anyhow!("waiting for GPU upload failed"));
        }
        Ok(())
    }

    fn draw_texture(
        &self,
        command: &sdl3::gpu::CommandBuffer,
        pass: &sdl3::gpu::RenderPass,
        texture: &Texture<'static>,
        rect: RectData,
        grayscale: bool,
        tint: [f32; 4],
    ) {
        command.push_vertex_uniform_data(0, &rect);
        let effect = EffectData {
            grayscale: u32::from(grayscale),
            padding: [0.0; 3],
            tint,
        };
        command.push_fragment_uniform_data(0, &effect);
        let binding = TextureSamplerBinding::new()
            .with_texture(texture)
            .with_sampler(&self.sampler);
        pass.bind_fragment_samplers(0, &[binding]);
        pass.draw_primitives(4, 1, 0, 0);
    }

    fn render(&self, frame: &VideoFrame, grayscale: bool, total_frames: i64) -> Result<()> {
        self.upload_texture(&frame.rgba, frame.width, frame.height)?;
        let mut command = self
            .gpu
            .acquire_command_buffer()
            .context("acquiring render command buffer")?;
        let swapchain = match command
            .wait_and_acquire_swapchain_texture(&self.window)
            .context("acquiring swapchain texture")?
        {
            Some(texture) => texture,
            None => {
                command.cancel();
                return Ok(());
            }
        };
        let color_target = ColorTargetInfo::default()
            .with_texture(&swapchain)
            .with_load_op(LoadOp::CLEAR)
            .with_store_op(StoreOp::STORE)
            .with_clear_color(Color::RGB(8, 8, 8));
        let pass = self
            .gpu
            .begin_render_pass(&command, &[color_target], None)
            .context("beginning render pass")?;
        pass.bind_graphics_pipeline(&self.pipeline);
        self.draw_texture(
            &command,
            &pass,
            &self.video_texture,
            RectData {
                x: 0.0,
                y: 0.18,
                width: 1.0,
                height: 0.82,
            },
            grayscale,
            [1.0, 1.0, 1.0, 1.0],
        );
        self.draw_texture(
            &command,
            &pass,
            &self.white_texture,
            RectData {
                x: 0.0,
                y: 0.0,
                width: 1.0,
                height: 0.12,
            },
            false,
            [0.12, 0.12, 0.14, 1.0],
        );
        let progress = if total_frames > 0 {
            (frame.index as f32 / total_frames as f32).clamp(0.0, 1.0)
        } else {
            0.0
        };
        self.draw_texture(
            &command,
            &pass,
            &self.white_texture,
            RectData {
                x: 0.0,
                y: 0.0,
                width: progress,
                height: 0.12,
            },
            false,
            [0.25, 0.65, 0.95, 1.0],
        );
        self.gpu.end_render_pass(pass);
        command
            .submit()
            .context("submitting render command buffer")?;
        Ok(())
    }

    fn timeline_frame(&self, x: f32, total_frames: i64) -> i64 {
        let (width, _) = self.window.size();
        if total_frames <= 0 || width == 0 {
            return 0;
        }
        let normalized = (x / width as f32).clamp(0.0, 1.0);
        (normalized * (total_frames - 1) as f32) as i64
    }
}

#[derive(Serialize)]
struct Report {
    language: &'static str,
    prototype_version: &'static str,
    platform: &'static str,
    width: u32,
    height: u32,
    fps: f64,
    requested_frames: usize,
    decoded_frames: usize,
    time_to_first_frame_ms: f64,
    decode_ms: f64,
    decode_fps: f64,
    preview_ms: f64,
    preview_fps: f64,
    peak_memory_bytes: u64,
    error: String,
    errors: Vec<String>,
}

fn platform() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else if cfg!(target_os = "linux") {
        "linux"
    } else {
        "other"
    }
}

fn memory_usage_bytes() -> u64 {
    #[cfg(target_os = "windows")]
    {
        use windows_sys::Win32::System::{
            ProcessStatus::{GetProcessMemoryInfo, PROCESS_MEMORY_COUNTERS},
            Threading::GetCurrentProcess,
        };
        let mut counters: PROCESS_MEMORY_COUNTERS = unsafe { std::mem::zeroed() };
        counters.cb = std::mem::size_of::<PROCESS_MEMORY_COUNTERS>() as u32;
        let success = unsafe {
            GetProcessMemoryInfo(
                GetCurrentProcess(),
                &mut counters,
                std::mem::size_of::<PROCESS_MEMORY_COUNTERS>() as u32,
            )
        };
        if success != 0 {
            return counters.WorkingSetSize as u64;
        }
    }
    #[cfg(unix)]
    {
        let mut usage = std::mem::MaybeUninit::<libc::rusage>::zeroed();
        if unsafe { libc::getrusage(libc::RUSAGE_SELF, usage.as_mut_ptr()) } == 0 {
            let usage = unsafe { usage.assume_init() };
            #[cfg(target_os = "macos")]
            return usage.ru_maxrss as u64;
            #[cfg(not(target_os = "macos"))]
            return usage.ru_maxrss as u64 * 1024;
        }
    }
    0
}

fn write_report(
    options: &Options,
    decoder: &VideoDecoder,
    decoded_frames: usize,
    first_frame_ms: f64,
    decode_ms: f64,
    preview_ms: f64,
    peak_memory: u64,
    errors: Vec<String>,
) -> Result<()> {
    let report = Report {
        language: "rust",
        prototype_version: "0.1.0",
        platform: platform(),
        width: decoder.decoder.width(),
        height: decoder.decoder.height(),
        fps: decoder.fps,
        requested_frames: options.frames,
        decoded_frames,
        time_to_first_frame_ms: first_frame_ms,
        decode_ms,
        decode_fps: if decode_ms > 0.0 {
            decoded_frames as f64 * 1000.0 / decode_ms
        } else {
            0.0
        },
        preview_ms,
        preview_fps: if preview_ms > 0.0 {
            decoded_frames as f64 * 1000.0 / preview_ms
        } else {
            0.0
        },
        peak_memory_bytes: peak_memory,
        error: String::new(),
        errors,
    };
    let json = serde_json::to_string_pretty(&report)? + "\n";
    if let Some(path) = &options.output {
        fs::write(path, json).with_context(|| format!("writing report {}", path.display()))?;
    } else {
        print!("{json}");
    }
    Ok(())
}

fn run(options: Options) -> Result<()> {
    if options.frames == 0 {
        return Err(anyhow!("--frames must be greater than zero"));
    }
    if let Some(effect) = &options.effect {
        if effect != "grayscale" {
            return Err(anyhow!("only the grayscale effect is supported"));
        }
    }
    if !options.input.exists() {
        return Err(anyhow!(
            "input file does not exist: {}",
            options.input.display()
        ));
    }

    let start = Instant::now();
    let mut decoder = VideoDecoder::open(&options.input)?;
    let first_decode_start = Instant::now();
    let mut frame = decoder
        .read()?
        .ok_or_else(|| anyhow!("input contains no decodable video frames"))?;
    let first_decode_ms = first_decode_start.elapsed().as_secs_f64() * 1000.0;
    let first_frame_ms = start.elapsed().as_secs_f64() * 1000.0;

    let sdl = sdl3::init().context("initializing SDL3")?;
    let video = sdl.video().context("initializing SDL3 video")?;
    let preview = GpuPreview::new(&video, frame.width, frame.height, options.benchmark)?;
    let mut grayscale = options.effect.as_deref() == Some("grayscale");
    let mut peak_memory = memory_usage_bytes();

    if options.benchmark {
        let mut decoded_frames = 0usize;
        let mut decode_ms = first_decode_ms;
        let mut preview_ms = 0.0;
        loop {
            let preview_start = Instant::now();
            preview.render(&frame, grayscale, decoder.total_frames)?;
            preview_ms += preview_start.elapsed().as_secs_f64() * 1000.0;
            decoded_frames += 1;
            peak_memory = peak_memory.max(memory_usage_bytes());
            if decoded_frames >= options.frames {
                break;
            }
            let decode_start = Instant::now();
            let Some(next) = decoder.read()? else { break };
            decode_ms += decode_start.elapsed().as_secs_f64() * 1000.0;
            frame = next;
        }
        write_report(
            &options,
            &decoder,
            decoded_frames,
            first_frame_ms,
            decode_ms,
            preview_ms,
            peak_memory,
            Vec::new(),
        )?;
        return Ok(());
    }

    let mut event_pump = sdl.event_pump().context("creating SDL3 event pump")?;
    let mut playing = true;
    let mut running = true;
    let mut next_frame_at = Instant::now();
    while running {
        for event in event_pump.poll_iter() {
            match event {
                Event::Quit { .. } => running = false,
                Event::KeyDown {
                    keycode: Some(Keycode::Escape),
                    repeat: false,
                    ..
                } => running = false,
                Event::KeyDown {
                    keycode: Some(Keycode::Space),
                    repeat: false,
                    ..
                } => playing = !playing,
                Event::KeyDown {
                    keycode: Some(Keycode::G),
                    repeat: false,
                    ..
                } => grayscale = !grayscale,
                Event::KeyDown {
                    keycode: Some(Keycode::Left),
                    repeat: false,
                    ..
                } => {
                    if let Some(seeked) = decoder.seek_frame(frame.index - 1)? {
                        frame = seeked;
                    }
                }
                Event::KeyDown {
                    keycode: Some(Keycode::Right),
                    repeat: false,
                    ..
                } => {
                    if let Some(seeked) = decoder.seek_frame(frame.index + 1)? {
                        frame = seeked;
                    }
                }
                Event::MouseButtonDown { x, y, .. } if y <= 100.0 => {
                    if let Some(seeked) =
                        decoder.seek_frame(preview.timeline_frame(x, decoder.total_frames))?
                    {
                        frame = seeked;
                    }
                }
                _ => {}
            }
        }
        preview.render(&frame, grayscale, decoder.total_frames)?;
        if playing && Instant::now() >= next_frame_at {
            if let Some(next) = decoder.read()? {
                frame = next;
                next_frame_at = Instant::now() + Duration::from_secs_f64(1.0 / decoder.fps);
            } else {
                playing = false;
            }
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    Ok(())
}

fn main() {
    let options = Options::parse();
    if let Err(error) = run(options) {
        eprintln!("Prototype error: {error:#}");
        std::process::exit(1);
    }
}
