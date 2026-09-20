use std::{env, fs, path::PathBuf, process::Command};

fn main() {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR is not set"));
    let glslc = env::var_os("GLSLC").unwrap_or_else(|| "glslc".into());
    let package_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR is not set"));
    let shader_dir = [package_dir.join("../shaders"), package_dir.join("../../shaders")]
        .into_iter()
        .find(|path| path.exists())
        .expect("could not locate the shared shader directory");

    for shader in ["video.vert", "video.frag"] {
        let source = shader_dir.join(shader);
        println!("cargo:rerun-if-changed={}", source.display());
        let output = out_dir.join(format!("{shader}.spv"));
        let result = Command::new(&glslc)
            .arg(&source)
            .arg("-o")
            .arg(&output)
            .status()
            .unwrap_or_else(|error| panic!("Could not execute glslc: {error}"));
        if !result.success() {
            panic!("glslc failed while compiling {}", source.display());
        }
        if !output.exists() {
            panic!("glslc did not produce {}", output.display());
        }
    }

    let _ = fs::metadata(out_dir.join("video.vert.spv"));
}
