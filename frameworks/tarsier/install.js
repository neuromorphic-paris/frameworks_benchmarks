const child_process = require('child_process');
const fs = require('fs');

console.log('\n\x1b[34mtarsier\n———————\x1b[0m');
child_process.execSync(`premake4 gmake && cd build && make`, {cwd: __dirname, stdio: 'inherit'});
