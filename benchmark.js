const frameworks = ['caer', 'kaer', 'tarsier', 'yarp'];
const pipelines = ['mask', 'flow', 'denoised_flow', 'masked_denoised_flow', 'masked_denoised_flow_activity'];
const experiments_and_repetitions = [['duration', 100], ['latencies', 10]];
const streams = ['squares', 'street', 'car'];

const child_process = require('child_process');
const fs = require('fs');
console.log(new Date());

/// job_to_hashes contains the expected hash values for each job (pipeline + experiment + stream).
const job_to_hashes = new Map();

/// are_equal does a shallow object comparison.
const are_equal = (a, b) => (Object.keys(a).length === Object.keys(b).length
    && Object.keys(a).every(key => b.hasOwnProperty(key) && a[key] === b[key]));

/// hashes_to_string convert hashes to a pretty-printed string.
const hashes_to_string = (hashes, indent = 1) => Object.entries(hashes).map(([key, value]) => `${' '.repeat(4 * indent)}${key}: ${value}`).join('\n');

/// run executes the given task.
const run = task => child_process.execSync(
    `node --max-old-space-size=16384 ${__dirname}/frameworks/${task.framework}/run_task.js ${task.pipeline} ${task.experiment} media/${task.stream}.es`,
    {maxBuffer: 2 ** 30});

// fill and shuffle a list of all tasks (job + framework, repeated).
const tasks = [];
for (const pipeline of pipelines) {
    for (const [experiment, repetitions] of experiments_and_repetitions) {
        for (const stream of streams) {
            const job = `${pipeline}::${experiment}::${stream}`;
            console.log(job);
            let valid = true;
            const framework_to_hashes = [];
            for (let index = 0; index < frameworks.length; ++index) {
                framework_to_hashes[index] = JSON.parse(run({framework: frameworks[index], pipeline, experiment, stream})).hashes;
                global.gc();
                if (index > 0 && !are_equal(framework_to_hashes[0], framework_to_hashes[index])) {
                    valid = false;
                }
            }
            if (!valid) {
                console.error(`the frameworks returned non-identical hashes for ${job}`);
                for (let index = 0; index < frameworks.length; ++index) {
                    console.error(`    ${frameworks[index]}:\n${hashes_to_string(framework_to_hashes[index], 2)}`);
                }
                process.exit(1);
            }
            console.log(hashes_to_string(framework_to_hashes[0]));
            for (const framework of frameworks) {
                for (let index = 0; index < repetitions; ++index) {
                    tasks.push({name: `${job}::${framework}::${index}`, job, framework, pipeline, experiment, stream});
                }
            }
        }
    }
}
for (let index = tasks.length - 1; index > 0; --index) {
    const other_index = Math.floor(Math.random() * (index + 1));
    [tasks[index], tasks[other_index]] = [tasks[other_index], tasks[index]];
}

// run the tasks sequentially
for (const [index, task] of tasks.entries()) {
    console.log(`${index + 1} / ${tasks.length} ${task.name} ${new Date()}`);
    fs.writeFileSync(`${__dirname}/results/${task.name}.json`, run(task));
    global.gc();
}

console.log(new Date());
