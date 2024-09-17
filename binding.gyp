{
  "targets": [
    {
      "target_name": "binding",
      "sources": [
        "binding.cc",
        "vendor/re2/re2/bitmap256.cc",
        "vendor/re2/re2/bitstate.cc",
        "vendor/re2/re2/compile.cc",
        "vendor/re2/re2/dfa.cc",
        "vendor/re2/re2/filtered_re2.cc",
        "vendor/re2/re2/mimics_pcre.cc",
        "vendor/re2/re2/nfa.cc",
        "vendor/re2/re2/onepass.cc",
        "vendor/re2/re2/parse.cc",
        "vendor/re2/re2/perl_groups.cc",
        "vendor/re2/re2/prefilter.cc",
        "vendor/re2/re2/prefilter_tree.cc",
        "vendor/re2/re2/prog.cc",
        "vendor/re2/re2/re2.cc",
        "vendor/re2/re2/regexp.cc",
        "vendor/re2/re2/set.cc",
        "vendor/re2/re2/simplify.cc",
        "vendor/re2/re2/tostring.cc",
        "vendor/re2/re2/unicode_casefold.cc",
        "vendor/re2/re2/unicode_groups.cc",
        "vendor/re2/util/pcre.cc",
        "vendor/re2/util/rune.cc",
        "vendor/re2/util/strutil.cc",
        "vendor/abseil-cpp/absl/base/internal/cycleclock.cc",
        "vendor/abseil-cpp/absl/base/internal/low_level_alloc.cc",
        "vendor/abseil-cpp/absl/base/internal/raw_logging.cc",
        "vendor/abseil-cpp/absl/base/internal/spinlock.cc",
        "vendor/abseil-cpp/absl/base/internal/spinlock_wait.cc",
        "vendor/abseil-cpp/absl/base/internal/strerror.cc",
        "vendor/abseil-cpp/absl/base/internal/sysinfo.cc",
        "vendor/abseil-cpp/absl/base/internal/thread_identity.cc",
        "vendor/abseil-cpp/absl/base/internal/throw_delegate.cc",
        "vendor/abseil-cpp/absl/base/internal/unscaledcycleclock.cc",
        "vendor/abseil-cpp/absl/debugging/internal/demangle.cc",
        "vendor/abseil-cpp/absl/container/internal/raw_hash_set.cc",
        "vendor/abseil-cpp/absl/debugging/internal/address_is_readable.cc",
        "vendor/abseil-cpp/absl/debugging/internal/elf_mem_image.cc",
        "vendor/abseil-cpp/absl/debugging/internal/examine_stack.cc",
        "vendor/abseil-cpp/absl/debugging/internal/vdso_support.cc",
        "vendor/abseil-cpp/absl/debugging/stacktrace.cc",
        "vendor/abseil-cpp/absl/debugging/symbolize.cc",
        "vendor/abseil-cpp/absl/flags/commandlineflag.cc",
        "vendor/abseil-cpp/absl/flags/internal/commandlineflag.cc",
        "vendor/abseil-cpp/absl/flags/internal/flag.cc",
        "vendor/abseil-cpp/absl/flags/internal/private_handle_accessor.cc",
        "vendor/abseil-cpp/absl/flags/internal/program_name.cc",
        "vendor/abseil-cpp/absl/flags/marshalling.cc",
        "vendor/abseil-cpp/absl/flags/reflection.cc",
        "vendor/abseil-cpp/absl/flags/usage_config.cc",
        "vendor/abseil-cpp/absl/hash/internal/city.cc",
        "vendor/abseil-cpp/absl/hash/internal/hash.cc",
        "vendor/abseil-cpp/absl/hash/internal/low_level_hash.cc",
        "vendor/abseil-cpp/absl/log/internal/globals.cc",
        "vendor/abseil-cpp/absl/log/internal/log_format.cc",
        "vendor/abseil-cpp/absl/log/internal/log_message.cc",
        "vendor/abseil-cpp/absl/log/internal/log_sink_set.cc",
        "vendor/abseil-cpp/absl/log/internal/nullguard.cc",
        "vendor/abseil-cpp/absl/log/internal/proto.cc",
        "vendor/abseil-cpp/absl/log/globals.cc",
        "vendor/abseil-cpp/absl/log/log_sink.cc",
        "vendor/abseil-cpp/absl/numeric/int128.cc",
        "vendor/abseil-cpp/absl/strings/ascii.cc",
        "vendor/abseil-cpp/absl/strings/charconv.cc",
        "vendor/abseil-cpp/absl/strings/internal/charconv_bigint.cc",
        "vendor/abseil-cpp/absl/strings/internal/charconv_parse.cc",
        "vendor/abseil-cpp/absl/strings/internal/memutil.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/arg.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/bind.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/extension.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/float_conversion.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/output.cc",
        "vendor/abseil-cpp/absl/strings/internal/str_format/parser.cc",
        "vendor/abseil-cpp/absl/strings/match.cc",
        "vendor/abseil-cpp/absl/strings/numbers.cc",
        "vendor/abseil-cpp/absl/strings/str_cat.cc",
        "vendor/abseil-cpp/absl/strings/str_split.cc",
        "vendor/abseil-cpp/absl/strings/string_view.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/create_thread_identity.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/graphcycles.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/futex_waiter.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/kernel_timeout.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/per_thread_sem.cc",
        "vendor/abseil-cpp/absl/synchronization/internal/waiter_base.cc",
        "vendor/abseil-cpp/absl/synchronization/mutex.cc",
        "vendor/abseil-cpp/absl/time/clock.cc",
        "vendor/abseil-cpp/absl/time/duration.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_fixed.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_if.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_impl.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_lookup.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/time_zone_posix.cc",
        "vendor/abseil-cpp/absl/time/internal/cctz/src/zone_info_source.cc",
        "vendor/abseil-cpp/absl/time/time.cc",
      ],
      "cflags": [
        "-fexceptions",
        "-std=c++2a",
        "-Wall",
        "-Wextra",
        "-Wno-sign-compare",
        "-Wno-unused-parameter",
        "-Wno-missing-field-initializers",
        "-Wno-cast-function-type",
        "-O3",
        "-march=znver1",
        "-g"
      ],
      'cflags_cc': [
        "-fexceptions",
        "-march=znver1",
      ],
      "defines": [
        "NDEBUG",
        "NOMINMAX"
      ],
      "include_dirs": [
        "<!(node -e \"require('napi-macros')\")",
        "vendor/re2",
        "vendor/abseil-cpp",
      ],
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "10.15",
        "CLANG_CXX_LANGUAGE_STANDARD": "c++2a",
        "CLANG_CXX_LIBRARY": "libc++",
        "OTHER_CFLAGS": [
          "-std=c++2a",
          "-fexceptions",
          "-Wall",
          "-Wextra",
          "-Wno-sign-compare",
          "-Wno-unused-parameter",
          "-Wno-missing-field-initializers",
          "-O3",
          "-g"
        ]
      },
      "conditions": [
        ["OS==\"linux\"", {
          "cflags": [
            "-pthread"
          ],
          "ldflags": [
            "-pthread"
          ]
        }],
      ]
    }
  ]
}
