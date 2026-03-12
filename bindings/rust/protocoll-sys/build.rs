use std::env;

fn main() {
    // Build the C++ library via CMake (always Release to match Rust's CRT)
    let mut cmake_cfg = cmake::Config::new("../../..");
    cmake_cfg
        .define("BUILD_TESTING", "OFF")
        .profile("Release");

    // On MSVC, ensure exception handling is enabled (cmake crate strips /EHsc)
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_env == "msvc" {
        cmake_cfg.cxxflag("/EHsc").cflag("/EHsc");
    }

    let dst = cmake_cfg.build();

    // Link the static library — search multiple possible output dirs
    for suffix in ["lib", "lib/Release", "lib/Debug", "Release", "Debug"] {
        println!("cargo:rustc-link-search=native={}/{}", dst.display(), suffix);
    }
    println!("cargo:rustc-link-lib=static=protocoll");
    println!("cargo:rustc-link-lib=static=monocypher_lib");

    // Platform-specific linking
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    match target_os.as_str() {
        "windows" => {
            println!("cargo:rustc-link-lib=ws2_32");
            println!("cargo:rustc-link-lib=bcrypt");
        }
        "linux" => {
            println!("cargo:rustc-link-lib=stdc++");
            println!("cargo:rustc-link-lib=pthread");
        }
        "macos" => {
            println!("cargo:rustc-link-lib=c++");
        }
        _ => {}
    }

    // Rerun if the C header changes
    println!("cargo:rerun-if-changed=../../../include/protocoll/protocoll.h");
}
