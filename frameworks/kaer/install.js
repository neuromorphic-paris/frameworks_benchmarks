const child_process = require('child_process');
const fs = require('fs');

console.log('\n\x1b[34mkaer\n————\x1b[0m');
child_process.execSync(`mkdir -p build && cd build && cmake .. && make`, {cwd: __dirname, stdio: 'inherit'});
