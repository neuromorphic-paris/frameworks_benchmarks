const fs = require('fs');
const child_process = require('child_process');
const path = require('path');

const delta = (a, b) => Number(BigInt(b) - BigInt(a));

const pipeline_to_experiment_to_parameters = {
    mask: {
        duration: {
            configuration: 'mask.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                duration: delta(reader, sink[0]),
                hashes: {
                    events: sink[1],
                    increases: sink[2],
                    t_hash: sink[3],
                    x_hash: sink[4],
                    y_hash: sink[5],
                },
            }),
        },
        latencies: {
            configuration: 'mask_latencies.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                hashes: {
                    events: sink[0],
                    increases: sink[1],
                    t_hash: sink[2],
                    x_hash: sink[3],
                    y_hash: sink[4],
                },
                points: sink[5].map(([t, time]) => [t, delta(reader, time)]),
            }),
        },
    },
    flow: {
        duration: {
            configuration: 'flow.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                duration: delta(reader, sink[0]),
                hashes: {
                    events: sink[1],
                    t_hash: sink[2],
                    vx_hash: sink[3],
                    vy_hash: sink[4],
                    x_hash: sink[5],
                    y_hash: sink[6],
                },
            }),
        },
        latencies: {
            configuration: 'flow_latencies.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                hashes: {
                    events: sink[0],
                    t_hash: sink[1],
                    vx_hash: sink[2],
                    vy_hash: sink[3],
                    x_hash: sink[4],
                    y_hash: sink[5],
                },
                points: sink[6].map(([t, time]) => [t, delta(reader, time)]),
            }),
        },
    },
    denoised_flow: {
        duration: {
            configuration: 'denoised_flow.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                duration: delta(reader, sink[0]),
                hashes: {
                    events: sink[1],
                    t_hash: sink[2],
                    vx_hash: sink[3],
                    vy_hash: sink[4],
                    x_hash: sink[5],
                    y_hash: sink[6],
                },
            }),
        },
        latencies: {
            configuration: 'denoised_flow_latencies.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                hashes: {
                    events: sink[0],
                    t_hash: sink[1],
                    vx_hash: sink[2],
                    vy_hash: sink[3],
                    x_hash: sink[4],
                    y_hash: sink[5],
                },
                points: sink[6].map(([t, time]) => [t, delta(reader, time)]),
            }),
        },
    },
    masked_denoised_flow: {
        duration: {
            configuration: 'masked_denoised_flow.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                duration: delta(reader, sink[0]),
                hashes: {
                    events: sink[1],
                    t_hash: sink[2],
                    vx_hash: sink[3],
                    vy_hash: sink[4],
                    x_hash: sink[5],
                    y_hash: sink[6],
                },
            }),
        },
        latencies: {
            configuration: 'masked_denoised_flow_latencies.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                hashes: {
                    events: sink[0],
                    t_hash: sink[1],
                    vx_hash: sink[2],
                    vy_hash: sink[3],
                    x_hash: sink[4],
                    y_hash: sink[5],
                },
                points: sink[6].map(([t, time]) => [t, delta(reader, time)]),
            }),
        },
    },
    masked_denoised_flow_activity: {
        duration: {
            configuration: 'masked_denoised_flow_activity.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                duration: delta(reader, sink[0]),
                hashes: {
                    events: sink[1],
                    t_hash: sink[2],
                    potential_hash: sink[3],
                    x_hash: sink[4],
                    y_hash: sink[5],
                },
            }),
        },
        latencies: {
            configuration: 'masked_denoised_flow_activity_latencies.xml',
            reader_and_sink_to_json: (reader, sink) => JSON.stringify({
                hashes: {
                    events: sink[0],
                    t_hash: sink[1],
                    potential_hash: sink[2],
                    x_hash: sink[3],
                    y_hash: sink[4],
                },
                points: sink[5].map(([t, time]) => [t, delta(reader, time)]),
            }),
        },
    },
};

const template = (input, parameters, output) => {
    let content = fs.readFileSync(input, 'utf-8');
    for (const [key, value] of Object.entries(parameters)) {
        content = content.replace(new RegExp(`@${key}`, 'g'), value);
    }
    fs.writeFileSync(output, content);
}
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
template(
    `${__dirname}/configurations/${parameters.configuration}`,
    {
        log: `${__dirname}/temporary/caer.log`,
        modules: `${__dirname}/usr/share/caer/modules/`,
        filename: path.resolve(process.argv[4]),
        reader_output: `${__dirname}/temporary/reader.json`,
        sink_output: `${__dirname}/temporary/sink.json`
    },
    `${__dirname}/temporary/configuration.xml`);
try {
    fs.unlinkSync(`${__dirname}/temporary/reader.json`);
    fs.unlinkSync(`${__dirname}/temporary/sink.json`);
} catch {}
child_process.execSync(`${__dirname}/usr/bin/caer-bin -c ${__dirname}/temporary/configuration.xml`, {stdio : 'pipe'});
process.stdout.write(parameters.reader_and_sink_to_json(
    JSON.parse(fs.readFileSync(`${__dirname}/temporary/reader.json`).toString()),
    JSON.parse(fs.readFileSync(`${__dirname}/temporary/sink.json`).toString())) + '\n');
