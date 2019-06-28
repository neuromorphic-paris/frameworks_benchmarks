const child_process = require('child_process');

const pipelines = ['mask', 'flow', 'denoised_flow', 'masked_denoised_flow', 'masked_denoised_flow_activity'];
const streams = ['street', 'squares', 'car'];

for (let index = 0; ; ++index) {
    const begin = new Date();
    const pipeline = pipelines[Math.floor(Math.random() * pipelines.length)];
    const stream = streams[Math.floor(Math.random() * streams.length)];
    console.log(`\n${index}: ${pipeline} ${stream}`);
    child_process.execSync(
        `${__dirname}/usr/bin/${pipeline} /home/idv/benchmark/media/${stream}.es ${__dirname}/temporary/output.json`,
        {stdio: 'inherit', maxBuffer: 2 ** 30});
    console.log(`${(new Date().getTime() - begin.getTime()) / 1000} s`);
}
