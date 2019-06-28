function benchmark_project(name)
    project(name)
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {'source/benchmark.hpp', 'source/' .. name .. '.cpp'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
        configuration 'linux'
            links {'pthread'}
            buildoptions {'-std=c++11'}
            linkoptions {'-std=c++11'}
        configuration 'macosx'
            buildoptions {'-std=c++11'}
            linkoptions {'-std=c++11'}
        configuration 'windows'
            files {'.clang-format'}
end

solution 'benchmark'
    configurations {'release', 'debug'}
    location 'build'
    benchmark_project 'mask'
    benchmark_project 'mask_latencies'
    benchmark_project 'flow'
    benchmark_project 'flow_latencies'
    benchmark_project 'denoised_flow'
    benchmark_project 'denoised_flow_latencies'
    benchmark_project 'masked_denoised_flow'
    benchmark_project 'masked_denoised_flow_latencies'
    benchmark_project 'masked_denoised_flow_activity'
    benchmark_project 'masked_denoised_flow_activity_latencies'
