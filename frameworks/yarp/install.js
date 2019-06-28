const child_process = require('child_process');
const fs = require('fs');

process.env.PKG_CONFIG_PATH = `${__dirname}/usr/lib/pkgconfig:${process.env.PKG_CONFIG_PATH}`;
child_process.execSync(`mkdir -p temporary`, {cwd: __dirname, stdio: 'inherit'});

console.log('\n\x1b[34myarp\n————\x1b[0m');
child_process.execSync(
    `mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=${__dirname}/usr .. && make install`,
    {cwd: `${__dirname}/yarp`, stdio: 'inherit'});

console.log('\n\x1b[34micub-contrib-common@yarp\n————————————————————————\x1b[0m');
child_process.execSync(
    `mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=${__dirname}/usr .. && make install`,
    {cwd: `${__dirname}/icub-contrib-common`, stdio: 'inherit'});

console.log('\n\x1b[34mevent-driven@yarp\n————————————\x1b[0m');
child_process.execSync(
    `mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=${__dirname}/usr .. && make install`,
    {cwd: `${__dirname}/event-driven`, stdio: 'inherit'});
