const std = @import("std");

const curl_src = @import("libs/curl.zig");
const zlib_src = @import("libs/zlib.zig");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const target_os = target.result.os.tag;

    // Create root module for au2cat
    const root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    root_module.addCSourceFiles(.{
        .files = &.{ "src/main.c", "src/miniz.c", "src/lang.c" },
        .flags = &.{
            "-std=c99",
            "-DWITH_GZFILEOP",
            "-Wno-unused-parameter",
            "-DCURL_STATICLIB",
            "-Wno-format-security",
        },
    });

    // Include project root for xxhash.h, miniz.h
    root_module.addIncludePath(b.path("src"));

    if (target_os == .windows) {
        // Cross-compile to Windows: build libcurl and zlib from source
        const libcurl = curl_src.create(b, target, optimize, null, false) orelse
            @panic("Failed to build libcurl");
        const libz = zlib_src.create(b, target, optimize, null) orelse
            @panic("Failed to build zlib");

        libcurl.root_module.linkLibrary(libz);
        root_module.linkLibrary(libcurl);
        
        root_module.linkSystemLibrary("ws2_32", .{});
        root_module.linkSystemLibrary("bcrypt", .{});
        root_module.linkSystemLibrary("crypt32", .{}); // Needed for Schannel
        root_module.linkSystemLibrary("secur32", .{}); // Needed for SSPI
    } else {
        // Native build (Linux/macOS): use system libcurl
        root_module.linkSystemLibrary("curl", .{});
    }

    const exe = b.addExecutable(.{
        .name = "au2cat",
        .root_module = root_module,
    });

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "au2cat を実行");
    run_step.dependOn(&run_cmd.step);

    const do_install = b.option(bool, "install", "Install to /usr/local/bin") orelse false;
    if (do_install) {
        const install_cmd = b.addSystemCommand(&.{ "install", "-m", "755" });
        install_cmd.addFileArg(exe.getEmittedBin());
        install_cmd.addArg("/usr/local/bin/au2cat");
        b.getInstallStep().dependOn(&install_cmd.step);
    }
}
