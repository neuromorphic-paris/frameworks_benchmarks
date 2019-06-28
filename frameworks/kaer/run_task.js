const child_process = require('child_process');

const pipeline_to_experiment_to_parameters = {
    mask: {
        duration: {
            name: 'mask',
            result_to_json: result => JSON.stringify({
                duration: result[0],
                hashes: {
                    events: result[1],
                    increases: result[2],
                    t_hash: result[3],
                    x_hash: result[4],
                    y_hash: result[5],
                },
            }),
        },
        latencies: {
            name: 'mask_latencies',
            result_to_json: result => JSON.stringify({
                hashes: {
                    events: result[0],
                    increases: result[1],
                    t_hash: result[2],
                    x_hash: result[3],
                    y_hash: result[4],
                },
                points: result[5].map(([t, time]) => [t, Number(BigInt(time))]),
            }),
        },
    },
    flow: {
        duration: {
            name: 'flow',
            result_to_json: result => JSON.stringify({
                duration: result[0],
                hashes: {
                    events: result[1],
                    t_hash: result[2],
                    vx_hash: result[3],
                    vy_hash: result[4],
                    x_hash: result[5],
                    y_hash: result[6],
                },
            }),
        },
        latencies: {
            name: 'flow_latencies',
            result_to_json: result => JSON.stringify({
                hashes: {
                    events: result[0],
                    t_hash: result[1],
                    vx_hash: result[2],
                    vy_hash: result[3],
                    x_hash: result[4],
                    y_hash: result[5],
                },
                points: result[6].map(([t, time]) => [t, Number(BigInt(time))]),
            }),
        },
    },
    denoised_flow: {
        duration: {
            name: 'denoised_flow',
            result_to_json: result => JSON.stringify({
                duration: result[0],
                hashes: {
                    events: result[1],
                    t_hash: result[2],
                    vx_hash: result[3],
                    vy_hash: result[4],
                    x_hash: result[5],
                    y_hash: result[6],
                },
            }),
        },
        latencies: {
            name: 'denoised_flow_latencies',
            result_to_json: result => JSON.stringify({
                hashes: {
                    events: result[0],
                    t_hash: result[1],
                    vx_hash: result[2],
                    vy_hash: result[3],
                    x_hash: result[4],
                    y_hash: result[5],
                },
                points: result[6].map(([t, time]) => [t, Number(BigInt(time))]),
            }),
        },
    },
    masked_denoised_flow: {
        duration: {
            name: 'masked_denoised_flow',
            result_to_json: result => JSON.stringify({
                duration: result[0],
                hashes: {
                    events: result[1],
                    t_hash: result[2],
                    vx_hash: result[3],
                    vy_hash: result[4],
                    x_hash: result[5],
                    y_hash: result[6],
                },
            }),
        },
        latencies: {
            name: 'masked_denoised_flow_latencies',
            result_to_json: result => JSON.stringify({
                hashes: {
                    events: result[0],
                    t_hash: result[1],
                    vx_hash: result[2],
                    vy_hash: result[3],
                    x_hash: result[4],
                    y_hash: result[5],
                },
                points: result[6].map(([t, time]) => [t, Number(BigInt(time))]),
            }),
        },
    },
    masked_denoised_flow_activity: {
        duration: {
            name: 'masked_denoised_flow_activity',
            result_to_json: result => JSON.stringify({
                duration: result[0],
                hashes: {
                    events: result[1],
                    t_hash: result[2],
                    potential_hash: result[3],
                    x_hash: result[4],
                    y_hash: result[5],
                },
            }),
        },
        latencies: {
            name: 'masked_denoised_flow_activity_latencies',
            result_to_json: result => JSON.stringify({
                hashes: {
                    events: result[0],
                    t_hash: result[1],
                    potential_hash: result[2],
                    x_hash: result[3],
                    y_hash: result[4],
                },
                points: result[5].map(([t, time]) => [t, Number(BigInt(time))]),
            }),
        },
    },
};

if (process.argv.length != 5) {
    console.error('3 arguments are expected (a pipeline name, an experiment name and an Event Stream filename)');
    process.exit(1);
}
const experiment_to_parameters = pipeline_to_experiment_to_parameters[process.argv[2]];
if (experiment_to_parameters == null) {
    console.error(`unknown pipeline ${process.argv[2]}`);
    process.exit(1);
}
const parameters = experiment_to_parameters[process.argv[3]];
if (parameters == null) {
    console.error(`unknown experiment ${process.argv[3]}`);
    process.exit(1);
}
process.stdout.write(parameters.result_to_json(JSON.parse(child_process.execSync(
    `${__dirname}/build/${parameters.name} ${process.argv[4]}`,
    {maxBuffer: 2 ** 30}))) + '\n');
