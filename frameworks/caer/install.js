const child_process = require('child_process');
const fs = require('fs');

process.env.PKG_CONFIG_PATH = `${__dirname}/usr/lib/pkgconfig:${process.env.PKG_CONFIG_PATH}`;
child_process.execSync(`mkdir -p temporary`, {cwd: __dirname, stdio: 'inherit'});

console.log('\n\x1b[34mlibcaer@caer\n————————————\x1b[0m');
child_process.execSync(
    `mkdir -p build && cd build && cmake -DUDEV_INSTALL=OFF -DCMAKE_INSTALL_PREFIX=${__dirname}/usr .. && make install`,
    {cwd: `${__dirname}/libcaer`, stdio: 'inherit'});

console.log('\n\x1b[34mcaer\n————\x1b[0m');
child_process.execSync(
    `mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=${__dirname}/usr .. && make install`,
    {cwd: `${__dirname}/caer`, stdio: 'inherit'});
