const host = 'localhost'; // '[login]@[server / IP address]' or 'localhost'
const path = ''; // ignored if host is 'localhost'
const frameworks = [
    'caer',
    'kaer',
    'tarsier',
    'yarp',
];

const child_process = require('child_process');
if (host == 'localhost') {
    for (const framework of frameworks) {
        child_process.execSync('node install.js', {cwd: `${__dirname}/frameworks/${framework}`, stdio: 'inherit'});
    }
} else {
    child_process.execSync(`rsync -avz --exclude=.DS_Store --exclude=results ${__dirname}/ ${host}:${path}/`, {stdio: 'inherit'});
    child_process.execSync(`ssh ${host} "cd ${path}; mkdir -p results"`, {stdio: 'inherit'});
    if (process.argv.length == 2 || !process.argv.slice(2).includes('do-not-compile')) {
        child_process.execSync(`ssh ${host} "cd ${path}/common && premake4 gmake && cd build && make"`, {stdio: 'inherit'});
        for (const framework of frameworks) {
            child_process.execSync(`ssh ${host} "cd ${path}/frameworks/${framework} && node install.js"`, {stdio: 'inherit'});
        }
    }
}
